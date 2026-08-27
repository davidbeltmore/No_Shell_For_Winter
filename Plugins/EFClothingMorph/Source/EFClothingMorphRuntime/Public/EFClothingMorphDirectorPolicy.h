#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingMorphDirectorPolicy.generated.h"

class UDataTable;

/**
 * Project-owned, Calysto-style control point for EF Clothing Morph V2.
 * It separates structural compilation data from harmless runtime clearance
 * tuning so a designer never has to sculpt a source Skeletal Mesh for fit.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Director"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphDirectorPolicy : public UDataAsset
{
	GENERATED_BODY()

public:
	UEFClothingMorphDirectorPolicy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "00 | Read Me", meta = (MultiLine = "true"))
	FText AuthoringGuide;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identity")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identity")
	FName DirectorId = TEXT("EFClothingMorphV2");

	/** Add/remove garment registrations here. Row name is the stable catalog index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Catalogs")
	TSoftObjectPtr<UDataTable> CompileCatalog;

	/** Adjust only Extra Surface Offset here; rows use the exact same stable index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Catalogs")
	TSoftObjectPtr<UDataTable> RuntimeTuningCatalog;

	/** Master switch for safe DataTable clearance offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning")
	bool bEnableRuntimeTuning = true;

	/** Optional outward clearance shared by all garments, expressed in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm"))
	float GlobalAdditionalClearanceCm = 0.0f;

	/** Hard budget shared by global, per-component and DataTable tuning offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm"))
	float MaximumAdditionalClearanceCm = 0.35f;

	/** Orphan tuning rows are harmless but reported for authoring hygiene. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 | Validation")
	bool bWarnOnOrphanTuningRows = true;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool ValidatePolicy(FString& OutError) const;

	/** Python/Blueprint-friendly validation wrapper with no out parameter. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool IsPolicyValid() const;

	/** Empty only when IsPolicyValid is true. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	FString GetPolicyValidationError() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	float ClampAdditionalClearanceCm(float RequestedClearanceCm) const;
};
