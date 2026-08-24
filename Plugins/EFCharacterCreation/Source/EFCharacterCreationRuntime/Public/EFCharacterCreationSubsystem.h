#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCharacterCreationTypes.h"
#include "EFCharacterCreationSubsystem.generated.h"

class ACameraActor;
class APlayerController;
class APawn;
class UCameraComponent;
class UEFCharacterCreationRootWidget;
class UEFCharacterCustomizationComponent;
class UInputComponent;
class USpringArmComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FEFCharacterCreationActiveChanged, bool /* bIsActive */);

UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCreationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool CanStartForPawn(APawn* Pawn, FString& OutReason);
	bool EnterCharacterCreationMode(APlayerController* PlayerController, APawn* Pawn);
	void ExitCharacterCreationMode(bool bKeepCurrentChanges);
	void ToggleCharacterCreationMode();

	void HandleStartGameRequested();
	void HandleBackRequested();
	void OrbitPreviewCamera(const FVector2D& PointerDelta);
	void PanPreviewCamera(const FVector2D& PointerDelta);
	void ZoomPreviewCamera(float WheelDelta);

	UFUNCTION(BlueprintCallable, Category = "EF Character Creation|Automation")
	bool OpenCharacterCreationForAutomation();

	UFUNCTION(BlueprintPure, Category = "EF Character Creation|Automation")
	UEFCharacterCreationRootWidget* GetActiveRootWidgetForAutomation() const;

	bool IsCharacterCreationActive() const { return bIsCharacterCreationActive; }
	UEFCharacterCustomizationComponent* GetCustomizationComponent() const { return ActiveCustomizationComponent.Get(); }
	FEFCharacterCreationActiveChanged& OnCharacterCreationActiveChanged() { return CharacterCreationActiveChanged; }

private:
	struct FCharacterCreationSessionSnapshot
	{
		TWeakObjectPtr<AActor> PreviousViewTarget;
		FCharacterCustomizationState OriginalState;
		bool bWasMouseCursorVisible = false;
		bool bWereClickEventsEnabled = false;
		bool bWereMouseOverEventsEnabled = false;
		bool bWasMoveInputIgnored = false;
		bool bWasLookInputIgnored = false;
		bool bWasPawnUseControllerRotationYaw = false;
		bool bHadSavedMovementState = false;
		TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
		uint8 PreviousCustomMovementMode = 0;
	};

	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void TryInitializeCharacterCreation(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	bool ShouldAutoEnterForWorld(const UWorld* World) const;
	void HandleToggleCharacterCreationPressed();

	UEFCharacterCustomizationComponent* FindOrCreateCustomizationComponent(APawn* Pawn) const;
	bool CreatePreviewCameraActor(APlayerController* PlayerController, APawn* Pawn);
	bool ResolvePreviewCameraRig(APawn* Pawn, UCameraComponent*& OutCameraComponent, USpringArmComponent*& OutSpringArmComponent, TArray<UCameraComponent*>& OutAllCameraComponents) const;
	void CachePreviewCameraRig(UCameraComponent* CameraComponent, USpringArmComponent* SpringArmComponent, const TArray<UCameraComponent*>& AllCameraComponents);
	void ActivatePreviewCameraRig(APawn* Pawn, UCameraComponent* CameraComponent, USpringArmComponent* SpringArmComponent);
	void UpdateDirectPreviewCameraTransform();
	void RestorePreviewCameraRig();
	void EnsureToggleInputBinding(APlayerController* PlayerController);
	void ReleaseToggleInputBinding();
	void ApplyCharacterCreationInputState(APlayerController* PlayerController, bool bEnableCharacterCreation);
	void SuspendGameplayForPawn(APawn* Pawn, bool bSuspendGameplay);
	void CacheRuntimeCustomizationState(const FCharacterCustomizationState& State);
	void CaptureRuntimeCustomizationStateFromWorld(UWorld* World);
	bool ApplyRuntimeCustomizationStateToPawn(APawn* Pawn);
	void CleanupSessionObjects();

private:
	FDelegateHandle WorldBeginPlayHandle;
	FDelegateHandle WorldCleanupHandle;
	FCharacterCreationSessionSnapshot SessionSnapshot;
	bool bIsCharacterCreationActive = false;
	FEFCharacterCreationActiveChanged CharacterCreationActiveChanged;
	bool bHasRuntimeCustomizationState = false;
	FCharacterCustomizationState RuntimeCustomizationState;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> ActivePlayerController;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> ActivePawn;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEFCharacterCustomizationComponent> ActiveCustomizationComponent;

	UPROPERTY(Transient)
	TObjectPtr<UEFCharacterCreationRootWidget> ActiveRootWidget;

	UPROPERTY(Transient)
	TWeakObjectPtr<ACameraActor> ActivePreviewCameraActor;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCameraComponent> ActivePreviewCameraComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<USpringArmComponent> ActivePreviewSpringArmComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> ToggleInputPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> ToggleInputComponent;

	TArray<TWeakObjectPtr<UCameraComponent>> CachedPawnCameraComponents;
	TArray<bool> CachedPawnCameraActiveStates;
	FVector CachedPreviewCameraRelativeLocation = FVector::ZeroVector;
	FRotator CachedPreviewCameraRelativeRotation = FRotator::ZeroRotator;
	float CachedPreviewCameraFieldOfView = 0.0f;
	bool bCachedPreviewCameraUsePawnControlRotation = false;
	FVector CachedPreviewSpringArmRelativeLocation = FVector::ZeroVector;
	FRotator CachedPreviewSpringArmRelativeRotation = FRotator::ZeroRotator;
	FVector CachedPreviewSpringArmSocketOffset = FVector::ZeroVector;
	FVector CachedPreviewSpringArmTargetOffset = FVector::ZeroVector;
	float CachedPreviewSpringArmLength = 0.0f;
	bool bCachedPreviewSpringArmCollisionTest = false;
	bool bCachedPreviewSpringArmUsePawnControlRotation = false;
	bool bHasCachedPreviewRig = false;
	FVector DirectPreviewFocusWorldLocation = FVector::ZeroVector;
	float DirectPreviewCameraDistance = 0.0f;
};

