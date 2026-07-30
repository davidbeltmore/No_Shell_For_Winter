#include "Training/ProjectTrainingSubsystem.h"

#include "EFProjectUIPalette.h"
#include "ACFTrainingComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "TimerManager.h"
#include "Training/ProjectTrainingLockpickWidget.h"
#include "UI/ProjectWidgetClassResolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectTraining, Log, All);

namespace ProjectTrainingSubsystemPrivate
{
	constexpr int32 DefaultWidgetZOrder = 10045;
}

void UProjectTrainingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ActiveTrainingComponent = nullptr;
	BoundTrainingComponent = nullptr;
	ActivePlayerController = nullptr;
	ActivePawn = nullptr;
	ActiveWidget = nullptr;
	InputSnapshot = FProjectTrainingInputStateSnapshot();
	bTrainingOpen = false;
}

void UProjectTrainingSubsystem::Deinitialize()
{
	CloseTraining(false);
	UnbindFromTrainingComponent();
	Super::Deinitialize();
}

void UProjectTrainingSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	if (!bTrainingOpen)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World && World->GetTimerManager().IsTimerActive(CloseAfterResultTimerHandle))
	{
		return;
	}

	if (!ActiveTrainingComponent || !ActivePawn || !ActiveTrainingComponent->IsTrainingActive())
	{
		CloseTraining(false);
	}
}

TStatId UProjectTrainingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectTrainingSubsystem, STATGROUP_Tickables);
}

bool UProjectTrainingSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

bool UProjectTrainingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectTrainingSubsystem::OpenTraining(UACFTrainingComponent* TrainingComponent, APawn* InteractingPawn)
{
	if (!TrainingComponent || !InteractingPawn || !TrainingComponent->IsTrainingActive())
	{
		return false;
	}

	if (!ResolveLocalContext(InteractingPawn))
	{
		return false;
	}

	if (bTrainingOpen && ActiveTrainingComponent && ActiveTrainingComponent != TrainingComponent)
	{
		CloseTraining(false);
	}

	ActiveTrainingComponent = TrainingComponent;
	ActivePawn = InteractingPawn;

	UnbindFromTrainingComponent();
	BoundTrainingComponent = ActiveTrainingComponent;
	if (BoundTrainingComponent)
	{
		BoundTrainingComponent->OnTrainingCompleted.AddUniqueDynamic(this, &ThisClass::HandleTrainingCompleted);
		BoundTrainingComponent->OnTrainingRejected.AddUniqueDynamic(this, &ThisClass::HandleTrainingRejected);
	}

	EnsureTrainingWidget(ActivePlayerController);
	if (!ActiveWidget)
	{
		return false;
	}

	if (!ActiveWidget->IsInViewport())
	{
		if (!ActiveWidget->AddToPlayerScreen(ProjectTrainingSubsystemPrivate::DefaultWidgetZOrder))
		{
			ActiveWidget->AddToViewport(ProjectTrainingSubsystemPrivate::DefaultWidgetZOrder);
		}
	}

	ActiveWidget->Configure(ActiveTrainingComponent, ActivePawn);
	ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectTraining", "Ready", "Time the center"), EFProjectUIPalette::SecondaryText(0.95f));

	if (!bTrainingOpen)
	{
		ApplyInputCapture(ActiveWidget, true);
		bTrainingOpen = true;
	}
	else
	{
		UpdateInputCaptureFocus(ActiveWidget, true);
	}

	RefreshTrainingFocusNextTick();
	return true;
}

void UProjectTrainingSubsystem::CloseTraining(const bool bCancelTraining)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(CloseAfterResultTimerHandle);
	}

	if (bCancelTraining && ActiveTrainingComponent && ActiveTrainingComponent->IsTrainingActive())
	{
		ActiveTrainingComponent->CancelTraining();
		StopTrainingEmote();
	}

	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
	}

	if (bTrainingOpen)
	{
		RestoreInputCapture();
	}

	bTrainingOpen = false;
	ActiveTrainingComponent = nullptr;
	ActivePawn = nullptr;
	UnbindFromTrainingComponent();
}

bool UProjectTrainingSubsystem::IsTrainingOpen() const
{
	return bTrainingOpen;
}

