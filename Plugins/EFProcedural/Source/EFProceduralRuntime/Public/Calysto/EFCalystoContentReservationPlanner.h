#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"

/** Geometry/collision/navigation proof is supplied by the native placement adapter.
 * This immutable input is never interpreted as permission to search or spawn in a world. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentSurface
{
	FGuid Id;
	int64 RoomId = 0;
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	FTransform Transform = FTransform::Identity;
	FVector Normal = FVector::UpVector;
	/** Validated free half extent in Transform coordinates, including the supported jitter region. */
	FVector AvailableHalfExtent = FVector::ZeroVector;
	double AvailableClearanceCm = 0.0;
	TSet<EEFCalystoGameplayRole> AllowedRoles;
	/** Empty is an empty compatibility set, never an implicit wildcard. */
	TSet<FGuid> CompatibleEntryIds;
	bool bCollisionValidated = false;
	/** The loaded ActorClass CDO collision contract was resolved before Chance/Amount/Weight. */
	bool bCollisionContractValidated = false;
	/** Actor-local conservative reservation envelope, including authored clearance but excluding jitter. */
	FBox ReservedLocalBounds = FBox(ForceInit);
	/** Hash of the exact loaded ActorClass CDO root-collision descriptor. */
	FString CollisionContractHash;
	/** Must cover the entire placement/jitter region when an entry requires navigation. */
	bool bNavigationValidated = false;
	bool bProtectsDoorway = false;
	bool bProtectsMainRoute = false;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoContentRoom
{
	int64 RoomId = 0;
	FGuid ThemeId;
	EEFCalystoProtectedRoom Protection = EEFCalystoProtectedRoom::None;
	TSet<EEFCalystoGameplayRole> AllowedRoles;
};

/** Validated inventory schema; capacity is not an Amount distribution. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContainerCapacity
{
	int32 Slots = 0;
	TSet<FGuid> CompatibleEntryIds;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoExistingContainer
{
	FGuid ContainerId;
	int64 RoomId = 0;
	FEFCalystoContainerCapacity Capacity;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoReservedSpace
{
	FGuid CandidateId;
	FBox WorldBounds = FBox(ForceInit);
	double SpacingCm = 0.0;
};

/** Shared usage is supplied before planning and returned only with an accepted manifest. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentBudgetUsage
{
	int32 Actors = 0;
	double Threat = 0.0;
	double Resources = 0.0;
	TMap<EEFCalystoBudgetMembership, int32> Buckets;
	TMap<EEFCalystoGameplayRole, int32> Categories;
	TMap<FGuid, int32> Entries;
	TMap<FGuid, int32> ScopedCategories;
	TMap<FGuid, int32> ScopedEntries;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoContentPlannerLimits
{
	int32 MaximumRooms = 2048;
	/** Matches the complete native 100 cm floor lattice supported by the V7
	 * surface adapter. This is a finite contract, never a post-selection cap. */
	int32 MaximumSurfaces = 16384;
	int32 MaximumEntriesPerGroup = 256;
	/** Matches the complete V7 surface compatibility ceiling. Entries are never
	 * truncated after feasibility has been established. */
	int32 MaximumCompatiblePairs = 32768;
	int32 MaximumAmount = 32;
	int32 MaximumReservations = 512;
	int32 MaximumContainers = 256;
	/** Counts compatibility/search/spacing work across the complete floor; no wall-clock polling. */
	int64 MaximumWorkUnits = 500000;
};

/** Result of the attempt-owned exact world predicate for an already deterministic
 * placement transform. The planner invokes it before Chance/Amount/Weight; it
 * never changes a transform or turns an unavailable class into another entry. */
enum class EEFCalystoExactPlacementPreflight : uint8
{
	Feasible,
	SpatiallyBlocked,
	Invalid
};

struct EFPROCEDURALRUNTIME_API FEFCalystoContentReservationRequest
{
	FEFCalystoRandomKey Random;
	TArray<FEFCalystoContentRoom> Rooms;
	TArray<FEFCalystoContentSurface> Surfaces;
	TArray<FEFCalystoExistingContainer> ExistingContainers;
	/** Required for each eligible world-container entry. Newly reserved containers create finite slot opportunities. */
	TMap<FGuid, FEFCalystoContainerCapacity> ContainerCapacities;
	TSet<FGuid> BlockedEntryIds;
	TSet<FGuid> GraveyardEligibleEntryIds;
	/** Last accepted prior floor. A cooldown of K excludes the next K floors. */
	TMap<FGuid, int64> LastSelectedFloor;
	TArray<FEFCalystoReservedSpace> ExistingSpace;
	FEFCalystoContentBudgetUsage InitialUsage;
	/** An authored nonzero ResourceCost requires an explicit finite resource budget. */
	TOptional<double> ResourceBudget;
	/** Captured once by the caller with the immutable floor request, never inferred from labels or live state.
	 * Required only by a matching enabled Snapshot binding; disabled adaptation ignores it exactly. */
	TOptional<FEFCalystoTraits> TraitSnapshot;
	/** Production world requests supply this read-only CDO predicate so the exact
	 * jittered transform receives the same collision preflight as deferred spawn.
	 * Pure fixture requests may omit it unless bRequireExactPlacementPreflight is set. */
	bool bRequireExactPlacementPreflight = false;
	TFunction<EEFCalystoExactPlacementPreflight(const FEFCalystoContentEntry& Entry,
		const FTransform& Transform, const FString& CollisionContractHash, FString& OutError)> ExactPlacementPreflight;
	FEFCalystoContentPlannerLimits Limits;
};

