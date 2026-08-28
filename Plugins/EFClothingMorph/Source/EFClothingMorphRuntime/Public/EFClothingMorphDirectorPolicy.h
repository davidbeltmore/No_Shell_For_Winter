#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.generated.h"

/**
 * Single project-owned control point for EF Clothing Morph V4. The existing
 * class and asset path remain stable so saved references do not need migration.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Director V4"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphDirectorPolicy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UEFClothingMorphDirectorPolicy();

	/** Internal text shown by the custom Details panel; not another setting. */
	UPROPERTY()
	FText AuthoringGuide;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	int32 SchemaVersion = 5;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Internal")
	FName DirectorId = TEXT("EFClothingMorphV4");

	/** The only public catalog. The serialized property name remains stable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothes", meta = (TitleProperty = "GarmentId", DisplayName = "Clothes", ToolTip = "Add one entry for each clothing mesh and body mesh pair. Every entry is fitted independently, so several clothes can be worn at the same time."))
	TArray<FEFClothingGarmentRow> Garments;

	/** Checks only the V4 schema and identity; row mistakes cannot fail this check. */
	bool ValidateIdentity(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	bool IsIdentityValid() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	FString GetIdentityValidationError() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	bool ValidatePolicy(FString& OutError) const;

	/** Python/Blueprint-friendly validation wrapper with no out parameter. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	bool IsPolicyValid() const;

	/** Empty only when IsPolicyValid is true. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	FString GetPolicyValidationError() const;

	/** Clamps one clothing entry's authored offset to the internal safe budget. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	float ClampAdditionalClearanceCm(float RequestedClearanceCm) const;

	/** Fast C++ lookup by the stable serialized clothing identity. */
	const FEFClothingGarmentRow* FindGarmentById(FName GarmentId) const;

	/** Blueprint/Python-friendly lookup without exposing a transient struct pointer. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4|Director")
	bool GetGarmentById(FName GarmentId, FEFClothingGarmentRow& OutGarment) const;

#if WITH_EDITOR
	/** Fills only missing names after both meshes are assigned; manual names are preserved. */
	bool EnsureMissingClothingNames();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
