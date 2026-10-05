#pragma once

#include "CoreMinimal.h"
#include "ProjectInnerStateMath.generated.h"

UENUM(BlueprintType)
enum class EProjectPoisonSpeed : uint8 { Fast, Normal, Slow };

/** A dose is a finite damage debt. Healing never changes ReferenceMaxHealth. */
USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPoisonDose
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) FName Toxin;
    UPROPERTY(SaveGame, BlueprintReadOnly) EProjectPoisonSpeed Speed = EProjectPoisonSpeed::Normal;
    UPROPERTY(SaveGame, BlueprintReadOnly) double RemainingPoints = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double ReferenceMaxHealth = 100.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) double ActivateAtSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPoisonTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double FastDelay = 3.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NormalDelay = 10.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double SlowDelay = 30.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double FastRate = 1.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double NormalRate = 0.5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double SlowRate = 0.25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double CombinedRateLimit = 1.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double ImmediateThreshold = 100.0;
    double Delay(EProjectPoisonSpeed Speed) const;
    double Rate(EProjectPoisonSpeed Speed) const;
};

/** Serialized simulation clock, not wall time. Advance returns damage due once;
 * immunity may block that damage, but may never put it back in the ledger. */
USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPoisonLedger
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) double ElapsedSeconds = 0.0;
    UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FProjectPoisonDose> Doses;
    // Bounded recent event replay guard; callers must issue a unique ID per real exposure.
    UPROPERTY(SaveGame) TArray<FGuid> RecentExposureIds;

    bool AddDose(FGuid ExposureId, FName Toxin, EProjectPoisonSpeed Speed,
        double Points, double MaxHealth, const FProjectPoisonTuning& Tuning);
    double Advance(double Seconds, const FProjectPoisonTuning& Tuning);
    double Neutralize(FName Toxin = NAME_None);
    double GetPendingPoints() const;
    double GetPendingDamage() const;
    bool HasActiveDose() const;
    void Sanitize();
};

/** World-free building blocks. All times have explicit units. */
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerStateMath
{
    /** Shared display category; publication must detect boundaries before integer rounding. */
    static int32 GetStimulationMoodBand(double Value) { return Value>20.0?1:Value<-20.0?-1:0; }
    static double ApproachHalfLife(double Current, double Target, double Elapsed, double HalfLife);
    static double IntegrateWellbeing(double Current, double NetPointsPerHour, double Hours, double HalfLifeHours);
    static double MoveTowards(double Current, double Target, double MaximumDelta);
    static double SeverityBelow(double Value, double Threshold);
    static double SeverityAbove(double Value, double Threshold, double Maximum = 100.0);
    static double ExposureProbability(double HazardPerHour, double Hours);
    static double AdvanceAwareness(double Current, double Capacity, double LossPerSecond,
        double RecoveryPerSecond, double CapacityFallPerSecond, double Seconds, bool bAllowRecovery);
};
