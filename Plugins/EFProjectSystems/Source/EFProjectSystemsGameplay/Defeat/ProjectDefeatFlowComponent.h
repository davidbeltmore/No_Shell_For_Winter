#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Defeat/ProjectDefeatInventoryBridge.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectDefeatFlowComponent.generated.h"

class UInputComponent;
class UActorComponent;
class UBrainComponent;
class AActor;
class ACharacter;
class APlayerController;
class USkeletalMeshComponent;
class UProjectileMovementComponent;
struct FProjectIncomingHitContext;
class UProjectDefeatBlueprintBridgeComponent;
class UProjectCombatAttributeComponent;
class UProjectEmoteComponent;
class UProjectKnockoutStruggleWidget;
class UProjectLocomotionOverrideComponent;
class UProjectPainDebugWidget;
class UProjectRealtimeSnapshotComponent;
class UProjectInnerDoctrineComponent;
class UProjectSurvivalNeedsComponent;
class UProjectSurvivalStatusComponent;
struct FProjectEmoteRuntimeActionRequest;
enum class EProjectEmoteRuntimeActionEndReason : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectKnockoutStateChangedSignature, EProjectDefeatPhase, NewPhase, EProjectKnockoutReason, KnockoutReason, bool, bActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectStruggleRoundStartedSignature, const FProjectStruggleRound&, Round);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectStruggleRoundFinishedSignature, const FProjectStruggleRound&, Round, bool, bWonRound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectDefeatedSceneChangedSignature, const FProjectDefeatSceneDefinition&, SceneDefinition, bool, bActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectDefeatStateRefreshedSignature);

