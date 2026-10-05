#include "Survival/ProjectHygieneMath.h"

FProjectHygieneExposure FProjectHygieneMath::Evaluate(TConstArrayView<FDirtyPawnHygieneRegion> Regions,
    const FProjectHygieneTuning& T, double Baseline, double Effort)
{
    FProjectHygieneExposure Out;
    double Coverage=0, Severity=0, Slow=0;
    for(const auto& R:Regions)
    {
        const double C=FMath::Max3(R.Mud,R.Sand,R.Snow);
        Coverage+=R.Weight*C;
        Severity+=R.Weight*FMath::Max3(FMath::Max(0.,T.Mud)*R.Mud,FMath::Max(0.,T.Sand)*R.Sand,FMath::Max(0.,T.Snow)*R.Snow);
        // Dominant global stain, and no companion dirt underneath mud/sand.
        Slow+=R.Weight*FMath::Max3(FMath::Max(0.,T.Dirt)*R.Dirt*(1-FMath::Max(R.Mud,R.Sand)),
            FMath::Max(0.,T.Blood)*R.Blood,FMath::Max(0.,T.Burn)*R.Burn);
        Out.Contamination+=R.Weight*FMath::Max3(R.Mud,R.Dirt,R.Blood);
        Out.CleanWaterCoverage+=R.Weight*R.CleanWater;
    }
    Out.LossPerHour=FMath::Clamp(FMath::Max(0.,Baseline)+Coverage*Severity+Slow+FMath::Max(0.,Effort),0.,FMath::Max(0.,T.ContinuousLossLimit));
    Out.CleanWaterCoverage=FMath::Clamp(Out.CleanWaterCoverage,0.,1.);
    Out.Contamination=FMath::Clamp(Out.Contamination,0.,1.);
    return Out;
}

double FProjectHygieneMath::Advance(double Hygiene,double Hours,const FProjectHygieneExposure& E,const FProjectHygieneTuning& T)
{
    Hours=FMath::Max(0.,Hours);
    const double Net=FMath::Max(0.,T.WaterRecoveryPerHour)*E.CleanWaterCoverage-E.LossPerHour;
    const double Ceiling=FMath::Clamp(T.WaterRecoveryCeiling,0.,100.);
    // Above the water ceiling, degrade naturally until reaching it; never snap down to it.
    if(Net>0 && Hygiene>Ceiling) return FMath::Clamp(FMath::Max(Ceiling,Hygiene-E.LossPerHour*Hours),0.,100.);
    return FMath::Clamp(Net>0?FMath::Min(Ceiling,Hygiene+Net*Hours):Hygiene+Net*Hours,0.,100.);
}
