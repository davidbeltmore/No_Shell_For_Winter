#include "Defeat/ProjectDefeatFlowComponent.h"

#include "ACFAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CCMPlayerCameraManager.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Combat/ProjectCombatTypes.h"
#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "Components/ACFAbilitySystemComponent.h"
#include "Components/ACFThreatManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InputComponent.h"
#include "Defeat/ProjectDefeatBlueprintBridgeComponent.h"
#include "Defeat/ProjectDefeatCurseClampInterface.h"
#include "Defeat/ProjectDefeatFlowLogic.h"
#include "Defeat/ProjectDefeatHitResolver.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "Defeat/ProjectDefeatTravelSubsystem.h"
#include "Defeat/ProjectKnockoutStruggleWidget.h"
#include "Defeat/ProjectPainDebugWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "UI/ProjectEmoteSubsystem.h"

namespace
{
	const FName PainName(TEXT("Pain"));
	const FName HealthName(TEXT("Health"));
	const FName DefeatedRespawnRuntimeActionId(TEXT("Presentation.System.Respawn.Private01"));
	constexpr float DefeatedArrivalSceneStartSettleSeconds = 0.20f;
	constexpr float NearbyEnemyCacheSeconds = 0.12f;

	FString BuildEnemyDisplayName(const FName EnemyClassName)
	{
		FString DisplayName = EnemyClassName.ToString();
		DisplayName.RemoveFromEnd(TEXT("_C"));
		DisplayName.ReplaceInline(TEXT("BP_"), TEXT(""));
		return DisplayName;
	}

	template <typename EnumType>
	FString GetEnumValueString(const EnumType Value)
	{
		if (const UEnum* Enum = StaticEnum<EnumType>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}

		return TEXT("Unknown");
	}

	bool IsACFControllerTargetingActor(const AACFAIController* ACFController, const AActor* Actor)
	{
		return ACFController
			&& Actor
			&& (ACFController->GetTarget() == Actor || ACFController->GetTargetActorBK() == Actor);
	}

	bool RemoveACFThreatForActor(AACFAIController* ACFController, AActor* Actor)
	{
		if (!ACFController || !Actor)
		{
			return false;
		}

		UACFThreatManagerComponent* ThreatManager = ACFController->GetThreatManager();
		if (!ThreatManager || !ThreatManager->IsThreatening(Actor))
		{
			return false;
		}

		ThreatManager->RemoveThreatening(Actor);
		return true;
	}

	void StopACFControllerFromTargetingActor(AACFAIController* ACFController, AActor* Actor)
	{
		if (!ACFController || !Actor)
		{
			return;
		}

		if (APawn* ControlledPawn = ACFController->GetPawn())
		{
			if (UACFAbilitySystemComponent* AbilityComponent = ControlledPawn->FindComponentByClass<UACFAbilitySystemComponent>())
			{
				AbilityComponent->ExitCurrentAction();
			}
		}

		ACFController->SetTarget(nullptr);
		ACFController->ResetToDefaultState();
		ACFController->StopMovement();
		ACFController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogProjectDefeatFlow, Log, All);

UProjectDefeatFlowComponent::UProjectDefeatFlowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UProjectDefeatFlowComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshDependencies();
	EnsurePainDebugWidget();
}

void UProjectDefeatFlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	InvalidateNearbyEnemyCache();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StruggleFreezeTimerHandle);
		World->GetTimerManager().ClearTimer(TravelDelayTimerHandle);
		World->GetTimerManager().ClearTimer(DefeatedArrivalSceneStartTimerHandle);
		World->GetTimerManager().ClearTimer(SceneCancelTimerHandle);
		World->GetTimerManager().ClearTimer(DeferredMovementRestoreTimerHandle);
	}

	RemoveStruggleWidget();
	RemovePainDebugWidget();
	RemoveSceneInputBinding();
	UnbindRuntimeActionEvents();
	EndStruggleGameplayFreeze();
	ApplyStruggleInputBlock(false);
	bPendingCrawlKnockout = false;
	PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	PendingCrawlKnockoutSourceId = NAME_None;
	ApplyKnockoutLocomotion(false);
	ApplyTransientInvulnerability(false);

	Super::EndPlay(EndPlayReason);
}

void UProjectDefeatFlowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.f)
	{
		return;
	}

	if (!NeedsComponent || !CombatAttributeComponent || !RealtimeSnapshotComponent || !InnerDoctrineComponent)
	{
		RefreshDependencies();
	}

	EnsurePainDebugWidget();

	UpdateDefeatCombatWindow();
	UpdatePendingCrawlKnockout();
	UpdatePainDecay(DeltaTime);

	if (CurrentPhase == EProjectDefeatPhase::None && IsAdvancedDefeatFlowEnabled())
	{
		EvaluatePainThreshold();
	}

	if (CurrentPhase == EProjectDefeatPhase::KnockedOut
		|| CurrentPhase == EProjectDefeatPhase::Struggle
		|| bPendingCrawlKnockout)
	{
		PacifyEnemiesTargetingOwnerWhileDowned();
	}

	UpdateKnockoutRecoveryState();
}

void UProjectDefeatFlowComponent::NotifyDamageReceived(const FProjectIncomingHitContext& HitContext)
{
	if (HitContext.AppliedDamage <= 0.f)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	FProjectDefeatHitResolution HitResolution;
	const bool bQualifiedEnemyHit = ResolveQualifiedEnemyHit(HitContext, HitResolution);

	LastDebugHitTimeSeconds = HitContext.WorldTimeSeconds;
	LastDebugDamageType = HitContext.DamageType;
	LastDebugSourceActorName = FProjectDefeatHitResolver::DescribeActor(HitContext.SourceActor.Get());
	LastDebugDamageCauserName = FProjectDefeatHitResolver::DescribeActor(HitContext.DamageCauser.Get());
	LastDebugResolvedActorName = bQualifiedEnemyHit
		? FProjectDefeatHitResolver::DescribeActor(HitResolution.ResolvedActor.Get())
		: TEXT("None");
	LastDebugQualificationReason = HitResolution.ResolutionReason;
	LastDebugPainDelta = 0.f;
	bLastDebugHitQualified = bQualifiedEnemyHit;

	if (!bQualifiedEnemyHit)
	{
		if (Settings && Settings->bEnablePainDebugLogs)
		{
			UE_LOG(
				LogProjectDefeatFlow,
				Warning,
				TEXT("[PainHit] Qualified=false DamageType=%s Applied=%.2f Reason=%s"),
				*HitContext.DamageType.ToString(),
				HitContext.AppliedDamage,
				*HitResolution.ResolutionReason);
		}
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : HitContext.WorldTimeSeconds;
	RegisterCombatEvent(Now, true);

	const float MaxHealth = CombatAttributeComponent
		? FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(CombatAttributeComponent->HealthAttributeName), 1.f)
		: 1.f;
	if (HitContext.RemainingHealth <= MaxHealth * (Settings ? Settings->LosingHealthThresholdPct : 0.30f))
	{
		SetLosingActive(true);
	}

	if (CurrentPhase == EProjectDefeatPhase::None)
	{
		LastDebugPainDelta = HitContext.PainAppliedDelta;
	}

	if (Settings && Settings->bEnablePainDebugLogs)
	{
		UE_LOG(
			LogProjectDefeatFlow,
			Log,
			TEXT("[PainHit] Qualified=true DamageType=%s Applied=%.2f PainDelta=%.2f Pain=%.2f/%.2f Resolved=%s Reason=%s"),
			*HitContext.DamageType.ToString(),
			HitContext.AppliedDamage,
			LastDebugPainDelta,
			GetPainCurrent(),
			GetPainThreshold(),
			*LastDebugResolvedActorName,
			*LastDebugQualificationReason);
	}
}

bool UProjectDefeatFlowComponent::TryHandleLethalDamage(
	const FProjectIncomingHitContext& HitContext,
	float& OutSurvivingHealth)
{
	if (!IsAdvancedDefeatFlowEnabled() || HitContext.AppliedDamage <= 0.f || !HitContext.bKilledTarget)
	{
		return false;
	}

	FProjectDefeatHitResolution HitResolution;
	const bool bQualifiedEnemyHit = ResolveQualifiedEnemyHit(HitContext, HitResolution);

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : HitContext.WorldTimeSeconds;
	const int32 NearbyQualifiedEnemyCount = CountNearbyQualifiedStruggleEnemies();
	const bool bCombatPressureActive = FProjectDefeatFlowLogic::IsCombatPressureActive(
		bQualifiedEnemyHit,
		ActiveCombatSessionId != 0,
		NearbyQualifiedEnemyCount);

	if (bQualifiedEnemyHit)
	{
		RegisterCombatEvent(Now, true);
	}
	else if (bCombatPressureActive)
	{
		RegisterCombatEvent(Now, false);
	}

	if (!bCombatPressureActive && Settings && Settings->bEnablePainDebugLogs)
	{
		UE_LOG(
			LogProjectDefeatFlow,
			Log,
			TEXT("[DefeatLethal] CombatPressure=false Applied=%.2f Reason=%s"),
			HitContext.AppliedDamage,
			*HitResolution.ResolutionReason);
	}

	if (Settings && Settings->bEnablePainDebugLogs)
	{
		UE_LOG(
			LogProjectDefeatFlow,
			Log,
			TEXT("[DefeatLethal] CombatPressure=%s Qualified=%s Applied=%.2f Session=%d NearbyEnemies=%d Resolved=%s Reason=%s"),
			bCombatPressureActive ? TEXT("true") : TEXT("false"),
			bQualifiedEnemyHit ? TEXT("true") : TEXT("false"),
			HitContext.AppliedDamage,
			ActiveCombatSessionId,
			NearbyQualifiedEnemyCount,
			*FProjectDefeatHitResolver::DescribeActor(HitResolution.ResolvedActor.Get()),
			*HitResolution.ResolutionReason);
	}

	OutSurvivingHealth = 1.f;

	AActor* ContextActor = HitResolution.ResolvedActor.Get();
	if (!ContextActor)
	{
		ContextActor = HitContext.SourceActor.Get()
			? HitContext.SourceActor.Get()
			: (HitContext.DamageCauser.Get()
				? HitContext.DamageCauser.Get()
				: HitContext.InstigatorActor.Get());
	}

	if (FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(
		CurrentPhase == EProjectDefeatPhase::KnockedOut || CurrentPhase == EProjectDefeatPhase::Struggle,
		ActiveCombatSessionId,
		LastKnockoutCombatSessionId))
	{
		EnterDefeated(EProjectDefeatReason::RepeatKnockout, ContextActor);
		return true;
	}

	EnterKnockedOut(EProjectKnockoutReason::PainMaxed);
	return true;
}

void UProjectDefeatFlowComponent::HandleDefeatedArrivalFromTravel(
	const FProjectDefeatTransferPayload& Payload,
	const FProjectDefeatInventorySnapshot& RetainedInventorySnapshot,
	const FTransform& SpawnTransform)
{
	InvalidateNearbyEnemyCache();
	RefreshDependencies();
	if (APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		PlayerController->FlushPressedKeys();
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	FProjectDefeatInventoryBridge::ApplyRetainedSubset(GetOwner(), RetainedInventorySnapshot, UProjectDefeatFlowSettings::Get());

	if (CombatAttributeComponent && !CombatAttributeComponent->HealthAttributeName.IsNone())
	{
		const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
		const float MaxHealth = FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(CombatAttributeComponent->HealthAttributeName), 1.f);
		CombatAttributeComponent->SetAttributeCurrentValue(
			CombatAttributeComponent->HealthAttributeName,
			FMath::Max(MaxHealth * (Settings ? Settings->DefeatedArrivalHealthPct : 0.25f), 1.f));
	}

	CurrentKnockoutReason = Payload.KnockoutReason;
	CurrentDefeatReason = Payload.DefeatReason;
	PendingTransferPayload = Payload;
	if (!PendingTransferPayload.bPostDefeatPresentationResolved)
	{
		PendingTransferPayload.bPostDefeatPresentationResolved = true;
		PendingTransferPayload.bPostDefeatPresentationEligible = false;
		PendingTransferPayload.PostDefeatPresentationRoll = -1.0f;
		PendingTransferPayload.PostDefeatPresentation = EProjectPostDefeatPresentation::None;
		PendingTransferPayload.SceneDefinition = FProjectDefeatSceneDefinition();
	}
	else if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette
		&& (!FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(PendingTransferPayload)
			|| !IsMatureDefeatAllowedByPolicy()))
	{
		const bool bStoredPresentationValid =
			FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(PendingTransferPayload);
		PendingTransferPayload.bPostDefeatPresentationDowngradedByPolicy =
			bStoredPresentationValid && !IsMatureDefeatAllowedByPolicy();
		PendingTransferPayload.bTechnicalFailure =
			PendingTransferPayload.bTechnicalFailure || !bStoredPresentationValid;
		PendingTransferPayload.PostDefeatPresentation = EProjectPostDefeatPresentation::None;
		PendingTransferPayload.SceneDefinition = FProjectDefeatSceneDefinition();
	}
	ActiveCameraInputSnapshot = Payload.CameraInputSnapshot;
	PendingRetainedInventorySnapshot = RetainedInventorySnapshot;
	bPendingDefeatedArrivalCameraRefresh = true;
	ClampCurseAfterDefeat();

	StartPlayerCameraFade(1.f, 1.f, 0.f, true);
	LogDefeatTransition(TEXT("DefeatedArrivalFromTravel"), nullptr);
	if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette)
	{
		ScheduleDefeatedArrivalSceneStart();
	}
	else
	{
		CompleteDefeatedRespawnWithoutPresentation();
	}
}

EProjectDefeatPhase UProjectDefeatFlowComponent::GetCurrentPhase() const
{
	return CurrentPhase;
}

EProjectKnockoutReason UProjectDefeatFlowComponent::GetCurrentKnockoutReason() const
{
	return CurrentKnockoutReason;
}

EProjectDefeatReason UProjectDefeatFlowComponent::GetCurrentDefeatReason() const
{
	return CurrentDefeatReason;
}

EProjectPostDefeatPresentation UProjectDefeatFlowComponent::GetCurrentPostDefeatPresentation() const
{
	return PendingTransferPayload.PostDefeatPresentation;
}

bool UProjectDefeatFlowComponent::IsKnockedOut() const
{
	return CurrentPhase == EProjectDefeatPhase::KnockedOut || CurrentPhase == EProjectDefeatPhase::Struggle;
}

bool UProjectDefeatFlowComponent::IsLosingActive() const
{
	return bLosingActive;
}

bool UProjectDefeatFlowComponent::IsAdvancedDefeatFlowEnabled() const
{
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	return Settings ? Settings->bEnableAdvancedDefeatFlow : false;
}

