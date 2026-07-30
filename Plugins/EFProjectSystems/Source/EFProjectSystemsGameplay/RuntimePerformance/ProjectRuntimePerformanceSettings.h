#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectRuntimePerformanceSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Runtime Performance"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRuntimePerformanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectRuntimePerformanceSettings();

	static const UProjectRuntimePerformanceSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	FSoftObjectPath DungeonCombatMap;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "1.0"))
	float DefaultDurationSeconds = 180.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "1.0"))
	float RealGameplayDurationSeconds = 300.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Full Stack Overload", meta = (ClampMin = "1.0"))
	float FullStackOverloadDurationSeconds = 540.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Full Stack Overload", meta = (ClampMin = "0"))
	int32 FullStackOverloadEnemyCap = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Full Stack Overload")
	bool bStrictFullStackScenarioFailures = true;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0.0"))
	float WarmupSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0"))
	int32 EnemyCount = 4;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	bool bUseVanillaAIPerceptionForBenchmarks = true;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	bool bAllowBenchmarkDirectAITargeting = false;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "100.0"))
	float EnemySpawnRadius = 700.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0.0"))
	float EnemySpawnHeightOffset = 120.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0.0"))
	float PreparationTimeoutSeconds = 90.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	FString OutputRelativePath = TEXT("Automation/Performance");

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	bool bAutoStartFromCommandLine = true;
};
