#pragma once
#include "CoreMinimal.h"
#include "DirtyPawnHygiene.h"
#include "ProjectHygieneMath.generated.h"

USTRUCT(BlueprintType)
struct FProjectHygieneTuning
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Mud=11.5;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Sand=5.25;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Snow=.5;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Dirt=1;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Blood=2;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double Burn=.25;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double ContinuousLossLimit=12.5;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0")) double WaterRecoveryPerHour=30;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0", ClampMax="100")) double WaterRecoveryCeiling=85;
};

struct FProjectHygieneExposure
{
    double LossPerHour=0, CleanWaterCoverage=0, Contamination=0;
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectHygieneMath
{
    static FProjectHygieneExposure Evaluate(TConstArrayView<FDirtyPawnHygieneRegion> Regions,
        const FProjectHygieneTuning& T, double Baseline, double Effort);
    static double Advance(double Hygiene, double Hours, const FProjectHygieneExposure& Exposure,
        const FProjectHygieneTuning& T);
};
