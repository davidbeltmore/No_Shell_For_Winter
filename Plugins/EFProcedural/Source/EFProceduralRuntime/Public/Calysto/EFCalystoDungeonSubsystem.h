#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/EFCalystoFloorLoadCoordinator.h"
#include "Calysto/EFCalystoPopulationPlannerV6.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoDungeonSubsystem.generated.h"

class UEFCalystoDungeonDirectorPolicyV6Asset;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEFCalystoFloorCompleted, FName /* TransitionId */);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnEFCalystoBeforeFloorAdvance,
	int64 /* CompletedFloor */,
	const FEFCalystoResolvedFloorIntentV6& /* CompletedIntent */);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnEFCalystoFloorReady,
	int64 /* FloorNumber */,
	int32 /* PCGSeed */,
	const FEFCalystoResolvedFloorIntentV6& /* Intent */,
	const FEFCalystoRealizedFloorManifestV6& /* Manifest */);
DECLARE_MULTICAST_DELEGATE(FOnEFCalystoFloorTravelFailed);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnEFCalystoBeforeAnyDirectorTravel,
	EEFCalystoDungeonTravelKindV6 /* TravelKind */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnEFCalystoDirectorWorldAccepted,
	int64 /* RunEpoch */,
	EEFCalystoDungeonTravelKindV6 /* TravelKind */,
	const FEFCalystoResolvedFloorIntentV6& /* Intent */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEFCalystoNewRunInitialized, int64 /* RunEpoch */);

/**
 * Definitive GameInstance owner for one deterministic Calysto run.
 *
 * The subsystem resolves exactly one Style before travel, freezes room-local
 * Themes after topology, retains four asynchronous asset leases, and accepts
 * immutable population evidence before enabling the floor door.
 */