bool UProjectDefeatFlowComponent::RequestKnockoutOrPendingCrawl(
	const EProjectKnockoutReason KnockoutReason,
	const FName SourceId)
{
	if (!IsAdvancedDefeatFlowEnabled())
	{
		return false;
	}

	if (!NeedsComponent || !RealtimeSnapshotComponent || !InnerDoctrineComponent)
	{
		RefreshDependencies();
	}

	if (CurrentPhase != EProjectDefeatPhase::None)
	{
		return false;
	}

	if (HasNearbyQualifiedStruggleEnemy())
	{
		if (ActiveCombatSessionId == 0)
		{
			EnsureCombatSessionActive(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
		}

		bPendingCrawlKnockout = false;
		PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
		PendingCrawlKnockoutSourceId = NAME_None;
		EnterKnockedOut(KnockoutReason);
		return true;
	}

	bPendingCrawlKnockout = true;
	PendingCrawlKnockoutReason = KnockoutReason;
	PendingCrawlKnockoutSourceId = SourceId;
	SetPainCurrent(0.f);
	ApplyKnockoutLocomotion(true);
	LogDefeatTransition(TEXT("PendingCrawlKnockoutStarted"), nullptr);
	BroadcastStateRefresh();
	return true;
}

bool UProjectDefeatFlowComponent::RequestTacticalRetreat(const FName SourceId)
{
	if (!IsAdvancedDefeatFlowEnabled() || CurrentPhase != EProjectDefeatPhase::None)
	{
		return false;
	}

	if (!NeedsComponent || !RealtimeSnapshotComponent || !InnerDoctrineComponent)
	{
		RefreshDependencies();
	}

	CurrentKnockoutReason = EProjectKnockoutReason::TacticalRetreat;
	EnterDefeated(
		EProjectDefeatReason::TacticalRetreat,
		nullptr,
		false,
		false,
		false);

	UE_LOG(
		LogProjectDefeatFlow,
		Display,
		TEXT("[DefeatFlow] Tactical retreat requested directly. Source=%s"),
		*SourceId.ToString());
	return CurrentPhase == EProjectDefeatPhase::DefeatedBlackout
		|| CurrentPhase == EProjectDefeatPhase::TravelPending;
}

bool UProjectDefeatFlowComponent::TryRecoverFromKnockoutIfNoNearbyEnemy(const FName SourceId)
{
	if (!IsAdvancedDefeatFlowEnabled())
	{
		return false;
	}

	if (!NeedsComponent || !RealtimeSnapshotComponent || !InnerDoctrineComponent)
	{
		RefreshDependencies();
	}

	if (HasNearbyQualifiedStruggleEnemy())
	{
		return false;
	}

	if (bPendingCrawlKnockout && CurrentPhase == EProjectDefeatPhase::None)
	{
		bPendingCrawlKnockout = false;
		PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
		PendingCrawlKnockoutSourceId = NAME_None;
		bWaitingOutOfCombatRecovery = false;
		OutOfCombatRecoveryStartTimeSeconds = -1.f;
		SetPainCurrent(0.f);
		ApplyKnockoutLocomotion(false);
		LogDefeatTransition(TEXT("PendingCrawlKnockoutRecoveredByInput"), nullptr);
		BroadcastStateRefresh();
		(void)SourceId;
		return true;
	}

	if (CurrentPhase == EProjectDefeatPhase::KnockedOut)
	{
		FinishKnockoutRecovery(false);
		LogDefeatTransition(TEXT("KnockoutRecoveredByInput"), nullptr);
		(void)SourceId;
		return true;
	}

	(void)SourceId;
	return false;
}

bool UProjectDefeatFlowComponent::DebugTriggerImmediateDefeat(const FName SourceId)
{
#if UE_BUILD_SHIPPING
	(void)SourceId;
	return false;
#else
	(void)SourceId;

	RefreshDependencies();
	if (CurrentPhase == EProjectDefeatPhase::DefeatedBlackout
		|| CurrentPhase == EProjectDefeatPhase::TravelPending
		|| CurrentPhase == EProjectDefeatPhase::DefeatedScene)
	{
		return false;
	}

	CurrentKnockoutReason = EProjectKnockoutReason::DebugForced;
	EnterDefeated(EProjectDefeatReason::DebugForced, nullptr);

	if (CurrentPhase == EProjectDefeatPhase::DefeatedBlackout)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TravelDelayTimerHandle);
		}
		TriggerPendingTravel();
	}

	return CurrentPhase == EProjectDefeatPhase::TravelPending || CurrentPhase == EProjectDefeatPhase::DefeatedScene;
#endif
}

bool UProjectDefeatFlowComponent::IsPendingCrawlKnockout() const
{
	return bPendingCrawlKnockout;
}

FProjectPainDebugSnapshot UProjectDefeatFlowComponent::BuildPainDebugSnapshot() const
{
	FProjectPainDebugSnapshot Snapshot;
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : LastQualifiedImpactTimeSeconds;

	Snapshot.CurrentPhase = CurrentPhase;
	Snapshot.PainCurrent = GetPainCurrent();
	Snapshot.PainThreshold = GetPainThreshold();
	Snapshot.LastPainDelta = LastDebugPainDelta;
	Snapshot.LastHitWorldTimeSeconds = LastDebugHitTimeSeconds;
	Snapshot.bAdvancedFlowEnabled = IsAdvancedDefeatFlowEnabled();
	Snapshot.bLastHitQualified = bLastDebugHitQualified;
	Snapshot.bLosingActive = bLosingActive;
	Snapshot.bHadKnockoutThisCombat = bHadKnockoutThisCombat;
	Snapshot.ActiveCombatSessionId = ActiveCombatSessionId;
	Snapshot.LastKnockoutCombatSessionId = LastKnockoutCombatSessionId;
	Snapshot.LastDamageType = LastDebugDamageType;
	Snapshot.LastSourceActorName = LastDebugSourceActorName;
	Snapshot.LastDamageCauserName = LastDebugDamageCauserName;
	Snapshot.LastResolvedActorName = LastDebugResolvedActorName;
	Snapshot.LastQualificationReason = LastDebugQualificationReason;

	const float DelaySeconds = Settings ? Settings->PainDecayDelaySeconds : 5.f;
	const float SinceImpactSeconds = Now - LastQualifiedImpactTimeSeconds;
	Snapshot.TimeUntilDecaySeconds = FMath::Max(0.f, DelaySeconds - SinceImpactSeconds);
	return Snapshot;
}

FText UProjectDefeatFlowComponent::BuildPainDebugText() const
{
	const FProjectPainDebugSnapshot Snapshot = BuildPainDebugSnapshot();
	const FString DebugText = FString::Printf(
		TEXT("Pain Debug\n")
		TEXT("Phase: %s\n")
		TEXT("Advanced Flow: %s\n")
		TEXT("Pain: %.2f / %.2f\n")
		TEXT("Decay In: %.2fs\n")
		TEXT("Losing: %s | Had KO: %s\n")
		TEXT("CombatSession: %d | LastKO Session: %d\n")
		TEXT("Last Hit Qualified: %s | Delta: %.2f\n")
		TEXT("DamageType: %s\n")
		TEXT("Source: %s\n")
		TEXT("Causer: %s\n")
		TEXT("Resolved: %s\n")
		TEXT("Reason: %s"),
		*StaticEnum<EProjectDefeatPhase>()->GetNameStringByValue(static_cast<int64>(Snapshot.CurrentPhase)),
		Snapshot.bAdvancedFlowEnabled ? TEXT("ON") : TEXT("OFF"),
		Snapshot.PainCurrent,
		Snapshot.PainThreshold,
		Snapshot.TimeUntilDecaySeconds,
		Snapshot.bLosingActive ? TEXT("YES") : TEXT("NO"),
		Snapshot.bHadKnockoutThisCombat ? TEXT("YES") : TEXT("NO"),
		Snapshot.ActiveCombatSessionId,
		Snapshot.LastKnockoutCombatSessionId,
		Snapshot.bLastHitQualified ? TEXT("YES") : TEXT("NO"),
		Snapshot.LastPainDelta,
		*Snapshot.LastDamageType.ToString(),
		*Snapshot.LastSourceActorName,
		*Snapshot.LastDamageCauserName,
		*Snapshot.LastResolvedActorName,
		*Snapshot.LastQualificationReason);
	return FText::FromString(DebugText);
}

void UProjectDefeatFlowComponent::RefreshDependencies()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	NeedsComponent = OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	StatusComponent = OwnerActor->FindComponentByClass<UProjectSurvivalStatusComponent>();
	CombatAttributeComponent = OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>();
	RealtimeSnapshotComponent = OwnerActor->FindComponentByClass<UProjectRealtimeSnapshotComponent>();
	InnerDoctrineComponent = OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>();
	LocomotionOverrideComponent = OwnerActor->FindComponentByClass<UProjectLocomotionOverrideComponent>();
	EmoteComponent = OwnerActor->FindComponentByClass<UProjectEmoteComponent>();
	BlueprintBridgeComponent = OwnerActor->FindComponentByClass<UProjectDefeatBlueprintBridgeComponent>();
}

bool UProjectDefeatFlowComponent::ResolveQualifiedEnemyHit(
	const FProjectIncomingHitContext& HitContext,
	FProjectDefeatHitResolution& OutResolution) const
{
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	if (!FProjectDefeatHitResolver::ResolveQualifiedEnemyActor(
		HitContext,
		Settings ? Settings->QualifiedEnemyClassNameHints : TArray<FString>(),
		OutResolution))
	{
		return false;
	}

	return OutResolution.ResolvedActor.IsValid();
}

bool UProjectDefeatFlowComponent::IsQualifiedEnemyActor(const AActor* Actor) const
{
	FProjectIncomingHitContext HitContext;
	HitContext.SourceActor = const_cast<AActor*>(Actor);

	FProjectDefeatHitResolution Resolution;
	return ResolveQualifiedEnemyHit(HitContext, Resolution);
}

