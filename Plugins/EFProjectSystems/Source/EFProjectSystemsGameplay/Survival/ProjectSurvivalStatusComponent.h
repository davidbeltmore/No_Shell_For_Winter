#pragma once

#include "Components/ActorComponent.h"
#include "AttributeSet.h"
#include "Survival/ProjectSurvivalStatusTypes.h"
#include "ProjectSurvivalStatusComponent.generated.h"

class FProperty;
class UAbilitySystemComponent;
class UAttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectSurvivalStatusChangedSignature, FName, StatusName, bool, bActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectSurvivalBlackoutChangedSignature, bool, bBlackoutActive);

struct FProjectSurvivalResolvedStatusHealthBinding
{
	FName ResolvedPropertyName = NAME_None;
	TWeakObjectPtr<UAttributeSet> AttributeSet;
	FGameplayAttribute GameplayAttribute;
	FProperty* ResolvedProperty = nullptr;
	float RecoveryBlockCeiling = 0.f;
	bool bResolved = false;
	bool bGameplayAttributeData = false;
	bool bFloatProperty = false;
	bool bHasRecoveryBlockCeiling = false;
};

struct FProjectSurvivalTimedStatusRuntime
{
	float GameplayStartTimeSeconds = 0.f;
	float GameplayEndTimeSeconds = 0.f;
	float DebugStartTimeSeconds = 0.f;
	float DebugEndTimeSeconds = 0.f;
	TWeakObjectPtr<AActor> GameplaySourceActor;
	TWeakObjectPtr<AActor> DebugSourceActor;
	bool bHasGameplayInstance = false;
	bool bHasDebugInstance = false;

	void ClearExpiredInstances(const float CurrentTimeSeconds)
	{
		if (bHasGameplayInstance && CurrentTimeSeconds >= GameplayEndTimeSeconds)
		{
			bHasGameplayInstance = false;
			GameplayStartTimeSeconds = 0.f;
			GameplayEndTimeSeconds = 0.f;
			GameplaySourceActor = nullptr;
		}

		if (bHasDebugInstance && CurrentTimeSeconds >= DebugEndTimeSeconds)
		{
			bHasDebugInstance = false;
			DebugStartTimeSeconds = 0.f;
			DebugEndTimeSeconds = 0.f;
			DebugSourceActor = nullptr;
		}
	}

	bool HasAnyActiveInstance(const float CurrentTimeSeconds) const
	{
		return (bHasGameplayInstance && CurrentTimeSeconds < GameplayEndTimeSeconds)
			|| (bHasDebugInstance && CurrentTimeSeconds < DebugEndTimeSeconds);
	}

	float GetRemainingDurationSeconds(const float CurrentTimeSeconds) const
	{
		float RemainingDurationSeconds = 0.f;
		if (bHasGameplayInstance && CurrentTimeSeconds < GameplayEndTimeSeconds)
		{
			RemainingDurationSeconds = FMath::Max(RemainingDurationSeconds, GameplayEndTimeSeconds - CurrentTimeSeconds);
		}

		if (bHasDebugInstance && CurrentTimeSeconds < DebugEndTimeSeconds)
		{
			RemainingDurationSeconds = FMath::Max(RemainingDurationSeconds, DebugEndTimeSeconds - CurrentTimeSeconds);
		}

		return RemainingDurationSeconds;
	}
};

