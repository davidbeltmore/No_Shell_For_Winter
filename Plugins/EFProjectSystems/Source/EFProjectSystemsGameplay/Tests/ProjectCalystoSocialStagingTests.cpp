#if WITH_DEV_AUTOMATION_TESTS

#include "Social/ProjectSocialSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace ProjectCalystoSocialStagingTests
{
	FGuid Id(uint32 N) { return FGuid(0x534F4349,0x414C5354,0,N); }
	FEFCalystoAttemptToken Token(uint32 N=1) { return {Id(100),Id(N)}; }
	FProjectSocialParticipantState State(FName Name)
	{
		FProjectSocialParticipantState Result; Result.ParticipantId=Name; Result.bVerifiedAdult=true;
		Result.bAlive=true; Result.bConscious=true; Result.bInSafeLocation=true; Result.bRecruitable=true;
		Result.bRecruitedCompanion=true; Result.bOffersPlayerInitiatedIntimacy=true; Result.Affinity=37; return Result;
	}
	struct FFixture
	{
		UWorld* World=nullptr;
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<UProjectSocialSubsystem> Social;
		FFixture()
		{
			if (!GEngine) return;
			const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
			if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Instance.Reset(NewObject<UGameInstance>()); Social.Reset(NewObject<UProjectSocialSubsystem>(Instance.Get()));
		}
		~FFixture()
		{
			if (Social.IsValid()) Social->Deinitialize();
			if (!World) return;
			// Actor destruction and world cleanup require this fixture's context to remain registered.
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			World=nullptr;
		}
		AActor* Spawn() const { return World?World->SpawnActor<AActor>():nullptr; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoSocialPublicationTest,
	"NoShellForWinter.CalystoDungeon.Director.SocialStaging.ExactUnpublishedIdentityAndAcceptance",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoSocialPublicationTest::RunTest(const FString&)
{
	using namespace ProjectCalystoSocialStagingTests;
	FFixture F; AActor* Existing=F.Spawn(); AActor* Selected=F.Spawn(); AActor* Other=F.Spawn();
	if (!Existing || !Selected || !Other || !F.Social.IsValid()) return false;
	const auto ExistingState=State(TEXT("Existing")); const auto SelectedState=State(TEXT("Selected")); FString Error;
	if (!TestTrue(TEXT("Ordinary participant exists before the attempt"),F.Social->RegisterOrUpdateParticipant(Existing,ExistingState))) return false;
	int32 Changed=0,Unregistered=0; bool bLockedDuringConfirmation=false;
	F.Social->OnNativeParticipantChanged().AddLambda([&](AActor* Actor)
	{
		++Changed;
		if (Changed==1 && Actor==Selected)
		{
			int32 Affinity=0;
			bLockedDuringConfirmation=!F.Social->AdjustAffinity(Selected,1,Affinity)
				&& !F.Social->RegisterOrUpdateParticipant(Other,SelectedState);
		}
	});
	F.Social->OnNativeParticipantUnregistered().AddLambda([&](AActor*) { ++Unregistered; });
	if (!TestTrue(TEXT("Exact actor and participant ID stage"),F.Social->StageParticipant(Token(),Selected,SelectedState,Error))) return false;
	TestTrue(TEXT("Repeated exact staging is idempotent"),F.Social->StageParticipant(Token(),Selected,SelectedState,Error));
	TestFalse(TEXT("A different actor cannot claim the reserved ID"),F.Social->StageParticipant(Token(),Other,SelectedState,Error));
	TestFalse(TEXT("A conflicting token cannot claim the reserved actor"),F.Social->StageParticipant(Token(2),Selected,SelectedState,Error));
	TestFalse(TEXT("Staging cannot evict an existing participant ID"),F.Social->StageParticipant(Token(),Other,ExistingState,Error));
	TestTrue(TEXT("Collision rejection preserves the existing participant"),F.Social->FindParticipantById(ExistingState.ParticipantId)==Existing);
	FProjectSocialParticipantState Observed;
	TestFalse(TEXT("Staged actor is absent from ordinary record reads"),F.Social->TryGetParticipantState(Selected,Observed));
	TestNull(TEXT("Staged identity is absent from ID lookup"),F.Social->FindParticipantById(SelectedState.ParticipantId));
	TestFalse(TEXT("Staged actors cannot offer dialogue"),F.Social->CanStartDialogue(Selected));
	TestFalse(TEXT("Staged actor is absent from companion radius queries"),F.Social->GetLivingCompanionsWithin(Other).Contains(Selected));
	int32 Affinity=0;
	TestFalse(TEXT("Ordinary registration cannot activate a reserved actor"),F.Social->RegisterOrUpdateParticipant(Selected,SelectedState));
	TestFalse(TEXT("Ordinary registration cannot steal a reserved ID"),F.Social->RegisterOrUpdateParticipant(Other,SelectedState));
	TestFalse(TEXT("Affinity cannot mutate a reserved actor"),F.Social->AdjustAffinity(Selected,1,Affinity));
	TestFalse(TEXT("Recruitment cannot mutate a reserved actor"),F.Social->SetRecruitedCompanion(Selected,false));
	TestFalse(TEXT("Consent cannot mutate either direction of a reserved actor"),F.Social->SetExplicitIntimacyConsent(Existing,Selected,true));
	F.Social->ClearIntimacyConsentForParticipant(Selected); F.Social->UnregisterParticipant(Selected);
	if (!TestTrue(TEXT("Preparation creates hidden storage and destruction bindings"),F.Social->PrepareStagedParticipants(Token(),Error))) return false;
	TestTrue(TEXT("Repeated preparation is idempotent"),F.Social->PrepareStagedParticipants(Token(),Error));
	TestFalse(TEXT("Prepared record remains invisible"),F.Social->TryGetParticipantState(Selected,Observed));
	TestNull(TEXT("Prepared ID slot remains invisible"),F.Social->FindParticipantById(SelectedState.ParticipantId));
	TestTrue(TEXT("Native observation binds the exact actor, ID and token"),F.Social->ObserveStagedParticipant(Token(),Selected,SelectedState.ParticipantId,Observed,Error));
	TestEqual(TEXT("Staging preserves exact native affinity"),Observed.Affinity,SelectedState.Affinity);
	TestFalse(TEXT("Stale observation cannot borrow the current stage"),F.Social->ObserveStagedParticipant(Token(2),Selected,SelectedState.ParticipantId,Observed,Error));
	TestEqual(TEXT("Preparation and blocked ordinary operations emit no changed events"),Changed,0);
	TestEqual(TEXT("Preparation and blocked unregister emit no unregister events"),Unregistered,0);
	if (!TestTrue(TEXT("The exact prepared batch publishes"),F.Social->PublishStagedParticipantsWithoutEvents(Token(),Error))) return false;
	TestTrue(TEXT("Repeated publication is idempotent"),F.Social->PublishStagedParticipantsWithoutEvents(Token(),Error));
	TestTrue(TEXT("Published record is readable"),F.Social->TryGetParticipantState(Selected,Observed));
	TestTrue(TEXT("Published identity resolves to its exact actor"),F.Social->FindParticipantById(SelectedState.ParticipantId)==Selected);
	TestFalse(TEXT("Published but unconfirmed affinity stays locked"),F.Social->AdjustAffinity(Selected,1,Affinity));
	F.Social->UnregisterParticipant(Selected);
	TestEqual(TEXT("Publication emits no changed event"),Changed,0); TestEqual(TEXT("Published unregister stays blocked"),Unregistered,0);
	TestFalse(TEXT("Stale confirmation has no effect"),F.Social->ConfirmStagedParticipants(Token(2),Error));
	TestTrue(TEXT("Final acceptance confirms the exact batch"),F.Social->ConfirmStagedParticipants(Token(),Error));
	TestTrue(TEXT("Confirmation retains locks during event dispatch"),bLockedDuringConfirmation);
	TestTrue(TEXT("Repeated confirmation is idempotent"),F.Social->ConfirmStagedParticipants(Token(),Error));
	TestEqual(TEXT("Acceptance emits exactly one participant-changed event"),Changed,1);
	TestTrue(TEXT("Accepted release retires its staging ledger"),F.Social->ReleaseStagedParticipants(Token(),true,Error));
	TestTrue(TEXT("Accepted release preserves the participant"),F.Social->FindParticipantById(SelectedState.ParticipantId)==Selected);
	TestTrue(TEXT("Ordinary affinity resumes after acceptance"),F.Social->AdjustAffinity(Selected,1,Affinity));
	TestEqual(TEXT("Ordinary post-acceptance behavior still broadcasts"),Changed,2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoSocialRollbackTest,
	"NoShellForWinter.CalystoDungeon.Director.SocialStaging.RejectedPublicationRestoresVisibilityWithoutEvents",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoSocialRollbackTest::RunTest(const FString&)
{
	using namespace ProjectCalystoSocialStagingTests;
	FFixture F; AActor* Existing=F.Spawn(); AActor* Selected=F.Spawn(); if (!Existing || !Selected) return false;
	const auto Original=State(TEXT("Original")); const auto Candidate=State(TEXT("Candidate")); FString Error;
	F.Social->RegisterOrUpdateParticipant(Existing,Original);
	int32 Changed=0,Unregistered=0;
	F.Social->OnNativeParticipantChanged().AddLambda([&](AActor*) { ++Changed; });
	F.Social->OnNativeParticipantUnregistered().AddLambda([&](AActor*) { ++Unregistered; });
	if (!TestTrue(TEXT("Rollback fixture stages"),F.Social->StageParticipant(Token(),Selected,Candidate,Error))
		|| !TestTrue(TEXT("Rollback fixture prepares"),F.Social->PrepareStagedParticipants(Token(),Error))
		|| !TestTrue(TEXT("Rollback fixture publishes reversibly"),F.Social->PublishStagedParticipantsWithoutEvents(Token(),Error))) return false;
	TestFalse(TEXT("Stale release cannot tear down the live social stage"),F.Social->ReleaseStagedParticipants(Token(2),false,Error));
	TestFalse(TEXT("Unconfirmed state cannot claim accepted cleanup"),F.Social->ReleaseStagedParticipants(Token(),true,Error));
	TestTrue(TEXT("Unconfirmed rejection removes the whole batch"),F.Social->ReleaseStagedParticipants(Token(),false,Error));
	TestTrue(TEXT("Exact rejection remains idempotent"),F.Social->ReleaseStagedParticipants(Token(),false,Error));
	FProjectSocialParticipantState Observed;
	TestFalse(TEXT("Rejected actor has no actual social record"),F.Social->TryGetParticipantState(Selected,Observed));
	TestNull(TEXT("Rejected participant ID has no actual lookup"),F.Social->FindParticipantById(Candidate.ParticipantId));
	TestTrue(TEXT("Unrelated original record remains readable"),F.Social->TryGetParticipantState(Existing,Observed));
	TestEqual(TEXT("Unrelated original affinity remains exact"),Observed.Affinity,Original.Affinity);
	TestFalse(TEXT("A retired token cannot reopen"),F.Social->StageParticipant(Token(),Selected,Candidate,Error));
	TestTrue(TEXT("Rejected actor can be destroyed after its binding is removed"),Selected->Destroy());
	TestEqual(TEXT("Rejected publication emits no changed event"),Changed,0);
	TestEqual(TEXT("Rollback and later actor destruction emit no unregister event"),Unregistered,0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoSocialDestroyedStageTest,
	"NoShellForWinter.CalystoDungeon.Director.SocialStaging.DestroyedActorRejectsWholeBatch",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoSocialDestroyedStageTest::RunTest(const FString&)
{
	using namespace ProjectCalystoSocialStagingTests;
	FFixture F; AActor* First=F.Spawn(); AActor* Second=F.Spawn(); if (!First || !Second) return false;
	int32 Changed=0,Unregistered=0; FString Error;
	F.Social->OnNativeParticipantChanged().AddLambda([&](AActor*) { ++Changed; });
	F.Social->OnNativeParticipantUnregistered().AddLambda([&](AActor*) { ++Unregistered; });
	const auto FirstState=State(TEXT("First")); const auto SecondState=State(TEXT("Second"));
	if (!F.Social->StageParticipant(Token(),First,FirstState,Error) || !F.Social->StageParticipant(Token(),Second,SecondState,Error)
		|| !F.Social->PrepareStagedParticipants(Token(),Error)) { AddError(Error); return false; }
	TestTrue(TEXT("A prepared actor can be destroyed before publication"),Second->Destroy());
	TestFalse(TEXT("One destroyed actor invalidates the entire prepared batch"),F.Social->PublishStagedParticipantsWithoutEvents(Token(),Error));
	FProjectSocialParticipantState Observed;
	TestFalse(TEXT("The surviving actor cannot be partially published"),F.Social->TryGetParticipantState(First,Observed));
	TestNull(TEXT("The surviving ID stays unpublished"),F.Social->FindParticipantById(FirstState.ParticipantId));
	TestFalse(TEXT("Native stage evidence reflects the failed batch"),F.Social->ObserveStagedParticipant(Token(),First,FirstState.ParticipantId,Observed,Error));
	TestTrue(TEXT("A destroyed participant does not prevent complete rejected release"),F.Social->ReleaseStagedParticipants(Token(),false,Error));
	TestEqual(TEXT("Early destruction emits no participant-changed event"),Changed,0);
	TestEqual(TEXT("Early destruction emits no participant-unregistered event"),Unregistered,0);
	AActor* Early=F.Spawn(); if (!Early) return false;
	TestTrue(TEXT("Another token can stage after exact release"),F.Social->StageParticipant(Token(2),Early,SecondState,Error));
	TestTrue(TEXT("An actor may disappear even before delegate preparation"),Early->Destroy());
	TestFalse(TEXT("Preparation detects a pre-binding disappearance"),F.Social->PrepareStagedParticipants(Token(2),Error));
	TestTrue(TEXT("Pre-binding cancellation releases every reserved identity"),F.Social->ReleaseStagedParticipants(Token(2),false,Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoSocialReentrantConfirmationTest,
	"NoShellForWinter.CalystoDungeon.Director.SocialStaging.ConfirmationRetainsBatchAcrossReentrantRelease",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoSocialReentrantConfirmationTest::RunTest(const FString&)
{
	using namespace ProjectCalystoSocialStagingTests;
	FFixture F; AActor* First=F.Spawn(); AActor* Second=F.Spawn(); if (!First || !Second) return false;
	const auto FirstState=State(TEXT("First")); const auto SecondState=State(TEXT("Second")); FString Error;
	if (!F.Social->StageParticipant(Token(),First,FirstState,Error) || !F.Social->StageParticipant(Token(),Second,SecondState,Error)
		|| !F.Social->PrepareStagedParticipants(Token(),Error) || !F.Social->PublishStagedParticipantsWithoutEvents(Token(),Error))
	{ AddError(Error); return false; }
	int32 Changed=0; bool bDeferred=false,bOtherLocked=false;
	F.Social->OnNativeParticipantChanged().AddLambda([&](AActor*)
	{
		++Changed;
		if (Changed==1)
		{
			FString CallbackError; int32 Affinity=0;
			bDeferred=!F.Social->ReleaseStagedParticipants(Token(),true,CallbackError);
			bOtherLocked=!F.Social->AdjustAffinity(Second,1,Affinity);
			F.Social->UnregisterParticipant(Second);
		}
	});
	TestTrue(TEXT("Confirmation completes after a release request inside its first callback"),F.Social->ConfirmStagedParticipants(Token(),Error));
	TestTrue(TEXT("Reentrant release waits for the complete event batch"),bDeferred);
	TestTrue(TEXT("Other batch members remain locked during the callback"),bOtherLocked);
	TestEqual(TEXT("Each accepted participant emits exactly one notification"),Changed,2);
	TestTrue(TEXT("The deferred accepted release becomes complete after callbacks unwind"),F.Social->ReleaseStagedParticipants(Token(),true,Error));
	TestTrue(TEXT("Accepted cleanup preserves the first registered actor"),F.Social->FindParticipantById(FirstState.ParticipantId)==First);
	TestTrue(TEXT("Accepted cleanup preserves the second registered actor"),F.Social->FindParticipantById(SecondState.ParticipantId)==Second);
	return true;
}

#endif
