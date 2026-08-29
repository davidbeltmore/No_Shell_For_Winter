#include "Survival/ProjectSurvivalNeedsComponent.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "Survival/ProjectSurvivalNeedsSettings.h"
#include "Survival/ProjectSurvivalStatusComponent.h"

namespace
{
	static const FName HungerName(TEXT("Hunger"));
	static const FName ThirstName(TEXT("Thirst"));
	static const FName SleepName(TEXT("Sleep"));
	static const FName MadnessName(TEXT("Madness"));
	static const FName PainName(TEXT("Pain"));
	static const FName CurseName(TEXT("Curse"));

	float IntegrateSensationValue(const float CurrentValue, const float MaxValue, const float DeltaPerSecond, const float DeltaTime)
	{
		return FMath::Clamp(CurrentValue + (DeltaPerSecond * FMath::Max(DeltaTime, 0.f)), 0.f, FMath::Max(MaxValue, 0.f));
	}
}

UProjectSurvivalNeedsComponent::UProjectSurvivalNeedsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.20f;
	CachedPenaltyMultiplier = 1.f;

	ApplySettingsDefaults();
}

void UProjectSurvivalNeedsComponent::BeginPlay()
{
	Super::BeginPlay();

	StatusComponent = GetOwner() ? GetOwner()->FindComponentByClass<UProjectSurvivalStatusComponent>() : nullptr;
	SanitizeState();
	UpdateCachedPenaltyMultiplier(false);
}

void UProjectSurvivalNeedsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEnableAutoDecay || DeltaTime <= 0.f)
	{
		return;
	}

	if (!StatusComponent && GetOwner())
	{
		StatusComponent = GetOwner()->FindComponentByClass<UProjectSurvivalStatusComponent>();
	}

	bool bChanged = false;
	for (FProjectSurvivalNeedState& Need : Needs)
	{
		if (Need.NeedName.IsNone())
		{
			continue;
		}

		if (Need.DecayPerSecond > 0.f)
		{
			const float OldValue = Need.CurrentValue;
			float EffectiveDecayPerSecond = Need.DecayPerSecond * FMath::Max(0.f, NeedsDecayMultiplier);
			if (StatusComponent)
			{
				EffectiveDecayPerSecond *= StatusComponent->GetNeedDecayMultiplier(Need.NeedName);
			}
			EffectiveDecayPerSecond *= GetExternalNeedDecayMultiplier(Need.NeedName);
			Need.CurrentValue = ClampToEntryRange(Need.CurrentValue - (EffectiveDecayPerSecond * DeltaTime), 0.f, Need.MaxValue);
			if (!FMath::IsNearlyEqual(OldValue, Need.CurrentValue))
			{
				bChanged = true;
				BroadcastValueChanged(Need.NeedName, OldValue, Need.CurrentValue, Need.MaxValue, false);
			}
		}

		if (Need.RecoveryPerSecond > 0.f && Need.CurrentValue < Need.MaxValue)
		{
			const float OldValue = Need.CurrentValue;
			Need.CurrentValue = ClampToEntryRange(Need.CurrentValue + (Need.RecoveryPerSecond * DeltaTime), 0.f, Need.MaxValue);
			if (!FMath::IsNearlyEqual(OldValue, Need.CurrentValue))
			{
				bChanged = true;
				BroadcastValueChanged(Need.NeedName, OldValue, Need.CurrentValue, Need.MaxValue, false);
			}
		}
	}

	for (FProjectSurvivalSensationState& Sensation : Sensations)
	{
		if (Sensation.SensationName.IsNone())
		{
			continue;
		}

		const float StatusDeltaPerSecond = StatusComponent
			? StatusComponent->GetSensationDeltaPerSecond(Sensation.SensationName)
			: 0.f;
		const float DeltaPerSecond = Sensation.PassiveDeltaPerSecond + StatusDeltaPerSecond;
		if (FMath::IsNearlyZero(DeltaPerSecond))
		{
			continue;
		}

		const float OldValue = Sensation.CurrentValue;
		Sensation.CurrentValue = IntegrateSensationValue(Sensation.CurrentValue, Sensation.MaxValue, DeltaPerSecond, DeltaTime);
		if (!FMath::IsNearlyEqual(OldValue, Sensation.CurrentValue))
		{
			bChanged = true;
			BroadcastValueChanged(Sensation.SensationName, OldValue, Sensation.CurrentValue, Sensation.MaxValue, true);
		}
	}

	if (bChanged)
	{
		UpdateCachedPenaltyMultiplier(true);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
float UProjectSurvivalNeedsComponent::AutomationIntegrateSensationValue(
	const float CurrentValue,
	const float MaxValue,
	const float DeltaPerSecond,
	const float DeltaTime)
{
	return IntegrateSensationValue(CurrentValue, MaxValue, DeltaPerSecond, DeltaTime);
}
#endif

void UProjectSurvivalNeedsComponent::ResetToDefaults()
{
	ExternalNeedDecayMultipliersBySource.Reset();
	ApplySettingsDefaults();
	SanitizeState();
	UpdateCachedPenaltyMultiplier(true);
}

bool UProjectSurvivalNeedsComponent::HasNeed(FName NeedName) const
{
	return FindNeed(NeedName) != nullptr;
}

float UProjectSurvivalNeedsComponent::GetNeedCurrentValue(FName NeedName) const
{
	if (const FProjectSurvivalNeedState* Need = FindNeed(NeedName))
	{
		return Need->CurrentValue;
	}

	return 0.f;
}

float UProjectSurvivalNeedsComponent::GetNeedNormalizedValue(FName NeedName) const
{
	if (const FProjectSurvivalNeedState* Need = FindNeed(NeedName))
	{
		const float MaxValue = FMath::Max(Need->MaxValue, 0.001f);
		return FMath::Clamp(Need->CurrentValue / MaxValue, 0.f, 1.f);
	}

	return 0.f;
}

int32 UProjectSurvivalNeedsComponent::GetNeedBarsFilled(FName NeedName) const
{
	const int32 SafeBarCount = FMath::Max(1, BarsPerNeed);
	return FMath::Clamp(FMath::FloorToInt(GetNeedNormalizedValue(NeedName) * static_cast<float>(SafeBarCount) + KINDA_SMALL_NUMBER), 0, SafeBarCount);
}

float UProjectSurvivalNeedsComponent::GetNeedMaxValue(FName NeedName) const
{
	if (const FProjectSurvivalNeedState* Need = FindNeed(NeedName))
	{
		return Need->MaxValue;
	}

	return 0.f;
}

float UProjectSurvivalNeedsComponent::GetNeedsPenaltyMultiplier() const
{
	return CachedPenaltyMultiplier;
}

float UProjectSurvivalNeedsComponent::GetNeedsPenaltyPercent() const
{
	return 1.f - GetNeedsPenaltyMultiplier();
}

float UProjectSurvivalNeedsComponent::ComputeAttributeValueAfterPenalty(float BaseValue) const
{
	return BaseValue * GetNeedsPenaltyMultiplier();
}

float UProjectSurvivalNeedsComponent::ComputeAttributeValueAfterPenaltyForName(FName AttributeName, float BaseValue) const
{
	return AffectedSecondaryAttributes.Contains(AttributeName) ? ComputeAttributeValueAfterPenalty(BaseValue) : BaseValue;
}

bool UProjectSurvivalNeedsComponent::SetNeedCurrentValue(FName NeedName, float NewValue, bool bBroadcast)
{
	if (FProjectSurvivalNeedState* Need = FindNeedMutable(NeedName))
	{
		const float OldValue = Need->CurrentValue;
		Need->CurrentValue = ClampToEntryRange(NewValue, 0.f, Need->MaxValue);
		if (bBroadcast && !FMath::IsNearlyEqual(OldValue, Need->CurrentValue))
		{
			BroadcastValueChanged(Need->NeedName, OldValue, Need->CurrentValue, Need->MaxValue, false);
		}
		UpdateCachedPenaltyMultiplier(bBroadcast);
		return true;
	}

	return false;
}

float UProjectSurvivalNeedsComponent::ModifyNeedValue(FName NeedName, float DeltaValue, bool bBroadcast)
{
	return ApplyNeedDeltaValue(NeedName, DeltaValue, bBroadcast, true);
}

float UProjectSurvivalNeedsComponent::ApplyNeedDeltaValue(FName NeedName, float DeltaValue, bool bBroadcast, bool bClampToRange)
{
	if (FProjectSurvivalNeedState* Need = FindNeedMutable(NeedName))
	{
		const float OldValue = Need->CurrentValue;
		Need->CurrentValue = bClampToRange
			? ClampToEntryRange(Need->CurrentValue + DeltaValue, 0.f, Need->MaxValue)
			: Need->CurrentValue + DeltaValue;
		const float AppliedDelta = Need->CurrentValue - OldValue;
		if (bBroadcast && !FMath::IsNearlyZero(AppliedDelta))
		{
			BroadcastValueChanged(Need->NeedName, OldValue, Need->CurrentValue, Need->MaxValue, false);
		}
		UpdateCachedPenaltyMultiplier(bBroadcast);
		return AppliedDelta;
	}

	return 0.f;
}

bool UProjectSurvivalNeedsComponent::SetNeedMaxValue(FName NeedName, float NewMaxValue, bool bClampCurrentValue)
{
	if (FProjectSurvivalNeedState* Need = FindNeedMutable(NeedName))
	{
		const float OldValue = Need->CurrentValue;
		const float OldMaxValue = Need->MaxValue;
		Need->MaxValue = FMath::Max(NewMaxValue, 0.001f);
		if (bClampCurrentValue)
		{
			Need->CurrentValue = ClampToEntryRange(Need->CurrentValue, 0.f, Need->MaxValue);
		}
		if (!FMath::IsNearlyEqual(OldMaxValue, Need->MaxValue) || !FMath::IsNearlyEqual(OldValue, Need->CurrentValue))
		{
			BroadcastValueChanged(Need->NeedName, OldValue, Need->CurrentValue, Need->MaxValue, false);
		}
		UpdateCachedPenaltyMultiplier(true);
		return true;
	}

	return false;
}

bool UProjectSurvivalNeedsComponent::SetNeedDecayRate(FName NeedName, float NewDecayPerSecond)
{
	if (FProjectSurvivalNeedState* Need = FindNeedMutable(NeedName))
	{
		Need->DecayPerSecond = FMath::Max(0.f, NewDecayPerSecond);
		return true;
	}

	return false;
}

bool UProjectSurvivalNeedsComponent::SetNeedRecoveryRate(FName NeedName, float NewRecoveryPerSecond)
{
	if (FProjectSurvivalNeedState* Need = FindNeedMutable(NeedName))
	{
		Need->RecoveryPerSecond = FMath::Max(0.f, NewRecoveryPerSecond);
		return true;
	}

	return false;
}

bool UProjectSurvivalNeedsComponent::SetExternalNeedDecayMultiplier(const FName SourceId, const FName NeedName, const float Multiplier)
{
	if (SourceId.IsNone() || NeedName.IsNone() || !HasNeed(NeedName))
	{
		return false;
	}

	TMap<FName, float>& MultipliersByNeed = ExternalNeedDecayMultipliersBySource.FindOrAdd(SourceId);
	MultipliersByNeed.Add(NeedName, FMath::Max(Multiplier, 0.0f));
	return true;
}

bool UProjectSurvivalNeedsComponent::ClearExternalNeedDecayMultiplier(const FName SourceId, const FName NeedName)
{
	if (SourceId.IsNone() || NeedName.IsNone())
	{
		return false;
	}

	TMap<FName, float>* MultipliersByNeed = ExternalNeedDecayMultipliersBySource.Find(SourceId);
	if (!MultipliersByNeed)
	{
		return false;
	}

	const int32 RemovedCount = MultipliersByNeed->Remove(NeedName);
	if (MultipliersByNeed->Num() == 0)
	{
		ExternalNeedDecayMultipliersBySource.Remove(SourceId);
	}

	return RemovedCount > 0;
}

float UProjectSurvivalNeedsComponent::GetExternalNeedDecayMultiplier(const FName NeedName) const
{
	if (NeedName.IsNone())
	{
		return 1.0f;
	}

	float CombinedMultiplier = 1.0f;
	for (const TPair<FName, TMap<FName, float>>& Pair : ExternalNeedDecayMultipliersBySource)
	{
		if (const float* Multiplier = Pair.Value.Find(NeedName))
		{
			CombinedMultiplier *= FMath::Max(*Multiplier, 0.0f);
		}
	}

	return CombinedMultiplier;
}

bool UProjectSurvivalNeedsComponent::SetSensationCurrentValue(FName SensationName, float NewValue, bool bBroadcast)
{
	if (FProjectSurvivalSensationState* Sensation = FindSensationMutable(SensationName))
	{
		const float OldValue = Sensation->CurrentValue;
		Sensation->CurrentValue = ClampToEntryRange(NewValue, 0.f, Sensation->MaxValue);
		if (bBroadcast && !FMath::IsNearlyEqual(OldValue, Sensation->CurrentValue))
		{
			BroadcastValueChanged(Sensation->SensationName, OldValue, Sensation->CurrentValue, Sensation->MaxValue, true);
		}
		return true;
	}

	return false;
}

bool UProjectSurvivalNeedsComponent::HasSensation(FName SensationName) const
{
	return FindSensation(SensationName) != nullptr;
}

float UProjectSurvivalNeedsComponent::GetSensationCurrentValue(FName SensationName) const
{
	if (const FProjectSurvivalSensationState* Sensation = FindSensation(SensationName))
	{
		return Sensation->CurrentValue;
	}

	return 0.f;
}

float UProjectSurvivalNeedsComponent::GetSensationNormalizedValue(FName SensationName) const
{
	if (const FProjectSurvivalSensationState* Sensation = FindSensation(SensationName))
	{
		const float MaxValue = FMath::Max(Sensation->MaxValue, 0.001f);
		return FMath::Clamp(Sensation->CurrentValue / MaxValue, 0.f, 1.f);
	}

	return 0.f;
}

float UProjectSurvivalNeedsComponent::GetSensationMaxValue(FName SensationName) const
{
	if (const FProjectSurvivalSensationState* Sensation = FindSensation(SensationName))
	{
		return Sensation->MaxValue;
	}

	return 0.f;
}

bool UProjectSurvivalNeedsComponent::SetSensationMaxValue(FName SensationName, const float NewMaxValue, const bool bClampCurrentValue)
{
	if (FProjectSurvivalSensationState* Sensation = FindSensationMutable(SensationName))
	{
		const float OldValue = Sensation->CurrentValue;
		const float OldMaxValue = Sensation->MaxValue;
		Sensation->MaxValue = FMath::Max(NewMaxValue, 0.001f);
		if (bClampCurrentValue)
		{
			Sensation->CurrentValue = ClampToEntryRange(Sensation->CurrentValue, 0.f, Sensation->MaxValue);
		}

		if (!FMath::IsNearlyEqual(OldMaxValue, Sensation->MaxValue) || !FMath::IsNearlyEqual(OldValue, Sensation->CurrentValue))
		{
			BroadcastValueChanged(Sensation->SensationName, OldValue, Sensation->CurrentValue, Sensation->MaxValue, true);
		}
		return true;
	}

	return false;
}

float UProjectSurvivalNeedsComponent::ModifySensationValue(FName SensationName, float DeltaValue, bool bBroadcast)
{
	return ApplySensationDeltaValue(SensationName, DeltaValue, bBroadcast, true);
}

float UProjectSurvivalNeedsComponent::ApplySensationDeltaValue(FName SensationName, float DeltaValue, bool bBroadcast, bool bClampToRange)
{
	if (FProjectSurvivalSensationState* Sensation = FindSensationMutable(SensationName))
	{
		const float OldValue = Sensation->CurrentValue;
		Sensation->CurrentValue = bClampToRange
			? ClampToEntryRange(Sensation->CurrentValue + DeltaValue, 0.f, Sensation->MaxValue)
			: Sensation->CurrentValue + DeltaValue;
		const float AppliedDelta = Sensation->CurrentValue - OldValue;
		if (bBroadcast && !FMath::IsNearlyZero(AppliedDelta))
		{
			BroadcastValueChanged(Sensation->SensationName, OldValue, Sensation->CurrentValue, Sensation->MaxValue, true);
		}
		return AppliedDelta;
	}

	return 0.f;
}

void UProjectSurvivalNeedsComponent::ApplyConsumableEffect(const FProjectSurvivalConsumableEffect& Effect)
{
	for (const FProjectSurvivalNeedDelta& Delta : Effect.NeedDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.DeltaValue))
		{
			continue;
		}

		ApplyNeedDeltaValue(Delta.EntryName, Delta.DeltaValue, true, Effect.bClampToRange);
	}

	for (const FProjectSurvivalNeedDelta& Delta : Effect.SensationDeltas)
	{
		if (Delta.EntryName.IsNone() || FMath::IsNearlyZero(Delta.DeltaValue))
		{
			continue;
		}

		ApplySensationDeltaValue(Delta.EntryName, Delta.DeltaValue, true, Effect.bClampToRange);
	}

	UpdateCachedPenaltyMultiplier(true);
}