int32 UProjectDefeatFlowComponent::CountNearbyQualifiedStruggleEnemies() const
{
	if (!RealtimeSnapshotComponent || !GetOwner())
	{
		return 0;
	}

	const UWorld* World = GetWorld();
	const float NowSeconds = World ? World->GetTimeSeconds() : 0.0f;
	if ((NowSeconds - CachedNearbyEnemyWorldTimeSeconds) >= 0.0f
		&& (NowSeconds - CachedNearbyEnemyWorldTimeSeconds) <= NearbyEnemyCacheSeconds)
	{
		return CachedNearbyEnemyCount;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const FProjectUnifiedRuntimeSnapshot Snapshot = RealtimeSnapshotComponent->BuildUnifiedRuntimeSnapshot();
	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	const float RadiusSquared = FMath::Square(Settings ? Settings->StruggleEnemyRadius : 1000.f);
	int32 NearbyEnemyCount = 0;

	for (const FProjectRealtimeActorHealthSnapshot& EnemySnapshot : Snapshot.ObservedEnemies)
	{
		AActor* EnemyActor = EnemySnapshot.Actor.Get();
		if (!EnemyActor
			|| EnemyActor->IsHidden()
			|| !EnemyActor->GetActorEnableCollision()
			|| EnemySnapshot.CurrentHealth <= KINDA_SMALL_NUMBER
			|| !EnemySnapshot.bRelevantEnemy
			|| !IsQualifiedEnemyActor(EnemyActor))
		{
			continue;
		}

		if (FVector::DistSquared(OwnerLocation, EnemyActor->GetActorLocation()) <= RadiusSquared)
		{
			++NearbyEnemyCount;
		}
	}

	CachedNearbyEnemyWorldTimeSeconds = NowSeconds;
	CachedNearbyEnemyCount = NearbyEnemyCount;
	return NearbyEnemyCount;
}

void UProjectDefeatFlowComponent::InvalidateNearbyEnemyCache() const
{
	CachedNearbyEnemyWorldTimeSeconds = -FLT_MAX;
	CachedNearbyEnemyCount = 0;
}

void UProjectDefeatFlowComponent::EnsureCombatSessionActive(const float EventTimeSeconds)
{
	const int32 PreviousSessionId = ActiveCombatSessionId;
	if (ActiveCombatSessionId == 0)
	{
		ActiveCombatSessionId = FMath::Max(1, NextCombatSessionId++);
	}

	LastCombatEventTimeSeconds = EventTimeSeconds;
	RefreshCombatFlags();

	if (PreviousSessionId == 0)
	{
		LogDefeatTransition(TEXT("CombatSessionStarted"), nullptr);
	}

	BroadcastStateRefresh();
}

void UProjectDefeatFlowComponent::RegisterCombatEvent(const float EventTimeSeconds, const bool bQualifiedImpact)
{
	EnsureCombatSessionActive(EventTimeSeconds);

	if (bQualifiedImpact)
	{
		LastQualifiedImpactTimeSeconds = EventTimeSeconds;
		PainDecayAccumulatorSeconds = 0.f;
	}
}

void UProjectDefeatFlowComponent::EndCombatSession()
{
	if (ActiveCombatSessionId == 0)
	{
		return;
	}

	LogDefeatTransition(TEXT("CombatSessionEnded"), nullptr);
	ActiveCombatSessionId = 0;
	LastCombatEventTimeSeconds = -FLT_MAX;
	LastKnockoutCombatSessionId = 0;
	RefreshCombatFlags();
	SetLosingActive(false);
	BroadcastStateRefresh();
}

void UProjectDefeatFlowComponent::RefreshCombatFlags()
{
	bHadKnockoutThisCombat = ActiveCombatSessionId != 0
		&& LastKnockoutCombatSessionId == ActiveCombatSessionId;
}

bool UProjectDefeatFlowComponent::SetLosingActive(const bool bNewValue)
{
	if (bLosingActive == bNewValue)
	{
		return false;
	}

	bLosingActive = bNewValue;
	BroadcastStateRefresh();
	return true;
}

void UProjectDefeatFlowComponent::BroadcastStateRefresh()
{
	OnDefeatStateRefreshed.Broadcast();
}

void UProjectDefeatFlowComponent::LogDefeatTransition(const TCHAR* EventLabel, AActor* ContextActor) const
{
	const int32 NearbyEnemyCount = CountNearbyQualifiedStruggleEnemies();
	const int32 MissCount = StruggleWidget ? StruggleWidget->GetMissCount() : 0;
	const int32 MaxMissCount = StruggleWidget
		? StruggleWidget->GetMaxMissCount()
		: (UProjectDefeatFlowSettings::Get() ? UProjectDefeatFlowSettings::Get()->MaxStruggleMisses : 5);

	UE_LOG(
		LogProjectDefeatFlow,
		Log,
		TEXT("[DefeatFlow] Event=%s Phase=%s KnockoutReason=%s DefeatReason=%s CombatSession=%d LastKnockoutSession=%d NearbyEnemies=%d Queue=%d NextRound=%d ActiveRoundNotes=%d Misses=%d/%d Context=%s"),
		EventLabel ? EventLabel : TEXT("Unknown"),
		*GetEnumValueString(CurrentPhase),
		*GetEnumValueString(CurrentKnockoutReason),
		*GetEnumValueString(CurrentDefeatReason),
		ActiveCombatSessionId,
		LastKnockoutCombatSessionId,
		NearbyEnemyCount,
		PendingStruggleRounds.Num(),
		NextStruggleRoundIndex,
		ActiveRound.NoteCount,
		MissCount,
		MaxMissCount,
		*FProjectDefeatHitResolver::DescribeActor(ContextActor));
}

void UProjectDefeatFlowComponent::UpdatePainDecay(const float DeltaTime)
{
	if (CurrentPhase != EProjectDefeatPhase::None || !NeedsComponent || DeltaTime <= 0.f)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float DelaySeconds = Settings ? Settings->PainDecayDelaySeconds : 5.f;
	const float IntervalSeconds = Settings ? Settings->PainDecayIntervalSeconds : 1.f;
	const float DecayAmount = Settings ? Settings->PainDecayAmountPerTick : 1.f;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if ((Now - LastQualifiedImpactTimeSeconds) < DelaySeconds || GetPainCurrent() <= KINDA_SMALL_NUMBER)
	{
		PainDecayAccumulatorSeconds = 0.f;
		return;
	}

	PainDecayAccumulatorSeconds += DeltaTime;
	while (PainDecayAccumulatorSeconds >= IntervalSeconds && GetPainCurrent() > KINDA_SMALL_NUMBER)
	{
		PainDecayAccumulatorSeconds -= IntervalSeconds;
		SetPainCurrent(GetPainCurrent() - DecayAmount);
	}
}

void UProjectDefeatFlowComponent::UpdateDefeatCombatWindow()
{
	if (CurrentPhase != EProjectDefeatPhase::None)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const int32 NearbyQualifiedEnemyCount = CountNearbyQualifiedStruggleEnemies();
	const float CombatWindowSeconds = Settings ? Settings->DefeatCombatWindowSeconds : 5.f;

	if (ActiveCombatSessionId != 0
		&& !FProjectDefeatFlowLogic::ShouldKeepCombatSessionActive(
			Now,
			LastCombatEventTimeSeconds,
			CombatWindowSeconds,
			NearbyQualifiedEnemyCount))
	{
		EndCombatSession();
		return;
	}

	if (ActiveCombatSessionId == 0
		&& bLosingActive
		&& NearbyQualifiedEnemyCount <= 0
		&& (Now - LastQualifiedImpactTimeSeconds) > CombatWindowSeconds)
	{
		SetLosingActive(false);
	}
}

void UProjectDefeatFlowComponent::UpdateKnockoutRecoveryState()
{
	if (CurrentPhase != EProjectDefeatPhase::KnockedOut || !bWaitingOutOfCombatRecovery)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float RecoverySeconds = Settings ? Settings->KnockoutOutOfCombatRecoverySeconds : 30.f;
	const int32 NearbyQualifiedEnemyCount = CountNearbyQualifiedStruggleEnemies();

	if (IsDefeatCombatWindowActive() || NearbyQualifiedEnemyCount > 0)
	{
		OutOfCombatRecoveryStartTimeSeconds = -1.f;

		if (NearbyQualifiedEnemyCount > 0)
		{
			if (ActiveCombatSessionId == 0)
			{
				EnsureCombatSessionActive(Now);
			}

			bWaitingOutOfCombatRecovery = false;
			FreezeStruggleQueue();
		}

		return;
	}

	if (OutOfCombatRecoveryStartTimeSeconds < 0.f)
	{
		OutOfCombatRecoveryStartTimeSeconds = Now;
		return;
	}

	if ((Now - OutOfCombatRecoveryStartTimeSeconds) >= RecoverySeconds)
	{
		FinishKnockoutRecovery(false);
	}
}

void UProjectDefeatFlowComponent::UpdatePendingCrawlKnockout()
{
	if (!bPendingCrawlKnockout)
	{
		return;
	}

	if (CurrentPhase != EProjectDefeatPhase::None)
	{
		bPendingCrawlKnockout = false;
		PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
		PendingCrawlKnockoutSourceId = NAME_None;
		return;
	}

	ApplyKnockoutLocomotion(true);
	if (!HasNearbyQualifiedStruggleEnemy())
	{
		return;
	}

	const EProjectKnockoutReason RequestedReason = PendingCrawlKnockoutReason != EProjectKnockoutReason::None
		? PendingCrawlKnockoutReason
		: EProjectKnockoutReason::PainMaxed;
	if (ActiveCombatSessionId == 0)
	{
		EnsureCombatSessionActive(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
	}

	bPendingCrawlKnockout = false;
	PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	PendingCrawlKnockoutSourceId = NAME_None;
	EnterKnockedOut(RequestedReason);
}

bool UProjectDefeatFlowComponent::IsDefeatCombatWindowActive() const
{
	return ActiveCombatSessionId != 0;
}

bool UProjectDefeatFlowComponent::HasNearbyQualifiedStruggleEnemy() const
{
	return CountNearbyQualifiedStruggleEnemies() > 0;
}

void UProjectDefeatFlowComponent::EvaluatePainThreshold()
{
	if (!NeedsComponent)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	if (Settings && !Settings->bEnableAdvancedDefeatFlow)
	{
		return;
	}

	const float Threshold = GetPainThreshold();
	if (GetPainCurrent() + KINDA_SMALL_NUMBER < Threshold)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const int32 NearbyQualifiedEnemyCount = CountNearbyQualifiedStruggleEnemies();
	const bool bCombatPressureActive = ActiveCombatSessionId != 0 || NearbyQualifiedEnemyCount > 0;
	AActor* ContextActor = RealtimeSnapshotComponent
		? RealtimeSnapshotComponent->FindNearestRelevantEnemyToOwner()
		: nullptr;

	if (bCombatPressureActive && ActiveCombatSessionId == 0)
	{
		EnsureCombatSessionActive(Now);
	}

	if (InnerDoctrineComponent && InnerDoctrineComponent->ShouldDeferPainKnockoutForGuardRecovery())
	{
		LogDefeatTransition(TEXT("PainThresholdDeferredForGuardRecovery"), ContextActor);
		return;
	}

	LogDefeatTransition(TEXT("PainThresholdTriggered"), ContextActor);

	if (FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(
		false,
		ActiveCombatSessionId,
		LastKnockoutCombatSessionId))
	{
		EnterDefeated(EProjectDefeatReason::RepeatKnockout, ContextActor);
		return;
	}

	EnterKnockedOut(EProjectKnockoutReason::PainMaxed);
}

void UProjectDefeatFlowComponent::EnterKnockedOut(const EProjectKnockoutReason KnockoutReason)
{
	if (CurrentPhase != EProjectDefeatPhase::None)
	{
		return;
	}

	InvalidateNearbyEnemyCache();
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	CurrentPhase = EProjectDefeatPhase::KnockedOut;
	CurrentKnockoutReason = KnockoutReason;
	CurrentDefeatReason = EProjectDefeatReason::None;
	bPendingCrawlKnockout = false;
	PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	PendingCrawlKnockoutSourceId = NAME_None;
	LastKnockoutCombatSessionId = ActiveCombatSessionId;
	RefreshCombatFlags();
	SetLosingActive(false);
	bStruggleQueueFrozen = false;
	bStruggleFreezeActive = false;
	bWaitingOutOfCombatRecovery = false;
	bStruggleWonThisKnockout = false;
	PendingStruggleRounds.Reset();
	NextStruggleRoundIndex = 0;
	ActiveRound = FProjectStruggleRound();
	KnockoutEnteredTimeSeconds = Now;
	OutOfCombatRecoveryStartTimeSeconds = -1.f;

	SetPainCurrent(GetPainThreshold());
	ApplyTransientInvulnerability(true);
	ApplyKnockoutLocomotion(true);

	if (StatusComponent)
	{
		StatusComponent->SetForcedStatusActive(Settings ? Settings->KnockoutStatusName : TEXT("KnockedOut"), true);
	}

	OnKnockoutStateChanged.Broadcast(CurrentPhase, CurrentKnockoutReason, true);
	LogDefeatTransition(TEXT("EnteredKnockedOut"), nullptr);
	BeginStruggleFreeze();
}

void UProjectDefeatFlowComponent::BeginWaitingOutOfCombatRecovery()
{
	bWaitingOutOfCombatRecovery = true;
	OutOfCombatRecoveryStartTimeSeconds = -1.f;
	LogDefeatTransition(TEXT("WaitingOutOfCombatRecovery"), nullptr);
}

void UProjectDefeatFlowComponent::BeginStruggleFreeze()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	World->GetTimerManager().ClearTimer(StruggleFreezeTimerHandle);
	World->GetTimerManager().SetTimer(
		StruggleFreezeTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::FreezeStruggleQueue),
		Settings ? Settings->StruggleGraceSeconds : 0.75f,
		false);
}

void UProjectDefeatFlowComponent::BeginStruggleGameplayFreeze()
{
	if (bStruggleFreezeActive)
	{
		return;
	}

	ACharacter* PlayerCharacter = Cast<ACharacter>(GetOwner());
	if (PlayerCharacter)
	{
		StruggleFreezeContext.PlayerCharacter = PlayerCharacter;

		if (UCharacterMovementComponent* CharacterMovement = PlayerCharacter->GetCharacterMovement())
		{
			StruggleFreezeContext.PlayerMovementComponent = CharacterMovement;
			StruggleFreezeContext.PlayerMovementMode = CharacterMovement->MovementMode;
			StruggleFreezeContext.PlayerCustomMovementMode = CharacterMovement->CustomMovementMode;
			StruggleFreezeContext.bPlayerMovementTickEnabled = CharacterMovement->IsComponentTickEnabled();
			StruggleFreezeContext.bPlayerMovementCaptured = true;

			if (LocomotionOverrideComponent && bKnockoutLocomotionActive)
			{
				LocomotionOverrideComponent->RemoveTickPrerequisiteComponent(CharacterMovement);
				LocomotionOverrideComponent->SetComponentTickEnabled(true);
				StruggleFreezeContext.bRemovedLocomotionTickPrerequisite = true;
			}

			CharacterMovement->StopMovementImmediately();
			CharacterMovement->DisableMovement();
			CharacterMovement->SetComponentTickEnabled(false);
		}
	}

	CaptureFrozenComponentTick(NeedsComponent);
	CaptureFrozenComponentTick(StatusComponent);
	CaptureFrozenComponentTick(CombatAttributeComponent);
	CaptureFrozenComponentTick(InnerDoctrineComponent);

	for (const FProjectStruggleRound& PendingRound : PendingStruggleRounds)
	{
		CaptureFrozenEnemy(PendingRound.EnemyActor.Get());
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	CaptureFrozenProjectiles(Settings ? Settings->StruggleEnemyRadius : 1000.f);

	bStruggleFreezeActive = true;
}

void UProjectDefeatFlowComponent::EndStruggleGameplayFreeze()
{
	if (!bStruggleFreezeActive)
	{
		StruggleFreezeContext.Reset();
		return;
	}

	for (const FProjectFrozenProjectileState& ProjectileState : StruggleFreezeContext.FrozenProjectiles)
	{
		if (UProjectileMovementComponent* ProjectileMovement = ProjectileState.ProjectileMovementComponent.Get())
		{
			ProjectileMovement->Velocity = ProjectileState.SavedVelocity;
			ProjectileMovement->SetComponentTickEnabled(ProjectileState.bTickEnabled);
			if (ProjectileState.bTickEnabled)
			{
				ProjectileMovement->Activate(true);
			}
		}
	}

	for (const FProjectFrozenEnemyState& EnemyState : StruggleFreezeContext.FrozenEnemies)
	{
		if (USkeletalMeshComponent* SkeletalMesh = EnemyState.SkeletalMeshComponent.Get())
		{
			SkeletalMesh->bPauseAnims = EnemyState.bPausedAnims;
			SkeletalMesh->GlobalAnimRateScale = EnemyState.AnimRateScale;
		}

		if (UCharacterMovementComponent* CharacterMovement = EnemyState.MovementComponent.Get())
		{
			CharacterMovement->SetComponentTickEnabled(EnemyState.bMovementTickEnabled);
			CharacterMovement->Activate(true);
			CharacterMovement->SetMovementMode(EnemyState.MovementMode, EnemyState.CustomMovementMode);
		}

		if (EnemyState.bBrainWasRunning)
		{
			if (UBrainComponent* BrainComponent = EnemyState.BrainComponent.Get())
			{
				BrainComponent->RestartLogic();
			}
		}
	}

	for (const FProjectFrozenComponentTickState& TickState : StruggleFreezeContext.FrozenPlayerComponents)
	{
		if (UActorComponent* Component = TickState.Component.Get())
		{
			Component->SetComponentTickEnabled(TickState.bTickEnabled);
		}
	}

	if (StruggleFreezeContext.bPlayerMovementCaptured)
	{
		if (UCharacterMovementComponent* CharacterMovement = StruggleFreezeContext.PlayerMovementComponent.Get())
		{
			if (StruggleFreezeContext.bRemovedLocomotionTickPrerequisite && LocomotionOverrideComponent)
			{
				LocomotionOverrideComponent->AddTickPrerequisiteComponent(CharacterMovement);
			}

			CharacterMovement->SetComponentTickEnabled(StruggleFreezeContext.bPlayerMovementTickEnabled);
			CharacterMovement->Activate(true);
			if (CharacterMovement->MovementMode == MOVE_None)
			{
				CharacterMovement->SetMovementMode(StruggleFreezeContext.PlayerMovementMode, StruggleFreezeContext.PlayerCustomMovementMode);
			}
		}
	}

	StruggleFreezeContext.Reset();
	bStruggleFreezeActive = false;
}

void UProjectDefeatFlowComponent::CaptureFrozenComponentTick(UActorComponent* Component)
{
	if (!Component || Component == this)
	{
		return;
	}

	const bool bAlreadyCaptured = StruggleFreezeContext.FrozenPlayerComponents.ContainsByPredicate(
		[Component](const FProjectFrozenComponentTickState& ExistingState)
		{
			return ExistingState.Component.Get() == Component;
		});
	if (bAlreadyCaptured)
	{
		return;
	}

	FProjectFrozenComponentTickState TickState;
	TickState.Component = Component;
	TickState.bTickEnabled = Component->IsComponentTickEnabled();
	StruggleFreezeContext.FrozenPlayerComponents.Add(TickState);
	Component->SetComponentTickEnabled(false);
}

void UProjectDefeatFlowComponent::CaptureFrozenEnemy(AActor* EnemyActor)
{
	if (!EnemyActor)
	{
		return;
	}

	const bool bAlreadyCaptured = StruggleFreezeContext.FrozenEnemies.ContainsByPredicate(
		[EnemyActor](const FProjectFrozenEnemyState& ExistingState)
		{
			return ExistingState.Actor.Get() == EnemyActor;
		});
	if (bAlreadyCaptured)
	{
		return;
	}

	FProjectFrozenEnemyState EnemyState;
	EnemyState.Actor = EnemyActor;

	if (APawn* EnemyPawn = Cast<APawn>(EnemyActor))
	{
		if (AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController()))
		{
			EnemyState.AIController = AIController;
			AIController->StopMovement();
			AIController->ClearFocus(EAIFocusPriority::Gameplay);

			if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
			{
				EnemyState.BrainComponent = BrainComponent;
				EnemyState.bBrainWasRunning = BrainComponent->IsRunning();
				if (EnemyState.bBrainWasRunning)
				{
					BrainComponent->StopLogic(TEXT("ProjectStruggleFreeze"));
				}
			}
		}
	}

	if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyActor))
	{
		if (UCharacterMovementComponent* CharacterMovement = EnemyCharacter->GetCharacterMovement())
		{
			EnemyState.MovementComponent = CharacterMovement;
			EnemyState.bMovementTickEnabled = CharacterMovement->IsComponentTickEnabled();
			EnemyState.MovementMode = CharacterMovement->MovementMode;
			EnemyState.CustomMovementMode = CharacterMovement->CustomMovementMode;
			CharacterMovement->StopMovementImmediately();
			CharacterMovement->DisableMovement();
			CharacterMovement->SetComponentTickEnabled(false);
		}
	}

	if (USkeletalMeshComponent* SkeletalMesh = EnemyActor->FindComponentByClass<USkeletalMeshComponent>())
	{
		EnemyState.SkeletalMeshComponent = SkeletalMesh;
		EnemyState.bPausedAnims = SkeletalMesh->bPauseAnims;
		EnemyState.AnimRateScale = SkeletalMesh->GlobalAnimRateScale;
		SkeletalMesh->bPauseAnims = true;
		SkeletalMesh->GlobalAnimRateScale = 0.f;
	}

	StruggleFreezeContext.FrozenEnemies.Add(MoveTemp(EnemyState));
}

