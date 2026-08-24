#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonTypes.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoDungeonSubsystem.generated.h"

class UEFCalystoDungeonDirectorPolicy;
class UEFCalystoDungeonDirectorPolicyV4;
class FEFCalystoV3TravelTransactionTest;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEFCalystoFloorCompleted, FName /* TransitionId */);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnEFCalystoBeforeFloorAdvance,
	int64 /* CompletedFloor */,
	const FEFCalystoResolvedFloorIntentV4& /* CompletedIntent */);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnEFCalystoFloorReady,
	int64 /* FloorNumber */,
	int32 /* PCGSeed */,
	const FEFCalystoResolvedFloorIntentV4& /* Intent */,
	const FEFCalystoRealizedFloorManifestV4& /* Manifest */);
DECLARE_MULTICAST_DELEGATE(FOnEFCalystoFloorTravelFailed);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnEFCalystoBeforeAnyDirectorTravel,
	EEFCalystoDungeonTravelKindV4 /* TravelKind */);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnEFCalystoDirectorWorldAccepted,
	int64 /* RunEpoch */,
	EEFCalystoDungeonTravelKindV4 /* TravelKind */,
	const FEFCalystoResolvedFloorIntentV4& /* Intent */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEFCalystoNewRunInitialized, int64 /* RunEpoch */);

/**
 * GameInstance-scoped owner of a deterministic, infinite Calysto run.
 *
 * V4 compiles one immutable Primary Data Asset policy. It freezes a probabilistic
 * floor intent before travel and accepts one realized population manifest after
 * PCG/navigation. No function edits or saves a Calysto asset.
 */
UCLASS()
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoDungeonSnapshotV4 GetSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoResolvedFloorIntentV4 GetResolvedFloorIntent() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoRealizedFloorManifestV4 GetRealizedFloorManifest() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	FEFCalystoRunEcologyStateV4 GetRunEcology() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Director")
	FEFCalystoDirectorIntentV4 GetNextFloorDirectorIntent() const;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	bool SetNextFloorDirectorIntent(const FEFCalystoDirectorIntentV4& NewIntent);

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	void ClearNextFloorDirectorIntent();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Director")
	bool SubmitFloorOutcome(const FEFCalystoFloorOutcomeV4& Outcome);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	bool HasActiveRun() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetCurrentFloor() const;

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetRunSeed() const;

	/** Monotonic identity for explicit New Run operations. It never participates in RNG. */
	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	int64 GetRunEpoch() const;

	/** Freezes the project-owned companion roster for the next non-New-Run floor resolution. */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Companions")
	bool SubmitCompanionRunSnapshot(const FEFCalystoCompanionSnapshotV4& Snapshot);

	/** Lets a bound project-owned adapter fail the synchronous pre-travel gate closed. */
	void ReportDirectorTravelPreparationFailure(FName FailureCode, const FString& FailureMessage);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon")
	bool IsTravelRequestPending() const;

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

	/** Development-only. It uses neutral synthetic history and never pretends skipped floors were played. */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Debug")
	bool RequestTravelToFloor(int64 TargetFloor);

	/** Called after PCG and navigation, before the floor can become ready. */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyPopulationRealized(const FEFCalystoRealizedFloorManifestV4& Manifest);

	/** ProjectSystems calls this only after the frozen companion roster is restored and verified. */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime|V4")
	bool NotifyCompanionRosterReady(const FString& CompanionSnapshotHash);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Runtime|V4")
	bool IsCompanionRosterReady() const;

	/** Final readiness gate. Rejects calls until a valid realized manifest is frozen. */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyFloorReady();

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|Runtime")
	bool NotifyGenerationFailed(FName FailureCode, const FString& FailureMessage);

	/** Deterministic, non-mutating Development report. Empty in Shipping. */
	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|Debug")
	FString SampleDirectorRolls(int32 SampleCount = 1000) const;

	FOnEFCalystoFloorCompleted& OnFloorCompleted();
	FOnEFCalystoBeforeFloorAdvance& OnBeforeFloorAdvance();
	FOnEFCalystoFloorReady& OnFloorReady();
	FOnEFCalystoFloorTravelFailed& OnFloorTravelFailed();
	FOnEFCalystoBeforeAnyDirectorTravel& OnBeforeAnyDirectorTravel();
	FOnEFCalystoDirectorWorldAccepted& OnDirectorWorldAccepted();
	FOnEFCalystoNewRunInitialized& OnNewRunInitialized();

	/** Pure/static surfaces used by native automation and authoring validation. */
	static bool ValidateDirectorPolicy(const UEFCalystoDungeonDirectorPolicy* Policy, FString& OutError);
	static FString ComputeCanonicalHash(const FString& Text);
	static FString ComputePolicyHash(const UEFCalystoDungeonDirectorPolicy* Policy);
	static FString ComputeEcologyHash(const FEFCalystoRunEcologyState& Ecology);
	static FString ComputeManifestHash(const FEFCalystoRealizedFloorManifest& Manifest);
	static FString ComputeManifestHashV4(const FEFCalystoRealizedFloorManifestV4& Manifest);
	static FString ComputeEcologyHashV4(const FEFCalystoRunEcologyStateV4& Ecology);
	/** Builds the one canonical, sorted set of floor-selected soft paths for every V4 preload owner. */
	static bool GatherResolvedFloorAssetPathsV4(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		TArray<FSoftObjectPath>& OutPaths,
		FString& OutError);
	static FEFCalystoRunEcologyStateV4 BuildInitialEcologyV4(
		int64 RunSeed,
		const FString& PolicyHash,
		int32 GeneratorVersion);
	static FEFCalystoRunEcologyState BuildInitialEcology(int64 RunSeed, const FString& PolicyHash, int32 GeneratorVersion);
	static bool CommitOutcomeToEcology(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		int64 CompletedFloor,
		const FEFCalystoResolvedFloorIntent& CompletedIntent,
		const FEFCalystoFloorOutcome& Outcome,
		FEFCalystoRunEcologyState& InOutEcology,
		FString& OutError);
	static bool ResolveFloorIntentForTesting(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoDungeonGenerationContext& Context,
		const FEFCalystoRunEcologyState& Ecology,
		const FEFCalystoDirectorIntent& DirectorIntent,
		const FEFCalystoFloorOutcome& FrozenOutcome,
		FEFCalystoResolvedFloorIntent& OutIntent,
		FString& OutError);