UCLASS(ClassGroup = (Defeat), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefeatFlowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectDefeatFlowComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void NotifyDamageReceived(const FProjectIncomingHitContext& HitContext);
	bool TryHandleLethalDamage(const FProjectIncomingHitContext& HitContext, float& OutSurvivingHealth);
	void HandleDefeatedArrivalFromTravel(const FProjectDefeatTransferPayload& Payload, const FProjectDefeatInventorySnapshot& RetainedInventorySnapshot, const FTransform& SpawnTransform);

	UFUNCTION(BlueprintPure, Category = "Defeat")
	EProjectDefeatPhase GetCurrentPhase() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	EProjectKnockoutReason GetCurrentKnockoutReason() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	EProjectDefeatReason GetCurrentDefeatReason() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	EProjectPostDefeatPresentation GetCurrentPostDefeatPresentation() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	bool IsKnockedOut() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	bool IsLosingActive() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	bool IsAdvancedDefeatFlowEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "Defeat")
	bool RequestKnockoutOrPendingCrawl(EProjectKnockoutReason KnockoutReason, FName SourceId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Defeat")
	bool RequestTacticalRetreat(FName SourceId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Defeat")
	bool TryRecoverFromKnockoutIfNoNearbyEnemy(FName SourceId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Defeat|Debug")
	bool DebugTriggerImmediateDefeat(FName SourceId = NAME_None);

	UFUNCTION(BlueprintPure, Category = "Defeat")
	bool HasNearbyQualifiedStruggleEnemy() const;

	UFUNCTION(BlueprintPure, Category = "Defeat")
	bool IsPendingCrawlKnockout() const;

	UFUNCTION(BlueprintPure, Category = "Defeat|Debug")
	FProjectPainDebugSnapshot BuildPainDebugSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Defeat|Debug")
	FText BuildPainDebugText() const;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectKnockoutStateChangedSignature OnKnockoutStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectStruggleRoundStartedSignature OnStruggleRoundStarted;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectStruggleRoundFinishedSignature OnStruggleRoundFinished;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatedSceneChangedSignature OnDefeatedSceneChanged;

	UPROPERTY(BlueprintAssignable, Category = "Defeat")
	FProjectDefeatStateRefreshedSignature OnDefeatStateRefreshed;

#if WITH_DEV_AUTOMATION_TESTS
	void AutomationCompleteActiveStruggleRound(bool bSuccess);
	int32 AutomationGetActiveCombatSessionId() const;
	int32 AutomationGetLastKnockoutCombatSessionId() const;
	int32 AutomationGetPendingStruggleRoundCount() const;
	void AutomationRequestDefeatedSceneCancel();
	void AutomationStartDefeatedSceneWithoutTravel();
	void AutomationRecoverFromKnockout();
#endif

private:
	struct FProjectFrozenComponentTickState
	{
		TWeakObjectPtr<UActorComponent> Component;
		bool bTickEnabled = false;
	};

	struct FProjectFrozenProjectileState
	{
		TWeakObjectPtr<UProjectileMovementComponent> ProjectileMovementComponent;
		FVector SavedVelocity = FVector::ZeroVector;
		bool bTickEnabled = false;
	};

	struct FProjectFrozenEnemyState
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<class AAIController> AIController;
		TWeakObjectPtr<UBrainComponent> BrainComponent;
		TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
		TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;
		bool bBrainWasRunning = false;
		bool bMovementTickEnabled = false;
		EMovementMode MovementMode = MOVE_Walking;
		uint8 CustomMovementMode = 0;
		bool bPausedAnims = false;
		float AnimRateScale = 1.f;
	};

	struct FProjectStruggleFreezeContext
	{
		TWeakObjectPtr<ACharacter> PlayerCharacter;
		TWeakObjectPtr<UCharacterMovementComponent> PlayerMovementComponent;
		EMovementMode PlayerMovementMode = MOVE_Walking;
		uint8 PlayerCustomMovementMode = 0;
		bool bPlayerMovementTickEnabled = false;
		bool bPlayerMovementCaptured = false;
		bool bRemovedLocomotionTickPrerequisite = false;
		TArray<FProjectFrozenComponentTickState> FrozenPlayerComponents;
		TArray<FProjectFrozenEnemyState> FrozenEnemies;
		TArray<FProjectFrozenProjectileState> FrozenProjectiles;

		void Reset()
		{
			PlayerCharacter.Reset();
			PlayerMovementComponent.Reset();
			PlayerMovementMode = MOVE_Walking;
			PlayerCustomMovementMode = 0;
			bPlayerMovementTickEnabled = false;
			bPlayerMovementCaptured = false;
			bRemovedLocomotionTickPrerequisite = false;
			FrozenPlayerComponents.Reset();
			FrozenEnemies.Reset();
			FrozenProjectiles.Reset();
		}
	};

	void RefreshDependencies();
	bool ResolveQualifiedEnemyHit(const FProjectIncomingHitContext& HitContext, struct FProjectDefeatHitResolution& OutResolution) const;
	bool IsQualifiedEnemyActor(const AActor* Actor) const;
	int32 CountNearbyQualifiedStruggleEnemies() const;
	void InvalidateNearbyEnemyCache() const;
	void EnsureCombatSessionActive(float EventTimeSeconds);
	void RegisterCombatEvent(float EventTimeSeconds, bool bQualifiedImpact);
	void EndCombatSession();
	void RefreshCombatFlags();
	bool SetLosingActive(bool bNewValue);
	void BroadcastStateRefresh();
	void LogDefeatTransition(const TCHAR* EventLabel, AActor* ContextActor) const;
	void UpdatePainDecay(float DeltaTime);
	void UpdateDefeatCombatWindow();
	void UpdateKnockoutRecoveryState();
	void UpdatePendingCrawlKnockout();
	bool IsDefeatCombatWindowActive() const;
	void EvaluatePainThreshold();
	void EnterKnockedOut(EProjectKnockoutReason KnockoutReason);
	void BeginWaitingOutOfCombatRecovery();
	void BeginStruggleFreeze();
	void BeginStruggleGameplayFreeze();
	void EndStruggleGameplayFreeze();
	void CaptureFrozenComponentTick(UActorComponent* Component);
	void CaptureFrozenEnemy(AActor* EnemyActor);
	void CaptureFrozenProjectiles(float Radius);
	void FreezeStruggleQueue();
	void EnforceMinimumTotalStruggleNotes();
	FProjectStruggleRound BuildStruggleRound(AActor* EnemyActor, int32 RoundIndex) const;
	void StartNextStruggleRound();
	void HandleStruggleRoundCompleted(
		bool bSuccess,
		bool bPlayerCompletedMinigame,
		const FProjectStruggleRound& CompletedRound);
	void FinishKnockoutRecovery(bool bApplyInventoryPenalty);
	void EnterDefeated(
		EProjectDefeatReason DefeatReason,
		AActor* InstigatorActor,
		bool bPlayerCompletedStruggleMinigame = false,
		bool bTechnicalFailure = false,
		bool bCancelledBeforeResolution = false);
	void TriggerPendingTravel();
	void ScheduleDefeatedArrivalSceneStart();
	void StartSettledDefeatedArrivalScene();
	void CompleteDefeatedRespawnWithoutPresentation();
	void StartDefeatedScene(const FProjectDefeatSceneDefinition& SceneDefinition);
	bool StartDefeatedSceneRuntimeAction(const FProjectDefeatSceneDefinition& SceneDefinition);
	void StopDefeatedScene(bool bCancelledByPlayer);
	void HandleDefeatedSceneCancelPressed();
	void HandleRuntimeActionEnded(const FProjectEmoteRuntimeActionRequest& Request, EProjectEmoteRuntimeActionEndReason EndReason);
	void UnbindRuntimeActionEvents();
	void FinalizeDefeatedSceneCancel();
	void ScheduleDeferredMovementRestore(float DelaySeconds);
	void ScheduleDefeatedCancelMovementRestore();
	void RestoreDeferredPlayerMovement();
	void RestorePostDefeatGameplayState(bool bDeferMovementRestore, const TCHAR* Reason);
	void ResetDefeatFlowAfterRespawn(const TCHAR* EventLabel);
	void ClearDefeatStatusAndLocomotion();
	void ClampCurseAfterDefeat() const;
	bool IsMatureDefeatAllowedByPolicy() const;
	void ApplyDefeatedSceneInputLock();
	void ReleaseDefeatedSceneInputLock(bool bForceWalking);
	void ReleaseDefeatedSceneControllerState();
	FProjectDefeatCameraInputSnapshot CaptureCameraInputSnapshot() const;
	void RestoreCameraInputSnapshot(const FProjectDefeatCameraInputSnapshot& Snapshot, const TCHAR* Reason) const;
	void RefreshCinematicCameraManager(const TCHAR* Reason) const;
	void ApplyKnockoutLocomotion(bool bEnabled);
	void SuspendKnockoutLocomotionForStruggle(bool bSuspend);
	void ApplyTransientInvulnerability(bool bEnabled);
	void ApplyCharacterMeshCollisionSuppression(bool bEnabled);
	void ApplyGameplayInputSuppression(bool bEnabled, bool bRestoreDefaultInputModeWhenReleasing = true);
	void ApplyStruggleInputBlock(bool bEnabled);
	void StartPlayerCameraFade(float FromAlpha, float ToAlpha, float DurationSeconds, bool bHoldWhenFinished) const;
	void EnsureSceneInputBinding();
	void RemoveSceneInputBinding();
	void EnsureStruggleWidget();
	void EnsurePainDebugWidget();
	void RemoveStruggleWidget();
	void RemovePainDebugWidget();
	void RestoreDefaultInputMode();
	void PacifyEnemiesTargetingOwnerWhileDowned() const;
	float GetPainCurrent() const;
	float GetPainMax() const;
	float GetPainThreshold() const;
	void SetPainCurrent(float NewValue) const;
	int32 ResolveCunningLevel() const;
	int32 ResolveEnemyLevel(const AActor* EnemyActor) const;
	int32 ResolveEnemyDifficultyWeight(const AActor* EnemyActor) const;
	int32 BuildRoundSeed(const AActor* EnemyActor, int32 RoundIndex) const;
	UProjectLocomotionOverrideComponent* EnsureLocomotionOverrideComponent();
	UProjectEmoteComponent* EnsureEmoteComponent();
	USkeletalMeshComponent* ResolveOwnerCharacterMesh() const;
	APlayerController* ResolveOwningPlayerController() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> CombatAttributeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectRealtimeSnapshotComponent> RealtimeSnapshotComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> InnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLocomotionOverrideComponent> LocomotionOverrideComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectEmoteComponent> EmoteComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectDefeatBlueprintBridgeComponent> BlueprintBridgeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectKnockoutStruggleWidget> StruggleWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectPainDebugWidget> PainDebugWidget;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> SceneInputComponent;

	EProjectDefeatPhase CurrentPhase = EProjectDefeatPhase::None;
	EProjectKnockoutReason CurrentKnockoutReason = EProjectKnockoutReason::None;
	EProjectDefeatReason CurrentDefeatReason = EProjectDefeatReason::None;
	FProjectStruggleRound ActiveRound;
	FProjectStruggleFreezeContext StruggleFreezeContext;
	FProjectDefeatTransferPayload PendingTransferPayload;
	FProjectDefeatCameraInputSnapshot ActiveCameraInputSnapshot;
	FProjectDefeatInventorySnapshot PendingRetainedInventorySnapshot;
	TArray<FProjectStruggleRound> PendingStruggleRounds;
	FTimerHandle StruggleFreezeTimerHandle;
	FTimerHandle TravelDelayTimerHandle;
	FTimerHandle DefeatedArrivalSceneStartTimerHandle;
	FTimerHandle SceneCancelTimerHandle;
	FTimerHandle DeferredMovementRestoreTimerHandle;
	FDelegateHandle RuntimeActionEndedDelegateHandle;
	float LastQualifiedImpactTimeSeconds = -FLT_MAX;
	float LastCombatEventTimeSeconds = -FLT_MAX;
	float PainDecayAccumulatorSeconds = 0.f;
	float KnockoutEnteredTimeSeconds = 0.f;
	float OutOfCombatRecoveryStartTimeSeconds = -1.f;
	float LastDebugHitTimeSeconds = 0.f;
	float LastDebugPainDelta = 0.f;
	int32 ActiveCombatSessionId = 0;
	int32 LastKnockoutCombatSessionId = 0;
	int32 NextCombatSessionId = 1;
	int32 NextStruggleRoundIndex = 0;
	int32 LastInventoryPenaltySeed = 0;
	bool bLosingActive = false;
	bool bHadKnockoutThisCombat = false;
	bool bStruggleQueueFrozen = false;
	bool bStruggleFreezeActive = false;
	bool bWaitingOutOfCombatRecovery = false;
	bool bStruggleWonThisKnockout = false;
	bool bKnockoutLocomotionActive = false;
	bool bKnockoutWalkWasEnabled = false;
	bool bKnockoutLocomotionSuspended = false;
	bool bChangedCanBeDamaged = false;
	bool bPreviousCanBeDamaged = true;
	bool bChangedCharacterMeshCollision = false;
	bool bPreviousCharacterMeshGenerateOverlapEvents = false;
	bool bLastDebugHitQualified = false;
	bool bPendingCrawlKnockout = false;
	TEnumAsByte<ECollisionEnabled::Type> PreviousCharacterMeshCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	TWeakObjectPtr<USkeletalMeshComponent> SuppressedCharacterMeshComponent;
	FName PreviousCharacterMeshCollisionProfileName = NAME_None;
	FName PendingCrawlKnockoutSourceId = NAME_None;
	FName LastDebugDamageType = NAME_None;
	FName ActiveDefeatedRuntimeActionId = NAME_None;
	EProjectKnockoutReason PendingCrawlKnockoutReason = EProjectKnockoutReason::None;
	FString LastDebugSourceActorName;
	FString LastDebugDamageCauserName;
	FString LastDebugResolvedActorName;
	FString LastDebugQualificationReason;
	bool bStoppingDefeatedScene = false;
	bool bDefeatedSceneInputLockApplied = false;
	bool bPendingDefeatedArrivalCameraRefresh = false;
	mutable float CachedNearbyEnemyWorldTimeSeconds = -FLT_MAX;
	mutable int32 CachedNearbyEnemyCount = 0;
};
