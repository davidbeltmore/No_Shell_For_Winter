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

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float SmokeWarmupSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float AcceptanceWarmupSeconds = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float NaturalGameplayWarmupSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float SmokeDurationSeconds = 30.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "30.0", ClampMax = "240.0"))
	float AcceptanceDurationSeconds = 120.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "30.0", ClampMax = "240.0"))
	float DiagnosticDurationSeconds = 120.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "30.0", ClampMax = "120.0"))
	float NaturalGameplayDurationSeconds = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite")
	int32 AcceptanceSeed = 42;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite", meta = (ClampMin = "8", ClampMax = "8"))
	int32 AcceptanceEnemyCount = 8;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Suite")
	FName DefaultQualityPreset = TEXT("Balanced58");

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Acceptance", meta = (ClampMin = "1.0"))
	float MinimumAverageFps = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Acceptance", meta = (ClampMin = "1.0"))
	float MinimumMedianFps = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Acceptance", meta = (ClampMin = "1.0"))
	float MinimumOnePercentLowFps = 55.0f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Acceptance", meta = (ClampMin = "1.0"))
	float MaximumP99FrameMs = 18.2f;

	UPROPERTY(EditAnywhere, Config, Category = "UE 5.8 Acceptance", meta = (ClampMin = "0"))
	int32 MaximumHitchesOver100Ms = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Full Stack Overload", meta = (ClampMin = "0"))
	int32 FullStackOverloadEnemyCap = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Full Stack Overload")
	bool bStrictFullStackScenarioFailures = true;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0.0"))
	float WarmupSeconds = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat", meta = (ClampMin = "0"))
	int32 EnemyCount = 8;

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
	FString OutputRelativePath = TEXT("Automation/Performance58");

	UPROPERTY(EditAnywhere, Config, Category = "Dungeon Combat")
	bool bAutoStartFromCommandLine = true;
};
