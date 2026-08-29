#pragma once

#include "CoreMinimal.h"

#include "Calysto/EFCalystoDungeonTypesV4.h"

class AActor;
class UWorld;

struct FEFCalystoPopulationMaterializationResultV4
{
	bool bSucceeded = false;
	FEFCalystoRealizedFloorManifestV4 Manifest;
	FString FailureReason;
};

/** Consumes transient Calysto anchors and realizes one immutable V4 manifest. */
class FEFCalystoPopulationMaterializerV4 final
{
public:
	static FEFCalystoPopulationMaterializationResultV4 Materialize(
		UWorld* World,
		AActor* DungeonActor,
		const FBox& DungeonBounds,
		const FEFCalystoResolvedFloorIntentV4& Intent);

	/** Fail-closed bridge lookup used by the PCG readiness boundary. */
	static bool ValidateCompanionRosterReady(
		UWorld* World,
		const FString& ExpectedCompanionSnapshotHash,
		FString& OutError);

	/** Idempotent transactional unwind used by post-materialization readiness failures. */
	static void RollbackMaterializedPopulation(
		UWorld* World,
		const FEFCalystoResolvedFloorIntentV4& Intent);
};
