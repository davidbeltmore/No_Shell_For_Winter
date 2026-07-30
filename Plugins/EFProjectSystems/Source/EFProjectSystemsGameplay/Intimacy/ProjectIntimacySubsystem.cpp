#include "Intimacy/ProjectIntimacySubsystem.h"

#include "Characters/ProjectTargetingFixComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Intimacy/ProjectIntimacyDialogueLibrary.h"
#include "Intimacy/ProjectIntimacyHudWidget.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Intimacy/ProjectIntimacySaveGame.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "Intimacy/ProjectIntimacyZoneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Social/ProjectSocialSubsystem.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectIntimacy, Log, All);

const FName UProjectIntimacySubsystem::FirstIntimacyHeartChestTattooRewardId(
	TEXT("Tattoo.TestHeartChest.IntimacyEncounter1"));
const FName UProjectIntimacySubsystem::TestTattooIntimacyRewardId(TEXT("Test_Tattoo_Intimacy"));

namespace ProjectIntimacySubsystemPrivate
{
	const FName IntimacySceneId(TEXT("Actions.Together.0001Scene"));
	const FName InCombatActorTag(TEXT("Project.State.InCombat"));
	const FName UnconsciousActorTag(TEXT("Project.State.Unconscious"));
	const FName KnockedOutActorTag(TEXT("Project.State.KnockedOut"));
	const FName HostileActorTag(TEXT("Project.State.Hostile"));
	const FName HubSocialCompanionTag(TEXT("Project.Social.HubCompanion"));
	const FName HubSocialZoneTag(TEXT("Project.Intimacy.HubAllowedZone"));
	constexpr int32 InputPriority = 75;

	bool IsSelectableTalkAction(const EProjectIntimacyTalkAction Action)
	{
		switch (Action)
		{
		case EProjectIntimacyTalkAction::SpeedSlow:
		case EProjectIntimacyTalkAction::SpeedNormal:
		case EProjectIntimacyTalkAction::SpeedIntense:
		case EProjectIntimacyTalkAction::Compliment:
		case EProjectIntimacyTalkAction::More:
		case EProjectIntimacyTalkAction::Back:
			return true;
		default:
			return false;
		}
	}

	FText SocialEligibilityFailureText(const EProjectSocialEligibilityFailure Failure)
	{
		switch (Failure)
		{
		case EProjectSocialEligibilityFailure::ParticipantNotRegistered:
			return FText::FromString(TEXT("Every participant must be registered."));
		case EProjectSocialEligibilityFailure::AdultVerificationRequired:
			return FText::FromString(TEXT("Every participant must be verified as an adult."));
		case EProjectSocialEligibilityFailure::ExplicitConsentRequired:
			return FText::FromString(TEXT("Bilateral explicit consent is required."));
		case EProjectSocialEligibilityFailure::ParticipantNotAlive:
			return FText::FromString(TEXT("Every participant must be alive."));
		case EProjectSocialEligibilityFailure::ParticipantNotConscious:
			return FText::FromString(TEXT("Every participant must be conscious."));
		case EProjectSocialEligibilityFailure::Hostile:
			return FText::FromString(TEXT("The selected character must be non-hostile."));
		case EProjectSocialEligibilityFailure::InCombat:
			return FText::FromString(TEXT("Intimacy is unavailable during combat."));
		case EProjectSocialEligibilityFailure::UnsafeLocation:
			return FText::FromString(TEXT("Intimacy is unavailable in this area."));
		case EProjectSocialEligibilityFailure::ParticipantNotCompanion:
			return FText::FromString(TEXT("The selected character is not an available companion."));
		case EProjectSocialEligibilityFailure::ConsentNotOffered:
			return FText::FromString(TEXT("The selected companion has not offered this interaction."));
		case EProjectSocialEligibilityFailure::AffinityTooLow:
			return FText::FromString(TEXT("More affinity is required."));
		case EProjectSocialEligibilityFailure::None:
		default:
			return FText();
		}
	}

	FProjectIntimacyResolvedOption MakeOption(const TCHAR* Id, const TCHAR* Label)
	{
		FProjectIntimacyResolvedOption Option;
		Option.OptionId = FName(Id);
		Option.Label = FText::FromString(Label);
		return Option;
	}

	FGameplayTag Tag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(TagName, false);
	}

	void AddTag(FGameplayTagContainer& Container, const TCHAR* TagName)
	{
		const FGameplayTag GameplayTag = Tag(TagName);
		if (GameplayTag.IsValid())
		{
			Container.AddTag(GameplayTag);
		}
	}

	void RemoveTag(FGameplayTagContainer& Container, const TCHAR* TagName)
	{
		const FGameplayTag GameplayTag = Tag(TagName);
		if (GameplayTag.IsValid())
		{
			Container.RemoveTag(GameplayTag);
		}
	}

	bool HasTag(const FGameplayTagContainer& Container, const TCHAR* TagName)
	{
		const FGameplayTag GameplayTag = Tag(TagName);
		return GameplayTag.IsValid() && Container.HasTagExact(GameplayTag);
	}

	FString FormatDuration(const float Seconds)
	{
		const int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(Seconds));
		const int32 Hours = TotalSeconds / 3600;
		const int32 Minutes = (TotalSeconds % 3600) / 60;
		const int32 Remainder = TotalSeconds % 60;
		return Hours > 0
			? FString::Printf(TEXT("%02d:%02d:%02d"), Hours, Minutes, Remainder)
			: FString::Printf(TEXT("%02d:%02d"), Minutes, Remainder);
	}
}

void UProjectIntimacySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RandomStream.Initialize(static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7fffffff));
	RuntimeUnlockedAutomaticTattooIds.Reset();
	HubSocialCompanionActor.Reset();
	HubSocialZoneActor.Reset();
	HubSocialRegisteredPlayer.Reset();
	bHubSocialProductRouteAttempted = false;
	LoadPersistentState();

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr)
	{
		ContentPolicy->RegisterMatureContentProvider(this);
	}
}

void UProjectIntimacySubsystem::Deinitialize()
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	if (MaturePresentationRequestId.IsValid())
	{
		const FGuid RequestId = MaturePresentationRequestId;
		if (!ContentPolicy || !ContentPolicy->CancelMaturePresentation(RequestId))
		{
			CancelMaturePresentation(RequestId);
		}
	}
	EndSession(false);
	if (ContentPolicy)
	{
		ContentPolicy->UnregisterMatureContentProvider(this);
	}
	DetachFromTrackedPlayerController();
	SavePersistentState();
	RuntimeUnlockedAutomaticTattooIds.Reset();
	HubSocialCompanionActor.Reset();
	HubSocialZoneActor.Reset();
	HubSocialRegisteredPlayer.Reset();
	bHubSocialProductRouteAttempted = false;
	IntimacySaveGame = nullptr;
	Super::Deinitialize();
}

void UProjectIntimacySubsystem::Tick(const float DeltaTime)
{
	TryResolveRuntimeContext();

	AActor* PartnerActor = nullptr;
	UProjectIntimacyPartnerComponent* PartnerComponent = nullptr;
	const bool bSceneActive = ResolveActiveIntimacyPartner(PartnerActor, PartnerComponent);

	if (!bSceneActive)
	{
		if (bSessionActive)
		{
			if (!ActiveSession.bSatisfied && !bSuppressStartUntilSceneEnds)
			{
				UE_LOG(
					LogProjectIntimacy,
					Warning,
					TEXT("[ProjectIntimacy] unexpected_intimacy_scene_end partner=%s hud=%s please=%s time=%.2f"),
					*GetNameSafe(ActiveSession.PartnerActor.Get()),
					ActiveSession.bHudVisible ? TEXT("true") : TEXT("false"),
					ActiveSession.bPleaseActive ? TEXT("true") : TEXT("false"),
					ActiveSession.SessionTimeSeconds);
			}
			EndSession(!ActiveSession.bSatisfied);
		}
		else if (MaturePresentationRequestId.IsValid())
		{
			ClearConsentForPartner(MaturePresentationPartnerActor.Get());
			EndMaturePresentationRegistration(true);
		}
		bSuppressStartUntilSceneEnds = false;
		return;
	}

	if (bSuppressStartUntilSceneEnds)
	{
		return;
	}

	if (!bSessionActive)
	{
		StartSession(PartnerActor, PartnerComponent);
	}
	else if (ActiveSession.PartnerActor.Get() != PartnerActor)
	{
		EndSession(!ActiveSession.bSatisfied);
		StartSession(PartnerActor, PartnerComponent);
	}

	UpdateActiveSession(DeltaTime);
}

TStatId UProjectIntimacySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectIntimacySubsystem, STATGROUP_Tickables);
}

bool UProjectIntimacySubsystem::IsTickable() const
{
	return !IsTemplate();
}

bool UProjectIntimacySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

int32 UProjectIntimacySubsystem::ResolveCharismaLevelForActor(const AActor* Actor)
{
	auto ResolveDirect = [](const AActor* Candidate) -> int32
	{
		const UProjectInnerDoctrineComponent* DoctrineComponent = IsValid(Candidate)
			? Candidate->FindComponentByClass<UProjectInnerDoctrineComponent>()
			: nullptr;
		return DoctrineComponent
			? DoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Charisma)
			: INDEX_NONE;
	};

	if (const int32 DirectLevel = ResolveDirect(Actor); DirectLevel != INDEX_NONE)
	{
		return DirectLevel;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const int32 ControllerLevel = ResolveDirect(Pawn->GetController()); ControllerLevel != INDEX_NONE)
		{
			return ControllerLevel;
		}
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		if (const int32 PawnLevel = ResolveDirect(Controller->GetPawn()); PawnLevel != INDEX_NONE)
		{
			return PawnLevel;
		}
	}

	return 0;
}

bool UProjectIntimacySubsystem::HasRequiredCharismaForActor(const AActor* Actor)
{
	return UProjectIntimacySettings::MeetsAdultInteractionCharismaRequirement(
		ResolveCharismaLevelForActor(Actor));
}

AActor* UProjectIntimacySubsystem::GetHubSocialCompanionActor() const
{
	return HubSocialCompanionActor.Get();
}

bool UProjectIntimacySubsystem::SupportsMatureFeature(
	const EProjectOptionalMatureFeature Feature) const
{
	return Feature == EProjectOptionalMatureFeature::IntimacySession;
}

bool UProjectIntimacySubsystem::IsMatureFeatureAvailable(
	const EProjectOptionalMatureFeature Feature) const
{
	const UWorld* World = GetWorld();
	return SupportsMatureFeature(Feature)
		&& World
		&& DoesSupportWorldType(World->WorldType);
}

bool UProjectIntimacySubsystem::TryBeginMaturePresentation(
	const FProjectMaturePresentationRequest& Request)
{
	if (!SupportsMatureFeature(Request.Feature)
		|| !Request.RequestId.IsValid()
		|| Request.PresentationId != ProjectIntimacySubsystemPrivate::IntimacySceneId
		|| MaturePresentationRequestId.IsValid())
	{
		return false;
	}

	TryResolveRuntimeContext();
	AActor* PartnerActor = Request.SecondaryParticipant.Get();
	if (!IsValid(TrackedPlayerPawn.Get())
		|| Request.PrimaryParticipant.Get() != TrackedPlayerPawn.Get()
		|| !IsValid(PartnerActor)
		|| PartnerActor == TrackedPlayerPawn.Get())
	{
		return false;
	}

	FText EligibilityFailure;
	if (!CanStartIntimacyWithPartner(PartnerActor, EligibilityFailure))
	{
		return false;
	}

	AActor* ActivePartnerActor = nullptr;
	UProjectIntimacyPartnerComponent* ActivePartnerComponent = nullptr;
	const bool bMatchingSceneAlreadyActive =
		ResolveActiveIntimacyPartner(ActivePartnerActor, ActivePartnerComponent)
		&& ActivePartnerActor == PartnerActor;
	if (!bMatchingSceneAlreadyActive && !BeginIntimacySceneAction(PartnerActor))
	{
		return false;
	}

	MaturePresentationRequestId = Request.RequestId;
	MaturePresentationPartnerActor = PartnerActor;
	return true;
}

void UProjectIntimacySubsystem::CancelMaturePresentation(const FGuid& RequestId)
{
	if (!RequestId.IsValid() || RequestId != MaturePresentationRequestId)
	{
		return;
	}

	AActor* PartnerActor = MaturePresentationPartnerActor.Get();
	MaturePresentationRequestId = FGuid();
	MaturePresentationPartnerActor.Reset();

	if (bSessionActive)
	{
		CancelActiveSession();
		return;
	}

	ClearConsentForPartner(PartnerActor);
	bSuppressStartUntilSceneEnds = true;
	if (UWorld* World = GetWorld())
	{
		if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
		{
			EmoteSubsystem->RequestCancelActiveEmote();
		}
	}
}

bool UProjectIntimacySubsystem::IsIntimacySessionActive() const
{
	return bSessionActive;
}

bool UProjectIntimacySubsystem::IsActorInActiveIntimacySession(const AActor* Actor) const
{
	return bSessionActive
		&& IsValid(Actor)
		&& ActiveSession.PartnerActor.Get() == Actor;
}

bool UProjectIntimacySubsystem::IsHudVisible() const
{
	return bSessionActive && ActiveSession.bHudVisible;
}

EProjectIntimacyHudMode UProjectIntimacySubsystem::GetHudMode() const
{
	return bSessionActive ? ActiveSession.HudMode : EProjectIntimacyHudMode::Main;
}

bool UProjectIntimacySubsystem::IsPleaseActive() const
{
	return bSessionActive && ActiveSession.bPleaseActive;
}

FString UProjectIntimacySubsystem::GetActivePartnerId() const
{
	return bSessionActive ? ActiveSession.PartnerId : FString();
}

float UProjectIntimacySubsystem::GetCurrentSessionProgress() const
{
	return bSessionActive ? ActiveSession.SessionProgress : 0.0f;
}

float UProjectIntimacySubsystem::GetCurrentSessionPeak() const
{
	return bSessionActive ? ActiveSession.SessionPeak : 0.0f;
}

float UProjectIntimacySubsystem::GetTalkCooldownRemaining() const
{
	return bSessionActive ? ActiveSession.TalkCooldownRemaining : 0.0f;
}