void UProjectTrainingSubsystem::HandleTrainingCompleted(const FName TrainingId, const EACFTrainingSessionResult Result, const FGameplayTag TargetAttribute, const float RewardValue)
{
	(void)TrainingId;
	(void)TargetAttribute;

	if (ActiveWidget)
	{
		if (Result == EACFTrainingSessionResult::Succeeded)
		{
			ActiveWidget->ShowFeedback(
				FText::Format(NSLOCTEXT("ProjectTraining", "Succeeded", "Success +{0}"), FText::AsNumber(FMath::RoundToInt(RewardValue))),
				EFProjectUIPalette::Positive());
		}
		else if (Result == EACFTrainingSessionResult::Failed)
		{
			ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectTraining", "Failed", "Failed"), EFProjectUIPalette::Negative());
		}
		else
		{
			ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectTraining", "Cancelled", "Cancelled"), EFProjectUIPalette::SecondaryText(0.95f));
		}
	}

	StopTrainingEmote();

	UWorld* World = GetWorld();
	if (!World)
	{
		CloseTraining(false);
		return;
	}

	World->GetTimerManager().SetTimer(CloseAfterResultTimerHandle, this, &ThisClass::CloseTrainingAfterResult, 0.45f, false);
}

void UProjectTrainingSubsystem::HandleTrainingRejected(const FName TrainingId, const FText Reason)
{
	(void)TrainingId;

	if (ActiveWidget)
	{
		ActiveWidget->ShowFeedback(Reason.IsEmpty() ? NSLOCTEXT("ProjectTraining", "Blocked", "Blocked") : Reason, EFProjectUIPalette::Negative());
	}

	StopTrainingEmote();
	CloseTraining(false);
}

void UProjectTrainingSubsystem::EnsureTrainingWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (ActiveWidget)
	{
		ActiveWidget->Configure(ActiveTrainingComponent, ActivePawn);
		ActiveWidget->OnConfirmRequested.RemoveAll(this);
		ActiveWidget->OnCancelRequested.RemoveAll(this);
		ActiveWidget->OnConfirmRequested.AddUObject(this, &ThisClass::HandleConfirmRequested);
		ActiveWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleCancelRequested);
		return;
	}

	const TSubclassOf<UProjectTrainingLockpickWidget> WidgetClass = ResolveTrainingWidgetClass();
	ActiveWidget = CreateWidget<UProjectTrainingLockpickWidget>(PlayerController, WidgetClass, TEXT("ProjectTrainingLockpickWidget"));
	if (!ActiveWidget)
	{
		UE_LOG(LogProjectTraining, Warning, TEXT("[ProjectTraining] Failed to create training widget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	ActiveWidget->Configure(ActiveTrainingComponent, ActivePawn);
	ActiveWidget->OnConfirmRequested.RemoveAll(this);
	ActiveWidget->OnCancelRequested.RemoveAll(this);
	ActiveWidget->OnConfirmRequested.AddUObject(this, &ThisClass::HandleConfirmRequested);
	ActiveWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleCancelRequested);
}

TSubclassOf<UProjectTrainingLockpickWidget> UProjectTrainingSubsystem::ResolveTrainingWidgetClass() const
{
	return ProjectWidgetClassResolver::ResolveWidgetClass<UProjectTrainingLockpickWidget>(
		FSoftClassPath(),
		TEXT("TrainingLockpick"));
}

void UProjectTrainingSubsystem::ApplyInputCapture(UUserWidget* WidgetToFocus, const bool bShowCursor)
{
	InputSnapshot = FProjectTrainingInputStateSnapshot();

	if (ActivePlayerController)
	{
		InputSnapshot.bHasSavedControllerState = true;
		InputSnapshot.bWasMoveInputIgnored = ActivePlayerController->IsMoveInputIgnored();
		InputSnapshot.bWasLookInputIgnored = ActivePlayerController->IsLookInputIgnored();
		InputSnapshot.bWasShowMouseCursor = ActivePlayerController->bShowMouseCursor;
		InputSnapshot.bWasClickEventsEnabled = ActivePlayerController->bEnableClickEvents;
		InputSnapshot.bWasMouseOverEventsEnabled = ActivePlayerController->bEnableMouseOverEvents;

		ActivePlayerController->FlushPressedKeys();

		if (!InputSnapshot.bWasMoveInputIgnored)
		{
			ActivePlayerController->SetIgnoreMoveInput(true);
			InputSnapshot.bAppliedMoveInputIgnore = true;
		}

		if (!InputSnapshot.bWasLookInputIgnored)
		{
			ActivePlayerController->SetIgnoreLookInput(true);
			InputSnapshot.bAppliedLookInputIgnore = true;
		}

		ActivePlayerController->DisableInput(ActivePlayerController);
		UpdateInputCaptureFocus(WidgetToFocus, bShowCursor);
	}

	if (ActivePawn)
	{
		if (ACharacter* Character = Cast<ACharacter>(ActivePawn))
		{
			Character->StopJumping();
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				InputSnapshot.bHasSavedMovementState = true;
				InputSnapshot.PreviousMovementMode = Movement->MovementMode;
				InputSnapshot.PreviousCustomMovementMode = Movement->CustomMovementMode;
				Movement->StopMovementImmediately();
				Movement->DisableMovement();
			}
		}

		if (ActivePlayerController)
		{
			ActivePawn->DisableInput(ActivePlayerController);
			InputSnapshot.bPawnInputSuspended = true;
		}
	}
}

