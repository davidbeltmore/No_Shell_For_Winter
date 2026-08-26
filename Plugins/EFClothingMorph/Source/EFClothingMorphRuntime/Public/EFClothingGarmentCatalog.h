#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "EFClothingGarmentCatalog.generated.h"

class USkeletalMesh;

/** Runtime backend selected per garment/body pair by the authored catalog. */
UENUM(BlueprintType)
enum class EEFClothingSurfaceBackend : uint8
{
	/** Geometry-compiled correspondence, certified weights and adaptive clearance. */
	GeometryFitFallback UMETA(DisplayName = "Geometry Fit Fallback"),

	/** Final GPU surface-wrap backend; rows can opt in as its cooked graph becomes available. */
	SurfaceWrapGPU UMETA(DisplayName = "Surface Wrap GPU"),

	Disabled UMETA(Hidden)
};

/**
 * Authored source of truth for meshes that EF Clothing Morph is allowed to manage.
 * A garment can have separate Female/Male rows without changing runtime code.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingGarmentRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	/** Exact visible body surface for this compiled row (Female now, Male later). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adapter")
	EEFClothingSurfaceBackend Backend = EEFClothingSurfaceBackend::GeometryFitFallback;

	/** Semantic coverage used by gameplay/UI and future body-region proxy masks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage")
	FGameplayTagContainer CoverageTags;

	/**
	 * Body material slots hidden only on the live body component while this garment
	 * is applied. Visibility is reference-counted and restored exactly on unequip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage")
	TArray<FName> HiddenBodyMaterialSlots;

	/**
	 * Body sections that must not participate in the compiler's closest-surface,
	 * clearance or skin-weight projection. This is distinct from visual hiding:
	 * auxiliary anatomy can be hidden at runtime and also excluded from the
	 * mathematical body envelope without hard-coding a garment or DAZ product.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adapter")
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	/**
	 * Root bones of optional anatomy branches that must never drive this garment.
	 * The compiler redirects their influence to the nearest non-excluded,
	 * hierarchy-compatible ancestor on the generated EF_AutoFit profile only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adapter")
	TArray<FName> ExcludedBodyBoneBranches;

	/** Body morph namespaces belonging to an excluded accessory/graft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adapter")
	TArray<FString> ExcludedBodyMorphPrefixes;

	/** Optional authored floor; it is always rounded upward to a compiler-certified tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float MinimumClearanceMultiplier = 1.0f;
};
