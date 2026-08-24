#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "UObject/ObjectKey.h"

#include "Interfaces/LevelReadinessProvider.h"
#include "Interfaces/PlayerStartResolver.h"
#include "Interfaces/SpawnPostProcessor.h"

#include "EFProceduralPCGSubsystem.generated.h"

class AActor;
class ANavMeshBoundsVolume;
class APawn;
class UWorld;
class UPCGComponent;
struct FStreamableHandle;
struct FEFCalystoResolvedFloorIntentV4;
enum class EEFCalystoDungeonTravelKindV4 : uint8;

UCLASS()
class EFPROCEDURALPCGRUNTIME_API UEFProceduralPCGSubsystem
	: public UGameInstanceSubsystem
	, public ILevelReadinessProvider
	, public IPlayerStartResolver
	, public ISpawnPostProcessor
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsManagedRuntimeWorld(const UWorld* World) const;
	bool IsDungeonRuntimeReady(UWorld* World);

	/**
	 * Re-runs the same native Start-to-Door navigation contract used by the
	 * readiness gate. This read-only diagnostic avoids invoking
	 * UNavigationSystemV1 static helpers through a reflected class default
	 * object, which is invalid in unattended Python automation.
	 */
	UFUNCTION(BlueprintPure, Category = "EF Procedural|Calysto|Diagnostics")
	bool HasCurrentDungeonNavigationPath() const;

	/** True once the native spawn hook observed this floor's door disabled before readiness. */
	UFUNCTION(BlueprintPure, Category = "EF Procedural|Calysto|Diagnostics")
	bool WasCurrentFloorDoorDisabledBeforeReadiness() const;

	virtual bool IsLevelRuntimeReady(UWorld* World) override;
	virtual bool ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const override;
	virtual bool ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const override;
	virtual void PostProcessSpawnedActor(AActor* SpawnedActor) override;

#if !UE_BUILD_SHIPPING
	bool ArmDevelopmentSuppressStartPointOnceForAutomation(FString& OutError);
#endif