TArray<FProjectSurvivalNeedSnapshot> UProjectSurvivalNeedsComponent::BuildNeedSnapshots() const
{
	TArray<FProjectSurvivalNeedSnapshot> Snapshots;
	Snapshots.Reserve(Needs.Num());
	for (const FProjectSurvivalNeedState& Need : Needs)
	{
		FProjectSurvivalNeedSnapshot Snapshot;
		Snapshot.NeedName = Need.NeedName;
		Snapshot.CurrentValue = Need.CurrentValue;
		Snapshot.MaxValue = Need.MaxValue;
		Snapshot.NormalizedValue = GetNeedNormalizedValue(Need.NeedName);
		Snapshot.MissingPercent = 1.f - Snapshot.NormalizedValue;
		Snapshot.FilledBars = GetNeedBarsFilled(Need.NeedName);
		Snapshot.TotalBars = FMath::Max(1, BarsPerNeed);
		Snapshot.DecayPerSecond = Need.DecayPerSecond;
		Snapshot.RecoveryPerSecond = Need.RecoveryPerSecond;
		Snapshots.Add(Snapshot);
	}

	return Snapshots;
}

TArray<FProjectSurvivalSensationSnapshot> UProjectSurvivalNeedsComponent::BuildSensationSnapshots() const
{
	TArray<FProjectSurvivalSensationSnapshot> Snapshots;
	Snapshots.Reserve(Sensations.Num());
	for (const FProjectSurvivalSensationState& Sensation : Sensations)
	{
		FProjectSurvivalSensationSnapshot Snapshot;
		Snapshot.SensationName = Sensation.SensationName;
		Snapshot.CurrentValue = Sensation.CurrentValue;
		Snapshot.MaxValue = Sensation.MaxValue;
		const float MaxValue = FMath::Max(Sensation.MaxValue, 0.001f);
		Snapshot.NormalizedValue = FMath::Clamp(Sensation.CurrentValue / MaxValue, 0.f, 1.f);
		const int32 SafeBarCount = FMath::Max(1, BarsPerNeed);
		Snapshot.FilledBars = FMath::Clamp(FMath::FloorToInt(Snapshot.NormalizedValue * static_cast<float>(SafeBarCount) + KINDA_SMALL_NUMBER), 0, SafeBarCount);
		Snapshot.TotalBars = SafeBarCount;
		Snapshot.PassiveDeltaPerSecond = Sensation.PassiveDeltaPerSecond;
		Snapshots.Add(Snapshot);
	}

	return Snapshots;
}

