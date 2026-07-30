#include "Characters/ProjectEnemyScalingMath.h"

float FProjectEnemyScalingMath::GetLevelMultiplier(const int32 AssignedLevel, const float MultiplierPerLevel)
{
	const int32 SafeLevel = FMath::Max(AssignedLevel, 1);
	return 1.0f + (FMath::Max(MultiplierPerLevel, 0.0f) * static_cast<float>(SafeLevel - 1));
}

float FProjectEnemyScalingMath::ScaleBaselineValue(const float BaselineValue, const int32 AssignedLevel, const float MultiplierPerLevel)
{
	return BaselineValue * GetLevelMultiplier(AssignedLevel, MultiplierPerLevel);
}

float FProjectEnemyScalingMath::ComputeDeltaFromBaseline(const float BaselineValue, const int32 AssignedLevel, const float MultiplierPerLevel)
{
	return ScaleBaselineValue(BaselineValue, AssignedLevel, MultiplierPerLevel) - BaselineValue;
}

FProjectEnemyScaledHealthValues FProjectEnemyScalingMath::ScaleHealthFromBaseline(
	const float BaselineMaxHealth,
	const float HealthRatio,
	const int32 AssignedLevel,
	const float MultiplierPerLevel)
{
	FProjectEnemyScaledHealthValues Result;
	Result.AppliedHealthRatio = FMath::Clamp(HealthRatio, 0.0f, 1.0f);
	Result.TargetMaxHealth = ScaleBaselineValue(BaselineMaxHealth, AssignedLevel, MultiplierPerLevel);
	Result.TargetCurrentHealth = Result.TargetMaxHealth * Result.AppliedHealthRatio;
	return Result;
}
