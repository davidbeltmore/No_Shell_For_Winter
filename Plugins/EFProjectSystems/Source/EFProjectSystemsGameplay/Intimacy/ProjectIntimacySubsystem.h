#pragma once

#include "CoreMinimal.h"
#include "Characters/ProjectEnemyCombatStatTypes.h"
#include "ContentPolicy/ProjectOptionalMatureContentProvider.h"
#include "InputCoreTypes.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "Social/ProjectSocialTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectIntimacySubsystem.generated.h"

class AActor;
class APlayerController;
class APawn;
class UDataTable;
class UInputComponent;
class UProjectEmoteComponent;
class UProjectInnerDoctrineComponent;
class UProjectIntimacyHudWidget;
class UProjectIntimacyPartnerComponent;
class UProjectIntimacySaveGame;
class UProjectIntimacyZoneComponent;
class UProjectTargetingFixComponent;
class USkeletalMeshComponent;

struct FProjectIntimacyResolvedOption
{
	FName OptionId = NAME_None;
	FText Label;
	EProjectIntimacyTalkAction TalkAction = EProjectIntimacyTalkAction::None;
	FName CategoryId = NAME_None;
	FGameplayTagContainer TalkTags;
	float ClimaxGain = 0.0f;
	EProjectIntimacyClimaxTarget ClimaxTarget = EProjectIntimacyClimaxTarget::Partner;
	int32 AffectDelta = 0;
	float AnimationRate = 1.0f;
	bool bCanBeCorrectTalkOption = true;
	bool bUsesTalkCooldown = true;
	bool bCanBeFlavorCorrectOption = true;
};

struct FProjectIntimacyRuntimeSession
{
	TWeakObjectPtr<AActor> PlayerActor;
	TWeakObjectPtr<AActor> PartnerActor;
	TWeakObjectPtr<UProjectIntimacyPartnerComponent> PartnerComponent;
	TWeakObjectPtr<UProjectInnerDoctrineComponent> CurseDoctrineComponent;
	FString PartnerId;
	float PlayerClimax = 0.0f;
	float PartnerClimax = 0.0f;
	float ClimaxMaximum = 100.0f;
	float PlayerClimaxPerSecond = 0.0f;
	float PartnerClimaxPerSecond = 0.0f;
	float SessionTimeSeconds = 0.0f;
	float AnimationRate = 1.0f;
	float ClimaxIntensityMultiplier = 1.0f;
	float OrgasmRushRemaining = 0.0f;
	EProjectIntimacySessionState SessionState = EProjectIntimacySessionState::BuildingClimax;
	EProjectIntimacyClimaxTarget OrgasmRushTarget = EProjectIntimacyClimaxTarget::Partner;
	bool bPlayerOrgasmRush = false;
	bool bPartnerOrgasmRush = false;
	float LastOrgasmEventTimeSeconds = -FLT_MAX;
	int32 PlayerSessionOrgasmCount = 0;
	int32 PartnerSessionOrgasmCount = 0;
	float CurseUpdateAccumulator = 0.0f;
	float HudRefreshAccumulator = 0.0f;
	EProjectIntimacyPersonality EffectivePersonality = EProjectIntimacyPersonality::Nice;
	float TalkCooldownRemaining = 0.0f;
	FName CorrectTalkOptionId = NAME_None;
	EProjectIntimacyHudMode HudMode = EProjectIntimacyHudMode::Main;
	FName ActiveTalkCategoryId = NAME_None;
	FName ActiveItemCategoryId = NAME_None;
	int32 SelectedOptionIndex = 0;
	bool bHudVisible = false;
	bool bPleaseActive = false;
	EProjectIntimacyClimaxTarget PleaseClimaxTarget = EProjectIntimacyClimaxTarget::Partner;
	int32 PleaseAttemptIndex = 0;
	int32 PleaseSuccessCount = 0;
	float PleaseElapsedSeconds = 0.0f;
	float PleasePulsePeriod = 1.0f;
	float PleaseCursorValue = 0.0f;
	float PleaseTargetCenter = 0.5f;
	float PleaseTargetHalfRange = 0.08f;
	FText StatusText;
};