private:
	struct FDungeonRuntimeState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> DungeonActor;
		TWeakObjectPtr<UPCGComponent> ControlledPCGComponent;
		TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsVolume;
		TArray<TWeakObjectPtr<APawn>> SpawnedPawns;
		TSet<TObjectKey<UPCGComponent>> PendingPCGComponents;
		TSharedPtr<FStreamableHandle> DungeonPreloadHandle;
		TArray<FSoftObjectPath> DungeonPreloadPaths;
		TArray<FName> ReadinessTrace;
		FString DirectorIntentHash;
		FVector TopologyRepairApproachLocation = FVector::ZeroVector;
		double BootstrapStartTimeSeconds = 0.0;
		bool bDirectorWorldAccepted = false;
		bool bDungeonPreloadVerified = false;
		bool bBootstrapStarted = false;
		bool bPCGGenerationTriggered = false;
		bool bPCGGenerationFinished = false;
		bool bPCGGenerationFailed = false;
		bool bRuntimeReadinessFailed = false;
		bool bNavBoundsCreated = false;
		bool bNavigationBuildRequested = false;
		bool bNavigationReady = false;
		bool bTopologyRepairAttempted = false;
		bool bTopologyRepairApplied = false;
		bool bTopologyRepairAwaitingNavRebuild = false;
		bool bTopologyReady = false;
		bool bSpawnedPawnsRevalidatedAfterNav = false;
		bool bPopulationMaterializationStarted = false;
		bool bPopulationReady = false;
		bool bCompanionRosterReady = false;
		bool bFloorDoorDisabledBeforeReadiness = false;
		bool bDungeonReady = false;
		bool bFloorReadyNotified = false;
		bool bFailureReported = false;
		int32 GenerateLocalRequestCount = 0;
		uint64 ControlledGenerationTaskId = MAX_uint64;
		int32 NavigationPreparationAttempts = 0;
		int32 NavigationPathValidationAttempts = 0;
		int32 TopologyRepairNavRebuildCount = 0;
		double DungeonReadinessPollStartTimeSeconds = -1.0;
		FTimerHandle DungeonReadinessPollHandle;
	};

	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleDirectorWorldAccepted(
		int64 RunEpoch,
		EEFCalystoDungeonTravelKindV4 TravelKind,
		const FEFCalystoResolvedFloorIntentV4& Intent);

	void BootstrapDungeon(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	void HandleDungeonPreloadComplete(
		TWeakObjectPtr<UWorld> WorldPtr,
		int32 AttemptIndex,
		FString ExpectedIntentHash);
	AActor* FindDungeonActor(UWorld* World, int32& OutDungeonActorCount) const;
	bool ApplyCalystoHarnessAndGenerate(UWorld* World, AActor* DungeonActor, bool bDestroyActorOnFailure);
	void TrackDungeonActor(UWorld* World, AActor* DungeonActor, UPCGComponent* ControlledPCGComponent);

	FDungeonRuntimeState& FindOrAddRuntimeState(UWorld* World);
	FDungeonRuntimeState* FindRuntimeState(UWorld* World);
	bool RecordReadinessMilestone(FDungeonRuntimeState& RuntimeState, FName Milestone) const;
	void RefreshDungeonRuntimeState(UWorld* World, FDungeonRuntimeState& RuntimeState);
	void HandlePCGComponentGenerated(UPCGComponent* PCGComponent);
	void HandlePCGComponentCancelled(UPCGComponent* PCGComponent);
	void HandlePCGComponentCleaned(UPCGComponent* PCGComponent);
	void HandleTrackedPCGComponentComplete(UPCGComponent* PCGComponent, bool bSucceeded);
	void ScheduleDungeonReadinessPoll(UWorld* World, FDungeonRuntimeState& RuntimeState);
	void PollDungeonReadiness(TWeakObjectPtr<UWorld> WorldPtr);
	void SetFloorDoorsEnabled(UWorld* World, bool bEnabled) const;
	bool EnsureRuntimeNavigation(UWorld* World, FDungeonRuntimeState& RuntimeState);
	bool ResolveOrRepairDungeonTopology(UWorld* World, FDungeonRuntimeState& RuntimeState);
	bool IsDungeonNavigationPathReady(
		UWorld* World,
		FVector& OutStartLocation,
		FVector& OutDoorLocation,
		const FVector* RequiredApproachLocation = nullptr) const;
	bool TryMaterializePopulation(UWorld* World, FDungeonRuntimeState& RuntimeState);
	FBox CollectDungeonBounds(UWorld* World, const FDungeonRuntimeState& RuntimeState) const;
	bool EnsureNavMeshBoundsVolume(UWorld* World, const FBox& DungeonBounds, FDungeonRuntimeState& RuntimeState);
	void RegisterDungeonNavigationInvoker(AActor* DungeonActor, const FBox& DungeonBounds) const;
	void RevalidateSpawnedPawns(UWorld* World);

	AActor* FindDungeonStartPoint(UWorld* World) const;
	bool ResolveDungeonFallbackStartTransform(UWorld* World, FTransform& OutStartTransform) const;
	bool ProjectDungeonStartCandidate(UWorld* World, const FVector& CandidateLocation, FVector& OutResolvedLocation) const;

	void HandleWorldActorSpawned(AActor* SpawnedActor);
	bool ShouldFixSpawnedPawn(const APawn* Pawn) const;
	void TrySanitizeSpawnedPawn(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex);
	bool SanitizeSpawnedPawnPlacement(APawn* Pawn, bool bRequireNavigation) const;
	bool SnapSpawnCandidateToGround(UWorld* World, APawn* Pawn, const FVector& CandidateLocation, FVector& OutSnappedLocation) const;
	void TryEnsurePawnController(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex);
	void ApplyFallbackAIControllerClass(APawn* Pawn) const;

private:
	FDelegateHandle PostWorldInitializationHandle;
	FDelegateHandle WorldCleanupHandle;
	TMap<TObjectKey<UWorld>, FDelegateHandle> ActorSpawnHandles;
	TMap<TObjectKey<UWorld>, FDungeonRuntimeState> RuntimeStates;
	mutable TSet<TObjectKey<UWorld>> WarnedDerivedMapWorlds;
	mutable TSet<TObjectKey<UWorld>> WarnedMissingDungeonConfigWorlds;
#if !UE_BUILD_SHIPPING
	bool bDevelopmentSuppressStartPointOnceForAutomation = false;
#endif
};
