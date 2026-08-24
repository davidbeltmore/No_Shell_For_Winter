#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "ACMEffectsDispatcherComponent.h"
#include "ACMTypes.h"
#include "ARSStatisticsComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Components/ACFEffectsManagerComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curse/ProjectCurseSourceComponent.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Defeat/ProjectDefeatHitResolver.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ACFDamageType.h"
#include "Game/ACFFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "InnerDoctrine/ProjectInnerDoctrineSaveGame.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"
#include "InnerDoctrine/ProjectInnerDoctrineTravelStateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectMovementModifierTags.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Social/ProjectSocialSubsystem.h"
#include "Sound/SoundBase.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectRealtimeSnapshotSettings.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalNeedsSettings.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "TimerManager.h"
#include "UI/ACFDamageWidget.h"

#define LOCTEXT_NAMESPACE "ProjectInnerDoctrineComponent"

namespace
{
	const FName MadnessName(TEXT("Madness"));
	const FName PainName(TEXT("Pain"));
	const FName CurseName(TEXT("Curse"));
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName HealthName(TEXT("Health"));
	const FName SpellDefenseName(TEXT("SpellDefense"));
	const FName CursedStatusName(TEXT("Cursed"));
	const FName ExhaustedRecoveryStatusName(TEXT("ExhaustedRecovery"));
	const FName TiredStatusName(TEXT("Tired"));
	const FName WillpowerImmunitySourceId(TEXT("Doctrine.Willpower.UnbrokenMind"));

	int32 ToAttributeIndex(const EProjectDoctrineAttribute Attribute)
	{
		return static_cast<int32>(Attribute);
	}

	bool IsSpellLikeDamage(const FName DamageType)
	{
		const FString Value = DamageType.ToString();
		return Value.Contains(TEXT("Spell"), ESearchCase::IgnoreCase)
			|| Value.Contains(TEXT("Magic"), ESearchCase::IgnoreCase);
	}

	bool IsOrdinaryPhysicalDamage(const FName DamageType)
	{
		if (DamageType.IsNone())
		{
			return true;
		}

		const FString Value = DamageType.ToString();
		return !IsSpellLikeDamage(DamageType)
			&& (Value.Contains(TEXT("Physical"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Melee"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Ranged"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Projectile"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Arrow"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Impact"), ESearchCase::IgnoreCase)
				|| Value.Contains(TEXT("Cut"), ESearchCase::IgnoreCase));
	}

	bool MayIgnoreCurseResistance(const EProjectCurseSourceKind SourceKind)
	{
		// Narrative is the explicit authored bypass; Debug is the registered
		// system-only bypass used by deterministic tests and developer commands.
		return SourceKind == EProjectCurseSourceKind::Narrative
			|| SourceKind == EProjectCurseSourceKind::Debug;
	}

	TSubclassOf<UACFDamageType> LoadFeedbackDamageClass(
		const TCHAR* ClassPath,
		const TSubclassOf<UACFDamageType> FallbackClass)
	{
		if (UClass* LoadedClass = LoadClass<UACFDamageType>(nullptr, ClassPath))
		{
			return LoadedClass;
		}
		return FallbackClass;
	}
}

UProjectInnerDoctrineComponent::UProjectInnerDoctrineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
	InitializeAttributeStorage();
}

void UProjectInnerDoctrineComponent::BeginPlay()
{
	Super::BeginPlay();

	PassiveCurseDecaySuppressionSources.Reset();
	InitializeAttributeStorage();
	RefreshCachedComponents();
	LoadPersistentState();
	TryRestoreTravelCurseState();
	ApplyPassiveAttributeEffects();

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	LastCombatImpactTimeSeconds = Now;
	LastCurseApplicationTimeSeconds = Now;
}

void UProjectInnerDoctrineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TryRestoreTravelCurseState();
	StoreTravelCurseState();
	bSuppressTravelCurseStateWrites = true;
	CancelCursedEpisode();
	bGuardRecoveryActive = false;
	GuardRecoveryPoolRemaining = 0.f;

	if (StatusComponent)
	{
		StatusComponent->ClearStatusImmunitySource(WillpowerImmunitySourceId);
	}
	ActiveCurseZonePresenceTokens.Reset();
	PassiveCurseDecaySuppressionSources.Reset();

	if (LocomotionOverrideComponent)
	{
		LocomotionOverrideComponent->ClearMovementSpeedModifier(
			ProjectMovementModifierTags::DoctrineCelerity(),
			EProjectMovementModifierLayer::DoctrineBonus);
		LocomotionOverrideComponent->ClearMovementSpeedModifier(
			ProjectMovementModifierTags::DoctrineRecoveredMomentum(),
			EProjectMovementModifierLayer::DoctrineBonus);
		LocomotionOverrideComponent->ClearStatusPenaltyMitigation(
			ProjectMovementModifierTags::ForStatus(CursedStatusName));
		LocomotionOverrideComponent->ClearStatusPenaltyMitigation(
			ProjectMovementModifierTags::ForStatus(ExhaustedRecoveryStatusName));
	}

	Super::EndPlay(EndPlayReason);
}

void UProjectInnerDoctrineComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.f)
	{
		return;
	}

	if (!bTravelCurseStateRestoreCompleted)
	{
		RefreshCachedComponents();
		TryRestoreTravelCurseState();
	}
	UpdateCombatState();

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (bCursedEpisodeActive && Now >= CursedEpisodeEndTimeSeconds)
	{
		CompleteCursedEpisode(true);
	}
	if (bGuardRecoveryActive && Now >= GuardRecoveryEndTimeSeconds)
	{
		CompleteGuardRecovery();
	}
	UpdateTimedDoctrineEffects();

	TickAccumulatorSeconds += DeltaTime;
	if (TickAccumulatorSeconds < 0.10f)
	{
		return;
	}

	const float StepSeconds = TickAccumulatorSeconds;
	TickAccumulatorSeconds = 0.f;
	RefreshCachedComponents();
	UpdateFaithRecovery(StepSeconds);
	UpdateCurse(StepSeconds);
	ApplyPassiveAttributeEffects();
	if (bCursedEpisodeActive)
	{
		StoreTravelCurseState();
	}
}

int32 UProjectInnerDoctrineComponent::GetCurrentRunDxp() const
{
	return CurrentRunDxp;
}

int32 UProjectInnerDoctrineComponent::GetMetaBankDxp() const
{
	return MetaBankDxp;
}

bool UProjectInnerDoctrineComponent::IsDoctrineMasteryModeEnabled() const
{
	return bDoctrineMasteryMode;
}

int32 UProjectInnerDoctrineComponent::GetAttributeLevel(const EProjectDoctrineAttribute Attribute) const
{
	const int32 Index = ToAttributeIndex(Attribute);
	return AttributeLevels.IsValidIndex(Index) ? AttributeLevels[Index] : 0;
}

int32 UProjectInnerDoctrineComponent::GetUpgradeCost(const EProjectDoctrineAttribute Attribute) const
{
	if (!IsAttributeIndexValid(Attribute))
	{
		return 0;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const int32 MaxLevel = FMath::Max(Settings ? Settings->MaxDoctrineAttributeLevel : 100, 1);
	return GetAttributeLevel(Attribute) >= MaxLevel
		? 0
		: UProjectInnerDoctrineSettings::ComputeAttributeUpgradeCost(GetAttributeLevel(Attribute));
}

FProjectInnerDoctrineSnapshot UProjectInnerDoctrineComponent::BuildSnapshot() const
{
	FProjectInnerDoctrineSnapshot Snapshot;
	Snapshot.CurrentRunDxp = CurrentRunDxp;
	Snapshot.MetaBankDxp = MetaBankDxp;
	Snapshot.bDoctrineMasteryMode = bDoctrineMasteryMode;
	Snapshot.Madness = GetSensationCurrent(MadnessName);
	Snapshot.MadnessMax = FMath::Max(GetSensationMax(MadnessName), ResolveBaseSensationMax(MadnessName));
	Snapshot.Curse = GetCurse();
	Snapshot.CurseMax = GetCurseMax();
	Snapshot.Pain = GetSensationCurrent(PainName);
	Snapshot.PainMax = FMath::Max(GetSensationMax(PainName), ResolveBaseSensationMax(PainName));
	Snapshot.bCursed = bCursedEpisodeActive;
	Snapshot.bGuardRecoveryActive = bGuardRecoveryActive;
	Snapshot.GuardRecoveryPoolRemaining = GuardRecoveryPoolRemaining;

	const int32 Count = ToAttributeIndex(EProjectDoctrineAttribute::Count);
	Snapshot.Attributes.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const EProjectDoctrineAttribute Attribute = static_cast<EProjectDoctrineAttribute>(Index);
		FProjectDoctrineAttributeState State;
		State.Attribute = Attribute;
		State.DisplayName = GetAttributeDisplayName(Attribute);
		State.Level = GetAttributeLevel(Attribute);
		State.NextLevelCost = GetUpgradeCost(Attribute);
		State.bMilestone5Unlocked = HasMilestone(Attribute, 5);
		State.bMilestone10Unlocked = HasMilestone(Attribute, 10);
		Snapshot.Attributes.Add(State);
	}

	if (const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get())
	{
		for (const FProjectDoctrineMilestoneDefinition& Definition : Settings->MilestoneDefinitions)
		{
			AddMilestoneSnapshot(Snapshot.Milestones, Definition);
		}
	}
	return Snapshot;
}

