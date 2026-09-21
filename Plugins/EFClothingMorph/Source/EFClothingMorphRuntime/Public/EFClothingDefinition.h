#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "EFClothingV5Types.h"
#include "EFClothingDefinition.generated.h"

class UEFClothingBodyProfile;
class USkeletalMesh;

/**
 * Generated V5.1 garment mirror. The Director row is the only author-edited
 * contract; runtime artifacts remain external and are reached through stable
 * binding identity instead of hard references.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Generated Definition"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SchemaVersion = EFClothingMorphV5::ClothingDefinitionSchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName GarmentId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Clothing")
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Clothing")
	TSoftObjectPtr<UEFClothingBodyProfile> BodyProfile;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Layering", meta = (ShowOnlyInnerProperties))
	FEFClothingLayerRule LayerRule;

	/** Regions covered even when they do not reserve fitting thickness. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Coverage")
	TArray<FName> CoveredBodyRegionIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Coverage")
	FGameplayTagContainer CoverageTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Materials", meta = (ShowOnlyInnerProperties))
	FEFClothingMaterialPolicySet MaterialPolicy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Fit")
	float AdditionalClearanceCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Fit")
	float ShellThicknessCm = 0.0f;

	/** Generated mirror of the per-row, morph-conditioned lower-body reserve. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Fit", meta = (ShowOnlyInnerProperties))
	FEFClothingLowerBodyMorphGuardRule LowerBodyMorphGuard;

	/** Content-addressed identity published by the V5 compiler. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Compiled Binding")
	FName BindingStableId = NAME_None;

	/** Schema/compiler contract version for BindingStableId. Zero means not compiled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Compiled Binding")
	int32 BindingVersion = 0;

	/** Validation is asset-only and never synchronously loads meshes or profiles. */
	bool ValidateDefinition(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Definition")
	bool IsDefinitionValid() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Definition")
	FString GetDefinitionValidationError() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Definition")
	bool HasCompiledBinding() const
	{
		return !BindingStableId.IsNone() && BindingVersion > 0;
	}
};
