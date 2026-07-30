#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectEnemyLevelSubsystem.generated.h"

class AActor;
class APawn;
class APlayerController;
class UProjectEnemyLevelComponent;
class UProjectEnemyTargetInfoComponent;
class UProjectTargetingFixComponent;

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

protected:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

private:
	void LoadSettings();
	void HandleActorSpawned(AActor* SpawnedActor);
	void QueueEnemyInitialization(APawn* Pawn, int32 AttemptIndex);
	void TryInitializeEnemy(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex);
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

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTargetingFixComponent> TrackedTargetingFixComponent;

	bool bInitialEnemyScanPending = true;
	bool bNeedsPlayerMaintenanceTick = true;
};
