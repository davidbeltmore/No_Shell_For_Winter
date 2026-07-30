#include "Camera/ProjectGameplayFreeCameraSubsystem.h"

#include "EFProjectInputSettings.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ProjectEmoteSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectGameplayFreeCamera, Log, All);

namespace ProjectGameplayFreeCameraPrivate
{
	constexpr int32 ToggleInputPriority = 3;
	constexpr int32 ActiveCameraInputPriority = 75;
	constexpr float BlendTime = 0.12f;
	constexpr float MoveSpeed = 560.0f;
	constexpr float RotationSpeedDegrees = 90.0f;
	constexpr float MinPitchDegrees = -89.0f;
	constexpr float MaxPitchDegrees = 89.0f;

	float ResolveSignedInput(const bool bPositivePressed, const bool bNegativePressed)
	{
		return (bPositivePressed ? 1.0f : 0.0f) - (bNegativePressed ? 1.0f : 0.0f);
	}
}

void UProjectGameplayFreeCameraSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	ToggleInputComponent = nullptr;
	FreeCameraInputComponent = nullptr;
	ActiveFreeCameraActor = nullptr;
	SavedViewTarget = nullptr;
	bNeedsMaintenanceTick = true;
	ClearFreeCameraInput();
}

void UProjectGameplayFreeCameraSubsystem::Deinitialize()
{
	StopGameplayFreeCamera();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
	}

	DetachFromTrackedPlayerController(false);
	Super::Deinitialize();
}

void UProjectGameplayFreeCameraSubsystem::Tick(const float DeltaTime)
{
	bNeedsMaintenanceTick = !TryResolveRuntimeContext();

	if (!IsGameplayFreeCameraActive())
	{
		return;
	}

	if (ShouldForceStopGameplayFreeCamera())
	{
		StopGameplayFreeCamera();
		return;
	}

	EnsureGameplayFreeCameraViewTarget();
	UpdateFreeCamera(DeltaTime);
}

TStatId UProjectGameplayFreeCameraSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectGameplayFreeCameraSubsystem, STATGROUP_Tickables);
}

bool UProjectGameplayFreeCameraSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectGameplayFreeCameraSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UProjectGameplayFreeCameraSubsystem::RequestToggleGameplayFreeCamera()
{
	HandleTogglePressed();
}

bool UProjectGameplayFreeCameraSubsystem::StartGameplayFreeCamera()
{
	if (IsGameplayFreeCameraActive())
	{
		return true;
	}

	if (!CanStartGameplayFreeCamera())
	{
		return false;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!World || !PlayerController)
	{
		return false;
	}

	World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFov = 90.0f;
	if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
	{
		CameraLocation = CameraManager->GetCameraLocation();
		CameraRotation = CameraManager->GetCameraRotation();
		CameraFov = CameraManager->GetFOVAngle();
	}
	else if (AActor* ViewTarget = PlayerController->GetViewTarget())
	{
		CameraLocation = ViewTarget->GetActorLocation();
		CameraRotation = ViewTarget->GetActorRotation();
	}
	else if (APawn* Pawn = PlayerController->GetPawn())
	{
		CameraLocation = Pawn->GetActorLocation();
		CameraRotation = Pawn->GetActorRotation();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerController->GetPawn();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveFreeCameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation, CameraRotation, SpawnParameters);
	if (!ActiveFreeCameraActor)
	{
		return false;
	}

	if (UCameraComponent* CameraComponent = ActiveFreeCameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(CameraFov);
	}

	SavedViewTarget = PlayerController->GetViewTarget();
	if (!ApplyFreeCameraInputCapture())
	{
		ActiveFreeCameraActor->Destroy();
		ActiveFreeCameraActor = nullptr;
		SavedViewTarget = nullptr;
		return false;
	}

	PlayerController->SetViewTargetWithBlend(ActiveFreeCameraActor, ProjectGameplayFreeCameraPrivate::BlendTime);
	UE_LOG(
		LogProjectGameplayFreeCamera,
		Verbose,
		TEXT("[ProjectGameplayFreeCamera] Started. SavedViewTarget=%s FreeCamera=%s FOV=%.2f"),
		*GetNameSafe(SavedViewTarget.Get()),
		*GetNameSafe(ActiveFreeCameraActor),
		CameraFov);
	return true;
}

