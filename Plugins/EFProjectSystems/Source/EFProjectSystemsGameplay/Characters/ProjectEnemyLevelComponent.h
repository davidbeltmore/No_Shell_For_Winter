#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Characters/ProjectEnemyCombatStatTypes.h"
#include "ProjectEnemyLevelComponent.generated.h"

class UProjectEnemyLevelSettings;
class USceneComponent;

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyLevelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectEnemyLevelComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Converts the Director's unbounded logical level to the only level that may
	 * be passed to ACF. Invalid logical levels resolve to zero and must fail the
	 * caller's validation instead of being silently promoted to level one.
	 */
	static int32 ResolvePhysicalAscentLevel(int32 LogicalLevel);

	void SetAssignedLevelData(int32 InWorldTier, int32 InMinRolledLevel, int32 InMaxRolledLevel, int32 InAssignedLevel, float InNormalizedLevel);
	bool SyncAssignedLevelToAscent(FString& OutDiagnosticMessage);
	bool CaptureGameplayScalingBaseline(const UProjectEnemyLevelSettings& Settings, FString& OutFailureReason);
	bool ApplyGameplayScaling(const UProjectEnemyLevelSettings& Settings, FString& OutFailureReason);
	void ResetGameplayScalingState();
	USceneComponent* EnsurePreferredTargetPoint(const UProjectEnemyLevelSettings& Settings, FString& OutFailureReason);

	/** Strict readiness check used by the V4 population transaction. */
	bool ValidateDirectorLevelState(int32 ExpectedLogicalLevel, FString& OutFailureReason) const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	bool HasAssignedLevel() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	int32 GetAssignedLevel() const;

	/** ACF-facing level; always min(LogicalLevel, 100) for valid assignments. */
	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	int32 GetPhysicalAscentLevel() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	int32 GetWorldTier() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	int32 GetMinRolledLevel() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	int32 GetMaxRolledLevel() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	float GetNormalizedLevel() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	USceneComponent* GetPreferredTargetPointComponent() const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	bool HasGameplayScalingBaseline() const;

	bool HasGameplayScalingAppliedForLevel(int32 ExpectedLogicalLevel) const;

	UFUNCTION(BlueprintPure, Category = "Project|EnemyLevel")
	bool TryGetDisplayHealthSnapshot(float& OutCurrentHealth, float& OutDisplayMaxHealth, float& OutDisplayRatio) const;

	bool TryGetCombatStatSnapshot(FProjectEnemyCombatStatSnapshot& OutSnapshot) const;

private:
	UFUNCTION()
	void OnRep_LevelData();

private:
	struct FGameplayScalingChannelState
	{
		float BaselineValue = 0.0f;
		float LastAppliedValue = 0.0f;
		bool bCaptured = false;
	};

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	int32 AssignedLevel = 0;

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	int32 WorldTier = 1;

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	int32 MinRolledLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	int32 MaxRolledLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	float NormalizedLevel = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_LevelData, VisibleInstanceOnly, Category = "Project|EnemyLevel")
	bool bLevelAssigned = false;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PreferredTargetPointComponent;

	TMap<FName, FGameplayScalingChannelState> ScalingChannels;
	TSet<FName> WarnedMissingChannels;
	float BaselineHealthMax = 0.0f;
	float BaselineHealthRatio = 1.0f;
	float AppliedSingleHealthCurrentDelta = 0.0f;
	bool bScalingBaselineCaptured = false;
	bool bHasHealthScalingBaseline = false;
	bool bGameplayScalingApplied = false;
	int32 GameplayScaledLevel = INDEX_NONE;
	bool bAscentSyncEvaluated = false;
	bool bAscentSyncSatisfied = false;
	int32 LastPhysicalAscentLevel = INDEX_NONE;
};
