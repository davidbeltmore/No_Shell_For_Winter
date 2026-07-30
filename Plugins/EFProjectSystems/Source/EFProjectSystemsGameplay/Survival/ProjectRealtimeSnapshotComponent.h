#pragma once

#include "Components/ActorComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "Survival/ProjectSurvivalAttributeBridgeComponent.h"
#include "Survival/ProjectSurvivalNeedsTypes.h"
#include "Survival/ProjectSurvivalStatusTypes.h"
#include "ProjectRealtimeSnapshotComponent.generated.h"

class UProjectCombatAttributeComponent;
class UProjectInnerDoctrineComponent;
class UProjectSurvivalNeedsComponent;
class UProjectSurvivalStatusComponent;
class AActor;

UENUM(BlueprintType)
enum class EProjectRealtimeCombatImpactType : uint8
{
	None UMETA(DisplayName = "None"),
	EnemyHealthLost UMETA(DisplayName = "Enemy Health Lost"),
	EnemyKilled UMETA(DisplayName = "Enemy Killed"),
	OwnerHealthLost UMETA(DisplayName = "Owner Health Lost")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectRealtimeActorHealthSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	FString ActorClassName;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	float CurrentHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bHasHealth = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bRelevantEnemy = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectRealtimeCombatImpact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	EProjectRealtimeCombatImpactType ImpactType = EProjectRealtimeCombatImpactType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TObjectPtr<AActor> LikelySourceActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	float OldHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	float NewHealth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	float DamageDelta = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bOwnerImpact = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bEnemyImpact = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bKilledTarget = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectUnifiedRuntimeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	FProjectRealtimeActorHealthSnapshot OwnerHealth;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TArray<FProjectRealtimeActorHealthSnapshot> ObservedEnemies;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TArray<FProjectSurvivalNeedSnapshot> Needs;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TArray<FProjectSurvivalSensationSnapshot> Sensations;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TArray<FProjectSurvivalStatusSnapshot> ActiveStatuses;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	TArray<FProjectSurvivalBridgeAttributeSnapshot> AttributeBridgeSnapshots;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	FProjectInnerDoctrineSnapshot InnerDoctrine;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bHasNeedsComponent = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bHasStatusComponent = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bHasInnerDoctrineComponent = false;

	UPROPERTY(BlueprintReadOnly, Category = "Realtime Snapshot")
	bool bAttributeBridgeReady = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectRealtimeCombatImpactSignature, const FProjectRealtimeCombatImpact&, Impact);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectRealtimeActorHealthChangedSignature, const FProjectRealtimeCombatImpact&, HealthChange);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectUnifiedRuntimeSnapshotChangedSignature, const FProjectUnifiedRuntimeSnapshot&, Snapshot);

UCLASS(ClassGroup = (Survival), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRealtimeSnapshotComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectRealtimeSnapshotComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Realtime Snapshot")
	void ForceRefreshSnapshot();

	UFUNCTION(BlueprintPure, Category = "Realtime Snapshot")
	FProjectUnifiedRuntimeSnapshot BuildUnifiedRuntimeSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Realtime Snapshot|Resources")
	bool TryReadActorResource(AActor* Actor, FName ResourceName, float& OutCurrentValue, float& OutMaxValue) const;

	UFUNCTION(BlueprintCallable, Category = "Realtime Snapshot|Resources")
	bool TryReadOwnerResource(FName ResourceName, float& OutCurrentValue, float& OutMaxValue) const;

	UFUNCTION(BlueprintCallable, Category = "Realtime Snapshot|Resources")
	float ApplyOwnerResourceDelta(FName ResourceName, float DeltaAmount, bool bClampToMax = true);

	UFUNCTION(BlueprintCallable, Category = "Realtime Snapshot|Resources")
	bool SetOwnerResourceFloor(FName ResourceName, float FloorValue);

	bool TryReadActorHealth(const AActor* Actor, float& OutCurrentHealth) const;
	bool IsRelevantCombatEnemy(const AActor* Actor) const;
	AActor* FindNearestRelevantEnemyToOwner() const;

	UPROPERTY(BlueprintAssignable, Category = "Realtime Snapshot")
	FProjectRealtimeCombatImpactSignature OnRealtimeCombatImpact;

	UPROPERTY(BlueprintAssignable, Category = "Realtime Snapshot")
	FProjectRealtimeActorHealthChangedSignature OnRealtimeActorHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Realtime Snapshot")
	FProjectUnifiedRuntimeSnapshotChangedSignature OnUnifiedRuntimeSnapshotChanged;

private:
	struct FProjectObservedActorHealthRuntime
	{
		TWeakObjectPtr<AActor> Actor;
		float LastHealth = 0.f;
		bool bHasLastHealth = false;
		bool bHasBroadcastKill = false;
	};

	void RefreshCachedComponents();
	void RefreshObservedEnemies();
	void ObserveEnemy(AActor* EnemyActor);
	void UpdateHealthSnapshots();
	void BroadcastSnapshotChanged();
	FProjectRealtimeActorHealthSnapshot BuildActorHealthSnapshot(AActor* Actor) const;
	bool IsEnemyWithinRelevantRadius(const AActor* EnemyActor) const;
	void EmitHealthChange(const FProjectRealtimeCombatImpact& Impact);
	void HandleActorSpawned(AActor* SpawnedActor);

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> InnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalAttributeBridgeComponent> AttributeBridgeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> CombatAttributeComponent;

	TArray<FProjectObservedActorHealthRuntime> ObservedEnemies;
	FDelegateHandle ActorSpawnedHandle;
	float HealthScanAccumulatorSeconds = 0.f;
	float EnemyRefreshAccumulatorSeconds = 0.f;
	float OwnerLastHealth = 0.f;
	bool bHasOwnerLastHealth = false;
};