void UProjectGameplayFreeCameraSubsystem::StopGameplayFreeCamera()
{
	const bool bHadActiveCamera = ActiveFreeCameraActor != nullptr;
	const bool bHadInputCapture = FreeCameraInputComponent != nullptr;
	ClearFreeCameraInput();
	if (!bHadActiveCamera && !bHadInputCapture)
	{
		return;
	}

	RestoreFreeCameraInputCapture();

	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (PlayerController)
	{
		AActor* CurrentViewTarget = PlayerController->GetViewTarget();
		AActor* RestoreTarget = ResolveRestoreViewTarget(PlayerController);
		if (RestoreTarget && CurrentViewTarget != RestoreTarget)
		{
			const float BlendTime = (ActiveFreeCameraActor && CurrentViewTarget == ActiveFreeCameraActor)
				? ProjectGameplayFreeCameraPrivate::BlendTime
				: 0.0f;
			PlayerController->SetViewTargetWithBlend(RestoreTarget, BlendTime);
		}
	}

	if (ACameraActor* CameraToDestroy = ActiveFreeCameraActor)
	{
		CameraToDestroy->SetLifeSpan(FMath::Max(ProjectGameplayFreeCameraPrivate::BlendTime + 0.05f, 0.05f));
		ActiveFreeCameraActor = nullptr;
	}

	SavedViewTarget = nullptr;
	ScheduleDeferredViewTargetRestore(PlayerController);
}

bool UProjectGameplayFreeCameraSubsystem::IsGameplayFreeCameraActive() const
{
	return ActiveFreeCameraActor != nullptr;
}

bool UProjectGameplayFreeCameraSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		DetachFromTrackedPlayerController(true);
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController(true);
		return false;
	}

	AttachToPlayerController(PlayerController);
	TrackedPawn = PlayerController->GetPawn();
	return TrackedPlayerController != nullptr && ToggleInputComponent != nullptr;
}

void UProjectGameplayFreeCameraSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindToggleInputToTrackedPlayerController();
		if (TrackedPawn != PlayerController->GetPawn())
		{
			HandlePossessedPawnChanged(TrackedPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController(true);

	TrackedPlayerController = PlayerController;
	TrackedPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	BindToggleInputToTrackedPlayerController();
}

void UProjectGameplayFreeCameraSubsystem::DetachFromTrackedPlayerController(const bool bStopActiveCamera)
{
	if (bStopActiveCamera)
	{
		StopGameplayFreeCamera();
	}
	else
	{
		RestoreFreeCameraInputCapture();
		ClearFreeCameraInput();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	UnbindToggleInputFromTrackedPlayerController();
	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	SavedViewTarget = nullptr;
}

void UProjectGameplayFreeCameraSubsystem::BindToggleInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || ToggleInputComponent)
	{
		return;
	}

	ToggleInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectGameplayFreeCameraToggleInputComponent"));
	if (!ToggleInputComponent)
	{
		return;
	}

	ToggleInputComponent->bBlockInput = false;
	ToggleInputComponent->Priority = ProjectGameplayFreeCameraPrivate::ToggleInputPriority;
	ToggleInputComponent->RegisterComponent();

	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	FInputKeyBinding& ToggleBinding = ToggleInputComponent->BindKey(
		InputSettings ? InputSettings->ToggleGameplayFreeCameraKey : EKeys::O,
		IE_Pressed,
		this,
		&ThisClass::HandleTogglePressed);
	ToggleBinding.bConsumeInput = true;

	TrackedPlayerController->PushInputComponent(ToggleInputComponent);
}

void UProjectGameplayFreeCameraSubsystem::UnbindToggleInputFromTrackedPlayerController()
{
	if (!ToggleInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(ToggleInputComponent);
	}

	if (ToggleInputComponent->IsRegistered())
	{
		ToggleInputComponent->DestroyComponent();
	}

	ToggleInputComponent = nullptr;
}

