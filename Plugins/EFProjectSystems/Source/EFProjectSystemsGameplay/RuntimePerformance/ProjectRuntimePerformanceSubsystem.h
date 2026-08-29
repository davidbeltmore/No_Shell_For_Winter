#pragma once

#include "CoreMinimal.h"
#include "Game/ACFDamageType.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ProjectRuntimePerformanceSubsystem.generated.h"

class AActor;
class ACameraActor;
class APawn;
class FJsonObject;
class FJsonValue;
struct FStreamableHandle;
class UACFDamageHandlerComponent;
class UBrainComponent;
class UProjectCombatAttributeComponent;
class UProjectLockpickableComponent;
class UProjectRuntimePerformanceSettings;

UENUM(BlueprintType)
enum class EProjectRuntimePerformanceProfile58 : uint8
{
	DungeonSmoke58,
	DungeonNaturalGameplay58,
	DungeonAcceptance58,
	DungeonFullStackDiagnostic58
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectRuntimePerformanceMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	int32 FrameSampleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double AverageFps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double MedianFps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double MinFps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double OnePercentLowFps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double ZeroPointOnePercentLowFps = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double P95FrameMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	double P99FrameMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	int32 HitchesOver50Ms = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	int32 HitchesOver100Ms = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	int32 HitchesOver500Ms = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectRuntimePerformanceBenchmarkRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	FName BenchmarkId = TEXT("DungeonAcceptance58");

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	float DurationSeconds = -1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	int32 EnemyCount = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	bool bQuitOnFinish = false;

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	bool bStrictScenarioFailures = false;

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	int32 Seed = 42;

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	FName QualityPreset = TEXT("Balanced58");

