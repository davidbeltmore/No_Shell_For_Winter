#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFClothingFitCompilerLibrary.generated.h"

class UEFClothingFitProfile;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingFitCompileOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputRoot = TEXT("/Game/_Generated/EFClothingMorphV2");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float MinimumClearanceCm = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float MaximumPushCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0", ClampMax = "20"))
	int32 SmoothingIterations = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin Weights", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaximumInfluences = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs")
	bool bTransferMissingBodyMorphs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaximumTransferredMorphs = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MinimumTransferredMorphDeltaCm = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformer")
	bool bCopyBodyDeformerToDerived = true;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingFitCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<USkeletalMesh> DerivedGarment = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingFitProfile> Profile = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

/** Editor-only, non-destructive compiler. Source assets and all USkeleton objects are read-only inputs. */
UCLASS()
class EFCLOTHINGMORPHEDITOR_API UEFClothingFitCompilerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static FEFClothingFitCompileResult CompileFitProfile(
		USkeletalMesh* SourceGarment,
		USkeletalMesh* BodySurface,
		USkeletalMesh* CompatibilityReference,
		FEFClothingFitCompileOptions Options);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static bool ValidateCompiledProfile(UEFClothingFitProfile* Profile, FString& OutReport);
};
