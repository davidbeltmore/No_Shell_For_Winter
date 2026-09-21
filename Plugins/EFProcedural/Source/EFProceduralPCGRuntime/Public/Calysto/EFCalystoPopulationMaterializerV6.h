#pragma once

#include "CoreMinimal.h"

#include "Calysto/EFCalystoPopulationPlannerV6.h"

class AActor;
class UWorld;
struct FPCGDataCollection;

/** Bounded placement controls; all defaults are runtime-safe centimeters. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPopulationMaterializationOptionsV6
{
	float RoomBoundsTolerance = 75.0f;
	float MinimumAnchorSpacing = 120.0f;
	FVector NavigationProjectionExtent = FVector(250.0f, 250.0f, 900.0f);
	int32 MaximumAnchorRecords = 4096;
};

/** One lightweight placement point routed from Calysto's native room-point generator. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPlacementCandidateV6
{
	int64 StableRoomId = 0;
	EEFCalystoPlacementZoneV6 Zone = EEFCalystoPlacementZoneV6::Floor;
	FString CandidateId;
	FTransform Transform = FTransform::Identity;
};

/** One verified actor realization from an immutable V6 decision. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoRealizedPopulationActorV6
{
	FString DecisionId;
	int64 StableRoomId = 0;
	FName CategoryId = NAME_None;
	FString AnchorId;
	FTransform Transform = FTransform::Identity;
	TWeakObjectPtr<AActor> Actor;
	TArray<FString> VerifiedChestContentDecisionIds;
};

/** Transactional result returned to the PCG readiness boundary. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPopulationMaterializationResultV6
{
	bool bSucceeded = false;
	bool bAlreadyApplied = false;
	int32 CandidateAnchorCount = 0;
	int32 SpawnedActorCount = 0;
	int32 VerifiedChestContentCount = 0;
	FString PopulationHash;
	FString MaterializationHash;
	FString FailureReason;
	TArray<FEFCalystoRealizedPopulationActorV6> RealizedActors;
};

/**
 * One-shot V6 population realization.
 *
 * The caller freezes the room manifest and population plan, asynchronously
 * loads GatherRequiredPreloadPaths(), waits for navigation, then invokes
 * Materialize exactly once. No method in this type performs an asset load.
 */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoPopulationMaterializerV6 final
{
public:
	/** Consume only requested native wall/corner/roof zones, once after PCG completes. */
	static bool GatherNativePlacementCandidates(
		const FPCGDataCollection& Output,
		const FTransform& DungeonTransform,
		const FEFCalystoRoomManifestV6& Manifest,
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FEFCalystoPlacementCandidateV6>& OutCandidates,
		FString& OutError);

	/** Exact selected class closure plus gameplay-bridge dependencies. */
	static bool GatherRequiredPreloadPaths(
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FSoftObjectPath>& OutPaths,
		FString& OutError);

	/** Pure structural validation, safe for focused automation. */
	static bool ValidatePlan(
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationPlanV6& Plan,
		FString& OutError);

	/** Find-only residency gate; never performs a blocking asset load. */
	static bool ValidatePreloadClosureResident(
		const FEFCalystoPopulationPlanV6& Plan,
		FString& OutError);

	static FEFCalystoPopulationMaterializationResultV6 Materialize(
		UWorld* World,
		AActor* DungeonActor,
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationMaterializationOptionsV6& Options =
			FEFCalystoPopulationMaterializationOptionsV6());

	/** Placement-aware overload. Floor keeps the proven NavMesh-anchor path. */
	static FEFCalystoPopulationMaterializationResultV6 Materialize(
		UWorld* World,
		AActor* DungeonActor,
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationPlanV6& Plan,
		TConstArrayView<FEFCalystoPlacementCandidateV6> PlacementCandidates,
		const FEFCalystoPopulationMaterializationOptionsV6& Options =
			FEFCalystoPopulationMaterializationOptionsV6());

	/** Idempotent transactional unwind for a readiness failure after success. */
	static void RollbackMaterializedPopulation(
		UWorld* World,
		AActor* DungeonActor,
		const FEFCalystoPopulationPlanV6& Plan);
};
