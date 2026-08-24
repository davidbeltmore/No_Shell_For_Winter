#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "ProjectEnemyLevelSubsystem.generated.h"

class AActor;
class APawn;
class APlayerController;
class UProjectEnemyLevelComponent;
class UProjectEnemyTargetInfoComponent;
class UProjectTargetingFixComponent;

struct FProjectPendingEnemyInitialization
{
	TWeakObjectPtr<APawn> Pawn;
	TObjectKey<UObject> ActorKey;
	int32 AttemptIndex = 0;
	uint64 EligibleFrameNumber = 0;
};

struct FProjectPreparedDirectorEnemyInitialization
{
	TWeakObjectPtr<APawn> Pawn;
	int32 LogicalLevel = 0;
	int32 PhysicalLevel = 0;
	bool bReplacedQueuedInitialization = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyLevelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|EnemyLevel")
	void RegisterContextProvider(UObject* Provider);

	UFUNCTION(BlueprintCallable, Category = "Project|EnemyLevel")
	void UnregisterContextProvider(UObject* Provider);

	/** True while this project-owned subsystem is preparing or finalizing the pawn's level. */
	bool IsEnemyInitializationPending(const APawn* Pawn) const;

	/**
	 * Pre-BeginPlay half of the V4 transaction. Writes CharacterInitLevel on the
	 * deferred instance so ACF starts at min(LogicalLevel, 100), then records an
	 * exact token required by InitializeDirectorEnemySynchronously.
	 */
	bool PrepareDeferredDirectorEnemy(
		APawn* DeferredPawn,
		int32 ExpectedLogicalLevel,
		int32 ExpectedPhysicalLevel,
		FString& OutFailureReason);

	/**
	 * Completes a V4 Director enemy on the game thread before PopulationRealized.
	 * The pawn must already have its immutable Director tags and must have
	 * completed FinishSpawning. Repeated calls only validate the completed state;
	 * they never reapply scaling.
	 */
	bool InitializeDirectorEnemySynchronously(
		APawn* Pawn,
		int32 ExpectedLogicalLevel,
		FString& OutFailureReason);

	/** Fail-closed verification of the exact logical/physical/scaling state. */
	bool ValidateDirectorEnemyInitialization(
		const APawn* Pawn,
		int32 ExpectedLogicalLevel,
		FString& OutFailureReason) const;

	/**
	 * Suppresses all queued work for a Director enemy whose population
	 * transaction is being rolled back. The caller must destroy the pawn.
	 */
	void RollbackDirectorEnemy(APawn* Pawn);

protected:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

private:
	bool LoadSettings();
	void HandleActorSpawned(AActor* SpawnedActor);
	void QueueEnemyInitialization(APawn* Pawn, int32 AttemptIndex);
	void PrepareEnemyInitialization(TWeakObjectPtr<APawn> PawnPtr, TObjectKey<UObject> ActorKey, int32 AttemptIndex);
	void ProcessPendingEnemyInitializations();
	void TryInitializeEnemy(TWeakObjectPtr<APawn> PawnPtr, TObjectKey<UObject> ActorKey, int32 AttemptIndex);
	void TryLogCompletedInitializationCohort();
	bool ShouldProcessPawn(const APawn* Pawn) const;
	bool IsTargetEnemyClass(const UClass* ActorClass) const;
	void ProcessExistingEnemies();
	bool ResolveWorldTierForWorld(UWorld* World, int32& OutWorldTier) const;
	UProjectEnemyLevelComponent* FindOrCreateEnemyLevelComponent(APawn* Pawn) const;
	UProjectEnemyTargetInfoComponent* FindOrCreateEnemyTargetInfoComponent(APawn* Pawn) const;
	void MarkActorProcessed(const AActor* Actor);
	bool IsActorProcessed(const AActor* Actor) const;
	bool IsActorPending(const AActor* Actor) const;
	void MarkActorPending(const AActor* Actor);
	void ClearActorPending(const AActor* Actor);
	bool CancelQueuedEnemyInitialization(const AActor* Actor);
	bool TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void EnsureTargetingFixComponent(APawn* Pawn);
	void MarkMaintenanceRequired();

private:
	FDelegateHandle ActorSpawnedHandle;
	TArray<TSubclassOf<APawn>> TargetEnemyBaseClasses;
	TArray<TWeakObjectPtr<UObject>> ContextProviders;
	TSet<TObjectKey<UObject>> ProcessedActors;
	TSet<TObjectKey<UObject>> PendingActors;
	TSet<TObjectKey<UObject>> SuppressedDirectorActors;
	TMap<TObjectKey<UObject>, FProjectPreparedDirectorEnemyInitialization> PreparedDirectorEnemies;
	TArray<FProjectPendingEnemyInitialization> PendingEnemyInitializations;
	uint64 InitializationCohortStartFrame = 0;
	int32 InitializationCohortScheduledCount = 0;
	int32 InitializationCohortCompletedCount = 0;
	int32 InitializationCohortFinalizeAttempts = 0;
	int32 InitializationCohortPeakQueueDepth = 0;
	int32 PendingPreparationCallbacks = 0;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTargetingFixComponent> TrackedTargetingFixComponent;

	bool bInitialEnemyScanPending = true;
	bool bNeedsPlayerMaintenanceTick = true;
	bool bTargetEnemyClassesPending = true;
};