struct FProjectIntimacySocialOverrideState
{
	TWeakObjectPtr<AActor> PlayerActor;
	TWeakObjectPtr<AActor> PartnerActor;
	FProjectSocialParticipantState PlayerState;
	FProjectSocialParticipantState PartnerState;
	bool bPlayerWasRegistered = false;
	bool bPartnerWasRegistered = false;
	bool bPlayerOriginallyConsented = false;
	bool bPartnerOriginallyConsented = false;
	bool bActive = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacySubsystem
	: public UTickableWorldSubsystem
	, public IProjectOptionalMatureContentProvider
{
	GENERATED_BODY()

public:
	static const FName FirstIntimacyHeartChestTattooRewardId;
	static const FName TestTattooIntimacyRewardId;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	virtual bool SupportsMatureFeature(EProjectOptionalMatureFeature Feature) const override;
	virtual bool IsMatureFeatureAvailable(EProjectOptionalMatureFeature Feature) const override;
	virtual bool TryBeginMaturePresentation(const FProjectMaturePresentationRequest& Request) override;
	virtual void CancelMaturePresentation(const FGuid& RequestId) override;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	bool IsIntimacySessionActive() const;

	/** True only for the exact partner participating in the current active session. */
	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	bool IsActorInActiveIntimacySession(const AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	bool IsHudVisible() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	EProjectIntimacyHudMode GetHudMode() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	bool IsPleaseActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	FString GetActivePartnerId() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	float GetCurrentSessionProgress() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	float GetCurrentSessionPeak() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Climax")
	float GetPlayerClimax() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Climax")
	float GetPartnerClimax() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Climax")
	bool IsOrgasmRushActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	float GetTalkCooldownRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Progress")
	int32 GetTotalIntimacyEncounterCount() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Progress")
	bool HasAnyIntimacyEncounter() const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy|Progress")
	bool IsAutomaticTattooRewardUnlocked(FName TattooRewardId) const;

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy|Progress")
	bool UnlockAutomaticTattooReward(FName TattooRewardId);

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy|Automation")
	bool GrantFirstIntimacyEncounterForAutomation();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy|Automation")
	bool ForceSessionPeakForAutomation();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy|Automation")
	bool ForcePartnerOrgasmForAutomation();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy|Automation")
	bool ForcePlayerOrgasmForAutomation();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void RequestToggleHud();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	bool RequestQuickStartIntimacy();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void RequestNavigate(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void RequestBack();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void RequestConfirm();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	void RequestCancelIntimacy();

	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	bool CanStartIntimacyWithPartner(AActor* PartnerActor, FText& OutFailureReason) const;

	/** Non-mutating preflight used by the Y/T product route before consent is recorded. */
	UFUNCTION(BlueprintCallable, Category = "Project|Intimacy")
	bool CanRequestIntimacyWithPartner(AActor* PartnerActor, FText& OutFailureReason) const;

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	AActor* GetHubSocialCompanionActor() const;

	static int32 ResolveCharismaLevelForActor(const AActor* Actor);
	static bool HasRequiredCharismaForActor(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Project|Intimacy")
	FProjectIntimacySessionSnapshot BuildSnapshot() const;

#if WITH_DEV_AUTOMATION_TESTS
	bool AutomationRunMenuAndPleaseSmoke(
		FString& OutFailureReason,
		int32& OutStepCount,
		bool& bOutPleaseCompleted,
		bool& bOutSessionPeakTriggered);
#endif

	bool TryGetPartnerProfile(AActor* PartnerActor, FProjectIntimacyPartnerProfile& OutProfile) const;
	bool BuildTargetSocialCardSnapshot(AActor* PartnerActor, FProjectSocialCardSnapshot& OutSnapshot);
	void AppendTargetIntimacyRows(AActor* PartnerActor, FProjectEnemyCombatStatSnapshot& InOutSnapshot);

private:
	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void SetSessionNavigationInputCaptureEnabled(bool bEnabled);
	bool ResolveActiveIntimacyPartner(AActor*& OutPartnerActor, UProjectIntimacyPartnerComponent*& OutPartnerComponent) const;
	void StartSession(AActor* PartnerActor, UProjectIntimacyPartnerComponent* PartnerComponent);
	void UpdateActiveSession(float DeltaTime);
	void EndSession(bool bCancelled);
	void CancelActiveSession();
	void EnsureHudWidget();
	void RefreshHudWidget();
	void RefreshResolvedOptions();
	void ChooseCorrectTalkOption();
	void SetHudMode(EProjectIntimacyHudMode NewMode);
	void HandleMainOption(const FName OptionId);
	void HandleTalkOption(const FProjectIntimacyResolvedOption& Option);
	void HandleItemsOption(const FProjectIntimacyResolvedOption& Option);
	void ExecuteTalkOption(const FProjectIntimacyResolvedOption& Option);
	void TriggerMediaCueForTalkOption(const FProjectIntimacyResolvedOption& Option);
	bool TryResolveMediaCueForTalkOption(const FProjectIntimacyResolvedOption& Option, FProjectIntimacyMediaCueRow& OutCue) const;
	void TriggerMediaCueForEvent(FName EventId);
	bool TryResolveMediaCueForEvent(FName EventId, FProjectIntimacyMediaCueRow& OutCue) const;
	void StartPlease();
	void StartNextPleaseAttempt();
	void ResolvePleasePress();
	void ApplyClimaxGain(EProjectIntimacyClimaxTarget Target, float Amount, const FText& ReasonText);
	void UpdateOrgasmRushState(float DeltaTime);
	void TriggerOrgasm(EProjectIntimacyClimaxTarget Target, int32 OrgasmCount);
	void UpdateCurseRecovery(float DeltaTime);
	float ComputeEffectiveAnimationRate() const;
	void ApplyAnimationRate(float NewRate);
	void RestoreAnimationRates();
	void CacheAndSetMeshRate(USkeletalMeshComponent* MeshComponent, float NewRate);
	void CollectSessionMeshes(TArray<USkeletalMeshComponent*>& OutMeshes) const;
	int32 ResolvePartnerLevel() const;
	EProjectIntimacyPersonality ResolveEffectivePersonality(const FProjectIntimacyPartnerProfile& Profile, const UProjectIntimacyPartnerComponent* PartnerComponent) const;
	void NormalizeProfile(UProjectIntimacyPartnerComponent* PartnerComponent, FProjectIntimacyPartnerProfile& Profile) const;
	void RefreshRelationshipTags(FProjectIntimacyPartnerProfile& Profile) const;
	bool RelationshipForcesChill(const FGameplayTagContainer& RelationshipTags) const;
	FGameplayTag GetBaseRelationshipTag(int32 Encounters) const;
	bool HasRelationshipTag(const FProjectIntimacyPartnerProfile& Profile, const TCHAR* TagName) const;
	FProjectIntimacyPartnerProfile& GetMutableProfile(UProjectIntimacyPartnerComponent* PartnerComponent);
	void LoadPersistentState();
	void SavePersistentState() const;
	UDataTable* LoadTable(const FSoftObjectPath& TablePath) const;
	FProjectIntimacyEligibilityContext BuildEligibilityContext(
		AActor* PartnerActor,
		const UProjectIntimacyPartnerComponent* PartnerComponent) const;
	bool IsCharismaMasteryTargetRoute(
		AActor* PartnerActor,
		const UProjectIntimacyPartnerComponent* PartnerComponent) const;
	bool IsCharismaTargetedPartnerClass(const AActor* PartnerActor) const;
	UProjectIntimacyPartnerComponent* ResolveOrCreateTargetParticipant(AActor* PartnerActor) const;
	void RegisterSocialParticipants(
		AActor* PartnerActor,
		const UProjectIntimacyPartnerComponent* PartnerComponent,
		bool bEstablishConsent);
	void RestoreCharismaSocialOverride();
	void EnsureHubSocialProductRoute();
	bool IsHubSocialProductMap() const;
	bool EnsureMaturePresentationRegistered(AActor* PartnerActor, FText& OutFailureReason);
	bool BeginIntimacySceneAction(AActor* PartnerActor);
	void EndMaturePresentationRegistration(bool bNotifyPolicy);
	void ClearConsentForPartner(AActor* PartnerActor, AActor* PlayerActorOverride = nullptr);
	void ClearActiveSessionConsent();

	void HandleToggleHudPressed();
	void HandleNavigateUpPressed();
	void HandleNavigateDownPressed();
	void HandleNavigateLeftPressed();
	void HandleNavigateRightPressed();
	void HandleConfirmPressed();

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectEmoteComponent> TrackedEmoteComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTargetingFixComponent> TrackedTargetingFixComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> IntimacyInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectIntimacyHudWidget> HudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectIntimacySaveGame> IntimacySaveGame;

	FProjectIntimacyRuntimeSession ActiveSession;
	FProjectIntimacySocialOverrideState CharismaSocialOverride;
	TSet<FName> RuntimeUnlockedAutomaticTattooIds;
	TArray<FProjectIntimacyResolvedOption> ResolvedOptions;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> CachedMeshRates;
	FRandomStream RandomStream;
	FGuid MaturePresentationRequestId;
	TWeakObjectPtr<AActor> MaturePresentationPartnerActor;
	TWeakObjectPtr<AActor> HubSocialCompanionActor;
	TWeakObjectPtr<AActor> HubSocialZoneActor;
	TWeakObjectPtr<APawn> HubSocialRegisteredPlayer;
	bool bSessionActive = false;
	bool bSuppressStartUntilSceneEnds = false;
	bool bHubSocialProductRouteAttempted = false;
};
