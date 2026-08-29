#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/ProjectCombatTypes.h"
#include "Defeat/ProjectDefeatCurseClampInterface.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineComponent.generated.h"

class UACFEffectsManagerComponent;
class UProjectCombatAttributeComponent;
class UProjectDefeatFlowComponent;
class UProjectLocomotionOverrideComponent;
class UProjectRealtimeSnapshotComponent;
class UProjectSurvivalNeedsComponent;
class UProjectSurvivalStatusComponent;
struct FACFDamageEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FProjectDoctrineDxpChangedSignature,
	int32,
	OldCurrentRunDxp,
	int32,
	NewCurrentRunDxp,
	int32,
	OldMetaBankDxp,
	int32,
	NewMetaBankDxp);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FProjectDoctrineAttributeLevelChangedSignature,
	EProjectDoctrineAttribute,
	Attribute,
	int32,
	OldLevel,
	int32,
	NewLevel,
	int32,
	NextLevelCost);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProjectDoctrineMilestoneTriggeredSignature,
	FName,
	AbilityId,
	EProjectDoctrineAttribute,
	Attribute,
	int32,
	Level);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FProjectCurseWarningSignature,
	float,
	Threshold,
	float,
	CurrentCurse);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectCursedStateChangedSignature, bool, bCursed);

