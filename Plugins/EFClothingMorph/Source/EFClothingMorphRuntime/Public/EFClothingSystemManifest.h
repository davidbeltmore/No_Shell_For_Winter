#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingSystemManifest.generated.h"

class UEFClothingBodyProfile;
class UEFClothingDefinition;
class UEFClothingFitRegistry;

/**
 * Version-neutral, streamable entry point for EF Clothing Morph.
 *
 * V5.1 keeps the existing project Director as the only authoring table and
 * compiles its scalable runtime metadata into this internal manifest. All references are soft so
 * opening or loading the manifest never pulls every body, garment, or compiled
 * surface binding into memory.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Generated Manifest"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingSystemManifest : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UEFClothingSystemManifest();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName SystemId = TEXT("EFClothingMorphV5");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Catalog")
	TArray<TSoftObjectPtr<UEFClothingBodyProfile>> BodyProfiles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Catalog")
	TArray<TSoftObjectPtr<UEFClothingDefinition>> Garments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generated Runtime")
	TSoftObjectPtr<UEFClothingFitRegistry> BindingRegistry;

	/** Validates identity, unique soft paths, and the configured V5 registry. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|V5")
	bool IsValidManifest() const;

	/** Empty only when IsValidManifest returns true. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|V5")
	FString GetValidationError() const;

	bool Validate(FString& OutError) const;
};
