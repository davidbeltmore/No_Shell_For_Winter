#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "ACFTrainingTypes.h"
#include "ACFTrainingComponent.generated.h"

class UACFTrainingMinigameBase;
class UARSStatisticsComponent;
class UAnimationAsset;
struct FAttributesSetModifier;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFTrainingStartedSignature, FName, TrainingId, FGameplayTag, TargetAttribute);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FACFTrainingCompletedSignature, FName, TrainingId, EACFTrainingSessionResult, Result, FGameplayTag, TargetAttribute, float, RewardValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFTrainingRejectedSignature, FName, TrainingId, FText, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFTrainingRewardChangedSignature, FGameplayTag, TargetAttribute, float, AccumulatedReward);

UCLASS(ClassGroup = (ACF), meta = (BlueprintSpawnableComponent))
class ACFTRAININGSYSTEM_API UACFTrainingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UACFTrainingComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	bool StartTrainingById(FName TrainingId);

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	void CancelTraining();

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	void CompleteTrainingMinigame(bool bSuccess);

	UFUNCTION(BlueprintCallable, Category = "ACF Training|Timing Minigame")
	bool StartTimingMinigameSession(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter = 0.5f);

	UFUNCTION(BlueprintCallable, Category = "ACF Training|Timing Minigame")
	bool ConfirmTimingMinigame();

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	bool CanStartTraining(FName TrainingId, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	bool GetTrainingProgress(FName TrainingId, float& OutProgress) const;

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	bool GetTrainingDefinition(FName TrainingId, FACFTrainingDefinition& OutDefinition) const;

	UFUNCTION(BlueprintPure, Category = "ACF Training")
	TArray<FACFTrainingDefinition> GetAvailableTrainingDefinitions() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training")
	bool IsTrainingActive() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training")
	FName GetActiveTrainingId() const;

	UFUNCTION(BlueprintCallable, Category = "ACF Training")
	bool GetActiveTrainingDefinition(FACFTrainingDefinition& OutDefinition) const;

	UFUNCTION(BlueprintPure, Category = "ACF Training")
	float GetAccumulatedRewardForAttribute(FGameplayTag TargetAttribute) const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	bool IsTimingMinigameSessionActive() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	int32 GetActiveTimingSessionId() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	float GetActiveTimingSpeedMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	float GetActiveTimingTargetHalfRange() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	float GetActiveTimingTargetCenter() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	float GetCurrentTimingPulseValue() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Timing Minigame")
	bool IsCurrentTimingPulseInTargetRange() const;

	UFUNCTION(BlueprintCallable, Category = "ACF Training|Minigame")
	UACFTrainingMinigameBase* CreateMinigameForActiveTraining();

	UFUNCTION(BlueprintNativeEvent, Category = "ACF Training|Requirements")
	bool ResolveScalarGateValue(const FACFTrainingScalarGate& Gate, float& OutValue) const;
	virtual bool ResolveScalarGateValue_Implementation(const FACFTrainingScalarGate& Gate, float& OutValue) const;

	UFUNCTION(BlueprintNativeEvent, Category = "ACF Training|Requirements")
	bool CheckInventoryRequirements(const FACFTrainingDefinition& Definition, FText& OutReason) const;
	virtual bool CheckInventoryRequirements_Implementation(const FACFTrainingDefinition& Definition, FText& OutReason) const;

	UFUNCTION(BlueprintNativeEvent, Category = "ACF Training|Requirements")
	bool CheckNearbyActorRequirements(const FACFTrainingDefinition& Definition, FText& OutReason) const;
	virtual bool CheckNearbyActorRequirements_Implementation(const FACFTrainingDefinition& Definition, FText& OutReason) const;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training")
	FACFTrainingStartedSignature OnTrainingStarted;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training")
	FACFTrainingCompletedSignature OnTrainingCompleted;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training")
	FACFTrainingRejectedSignature OnTrainingRejected;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training")
	FACFTrainingRewardChangedSignature OnTrainingRewardChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (TitleProperty = "TrainingId"), Category = "ACF Training")
	TArray<FACFTrainingDefinition> TrainingDefinitionOverrides;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveTrainingId, BlueprintReadOnly, Category = "ACF Training")
	FName ActiveTrainingId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training")
	float ActiveTrainingStartTime;

	UPROPERTY(Replicated, BlueprintReadOnly, SaveGame, meta = (TitleProperty = "TrainingId"), Category = "ACF Training")
	TArray<FACFTrainingProgressEntry> TrainingProgress;

	UPROPERTY(Replicated, BlueprintReadOnly, SaveGame, meta = (TitleProperty = "TargetAttribute"), Category = "ACF Training")
	TArray<FACFTrainingAttributeRewardEntry> AccumulatedAttributeRewards;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	int32 ActiveTimingSessionId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	float ActiveTimingServerStartTimeSeconds;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	float ActiveTimingSpeedMultiplier;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	float ActiveTimingTargetHalfRange;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	float ActiveTimingTargetCenter;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	int32 ActiveTimingDifficulty;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ACF Training|Timing Minigame")
	int32 ActiveTimingCunningLevel;

	UPROPERTY(SaveGame)
	TMap<FName, float> LastFailureTimeByTrainingId;

	UFUNCTION()
	void OnRep_ActiveTrainingId();

	UFUNCTION(Server, Reliable)
	void ServerStartTrainingById(FName TrainingId);

	UFUNCTION(Server, Reliable)
	void ServerCancelTraining();

	UFUNCTION(Server, Reliable)
	void ServerCompleteTrainingMinigame(bool bSuccess);

	UFUNCTION(Server, Reliable)
	void ServerStartTimingMinigameSession(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter);

	UFUNCTION(Server, Reliable)
	void ServerConfirmTimingMinigame();

	bool StartTrainingAuthority(FName TrainingId);
	void CancelTrainingAuthority();
	void CompleteTrainingAuthority(bool bSuccess);
	bool StartTimingMinigameSessionAuthority(int32 Difficulty, int32 CunningLevel, float SpeedMultiplier, float TargetHalfRange, float TargetCenter);
	bool ConfirmTimingMinigameAuthority();
	void ClearTimingMinigameSession();

	const FACFTrainingDefinition* FindTrainingDefinition(FName TrainingId) const;
	const TArray<FACFTrainingDefinition>& GetTrainingDefinitionSource() const;
	TArray<FACFTrainingDefinition> ResolveTrainingDefinitions() const;
	UARSStatisticsComponent* FindStatisticsComponent() const;

	bool ValidateScalarRequirements(const FACFTrainingDefinition& Definition, FText& OutReason) const;
	bool GrantTrainingReward(const FACFTrainingDefinition& Definition);
	bool ReapplyTrainingRewardModifier();
	FAttributesSetModifier BuildAccumulatedRewardModifier() const;
	float* FindTrainingProgressValue(FName TrainingId);
	const float* FindTrainingProgressValue(FName TrainingId) const;
	float& FindOrAddTrainingProgressValue(FName TrainingId);
	float* FindAttributeRewardValue(FGameplayTag TargetAttribute);
	const float* FindAttributeRewardValue(FGameplayTag TargetAttribute) const;
	float& FindOrAddAttributeRewardValue(FGameplayTag TargetAttribute);
	float ResolveServerWorldTimeSeconds() const;
	static float ComputeTimingPulseValue(float ElapsedSeconds, float SpeedMultiplier);

private:
	FActiveGameplayEffectHandle AccumulatedRewardModifierHandle;
	int32 LastIssuedTimingSessionId;
};