void UProjectDefeatFlowComponent::CaptureFrozenProjectiles(const float Radius)
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const float RadiusSquared = FMath::Square(FMath::Max(0.f, Radius));
	const FVector OwnerLocation = OwnerActor->GetActorLocation();

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* CandidateActor = *ActorIt;
		if (!CandidateActor || CandidateActor == OwnerActor)
		{
			continue;
		}

		if (RadiusSquared > 0.f
			&& FVector::DistSquared(OwnerLocation, CandidateActor->GetActorLocation()) > RadiusSquared)
		{
			continue;
		}

		UProjectileMovementComponent* ProjectileMovement = CandidateActor->FindComponentByClass<UProjectileMovementComponent>();
		if (!ProjectileMovement)
		{
			continue;
		}

		FProjectFrozenProjectileState ProjectileState;
		ProjectileState.ProjectileMovementComponent = ProjectileMovement;
		ProjectileState.SavedVelocity = ProjectileMovement->Velocity;
		ProjectileState.bTickEnabled = ProjectileMovement->IsComponentTickEnabled();
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->SetComponentTickEnabled(false);
		StruggleFreezeContext.FrozenProjectiles.Add(MoveTemp(ProjectileState));
	}
}

void UProjectDefeatFlowComponent::FreezeStruggleQueue()
{
	if (CurrentPhase != EProjectDefeatPhase::KnockedOut || !RealtimeSnapshotComponent || !GetOwner())
	{
		return;
	}

	InvalidateNearbyEnemyCache();
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const FProjectUnifiedRuntimeSnapshot Snapshot = RealtimeSnapshotComponent->BuildUnifiedRuntimeSnapshot();
	const FVector OwnerLocation = GetOwner()->GetActorLocation();

	PendingStruggleRounds.Reset();
	NextStruggleRoundIndex = 0;
	bStruggleQueueFrozen = true;
	bStruggleWonThisKnockout = false;

	for (const FProjectRealtimeActorHealthSnapshot& EnemySnapshot : Snapshot.ObservedEnemies)
	{
		AActor* EnemyActor = EnemySnapshot.Actor.Get();
		if (!EnemyActor
			|| EnemyActor->IsHidden()
			|| !EnemyActor->GetActorEnableCollision()
			|| EnemySnapshot.CurrentHealth <= KINDA_SMALL_NUMBER
			|| !EnemySnapshot.bRelevantEnemy
			|| !IsQualifiedEnemyActor(EnemyActor))
		{
			continue;
		}

		if (FVector::DistSquared(OwnerLocation, EnemyActor->GetActorLocation()) > FMath::Square(Settings ? Settings->StruggleEnemyRadius : 1000.f))
		{
			continue;
		}

		PendingStruggleRounds.Add(BuildStruggleRound(EnemyActor, PendingStruggleRounds.Num()));
	}

	EnforceMinimumTotalStruggleNotes();

	if (PendingStruggleRounds.Num() <= 0)
	{
		BeginWaitingOutOfCombatRecovery();
		return;
	}

	LogDefeatTransition(TEXT("StruggleQueueFrozen"), PendingStruggleRounds[0].EnemyActor.Get());
	StartNextStruggleRound();
}

void UProjectDefeatFlowComponent::EnforceMinimumTotalStruggleNotes()
{
	if (PendingStruggleRounds.Num() <= 0)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const int32 MinimumTotalNotes = Settings ? FMath::Max(Settings->MinTotalStruggleNoteCount, 1) : 20;
	const int32 MaxNotesPerRound = Settings ? FMath::Max(Settings->MaxStruggleNoteCount, 1) : 24;
	const float MaxSpacingSeconds = PendingStruggleRounds[0].NoteSpacingMaxSeconds > KINDA_SMALL_NUMBER
		? PendingStruggleRounds[0].NoteSpacingMaxSeconds
		: (Settings ? Settings->StruggleNoteSpacingMaxSeconds : 0.56f);
	FProjectDefeatFlowLogic::EnforceMinimumTotalStruggleNotes(
		PendingStruggleRounds,
		MinimumTotalNotes,
		MaxNotesPerRound,
		MaxSpacingSeconds);
}

FProjectStruggleRound UProjectDefeatFlowComponent::BuildStruggleRound(AActor* EnemyActor, const int32 RoundIndex) const
{
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	FProjectStruggleRound Round;
	Round.EnemyActor = EnemyActor;
	Round.EnemyClassName = EnemyActor && EnemyActor->GetClass() ? EnemyActor->GetClass()->GetFName() : NAME_None;
	Round.EnemyLevel = ResolveEnemyLevel(EnemyActor);
	Round.RoundIndex = RoundIndex;
	Round.ChartSeed = BuildRoundSeed(EnemyActor, RoundIndex);
	Round.CunningLevel = ResolveCunningLevel();

	const int32 DifficultyWeight = ResolveEnemyDifficultyWeight(EnemyActor) + FMath::Max(0, Round.RoundIndex / 2);
	Round.NoteCount = FMath::Clamp(
		(Settings ? Settings->BaseStruggleNoteCount : 4) + FMath::Max(1, DifficultyWeight / 2) + FMath::FloorToInt(static_cast<float>(Round.EnemyLevel) * 0.20f),
		Settings ? Settings->BaseStruggleNoteCount : 4,
		Settings ? Settings->MaxStruggleNoteCount : 14);

	const float BaseHitWindowSeconds = FMath::Max(
		Settings ? Settings->MinHitWindowSeconds : 0.18f,
		(Settings ? Settings->BaseHitWindowSeconds : 0.30f) - (DifficultyWeight * 0.0045f) - (Round.EnemyLevel * 0.0010f));
	const float BaseTravelTimeSeconds = FMath::Max(
		Settings ? Settings->MinTravelTimeSeconds : 1.20f,
		(Settings ? Settings->BaseTravelTimeSeconds : 1.75f) - (DifficultyWeight * 0.020f) - (Round.EnemyLevel * 0.004f));

	const UProjectInnerDoctrineSettings* DoctrineSettings = UProjectInnerDoctrineSettings::Get();
	Round.StruggleSpeedMultiplier = UProjectInnerDoctrineSettings::ComputeCunningStruggleSpeedMultiplier(
		Round.CunningLevel,
		DoctrineSettings ? DoctrineSettings->CunningStruggleSpeedBaseMultiplier : 1.30f,
		DoctrineSettings ? DoctrineSettings->CunningStruggleMaxSlowPct : 0.50f,
		DoctrineSettings ? DoctrineSettings->CunningStruggleTimePivot : 10.f);
	Round.TimeScale = 1.f / FMath::Max(Round.StruggleSpeedMultiplier, 0.001f);
	Round.HitWindowSeconds = UProjectInnerDoctrineSettings::ComputeCunningScaledStruggleSeconds(
		BaseHitWindowSeconds,
		Round.StruggleSpeedMultiplier,
		0.14f,
		0.36f);
	Round.TravelTimeSeconds = UProjectInnerDoctrineSettings::ComputeCunningScaledStruggleSeconds(
		BaseTravelTimeSeconds,
		Round.StruggleSpeedMultiplier,
		0.85f,
		2.40f);
	Round.NoteSpacingMinSeconds = FMath::Max(
		0.05f,
		(Settings ? Settings->StruggleNoteSpacingMinSeconds : 0.32f) * Round.TimeScale);
	Round.NoteSpacingMaxSeconds = FMath::Max(
		Round.NoteSpacingMinSeconds,
		(Settings ? Settings->StruggleNoteSpacingMaxSeconds : 0.56f) * Round.TimeScale);
	Round.MaxMissCount = UProjectInnerDoctrineSettings::ComputeCunningStruggleMaxMisses(
		Round.CunningLevel,
		Settings ? Settings->MaxStruggleMisses : 5,
		5,
		DoctrineSettings ? DoctrineSettings->CunningStruggleMilestoneMaxMisses : 10);
	Round.DurationSeconds = Round.TravelTimeSeconds + (Round.NoteCount * Round.NoteSpacingMaxSeconds);
	return Round;
}

void UProjectDefeatFlowComponent::StartNextStruggleRound()
{
	if (!PendingStruggleRounds.IsValidIndex(NextStruggleRoundIndex))
	{
		bStruggleWonThisKnockout = true;
		FinishKnockoutRecovery(false);
		return;
	}

	CurrentPhase = EProjectDefeatPhase::Struggle;
	BeginStruggleGameplayFreeze();
	SuspendKnockoutLocomotionForStruggle(true);
	EnsureStruggleWidget();
	if (!StruggleWidget)
	{
		EnterDefeated(EProjectDefeatReason::StruggleUnavailable, nullptr, false, true);
		return;
	}

	ApplyStruggleInputBlock(true);

	ActiveRound = PendingStruggleRounds[NextStruggleRoundIndex];
	OnKnockoutStateChanged.Broadcast(CurrentPhase, CurrentKnockoutReason, true);
	OnStruggleRoundStarted.Broadcast(ActiveRound);
	StruggleWidget->StartRound(ActiveRound);
	LogDefeatTransition(TEXT("StruggleRoundStarted"), ActiveRound.EnemyActor.Get());
}

void UProjectDefeatFlowComponent::HandleStruggleRoundCompleted(
	const bool bSuccess,
	const bool bPlayerCompletedMinigame,
	const FProjectStruggleRound& CompletedRound)
{
	OnStruggleRoundFinished.Broadcast(CompletedRound, bSuccess);
	LogDefeatTransition(
		bPlayerCompletedMinigame
			? (bSuccess ? TEXT("StruggleRoundWon") : TEXT("StruggleRoundLost"))
			: TEXT("StruggleRoundAborted"),
		CompletedRound.EnemyActor.Get());

	if (CurrentPhase != EProjectDefeatPhase::Struggle)
	{
		return;
	}

	if (!bSuccess)
	{
		EnterDefeated(
			bPlayerCompletedMinigame
				? EProjectDefeatReason::LostStruggle
				: EProjectDefeatReason::StruggleUnavailable,
			CompletedRound.EnemyActor.Get(),
			bPlayerCompletedMinigame,
			!bPlayerCompletedMinigame);
		return;
	}

	bStruggleWonThisKnockout = true;
	++NextStruggleRoundIndex;
	StartNextStruggleRound();
}

void UProjectDefeatFlowComponent::FinishKnockoutRecovery(const bool bApplyInventoryPenalty)
{
	if (CurrentPhase != EProjectDefeatPhase::KnockedOut && CurrentPhase != EProjectDefeatPhase::Struggle)
	{
		return;
	}

	InvalidateNearbyEnemyCache();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StruggleFreezeTimerHandle);
	}

	RemoveStruggleWidget();
	EndStruggleGameplayFreeze();
	ApplyStruggleInputBlock(false);
	SuspendKnockoutLocomotionForStruggle(false);
	ApplyKnockoutLocomotion(false);
	ApplyTransientInvulnerability(false);

	if (StatusComponent)
	{
		const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
		StatusComponent->SetForcedStatusActive(Settings ? Settings->KnockoutStatusName : TEXT("KnockedOut"), false);
		StatusComponent->ClearStatus(Settings ? Settings->KnockoutStatusName : TEXT("KnockedOut"));
	}

	if (CombatAttributeComponent && !CombatAttributeComponent->HealthAttributeName.IsNone())
	{
		const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
		const float MaxHealth = FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(CombatAttributeComponent->HealthAttributeName), 1.f);
		const float CurrentHealth = CombatAttributeComponent->GetAttributeCurrentValue(CombatAttributeComponent->HealthAttributeName);
		CombatAttributeComponent->SetAttributeCurrentValue(
			CombatAttributeComponent->HealthAttributeName,
			FMath::Max(CurrentHealth, MaxHealth * (Settings ? Settings->KnockoutRecoveryHealthPct : 0.50f)));
	}

	(void)bApplyInventoryPenalty;

	if (bStruggleWonThisKnockout)
	{
		ScheduleDeferredMovementRestore(1.0f);
	}

	SetPainCurrent(0.f);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (bStruggleWonThisKnockout && ActiveCombatSessionId != 0)
	{
		RegisterCombatEvent(Now, false);
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	if (ActiveCombatSessionId != 0
		&& !FProjectDefeatFlowLogic::ShouldKeepCombatSessionActive(
			Now,
			LastCombatEventTimeSeconds,
			Settings ? Settings->DefeatCombatWindowSeconds : 5.f,
			CountNearbyQualifiedStruggleEnemies()))
	{
		EndCombatSession();
	}
	else
	{
		RefreshCombatFlags();
	}

	if (ActiveCombatSessionId == 0)
	{
		SetLosingActive(false);
	}

	CurrentPhase = EProjectDefeatPhase::None;
	CurrentKnockoutReason = EProjectKnockoutReason::None;
	CurrentDefeatReason = EProjectDefeatReason::None;
	KnockoutEnteredTimeSeconds = 0.f;
	OutOfCombatRecoveryStartTimeSeconds = -1.f;
	ActiveRound = FProjectStruggleRound();
	PendingStruggleRounds.Reset();
	NextStruggleRoundIndex = 0;
	bStruggleQueueFrozen = false;
	bWaitingOutOfCombatRecovery = false;
	bStruggleWonThisKnockout = false;
	bPendingCrawlKnockout = false;
	PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	PendingCrawlKnockoutSourceId = NAME_None;
	OnKnockoutStateChanged.Broadcast(CurrentPhase, CurrentKnockoutReason, false);
	LogDefeatTransition(TEXT("KnockoutRecovered"), nullptr);
}