TArray<FProjectSurvivalAttributeProjection> UProjectSurvivalNeedsComponent::BuildAttributeProjectionsFromCombatComponent(const UProjectCombatAttributeComponent* CombatComponent) const
{
	TArray<FProjectSurvivalAttributeProjection> Projections;
	if (!CombatComponent)
	{
		return Projections;
	}

	TArray<FName> AttributesToProject = AffectedSecondaryAttributes;
	if (AttributesToProject.Num() == 0)
	{
		AttributesToProject.Add(TEXT("MeleeDamage"));
		AttributesToProject.Add(TEXT("RangedDamage"));
		AttributesToProject.Add(TEXT("SpellDamage"));
		AttributesToProject.Add(TEXT("PhysicalDefense"));
		AttributesToProject.Add(TEXT("SpellDefense"));
		AttributesToProject.Add(TEXT("CritChance"));
	}

	Projections.Reserve(AttributesToProject.Num());
	for (const FName AttributeName : AttributesToProject)
	{
		FProjectSurvivalAttributeProjection Projection;
		Projection.AttributeName = AttributeName;
		Projection.BaseValue = CombatComponent->GetAttributeCurrentValue(AttributeName);
		Projection.PenaltyMultiplier = GetNeedsPenaltyMultiplier();
		Projection.EffectiveValue = Projection.BaseValue * Projection.PenaltyMultiplier;
		Projection.PenaltyPercent = 1.f - Projection.PenaltyMultiplier;
		Projections.Add(Projection);
	}

	return Projections;
}