bool UProjectGameplayFreeCameraSubsystem::ApplyFreeCameraInputCapture()
{
	if (FreeCameraInputComponent)
	{
		return true;
	}

	if (!TrackedPlayerController)
	{
		return false;
	}

	FreeCameraInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectGameplayFreeCameraInputComponent"));
	if (!FreeCameraInputComponent)
	{
		return false;
	}

	FreeCameraInputComponent->bBlockInput = false;
	FreeCameraInputComponent->Priority = ProjectGameplayFreeCameraPrivate::ActiveCameraInputPriority;
	FreeCameraInputComponent->RegisterComponent();

	auto BindCameraKey = [this](const FKey& Key, const EInputEvent InputEvent, void (ThisClass::*Handler)())
	{
		FInputKeyBinding& Binding = FreeCameraInputComponent->BindKey(Key, InputEvent, this, Handler);
		Binding.bConsumeInput = true;
	};

	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	BindCameraKey(InputSettings ? InputSettings->ToggleGameplayFreeCameraKey : EKeys::O, IE_Pressed, &ThisClass::HandleTogglePressed);
	BindCameraKey(EKeys::NumPadEight, IE_Pressed, &ThisClass::HandlePitchUpPressed);
	BindCameraKey(EKeys::NumPadEight, IE_Released, &ThisClass::HandlePitchUpReleased);
	BindCameraKey(EKeys::NumPadTwo, IE_Pressed, &ThisClass::HandlePitchDownPressed);
	BindCameraKey(EKeys::NumPadTwo, IE_Released, &ThisClass::HandlePitchDownReleased);
	BindCameraKey(EKeys::NumPadFour, IE_Pressed, &ThisClass::HandleYawLeftPressed);
	BindCameraKey(EKeys::NumPadFour, IE_Released, &ThisClass::HandleYawLeftReleased);
	BindCameraKey(EKeys::NumPadSix, IE_Pressed, &ThisClass::HandleYawRightPressed);
	BindCameraKey(EKeys::NumPadSix, IE_Released, &ThisClass::HandleYawRightReleased);
	BindCameraKey(EKeys::NumPadSeven, IE_Pressed, &ThisClass::HandleMoveUpPressed);
	BindCameraKey(EKeys::NumPadSeven, IE_Released, &ThisClass::HandleMoveUpReleased);
	BindCameraKey(EKeys::NumPadNine, IE_Pressed, &ThisClass::HandleMoveDownPressed);
	BindCameraKey(EKeys::NumPadNine, IE_Released, &ThisClass::HandleMoveDownReleased);
	BindCameraKey(EKeys::NumPadFive, IE_Pressed, &ThisClass::HandleMoveForwardPressed);
	BindCameraKey(EKeys::NumPadFive, IE_Released, &ThisClass::HandleMoveForwardReleased);
	BindCameraKey(EKeys::NumPadZero, IE_Pressed, &ThisClass::HandleMoveBackwardPressed);
	BindCameraKey(EKeys::NumPadZero, IE_Released, &ThisClass::HandleMoveBackwardReleased);
	BindCameraKey(EKeys::NumPadOne, IE_Pressed, &ThisClass::HandleRollLeftPressed);
	BindCameraKey(EKeys::NumPadOne, IE_Released, &ThisClass::HandleRollLeftReleased);
	BindCameraKey(EKeys::NumPadThree, IE_Pressed, &ThisClass::HandleRollRightPressed);
	BindCameraKey(EKeys::NumPadThree, IE_Released, &ThisClass::HandleRollRightReleased);

	TrackedPlayerController->PushInputComponent(FreeCameraInputComponent);
	return true;
}

void UProjectGameplayFreeCameraSubsystem::RestoreFreeCameraInputCapture()
{
	if (!FreeCameraInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(FreeCameraInputComponent);
	}

	if (FreeCameraInputComponent->IsRegistered())
	{
		FreeCameraInputComponent->DestroyComponent();
	}

	FreeCameraInputComponent = nullptr;
}