void UProjectDefeatFlowComponent::EnterDefeated(
	const EProjectDefeatReason DefeatReason,
	AActor* InstigatorActor,
	const bool bPlayerCompletedStruggleMinigame,
	const bool bTechnicalFailure,
	const bool bCancelledBeforeResolution)
{
	if (CurrentPhase == EProjectDefeatPhase::DefeatedBlackout
		|| CurrentPhase == EProjectDefeatPhase::TravelPending
		|| CurrentPhase == EProjectDefeatPhase::DefeatedScene)
	{
		return;
	}

	InvalidateNearbyEnemyCache();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StruggleFreezeTimerHandle);
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();

	bPendingCrawlKnockout = false;
	PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	PendingCrawlKnockoutSourceId = NAME_None;

	RemoveStruggleWidget();
	EndStruggleGameplayFreeze();
	ApplyStruggleInputBlock(false);
	SuspendKnockoutLocomotionForStruggle(false);
	ApplyTransientInvulnerability(true);
	RestoreDefaultInputMode();

	FProjectDefeatInventorySnapshot FullSnapshot;
	FProjectDefeatInventoryBridge::CaptureSnapshot(GetOwner(), UProjectDefeatFlowSettings::Get(), FullSnapshot);
	const int32 Seed = (GetWorld() ? FMath::RoundToInt(GetWorld()->GetTimeSeconds() * 997.f) : 0) + 37;
	const int32 CunningLevelBeforeRunDeath = ResolveCunningLevel();
	FProjectDefeatInventoryBridge::BuildDefeatedRetainedSnapshotForCunning(
		FullSnapshot,
		UProjectDefeatFlowSettings::Get(),
		Seed,
		CunningLevelBeforeRunDeath,
		10,
		PendingRetainedInventorySnapshot);

	CurrentDefeatReason = DefeatReason;
	CurrentPhase = EProjectDefeatPhase::DefeatedBlackout;
	PendingTransferPayload = FProjectDefeatTransferPayload();
	PendingTransferPayload.TransferId = FGuid::NewGuid();
	PendingTransferPayload.KnockoutReason = CurrentKnockoutReason;
	PendingTransferPayload.DefeatReason = CurrentDefeatReason;
	FProjectPostDefeatEligibilityContext PresentationContext;
	PresentationContext.DefeatReason = CurrentDefeatReason;
	PresentationContext.KnockoutReason = CurrentKnockoutReason;
	PresentationContext.bPlayerCompletedStruggleMinigame = bPlayerCompletedStruggleMinigame;
	PresentationContext.bMatureDefeatAllowed = IsMatureDefeatAllowedByPolicy();
	UProjectEmoteComponent* DefeatEmoteComponent = EnsureEmoteComponent();
	PresentationContext.bPresentationAvailable =
		Settings
		&& !Settings->DefaultSceneDefinition.InteractionId.IsNone()
		&& DefeatEmoteComponent
		&& DefeatEmoteComponent->IsMatureDefeatPresentationAvailableById(
			Settings->DefaultSceneDefinition.InteractionId);
	PresentationContext.bAuthority = GetOwner() && GetOwner()->HasAuthority();
	PresentationContext.bTechnicalFailure = bTechnicalFailure;
	PresentationContext.bCancelledBeforeResolution = bCancelledBeforeResolution;
	const float PresentationRoll = FProjectDefeatFlowLogic::IsMaturePresentationEligible(PresentationContext)
		? FMath::FRand()
		: -1.0f;
	FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(
		PendingTransferPayload,
		PresentationContext,
		PresentationRoll);
	PendingTransferPayload.SceneDefinition =
		PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette && Settings
			? Settings->DefaultSceneDefinition
			: FProjectDefeatSceneDefinition();
	PendingTransferPayload.CameraInputSnapshot = CaptureCameraInputSnapshot();
	ActiveCameraInputSnapshot = PendingTransferPayload.CameraInputSnapshot;
	PendingTransferPayload.RetainedEntryCount = PendingRetainedInventorySnapshot.InventoryEntries.Num();
	PendingTransferPayload.RetainedEquipmentCount = PendingRetainedInventorySnapshot.EquipmentEntries.Num();

	if (InnerDoctrineComponent)
	{
		InnerDoctrineComponent->HandleRunDeath();
	}

	SetPainCurrent(0.f);
	ActiveCombatSessionId = 0;
	LastCombatEventTimeSeconds = -FLT_MAX;
	LastKnockoutCombatSessionId = 0;
	RefreshCombatFlags();
	SetLosingActive(false);
	bStruggleQueueFrozen = false;
	bWaitingOutOfCombatRecovery = false;
	bStruggleWonThisKnockout = false;
	KnockoutEnteredTimeSeconds = 0.f;
	OutOfCombatRecoveryStartTimeSeconds = -1.f;
	OnKnockoutStateChanged.Broadcast(CurrentPhase, CurrentKnockoutReason, false);
	ApplyGameplayInputSuppression(true, false);
	StartPlayerCameraFade(0.f, 1.f, Settings ? Settings->DefeatedTravelDelaySeconds : 2.0f, true);
	LogDefeatTransition(TEXT("EnteredDefeated"), InstigatorActor);
	UE_LOG(
		LogProjectDefeatFlow,
		Display,
		TEXT("[DefeatOutcome] Transfer=%s Eligible=%s Roll=%.3f Presentation=%s"),
		*PendingTransferPayload.TransferId.ToString(EGuidFormats::DigitsWithHyphensLower),
		PendingTransferPayload.bPostDefeatPresentationEligible ? TEXT("true") : TEXT("false"),
		PendingTransferPayload.PostDefeatPresentationRoll,
		*GetEnumValueString(PendingTransferPayload.PostDefeatPresentation));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TravelDelayTimerHandle);
		World->GetTimerManager().SetTimer(
			TravelDelayTimerHandle,
			FTimerDelegate::CreateUObject(this, &ThisClass::TriggerPendingTravel),
			Settings ? Settings->DefeatedTravelDelaySeconds : 2.0f,
			false);
	}

	(void)InstigatorActor;
}

void UProjectDefeatFlowComponent::TriggerPendingTravel()
{
	InvalidateNearbyEnemyCache();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectDefeatTravelSubsystem* TravelSubsystem = GameInstance ? GameInstance->GetSubsystem<UProjectDefeatTravelSubsystem>() : nullptr;
	if (!TravelSubsystem || !TravelSubsystem->BeginDefeatedTravel(this, PendingTransferPayload, PendingRetainedInventorySnapshot))
	{
		LogDefeatTransition(TEXT("DefeatedTravelFallback"), nullptr);
		FProjectDefeatInventoryBridge::ApplyRetainedSubset(GetOwner(), PendingRetainedInventorySnapshot, UProjectDefeatFlowSettings::Get());
		ClampCurseAfterDefeat();
		if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette)
		{
			StartDefeatedScene(PendingTransferPayload.SceneDefinition);
		}
		else
		{
			CompleteDefeatedRespawnWithoutPresentation();
		}
		return;
	}

	CurrentPhase = EProjectDefeatPhase::TravelPending;
	LogDefeatTransition(TEXT("DefeatedTravelStarted"), nullptr);
}

void UProjectDefeatFlowComponent::ScheduleDefeatedArrivalSceneStart()
{
	if (PendingTransferPayload.PostDefeatPresentation != EProjectPostDefeatPresentation::MatureSoloVignette)
	{
		CompleteDefeatedRespawnWithoutPresentation();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		StartSettledDefeatedArrivalScene();
		return;
	}

	World->GetTimerManager().ClearTimer(DefeatedArrivalSceneStartTimerHandle);
	World->GetTimerManager().SetTimer(
		DefeatedArrivalSceneStartTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::StartSettledDefeatedArrivalScene),
		DefeatedArrivalSceneStartSettleSeconds,
		false);
}

void UProjectDefeatFlowComponent::StartSettledDefeatedArrivalScene()
{
	RefreshDependencies();
	if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette)
	{
		StartDefeatedScene(PendingTransferPayload.SceneDefinition);
	}
	else
	{
		CompleteDefeatedRespawnWithoutPresentation();
	}
}

void UProjectDefeatFlowComponent::CompleteDefeatedRespawnWithoutPresentation()
{
	InvalidateNearbyEnemyCache();
	RefreshDependencies();
	ClearDefeatStatusAndLocomotion();
	RestorePostDefeatGameplayState(false, TEXT("DirectRespawn"));
	ResetDefeatFlowAfterRespawn(TEXT("DefeatedRespawnDirect"));
}

void UProjectDefeatFlowComponent::StartDefeatedScene(const FProjectDefeatSceneDefinition& SceneDefinition)
{
	const bool bStoredPresentationValid =
		FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(PendingTransferPayload);
	const bool bMatureDefeatAllowed = IsMatureDefeatAllowedByPolicy();
	if (!bStoredPresentationValid || !bMatureDefeatAllowed)
	{
		if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette)
		{
			PendingTransferPayload.bPostDefeatPresentationDowngradedByPolicy =
				bStoredPresentationValid && !bMatureDefeatAllowed;
			PendingTransferPayload.bTechnicalFailure =
				PendingTransferPayload.bTechnicalFailure || !bStoredPresentationValid;
			PendingTransferPayload.PostDefeatPresentation = EProjectPostDefeatPresentation::None;
			PendingTransferPayload.SceneDefinition = FProjectDefeatSceneDefinition();
		}
		CompleteDefeatedRespawnWithoutPresentation();
		return;
	}

	InvalidateNearbyEnemyCache();
	CurrentPhase = EProjectDefeatPhase::DefeatedScene;
	ApplyTransientInvulnerability(true);
	RemoveSceneInputBinding();
	UnbindRuntimeActionEvents();
	RestoreDefaultInputMode();
	ReleaseDefeatedSceneControllerState();
	RestoreCameraInputSnapshot(ActiveCameraInputSnapshot, TEXT("StartDefeatedScene"));
	if (bPendingDefeatedArrivalCameraRefresh)
	{
		bPendingDefeatedArrivalCameraRefresh = false;
		RefreshCinematicCameraManager(TEXT("DefeatedArrivalSettled"));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredMovementRestoreTimerHandle);
	}

	// Release the forced knockout crawl only once the optional presentation is ready
	// to take over, which keeps the player downed during blackout and travel.
	ClearDefeatStatusAndLocomotion();

	StartPlayerCameraFade(1.f, 1.f, 0.f, true);

	const bool bStartedBlueprintScene = BlueprintBridgeComponent
		? BlueprintBridgeComponent->TryStartExternalRuntimeScene(PendingTransferPayload)
		: false;
	const bool bStartedRuntimeAction = !bStartedBlueprintScene && StartDefeatedSceneRuntimeAction(SceneDefinition);
	if (bStartedBlueprintScene)
	{
		EnsureSceneInputBinding();
		ApplyDefeatedSceneInputLock();
	}
	else if (!bStartedRuntimeAction)
	{
		UE_LOG(
			LogProjectDefeatFlow,
			Warning,
			TEXT("The selected mature defeat vignette was unavailable; falling back to direct respawn."));
		PendingTransferPayload.bTechnicalFailure = true;
		PendingTransferPayload.PostDefeatPresentation = EProjectPostDefeatPresentation::None;
		PendingTransferPayload.SceneDefinition = FProjectDefeatSceneDefinition();
		CompleteDefeatedRespawnWithoutPresentation();
		return;
	}

	OnDefeatedSceneChanged.Broadcast(SceneDefinition, true);
	LogDefeatTransition(TEXT("DefeatedSceneStarted"), nullptr);
}

bool UProjectDefeatFlowComponent::StartDefeatedSceneRuntimeAction(const FProjectDefeatSceneDefinition& SceneDefinition)
{
	UWorld* World = GetWorld();
	UProjectEmoteSubsystem* EmoteSubsystem = World ? World->GetSubsystem<UProjectEmoteSubsystem>() : nullptr;
	if (!EmoteSubsystem)
	{
		UE_LOG(LogProjectDefeatFlow, Warning, TEXT("Defeated scene failed to start: ProjectEmoteSubsystem is unavailable."));
		return false;
	}

	FProjectEmoteRuntimeActionRequest Request;
	Request.RuntimeActionId = DefeatedRespawnRuntimeActionId;
	Request.InteractionId = SceneDefinition.InteractionId.IsNone() ? DefeatedRespawnRuntimeActionId : SceneDefinition.InteractionId;
	Request.Source = EProjectEmoteRuntimeActionSource::Respawn;
	Request.bAllowCancel = SceneDefinition.bAllowCancel;
	Request.bCancelWithY = true;
	Request.bRestoreMovementOnEnd = true;
	Request.bHiddenFromMenu = true;

	ActiveDefeatedRuntimeActionId = Request.RuntimeActionId;
	UnbindRuntimeActionEvents();
	RuntimeActionEndedDelegateHandle = EmoteSubsystem->OnRuntimeActionEnded.AddUObject(this, &ThisClass::HandleRuntimeActionEnded);

	if (!EmoteSubsystem->StartRuntimeAction(Request))
	{
		UE_LOG(LogProjectDefeatFlow, Warning, TEXT("Defeated scene failed to start runtime action %s."), *Request.RuntimeActionId.ToString());
		UnbindRuntimeActionEvents();
		ActiveDefeatedRuntimeActionId = NAME_None;
		return false;
	}

	return true;
}

