#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFClothingMorphV2Settings.generated.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UDataTable;
class UOptimusDeformer;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Clothing Morph V2"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphV2Settings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UEFClothingFitRegistry> Registry = TSoftObjectPtr<UEFClothingFitRegistry>(
		FSoftObjectPath(TEXT("/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));

	/** Authored allow-list and per-body coverage policy for every clothing mesh. */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UDataTable> GarmentCatalog = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments.DT_EFClothingGarments")));

	/** Project-owned Calysto-style hub for compile catalog, tuning catalog and safe offset limits. */
	UPROPERTY(Config, EditAnywhere, Category = "Director")
	TSoftObjectPtr<UEFClothingMorphDirectorPolicy> DirectorPolicy = TSoftObjectPtr<UEFClothingMorphDirectorPolicy>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.DA_EFClothingMorphDirector")));

	/** Runtime-only, topology-free table. Its row names match GarmentCatalog row names exactly. */
	UPROPERTY(Config, EditAnywhere, Category = "Director")
	TSoftObjectPtr<UDataTable> GarmentTuningCatalog = TSoftObjectPtr<UDataTable>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarmentTuning.DT_EFClothingGarmentTuning")));

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float ReconcileIntervalSeconds = 0.10f;

	/** Zero evaluates pose/JCM curves every post-animation frame but writes only changed values. */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MorphSyncIntervalSeconds = 0.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "0.00001", ClampMax = "0.1"))
	float MorphWriteEpsilon = 0.0005f;

	/** Values below 1 are rejected because they would invalidate the compiled anti-clipping clearance. */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float ClearanceMultiplier = 1.0f;

	/**
	 * Project-owned universal late surface constraint. This is composed with the
	 * garment's existing DAZ deformer; it never replaces the mesh deformer asset.
	 * SurfaceWrapGPU rows remain hidden when this graph is missing or invalid.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Wrap")
	TSoftObjectPtr<UOptimusDeformer> SurfaceConstraintDeformer = TSoftObjectPtr<UOptimusDeformer>(
		FSoftObjectPath(TEXT("/EFClothingMorph/Deformers/DG_EFGarmentSurfaceConstraint.DG_EFGarmentSurfaceConstraint")));

	/** Compatibility global offset; new authoring should use DA_EFClothingMorphDirector. */
	UPROPERTY(Config, EditAnywhere, Category = "98 | Legacy Compatibility", meta = (ClampMin = "0.0", ClampMax = "0.35", Units = "cm", DisplayName = "Legacy Global Offset (cm)"))
	float GlobalClearanceOffsetCm = 0.0f;

	/** Number of corrected render frames required before a newly equipped garment may become visible. */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Wrap", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SurfaceWarmupFrames = 2;

	/**
	 * Maximum fail-closed wait for first-time shader/DDC warm-up. A garment is
	 * never exposed during this interval; persistent fallbacks transition to
	 * Failed when the timeout expires.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Wrap", meta = (ClampMin = "1.0", ClampMax = "60.0", Units = "s"))
	float SurfaceShaderWarmupTimeoutSeconds = 15.0f;

	/**
	 * Removes the visible first-equip IO window. Generated fits are streamed
	 * asynchronously after the small registry loads, before ACF assigns garments.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bPrefetchCompiledFitsOnBeginPlay = false;

	UPROPERTY(Config, EditAnywhere, Category = "Safety")
	bool bRequireStrictReferenceSkeleton = true;

	/**
	 * Some DAZ garments intentionally retain a per-mesh reference pose while sharing the
	 * exact USkeleton hierarchy. Fingerprints still lock each pose against later mutation.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Safety")
	bool bRequireExactReferencePoseMatch = false;

	/** NPCs can opt in by adding the runtime component explicitly or enabling this project-wide. */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableForNonPlayerPawns = false;
};