void UProjectTrainingSubsystem::UpdateInputCaptureFocus(UUserWidget* WidgetToFocus, const bool bShowCursor)
{
	if (!ActivePlayerController)
	{
		return;
	}

	ActivePlayerController->bShowMouseCursor = bShowCursor;
	ActivePlayerController->bEnableClickEvents = bShowCursor;
	ActivePlayerController->bEnableMouseOverEvents = bShowCursor;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(WidgetToFocus ? WidgetToFocus->TakeWidget() : TSharedPtr<SWidget>());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	ActivePlayerController->SetInputMode(InputMode);
}

void UProjectTrainingSubsystem::RestoreInputCapture()
{
	if (ActivePawn && InputSnapshot.bPawnInputSuspended)
	{
		if (APlayerController* OwningController = Cast<APlayerController>(ActivePawn->GetController()))
		{
			ActivePawn->EnableInput(OwningController);
		}
		else if (ActivePlayerController)
		{
			ActivePawn->EnableInput(ActivePlayerController);
		}
	}

	if (ActivePawn && InputSnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(ActivePawn))
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->SetMovementMode(InputSnapshot.PreviousMovementMode, InputSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (ActivePlayerController && InputSnapshot.bHasSavedControllerState)
	{
		if (InputSnapshot.bAppliedLookInputIgnore)
		{
			ActivePlayerController->SetIgnoreLookInput(false);
		}

		if (InputSnapshot.bAppliedMoveInputIgnore)
		{
			ActivePlayerController->SetIgnoreMoveInput(false);
		}

		ActivePlayerController->EnableInput(ActivePlayerController);
		ActivePlayerController->bShowMouseCursor = InputSnapshot.bWasShowMouseCursor;
		ActivePlayerController->bEnableClickEvents = InputSnapshot.bWasClickEventsEnabled;
		ActivePlayerController->bEnableMouseOverEvents = InputSnapshot.bWasMouseOverEventsEnabled;
		ActivePlayerController->SetInputMode(FInputModeGameOnly());
		ActivePlayerController->FlushPressedKeys();
	}

	InputSnapshot = FProjectTrainingInputStateSnapshot();
}

void UProjectTrainingSubsystem::HandleConfirmRequested()
{
	if (!ActiveTrainingComponent)
	{
		CloseTraining(false);
		return;
	}

	if (!ActiveTrainingComponent->IsTimingMinigameSessionActive())
	{
		if (ActiveWidget)
		{
			ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectTraining", "WaitingForSession", "Waiting..."), EFProjectUIPalette::SecondaryText(0.95f));
		}
		return;
	}

	ActiveTrainingComponent->ConfirmTimingMinigame();
}

void UProjectTrainingSubsystem::HandleCancelRequested()
{
	CloseTraining(true);
}

bool UProjectTrainingSubsystem::ResolveLocalContext(APawn* InteractingPawn)
{
	if (!InteractingPawn)
	{
		return false;
	}

	APlayerController* PlayerController = Cast<APlayerController>(InteractingPawn->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}

	ActivePlayerController = PlayerController;
	ActivePawn = InteractingPawn;
	return true;
}

void UProjectTrainingSubsystem::UnbindFromTrainingComponent()
{
	if (!BoundTrainingComponent)
	{
		return;
	}

	BoundTrainingComponent->OnTrainingCompleted.RemoveDynamic(this, &ThisClass::HandleTrainingCompleted);
	BoundTrainingComponent->OnTrainingRejected.RemoveDynamic(this, &ThisClass::HandleTrainingRejected);
	BoundTrainingComponent = nullptr;
}

void UProjectTrainingSubsystem::RefreshTrainingFocusNextTick()
{
	if (!ActiveWidget)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		ActiveWidget->FocusTrainingWidget();
		return;
	}

	FTimerDelegate FocusDelegate;
	FocusDelegate.BindWeakLambda(ActiveWidget, [Widget = TWeakObjectPtr<UProjectTrainingLockpickWidget>(ActiveWidget)]()
	{
		if (Widget.IsValid())
		{
			Widget->FocusTrainingWidget();
		}
	});
	World->GetTimerManager().SetTimerForNextTick(FocusDelegate);
}

void UProjectTrainingSubsystem::StopTrainingEmote()
{
	if (ActivePawn)
	{
		if (UProjectEmoteComponent* EmoteComponent = ActivePawn->FindComponentByClass<UProjectEmoteComponent>())
		{
			EmoteComponent->StopEmote();
		}
	}
}

void UProjectTrainingSubsystem::CloseTrainingAfterResult()
{
	CloseTraining(false);
}