UCLASS()
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoDungeonSnapshotV6 GetSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoResolvedFloorIntentV6 GetResolvedFloorIntent() const;

	/** Native spelling retained for PCG code that makes its generation explicit. */
	FEFCalystoResolvedFloorIntentV6 GetResolvedFloorIntentV6() const { return GetResolvedFloorIntent(); }

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoResolvedFloorPlanV6 GetResolvedFloorPlanV6() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoRoomManifestV6 GetRoomManifestV6() const;

	const FEFCalystoPopulationPlanV6& GetPopulationPlanV6() const { return ActivePopulationPlan; }

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoRealizedFloorManifestV6 GetRealizedFloorManifest() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoRunEcologyStateV6 GetRunEcology() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Director")
	FEFCalystoDirectorIntentV6 GetNextFloorDirectorIntent() const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	bool SetNextFloorDirectorIntent(const FEFCalystoDirectorIntentV6& NewIntent);

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	void ClearNextFloorDirectorIntent();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	bool SubmitFloorOutcome(const FEFCalystoFloorOutcomeV6& Outcome);

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Companions")
	bool SubmitCompanionRunSnapshot(const FEFCalystoCompanionRosterSnapshotV6& Snapshot);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	bool HasActiveRun() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetCurrentFloor() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetRunSeed() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetRunEpoch() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	bool IsTravelRequestPending() const;

	/** True only while this exact accepted intent still owns generation. */
	bool IsGenerationBootstrapCurrentV6(const FString& ExpectedIntentHash) const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Travel")
	bool RequestStartNewRun();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Travel")
	bool RequestStartNewRunWithSeed(int64 NewRunSeed);

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Travel")
	bool RequestAdvanceFloor();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Travel")
	bool RequestRerollCurrentFloor();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Travel")
	bool RequestReplayCurrentFloor();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Debug")
	bool RequestTravelToFloor(int64 TargetFloor);

	/** Freezes the complete deduplicated PCG room set exactly once. */
	bool BuildAndFreezeRoomManifestV6(
		const TArray<FEFCalystoRoomIdentityInputV6>& RoomInputs,
		FEFCalystoRoomManifestV6& OutManifest,
		FString& OutError);

	/** Starts the sole post-topology content lease. */
	bool BeginPostTopologyContentLoadV6(
		TConstArrayView<FSoftObjectPath> PopulationMaterializationPaths,
		FString& OutError);

	EEFCalystoLoadPhaseState GetPostTopologyAssetLoadStateV6(FString& OutError) const;
	bool ArePostTopologyAssetsReadyV6(FString& OutError) const;
	bool EnsureFloorVisualAssetsV6(FStreamableDelegate Completion, FString& OutError);
	EEFCalystoLoadPhaseState GetFloorVisualAssetLoadStateV6(FString& OutError) const;
	bool AreFloorVisualAssetsReadyV6(FString& OutError) const;

	/** Freezes acceptance of exactly one materialized population plan. */
	bool NotifyPopulationRealizedV6(
		const FString& MaterializationHash,
		int32 CandidateAnchorCount,
		TConstArrayView<FEFCalystoRealizedPopulationActorRecordV6> RealizedActors);

	void RollbackPopulationRealizedV6(
		const FString& PopulationHash,
		const FString& MaterializationHash);

	bool IsPopulationRealizedV6() const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyCompanionRosterReady(const FString& CompanionSnapshotHash);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Runtime")
	bool IsCompanionRosterReady() const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyFloorReady();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyGenerationFailed(FName FailureCode, const FString& FailureMessage);

	/** Lets a bound adapter fail the synchronous preparation gate closed. */
	void ReportDirectorTravelPreparationFailure(FName FailureCode, const FString& FailureMessage);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Debug")
	FString SampleDirectorRolls(int32 SampleCount = 1000) const;

	FOnEFCalystoFloorCompleted& OnFloorCompleted() { return FloorCompletedEvent; }
	FOnEFCalystoBeforeFloorAdvance& OnBeforeFloorAdvance() { return BeforeFloorAdvanceEvent; }
	FOnEFCalystoFloorReady& OnFloorReady() { return FloorReadyEvent; }
	FOnEFCalystoFloorTravelFailed& OnFloorTravelFailed() { return FloorTravelFailedEvent; }
	FOnEFCalystoBeforeAnyDirectorTravel& OnBeforeAnyDirectorTravel() { return BeforeAnyDirectorTravelEvent; }
	FOnEFCalystoDirectorWorldAccepted& OnDirectorWorldAccepted() { return DirectorWorldAcceptedEvent; }
	FOnEFCalystoNewRunInitialized& OnNewRunInitialized() { return NewRunInitializedEvent; }

	static FString ComputeCanonicalHash(const FString& Text);
	static FEFCalystoRunEcologyStateV6 BuildInitialEcologyV6(
		int64 RunSeed,
		const FString& PolicyHash,
		const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster);
	static bool CommitOutcomeToEcologyV6(
		const FEFCalystoFloorOutcomeV6& Outcome,
		const FEFCalystoResolvedFloorIntentV6& CompletedIntent,
		const FEFCalystoRealizedFloorManifestV6& CompletedManifest,
		FEFCalystoRunEcologyStateV6& InOutEcology,
		FString& OutError);
	static bool ResolveFloorIntentForTestingV6(
		const UEFCalystoDungeonDirectorPolicyV6Asset* Policy,
		const FEFCalystoDungeonGenerationContextV6& Context,
		const FEFCalystoDirectorIntentV6& DirectorIntent,
		const FEFCalystoFloorOutcomeV6& Outcome,
		const FEFCalystoRunEcologyStateV6& Ecology,
		const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster,
		FEFCalystoResolvedFloorIntentV6& OutIntent,
		FString& OutError);

	/**
	 * Validates materializer evidence against every frozen actor/content decision
	 * and builds the canonical post-placement manifest without loading objects.
	 */
	static bool BuildRealizedFloorManifestV6(
		const FEFCalystoDungeonGenerationContextV6& Context,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationPlanV6& PopulationPlan,
		const FString& MaterializationHash,
		int32 CandidateAnchorCount,
		TConstArrayView<FEFCalystoRealizedPopulationActorRecordV6> RealizedActors,
		FEFCalystoRealizedFloorManifestV6& OutManifest,
		FString& OutError);

#if WITH_DEV_AUTOMATION_TESTS
	void SetForcedDungeonEdgeForAutomation(int32 Edge) { AutomationForcedDungeonEdge = Edge; }
	void ClearForcedDungeonEdgeForAutomation() { AutomationForcedDungeonEdge = 0; }
	void SetPopulationScenarioForAutomation(FName Scenario) { AutomationPopulationScenario = Scenario; }
	void ClearPopulationScenarioForAutomation() { AutomationPopulationScenario = NAME_None; }
#endif