int32 UProjectIntimacySubsystem::GetTotalIntimacyEncounterCount() const
{
	int32 TotalEncounters = 0;
	if (!IntimacySaveGame)
	{
		return TotalEncounters;
	}

	for (const TPair<FString, FProjectIntimacyPartnerProfile>& ProfilePair : IntimacySaveGame->PartnerProfiles)
	{
		TotalEncounters += FMath::Max(0, ProfilePair.Value.Encounters);
	}

	return TotalEncounters;
}

bool UProjectIntimacySubsystem::HasAnyIntimacyEncounter() const
{
	return GetTotalIntimacyEncounterCount() > 0;
}

bool UProjectIntimacySubsystem::IsAutomaticTattooRewardUnlocked(const FName TattooRewardId) const
{
	if (TattooRewardId.IsNone())
	{
		return false;
	}

	return RuntimeUnlockedAutomaticTattooIds.Contains(TattooRewardId);
}

bool UProjectIntimacySubsystem::UnlockAutomaticTattooReward(const FName TattooRewardId)
{
	if (TattooRewardId.IsNone())
	{
		return false;
	}

	const int32 PreviousCount = RuntimeUnlockedAutomaticTattooIds.Num();
	RuntimeUnlockedAutomaticTattooIds.Add(TattooRewardId);
	const bool bAdded = RuntimeUnlockedAutomaticTattooIds.Num() != PreviousCount;
	if (bAdded)
	{
		UE_LOG(LogProjectIntimacy, Display, TEXT("[ProjectIntimacy] runtime_automatic_tattoo_reward_unlocked id=%s"), *TattooRewardId.ToString());
	}

	return true;
}

bool UProjectIntimacySubsystem::GrantFirstIntimacyEncounterForAutomation()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	LoadPersistentState();
	if (!IntimacySaveGame)
	{
		return false;
	}

	const FString PartnerId(TEXT("TattooShopAutoTattooQA"));
	FProjectIntimacyPartnerProfile& Profile = IntimacySaveGame->PartnerProfiles.FindOrAdd(PartnerId);
	Profile.PartnerId = PartnerId;
	Profile.Encounters = FMath::Max(1, Profile.Encounters);
	Profile.bHasFirstEncounter = true;
	if (Profile.FirstEncounterUtc.GetTicks() == 0)
	{
		Profile.FirstEncounterUtc = FDateTime::UtcNow();
	}
	RefreshRelationshipTags(Profile);
	UnlockAutomaticTattooReward(TestTattooIntimacyRewardId);
	SavePersistentState();
	return true;
#endif
}

bool UProjectIntimacySubsystem::ForceSessionPeakForAutomation()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (!bSessionActive)
	{
		return false;
	}

	UProjectIntimacyPartnerComponent* PartnerComponent = ActiveSession.PartnerComponent.Get();
	if (!PartnerComponent)
	{
		return false;
	}

	FProjectIntimacyPartnerProfile& Profile = GetMutableProfile(PartnerComponent);
	const int32 PreviousPeakCount = Profile.SessionPeakCount;
	const float RequiredProgress = FMath::Max(
		1.0f,
		ActiveSession.SessionPeakThreshold - ActiveSession.SessionPeak + 1.0f);
	ApplySessionProgress(RequiredProgress, FText::FromString(TEXT("Automation session peak")));
	return Profile.SessionPeakCount > PreviousPeakCount;
#endif
}

void UProjectIntimacySubsystem::RequestToggleHud()
{
	if (!bSessionActive)
	{
		return;
	}

	ActiveSession.bHudVisible = !ActiveSession.bHudVisible;
	RefreshHudWidget();
}

bool UProjectIntimacySubsystem::RequestQuickStartIntimacy()
{
	if (bSessionActive)
	{
		return false;
	}

	TryResolveRuntimeContext();
	if (!TrackedPlayerPawn)
	{
		UE_LOG(LogProjectIntimacy, Warning, TEXT("[ProjectIntimacy] Quick start failed: no tracked player pawn."));
		return false;
	}

	if (!TrackedTargetingFixComponent)
	{
		UE_LOG(LogProjectIntimacy, Warning, TEXT("[ProjectIntimacy] Quick start failed: no targeting component. Select/observe a target with T first."));
		return false;
	}

	AActor* TargetActor = TrackedTargetingFixComponent->GetCurrentTargetActor();
	if (!IsValid(TargetActor) || TargetActor == TrackedPlayerPawn.Get())
	{
		UE_LOG(LogProjectIntimacy, Warning, TEXT("[ProjectIntimacy] Quick start failed: no valid target. Select/observe a target with T first."));
		return false;
	}

	UProjectIntimacyPartnerComponent* TargetPartnerComponent =
		TargetActor->FindComponentByClass<UProjectIntimacyPartnerComponent>();
	if (!TargetPartnerComponent)
	{
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Quick start denied: the selected actor is not an authored social companion."));
		return false;
	}

	FText EligibilityFailure;
	RegisterSocialParticipants(TargetActor, TargetPartnerComponent, false);
	if (!CanRequestIntimacyWithPartner(TargetActor, EligibilityFailure))
	{
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Quick start request denied: %s"),
			*EligibilityFailure.ToString());
		return false;
	}

	RegisterSocialParticipants(TargetActor, TargetPartnerComponent, true);
	if (!CanStartIntimacyWithPartner(TargetActor, EligibilityFailure))
	{
		ClearConsentForPartner(TargetActor);
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Quick start denied: %s"),
			*EligibilityFailure.ToString());
		return false;
	}

	if (!EnsureMaturePresentationRegistered(TargetActor, EligibilityFailure))
	{
		ClearConsentForPartner(TargetActor);
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Quick start denied by the mature-content presentation authority: %s"),
			*EligibilityFailure.ToString());
		return false;
	}

	return true;
}

bool UProjectIntimacySubsystem::CanRequestIntimacyWithPartner(
	AActor* PartnerActor,
	FText& OutFailureReason) const
{
	const UProjectIntimacyPartnerComponent* PartnerComponent = IsValid(PartnerActor)
		? PartnerActor->FindComponentByClass<UProjectIntimacyPartnerComponent>()
		: nullptr;
	if (!PartnerComponent || !PartnerComponent->bSocialCompanion)
	{
		OutFailureReason = FText::FromString(TEXT("The selected character is not an available companion."));
		return false;
	}
	if (!PartnerComponent->bOffersPlayerInitiatedConsent)
	{
		OutFailureReason = FText::FromString(TEXT("The selected companion has not offered this interaction."));
		return false;
	}

	FProjectIntimacyEligibilityContext Context = BuildEligibilityContext(PartnerActor, PartnerComponent);
	// This is a non-mutating preflight. The subsequent request records both
	// directional consent entries atomically; all other gates remain real.
	Context.bExplicitConsent = true;
	const EProjectIntimacyEligibilityFailure Failure =
		UProjectIntimacySettings::EvaluateEligibility(Context);
	if (Failure != EProjectIntimacyEligibilityFailure::None)
	{
		OutFailureReason = UProjectIntimacySettings::GetEligibilityFailureText(Failure);
		return false;
	}

	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!SocialSubsystem)
	{
		OutFailureReason = FText::FromString(TEXT("Social eligibility is unavailable."));
		return false;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const FProjectSocialEligibilityResult SocialResult =
		SocialSubsystem->EvaluateIntimacyConsentOffer(
			TrackedPlayerPawn.Get(),
			PartnerActor,
			Settings ? Settings->MinimumAffinityForIntimacyConsent : 25);
	OutFailureReason = ProjectIntimacySubsystemPrivate::SocialEligibilityFailureText(SocialResult.Failure);
	return SocialResult.bEligible;
}

bool UProjectIntimacySubsystem::CanStartIntimacyWithPartner(
	AActor* PartnerActor,
	FText& OutFailureReason) const
{
	const UProjectIntimacyPartnerComponent* PartnerComponent = IsValid(PartnerActor)
		? PartnerActor->FindComponentByClass<UProjectIntimacyPartnerComponent>()
		: nullptr;
	const FProjectIntimacyEligibilityContext Context = BuildEligibilityContext(PartnerActor, PartnerComponent);
	const EProjectIntimacyEligibilityFailure Failure = UProjectIntimacySettings::EvaluateEligibility(Context);
	OutFailureReason = UProjectIntimacySettings::GetEligibilityFailureText(Failure);
	if (Failure != EProjectIntimacyEligibilityFailure::None)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!SocialSubsystem)
	{
		OutFailureReason = FText::FromString(TEXT("Social eligibility is unavailable."));
		return false;
	}

	const FProjectSocialEligibilityResult SocialResult =
		SocialSubsystem->EvaluateIntimacyEligibility(TrackedPlayerPawn.Get(), PartnerActor);
	OutFailureReason = ProjectIntimacySubsystemPrivate::SocialEligibilityFailureText(SocialResult.Failure);
	return SocialResult.bEligible;
}

