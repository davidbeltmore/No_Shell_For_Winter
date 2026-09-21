#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class AActor;

/** Result of the fail-closed Calysto adaptation performed before PCG generation. */
struct FEFCalystoPCGAdapterResult
{
	bool bApplied = false;
	bool bGetPiecesShapeInvoked = false;
	int32 PCGSeed = 0;
	int32 UpdatedAnchorEntries = 0;
	int32 UpdatedThemeEntries = 0;
	int32 UpdatedDungeonMaterialSlots = 0;
	int32 UpdatedThemeMaterialEntries = 0;

	/** One runtime-only Calysto room architecture object per reachable V6 Theme. */
	TMap<FName, TObjectPtr<UObject>> ThemeRoomTypes;

	/**
	 * PCG metadata does not currently participate in UObject reference collection.
	 * Keep every synthesized Room Type alive until the owning floor runtime state ends.
	 */
	TArray<TStrongObjectPtr<UObject>> RuntimeStrongReferences;
	FString FailureReason;
};

/**
 * Runtime-only bridge between the project-owned Calysto harness and BP_MassiveDungeon.
 *
 * This adapter never edits a Calysto package. It validates the reflected schema, stages
 * transient duplicates, applies the frozen V6 floor intent (including Style and
 * reachable room-Theme materials), and then invokes Calysto's exact
 * zero-parameter GetPiecesShape boundary. It never generates PCG itself.
 */
class FEFCalystoPCGAdapter final
{
public:
	static FEFCalystoPCGAdapterResult TryApply(AActor* DungeonActor);
};
