#pragma once

#include "CoreMinimal.h"
#include "EFClothingBodyTarget.generated.h"

class USkeletalMesh;
class UMeshDeformer;

/** One body registration applies to every garment, independently of gender identity. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingBodyTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Semantic aliases translate the reference body's section names to this body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TMap<FName, FName> MaterialSlotAliases;

	/** Geometry that must never attract clothing. Does not itself hide skin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TArray<FName> ExcludedFitBoneBranches;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TArray<FName> GenitalMaterialSlots;

	/** For anatomy sharing a skin material, hide only its weighted bone branches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TArray<FName> GenitalBoneBranches;

	/** Optional visibility-aware adapter, used only while bone coverage is owned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<UMeshDeformer> BoneCoverageDeformer;

	/** Replace only this known source deformer; preserve third-party overrides. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<UMeshDeformer> BoneCoverageDeformerSource;
};
