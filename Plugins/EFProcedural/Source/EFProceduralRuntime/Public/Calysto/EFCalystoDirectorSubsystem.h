#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDirectorRuntime.h"
#include "Containers/Ticker.h"
#include "Engine/StreamableManager.h"
#include "Interfaces/LevelReadinessProvider.h"
#include "Interfaces/PlayerStartResolver.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoDirectorSubsystem.generated.h"

class APawn;
class IEFCalystoContentAttemptProvider;
class IEFCalystoGameplaySnapshot;

UENUM(BlueprintType)
enum class EEFCalystoDirectorState : uint8 { Idle, Loading, Traveling, Generating, Recovering, Ready, Failed, Cancelled, AwaitingPlayerRelease };

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EEFCalystoDirectorState State = EEFCalystoDirectorState::Idle;
	UPROPERTY(BlueprintReadOnly) int64 RunSeed = 0;
	UPROPERTY(BlueprintReadOnly) int64 RunEpoch = 0;
	UPROPERTY(BlueprintReadOnly) int64 FloorNumber = 0;
	/** Requested floor and last committed floor are distinct while loading or recovering. */
	UPROPERTY(BlueprintReadOnly) int64 LastCommittedFloorNumber = 0;
	UPROPERTY(BlueprintReadOnly) int64 LastCommittedRunEpoch = 0;
	UPROPERTY(BlueprintReadOnly) int32 RerollIndex = 0;
	UPROPERTY(BlueprintReadOnly) int32 AttemptCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 TopologySeed = 0;
	UPROPERTY(BlueprintReadOnly) FGuid StyleId;
	UPROPERTY(BlueprintReadOnly) FString FailureMessage;
	UPROPERTY(BlueprintReadOnly) double RequestElapsedSeconds = 0;
	UPROPERTY(BlueprintReadOnly) bool bCanRetry = false;
	UPROPERTY(BlueprintReadOnly) bool bCanReturnToHub = false;
	UPROPERTY(BlueprintReadOnly) bool bGameplayVerified = false;
	UPROPERTY(BlueprintReadOnly) bool bNativeFloorVerified = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FEFCalystoDirectorTravelEvent, int64);
DECLARE_MULTICAST_DELEGATE_OneParam(FEFCalystoDirectorReadyEvent, const FEFCalystoDirectorSnapshot&);

/** Sole run/request owner for the unversioned Director. It never compiles or loads a legacy policy. */
UCLASS()
class EFPROCEDURALRUNTIME_API UEFCalystoDirectorSubsystem final : public UGameInstanceSubsystem,
	public ILevelReadinessProvider, public IPlayerStartResolver
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool RegisterRuntime(TSharedRef<IEFCalystoDirectorAttemptRuntime> Runtime);
	void UnregisterRuntime(const IEFCalystoDirectorAttemptRuntime* Runtime);
	UFUNCTION(BlueprintPure, Category = "Calysto|Director") FEFCalystoDirectorSnapshot GetSnapshot() const;
	/** Bounded read-only evidence export for the inspector and Development test harness. */
	UFUNCTION(BlueprintPure, Category = "Calysto|Director") FString GetDiagnosticsJson() const;
	UFUNCTION(BlueprintPure, Category = "Calysto|Director") bool HasActiveRun() const { return Snapshot.FloorNumber > 0; }
	UFUNCTION(BlueprintPure, Category = "Calysto|Director") bool IsTravelRequestPending() const;
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestStartNewRun();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestStartNewRunWithSeed(int64 Seed);
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestAdvanceFloor();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestReplayCurrentFloor();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestRerollCurrentFloor();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestRetry();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestReturnToHub();
	UFUNCTION(BlueprintCallable, Category = "Calysto|Travel") bool RequestCancel();
	virtual bool IsLevelRuntimeReady(UWorld* World) override;
	virtual bool ResolvePlayerStartTransform(UWorld* World, FTransform& OutTransform) const override;
	/** Native travel confirms actual pawn placement and visual readiness before final commitment. */
	EEFCalystoPlayerReleaseResult ConfirmPlayerRelease(UWorld* World, APawn* Player, FString& Error);
	bool RejectPlayerRelease(UWorld* World, FName Code, const FString& Message);
	bool IsDungeonWorld(const UWorld* World) const;
	FEFCalystoDirectorTravelEvent BeforeTravel;
	FEFCalystoDirectorReadyEvent FloorReady;
	FEFCalystoDirectorReadyEvent RequestFailed;
	const FEFCalystoFloorTransaction* GetTransaction() const { return Transaction.Get(); }