int32 UProjectInnerDoctrineComponent::GrantDxp(
	const FName ReasonId,
	const int32 Amount,
	const EProjectDoctrineExperienceSource Source)
{
	if (Amount <= 0 || Source == EProjectDoctrineExperienceSource::MaturePresentation)
	{
		return 0;
	}

	float SourceMultiplier = ResolveExternalDxpGainMultiplier();
	if ((Source == EProjectDoctrineExperienceSource::Training
			|| Source == EProjectDoctrineExperienceSource::Dialogue
			|| Source == EProjectDoctrineExperienceSource::Quest))
	{
		const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
		const float CharismaBonus = FMath::Min(
			static_cast<float>(GetAttributeLevel(EProjectDoctrineAttribute::Charisma))
				* (Settings ? Settings->CharismaDxpBonusPerLevel : 0.01f),
			Settings ? Settings->CharismaDxpBonusCap : 0.25f);
		SourceMultiplier *= 1.f + FMath::Max(0.f, CharismaBonus);
	}

	const int32 FinalAmount = FMath::Max(
		0,
		FMath::RoundToInt(static_cast<float>(Amount) * FMath::Max(0.f, SourceMultiplier)));
	if (FinalAmount <= 0)
	{
		return 0;
	}

	const int32 OldRun = CurrentRunDxp;
	const int32 OldMeta = MetaBankDxp;
	CurrentRunDxp = FMath::Max(0, CurrentRunDxp + FinalAmount);
	BroadcastDxpChanged(OldRun, OldMeta);
	(void)ReasonId;
	return FinalAmount;
}

int32 UProjectInnerDoctrineComponent::GrantDxpWithAttributeAffinity(
	const FName ReasonId,
	const int32 Amount,
	const EProjectDoctrineExperienceSource Source,
	const TArray<EProjectDoctrineAttribute>& Affinities)
{
	if (Amount <= 0 || Source == EProjectDoctrineExperienceSource::MaturePresentation)
	{
		return 0;
	}
	const int32 Adjusted = FMath::Max(
		0,
		FMath::RoundToInt(static_cast<float>(Amount) * ResolveBestDxpGainMultiplier(Affinities)));
	return GrantDxp(ReasonId, Adjusted, Source);
}