void UProjectIntimacySubsystem::RequestNavigate(const int32 Direction)
{
	if (!bSessionActive || !ActiveSession.bHudVisible || ResolvedOptions.Num() <= 0)
	{
		return;
	}

	ActiveSession.SelectedOptionIndex = (ActiveSession.SelectedOptionIndex + Direction) % ResolvedOptions.Num();
	if (ActiveSession.SelectedOptionIndex < 0)
	{
		ActiveSession.SelectedOptionIndex += ResolvedOptions.Num();
	}
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::RequestBack()
{
	if (!bSessionActive || !ActiveSession.bHudVisible)
	{
		return;
	}

	if (ActiveSession.HudMode == EProjectIntimacyHudMode::Main)
	{
		ActiveSession.bHudVisible = false;
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Talk && !ActiveSession.ActiveTalkCategoryId.IsNone())
	{
		ActiveSession.ActiveTalkCategoryId = NAME_None;
		ActiveSession.StatusText = FText::FromString(TEXT("Choose a Talk style."));
		RefreshResolvedOptions();
		RefreshHudWidget();
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Items && !ActiveSession.ActiveItemCategoryId.IsNone())
	{
		ActiveSession.ActiveItemCategoryId = NAME_None;
		ActiveSession.StatusText = FText::FromString(TEXT("Choose an item category."));
		RefreshResolvedOptions();
		RefreshHudWidget();
	}
	else
	{
		SetHudMode(EProjectIntimacyHudMode::Main);
	}
}

void UProjectIntimacySubsystem::RequestConfirm()
{
	if (!bSessionActive || !ActiveSession.bHudVisible)
	{
		return;
	}

	if (ActiveSession.bPleaseActive)
	{
		ResolvePleasePress();
		return;
	}

	if (!ResolvedOptions.IsValidIndex(ActiveSession.SelectedOptionIndex))
	{
		return;
	}

	const FProjectIntimacyResolvedOption Option = ResolvedOptions[ActiveSession.SelectedOptionIndex];
	if (ActiveSession.HudMode == EProjectIntimacyHudMode::Main)
	{
		HandleMainOption(Option.OptionId);
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Talk)
	{
		HandleTalkOption(Option);
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Items)
	{
		HandleItemsOption(Option);
	}
}

void UProjectIntimacySubsystem::RequestCancelIntimacy()
{
	CancelActiveSession();
}

#if WITH_DEV_AUTOMATION_TESTS
bool UProjectIntimacySubsystem::AutomationRunMenuAndPleaseSmoke(
	FString& OutFailureReason,
	int32& OutStepCount,
	bool& bOutPleaseCompleted,
	bool& bOutSessionPeakTriggered)
{
	OutFailureReason.Reset();
	OutStepCount = 0;
	bOutPleaseCompleted = false;
	bOutSessionPeakTriggered = false;

	auto Fail = [&](const FString& Reason) -> bool
	{
		OutFailureReason = Reason;
		RefreshHudWidget();
		return false;
	};

	if (!bSessionActive)
	{
		return Fail(TEXT("Intimacy session is not active."));
	}

	if (!ActiveSession.bHudVisible)
	{
		RequestToggleHud();
		++OutStepCount;
	}

	RefreshResolvedOptions();
	auto FindOptionIndex = [this](const FName OptionId) -> int32
	{
		for (int32 Index = 0; Index < ResolvedOptions.Num(); ++Index)
		{
			if (ResolvedOptions[Index].OptionId == OptionId)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	};

	auto SelectOption = [&](const FName OptionId, const TCHAR* StepName) -> bool
	{
		RefreshResolvedOptions();
		const int32 OptionIndex = FindOptionIndex(OptionId);
		if (!ResolvedOptions.IsValidIndex(OptionIndex))
		{
			OutFailureReason = FString::Printf(TEXT("%s option %s was not available."), StepName, *OptionId.ToString());
			RefreshHudWidget();
			return false;
		}

		ActiveSession.SelectedOptionIndex = OptionIndex;
		RequestConfirm();
		++OutStepCount;
		return true;
	};

	if (FindOptionIndex(TEXT("Main.Talk")) == INDEX_NONE || FindOptionIndex(TEXT("Main.Please")) == INDEX_NONE)
	{
		return Fail(TEXT("Intimacy main menu did not expose Talk and Please."));
	}

	if (FindOptionIndex(TEXT("Main.Items")) != INDEX_NONE)
	{
		if (!SelectOption(TEXT("Main.Items"), TEXT("Items")))
		{
			return Fail(OutFailureReason);
		}
		if (ActiveSession.HudMode != EProjectIntimacyHudMode::Items)
		{
			return Fail(TEXT("Items menu did not open."));
		}
		if (FindOptionIndex(TEXT("Items.Category.Drugs")) != INDEX_NONE)
		{
			if (!SelectOption(TEXT("Items.Category.Drugs"), TEXT("Items category")))
			{
				return Fail(OutFailureReason);
			}
			RequestBack();
			++OutStepCount;
		}
		RequestBack();
		++OutStepCount;
	}

	SetHudMode(EProjectIntimacyHudMode::Main);
	if (!SelectOption(TEXT("Main.Talk"), TEXT("Talk")))
	{
		return Fail(OutFailureReason);
	}
	if (ActiveSession.HudMode != EProjectIntimacyHudMode::Talk)
	{
		return Fail(TEXT("Talk menu did not open."));
	}

	const FName PreferredTalkCategories[] =
	{
		FName(TEXT("Talk.Category.Neutral")),
		FName(TEXT("Talk.Category.Intensity"))
	};

	FName SelectedTalkCategory = NAME_None;
	for (const FName CategoryId : PreferredTalkCategories)
	{
		if (FindOptionIndex(CategoryId) != INDEX_NONE)
		{
			SelectedTalkCategory = CategoryId;
			break;
		}
	}
	if (SelectedTalkCategory.IsNone())
	{
		return Fail(TEXT("Talk menu did not expose any usable category."));
	}
	if (!SelectOption(SelectedTalkCategory, TEXT("Talk category")))
	{
		return Fail(OutFailureReason);
	}
	if (ActiveSession.ActiveTalkCategoryId.IsNone())
	{
		return Fail(TEXT("Talk category selection did not enter a talk option list."));
	}

	RefreshResolvedOptions();
	int32 TalkOptionIndex = INDEX_NONE;
	if (!ActiveSession.CorrectTalkOptionId.IsNone())
	{
		TalkOptionIndex = FindOptionIndex(ActiveSession.CorrectTalkOptionId);
	}
	if (!ResolvedOptions.IsValidIndex(TalkOptionIndex))
	{
		for (int32 Index = 0; Index < ResolvedOptions.Num(); ++Index)
		{
			const FProjectIntimacyResolvedOption& Option = ResolvedOptions[Index];
			if (Option.OptionId != TEXT("Talk.Category.Back")
				&& Option.OptionId != TEXT("Talk.Back")
				&& Option.TalkAction != EProjectIntimacyTalkAction::Back)
			{
				TalkOptionIndex = Index;
				break;
			}
		}
	}
	if (!ResolvedOptions.IsValidIndex(TalkOptionIndex))
	{
		return Fail(TEXT("Talk category did not expose a playable talk option."));
	}

	ActiveSession.SelectedOptionIndex = TalkOptionIndex;
	RequestConfirm();
	++OutStepCount;
	RequestBack();
	++OutStepCount;
	SetHudMode(EProjectIntimacyHudMode::Main);

	if (!SelectOption(TEXT("Main.Please"), TEXT("Please")))
	{
		return Fail(OutFailureReason);
	}
	if (!ActiveSession.bPleaseActive)
	{
		return Fail(TEXT("Please minigame did not start."));
	}

	const int32 AttemptCount = UProjectIntimacySettings::Get()
		? FMath::Max(1, UProjectIntimacySettings::Get()->PleaseAttemptCount)
		: 5;
	const int32 MaxPresses = AttemptCount + 2;
	for (int32 PressIndex = 0; ActiveSession.bPleaseActive && PressIndex < MaxPresses; ++PressIndex)
	{
		ActiveSession.PleaseCursorValue = ActiveSession.PleaseTargetCenter;
		RequestConfirm();
		++OutStepCount;
	}

	bOutPleaseCompleted = !ActiveSession.bPleaseActive && ActiveSession.PleaseSuccessCount > 0;
	if (!bOutPleaseCompleted)
	{
		return Fail(TEXT("Please minigame did not complete with a successful hit."));
	}

	if (UProjectIntimacyPartnerComponent* PartnerComponent = ActiveSession.PartnerComponent.Get())
	{
		FProjectIntimacyPartnerProfile& Profile = GetMutableProfile(PartnerComponent);
		const int32 PreviousPeakCount = Profile.SessionPeakCount;
		const float RequiredProgress = FMath::Max(
			1.0f,
			ActiveSession.SessionPeakThreshold - ActiveSession.SessionPeak + 1.0f);
		ApplySessionProgress(RequiredProgress, FText::FromString(TEXT("Automation session peak")));
		bOutSessionPeakTriggered = Profile.SessionPeakCount > PreviousPeakCount;
		++OutStepCount;
	}

	if (!bOutSessionPeakTriggered)
	{
		return Fail(TEXT("Automation session peak did not trigger."));
	}

	RefreshResolvedOptions();
	RefreshHudWidget();
	return true;
}
#endif

FProjectIntimacySessionSnapshot UProjectIntimacySubsystem::BuildSnapshot() const
{
	FProjectIntimacySessionSnapshot Snapshot;
	Snapshot.bActive = bSessionActive;
	Snapshot.bHudVisible = bSessionActive && ActiveSession.bHudVisible;
	Snapshot.HudMode = ActiveSession.HudMode;
	Snapshot.SessionProgress = UProjectIntimacySettings::ClampSessionProgress(ActiveSession.SessionProgress);
	Snapshot.SessionPeak = FMath::Clamp(
		ActiveSession.SessionPeakThreshold > 0.0f
			? (ActiveSession.SessionPeak / ActiveSession.SessionPeakThreshold) * 100.0f
			: 0.0f,
		0.0f,
		100.0f);
	Snapshot.SessionProgressPerSecond = ActiveSession.SessionProgressPerSecond;
	Snapshot.RelationshipText = FText::FromString(TEXT("Unknown"));
	Snapshot.GenderText = FText::FromString(TEXT("Unknown"));
	Snapshot.TalkCooldownRemaining = ActiveSession.TalkCooldownRemaining;
	Snapshot.CorrectTalkOptionId = ActiveSession.CorrectTalkOptionId;
	Snapshot.bPleaseActive = ActiveSession.bPleaseActive;
	Snapshot.PleaseAttemptIndex = ActiveSession.PleaseAttemptIndex;
	Snapshot.PleaseAttemptCount = UProjectIntimacySettings::Get()
		? UProjectIntimacySettings::Get()->PleaseAttemptCount
		: 5;
	Snapshot.PleaseSuccessCount = ActiveSession.PleaseSuccessCount;
	Snapshot.PleaseCursorValue = ActiveSession.PleaseCursorValue;
	Snapshot.PleaseTargetCenter = ActiveSession.PleaseTargetCenter;
	Snapshot.PleaseTargetHalfRange = ActiveSession.PleaseTargetHalfRange;
	Snapshot.PleasePreviewProgress = UProjectIntimacySettings::ComputePleaseProgressGain(ActiveSession.PleaseSuccessCount);
	Snapshot.SelectedOptionIndex = ActiveSession.SelectedOptionIndex;
	Snapshot.StatusText = ActiveSession.StatusText;
	Snapshot.HintText = FText::FromString(TEXT("Arrows navigate. Space confirms. '-' hides."));

	if (const UProjectIntimacyPartnerComponent* PartnerComponent = ActiveSession.PartnerComponent.Get())
	{
		Snapshot.PartnerDisplayName = PartnerComponent->GetPartnerDisplayName();
		Snapshot.PartnerId = ActiveSession.PartnerId;
		Snapshot.Personality = PartnerComponent->GetResolvedPersonality();
		Snapshot.EffectivePersonality = ActiveSession.EffectivePersonality;
		Snapshot.GenderTag = PartnerComponent->GetResolvedGenderTag();
		Snapshot.GenderText = UProjectIntimacyPartnerComponent::GenderTagToText(Snapshot.GenderTag);
	}

	if (IntimacySaveGame)
	{
		if (const FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId))
		{
			Snapshot.Relationship = Profile->Relationship;
			Snapshot.RelationshipTags = Profile->RelationshipTags;
			Snapshot.RelationshipText = UProjectIntimacyPartnerComponent::RelationshipTagsToText(Profile->RelationshipTags);
			Snapshot.GenderTag = Snapshot.GenderTag.IsValid() ? Snapshot.GenderTag : Profile->GenderTag;
			Snapshot.GenderText = UProjectIntimacyPartnerComponent::GenderTagToText(Snapshot.GenderTag);
			Snapshot.Affect = Profile->Affect;
			Snapshot.SatisfiedWins = Profile->SatisfiedWins;
			Snapshot.Encounters = Profile->Encounters;
			Snapshot.TotalIntimateTimeSeconds = Profile->TotalIntimateTimeSeconds;
			Snapshot.bProfileHistoryVisible = Profile->bHasFirstEncounter;
		}
	}

	Snapshot.Options.Reserve(ResolvedOptions.Num());
	for (const FProjectIntimacyResolvedOption& Option : ResolvedOptions)
	{
		FProjectIntimacyHudOption& HudOption = Snapshot.Options.AddDefaulted_GetRef();
		HudOption.OptionId = Option.OptionId;
		HudOption.Label = Option.Label;
	}

	return Snapshot;
}

bool UProjectIntimacySubsystem::TryGetPartnerProfile(AActor* PartnerActor, FProjectIntimacyPartnerProfile& OutProfile) const
{
	const UProjectIntimacyPartnerComponent* PartnerComponent = PartnerActor
		? PartnerActor->FindComponentByClass<UProjectIntimacyPartnerComponent>()
		: nullptr;
	if (!PartnerComponent || !IntimacySaveGame)
	{
		return false;
	}

	if (const FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame->PartnerProfiles.Find(PartnerComponent->GetResolvedPartnerId()))
	{
		OutProfile = *Profile;
		return true;
	}

	return false;
}

void UProjectIntimacySubsystem::AppendTargetIntimacyRows(AActor* PartnerActor, FProjectEnemyCombatStatSnapshot& InOutSnapshot)
{
	InOutSnapshot.Rows.Reset();

	UProjectIntimacyPartnerComponent* PartnerComponent = IsValid(PartnerActor)
		? PartnerActor->FindComponentByClass<UProjectIntimacyPartnerComponent>()
		: nullptr;
	if (!PartnerComponent || !PartnerComponent->bSocialCompanion)
	{
		return;
	}

	const FString PartnerId = PartnerComponent->GetResolvedPartnerId();
	LoadPersistentState();
	FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(PartnerId) : nullptr;
	if (Profile)
	{
		NormalizeProfile(PartnerComponent, *Profile);
		RefreshRelationshipTags(*Profile);
	}

	TArray<FProjectSocialCardRow> DefaultSocialRows;
	UProjectIntimacyDialogueLibrary::BuildFallbackSocialCardRows(DefaultSocialRows);

	TSet<FName> SupportedValueIds;
	for (const FProjectSocialCardRow& DefaultRow : DefaultSocialRows)
	{
		SupportedValueIds.Add(!DefaultRow.ValueId.IsNone() ? DefaultRow.ValueId : DefaultRow.RowId);
	}

	TArray<FProjectSocialCardRow> SocialRows;
	if (const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get())
	{
		if (UDataTable* SocialRowsTable = LoadTable(Settings->SocialCardRowsTable))
		{
			if (SocialRowsTable->GetRowStruct() == FProjectSocialCardRow::StaticStruct())
			{
				TArray<FProjectSocialCardRow*> TableRows;
				SocialRowsTable->GetAllRows(TEXT("ProjectSocialCardRows"), TableRows);
				for (const FProjectSocialCardRow* Row : TableRows)
				{
					if (Row)
					{
						const FName RowValueId = !Row->ValueId.IsNone() ? Row->ValueId : Row->RowId;
						if (SupportedValueIds.Contains(RowValueId))
						{
							SocialRows.Add(*Row);
						}
					}
				}
			}
		}
	}

	if (SocialRows.Num() <= 0)
	{
		SocialRows = DefaultSocialRows;
	}
	else if (!SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		const FName ValueId = !Row.ValueId.IsNone() ? Row.ValueId : Row.RowId;
		return ValueId == TEXT("SessionPeakCount");
	}))
	{
		if (const FProjectSocialCardRow* PeakRow = DefaultSocialRows.FindByPredicate([](const FProjectSocialCardRow& Row)
		{
			const FName ValueId = !Row.ValueId.IsNone() ? Row.ValueId : Row.RowId;
			return ValueId == TEXT("SessionPeakCount");
		}))
		{
			SocialRows.Add(*PeakRow);
		}
	}

	SocialRows.Sort([](const FProjectSocialCardRow& Left, const FProjectSocialCardRow& Right)
	{
		if (Left.SortOrder != Right.SortOrder)
		{
			return Left.SortOrder < Right.SortOrder;
		}
		return Left.RowId.LexicalLess(Right.RowId);
	});

	const bool bHasHistory = Profile && Profile->bHasFirstEncounter;
	const EProjectIntimacyPersonality EffectivePersonality = Profile
		? ResolveEffectivePersonality(*Profile, PartnerComponent)
		: PartnerComponent->GetResolvedPersonality();
	const FGameplayTag ComponentGenderTag = PartnerComponent->GetResolvedGenderTag();
	const FGameplayTag GenderTag = ComponentGenderTag.IsValid()
		? ComponentGenderTag
		: (Profile ? Profile->GenderTag : FGameplayTag());

	auto ResolveValue = [this, PartnerComponent, Profile, EffectivePersonality, GenderTag](const FName ValueId, FString& OutValue) -> bool
	{
		if (ValueId == TEXT("SessionProgress"))
		{
			const bool bIsActivePartner = bSessionActive && ActiveSession.PartnerComponent.Get() == PartnerComponent;
			OutValue = FString::Printf(TEXT("%.0f%%"), bIsActivePartner ? ActiveSession.SessionProgress : 0.0f);
			return true;
		}
		if (ValueId == TEXT("Gender"))
		{
			OutValue = UProjectIntimacyPartnerComponent::GenderTagToText(GenderTag).ToString();
			return true;
		}
		if (ValueId == TEXT("Personality"))
		{
			OutValue = UProjectIntimacyPartnerComponent::PersonalityToText(EffectivePersonality).ToString();
			return true;
		}
		if (!Profile)
		{
			return false;
		}
		if (ValueId == TEXT("Relationship"))
		{
			OutValue = UProjectIntimacyPartnerComponent::RelationshipTagsToText(Profile->RelationshipTags).ToString();
			return true;
		}
		if (ValueId == TEXT("Affect"))
		{
			OutValue = FString::Printf(TEXT("%d"), Profile->Affect);
			return true;
		}
		if (ValueId == TEXT("Encounters"))
		{
			OutValue = FString::Printf(TEXT("%d"), Profile->Encounters);
			return true;
		}
		if (ValueId == TEXT("SatisfiedWins"))
		{
			OutValue = FString::Printf(TEXT("%d"), Profile->SatisfiedWins);
			return true;
		}
		if (ValueId == TEXT("SessionPeakCount"))
		{
			OutValue = FString::Printf(TEXT("%d"), Profile->SessionPeakCount);
			return true;
		}
		if (ValueId == TEXT("FirstEncounter"))
		{
			OutValue = Profile->FirstEncounterUtc.GetTicks() > 0
				? Profile->FirstEncounterUtc.ToString(TEXT("%Y-%m-%d"))
				: TEXT("--");
			return true;
		}
		if (ValueId == TEXT("TotalIntimateTime"))
		{
			OutValue = ProjectIntimacySubsystemPrivate::FormatDuration(Profile->TotalIntimateTimeSeconds);
			return true;
		}
		return false;
	};

	TSet<FName> AddedValueIds;
	for (const FProjectSocialCardRow& SocialRow : SocialRows)
	{
		if (!SocialRow.bEnabled)
		{
			continue;
		}
		if (bHasHistory)
		{
			if (!SocialRow.bShowAfterFirstEncounter)
			{
				continue;
			}
		}
		else if (!SocialRow.bShowBeforeFirstEncounter)
		{
			continue;
		}

		const FName ValueId = !SocialRow.ValueId.IsNone() ? SocialRow.ValueId : SocialRow.RowId;
		if (!SupportedValueIds.Contains(ValueId) || AddedValueIds.Contains(ValueId))
		{
			continue;
		}

		FString Value;
		if (!ResolveValue(ValueId, Value))
		{
			continue;
		}

		AddedValueIds.Add(ValueId);
		FProjectEnemyCombatStatRow& Row = InOutSnapshot.Rows.AddDefaulted_GetRef();
		Row.Label = !SocialRow.Label.IsEmpty() ? SocialRow.Label : FText::FromName(ValueId);
		Row.Section = NAME_None;
		Row.ValueOverride = FText::FromString(Value);
		Row.bIsAvailable = true;
	}
}

