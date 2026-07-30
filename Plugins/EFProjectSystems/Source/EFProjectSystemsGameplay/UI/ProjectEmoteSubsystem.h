#pragma once

#include "ContentPolicy/ProjectOptionalMatureContentProvider.h"
#include "CoreMinimal.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectEmoteSubsystem.generated.h"

class APlayerController;
class APawn;
class UACFTrainingComponent;
class UActorComponent;
class UInputComponent;
class UProjectEmoteComponent;
class UProjectEmoteMenuWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectEmoteRuntimeActionStartedSignature, const FProjectEmoteRuntimeActionRequest&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FProjectEmoteRuntimeActionEndedSignature, const FProjectEmoteRuntimeActionRequest&, EProjectEmoteRuntimeActionEndReason);

struct FProjectEmoteMenuStateSnapshot
{
	bool bHasSavedControllerState = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;
	bool bHasSavedMovementState = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	bool bPawnInputSuspended = false;
	bool bHasSavedProjectHudVisibility = false;
	bool bWasProjectHudVisible = false;
	bool bHasSavedPlayerHudVisibility = false;
	bool bWasPlayerHudVisible = true;
	bool bAppliedAcfHudDisable = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteSubsystem
	: public UTickableWorldSubsystem
	, public IProjectOptionalMatureContentProvider
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	virtual bool SupportsMatureFeature(EProjectOptionalMatureFeature Feature) const override;
	virtual bool IsMatureFeatureAvailable(EProjectOptionalMatureFeature Feature) const override;
	virtual bool TryBeginMaturePresentation(const FProjectMaturePresentationRequest& Request) override;
	virtual void CancelMaturePresentation(const FGuid& RequestId) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestToggleEmoteMenu();

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestNavigateEmoteMenu(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestSelectEmoteMenuOption(int32 SelectionIndex);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestConfirmEmoteMenuSelection();

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestActivateEmoteMenuOption(int32 SelectionIndex);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestCancelEmoteMenu();

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void RequestCancelActiveEmote();

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	bool RequestStartInteractionById(FName InteractionId);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote|Runtime Action")
	bool StartRuntimeAction(const FProjectEmoteRuntimeActionRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote|Runtime Action")
	bool CancelRuntimeAction(FName ReasonId = NAME_None);

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Runtime Action")
	bool IsRuntimeActionActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Runtime Action")
	FName GetActiveRuntimeActionId() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Runtime Action")
	EProjectEmoteRuntimeActionSource GetActiveRuntimeActionSource() const;

	FProjectEmoteRuntimeActionStartedSignature OnRuntimeActionStarted;
	FProjectEmoteRuntimeActionEndedSignature OnRuntimeActionEnded;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmoteMenuOpen() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	int32 GetEmoteMenuSelectedIndex() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsTrackedEmoteMenuWidgetInViewport() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	FString GetTrackedEmoteMenuWidgetClassName() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmoteActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmoteTransitionPending() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmotePlaybackStarted() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	int32 GetActiveEmoteValue() const;

protected:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

private:
	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController(bool bStopActiveEmote);
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void EnsureEmoteComponent(APawn* Pawn);
	UACFTrainingComponent* EnsureTrainingComponent(APawn* Pawn);
	void EnsureEmoteMenuWidget(APlayerController* PlayerController);
	void OpenMenu();
	void CloseMenu(bool bScheduleDelayedRecoveryCheck);
	bool StartInteractionById(FName InteractionId);
	bool RequestPrivateSoloPresentation();
	void EndPrivateSoloPresentationRegistration(bool bNotifyPolicy);
	bool IsPrivateSoloPresentationPlaying() const;
	void EnsureTrackedPlayerSocialRegistration();
	bool TryParseTrainingInteractionId(FName InteractionId, FName& OutTrainingId) const;
	void StartTrainingInteractionById(FName InteractionId);
	int32 ResolveTrainingCunningLevel(APawn* Pawn) const;
	FName ResolveRuntimeActionInteractionId(const FProjectEmoteRuntimeActionRequest& Request) const;
	void CompleteRuntimeAction(EProjectEmoteRuntimeActionEndReason EndReason);
	void CleanupMenuAndEmoteState(bool bRestoreTargetActor = true);
	void ApplyMenuInputCapture();
	void RestoreMenuInputCapture();
	void ApplyMenuHudSuppression();
	void RestoreMenuHudSuppression();
	bool TrySetReflectedHudEnabled(class AHUD* HudActor, bool bEnabled) const;
	void RefreshMenuFocusNextTick();
	void RefreshCurrentMenuModel(FName PreferredOptionId = NAME_None);
	FName GetPreferredRootOptionId() const;
	FName GetCurrentMenuParentNodeId() const;
	bool IsAtMenuRoot() const;
	bool IsCurrentMenuActionsCategoryRoot() const;
	FText ResolveCurrentMenuTitle() const;
	FText ResolveCurrentMenuHint() const;
	EProjectEmoteMenuVisualMode ResolveCurrentMenuVisualMode() const;
	void BuildMenuNodeOptions(TArray<FProjectEmoteMenuOption>& OutOptions);
	void ScheduleDelayedMovementRecoveryCheck(const FProjectEmoteMenuStateSnapshot& Snapshot);
	void HandleDelayedMovementRecoveryCheck();
	bool ShouldSkipDelayedMovementRecovery() const;
	bool IsGamePauseBlockingEmoteFlow() const;
	bool IsCombatBlockingEmoteMenu() const;
	bool IsACFBattleActive() const;
	bool TryInvokeBoolFunction(UObject* TargetObject, FName FunctionName) const;
	void RefreshActiveEmoteInputCapture();
	void ApplyActiveEmoteInputCapture();
	void RestoreActiveEmoteInputCapture();
	void SetFreeCameraForwardInput(float Value);
	void SetFreeCameraRightInput(float Value);
	void SetFreeCameraVerticalInput(float Value);
	void SetFreeCameraBoostActive(bool bActive);
	void HandleToggleMenuPressed();
	void HandleMenuOptionConfirmed(FName OptionId);
	void HandleMenuCancelRequested();
	void HandleFreeCameraForwardPressed();
	void HandleFreeCameraForwardReleased();
	void HandleFreeCameraBackwardPressed();
	void HandleFreeCameraBackwardReleased();
	void HandleFreeCameraRightPressed();
	void HandleFreeCameraRightReleased();
	void HandleFreeCameraLeftPressed();
	void HandleFreeCameraLeftReleased();
	void HandleFreeCameraUpPressed();
	void HandleFreeCameraUpReleased();
	void HandleFreeCameraDownPressed();
	void HandleFreeCameraDownReleased();
	void HandleFreeCameraBoostPressed();
	void HandleFreeCameraBoostReleased();
	void HandleActiveEmoteMenuNavigateUp();
	void HandleActiveEmoteMenuNavigateDown();
	void HandleActiveEmoteMenuNavigateBack();
	void HandleActiveEmoteMenuNavigateForward();
	void HandleActiveEmoteMenuConfirm();
	void HandleActiveEmoteCancelPressed();
	void HandleAllowedNeedsHudPressed();
	void HandleAllowedChronicleExpandPressed();
	void HandleAllowedPausePressed();
	void HandleBlockedActiveEmoteInput();

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> ActiveEmoteInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectEmoteComponent> TrackedEmoteComponent;

	UPROPERTY(Transient)
	TObjectPtr<UACFTrainingComponent> TrackedTrainingComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectEmoteMenuWidget> TrackedEmoteMenuWidget;

	FProjectEmoteMenuStateSnapshot MenuStateSnapshot;
	FProjectEmoteMenuStateSnapshot DelayedRecoverySnapshot;
	FProjectEmoteRuntimeActionRequest ActiveRuntimeActionRequest;
	TMap<FName, FProjectEmoteMenuNodeDefinition> CachedVisibleMenuNodes;
	TArray<FName> MenuNodeStack;
	FTimerHandle DelayedMovementRecoveryTimerHandle;
	FGuid PrivateSoloPresentationRequestId;
	bool bMenuOpen = false;
	bool bActiveEmoteInputCaptureApplied = false;
	bool bRuntimeActionActive = false;
};
