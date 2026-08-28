#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFClothingSurfaceBinding.generated.h"

class USkeletalMesh;

/** Cooked-data contract shared by the V26 compiler and Surface Wrap runtime. */
namespace EFClothingMorphV26
{
	inline constexpr int32 CompilerVersion = 26;
	inline constexpr int32 SurfaceBindingSchemaVersion = 6;
	inline constexpr float AutomaticCentimeterValue = -1.0f;
	inline constexpr float DefaultBaseClearanceCm = 0.45f;
	inline constexpr float DefaultCompiledReserveCm = 0.10f;
	inline constexpr float MaximumRuntimeAdditionalClearanceCm = 0.35f;
	/** Stable authoring shell used only to publish paired inner/outer topology. */
	inline constexpr float CompiledThicknessReferenceCm = 0.05f;
	/** Runtime-visible range. Invalid authored values are clamped, never used to hide a garment. */
	inline constexpr float MinimumRuntimeVisibleThicknessCm = 0.01f;
	inline constexpr float MaximumRuntimeVisibleThicknessCm = 0.35f;

	FORCEINLINE bool IsAutomaticCentimeterValue(const float Value)
	{
		return FMath::IsFinite(Value) && Value < 0.0f;
	}
}

/** Native-source publication contract introduced after the V26 derived-mesh path. */
namespace EFClothingMorphV3
{
	inline constexpr int32 CompilerVersion = 27;
	inline constexpr int32 SurfaceBindingSchemaVersion = 7;
	inline constexpr TCHAR CompiledOutputRoot[] = TEXT("/EFClothingMorph/_Internal/Compiled/V3");
	/** Native-first V3 preserves the authored silhouette unless skin is actually penetrated. */
	inline constexpr float DefaultCollisionClearanceCm = 0.0f;
	/** V3 exposes every visible gap through the per-garment runtime control; no hidden reserve. */
	inline constexpr float CompiledClearanceReserveCm = 0.0f;
	inline constexpr float MaximumRuntimeClearanceCm = 2.0f;
	inline constexpr float MaximumRuntimeInflateCm = 2.0f;
}

/**
 * Multi-clothing publication contract. V4 keeps the source-authoritative V3
 * geometry path, but gives every clothing entry an independent identity and
 * binding lifecycle so one draft or failed item cannot disable another.
 */
namespace EFClothingMorphV4
{
	inline constexpr int32 CompilerVersion = 28;
	inline constexpr int32 SurfaceBindingSchemaVersion = 8;
	inline constexpr TCHAR CompiledOutputRoot[] = TEXT("/EFClothingMorph/_Internal/Compiled/V4");
	inline constexpr float DefaultCollisionClearanceCm = 0.0f;
	inline constexpr float CompiledClearanceReserveCm = 0.0f;
	inline constexpr float MaximumRuntimeClearanceCm = 2.0f;
	inline constexpr float MaximumRuntimeInflateCm = 2.0f;
}

/** Per-vertex behavior selected automatically by the V26 compiler. */
UENUM(BlueprintType)
enum class EEFClothingSurfaceVertexMode : uint8
{
	/** Preserve the rest offset in the animated body triangle's tangent frame. */
	SurfaceFollow UMETA(DisplayName = "Surface Follow"),

	/** Blend animated garment motion with the transported surface anchor. */
	Hybrid UMETA(DisplayName = "Hybrid"),

	/** Preserve skinning/Chaos output and only apply an outward collision correction. */
	CollisionOnly UMETA(DisplayName = "Collision Only"),

	/** Explicit catalog/body-anatomy exclusion; preserve the upstream garment position. */
	PreserveUpstream UMETA(DisplayName = "Preserve Upstream")
};

/** Exact cooked render topology identity for one Skeletal Mesh LOD. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceTopologyFingerprint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Topology")
	int32 LODIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Topology")
	int32 RenderVertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Topology")
	int32 RenderIndexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Topology")
	int32 TriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Topology")
	int32 SectionCount = 0;

	/** Deterministic hash of section ranges, render indices and vertex-to-import mapping. */
	UPROPERTY(VisibleAnywhere, Category = "Topology")
	FString TopologyFingerprint;

	/** DDC-backed mesh content identity captured when this binding was compiled. */
	UPROPERTY(VisibleAnywhere, Category = "Topology")
	FString ContentFingerprint;
};