bool UProjectIntimacySubsystem::BuildTargetSocialCardSnapshot(AActor* PartnerActor, FProjectSocialCardSnapshot& OutSnapshot)
{
	OutSnapshot.Rows.Reset();

	FProjectEnemyCombatStatSnapshot LegacySnapshot;
	AppendTargetIntimacyRows(PartnerActor, LegacySnapshot);
	OutSnapshot.Rows = MoveTemp(LegacySnapshot.Rows);
	return OutSnapshot.Rows.Num() > 0;
}

void UProjectIntimacySubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController != TrackedPlayerController)
	{
		AttachToPlayerController(PlayerController);
	}

	TrackedPlayerPawn = TrackedPlayerController ? TrackedPlayerController->GetPawn() : nullptr;
	TrackedEmoteComponent = TrackedPlayerPawn ? TrackedPlayerPawn->FindComponentByClass<UProjectEmoteComponent>() : nullptr;
	TrackedTargetingFixComponent = TrackedPlayerPawn ? TrackedPlayerPawn->FindComponentByClass<UProjectTargetingFixComponent>() : nullptr;
	EnsureHubSocialProductRoute();
}

bool UProjectIntimacySubsystem::IsHubSocialProductMap() const
{
	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const FName RequiredMap = Settings ? Settings->HubSocialMapName : FName(TEXT("HUB"));
	return !RequiredMap.IsNone()
		&& UGameplayStatics::GetCurrentLevelName(this, true).Equals(
			RequiredMap.ToString(),
			ESearchCase::IgnoreCase);
}

void UProjectIntimacySubsystem::EnsureHubSocialProductRoute()
{
	UWorld* World = GetWorld();
	if (!World
		|| !World->IsGameWorld()
		|| !IsHubSocialProductMap()
		|| !IsValid(TrackedPlayerPawn.Get()))
	{
		return;
	}

	if (IsValid(HubSocialCompanionActor.Get()))
	{
		if (HubSocialRegisteredPlayer.Get() != TrackedPlayerPawn.Get())
		{
			if (const UProjectIntimacyPartnerComponent* PartnerComponent =
				HubSocialCompanionActor->FindComponentByClass<UProjectIntimacyPartnerComponent>())
			{
				RegisterSocialParticipants(HubSocialCompanionActor.Get(), PartnerComponent, false);
				HubSocialRegisteredPlayer = TrackedPlayerPawn.Get();
			}
		}
		return;
	}

	if (bHubSocialProductRouteAttempted || !TrackedPlayerPawn->HasAuthority())
	{
		return;
	}
	bHubSocialProductRouteAttempted = true;

	AActor* CompanionActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		const UProjectIntimacyPartnerComponent* CandidatePartner =
			IsValid(Candidate)
				? Candidate->FindComponentByClass<UProjectIntimacyPartnerComponent>()
				: nullptr;
		if (Candidate
			&& Candidate->ActorHasTag(ProjectIntimacySubsystemPrivate::HubSocialCompanionTag)
			&& CandidatePartner
			&& CandidatePartner->bSocialCompanion
			&& CandidatePartner->bOffersPlayerInitiatedConsent)
		{
			CompanionActor = Candidate;
			break;
		}
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	bool bSpawnedCompanion = false;
	if (!CompanionActor)
	{
		if (!Settings || !Settings->bAutoSpawnHubSocialCompanion)
		{
			UE_LOG(
				LogProjectIntimacy,
				Verbose,
				TEXT("[ProjectIntimacy] HUB has no authored social companion; automatic companion spawning is disabled."));
			return;
		}

		UClass* CompanionClass = Settings
			? Settings->HubSocialCompanionClass.TryLoadClass<APawn>()
			: nullptr;
		if (!CompanionClass)
		{
			UE_LOG(
				LogProjectIntimacy,
				Error,
				TEXT("[ProjectIntimacy] HUB social route unavailable: companion class could not be loaded."));
			return;
		}

		const FVector LocalOffset = Settings
			? Settings->HubSocialCompanionOffset
			: FVector(240.0f, 140.0f, 0.0f);
		const FTransform PlayerTransform = TrackedPlayerPawn->GetActorTransform();
		const FVector SpawnLocation =
			PlayerTransform.GetLocation() + PlayerTransform.TransformVectorNoScale(LocalOffset);
		const FTransform SpawnTransform(
			PlayerTransform.GetRotation(),
			SpawnLocation,
			FVector::OneVector);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = TrackedPlayerPawn.Get();
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		CompanionActor = World->SpawnActor<AActor>(
			CompanionClass,
			SpawnTransform,
			SpawnParameters);
		bSpawnedCompanion = IsValid(CompanionActor);
	}

	if (!IsValid(CompanionActor))
	{
		UE_LOG(
			LogProjectIntimacy,
			Error,
			TEXT("[ProjectIntimacy] HUB social route unavailable: companion spawn failed."));
		return;
	}

	UProjectIntimacyPartnerComponent* PartnerComponent =
		CompanionActor->FindComponentByClass<UProjectIntimacyPartnerComponent>();
	if (!PartnerComponent && bSpawnedCompanion)
	{
		PartnerComponent = UProjectIntimacyPartnerComponent::FindOrCreateForActor(CompanionActor);
	}
	if (!PartnerComponent)
	{
		if (bSpawnedCompanion)
		{
			CompanionActor->Destroy();
		}
		UE_LOG(
			LogProjectIntimacy,
			Error,
			TEXT("[ProjectIntimacy] HUB social route unavailable: explicit partner component missing."));
		return;
	}

	if (bSpawnedCompanion)
	{
		CompanionActor->Tags.AddUnique(ProjectIntimacySubsystemPrivate::HubSocialCompanionTag);
		PartnerComponent->PartnerId = TEXT("Companion.Hub.Adult01");
		PartnerComponent->DisplayNameOverride = FText::FromString(TEXT("Rowan"));
		PartnerComponent->InitialAffect = Settings
			? FProjectSocialRules::ClampAffinity(Settings->HubSocialCompanionAffinity)
			: 50;
		PartnerComponent->bAdultVerified = true;
		PartnerComponent->bExplicitConsent = false;
		PartnerComponent->bConscious = true;
		PartnerComponent->bNonHostileVerified = true;
		PartnerComponent->bOutsideCombat = true;
		PartnerComponent->bIntimacyZoneAllowed = false;
		PartnerComponent->bSocialCompanion = true;
		PartnerComponent->bOffersPlayerInitiatedConsent = true;
	}

	AActor* ZoneActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		const UProjectIntimacyZoneComponent* CandidateZone =
			IsValid(Candidate)
				? Candidate->FindComponentByClass<UProjectIntimacyZoneComponent>()
				: nullptr;
		if (Candidate
			&& Candidate->ActorHasTag(ProjectIntimacySubsystemPrivate::HubSocialZoneTag)
			&& CandidateZone
			&& CandidateZone->bAllowsIntimacy)
		{
			ZoneActor = Candidate;
			break;
		}
	}

	if (!ZoneActor && bSpawnedCompanion)
	{
		FActorSpawnParameters ZoneSpawnParameters;
		ZoneSpawnParameters.Owner = CompanionActor;
		ZoneSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ZoneActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			CompanionActor->GetActorTransform(),
			ZoneSpawnParameters);
		if (ZoneActor)
		{
			UProjectIntimacyZoneComponent* ZoneComponent =
				NewObject<UProjectIntimacyZoneComponent>(
					ZoneActor,
					UProjectIntimacyZoneComponent::StaticClass(),
					TEXT("ProjectIntimacyAllowedZone"));
			if (ZoneComponent)
			{
				ZoneComponent->bAllowsIntimacy = true;
				ZoneComponent->AllowedRadius = Settings
					? FMath::Max(100.0f, Settings->HubSocialZoneRadius)
					: 650.0f;
				ZoneActor->AddInstanceComponent(ZoneComponent);
				ZoneActor->SetRootComponent(ZoneComponent);
				ZoneComponent->RegisterComponent();
				ZoneComponent->SetWorldLocation(CompanionActor->GetActorLocation());
				ZoneActor->Tags.AddUnique(ProjectIntimacySubsystemPrivate::HubSocialZoneTag);
			}
		}
	}

	const UProjectIntimacyZoneComponent* AllowedZone = ZoneActor
		? ZoneActor->FindComponentByClass<UProjectIntimacyZoneComponent>()
		: nullptr;
	if (!AllowedZone || !AllowedZone->bAllowsIntimacy)
	{
		if (bSpawnedCompanion)
		{
			CompanionActor->Destroy();
		}
		if (ZoneActor && ZoneActor->GetOwner() == CompanionActor)
		{
			ZoneActor->Destroy();
		}
		UE_LOG(
			LogProjectIntimacy,
			Error,
			TEXT("[ProjectIntimacy] HUB social route unavailable: explicit allowed zone missing."));
		return;
	}

	HubSocialCompanionActor = CompanionActor;
	HubSocialZoneActor = ZoneActor;
	RegisterSocialParticipants(CompanionActor, PartnerComponent, false);
	HubSocialRegisteredPlayer = TrackedPlayerPawn.Get();

	if (AController* CompanionController = Cast<APawn>(CompanionActor)
		? Cast<APawn>(CompanionActor)->GetController()
		: nullptr)
	{
		CompanionController->StopMovement();
	}

	UE_LOG(
		LogProjectIntimacy,
		Log,
		TEXT("[ProjectIntimacy] HUB social route ready companion=%s zone=%s required_charisma=%d."),
		*GetNameSafe(CompanionActor),
		*GetNameSafe(ZoneActor),
		FProjectContentPolicyRules::MatureUnlockCharismaLevel);
}

void UProjectIntimacySubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	DetachFromTrackedPlayerController();
	TrackedPlayerController = PlayerController;
	if (TrackedPlayerController && TrackedPlayerController->IsLocalController())
	{
		BindInputToTrackedPlayerController();
	}
}

void UProjectIntimacySubsystem::DetachFromTrackedPlayerController()
{
	UnbindInputFromTrackedPlayerController();
	if (HudWidget)
	{
		HudWidget->RemoveFromParent();
		HudWidget = nullptr;
	}
	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedEmoteComponent = nullptr;
	TrackedTargetingFixComponent = nullptr;
}

void UProjectIntimacySubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || IntimacyInputComponent)
	{
		return;
	}

	IntimacyInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectIntimacyInputComponent"));
	if (!IntimacyInputComponent)
	{
		return;
	}

	IntimacyInputComponent->bBlockInput = false;
	IntimacyInputComponent->Priority = ProjectIntimacySubsystemPrivate::InputPriority;
	IntimacyInputComponent->RegisterComponent();

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	auto BindKey = [this](const FKey& Key, void (UProjectIntimacySubsystem::*Handler)())
	{
		if (Key.IsValid())
		{
			FInputKeyBinding& Binding = IntimacyInputComponent->BindKey(Key, IE_Pressed, this, Handler);
			Binding.bConsumeInput = true;
		}
	};

	BindKey(Settings ? Settings->HudToggleKey : EKeys::Hyphen, &ThisClass::HandleToggleHudPressed);
	BindKey(Settings ? Settings->HudSecondaryToggleKey : EKeys::Subtract, &ThisClass::HandleToggleHudPressed);
	BindKey(EKeys::Up, &ThisClass::HandleNavigateUpPressed);
	BindKey(EKeys::Down, &ThisClass::HandleNavigateDownPressed);
	BindKey(EKeys::Left, &ThisClass::HandleNavigateLeftPressed);
	BindKey(EKeys::Right, &ThisClass::HandleNavigateRightPressed);
	BindKey(EKeys::SpaceBar, &ThisClass::HandleConfirmPressed);
	BindKey(EKeys::Enter, &ThisClass::HandleConfirmPressed);

	TrackedPlayerController->PushInputComponent(IntimacyInputComponent);
}

void UProjectIntimacySubsystem::UnbindInputFromTrackedPlayerController()
{
	if (TrackedPlayerController && IntimacyInputComponent)
	{
		TrackedPlayerController->PopInputComponent(IntimacyInputComponent);
	}

	if (IntimacyInputComponent && IntimacyInputComponent->IsRegistered())
	{
		IntimacyInputComponent->DestroyComponent();
	}
	IntimacyInputComponent = nullptr;
}