bool UProjectInnerDoctrineComponent::SpendDxpOnAttribute(const EProjectDoctrineAttribute Attribute)
{
	if (!IsAttributeIndexValid(Attribute))
	{
		return false;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const int32 MaxLevel = FMath::Max(Settings ? Settings->MaxDoctrineAttributeLevel : 100, 1);
	const int32 Index = ToAttributeIndex(Attribute);
	const int32 OldLevel = AttributeLevels[Index];
	const int32 Cost = GetUpgradeCost(Attribute);
	if (OldLevel >= MaxLevel || Cost <= 0 || CurrentRunDxp < Cost)
	{
		return false;
	}

	const int32 OldRun = CurrentRunDxp;
	const int32 OldMeta = MetaBankDxp;
	CurrentRunDxp -= Cost;
	AttributeLevels[Index] = FMath::Min(OldLevel + 1, MaxLevel);
	ApplyPassiveAttributeEffects();
	BroadcastDxpChanged(OldRun, OldMeta);
	OnAttributeLevelChanged.Broadcast(Attribute, OldLevel, AttributeLevels[Index], GetUpgradeCost(Attribute));
	BroadcastMilestonesForLevelRange(Attribute, OldLevel, AttributeLevels[Index]);
	return true;
}

bool UProjectInnerDoctrineComponent::ApplyFreeAttributeLevels(
	const EProjectDoctrineAttribute Attribute,
	const int32 Delta)
{
	if (!IsAttributeIndexValid(Attribute) || Delta <= 0)
	{
		return false;
	}

	InitializeAttributeStorage();
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const int32 MaxLevel = FMath::Max(Settings ? Settings->MaxDoctrineAttributeLevel : 100, 1);
	const int32 Index = ToAttributeIndex(Attribute);
	const int32 OldLevel = AttributeLevels[Index];
	const int32 NewLevel = FMath::Clamp(OldLevel + Delta, 0, MaxLevel);
	if (OldLevel == NewLevel)
	{
		return false;
	}

	AttributeLevels[Index] = NewLevel;
	ApplyPassiveAttributeEffects();
	OnAttributeLevelChanged.Broadcast(Attribute, OldLevel, NewLevel, GetUpgradeCost(Attribute));
	BroadcastMilestonesForLevelRange(Attribute, OldLevel, NewLevel);
	return true;
}

void UProjectInnerDoctrineComponent::ResetRunProgressForBackgroundChange()
{
	const int32 OldRun = CurrentRunDxp;
	const int32 OldMeta = MetaBankDxp;
	CurrentRunDxp = 0;
	ResetRunAttributes();
	ClearDxpGainMultipliers();
	BroadcastDxpChanged(OldRun, OldMeta);
}

void UProjectInnerDoctrineComponent::SetDxpGainMultipliers(
	const TMap<EProjectDoctrineAttribute, float>& InMultipliers)
{
	DxpGainMultipliersByAttribute.Reset();
	for (const TPair<EProjectDoctrineAttribute, float>& Pair : InMultipliers)
	{
		if (IsAttributeIndexValid(Pair.Key))
		{
			DxpGainMultipliersByAttribute.Add(Pair.Key, FMath::Max(0.f, Pair.Value));
		}
	}
}

void UProjectInnerDoctrineComponent::ClearDxpGainMultipliers()
{
	DxpGainMultipliersByAttribute.Reset();
}

void UProjectInnerDoctrineComponent::SetExternalDxpGainMultiplier(
	const FName SourceId,
	const float Multiplier)
{
	if (!SourceId.IsNone())
	{
		ExternalDxpGainMultipliersBySource.Add(SourceId, FMath::Max(0.f, Multiplier));
	}
}

void UProjectInnerDoctrineComponent::ClearExternalDxpGainMultiplier(const FName SourceId)
{
	if (!SourceId.IsNone())
	{
		ExternalDxpGainMultipliersBySource.Remove(SourceId);
	}
}

void UProjectInnerDoctrineComponent::SetExternalFlatDamageBonus(
	const FName SourceId,
	const float FlatDamageBonus)
{
	if (!SourceId.IsNone())
	{
		ExternalFlatDamageBonusesBySource.Add(SourceId, FMath::Max(0.f, FlatDamageBonus));
	}
}

void UProjectInnerDoctrineComponent::ClearExternalFlatDamageBonus(const FName SourceId)
{
	if (!SourceId.IsNone())
	{
		ExternalFlatDamageBonusesBySource.Remove(SourceId);
	}
}

float UProjectInnerDoctrineComponent::GetDxpGainMultiplierForAttribute(
	const EProjectDoctrineAttribute Attribute) const
{
	if (const float* Value = DxpGainMultipliersByAttribute.Find(Attribute))
	{
		return FMath::Max(0.f, *Value);
	}
	return 1.f;
}

int32 UProjectInnerDoctrineComponent::WithdrawMetaDxp(const int32 RequestedAmount)
{
	const int32 Amount = RequestedAmount <= 0 ? MetaBankDxp : FMath::Min(RequestedAmount, MetaBankDxp);
	if (Amount <= 0)
	{
		return 0;
	}

	const int32 OldRun = CurrentRunDxp;
	const int32 OldMeta = MetaBankDxp;
	MetaBankDxp -= Amount;
	CurrentRunDxp += Amount;
	BroadcastDxpChanged(OldRun, OldMeta);
	SavePersistentState();
	return Amount;
}

void UProjectInnerDoctrineComponent::HandleRunDeath()
{
	CancelCursedEpisode();
	bGuardRecoveryActive = false;
	GuardRecoveryPoolRemaining = 0.f;
	bCombatStateActive = false;
	LastCombatImpactTimeSeconds = -FLT_MAX;
	ProcessedFloorTransitionIds.Reset();

	const int32 OldRun = CurrentRunDxp;
	const int32 OldMeta = MetaBankDxp;
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float LossRatio = FMath::Clamp(Settings ? Settings->DeathRunLossRatio : 0.90f, 0.f, 1.f);
	const int32 Banked = bDoctrineMasteryMode
		? CurrentRunDxp
		: FMath::Max(0, FMath::RoundToInt(static_cast<float>(CurrentRunDxp) * (1.f - LossRatio)));
	MetaBankDxp += Banked;
	CurrentRunDxp = 0;
	ResetRunAttributes();
	ClampCurseAfterDefeat_Implementation(Settings ? Settings->DefeatCurseClamp : 60.f);
	BroadcastDxpChanged(OldRun, OldMeta);
	SavePersistentState();
}

void UProjectInnerDoctrineComponent::SetDoctrineMasteryMode(const bool bEnabled)
{
	if (bDoctrineMasteryMode != bEnabled)
	{
		bDoctrineMasteryMode = bEnabled;
		SavePersistentState();
		OnDxpChanged.Broadcast(CurrentRunDxp, CurrentRunDxp, MetaBankDxp, MetaBankDxp);
	}
}

FProjectCurseApplicationResult UProjectInnerDoctrineComponent::ApplyCurse(
	const FProjectCurseApplicationContext& Context)
{
	FProjectCurseApplicationResult Result;
	Result.RequestedAmount = FMath::Max(0.f, Context.Amount);
	if (!NeedsComponent)
	{
		RefreshCachedComponents();
	}
	Result.NewCurse = GetCurse();

	AActor* OwnerActor = GetOwner();
	if (OwnerActor && !OwnerActor->HasAuthority())
	{
		return Result;
	}

	if (!Context.ApplicationId.IsValid())
	{
		Result.bRejectedInvalidApplicationId = true;
		return Result;
	}

	if (ProcessedCurseApplicationIds.Contains(Context.ApplicationId))
	{
		Result.bDuplicate = true;
		return Result;
	}
	RecordCurseApplicationId(Context.ApplicationId);

	if (Result.RequestedAmount <= KINDA_SMALL_NUMBER)
	{
		return Result;
	}

	if (ConsumeCunningTrapProtection(Context.SourceKind))
	{
		return Result;
	}

	FProjectCurseApplicationContext SanitizedContext = Context;
	if (!SanitizedContext.bResistible && !MayIgnoreCurseResistance(SanitizedContext.SourceKind))
	{
		SanitizedContext.bResistible = true;
	}

	Result.ResistanceMultiplier = SanitizedContext.bResistible
		? ResolveCurseResistanceMultiplier(SanitizedContext)
		: 1.f;
	LastResolvedCurseResistanceMultiplier = Result.ResistanceMultiplier;
	const float OldCurse = GetCurse();
	SetCurse(OldCurse + Result.RequestedAmount * Result.ResistanceMultiplier);
	Result.NewCurse = GetCurse();
	Result.AppliedAmount = FMath::Max(0.f, Result.NewCurse - OldCurse);
	LastCurseApplicationTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (SanitizedContext.SourceKind == EProjectCurseSourceKind::EnemyAttack)
	{
		MarkCombatImpact();
	}

	if (SanitizedContext.bCanTriggerCursed
		&& Result.NewCurse >= GetCurseMax() - KINDA_SMALL_NUMBER
		&& !bCursedEpisodeActive)
	{
		Result.bTriggeredCursed = StartCursedEpisode();
	}
	return Result;
}

float UProjectInnerDoctrineComponent::CleanseCurse(
	const float Amount,
	const bool bServiceOrShrine)
{
	if (Amount <= 0.f)
	{
		return 0.f;
	}
	if (!NeedsComponent)
	{
		RefreshCachedComponents();
	}

	const float EffectiveAmount = Amount;
	(void)bServiceOrShrine;

	const float OldCurse = GetCurse();
	SetCurse(OldCurse - EffectiveAmount);
	return FMath::Max(0.f, OldCurse - GetCurse());
}

float UProjectInnerDoctrineComponent::NotifyServiceOrShrineCleanse(
	const float Amount,
	const FName ServiceSourceId)
{
	return ServiceSourceId.IsNone() ? 0.f : CleanseCurse(Amount, true);
}

float UProjectInnerDoctrineComponent::GetCurse() const
{
	return GetSensationCurrent(CurseName);
}

float UProjectInnerDoctrineComponent::GetCurseMax() const
{
	return ProjectCurse::Maximum;
}

bool UProjectInnerDoctrineComponent::IsCursed() const
{
	return bCursedEpisodeActive;
}

bool UProjectInnerDoctrineComponent::SetPassiveCurseDecaySuppressed(
	const FName SourceId,
	const bool bSuppressed)
{
	if (SourceId.IsNone())
	{
		return false;
	}

	if (bSuppressed)
	{
		const bool bWasAlreadySuppressed =
			PassiveCurseDecaySuppressionSources.Contains(SourceId);
		PassiveCurseDecaySuppressionSources.Add(SourceId);
		return !bWasAlreadySuppressed;
	}

	return PassiveCurseDecaySuppressionSources.Remove(SourceId) > 0;
}

bool UProjectInnerDoctrineComponent::IsPassiveCurseDecaySuppressed() const
{
	return PassiveCurseDecaySuppressionSources.Num() > 0;
}

bool UProjectInnerDoctrineComponent::RegisterCurseZonePresence(
	const FGuid ZonePresenceToken)
{
	if (!ZonePresenceToken.IsValid()
		|| ActiveCurseZonePresenceTokens.Contains(ZonePresenceToken))
	{
		return false;
	}
	ActiveCurseZonePresenceTokens.Add(ZonePresenceToken);
	return true;
}

bool UProjectInnerDoctrineComponent::UnregisterCurseZonePresence(
	const FGuid ZonePresenceToken)
{
	return ZonePresenceToken.IsValid()
		&& ActiveCurseZonePresenceTokens.Remove(ZonePresenceToken) > 0;
}

int32 UProjectInnerDoctrineComponent::GetActiveCurseZoneCount() const
{
	return ActiveCurseZonePresenceTokens.Num();
}

bool UProjectInnerDoctrineComponent::NotifyDungeonFloorCompleted(const FName FloorTransitionId)
{
	if (FloorTransitionId.IsNone()
		|| ProcessedFloorTransitionIds.Contains(FloorTransitionId))
	{
		return false;
	}
	ProcessedFloorTransitionIds.Add(FloorTransitionId);
	bCunningTrapProtectionConsumed = false;
	if (!HasMilestone(EProjectDoctrineAttribute::Willpower, 5))
	{
		return false;
	}

	RefreshCachedComponents();
	if (!NeedsComponent)
	{
		return false;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float RecoveryPct = FMath::Clamp(
		Settings ? Settings->WillpowerSecondBreathRecoveryPct : 0.20f,
		0.f,
		1.f);
	bool bChanged = false;
	for (const FName NeedName : { HungerName, ThirstName, SleepName })
	{
		if (NeedsComponent->HasNeed(NeedName))
		{
			bChanged |= !FMath::IsNearlyZero(NeedsComponent->ModifyNeedValue(
				NeedName,
				NeedsComponent->GetNeedMaxValue(NeedName) * RecoveryPct,
				true));
		}
	}

	const float Reduction = Settings ? Settings->WillpowerSecondBreathSensationReduction : 20.f;
	for (const FName SensationName : { PainName, MadnessName })
	{
		if (NeedsComponent->HasSensation(SensationName))
		{
			bChanged |= !FMath::IsNearlyZero(
				NeedsComponent->ModifySensationValue(SensationName, -Reduction, true));
		}
	}
	if (!bChanged)
	{
		return false;
	}
	OnMilestoneTriggered.Broadcast(TEXT("SecondBreath"), EProjectDoctrineAttribute::Willpower, 5);
	return true;
}

bool UProjectInnerDoctrineComponent::NotifySleepCompleted(const FName SleepSourceId)
{
	if (SleepSourceId.IsNone())
	{
		return false;
	}
	RefreshCachedComponents();
	if (StatusComponent)
	{
		StatusComponent->ClearStatus(TiredStatusName);
	}
	if (!HasMilestone(EProjectDoctrineAttribute::Faith, 10) || !NeedsComponent)
	{
		return false;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	bool bChanged = false;
	if (NeedsComponent->HasSensation(MadnessName))
	{
		const float Reduction = UProjectInnerDoctrineSettings::ComputeFaithSleepMadnessRestore(
			GetSensationMax(MadnessName),
			Settings ? Settings->FaithSleepMadnessRestorePct : 0.50f);
		bChanged |= !FMath::IsNearlyZero(
			NeedsComponent->ModifySensationValue(MadnessName, -Reduction, true));
	}
	if (bChanged)
	{
		OnMilestoneTriggered.Broadcast(TEXT("SanctifiedRest"), EProjectDoctrineAttribute::Faith, 10);
	}
	return bChanged;
}

bool UProjectInnerDoctrineComponent::TryActivateTacticalRetreat()
{
	if (!HasMilestone(EProjectDoctrineAttribute::Defensive, 10) || bGuardRecoveryActive)
	{
		return false;
	}
	RefreshCachedComponents();
	if (!DefeatFlowComponent || DefeatFlowComponent->GetCurrentPhase() != EProjectDefeatPhase::None)
	{
		return false;
	}

	const bool bActivated = DefeatFlowComponent->RequestTacticalRetreat(
		TEXT("Defensive.TacticalRetreat"));
	if (bActivated)
	{
		OnMilestoneTriggered.Broadcast(TEXT("TacticalRetreat"), EProjectDoctrineAttribute::Defensive, 10);
	}
	return bActivated;
}

bool UProjectInnerDoctrineComponent::IsGuardRecoveryActive() const
{
	return bGuardRecoveryActive;
}

bool UProjectInnerDoctrineComponent::ShouldDeferPainKnockoutForGuardRecovery() const
{
	return false;
}

void UProjectInnerDoctrineComponent::ClampCurseAfterDefeat_Implementation(const float MaximumCurse)
{
	SetCurse(FMath::Min(GetCurse(), FMath::Max(0.f, MaximumCurse)));
}

float UProjectInnerDoctrineComponent::ModifyIncomingHealthDamage(
	AActor* SourceActor,
	const FName DamageType,
	const float PostArmorDamage,
	float& OutFlatNegatedDamage,
	float& OutGuardAbsorbedDamage)
{
	OutFlatNegatedDamage = 0.f;
	OutGuardAbsorbedDamage = 0.f;
	float Remaining = FMath::Max(0.f, PostArmorDamage);
	if (Remaining <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	if (IsOrdinaryPhysicalDamage(DamageType) || IsSpellLikeDamage(DamageType))
	{
		const float FlatNegation = UProjectInnerDoctrineSettings::ComputeDefensiveFlatDamageNegation(
			GetAttributeLevel(EProjectDoctrineAttribute::Defensive),
			Settings ? Settings->DefensiveFlatDamageNegationPerLevel : 4.f);
		OutFlatNegatedDamage = FMath::Min(Remaining, FlatNegation);
		Remaining -= OutFlatNegatedDamage;
	}

	(void)SourceActor;
	return FMath::Max(0.f, Remaining);
}

bool UProjectInnerDoctrineComponent::TryPreventDeathFromDamage(
	const FProjectIncomingHitContext& HitContext,
	const float CurrentHealth,
	float& OutSurvivingHealth)
{
	if (HitContext.AppliedDamage <= 0.f
		|| CurrentHealth > HitContext.AppliedDamage + KINDA_SMALL_NUMBER)
	{
		return false;
	}
	if (!DefeatFlowComponent)
	{
		RefreshCachedComponents();
	}
	return DefeatFlowComponent
		? DefeatFlowComponent->TryHandleLethalDamage(HitContext, OutSurvivingHealth)
		: false;
}

void UProjectInnerDoctrineComponent::ModifyOutgoingDamageSpec(
	AActor* TargetActor,
	FProjectCombatDamageSpec& InOutDamageSpec)
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	float FlatBonus = 0.f;
	if (IsSpellLikeDamage(InOutDamageSpec.DamageType))
	{
		FlatBonus += static_cast<float>(GetAttributeLevel(EProjectDoctrineAttribute::Faith))
			* (Settings ? Settings->FaithSpellDamagePerLevel : 2.f);
	}
	else
	{
		FlatBonus += static_cast<float>(GetAttributeLevel(EProjectDoctrineAttribute::Offensive))
			* (Settings ? Settings->OffensiveDamagePerLevel : 2.f);

		(void)TargetActor;
	}

	InOutDamageSpec.FlatBonusDamage += FMath::Max(0.f, FlatBonus);
	InOutDamageSpec.FlatBonusDamage += ResolveExternalFlatDamageBonus();
}

void UProjectInnerDoctrineComponent::NotifyDamageDealt(
	AActor* TargetActor,
	const FProjectCombatDamageResult& DamageResult)
{
	if (DamageResult.AppliedDamage <= 0.f)
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	GrantDxpWithAttributeAffinity(
		TEXT("Combat.Hit"),
		Settings ? Settings->CombatHitDxp : 3,
		EProjectDoctrineExperienceSource::Combat,
		{ IsSpellLikeDamage(DamageResult.DamageType)
			? EProjectDoctrineAttribute::Faith
			: EProjectDoctrineAttribute::Offensive });
	MarkCombatImpact();

	if (DamageResult.bKilledTarget)
	{
		GrantDxpWithAttributeAffinity(
			TEXT("Combat.Kill"),
			Settings ? Settings->EnemyKillDxp : 15,
			EProjectDoctrineExperienceSource::Combat,
			{ EProjectDoctrineAttribute::Offensive, EProjectDoctrineAttribute::Willpower });
	}
}

void UProjectInnerDoctrineComponent::NotifyDamageReceived(const FProjectIncomingHitContext& HitContext)
{
	if (HitContext.RequestedDamage <= 0.f)
	{
		return;
	}
	MarkCombatImpact();
	if (!DefeatFlowComponent)
	{
		RefreshCachedComponents();
	}
	if (DefeatFlowComponent)
	{
		DefeatFlowComponent->NotifyDamageReceived(HitContext);
	}
	if (HitContext.bKilledTarget)
	{
		HandleRunDeath();
	}
}

float UProjectInnerDoctrineComponent::ComputeCurseResistanceMultiplier(
	const FProjectCurseApplicationContext& Context,
	const int32 WillpowerLevel,
	const int32 FaithLevel,
	const int32 CunningLevel,
	const bool bRallyingPresenceActive,
	const UProjectInnerDoctrineSettings* Settings)
{
	if (!Context.bResistible)
	{
		return 1.f;
	}

	const UProjectInnerDoctrineSettings* EffectiveSettings =
		Settings ? Settings : UProjectInnerDoctrineSettings::Get();
	float Multiplier = 1.f;
	const float WillpowerReduction = FMath::Min(
		FMath::Max(0, WillpowerLevel)
			* (EffectiveSettings ? EffectiveSettings->WillpowerCurseResistancePerLevel : 0.01f),
		EffectiveSettings ? EffectiveSettings->WillpowerCurseResistanceCap : 0.25f);
	Multiplier *= 1.f - FMath::Clamp(WillpowerReduction, 0.f, 1.f);

	if (Context.SourceKind == EProjectCurseSourceKind::Magic
		|| Context.SourceKind == EProjectCurseSourceKind::Relic
		|| Context.SourceKind == EProjectCurseSourceKind::Narrative)
	{
		const float FaithReduction = FMath::Min(
			FMath::Max(0, FaithLevel)
				* (EffectiveSettings ? EffectiveSettings->FaithCurseResistancePerLevel : 0.01f),
			EffectiveSettings ? EffectiveSettings->FaithCurseResistanceCap : 0.30f);
		Multiplier *= 1.f - FMath::Clamp(FaithReduction, 0.f, 1.f);
	}

	if (Context.SourceKind == EProjectCurseSourceKind::Trap
		|| Context.SourceKind == EProjectCurseSourceKind::Environment
		|| Context.SourceKind == EProjectCurseSourceKind::CleansingFailure)
	{
		const float CunningReduction = FMath::Min(
			FMath::Max(0, CunningLevel)
				* (EffectiveSettings ? EffectiveSettings->CunningCurseResistancePerLevel : 0.01f),
			EffectiveSettings ? EffectiveSettings->CunningCurseResistanceCap : 0.40f);
		Multiplier *= 1.f - FMath::Clamp(CunningReduction, 0.f, 1.f);
	}

	if (bRallyingPresenceActive)
	{
		Multiplier *= 1.f - FMath::Clamp(
			EffectiveSettings ? EffectiveSettings->CharismaCompanionCurseResistance : 0.15f,
			0.f,
			1.f);
	}

	return FMath::Max(
		EffectiveSettings ? EffectiveSettings->CurseMinimumResistibleMultiplier : 0.25f,
		Multiplier);
}

float UProjectInnerDoctrineComponent::ComputeExhaustedRecoveryPenaltyMitigation(
	const bool bMomentumUnlocked,
	const bool bRecoveredMomentumActive,
	const float MomentumMitigation)
{
	if (!bMomentumUnlocked)
	{
		return 0.f;
	}
	return bRecoveredMomentumActive
		? 1.f
		: FMath::Clamp(MomentumMitigation, 0.f, 1.f);
}

void UProjectInnerDoctrineComponent::RefreshCachedComponents()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		NeedsComponent = nullptr;
		StatusComponent = nullptr;
		CombatAttributeComponent = nullptr;
		RealtimeSnapshotComponent = nullptr;
		DefeatFlowComponent = nullptr;
		LocomotionOverrideComponent = nullptr;
		AcfEffectsManagerComponent = nullptr;
		return;
	}

	NeedsComponent = OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	StatusComponent = OwnerActor->FindComponentByClass<UProjectSurvivalStatusComponent>();
	CombatAttributeComponent = OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>();
	RealtimeSnapshotComponent = OwnerActor->FindComponentByClass<UProjectRealtimeSnapshotComponent>();
	DefeatFlowComponent = OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>();
	LocomotionOverrideComponent = OwnerActor->FindComponentByClass<UProjectLocomotionOverrideComponent>();
	AcfEffectsManagerComponent = OwnerActor->FindComponentByClass<UACFEffectsManagerComponent>();
}

bool UProjectInnerDoctrineComponent::IsPlayerTravelStateOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}
	if (OwnerPawn->IsPlayerControlled())
	{
		return true;
	}

	const UWorld* World = GetWorld();
	return World && UGameplayStatics::GetPlayerPawn(World, 0) == OwnerPawn;
}

