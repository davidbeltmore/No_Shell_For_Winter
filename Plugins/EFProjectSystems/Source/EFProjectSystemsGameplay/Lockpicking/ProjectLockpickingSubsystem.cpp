#include "Lockpicking/ProjectLockpickingSubsystem.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/UserWidget.h"
#include "Components/ACFInteractionComponent.h"
#include "EFProjectUISettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Lockpicking/ProjectLockpickPromptWidget.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Lockpicking/ProjectLockpickingWidget.h"
#include "TimerManager.h"
#include "UI/ProjectWidgetClassResolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectLockpicking, Log, All);

namespace ProjectLockpickingSubsystemPrivate
{
	constexpr int32 DefaultWidgetZOrder = 10040;
}

void UProjectLockpickingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ActiveLockedActor = nullptr;
	ActiveLockpickableComponent = nullptr;
	BoundLockpickableComponent = nullptr;
	ActivePlayerController = nullptr;
	ActivePawn = nullptr;
	ActiveWidget = nullptr;
	ActivePromptWidget = nullptr;
	InputSnapshot = FProjectLockpickingInputStateSnapshot();
	bLockpickingOpen = false;
	bPromptOpen = false;
}

void UProjectLockpickingSubsystem::Deinitialize()
{
	CloseLockpicking();
	CloseLockpickPrompt();
	UnbindFromActiveComponent();
	Super::Deinitialize();
}

void UProjectLockpickingSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	if (!bLockpickingOpen)
	{
		if (bPromptOpen && (!ActiveLockpickableComponent || !ActivePawn || !ActiveLockpickableComponent->IsLocked()))
		{
			CloseLockpickPrompt();
		}
		return;
	}

	if (!ActiveLockpickableComponent || !ActivePawn || !ActiveLockpickableComponent->IsLocked())
	{
		CloseLockpicking();
	}
}

TStatId UProjectLockpickingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectLockpickingSubsystem, STATGROUP_Tickables);
}

bool UProjectLockpickingSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

bool UProjectLockpickingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectLockpickingSubsystem::OpenLockpicking(
	AActor* LockedActor,
	UProjectLockpickableComponent* LockpickableComponent,
	APawn* InteractingPawn)
{
	if (!LockedActor || !LockpickableComponent || !InteractingPawn || !LockpickableComponent->IsLocked())
	{
		return false;
	}

	if (!ResolveLocalContext(InteractingPawn))
	{
		return false;
	}

	if (bLockpickingOpen && ActiveLockpickableComponent && ActiveLockpickableComponent != LockpickableComponent)
	{
		CloseLockpicking();
	}

	if (bPromptOpen)
	{
		CloseLockpickPrompt();
	}

	ActiveLockedActor = LockedActor;
	ActiveLockpickableComponent = LockpickableComponent;
	ActivePawn = InteractingPawn;

	UnbindFromActiveComponent();
	BoundLockpickableComponent = ActiveLockpickableComponent;
	if (BoundLockpickableComponent)
	{
		BoundLockpickableComponent->OnLockpickSucceeded.AddUniqueDynamic(this, &ThisClass::HandleLockpickSucceeded);
		BoundLockpickableComponent->OnLockpickFailed.AddUniqueDynamic(this, &ThisClass::HandleLockpickFailed);
	}

	EnsureLockpickingWidget(ActivePlayerController);
	if (!ActiveWidget)
	{
		return false;
	}

	if (!ActiveWidget->IsInViewport())
	{
		if (!ActiveWidget->AddToPlayerScreen(ProjectLockpickingSubsystemPrivate::DefaultWidgetZOrder))
		{
			ActiveWidget->AddToViewport(ProjectLockpickingSubsystemPrivate::DefaultWidgetZOrder);
		}
	}

	ActiveWidget->Configure(ActiveLockpickableComponent, ActivePawn);
	if (!bLockpickingOpen)
	{
		ApplyInputCapture(ActiveWidget, false);
		bLockpickingOpen = true;
	}
	else
	{
		UpdateInputCaptureFocus(ActiveWidget, false);
	}

	RefreshLockpickingFocusNextTick();
	return true;
}

