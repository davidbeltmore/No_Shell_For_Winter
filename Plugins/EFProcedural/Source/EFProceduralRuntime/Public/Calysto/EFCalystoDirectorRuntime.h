#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Calysto/EFCalystoFloorTransaction.h"

class UWorld;
class IEFCalystoGameplaySnapshot;

/** Explicit immutable input; routing IDs never enter a random domain. */
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorAttemptRequest
{
	FEFCalystoAttemptToken Token;
	TSharedPtr<const FEFCalystoCompiledDirector> Configuration;
	FEFCalystoRandomKey Random;
	int32 TopologySeed = 0;
	double DeadlineSeconds = 0;
	TWeakObjectPtr<UWorld> World;
	/** Captured in the departing world under Token.Request, prepared and detached
	 * immediately before OpenLevel. Native realization receives it only to route
	 * the exact immutable gameplay state; it never creates a new destination-time
	 * snapshot. */
	TSharedPtr<IEFCalystoGameplaySnapshot> PreFloorGameplaySnapshot;
};

enum class EEFCalystoObservation : uint8
{
	Generating, StructuralVerification, NavigationAndReservations, RealizationVerification, Verified, Failed
};

/** Retained values from the actual attempt owner; unknown measurements stay unknown. */
struct EFPROCEDURALRUNTIME_API FEFCalystoAttemptMetrics
{
	int32 GenerateLocalCalls = INDEX_NONE;
	FName ObservationCode;
	FString ObservationMessage;
	FString NavigationDataPath;
	int32 FloorInstances = 0;
	int32 WallInstances = 0;
	int32 RoofInstances = 0;
	int32 ActiveNavigationTiles = INDEX_NONE;
	int32 RoutePoints = 0;
	float NavigationAgentRadius = -1;
	float NavigationAgentHeight = -1;
	float NavigationDefaultCellSize = -1;
	bool bBoundsRegistered = false;
	bool bNavigationBuilding = false;
	bool bStartProjected = false;
	bool bEndProjected = false;
	double NativeOutputSeconds = -1;
	double NavigationObservationSeconds = -1;
	int32 ArchitectureOpportunities = 0;
	int32 ReservedArchitectureParents = 0;
	int32 ReservedArchitectureMeshes = 0;
	int32 VerifiedArchitectureMeshes = 0;
	int32 ReservedContentElements = 0;
	int32 VerifiedContentElements = 0;
	int32 ContentOwnedActors = 0;
	int32 ContentRetainedLeases = 0;
	int32 DecalOpportunities = 0;
	int32 ReservedDecals = 0;
	int32 VerifiedDecals = 0;
	int32 DecalOwnedPoolActors = 0;
	int32 DecalRetainedLeases = 0;
	int32 SurfacePhysicsQueries = 0;
	int32 SurfaceGeometryRejections = 0;
	int32 SurfaceProtectionRejections = 0;
	FString FirstArchitectureRejection;
	double ArchitectureReservationSeconds = -1;
	double ArchitectureRealizationSeconds = -1;
};

/** A settled failure must be explicit. Pending observations consume no retry. */
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorAttemptObservation
{
	FEFCalystoAttemptToken Token;
	EEFCalystoObservation Stage = EEFCalystoObservation::Generating;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Configuration;
	FName FailureCode;
	FString Message;
	FString ReservationHash;
	TSet<FGuid> ReservedElements;
	FEFCalystoCommitEvidence Evidence;
	FEFCalystoAttemptMetrics Metrics;
};

/** Project-owned native integration; implementation belongs to EFProceduralPCGRuntime.
 * No Blueprint/debug function can supply a successful realization or release the player. */
class EFPROCEDURALRUNTIME_API IEFCalystoDirectorAttemptRuntime
{
public:
	virtual ~IEFCalystoDirectorAttemptRuntime() = default;
	virtual TArray<FSoftObjectPath> GetSharedDependencies() const = 0;
	/** Read-only expansion of already loaded native baked payloads. One bounded phase, no graph execution. */
	virtual bool GetBakedVisualDependencies(TConstArrayView<FSoftObjectPath> LoadedVisuals,
		TArray<FSoftObjectPath>& Dependencies, FString& Error) const
	{ Dependencies.Reset(); Error.Reset(); return true; }
	virtual bool Preflight(const FEFCalystoCompiledDirector& Configuration, const FGuid& Style,
		FString& Error) const = 0;
	/** Exactly one root generation request is armed by the coordinator before this call. */
	virtual bool BeginAttempt(const FEFCalystoDirectorAttemptRequest& Request, FString& Error) = 0;
	virtual FEFCalystoDirectorAttemptObservation ObserveAttempt(double NowSeconds) = 0;
	/** Final fallible publication, after exact player/manifest verification and before coordinator commit.
	 * Must remain reversible until OnCommitted; no gameplay events, activation, loads or spawning.
	 * Failure or cancellation is followed by token-scoped ReleaseAttempt and verified rollback. */
	virtual bool PublishPrepared(const FEFCalystoAttemptToken& Token, const FEFCalystoCommitEvidence& Evidence,
		double NowSeconds, EEFCalystoAttemptFailure& Failure, FString& Error) = 0;
	/** Activate verified ownership after acceptance. An invariant failure cannot undo accepted outcomes.
	 * The coordinator keeps the player protected and performs accepted cleanup without reseeding. */
	virtual EEFCalystoActivationResult OnCommitted(const FEFCalystoAttemptToken& Token, FString& Error)
	{ Error.Reset(); return EEFCalystoActivationResult::Activated; }
	/** Idempotent and token scoped. Teardown must finish before another BeginAttempt. */
	virtual void ReleaseAttempt(const FEFCalystoAttemptToken& Token) = 0;
	virtual FEFCalystoRollbackEvidence ObserveRelease(const FEFCalystoAttemptToken& Token) const = 0;
	/** The coordinator calls this after the request is terminal, never between spatial retries.
	 * Implementations release any immutable project snapshot lease retained across attempts.
	 * A false result preserves the terminal failure as observable protected state. */
	virtual bool FinishRequest(const FGuid& RequestId, bool bAccepted)
	{ return true; }
};