UCLASS(ClassGroup = (InnerDoctrine), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineComponent
	: public UActorComponent
	, public IProjectDefeatCurseClampReceiver
{
	GENERATED_BODY()

public:
	UProjectInnerDoctrineComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	int32 GetCurrentRunDxp() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	int32 GetMetaBankDxp() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	bool IsDoctrineMasteryModeEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	int32 GetAttributeLevel(EProjectDoctrineAttribute Attribute) const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	int32 GetUpgradeCost(EProjectDoctrineAttribute Attribute) const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	FProjectInnerDoctrineSnapshot BuildSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|DXP")
	int32 GrantDxp(FName ReasonId, int32 Amount, EProjectDoctrineExperienceSource Source);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|DXP")
	int32 GrantDxpWithAttributeAffinity(
		FName ReasonId,
		int32 Amount,
		EProjectDoctrineExperienceSource Source,
		const TArray<EProjectDoctrineAttribute>& Affinities);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	bool SpendDxpOnAttribute(EProjectDoctrineAttribute Attribute);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	bool ApplyFreeAttributeLevels(EProjectDoctrineAttribute Attribute, int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	void ResetRunProgressForBackgroundChange();

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void SetDxpGainMultipliers(const TMap<EProjectDoctrineAttribute, float>& InMultipliers);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void ClearDxpGainMultipliers();

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void SetExternalDxpGainMultiplier(FName SourceId, float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void ClearExternalDxpGainMultiplier(FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void SetExternalFlatDamageBonus(FName SourceId, float FlatDamageBonus);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|External Effects")
	void ClearExternalFlatDamageBonus(FName SourceId);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine")
	float GetDxpGainMultiplierForAttribute(EProjectDoctrineAttribute Attribute) const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	int32 WithdrawMetaDxp(int32 RequestedAmount);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	void HandleRunDeath();

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine")
	void SetDoctrineMasteryMode(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Curse")
	FProjectCurseApplicationResult ApplyCurse(const FProjectCurseApplicationContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Curse")
	float CleanseCurse(float Amount, bool bServiceOrShrine = false);

	/**
	 * Canonical bridge for completed NPC services and shrine interactions.
	 * Integration status: PENDING callers in the owning service/sanctuary
	 * systems; no authoritative completion event is currently project-owned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Hooks")
	float NotifyServiceOrShrineCleanse(float Amount, FName ServiceSourceId);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Curse")
	float GetCurse() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Curse")
	float GetCurseMax() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Curse")
	bool IsCursed() const;

	/**
	 * Adds or removes a source-owned suppression of Curse's passive decay.
	 * Explicit cleansing through CleanseCurse remains available while suppressed.
	 * Returns false for NAME_None or when the requested state was already set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Curse")
	bool SetPassiveCurseDecaySuppressed(FName SourceId, bool bSuppressed);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Curse")
	bool IsPassiveCurseDecaySuppressed() const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Curse")
	bool RegisterCurseZonePresence(FGuid ZonePresenceToken);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Curse")
	bool UnregisterCurseZonePresence(FGuid ZonePresenceToken);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Curse")
	int32 GetActiveCurseZoneCount() const;

	/**
	 * Idempotent bridge for an authoritative unique floor transition.
	 * Integration status: PENDING caller in the procedural floor-transition
	 * owner; dungeon initialization is not equivalent to floor completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Hooks")
	bool NotifyDungeonFloorCompleted(FName FloorTransitionId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Hooks")
	bool NotifySleepCompleted(FName SleepSourceId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Defensive")
	bool TryActivateTacticalRetreat();

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Defensive")
	bool IsGuardRecoveryActive() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|Defensive")
	bool ShouldDeferPainKnockoutForGuardRecovery() const;

	virtual void ClampCurseAfterDefeat_Implementation(float MaximumCurse) override;

	float ModifyIncomingHealthDamage(
		AActor* SourceActor,
		FName DamageType,
		float PostArmorDamage,
		float& OutFlatNegatedDamage,
		float& OutGuardAbsorbedDamage);
	bool TryPreventDeathFromDamage(const FProjectIncomingHitContext& HitContext, float CurrentHealth, float& OutSurvivingHealth);
	void ModifyOutgoingDamageSpec(AActor* TargetActor, FProjectCombatDamageSpec& InOutDamageSpec);
	void NotifyDamageDealt(AActor* TargetActor, const FProjectCombatDamageResult& DamageResult);
	void NotifyDamageReceived(const FProjectIncomingHitContext& HitContext);

	static float ComputeCurseResistanceMultiplier(
		const FProjectCurseApplicationContext& Context,
		int32 WillpowerLevel,
		int32 FaithLevel,
		int32 CunningLevel,
		bool bRallyingPresenceActive,
		const class UProjectInnerDoctrineSettings* Settings);

	static float ComputeExhaustedRecoveryPenaltyMitigation(
		bool bMomentumUnlocked,
		bool bRecoveredMomentumActive,
		float MomentumMitigation);

	static bool IsCanonicalCurseSourceActor(const AActor* Actor);

	UPROPERTY(BlueprintAssignable, Category = "Inner Doctrine")
	FProjectDoctrineDxpChangedSignature OnDxpChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inner Doctrine")
	FProjectDoctrineAttributeLevelChangedSignature OnAttributeLevelChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inner Doctrine")
	FProjectDoctrineMilestoneTriggeredSignature OnMilestoneTriggered;

	UPROPERTY(BlueprintAssignable, Category = "Inner Doctrine|Curse")
	FProjectCurseWarningSignature OnCurseWarning;

	UPROPERTY(BlueprintAssignable, Category = "Inner Doctrine|Curse")
	FProjectCursedStateChangedSignature OnCursedStateChanged;

#if WITH_DEV_AUTOMATION_TESTS
	void AutomationSetCurse(float NewCurse);
	void AutomationSetCombatState(bool bActive);
	void AutomationCompleteCursedEpisode();
	bool AutomationIsRecoveredMomentumActive() const;
	void AutomationExpireRecoveredMomentum();
	float AutomationGetGuardPoolRemaining() const;
	float AutomationGetLastResolvedCurseResistanceMultiplier() const;
#endif

private:
	void RefreshCachedComponents();
	void InitializeAttributeStorage();
	void LoadPersistentState();
	void SavePersistentState() const;
	void BroadcastDxpChanged(int32 OldCurrentRunDxp, int32 OldMetaBankDxp);
	void ResetRunAttributes();
	void ApplyPassiveAttributeEffects();
	void ApplyDynamicMaximums();
	void ApplyDynamicNeedMaximum(FName NeedName, float NewMaxValue);
	void ApplyDynamicSensationMaximum(FName SensationName, float NewMaxValue);
	float CalculateDynamicMaximum(FName EntryName, bool bIsSensation, float BaseMax) const;
	float ResolveBaseNeedMax(FName NeedName) const;
	float ResolveBaseSensationMax(FName SensationName) const;
	void ApplyWillpowerStatusImmunities();
	void ApplyFaithPassiveAttributeEffects();
	void UpdateFaithRecovery(float DeltaTime);
	void UpdateCombatState();
	void MarkCombatImpact();
	void UpdateCurse(float DeltaTime);
	bool StartCursedEpisode();
	void CompleteCursedEpisode(bool bApplyNormalRecovery);
	void CancelCursedEpisode();
	float ResolveCursedDurationSeconds() const;
	float ResolveCurseResistanceMultiplier(const FProjectCurseApplicationContext& Context) const;
	bool HasRallyingPresence() const;
	bool ConsumeCunningTrapProtection(EProjectCurseSourceKind SourceKind);
	void RecordCurseApplicationId(const FGuid& ApplicationId);
	void BroadcastCrossedCurseWarnings(float OldCurse, float NewCurse);
	void SetCurse(float NewCurse);
	float ModifySensation(FName SensationName, float Delta);
	float GetSensationCurrent(FName SensationName) const;
	float GetSensationMax(FName SensationName) const;
	void SetSensationCurrent(FName SensationName, float NewValue);
	void SetForcedStatus(FName StatusName, bool bActive);
	bool HasMilestone(EProjectDoctrineAttribute Attribute, int32 Level) const;
	bool IsAttributeIndexValid(EProjectDoctrineAttribute Attribute) const;
	FText GetAttributeDisplayName(EProjectDoctrineAttribute Attribute) const;
	float ResolveBestDxpGainMultiplier(const TArray<EProjectDoctrineAttribute>& Affinities) const;
	float ResolveExternalDxpGainMultiplier() const;
	float ResolveExternalFlatDamageBonus() const;
	bool IsCurseSourceActor(const AActor* Actor) const;
	bool IsRelevantEnemyActor(const AActor* Actor) const;
	void TryTriggerCleanFinish(AActor* TargetActor, const FProjectCombatDamageResult& DamageResult);
	void UpdateTimedDoctrineEffects();
	void ApplyLocomotionDoctrineModifiers();
	bool IsPlayerTravelStateOwner() const;
	void TryRestoreTravelCurseState();
	void StoreTravelCurseState();
	bool TryStartGuardRecovery(AActor* SourceActor);
	void CompleteGuardRecovery();
	float ResolveOwnerMaxHealth() const;
	void EmitPainSpike();
	void TriggerGuardRecoveryHitFeedback(AActor* SourceActor);
	void QueueGuardRecoveryDamageTextSuppression();
	void SuppressGuardRecoveryDamageTextWidgets();
	void PlayGuardRecoveryHitSound(const FACFDamageEvent& DamageEvent);
	TSubclassOf<class UACFDamageType> ResolveGuardRecoveryFeedbackDamageClass(AActor* SourceActor) const;
	void AddMilestoneSnapshot(
		TArray<FProjectDoctrineMilestoneState>& OutMilestones,
		const FProjectDoctrineMilestoneDefinition& Definition) const;
	void BroadcastMilestonesForLevelRange(EProjectDoctrineAttribute Attribute, int32 OldLevel, int32 NewLevel);

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalNeedsComponent> NeedsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSurvivalStatusComponent> StatusComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> CombatAttributeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectRealtimeSnapshotComponent> RealtimeSnapshotComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectDefeatFlowComponent> DefeatFlowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLocomotionOverrideComponent> LocomotionOverrideComponent;

	UPROPERTY(Transient)
	TObjectPtr<UACFEffectsManagerComponent> AcfEffectsManagerComponent;

	UPROPERTY(Transient)
	TArray<int32> AttributeLevels;

	TMap<EProjectDoctrineAttribute, float> DxpGainMultipliersByAttribute;
	TMap<FName, float> ExternalDxpGainMultipliersBySource;
	TMap<FName, float> ExternalFlatDamageBonusesBySource;
	TMap<FName, float> RuntimeBaseNeedMaxByName;
	TMap<FName, float> RuntimeBaseSensationMaxByName;
	TSet<FName> ProcessedFloorTransitionIds;
	TSet<FName> PassiveCurseDecaySuppressionSources;
	TSet<FGuid> ActiveCurseZonePresenceTokens;
	TSet<FGuid> ProcessedCurseApplicationIds;
	TArray<FGuid> ProcessedCurseApplicationOrder;

	int32 CurrentRunDxp = 0;
	int32 MetaBankDxp = 0;
	bool bDoctrineMasteryMode = false;
	bool bCombatStateActive = false;
	bool bCursedEpisodeActive = false;
	bool bGuardRecoveryActive = false;
	bool bCunningTrapProtectionConsumed = false;
	bool bTravelCurseStateRestoreCompleted = false;
	bool bWasPlayerTravelStateOwner = false;
	bool bSuppressTravelCurseStateWrites = false;
	bool bTravelStateLifecycleGenerationCaptured = false;
	uint64 TravelStateLifecycleGeneration = 0;
	float LastCombatImpactTimeSeconds = -FLT_MAX;
	float LastCurseApplicationTimeSeconds = -FLT_MAX;
	float CursedEpisodeEndTimeSeconds = -FLT_MAX;
	float GuardRecoveryEndTimeSeconds = -FLT_MAX;
	float GuardRecoveryPoolRemaining = 0.f;
	float GuardRecoveryAbsorbedDamage = 0.f;
	float LastGuardRecoveryFeedbackTimeSeconds = -FLT_MAX;
	float CleanFinishBuffEndTimeSeconds = -FLT_MAX;
	float CleanFinishCooldownEndTimeSeconds = -FLT_MAX;
	float RecoveredMomentumEndTimeSeconds = -FLT_MAX;
	float AppliedFaithSpellDefenseBonus = 0.f;
	float TickAccumulatorSeconds = 0.f;
	float LastResolvedCurseResistanceMultiplier = 1.f;
};
