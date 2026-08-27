#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFClothingFitCompilerLibrary.generated.h"

class UEFClothingFitProfile;
class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSurfaceBinding;
class USkeletalMesh;

/** Generic request for one unordered, simultaneously active body-morph pair. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingMorphPairCompileRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs")
	FName FirstBodyMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs")
	FName SecondBodyMorph = NAME_None;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingFitCompileOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputRoot = TEXT("/EFClothingMorph/_Internal/Compiled/V26");

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

	/**
	 * Generate and certify body-morph bindings. Disable only for a rest/animation
	 * QA profile; relevant body morphs remain monitored and fail closed at runtime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs")
	bool bCompileBodyMorphBindings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaximumTransferredMorphs = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MinimumTransferredMorphDeltaCm = 0.02f;

	/** Non-zero samples over 0..1 used to certify every bound morph/JCM against the body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "2", ClampMax = "8"))
	int32 MorphClearanceSampleCount = 4;

	/** Residual limit used only at morph extremes; independent from the rest-pose sculpt limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float MaximumMorphRepairCm = 5.0f;

	/** Pair certificates to compile; requests are canonicalized lexically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs")
	TArray<FEFClothingMorphPairCompileRequest> MorphPairRequests;

	/** Uniform cell count on both axes; V24 initially certifies a 4x4 grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs", meta = (ClampMin = "2", ClampMax = "16"))
	int32 MorphPairGridResolution = 4;

	/** Low/mid/high samples per axis produce nine body probes per cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs", meta = (ClampMin = "3", ClampMax = "9"))
	int32 MorphPairProbeCountPerAxis = 3;

	/** Any strictly non-zero monitored value participates in the active set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Pairs", meta = (ClampMin = "0.0", ClampMax = "0.0"))
	float MorphActivationEpsilon = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deformer")
	bool bCopyBodyDeformerToDerived = true;

	/**
	 * Native batch compiler transaction flag. Deliberately private and not
	 * reflected: only CompileGarmentCatalog may publish the atomic registry.
	 */
private:
	bool bDeferRegistryPublication = true;

	friend class UEFClothingFitCompilerLibrary;
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
	TObjectPtr<UEFClothingSurfaceBinding> SurfaceBinding = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

/** Per-row evidence emitted by the atomic catalog compiler. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingCatalogCompileRowResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FName RowName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/** True only for rows whose certified runtime contract requires a V26 GPU binding. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bRequiresSurfaceBinding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<USkeletalMesh> DerivedGarment = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingFitProfile> Profile = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingSurfaceBinding> SurfaceBinding = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

/** Whole-catalog transaction result. The registry changes only on total success. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingCatalogCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 EnabledRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 CompiledRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 SurfaceWrapRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FEFClothingCatalogCompileRowResult> Rows;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingFitRegistry> Registry = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingFitValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

/** Editor-only, non-destructive compiler. Source assets and all USkeleton objects are read-only inputs. */
UCLASS()
class EFCLOTHINGMORPHEDITOR_API UEFClothingFitCompilerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Editor automation bridge for importing registered coverage tags from the legacy catalog. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Compiler")
	static FGameplayTagContainer MakeGameplayTagContainerFromNames(const TArray<FName>& TagNames);

	/** One-time schema gate; identity remains read-only in the Director details panel. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static bool UpgradeDirectorIdentityToSchema2(UEFClothingMorphDirectorPolicy* Director);

	/** Internal staging primitive. CompileGarmentCatalog is the only public publication API. */
	static FEFClothingFitCompileResult CompileFitProfile(
		USkeletalMesh* SourceGarment,
		USkeletalMesh* BodySurface,
		USkeletalMesh* CompatibilityReference,
		FEFClothingFitCompileOptions Options);

	/**
	 * Compiles every enabled catalog row into staging assets, validates the full
	 * set, then replaces the generated registry with one atomic save.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static FEFClothingCatalogCompileResult CompileGarmentCatalog(
		UEFClothingMorphDirectorPolicy* Director,
		USkeletalMesh* CompatibilityReference,
		FEFClothingFitCompileOptions Options);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static bool ValidateCompiledProfile(UEFClothingFitProfile* Profile, FString& OutReport);

	/** Reflection-stable result for Python/automation, including failure reports. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static FEFClothingFitValidationResult ValidateCompiledProfileDetailed(UEFClothingFitProfile* Profile);
};
