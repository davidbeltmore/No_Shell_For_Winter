#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectLockpickingSubsystem.generated.h"

class AActor;
class APlayerController;
class APawn;
class UProjectLockpickableComponent;
class UProjectLockpickPromptWidget;
class UProjectLockpickingWidget;
class UUserWidget;

struct FProjectLockpickingInputStateSnapshot
{
	bool bHasSavedControllerState = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;
	bool bWasShowMouseCursor = false;
	bool bWasClickEventsEnabled = false;
	bool bWasMouseOverEventsEnabled = false;
	bool bHasSavedMovementState = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	bool bPawnInputSuspended = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|UI")
	bool OpenLockpicking(AActor* LockedActor, UProjectLockpickableComponent* LockpickableComponent, APawn* InteractingPawn);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|UI")
	void CloseLockpicking();

	UFUNCTION(BlueprintPure, Category = "Lockpicking|UI")
	bool IsLockpickingOpen() const;

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|UI")
	bool OpenLockpickPrompt(AActor* LockedActor, UProjectLockpickableComponent* LockpickableComponent, APawn* InteractingPawn);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|UI")
	void CloseLockpickPrompt();

	UFUNCTION(BlueprintPure, Category = "Lockpicking|UI")
	bool IsLockpickPromptOpen() const;

private:
	UFUNCTION()
	void HandleLockpickSucceeded(APawn* Pawn, int32 SessionId, float PulseValue);

	UFUNCTION()
	void HandleLockpickFailed(APawn* Pawn, int32 SessionId, float PulseValue);

	void EnsureLockpickingWidget(APlayerController* PlayerController);
	void EnsureLockpickPromptWidget(APlayerController* PlayerController);
	TSubclassOf<UProjectLockpickingWidget> ResolveLockpickingWidgetClass() const;
	TSubclassOf<UProjectLockpickPromptWidget> ResolveLockpickPromptWidgetClass() const;
	void ApplyInputCapture(UUserWidget* WidgetToFocus, bool bShowCursor);
	void UpdateInputCaptureFocus(UUserWidget* WidgetToFocus, bool bShowCursor);
	void RestoreInputCapture();
	void HandleConfirmRequested();
	void HandlePromptLockpickSelected();
	void HandlePromptCancelSelected();
	bool ResolveLocalContext(APawn* InteractingPawn);
	void UnbindFromActiveComponent();
	void RefreshLockpickingFocusNextTick();
	void RefreshPromptFocusNextTick();

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveLockedActor;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLockpickableComponent> ActiveLockpickableComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLockpickableComponent> BoundLockpickableComponent;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> ActivePlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> ActivePawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLockpickingWidget> ActiveWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectLockpickPromptWidget> ActivePromptWidget;

	FProjectLockpickingInputStateSnapshot InputSnapshot;
	bool bLockpickingOpen = false;
	bool bPromptOpen = false;
};
