#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.generated.h"

/**
 * Project-owned, Calysto-style control point for EF Clothing Morph V2.
 * This is the only human-authored catalog: each garment index owns its compile,
 * coverage and topology-free runtime-clearance options in one array element.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Director"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphDirectorPolicy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UEFClothingMorphDirectorPolicy();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "00 | Read Me", meta = (MultiLine = "true"))
	FText AuthoringGuide;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identity")
	int32 SchemaVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identity")
	FName DirectorId = TEXT("EFClothingMorphV2");

	/** Add garments here. GarmentId is stable; array order is presentation-only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Garment Catalog", meta = (TitleProperty = "GarmentId", DisplayName = "Clothing Catalog", ToolTip = "One index per garment/body pair. Expand an entry to edit every compile, coverage and runtime-offset option in one place."))
	TArray<FEFClothingGarmentRow> Garments;

	/** Master switch for safe per-garment clearance offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning", meta = (DisplayName = "Enable Runtime Offsets"))
	bool bEnableRuntimeTuning = true;

	/** Optional outward clearance shared by all garments, expressed in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning", meta = (ClampMin = "0.0", ClampMax = "0.35", UIMin = "0.0", UIMax = "0.35", Units = "cm", DisplayName = "Global Extra Surface Offset (cm)", EditCondition = "bEnableRuntimeTuning", ToolTip = "Outward runtime offset shared by every garment. Values are clamped and never invalidate the catalog."))
	float GlobalAdditionalClearanceCm = 0.0f;

	/** Hard budget shared by global, per-component and per-garment Director offsets. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "03 | Safe Runtime Tuning", meta = (Units = "cm", DisplayName = "Certified Runtime Offset Limit (cm)", AdvancedDisplay))
	float MaximumAdditionalClearanceCm = 0.35f;

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

	/** Fast C++ lookup by the stable garment identity. */
	const FEFClothingGarmentRow* FindGarmentById(FName GarmentId) const;

	/** Blueprint/Python-friendly lookup without exposing a transient struct pointer. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool GetGarmentById(FName GarmentId, FEFClothingGarmentRow& OutGarment) const;
};