void UProjectInnerDoctrineComponent::TryRestoreTravelCurseState()
{
	if (bTravelCurseStateRestoreCompleted || !NeedsComponent || !IsPlayerTravelStateOwner())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UProjectInnerDoctrineTravelStateSubsystem* TravelStateSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectInnerDoctrineTravelStateSubsystem>()
		: nullptr;
	if (!TravelStateSubsystem)
	{
		return;
	}

	TravelStateLifecycleGeneration = TravelStateSubsystem->GetLifecycleGeneration();
	bTravelStateLifecycleGenerationCaptured = true;
	bWasPlayerTravelStateOwner = true;
	bTravelCurseStateRestoreCompleted = true;

	FProjectCurseTravelState TravelState;
	if (!TravelStateSubsystem->TryGetCurseState(TravelState))
	{
		StoreTravelCurseState();
		return;
	}

	SetCurse(TravelState.Curse);
	if (TravelState.bCursedEpisodeActive
		&& TravelState.CursedEpisodeRemainingSeconds > KINDA_SMALL_NUMBER)
	{
		bCursedEpisodeActive = true;
		CursedEpisodeEndTimeSeconds =
			World->GetTimeSeconds() + TravelState.CursedEpisodeRemainingSeconds;
		SetForcedStatus(CursedStatusName, true);
		OnCursedStateChanged.Broadcast(true);
	}
	StoreTravelCurseState();
}

void UProjectInnerDoctrineComponent::StoreTravelCurseState()
{
	if (bSuppressTravelCurseStateWrites
		|| !NeedsComponent
		|| (!bWasPlayerTravelStateOwner && !IsPlayerTravelStateOwner()))
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UProjectInnerDoctrineTravelStateSubsystem* TravelStateSubsystem = GameInstance
		? GameInstance->GetSubsystem<UProjectInnerDoctrineTravelStateSubsystem>()
		: nullptr;
	if (!TravelStateSubsystem)
	{
		return;
	}
	if (bTravelStateLifecycleGenerationCaptured
		&& TravelStateSubsystem->GetLifecycleGeneration()
			!= TravelStateLifecycleGeneration)
	{
		// A new game began while this old pawn was still alive. Do not let its
		// EndPlay overwrite the freshly reset travel state.
		return;
	}
	if (!bTravelStateLifecycleGenerationCaptured)
	{
		TravelStateLifecycleGeneration =
			TravelStateSubsystem->GetLifecycleGeneration();
		bTravelStateLifecycleGenerationCaptured = true;
	}

	bWasPlayerTravelStateOwner = true;
	const float RemainingSeconds = bCursedEpisodeActive
		? FMath::Max(0.f, CursedEpisodeEndTimeSeconds - World->GetTimeSeconds())
		: 0.f;
	TravelStateSubsystem->StoreCurseState(
		GetCurse(),
		bCursedEpisodeActive,
		RemainingSeconds);
}

