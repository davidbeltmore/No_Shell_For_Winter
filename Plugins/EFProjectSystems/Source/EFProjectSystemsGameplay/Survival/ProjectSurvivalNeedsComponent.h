#pragma once

#include "Components/ActorComponent.h"
#include "ProjectSurvivalNeedsTypes.h"
#include "ProjectSurvivalNeedsComponent.generated.h"

class UProjectCombatAttributeComponent;
class UProjectSurvivalStatusComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FProjectSurvivalValueChangedSignature, FName, EntryName, float, OldValue, float, NewValue, float, MaxValue, bool, bIsSensation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectSurvivalPenaltyChangedSignature, float, OldPenaltyMultiplier, float, NewPenaltyMultiplier);

UCLASS(ClassGroup = (Survival), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalNeedsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectSurvivalNeedsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	void ResetToDefaults();

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool HasNeed(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetNeedCurrentValue(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetNeedNormalizedValue(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	int32 GetNeedBarsFilled(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetNeedMaxValue(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetNeedsPenaltyMultiplier() const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetNeedsPenaltyPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float ComputeAttributeValueAfterPenalty(float BaseValue) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float ComputeAttributeValueAfterPenaltyForName(FName AttributeName, float BaseValue) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetNeedCurrentValue(FName NeedName, float NewValue, bool bBroadcast = true);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float ModifyNeedValue(FName NeedName, float DeltaValue, bool bBroadcast = true);

	float ApplyNeedDeltaValue(FName NeedName, float DeltaValue, bool bBroadcast, bool bClampToRange);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetNeedMaxValue(FName NeedName, float NewMaxValue, bool bClampCurrentValue = true);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetNeedDecayRate(FName NeedName, float NewDecayPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetNeedRecoveryRate(FName NeedName, float NewRecoveryPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetExternalNeedDecayMultiplier(FName SourceId, FName NeedName, float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool ClearExternalNeedDecayMultiplier(FName SourceId, FName NeedName);

	UFUNCTION(BlueprintPure, Category = "Survival")
	float GetExternalNeedDecayMultiplier(FName NeedName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetSensationCurrentValue(FName SensationName, float NewValue, bool bBroadcast = true);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool HasSensation(FName SensationName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetSensationCurrentValue(FName SensationName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetSensationNormalizedValue(FName SensationName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float GetSensationMaxValue(FName SensationName) const;

	UFUNCTION(BlueprintCallable, Category = "Survival")
	bool SetSensationMaxValue(FName SensationName, float NewMaxValue, bool bClampCurrentValue = true);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	float ModifySensationValue(FName SensationName, float DeltaValue, bool bBroadcast = true);

	float ApplySensationDeltaValue(FName SensationName, float DeltaValue, bool bBroadcast, bool bClampToRange);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	void ApplyConsumableEffect(const FProjectSurvivalConsumableEffect& Effect);

	UFUNCTION(BlueprintPure, Category = "Survival")
	TArray<FProjectSurvivalNeedSnapshot> BuildNeedSnapshots() const;

	UFUNCTION(BlueprintPure, Category = "Survival")
	TArray<FProjectSurvivalSensationSnapshot> BuildSensationSnapshots() const;

	UFUNCTION(BlueprintPure, Category = "Survival")
	TArray<FProjectSurvivalAttributeProjection> BuildAttributeProjectionsFromCombatComponent(const UProjectCombatAttributeComponent* CombatComponent) const;

	UFUNCTION(BlueprintPure, Category = "Survival")
	bool HasAnyRegisteredNeeds() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	int32 BarsPerNeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PenaltyPerNeedAtZero;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bEnableAutoDecay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bClampToRangeByDefault;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival", meta = (ClampMin = "0.0"))
	float NeedsDecayMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalNeedState> Needs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalSensationState> Sensations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FName> AffectedSecondaryAttributes;

	UPROPERTY(BlueprintAssignable, Category = "Survival")
	FProjectSurvivalValueChangedSignature OnSurvivalValueChanged;

	UPROPERTY(BlueprintAssignable, Category = "Survival")
	FProjectSurvivalPenaltyChangedSignature OnPenaltyMultiplierChanged;

protected:
	virtual void ApplySettingsDefaults();
	virtual void InitializeDefaultState();
	virtual void SanitizeState();
	virtual float ComputePenaltyMultiplier() const;
	virtual float ClampToEntryRange(float Value, float MinValue, float MaxValue) const;
	virtual FProjectSurvivalNeedState* FindNeedMutable(FName NeedName);
	virtual const FProjectSurvivalNeedState* FindNeed(FName NeedName) const;
	virtual FProjectSurvivalSensationState* FindSensationMutable(FName SensationName);
	virtual const FProjectSurvivalSensationState* FindSensation(FName SensationName) const;
	virtual void UpdateCachedPenaltyMultiplier(bool bBroadcastChange);
	virtual void BroadcastValueChanged(FName EntryName, float OldValue, float NewValue, float MaxValue, bool bIsSensation);

private:
	float CachedPenaltyMultiplier;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	TMap<FName, TMap<FName, float>> ExternalNeedDecayMultipliersBySource;
};
