#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
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

	virtual bool IsLevelRuntimeReady(UWorld* World) override;
	virtual bool ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const override;
	virtual bool ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const override;
	virtual void PostProcessSpawnedActor(AActor* SpawnedActor) override;

private:
	struct FDungeonRuntimeState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> DungeonActor;
		TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsVolume;
		TArray<TWeakObjectPtr<APawn>> SpawnedPawns;
		TSet<TObjectKey<UPCGComponent>> PendingPCGComponents;
		double BootstrapStartTimeSeconds = 0.0;
		bool bBootstrapStarted = false;
		bool bPCGGenerationTriggered = false;
		bool bPCGGenerationFinished = false;
		bool bNavBoundsCreated = false;
		bool bNavigationBuildRequested = false;
		bool bNavigationReady = false;
		bool bSpawnedPawnsRevalidatedAfterNav = false;
		bool bDungeonReady = false;
	};

	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	void BootstrapDungeon(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	AActor* FindDungeonActor(UWorld* World) const;
	void TrackDungeonActor(UWorld* World, AActor* DungeonActor, bool bGenerationTriggered);
	void RandomizeAndGenerateDungeon(AActor* DungeonActor);
	bool TryInvokeActorFunction(AActor* Actor, TConstArrayView<FName> CandidateNames) const;
	void RandomizePCGComponentSeeds(AActor* DungeonActor, int32 BaseSeed) const;
	void GeneratePCGComponents(AActor* DungeonActor) const;

	FDungeonRuntimeState& FindOrAddRuntimeState(UWorld* World);
	FDungeonRuntimeState* FindRuntimeState(UWorld* World);
	void RefreshDungeonRuntimeState(UWorld* World, FDungeonRuntimeState& RuntimeState);
	void HandlePCGComponentGenerated(UPCGComponent* PCGComponent);
	void HandlePCGComponentCancelled(UPCGComponent* PCGComponent);
	void HandlePCGComponentCleaned(UPCGComponent* PCGComponent);
	void HandleTrackedPCGComponentComplete(UPCGComponent* PCGComponent);
	bool EnsureRuntimeNavigation(UWorld* World, FDungeonRuntimeState& RuntimeState);
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
	bool BuildSpawnCandidateLocations(APawn* Pawn, bool bRequireNavigation, TArray<FVector>& OutCandidates) const;
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
};