bool UProjectIntimacySubsystem::ResolveActiveIntimacyPartner(
	AActor*& OutPartnerActor,
	UProjectIntimacyPartnerComponent*& OutPartnerComponent) const
{
	OutPartnerActor = nullptr;
	OutPartnerComponent = nullptr;

	if (!TrackedPlayerPawn || !TrackedEmoteComponent || !TrackedEmoteComponent->IsEmoteActive())
	{
		return false;
	}

	if (TrackedEmoteComponent->GetActiveInteractionId() != ProjectIntimacySubsystemPrivate::IntimacySceneId)
	{
		return false;
	}

	OutPartnerActor = TrackedEmoteComponent->GetActiveBlueprintSceneTargetActor();
	if (!OutPartnerActor && TrackedTargetingFixComponent)
	{
		OutPartnerActor = TrackedTargetingFixComponent->GetCurrentTargetActor();
	}

	if (!IsValid(OutPartnerActor) || OutPartnerActor == TrackedPlayerPawn.Get())
	{
		return false;
	}

	OutPartnerComponent = OutPartnerActor->FindComponentByClass<UProjectIntimacyPartnerComponent>();
	return OutPartnerComponent
		&& OutPartnerComponent->bSocialCompanion
		&& OutPartnerComponent->bOffersPlayerInitiatedConsent;
}

void UProjectIntimacySubsystem::StartSession(AActor* PartnerActor, UProjectIntimacyPartnerComponent* PartnerComponent)
{
	if (!PartnerActor || !PartnerComponent)
	{
		return;
	}

	FText EligibilityFailure;
	RegisterSocialParticipants(PartnerActor, PartnerComponent, false);
	if (!CanRequestIntimacyWithPartner(PartnerActor, EligibilityFailure))
	{
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Session request denied: %s"),
			*EligibilityFailure.ToString());
		bSuppressStartUntilSceneEnds = true;
		if (UWorld* World = GetWorld())
		{
			if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
			{
				EmoteSubsystem->RequestCancelActiveEmote();
			}
		}
		ClearConsentForPartner(PartnerActor);
		return;
	}

	RegisterSocialParticipants(PartnerActor, PartnerComponent, true);
	if (!CanStartIntimacyWithPartner(PartnerActor, EligibilityFailure))
	{
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Session denied: %s"),
			*EligibilityFailure.ToString());
		bSuppressStartUntilSceneEnds = true;
		if (UWorld* World = GetWorld())
		{
			if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
			{
				EmoteSubsystem->RequestCancelActiveEmote();
			}
		}
		ClearConsentForPartner(PartnerActor);
		return;
	}

	if (!EnsureMaturePresentationRegistered(PartnerActor, EligibilityFailure))
	{
		UE_LOG(
			LogProjectIntimacy,
			Warning,
			TEXT("[ProjectIntimacy] Session denied by the mature-content presentation authority: %s"),
			*EligibilityFailure.ToString());
		bSuppressStartUntilSceneEnds = true;
		if (UWorld* World = GetWorld())
		{
			if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
			{
				EmoteSubsystem->RequestCancelActiveEmote();
			}
		}
		ClearConsentForPartner(PartnerActor);
		return;
	}

	ActiveSession = FProjectIntimacyRuntimeSession();
	ActiveSession.PartnerActor = PartnerActor;
	ActiveSession.PartnerComponent = PartnerComponent;
	ActiveSession.PartnerId = PartnerComponent->GetResolvedPartnerId();
	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	ActiveSession.SessionProgress = 0.0f;
	ActiveSession.SessionProgressPerSecond = Settings
		? FMath::Max(0.0f, Settings->PassiveSessionProgressPerSecond)
		: 1.0f;
	ActiveSession.SessionPeakThreshold = Settings
		? FMath::Clamp(Settings->SessionPeakThreshold, 1.0f, 100.0f)
		: 25.0f;
	ActiveSession.SessionPeak = 0.0f;
	ActiveSession.SessionPeakMultiplier = 1.0f;
	ActiveSession.SessionPeakRecoveryRemaining = 0.0f;
	ActiveSession.StatusText = FText::FromString(TEXT("Session started."));

	FProjectIntimacyPartnerProfile& Profile = GetMutableProfile(PartnerComponent);
	Profile.Encounters += 1;
	Profile.bHasFirstEncounter = true;
	if (Profile.FirstEncounterUtc.GetTicks() == 0)
	{
		Profile.FirstEncounterUtc = FDateTime::UtcNow();
	}
	RefreshRelationshipTags(Profile);
	UnlockAutomaticTattooReward(TestTattooIntimacyRewardId);
	ActiveSession.EffectivePersonality = ResolveEffectivePersonality(Profile, PartnerComponent);
	switch (ActiveSession.EffectivePersonality)
	{
	case EProjectIntimacyPersonality::Chill:
		ActiveSession.AnimationRate = Settings ? Settings->ChillAnimationRate : 0.75f;
		break;
	case EProjectIntimacyPersonality::Stallion:
		ActiveSession.AnimationRate = Settings ? Settings->StallionAnimationRate : 1.35f;
		break;
	case EProjectIntimacyPersonality::Nice:
	case EProjectIntimacyPersonality::Auto:
	default:
		ActiveSession.AnimationRate = Settings ? Settings->NiceAnimationRate : 1.0f;
		break;
	}

	bSessionActive = true;
	ApplyAnimationRate(ActiveSession.AnimationRate);
	RefreshResolvedOptions();
	EnsureHudWidget();
	RefreshHudWidget();
	SavePersistentState();
}

void UProjectIntimacySubsystem::UpdateActiveSession(const float DeltaTime)
{
	if (!bSessionActive)
	{
		return;
	}

	FText EligibilityFailure;
	if (!CanStartIntimacyWithPartner(ActiveSession.PartnerActor.Get(), EligibilityFailure))
	{
		UE_LOG(
			LogProjectIntimacy,
			Display,
			TEXT("[ProjectIntimacy] Session cancelled safely: %s"),
			*EligibilityFailure.ToString());
		CancelActiveSession();
		return;
	}

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	ActiveSession.SessionTimeSeconds += DeltaTime;
	ActiveSession.TalkCooldownRemaining = FMath::Max(0.0f, ActiveSession.TalkCooldownRemaining - DeltaTime);

	if (FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
	{
		Profile->TotalIntimateTimeSeconds += DeltaTime;
	}

	ApplySessionProgress(ActiveSession.SessionProgressPerSecond * DeltaTime, FText());
	UpdateSessionPeakIntensity(DeltaTime);

	if (ActiveSession.bPleaseActive)
	{
		ActiveSession.PleaseElapsedSeconds += DeltaTime;
		const float Period = FMath::Max(0.01f, ActiveSession.PleasePulsePeriod);
		ActiveSession.PleaseCursorValue = 0.5f + 0.5f * FMath::Sin((ActiveSession.PleaseElapsedSeconds / Period) * UE_TWO_PI);
	}

	ApplyAnimationRate(ActiveSession.AnimationRate);

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const float ProgressMaximum = Settings
		? FMath::Clamp(Settings->SessionProgressMaximum, 1.0f, 100.0f)
		: 100.0f;
	if (ActiveSession.SessionProgress >= ProgressMaximum && !ActiveSession.bSatisfied)
	{
		FinishSatisfiedSession();
		return;
	}

	RefreshHudWidget();
}

void UProjectIntimacySubsystem::EndSession(const bool bCancelled)
{
	(void)bCancelled;
	if (!bSessionActive)
	{
		return;
	}

	if (FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
	{
		RefreshRelationshipTags(*Profile);
	}
	RestoreAnimationRates();
	SavePersistentState();
	ClearActiveSessionConsent();
	EndMaturePresentationRegistration(true);
	bSessionActive = false;
	ActiveSession = FProjectIntimacyRuntimeSession();
	ResolvedOptions.Reset();
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::FinishSatisfiedSession()
{
	if (!bSessionActive || ActiveSession.bSatisfied)
	{
		return;
	}

	ActiveSession.bSatisfied = true;
	if (FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
	{
		const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
		Profile->SatisfiedWins += 1;
		Profile->Affect = FMath::Min(
			Settings ? FMath::Max(1, Settings->AffectMax) : 100,
			Profile->Affect + (Settings ? Settings->SatisfiedWinAffectGain : 10));
		RefreshRelationshipTags(*Profile);
	}

	bSuppressStartUntilSceneEnds = true;

	if (UWorld* World = GetWorld())
	{
		if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
		{
			EmoteSubsystem->RequestCancelActiveEmote();
		}
	}

	EndSession(false);
}

void UProjectIntimacySubsystem::CancelActiveSession()
{
	if (!bSessionActive)
	{
		return;
	}

	bSuppressStartUntilSceneEnds = true;
	if (UWorld* World = GetWorld())
	{
		if (UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
		{
			EmoteSubsystem->RequestCancelActiveEmote();
		}
	}

	EndSession(true);
}

void UProjectIntimacySubsystem::EnsureHudWidget()
{
	if (!TrackedPlayerController || HudWidget)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const TSubclassOf<UProjectIntimacyHudWidget> WidgetClass =
		ProjectWidgetClassResolver::ResolveWidgetClass<UProjectIntimacyHudWidget>(
			Settings ? Settings->IntimacyHudWidgetClass : FSoftClassPath(),
			TEXT("ProjectIntimacyHudWidget"));
	TSubclassOf<UUserWidget> ResolvedWidgetClass = WidgetClass;
	if (!ResolvedWidgetClass)
	{
		ResolvedWidgetClass = UProjectIntimacyHudWidget::StaticClass();
	}

	HudWidget = CreateWidget<UProjectIntimacyHudWidget>(
		TrackedPlayerController,
		ResolvedWidgetClass,
		TEXT("ProjectIntimacyHudWidget"));
	if (!HudWidget)
	{
		UE_LOG(LogProjectIntimacy, Warning, TEXT("[Intimacy] Failed to create HUD widget."));
		return;
	}

	const int32 ZOrder = Settings ? Settings->IntimacyHudZOrder : 325;
	if (!HudWidget->AddToPlayerScreen(ZOrder))
	{
		HudWidget->AddToViewport(ZOrder);
	}
}

void UProjectIntimacySubsystem::RefreshHudWidget()
{
	if (HudWidget)
	{
		HudWidget->SetSnapshot(BuildSnapshot());
	}
}

void UProjectIntimacySubsystem::RefreshResolvedOptions()
{
	ResolvedOptions.Reset();

	if (!bSessionActive)
	{
		return;
	}

	if (ActiveSession.HudMode == EProjectIntimacyHudMode::Main)
	{
		ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Main.Please"), TEXT("Please")));
		ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Main.Talk"), TEXT("Talk")));
		ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Main.Items"), TEXT("Items")));
		ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Main.Cancel"), TEXT("Cancel")));
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Talk)
	{
		if (ActiveSession.ActiveTalkCategoryId.IsNone())
		{
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Talk.Category.Intensity"), TEXT("Intensity")));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Talk.Category.Neutral"), TEXT("Neutral")));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Talk.Back"), TEXT("Back")));
		}
		else
		{
			const FGameplayTag PersonalityTag = UProjectIntimacyDialogueLibrary::GetPersonalityTag(ActiveSession.EffectivePersonality);

			TArray<FProjectIntimacyTalkOptionRow> TalkRows;
			if (const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get())
			{
				if (UDataTable* TalkTable = LoadTable(Settings->TalkOptionsTable))
				{
					TArray<FProjectIntimacyTalkOptionRow*> TableRows;
					TalkTable->GetAllRows(TEXT("ProjectIntimacyTalkOptions"), TableRows);
					for (const FProjectIntimacyTalkOptionRow* Row : TableRows)
					{
						if (Row)
						{
							TalkRows.Add(*Row);
						}
					}
				}
			}

			if (TalkRows.Num() <= 0)
			{
				UProjectIntimacyDialogueLibrary::BuildFallbackTalkOptions(TalkRows);
			}

			int32 AddedRows = 0;
			for (const FProjectIntimacyTalkOptionRow& Row : TalkRows)
			{
				if (!ProjectIntimacySubsystemPrivate::IsSelectableTalkAction(Row.Action))
				{
					continue;
				}
				if (Row.CategoryId != ActiveSession.ActiveTalkCategoryId)
				{
					continue;
				}
				if (Row.RequiredPersonalityTag.IsValid()
					&& PersonalityTag.IsValid()
					&& Row.RequiredPersonalityTag != PersonalityTag)
				{
					continue;
				}
				FProjectIntimacyResolvedOption& Option = ResolvedOptions.AddDefaulted_GetRef();
				Option.OptionId = !Row.OptionId.IsNone() ? Row.OptionId : FName(*UEnum::GetValueAsString(Row.Action));
				Option.Label = !Row.Label.IsEmpty() ? Row.Label : FText::FromName(Option.OptionId);
				Option.TalkAction = Row.Action;
				Option.CategoryId = Row.CategoryId;
				Option.TalkTags = Row.TalkTags;
				Option.SessionProgressGain = Row.SessionProgressGain;
				Option.AffectDelta = Row.AffectDelta;
				Option.AnimationRate = Row.AnimationRate;
				Option.bCanBeCorrectTalkOption = Row.bCanBeCorrectTalkOption;
				Option.bUsesTalkCooldown = Row.bUsesTalkCooldown;
				Option.bCanBeFlavorCorrectOption = Row.bCanBeFlavorCorrectOption;
				++AddedRows;
			}

			if (AddedRows <= 0)
			{
				TArray<FProjectIntimacyTalkOptionRow> FallbackRows;
				UProjectIntimacyDialogueLibrary::BuildFallbackTalkOptions(FallbackRows);
				for (const FProjectIntimacyTalkOptionRow& Row : FallbackRows)
				{
					if (!ProjectIntimacySubsystemPrivate::IsSelectableTalkAction(Row.Action))
					{
						continue;
					}
					if (Row.CategoryId != ActiveSession.ActiveTalkCategoryId)
					{
						continue;
					}

					FProjectIntimacyResolvedOption& Option = ResolvedOptions.AddDefaulted_GetRef();
					Option.OptionId = Row.OptionId;
					Option.Label = Row.Label;
					Option.TalkAction = Row.Action;
					Option.CategoryId = Row.CategoryId;
					Option.TalkTags = Row.TalkTags;
					Option.SessionProgressGain = Row.SessionProgressGain;
					Option.AffectDelta = Row.AffectDelta;
					Option.AnimationRate = Row.AnimationRate;
					Option.bCanBeCorrectTalkOption = Row.bCanBeCorrectTalkOption;
					Option.bUsesTalkCooldown = Row.bUsesTalkCooldown;
					Option.bCanBeFlavorCorrectOption = Row.bCanBeFlavorCorrectOption;
				}
			}

			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Talk.Category.Back"), TEXT("Back")));
		}
	}
	else if (ActiveSession.HudMode == EProjectIntimacyHudMode::Items)
	{
		if (ActiveSession.ActiveItemCategoryId.IsNone())
		{
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Items.Category.Drugs"), TEXT("Drugs")));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Items.Category.Toys"), TEXT("Toys")));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Items.Back"), TEXT("Back")));
		}
		else
		{
			const FString CategoryName = ActiveSession.ActiveItemCategoryId == TEXT("Items.Category.Toys")
				? FString(TEXT("toys"))
				: FString(TEXT("drugs"));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(
				TEXT("Items.Empty"),
				*FString::Printf(TEXT("No %s yet"), *CategoryName)));
			ResolvedOptions.Add(ProjectIntimacySubsystemPrivate::MakeOption(TEXT("Items.Category.Back"), TEXT("Back")));
		}
	}

	ActiveSession.SelectedOptionIndex = FMath::Clamp(ActiveSession.SelectedOptionIndex, 0, FMath::Max(0, ResolvedOptions.Num() - 1));
	if (ActiveSession.HudMode == EProjectIntimacyHudMode::Talk && !ActiveSession.ActiveTalkCategoryId.IsNone())
	{
		ChooseCorrectTalkOption();
	}
	else
	{
		ActiveSession.CorrectTalkOptionId = NAME_None;
	}
}