	UPROPERTY(BlueprintReadWrite, Category = "Project|Runtime Performance")
	FString SourceCommit;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectRuntimePerformanceGateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	bool bPassed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Project|Runtime Performance")
	TArray<FString> FailureReasons;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRuntimePerformanceSubsystem
	: public UGameInstanceSubsystem
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonSmoke58(bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonNaturalGameplay58(float DurationSeconds = -1.0f, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonAcceptance58(float DurationSeconds = -1.0f, int32 EnemyCount = 8, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonFullStackDiagnostic58(float DurationSeconds = -1.0f, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDefaultDungeonCombatBenchmark(float DurationSeconds = -1.0f, int32 EnemyCount = -1, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonGameplayRealBenchmark(float DurationSeconds = -1.0f, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool RequestDungeonFullStackOverloadBenchmark(float DurationSeconds = -1.0f, int32 EnemyCount = -1, bool bQuitOnFinish = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Runtime Performance")
	bool CancelBenchmark(FName ReasonId = NAME_None);

	UFUNCTION(BlueprintPure, Category = "Project|Runtime Performance")
	bool IsBenchmarkRunning() const;

	UFUNCTION(BlueprintPure, Category = "Project|Runtime Performance")
	FString GetLastBenchmarkSummary() const;

	static FProjectRuntimePerformanceMetrics BuildMetrics(const TArray<double>& FrameTimesMs);
	static double PercentileSorted(const TArray<double>& SortedValues, double Percentile);
	static double AverageSlowestFps(const TArray<double>& SortedFrameTimesDescending, double Fraction);
	static bool IsSupportedBenchmarkId(FName BenchmarkId);
	static TArray<FName> BuildFullStackStageIds();
	static TArray<FName> BuildAcceptanceStageIds();
	static TArray<FName> BuildNaturalGameplayStageIds();
	static bool ShouldFailForScenarioIssue(bool bStrictScenarioFailures, bool bRequiredStage);
	static FProjectRuntimePerformanceGateResult EvaluateAcceptanceMetrics(
		const FProjectRuntimePerformanceMetrics& Metrics,
		double MinimumAverageFps = 60.0,
		double MinimumMedianFps = 60.0,
		double MinimumOnePercentLowFps = 55.0,
		double MaximumP99FrameMs = 18.2,
		int32 MaximumHitchesOver100Ms = 0);

private:
	enum class EBenchmarkPhase : uint8
	{
		Idle,
		PendingTravel,
		Preparing,
		Warmup,
		Running,
		Finishing
	};

	struct FProjectRuntimePerformanceFrameSample
	{
		uint64 FrameNumber = 0;
		double TimeSeconds = 0.0;
		double DeltaSeconds = 0.0;
		double FrameMs = 0.0;
		double GameThreadMs = 0.0;
		double RenderThreadMs = 0.0;
		double GpuMs = 0.0;
		double Fps = 0.0;
		double UsedMemoryMb = 0.0;
		FName StageId = NAME_None;
		FString StageName;
		FString MapName;
		FString ScenarioFlags;
		FVector PlayerLocation = FVector::ZeroVector;
		double PlayerSpeedCmPerSecond = 0.0;
		double TraversalDistance = 0.0;
		int32 RoutePointIndex = INDEX_NONE;
		int32 ActorCount = 0;
		int32 PawnCount = 0;
		int32 BenchmarkSpawnedEnemyCount = 0;
		int32 WorldRuntimeEnemyCount = 0;
		int32 BudgetObservedEnemyCount = 0;
		int32 ActiveCombatEnemyCount = 0;
		int32 FullRateEnemyCount = 0;
		int32 RuntimeEnemyCount = 0;
		int32 BudgetRuntimeEnemyCount = 0;
		int32 BudgetFullRateEnemyCount = 0;
		int32 BudgetMidRateEnemyCount = 0;
		int32 BudgetFarRateEnemyCount = 0;
		int32 BudgetSuspendedEnemyCount = 0;
		int32 BudgetNiagaraComponentCount = 0;
		int32 RuntimeEnemyWithoutControllerCount = 0;
		int32 HiddenRuntimeEnemyCount = 0;
		int32 CollisionDisabledRuntimeEnemyCount = 0;
		int32 EnemyMeshTickDisabledCount = 0;
		int32 EnemyMeshForcedLodCount = 0;
		int32 EnemyMeshUpdateRateOptimizationCount = 0;
		int32 SkeletalMeshComponentCount = 0;
		int32 NiagaraComponentCount = 0;
		int32 VisibleWidgetCount = 0;
		int32 ActiveStatusCount = 0;
		int32 ChronicleEntryCount = 0;
		int32 DirtyPaintActiveCount = 0;
		bool bIntimacyActive = false;
		bool bLockpickActive = false;
		int32 DebugCommandCount = 0;
		bool bAsyncLoading = false;
		bool bExcludedFromMetrics = false;
		bool bSyntheticBenchmarkWork = false;
		bool bExpectedIntimacySuppression = false;
		FString SyntheticBenchmarkReason;
		bool bWarmup = false;
	};

	struct FProjectRuntimePerformanceWorldSnapshot
	{
		int32 ActorCount = 0;
		int32 PawnCount = 0;
		int32 BenchmarkSpawnedEnemyCount = 0;
		int32 WorldRuntimeEnemyCount = 0;
		int32 BudgetObservedEnemyCount = 0;
		int32 ActiveCombatEnemyCount = 0;
		int32 FullRateEnemyCount = 0;
		int32 RuntimeEnemyCount = 0;
		int32 BudgetRuntimeEnemyCount = 0;
		int32 BudgetFullRateEnemyCount = 0;
		int32 BudgetMidRateEnemyCount = 0;
		int32 BudgetFarRateEnemyCount = 0;
		int32 BudgetSuspendedEnemyCount = 0;
		int32 BudgetNiagaraComponentCount = 0;
		int32 RuntimeEnemyWithoutControllerCount = 0;
		int32 HiddenRuntimeEnemyCount = 0;
		int32 CollisionDisabledRuntimeEnemyCount = 0;
		int32 EnemyMeshTickDisabledCount = 0;
		int32 EnemyMeshForcedLodCount = 0;
		int32 EnemyMeshUpdateRateOptimizationCount = 0;
		int32 SkeletalMeshComponentCount = 0;
		int32 NiagaraComponentCount = 0;
		int32 VisibleWidgetCount = 0;
		int32 ActiveStatusCount = 0;
		int32 ChronicleEntryCount = 0;
		int32 DirtyPaintActiveCount = 0;
		bool bIntimacyActive = false;
		bool bLockpickActive = false;
		int32 DebugCommandCount = 0;
		int32 AsyncLoadingSampleCount = 0;
		int32 MapTravelCount = 0;
		int32 TexturePoolSizeMb = 0;
		uint64 UsedPhysicalMemoryBytes = 0;
	};

	struct FProjectRuntimePerformanceSystemAccumulator
	{
		FName SystemId = NAME_None;
		double TotalCpuMs = 0.0;
		int32 CallCount = 0;
		int32 ActiveCountPeak = 0;
		TArray<double> CpuSamplesMs;
	};

	struct FProjectRuntimePerformanceFullStackStage
	{
		FName StageId = NAME_None;
		FString StageName;
		double DurationFraction = 0.0;
		double StartSeconds = 0.0;
		double EndSeconds = 0.0;
		bool bRequired = true;
		bool bStarted = false;
		bool bCompleted = false;
		bool bFailed = false;
		FString FailureReason;
	};

	struct FProjectRuntimePerformanceEnemySceneSuppressionState
	{
		TWeakObjectPtr<APawn> EnemyPawn;
		TWeakObjectPtr<UBrainComponent> BrainComponent;
		bool bHidden = false;
		bool bCollisionEnabled = true;
		bool bCanBeDamaged = true;
		bool bActorTickEnabled = true;
		bool bBrainWasPaused = false;
		bool bPartner = false;
	};

	void MaybeStartCommandLineBenchmark();
	bool RequestBenchmark(const FProjectRuntimePerformanceBenchmarkRequest& Request);
	void BeginPreparing();
	void StartWarmupOrRun();
	void FinishBenchmark(bool bSuccess, const FString& Reason);
	void ResetRuntimeState();
	void TickPreparing(float DeltaTime);
	void TickSampling(float DeltaTime);
	void CaptureNaturalBootstrapSample(float DeltaTime);
	void ApplyAutopilot(float DeltaTime);
	void ApplyAcceptanceAutopilot(float DeltaTime);
	void ApplyNaturalGameplayAutopilot(float DeltaTime);
	bool IsSmoke58Profile() const;
	bool IsNaturalGameplay58Profile() const;
	bool IsAcceptance58Profile() const;
	bool IsDiagnostic58Profile() const;
	bool IsStableCombatProfile() const;
	bool IsRealGameplayProfile() const;
	bool IsFullStackOverloadProfile() const;
	float ResolveActiveDurationSeconds() const;
	float ResolveActiveWarmupSeconds() const;
	int32 ResolveFullStackEnemyCap() const;
	bool IsTargetMapLoaded() const;
	FString GetConfiguredMapPackageName() const;
	FString GetCurrentShortMapName() const;
	bool OpenConfiguredMap();
	bool IsDungeonRuntimeReady() const;
	bool ResolveBenchmarkStartTransform(FTransform& OutTransform) const;
	bool ResolveVisualSafeStartTransform(const FTransform& RequestedTransform, FTransform& OutTransform);
	bool ResolveGroundedBenchmarkLocation(const FVector& RequestedLocation, FVector& OutLocation, FString& OutReason) const;
	void TeleportPlayerToBenchmarkStart(const FTransform& StartTransform, bool bUseBenchmarkCamera = true);
	void CleanupBenchmarkVisualState(bool bCancelInteractions);
	void RemoveBenchmarkBlockingMenus();
	void ApplyBenchmarkPlayerDamageGuard(APawn* PlayerPawn, bool bEnabled);
	void SetFullStackEnemyAiPaused(bool bPaused);
	void SetFullStackSceneCombatSuppressed(APawn* PartnerPawn, bool bSuppressed);
	void StabilizeBenchmarkCamera(const FTransform& FocusTransform);
	ACameraActor* EnsureBenchmarkCameraActor();
	bool UpdateBenchmarkCameraActor(bool bForceViewTarget);
	void MaybeCaptureBenchmarkVisualScreenshot();
	void RequestBenchmarkTargetedPreload();
	void MarkBenchmarkSyntheticWork(float ExclusionSeconds, const FString& Reason);
	bool ApplyBenchmarkQualityPreset();
	FString BuildEffectiveCVarSnapshot() const;
	FString BuildEffectiveCVarHash() const;
	int32 SpawnBenchmarkEnemies(const FTransform& StartTransform);
	int32 SpawnAcceptanceEnemies(const FTransform& StartTransform);
	APawn* SpawnBenchmarkEnemyByClassHint(const FTransform& CenterTransform, FName ClassHint, int32 SpawnIndex, bool bRequired);
	int32 EnsureFullStackEnemyPopulation(const FTransform& CenterTransform, int32 DesiredEnemyCount);
	bool PrepareBenchmarkEnemyAI(APawn* EnemyPawn, APawn* PlayerPawn, bool bInitialSetup);
	void RefreshBenchmarkEnemyAI();
	bool ShouldUseDirectBenchmarkEnemyTargeting() const;
	void ResolveConfiguredRuntimeEnemyClasses(bool bAllowSynchronousLoad = true) const;
	bool IsConfiguredRuntimeEnemyPawn(const APawn* Pawn) const;
	int32 RemovePreexistingRuntimeEnemiesForBenchmark();
	int32 CountActiveBenchmarkEnemies() const;
	void InitializeAcceptanceStages();
	void InitializeNaturalGameplayStage();
	bool BuildAcceptanceRoute(APawn* PlayerPawn);
	void BeginNaturalGameplayStage();
	void CompleteNaturalGameplayStage();
	void TickNaturalGameplayScenario(float DeltaTime);
	bool ValidateNaturalGameplayIntegrity(FString& OutReason) const;
	void FailNaturalGameplayScenario(const FString& Reason);
	void TickAcceptanceScenario(float DeltaTime);
	void BeginAcceptanceStage(int32 StageIndex);
	void CompleteAcceptanceStage(int32 StageIndex);
	bool ValidateAcceptanceIntegrity(FString& OutReason) const;
	void FailAcceptanceScenario(const FString& Reason);
	void InitializeFullStackStages();
	void TickFullStackScenario(float DeltaTime);
	bool BeginFullStackStage(int32 StageIndex);
	bool ValidateFullStackStageCompletion(const FProjectRuntimePerformanceFullStackStage& Stage) const;
	void CompleteFullStackStage(int32 StageIndex);
	void CleanupCompletedFullStackStage(FName StageId);
	void FailFullStackStage(const FString& Reason, bool bRequired);
	void RunFullStackStageTick(float DeltaTime);
	APawn* ResolvePrimaryFullStackEnemy() const;
	APawn* ResolveStageEnemy() const;
	void TrackScenarioActor(AActor* Actor);
	void BindDamageTelemetry(AActor* Actor);
	bool ApplyBenchmarkDamage(AActor* TargetActor, AActor* SourceActor, float DamageAmount, FName DamageType);
	bool OpenFullStackHudAndChronicles();
	bool ApplyFullStackStatusesAndNeeds(APawn* PlayerPawn);
	bool ApplyFullStackDirtyPawnWorkload(APawn* PlayerPawn);
	bool StartFullStackDefeatStage(APawn* PlayerPawn);
	bool StartFullStackIntimacyStage(APawn* PlayerPawn);
	bool StartFullStackLockpickingStage(APawn* PlayerPawn);
	bool StartFullStackYMenuAction(APawn* PlayerPawn);
	bool ExecuteNextFullStackDebugCommand(APawn* PlayerPawn);
	void RecordSystemMetric(FName SystemId, double CpuMs, int32 ActiveCount = 0);
	TArray<TSharedPtr<FJsonValue>> BuildSystemMetricValues(double MeasuredDurationSeconds) const;
	void WriteSystemMetricsCsv(const FString& RunDirectory, double MeasuredDurationSeconds) const;
	void UpdateWorldSnapshot();
	void CopyWorldSnapshotToSample(FProjectRuntimePerformanceFrameSample& Sample) const;
	double ReadGameThreadMs() const;
	double ReadRenderThreadMs() const;
	double ReadGpuMs() const;
	int32 ReadTexturePoolSizeMb() const;
	void AppendEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Fields = nullptr);
	void WriteArtifacts(bool bSuccess, const FString& Reason);
	FString BuildRunId() const;
	FString ResolveOutputRoot() const;
	FString ResolveRunDirectory() const;
	uint64 ReadUsedPhysicalMemoryBytes() const;
	uint64 ReadPeakPhysicalMemoryBytes() const;

	UFUNCTION()
	void HandleBenchmarkDamageApplied(AActor* SourceActor, FName DamageType, float RequestedDamage, float AppliedDamage, float RemainingValue, bool bKilledTarget);

	UFUNCTION()
	void HandleAcfDamageReceived(const FACFDamageEvent& DamageEvent);

private:
	EBenchmarkPhase Phase = EBenchmarkPhase::Idle;
	FProjectRuntimePerformanceBenchmarkRequest ActiveRequest;
	FProjectRuntimePerformanceMetrics LastMetrics;
	FString LastSummaryJson;
	FString ActiveRunId;
	double PhaseElapsedSeconds = 0.0;
	double BenchmarkElapsedSeconds = 0.0;
	double TotalElapsedSeconds = 0.0;
	double VisualSafeStartRetrySeconds = 0.0;
	int32 SpawnedEnemyCount = 0;
	int32 MapTravelCount = 0;
	int32 AsyncLoadingSampleCount = 0;
	uint64 PeakPhysicalMemoryBytes = 0;
	bool bCommandLineChecked = false;
	bool bArtifactsWritten = false;
	bool bMapTravelRequested = false;
	bool bVisualScreenshotRequested = false;
	bool bDamageGuardApplied = false;
	bool bDamageGuardPreviousCanBeDamaged = true;
	bool bDamageGuardPreviousAcfImmortal = false;
	bool bFullStackEnemyAiPaused = false;
	double MetricsExclusionUntilSeconds = 0.0;
	bool bSyntheticBenchmarkWorkThisFrame = false;
	FString SyntheticBenchmarkReasonThisFrame;
	double SyntheticBenchmarkWorkUntilSeconds = 0.0;
	FString SyntheticBenchmarkWorkReasonActive;

	TArray<TWeakObjectPtr<APawn>> SpawnedEnemies;
	TArray<TWeakObjectPtr<AActor>> ScenarioActors;
	mutable TArray<TWeakObjectPtr<UClass>> CachedBenchmarkRuntimeEnemyClasses;
	mutable bool bCachedBenchmarkRuntimeEnemyClassesResolved = false;
	TWeakObjectPtr<ACameraActor> BenchmarkCameraActor;
	TWeakObjectPtr<AActor> DamageGuardedActor;
	TWeakObjectPtr<UACFDamageHandlerComponent> DamageGuardedAcfComponent;
	TArray<TWeakObjectPtr<UProjectCombatAttributeComponent>> BoundDamageTelemetryComponents;
	TArray<TWeakObjectPtr<UACFDamageHandlerComponent>> BoundAcfDamageTelemetryComponents;
	TArray<FProjectRuntimePerformanceFullStackStage> FullStackStages;
	TArray<FProjectRuntimePerformanceEnemySceneSuppressionState> FullStackSceneSuppressionStates;
	TArray<FProjectRuntimePerformanceFrameSample> Samples;
	TArray<FProjectRuntimePerformanceFrameSample> NaturalBootstrapSamples;
	TMap<FName, FProjectRuntimePerformanceSystemAccumulator> SystemMetricAccumulators;
	TSharedPtr<FStreamableHandle> RuntimeBenchmarkPreloadHandle;
	bool bBenchmarkPreloadRequested = false;
	bool bBenchmarkPreloadCompleted = false;
	FProjectRuntimePerformanceWorldSnapshot CurrentWorldSnapshot;
	FProjectRuntimePerformanceWorldSnapshot PeakWorldSnapshot;
	FProjectRuntimePerformanceWorldSnapshot NaturalBootstrapWorldSnapshot;
	double WorldSnapshotElapsedSeconds = 0.0;
	double NaturalBootstrapSnapshotElapsedSeconds = 0.0;
	FString LastObservedMapName;
	FTransform FullStackStartTransform = FTransform::Identity;
	TWeakObjectPtr<APawn> ActiveStageEnemy;
	TWeakObjectPtr<UProjectLockpickableComponent> ActiveLockpickComponent;
	int32 ActiveFullStackStageIndex = INDEX_NONE;
	double ActiveFullStackStageElapsedSeconds = 0.0;
	double FullStackActionElapsedSeconds = 0.0;
	int32 FullStackEnemyToPlayerDamageCount = 0;
	int32 FullStackPlayerToEnemyDamageCount = 0;
	int32 StageStartEnemyToPlayerDamageCount = 0;
	int32 StageStartPlayerToEnemyDamageCount = 0;
	int32 FullStackStatusApplyCount = 0;
	int32 FullStackDirtyPaintApplyCount = 0;
	int32 FullStackDebugCommandCount = 0;
	int32 FullStackYMenuActionCount = 0;
	bool bFullStackScenarioFailed = false;
	bool bFullStackDefeatStarted = false;
	bool bFullStackDefeatRecovered = false;
	bool bFullStackIntimacyStarted = false;
	bool bFullStackIntimacyActiveSeen = false;
	bool bFullStackIntimacyAutomationAttempted = false;
	bool bFullStackIntimacyMenuTestCompleted = false;
	bool bFullStackIntimacyInteractionCompleted = false;
	bool bFullStackIntimacySessionPeakTriggered = false;
	bool bFullStackLockpickStarted = false;
	bool bFullStackLockpickConfirmed = false;
	bool bFullStackHudOpened = false;
	bool bFullStackDirtyWorkloadApplied = false;
	int32 FullStackIntimacyAutomationStepCount = 0;
	FString FullStackIntimacyFailureReason;
	FString FullStackScenarioFailureReason;
	FString CurrentStageName;
	FName CurrentStageId = NAME_None;
	FString CurrentScenarioFlags;
	TArray<FVector> AcceptanceRoutePoints;
	int32 AcceptanceRoutePointIndex = 0;
	int32 NaturalRouteDirection = 1;
	int32 NaturalRouteReversalCount = 0;
	int32 NaturalInitialEnemyCount = 0;
	int32 ActiveAcceptanceStageIndex = INDEX_NONE;
	double AcceptanceStageElapsedSeconds = 0.0;
	double AcceptanceIntegrityElapsedSeconds = 0.0;
	double AcceptanceCombatInputElapsedSeconds = 0.0;
	FVector AcceptanceStartLocation = FVector::ZeroVector;
	FVector AcceptanceLastPlayerLocation = FVector::ZeroVector;
	double AcceptanceTraversalDistance = 0.0;
	int32 AcceptanceAttackInputCount = 0;
	int32 AcceptanceDodgeInputCount = 0;
	bool bAcceptanceRouteBuilt = false;
	bool bAcceptanceEnemiesSpawned = false;
	bool bAcceptanceDirtyWorkloadStarted = false;
	bool bAcceptanceUiOpened = false;
	bool bAcceptanceScenarioFailed = false;
	FString AcceptanceScenarioFailureReason;
	double NaturalGameplayIntegrityElapsedSeconds = 0.0;
	bool bNaturalGameplayScenarioFailed = false;
	FString NaturalGameplayScenarioFailureReason;
};
