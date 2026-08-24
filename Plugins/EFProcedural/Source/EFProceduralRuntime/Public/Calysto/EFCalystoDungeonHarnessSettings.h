#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFCalystoDungeonHarnessSettings.generated.h"

class AActor;
class UEFCalystoDungeonDirectorPolicyV4;
class UStaticMesh;
class UWorld;

/** Project-owned integration paths. Generation policy lives exclusively in the V4 Primary Data Asset. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Calysto Dungeon Director V4"))
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonHarnessSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFCalystoDungeonHarnessSettings();

	static const UEFCalystoDungeonHarnessSettings* Get();
	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftObjectPtr<UWorld> DungeonMap;

	/** Sole V4 authoring authority. Missing or invalid policy fails generation closed. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets|Policy V4")
	TSoftObjectPtr<UEFCalystoDungeonDirectorPolicyV4> DirectorPolicy;

	/** Native Calysto DA_DungeonMesh source; the adapter duplicates it transiently. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftObjectPtr<UObject> DungeonMeshDataAsset;

	/** Exact vendor source cloned transiently for candidate anchors; never modified or saved. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftObjectPtr<UObject> SpawnerDataAsset;

	/** Exact vendor source cloned transiently for probabilistic room themes; never modified or saved. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftObjectPtr<UObject> RoomThemeDataAsset;

	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftClassPtr<AActor> DungeonFloorDoorClass;

	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftObjectPtr<UStaticMesh> DungeonFloorDoorMesh;

	/** Project-owned lightweight actor used only as a deterministic candidate marker. */
	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	TSoftClassPtr<AActor> PopulationAnchorClass;

	/** Exists only while async preload/OpenLevel is pending; it is not a permanent tick. */
	UPROPERTY(EditAnywhere, Config, Category = "Travel", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float TravelWatchdogSeconds = 30.0f;
};
