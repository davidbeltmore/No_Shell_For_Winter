#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.generated.h"

/**
 * Single project-owned control point for EF Clothing Morph V3. The existing
 * class and asset path remain stable so saved references do not need migration.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Director V3"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphDirectorPolicy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UEFClothingMorphDirectorPolicy();

	/** Internal text shown by the custom Details panel; not another setting. */
	UPROPERTY()
	FText AuthoringGuide;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	int32 SchemaVersion = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	FName DirectorId = TEXT("EFClothingMorphV3");

	/** The only public catalog. Add one entry for each garment/body pair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Garments", meta = (TitleProperty = "GarmentId", DisplayName = "Garments", ToolTip = "The complete EF Clothing Morph catalog. Each array entry contains one garment mesh, its reference body, runtime fit controls, exclusions, and optional native authoring controls."))
	TArray<FEFClothingGarmentRow> Garments;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V3|Director")
	bool ValidatePolicy(FString& OutError) const;

	/** Python/Blueprint-friendly validation wrapper with no out parameter. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V3|Director")
	bool IsPolicyValid() const;

	/** Empty only when IsPolicyValid is true. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V3|Director")
	FString GetPolicyValidationError() const;

	/** Clamps one garment index's authored offset to the internal certified budget. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V3|Director")
	float ClampAdditionalClearanceCm(float RequestedClearanceCm) const;

	/** Fast C++ lookup by the stable garment identity. */
	const FEFClothingGarmentRow* FindGarmentById(FName GarmentId) const;

	/** Blueprint/Python-friendly lookup without exposing a transient struct pointer. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V3|Director")
	bool GetGarmentById(FName GarmentId, FEFClothingGarmentRow& OutGarment) const;
};