void UProjectIntimacySubsystem::ChooseCorrectTalkOption()
{
	if (!bSessionActive || ActiveSession.HudMode != EProjectIntimacyHudMode::Talk)
	{
		ActiveSession.CorrectTalkOptionId = NAME_None;
		return;
	}

	FGameplayTagContainer RelationshipTags;
	if (const FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
	{
		RelationshipTags = Profile->RelationshipTags;
	}

	FGameplayTagContainer PreferredTags;
	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	if (Settings)
	{
		if (UDataTable* AffinityTable = LoadTable(Settings->TalkAffinityTable))
		{
			TArray<FProjectIntimacyTalkAffinityRow*> Rows;
			AffinityTable->GetAllRows(TEXT("ProjectIntimacyTalkAffinity"), Rows);
			for (const FProjectIntimacyTalkAffinityRow* Row : Rows)
			{
				if (!Row)
				{
					continue;
				}
				if (Row->Personality != EProjectIntimacyPersonality::Auto && Row->Personality != ActiveSession.EffectivePersonality)
				{
					continue;
				}
				if (Row->RelationshipTag.IsValid() && !RelationshipTags.HasTagExact(Row->RelationshipTag))
				{
					continue;
				}
				PreferredTags.AppendTags(Row->PreferredTalkTags);
			}
		}
	}

	if (PreferredTags.Num() <= 0)
	{
		UProjectIntimacyDialogueLibrary::BuildPreferredTalkTags(
			ActiveSession.EffectivePersonality,
			RelationshipTags,
			PreferredTags);
	}

	int32 BestScore = INDEX_NONE;
	TArray<int32> BestOptionIndexes;
	for (int32 Index = 0; Index < ResolvedOptions.Num(); ++Index)
	{
		const FProjectIntimacyResolvedOption& Option = ResolvedOptions[Index];
		if (!Option.bCanBeFlavorCorrectOption || Option.TalkAction == EProjectIntimacyTalkAction::Back)
		{
			continue;
		}

		FProjectIntimacyTalkOptionRow ScoreRow;
		ScoreRow.TalkTags = Option.TalkTags;
		const int32 Score = UProjectIntimacyDialogueLibrary::ScoreTalkOptionForPreferredTags(ScoreRow, PreferredTags);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestOptionIndexes.Reset();
			BestOptionIndexes.Add(Index);
		}
		else if (Score == BestScore)
		{
			BestOptionIndexes.Add(Index);
		}
	}

	if (BestOptionIndexes.Num() <= 0)
	{
		ActiveSession.CorrectTalkOptionId = NAME_None;
		return;
	}

	const int32 PickedIndex = BestOptionIndexes[RandomStream.RandRange(0, BestOptionIndexes.Num() - 1)];
	ActiveSession.CorrectTalkOptionId = ResolvedOptions.IsValidIndex(PickedIndex)
		? ResolvedOptions[PickedIndex].OptionId
		: NAME_None;
}