bool UProjectSurvivalNeedsComponent::HasAnyRegisteredNeeds() const
{
	return Needs.Num() > 0 || Sensations.Num() > 0;
}

void UProjectSurvivalNeedsComponent::ApplySettingsDefaults()
{
	const UProjectSurvivalNeedsSettings* Settings = UProjectSurvivalNeedsSettings::Get();
	if (!Settings)
	{
		BarsPerNeed = 10;
		PenaltyPerNeedAtZero = 0.25f;
		bEnableAutoDecay = true;
		bClampToRangeByDefault = true;
		NeedsDecayMultiplier = 1.f;
		InitializeDefaultState();
		return;
	}

	BarsPerNeed = Settings->BarsPerNeed;
	PenaltyPerNeedAtZero = Settings->PenaltyPerNeedAtZero;
	bEnableAutoDecay = Settings->bEnableAutoDecay;
	bClampToRangeByDefault = Settings->bClampToRangeByDefault;
	NeedsDecayMultiplier = Settings->DebugNeedsDecayMultiplier;
	InitializeDefaultState();
}

void UProjectSurvivalNeedsComponent::InitializeDefaultState()
{
	const UProjectSurvivalNeedsSettings* Settings = UProjectSurvivalNeedsSettings::Get();
	if (!Settings)
	{
		Needs = {
			FProjectSurvivalNeedState(HungerName, 100.f, 100.f, 0.16f, 0.f),
			FProjectSurvivalNeedState(ThirstName, 100.f, 100.f, 0.30f, 0.f),
			FProjectSurvivalNeedState(SleepName, 100.f, 100.f, 0.105f, 0.f),
		};

		Sensations = {
			FProjectSurvivalSensationState(MadnessName, 0.f, 100.f),
			FProjectSurvivalSensationState(PainName, 0.f, 100.f),
			FProjectSurvivalSensationState(CurseName, 0.f, 100.f),
			FProjectSurvivalSensationState(TEXT("Alcohol"), 0.f, 100.f, -0.25f),
		};

		AffectedSecondaryAttributes = {
			TEXT("MeleeDamage"),
			TEXT("RangedDamage"),
			TEXT("SpellDamage"),
			TEXT("PhysicalDefense"),
			TEXT("SpellDefense"),
			TEXT("CritChance"),
		};
		return;
	}

	Needs = Settings->DefaultNeeds;
	Sensations = Settings->DefaultSensations;
	AffectedSecondaryAttributes = Settings->AffectedSecondaryAttributes;
}

