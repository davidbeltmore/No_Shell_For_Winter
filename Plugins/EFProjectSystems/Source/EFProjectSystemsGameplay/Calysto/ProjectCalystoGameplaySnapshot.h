#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentGameplayBridge.h"
#include "UObject/GCObject.h"

class UProjectRunCompanionSubsystem;
class UProjectCalystoFloorOutcomeSubsystem;

/** Caller supplies already owned, inactive native destination actors; this API never spawns companions. */
struct FProjectCalystoInventoryDestination
{
	FGuid CompanionId; // Invalid only for the player.
	bool bCorpse = false;
	AActor* Actor = nullptr;
};

/** A destination binding requirement is derived from the captured object graph,
 * never from a display name or catalog identifier. The project provider owns
 * creation of the already-loaded destination actor, while snapshot restoration
 * only consumes the exact bindings. */
struct FProjectCalystoInventoryDestinationRequirement
{
	FGuid CompanionId; // Invalid only for the player.
	bool bCorpse = false;
};

/** Exact same-world pre-floor state. Capture never calls legacy travel, serialization or RNG.
 * The request ID owns the lease only; it is excluded from the canonical gameplay hash.
 * Missing original owners/equipment topology are unrecoverable rollback failures, not clean release. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoGameplaySnapshot final
	: public IEFCalystoGameplaySnapshot, public FGCObject
{
public:
	static TSharedPtr<FProjectCalystoGameplaySnapshot> Capture(FGuid RequestId, UWorld* World, AActor* Player,
		UProjectRunCompanionSubsystem* Roster, UProjectCalystoFloorOutcomeSubsystem* Outcomes, FString& Error);
	virtual ~FProjectCalystoGameplaySnapshot() override;
	virtual FString GetCanonicalHash() const override;
	virtual bool IsDetachedForTravel() const override;
	bool VerifyUnchanged(FGuid RequestId, FString& Error) const;
	/** Validates every owner/graph first, restores unpublished data without events, then re-verifies. */
	bool Restore(FGuid RequestId, FString& Error);
	/** Must follow VerifyUnchanged/Restore before a rejected attempt is declared clean. */
	bool Release(FGuid RequestId, FString& Error);
	/** Read-only preparation that freezes equipped intent and its native actor projection for transactional reconstruction. */
	bool PrepareForTravel(FGuid RequestId, FString& Error);
	/** Explicit actual-travel boundary: verify again, then drop every source-world reference. */
	bool DetachForTravel(FGuid RequestId, FString& Error);
	/** Read-only detached-lease proof used by the destination coordinator before
	 * it creates any travel projections or writes an inventory. */
	bool ValidateDestinationLease(FGuid RequestId, FString& Error) const;
	bool GetDestinationRequirements(FGuid RequestId,
		TArray<FProjectCalystoInventoryDestinationRequirement>& OutRequirements, FString& Error) const;
	/** Reconstruct the immutable graph after clearing a validated map-default destination inventory through ACF's public lifecycle. */
	bool ReconstructInWorld(FGuid RequestId, UWorld* World,
		TConstArrayView<FProjectCalystoInventoryDestination> Destinations, FString& Error);
	/** Coordinator-only acceptance boundary. Verifies current owned state, releases without restoring old outcomes. */
	bool ReleaseAccepted(FGuid RequestId, FString& Error);
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("ProjectCalystoGameplaySnapshot"); }
private:
	FProjectCalystoGameplaySnapshot();
	struct FState;
	TUniquePtr<FState> State;
	friend class FProjectCalystoGameplaySnapshotTest;
};
