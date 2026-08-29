#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "EFClothingSurfaceDeformerBuilderLibrary.generated.h"

/** Result returned to editor UI, Python and commandlet automation. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingSurfaceDeformerBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing Morph V2|Surface Deformer")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing Morph V2|Surface Deformer")
	bool bRebuilt = false;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing Morph V2|Surface Deformer")
	FSoftObjectPath DeformerAsset;

	/** Structural validation and Optimus compile diagnostics. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing Morph V2|Surface Deformer")
	FString Report;
};

/**
 * Reproducible project-owned builder for the universal garment surface constraint.
 *
 * The implementation intentionally uses only public Optimus APIs plus reflected
 * properties/classes for Optimus node types whose headers are private in UE 5.8.
 * It never modifies a Skeletal Mesh, Skeleton, Blueprint, Engine or Marketplace asset.
 *
 * Python/commandlet entry point:
 *   unreal.EFClothingSurfaceDeformerBuilderLibrary.build_or_update_surface_constraint_deformer(False)
 */
UCLASS()
class EFCLOTHINGMORPHEDITOR_API UEFClothingSurfaceDeformerBuilderLibrary final
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Build, structurally validate, compile and save the canonical V26 graph asset. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Surface Deformer")
	static FEFClothingSurfaceDeformerBuildResult BuildOrUpdateSurfaceConstraintDeformer(
		bool bForceRebuild = false);

	/** Read-only structural/status validation of the canonical graph asset. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Surface Deformer")
	static FEFClothingSurfaceDeformerBuildResult ValidateSurfaceConstraintDeformer();
};
