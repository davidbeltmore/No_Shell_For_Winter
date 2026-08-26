#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFClothingSurfaceReadbackQALibrary.generated.h"

class UEFClothingSurfaceBinding;
class USkeletalMeshComponent;

/** Lifecycle of one asynchronous GPU geometry QA request. */
UENUM(BlueprintType)
enum class EEFClothingSurfaceReadbackQAState : uint8
{
	Unknown,
	PendingGPU,
	Analyzing,
	Ready,
	Failed
};

/**
 * Metrics calculated from the actual render-vertex buffers produced by Optimus.
 * All distances are in Unreal centimeters in garment-component space.
 */
USTRUCT(BlueprintType)
struct EFCLOTHINGMORPHEDITOR_API FEFClothingSurfaceReadbackQAResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	EEFClothingSurfaceReadbackQAState State = EEFClothingSurfaceReadbackQAState::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	FString RequestId;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	FString Error;

	/** Optimus instance that fulfilled the post-EF garment readback. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	FString SurfaceInstanceName;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int64 RequestedGameFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int64 RequestedGarmentPoseRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int64 RequestedBodyPoseRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 GarmentLODIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 BodyLODIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 GarmentRenderVertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 BodyRenderVertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 TestedGarmentTriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 TestedBodyTriangleCount = 0;

	/** Exact non-coplanar triangle/triangle intersections reported by GeometryCore. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 TriangleIntersectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float MinimumVertexSkinGapCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 VertexSkinGapViolationCount = 0;

	/** Compiled V26 edge/interior witnesses, evaluated on final GPU positions. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 TriangleSampleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float MinimumTriangleSampleSkinGapCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 TriangleSampleSkinGapViolationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float MinimumClearanceResidualCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 ClearanceResidualViolationCount = 0;

	/** Magnitude of post-EF position minus the normal DAZ/cloth output. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float CorrectionMagnitudeP99Cm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float MaximumCorrectionMagnitudeCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 InvalidOrNonFiniteVertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 SkippedDegenerateTriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	int32 ExcludedOrHiddenBodySectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	bool bUsedExactV26Binding = false;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	bool bLODPairMatched = false;

	/** False means the binding supplied no compiled face witnesses for this LOD pair. */
	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	bool bTriangleSampleCoverageAvailable = false;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float TotalLatencyMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EF Clothing|QA")
	float AnalysisTimeMs = 0.0f;
};

/**
 * Editor-only asynchronous readback entry point for PIE automation and Python.
 * It never calls FlushRenderingCommands and never changes either component.
 */
UCLASS()
class EFCLOTHINGMORPHEDITOR_API UEFClothingSurfaceReadbackQALibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Arms three readbacks for the next deformer dispatch: final body, normal
	 * garment output, and the EF BeginInitViews surface-pass output.
	 *
	 * ExpectedSurfaceDeformer must be DG_EFGarmentSurfaceConstraint. It is an
	 * UObject parameter so the editor-only API does not expose Optimus types to
	 * project runtime modules.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing|QA", meta = (DevelopmentOnly))
	static bool BeginFinalGeometryReadback(
		USkeletalMeshComponent* GarmentComponent,
		USkeletalMeshComponent* BodyComponent,
		UObject* ExpectedSurfaceDeformer,
		UEFClothingSurfaceBinding* SurfaceBinding,
		float GlobalClearanceOffsetCm,
		float GarmentClearanceOffsetCm,
		FString& OutRequestId,
		FString& OutError);

	/** Non-blocking poll. Python should tick PIE between polls. */
	UFUNCTION(BlueprintPure, Category = "EF Clothing|QA", meta = (DevelopmentOnly))
	static FEFClothingSurfaceReadbackQAResult PollFinalGeometryReadback(
		const FString& RequestId);

	/** Releases the registry reference; in-flight callbacks remain memory-safe. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing|QA", meta = (DevelopmentOnly))
	static bool ReleaseFinalGeometryReadback(const FString& RequestId);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing|QA", meta = (DevelopmentOnly))
	static void ReleaseAllFinalGeometryReadbacks();
};