bool UProjectGameplayFreeCameraSubsystem::CanStartGameplayFreeCamera() const
{
	if (!TrackedPlayerController || !TrackedPlayerController->IsLocalController() || !TrackedPlayerController->GetPawn())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->IsNetMode(NM_DedicatedServer) || UGameplayStatics::IsGamePaused(World))
	{
		return false;
	}

	if (IsProjectEmoteFlowBlocking())
	{
		return false;
	}

	return !TrackedPlayerController->IsMoveInputIgnored() && !TrackedPlayerController->IsLookInputIgnored();
}

bool UProjectGameplayFreeCameraSubsystem::ShouldForceStopGameplayFreeCamera() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->IsNetMode(NM_DedicatedServer))
	{
		return true;
	}

	if (!TrackedPlayerController || !TrackedPlayerController->IsLocalController() || !IsValid(ActiveFreeCameraActor))
	{
		return true;
	}

	return false;
}

bool UProjectGameplayFreeCameraSubsystem::IsProjectEmoteFlowBlocking() const
{
	const UWorld* World = GetWorld();
	const UProjectEmoteSubsystem* EmoteSubsystem = World ? World->GetSubsystem<UProjectEmoteSubsystem>() : nullptr;
	return EmoteSubsystem
		&& (EmoteSubsystem->IsEmoteMenuOpen()
			|| EmoteSubsystem->IsEmoteActive()
			|| EmoteSubsystem->IsEmoteTransitionPending()
			|| EmoteSubsystem->IsRuntimeActionActive());
}

void UProjectGameplayFreeCameraSubsystem::EnsureGameplayFreeCameraViewTarget()
{
	if (!TrackedPlayerController || !ActiveFreeCameraActor)
	{
		return;
	}

	if (TrackedPlayerController->GetViewTarget() != ActiveFreeCameraActor)
	{
		TrackedPlayerController->SetViewTargetWithBlend(ActiveFreeCameraActor, 0.0f);
	}
}

void UProjectGameplayFreeCameraSubsystem::UpdateFreeCamera(const float DeltaTime)
{
	if (!ActiveFreeCameraActor || DeltaTime <= 0.0f)
	{
		return;
	}

	const float PitchInput = ProjectGameplayFreeCameraPrivate::ResolveSignedInput(bPitchUpPressed, bPitchDownPressed);
	const float YawInput = ProjectGameplayFreeCameraPrivate::ResolveSignedInput(bYawRightPressed, bYawLeftPressed);
	const float VerticalInput = ProjectGameplayFreeCameraPrivate::ResolveSignedInput(bMoveUpPressed, bMoveDownPressed);
	const float ForwardInput = ProjectGameplayFreeCameraPrivate::ResolveSignedInput(bMoveForwardPressed, bMoveBackwardPressed);
	const float RollInput = ProjectGameplayFreeCameraPrivate::ResolveSignedInput(bRollRightPressed, bRollLeftPressed);

	FRotator CameraRotation = ActiveFreeCameraActor->GetActorRotation();
	const float RotationDelta = ProjectGameplayFreeCameraPrivate::RotationSpeedDegrees * DeltaTime;
	CameraRotation.Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(CameraRotation.Pitch + (PitchInput * RotationDelta)),
		ProjectGameplayFreeCameraPrivate::MinPitchDegrees,
		ProjectGameplayFreeCameraPrivate::MaxPitchDegrees);
	CameraRotation.Yaw = FRotator::NormalizeAxis(CameraRotation.Yaw + (YawInput * RotationDelta));
	CameraRotation.Roll = FRotator::NormalizeAxis(CameraRotation.Roll + (RollInput * RotationDelta));
	ActiveFreeCameraActor->SetActorRotation(CameraRotation);

	const FVector MoveDirection = ((CameraRotation.Vector() * ForwardInput) + (FVector::UpVector * VerticalInput)).GetClampedToMaxSize(1.0f);
	if (!MoveDirection.IsNearlyZero())
	{
		ActiveFreeCameraActor->AddActorWorldOffset(MoveDirection * ProjectGameplayFreeCameraPrivate::MoveSpeed * DeltaTime, false);
	}
}

