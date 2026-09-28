#pragma once
#include "CoreMinimal.h"

class AActor;

/** Bounded, effective-GAS bonuses; never modifies learned ranks or state thresholds. */
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectAttributeSynergies
{
    static float EffectiveRank(const AActor* Owner, FName Attribute);
    static float PainGainMultiplier(const AActor* Owner);
    static float SweatGainMultiplier(const AActor* Owner);
    static float PhysicalEffortIntensity(const AActor* Owner);
    static int32 IntelligenceLockpickBonus(const AActor* Owner);
    static float BoundedReduction(float Rank, float MaximumRank, float MaximumReduction);
};
