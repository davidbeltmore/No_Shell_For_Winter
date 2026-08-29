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
	int32 ReplacedLegacySubgraphCount = 0;
	int32 InvalidatedCookedCompilationDataCount = 0;
	bool bApplied = false;
};

/**
 * Builds a transient graph chain for cooked games without mutating Calysto assets.
 *
 * Calysto's shared ST_ObjectSimple helper selects friendly UserDefinedStruct field
 * names (for example "Object Transform"). Those friendly names are editor data;
 * cooked reflection exposes the deterministic authored fallback ("ObjectTransform").
 * Calysto already ships an otherwise-equivalent dungeon helper for those cooked
 * names. This adapter reconnects only the affected transient graph instances.
 */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoPCGCookedCompatibility final
{
public:
	static FEFCalystoPCGCookedCompatibilityResult TryBuild(
		UPCGGraph* SourceRootGraph,
		UObject* TransientOuter,
		bool bForceCookedRulesForAutomation = false);
};