void UProjectIntimacySubsystem::SetHudMode(const EProjectIntimacyHudMode NewMode)
{
	ActiveSession.HudMode = NewMode;
	if (NewMode != EProjectIntimacyHudMode::Talk)
	{
		ActiveSession.ActiveTalkCategoryId = NAME_None;
	}
	if (NewMode != EProjectIntimacyHudMode::Items)
	{
		ActiveSession.ActiveItemCategoryId = NAME_None;
	}
	ActiveSession.SelectedOptionIndex = 0;
	ActiveSession.bPleaseActive = NewMode == EProjectIntimacyHudMode::Please && ActiveSession.bPleaseActive;
	RefreshResolvedOptions();
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::HandleMainOption(const FName OptionId)
{
	if (OptionId == TEXT("Main.Please"))
	{
		StartPlease();
	}
	else if (OptionId == TEXT("Main.Talk"))
	{
		SetHudMode(EProjectIntimacyHudMode::Talk);
	}
	else if (OptionId == TEXT("Main.Items"))
	{
		SetHudMode(EProjectIntimacyHudMode::Items);
	}
	else if (OptionId == TEXT("Main.Cancel"))
	{
		CancelActiveSession();
	}
}

void UProjectIntimacySubsystem::HandleTalkOption(const FProjectIntimacyResolvedOption& Option)
{
	if (Option.OptionId == TEXT("Talk.Back"))
	{
		SetHudMode(EProjectIntimacyHudMode::Main);
		return;
	}

	if (Option.OptionId == TEXT("Talk.Category.Back"))
	{
		ActiveSession.ActiveTalkCategoryId = NAME_None;
		ActiveSession.SelectedOptionIndex = 0;
		ActiveSession.StatusText = FText::FromString(TEXT("Choose a Talk style."));
		RefreshResolvedOptions();
		RefreshHudWidget();
		return;
	}

	if (ActiveSession.ActiveTalkCategoryId.IsNone() && Option.OptionId.ToString().StartsWith(TEXT("Talk.Category.")))
	{
		ActiveSession.ActiveTalkCategoryId = Option.OptionId;
		ActiveSession.SelectedOptionIndex = 0;
		ActiveSession.StatusText = FText::FromString(FString::Printf(
			TEXT("%s selected."),
			*Option.Label.ToString()));
		RefreshResolvedOptions();
		RefreshHudWidget();
		return;
	}

	ExecuteTalkOption(Option);
}

void UProjectIntimacySubsystem::HandleItemsOption(const FProjectIntimacyResolvedOption& Option)
{
	if (Option.OptionId == TEXT("Items.Back"))
	{
		SetHudMode(EProjectIntimacyHudMode::Main);
		return;
	}

	if (Option.OptionId == TEXT("Items.Category.Back"))
	{
		ActiveSession.ActiveItemCategoryId = NAME_None;
		ActiveSession.SelectedOptionIndex = 0;
		ActiveSession.StatusText = FText::FromString(TEXT("Choose an item category."));
		RefreshResolvedOptions();
		RefreshHudWidget();
		return;
	}

	if (ActiveSession.ActiveItemCategoryId.IsNone()
		&& (Option.OptionId == TEXT("Items.Category.Drugs") || Option.OptionId == TEXT("Items.Category.Toys")))
	{
		ActiveSession.ActiveItemCategoryId = Option.OptionId;
		ActiveSession.SelectedOptionIndex = 0;
		ActiveSession.StatusText = Option.OptionId == TEXT("Items.Category.Toys")
			? FText::FromString(TEXT("No toys configured yet."))
			: FText::FromString(TEXT("No drugs configured yet."));
		RefreshResolvedOptions();
		RefreshHudWidget();
		return;
	}

	ActiveSession.StatusText = FText::FromString(TEXT("No intimate items configured yet."));
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::ExecuteTalkOption(const FProjectIntimacyResolvedOption& Option)
{
	if (Option.TalkAction == EProjectIntimacyTalkAction::Back)
	{
		SetHudMode(EProjectIntimacyHudMode::Main);
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	if (Option.bUsesTalkCooldown && ActiveSession.TalkCooldownRemaining > 0.0f)
	{
		ActiveSession.StatusText = FText::FromString(FString::Printf(
			TEXT("Talk cooldown %.1fs."),
			ActiveSession.TalkCooldownRemaining));
		RefreshHudWidget();
		return;
	}

	if (Option.bUsesTalkCooldown)
	{
		ActiveSession.TalkCooldownRemaining = Settings ? FMath::Max(0.0f, Settings->TalkCooldownSeconds) : 2.0f;
	}

	const float RefusalChance = Settings ? FMath::Clamp(Settings->TalkRefusalChance, 0.0f, 1.0f) : 0.10f;
	const bool bSpeedRequest = Option.TalkAction == EProjectIntimacyTalkAction::SpeedSlow
		|| Option.TalkAction == EProjectIntimacyTalkAction::SpeedNormal
		|| Option.TalkAction == EProjectIntimacyTalkAction::SpeedIntense;
	const bool bAccepted = !bSpeedRequest || RandomStream.FRand() >= RefusalChance;

	UDataTable* ResponseTable = Settings ? LoadTable(Settings->PartnerResponsesTable) : nullptr;
	const EProjectIntimacyPersonality Personality = ActiveSession.EffectivePersonality;
	FText Response = UProjectIntimacyDialogueLibrary::ResolvePartnerResponse(ResponseTable, Option.OptionId, Personality, bAccepted);
	ActiveSession.StatusText = Response;

	if (!bAccepted)
	{
		RefreshHudWidget();
		return;
	}

	switch (Option.TalkAction)
	{
	case EProjectIntimacyTalkAction::SpeedSlow:
		ApplyAnimationRate(Settings ? Settings->ChillAnimationRate : 0.75f);
		break;
	case EProjectIntimacyTalkAction::SpeedNormal:
		ApplyAnimationRate(Settings ? Settings->NiceAnimationRate : 1.0f);
		break;
	case EProjectIntimacyTalkAction::SpeedIntense:
		ApplyAnimationRate(Settings ? Settings->StallionAnimationRate : 1.35f);
		break;
	case EProjectIntimacyTalkAction::Compliment:
		break;
	case EProjectIntimacyTalkAction::More:
		break;
	default:
		break;
	}

	const float CorrectOptionBonus = Option.OptionId == ActiveSession.CorrectTalkOptionId
		? (Settings ? FMath::Max(0.0f, Settings->CorrectTalkProgressBonus) : 5.0f)
		: 0.0f;
	const float ProgressGain = FMath::Max(0.0f, Option.SessionProgressGain) + CorrectOptionBonus;
	ApplySessionProgress(ProgressGain, FText::FromString(TEXT("Talk")));

	if (Option.AffectDelta != 0)
	{
		if (FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
		{
			Profile->Affect = FMath::Clamp(Profile->Affect + Option.AffectDelta, 0, Settings ? Settings->AffectMax : 100);
			SavePersistentState();
		}
	}

	TriggerMediaCueForTalkOption(Option);
	RefreshResolvedOptions();
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::TriggerMediaCueForTalkOption(const FProjectIntimacyResolvedOption& Option)
{
	if (!HudWidget)
	{
		EnsureHudWidget();
	}

	if (!HudWidget)
	{
		return;
	}

	FProjectIntimacyMediaCueRow Cue;
	if (TryResolveMediaCueForTalkOption(Option, Cue))
	{
		HudWidget->PlayMediaCue(Cue);
	}
}

bool UProjectIntimacySubsystem::TryResolveMediaCueForTalkOption(
	const FProjectIntimacyResolvedOption& Option,
	FProjectIntimacyMediaCueRow& OutCue) const
{
	auto MatchesOption = [&Option](const FProjectIntimacyMediaCueRow& Cue)
	{
		if (!Cue.bEnabled)
		{
			return false;
		}
		if (!Cue.TriggerOptionId.IsNone() && Cue.TriggerOptionId == Option.OptionId)
		{
			return true;
		}
		return Cue.TriggerTalkAction != EProjectIntimacyTalkAction::None
			&& Cue.TriggerTalkAction == Option.TalkAction;
	};

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	if (Settings)
	{
		if (UDataTable* MediaCuesTable = LoadTable(Settings->MediaCuesTable))
		{
			if (MediaCuesTable->GetRowStruct() == FProjectIntimacyMediaCueRow::StaticStruct())
			{
				TArray<FProjectIntimacyMediaCueRow*> Rows;
				MediaCuesTable->GetAllRows(TEXT("ProjectIntimacyMediaCues"), Rows);
				for (const FProjectIntimacyMediaCueRow* Row : Rows)
				{
					if (Row && MatchesOption(*Row))
					{
						OutCue = *Row;
						return true;
					}
				}
			}
		}
	}

	TArray<FProjectIntimacyMediaCueRow> FallbackCues;
	UProjectIntimacyDialogueLibrary::BuildFallbackMediaCues(FallbackCues);
	for (const FProjectIntimacyMediaCueRow& Cue : FallbackCues)
	{
		if (MatchesOption(Cue))
		{
			OutCue = Cue;
			return true;
		}
	}

	return false;
}

void UProjectIntimacySubsystem::TriggerMediaCueForEvent(const FName EventId)
{
	if (EventId.IsNone())
	{
		return;
	}

	if (!HudWidget)
	{
		EnsureHudWidget();
	}

	if (!HudWidget)
	{
		return;
	}

	FProjectIntimacyMediaCueRow Cue;
	if (TryResolveMediaCueForEvent(EventId, Cue))
	{
		HudWidget->PlayMediaCue(Cue);
	}
}

bool UProjectIntimacySubsystem::TryResolveMediaCueForEvent(const FName EventId, FProjectIntimacyMediaCueRow& OutCue) const
{
	if (EventId.IsNone())
	{
		return false;
	}

	auto MatchesEvent = [EventId](const FProjectIntimacyMediaCueRow& Cue)
	{
		return Cue.bEnabled && !Cue.TriggerEventId.IsNone() && Cue.TriggerEventId == EventId;
	};

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	if (Settings)
	{
		if (UDataTable* MediaCuesTable = LoadTable(Settings->MediaCuesTable))
		{
			if (MediaCuesTable->GetRowStruct() == FProjectIntimacyMediaCueRow::StaticStruct())
			{
				TArray<FProjectIntimacyMediaCueRow*> Rows;
				MediaCuesTable->GetAllRows(TEXT("ProjectIntimacyMediaCues"), Rows);
				for (const FProjectIntimacyMediaCueRow* Row : Rows)
				{
					if (Row && MatchesEvent(*Row))
					{
						OutCue = *Row;
						return true;
					}
				}
			}
		}
	}

	TArray<FProjectIntimacyMediaCueRow> FallbackCues;
	UProjectIntimacyDialogueLibrary::BuildFallbackMediaCues(FallbackCues);
	for (const FProjectIntimacyMediaCueRow& Cue : FallbackCues)
	{
		if (MatchesEvent(Cue))
		{
			OutCue = Cue;
			return true;
		}
	}

	return false;
}

void UProjectIntimacySubsystem::StartPlease()
{
	ActiveSession.HudMode = EProjectIntimacyHudMode::Please;
	ActiveSession.bPleaseActive = true;
	ActiveSession.PleaseAttemptIndex = 0;
	ActiveSession.PleaseSuccessCount = 0;
	ActiveSession.StatusText = FText::FromString(TEXT("Press Space on the sweet spot."));
	StartNextPleaseAttempt();
	RefreshResolvedOptions();
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::StartNextPleaseAttempt()
{
	ActiveSession.PleaseElapsedSeconds = 0.0f;
	ActiveSession.PleaseCursorValue = 0.0f;
	ActiveSession.PleaseTargetCenter = RandomStream.FRandRange(0.25f, 0.75f);
	ActiveSession.PleasePulsePeriod = UProjectIntimacySettings::ComputePleasePulsePeriod(ResolvePartnerLevel());
	ActiveSession.PleaseTargetHalfRange = UProjectIntimacySettings::ComputePleaseTargetHalfRange(ResolvePartnerLevel());
}

void UProjectIntimacySubsystem::ResolvePleasePress()
{
	if (!ActiveSession.bPleaseActive)
	{
		return;
	}

	if (FMath::Abs(ActiveSession.PleaseCursorValue - ActiveSession.PleaseTargetCenter) <= ActiveSession.PleaseTargetHalfRange)
	{
		ActiveSession.PleaseSuccessCount += 1;
		ActiveSession.StatusText = FText::FromString(TEXT("Good."));
	}
	else
	{
		ActiveSession.StatusText = FText::FromString(TEXT("Miss."));
	}

	ActiveSession.PleaseAttemptIndex += 1;
	const int32 AttemptCount = UProjectIntimacySettings::Get() ? UProjectIntimacySettings::Get()->PleaseAttemptCount : 5;
	if (ActiveSession.PleaseAttemptIndex >= FMath::Max(1, AttemptCount))
	{
		const float ProgressGain = UProjectIntimacySettings::ComputePleaseProgressGain(ActiveSession.PleaseSuccessCount);
		ApplySessionProgress(ProgressGain, FText::FromString(TEXT("Please")));
		ActiveSession.bPleaseActive = false;
		SetHudMode(EProjectIntimacyHudMode::Main);
		return;
	}

	StartNextPleaseAttempt();
	RefreshHudWidget();
}

void UProjectIntimacySubsystem::ApplySessionProgress(const float Amount, const FText& ReasonText)
{
	if (!bSessionActive || Amount <= 0.0f)
	{
		return;
	}

	const float PreviousProgress = ActiveSession.SessionProgress;
	ActiveSession.SessionProgress = UProjectIntimacySettings::ClampSessionProgress(
		ActiveSession.SessionProgress + Amount);
	const float ActualGain = FMath::Max(0.0f, ActiveSession.SessionProgress - PreviousProgress);
	if (ActualGain > 0.0f)
	{
		if (ActiveSession.SessionPeakThreshold <= 0.0f)
		{
			const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
			ActiveSession.SessionPeakThreshold = Settings
				? FMath::Clamp(Settings->SessionPeakThreshold, 1.0f, 100.0f)
				: 25.0f;
		}

		float RemainingPeak = ActiveSession.SessionPeak;
		const int32 PeakCount = UProjectIntimacySettings::ConsumeSessionPeak(
			ActiveSession.SessionPeak,
			ActualGain,
			ActiveSession.SessionPeakThreshold,
			RemainingPeak);
		ActiveSession.SessionPeak = RemainingPeak;
		if (PeakCount > 0)
		{
			TriggerSessionPeak(PeakCount);
		}
	}
	if (!ReasonText.IsEmpty())
	{
		ActiveSession.StatusText = FText::FromString(FString::Printf(
			TEXT("%s added %.0f session progress."),
			*ReasonText.ToString(),
			ActualGain));
	}
}

void UProjectIntimacySubsystem::UpdateSessionPeakIntensity(const float DeltaTime)
{
	if (!bSessionActive)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const float TargetMultiplier = Settings ? FMath::Max(1.0f, Settings->SessionPeakIntensityMultiplier) : 1.25f;
	const float RecoverySeconds = Settings ? FMath::Max(0.0f, Settings->SessionPeakRecoverySeconds) : 2.0f;

	float RecoveryMultiplier = 1.0f;
	if (ActiveSession.SessionPeakRecoveryRemaining > 0.0f && RecoverySeconds > 0.0f)
	{
		ActiveSession.SessionPeakRecoveryRemaining = FMath::Max(
			0.0f,
			ActiveSession.SessionPeakRecoveryRemaining - FMath::Max(0.0f, DeltaTime));
		const float Alpha = FMath::Clamp(ActiveSession.SessionPeakRecoveryRemaining / RecoverySeconds, 0.0f, 1.0f);
		RecoveryMultiplier = FMath::Lerp(1.0f, TargetMultiplier, Alpha);
	}
	else
	{
		ActiveSession.SessionPeakRecoveryRemaining = 0.0f;
	}

	const float AnticipationMultiplier = UProjectIntimacySettings::ComputeSessionPeakAnticipationMultiplier(
		ActiveSession.SessionPeak,
		ActiveSession.SessionPeakThreshold,
		Settings);
	ActiveSession.SessionPeakMultiplier = FMath::Max(1.0f, FMath::Max(RecoveryMultiplier, AnticipationMultiplier));
}

void UProjectIntimacySubsystem::TriggerSessionPeak(const int32 PeakCount)
{
	if (!bSessionActive || PeakCount <= 0)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	ActiveSession.SessionPeakRecoveryRemaining = Settings
		? FMath::Max(0.0f, Settings->SessionPeakRecoverySeconds)
		: 2.0f;
	ActiveSession.SessionPeakMultiplier = Settings
		? FMath::Max(1.0f, Settings->SessionPeakIntensityMultiplier)
		: 1.25f;
	if (FProjectIntimacyPartnerProfile* Profile = IntimacySaveGame ? IntimacySaveGame->PartnerProfiles.Find(ActiveSession.PartnerId) : nullptr)
	{
		Profile->SessionPeakCount += PeakCount;
		SavePersistentState();
	}
	TriggerMediaCueForEvent(Settings ? Settings->SessionPeakMediaEventId : FName(TEXT("SessionPeak")));
	ActiveSession.StatusText = FText::FromString(TEXT("Session peak"));
	if (TrackedEmoteComponent
		&& TrackedEmoteComponent->GetActiveInteractionId() == ProjectIntimacySubsystemPrivate::IntimacySceneId)
	{
		TrackedEmoteComponent->TriggerBlueprintSceneVisualSessionPeakCue();
	}
	ApplyAnimationRate(ActiveSession.AnimationRate);
}

void UProjectIntimacySubsystem::ApplyAnimationRate(const float NewRate)
{
	ActiveSession.AnimationRate = FMath::Max(0.05f, NewRate);
	const float EffectiveRate = ComputeEffectiveAnimationRate();
	TArray<USkeletalMeshComponent*> Meshes;
	CollectSessionMeshes(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		CacheAndSetMeshRate(Mesh, EffectiveRate);
	}
}

float UProjectIntimacySubsystem::ComputeEffectiveAnimationRate() const
{
	return FMath::Max(0.05f, ActiveSession.AnimationRate * FMath::Max(1.0f, ActiveSession.SessionPeakMultiplier));
}

void UProjectIntimacySubsystem::RestoreAnimationRates()
{
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, float>& Pair : CachedMeshRates)
	{
		if (USkeletalMeshComponent* Mesh = Pair.Key.Get())
		{
			Mesh->GlobalAnimRateScale = Pair.Value;
		}
	}
	CachedMeshRates.Reset();
}

void UProjectIntimacySubsystem::CacheAndSetMeshRate(USkeletalMeshComponent* MeshComponent, const float NewRate)
{
	if (!MeshComponent)
	{
		return;
	}

	if (!CachedMeshRates.Contains(MeshComponent))
	{
		CachedMeshRates.Add(MeshComponent, MeshComponent->GlobalAnimRateScale);
	}
	MeshComponent->GlobalAnimRateScale = FMath::Max(0.05f, NewRate);
}

void UProjectIntimacySubsystem::CollectSessionMeshes(TArray<USkeletalMeshComponent*>& OutMeshes) const
{
	OutMeshes.Reset();

	auto AddActorMeshes = [&OutMeshes](AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Actor);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh)
			{
				OutMeshes.AddUnique(Mesh);
			}
		}
	};

	AddActorMeshes(TrackedPlayerPawn.Get());
	AddActorMeshes(ActiveSession.PartnerActor.Get());
	if (TrackedEmoteComponent)
	{
		AddActorMeshes(TrackedEmoteComponent->GetActiveBlueprintSceneVisualActor());
	}
}

int32 UProjectIntimacySubsystem::ResolvePartnerLevel() const
{
	if (const UProjectIntimacyPartnerComponent* PartnerComponent = ActiveSession.PartnerComponent.Get())
	{
		return PartnerComponent->GetPartnerLevel();
	}

	return 1;
}

EProjectIntimacyPersonality UProjectIntimacySubsystem::ResolveEffectivePersonality(
	const FProjectIntimacyPartnerProfile& Profile,
	const UProjectIntimacyPartnerComponent* PartnerComponent) const
{
	if (RelationshipForcesChill(Profile.RelationshipTags))
	{
		return EProjectIntimacyPersonality::Chill;
	}

	return PartnerComponent ? PartnerComponent->GetResolvedPersonality() : Profile.Personality;
}

void UProjectIntimacySubsystem::NormalizeProfile(
	UProjectIntimacyPartnerComponent* PartnerComponent,
	FProjectIntimacyPartnerProfile& Profile) const
{
	if (Profile.PartnerId.IsEmpty() && PartnerComponent)
	{
		Profile.PartnerId = PartnerComponent->GetResolvedPartnerId();
	}

	const FGameplayTag ComponentGenderTag = PartnerComponent
		? PartnerComponent->GetResolvedGenderTag()
		: FGameplayTag();
	if (ComponentGenderTag.IsValid())
	{
		// The registered character class is authoritative. Repair legacy profiles that
		// persisted an incompatible gender before the Male/Female registry existed.
		Profile.GenderTag = ComponentGenderTag;
	}
	else if (!Profile.GenderTag.IsValid())
	{
		Profile.GenderTag = ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Gender.Male"));
	}

	if (Profile.RelationshipTags.Num() <= 0)
	{
		RefreshRelationshipTags(Profile);
	}

}

FGameplayTag UProjectIntimacySubsystem::GetBaseRelationshipTag(const int32 Encounters) const
{
	if (Encounters >= 50)
	{
		return ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Intimacy.Relationship.Partner"));
	}
	if (Encounters >= 30)
	{
		return ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Intimacy.Relationship.Devoted"));
	}
	if (Encounters >= 20)
	{
		return ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Intimacy.Relationship.Attached"));
	}
	if (Encounters >= 10)
	{
		return ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Intimacy.Relationship.Interested"));
	}
	return ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Intimacy.Relationship.Unknown"));
}

void UProjectIntimacySubsystem::RefreshRelationshipTags(FProjectIntimacyPartnerProfile& Profile) const
{
	ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Unknown"));
	ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Interested"));
	ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Attached"));
	ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Devoted"));
	ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Partner"));

	const FGameplayTag BaseRelationshipTag = GetBaseRelationshipTag(Profile.Encounters);
	if (BaseRelationshipTag.IsValid())
	{
		Profile.RelationshipTags.AddTag(BaseRelationshipTag);
	}

	const FGameplayTag MaleTag = ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Gender.Male"));
	if (Profile.bHasHusbandRing && MaleTag.IsValid() && Profile.GenderTag == MaleTag)
	{
		ProjectIntimacySubsystemPrivate::AddTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Husband"));
	}
	else
	{
		ProjectIntimacySubsystemPrivate::RemoveTag(Profile.RelationshipTags, TEXT("Project.Intimacy.Relationship.Husband"));
	}
}

bool UProjectIntimacySubsystem::RelationshipForcesChill(const FGameplayTagContainer& RelationshipTags) const
{
	return UProjectIntimacyDialogueLibrary::RelationshipTagsForceChill(RelationshipTags);
}

bool UProjectIntimacySubsystem::HasRelationshipTag(
	const FProjectIntimacyPartnerProfile& Profile,
	const TCHAR* TagName) const
{
	return ProjectIntimacySubsystemPrivate::HasTag(Profile.RelationshipTags, TagName);
}

FProjectIntimacyPartnerProfile& UProjectIntimacySubsystem::GetMutableProfile(UProjectIntimacyPartnerComponent* PartnerComponent)
{
	LoadPersistentState();

	const FString PartnerId = PartnerComponent ? PartnerComponent->GetResolvedPartnerId() : FString(TEXT("UnknownPartner"));
	if (!IntimacySaveGame->PartnerProfiles.Contains(PartnerId))
	{
		FProjectIntimacyPartnerProfile Profile;
		Profile.PartnerId = PartnerId;
		Profile.Personality = PartnerComponent ? PartnerComponent->GetResolvedPersonality() : EProjectIntimacyPersonality::Nice;
		Profile.Relationship = PartnerComponent ? PartnerComponent->InitialRelationship : EProjectIntimacyRelationship::Unknown;
		Profile.Affect = PartnerComponent ? PartnerComponent->InitialAffect : 0;
		Profile.GenderTag = PartnerComponent
			? PartnerComponent->GetResolvedGenderTag()
			: ProjectIntimacySubsystemPrivate::Tag(TEXT("Project.Gender.Male"));
		RefreshRelationshipTags(Profile);
		IntimacySaveGame->PartnerProfiles.Add(PartnerId, Profile);
	}

	FProjectIntimacyPartnerProfile& Profile = IntimacySaveGame->PartnerProfiles.FindChecked(PartnerId);
	NormalizeProfile(PartnerComponent, Profile);
	RefreshRelationshipTags(Profile);
	return Profile;
}

void UProjectIntimacySubsystem::LoadPersistentState()
{
	if (IntimacySaveGame)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : FString(TEXT("ProjectIntimacy"));
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;
	if (USaveGame* LoadedSave = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex))
	{
		IntimacySaveGame = Cast<UProjectIntimacySaveGame>(LoadedSave);
	}

	if (!IntimacySaveGame)
	{
		IntimacySaveGame = Cast<UProjectIntimacySaveGame>(UGameplayStatics::CreateSaveGameObject(UProjectIntimacySaveGame::StaticClass()));
	}
}

void UProjectIntimacySubsystem::SavePersistentState() const
{
	if (!IntimacySaveGame)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : FString(TEXT("ProjectIntimacy"));
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;
	UGameplayStatics::SaveGameToSlot(IntimacySaveGame, SlotName, UserIndex);
}

UDataTable* UProjectIntimacySubsystem::LoadTable(const FSoftObjectPath& TablePath) const
{
	return TablePath.IsValid() ? Cast<UDataTable>(TablePath.TryLoad()) : nullptr;
}

FProjectIntimacyEligibilityContext UProjectIntimacySubsystem::BuildEligibilityContext(
	AActor* PartnerActor,
	const UProjectIntimacyPartnerComponent* PartnerComponent) const
{
	FProjectIntimacyEligibilityContext Context;
	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;

	auto IsAlive = [](const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return false;
		}
		const UProjectCombatAttributeComponent* CombatComponent =
			Actor->FindComponentByClass<UProjectCombatAttributeComponent>();
		if (CombatComponent && CombatComponent->IsDead())
		{
			return false;
		}

		const UACFDamageHandlerComponent* DamageHandlerComponent =
			Actor->FindComponentByClass<UACFDamageHandlerComponent>();
		return !DamageHandlerComponent || DamageHandlerComponent->GetIsAlive();
	};

	auto IsConscious = [](const AActor* Actor)
	{
		return IsValid(Actor)
			&& !Actor->ActorHasTag(ProjectIntimacySubsystemPrivate::UnconsciousActorTag)
			&& !Actor->ActorHasTag(ProjectIntimacySubsystemPrivate::KnockedOutActorTag);
	};

	Context.bContentAllowed = ContentPolicy && ContentPolicy->IsIntimacyAllowed();
	Context.bCharismaMasteryUnlocked = HasRequiredCharismaForActor(TrackedPlayerPawn.Get());
	Context.bPlayerAdultVerified = Settings && Settings->bPlayerCharacterAdultVerified;
	Context.bPartnerAdultVerified = PartnerComponent && PartnerComponent->bAdultVerified;
	const UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	Context.bExplicitConsent = SocialSubsystem
		&& SocialSubsystem->HasExplicitIntimacyConsent(TrackedPlayerPawn.Get(), PartnerActor)
		&& SocialSubsystem->HasExplicitIntimacyConsent(PartnerActor, TrackedPlayerPawn.Get());
	Context.bPlayerAlive = IsAlive(TrackedPlayerPawn.Get());
	Context.bPartnerAlive = IsAlive(PartnerActor);
	Context.bPlayerConscious = IsConscious(TrackedPlayerPawn.Get());
	Context.bPartnerConscious = IsConscious(PartnerActor)
		&& PartnerComponent
		&& PartnerComponent->bConscious;
	Context.bPartnerNonHostile = PartnerComponent
		&& PartnerComponent->bNonHostileVerified
		&& !PartnerActor->ActorHasTag(ProjectIntimacySubsystemPrivate::HostileActorTag);
	const bool bCombatLockout = TrackedEmoteComponent
		&& TrackedEmoteComponent->IsCombatLockoutActive(Settings ? Settings->CombatLockoutSeconds : 8.0f);
	Context.bOutsideCombat = PartnerComponent
		&& PartnerComponent->bOutsideCombat
		&& !bCombatLockout
		&& IsValid(TrackedPlayerPawn.Get())
		&& IsValid(PartnerActor)
		&& !TrackedPlayerPawn->ActorHasTag(ProjectIntimacySubsystemPrivate::InCombatActorTag)
		&& !PartnerActor->ActorHasTag(ProjectIntimacySubsystemPrivate::InCombatActorTag);
	Context.bZoneAllowed = UProjectIntimacyZoneComponent::IsAnyAllowedZoneContaining(
		this,
		TrackedPlayerPawn.Get(),
		PartnerActor);
	return Context;
}