private:
	bool BeginRequest(int64 Floor, int32 Reroll, bool bPreserveStyle);
	void HandleConfigurationLoaded(FGuid Request);
	void HandleVisualsLoaded(FGuid Request);
	void HandleBakedVisualsLoaded(FGuid Request);
	void TryFinishDependencyLoading(FGuid Request);
	void StartSharedDependencyLoading();
	void HandleSharedDependenciesLoaded(FGuid Generation);
	double GetSharedDependencyDeadlineSeconds() const;
	void ReleaseSharedDependencies();
	void RefreshAsyncLoadingBudget();
	bool TickRequest(float DeltaSeconds);
	void BeginNativeAttempt(UWorld* World);
	void ObserveNativeAttempt(double Now);
	void BeginRollback();
	bool CapturePreTravelGameplaySnapshot(FString& Error);
	bool PrepareAndDetachPreTravelGameplaySnapshot(FString& Error);
	/** Resolves a terminal detached graph only after native cleanup. It restores
	 * exact persistent state in the owned destination before any Retry or HUB
	 * travel; failure retains the graph and keeps the player protected. */
	bool ResolveTerminalPreTravelGameplaySnapshot(FString& Error);
	/** Releases a source-world snapshot only when the runtime has not yet taken
	 * ownership. A detached unreconstructed graph is deliberately retained for a
	 * protected recovery path instead of being silently discarded. */
	void FinishPreTravelGameplaySnapshot(const FGuid& RequestId, bool bAccepted);
	/** Releases a request-owned gameplay snapshot only after the coordinator has
	 * observed terminal cleanup and ruled out a spatial retry. */
	bool FinishRuntimeRequest(const TSharedPtr<IEFCalystoDirectorAttemptRuntime>& Runtime,
		const FGuid& RequestId, bool bAccepted);
	void Fail(FName Code, const FString& Message, EEFCalystoAttemptFailure Kind);
	void ReleaseLoads();
	void InvalidatePendingLoads();
	bool CanReceiveLoadCallback(FGuid Request) const;
	void IssueRequestedHubTravel();
	void PublishFailure();
	friend class FEFCalystoDirectorLifecycleTests;
	friend class FEFCalystoDirectorDetachedSnapshotRecoveryTests;
	friend class FEFCalystoDirectorPublicationTests;
	friend class FEFCalystoDirectorBakedLoadingTests;
	FEFCalystoDirectorSnapshot Snapshot;
	TUniquePtr<FEFCalystoFloorTransaction> Transaction;
	/** At most four records, retained across rejected attempts until the next request. */
	TMap<FGuid, FEFCalystoAttemptMetrics> AttemptMetrics;
	TSharedPtr<const FEFCalystoCompiledDirector> Configuration;
	TSharedPtr<IEFCalystoDirectorAttemptRuntime> AttemptRuntime;
	TSharedPtr<IEFCalystoGameplaySnapshot> PreTravelGameplaySnapshot;
	IEFCalystoContentAttemptProvider* PreTravelGameplayProvider = nullptr;
	FGuid PreTravelGameplayRequest;
	TSharedPtr<FStreamableHandle> ConfigurationLease;
	TSharedPtr<FStreamableHandle> VisualLease;
	/** Child resources retain a separate lease; completing this phase must not release its PCGDataAsset parents. */
	TSharedPtr<FStreamableHandle> BakedVisualLease;
	/** Session resources survive individual requests and verified attempt cleanup. */
	TSharedPtr<FStreamableHandle> SharedLease;
	TArray<FSoftObjectPath> SharedPaths;
	FGuid SharedLoadGeneration;
	FString SharedLoadFailure;
	double SharedLoadStartedSeconds = -1;
	double SharedLoadCompletedSeconds = -1;
	double ConfigurationLoadStartedSeconds = -1;
	double ConfigurationLoadCompletedSeconds = -1;
	double ConfigurationCompileSeconds = -1;
	double VisualLoadStartedSeconds = -1;
	double VisualLoadCompletedSeconds = -1;
	double BakedVisualLoadStartedSeconds = -1;
	double BakedVisualLoadCompletedSeconds = -1;
	double DependenciesReadySeconds = -1;
	int32 RequestVisualPathCount = 0;
	bool bSharedLoadPending = false;
	bool bSharedLoadReady = false;
	bool bSharedReadyAtRequestStart = false;
	bool bVisualDependenciesLoaded = false;
	/** Explicit Development parity input only; never an authored-asset load fallback. */
	UPROPERTY(Transient) TObjectPtr<UEFCalystoDungeonDirectorAsset> NativeParityFixture;
	TArray<FSoftObjectPath> RequiredVisualPaths;
	FTSTicker::FDelegateHandle Ticker;
	FGuid RoutingRequest;
	/** Guards the provider's exactly-once terminal release for a request. */
	FGuid FinishedRuntimeRequest;
	FEFCalystoAttemptToken ReleasingToken;
	TWeakObjectPtr<UWorld> AttemptWorld;
	TWeakObjectPtr<UWorld> DepartingWorld;
	double RequestStartedSeconds = 0;
	double CleanupStartedSeconds = 0;
	bool bPreserveSelectedStyle = false;
	bool bTravelIssued = false;
	bool bAttemptStarted = false;
	bool bPreTravelGameplayDetached = false;
	bool bPreTravelGameplayHandedToRuntime = false;
	bool bTerminalGameplayRecoveryPending = false;
	FString TerminalGameplayRecoveryMessage;
	bool bReleaseStarted = false;
	bool bFailurePublished = false;
	bool bReturnToHubAfterRelease = false;
	bool bCancelAfterRelease = false;
	bool bSetupPending = false;
	bool bConfigurationLoadPending = false;
	bool bVisualLoadPending = false;
	bool bBakedVisualsExpanded = false;
	bool bRuntimeUnregisterPending = false;
	bool bCleanupFailed = false;
	bool bHubTravelIssued = false;
	bool bDeinitializing = false;
	bool bConfirmingPlayerRelease = false;
	double NextCleanupObservationSeconds = 0;
};