void UProjectSurvivalNeedsComponent::SanitizeState()
{
	NeedsDecayMultiplier = FMath::Max(0.f, NeedsDecayMultiplier);

	TSet<FName> SeenNeeds;
	for (FProjectSurvivalNeedState& Need : Needs)
	{
		if (Need.NeedName.IsNone() || SeenNeeds.Contains(Need.NeedName))
		{
			Need.NeedName = NAME_None;
		}
		else
		{
			SeenNeeds.Add(Need.NeedName);
		}

		Need.MaxValue = FMath::Max(Need.MaxValue, 0.001f);
		Need.CurrentValue = ClampToEntryRange(Need.CurrentValue, 0.f, Need.MaxValue);
		Need.DecayPerSecond = FMath::Max(0.f, Need.DecayPerSecond);
		Need.RecoveryPerSecond = FMath::Max(0.f, Need.RecoveryPerSecond);
	}

	TSet<FName> SeenSensations;
	for (FProjectSurvivalSensationState& Sensation : Sensations)
	{
		if (Sensation.SensationName.IsNone() || SeenSensations.Contains(Sensation.SensationName))
		{
			Sensation.SensationName = NAME_None;
		}
		else
		{
			SeenSensations.Add(Sensation.SensationName);
		}

		Sensation.MaxValue = FMath::Max(Sensation.MaxValue, 0.001f);
		Sensation.CurrentValue = ClampToEntryRange(Sensation.CurrentValue, 0.f, Sensation.MaxValue);
		if (!FMath::IsFinite(Sensation.PassiveDeltaPerSecond))
		{
			Sensation.PassiveDeltaPerSecond = 0.f;
		}
	}
}