void UProjectLockpickingSubsystem::CloseLockpicking()
{
	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
	}

	if (bLockpickingOpen)
	{
		RestoreInputCapture();
	}

	bLockpickingOpen = false;
	ActiveLockedActor = nullptr;
	ActiveLockpickableComponent = nullptr;
	ActivePawn = nullptr;
	UnbindFromActiveComponent();
}

bool UProjectLockpickingSubsystem::IsLockpickingOpen() const
{
	return bLockpickingOpen;
}

bool UProjectLockpickingSubsystem::OpenLockpickPrompt(
	AActor* LockedActor,
	UProjectLockpickableComponent* LockpickableComponent,
	APawn* InteractingPawn)
{
	if (!LockedActor || !LockpickableComponent || !InteractingPawn || !LockpickableComponent->IsLocked())
	{
		return false;
	}

	if (!ResolveLocalContext(InteractingPawn))
	{
		return false;
	}

	if (bLockpickingOpen)
	{
		CloseLockpicking();
	}

	ActiveLockedActor = LockedActor;
	ActiveLockpickableComponent = LockpickableComponent;
	ActivePawn = InteractingPawn;

	EnsureLockpickPromptWidget(ActivePlayerController);
	if (!ActivePromptWidget)
	{
		return false;
	}

	if (!ActivePromptWidget->IsInViewport())
	{
		if (!ActivePromptWidget->AddToPlayerScreen(ProjectLockpickingSubsystemPrivate::DefaultWidgetZOrder))
		{
			ActivePromptWidget->AddToViewport(ProjectLockpickingSubsystemPrivate::DefaultWidgetZOrder);
		}
	}

	ActivePromptWidget->Configure(
		ActiveLockpickableComponent->CanPawnAttemptLockpick(ActivePawn),
		ActiveLockpickableComponent->GetRequiredLockpickCount(ActivePawn),
		ActiveLockpickableComponent->GetRequiredItemDisplayName());

	if (!bPromptOpen)
	{
		ApplyInputCapture(ActivePromptWidget, true);
		bPromptOpen = true;
	}
	else
	{
		UpdateInputCaptureFocus(ActivePromptWidget, true);
	}

	RefreshPromptFocusNextTick();
	return true;
}

void UProjectLockpickingSubsystem::CloseLockpickPrompt()
{
	if (ActivePromptWidget)
	{
		ActivePromptWidget->RemoveFromParent();
	}

	if (bPromptOpen && !bLockpickingOpen)
	{
		RestoreInputCapture();
	}

	bPromptOpen = false;
	if (!bLockpickingOpen)
	{
		ActiveLockedActor = nullptr;
		ActiveLockpickableComponent = nullptr;
		ActivePawn = nullptr;
	}
}

bool UProjectLockpickingSubsystem::IsLockpickPromptOpen() const
{
	return bPromptOpen;
}

