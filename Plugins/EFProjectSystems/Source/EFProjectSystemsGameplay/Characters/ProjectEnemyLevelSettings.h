#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectEnemyLevelSettings.generated.h"

class APawn;

USTRUCT()
struct FProjectEnemyMapLevelRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Map")
	FString MapNamePattern;

	UPROPERTY(EditAnywhere, Config, Category = "Map", meta = (ClampMin = "1", UIMin = "1"))
	int32 WorldTier = 1;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Enemy Level"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyLevelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectEnemyLevelSettings();

	static const UProjectEnemyLevelSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Targets", meta = (AllowedClasses = "/Script/Engine.Pawn"))
	TArray<TSoftClassPtr<APawn>> TargetEnemyBaseClasses;

	UPROPERTY(EditAnywhere, Config, Category = "Map", meta = (TitleProperty = "MapNamePattern"))
	TArray<FProjectEnemyMapLevelRule> MapLevelRules;

	UPROPERTY(EditAnywhere, Config, Category = "Map", meta = (ClampMin = "1", UIMin = "1"))
	int32 DefaultWorldTier = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "1", UIMin = "1"))
	int32 LevelRangeWidth = 5;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution")
	TArray<int32> LevelOffsetWeights;

	UPROPERTY(EditAnywhere, Config, Category = "Scaling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthPerLevel = 0.12f;

	UPROPERTY(EditAnywhere, Config, Category = "Scaling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DamagePerLevel = 0.08f;

	UPROPERTY(EditAnywhere, Config, Category = "Scaling", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefensePerLevel = 0.06f;

	UPROPERTY(EditAnywhere, Config, Category = "Ascent")
	bool bSyncAssignedLevelToAscentLevelingComponent = false;

	UPROPERTY(EditAnywhere, Config, Category = "Ascent")
	bool bReinitializeAscentStatisticsOnLevelSync = false;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs", meta = (ClampMin = "2", UIMin = "2"))
	int32 MorphBiasMaxLevel = 25;

	UPROPERTY(EditAnywhere, Config, Category = "Targeting")
	TArray<FName> PreferredTargetPointSockets;

	UPROPERTY(EditAnywhere, Config, Category = "Targeting", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float FallbackTargetHeightRatio = 0.65f;

	UPROPERTY(EditAnywhere, Config, Category = "Targeting", meta = (ClampMin = "0", UIMin = "0"))
	int32 TargetLevelWidgetZOrder = 1000;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "0", UIMin = "0"))
	int32 InitializationRetryCount = 4;
};