enum class EEFCalystoContentOpportunityOutcome : uint8
{
	IneligibleRoom, NoCompatibleEntries, NoFeasibleAmount, ChanceAbsent, Reserved
};

struct EFPROCEDURALRUNTIME_API FEFCalystoContentOpportunityReport
{
	FGuid OpportunityId;
	int64 RoomId = 0;
	FGuid GroupId;
	FGuid ContainerId;
	EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Prop;
	EEFCalystoContentOpportunityOutcome Outcome = EEFCalystoContentOpportunityOutcome::NoCompatibleEntries;
	double RequestedChancePercent = 0.0;
	double EffectiveChancePercent = 0.0;
	double ChanceMultiplier = 1.0;
	/** Effective weights used before feasibility and conditional entry selection; authored payloads stay intact. */
	TArray<FEFCalystoWeightedAlternative> EffectiveEntryWeights;
	/** PMF mass removed by feasibility is reported; it is not another Chance roll. */
	double FeasibleAmountProbabilityMass = 0.0;
	TArray<int32> FeasibleAmounts;
	bool bChanceRolled = false;
	int32 SelectedAmount = 0;
	int32 ReservedAmount = 0;
	FName Reason;
};

enum class EEFCalystoContentPlanningStatus : uint8 { Complete, InvalidConfiguration, WorkLimitExceeded };

struct EFPROCEDURALRUNTIME_API FEFCalystoContentPlanningReport
{
	EEFCalystoContentPlanningStatus Status = EEFCalystoContentPlanningStatus::InvalidConfiguration;
	FName FailureCode;
	FString Message;
	int64 WorkUnits = 0;
	TArray<FEFCalystoContentOpportunityReport> Opportunities;
};

/** A frozen request, not a spawned actor or a successful realization. */
struct EFPROCEDURALRUNTIME_API FEFCalystoReservedContent
{
	FGuid Id;
	FGuid OpportunityId;
	FGuid CandidateId;
	FGuid GroupId;
	FGuid ScopeId;
	FGuid ParentContainerId;
	int32 InventorySlot = INDEX_NONE;
	int64 RoomId = 0;
	FGuid ThemeId;
	EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Prop;
	EEFCalystoBudgetMembership Budget = EEFCalystoBudgetMembership::None;
	FEFCalystoContentEntry Entry;
	FTransform Transform = FTransform::Identity;
	FBox ReservedBounds = FBox(ForceInit);
	FString CollisionContractHash;
};

/** No mutating getters: a materializer consumes precisely these selected identities and payloads. */
class EFPROCEDURALRUNTIME_API FEFCalystoReservedContentManifest final
{
public:
	bool IsValid() const { return bValid; }
	const TArray<FEFCalystoReservedContent>& GetElements() const { return Elements; }
	const TArray<FSoftObjectPath>& GetSelectedDependencies() const { return Dependencies; }
	const FEFCalystoContentBudgetUsage& GetFinalUsage() const { return FinalUsage; }
	const FString& GetHash() const { return Hash; }
private:
	friend struct FEFCalystoContentReservationPlanner;
	bool bValid = false;
	TArray<FEFCalystoReservedContent> Elements;
	TArray<FSoftObjectPath> Dependencies;
	FEFCalystoContentBudgetUsage FinalUsage;
	FString Hash;
};

/** Pure bounded reservation. False invalidates the whole output; no selected object is truncated.
 * Amount feasibility is established before Chance. Each subsequent weighted choice retains
 * a proved completion for the remaining count. No universal O(1) claim is made. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentReservationPlanner final
{
	static bool Build(const FEFCalystoCompiledDirector& Configuration,
		const FEFCalystoContentReservationRequest& Request,
		FEFCalystoReservedContentManifest& OutManifest, FEFCalystoContentPlanningReport& OutReport);
	/** Stable keys used when supplying existing shared usage; labels/order are deliberately absent. */
	static FGuid ScopeIdentity(FGuid StyleId, FGuid ThemeId, FGuid GroupId);
	static FGuid ScopedEntryIdentity(FGuid ScopeId, FGuid EntryId);
};
