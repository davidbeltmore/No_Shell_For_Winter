#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentReservationPlanner.h"
#include "Calysto/EFCalystoFloorTransaction.h"

class AActor;
class UWorld;

/** The project supplies a real immutable roster/inventory/outcome snapshot, never an empty placeholder hash. */
class EFPROCEDURALRUNTIME_API IEFCalystoGameplaySnapshot
{
public:
	virtual ~IEFCalystoGameplaySnapshot() = default;
	virtual FString GetCanonicalHash() const = 0;
	/** True only between source-world detachment and successful destination
	 * reconstruction. It is a lifecycle fact, never a probability input. */
	virtual bool IsDetachedForTravel() const = 0;
};

/** Selected gameplay mathematics belongs before realization; this layer never rolls another level. */
struct EFPROCEDURALRUNTIME_API FEFCalystoFrozenActorGameplay
{
	FGuid ReservationId;
	int32 LogicalLevel = 1;
	int32 PhysicalLevel = 1;
	FGuid CompanionIdentity;
};

/** Explicit content material binding. Structural Floor/Wall/Roof materials remain the native adapter's evidence. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentMaterialExpectation
{
	FGuid ReservationId;
	FName ComponentName;
	int32 Slot = 0;
	FSoftObjectPath Material;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoContentGameplayContext
{
	FEFCalystoAttemptToken Token;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> AttemptOwner;
	TSharedPtr<const FEFCalystoReservedContentManifest> Manifest;
	TSharedPtr<const IEFCalystoGameplaySnapshot> PreFloorSnapshot;
	int64 FloorNumber = 1;
	TMap<FGuid, FEFCalystoFrozenActorGameplay> Actors;
	TArray<FEFCalystoContentMaterialExpectation> Materials;
	/** Exact pre-existing containers, if the bridge supports reversible insertion into them. */
	TMap<FGuid, TWeakObjectPtr<AActor>> ExistingContainers;
	double DeadlineSeconds = 0;
};

enum class EEFCalystoGameplayObservation : uint8 { Pending, Verified, Failed };
enum class EEFCalystoContentReleaseIntent : uint8 { RejectedAttempt, AcceptedFloorExit };

/** A project-native verifier supplies binding evidence, not a generic success Boolean. */
struct EFPROCEDURALRUNTIME_API FEFCalystoGameplayElementEvidence
{
	FEFCalystoAttemptToken Token;
	FGuid ReservationId;
	FGuid EntryId;
	FGuid ParentContainerId;
	int32 InventorySlot = INDEX_NONE;
	FSoftObjectPath Payload;
	FString SnapshotHash;
	/** Canonical actual native state: level/scaling, typed NPC identity, or exact stored item identity/count. */
	FString NativeStateHash;
	EEFCalystoGameplayObservation State = EEFCalystoGameplayObservation::Pending;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Configuration;
	FString Message;
	bool Matches(const FEFCalystoContentGameplayContext& Context, const FEFCalystoReservedContent& Reservation,
		FString& Error) const;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoGameplayCommitReceipt
{
	FEFCalystoAttemptToken Token;
	FString ManifestHash;
	FString PreFloorSnapshotHash;
	FString PreparedStateHash;
	TSet<FGuid> PreparedElements;
	bool Matches(const FEFCalystoContentGameplayContext& Context, FString& Error) const;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoGameplayReleaseEvidence
{
	FEFCalystoAttemptToken Token;
	int32 PendingCallbacks = 0;
	int32 OwnedObjects = 0;
	int32 SnapshotLeases = 0;
	bool bSafeToDestroyActors = false;
	bool bPersistentStateVerified = false;
	/** Valid only on accepted floor exit. The bridge proves each exact actor now belongs to persistent gameplay. */
	TMap<FGuid, TWeakObjectPtr<AActor>> TransferredActors;
	bool IsComplete() const;
};

/** Implement in EFProjectSystems using its direct native APIs. No V6 conversion is permitted.
 * All calls run on the game thread, are token scoped, and must tolerate repeated observation/release.
 * Preflight rejects unsupported classes, auto-spawning construction/BeginPlay behavior, container
 * capacity, or unavailable gameplay infrastructure before any selected actor is created. */
class EFPROCEDURALRUNTIME_API IEFCalystoContentGameplayBridge
{
public:
	virtual ~IEFCalystoContentGameplayBridge() = default;
	/** Read-only: validate the actual snapshot and selected contracts; add exact selected native dependencies. */
	virtual bool Preflight(const FEFCalystoContentGameplayContext& Context,
		TArray<FSoftObjectPath>& AdditionalDependencies, FString& Error) const = 0;
	/** Reversible actor-local preparation, including exact selected chest contents before BeginPlay.
	 * Must suppress AI, interactions, timers and untracked spawning until coordinator acceptance. */
	virtual bool PrepareDeferredActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor,
		TConstArrayView<FEFCalystoReservedContent> ContainerContents, FString& Error) = 0;
	/** Optional supported existing-container contract. Must keep exact original storage for rollback. */
	virtual bool StageExistingContainer(const FEFCalystoContentGameplayContext& Context, FGuid ContainerId,
		AActor* Container, TConstArrayView<FEFCalystoReservedContent> Contents, FString& Error) = 0;
	/** Idempotent reversible native initialization after FinishSpawning. Delayed BeginPlay stays Pending.
	 * This explicit mutation step is separate from read-only ObserveActor evidence. */
	virtual EEFCalystoGameplayObservation FinalizeSpawnedActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor, FString& Error) = 0;
	virtual FEFCalystoGameplayElementEvidence ObserveActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor) const = 0;
	virtual FEFCalystoGameplayElementEvidence ObserveInventoryItem(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Container) const = 0;
	/** Prepare exact unpublished gameplay changes. Pending is unfinished work, never a reroll. */
	virtual EEFCalystoGameplayObservation PrepareCommit(const FEFCalystoContentGameplayContext& Context,
		FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) = 0;
	/** Atomic, bounded, allocation/load/spawn-free publication. Failure leaves the original snapshot intact.
	 * Success remains reversible and emits no external outcomes/recruitment events until ConfirmAccepted.
	 * The coordinator calls this only in the validated actual-player-release operation. */
	virtual bool CommitPrepared(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) = 0;
	/** Native activation after irreversible coordinator acceptance; no loading, spawning or deferred work.
	 * InvariantFailure preserves accepted persistent state and requires accepted cleanup.
	 * AcceptedExitRequested requires an actual coordinator exit/cancellation request that has already
	 * initiated exact-token release; returning the enum alone cannot authorize an exit.
	 * The materializer invokes this at most once, including failure or reentrant release. */
	virtual EEFCalystoActivationResult ConfirmAccepted(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) = 0;
	/** Cancel token callbacks before returning; restore rejected staged state without outcome broadcasts.
	 * Accepted floor cleanup retains accepted outcomes and proves any companion ownership transfers. */
	virtual void BeginRelease(const FEFCalystoContentGameplayContext& Context, EEFCalystoContentReleaseIntent Intent) = 0;
	virtual FEFCalystoGameplayReleaseEvidence ObserveRelease(const FEFCalystoContentGameplayContext& Context,
		EEFCalystoContentReleaseIntent Intent) const = 0;
};