float UProjectSurvivalNeedsComponent::ComputePenaltyMultiplier() const
{
	if (Needs.Num() == 0)
	{
		return 1.f;
	}

	float TotalPenalty = 0.f;
	for (const FProjectSurvivalNeedState& Need : Needs)
	{
		if (Need.NeedName.IsNone())
		{
			continue;
		}

		const float MaxValue = FMath::Max(Need.MaxValue, 0.001f);
		const float MissingPercent = 1.f - FMath::Clamp(Need.CurrentValue / MaxValue, 0.f, 1.f);
		TotalPenalty += MissingPercent * PenaltyPerNeedAtZero;
	}

	return FMath::Clamp(1.f - TotalPenalty, 0.25f, 1.f);
}

float UProjectSurvivalNeedsComponent::ClampToEntryRange(float Value, float MinValue, float MaxValue) const
{
	return FMath::Clamp(Value, MinValue, FMath::Max(MaxValue, MinValue));
}

FProjectSurvivalNeedState* UProjectSurvivalNeedsComponent::FindNeedMutable(FName NeedName)
{
	return Needs.FindByPredicate([NeedName](const FProjectSurvivalNeedState& Need)
	{
		return Need.NeedName == NeedName;
	});
}

const FProjectSurvivalNeedState* UProjectSurvivalNeedsComponent::FindNeed(FName NeedName) const
{
	return Needs.FindByPredicate([NeedName](const FProjectSurvivalNeedState& Need)
	{
		return Need.NeedName == NeedName;
	});
}

FProjectSurvivalSensationState* UProjectSurvivalNeedsComponent::FindSensationMutable(FName SensationName)
{
	return Sensations.FindByPredicate([SensationName](const FProjectSurvivalSensationState& Sensation)
	{
		return Sensation.SensationName == SensationName;
	});
}

const FProjectSurvivalSensationState* UProjectSurvivalNeedsComponent::FindSensation(FName SensationName) const
{
	return Sensations.FindByPredicate([SensationName](const FProjectSurvivalSensationState& Sensation)
	{
		return Sensation.SensationName == SensationName;
	});
}

void UProjectSurvivalNeedsComponent::UpdateCachedPenaltyMultiplier(bool bBroadcastChange)
{
	const float OldPenaltyMultiplier = CachedPenaltyMultiplier;
	CachedPenaltyMultiplier = ComputePenaltyMultiplier();
	if (bBroadcastChange && !FMath::IsNearlyEqual(OldPenaltyMultiplier, CachedPenaltyMultiplier))
	{
		OnPenaltyMultiplierChanged.Broadcast(OldPenaltyMultiplier, CachedPenaltyMultiplier);
	}
}

void UProjectSurvivalNeedsComponent::BroadcastValueChanged(FName EntryName, float OldValue, float NewValue, float MaxValue, bool bIsSensation)
{
	OnSurvivalValueChanged.Broadcast(EntryName, OldValue, NewValue, MaxValue, bIsSensation);
}
