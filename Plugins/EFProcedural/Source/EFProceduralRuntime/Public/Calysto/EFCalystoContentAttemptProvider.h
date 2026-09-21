#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentGameplayBridge.h"
#include "Features/IModularFeatures.h"

class AActor;
class UWorld;

/**
 * Neutral, request-owned bridge factory.  EFProcedural owns the transaction and
 * cannot depend on project gameplay modules; EFProjectSystems registers exactly
 * one implementation that supplies the real snapshot and typed native bridge.
 */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentAttemptProviderRequest
{
	FEFCalystoContentAttemptProviderRequest();
	~FEFCalystoContentAttemptProviderRequest();
	FEFCalystoContentAttemptProviderRequest(const FEFCalystoContentAttemptProviderRequest&);
	FEFCalystoContentAttemptProviderRequest(FEFCalystoContentAttemptProviderRequest&&);
	FEFCalystoContentAttemptProviderRequest& operator=(const FEFCalystoContentAttemptProviderRequest&);
	FEFCalystoContentAttemptProviderRequest& operator=(FEFCalystoContentAttemptProviderRequest&&);

	FEFCalystoAttemptToken Token;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> AttemptOwner;
	TSharedPtr<const FEFCalystoCompiledDirector> Configuration;
	FEFCalystoRandomKey Random;
	int64 FloorNumber = 1;
	double DeadlineSeconds = 0.0;
	/** Retained across spatial retries for the same request only. */
	TSharedPtr<IEFCalystoGameplaySnapshot> ExistingSnapshot;
};

/** Values captured before planning and materialization.  These are inputs, never
 * implicit post-selection adjustments. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentAttemptProviderResult
{
	FEFCalystoContentAttemptProviderResult();
	~FEFCalystoContentAttemptProviderResult();
	FEFCalystoContentAttemptProviderResult(const FEFCalystoContentAttemptProviderResult&);
	FEFCalystoContentAttemptProviderResult(FEFCalystoContentAttemptProviderResult&&);
	FEFCalystoContentAttemptProviderResult& operator=(const FEFCalystoContentAttemptProviderResult&);
	FEFCalystoContentAttemptProviderResult& operator=(FEFCalystoContentAttemptProviderResult&&);

	TSharedPtr<IEFCalystoGameplaySnapshot> PreFloorSnapshot;
	TSharedPtr<IEFCalystoContentGameplayBridge> GameplayBridge;
	FEFCalystoContentBudgetUsage InitialUsage;
	TMap<FGuid, int64> LastSelectedFloor;
	TSet<FGuid> BlockedEntryIds;
	TSet<FGuid> GraveyardEligibleEntryIds;
	TOptional<double> ResourceBudget;
	TOptional<FEFCalystoTraits> TraitSnapshot;
};

/**
 * Project implementation of V7 content state.  The runtime uses this only
 * after structural navigation and surface feasibility have settled.  It must
 * never translate V7 selections into V6 plans or choose replacement content.
 */
class EFPROCEDURALRUNTIME_API IEFCalystoContentAttemptProvider : public IModularFeature
{
public:
	virtual ~IEFCalystoContentAttemptProvider() = default;

	static FName GetModularFeatureName()
	{
		static const FName Name(TEXT("EFCalystoContentAttemptProvider"));
		return Name;
	}

	/** Capture occurs in the source game world before a floor transaction exists.
	 * RequestId is the routing identity later passed to FEFCalystoFloorTransaction;
	 * it is never a random seed. */
	virtual bool CapturePreTravelSnapshot(const FGuid& RequestId, UWorld* SourceWorld,
		TSharedPtr<IEFCalystoGameplaySnapshot>& OutSnapshot, FString& Error) = 0;
	/** Provider-owned capability token for its concrete snapshot implementation.
	 * Callers must reject a snapshot the selected provider did not capture rather
	 * than relying on an unchecked shared-pointer downcast. */
	virtual bool CanHandleSnapshot(const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot) const = 0;
	/** Both calls run immediately before OpenLevel. Preparation is read-only;
	 * detachment drops source-world references only after preparation validates the
	 * full portable graph. A failure leaves the source world untouched. */
	virtual bool PreparePreTravelSnapshot(const FGuid& RequestId,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) = 0;
	virtual bool DetachPreTravelSnapshot(const FGuid& RequestId,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) = 0;
	/** Rebuild the exact detached graph in the destination after structural
	 * navigation/entry verification and before any content reservation. This is
	 * required even if the selected content catalog is empty. */
	virtual bool ReconstructPreTravelSnapshot(const FEFCalystoAttemptToken& Token, UWorld* DestinationWorld,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) = 0;
	/** Terminal-only recovery after verified native cleanup. Reconstructs the
	 * exact detached graph in the still-owned destination, then releases the
	 * rejected lease. A failure retains Snapshot so the protected recovery action
	 * can be tried again; it must never discard the portable graph. */
	virtual bool RecoverDetachedSnapshot(const FEFCalystoAttemptToken& Token, UWorld* DestinationWorld,
		TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) = 0;

	/** Captures or reuses the immutable pre-floor state.  No actor is spawned,
	 * no probability is rolled and no persistent outcome is published here. */
	virtual bool PrepareAttempt(const FEFCalystoContentAttemptProviderRequest& Request,
		FEFCalystoContentAttemptProviderResult& OutResult, FString& Error) = 0;

	/** Binds the exact already-reserved manifest to typed levels, identities and
	 * the project bridge.  A false result rejects the whole attempt before any
	 * selected actor is spawned. */
	virtual bool BuildMaterializationContext(const FEFCalystoContentAttemptProviderRequest& Request,
		const FEFCalystoContentAttemptProviderResult& Prepared,
		TSharedRef<const FEFCalystoReservedContentManifest> Manifest,
		FEFCalystoContentGameplayContext& OutContext, FString& Error) = 0;

	/** Called only when the coordinator has finished a request permanently.
	 * Rejected spatial retries retain the snapshot; a detached rejected graph
	 * is deferred to RecoverDetachedSnapshot rather than released from nowhere.
	 * A false result keeps the lease owned so callers can preserve protection and
	 * report the failed terminal boundary rather than silently dropping it. */
	virtual bool FinishRequest(const FGuid& RequestId,
		TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, bool bAccepted) = 0;
};
