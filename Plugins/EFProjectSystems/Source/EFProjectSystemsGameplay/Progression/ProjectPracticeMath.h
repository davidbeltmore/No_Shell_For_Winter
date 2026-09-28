#pragma once

#include "CoreMinimal.h"
#include <cmath>

// Calendar units throughout. No wall-clock time or frame count enters progression.
namespace ProjectPracticeMath
{
    constexpr double MinimumEfficiency = 0.0001;

    inline double Efficiency(double Exposure, double HalfLife)
    {
        if (!FMath::IsFinite(Exposure) || !FMath::IsFinite(HalfLife) || HalfLife <= 0.0) return MinimumEfficiency;
        return MinimumEfficiency + (1.0 - MinimumEfficiency) * FMath::Exp2(-FMath::Max(0.0, Exposure) / HalfLife);
    }

    // Exact integral: splitting a session into frames or restarting it cannot improve rewards.
    inline double EffectiveHours(double Exposure, double Hours, double HalfLife)
    {
        if (!FMath::IsFinite(Hours) || Hours <= 0.0 || !FMath::IsFinite(HalfLife) || HalfLife <= 0.0) return 0.0;
        const double K = 0.6931471805599453 / HalfLife;
        const double Variable = FMath::Max(0.0, Efficiency(Exposure, HalfLife) - MinimumEfficiency);
        return MinimumEfficiency * Hours + Variable * (-std::expm1(-K * Hours)) / K;
    }

    // dExposure/dRest = -Rate/(1+Exposure/Scale). More abuse requires more recovery,
    // without a midnight reset or a punitive fixed lockout.
    inline double Recover(double Exposure, double Hours, double Rate, double Scale)
    {
        if (!FMath::IsFinite(Exposure) || Exposure <= 0.0) return 0.0;
        if (!FMath::IsFinite(Hours) || Hours <= 0.0 || Rate <= 0.0 || Scale <= 0.0) return Exposure;
        const double Remaining = FMath::Max(0.0, Exposure + Exposure * Exposure / (2.0 * Scale) - Rate * Hours);
        return 2.0 * Remaining / (1.0 + FMath::Sqrt(1.0 + 2.0 * Remaining / Scale));
    }

    inline double HoursToRecover(double Exposure, double Rate, double Scale)
    {
        return Rate > 0.0 && Scale > 0.0 ? (Exposure + Exposure * Exposure / (2.0 * Scale)) / Rate : 0.0;
    }
}
