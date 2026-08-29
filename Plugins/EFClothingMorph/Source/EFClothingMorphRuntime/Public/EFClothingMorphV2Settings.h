#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFClothingMorphV2Settings.generated.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UOptimusDeformer;

// The C++ class name remains V2 for config/class-path compatibility. The public
// product and Director schema are V3.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Clothing Morph V3"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphV2Settings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (DisplayName = "Enable EF Clothing Morph", ToolTip = "Enables the EF Clothing Morph runtime. Per-garment fit controls remain in the Clothing Morph Director."))
	bool bEnabled = true;

	/** Internal generated registry path. Deliberately hidden from Project Settings. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingFitRegistry> Registry = TSoftObjectPtr<UEFClothingFitRegistry>(
		FSoftObjectPath(TEXT("/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry")));

	/** Fixed public Director path. Hidden so the project cannot accidentally split its catalog. */
	UPROPERTY(Config)
	TSoftObjectPtr<UEFClothingMorphDirectorPolicy> DirectorPolicy = TSoftObjectPtr<UEFClothingMorphDirectorPolicy>(
		FSoftObjectPath(TEXT("/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.DA_EFClothingMorphDirector")));

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
