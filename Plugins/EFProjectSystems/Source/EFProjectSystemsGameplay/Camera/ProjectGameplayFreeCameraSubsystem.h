#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "TimerManager.h"
#include "ProjectGameplayFreeCameraSubsystem.generated.h"

class AActor;
class ACameraActor;
class APlayerController;
class APawn;
class UInputComponent;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayFreeCameraSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|Camera")
	void RequestToggleGameplayFreeCamera();

	UFUNCTION(BlueprintCallable, Category = "Project|Camera")
	bool StartGameplayFreeCamera();

	UFUNCTION(BlueprintCallable, Category = "Project|Camera")
	void StopGameplayFreeCamera();

	UFUNCTION(BlueprintPure, Category = "Project|Camera")
	bool IsGameplayFreeCameraActive() const;

protected:
	bool TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController(bool bStopActiveCamera);
	void BindToggleInputToTrackedPlayerController();
	void UnbindToggleInputFromTrackedPlayerController();
	bool ApplyFreeCameraInputCapture();
	void RestoreFreeCameraInputCapture();
	bool CanStartGameplayFreeCamera() const;
	bool ShouldForceStopGameplayFreeCamera() const;
	bool IsProjectEmoteFlowBlocking() const;
	void EnsureGameplayFreeCameraViewTarget();
	void UpdateFreeCamera(float DeltaTime);
	void ClearFreeCameraInput();
	AActor* ResolveRestoreViewTarget(APlayerController* PlayerController) const;
	void ScheduleDeferredViewTargetRestore(APlayerController* PlayerController);
	void RestorePostFreeCameraViewTarget();
	void MarkMaintenanceRequired();

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void HandleTogglePressed();
	void HandlePitchUpPressed();
	void HandlePitchUpReleased();
	void HandlePitchDownPressed();
	void HandlePitchDownReleased();
	void HandleYawLeftPressed();
	void HandleYawLeftReleased();
	void HandleYawRightPressed();
	void HandleYawRightReleased();
	void HandleMoveUpPressed();
	void HandleMoveUpReleased();
	void HandleMoveDownPressed();
	void HandleMoveDownReleased();
	void HandleMoveForwardPressed();
	void HandleMoveForwardReleased();
	void HandleMoveBackwardPressed();
	void HandleMoveBackwardReleased();
	void HandleRollLeftPressed();
	void HandleRollLeftReleased();
	void HandleRollRightPressed();
	void HandleRollRightReleased();

protected:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> ToggleInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> FreeCameraInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ActiveFreeCameraActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SavedViewTarget;

	FTimerHandle DeferredViewTargetRestoreTimerHandle;

	bool bNeedsMaintenanceTick = true;
	bool bPitchUpPressed = false;
	bool bPitchDownPressed = false;
	bool bYawLeftPressed = false;
	bool bYawRightPressed = false;
	bool bMoveUpPressed = false;
	bool bMoveDownPressed = false;
	bool bMoveForwardPressed = false;
	bool bMoveBackwardPressed = false;
	bool bRollLeftPressed = false;
	bool bRollRightPressed = false;
};