UCLASS(ClassGroup = (Survival), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectSurvivalStatusComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ForceRefresh();

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	bool ApplyStatus(FName StatusName, float DurationOverride, AActor* SourceActor);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ClearStatus(FName StatusName);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ClearAllDebugStatuses();

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	bool CycleDebugStatus();

	UFUNCTION(BlueprintCallable, Category = "Survival|Status|Debug")
	bool ApplyDebugStatus(FName StatusName, bool bBypassImmunity = true);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status|Debug")
	bool ApplyDebugStatuses(const TArray<FName>& StatusNames, bool bBypassImmunity = true);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void SetForcedStatusActive(FName StatusName, bool bActive);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void SetStatusImmunitySource(FName SourceId, const TArray<FName>& StatusNames);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void ClearStatusImmunitySource(FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	void TriggerExhaustionSequence(float DurationOverrideSeconds);

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool IsStatusActive(FName StatusName) const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool IsStatusImmune(FName StatusName) const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool IsBlackoutActive() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool IsHealthRecoveryBlocked() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	float GetNeedDecayMultiplier(FName NeedName) const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	float GetSensationDeltaPerSecond(FName SensationName) const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	bool HasResolvedHealthBinding() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	float GetCurrentHealthValue() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	FString GetResolvedHealthPropertyName() const;

	UFUNCTION(BlueprintPure, Category = "Survival|Status")
	TArray<FProjectSurvivalStatusSnapshot> BuildActiveStatusSnapshots() const;

	UFUNCTION(BlueprintCallable, Category = "Survival|Status")
	TArray<FProjectSurvivalStatusSnapshot> BuildVisibleStatusSnapshots(int32 MaxVisibleStatuses, int32& OutOverflowCount) const;

	static TArray<FProjectSurvivalStatusSnapshot> SelectVisibleStatusSnapshotsForHud(
		const TArray<FProjectSurvivalStatusSnapshot>& ActiveSnapshots,
		int32 MaxVisibleStatuses,
		int32& OutOverflowCount);

#if WITH_DEV_AUTOMATION_TESTS
	static float AutomationResolveInvertedMovementInput(float RawInput);
	static bool AutomationEvaluateThresholdStatus(
		bool bWasActive,
		float NormalizedValue,
		EProjectSurvivalStatusThresholdMode ThresholdMode,
		float ActivationThresholdNormalized,
		float DeactivationThresholdNormalized);
#endif

	UPROPERTY(BlueprintAssignable, Category = "Survival|Status")
	FProjectSurvivalStatusChangedSignature OnStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Survival|Status")
	FProjectSurvivalBlackoutChangedSignature OnBlackoutChanged;

protected:
	UFUNCTION()
	void HandleOwnerDamageApplied(AActor* SourceActor, FName DamageType, float RequestedDamage, float AppliedDamage, float RemainingValue, bool bKilledTarget);

	void ResolveDependencies();
	void BindDamageObserver();
	void UnbindDamageObserver();
	void InitializeRuntimeStates();
	bool NeedsDependencyRefresh() const;
	void MarkStatusAttributeModifiersDirty();
	void UpdateStatuses(float DeltaTime);
	void RefreshStatusAttributeModifiers();
	void RefreshStatusMovementModifiers();
	void UpdateInvertedMovementInput(float DeltaTime);
	bool IsTimedStatusActive(FName StatusName, float CurrentTimeSeconds, float* OutRemainingDurationSeconds = nullptr) const;
	bool ApplyTimedStatusInstance(FName StatusName, float DurationSeconds, AActor* SourceActor, bool bDebugInstance);
	void ClearTimedStatusInstance(FName StatusName, bool bClearGameplayInstance, bool bClearDebugInstance);
	void PruneExpiredTimedStatuses(float CurrentTimeSeconds);
	void ApplyHealthRecoveryBlock(bool bBlocked);
	void ApplyPeriodicStatusDamage(FName StatusName, float DamageAmount);
	void StartExhaustionSequence(float DurationOverrideSeconds = -1.f);
	void FinishExhaustionSequence();
	class UTexture2D* LoadIconTexture(const FProjectSurvivalStatusDefinition& Definition);
	class UTexture2D* CreateProceduralIconTexture(FName MinimalIconName) const;
	bool ResolveHealthBinding();
	float ReadCurrentHealthValue() const;
	bool ApplyHealthDelta(float DeltaAmount);
	void RefreshHealthRecoveryCeiling(bool bResetToCurrentValue);
	void EnforceHealthRecoveryBlock();

private:
	UPROPERTY(Transient)
	TObjectPtr<class UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectCombatAttributeComponent> CombatAttributeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectSurvivalAttributeBridgeComponent> AttributeBridgeComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectLocomotionOverrideComponent> LocomotionOverrideComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectCombatAttributeComponent> BoundDamageObservedCombatAttributeComponent;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<class UTexture2D>> LoadedIconTextures;

	TArray<FProjectSurvivalStatusDefinition> RuntimeStatusDefinitions;
	TMap<FName, FProjectSurvivalStatusDefinition> StatusDefinitionsByName;
	TMap<FName, bool> ActiveStatusByName;
	TMap<FName, float> PendingDamageByStatus;
	TMap<FName, FProjectSurvivalTimedStatusRuntime> TimedStatusByName;
	TMap<FName, TSet<FName>> StatusImmunitiesBySource;
	TSet<FName> ForcedActiveStatusNames;
	TSet<FName> DebugAppliedStatusNames;
	TSet<FName> DebugBypassImmunityStatusNames;
	FName CurrentDebugCycleStatusName = NAME_None;
	FProjectSurvivalResolvedStatusHealthBinding ResolvedHealthBinding;
	bool bDependenciesResolved = false;
	bool bRuntimeStatesInitialized = false;
	bool bHealthRecoveryBlocked = false;
	bool bExhaustionSequenceActive = false;
	bool bInvertedMovementInputApplied = false;
	bool bStatusAttributeModifiersDirty = true;
	bool bStatusMovementModifiersDirty = true;
	float ExhaustionRemainingSeconds = 0.f;
};
