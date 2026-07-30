#pragma once

#include "CoreMinimal.h"
#include "ProjectEnemyLevelLogic.generated.h"

class UProjectEnemyLevelSettings;

USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyLevelContext
{
	GENERATED_BODY()

	UPROPERTY()
	int32 WorldTier = 1;

	UPROPERTY()
	int32 MinLevel = 1;

	UPROPERTY()
	int32 MaxLevel = 1;
};

USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyLevelRollResult
{
	GENERATED_BODY()

	UPROPERTY()
	int32 WorldTier = 1;

	UPROPERTY()
	int32 MinRolledLevel = 1;

	UPROPERTY()
	int32 MaxRolledLevel = 1;

	UPROPERTY()
	int32 AssignedLevel = 1;

	UPROPERTY()
	int32 SelectedOffset = 0;

	UPROPERTY()
	float NormalizedLevel = 0.0f;
};

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyLevelLogic
{
public:
	static bool ResolveWorldTierForMapName(const FString& MapName, const UProjectEnemyLevelSettings& Settings, int32& OutWorldTier);

	static FProjectEnemyLevelContext BuildLevelContext(int32 WorldTier, const UProjectEnemyLevelSettings& Settings);

	static bool RollEnemyLevel(
		const FProjectEnemyLevelContext& Context,
		const UProjectEnemyLevelSettings& Settings,
		FProjectEnemyLevelRollResult& OutResult);

	static bool RollEnemyLevel(
		const FProjectEnemyLevelContext& Context,
		const UProjectEnemyLevelSettings& Settings,
		FRandomStream& RandomStream,
		FProjectEnemyLevelRollResult& OutResult);

	static float NormalizeEnemyLevel(int32 AssignedLevel, const UProjectEnemyLevelSettings& Settings);
};