void UProjectInnerDoctrineComponent::InitializeAttributeStorage()
{
	const int32 Count = ToAttributeIndex(EProjectDoctrineAttribute::Count);
	if (AttributeLevels.Num() != Count)
	{
		AttributeLevels.Init(0, Count);
	}
}

void UProjectInnerDoctrineComponent::LoadPersistentState()
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : TEXT("ProjectInnerDoctrineV1");
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return;
	}

	if (const UProjectInnerDoctrineSaveGame* Save = Cast<UProjectInnerDoctrineSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex)))
	{
		MetaBankDxp = FMath::Max(0, Save->MetaBankDxp);
		bDoctrineMasteryMode = Save->bDoctrineMasteryMode;
	}
}

void UProjectInnerDoctrineComponent::SavePersistentState() const
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : TEXT("ProjectInnerDoctrineV1");
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;
	UProjectInnerDoctrineSaveGame* Save = Cast<UProjectInnerDoctrineSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UProjectInnerDoctrineSaveGame::StaticClass()));
	if (Save)
	{
		Save->MetaBankDxp = FMath::Max(0, MetaBankDxp);
		Save->bDoctrineMasteryMode = bDoctrineMasteryMode;
		UGameplayStatics::SaveGameToSlot(Save, SlotName, UserIndex);
	}
}

void UProjectInnerDoctrineComponent::BroadcastDxpChanged(
	const int32 OldCurrentRunDxp,
	const int32 OldMetaBankDxp)
{
	if (OldCurrentRunDxp != CurrentRunDxp || OldMetaBankDxp != MetaBankDxp)
	{
		OnDxpChanged.Broadcast(OldCurrentRunDxp, CurrentRunDxp, OldMetaBankDxp, MetaBankDxp);
	}
}

void UProjectInnerDoctrineComponent::ResetRunAttributes()
{
	for (int32 Index = 0; Index < AttributeLevels.Num(); ++Index)
	{
		const int32 OldLevel = AttributeLevels[Index];
		if (OldLevel > 0)
		{
			AttributeLevels[Index] = 0;
			const EProjectDoctrineAttribute Attribute = static_cast<EProjectDoctrineAttribute>(Index);
			OnAttributeLevelChanged.Broadcast(Attribute, OldLevel, 0, GetUpgradeCost(Attribute));
		}
	}
	bCunningTrapProtectionConsumed = false;
	ProcessedFloorTransitionIds.Reset();
	ApplyPassiveAttributeEffects();
}

void UProjectInnerDoctrineComponent::ApplyPassiveAttributeEffects()
{
	ApplyDynamicMaximums();
	ApplyWillpowerStatusImmunities();
	ApplyFaithPassiveAttributeEffects();
	ApplyLocomotionDoctrineModifiers();
}

void UProjectInnerDoctrineComponent::ApplyDynamicMaximums()
{
	if (!NeedsComponent)
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	if (!Settings)
	{
		return;
	}

	TSet<FName> NeedNames;
	TSet<FName> SensationNames;
	for (const FProjectSurvivalNeedState& Need : NeedsComponent->Needs)
	{
		if (!Need.NeedName.IsNone())
		{
			NeedNames.Add(Need.NeedName);
			RuntimeBaseNeedMaxByName.FindOrAdd(Need.NeedName, FMath::Max(Need.MaxValue, 0.001f));
		}
	}
	for (const FProjectSurvivalSensationState& Sensation : NeedsComponent->Sensations)
	{
		if (!Sensation.SensationName.IsNone())
		{
			SensationNames.Add(Sensation.SensationName);
			RuntimeBaseSensationMaxByName.FindOrAdd(
				Sensation.SensationName,
				FMath::Max(Sensation.MaxValue, 0.001f));
		}
	}
	for (const FProjectInnerDoctrineDynamicMaxRule& Rule : Settings->DynamicMaximumRules)
	{
		(Rule.bIsSensation ? SensationNames : NeedNames).Add(Rule.EntryName);
	}

	for (const FName Name : NeedNames)
	{
		if (NeedsComponent->HasNeed(Name))
		{
			ApplyDynamicNeedMaximum(Name, CalculateDynamicMaximum(Name, false, ResolveBaseNeedMax(Name)));
		}
	}
	for (const FName Name : SensationNames)
	{
		if (Name != CurseName && NeedsComponent->HasSensation(Name))
		{
			ApplyDynamicSensationMaximum(
				Name,
				CalculateDynamicMaximum(Name, true, ResolveBaseSensationMax(Name)));
		}
	}

	// Curse is deliberately fixed and never receives a Doctrine maximum bonus.
	if (NeedsComponent->HasSensation(CurseName))
	{
		NeedsComponent->SetSensationMaxValue(CurseName, GetCurseMax(), true);
	}
}

void UProjectInnerDoctrineComponent::ApplyDynamicNeedMaximum(
	const FName NeedName,
	const float NewMaxValue)
{
	if (!NeedsComponent || !NeedsComponent->HasNeed(NeedName))
	{
		return;
	}
	const float OldMax = NeedsComponent->GetNeedMaxValue(NeedName);
	if (FMath::IsNearlyEqual(OldMax, NewMaxValue))
	{
		return;
	}
	const float Ratio = OldMax > KINDA_SMALL_NUMBER
		? FMath::Clamp(NeedsComponent->GetNeedCurrentValue(NeedName) / OldMax, 0.f, 1.f)
		: 0.f;
	NeedsComponent->SetNeedMaxValue(NeedName, NewMaxValue, true);
	NeedsComponent->SetNeedCurrentValue(NeedName, NewMaxValue * Ratio, true);
}

void UProjectInnerDoctrineComponent::ApplyDynamicSensationMaximum(
	const FName SensationName,
	const float NewMaxValue)
{
	if (NeedsComponent
		&& NeedsComponent->HasSensation(SensationName)
		&& !FMath::IsNearlyEqual(NeedsComponent->GetSensationMaxValue(SensationName), NewMaxValue))
	{
		NeedsComponent->SetSensationMaxValue(SensationName, NewMaxValue, true);
	}
}

float UProjectInnerDoctrineComponent::CalculateDynamicMaximum(
	const FName EntryName,
	const bool bIsSensation,
	const float BaseMax) const
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	if (!Settings || EntryName == CurseName)
	{
		return EntryName == CurseName ? GetCurseMax() : FMath::Max(BaseMax, 0.001f);
	}

	float Flat = 0.f;
	float Multiplier = 1.f;
	for (const FProjectInnerDoctrineDynamicMaxRule& Rule : Settings->DynamicMaximumRules)
	{
		if (Rule.EntryName == EntryName
			&& Rule.bIsSensation == bIsSensation
			&& IsAttributeIndexValid(Rule.Attribute))
		{
			UProjectInnerDoctrineSettings::AccumulateDynamicMaxRule(
				Rule,
				GetAttributeLevel(Rule.Attribute),
				Flat,
				Multiplier);
		}
	}
	return UProjectInnerDoctrineSettings::ComputeDynamicMaxValue(BaseMax, Flat, Multiplier);
}

float UProjectInnerDoctrineComponent::ResolveBaseNeedMax(const FName NeedName) const
{
	if (const UProjectSurvivalNeedsSettings* Settings = UProjectSurvivalNeedsSettings::Get())
	{
		for (const FProjectSurvivalNeedState& Need : Settings->DefaultNeeds)
		{
			if (Need.NeedName == NeedName)
			{
				return FMath::Max(Need.MaxValue, 0.001f);
			}
		}
	}
	if (const float* RuntimeValue = RuntimeBaseNeedMaxByName.Find(NeedName))
	{
		return FMath::Max(*RuntimeValue, 0.001f);
	}
	return 100.f;
}

float UProjectInnerDoctrineComponent::ResolveBaseSensationMax(const FName SensationName) const
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	if (SensationName == MadnessName)
	{
		return Settings ? FMath::Max(Settings->MadnessMax, 0.001f) : 100.f;
	}
	if (SensationName == PainName)
	{
		return Settings ? FMath::Max(Settings->PainMax, 0.001f) : 100.f;
	}
	if (SensationName == CurseName)
	{
		return GetCurseMax();
	}
	if (const UProjectSurvivalNeedsSettings* NeedsSettings = UProjectSurvivalNeedsSettings::Get())
	{
		for (const FProjectSurvivalSensationState& Sensation : NeedsSettings->DefaultSensations)
		{
			if (Sensation.SensationName == SensationName)
			{
				return FMath::Max(Sensation.MaxValue, 0.001f);
			}
		}
	}
	if (const float* RuntimeValue = RuntimeBaseSensationMaxByName.Find(SensationName))
	{
		return FMath::Max(*RuntimeValue, 0.001f);
	}
	return 100.f;
}

void UProjectInnerDoctrineComponent::ApplyWillpowerStatusImmunities()
{
	if (!StatusComponent)
	{
		return;
	}
	if (HasMilestone(EProjectDoctrineAttribute::Willpower, 10))
	{
		const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
		StatusComponent->SetStatusImmunitySource(
			WillpowerImmunitySourceId,
			Settings ? Settings->WillpowerImmunityStatusNames : TArray<FName>{ TEXT("Fear"), TEXT("Dizzy") });
	}
	else
	{
		StatusComponent->ClearStatusImmunitySource(WillpowerImmunitySourceId);
	}
}

