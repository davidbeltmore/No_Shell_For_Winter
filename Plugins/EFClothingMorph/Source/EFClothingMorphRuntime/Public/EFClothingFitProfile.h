#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingFitProfile.generated.h"

class USkeletalMesh;

/** Shared discrete runtime-clearance contract for V25 certificates. */
namespace EFClothingMorphV25
{
	inline constexpr int32 ClearanceTierCount = 9;
	inline constexpr float ClearanceTierMin = 1.0f;
	inline constexpr float ClearanceTierMax = 2.0f;
	inline constexpr float ClearanceTierTolerance = 1.0e-4f;

	FORCEINLINE bool IsCertifiedClearanceMultiplier(const float Value)
	{
		if (!FMath::IsFinite(Value)
			|| Value < ClearanceTierMin - ClearanceTierTolerance
			|| Value > ClearanceTierMax + ClearanceTierTolerance)
		{
			return false;
		}

		constexpr float TierStep =
			(ClearanceTierMax - ClearanceTierMin)
			/ static_cast<float>(ClearanceTierCount - 1);
		const float RawTierIndex = (Value - ClearanceTierMin) / TierStep;
		if (!FMath::IsFinite(RawTierIndex))
		{
			return false;
		}

		const int32 TierIndex = FMath::RoundToInt(RawTierIndex);
		return TierIndex >= 0
			&& TierIndex < ClearanceTierCount
			&& FMath::IsNearlyEqual(
				Value,
				ClearanceTierMin + TierIndex * TierStep,
				ClearanceTierTolerance);
	}
}

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float BodyValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	FName GarmentMorph = NAME_None;

	/** Explicit zero-delta key; no empty Unreal morph target is generated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	bool bIdentity = false;

	/** If true, this key switches from the previous key without unsafe linear blending. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	bool bStepFromPrevious = false;

	/** Absolute body value where a stepped interval switches to this key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float StepSwitchBodyValue = 0.0f;

	/** Lowest compiler-certified EF_AutoFit multiplier for this absolute shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float MinimumClearanceMultiplier = 0.0f;
};

UENUM(BlueprintType)
enum class EEFClothingFitMode : uint8
{
	Tight,
	Hybrid,
	Loose,
	Rigid
};

USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	FName BodyMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	FName GarmentMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float Bias = 0.0f;

	/** Surface-clearance range certified by the compiler for this binding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float MinimumCertifiedValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	float MaximumCertifiedValue = 1.0f;

	/** Piecewise corrective shapes; runtime blends only the two surrounding samples. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	TArray<FEFClothingMorphSample> Samples;
};

/**
 * One absolute, cooked garment envelope for a rectangular two-morph domain.
 * Runtime selects exactly one cell at weight 1; cells are never blended or
 * added to the one-dimensional morph samples.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphPairCell
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 FirstCellIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 SecondCellIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float FirstMinimumValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float FirstMaximumValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float SecondMinimumValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float SecondMaximumValue = 0.0f;

	/** Absolute envelope shape relative to the clearance-fitted rest garment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	FName GarmentMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float MinimumCertifiedGapCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 CertifiedBodyProbeCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 CertifiedOffsetEvaluationCount = 0;

	/** Lowest compiler-certified EF_AutoFit multiplier for every probe in this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float MinimumClearanceMultiplier = 0.0f;
};

/**
 * Generic fail-closed certificate for one unordered pair of body morphs.
 * Names are stored in deterministic lexical order by the compiler.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingMorphPairCertificate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	FName FirstBodyMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	FName SecondBodyMorph = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float FirstMinimumCertifiedValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float FirstMaximumCertifiedValue = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float SecondMinimumCertifiedValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float SecondMaximumCertifiedValue = 1.0f;

	/** Number of cells along each body-morph axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 GridResolution = 4;

	/** Low/mid/high by default; the compiler certifies the Cartesian product. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 ProbeCountPerAxis = 3;

	/** Must agree with the profile-wide certified runtime offset tier table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	int32 CertifiedOffsetTierCount = 9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	TArray<FEFClothingMorphPairCell> Cells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float MinimumCertifiedGapCm = 0.0f;
};

/**
 * Immutable runtime contract produced by the EF Clothing Morph V2 editor compiler.
 * It references a source garment but only ever writes to a generated derivative.
 */