#if !UE_BUILD_SHIPPING
	/**
	 * Unreflected, unattended-PIE-only policy clone used by size certification.
	 * The authored Primary Data Asset is never modified or saved.
	 */
	bool SetDevelopmentCandidateValidatedDungeonSizesForAutomation(
		const TArray<int32>& CandidateSizes,
		FString& OutCandidatePolicyHash,
		FString& OutError);
	bool ClearDevelopmentCandidateValidatedDungeonSizesForAutomation(FString& OutError);

	/**
	 * Unreflected exact edge used only by unattended Development certification.
	 * The optional cooked-game path additionally requires the explicit packaged
	 * acceptance runner and standalone net mode.
	 * Production resolution continues to select only from ValidatedDungeonSizes.
	 */
	bool SetDevelopmentForcedDungeonEdgeForAutomation(
		int32 DungeonEdge,
		bool bAllowPackagedGameAcceptance = false);
	void ClearDevelopmentForcedDungeonEdgeForAutomation();

	/**
	 * Unreflected exact population scenario used only by unattended Development
	 * acceptance automation. The configured Primary Data Asset remains untouched;
	 * resolution uses a validated transient clone until the scenario is cleared.
	 */
	bool SetDevelopmentPopulationScenarioForAutomation(
		FName Scenario,
		FString& OutScenarioPolicyHash,
		FString& OutError,
		bool bAllowPackagedGameAcceptance = false);
	bool ClearDevelopmentPopulationScenarioForAutomation(FString& OutError);
#endif

