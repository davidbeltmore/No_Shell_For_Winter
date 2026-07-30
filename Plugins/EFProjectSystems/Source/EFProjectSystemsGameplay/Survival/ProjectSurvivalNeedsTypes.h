#pragma once

#include "CoreMinimal.h"
#include "ProjectSurvivalNeedsTypes.generated.h"

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalNeedState
{
	GENERATED_BODY()

	FProjectSurvivalNeedState()
		: NeedName(NAME_None)
		, CurrentValue(100.f)
		, MaxValue(100.f)
		, DecayPerSecond(0.f)
		, RecoveryPerSecond(0.f)
	{
	}

	FProjectSurvivalNeedState(FName InNeedName, float InCurrentValue, float InMaxValue, float InDecayPerSecond, float InRecoveryPerSecond)
		: NeedName(InNeedName)
		, CurrentValue(InCurrentValue)
		, MaxValue(InMaxValue)
		, DecayPerSecond(InDecayPerSecond)
		, RecoveryPerSecond(InRecoveryPerSecond)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName NeedName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DecayPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float RecoveryPerSecond;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalSensationState
{
	GENERATED_BODY()

	FProjectSurvivalSensationState()
		: SensationName(NAME_None)
		, CurrentValue(0.f)
		, MaxValue(100.f)
	{
	}

	FProjectSurvivalSensationState(FName InSensationName, float InCurrentValue, float InMaxValue)
		: SensationName(InSensationName)
		, CurrentValue(InCurrentValue)
		, MaxValue(InMaxValue)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SensationName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MaxValue;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalNeedDelta
{
	GENERATED_BODY()

	FProjectSurvivalNeedDelta()
		: EntryName(NAME_None)
		, DeltaValue(0.f)
	{
	}

	FProjectSurvivalNeedDelta(FName InEntryName, float InDeltaValue)
		: EntryName(InEntryName)
		, DeltaValue(InDeltaValue)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName EntryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DeltaValue;
};

UENUM(BlueprintType)
enum class EProjectSurvivalConsumableStackPolicy : uint8
{
	RefreshDuration UMETA(DisplayName = "Refresh Duration"),
	Stack UMETA(DisplayName = "Stack"),
	IgnoreIfActive UMETA(DisplayName = "Ignore If Active"),
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalTimedNeedDelta
{
	GENERATED_BODY()

	FProjectSurvivalTimedNeedDelta()
		: EntryName(NAME_None)
		, TotalDelta(0.f)
		, DurationSeconds(0.f)
		, TickIntervalSeconds(0.f)
	{
	}

	FProjectSurvivalTimedNeedDelta(FName InEntryName, float InTotalDelta, float InDurationSeconds, float InTickIntervalSeconds)
		: EntryName(InEntryName)
		, TotalDelta(InTotalDelta)
		, DurationSeconds(InDurationSeconds)
		, TickIntervalSeconds(InTickIntervalSeconds)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName EntryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float TotalDelta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0"))
	float DurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0"))
	float TickIntervalSeconds;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalTimedSensationDelta
{
	GENERATED_BODY()

	FProjectSurvivalTimedSensationDelta()
		: EntryName(NAME_None)
		, TotalDelta(0.f)
		, DurationSeconds(0.f)
		, TickIntervalSeconds(0.f)
	{
	}

	FProjectSurvivalTimedSensationDelta(FName InEntryName, float InTotalDelta, float InDurationSeconds, float InTickIntervalSeconds)
		: EntryName(InEntryName)
		, TotalDelta(InTotalDelta)
		, DurationSeconds(InDurationSeconds)
		, TickIntervalSeconds(InTickIntervalSeconds)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName EntryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float TotalDelta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0"))
	float DurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0"))
	float TickIntervalSeconds;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalConsumableProfile
{
	GENERATED_BODY()

	FProjectSurvivalConsumableProfile()
		: SourceId(NAME_None)
		, bClampToRange(true)
		, StackPolicy(EProjectSurvivalConsumableStackPolicy::RefreshDuration)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	TArray<FProjectSurvivalNeedDelta> NeedDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	TArray<FProjectSurvivalNeedDelta> SensationDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	TArray<FProjectSurvivalTimedNeedDelta> TimedNeedDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	TArray<FProjectSurvivalTimedSensationDelta> TimedSensationDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	bool bClampToRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	EProjectSurvivalConsumableStackPolicy StackPolicy;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalNeedSnapshot
{
	GENERATED_BODY()

	FProjectSurvivalNeedSnapshot()
		: NeedName(NAME_None)
		, CurrentValue(0.f)
		, MaxValue(0.f)
		, NormalizedValue(0.f)
		, MissingPercent(1.f)
		, FilledBars(0)
		, TotalBars(10)
		, DecayPerSecond(0.f)
		, RecoveryPerSecond(0.f)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName NeedName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float NormalizedValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MissingPercent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	int32 FilledBars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	int32 TotalBars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DecayPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float RecoveryPerSecond;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalSensationSnapshot
{
	GENERATED_BODY()

	FProjectSurvivalSensationSnapshot()
		: SensationName(NAME_None)
		, CurrentValue(0.f)
		, MaxValue(100.f)
		, NormalizedValue(0.f)
		, FilledBars(0)
		, TotalBars(10)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SensationName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float NormalizedValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	int32 FilledBars;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	int32 TotalBars;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalAttributeProjection
{
	GENERATED_BODY()

	FProjectSurvivalAttributeProjection()
		: AttributeName(NAME_None)
		, BaseValue(0.f)
		, PenaltyMultiplier(1.f)
		, EffectiveValue(0.f)
		, PenaltyPercent(0.f)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float BaseValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float PenaltyMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float EffectiveValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float PenaltyPercent;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalConsumableEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalNeedDelta> NeedDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalNeedDelta> SensationDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bClampToRange;

	FProjectSurvivalConsumableEffect()
		: SourceId(NAME_None)
		, bClampToRange(true)
	{
	}
};
