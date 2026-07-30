#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectPerformanceBudgetSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Performance Budget"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPerformanceBudgetSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectPerformanceBudgetSettings();

	static const UProjectPerformanceBudgetSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	bool bEnableRuntimeBudgeting = false;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	bool bPreloadRuntimeCombatAssets = false;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "0.1"))
	float BudgetUpdateIntervalSeconds = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	TArray<FSoftObjectPath> AdditionalPreloadAssets;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "1"))
	int32 MaxFullRateEnemyAnimations = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "1"))
	int32 MaxAwakeDungeonEnemies = 20;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "1"))
	int32 MaxRuntimeEnemyActors = 32;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "1"))
	int32 MaxMidRateEnemyAnimations = 8;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "100.0"))
	float NearEnemyDistance = 1800.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "100.0"))
	float MidEnemyDistance = 3500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "100.0"))
	float FarEnemyDistance = 6000.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0"))
	float EnemyMeshCullDistance = 7500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0"))
	float MidEnemyTickInterval = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0"))
	float FarEnemyTickInterval = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0"))
	float FarVisualTickInterval = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0"))
	float DormantEnemyTickInterval = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies")
	bool bApplyEnemyActorTickBudget = false;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies")
	bool bApplyEnemyAnimationBudget = false;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies")
	bool bApplyEnemyMovementTickBudget = false;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies")
	bool bCullExcessRuntimeEnemies = false;

	UPROPERTY(EditAnywhere, Config, Category = "VFX")
	bool bApplyWorldVfxBudget = false;

	UPROPERTY(EditAnywhere, Config, Category = "VFX", meta = (ClampMin = "0.0"))
	float NiagaraCullDistance = 5000.0f;

	UPROPERTY(EditAnywhere, Config, Category = "VFX", meta = (ClampMin = "0.0"))
	float NiagaraFarTickInterval = 0.25f;
};
