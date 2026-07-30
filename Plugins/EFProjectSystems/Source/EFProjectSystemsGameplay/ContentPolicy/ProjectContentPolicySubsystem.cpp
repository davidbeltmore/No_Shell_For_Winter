#include "ContentPolicy/ProjectContentPolicySubsystem.h"

#include "ContentPolicy/ProjectOptionalMatureContentProvider.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Social/ProjectSocialSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectContentPolicy, Log, All);

void UProjectContentPolicySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProjectSocialSubsystem>();

	bStreamerSafeForced = IsStreamerSafeCommandLineActive();
	TrackedDoctrineComponent = nullptr;
	CachedPolicy = GetPolicySnapshot();
	bHasCachedPolicy = true;
	PolicyTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::HandlePolicyTicker));

	if (UProjectSocialSubsystem* SocialSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr)
	{
		SocialSubsystem->OnParticipantChanged.AddDynamic(
			this,
			&ThisClass::HandleSocialParticipantChanged);
		SocialSubsystem->OnParticipantUnregistered.AddDynamic(
			this,
			&ThisClass::HandleSocialParticipantUnregistered);
		SocialSubsystem->OnIntimacyConsentChanged.AddDynamic(
			this,
			&ThisClass::HandleSocialIntimacyConsentChanged);
	}

	if (bStreamerSafeForced)
	{
		UE_LOG(LogProjectContentPolicy, Display, TEXT("Streamer-safe policy is forced by command line."));
	}
}

void UProjectContentPolicySubsystem::Deinitialize()
{
	if (PolicyTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PolicyTickerHandle);
		PolicyTickerHandle.Reset();
	}

	if (TrackedDoctrineComponent)
	{
		TrackedDoctrineComponent->OnAttributeLevelChanged.RemoveAll(this);
		TrackedDoctrineComponent = nullptr;
	}

	if (UProjectSocialSubsystem* SocialSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr)
	{
		SocialSubsystem->OnParticipantChanged.RemoveAll(this);
		SocialSubsystem->OnParticipantUnregistered.RemoveAll(this);
		SocialSubsystem->OnIntimacyConsentChanged.RemoveAll(this);
	}

	CancelAllPresentations();
	MatureContentProviders.Reset();
	Super::Deinitialize();
}

bool UProjectContentPolicySubsystem::HandlePolicyTicker(const float DeltaTime)
{
	(void)DeltaTime;
	RefreshTrackedDoctrineComponent();
	RefreshPolicyState();
	return true;
}

FProjectContentPolicySnapshot UProjectContentPolicySubsystem::GetPolicySnapshot() const
{
	return BuildPolicySnapshot(ResolveLocalPlayerDoctrineComponent());
}

FProjectContentPolicySnapshot UProjectContentPolicySubsystem::GetPolicySnapshotForActor(
	AActor* Participant) const
{
	return BuildPolicySnapshot(ResolveDoctrineComponent(Participant));
}

bool UProjectContentPolicySubsystem::IsStreamerSafeForced() const
{
	return GetPolicySnapshot().bStreamerSafeForced;
}

bool UProjectContentPolicySubsystem::IsMatureContentUnlocked() const
{
	return FProjectContentPolicyRules::IsMatureContentUnlocked(GetPolicySnapshot());
}

bool UProjectContentPolicySubsystem::IsMatureContentUnlockedForActor(
	AActor* Participant) const
{
	return FProjectContentPolicyRules::IsMatureContentUnlocked(
		GetPolicySnapshotForActor(Participant));
}

bool UProjectContentPolicySubsystem::IsIntimacyAllowed() const
{
	return FProjectContentPolicyRules::IsIntimacyAllowed(GetPolicySnapshot());
}

bool UProjectContentPolicySubsystem::IsMatureDefeatAllowed() const
{
	return FProjectContentPolicyRules::IsMatureDefeatAllowed(GetPolicySnapshot());
}

bool UProjectContentPolicySubsystem::IsPrivateSoloPresentationAllowed() const
{
	return IsMatureFeatureAllowed(EProjectOptionalMatureFeature::PrivateSoloPresentation);
}

