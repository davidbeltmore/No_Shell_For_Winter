#pragma once

#include "CoreMinimal.h"

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyScaledHealthValues
{
	float TargetMaxHealth = 0.0f;
	float TargetCurrentHealth = 0.0f;
	float AppliedHealthRatio = 1.0f;
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyScalingMath
{
	static float GetLevelMultiplier(int32 AssignedLevel, float MultiplierPerLevel);
	static float ScaleBaselineValue(float BaselineValue, int32 AssignedLevel, float MultiplierPerLevel);
	static float ComputeDeltaFromBaseline(float BaselineValue, int32 AssignedLevel, float MultiplierPerLevel);
	static FProjectEnemyScaledHealthValues ScaleHealthFromBaseline(
		float BaselineMaxHealth,
		float HealthRatio,
		int32 AssignedLevel,
		float MultiplierPerLevel);
};
