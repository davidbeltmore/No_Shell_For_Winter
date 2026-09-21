#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingBodyTarget.h"
#include "EFClothingMorphDirectorPolicy.generated.h"

/**
 * The single project-owned authoring table for EF Clothing Morph V5.1. The
 * serialized V4 identity, class and asset path remain unchanged so existing
 * saves, Blueprints and the rollback compiler need no destructive migration.
 * Every other V5.1 asset is an internal generated output and is never another
 * authoring surface.
 */
UCLASS(BlueprintType, meta = (DisplayName = "EF Clothing Morph Table V5.1"))
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

	/** The only public catalog and the only table a clothing author edits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clothes", meta = (TitleProperty = "GarmentId", DisplayName = "Clothes", ToolTip = "This is the only clothing table to maintain. Add each clothing mesh once with its reference body. All registered bodies receive an automatic unisex fit."))
	TArray<FEFClothingGarmentRow> Garments;

	/** Register each body once. Every clothing row is unisex. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bodies", meta = (TitleProperty = "BodySurface"))
	TArray<FEFClothingBodyTarget> Bodies;

	/** Internal expansion; stable authored clothing rows and paths are unchanged. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph|Bodies")
	TArray<FEFClothingGarmentRow> BuildBodyVariants() const;

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