void UProjectGameplayFreeCameraSubsystem::ClearFreeCameraInput()
{
	bPitchUpPressed = false;
	bPitchDownPressed = false;
	bYawLeftPressed = false;
	bYawRightPressed = false;
	bMoveUpPressed = false;
	bMoveDownPressed = false;
	bMoveForwardPressed = false;
	bMoveBackwardPressed = false;
	bRollLeftPressed = false;
	bRollRightPressed = false;
}

AActor* UProjectGameplayFreeCameraSubsystem::ResolveRestoreViewTarget(APlayerController* PlayerController) const
{
	if (PlayerController)
	{
		if (APawn* ControlledPawn = PlayerController->GetPawn())
		{
			if (IsValid(ControlledPawn))
			{
				return ControlledPawn;
			}
		}
	}

	if (APawn* Pawn = TrackedPawn.Get())
	{
		if (IsValid(Pawn))
		{
			return Pawn;
		}
	}

	AActor* PreviousViewTarget = SavedViewTarget.Get();
	if (PreviousViewTarget && IsValid(PreviousViewTarget) && PreviousViewTarget != ActiveFreeCameraActor)
	{
		return PreviousViewTarget;
	}

	return nullptr;
}

void UProjectGameplayFreeCameraSubsystem::ScheduleDeferredViewTargetRestore(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
		World->GetTimerManager().SetTimer(
			DeferredViewTargetRestoreTimerHandle,
			this,
			&ThisClass::RestorePostFreeCameraViewTarget,
			FMath::Max(ProjectGameplayFreeCameraPrivate::BlendTime + 0.05f, 0.05f),
			false);
	}
}

void UProjectGameplayFreeCameraSubsystem::RestorePostFreeCameraViewTarget()
{
	if (IsGameplayFreeCameraActive())
	{
		return;
	}

	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	AActor* RestoreTarget = ResolveRestoreViewTarget(PlayerController);
	if (RestoreTarget && PlayerController->GetViewTarget() != RestoreTarget)
	{
		PlayerController->SetViewTargetWithBlend(RestoreTarget, 0.0f);
	}
}

void UProjectGameplayFreeCameraSubsystem::MarkMaintenanceRequired()
{
	bNeedsMaintenanceTick = true;
}

void UProjectGameplayFreeCameraSubsystem::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	TrackedPawn = NewPawn;
	MarkMaintenanceRequired();
}

void UProjectGameplayFreeCameraSubsystem::HandleTogglePressed()
{
	if (IsGameplayFreeCameraActive())
	{
		StopGameplayFreeCamera();
		return;
	}

	StartGameplayFreeCamera();
}

void UProjectGameplayFreeCameraSubsystem::HandlePitchUpPressed()
{
	bPitchUpPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandlePitchUpReleased()
{
	bPitchUpPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandlePitchDownPressed()
{
	bPitchDownPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandlePitchDownReleased()
{
	bPitchDownPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleYawLeftPressed()
{
	bYawLeftPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleYawLeftReleased()
{
	bYawLeftPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleYawRightPressed()
{
	bYawRightPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleYawRightReleased()
{
	bYawRightPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveUpPressed()
{
	bMoveUpPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveUpReleased()
{
	bMoveUpPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveDownPressed()
{
	bMoveDownPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveDownReleased()
{
	bMoveDownPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveForwardPressed()
{
	bMoveForwardPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveForwardReleased()
{
	bMoveForwardPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveBackwardPressed()
{
	bMoveBackwardPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleMoveBackwardReleased()
{
	bMoveBackwardPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleRollLeftPressed()
{
	bRollLeftPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleRollLeftReleased()
{
	bRollLeftPressed = false;
}

void UProjectGameplayFreeCameraSubsystem::HandleRollRightPressed()
{
	bRollRightPressed = true;
}

void UProjectGameplayFreeCameraSubsystem::HandleRollRightReleased()
{
	bRollRightPressed = false;
}
