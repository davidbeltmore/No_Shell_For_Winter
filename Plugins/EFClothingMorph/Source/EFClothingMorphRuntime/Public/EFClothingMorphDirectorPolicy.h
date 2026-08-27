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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "00 | Como usar", meta = (MultiLine = "true", DisplayName = "Guia rapida"))
	FText AuthoringGuide;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identidad", meta = (DisplayName = "Version del formato", AdvancedDisplay))
	int32 SchemaVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 | Identidad", meta = (DisplayName = "ID del Director", AdvancedDisplay))
	FName DirectorId = TEXT("EFClothingMorphV2");

	/** Add garments here. GarmentId is stable; array order is presentation-only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 | Catalogo de prendas", meta = (TitleProperty = "GarmentId", DisplayName = "Catalogo de prendas", ToolTip = "Un indice por cada combinacion de prenda y cuerpo. Expande una entrada para configurar todo en un solo lugar."))
	TArray<FEFClothingGarmentRow> Garments;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool ValidatePolicy(FString& OutError) const;

	/** Python/Blueprint-friendly validation wrapper with no out parameter. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool IsPolicyValid() const;

	/** Empty only when IsPolicyValid is true. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	FString GetPolicyValidationError() const;

	/** Clamps one garment index's authored offset to the internal certified budget. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	float ClampAdditionalClearanceCm(float RequestedClearanceCm) const;

	/** Fast C++ lookup by the stable garment identity. */
	const FEFClothingGarmentRow* FindGarmentById(FName GarmentId) const;

	/** Blueprint/Python-friendly lookup without exposing a transient struct pointer. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Director")
	bool GetGarmentById(FName GarmentId, FEFClothingGarmentRow& OutGarment) const;
};