private:
	friend class FEFCalystoV3TravelTransactionTest;
	static bool ResolveFloorIntentInternal(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoDungeonGenerationContext& Context,
		const FEFCalystoRunEcologyState& Ecology,
		const FEFCalystoDirectorIntent& DirectorIntent,
		const FEFCalystoFloorOutcome& FrozenOutcome,
		int32 DevelopmentForcedDungeonEdge,
		FEFCalystoResolvedFloorIntent& OutIntent,
		FString& OutError);

	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleDungeonWorldReady(TWeakObjectPtr<UWorld> WorldPtr);
	void HandleDungeonWorldReadyLegacyV3(TWeakObjectPtr<UWorld> WorldPtr);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	bool HandleTravelWatchdog(float DeltaTime);
	bool IsConfiguredDungeonWorld(const UWorld* World) const;

	bool CompilePolicy();
	FEFCalystoDungeonGenerationContext MakeContext(int64 RunSeed, int64 FloorNumber, int64 GenerationSerial) const;
	bool BeginTravel(const FEFCalystoDungeonGenerationContext& TargetContext, EEFCalystoDungeonTravelKind Kind);
	bool BeginTravelLegacyV3(const FEFCalystoDungeonGenerationContext& TargetContext, EEFCalystoDungeonTravelKind Kind);
	void ExecutePendingTravel(int64 TravelRequestId);
	void CommitAcceptedPendingInputs();
	void ResetPendingTravelTransaction();
	void ArmTravelWatchdog();
	void CancelTravelWatchdog();
	void RejectPendingTravel(const TCHAR* Reason);
	void PreloadCoreRuntimeAssets();
	void PreloadResolvedFloorAssets(const FEFCalystoResolvedFloorIntentV4& Intent);
	void PreloadResolvedFloorAssetsLegacyV3(const FEFCalystoResolvedFloorIntent& Intent);
	bool ReloadFrozenIntentForRecovery();
	void ReturnFromFailedDungeon();
	bool BuildResolveContextV4(
		const FEFCalystoDungeonGenerationContext& TargetContext,
		const FEFCalystoRunEcologyStateV4& Ecology,
		const FEFCalystoDirectorIntentV4& DirectorIntent,
		const FEFCalystoFloorOutcomeV4& FrozenOutcome,
		bool bHasFrozenOutcome,
		const FEFCalystoCompanionSnapshotV4& CompanionSnapshot,
		FEFCalystoResolveContextV4& OutContext,
		FString& OutError) const;
	bool CommitAcceptedFloorToEcologyV4(
		const FEFCalystoFloorOutcomeV4& FrozenOutcome,
		FEFCalystoRunEcologyStateV4& InOutEcology,
		FString& OutError) const;
	/** Temporary unreflected retirement surface. It is never selected by V4 runtime. */
	bool NotifyPopulationRealizedLegacyV3(const FEFCalystoRealizedFloorManifest& Manifest);

	bool bPolicyCompilationAttempted = false;
	bool bPolicyValid = false;
	FString PolicyError;
	FString CompiledPolicyHash;

	UPROPERTY(Transient)
	TObjectPtr<UEFCalystoDungeonDirectorPolicy> CachedDirectorPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UEFCalystoDungeonDirectorPolicyV4> CachedDirectorPolicyV4;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> PreloadedCoreAssets;

	TSharedPtr<FStreamableHandle> ResolvedFloorPreloadHandle;
	TSharedPtr<FStreamableHandle> PendingResolvedFloorPreloadHandle;
	TArray<FSoftObjectPath> PendingResolvedFloorAssetPaths;

	bool bHasActiveRun = false;
	int64 RunEpoch = 0;
	int64 PendingRunEpoch = 0;
	FEFCalystoDungeonGenerationContext ActiveContext;
	FEFCalystoResolvedFloorIntent ActiveIntent;
	FEFCalystoRealizedFloorManifest ActiveManifest;
	FEFCalystoResolvedFloorIntentV4 ActiveIntentV4;
	FEFCalystoRealizedFloorManifestV4 ActiveManifestV4;
	bool bCompanionRosterReady = false;
	FString ReadyCompanionSnapshotHash;
	FEFCalystoRunEcologyState RunEcology;
	FEFCalystoRunEcologyStateV4 RunEcologyV4;

	bool bHasQueuedDirectorIntent = false;
	FEFCalystoDirectorIntent QueuedDirectorIntent;
	FEFCalystoDirectorIntentV4 QueuedDirectorIntentV4;
	bool bHasSubmittedOutcome = false;
	FEFCalystoFloorOutcome SubmittedOutcome;
	FEFCalystoFloorOutcomeV4 SubmittedOutcomeV4;
	bool bHasSubmittedCompanionSnapshot = false;
	FEFCalystoCompanionRunSnapshot SubmittedCompanionSnapshot;
	FEFCalystoCompanionSnapshotV4 SubmittedCompanionSnapshotV4;

	bool bTravelRequestPending = false;
	bool bOpenLevelIssued = false;
	bool bTravelPreparationInProgress = false;
	bool bExternalTravelPreparationFailed = false;
	FName ExternalTravelPreparationFailureCode = NAME_None;
	FString ExternalTravelPreparationFailureMessage;
	int64 NextTravelRequestId = 0;
	int64 PendingTravelRequestId = 0;
	FEFCalystoDungeonGenerationContext PendingContext;
	FEFCalystoResolvedFloorIntent PendingIntent;
	FEFCalystoRealizedFloorManifest PendingExpectedManifest;
	FEFCalystoRealizedFloorManifest ExpectedManifestForRealization;
	FEFCalystoResolvedFloorIntentV4 PendingIntentV4;
	FEFCalystoRealizedFloorManifestV4 PendingExpectedManifestV4;
	FEFCalystoRealizedFloorManifestV4 ExpectedManifestForRealizationV4;
	FEFCalystoRunEcologyState PendingEcology;
	FEFCalystoRunEcologyStateV4 PendingEcologyV4;
	TWeakObjectPtr<UWorld> PendingSourceWorld;
	bool bPendingSourceWasDungeon = false;
	EEFCalystoDungeonTravelState PendingSourceTravelState = EEFCalystoDungeonTravelState::Idle;
	EEFCalystoGenerationState PendingSourceGenerationState = EEFCalystoGenerationState::Idle;
	FString PendingReturnMapPackage;
	int32 PendingGenerationAttempt = 0;
	bool bPendingConsumesSubmittedOutcome = false;
	bool bPendingConsumesQueuedDirectorIntent = false;
	EEFCalystoDungeonTravelKind PendingTravelKind = EEFCalystoDungeonTravelKind::None;
	EEFCalystoDungeonTravelKind LastTravelKind = EEFCalystoDungeonTravelKind::None;
	EEFCalystoDungeonTravelState TravelState = EEFCalystoDungeonTravelState::Idle;
	EEFCalystoGenerationState GenerationState = EEFCalystoGenerationState::Idle;
	FName LastFailureCode = NAME_None;
	FString LastFailureMessage;
	int32 CurrentGenerationAttempt = 0;
	static constexpr int32 MaximumGenerationAttempts = 2;
	FString ReturnMapPackage;
	bool bRecoveryTravelPending = false;
	FName PendingCompletionTransitionId = NAME_None;
	FName AwaitingCompletionTransitionId = NAME_None;

#if !UE_BUILD_SHIPPING
	bool bUsingDevelopmentCandidatePolicyForAutomation = false;
	TArray<int32> DevelopmentCandidateValidatedDungeonSizesForAutomation;
	int32 DevelopmentForcedDungeonEdgeForAutomation = 0;
	FName DevelopmentPopulationScenarioForAutomation = NAME_None;
#endif

	FOnEFCalystoFloorCompleted FloorCompletedEvent;
	FOnEFCalystoBeforeFloorAdvance BeforeFloorAdvanceEvent;
	FOnEFCalystoFloorReady FloorReadyEvent;
	FOnEFCalystoFloorTravelFailed FloorTravelFailedEvent;
	FOnEFCalystoBeforeAnyDirectorTravel BeforeAnyDirectorTravelEvent;
	FOnEFCalystoDirectorWorldAccepted DirectorWorldAcceptedEvent;
	FOnEFCalystoNewRunInitialized NewRunInitializedEvent;
	FDelegateHandle PostWorldInitializationHandle;
	FDelegateHandle TravelFailureHandle;
	FTSTicker::FDelegateHandle TravelWatchdogHandle;
};
