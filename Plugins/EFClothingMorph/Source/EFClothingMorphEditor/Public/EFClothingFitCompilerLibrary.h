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

/**
 * Native-source binding compiler controls. This path never creates or modifies
 * a Skeletal Mesh, morph target, skin-weight profile or USkeleton.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingNativeSourceCompileOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	FString OutputRoot = TEXT("/EFClothingMorph/_Internal/Compiled/V4");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "V3 base clearance is fixed at zero. Use the Director's per-garment Skin Clearance runtime value."))
	float MinimumClearanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clearance", meta = (ClampMin = "0.05", ClampMax = "10.0", Units = "cm"))
	float MaximumPushCm = 2.5f;

	/** Reuse current immutable bindings whose source/body/render fingerprints still match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incremental")
	bool bOnlyStale = true;

	/**
	 * Compile or validate only this clothing entry. NAME_None processes every
	 * ready entry while preserving unrelated published bindings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incremental")
	FName TargetClothingName = NAME_None;

	/**
	 * Cook/package certification mode. Every enabled entry must be complete,
	 * valid, fresh and represented exactly once in the registry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Certification")
	bool bStrictCatalogCertification = false;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingNativeSourceCompileRowResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FName GarmentId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bReusedFreshBinding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingSurfaceBinding> SurfaceBinding = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

/** Native-source publication result with per-clothing diagnostics and counters. */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingNativeSourceCatalogCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 EnabledRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 CompiledRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ReusedFreshRowCount = 0;

	/** Disabled or incomplete entries skipped as harmless authoring drafts. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 DraftRowCount = 0;

	/** Complete entries that failed row-local validation or binding compilation. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 FailedRowCount = 0;

	/** Successful entries merged into the active registry publication. */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 PublishedRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FEFClothingNativeSourceCompileRowResult> Rows;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TObjectPtr<UEFClothingFitRegistry> Registry = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingNativeSourceFreshnessResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bFresh = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 EnabledRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ValidBindingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 DraftRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 InvalidRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 StaleRowCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString Report;
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

	/** Schema-3 authoring gate for per-garment real-thickness controls. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static bool UpgradeDirectorIdentityToSchema3(UEFClothingMorphDirectorPolicy* Director);

	/**
	 * Explicit V3 migration. Consolidates legacy body-section exclusions and
	 * removes generated-shell authoring without modifying either source mesh.
	 * The caller remains responsible for saving the Director asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Compiler")
	static bool UpgradeDirectorIdentityToSchema4(UEFClothingMorphDirectorPolicy* Director);

	/** Upgrade the stable Director asset to the isolated multi-clothing V4 schema. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Compiler")
	static bool UpgradeDirectorIdentityToSchema5(UEFClothingMorphDirectorPolicy* Director);

	/** Internal staging primitive. CompileGarmentCatalog is the only public publication API. */
	static FEFClothingFitCompileResult CompileFitProfile(
		USkeletalMesh* SourceGarment,
		USkeletalMesh* BodySurface,
		USkeletalMesh* CompatibilityReference,
		FEFClothingFitCompileOptions Options);

	/**
	 * Legacy V26 compatibility compiler. V3 Directors are rejected so native-first
	 * projects cannot accidentally regenerate fitted meshes or fit profiles.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static FEFClothingCatalogCompileResult CompileGarmentCatalog(
		UEFClothingMorphDirectorPolicy* Director,
		USkeletalMesh* CompatibilityReference,
		FEFClothingFitCompileOptions Options);

	/**
	 * Bakes one immutable V3 correspondence directly against each row's exact
	 * SourceGarment render buffers, then publishes a registry containing bindings
	 * only. No fitted mesh or fit profile is produced.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Compiler")
	static FEFClothingNativeSourceCatalogCompileResult CompileNativeSourceCatalogV3(
		UEFClothingMorphDirectorPolicy* Director,
		USkeletalMesh* CompatibilityReference,
		FEFClothingNativeSourceCompileOptions Options);

	/** Cheap exact-topology/content check used by pre-PIE and pre-cook gates. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Compiler")
	static FEFClothingNativeSourceFreshnessResult ValidateNativeSourceCatalogV3(
		UEFClothingMorphDirectorPolicy* Director,
		UEFClothingFitRegistry* Registry,
		USkeletalMesh* CompatibilityReference,
		FEFClothingNativeSourceCompileOptions Options);

	/**
	 * Incrementally bakes ready V4 entries and merges them into the current
	 * registry. Drafts and failed entries never discard successful clothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Compiler")
	static FEFClothingNativeSourceCatalogCompileResult CompileNativeSourceCatalogV4(
		UEFClothingMorphDirectorPolicy* Director,
		USkeletalMesh* CompatibilityReference,
		FEFClothingNativeSourceCompileOptions Options);

	/** Row-isolated V4 freshness check used by ordinary editor and strict cook gates. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Compiler")
	static FEFClothingNativeSourceFreshnessResult ValidateNativeSourceCatalogV4(
		UEFClothingMorphDirectorPolicy* Director,
		UEFClothingFitRegistry* Registry,
		USkeletalMesh* CompatibilityReference,
		FEFClothingNativeSourceCompileOptions Options);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static bool ValidateCompiledProfile(UEFClothingFitProfile* Profile, FString& OutReport);

	/** Reflection-stable result for Python/automation, including failure reports. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Compiler")
	static FEFClothingFitValidationResult ValidateCompiledProfileDetailed(UEFClothingFitProfile* Profile);
};
