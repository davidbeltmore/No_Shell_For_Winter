#pragma once

#include "CoreMinimal.h"

#include "Calysto/EFCalystoDungeonTypes.h"

class AActor;
class UWorld;

struct FEFCalystoPopulationMaterializationResult
{
	bool bSucceeded = false;
	FEFCalystoRealizedFloorManifest Manifest;
	FString FailureReason;
};

/** Consumes transient Calysto anchors and realizes one immutable V3 manifest. */
class FEFCalystoPopulationMaterializer final
{
public:
	static FEFCalystoPopulationMaterializationResult Materialize(
		UWorld* World,
		AActor* DungeonActor,
		const FBox& DungeonBounds,
		const FEFCalystoResolvedFloorIntent& Intent);
};
