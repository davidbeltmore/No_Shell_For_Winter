#pragma once

#include "CoreMinimal.h"

class AActor;

/** Result of the fail-closed Calysto adaptation performed before PCG generation. */
struct FEFCalystoPCGAdapterResult
{
	bool bApplied = false;
	bool bGetPiecesShapeInvoked = false;
	int32 PCGSeed = 0;
	int32 UpdatedAnchorEntries = 0;
	int32 UpdatedThemeEntries = 0;
	FString FailureReason;
};

/**
 * Runtime-only bridge between the project-owned Calysto harness and BP_MassiveDungeon.
 *
 * This adapter never edits a Calysto package. It validates the reflected schema, stages
 * transient duplicates, applies a frozen V4 floor intent, and then invokes Calysto's exact
 * zero-parameter GetPiecesShape boundary. It never generates PCG itself.
 */
class FEFCalystoPCGAdapter final
{
public:
	static FEFCalystoPCGAdapterResult TryApply(AActor* DungeonActor);
};
