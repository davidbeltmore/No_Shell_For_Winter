#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "UI/ProjectActivityFeedTypes.h"
#include "UObject/ObjectKey.h"
#include "ProjectActivityFeedSubsystem.generated.h"

class AActor;
class APawn;
class APlayerController;
class UActorComponent;
class UInputComponent;
class UProjectActivityFeedWidget;
class UProjectCombatAttributeComponent;
class UProjectInnerDoctrineComponent;
class UProjectSurvivalStatusComponent;

struct FProjectActivityFeedObservedInventoryItem
{
	FString Key;
	FText DisplayName;
	int32 Count = 0;
};

struct FProjectActivityFeedPendingLootEntry
{
	FText DisplayName;
	int32 Count = 0;
};

struct FProjectActivityFeedTrackedEnemyState
{
	TWeakObjectPtr<APawn> EnemyPawn;
	TWeakObjectPtr<UProjectCombatAttributeComponent> CombatComponent;
	bool bDamageBound = false;
	bool bWasDead = false;
	bool bAwarenessActive = false;
	double LastSeenTimeSeconds = -1.0;
	double LastBarkTimeSeconds = -1000.0;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectActivityFeedSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	bool HasFeedWidget() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	bool IsFeedExpanded() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	bool IsFeedHudVisible() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	int32 GetStoredEntryCount() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	int32 GetTrackedEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	FProjectActivityFeedEntry GetLatestEntry() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	FText GetLatestEntryMessage() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	FString GetLatestEntryMessageString() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed")
	int32 GetLatestEntryChannelValue() const;

	UFUNCTION(BlueprintPure, Category = "Project|ActivityFeed|Layout")
	FProjectChronicleLayoutPolicy GetChronicleLayoutPolicy(bool bForExpanded) const;

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void RequestToggleExpanded();

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void RequestScrollHistory(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void DebugAddSystemEntry(const FText& Message);

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void DebugAddEnemyBarkEntry(AActor* EnemyActor, bool bGroupBark);

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void AddSystemEntry(const FText& Message);

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void AddDialogueEntry(const FText& Message);

	UFUNCTION(BlueprintCallable, Category = "Project|ActivityFeed")
	void AddPartnerDialogueEntry(AActor* PartnerActor, const FText& Message);

protected:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandlePlayerAttributeChanged(FName AttributeName, float OldValue, float NewValue, float MaxValue);

	UFUNCTION()
	void HandleStatusChanged(FName StatusName, bool bActive);

	UFUNCTION()
	void HandleBlackoutChanged(bool bBlackoutActive);

	UFUNCTION()
	void HandleEnemyDamageApplied(AActor* SourceActor, FName DamageType, float RequestedDamage, float AppliedDamage, float RemainingValue, bool bKilledTarget);

private:
	void LoadEnemyTargetClasses();
	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController(bool bRemoveWidget);
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void HandleToggleExpandedPressed();
	void HandleTrackedPawnUpdated(APawn* NewPawn);
	void ResolveTrackedPawnDependencies();
	void BindToTrackedComponents();
	void UnbindFromTrackedComponents();
	void EnsureFeedWidget(APlayerController* PlayerController);
	void RefreshFeedWidget();
	void AddFeedEntry(EProjectActivityFeedChannel Channel, const FText& Message);
	void AddFeedEntry(const FProjectActivityFeedEntry& Entry);
	void TrimStoredEntries();
	void ResetReflectionState();
	void ResetInventoryState();
	void ResetAggregates();
	void ProcessExistingEnemies();
	void HandleActorSpawned(AActor* SpawnedActor);
	void RegisterEnemyPawn(APawn* EnemyPawn);
	void RefreshEnemyBindings();
	void UnregisterInvalidEnemies();
	void ClearTrackedEnemies();
	void EvaluateEnemyBarks(float CurrentTimeSeconds);
	void FlushPendingAggregates(float CurrentTimeSeconds);
	void FlushPendingExperience();
	void FlushPendingHeal();
	void FlushPendingLoot();
	void PollLevelingBridge(float CurrentTimeSeconds);
	void PollInventoryBridge(float CurrentTimeSeconds);
	bool TryResolveLevelingComponent();
	bool TryResolveInventoryComponent();
	bool TryBuildInventorySnapshot(TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot) const;
	void QueueLootGain(const FText& DisplayName, int32 Count, float CurrentTimeSeconds);
	void QueueExperienceGain(int32 DeltaExperience, float CurrentTimeSeconds, bool bDoctrineExperience);
	void QueueHeal(float Amount, float CurrentTimeSeconds);
	void TryEmitEnemyDefeatFromPendingDamage(AActor* SourceActor);
	bool ShouldTrackEnemyPawn(const APawn* Pawn) const;
	bool DoesActorBelongToTrackedPlayer(const AActor* Actor) const;
	bool TryGetEnemyDisplayName(const AActor* EnemyActor, FText& OutDisplayName) const;
	FText BuildEnemyBarkText(const AActor* EnemyActor, bool bGroupBark) const;
	FProjectActivityFeedEntry BuildEnemyBarkEntry(const AActor* EnemyActor, bool bGroupBark) const;
	TSubclassOf<UProjectActivityFeedWidget> ResolveFeedWidgetClass() const;

private:
	FDelegateHandle ActorSpawnedHandle;
	TArray<TSubclassOf<APawn>> TargetEnemyBaseClasses;
	TMap<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState> TrackedEnemies;
	TArray<FProjectActivityFeedEntry> StoredEntries;
	TMap<FString, FProjectActivityFeedObservedInventoryItem> LastInventorySnapshot;
	TMap<FString, FProjectActivityFeedPendingLootEntry> PendingLootEntries;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectActivityFeedWidget> TrackedFeedWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> TrackedCombatAttributeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> TrackedStatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> TrackedInnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> BoundCombatAttributeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> BoundStatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UActorComponent> CachedLevelingComponent;

	UPROPERTY(Transient)
	TObjectPtr<UActorComponent> CachedInventoryComponent;

	bool bInitialEnemyScanPending = true;
	bool bExpanded = false;
	bool bHasInventorySnapshot = false;
	bool bHasObservedExperience = false;
	bool bWarnedLevelingBridgeFailure = false;
	bool bWarnedInventoryBridgeFailure = false;
	bool bWarnedMissingLevelingComponent = false;
	bool bWarnedMissingInventoryComponent = false;

	int32 NextEntrySequence = 1;
	int32 LastObservedLevel = INDEX_NONE;
	double LastObservedExperience = 0.0;
	double LastLevelPollTimeSeconds = -1.0;
	double LastInventoryPollTimeSeconds = -1.0;
	double LastEnemyAwarenessPollTimeSeconds = -1.0;

	bool bHasPendingHeal = false;
	float PendingHealAmount = 0.0f;
	double PendingHealExpiryTimeSeconds = -1.0;

	bool bHasPendingExperience = false;
	bool bPendingExperienceIsDoctrine = false;
	int32 PendingExperienceAmount = 0;
	double PendingExperienceExpiryTimeSeconds = -1.0;

	double PendingLootExpiryTimeSeconds = -1.0;
};