void UProjectInnerDoctrineComponent::ApplyFaithPassiveAttributeEffects()
{
	if (!CombatAttributeComponent || !CombatAttributeComponent->HasAttribute(SpellDefenseName))
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float DesiredBonus = UProjectInnerDoctrineSettings::ComputeFaithPassiveBonus(
		GetAttributeLevel(EProjectDoctrineAttribute::Faith),
		Settings ? Settings->FaithSpellDefensePerLevel : 1.f);
	if (FMath::IsNearlyEqual(AppliedFaithSpellDefenseBonus, DesiredBonus, 0.001f))
	{
		return;
	}

	const float BaseMax = FMath::Max(
		0.f,
		CombatAttributeComponent->GetAttributeMaxValue(SpellDefenseName)
			- AppliedFaithSpellDefenseBonus);
	const float BaseCurrent = FMath::Max(
		0.f,
		CombatAttributeComponent->GetAttributeCurrentValue(SpellDefenseName)
			- AppliedFaithSpellDefenseBonus);
	CombatAttributeComponent->SetAttributeMaxValue(SpellDefenseName, BaseMax + DesiredBonus, false);
	CombatAttributeComponent->SetAttributeCurrentValue(SpellDefenseName, BaseCurrent + DesiredBonus);
	AppliedFaithSpellDefenseBonus = DesiredBonus;
}

void UProjectInnerDoctrineComponent::UpdateFaithRecovery(const float DeltaTime)
{
	if (DeltaTime <= 0.f
		|| !HasMilestone(EProjectDoctrineAttribute::Faith, 5)
		|| !NeedsComponent
		|| !NeedsComponent->HasSensation(MadnessName))
	{
		return;
	}
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Recovery = UProjectInnerDoctrineSettings::ComputeFaithMadnessRecovery(
		DeltaTime,
		Settings ? Settings->FaithMadnessRecoveryPerSecond : 0.5f);
	NeedsComponent->ModifySensationValue(MadnessName, -Recovery, true);
}

void UProjectInnerDoctrineComponent::UpdateCombatState()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float ExitDelay = Settings ? FMath::Max(0.f, Settings->CombatStateExitSeconds) : 8.f;
	bCombatStateActive = Now - LastCombatImpactTimeSeconds < ExitDelay;
}

void UProjectInnerDoctrineComponent::MarkCombatImpact()
{
	LastCombatImpactTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	bCombatStateActive = true;
}

void UProjectInnerDoctrineComponent::UpdateCurse(const float DeltaTime)
{
	if (DeltaTime <= 0.f
		|| IsPassiveCurseDecaySuppressed()
		|| bCursedEpisodeActive
		|| ActiveCurseZonePresenceTokens.Num() > 0
		|| bCombatStateActive)
	{
		return;
	}
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float LastDanger = FMath::Max(LastCombatImpactTimeSeconds, LastCurseApplicationTimeSeconds);
	if (Now - LastDanger < (Settings ? Settings->CurseDecayDelaySeconds : 10.f))
	{
		return;
	}

	const float Rate = Settings ? Settings->CurseDecayPerSecond : 1.f;
	CleanseCurse(FMath::Max(0.f, Rate) * DeltaTime);
}

bool UProjectInnerDoctrineComponent::StartCursedEpisode()
{
	if (bCursedEpisodeActive)
	{
		return false;
	}
	bCursedEpisodeActive = true;
	CursedEpisodeEndTimeSeconds =
		(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) + ResolveCursedDurationSeconds();
	SetForcedStatus(CursedStatusName, true);
	OnCursedStateChanged.Broadcast(true);
	OnMilestoneTriggered.Broadcast(TEXT("Cursed"), EProjectDoctrineAttribute::Willpower, 0);
	StoreTravelCurseState();
	return true;
}

void UProjectInnerDoctrineComponent::CompleteCursedEpisode(const bool bApplyNormalRecovery)
{
	if (!bCursedEpisodeActive)
	{
		return;
	}
	bCursedEpisodeActive = false;
	CursedEpisodeEndTimeSeconds = -FLT_MAX;
	SetForcedStatus(CursedStatusName, false);
	OnCursedStateChanged.Broadcast(false);

	if (!bApplyNormalRecovery)
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	SetCurse(Settings ? Settings->CursedResidualCurse : 60.f);
	if (StatusComponent)
	{
		StatusComponent->ApplyStatus(
			ExhaustedRecoveryStatusName,
			Settings ? Settings->CursedRecoveryDurationSeconds : 8.f,
			GetOwner());
	}
	ApplyLocomotionDoctrineModifiers();
	StoreTravelCurseState();
}

void UProjectInnerDoctrineComponent::CancelCursedEpisode()
{
	if (!bCursedEpisodeActive)
	{
		return;
	}
	bCursedEpisodeActive = false;
	CursedEpisodeEndTimeSeconds = -FLT_MAX;
	SetForcedStatus(CursedStatusName, false);
	OnCursedStateChanged.Broadcast(false);
	StoreTravelCurseState();
}

float UProjectInnerDoctrineComponent::ResolveCursedDurationSeconds() const
{
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Duration = Settings ? Settings->CursedDurationSeconds : 8.f;
	return FMath::Max(0.1f, Duration);
}

float UProjectInnerDoctrineComponent::ResolveCurseResistanceMultiplier(
	const FProjectCurseApplicationContext& Context) const
{
	return ComputeCurseResistanceMultiplier(
		Context,
		0,
		0,
		0,
		false,
		UProjectInnerDoctrineSettings::Get());
}

bool UProjectInnerDoctrineComponent::HasRallyingPresence() const
{
	if (!HasMilestone(EProjectDoctrineAttribute::Charisma, 10)
		|| !GetWorld()
		|| !GetWorld()->GetGameInstance())
	{
		return false;
	}
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const UProjectSocialSubsystem* Social =
		GetWorld()->GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>();
	return Social && Social->HasLivingRecruitedCompanionWithin(
		GetOwner(),
		Settings ? Settings->CharismaCompanionRadius : 1200.f);
}

bool UProjectInnerDoctrineComponent::ConsumeCunningTrapProtection(
	const EProjectCurseSourceKind SourceKind)
{
	(void)SourceKind;
	return false;
}

void UProjectInnerDoctrineComponent::RecordCurseApplicationId(const FGuid& ApplicationId)
{
	if (!ApplicationId.IsValid())
	{
		return;
	}
	ProcessedCurseApplicationIds.Add(ApplicationId);
	ProcessedCurseApplicationOrder.Add(ApplicationId);
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const int32 MaxHistory = FMath::Max(
		1,
		Settings ? Settings->ProcessedCurseApplicationHistory : 256);
	while (ProcessedCurseApplicationOrder.Num() > MaxHistory)
	{
		const FGuid Removed = ProcessedCurseApplicationOrder[0];
		ProcessedCurseApplicationOrder.RemoveAt(0, 1, EAllowShrinking::No);
		ProcessedCurseApplicationIds.Remove(Removed);
	}
}

void UProjectInnerDoctrineComponent::BroadcastCrossedCurseWarnings(
	const float OldCurse,
	const float NewCurse)
{
	if (NewCurse <= OldCurse)
	{
		return;
	}
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Warning = Settings ? Settings->CurseWarningThreshold : 50.f;
	const float Critical = Settings ? Settings->CurseCriticalThreshold : 75.f;
	if (OldCurse < Warning && NewCurse >= Warning)
	{
		OnCurseWarning.Broadcast(Warning, NewCurse);
	}
	if (OldCurse < Critical && NewCurse >= Critical)
	{
		OnCurseWarning.Broadcast(Critical, NewCurse);
	}
}

void UProjectInnerDoctrineComponent::SetCurse(const float NewCurse)
{
	const float OldCurse = GetCurse();
	SetSensationCurrent(CurseName, FMath::Clamp(NewCurse, 0.f, GetCurseMax()));
	BroadcastCrossedCurseWarnings(OldCurse, GetCurse());
	StoreTravelCurseState();
}

float UProjectInnerDoctrineComponent::ModifySensation(
	const FName SensationName,
	const float Delta)
{
	return NeedsComponent
		? NeedsComponent->ModifySensationValue(SensationName, Delta, true)
		: 0.f;
}

float UProjectInnerDoctrineComponent::GetSensationCurrent(const FName SensationName) const
{
	return NeedsComponent ? NeedsComponent->GetSensationCurrentValue(SensationName) : 0.f;
}

float UProjectInnerDoctrineComponent::GetSensationMax(const FName SensationName) const
{
	return NeedsComponent ? NeedsComponent->GetSensationMaxValue(SensationName) : 0.f;
}

void UProjectInnerDoctrineComponent::SetSensationCurrent(
	const FName SensationName,
	const float NewValue)
{
	if (NeedsComponent)
	{
		NeedsComponent->SetSensationCurrentValue(SensationName, NewValue, true);
	}
}

void UProjectInnerDoctrineComponent::SetForcedStatus(
	const FName StatusName,
	const bool bActive)
{
	if (StatusComponent)
	{
		StatusComponent->SetForcedStatusActive(StatusName, bActive);
	}
}

bool UProjectInnerDoctrineComponent::HasMilestone(
	const EProjectDoctrineAttribute Attribute,
	const int32 Level) const
{
	return GetAttributeLevel(Attribute) >= Level;
}

bool UProjectInnerDoctrineComponent::IsAttributeIndexValid(
	const EProjectDoctrineAttribute Attribute) const
{
	const int32 Index = ToAttributeIndex(Attribute);
	return Index >= 0 && Index < ToAttributeIndex(EProjectDoctrineAttribute::Count);
}

FText UProjectInnerDoctrineComponent::GetAttributeDisplayName(
	const EProjectDoctrineAttribute Attribute) const
{
	switch (Attribute)
	{
	case EProjectDoctrineAttribute::Willpower: return LOCTEXT("Willpower", "Willpower");
	case EProjectDoctrineAttribute::Offensive: return LOCTEXT("Offensive", "Offensive");
	case EProjectDoctrineAttribute::Defensive: return LOCTEXT("Defensive", "Defensive");
	case EProjectDoctrineAttribute::Faith: return LOCTEXT("Faith", "Faith");
	case EProjectDoctrineAttribute::Cunning: return LOCTEXT("Cunning", "Cunning");
	case EProjectDoctrineAttribute::Celerity: return LOCTEXT("Celerity", "Celerity");
	case EProjectDoctrineAttribute::Charisma: return LOCTEXT("Charisma", "Charisma");
	default: return LOCTEXT("Unknown", "Unknown");
	}
}

