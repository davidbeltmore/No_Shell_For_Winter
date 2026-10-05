#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Survival/ProjectInnerStateMath.h"
#include "Survival/ProjectHygieneMath.h"
#include "ProjectInnerStateSettings.generated.h"

/** Slow rates are per calendar hour; acute rates/delays are per simulation second. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Project Inner State"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Config, Category="Care", meta=(ClampMin="0")) double SleepingRecoveryPerHour = 15.125;
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
    UPROPERTY(EditAnywhere, Config, Category="Hygiene") FProjectHygieneTuning SurfaceHygiene;
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
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double HungerStressWeight = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double ThirstStressWeight = 25.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double SleepStressWeight = 30.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double PainStressWeight = 35.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double SicknessStressWeight = 25.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double OverworkStressWeight = 15.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double HygieneStressWeight = 10.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double BathroomStressWeight = 10.0;
    UPROPERTY(EditAnywhere, Config, Category="Stress", meta=(ClampMin="0")) double TerrorStressWeight = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double StressWellbeingLossPerHour = 0.6;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double HygieneWellbeingLossPerHour = 0.2;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double LonelyWellbeingLossPerHour = 0.25;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double MadnessWellbeingLossPerHour = 0.25;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double SicknessWellbeingLossPerHour = 0.2;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double MaximumWellbeingRatePerHour = 1.5;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double ExerciseWellbeingPerHour = 0.25;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double SocialWellbeingPerHour = 0.25;
    UPROPERTY(EditAnywhere, Config, Category="Wellbeing", meta=(ClampMin="0")) double RestWellbeingPerHour = 0.2;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double PainCapacityWeight = 40.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double SleepCapacityWeight = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double SicknessCapacityWeight = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double LowHealthCapacityWeight = 30.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double AlcoholCapacityWeight = 40.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double ExertionCapacityWeight = 20.0;
    UPROPERTY(EditAnywhere, Config, Category="Awareness", meta=(ClampMin="0")) double MaximumCapacityLoss = 80.0;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double DepressionMadnessMultiplier = 1.25;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double ContentMadnessMultiplier = 0.9;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double SevereSleepMadnessPerHour = 0.5;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double SevereHygieneMadnessPerHour = 0.05;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double PleasureReturnPerHour = 10.0;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double StressStaminaPenalty = 0.1;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double ComposureStaminaBonus = 0.05;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double DepressionPracticePenalty = 0.1;
    UPROPERTY(EditAnywhere, Config, Category="Synergies", meta=(ClampMin="0")) double ContentPracticeBonus = 0.05;
};