bool UProjectContentPolicySubsystem::IsMatureFeatureAllowed(const EProjectOptionalMatureFeature Feature) const
{
	return FProjectContentPolicyRules::IsFeatureAllowed(GetPolicySnapshot(), Feature);
}

bool UProjectContentPolicySubsystem::IsStreamerSafeCommandLineActive()
{
	return FParse::Param(FCommandLine::Get(), TEXT("StreamerSafe"));
}

bool UProjectContentPolicySubsystem::RegisterMatureContentProvider(UObject* ProviderObject)
{
	if (!IsValid(ProviderObject)
		|| !ProviderObject->GetClass()->ImplementsInterface(UProjectOptionalMatureContentProvider::StaticClass()))
	{
		return false;
	}

	PruneProviders();
	MatureContentProviders.AddUnique(ProviderObject);
	return true;
}

void UProjectContentPolicySubsystem::UnregisterMatureContentProvider(UObject* ProviderObject)
{
	if (!ProviderObject)
	{
		return;
	}

	TArray<FGuid> RequestsToCancel;
	for (const TPair<FGuid, FActiveMaturePresentation>& Pair : ActiveMaturePresentations)
	{
		if (Pair.Value.Provider.Get() == ProviderObject)
		{
			RequestsToCancel.Add(Pair.Key);
		}
	}

	for (const FGuid& RequestId : RequestsToCancel)
	{
		CancelMaturePresentation(RequestId);
	}

	MatureContentProviders.Remove(ProviderObject);
}

bool UProjectContentPolicySubsystem::IsMatureFeaturePresentationAvailable(
	const EProjectOptionalMatureFeature Feature) const
{
	if (!IsMatureFeatureAllowed(Feature))
	{
		return false;
	}

	for (const TWeakObjectPtr<UObject>& ProviderPtr : MatureContentProviders)
	{
		UObject* ProviderObject = ProviderPtr.Get();
		IProjectOptionalMatureContentProvider* Provider = ProviderObject
			? Cast<IProjectOptionalMatureContentProvider>(ProviderObject)
			: nullptr;
		if (Provider
			&& Provider->SupportsMatureFeature(Feature)
			&& Provider->IsMatureFeatureAvailable(Feature))
		{
			return true;
		}
	}

	return false;
}

FGuid UProjectContentPolicySubsystem::TryBeginMaturePresentation(FProjectMaturePresentationRequest Request)
{
	if (!IsMatureFeatureAllowedForActor(Request.Feature, Request.PrimaryParticipant)
		|| Request.PresentationId.IsNone()
		|| !AreParticipantsEligible(Request))
	{
		return FGuid();
	}

	PruneProviders();
	Request.RequestId = Request.RequestId.IsValid() ? Request.RequestId : FGuid::NewGuid();
	if (ActiveMaturePresentations.Contains(Request.RequestId))
	{
		return FGuid();
	}

	for (const TWeakObjectPtr<UObject>& ProviderPtr : MatureContentProviders)
	{
		UObject* ProviderObject = ProviderPtr.Get();
		IProjectOptionalMatureContentProvider* Provider = ProviderObject
			? Cast<IProjectOptionalMatureContentProvider>(ProviderObject)
			: nullptr;
		if (!Provider
			|| !Provider->SupportsMatureFeature(Request.Feature)
			|| !Provider->IsMatureFeatureAvailable(Request.Feature))
		{
			continue;
		}

		if (Provider->TryBeginMaturePresentation(Request))
		{
			FActiveMaturePresentation& ActivePresentation = ActiveMaturePresentations.Add(Request.RequestId);
			ActivePresentation.Provider = ProviderObject;
			ActivePresentation.PrimaryParticipant = Request.PrimaryParticipant;
			ActivePresentation.SecondaryParticipant = Request.SecondaryParticipant;
			ActivePresentation.Feature = Request.Feature;
			return Request.RequestId;
		}
	}

	return FGuid();
}

