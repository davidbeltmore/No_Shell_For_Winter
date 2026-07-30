#pragma once

#include "CoreMinimal.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "Tickable.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectGameplayDebugSubsystem.generated.h"

class ACameraActor;
class APlayerController;
class APawn;
class UInputComponent;
class UProjectAutomaticTattooTunerWidget;
class UProjectGameplayDebugMenuWidget;

enum class EProjectAutomaticTattooTunerCameraView : uint8;

struct FProjectGameplayDebugMenuStateSnapshot
{
	bool bHasSavedControllerState = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;
	bool bWasMouseCursorVisible = false;
	bool bWereClickEventsEnabled = false;
	bool bWereMouseOverEventsEnabled = false;
	bool bHasSavedMovementState = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	bool bPawnInputSuspended = false;
	bool bHasSavedProjectHudVisibility = false;
	bool bWasProjectHudVisible = true;
	bool bHasSavedPlayerHudVisibility = false;
	bool bWasPlayerHudVisible = true;
	bool bAppliedAcfHudDisable = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|Gameplay Debug")
	void RequestToggleGameplayDebugMenu();

	UFUNCTION(BlueprintPure, Category = "Project|Gameplay Debug")
	bool IsGameplayDebugMenuOpen() const;

private:
	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void EnsureDebugMenuWidget(APlayerController* PlayerController);
	void OpenMenu();
	void CloseMenu();
	void ApplyMenuInputCapture();
	void RestoreMenuInputCapture();
	void RefreshMenuFocusNextTick();
	void RefreshCurrentMenuModel(FName PreferredOptionId = NAME_None);
	FName GetCurrentMenuParentNodeId() const;
	bool IsAtMenuRoot() const;
	FText ResolveCurrentMenuTitle() const;
	FText ResolveCurrentMenuHint() const;
	EProjectEmoteMenuVisualMode ResolveCurrentMenuVisualMode() const;
	void BuildMenuNodeOptions(TArray<FProjectEmoteMenuOption>& OutOptions);
	void AddVisibleOption(
		TArray<FProjectEmoteMenuOption>& OutOptions,
		FName NodeId,
		FName ParentNodeId,
		const FText& Label,
		const FText& Description,
		EProjectEmoteMenuNodeType NodeType,
		int32 SortOrder,
		FName VisualIconId = NAME_None,
		FName VisualAttribute = TEXT("None"));
	void AddBackOption(TArray<FProjectEmoteMenuOption>& OutOptions);
	bool ExecuteCommand(FName OptionId);
	bool TryResolveAttributeFromCommandId(FName OptionId, int32 TargetLevel, EProjectDoctrineAttribute& OutAttribute) const;
	void HandleToggleMenuPressed();
	void HandleMenuOptionConfirmed(FName OptionId);
	void HandleMenuCancelRequested();
	void HandleMenuBackRequested();
	void OpenAutomaticTattooTuner(FName RowName);
	void CloseAutomaticTattooTuner(bool bApply);
	void HandleAutomaticTattooTunerPreviewChanged(const FProjectAutomaticTattooTableRow& EditedRow);
	void HandleAutomaticTattooTunerApplyRequested(const FProjectAutomaticTattooTableRow& EditedRow);
	void HandleAutomaticTattooTunerCancelRequested();
	void HandleAutomaticTattooTunerViewRequested(EProjectAutomaticTattooTunerCameraView View);
	void HandleAutomaticTattooTunerOrbitRequested(const FVector2D& PointerDelta);
	void HandleAutomaticTattooTunerPanRequested(const FVector2D& PointerDelta);
	void HandleAutomaticTattooTunerZoomRequested(float WheelDelta);
	void FlushAutomaticTattooTunerPreview();
	bool CreateAutomaticTattooTunerPreviewCamera();
	void DestroyAutomaticTattooTunerPreviewCamera();
	void UpdateAutomaticTattooTunerPreviewCameraTransform();
	void SetAutomaticTattooTunerCameraView(EProjectAutomaticTattooTunerCameraView View);
	void ApplyAutomaticTattooTunerInputCapture();
	void RestoreAutomaticTattooTunerInputCapture();
	void ApplyAutomaticTattooTunerHudSuppression();
	void RestoreAutomaticTattooTunerHudSuppression();
	bool TrySetReflectedHudEnabled(class AHUD* HudActor, bool bEnabled) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectGameplayDebugMenuWidget> TrackedDebugMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectAutomaticTattooTunerWidget> TrackedAutomaticTattooTunerWidget;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> AutomaticTattooTunerPreviewCameraActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> AutomaticTattooTunerPreviousViewTarget;

	FProjectGameplayDebugMenuStateSnapshot MenuStateSnapshot;
	FProjectGameplayDebugMenuStateSnapshot AutomaticTattooTunerStateSnapshot;
	FProjectAutomaticTattooRuntimeDebugState AutomaticTattooTunerInitialState;
	FProjectAutomaticTattooTableRow PendingAutomaticTattooTunerPreviewRow;
	TMap<FName, FProjectEmoteMenuNodeDefinition> CachedVisibleMenuNodes;
	TArray<FName> MenuNodeStack;
	FTimerHandle AutomaticTattooTunerPreviewTimerHandle;
	FName ActiveAutomaticTattooTunerRow = NAME_None;
	FVector AutomaticTattooTunerCameraFocusWorldLocation = FVector::ZeroVector;
	FRotator AutomaticTattooTunerCameraRotation = FRotator::ZeroRotator;
	float AutomaticTattooTunerCameraDistance = 240.0f;
	bool bMenuOpen = false;
	bool bAutomaticTattooTunerOpen = false;
	bool bHasPendingAutomaticTattooTunerPreview = false;
};