void UProjectDefeatFlowComponent::StopDefeatedScene(const bool bCancelledByPlayer)
{
	if (CurrentPhase != EProjectDefeatPhase::DefeatedScene)
	{
		return;
	}

	bStoppingDefeatedScene = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SceneCancelTimerHandle);
		World->GetTimerManager().ClearTimer(DeferredMovementRestoreTimerHandle);
	}

	const bool bHadRuntimeAction = !ActiveDefeatedRuntimeActionId.IsNone();
	if (bHadRuntimeAction)
	{
		if (UProjectEmoteSubsystem* EmoteSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UProjectEmoteSubsystem>() : nullptr)
		{
			UnbindRuntimeActionEvents();
			if (EmoteSubsystem->IsRuntimeActionActive() && EmoteSubsystem->GetActiveRuntimeActionId() == ActiveDefeatedRuntimeActionId)
			{
				EmoteSubsystem->CancelRuntimeAction(TEXT("DefeatFlowStop"));
			}
		}
		ActiveDefeatedRuntimeActionId = NAME_None;
	}

	const bool bStoppedBlueprintScene = BlueprintBridgeComponent
		? BlueprintBridgeComponent->TryStopExternalRuntimeScene(PendingTransferPayload, bCancelledByPlayer)
		: false;
	if (!bStoppedBlueprintScene && !bHadRuntimeAction)
	{
		if (UProjectEmoteComponent* LocalEmoteComponent = EnsureEmoteComponent())
		{
			if (LocalEmoteComponent->IsEmoteActive())
			{
				LocalEmoteComponent->StopEmote();
				if (bCancelledByPlayer)
				{
					LocalEmoteComponent->OverrideDelayedPostEmoteRecovery(1.0f, false, false);
				}
			}
		}
	}

	ClearDefeatStatusAndLocomotion();
	RestorePostDefeatGameplayState(bCancelledByPlayer, TEXT("StopDefeatedScene"));
	OnDefeatedSceneChanged.Broadcast(PendingTransferPayload.SceneDefinition, false);
	ResetDefeatFlowAfterRespawn(bCancelledByPlayer ? TEXT("DefeatedSceneCancelled") : TEXT("DefeatedSceneStopped"));
}

void UProjectDefeatFlowComponent::HandleRuntimeActionEnded(
	const FProjectEmoteRuntimeActionRequest& Request,
	const EProjectEmoteRuntimeActionEndReason EndReason)
{
	if (ActiveDefeatedRuntimeActionId.IsNone() || Request.RuntimeActionId != ActiveDefeatedRuntimeActionId)
	{
		return;
	}

	UnbindRuntimeActionEvents();
	ActiveDefeatedRuntimeActionId = NAME_None;

	if (CurrentPhase != EProjectDefeatPhase::DefeatedScene || bStoppingDefeatedScene)
	{
		return;
	}

	const bool bCancelledByPlayer = EndReason == EProjectEmoteRuntimeActionEndReason::Cancelled;
	StopDefeatedScene(bCancelledByPlayer);
}

void UProjectDefeatFlowComponent::UnbindRuntimeActionEvents()
{
	if (!RuntimeActionEndedDelegateHandle.IsValid())
	{
		return;
	}

	if (UProjectEmoteSubsystem* EmoteSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UProjectEmoteSubsystem>() : nullptr)
	{
		EmoteSubsystem->OnRuntimeActionEnded.Remove(RuntimeActionEndedDelegateHandle);
	}

	RuntimeActionEndedDelegateHandle.Reset();
}

void UProjectDefeatFlowComponent::HandleDefeatedSceneCancelPressed()
{
	if (CurrentPhase != EProjectDefeatPhase::DefeatedScene || !PendingTransferPayload.SceneDefinition.bAllowCancel)
	{
		return;
	}

	StartPlayerCameraFade(0.f, 1.f, PendingTransferPayload.SceneDefinition.CancelFadeSeconds, true);
	LogDefeatTransition(TEXT("DefeatedSceneCancelRequested"), nullptr);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SceneCancelTimerHandle);
		World->GetTimerManager().SetTimer(
			SceneCancelTimerHandle,
			FTimerDelegate::CreateUObject(this, &ThisClass::FinalizeDefeatedSceneCancel),
			PendingTransferPayload.SceneDefinition.CancelFadeSeconds,
			false);
	}
}

void UProjectDefeatFlowComponent::FinalizeDefeatedSceneCancel()
{
	StopDefeatedScene(true);
}

void UProjectDefeatFlowComponent::ScheduleDeferredMovementRestore(const float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		RestoreDeferredPlayerMovement();
		return;
	}

	World->GetTimerManager().ClearTimer(DeferredMovementRestoreTimerHandle);
	World->GetTimerManager().SetTimer(
		DeferredMovementRestoreTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::RestoreDeferredPlayerMovement),
		FMath::Max(DelaySeconds, 0.f),
		false);
}

void UProjectDefeatFlowComponent::ScheduleDefeatedCancelMovementRestore()
{
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	ScheduleDeferredMovementRestore(Settings ? Settings->DefeatedCancelMovementRestoreDelaySeconds : 1.0f);
}

void UProjectDefeatFlowComponent::RestoreDeferredPlayerMovement()
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!PlayerController)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	SuspendKnockoutLocomotionForStruggle(false);
	ApplyKnockoutLocomotion(false);
	if (LocomotionOverrideComponent)
	{
		LocomotionOverrideComponent->SetWalkModeEnabled(false);
	}

	if (StatusComponent)
	{
		StatusComponent->SetForcedStatusActive(Settings ? Settings->KnockoutStatusName : TEXT("KnockedOut"), false);
		StatusComponent->ClearStatus(Settings ? Settings->KnockoutStatusName : TEXT("KnockedOut"));
	}

	PlayerController->ResetIgnoreMoveInput();
	PlayerController->ResetIgnoreLookInput();
	ReleaseDefeatedSceneInputLock(true);
	RestoreCameraInputSnapshot(ActiveCameraInputSnapshot, TEXT("RestoreDeferredPlayerMovement"));

	if (CharacterOwner)
	{
		if (UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement())
		{
			CharacterMovement->SetComponentTickEnabled(true);
			CharacterMovement->Activate(true);
			CharacterMovement->SetMovementMode(MOVE_Walking);
		}
	}

	RefreshCinematicCameraManager(TEXT("RestoreDeferredPlayerMovement"));
}

void UProjectDefeatFlowComponent::RestorePostDefeatGameplayState(
	const bool bDeferMovementRestore,
	const TCHAR* Reason)
{
	RemoveSceneInputBinding();
	UnbindRuntimeActionEvents();
	ApplyTransientInvulnerability(false);

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	RestoreCameraInputSnapshot(ActiveCameraInputSnapshot, Reason);
	if (bDeferMovementRestore)
	{
		ApplyDefeatedSceneInputLock();
		ScheduleDefeatedCancelMovementRestore();
	}
	else
	{
		RefreshCinematicCameraManager(Reason);
		ReleaseDefeatedSceneInputLock(true);
	}

	if (!EmoteComponent || !EmoteComponent->IsEmoteActive())
	{
		StartPlayerCameraFade(1.f, 0.f, 0.15f, false);
	}
}

void UProjectDefeatFlowComponent::ResetDefeatFlowAfterRespawn(const TCHAR* EventLabel)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TravelDelayTimerHandle);
		World->GetTimerManager().ClearTimer(DefeatedArrivalSceneStartTimerHandle);
		World->GetTimerManager().ClearTimer(SceneCancelTimerHandle);
	}

	CurrentPhase = EProjectDefeatPhase::None;
	CurrentKnockoutReason = EProjectKnockoutReason::None;
	CurrentDefeatReason = EProjectDefeatReason::None;
	PendingTransferPayload = FProjectDefeatTransferPayload();
	PendingRetainedInventorySnapshot = FProjectDefeatInventorySnapshot();
	bPendingDefeatedArrivalCameraRefresh = false;
	RefreshCombatFlags();
	SetLosingActive(false);
	BroadcastStateRefresh();
	bStoppingDefeatedScene = false;
	LogDefeatTransition(EventLabel, nullptr);
}

void UProjectDefeatFlowComponent::ClearDefeatStatusAndLocomotion()
{
	SuspendKnockoutLocomotionForStruggle(false);
	ApplyKnockoutLocomotion(false);

	if (StatusComponent)
	{
		const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
		const FName KnockoutStatusName = Settings ? Settings->KnockoutStatusName : FName(TEXT("KnockedOut"));
		StatusComponent->SetForcedStatusActive(KnockoutStatusName, false);
		StatusComponent->ClearStatus(KnockoutStatusName);
	}
}

void UProjectDefeatFlowComponent::ClampCurseAfterDefeat() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const float MaximumCurse = FMath::Clamp(
		Settings ? Settings->DefeatedArrivalCurseMaximum : 60.0f,
		0.0f,
		100.0f);

	if (OwnerActor->GetClass()->ImplementsInterface(UProjectDefeatCurseClampReceiver::StaticClass()))
	{
		IProjectDefeatCurseClampReceiver::Execute_ClampCurseAfterDefeat(OwnerActor, MaximumCurse);
	}

	TInlineComponentArray<UActorComponent*> Components(OwnerActor);
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->GetClass()->ImplementsInterface(UProjectDefeatCurseClampReceiver::StaticClass()))
		{
			IProjectDefeatCurseClampReceiver::Execute_ClampCurseAfterDefeat(Component, MaximumCurse);
		}
	}
}

bool UProjectDefeatFlowComponent::IsMatureDefeatAllowedByPolicy() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	return ContentPolicy && ContentPolicy->IsMatureDefeatAllowed();
}

void UProjectDefeatFlowComponent::ApplyDefeatedSceneInputLock()
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	ApplyGameplayInputSuppression(true, false);

	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		if (!bDefeatedSceneInputLockApplied)
		{
			CharacterOwner->DisableInput(PlayerController);
			bDefeatedSceneInputLockApplied = true;
		}

		if (UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement())
		{
			CharacterMovement->StopMovementImmediately();
			CharacterMovement->DisableMovement();
		}
	}
}

void UProjectDefeatFlowComponent::ReleaseDefeatedSceneInputLock(const bool bForceWalking)
{
	ReleaseDefeatedSceneControllerState();

	const bool bShouldRestorePawnInput = bDefeatedSceneInputLockApplied;
	bDefeatedSceneInputLockApplied = false;

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		if (bShouldRestorePawnInput)
		{
			CharacterOwner->EnableInput(PlayerController);
		}

		if (UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement())
		{
			CharacterMovement->SetComponentTickEnabled(true);
			CharacterMovement->Activate(true);
			if (bForceWalking || CharacterMovement->MovementMode == MOVE_None)
			{
				CharacterMovement->SetMovementMode(MOVE_Walking);
			}
		}
	}
}

void UProjectDefeatFlowComponent::ReleaseDefeatedSceneControllerState()
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	PlayerController->ResetIgnoreMoveInput();
	PlayerController->ResetIgnoreLookInput();
	ApplyGameplayInputSuppression(false, true);
}

FProjectDefeatCameraInputSnapshot UProjectDefeatFlowComponent::CaptureCameraInputSnapshot() const
{
	FProjectDefeatCameraInputSnapshot Snapshot;
	const APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return Snapshot;
	}

	if (PlayerController->PlayerInput)
	{
		Snapshot.bHasPlayerInputSensitivity = true;
		Snapshot.MouseSensitivityX = PlayerController->PlayerInput->GetMouseSensitivityX();
		Snapshot.MouseSensitivityY = PlayerController->PlayerInput->GetMouseSensitivityY();
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Snapshot.bHasLegacyInputScales = true;
	Snapshot.LegacyYawScale = PlayerController->GetDeprecatedInputYawScale();
	Snapshot.LegacyPitchScale = PlayerController->GetDeprecatedInputPitchScale();
	Snapshot.LegacyRollScale = PlayerController->GetDeprecatedInputRollScale();
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (const APawn* PlayerPawn = PlayerController->GetPawn())
	{
		Snapshot.bHasPawnCameraRig = true;
		Snapshot.bPawnUseControllerRotationPitch = PlayerPawn->bUseControllerRotationPitch;
		Snapshot.bPawnUseControllerRotationYaw = PlayerPawn->bUseControllerRotationYaw;
		Snapshot.bPawnUseControllerRotationRoll = PlayerPawn->bUseControllerRotationRoll;

		TArray<UCameraComponent*> CameraComponents;
		PlayerPawn->GetComponents<UCameraComponent>(CameraComponents);
		for (const UCameraComponent* CameraComponent : CameraComponents)
		{
			if (!CameraComponent)
			{
				continue;
			}

			FProjectDefeatCameraComponentSnapshot ComponentSnapshot;
			ComponentSnapshot.ComponentName = CameraComponent->GetName();
			ComponentSnapshot.bWasActive = CameraComponent->IsActive();
			ComponentSnapshot.bUsePawnControlRotation = CameraComponent->bUsePawnControlRotation;
			ComponentSnapshot.FieldOfView = CameraComponent->FieldOfView;
			Snapshot.CameraComponents.Add(ComponentSnapshot);
		}

		TArray<USpringArmComponent*> SpringArmComponents;
		PlayerPawn->GetComponents<USpringArmComponent>(SpringArmComponents);
		for (const USpringArmComponent* SpringArmComponent : SpringArmComponents)
		{
			if (!SpringArmComponent)
			{
				continue;
			}

			FProjectDefeatSpringArmSnapshot ArmSnapshot;
			ArmSnapshot.ComponentName = SpringArmComponent->GetName();
			ArmSnapshot.RelativeLocation = SpringArmComponent->GetRelativeLocation();
			ArmSnapshot.RelativeRotation = SpringArmComponent->GetRelativeRotation();
			ArmSnapshot.TargetArmLength = SpringArmComponent->TargetArmLength;
			ArmSnapshot.SocketOffset = SpringArmComponent->SocketOffset;
			ArmSnapshot.TargetOffset = SpringArmComponent->TargetOffset;
			ArmSnapshot.bUsePawnControlRotation = SpringArmComponent->bUsePawnControlRotation;
			ArmSnapshot.bInheritPitch = SpringArmComponent->bInheritPitch;
			ArmSnapshot.bInheritYaw = SpringArmComponent->bInheritYaw;
			ArmSnapshot.bInheritRoll = SpringArmComponent->bInheritRoll;
			ArmSnapshot.bDoCollisionTest = SpringArmComponent->bDoCollisionTest;
			ArmSnapshot.bEnableCameraLag = SpringArmComponent->bEnableCameraLag;
			ArmSnapshot.bEnableCameraRotationLag = SpringArmComponent->bEnableCameraRotationLag;
			ArmSnapshot.CameraLagSpeed = SpringArmComponent->CameraLagSpeed;
			ArmSnapshot.CameraRotationLagSpeed = SpringArmComponent->CameraRotationLagSpeed;
			ArmSnapshot.CameraLagMaxDistance = SpringArmComponent->CameraLagMaxDistance;
			Snapshot.SpringArmComponents.Add(ArmSnapshot);
		}
	}

	UE_LOG(
		LogProjectDefeatFlow,
		VeryVerbose,
		TEXT("[DefeatFlow] Captured camera input snapshot: Mouse=(%.4f, %.4f), Legacy=(Yaw %.4f, Pitch %.4f, Roll %.4f), CameraRig=(PawnFlags %s/%s/%s, Cameras %d, Arms %d)."),
		Snapshot.MouseSensitivityX,
		Snapshot.MouseSensitivityY,
		Snapshot.LegacyYawScale,
		Snapshot.LegacyPitchScale,
		Snapshot.LegacyRollScale,
		Snapshot.bPawnUseControllerRotationPitch ? TEXT("true") : TEXT("false"),
		Snapshot.bPawnUseControllerRotationYaw ? TEXT("true") : TEXT("false"),
		Snapshot.bPawnUseControllerRotationRoll ? TEXT("true") : TEXT("false"),
		Snapshot.CameraComponents.Num(),
		Snapshot.SpringArmComponents.Num());

	return Snapshot;
}

