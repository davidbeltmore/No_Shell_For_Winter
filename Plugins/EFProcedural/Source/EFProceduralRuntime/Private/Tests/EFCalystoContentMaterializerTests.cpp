#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoContentMaterializer.h"
#include "Calysto/EFCalystoContentCollisionContract.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace EFCalystoContentMaterializerTests
{
	FGuid Id(uint32 N) { return FGuid(0x4D415445,0x5249414C,0,N); }
	struct FSnapshot final : IEFCalystoGameplaySnapshot
	{
		virtual FString GetCanonicalHash() const override { return TEXT("ExplicitEmptyMaterializerUnitFixture"); }
		virtual bool IsDetachedForTravel() const override { return false; }
	};
	struct FWorld
	{
		UWorld* Value=nullptr;
		FWorld()
		{
			if (!GEngine) return;
			const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			Value=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
			if (Value) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Value);
		}
		~FWorld()
		{
			if (!Value) return;
			// Retain the context through actor destruction and world cleanup, then remove only
			// this isolated fixture's context. Runtime DestroyActor must see a real world context.
			Value->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(Value);
			Value->MarkObjectsPendingKill();
			Value=nullptr;
		}
	};
	TSharedPtr<const FEFCalystoReservedContentManifest> Manifest(bool WithActor,FAutomationTestBase& Test)
	{
		auto* Asset=NewObject<UEFCalystoDungeonDirectorAsset>();
		FEFCalystoStyle S; S.Selection.Id=Id(1); S.Selection.DisplayName=TEXT("Materializer Unit Fixture");
		S.Layout.DungeonSize.Value=24; S.Layout.CandidateDensity.Value=0.32; S.Layout.SidePathPercent.Value=50;
		const TSoftObjectPtr<UMaterialInterface> Material(FSoftObjectPath(TEXT("/Game/Test/Material.Material")));
		S.Materials.Floor=S.Materials.Wall=S.Materials.Roof=Material; S.Decals.Mode=EEFCalystoDecalMode::Block;
		FEFCalystoArchitectureEntry Mesh; Mesh.Selection.Id=Id(2); Mesh.Selection.DisplayName=TEXT("Native Surface");
		Mesh.Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Test/Mesh.Mesh")));
		S.Architecture.Floor.Add(Mesh); Mesh.Selection.Id=Id(3); S.Architecture.Wall.Add(Mesh); Mesh.Selection.Id=Id(4); S.Architecture.Roof.Add(Mesh);
		if (WithActor)
		{
			FEFCalystoContentGroup G; G.Id=Id(10); G.DisplayName=TEXT("Fixture Props"); G.Role=EEFCalystoGameplayRole::Prop;
			G.MaximumPerFloor=1; G.Chance.FirstPercent=G.Chance.LastPercent=100;
			FEFCalystoContentEntry E; E.Selection.Id=Id(11); E.Selection.DisplayName=TEXT("Native Fixture Actor");
			E.ActorClass=TSoftClassPtr<AActor>(AActor::StaticClass()); E.Placement.FootprintHalfExtent=FVector(10);
			E.Placement.Clearance=2; E.Placement.PositionVariationCm=0; E.Placement.bRequiresNavigation=false;
			G.Entries.Add(E); S.Content.Add(G);
		}
		Asset->Styles.Add(S); FEFCalystoTheme T; T.Selection.Id=Id(5); T.Selection.DisplayName=TEXT("Neutral"); T.Decals.Mode=EEFCalystoDecalMode::Block; Asset->RoomThemes.Add(T);
		FEFCalystoCompiledDirector C; TArray<FEFCalystoValidationIssue> Issues;
		if (!Asset->Compile(C,Issues)) { for (const auto& I:Issues) Test.AddError(I.Field+TEXT(": ")+I.Message); return {}; }
		FEFCalystoContentReservationRequest R; R.Random.StyleId=Id(1); R.Random.FloorNumber=1;
		if (WithActor)
		{
			FEFCalystoContentRoom Room; Room.RoomId=1; Room.AllowedRoles.Add(EEFCalystoGameplayRole::Prop); R.Rooms.Add(Room);
			FEFCalystoContentSurface Surface; Surface.Id=Id(12); Surface.RoomId=1; Surface.AvailableHalfExtent=FVector(100);
			FEFCalystoContentCollisionContract Collision; FString CollisionError;
			if (!FEFCalystoContentCollisionContracts::Build(AActor::StaticClass(),Collision,CollisionError)) { Test.AddError(CollisionError); return {}; }
			Surface.AvailableClearanceCm=10; Surface.bCollisionValidated=true; Surface.bCollisionContractValidated=true;
			Surface.ReservedLocalBounds=FBox(FVector(-12),FVector(12)); Surface.CollisionContractHash=Collision.Hash;
			Surface.AllowedRoles.Add(EEFCalystoGameplayRole::Prop); Surface.CompatibleEntryIds.Add(Id(11)); R.Surfaces.Add(Surface);
		}
		auto M=MakeShared<FEFCalystoReservedContentManifest>(); FEFCalystoContentPlanningReport Report;
		if (!FEFCalystoContentReservationPlanner::Build(C,R,*M,Report)) { Test.AddError(Report.Message); return {}; }
		return M;
	}
	FEFCalystoContentGameplayContext Context(UWorld* World,TSharedPtr<const FEFCalystoReservedContentManifest> M)
	{
		FEFCalystoContentGameplayContext C; C.Token={Id(100),Id(101)}; C.World=World; C.AttemptOwner=World->SpawnActor<AActor>();
		C.Manifest=M; C.PreFloorSnapshot=MakeShared<FSnapshot>(); C.DeadlineSeconds=30;
		for (const auto& R:M->GetElements()) if (R.InventorySlot==INDEX_NONE) { FEFCalystoFrozenActorGameplay G; G.ReservationId=R.Id; C.Actors.Add(R.Id,G); }
		return C;
	}
	struct FBridge final : IEFCalystoContentGameplayBridge
	{
		mutable int32 PreflightCalls=0;
		int32 CommitCalls=0,AcceptCalls=0,ReleaseCalls=0;
		EEFCalystoContentReleaseIntent LastReleaseIntent=EEFCalystoContentReleaseIntent::RejectedAttempt;
		EEFCalystoActivationResult AcceptanceResult=EEFCalystoActivationResult::Activated;
		bool bInvalidDependency=false,bPending=false,bPublished=false,bAccepted=false,bMoveDuringObservation=false;
		TFunction<void()> PublicationAction;
		TFunction<void()> AcceptanceAction;
		FEFCalystoContentMaterializer* ReenterCommit=nullptr;
		FEFCalystoContentMaterializer* ReenterRelease=nullptr;
		TWeakObjectPtr<AActor> LastActor;
		virtual bool Preflight(const FEFCalystoContentGameplayContext&,TArray<FSoftObjectPath>& Paths,FString&) const override
		{ ++PreflightCalls; if (bInvalidDependency) Paths.Add(FSoftObjectPath()); return true; }
		virtual bool PrepareDeferredActor(const FEFCalystoContentGameplayContext&,const FEFCalystoReservedContent&,AActor* Actor,TConstArrayView<FEFCalystoReservedContent>,FString&) override
		{
			auto* Root=NewObject<UBoxComponent>(Actor,TEXT("FixtureBounds")); Root->InitBoxExtent(FVector(5));
			Root->SetCollisionEnabled(ECollisionEnabled::NoCollision); Actor->AddInstanceComponent(Root); Actor->SetRootComponent(Root); Root->RegisterComponent(); LastActor=Actor; return true;
		}
		virtual bool StageExistingContainer(const FEFCalystoContentGameplayContext&,FGuid,AActor*,TConstArrayView<FEFCalystoReservedContent>,FString& Error) override
		{ Error=TEXT("Unit fixture does not support existing-container insertion."); return false; }
		virtual EEFCalystoGameplayObservation FinalizeSpawnedActor(const FEFCalystoContentGameplayContext&,const FEFCalystoReservedContent&,AActor*,FString&) override
		{ return EEFCalystoGameplayObservation::Verified; }
		FEFCalystoGameplayElementEvidence Evidence(const FEFCalystoContentGameplayContext& C,const FEFCalystoReservedContent& R) const
		{
			FEFCalystoGameplayElementEvidence E; E.Token=C.Token; E.ReservationId=R.Id; E.EntryId=R.Entry.Selection.Id;
			E.ParentContainerId=R.ParentContainerId; E.InventorySlot=R.InventorySlot;
			E.Payload=R.InventorySlot==INDEX_NONE?R.Entry.ActorClass.ToSoftObjectPath():R.Entry.InventoryClass.ToSoftObjectPath();
			E.SnapshotHash=C.PreFloorSnapshot->GetCanonicalHash(); E.NativeStateHash=TEXT("ExactFixtureNativeState"); E.State=EEFCalystoGameplayObservation::Verified; return E;
		}
		virtual FEFCalystoGameplayElementEvidence ObserveActor(const FEFCalystoContentGameplayContext& C,const FEFCalystoReservedContent& R,AActor* Actor) const override
		{ if (bMoveDuringObservation) Actor->SetActorLocation(FVector(1000)); return Evidence(C,R); }
		virtual FEFCalystoGameplayElementEvidence ObserveInventoryItem(const FEFCalystoContentGameplayContext& C,const FEFCalystoReservedContent& R,AActor*) const override { return Evidence(C,R); }
		virtual EEFCalystoGameplayObservation PrepareCommit(const FEFCalystoContentGameplayContext& C,FEFCalystoGameplayCommitReceipt& Receipt,FString&) override
		{
			if (bPending) return EEFCalystoGameplayObservation::Pending;
			Receipt.Token=C.Token; Receipt.ManifestHash=C.Manifest->GetHash(); Receipt.PreFloorSnapshotHash=C.PreFloorSnapshot->GetCanonicalHash();
			Receipt.PreparedStateHash=TEXT("UnpublishedFixtureState"); for (const auto& R:C.Manifest->GetElements()) Receipt.PreparedElements.Add(R.Id); return EEFCalystoGameplayObservation::Verified;
		}
		virtual bool CommitPrepared(const FEFCalystoContentGameplayContext& C,const FEFCalystoGameplayCommitReceipt&,FString&) override
		{ ++CommitCalls; if (ReenterCommit) ReenterCommit->Release(C.Token); bPublished=true; if (PublicationAction) PublicationAction(); return true; }
		virtual EEFCalystoActivationResult ConfirmAccepted(const FEFCalystoContentGameplayContext&,const FEFCalystoGameplayCommitReceipt&,FString& Error) override
		{
			++AcceptCalls; bAccepted=true; if (AcceptanceAction) AcceptanceAction();
			Error=AcceptanceResult==EEFCalystoActivationResult::InvariantFailure?TEXT("Injected native bridge activation failure."):TEXT("");
			return AcceptanceResult;
		}
		virtual void BeginRelease(const FEFCalystoContentGameplayContext&,EEFCalystoContentReleaseIntent Intent) override
		{ ++ReleaseCalls; LastReleaseIntent=Intent; if (ReenterRelease) ReenterRelease->Observe(2); if (Intent==EEFCalystoContentReleaseIntent::RejectedAttempt) bPublished=false; }
		virtual FEFCalystoGameplayReleaseEvidence ObserveRelease(const FEFCalystoContentGameplayContext& C,EEFCalystoContentReleaseIntent) const override
		{ FEFCalystoGameplayReleaseEvidence E; E.Token=C.Token; E.bSafeToDestroyActors=E.bPersistentStateVerified=true; return E; }
	};
	bool Drain(FEFCalystoContentMaterializer& M,const FEFCalystoAttemptToken& Token,double Now=3.0)
	{ M.Release(Token); for (int32 I=0;I<8 && !M.GetReleaseEvidence(Token).IsComplete();++I) M.Observe(Now+I*0.01,32); return M.GetReleaseEvidence(Token).IsComplete(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoCollisionContractTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.CollisionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoCollisionContractTest::RunTest(const FString&)
{
	FEFCalystoContentCollisionContract First, Second, Empty;
	FString Error;
	TestTrue(TEXT("Character CDO collision contract builds without spawning"),
		FEFCalystoContentCollisionContracts::Build(ACharacter::StaticClass(), First, Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("Character movement/root collision contributes a finite conservative envelope"),
		First.bHasQueryCollision && First.LocalRootBounds.IsValid && First.LocalRootBounds.GetExtent().SizeSquared() > 0.0);
	TestTrue(TEXT("Character CDO primitive envelope reserves more than collision alone when required"),
		First.bHasPrimitiveBounds && First.LocalPrimitiveBounds.IsValid
		&& First.LocalPrimitiveBounds.IsInsideOrOn(First.LocalRootBounds.Min)
		&& First.LocalPrimitiveBounds.IsInsideOrOn(First.LocalRootBounds.Max));
	TestTrue(TEXT("Repeated CDO inspection has a stable collision identity"),
		FEFCalystoContentCollisionContracts::Build(ACharacter::StaticClass(), Second, Error) && First.Hash == Second.Hash);
	TestTrue(TEXT("A collision-free base actor still has an immutable descriptor"),
		FEFCalystoContentCollisionContracts::Build(AActor::StaticClass(), Empty, Error) && !Empty.bHasQueryCollision && !Empty.Hash.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoMaterializerBindingTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.ExactEvidenceBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoMaterializerBindingTest::RunTest(const FString&)
{
	using namespace EFCalystoContentMaterializerTests;
	FEFCalystoContentGameplayContext C; C.Token={Id(1),Id(2)}; C.PreFloorSnapshot=MakeShared<FSnapshot>();
	FEFCalystoReservedContent R; R.Id=Id(3); R.Entry.Selection.Id=Id(4); R.ParentContainerId=Id(5); R.InventorySlot=1;
	R.Entry.InventoryClass=TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/Test/Item.Item_C")));
	FBridge Bridge; const auto Correct=Bridge.Evidence(C,R); FString Error;
	TestTrue(TEXT("Exact typed inventory identity verifies"),Correct.Matches(C,R,Error));
	for (int32 Case=0;Case<5;++Case)
	{
		auto E=Correct;
		if (Case==0) E.Token.Attempt=Id(99); if (Case==1) E.EntryId=Id(99); if (Case==2) E.InventorySlot=2;
		if (Case==3) E.Payload=FSoftObjectPath(TEXT("/Game/Test/Substitution.Substitution_C")); if (Case==4) E.SnapshotHash=TEXT("ChangedSnapshot");
		TestFalse(TEXT("Stale or substituted evidence cannot verify"),E.Matches(C,R,Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoMaterializerTransactionTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.StagingCancellationAndPublication",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoMaterializerTransactionTest::RunTest(const FString&)
{
	using namespace EFCalystoContentMaterializerTests;
	FWorld World; auto ManifestValue=Manifest(false,*this); if (!World.Value || !ManifestValue) return false;
	auto C=Context(World.Value,ManifestValue); auto B=MakeShared<FBridge>(); FString Error;
	FEFCalystoContentMaterializer M; B->bInvalidDependency=true;
	TestFalse(TEXT("Bad selected dependency rejects initialization"),M.Begin(C,B,1,Error));
	TestEqual(TEXT("Retained failed initialization enters release"),M.GetObservation().State,EEFCalystoContentRealizationState::Releasing);
	B->ReenterRelease=&M;
	TestTrue(TEXT("Reentrant bridge release cannot clear its own active context"),Drain(M,C.Token));
	TestEqual(TEXT("Release callback executes exactly once"),B->ReleaseCalls,1);
	B->bInvalidDependency=false; B->bPending=true;
	const auto Retired=C.Token;
	TestFalse(TEXT("A retired attempt cannot reopen and receive stale callbacks"),M.Begin(C,B,4,Error));
	C.Token.Attempt=Id(102);
	TestTrue(TEXT("Released owner accepts a fresh attempt"),M.Begin(C,B,4,Error));
	const int32 Calls=B->PreflightCalls;
	TestTrue(TEXT("Identical request is idempotent"),M.Begin(C,B,4,Error)); TestEqual(TEXT("Idempotence performs no second preflight/load"),B->PreflightCalls,Calls);
	M.Release(Retired); TestEqual(TEXT("Stale release cannot cancel the fresh attempt"),M.GetObservation().State,EEFCalystoContentRealizationState::Loading);
	M.Observe(4.1,4); TestEqual(TEXT("Unfinished gameplay remains pending"),M.GetObservation().State,EEFCalystoContentRealizationState::Verifying);
	B->bPending=false; M.Observe(4.2,4);
	TestEqual(TEXT("All selected evidence prepares without publication"),M.GetObservation().State,EEFCalystoContentRealizationState::Prepared);
	TestFalse(TEXT("Preparation never commits gameplay"),B->bPublished);
	B->ReenterCommit=&M;
	TestFalse(TEXT("Cancellation inside publication does not accept the attempt"),M.Commit(C.Token,4.3,Error));
	TestTrue(TEXT("Rollback waits for the publication callback to unwind"),B->bPublished);
	TestTrue(TEXT("Rejected publication completely releases"),Drain(M,C.Token,6));
	TestFalse(TEXT("Rejected staged publication restores original state"),B->bPublished); TestEqual(TEXT("No acceptance event escaped"),B->AcceptCalls,0);
	B->ReenterCommit=nullptr; C.Token.Attempt=Id(103); TestTrue(TEXT("Fresh accepted fixture begins"),M.Begin(C,B,8,Error)); M.Observe(8.1,4);
	TestTrue(TEXT("Validated publication stages atomically"),M.Commit(C.Token,8.2,Error));
	TestEqual(TEXT("Coordinator still owns final acceptance"),M.GetObservation().State,EEFCalystoContentRealizationState::GameplayPublished);
	TestEqual(TEXT("Acceptance follows the coordinator transaction"),M.ActivateCommitted(C.Token,Error),EEFCalystoActivationResult::Activated);
	TestTrue(TEXT("Accepted floor cleanup completes"),Drain(M,C.Token,10)); TestTrue(TEXT("Accepted state survives floor cleanup"),B->bPublished);
	TestEqual(TEXT("Accepted event publishes exactly once"),B->AcceptCalls,1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoMaterializerClockClassificationTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.ClockClassification",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoMaterializerClockClassificationTest::RunTest(const FString&)
{
	using namespace EFCalystoContentMaterializerTests;
	FWorld World; auto ManifestValue=Manifest(false,*this); if (!World.Value || !ManifestValue) return false;
	FString Error;
	{
		auto C=Context(World.Value,ManifestValue); auto B=MakeShared<FBridge>(); FEFCalystoContentMaterializer M;
		if (!TestTrue(TEXT("Clock fixture begins before its shared deadline"),M.Begin(C,B,5.0,Error))) return false;
		M.Observe(4.999,1);
		TestEqual(TEXT("A backward coordinator clock is configuration, not a request timeout"),M.GetObservation().Failure,EEFCalystoAttemptFailure::Configuration);
		TestEqual(TEXT("A backward coordinator clock has an exact diagnostic code"),M.GetObservation().FailureCode,FName(TEXT("ContentRequestClockInvalid")));
		TestTrue(TEXT("A backward-clock rejection releases all ownership"),Drain(M,C.Token,5.1));
	}
	{
		auto C=Context(World.Value,ManifestValue); C.Token.Attempt=Id(104); C.DeadlineSeconds=6.0;
		auto B=MakeShared<FBridge>(); FEFCalystoContentMaterializer M;
		if (!TestTrue(TEXT("Deadline fixture begins with remaining shared time"),M.Begin(C,B,5.0,Error))) return false;
		M.Observe(6.0,1);
		TestEqual(TEXT("An actual expired request remains a deadline failure"),M.GetObservation().Failure,EEFCalystoAttemptFailure::Deadline);
		TestEqual(TEXT("An actual expired request retains its deadline code"),M.GetObservation().FailureCode,FName(TEXT("ContentRequestDeadline")));
		TestTrue(TEXT("An expired request releases all ownership"),Drain(M,C.Token,6.1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoMaterializerActorTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.ExactActorAndWholeRollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoMaterializerActorTest::RunTest(const FString&)
{
	using namespace EFCalystoContentMaterializerTests;
	FWorld World; auto ManifestValue=Manifest(true,*this); if (!World.Value || !ManifestValue) return false;
	auto C=Context(World.Value,ManifestValue); auto B=MakeShared<FBridge>(); FEFCalystoContentMaterializer M; FString Error;
	if (!TestTrue(TEXT("Resident native actor payload begins without file loading"),M.Begin(C,B,1,Error))) return false;
	for (int32 I=0;I<16 && M.GetObservation().State!=EEFCalystoContentRealizationState::Prepared;++I) M.Observe(1.1+0.01*I,4);
	const bool Prepared=TestEqual(TEXT("Exact reserved actor prepares in bounded native observations"),M.GetObservation().State,EEFCalystoContentRealizationState::Prepared);
	if (!Prepared) { AddError(M.GetObservation().Message); Drain(M,C.Token); return false; }
	TestEqual(TEXT("Exactly one owned actor realizes"),M.GetObservation().OwnedActors,1);
	TestTrue(TEXT("Prepared actor is hidden and inert"),B->LastActor.IsValid() && B->LastActor->IsHidden() && !B->LastActor->GetActorEnableCollision());
	B->bMoveDuringObservation=true;
	TestFalse(TEXT("Mutation inside final native observation prevents commit"),M.Commit(C.Token,1.5,Error));
	TestEqual(TEXT("No bridge publication occurs after actor mutation"),B->CommitCalls,0);
	TestTrue(TEXT("Whole-attempt rollback removes actor, reservation and lease ownership"),Drain(M,C.Token));
	TestEqual(TEXT("No partial accepted IDs remain"),M.GetObservation().VerifiedElements.Num(),0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoMaterializerAcceptedActivationTest,
	"NoShellForWinter.CalystoDungeon.Director.ContentRealization.AcceptedActivationAndCleanup",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoMaterializerAcceptedActivationTest::RunTest(const FString&)
{
	using namespace EFCalystoContentMaterializerTests;
	using Result=EEFCalystoActivationResult;
	FWorld World; auto ManifestValue=Manifest(true,*this); if (!World.Value || !ManifestValue) return false;
	// 0 success; 1 actor lost after publication; 2 explicit exit during acceptance;
	// 3 actor destroyed by acceptance; 4 activation during reversible publication;
	// 5 ownership loss during reversible publication; 6 native bridge failure;
	// 7 bridge exit with exact release; 8 unbacked bridge exit; 9 bridge failure plus release.
	for (int32 Scenario=0;Scenario<10;++Scenario)
	{
		auto C=Context(World.Value,ManifestValue); auto B=MakeShared<FBridge>(); FEFCalystoContentMaterializer M; FString Error;
		if (!TestTrue(TEXT("Nonempty accepted-content fixture begins"),M.Begin(C,B,1,Error))) return false;
		for (int32 I=0;I<16 && M.GetObservation().State!=EEFCalystoContentRealizationState::Prepared;++I) M.Observe(1.1+0.01*I,4);
		if (!TestEqual(TEXT("One actual selected actor is prepared"),M.GetObservation().State,EEFCalystoContentRealizationState::Prepared))
		{ AddError(M.GetObservation().Message); Drain(M,C.Token); return false; }
		AActor* Actor=B->LastActor.Get();
		if (!TestNotNull(TEXT("Native selected actor exists"),Actor)) { Drain(M,C.Token); return false; }
		TestTrue(TEXT("Prepared actor stays hidden, noncolliding and unticked"),Actor->IsHidden() && !Actor->GetActorEnableCollision() && !Actor->IsActorTickEnabled());
		auto Foreign=C.Token; Foreign.Attempt=Id(999);
		TestEqual(TEXT("Foreign activation has no authority"),M.ActivateCommitted(Foreign,Error),Result::InvariantFailure);
		TestEqual(TEXT("Foreign activation leaves preparation intact"),M.GetObservation().State,EEFCalystoContentRealizationState::Prepared);
		TestEqual(TEXT("Foreign activation emits no acceptance"),B->AcceptCalls,0);
		if (Scenario==4) B->PublicationAction=[Actor]() { Actor->SetActorHiddenInGame(false); };
		if (Scenario==5) B->PublicationAction=[Actor]() { Actor->SetOwner(nullptr); };
		const bool Published=M.Commit(C.Token,1.5,Error);
		if (Scenario==4 || Scenario==5)
		{
			TestFalse(TEXT("Publication must retain the exact activation/quarantine ledger"),Published);
			TestEqual(TEXT("A broken publication never notifies acceptance"),B->AcceptCalls,0);
			TestTrue(TEXT("Reversible publication failure drains"),Drain(M,C.Token));
			TestEqual(TEXT("Failure before coordinator acceptance uses rejected cleanup"),B->LastReleaseIntent,EEFCalystoContentReleaseIntent::RejectedAttempt);
			TestFalse(TEXT("Rejected publication restores persistent state"),B->bPublished);
			continue;
		}
		if (!TestTrue(TEXT("Exact publication succeeds while actors remain dormant"),Published)) { Drain(M,C.Token); return false; }
		TestTrue(TEXT("Publication does not activate the selected actor"),Actor->IsHidden() && !Actor->GetActorEnableCollision() && !Actor->IsActorTickEnabled());
		TestEqual(TEXT("Publication alone emits no acceptance event"),B->AcceptCalls,0);
		TestEqual(TEXT("Foreign token cannot accept published gameplay"),M.ActivateCommitted(Foreign,Error),Result::InvariantFailure);
		if (Scenario==1) Actor->Destroy();
		if (Scenario==2) B->AcceptanceAction=[&]() { M.Release(C.Token); };
		if (Scenario==3) B->AcceptanceAction=[Actor]() { Actor->Destroy(); };
		if (Scenario==6 || Scenario==9) B->AcceptanceResult=Result::InvariantFailure;
		if (Scenario==7 || Scenario==8) B->AcceptanceResult=Result::AcceptedExitRequested;
		if (Scenario==7 || Scenario==9) B->AcceptanceAction=[&]() { M.Release(C.Token); };
		const Result Expected=Scenario==0?Result::Activated:(Scenario==2 || Scenario==7)?Result::AcceptedExitRequested:Result::InvariantFailure;
		TestEqual(FString::Printf(TEXT("Scenario %d returns its actual post-acceptance outcome"),Scenario),M.ActivateCommitted(C.Token,Error),Expected);
		if (Scenario==6 || Scenario==9)
		{
			TestEqual(TEXT("Actual bridge failure diagnostic reaches the caller"),Error,FString(TEXT("Injected native bridge activation failure.")));
			TestEqual(TEXT("Bridge failure is retained even with a concurrent accepted release"),M.GetObservation().FailureCode,FName(TEXT("ContentActivationInvariant")));
		}
		if (Scenario==8)
			TestTrue(TEXT("An exit result alone cannot claim coordinator cancellation"),Error.Contains(TEXT("without an actual exact-token release request")));
		const int32 ExpectedNotifications=Scenario==1?0:1;
		TestEqual(TEXT("Acceptance notification is at most once, including interrupted activation"),B->AcceptCalls,ExpectedNotifications);
		const auto AcceptedState=M.GetObservation().State;
		TestEqual(TEXT("A stale token cannot notify or overwrite accepted activation"),M.ActivateCommitted(Foreign,Error),Result::InvariantFailure);
		TestEqual(TEXT("Stale activation leaves the accepted lifecycle intact"),M.GetObservation().State,AcceptedState);
		B->AcceptanceResult=Result::Activated; // A retry cannot turn an earlier native failure/exit into success.
		TestEqual(TEXT("Repeated exact activation preserves its explicit outcome"),M.ActivateCommitted(C.Token,Error),Expected);
		if (Scenario==6 || Scenario==9) TestEqual(TEXT("Repeated activation retains the original native failure"),Error,FString(TEXT("Injected native bridge activation failure.")));
		TestEqual(TEXT("Retry cannot duplicate acceptance notifications"),B->AcceptCalls,ExpectedNotifications);
		M.Release(Foreign);
		if (Scenario==0)
		{
			TestEqual(TEXT("Foreign release does not cancel the accepted floor"),M.GetObservation().State,EEFCalystoContentRealizationState::Committed);
			TestFalse(TEXT("Accepted selected actor is visible"),Actor->IsHidden());
			TestTrue(TEXT("Accepted selected actor restores its authored actor collision flag"),Actor->GetActorEnableCollision());
		}
		else if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			TestTrue(TEXT("Interrupted activation quarantines every surviving selected actor"),Actor->IsHidden() && !Actor->GetActorEnableCollision() && !Actor->IsActorTickEnabled());
		TestTrue(TEXT("Accepted cleanup removes owned actors and leases"),Drain(M,C.Token));
		TestEqual(TEXT("Coordinator acceptance is sticky even when the first activation check fails"),B->LastReleaseIntent,EEFCalystoContentReleaseIntent::AcceptedFloorExit);
		TestTrue(TEXT("Accepted persistent publication survives cleanup"),B->bPublished);
		TestEqual(TEXT("Bridge release is performed exactly once"),B->ReleaseCalls,1);
		TestEqual(TEXT("Cleanup cannot duplicate accepted notifications"),B->AcceptCalls,ExpectedNotifications);
		TestEqual(TEXT("No owned selected actors survive cleanup"),M.GetObservation().OwnedActors,0);
	}
	return true;
}

#endif