float UProjectInnerDoctrineComponent::ResolveBestDxpGainMultiplier(
	const TArray<EProjectDoctrineAttribute>& Affinities) const
{
	bool bFound = false;
	float Best = 1.f;
	for (const EProjectDoctrineAttribute Attribute : Affinities)
	{
		if (const float* Value = DxpGainMultipliersByAttribute.Find(Attribute))
		{
			Best = bFound ? FMath::Max(Best, FMath::Max(0.f, *Value)) : FMath::Max(0.f, *Value);
			bFound = true;
		}
	}
	return Best;
}

float UProjectInnerDoctrineComponent::ResolveExternalDxpGainMultiplier() const
{
	float Multiplier = 1.f;
	for (const TPair<FName, float>& Pair : ExternalDxpGainMultipliersBySource)
	{
		Multiplier *= FMath::Max(0.f, Pair.Value);
	}
	return Multiplier;
}

float UProjectInnerDoctrineComponent::ResolveExternalFlatDamageBonus() const
{
	float Total = 0.f;
	for (const TPair<FName, float>& Pair : ExternalFlatDamageBonusesBySource)
	{
		Total += FMath::Max(0.f, Pair.Value);
	}
	return Total;
}

bool UProjectInnerDoctrineComponent::IsCurseSourceActor(const AActor* Actor) const
{
	return IsCanonicalCurseSourceActor(Actor);
}

bool UProjectInnerDoctrineComponent::IsCanonicalCurseSourceActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (const UProjectCurseSourceComponent* SourceComponent =
		Actor->FindComponentByClass<UProjectCurseSourceComponent>())
	{
		return SourceComponent->IsCurseSourceEnabled();
	}

	if (const IGameplayTagAssetInterface* TagInterface =
		Cast<IGameplayTagAssetInterface>(Actor))
	{
		if (TagInterface->HasMatchingGameplayTag(ProjectCurseGameplayTags::Source))
		{
			return true;
		}
	}

	const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(
			Actor,
			true);
	return AbilitySystem
		&& AbilitySystem->HasMatchingGameplayTag(ProjectCurseGameplayTags::Source);
}

bool UProjectInnerDoctrineComponent::IsRelevantEnemyActor(const AActor* Actor) const
{
	if (!Actor || Actor == GetOwner())
	{
		return false;
	}
	if (Actor->ActorHasTag(TEXT("Project.Enemy")) || IsCurseSourceActor(Actor))
	{
		return true;
	}
	const UProjectRealtimeSnapshotSettings* Settings = UProjectRealtimeSnapshotSettings::Get();
	return FProjectDefeatHitResolver::DoesActorMatchClassHintsRecursive(
		Actor,
		Settings ? Settings->RelevantEnemyClassNameHints : TArray<FString>());
}

void UProjectInnerDoctrineComponent::TryTriggerCleanFinish(
	AActor* TargetActor,
	const FProjectCombatDamageResult& DamageResult)
{
	if (!HasMilestone(EProjectDoctrineAttribute::Offensive, 10)
		|| !DamageResult.bKilledTarget
		|| !IsCurseSourceActor(TargetActor)
		|| DamageResult.TargetMaxValue <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Threshold = FMath::Clamp(
		Settings ? Settings->OffensiveExecuteHealthThresholdPct : 0.20f,
		0.f,
		1.f);
	if (DamageResult.PreDamageTargetValue / DamageResult.TargetMaxValue > Threshold)
	{
		return;
	}
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now < CleanFinishCooldownEndTimeSeconds)
	{
		return;
	}

	CleanseCurse(Settings ? Settings->OffensiveCleanFinishCurseReduction : 10.f);
	CleanFinishBuffEndTimeSeconds =
		Now + (Settings ? Settings->OffensiveCleanFinishBuffSeconds : 6.f);
	CleanFinishCooldownEndTimeSeconds =
		Now + (Settings ? Settings->OffensiveCleanFinishCooldownSeconds : 10.f);
	OnMilestoneTriggered.Broadcast(TEXT("CleanFinish"), EProjectDoctrineAttribute::Offensive, 10);
}

void UProjectInnerDoctrineComponent::UpdateTimedDoctrineEffects()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (RecoveredMomentumEndTimeSeconds > -FLT_MAX && Now >= RecoveredMomentumEndTimeSeconds)
	{
		RecoveredMomentumEndTimeSeconds = -FLT_MAX;
		ApplyLocomotionDoctrineModifiers();
	}
}

void UProjectInnerDoctrineComponent::ApplyLocomotionDoctrineModifiers()
{
	if (!LocomotionOverrideComponent)
	{
		return;
	}

	// Curse-linked Doctrine movement abilities are intentionally blank pending redesign.
	LocomotionOverrideComponent->ClearMovementSpeedModifier(
		ProjectMovementModifierTags::DoctrineCelerity(),
		EProjectMovementModifierLayer::DoctrineBonus);
	LocomotionOverrideComponent->ClearMovementSpeedModifier(
		ProjectMovementModifierTags::DoctrineRecoveredMomentum(),
		EProjectMovementModifierLayer::DoctrineBonus);
	LocomotionOverrideComponent->ClearStatusPenaltyMitigation(
		ProjectMovementModifierTags::ForStatus(CursedStatusName));
	LocomotionOverrideComponent->ClearStatusPenaltyMitigation(
		ProjectMovementModifierTags::ForStatus(ExhaustedRecoveryStatusName));
}

bool UProjectInnerDoctrineComponent::TryStartGuardRecovery(AActor* SourceActor)
{
	if (bGuardRecoveryActive
		|| !bCursedEpisodeActive
		|| !HasMilestone(EProjectDoctrineAttribute::Defensive, 5)
		|| !IsRelevantEnemyActor(SourceActor))
	{
		return false;
	}

	const float PainMax = GetSensationMax(PainName);
	if (PainMax <= KINDA_SMALL_NUMBER
		|| GetSensationCurrent(PainName) < PainMax - KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const bool bPainSpike = HasMilestone(EProjectDoctrineAttribute::Defensive, 10);
	CancelCursedEpisode();
	bGuardRecoveryActive = true;
	GuardRecoveryAbsorbedDamage = 0.f;
	GuardRecoveryPoolRemaining = ResolveOwnerMaxHealth()
		* (bPainSpike
			? (Settings ? Settings->PainSpikePoolPct : 0.40f)
			: (Settings ? Settings->GuardRecoveryPoolPct : 0.25f));
	if (GuardRecoveryPoolRemaining <= KINDA_SMALL_NUMBER)
	{
		bGuardRecoveryActive = false;
		GuardRecoveryPoolRemaining = 0.f;
		return false;
	}
	GuardRecoveryEndTimeSeconds =
		(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f)
		+ (Settings ? Settings->GuardRecoveryDurationSeconds : 3.f);
	OnMilestoneTriggered.Broadcast(TEXT("GuardRecovery"), EProjectDoctrineAttribute::Defensive, 5);
	return true;
}

void UProjectInnerDoctrineComponent::CompleteGuardRecovery()
{
	if (!bGuardRecoveryActive)
	{
		return;
	}
	bGuardRecoveryActive = false;
	GuardRecoveryPoolRemaining = 0.f;
	GuardRecoveryEndTimeSeconds = -FLT_MAX;

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const bool bPainSpike = HasMilestone(EProjectDoctrineAttribute::Defensive, 10);
	SetSensationCurrent(
		PainName,
		bPainSpike
			? (Settings ? Settings->PainSpikeEndPain : 0.f)
			: (Settings ? Settings->GuardRecoveryEndPain : 50.f));
	SetCurse(
		bPainSpike
			? (Settings ? Settings->PainSpikeEndCurse : 60.f)
			: (Settings ? Settings->GuardRecoveryEndCurse : 70.f));
	if (StatusComponent)
	{
		StatusComponent->ApplyStatus(
			ExhaustedRecoveryStatusName,
			bPainSpike
				? (Settings ? Settings->PainSpikeRecoverySeconds : 5.f)
				: (Settings ? Settings->GuardRecoveryRecoverySeconds : 8.f),
			GetOwner());
	}
	if (bPainSpike)
	{
		EmitPainSpike();
		OnMilestoneTriggered.Broadcast(TEXT("PainSpike"), EProjectDoctrineAttribute::Defensive, 10);
	}
}

float UProjectInnerDoctrineComponent::ResolveOwnerMaxHealth() const
{
	float Current = 0.f;
	float Max = 0.f;
	if (RealtimeSnapshotComponent
		&& RealtimeSnapshotComponent->TryReadOwnerResource(HealthName, Current, Max)
		&& Max > KINDA_SMALL_NUMBER)
	{
		return Max;
	}
	if (CombatAttributeComponent)
	{
		const FName AttributeName = CombatAttributeComponent->HealthAttributeName;
		return FMath::Max(0.f, CombatAttributeComponent->GetAttributeMaxValue(AttributeName));
	}
	return 100.f;
}

void UProjectInnerDoctrineComponent::EmitPainSpike()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Radius = Settings ? Settings->PainSpikeRadius : 350.f;
	const float PoiseDelta = -(Settings ? Settings->PainSpikePoiseDamage : 25.f);
	const FGameplayTag EquilibriumTag = FGameplayTag::RequestGameplayTag(
		TEXT("RPG.Statistics.Equilibrium"),
		false);
	if (!EquilibriumTag.IsValid())
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsRelevantEnemyActor(Candidate)
			|| FVector::DistSquared(Candidate->GetActorLocation(), OwnerActor->GetActorLocation())
				> FMath::Square(Radius))
		{
			continue;
		}
		if (UARSStatisticsComponent* Statistics =
			Candidate->FindComponentByClass<UARSStatisticsComponent>())
		{
			Statistics->ModifyStatistic(EquilibriumTag, PoiseDelta);
		}
	}
}