bool UProjectIntimacySubsystem::EnsureMaturePresentationRegistered(
	AActor* PartnerActor,
	FText& OutFailureReason)
{
	if (MaturePresentationRequestId.IsValid())
	{
		const bool bMatchesCurrentRequest = MaturePresentationPartnerActor.Get() == PartnerActor;
		if (!bMatchesCurrentRequest)
		{
			OutFailureReason = FText::FromString(TEXT("Another optional presentation is already active."));
		}
		return bMatchesCurrentRequest;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	if (!ContentPolicy)
	{
		OutFailureReason = FText::FromString(TEXT("Mature-content policy is unavailable."));
		return false;
	}

	FProjectMaturePresentationRequest Request;
	Request.Feature = EProjectOptionalMatureFeature::IntimacySession;
	Request.PresentationId = ProjectIntimacySubsystemPrivate::IntimacySceneId;
	Request.PrimaryParticipant = TrackedPlayerPawn.Get();
	Request.SecondaryParticipant = PartnerActor;
	const FGuid RequestId = ContentPolicy->TryBeginMaturePresentation(Request);
	if (!RequestId.IsValid()
		|| RequestId != MaturePresentationRequestId
		|| MaturePresentationPartnerActor.Get() != PartnerActor)
	{
		if (RequestId.IsValid())
		{
			ContentPolicy->CancelMaturePresentation(RequestId);
		}
		OutFailureReason = FText::FromString(TEXT("The optional presentation could not be started."));
		return false;
	}

	OutFailureReason = FText();
	return true;
}

bool UProjectIntimacySubsystem::BeginIntimacySceneAction(AActor* PartnerActor)
{
	if (!IsValid(PartnerActor)
		|| !TrackedEmoteComponent
		|| !TrackedTargetingFixComponent
		|| TrackedTargetingFixComponent->GetCurrentTargetActor() != PartnerActor)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UProjectEmoteSubsystem* EmoteSubsystem = World ? World->GetSubsystem<UProjectEmoteSubsystem>() : nullptr;
	if (!EmoteSubsystem || EmoteSubsystem->IsRuntimeActionActive())
	{
		return false;
	}

	FProjectEmoteRuntimeActionRequest RuntimeActionRequest;
	RuntimeActionRequest.RuntimeActionId = ProjectIntimacySubsystemPrivate::IntimacySceneId;
	RuntimeActionRequest.InteractionId = ProjectIntimacySubsystemPrivate::IntimacySceneId;
	RuntimeActionRequest.Source = EProjectEmoteRuntimeActionSource::Interaction;
	RuntimeActionRequest.bAllowCancel = true;
	RuntimeActionRequest.bCancelWithY = true;
	RuntimeActionRequest.bRestoreMovementOnEnd = true;
	RuntimeActionRequest.bHiddenFromMenu = true;
	if (EmoteSubsystem->StartRuntimeAction(RuntimeActionRequest))
	{
		return true;
	}

	return TrackedEmoteComponent->StartRuntimeInteractionById(
		ProjectIntimacySubsystemPrivate::IntimacySceneId);
}

void UProjectIntimacySubsystem::EndMaturePresentationRegistration(const bool bNotifyPolicy)
{
	if (!MaturePresentationRequestId.IsValid())
	{
		MaturePresentationPartnerActor.Reset();
		return;
	}

	const FGuid RequestId = MaturePresentationRequestId;
	MaturePresentationRequestId = FGuid();
	MaturePresentationPartnerActor.Reset();
	if (!bNotifyPolicy)
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr)
	{
		ContentPolicy->NotifyMaturePresentationEnded(RequestId, this);
	}
}

void UProjectIntimacySubsystem::RegisterSocialParticipants(
	AActor* PartnerActor,
	const UProjectIntimacyPartnerComponent* PartnerComponent,
	const bool bEstablishConsent)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!SocialSubsystem || !IsValid(TrackedPlayerPawn.Get()) || !IsValid(PartnerActor) || !PartnerComponent)
	{
		return;
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	const FProjectIntimacyEligibilityContext Context = BuildEligibilityContext(PartnerActor, PartnerComponent);
	FProjectSocialParticipantState PlayerState;
	SocialSubsystem->TryGetParticipantState(TrackedPlayerPawn.Get(), PlayerState);
	PlayerState.ParticipantId = TEXT("Player.Local");
	PlayerState.bVerifiedAdult = Context.bPlayerAdultVerified;
	PlayerState.bAlive = Context.bPlayerAlive;
	PlayerState.bConscious = Context.bPlayerConscious;
	PlayerState.bHostile = false;
	PlayerState.bInCombat = !Context.bOutsideCombat;
	PlayerState.bInSafeLocation = Context.bZoneAllowed;

	FProjectSocialParticipantState PartnerState;
	const bool bPartnerAlreadyRegistered =
		SocialSubsystem->TryGetParticipantState(PartnerActor, PartnerState);
	PartnerState.ParticipantId = FName(*PartnerComponent->GetResolvedPartnerId());
	PartnerState.bVerifiedAdult = Context.bPartnerAdultVerified;
	PartnerState.bAlive = Context.bPartnerAlive;
	PartnerState.bConscious = Context.bPartnerConscious;
	PartnerState.bHostile = !Context.bPartnerNonHostile;
	PartnerState.bInCombat = !Context.bOutsideCombat;
	PartnerState.bInSafeLocation = Context.bZoneAllowed;
	PartnerState.bRecruitable = PartnerComponent->bSocialCompanion;
	PartnerState.bRecruitedCompanion = PartnerComponent->bSocialCompanion;
	PartnerState.bOffersPlayerInitiatedIntimacy =
		PartnerComponent->bSocialCompanion
		&& PartnerComponent->bOffersPlayerInitiatedConsent;
	PartnerState.MinimumIntimacyAffinity = Settings
		? FProjectSocialRules::ClampAffinity(Settings->MinimumAffinityForIntimacyConsent)
		: 25;
	if (!bPartnerAlreadyRegistered)
	{
		PartnerState.Affinity = FProjectSocialRules::ClampAffinity(PartnerComponent->InitialAffect);
	}

	if (!SocialSubsystem->RegisterOrUpdateParticipant(TrackedPlayerPawn.Get(), PlayerState)
		|| !SocialSubsystem->RegisterOrUpdateParticipant(PartnerActor, PartnerState)
		|| !bEstablishConsent)
	{
		return;
	}

	if (!Context.bContentAllowed
		|| !Context.bCharismaMasteryUnlocked
		|| !PartnerComponent->bSocialCompanion
		|| !PartnerComponent->bOffersPlayerInitiatedConsent)
	{
		return;
	}

	// Selecting the partner action is the player's explicit request. The
	// social authority independently validates the companion's authored offer,
	// affinity and live safety, then records both directions atomically.
	const FProjectSocialEligibilityResult ConsentResult =
		SocialSubsystem->TryEstablishBilateralIntimacyConsent(
			TrackedPlayerPawn.Get(),
			PartnerActor,
			Settings ? Settings->MinimumAffinityForIntimacyConsent : 25);
	if (!ConsentResult.bEligible)
	{
		UE_LOG(
			LogProjectIntimacy,
			Display,
			TEXT("[ProjectIntimacy] Bilateral consent request denied: %s"),
			*ProjectIntimacySubsystemPrivate::SocialEligibilityFailureText(
				ConsentResult.Failure).ToString());
	}
}

void UProjectIntimacySubsystem::ClearConsentForPartner(AActor* PartnerActor)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectSocialSubsystem* SocialSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
		: nullptr;
	if (!SocialSubsystem || !IsValid(TrackedPlayerPawn.Get()) || !IsValid(PartnerActor))
	{
		return;
	}

	SocialSubsystem->SetExplicitIntimacyConsent(TrackedPlayerPawn.Get(), PartnerActor, false);
	SocialSubsystem->SetExplicitIntimacyConsent(PartnerActor, TrackedPlayerPawn.Get(), false);
}

void UProjectIntimacySubsystem::ClearActiveSessionConsent()
{
	ClearConsentForPartner(ActiveSession.PartnerActor.Get());
}

void UProjectIntimacySubsystem::HandleToggleHudPressed()
{
	if (bSessionActive)
	{
		RequestToggleHud();
		return;
	}

	RequestQuickStartIntimacy();
}

void UProjectIntimacySubsystem::HandleNavigateUpPressed()
{
	RequestNavigate(-1);
}

void UProjectIntimacySubsystem::HandleNavigateDownPressed()
{
	RequestNavigate(1);
}

void UProjectIntimacySubsystem::HandleNavigateLeftPressed()
{
	RequestBack();
}

void UProjectIntimacySubsystem::HandleNavigateRightPressed()
{
	RequestConfirm();
}

void UProjectIntimacySubsystem::HandleConfirmPressed()
{
	RequestConfirm();
}
