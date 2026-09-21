#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"

#include "Calysto/EFCalystoAssignRoomThemeV6.h"

class UPCGComponent;
class UPCGGraph;

/** Outcome of composing the V6 Room Theme stage into a transient Calysto graph closure. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGRuntimeGraphResultV6
{
	UPCGGraph* RuntimeGraph = nullptr;
	FString FailureReason;
	/** BLAKE3 over the resolved Theme configuration and the frozen native placement-routing schema. */
	FString ConfigurationFingerprint;
	/** BLAKE3 of the exact Subgraph_3 source-pin -> root-output placement contract. */
	FString PlacementRoutingFingerprint;
	int32 ClonedGraphCount = 0;
	int32 InvalidatedCookedCompilationDataCount = 0;
	int32 RoutedContextBoundaryCount = 0;
	int32 RoutedPlacementOutputCount = 0;
	int32 ReplacedNativeThemeNodeCount = 0;
	bool bCookedCompatibilityApplied = false;
	bool bBuilt = false;
};

/**
 * Stable root-output labels for Calysto's native surface placement candidates.
 * Wall Light intentionally has no EF output: it remains owned by Style Architecture.
 */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPlacementCandidatePinsV6
{
	static const FName Floor;
	static const FName WallBottom;
	static const FName WallMiddle;
	static const FName WallTop;
	static const FName CornerBottom;
	static const FName CornerMiddle;
	static const FName CornerTop;
	static const FName Roof;
};

/**
 * Produces an unsaved graph view that owns V6 room selection while retaining the
 * exact vendor source graph as immutable input. No package is marked dirty.
 */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGRuntimeGraphBuilderV6 final
{
public:
	static FEFCalystoPCGRuntimeGraphResultV6 TryBuild(
		UPCGGraph* SourceRootGraph,
		UObject* TransientOuter,
		const FEFCalystoRoomThemeGenerationConfigV6& ResolvedConfig,
		bool bForceCookedRulesForAutomation = false);

	/** Validates that the graph closure contains exactly one V6 assignment node. */
	static bool ValidateRuntimeGraph(const UPCGGraph* RuntimeGraph, FString& OutError);
};

/**
 * Move-only, fail-closed gate for the one legal GenerateLocal request.
 * Creation sets the transient graph once; GenerateOnce can be consumed only once.
 */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGSingleGenerationGateV6 final
{
public:
	static TSharedPtr<FEFCalystoPCGSingleGenerationGateV6> CreateAndPrepare(
		UPCGComponent* Component,
		UPCGGraph* RuntimeGraph,
		FString& OutError);

	~FEFCalystoPCGSingleGenerationGateV6() = default;

	FEFCalystoPCGSingleGenerationGateV6(const FEFCalystoPCGSingleGenerationGateV6&) = delete;
	FEFCalystoPCGSingleGenerationGateV6& operator=(const FEFCalystoPCGSingleGenerationGateV6&) = delete;

	FPCGTaskId GenerateOnce(FString& OutError, bool bForce = false);
	bool IsConsumed() const { return bConsumed; }

private:
	FEFCalystoPCGSingleGenerationGateV6(UPCGComponent* InComponent, UPCGGraph* InRuntimeGraph);

	TWeakObjectPtr<UPCGComponent> Component;
	TWeakObjectPtr<UPCGGraph> RuntimeGraph;
	bool bConsumed = false;
};
