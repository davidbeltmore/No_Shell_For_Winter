#pragma once

#include "ACFTrainingTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectTrainingSubsystem.generated.h"

class APlayerController;
class APawn;
class UACFTrainingComponent;
class UProjectTrainingLockpickWidget;
class UUserWidget;

struct FProjectTrainingInputStateSnapshot
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
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTrainingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Training|UI")
	bool OpenTraining(UACFTrainingComponent* TrainingComponent, APawn* InteractingPawn);

	UFUNCTION(BlueprintCallable, Category = "Training|UI")
	void CloseTraining(bool bCancelTraining = false);

	UFUNCTION(BlueprintPure, Category = "Training|UI")
	bool IsTrainingOpen() const;

private:
	UFUNCTION()
	void HandleTrainingCompleted(FName TrainingId, EACFTrainingSessionResult Result, FGameplayTag TargetAttribute, float RewardValue);

	UFUNCTION()
	void HandleTrainingRejected(FName TrainingId, FText Reason);

	void EnsureTrainingWidget(APlayerController* PlayerController);
	TSubclassOf<UProjectTrainingLockpickWidget> ResolveTrainingWidgetClass() const;
	void ApplyInputCapture(UUserWidget* WidgetToFocus, bool bShowCursor);
	void UpdateInputCaptureFocus(UUserWidget* WidgetToFocus, bool bShowCursor);
	void RestoreInputCapture();
	void HandleConfirmRequested();
	void HandleCancelRequested();
	bool ResolveLocalContext(APawn* InteractingPawn);
	void UnbindFromTrainingComponent();
	void RefreshTrainingFocusNextTick();
	void StopTrainingEmote();
	void CloseTrainingAfterResult();

private:
	UPROPERTY(Transient)
	TObjectPtr<UACFTrainingComponent> ActiveTrainingComponent;

	UPROPERTY(Transient)
	TObjectPtr<UACFTrainingComponent> BoundTrainingComponent;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> ActivePlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> ActivePawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTrainingLockpickWidget> ActiveWidget;

	FProjectTrainingInputStateSnapshot InputSnapshot;
	FTimerHandle CloseAfterResultTimerHandle;
	bool bTrainingOpen = false;
};
