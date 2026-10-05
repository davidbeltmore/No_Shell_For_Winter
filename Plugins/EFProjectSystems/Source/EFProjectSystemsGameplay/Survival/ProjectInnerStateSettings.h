#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Survival/ProjectInnerStateMath.h"
#include "ProjectInnerStateSettings.generated.h"

/** Slow rates are per calendar hour; acute rates/delays are per simulation second. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Project Inner State"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetCategoryName() const override { return TEXT("EF Project Systems"); }
    UPROPERTY(EditAnywhere, Config, Category="Poison") FProjectPoisonTuning Poison;
    UPROPERTY(EditAnywhere, Config, Category="Runtime", meta=(ClampMin="0.02", ClampMax="1")) double AcuteStepSeconds = 0.1;
    UPROPERTY(EditAnywhere, Config, Category="Runtime", meta=(ClampMin="0.1")) double SlowStepSeconds = 1.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress") double StressRiseHalfLifeHours = 2.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress") double StressRecoveryHalfLifeHours = 3.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress") double StressSleepHalfLifeHours = 1.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress") double RepetitiveGraceHours = 4.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing") double WellbeingHalfLifeHours = 48.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing") double LonelyGraceHours = 72.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing") double LonelyRampHours = 72.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing") double DepressedEntryHours = 48.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing") double DepressedExitHours = 24.0;
    UPROPERTY(EditAnywhere, Config, Category="Hygiene") double HygieneLossPerHour = 1.0;
    UPROPERTY(EditAnywhere, Config, Category="Hygiene") double ExtraHygieneLossPerDay = 12.0;
    UPROPERTY(EditAnywhere, Config, Category="Hygiene") double ContaminationHazardPerHour = 0.005;
    UPROPERTY(EditAnywhere, Config, Category="Bathroom") double BathroomDeadlineHours = 48.0;
    UPROPERTY(EditAnywhere, Config, Category="Bathroom") double AccidentHygieneLoss = 35.0;
    UPROPERTY(EditAnywhere, Config, Category="Sickness") double InitialSickness = 25.0;
    UPROPERTY(EditAnywhere, Config, Category="Sickness") double SicknessGainPerHour = 2.0;
    UPROPERTY(EditAnywhere, Config, Category="Sickness") double TreatedSicknessRecoveryPerHour = 6.0;
    UPROPERTY(EditAnywhere, Config, Category="Sickness") double CriticalSicknessHealthFractionPerSecond = 0.001;
    UPROPERTY(EditAnywhere, Config, Category="Sickness") double CriticalSicknessAwarenessPerSecond = 0.1;
    UPROPERTY(EditAnywhere, Config, Category="Air") double AirReserveSeconds = 45.0;
    UPROPERTY(EditAnywhere, Config, Category="Air") double AirReserveExertionSeconds = 30.0;
    UPROPERTY(EditAnywhere, Config, Category="Air") double AirRecoveryPerSecond = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Air") double AsphyxiaHealthFractionPerSecond = 0.01;
    UPROPERTY(EditAnywhere, Config, Category="Air") double AsphyxiaAwarenessPerSecond = 5.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double AwarenessRecoveryPerSecond = 4.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double AwarenessCapacityFallPerSecond = 2.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double HitRecoveryDelaySeconds = 5.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double WakeThreshold = 30.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double MinimumUnconsciousSeconds = 10.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double ExtremePainLossPerSecond = 2.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness") double ExtremePainGraceSeconds = 5.0;
    UPROPERTY(EditAnywhere, Config, Category="Composure") double FearHalfLifeSeconds = 3.0;
    UPROPERTY(EditAnywhere, Config, Category="Composure") double ConfidenceHalfLifeSeconds = 20.0;
};