UCLASS(BlueprintType)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> FittedGarment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	/** Multiple is compatibility evidence only. Runtime never assigns or mutates it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> CompatibilityReference;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid BuildGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 CompilerVersion = EFClothingMorphV26::CompilerVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EEFClothingFitMode FitMode = EEFClothingFitMode::Tight;

	/** V26 final-deformed-surface contract. V25 fallback profiles intentionally leave this null. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TSoftObjectPtr<UEFClothingSurfaceBinding> SurfaceBinding;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skinning")
	FName SkinWeightProfileName = TEXT("EF_AutoFit");

	/**
	 * Exact non-zero bones used by the compiled skin-weight profile. Runtime
	 * refuses a LeaderPose source which cannot animate every one of these bones.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skinning")
	TArray<FName> RequiredWeightedBones;

	/** Exact catalog-authored body sections omitted from every surface query. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	/** Catalog-authored optional anatomy branches redirected during weight transfer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TArray<FName> ExcludedBodyBoneBranches;

	/** Catalog-authored optional anatomy morph namespaces ignored by this fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TArray<FString> ExcludedBodyMorphPrefixes;

	/** LOD0 body triangles removed from the transient compiler surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 ExcludedBodySurfaceTriangleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	FName ClearanceMorphName = TEXT("EF_AutoFit_Clearance");

	/** Runtime value for the baked clearance morph. A value of 1 applies the compiled clearance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DefaultClearanceValue = 1.0f;

	/** Discrete lower/upper offset range exhaustively sampled by the compiler. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CertifiedClearanceMultiplierMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CertifiedClearanceMultiplierMax = 1.0f;

	/** Runtime rounds requested offset upward to one of these certified tiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	int32 CertifiedClearanceTierCount = 1;

	/** Lowest sampled gap over all certified offset tiers and individual morph shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float MinimumCertifiedOffsetGapCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMinimumClearanceCm = 0.35f;

	/** Extra base clearance baked above the public minimum to absorb interpolation loss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledClearanceReserveCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMaxPushCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMaximumMorphRepairCm = 5.0f;

	/** Largest generated sample displacement relative to the clearance-fitted rest garment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMaximumMorphDisplacementCm = 0.0f;

	/** Exact UE LOD build threshold used while cooking every generated morph target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledMorphThresholdPositionCm = 0.0f;

	/** Number of requested vertex deltas changed by UE's per-vertex morph cook threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PostThresholdAlteredDeltaCount = 0;

	/** Conservative culling expansion over every mutually-exclusive V24 render state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	FVector CompiledConcurrentBoundsExpansionCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clearance")
	float CompiledConcurrentSphereExpansionCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph")
	TArray<FEFClothingMorphBinding> MorphBindings;

	/**
	 * Complete sorted set of body morphs whose garment-region influence is
	 * monitored. Any unsupported simultaneous active set is hidden fail-closed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	TArray<FName> MonitoredBodyMorphNames;

	/** Immutable V24 pair certificates; V23 assets leave this array empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	TArray<FEFClothingMorphPairCertificate> MorphPairCertificates;

	/** Exact active-set threshold compiled into the V24 contract; currently zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morph Pair")
	float CompiledMorphActivationEpsilon = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SourceSkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString BodySkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString CompatibilitySkeletonFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString FittedSkeletonFingerprint;

	/** Protects the shared USkeleton hierarchy and virtual-bone metadata itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SharedSkeletonFingerprint;

	/** Editor-only retarget-source/reference-pose extension of the shared skeleton guard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SharedSkeletonEditorFingerprint;

	/** Package identity guards. Content freshness is tracked separately below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SourcePackageGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString BodyPackageGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString CompatibilityPackageGuid;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString FittedPackageGuid;

	/** DDC-backed content signatures invalidate profiles after editor-time mesh reimports. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString SourceContentFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString BodyContentFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString CompatibilityContentFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Integrity")
	FString FittedContentFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 CompiledLODCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 SourceVertexCount = 0;

	/** Final generated LOD0 import vertices after optional risk densification. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 FittedVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 AdjustedVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PenetratingVertexCountBefore = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PenetratingVertexCountAfter = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSignedGapBeforeCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSignedGapAfterCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 TransferredMorphCount = 0;

	/** Distinct unsafe/missing body bones conservatively collapsed to a compatible ancestor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 RemappedWeightedBoneCount = 0;

	/** Dynamic split vertices normalized to one deterministic stored weight set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 ReconciledSplitVertexCount = 0;

	/** MeshDescription vertices whose committed EF_AutoFit weights matched exact readback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 CertifiedSkinWeightVertexCount = 0;

	/** Morphs rebuilt or corrected by the sampled surface-clearance solver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 ClearanceValidatedMorphCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 ClearanceRepairedMorphCount = 0;

	/** Lowest sampled vertex-to-body signed gap across bound morphs, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSampledMorphGapCm = 0.0f;

	/** Uniform seed-key count; unsafe intervals may receive extra adaptive keys. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 MorphClearanceSampleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 GeneratedMorphSampleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 MaximumMorphSamplesPerBinding = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 SteppedMorphIntervalCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 IdentityMorphSampleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 CertifiedMorphPairCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 GeneratedPairCellMorphCount = 0;

	/** Total combined-body samples certified across all pair cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PairBodyProbeCount = 0;

	/** Total pair body-probe by runtime-offset-tier geometric evaluations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	int32 PairOffsetEvaluationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metrics")
	float MinimumSampledPairGapCm = 0.0f;

	bool MatchesSource(const USkeletalMesh* Mesh) const;
};

UCLASS(BlueprintType)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EF Clothing Morph V2")
	TArray<TObjectPtr<UEFClothingFitProfile>> Profiles;

	const UEFClothingFitProfile* FindProfileForSource(const USkeletalMesh* SourceMesh) const;
	const UEFClothingFitProfile* FindProfileForSourceAndBody(
		const USkeletalMesh* SourceMesh,
		const USkeletalMesh* BodyMesh) const;

private:
	void RebuildRuntimeIndex() const;
	static FString MakeRuntimeKey(const FSoftObjectPath& SourcePath, const FSoftObjectPath& BodyPath);

	mutable TMap<FString, TWeakObjectPtr<const UEFClothingFitProfile>> RuntimeProfileIndex;
	mutable int32 IndexedProfileCount = INDEX_NONE;
};
