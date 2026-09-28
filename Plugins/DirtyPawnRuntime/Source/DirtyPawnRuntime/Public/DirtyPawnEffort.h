#pragma once
#include "CoreMinimal.h"

/** Linear exertion/recovery with exact integration, independent of tick subdivision. */
struct FDirtyPawnEffort
{
    static float Advance(float& Effort, float Intensity, float Seconds, float RiseSeconds, float RecoverySeconds, float Threshold)
    {
        if (!FMath::IsFinite(Seconds) || Seconds <= 0.f) return 0.f;
        Intensity = FMath::IsFinite(Intensity) ? FMath::Clamp(Intensity, 0.f, 1.f) : 0.f;
        Effort = FMath::IsFinite(Effort) ? FMath::Clamp(Effort, 0.f, 1.f) : 0.f;
        Threshold = FMath::IsFinite(Threshold) ? FMath::Clamp(Threshold, 0.f, .99f) : .25f;
        const float Duration = Intensity > Effort ? RiseSeconds : RecoverySeconds;
        const float Rate = 1.f / (FMath::IsFinite(Duration) ? FMath::Max(.01f, Duration) : 10.f);
        const float Start = Effort;
        const float Ramp = FMath::Min(Seconds, FMath::Abs(Intensity - Start) / Rate);
        Effort = FMath::FInterpConstantTo(Start, Intensity, Seconds, Rate);
        if (Intensity <= 0.f) return 0.f; // Rest recovers effort, never creates new sweat.
        const auto PositiveArea = [Threshold](float Value) { return FMath::Square(FMath::Max(0.f, Value - Threshold)) * .5f; };
        const float Area = FMath::Abs(Effort - Start) > SMALL_NUMBER
            ? Ramp * (PositiveArea(Effort) - PositiveArea(Start)) / (Effort - Start) : Ramp * FMath::Max(0.f, Start - Threshold);
        return (Area + (Seconds - Ramp) * FMath::Max(0.f, Effort - Threshold)) / (1.f - Threshold);
    }
};