void UProjectInnerDoctrineComponent::TriggerGuardRecoveryHitFeedback(AActor* SourceActor)
{
	if (!bGuardRecoveryActive)
	{
		return;
	}
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	const float Now = World->GetTimeSeconds();
	const float Cooldown = Settings ? Settings->GuardRecoveryFeedbackCooldownSeconds : 0.08f;
	if (Now - LastGuardRecoveryFeedbackTimeSeconds < FMath::Max(0.f, Cooldown))
	{
		return;
	}
	if (!AcfEffectsManagerComponent)
	{
		AcfEffectsManagerComponent = OwnerActor->FindComponentByClass<UACFEffectsManagerComponent>();
	}
	if (!AcfEffectsManagerComponent)
	{
		return;
	}
	if (!SourceActor && RealtimeSnapshotComponent)
	{
		SourceActor = RealtimeSnapshotComponent->FindNearestRelevantEnemyToOwner();
	}

	FACFDamageEvent Event;
	Event.DamageDealer = SourceActor;
	Event.DamageReceiver = OwnerActor;
	Event.FinalDamage = 0.f;
	Event.contextString = TEXT("Defensive.GuardRecoveryFeedback");
	Event.DamageClass = ResolveGuardRecoveryFeedbackDamageClass(SourceActor);
	Event.HitResponseAction = UACFFunctionLibrary::GetDefaultHitState();
	Event.DamageZone = EDamageZone::ENormal;
	Event.bIsCritical = false;

	const FVector OwnerLocation = OwnerActor->GetActorLocation();
	const FVector SourceLocation = SourceActor
		? SourceActor->GetActorLocation()
		: OwnerLocation - OwnerActor->GetActorForwardVector() * 100.f;
	const FVector Direction = (OwnerLocation - SourceLocation).GetSafeNormal();
	const FVector Normal = Direction.IsNearlyZero() ? -OwnerActor->GetActorForwardVector() : -Direction;
	Event.hitResult.Location = OwnerLocation;
	Event.hitResult.ImpactPoint = OwnerLocation;
	Event.hitResult.TraceStart = SourceLocation;
	Event.hitResult.TraceEnd = OwnerLocation;
	Event.hitResult.Normal = Normal;
	Event.hitResult.ImpactNormal = Normal;
	Event.hitResult.Distance = FVector::Distance(SourceLocation, OwnerLocation);
	Event.hitResult.HitObjectHandle = FActorInstanceHandle(OwnerActor);
	Event.hitResult.BoneName = TEXT("pelvis");
	Event.hitDirection = Direction;
	if (const ACharacter* Character = Cast<ACharacter>(OwnerActor))
	{
		Event.hitResult.Component = Character->GetMesh();
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (FBodyInstance* Body = Mesh->GetBodyInstance(Event.hitResult.BoneName))
			{
				Event.PhysMaterial = Body->GetSimplePhysicalMaterial();
				Event.hitResult.PhysMaterial = Event.PhysMaterial;
			}
		}
	}
	else
	{
		Event.hitResult.Component = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
	}

	if (OwnerActor->HasAuthority())
	{
		AcfEffectsManagerComponent->PlayHitReactionEffect(Event);
	}
	AcfEffectsManagerComponent->OnDamageImpactReceived(Event);
	PlayGuardRecoveryHitSound(Event);
	if (Settings ? Settings->bGuardRecoverySuppressDamageText : true)
	{
		QueueGuardRecoveryDamageTextSuppression();
	}
	LastGuardRecoveryFeedbackTimeSeconds = Now;
}

void UProjectInnerDoctrineComponent::QueueGuardRecoveryDamageTextSuppression()
{
	UWorld* World = GetWorld();
	if (!World || !bGuardRecoveryActive)
	{
		return;
	}
	SuppressGuardRecoveryDamageTextWidgets();
	FTimerDelegate NextTick;
	NextTick.BindWeakLambda(this, [this]() { SuppressGuardRecoveryDamageTextWidgets(); });
	World->GetTimerManager().SetTimerForNextTick(NextTick);
}

void UProjectInnerDoctrineComponent::SuppressGuardRecoveryDamageTextWidgets()
{
	UWorld* World = GetWorld();
	if (!World || !bGuardRecoveryActive)
	{
		return;
	}
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
		World,
		Widgets,
		UACFDamageWidget::StaticClass(),
		false);
	for (UUserWidget* Widget : Widgets)
	{
		if (Widget && Widget->GetWorld() == World)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			Widget->RemoveFromParent();
		}
	}
}

void UProjectInnerDoctrineComponent::PlayGuardRecoveryHitSound(
	const FACFDamageEvent& DamageEvent)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World || !AcfEffectsManagerComponent)
	{
		return;
	}
	FBaseFX HitFx;
	if (!AcfEffectsManagerComponent->TryGetDamageFX(
			DamageEvent.HitResponseAction,
			DamageEvent.DamageClass,
			HitFx)
		|| !HitFx.ActionSound)
	{
		return;
	}
	if (ACharacter* Character = Cast<ACharacter>(OwnerActor))
	{
		UGameplayStatics::SpawnSoundAttached(HitFx.ActionSound, Character->GetMesh(), TEXT("pelvis"));
	}
	else
	{
		UGameplayStatics::SpawnSoundAtLocation(World, HitFx.ActionSound, OwnerActor->GetActorLocation());
	}
}

TSubclassOf<UACFDamageType> UProjectInnerDoctrineComponent::ResolveGuardRecoveryFeedbackDamageClass(
	AActor* SourceActor) const
{
	const FString SourceName = SourceActor
		? SourceActor->GetName() + TEXT(" ") + SourceActor->GetClass()->GetName()
		: FString();
	if (SourceName.Contains(TEXT("Mage"), ESearchCase::IgnoreCase)
		|| SourceName.Contains(TEXT("Magic"), ESearchCase::IgnoreCase)
		|| SourceName.Contains(TEXT("Spell"), ESearchCase::IgnoreCase))
	{
		return USpellDamageType::StaticClass();
	}
	if (SourceName.Contains(TEXT("Ranged"), ESearchCase::IgnoreCase)
		|| SourceName.Contains(TEXT("Projectile"), ESearchCase::IgnoreCase)
		|| SourceName.Contains(TEXT("Arrow"), ESearchCase::IgnoreCase))
	{
		return LoadFeedbackDamageClass(
			TEXT("/AscentCombatFramework/Blueprints/DamageTypes/ACFProjectileDamageType.ACFProjectileDamageType_C"),
			URangedDamageType::StaticClass());
	}
	return LoadFeedbackDamageClass(
		TEXT("/AscentCombatFramework/Blueprints/DamageTypes/ACFCutDamageType.ACFCutDamageType_C"),
		UMeleeDamageType::StaticClass());
}

void UProjectInnerDoctrineComponent::AddMilestoneSnapshot(
	TArray<FProjectDoctrineMilestoneState>& OutMilestones,
	const FProjectDoctrineMilestoneDefinition& Definition) const
{
	FProjectDoctrineMilestoneState State;
	State.Definition = Definition;
	State.bUnlocked = HasMilestone(Definition.Attribute, Definition.RequiredLevel);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Definition.AbilityId == TEXT("CleanFinish"))
	{
		State.CooldownRemainingSeconds = FMath::Max(0.f, CleanFinishCooldownEndTimeSeconds - Now);
		State.bOnCooldown = State.CooldownRemainingSeconds > 0.f;
	}
	OutMilestones.Add(State);
}

void UProjectInnerDoctrineComponent::BroadcastMilestonesForLevelRange(
	const EProjectDoctrineAttribute Attribute,
	const int32 OldLevel,
	const int32 NewLevel)
{
	if (NewLevel <= OldLevel)
	{
		return;
	}
	if (const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get())
	{
		for (const FProjectDoctrineMilestoneDefinition& Definition : Settings->MilestoneDefinitions)
		{
			if (Definition.Attribute == Attribute
				&& OldLevel < Definition.RequiredLevel
				&& NewLevel >= Definition.RequiredLevel)
			{
				OnMilestoneTriggered.Broadcast(
					Definition.AbilityId,
					Attribute,
					Definition.RequiredLevel);
			}
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void UProjectInnerDoctrineComponent::AutomationSetCurse(const float NewCurse)
{
	SetCurse(NewCurse);
}

void UProjectInnerDoctrineComponent::AutomationSetCombatState(const bool bActive)
{
	bCombatStateActive = bActive;
	LastCombatImpactTimeSeconds = bActive
		? (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f)
		: -FLT_MAX;
}

void UProjectInnerDoctrineComponent::AutomationCompleteCursedEpisode()
{
	CompleteCursedEpisode(true);
}

bool UProjectInnerDoctrineComponent::AutomationIsRecoveredMomentumActive() const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	return Now < RecoveredMomentumEndTimeSeconds;
}

void UProjectInnerDoctrineComponent::AutomationExpireRecoveredMomentum()
{
	RecoveredMomentumEndTimeSeconds = -FLT_MAX;
	ApplyLocomotionDoctrineModifiers();
}

float UProjectInnerDoctrineComponent::AutomationGetGuardPoolRemaining() const
{
	return GuardRecoveryPoolRemaining;
}

float UProjectInnerDoctrineComponent::AutomationGetLastResolvedCurseResistanceMultiplier() const
{
	return LastResolvedCurseResistanceMultiplier;
}
#endif

#undef LOCTEXT_NAMESPACE
