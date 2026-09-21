#pragma once

#include "CoreMinimal.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSystemManifest;

/** Idempotent upgrades applied directly to the one public Director table. */
struct FEFClothingV5DirectorPreparationResult
{
	bool bChanged = false;
	int32 MigratedLayerRowCount = 0;
	int32 MigratedLowerBodyGuardRowCount = 0;
};

/** Result of compiling the one stable authoring table into internal V5.1 assets. */
struct FEFClothingV5EditorSyncResult
{
	bool bSuccess = false;
	bool bChanged = false;
	int32 BodyProfileCount = 0;
	int32 GarmentDefinitionCount = 0;
	int32 StreamableBindingCount = 0;
	int32 CreatedAssetCount = 0;
	int32 UpdatedAssetCount = 0;
	int32 SavedAssetCount = 0;
	int32 SkippedDraftCount = 0;
	int32 MigratedAuthoringRowCount = 0;
	int32 MigratedLowerBodyGuardRowCount = 0;
	FString Report;

	/** Project-owned V5 registry produced by the sync. Never aliases the V4 input. */
	UEFClothingFitRegistry* Registry = nullptr;

	/** Internal version-neutral, soft-reference entry point produced by the sync. */
	UEFClothingSystemManifest* Manifest = nullptr;
};

/** Editor-only, deterministic single-table-to-V5.1 publication bridge. */
class FEFClothingV5EditorBridge final
{
public:
	/**
	 * Materializes single-table defaults before the V4 binding gate fingerprints
	 * rows. Repeated calls are inert and explicit authored guard values win.
	 */
	static FEFClothingV5DirectorPreparationResult PrepareDirectorDefaults(
		UEFClothingMorphDirectorPolicy* Director);

	/**
	 * Compiles complete enabled Director rows into internal V5.1 assets.
	 *
	 * V4Registry is strictly read-only. Director remains the only authoring
	 * source; the bridge may materialize legacy layer defaults and the explicit
	 * V5.1 lower-body guards into their original rows once. When bSaveAssets is
	 * false, changes remain dirty.
	 */
	static FEFClothingV5EditorSyncResult SyncFromV4(
		UEFClothingMorphDirectorPolicy* Director,
		UEFClothingFitRegistry* V4Registry,
		bool bSaveAssets);
};