bool UProjectContentPolicySubsystem::CancelMaturePresentation(const FGuid& RequestId)
{
	FActiveMaturePresentation ActivePresentation;
	if (!RequestId.IsValid() || !ActiveMaturePresentations.RemoveAndCopyValue(RequestId, ActivePresentation))
	{
		return false;
	}

	if (UObject* ProviderObject = ActivePresentation.Provider.Get())
	{
		if (IProjectOptionalMatureContentProvider* Provider = Cast<IProjectOptionalMatureContentProvider>(ProviderObject))
		{
			Provider->CancelMaturePresentation(RequestId);
		}
	}

	return true;
}

void UProjectContentPolicySubsystem::NotifyMaturePresentationEnded(
	const FGuid& RequestId,
	UObject* ProviderObject)
{
	const FActiveMaturePresentation* ActivePresentation = ActiveMaturePresentations.Find(RequestId);
	if (ActivePresentation && ActivePresentation->Provider.Get() == ProviderObject)
	{
		ActiveMaturePresentations.Remove(RequestId);
	}
}

void UProjectContentPolicySubsystem::HandleDoctrineAttributeLevelChanged(
	const EProjectDoctrineAttribute Attribute,
	const int32 OldLevel,
	const int32 NewLevel,
	const int32 NextLevelCost)
{
	(void)OldLevel;
	(void)NewLevel;
	(void)NextLevelCost;
	if (Attribute == EProjectDoctrineAttribute::Charisma)
	{
		RefreshPolicyState();
	}
}

void UProjectContentPolicySubsystem::RefreshTrackedDoctrineComponent()
{
	UProjectInnerDoctrineComponent* ResolvedComponent = ResolveLocalPlayerDoctrineComponent();
	if (ResolvedComponent == TrackedDoctrineComponent)
	{
		return;
	}

	if (TrackedDoctrineComponent)
	{
		TrackedDoctrineComponent->OnAttributeLevelChanged.RemoveAll(this);
	}

	TrackedDoctrineComponent = ResolvedComponent;
	if (TrackedDoctrineComponent)
	{
		TrackedDoctrineComponent->OnAttributeLevelChanged.AddDynamic(
			this,
			&ThisClass::HandleDoctrineAttributeLevelChanged);
	}
}

void UProjectContentPolicySubsystem::RefreshPolicyState()
{
	const FProjectContentPolicySnapshot CurrentPolicy = GetPolicySnapshot();
	if (bHasCachedPolicy && ArePoliciesEquivalent(CachedPolicy, CurrentPolicy))
	{
		return;
	}

	CachedPolicy = CurrentPolicy;
	bHasCachedPolicy = true;
	CancelPresentationsNoLongerAllowed();
	OnContentPolicyChanged.Broadcast(CurrentPolicy);
}

UProjectInnerDoctrineComponent* UProjectContentPolicySubsystem::ResolveDoctrineComponent(
	AActor* Participant) const
{
	if (!IsValid(Participant))
	{
		return nullptr;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Participant))
	{
		Participant = PlayerController->GetPawn();
	}

	return IsValid(Participant)
		? Participant->FindComponentByClass<UProjectInnerDoctrineComponent>()
		: nullptr;
}

UProjectInnerDoctrineComponent* UProjectContentPolicySubsystem::ResolveLocalPlayerDoctrineComponent() const
{
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	return ResolveDoctrineComponent(PlayerController);
}

FProjectContentPolicySnapshot UProjectContentPolicySubsystem::BuildPolicySnapshot(
	const UProjectInnerDoctrineComponent* DoctrineComponent) const
{
	FProjectContentPolicySnapshot Snapshot;
	Snapshot.CharismaLevel = DoctrineComponent
		? FMath::Max(0, DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Charisma))
		: 0;
	Snapshot.bMatureUnlockedByCharisma =
		FProjectContentPolicyRules::IsMatureUnlockedByCharismaLevel(Snapshot.CharismaLevel);
	Snapshot.bStreamerSafeForced = bStreamerSafeForced || IsStreamerSafeCommandLineActive();
	return Snapshot;
}