void UProjectDefeatFlowComponent::RestoreCameraInputSnapshot(
	const FProjectDefeatCameraInputSnapshot& Snapshot,
	const TCHAR* Reason) const
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	const float PreviousMouseSensitivityX = PlayerController->PlayerInput ? PlayerController->PlayerInput->GetMouseSensitivityX() : 0.0f;
	const float PreviousMouseSensitivityY = PlayerController->PlayerInput ? PlayerController->PlayerInput->GetMouseSensitivityY() : 0.0f;

	if (Snapshot.bHasPlayerInputSensitivity && PlayerController->PlayerInput)
	{
		PlayerController->PlayerInput->SetMouseSensitivity(Snapshot.MouseSensitivityX, Snapshot.MouseSensitivityY);
		PlayerController->PlayerInput->ClearSmoothing();
	}

	float PreviousYawScale = 1.0f;
	float PreviousPitchScale = 1.0f;
	float PreviousRollScale = 1.0f;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PreviousYawScale = PlayerController->GetDeprecatedInputYawScale();
	PreviousPitchScale = PlayerController->GetDeprecatedInputPitchScale();
	PreviousRollScale = PlayerController->GetDeprecatedInputRollScale();
	if (Snapshot.bHasLegacyInputScales)
	{
		PlayerController->SetDeprecatedInputYawScale(Snapshot.LegacyYawScale);
		PlayerController->SetDeprecatedInputPitchScale(Snapshot.LegacyPitchScale);
		PlayerController->SetDeprecatedInputRollScale(Snapshot.LegacyRollScale);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	int32 RestoredCameraComponents = 0;
	int32 RestoredSpringArmComponents = 0;
	if (Snapshot.bHasPawnCameraRig)
	{
		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			PlayerPawn->bUseControllerRotationPitch = Snapshot.bPawnUseControllerRotationPitch;
			PlayerPawn->bUseControllerRotationYaw = Snapshot.bPawnUseControllerRotationYaw;
			PlayerPawn->bUseControllerRotationRoll = Snapshot.bPawnUseControllerRotationRoll;

			TArray<UCameraComponent*> CameraComponents;
			PlayerPawn->GetComponents<UCameraComponent>(CameraComponents);
			for (UCameraComponent* CameraComponent : CameraComponents)
			{
				if (!CameraComponent)
				{
					continue;
				}

				const FProjectDefeatCameraComponentSnapshot* ComponentSnapshot = Snapshot.CameraComponents.FindByPredicate(
					[CameraComponent](const FProjectDefeatCameraComponentSnapshot& Candidate)
					{
						return Candidate.ComponentName == CameraComponent->GetName();
					});

				if (!ComponentSnapshot)
				{
					continue;
				}

				CameraComponent->bUsePawnControlRotation = ComponentSnapshot->bUsePawnControlRotation;
				CameraComponent->SetFieldOfView(ComponentSnapshot->FieldOfView);
				CameraComponent->SetActive(ComponentSnapshot->bWasActive);
				++RestoredCameraComponents;
			}

			TArray<USpringArmComponent*> SpringArmComponents;
			PlayerPawn->GetComponents<USpringArmComponent>(SpringArmComponents);
			for (USpringArmComponent* SpringArmComponent : SpringArmComponents)
			{
				if (!SpringArmComponent)
				{
					continue;
				}

				const FProjectDefeatSpringArmSnapshot* ArmSnapshot = Snapshot.SpringArmComponents.FindByPredicate(
					[SpringArmComponent](const FProjectDefeatSpringArmSnapshot& Candidate)
					{
						return Candidate.ComponentName == SpringArmComponent->GetName();
					});

				if (!ArmSnapshot)
				{
					continue;
				}

				SpringArmComponent->SetRelativeLocation(ArmSnapshot->RelativeLocation);
				SpringArmComponent->SetRelativeRotation(ArmSnapshot->RelativeRotation);
				SpringArmComponent->TargetArmLength = ArmSnapshot->TargetArmLength;
				SpringArmComponent->SocketOffset = ArmSnapshot->SocketOffset;
				SpringArmComponent->TargetOffset = ArmSnapshot->TargetOffset;
				SpringArmComponent->bUsePawnControlRotation = ArmSnapshot->bUsePawnControlRotation;
				SpringArmComponent->bInheritPitch = ArmSnapshot->bInheritPitch;
				SpringArmComponent->bInheritYaw = ArmSnapshot->bInheritYaw;
				SpringArmComponent->bInheritRoll = ArmSnapshot->bInheritRoll;
				SpringArmComponent->bDoCollisionTest = ArmSnapshot->bDoCollisionTest;
				SpringArmComponent->bEnableCameraLag = ArmSnapshot->bEnableCameraLag;
				SpringArmComponent->bEnableCameraRotationLag = ArmSnapshot->bEnableCameraRotationLag;
				SpringArmComponent->CameraLagSpeed = ArmSnapshot->CameraLagSpeed;
				SpringArmComponent->CameraRotationLagSpeed = ArmSnapshot->CameraRotationLagSpeed;
				SpringArmComponent->CameraLagMaxDistance = ArmSnapshot->CameraLagMaxDistance;
				++RestoredSpringArmComponents;
			}
		}
	}

	UE_LOG(
		LogProjectDefeatFlow,
		VeryVerbose,
		TEXT("[DefeatFlow] Restored camera input snapshot (%s): Mouse %.4f/%.4f -> %.4f/%.4f, Legacy YPR %.4f/%.4f/%.4f -> %.4f/%.4f/%.4f, CameraRig=(PawnFlags %s/%s/%s, Cameras %d/%d, Arms %d/%d)."),
		Reason ? Reason : TEXT("Unknown"),
		PreviousMouseSensitivityX,
		PreviousMouseSensitivityY,
		Snapshot.MouseSensitivityX,
		Snapshot.MouseSensitivityY,
		PreviousYawScale,
		PreviousPitchScale,
		PreviousRollScale,
		Snapshot.LegacyYawScale,
		Snapshot.LegacyPitchScale,
		Snapshot.LegacyRollScale,
		Snapshot.bPawnUseControllerRotationPitch ? TEXT("true") : TEXT("false"),
		Snapshot.bPawnUseControllerRotationYaw ? TEXT("true") : TEXT("false"),
		Snapshot.bPawnUseControllerRotationRoll ? TEXT("true") : TEXT("false"),
		RestoredCameraComponents,
		Snapshot.CameraComponents.Num(),
		RestoredSpringArmComponents,
		Snapshot.SpringArmComponents.Num());
}

void UProjectDefeatFlowComponent::RefreshCinematicCameraManager(const TCHAR* Reason) const
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	APawn* PlayerPawn = PlayerController->GetPawn();
	ACCMPlayerCameraManager* CinematicCameraManager = Cast<ACCMPlayerCameraManager>(PlayerController->PlayerCameraManager);
	if (!CinematicCameraManager)
	{
		UE_LOG(
			LogProjectDefeatFlow,
			Verbose,
			TEXT("[DefeatFlow] Skipped ACF cinematic camera refresh (%s): PlayerCameraManager=%s."),
			Reason ? Reason : TEXT("Unknown"),
			*GetNameSafe(PlayerController->PlayerCameraManager));
		return;
	}

	if (PlayerPawn && PlayerController->GetViewTarget() != PlayerPawn)
	{
		PlayerController->SetViewTarget(PlayerPawn);
	}

	// Clear the old pointers first so ACF does not reset the newly restored
	// spring arm/FOV with a stale pre-travel baseline while rebinding.
	CinematicCameraManager->StopLookingActor();
	CinematicCameraManager->OverrideCameraReferences(nullptr, nullptr);
	CinematicCameraManager->UpdateCameraReferences();
	CinematicCameraManager->ResetCameraPosition(true);

	UE_LOG(
		LogProjectDefeatFlow,
		Verbose,
		TEXT("[DefeatFlow] Refreshed ACF cinematic camera manager (%s): Manager=%s Pawn=%s ViewTarget=%s PawnLocation=%s."),
		Reason ? Reason : TEXT("Unknown"),
		*CinematicCameraManager->GetName(),
		*GetNameSafe(PlayerPawn),
		*GetNameSafe(PlayerController->GetViewTarget()),
		PlayerPawn ? *PlayerPawn->GetActorLocation().ToCompactString() : TEXT("None"));
}

void UProjectDefeatFlowComponent::ApplyKnockoutLocomotion(const bool bEnabled)
{
	if (!bEnabled)
	{
		if (LocomotionOverrideComponent && bKnockoutLocomotionActive)
		{
			LocomotionOverrideComponent->SetCrawlModeEnabled(false);
			if (!bKnockoutWalkWasEnabled)
			{
				LocomotionOverrideComponent->SetWalkModeEnabled(false);
			}
		}

		bKnockoutLocomotionActive = false;
		bKnockoutWalkWasEnabled = false;
		return;
	}

	UProjectLocomotionOverrideComponent* LocalLocomotionOverride = EnsureLocomotionOverrideComponent();
	if (!LocalLocomotionOverride)
	{
		return;
	}

	if (bKnockoutLocomotionActive)
	{
		LocalLocomotionOverride->SetWalkModeEnabled(true);
		LocalLocomotionOverride->SetCrawlModeEnabled(true);
		return;
	}

	bKnockoutWalkWasEnabled = LocalLocomotionOverride->IsWalkModeEnabled();
	LocalLocomotionOverride->SetWalkModeEnabled(true);
	LocalLocomotionOverride->SetCrawlModeEnabled(true);
	bKnockoutLocomotionActive = true;
}

void UProjectDefeatFlowComponent::SuspendKnockoutLocomotionForStruggle(const bool bSuspend)
{
	if (!bKnockoutLocomotionActive || !LocomotionOverrideComponent)
	{
		bKnockoutLocomotionSuspended = false;
		return;
	}

	// During the struggle pause we keep crawl forced so the downed silhouette
	// remains stable instead of dropping back to the standing locomotion graph.
	LocomotionOverrideComponent->SetWalkModeEnabled(true);
	LocomotionOverrideComponent->SetCrawlModeEnabled(true);
	LocomotionOverrideComponent->SetComponentTickEnabled(true);
	bKnockoutLocomotionSuspended = bSuspend;
}

void UProjectDefeatFlowComponent::ApplyTransientInvulnerability(const bool bEnabled)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (bEnabled)
	{
		ApplyCharacterMeshCollisionSuppression(true);

		if (!bChangedCanBeDamaged)
		{
			bPreviousCanBeDamaged = OwnerActor->CanBeDamaged();
			OwnerActor->SetCanBeDamaged(false);
			bChangedCanBeDamaged = true;
		}
		return;
	}

	if (bChangedCanBeDamaged)
	{
		OwnerActor->SetCanBeDamaged(bPreviousCanBeDamaged);
		bChangedCanBeDamaged = false;
		bPreviousCanBeDamaged = true;
	}

	ApplyCharacterMeshCollisionSuppression(false);
}

void UProjectDefeatFlowComponent::ApplyCharacterMeshCollisionSuppression(const bool bEnabled)
{
	USkeletalMeshComponent* CharacterMesh = bEnabled
		? ResolveOwnerCharacterMesh()
		: SuppressedCharacterMeshComponent.Get();
	if (!CharacterMesh)
	{
		if (!bEnabled)
		{
			bChangedCharacterMeshCollision = false;
			bPreviousCharacterMeshGenerateOverlapEvents = false;
			PreviousCharacterMeshCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
			PreviousCharacterMeshCollisionProfileName = NAME_None;
			SuppressedCharacterMeshComponent.Reset();
		}

		return;
	}

	if (bEnabled)
	{
		if (bChangedCharacterMeshCollision)
		{
			return;
		}

		PreviousCharacterMeshCollisionProfileName = CharacterMesh->GetCollisionProfileName();
		PreviousCharacterMeshCollisionEnabled = CharacterMesh->GetCollisionEnabled();
		bPreviousCharacterMeshGenerateOverlapEvents = CharacterMesh->GetGenerateOverlapEvents();
		SuppressedCharacterMeshComponent = CharacterMesh;
		bChangedCharacterMeshCollision = true;

		CharacterMesh->SetGenerateOverlapEvents(false);
		CharacterMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	if (!bChangedCharacterMeshCollision)
	{
		return;
	}

	if (!PreviousCharacterMeshCollisionProfileName.IsNone())
	{
		CharacterMesh->SetCollisionProfileName(PreviousCharacterMeshCollisionProfileName);
	}

	CharacterMesh->SetCollisionEnabled(PreviousCharacterMeshCollisionEnabled);
	CharacterMesh->SetGenerateOverlapEvents(bPreviousCharacterMeshGenerateOverlapEvents);

	bChangedCharacterMeshCollision = false;
	bPreviousCharacterMeshGenerateOverlapEvents = false;
	PreviousCharacterMeshCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	PreviousCharacterMeshCollisionProfileName = NAME_None;
	SuppressedCharacterMeshComponent.Reset();
}

void UProjectDefeatFlowComponent::ApplyGameplayInputSuppression(
	const bool bEnabled,
	const bool bRestoreDefaultInputModeWhenReleasing)
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	PlayerController->SetIgnoreMoveInput(bEnabled);
	PlayerController->SetIgnoreLookInput(bEnabled);

	if (!bEnabled && bRestoreDefaultInputModeWhenReleasing)
	{
		RestoreDefaultInputMode();
	}
}