void UProjectLockpickingSubsystem::EnsureLockpickingWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (ActiveWidget)
	{
		ActiveWidget->Configure(ActiveLockpickableComponent, ActivePawn);
		return;
	}

	const TSubclassOf<UProjectLockpickingWidget> WidgetClass = ResolveLockpickingWidgetClass();
	ActiveWidget = CreateWidget<UProjectLockpickingWidget>(PlayerController, WidgetClass, TEXT("ProjectLockpickingWidget"));
	if (!ActiveWidget)
	{
		UE_LOG(LogProjectLockpicking, Warning, TEXT("[ProjectLockpicking] Failed to create lockpicking widget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	ActiveWidget->Configure(ActiveLockpickableComponent, ActivePawn);
	ActiveWidget->OnConfirmRequested.RemoveAll(this);
	ActiveWidget->OnConfirmRequested.AddUObject(this, &ThisClass::HandleConfirmRequested);
}

void UProjectLockpickingSubsystem::EnsureLockpickPromptWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (ActivePromptWidget)
	{
		ActivePromptWidget->Configure(
			ActiveLockpickableComponent ? ActiveLockpickableComponent->CanPawnAttemptLockpick(ActivePawn) : false,
			ActiveLockpickableComponent ? ActiveLockpickableComponent->GetRequiredLockpickCount(ActivePawn) : 0,
			ActiveLockpickableComponent ? ActiveLockpickableComponent->GetRequiredItemDisplayName() : FText::GetEmpty());
		ActivePromptWidget->OnLockpickSelected.RemoveAll(this);
		ActivePromptWidget->OnCancelSelected.RemoveAll(this);
		ActivePromptWidget->OnLockpickSelected.AddUObject(this, &ThisClass::HandlePromptLockpickSelected);
		ActivePromptWidget->OnCancelSelected.AddUObject(this, &ThisClass::HandlePromptCancelSelected);
		return;
	}

	const TSubclassOf<UProjectLockpickPromptWidget> WidgetClass = ResolveLockpickPromptWidgetClass();
	ActivePromptWidget = CreateWidget<UProjectLockpickPromptWidget>(PlayerController, WidgetClass, TEXT("ProjectLockpickPromptWidget"));
	if (!ActivePromptWidget)
	{
		UE_LOG(LogProjectLockpicking, Warning, TEXT("[ProjectLockpicking] Failed to create lockpick prompt widget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	ActivePromptWidget->Configure(
		ActiveLockpickableComponent ? ActiveLockpickableComponent->CanPawnAttemptLockpick(ActivePawn) : false,
		ActiveLockpickableComponent ? ActiveLockpickableComponent->GetRequiredLockpickCount(ActivePawn) : 0,
		ActiveLockpickableComponent ? ActiveLockpickableComponent->GetRequiredItemDisplayName() : FText::GetEmpty());
	ActivePromptWidget->OnLockpickSelected.RemoveAll(this);
	ActivePromptWidget->OnCancelSelected.RemoveAll(this);
	ActivePromptWidget->OnLockpickSelected.AddUObject(this, &ThisClass::HandlePromptLockpickSelected);
	ActivePromptWidget->OnCancelSelected.AddUObject(this, &ThisClass::HandlePromptCancelSelected);
}

TSubclassOf<UProjectLockpickingWidget> UProjectLockpickingSubsystem::ResolveLockpickingWidgetClass() const
{
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	return ProjectWidgetClassResolver::ResolveWidgetClass<UProjectLockpickingWidget>(
		UISettings ? UISettings->LockpickingWidgetClass : FSoftClassPath(),
		TEXT("LockpickingMinigame"));
}

TSubclassOf<UProjectLockpickPromptWidget> UProjectLockpickingSubsystem::ResolveLockpickPromptWidgetClass() const
{
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	return ProjectWidgetClassResolver::ResolveWidgetClass<UProjectLockpickPromptWidget>(
		UISettings ? UISettings->LockpickingPromptWidgetClass : FSoftClassPath(),
		TEXT("LockpickPrompt"));
}

void UProjectLockpickingSubsystem::ApplyInputCapture(UUserWidget* WidgetToFocus, const bool bShowCursor)
{
	InputSnapshot = FProjectLockpickingInputStateSnapshot();

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

void UProjectLockpickingSubsystem::UpdateInputCaptureFocus(UUserWidget* WidgetToFocus, const bool bShowCursor)
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

void UProjectLockpickingSubsystem::RestoreInputCapture()
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

	InputSnapshot = FProjectLockpickingInputStateSnapshot();
}

void UProjectLockpickingSubsystem::HandleConfirmRequested()
{
	if (!ActivePawn || !ActiveLockpickableComponent)
	{
		CloseLockpicking();
		return;
	}

	const FString ConfirmInteractionType = ActiveLockpickableComponent->BuildConfirmInteractionType();
	if (ConfirmInteractionType.IsEmpty())
	{
		if (ActiveWidget)
		{
			ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectLockpicking", "WaitingForSession", "Waiting..."), EFProjectUIPalette::SecondaryText(0.92f));
		}
		return;
	}

	if (UACFInteractionComponent* InteractionComponent = ActivePawn->FindComponentByClass<UACFInteractionComponent>())
	{
		InteractionComponent->Interact(ConfirmInteractionType);
		return;
	}

	UE_LOG(LogProjectLockpicking, Warning, TEXT("[ProjectLockpicking] %s has no UACFInteractionComponent for lockpick confirmation"), *GetNameSafe(ActivePawn));
	CloseLockpicking();
}

void UProjectLockpickingSubsystem::HandlePromptLockpickSelected()
{
	if (!ActivePawn || !ActiveLockpickableComponent)
	{
		CloseLockpickPrompt();
		return;
	}

	if (!ActiveLockpickableComponent->CanPawnAttemptLockpick(ActivePawn))
	{
		if (ActivePromptWidget)
		{
			ActivePromptWidget->Configure(
				false,
				ActiveLockpickableComponent->GetRequiredLockpickCount(ActivePawn),
				ActiveLockpickableComponent->GetRequiredItemDisplayName());
			RefreshPromptFocusNextTick();
		}
		return;
	}

	const FString BeginInteractionType = ActiveLockpickableComponent->BuildBeginInteractionType();
	if (BeginInteractionType.IsEmpty())
	{
		CloseLockpickPrompt();
		return;
	}

	UACFInteractionComponent* InteractionComponent = ActivePawn->FindComponentByClass<UACFInteractionComponent>();
	if (!InteractionComponent)
	{
		UE_LOG(LogProjectLockpicking, Warning, TEXT("[ProjectLockpicking] %s has no UACFInteractionComponent for lockpick begin"), *GetNameSafe(ActivePawn));
		CloseLockpickPrompt();
		return;
	}

	CloseLockpickPrompt();
	InteractionComponent->Interact(BeginInteractionType);
}

void UProjectLockpickingSubsystem::HandlePromptCancelSelected()
{
	CloseLockpickPrompt();
}

void UProjectLockpickingSubsystem::HandleLockpickSucceeded(APawn* Pawn, const int32 SessionId, const float PulseValue)
{
	(void)Pawn;
	(void)SessionId;
	(void)PulseValue;

	if (ActiveWidget)
	{
		ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectLockpicking", "Success", "Unlocked"), EFProjectUIPalette::Positive());
	}

	CloseLockpicking();
}

void UProjectLockpickingSubsystem::HandleLockpickFailed(APawn* Pawn, const int32 SessionId, const float PulseValue)
{
	(void)Pawn;
	(void)SessionId;
	(void)PulseValue;

	if (ActiveWidget)
	{
		ActiveWidget->ShowFeedback(NSLOCTEXT("ProjectLockpicking", "Failed", "Failed"), EFProjectUIPalette::Negative());
	}

	CloseLockpicking();
}

bool UProjectLockpickingSubsystem::ResolveLocalContext(APawn* InteractingPawn)
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

void UProjectLockpickingSubsystem::UnbindFromActiveComponent()
{
	if (!BoundLockpickableComponent)
	{
		return;
	}

	BoundLockpickableComponent->OnLockpickSucceeded.RemoveDynamic(this, &ThisClass::HandleLockpickSucceeded);
	BoundLockpickableComponent->OnLockpickFailed.RemoveDynamic(this, &ThisClass::HandleLockpickFailed);
	BoundLockpickableComponent = nullptr;
}

void UProjectLockpickingSubsystem::RefreshLockpickingFocusNextTick()
{
	if (!ActiveWidget)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		ActiveWidget->FocusLockpickingWidget();
		return;
	}

	FTimerDelegate FocusDelegate;
	FocusDelegate.BindWeakLambda(ActiveWidget, [Widget = TWeakObjectPtr<UProjectLockpickingWidget>(ActiveWidget)]()
	{
		if (Widget.IsValid())
		{
			Widget->FocusLockpickingWidget();
		}
	});
	World->GetTimerManager().SetTimerForNextTick(FocusDelegate);
}

void UProjectLockpickingSubsystem::RefreshPromptFocusNextTick()
{
	if (!ActivePromptWidget)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		ActivePromptWidget->FocusPromptWidget();
		return;
	}

	FTimerDelegate FocusDelegate;
	FocusDelegate.BindWeakLambda(ActivePromptWidget, [Widget = TWeakObjectPtr<UProjectLockpickPromptWidget>(ActivePromptWidget)]()
	{
		if (Widget.IsValid())
		{
			Widget->FocusPromptWidget();
		}
	});
	World->GetTimerManager().SetTimerForNextTick(FocusDelegate);
}
