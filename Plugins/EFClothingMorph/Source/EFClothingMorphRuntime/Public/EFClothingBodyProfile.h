#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingV5Types.h"
#include "EFClothingBodyProfile.generated.h"

class USkeletalMesh;

/**
 * Generated V5.1 body-side mirror. The Director remains the only authoring
 * table; this internal profile never modifies the body mesh, skin weights, or
 * shared skeleton.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Generated Body Profile"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingBodyProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SchemaVersion = EFClothingMorphV5::BodyProfileSchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName BodyProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Body")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Complete list of skin slots eligible for coverage/hiding on this body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Body")
	TArray<FName> SkinMaterialSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Body", meta = (TitleProperty = "RegionId"))
	TArray<FEFClothingBodyRegionDefinition> Regions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Morphs", meta = (TitleProperty = "MorphGroupId"))
	TArray<FEFClothingMorphGroupDefinition> MorphGroups;

	/** Lightweight validation that does not synchronously load the body mesh. */
	bool ValidateProfile(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Body Profile")
	bool IsProfileValid() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Body Profile")
	FString GetProfileValidationError() const;

	const FEFClothingBodyRegionDefinition* FindRegion(FName RegionId) const;
};
