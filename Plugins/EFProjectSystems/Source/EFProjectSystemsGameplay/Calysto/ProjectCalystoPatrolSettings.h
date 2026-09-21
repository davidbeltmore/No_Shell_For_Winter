#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectCalystoPatrolSettings.generated.h"

/** Runtime policy for project-owned Calysto enemy patrols. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Calysto Patrol"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCalystoPatrolSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectCalystoPatrolSettings();

	static const UProjectCalystoPatrolSettings* Get();
	virtual FName GetCategoryName() const override;

	/** Enables patrol only for Calysto V6 population enemies in the configured dungeon map. */
	UPROPERTY(EditAnywhere, Config, Category = "Patrol")
	bool bEnableDungeonEnemyPatrol = true;

	/** A PIE-safe substring matched against the active dungeon map name. */
	UPROPERTY(EditAnywhere, Config, Category = "Patrol")
	FString DungeonMapNamePattern = TEXT("DungeonGeneration");

	/** Radius, in centimetres, for an enemy's reachable random patrol destination. */
	UPROPERTY(EditAnywhere, Config, Category = "Patrol", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RandomPatrolRadius = 1500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Patrol", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WaitTimeAtPointSeconds = 2.0f;

	/** How often the subsystem discovers newly materialized population enemies. */
	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float ScanIntervalSeconds = 0.25f;

	/** Delay before retrying an enemy whose controller, movement, or matching NavData is not ready. */
	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float RetryIntervalSeconds = 1.0f;

	/** Recover a patrol loop only after this long without meaningful position progress. */
	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float PatrolStallRecoverySeconds = 12.0f;

	/** Minimum two-dimensional displacement that resets the patrol-stall timer. */
	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float PatrolProgressDistance = 50.0f;

	/**
	 * Calysto can expose more than one runtime NavData. Disable ACF's Crowd follower only for
	 * transient tagged dungeon enemies so each request follows the NavData selected for its pawn.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	bool bDisableCrowdSimulationForDungeonPatrol = true;

	/** Local extents used only to verify that the enemy's own nav agent can project to its NavData. */
	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	FVector NavigationProjectionExtent = FVector(200.0f, 200.0f, 500.0f);
};