/** Flat-array range used for per-vertex neighbor and candidate lookup. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceIndexRange
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	int32 Offset = 0;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	int32 Count = 0;
};

/** A fallback body triangle retained for moving or loose garment regions. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceCandidateTriangle
{
	GENERATED_BODY()

	/** Logical triangle index in the body LOD index buffer. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	int32 BodyTriangleIndex = INDEX_NONE;

	/** Exact render-vertex indices, including split vertices. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FIntVector BodyRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float RestDistanceCm = 0.0f;
};

/** One render vertex bound to the final deformed body surface. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceVertexBinding
{
	GENERATED_BODY()

	/** Exact vertex in the fitted garment render buffer. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	int32 GarmentRenderVertexIndex = INDEX_NONE;

	/** Primary body triangle in exact render-buffer index space. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FIntVector BodyRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	/** Normalized barycentrics on BodyRenderVertexIndices. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FVector3f BodyBarycentrics = FVector3f::ZeroVector;

	/** Tangent, bitangent and normal coordinates relative to the primary body triangle, in cm. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FVector3f RestTangentFrameOffsetCm = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float RestSignedGapCm = 0.0f;

	/** Compiler-selected minimum surface distance before runtime offsets, in cm. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float TargetClearanceCm = EFClothingMorphV26::DefaultBaseClearanceCm;

	/** Surface-anchor blend. CollisionOnly normally stores zero. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float FollowWeight = 0.0f;

	/** Certified one-frame outward correction delta; not an absolute body-gap target. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float MaximumCorrectionCm = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	EEFClothingSurfaceVertexMode Mode = EEFClothingSurfaceVertexMode::CollisionOnly;

	/**
	 * Exact render vertex on the fitted inner layer paired with this vertex.
	 * INDEX_NONE means the garment has no generated thickness shell.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Thickness")
	int32 ThicknessReferenceRenderVertexIndex = INDEX_NONE;

	/** Only outer-layer vertices move when visible thickness is changed at runtime. */
	UPROPERTY(VisibleAnywhere, Category = "Thickness")
	bool bOuterThicknessLayer = false;

	/** Range into FEFClothingSurfaceLODPairBinding::NeighborRenderVertexIndices. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FEFClothingSurfaceIndexRange NeighborRange;

	/** Range into FEFClothingSurfaceLODPairBinding::CandidateTriangles. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FEFClothingSurfaceIndexRange CandidateRange;
};

/** Edge/interior sample preventing a garment face from crossing skin between its vertices. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceWitness
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	int32 GarmentTriangleIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FIntVector GarmentRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	/** Sample coordinates on the garment triangle; also distribute correction to its vertices. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FVector3f GarmentBarycentrics = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FIntVector BodyRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	FVector3f BodyBarycentrics = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float TargetClearanceCm = EFClothingMorphV26::DefaultBaseClearanceCm;

	/** Certified correction delta shared by this witness' garment vertices. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float MaximumCorrectionCm = 0.0f;
};

/** Compiler evidence for one exact garment/body LOD pair. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceBindingMetrics
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 BoundRenderVertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 InvalidAnchorCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 SurfaceFollowVertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 HybridVertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 CollisionOnlyVertexCount = 0;

	/** Vertices inside a catalog-derived optional-anatomy exclusion domain. */
	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 PreserveUpstreamVertexCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 NeighborReferenceCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 CandidateTriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 WitnessCount = 0;

	/** Garment faces omitted because they touch the PreserveUpstream domain. */
	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 ExcludedPreserveUpstreamGarmentTriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 DegenerateBodyTriangleCount = 0;

	/** Rest-pose zero-area source triangles removed before BVH/candidate generation. */
	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 ExcludedDegenerateBodyTriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	int32 ExcludedBodyTriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	float MinimumRestSignedGapCm = 0.0f;

	/** Largest unilateral push required before the first corrected frame. */
	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	float MaximumInitialCorrectionCm = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	float MaximumAnchorErrorCm = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	float MeanAnchorErrorCm = 0.0f;
};

/** Immutable binding for one exact fitted-garment LOD and body-surface LOD. */
USTRUCT()
struct EFCLOTHINGMORPHRUNTIME_API FEFClothingSurfaceLODPairBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FEFClothingSurfaceTopologyFingerprint GarmentTopology;

	UPROPERTY(VisibleAnywhere, Category = "Identity")
	FEFClothingSurfaceTopologyFingerprint BodyTopology;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float BaseClearanceCm = EFClothingMorphV26::DefaultBaseClearanceCm;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	float CompiledReserveCm = EFClothingMorphV26::DefaultCompiledReserveCm;

	/** One entry per exact fitted-garment render vertex, including all render splits. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<FEFClothingSurfaceVertexBinding> VertexBindings;

	/** Flat adjacency pool addressed by each vertex binding's NeighborRange. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<int32> NeighborRenderVertexIndices;

	/** Flat fallback triangle pool addressed by each vertex binding's CandidateRange. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<FEFClothingSurfaceCandidateTriangle> CandidateTriangles;

	/** Adaptive edge/face samples; the compiler targets at most 0.5 cm spacing. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<FEFClothingSurfaceWitness> Witnesses;

	UPROPERTY(VisibleAnywhere, Category = "Metrics")
	FEFClothingSurfaceBindingMetrics Metrics;

	/** Publication is fail-closed; runtime ignores uncertified entries. */
	UPROPERTY(VisibleAnywhere, Category = "Certification")
	bool bCertified = false;
};

/**
 * Generated immutable surface contract for one source/fitted garment and body pair.
 * Runtime may upload this data but must never modify this asset or either source mesh.
 */
UCLASS(BlueprintType)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingSurfaceBinding : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable Director identity. Array order is deliberately not part of this key. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName GarmentId = NAME_None;

	/** Hash of binding-relevant Director authoring. Runtime-only sliders are excluded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FString GarmentCompileFingerprint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> SourceGarment;

	/**
	 * Legacy V26 field. V3 leaves it null because SourceGarment is the exact mesh
	 * rendered by the component; no generated Skeletal Mesh is created or swapped.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> FittedGarment;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<USkeletalMesh> BodySurface;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FGuid BuildGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 CompilerVersion = EFClothingMorphV26::CompilerVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SchemaVersion = EFClothingMorphV26::SurfaceBindingSchemaVersion;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString SourceContentFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString FittedContentFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString BodyContentFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString SourceSkeletonFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString FittedSkeletonFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString BodySkeletonFingerprint;

	UPROPERTY(VisibleAnywhere, Category = "Integrity")
	FString SharedSkeletonFingerprint;

	/** Surface material slots omitted from every compiled body query. */
	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<FName> ExcludedBodySurfaceMaterialSlots;

	UPROPERTY(VisibleAnywhere, Category = "Surface")
	TArray<FEFClothingSurfaceLODPairBinding> LODPairBindings;

	const FEFClothingSurfaceLODPairBinding* FindLODPair(
		int32 GarmentLODIndex,
		int32 BodyLODIndex) const;
};