bool UProjectContentPolicySubsystem::IsMatureFeatureAllowedForActor(
	const EProjectOptionalMatureFeature Feature,
	AActor* Participant) const
{
	return FProjectContentPolicyRules::IsFeatureAllowed(
		GetPolicySnapshotForActor(Participant),
		Feature);
}

void UProjectContentPolicySubsystem::HandleSocialParticipantChanged(AActor* Participant)
{
	(void)Participant;
	CancelPresentationsWithInvalidParticipants();
}

void UProjectContentPolicySubsystem::HandleSocialParticipantUnregistered(AActor* Participant)
{
	(void)Participant;
	CancelPresentationsWithInvalidParticipants();
}

void UProjectContentPolicySubsystem::HandleSocialIntimacyConsentChanged(
	AActor* GrantingParticipant,
	AActor* OtherParticipant,
	const bool bConsented)
{
	(void)GrantingParticipant;
	(void)OtherParticipant;
	if (!bConsented)
	{
		CancelPresentationsWithInvalidParticipants();
	}
}

void UProjectContentPolicySubsystem::CancelPresentationsNoLongerAllowed()
{
	TArray<FGuid> RequestsToCancel;
	for (const TPair<FGuid, FActiveMaturePresentation>& Pair : ActiveMaturePresentations)
	{
		if (!IsMatureFeatureAllowedForActor(
			Pair.Value.Feature,
			Pair.Value.PrimaryParticipant.Get()))
		{
			RequestsToCancel.Add(Pair.Key);
		}
	}

	for (const FGuid& RequestId : RequestsToCancel)
	{
		CancelMaturePresentation(RequestId);
	}
}

void UProjectContentPolicySubsystem::CancelPresentationsWithInvalidParticipants()
{
	TArray<FGuid> RequestsToCancel;
	for (const TPair<FGuid, FActiveMaturePresentation>& Pair : ActiveMaturePresentations)
	{
		FProjectMaturePresentationRequest Request;
		Request.Feature = Pair.Value.Feature;
		Request.PrimaryParticipant = Pair.Value.PrimaryParticipant.Get();
		Request.SecondaryParticipant = Pair.Value.SecondaryParticipant.Get();
		if (!AreParticipantsEligible(Request))
		{
			RequestsToCancel.Add(Pair.Key);
		}
	}

	for (const FGuid& RequestId : RequestsToCancel)
	{
		CancelMaturePresentation(RequestId);
	}
}

void UProjectContentPolicySubsystem::CancelAllPresentations()
{
	TArray<FGuid> RequestIds;
	ActiveMaturePresentations.GenerateKeyArray(RequestIds);
	for (const FGuid& RequestId : RequestIds)
	{
		CancelMaturePresentation(RequestId);
	}

	ActiveMaturePresentations.Reset();
}

void UProjectContentPolicySubsystem::PruneProviders()
{
	MatureContentProviders.RemoveAll(
		[](const TWeakObjectPtr<UObject>& Provider)
		{
			return !Provider.IsValid();
		});
}

bool UProjectContentPolicySubsystem::AreParticipantsEligible(
	const FProjectMaturePresentationRequest& Request) const
{
	if (!IsValid(Request.PrimaryParticipant))
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!SocialSubsystem || !SocialSubsystem->IsVerifiedAdult(Request.PrimaryParticipant))
	{
		return false;
	}

	if (Request.Feature != EProjectOptionalMatureFeature::IntimacySession)
	{
		return true;
	}

	if (!IsValid(Request.SecondaryParticipant))
	{
		return false;
	}

	return SocialSubsystem->EvaluateIntimacyEligibility(
		Request.PrimaryParticipant,
		Request.SecondaryParticipant).bEligible;
}

bool UProjectContentPolicySubsystem::ArePoliciesEquivalent(
	const FProjectContentPolicySnapshot& Left,
	const FProjectContentPolicySnapshot& Right)
{
	return Left.CharismaLevel == Right.CharismaLevel
		&& Left.bMatureUnlockedByCharisma == Right.bMatureUnlockedByCharisma
		&& Left.bStreamerSafeForced == Right.bStreamerSafeForced;
}
