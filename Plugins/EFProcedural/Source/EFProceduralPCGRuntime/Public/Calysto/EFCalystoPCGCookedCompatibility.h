#pragma once

#include "CoreMinimal.h"

class UObject;
class UPCGGraph;

/** Result of building the packaged-runtime-only Calysto graph view. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGCookedCompatibilityResult
{
	UPCGGraph* RuntimeGraph = nullptr;
	FString FailureReason;
	int32 ClonedGraphCount = 0;
	int32 RelinkedInternalClosureCount = 0;
	int32 ValidatedInternalGraphCount = 0;
	int32 InvalidatedCookedCompilationDataCount = 0;
	bool bApplied = false;
};

/**
 * Builds the one transient Master graph required by cooked games without
 * mutating Calysto assets.
 *
 * Calysto's shared ST_ObjectSimple helper selects friendly UserDefinedStruct field
 * names (for example "Object Transform"). Those friendly names are editor data;
 * cooked reflection exposes the deterministic authored fallback ("ObjectTransform").
 * Two cooked-safe, project-owned internal graphs freeze the reviewed vendor
 * SetDungeonMesh/AddRamps topology and use Calysto's equivalent dungeon helper.
 * This adapter validates that resident closure and reconnects only Master's exact
 * Subgraph_43 call. The V6 compositor independently owns the transient Shape clone.
 */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGCookedCompatibility final
{
public:
	static FEFCalystoPCGCookedCompatibilityResult TryBuild(
		UPCGGraph* SourceRootGraph,
		UObject* TransientOuter,
		bool bForceCookedRulesForAutomation = false);

	/** Find-only validation used by runtime and the idempotent Editor creator. */
	static bool ValidateResidentInternalClosure(FString& OutError);

#if WITH_EDITOR
	/**
	 * Converts two fresh vendor duplicates into the reviewed internal closure.
	 * Both objects must still be exact, unpatched duplicates. No package is saved.
	 */
	static bool PrepareInternalClosureForEditor(
		UPCGGraph* InternalSetDungeonMesh,
		UPCGGraph* InternalAddRamps,
		FString& OutError);
#endif
};