private:
	bool BeginTravel(const FEFCalystoDungeonGenerationContextV6& Context, EEFCalystoDungeonTravelKindV6 TravelKind);
	bool PreparePendingIntent(FString& OutError);
	bool BeginPendingFloorVisualLoad(FString& OutError);
	void HandlePendingFloorVisualReady(int64 TransactionId);
	void ExecutePendingTravel(int64 TransactionId);
	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS);
	void HandleDungeonWorldReady(TWeakObjectPtr<UWorld> World, int64 TransactionId);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandleSessionCoreReady();
	bool CompilePolicyFromResidentSessionCore(FString& OutError);
	void BeginSessionCoreLoad();
	void AcceptPendingWorld(UWorld* World);
	void RejectPendingTravel(FName FailureCode, const FString& FailureMessage, bool bRecoverToSourceWorld);
	void ClearPendingTravel();
	void ResetActiveFloorEvidence();
	bool BeginFloorRecovery(FString& OutError);
	void ReturnFromFailedDungeon(FName FailureCode, const FString& FailureMessage);
	bool IsConfiguredDungeonWorld(const UWorld* World) const;
	FName GetConfiguredDungeonPackageName() const;
	FName GetSafeReturnPackageName() const;
	bool ArmWatchdog(double TimeoutSeconds);
	void CancelWatchdog();
	bool HandleWatchdogTick(float DeltaSeconds);
	bool ValidateActiveIdentity(FString& OutError) const;
	void SetFailure(FName FailureCode, const FString& FailureMessage);
	FEFCalystoDungeonSnapshotV6 BuildSnapshot() const;

	UPROPERTY(Transient)
	TObjectPtr<UEFCalystoDungeonDirectorPolicyV6Asset> RuntimePolicy = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> SessionCoreObjects;

	FEFCalystoFloorLoadCoordinator LoadCoordinator;

	FEFCalystoDungeonGenerationContextV6 ActiveContext;
	FEFCalystoResolvedFloorIntentV6 ActiveIntent;
	FEFCalystoResolvedFloorPlanV6 ActiveFloorPlan;
	FEFCalystoRoomManifestV6 ActiveRoomManifest;
	FEFCalystoPopulationPlanV6 ActivePopulationPlan;
	FEFCalystoRealizedFloorManifestV6 ActiveRealizedManifest;
	FEFCalystoRealizedFloorManifestV6 ExpectedRealizedManifest;
	FEFCalystoRunEcologyStateV6 ActiveEcology;

	FEFCalystoDungeonGenerationContextV6 PendingContext;
	FEFCalystoResolvedFloorIntentV6 PendingIntent;
	FEFCalystoResolvedFloorPlanV6 PendingFloorPlan;
	FEFCalystoRunEcologyStateV6 PendingEcology;
	FEFCalystoRealizedFloorManifestV6 PendingExpectedManifest;

	FEFCalystoDirectorIntentV6 QueuedDirectorIntent;
	FEFCalystoFloorOutcomeV6 QueuedFloorOutcome;
	FEFCalystoCompanionRosterSnapshotV6 QueuedCompanionRoster;

	EEFCalystoDungeonTravelStateV6 TravelState = EEFCalystoDungeonTravelStateV6::Idle;
	EEFCalystoDungeonTravelKindV6 ActiveTravelKind = EEFCalystoDungeonTravelKindV6::None;
	EEFCalystoDungeonTravelKindV6 PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	EEFCalystoDungeonTravelStateV6 SourceTravelState = EEFCalystoDungeonTravelStateV6::Idle;

	FName SourceWorldPackage = NAME_None;
	FName ReturnWorldPackage = NAME_None;
	FName LastFailureCode = NAME_None;
	FString LastFailureMessage;
	FString RuntimePolicyHash;
	FString AcceptedMaterializationHash;

	int64 RunEpoch = 0;
	int64 PendingRunEpoch = 0;
	int64 NextTransactionId = 0;
	int64 PendingTransactionId = 0;
	int32 RecoveryAttempt = 0;
	int32 AcceptedSpawnedActorCount = 0;
	int32 AcceptedChestContentCount = 0;

	bool bSessionCoreReady = false;
	bool bHasActiveRun = false;
	bool bTravelRequestPending = false;
	bool bOpenLevelIssued = false;
	bool bPreparationBroadcastActive = false;
	bool bPreparationFailed = false;
	bool bHasQueuedDirectorIntent = false;
	bool bHasQueuedFloorOutcome = false;
	bool bHasQueuedCompanionRoster = false;
	bool bPendingConsumesDirectorIntent = false;
	bool bPendingConsumesFloorOutcome = false;
	bool bPendingConsumesCompanionRoster = false;
	bool bCompanionRosterReady = false;
	bool bPopulationRealized = false;

	FName PreparationFailureCode = NAME_None;
	FString PreparationFailureMessage;

	FDelegateHandle PostWorldInitializationHandle;
	FDelegateHandle TravelFailureHandle;
	FTSTicker::FDelegateHandle WatchdogTickerHandle;
	double WatchdogDeadlineSeconds = 0.0;

	FOnEFCalystoFloorCompleted FloorCompletedEvent;
	FOnEFCalystoBeforeFloorAdvance BeforeFloorAdvanceEvent;
	FOnEFCalystoFloorReady FloorReadyEvent;
	FOnEFCalystoFloorTravelFailed FloorTravelFailedEvent;
	FOnEFCalystoBeforeAnyDirectorTravel BeforeAnyDirectorTravelEvent;
	FOnEFCalystoDirectorWorldAccepted DirectorWorldAcceptedEvent;
	FOnEFCalystoNewRunInitialized NewRunInitializedEvent;

#if WITH_DEV_AUTOMATION_TESTS
	int32 AutomationForcedDungeonEdge = 0;
	FName AutomationPopulationScenario = NAME_None;
#endif
};