void UProjectDefeatFlowComponent::ApplyStruggleInputBlock(const bool bEnabled)
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	if (bEnabled)
	{
		ApplyGameplayInputSuppression(true, false);

		if (StruggleWidget)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(StruggleWidget->TakeWidget());
			InputMode.SetHideCursorDuringCapture(true);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PlayerController->SetInputMode(InputMode);
		}

		return;
	}

	ApplyGameplayInputSuppression(false);
}

void UProjectDefeatFlowComponent::StartPlayerCameraFade(
	const float FromAlpha,
	const float ToAlpha,
	const float DurationSeconds,
	const bool bHoldWhenFinished) const
{
	if (APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StartCameraFade(
				FromAlpha,
				ToAlpha,
				DurationSeconds,
				FLinearColor::Black,
				false,
				bHoldWhenFinished);
		}
	}
}

void UProjectDefeatFlowComponent::EnsureSceneInputBinding()
{
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController || SceneInputComponent)
	{
		return;
	}

	SceneInputComponent = NewObject<UInputComponent>(PlayerController, TEXT("ProjectDefeatedSceneInput"));
	if (!SceneInputComponent)
	{
		return;
	}

	SceneInputComponent->bBlockInput = true;
	SceneInputComponent->Priority = 250;
	SceneInputComponent->RegisterComponent();
	FInputKeyBinding& CancelBinding = SceneInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ThisClass::HandleDefeatedSceneCancelPressed);
	CancelBinding.bConsumeInput = true;
	PlayerController->PushInputComponent(SceneInputComponent);
}

void UProjectDefeatFlowComponent::RemoveSceneInputBinding()
{
	if (!SceneInputComponent)
	{
		return;
	}

	if (APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		PlayerController->PopInputComponent(SceneInputComponent);
	}

	if (SceneInputComponent->IsRegistered())
	{
		SceneInputComponent->DestroyComponent();
	}

	SceneInputComponent = nullptr;
}

void UProjectDefeatFlowComponent::EnsureStruggleWidget()
{
	if (StruggleWidget)
	{
		return;
	}

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	TSubclassOf<UProjectKnockoutStruggleWidget> StruggleWidgetClass = UProjectKnockoutStruggleWidget::StaticClass();
	if (Settings && !Settings->KnockoutStruggleWidgetClass.IsNull())
	{
		if (UClass* ConfiguredWidgetClass = Settings->KnockoutStruggleWidgetClass.LoadSynchronous())
		{
			if (ConfiguredWidgetClass->IsChildOf(UProjectKnockoutStruggleWidget::StaticClass()))
			{
				StruggleWidgetClass = ConfiguredWidgetClass;
			}
		}
	}

	StruggleWidget = CreateWidget<UProjectKnockoutStruggleWidget>(PlayerController, StruggleWidgetClass, TEXT("ProjectKnockoutStruggleWidget"));
	if (!StruggleWidget)
	{
		return;
	}

	StruggleWidget->OnRoundCompleted.RemoveAll(this);
	StruggleWidget->OnRoundCompleted.AddUObject(this, &ThisClass::HandleStruggleRoundCompleted);

	if (!StruggleWidget->AddToPlayerScreen(Settings ? Settings->KnockoutStruggleWidgetZOrder : 240))
	{
		StruggleWidget->AddToViewport(Settings ? Settings->KnockoutStruggleWidgetZOrder : 240);
	}
}

void UProjectDefeatFlowComponent::EnsurePainDebugWidget()
{
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	if (!Settings || !Settings->bEnablePainDebugWidget)
	{
		RemovePainDebugWidget();
		return;
	}

	if (PainDebugWidget)
	{
		return;
	}

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	PainDebugWidget = CreateWidget<UProjectPainDebugWidget>(PlayerController, UProjectPainDebugWidget::StaticClass(), TEXT("ProjectPainDebugWidget"));
	if (!PainDebugWidget)
	{
		return;
	}

	PainDebugWidget->SetObservedFlowComponent(this);
	if (!PainDebugWidget->AddToPlayerScreen(Settings->PainDebugWidgetZOrder))
	{
		PainDebugWidget->AddToViewport(Settings->PainDebugWidgetZOrder);
	}
}

void UProjectDefeatFlowComponent::RemoveStruggleWidget()
{
	if (!StruggleWidget)
	{
		return;
	}

	StruggleWidget->OnRoundCompleted.RemoveAll(this);
	StruggleWidget->RemoveFromParent();
	StruggleWidget = nullptr;
}

void UProjectDefeatFlowComponent::RemovePainDebugWidget()
{
	if (!PainDebugWidget)
	{
		return;
	}

	PainDebugWidget->SetObservedFlowComponent(nullptr);
	PainDebugWidget->RemoveFromParent();
	PainDebugWidget = nullptr;
}

void UProjectDefeatFlowComponent::RestoreDefaultInputMode()
{
	if (APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
	}
}

void UProjectDefeatFlowComponent::PacifyEnemiesTargetingOwnerWhileDowned() const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World)
	{
		return;
	}

	for (TActorIterator<AACFAIController> ControllerIt(World); ControllerIt; ++ControllerIt)
	{
		AACFAIController* ACFController = *ControllerIt;
		const bool bWasTargetingOwner = IsACFControllerTargetingActor(ACFController, OwnerActor);
		const bool bHadOwnerThreat = RemoveACFThreatForActor(ACFController, OwnerActor);
		if (!bWasTargetingOwner && !bHadOwnerThreat)
		{
			continue;
		}

		if (bWasTargetingOwner)
		{
			StopACFControllerFromTargetingActor(ACFController, OwnerActor);
		}
	}

	if (!RealtimeSnapshotComponent)
	{
		return;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	const FVector OwnerLocation = OwnerActor->GetActorLocation();
	const FProjectUnifiedRuntimeSnapshot Snapshot = RealtimeSnapshotComponent->BuildUnifiedRuntimeSnapshot();

	for (const FProjectRealtimeActorHealthSnapshot& EnemySnapshot : Snapshot.ObservedEnemies)
	{
		AActor* EnemyActor = EnemySnapshot.Actor.Get();
		APawn* EnemyPawn = Cast<APawn>(EnemyActor);
		AAIController* AIController = EnemyPawn ? Cast<AAIController>(EnemyPawn->GetController()) : nullptr;
		if (!EnemyActor
			|| EnemyActor->IsHidden()
			|| !EnemyActor->GetActorEnableCollision()
			|| !AIController
			|| !IsQualifiedEnemyActor(EnemyActor))
		{
			continue;
		}

		if (FVector::DistSquared(OwnerLocation, EnemyActor->GetActorLocation()) > FMath::Square(Settings ? Settings->StruggleEnemyRadius : 1000.f))
		{
			continue;
		}

		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

float UProjectDefeatFlowComponent::GetPainCurrent() const
{
	return NeedsComponent ? NeedsComponent->GetSensationCurrentValue(PainName) : 0.f;
}

float UProjectDefeatFlowComponent::GetPainMax() const
{
	return NeedsComponent ? NeedsComponent->GetSensationMaxValue(PainName) : 100.f;
}

float UProjectDefeatFlowComponent::GetPainThreshold() const
{
	if (NeedsComponent)
	{
		const float RuntimePainMax = GetPainMax();
		if (RuntimePainMax > KINDA_SMALL_NUMBER)
		{
			return RuntimePainMax;
		}
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	return Settings ? FMath::Max(Settings->PainKnockoutThreshold, 1.f) : 100.f;
}

void UProjectDefeatFlowComponent::SetPainCurrent(const float NewValue) const
{
	if (NeedsComponent)
	{
		NeedsComponent->SetSensationCurrentValue(PainName, FMath::Clamp(NewValue, 0.f, GetPainMax()), true);
	}
}

int32 UProjectDefeatFlowComponent::ResolveCunningLevel() const
{
	if (InnerDoctrineComponent)
	{
		return InnerDoctrineComponent->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
	}

	if (const AActor* OwnerActor = GetOwner())
	{
		if (const UProjectInnerDoctrineComponent* Component = OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>())
		{
			return Component->GetAttributeLevel(EProjectDoctrineAttribute::Cunning);
		}
	}

	return 0;
}

int32 UProjectDefeatFlowComponent::ResolveEnemyLevel(const AActor* EnemyActor) const
{
	const UProjectEnemyLevelComponent* EnemyLevelComponent = EnemyActor ? EnemyActor->FindComponentByClass<UProjectEnemyLevelComponent>() : nullptr;
	if (!EnemyLevelComponent)
	{
		return 1;
	}

	return FMath::Max(EnemyLevelComponent->GetAssignedLevel(), 1);
}

int32 UProjectDefeatFlowComponent::ResolveEnemyDifficultyWeight(const AActor* EnemyActor) const
{
	const FString EnemyName = EnemyActor && EnemyActor->GetClass() ? EnemyActor->GetClass()->GetName() : FString();
	if (EnemyName.Contains(TEXT("MageMale"), ESearchCase::IgnoreCase)
		|| EnemyName.Contains(TEXT("ACFMageEnemyBP"), ESearchCase::IgnoreCase))
	{
		return 4;
	}

	if (EnemyName.Contains(TEXT("RangedMale"), ESearchCase::IgnoreCase)
		|| EnemyName.Contains(TEXT("ACFRangedEnemyBP"), ESearchCase::IgnoreCase))
	{
		return 3;
	}

	if (EnemyName.Contains(TEXT("MeleeMale"), ESearchCase::IgnoreCase)
		|| EnemyName.Contains(TEXT("ACFMeleeEnemyBP"), ESearchCase::IgnoreCase))
	{
		return 2;
	}

	if (EnemyName.Contains(TEXT("DummyMale"), ESearchCase::IgnoreCase))
	{
		return 1;
	}

	return 1;
}

int32 UProjectDefeatFlowComponent::BuildRoundSeed(const AActor* EnemyActor, const int32 RoundIndex) const
{
	const int32 ActorSeed = EnemyActor ? GetTypeHash(EnemyActor->GetFName()) : 0;
	return ActorSeed ^ (RoundIndex * 7919) ^ 0x5F3759DF;
}

UProjectLocomotionOverrideComponent* UProjectDefeatFlowComponent::EnsureLocomotionOverrideComponent()
{
	if (LocomotionOverrideComponent)
	{
		return LocomotionOverrideComponent;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	LocomotionOverrideComponent = OwnerActor->FindComponentByClass<UProjectLocomotionOverrideComponent>();
	if (!LocomotionOverrideComponent)
	{
		LocomotionOverrideComponent = NewObject<UProjectLocomotionOverrideComponent>(OwnerActor, UProjectLocomotionOverrideComponent::StaticClass(), TEXT("ProjectDefeatLocomotionOverrideComponent"));
		if (LocomotionOverrideComponent)
		{
			OwnerActor->AddInstanceComponent(LocomotionOverrideComponent);
			LocomotionOverrideComponent->OnComponentCreated();
			LocomotionOverrideComponent->RegisterComponent();
			LocomotionOverrideComponent->Activate(true);
		}
	}

	return LocomotionOverrideComponent;
}

UProjectEmoteComponent* UProjectDefeatFlowComponent::EnsureEmoteComponent()
{
	if (EmoteComponent)
	{
		return EmoteComponent;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	EmoteComponent = OwnerActor->FindComponentByClass<UProjectEmoteComponent>();
	if (!EmoteComponent)
	{
		EmoteComponent = NewObject<UProjectEmoteComponent>(OwnerActor, UProjectEmoteComponent::StaticClass(), TEXT("ProjectDefeatEmoteComponent"));
		if (EmoteComponent)
		{
			OwnerActor->AddInstanceComponent(EmoteComponent);
			EmoteComponent->OnComponentCreated();
			EmoteComponent->RegisterComponent();
			EmoteComponent->Activate(true);
		}
	}

	return EmoteComponent;
}

USkeletalMeshComponent* UProjectDefeatFlowComponent::ResolveOwnerCharacterMesh() const
{
	if (const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* CharacterMesh = CharacterOwner->GetMesh())
		{
			return CharacterMesh;
		}
	}

	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

APlayerController* UProjectDefeatFlowComponent::ResolveOwningPlayerController() const
{
	if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void UProjectDefeatFlowComponent::AutomationCompleteActiveStruggleRound(const bool bSuccess)
{
	if (CurrentPhase != EProjectDefeatPhase::Struggle)
	{
		return;
	}

	if (StruggleWidget && StruggleWidget->IsRoundActive())
	{
		StruggleWidget->OnRoundCompleted.RemoveAll(this);
		StruggleWidget->AbortRound(false);
		StruggleWidget->OnRoundCompleted.AddUObject(this, &ThisClass::HandleStruggleRoundCompleted);
	}

	HandleStruggleRoundCompleted(bSuccess, true, ActiveRound);
}

int32 UProjectDefeatFlowComponent::AutomationGetActiveCombatSessionId() const
{
	return ActiveCombatSessionId;
}

int32 UProjectDefeatFlowComponent::AutomationGetLastKnockoutCombatSessionId() const
{
	return LastKnockoutCombatSessionId;
}

int32 UProjectDefeatFlowComponent::AutomationGetPendingStruggleRoundCount() const
{
	return PendingStruggleRounds.Num();
}

void UProjectDefeatFlowComponent::AutomationRequestDefeatedSceneCancel()
{
	HandleDefeatedSceneCancelPressed();
}

void UProjectDefeatFlowComponent::AutomationStartDefeatedSceneWithoutTravel()
{
	if (CurrentPhase != EProjectDefeatPhase::DefeatedBlackout && CurrentPhase != EProjectDefeatPhase::TravelPending)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TravelDelayTimerHandle);
	}

	ClampCurseAfterDefeat();
	if (PendingTransferPayload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette)
	{
		StartDefeatedScene(PendingTransferPayload.SceneDefinition);
	}
	else
	{
		CompleteDefeatedRespawnWithoutPresentation();
	}
}

void UProjectDefeatFlowComponent::AutomationRecoverFromKnockout()
{
	if (CurrentPhase == EProjectDefeatPhase::Struggle)
	{
		AutomationCompleteActiveStruggleRound(true);
	}

	if (CurrentPhase == EProjectDefeatPhase::KnockedOut || CurrentPhase == EProjectDefeatPhase::Struggle)
	{
		FinishKnockoutRecovery(false);
		return;
	}

	if (CurrentPhase == EProjectDefeatPhase::DefeatedScene)
	{
		HandleDefeatedSceneCancelPressed();
	}
}
#endif
