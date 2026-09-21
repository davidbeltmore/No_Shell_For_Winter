#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentReservationPlanner.h"
#include "Calysto/EFCalystoArchitecturePlanner.h"
#include "Calysto/EFCalystoDecalReservation.h"
#include "Calysto/EFCalystoFloorTransaction.h"
#include "Calysto/EFCalystoNativeAdapter.h"
#include "Calysto/EFCalystoStructuralNavigation.h"

/** Spatial resolution and finite QA work, never probabilities or post-selection truncation. */
struct FEFCalystoSurfaceCandidateLimits
{
	double FloorGridSpacingCm = 100.0;
	int32 MaximumFaces = 16384;
	/** Complete 100 cm floor-lattice capacity for the supported V7 floor sizes.
	 * A larger complete set fails rather than being truncated or ranked away. */
	int32 MaximumCandidates = 16384;
	/** Matches the decal reservation input ceiling. A larger complete set fails rather than being truncated. */
	int32 MaximumDecalOpportunities = 4096;
	/** Complete compatibility is retained through reservation; this ceiling is
	 * deliberately shared with the runtime content planner default. */
	int32 MaximumCompatiblePairs = 32768;
	int32 MaximumPhysicsQueries = 65536;
	int32 MaximumNavigationPolygons = 8192;
	int64 MaximumWorkUnits = 1000000;
};

/** All references must be the same attempt's frozen native output and accepted navigation observation.
 * Sources come from GatherNativeSources; this consumer never discovers unrelated world components. */
struct FEFCalystoSurfaceCandidateRequest
{
	const FEFCalystoCompiledDirector* Configuration = nullptr;
	const FEFCalystoNativeRoomConfig* RoomConfiguration = nullptr;
	const FEFCalystoNativeResult* Native = nullptr;
	const FEFCalystoStructuralNavigationInput* NavigationInput = nullptr;
	const FEFCalystoStructuralNavigationResult* Navigation = nullptr;
	/** Borrowed from the attempt's retained dependency leases. No load or global object lookup occurs. */
	TConstArrayView<UObject*> LoadedArchitectureResources;
	/** Same frozen cooldown scope supplied to the architecture planner. */
	TArray<FGuid> ArchitectureCoolingDownIds;
	FEFCalystoSurfaceCandidateLimits Limits;
};

struct FEFCalystoSurfaceSupportReceipt
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	int32 InstanceIndex = INDEX_NONE;
	FTransform InstanceTransform;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

struct FEFCalystoSurfaceCandidateReceipt
{
	FGuid CandidateId;
	FBox SweptWorldBounds = FBox(ForceInit);
	TArray<FEFCalystoSurfaceSupportReceipt> Supports;
	uint64 NavigationPolygon = 0;
};

/** The decal opportunity carries one exact center-support binding for the pool. This receipt retains
 * the stronger proof: every point of the largest possible square lies on actual collision, including
 * adjacent native instances. It is feasibility evidence only; it never selects a decal. */
struct FEFCalystoDecalSupportReceipt
{
	FGuid OpportunityId;
	FBox FootprintWorldBounds = FBox(ForceInit);
	TArray<FEFCalystoSurfaceSupportReceipt> Supports;
};

struct FEFCalystoArchitectureMeshSupportReceipt
{
	FGuid ChildId;
	TWeakObjectPtr<UStaticMesh> Mesh;
	FBox ActualLocalBounds = FBox(ForceInit);
	FBox SweptWorldBounds = FBox(ForceInit);
};

struct FEFCalystoArchitectureSupportReceipt
{
	FGuid OpportunityId;
	FGuid EntryId;
	FTransform NativeTransform;
	FBox SweptWorldBounds = FBox(ForceInit);
	TArray<FEFCalystoArchitectureMeshSupportReceipt> Meshes;
	TArray<FEFCalystoSurfaceSupportReceipt> Supports;
};

struct FEFCalystoSurfaceCandidateResult
{
	FName FailureCode;
	FString Message;
	/** First finite compatibility rejection, for the on-demand inspector; never changes selection. */
	FString FirstArchitectureRejection;
	/** False Build may request another bounded observation of actual pending navigation work.
	 * Failure is meaningful only when Build is false and bPending is false. No partial output survives. */
	bool bPending = false;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Configuration;
	TArray<FEFCalystoContentRoom> Rooms;
	TArray<FEFCalystoContentSurface> Surfaces;
	TArray<FEFCalystoSurfaceCandidateReceipt> Receipts;
	TArray<FEFCalystoArchitectureOpportunity> ArchitectureOpportunities;
	TArray<FEFCalystoArchitectureSupportReceipt> ArchitectureReceipts;
	/** Exact native Floor/Wall/Roof opportunities that can support the resolved maximum decal footprint. */
	TArray<FEFCalystoDecalOpportunity> DecalOpportunities;
	TArray<FEFCalystoDecalSupportReceipt> DecalReceipts;
	int64 WorkUnits = 0;
	int32 PhysicsQueries = 0;
	int32 NavigationPolygons = 0;
	int32 CookedCollisionTriangles = 0;
	int32 CookedConvexPatches = 0;
	int32 RejectedGeometry = 0;
	int32 RejectedProtection = 0;
	int32 RejectedNavigation = 0;
	/** First complete pre-selection rejection for each placement zone. This is
	 * diagnostic-only and never influences catalog order, selection or retries. */
	TMap<EEFCalystoPlacementZone, FName> FirstCandidateRejectionByZone;
	int32 RejectedDecalGeometry = 0;
	int32 RejectedDecalProtection = 0;
};

/** Game-thread read-only feasibility. False clears every candidate/receipt; no RNG, loads, spawning,
 * navigation rebuilding, or global scans. Supports planar simple box/convex faces and bounded
 * convex patches formed by the exact union of connected coplanar cooked collision triangles.
 * Architecture and decals prove complete swept-footprint coverage by bounded coplanar face subtraction,
 * including adjacent native instances, and trace each contributing face. Gaps are never hull-filled.
 * Whole swept envelopes deliberately reject some physically valid complex/curved placements.
 * These receipts expire when the accepted structural world changes; they are not a permanent lease. */
struct FEFCalystoSurfaceCandidates final
{
	static bool Build(const FEFCalystoSurfaceCandidateRequest& Request, FEFCalystoSurfaceCandidateResult& Result);
};
