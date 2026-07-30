#include "InnerDoctrine/ProjectInnerDoctrineMenuSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "EFProjectUISettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineExchangeMenuWidget.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "TimerManager.h"
#include "UI/ProjectWidgetClassResolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectInnerDoctrineMenu, Log, All);

namespace ProjectInnerDoctrineMenuSubsystemPrivate
{
	constexpr int32 DefaultMenuZOrder = 285;

	struct FSetHudEnabledParams
	{
		bool bEnabled = false;
	};
}

void UProjectInnerDoctrineMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	BoundInnerDoctrineComponent = nullptr;
	TrackedExchangeMenuWidget = nullptr;
	MenuStateSnapshot = FProjectDoctrineExchangeMenuStateSnapshot();
	bMenuOpen = false;
}

void UProjectInnerDoctrineMenuSubsystem::Deinitialize()
{
	CloseMenu();
	UnbindFromTrackedComponent();
	DetachFromTrackedPlayerController(true);
	Super::Deinitialize();
}

void UProjectInnerDoctrineMenuSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();

	if (IsGamePauseBlockingMenu() && bMenuOpen)
	{
		CloseMenu();
	}
}

TStatId UProjectInnerDoctrineMenuSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectInnerDoctrineMenuSubsystem, STATGROUP_Tickables);
}

bool UProjectInnerDoctrineMenuSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectInnerDoctrineMenuSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectInnerDoctrineMenuSubsystem::OpenMenuForActor(AActor* InteractingActor)
{
	if (IsGamePauseBlockingMenu())
	{
		return false;
	}

	TryResolveRuntimeContext();
	if (!ResolveLocalInteractionContext(InteractingActor))
	{
		return false;
	}

	EnsureInnerDoctrineComponent(TrackedPlayerPawn);
	BindToTrackedComponent();
	EnsureExchangeMenuWidget(TrackedPlayerController);
	if (!TrackedPlayerController || !TrackedExchangeMenuWidget || !TrackedInnerDoctrineComponent)
	{
		return false;
	}

	if (!TrackedExchangeMenuWidget->IsInViewport())
	{
		const int32 MenuZOrder = UProjectInnerDoctrineSettings::Get()
			? UProjectInnerDoctrineSettings::Get()->ExchangeMenuZOrder
			: ProjectInnerDoctrineMenuSubsystemPrivate::DefaultMenuZOrder;
		if (!TrackedExchangeMenuWidget->AddToPlayerScreen(MenuZOrder))
		{
			TrackedExchangeMenuWidget->AddToViewport(MenuZOrder);
		}
	}

	TrackedExchangeMenuWidget->SetInnerDoctrineComponent(TrackedInnerDoctrineComponent);
	TrackedExchangeMenuWidget->RefreshDisplay();

	if (!bMenuOpen)
	{
		ApplyMenuInputCapture();
		bMenuOpen = true;
	}

	RefreshMenuFocusNextTick();
	return true;
}

void UProjectInnerDoctrineMenuSubsystem::CloseMenu()
{
	if (TrackedExchangeMenuWidget)
	{
		TrackedExchangeMenuWidget->RemoveFromParent();
	}

	if (bMenuOpen)
	{
		RestoreMenuInputCapture();
	}

	bMenuOpen = false;
}

bool UProjectInnerDoctrineMenuSubsystem::IsMenuOpen() const
{
	return bMenuOpen;
}

bool UProjectInnerDoctrineMenuSubsystem::HasTrackedExchangeMenuWidget() const
{
	return TrackedExchangeMenuWidget != nullptr;
}

bool UProjectInnerDoctrineMenuSubsystem::IsTrackedExchangeMenuWidgetInViewport() const
{
	return TrackedExchangeMenuWidget != nullptr && TrackedExchangeMenuWidget->IsInViewport();
}

FString UProjectInnerDoctrineMenuSubsystem::GetTrackedExchangeMenuWidgetClassName() const
{
	return TrackedExchangeMenuWidget ? GetNameSafe(TrackedExchangeMenuWidget->GetClass()) : FString();
}

int32 UProjectInnerDoctrineMenuSubsystem::GetTrackedExchangeMenuSelectedIndex() const
{
	return TrackedExchangeMenuWidget ? TrackedExchangeMenuWidget->GetSelectedIndex() : INDEX_NONE;
}

FProjectInnerDoctrineSnapshot UProjectInnerDoctrineMenuSubsystem::GetTrackedExchangeMenuSnapshot() const
{
	return TrackedExchangeMenuWidget ? TrackedExchangeMenuWidget->GetCachedSnapshot() : FProjectInnerDoctrineSnapshot();
}

void UProjectInnerDoctrineMenuSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (bMenuOpen)
	{
		CloseMenu();
	}

	TrackedPlayerPawn = NewPawn;
	TrackedInnerDoctrineComponent = nullptr;
	BindToTrackedComponent();
	EnsureInnerDoctrineComponent(NewPawn);
}

void UProjectInnerDoctrineMenuSubsystem::HandleDxpChanged(int32 OldCurrentRunDxp, int32 NewCurrentRunDxp, int32 OldMetaBankDxp, int32 NewMetaBankDxp)
{
	RefreshMenuDisplay();
}

void UProjectInnerDoctrineMenuSubsystem::HandleAttributeLevelChanged(EProjectDoctrineAttribute Attribute, int32 OldLevel, int32 NewLevel, int32 NextLevelCost)
{
	RefreshMenuDisplay();
}

void UProjectInnerDoctrineMenuSubsystem::HandleMilestoneTriggered(FName AbilityId, EProjectDoctrineAttribute Attribute, int32 Level)
{
	RefreshMenuDisplay();
}

void UProjectInnerDoctrineMenuSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	AttachToPlayerController(PlayerController);
	EnsureInnerDoctrineComponent(PlayerController->GetPawn());
	BindToTrackedComponent();
}

void UProjectInnerDoctrineMenuSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		if (TrackedPlayerPawn != PlayerController->GetPawn())
		{
			HandleTrackedPawnChanged(TrackedPlayerPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController(true);

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
}

void UProjectInnerDoctrineMenuSubsystem::DetachFromTrackedPlayerController(const bool bRemoveWidget)
{
	if (bMenuOpen)
	{
		RestoreMenuInputCapture();
		bMenuOpen = false;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	UnbindFromTrackedComponent();

	if (bRemoveWidget && TrackedExchangeMenuWidget)
	{
		TrackedExchangeMenuWidget->RemoveFromParent();
		TrackedExchangeMenuWidget = nullptr;
	}

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	MenuStateSnapshot = FProjectDoctrineExchangeMenuStateSnapshot();
}

void UProjectInnerDoctrineMenuSubsystem::EnsureInnerDoctrineComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedPlayerPawn = nullptr;
		TrackedInnerDoctrineComponent = nullptr;
		BindToTrackedComponent();
		return;
	}

	TrackedPlayerPawn = Pawn;
	UProjectInnerDoctrineComponent* Component = Pawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
	if (!Component)
	{
		Component = NewObject<UProjectInnerDoctrineComponent>(Pawn, UProjectInnerDoctrineComponent::StaticClass(), TEXT("ProjectInnerDoctrineComponent"));
		if (Component)
		{
			Pawn->AddInstanceComponent(Component);
			Component->OnComponentCreated();
			Component->RegisterComponent();
			Component->Activate(true);
		}
	}

	TrackedInnerDoctrineComponent = Component;
	BindToTrackedComponent();
}

void UProjectInnerDoctrineMenuSubsystem::EnsureExchangeMenuWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedExchangeMenuWidget)
	{
		return;
	}

	const TSubclassOf<UProjectInnerDoctrineExchangeMenuWidget> ExchangeMenuWidgetClass = ResolveExchangeMenuWidgetClass();
	TrackedExchangeMenuWidget = CreateWidget<UProjectInnerDoctrineExchangeMenuWidget>(
		PlayerController,
		ExchangeMenuWidgetClass);
	if (!TrackedExchangeMenuWidget)
	{
		UE_LOG(LogProjectInnerDoctrineMenu, Warning, TEXT("[ProjectInnerDoctrineMenu] Failed to create exchange widget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	TrackedExchangeMenuWidget->OnPurchaseRequested.AddUObject(this, &ThisClass::HandlePurchaseRequested);
	TrackedExchangeMenuWidget->OnWithdrawRequested.AddUObject(this, &ThisClass::HandleWithdrawRequested);
	TrackedExchangeMenuWidget->OnCloseRequested.AddUObject(this, &ThisClass::HandleCloseRequested);
}

TSubclassOf<UProjectInnerDoctrineExchangeMenuWidget> UProjectInnerDoctrineMenuSubsystem::ResolveExchangeMenuWidgetClass() const
{
	FSoftClassPath ConfiguredWidgetClass;
	if (const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get())
	{
		ConfiguredWidgetClass = UISettings->InnerDoctrineExchangeMenuWidgetClass;
	}

	if (UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClassWithPriority(
		ConfiguredWidgetClass,
		UProjectInnerDoctrineExchangeMenuWidget::StaticClass(),
		TEXT("ProjectInnerDoctrineAltarExchangeMenu"),
		TEXT("InnerDoctrineAltar")))
	{
		if (ResolvedClass->IsChildOf(UProjectInnerDoctrineExchangeMenuWidget::StaticClass()))
		{
			return ResolvedClass;
		}
	}

	return UProjectInnerDoctrineExchangeMenuWidget::StaticClass();
}

void UProjectInnerDoctrineMenuSubsystem::BindToTrackedComponent()
{
	if (BoundInnerDoctrineComponent == TrackedInnerDoctrineComponent)
	{
		return;
	}

	UnbindFromTrackedComponent();

	BoundInnerDoctrineComponent = TrackedInnerDoctrineComponent;
	if (!BoundInnerDoctrineComponent)
	{
		return;
	}

	BoundInnerDoctrineComponent->OnDxpChanged.AddUniqueDynamic(this, &ThisClass::HandleDxpChanged);
	BoundInnerDoctrineComponent->OnAttributeLevelChanged.AddUniqueDynamic(this, &ThisClass::HandleAttributeLevelChanged);
	BoundInnerDoctrineComponent->OnMilestoneTriggered.AddUniqueDynamic(this, &ThisClass::HandleMilestoneTriggered);

	if (TrackedExchangeMenuWidget)
	{
		TrackedExchangeMenuWidget->SetInnerDoctrineComponent(BoundInnerDoctrineComponent);
	}
}

void UProjectInnerDoctrineMenuSubsystem::UnbindFromTrackedComponent()
{
	if (!BoundInnerDoctrineComponent)
	{
		return;
	}

	BoundInnerDoctrineComponent->OnDxpChanged.RemoveDynamic(this, &ThisClass::HandleDxpChanged);
	BoundInnerDoctrineComponent->OnAttributeLevelChanged.RemoveDynamic(this, &ThisClass::HandleAttributeLevelChanged);
	BoundInnerDoctrineComponent->OnMilestoneTriggered.RemoveDynamic(this, &ThisClass::HandleMilestoneTriggered);
	BoundInnerDoctrineComponent = nullptr;
}

void UProjectInnerDoctrineMenuSubsystem::ApplyMenuInputCapture()
{
	MenuStateSnapshot = FProjectDoctrineExchangeMenuStateSnapshot();
	ApplyMenuHudSuppression();

	if (TrackedPlayerController)
	{
		MenuStateSnapshot.bHasSavedControllerState = true;
		MenuStateSnapshot.bWasMoveInputIgnored = TrackedPlayerController->IsMoveInputIgnored();
		MenuStateSnapshot.bWasLookInputIgnored = TrackedPlayerController->IsLookInputIgnored();

		TrackedPlayerController->FlushPressedKeys();

		if (!MenuStateSnapshot.bWasMoveInputIgnored)
		{
			TrackedPlayerController->SetIgnoreMoveInput(true);
			MenuStateSnapshot.bAppliedMoveInputIgnore = true;
		}

		if (!MenuStateSnapshot.bWasLookInputIgnored)
		{
			TrackedPlayerController->SetIgnoreLookInput(true);
			MenuStateSnapshot.bAppliedLookInputIgnore = true;
		}

		TrackedPlayerController->DisableInput(TrackedPlayerController);
		TrackedPlayerController->bShowMouseCursor = false;
		TrackedPlayerController->bEnableClickEvents = false;
		TrackedPlayerController->bEnableMouseOverEvents = false;

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(TrackedExchangeMenuWidget ? TrackedExchangeMenuWidget->TakeWidget() : TSharedPtr<SWidget>());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		TrackedPlayerController->SetInputMode(InputMode);
	}

	if (TrackedPlayerPawn)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			Character->StopJumping();
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				MenuStateSnapshot.bHasSavedMovementState = true;
				MenuStateSnapshot.PreviousMovementMode = CharacterMovement->MovementMode;
				MenuStateSnapshot.PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
				CharacterMovement->StopMovementImmediately();
				CharacterMovement->DisableMovement();
			}
		}

		if (TrackedPlayerController)
		{
			TrackedPlayerPawn->DisableInput(TrackedPlayerController);
			MenuStateSnapshot.bPawnInputSuspended = true;
		}
	}
}

void UProjectInnerDoctrineMenuSubsystem::RestoreMenuInputCapture()
{
	RestoreMenuHudSuppression();

	if (TrackedPlayerPawn && MenuStateSnapshot.bPawnInputSuspended)
	{
		if (APlayerController* OwningController = Cast<APlayerController>(TrackedPlayerPawn->GetController()))
		{
			TrackedPlayerPawn->EnableInput(OwningController);
		}
		else if (TrackedPlayerController)
		{
			TrackedPlayerPawn->EnableInput(TrackedPlayerController);
		}
	}

	if (TrackedPlayerPawn && MenuStateSnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->SetMovementMode(MenuStateSnapshot.PreviousMovementMode, MenuStateSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (TrackedPlayerController && MenuStateSnapshot.bHasSavedControllerState)
	{
		TrackedPlayerController->EnableInput(TrackedPlayerController);

		if (MenuStateSnapshot.bAppliedMoveInputIgnore)
		{
			TrackedPlayerController->SetIgnoreMoveInput(false);
		}

		if (MenuStateSnapshot.bAppliedLookInputIgnore)
		{
			TrackedPlayerController->SetIgnoreLookInput(false);
		}

		TrackedPlayerController->bShowMouseCursor = false;
		TrackedPlayerController->bEnableClickEvents = false;
		TrackedPlayerController->bEnableMouseOverEvents = false;

		FInputModeGameOnly InputMode;
		TrackedPlayerController->SetInputMode(InputMode);
		TrackedPlayerController->FlushPressedKeys();
	}

	MenuStateSnapshot = FProjectDoctrineExchangeMenuStateSnapshot();
}

void UProjectInnerDoctrineMenuSubsystem::ApplyMenuHudSuppression()
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			MenuStateSnapshot.bHasSavedProjectHudVisibility = true;
			MenuStateSnapshot.bWasProjectHudVisible = NeedsSubsystem->IsNeedsHudVisible();
			NeedsSubsystem->SetNeedsHudVisible(false);
		}
	}

	if (!TrackedPlayerController)
	{
		return;
	}

	if (AHUD* HudActor = TrackedPlayerController->GetHUD())
	{
		MenuStateSnapshot.bHasSavedPlayerHudVisibility = true;
		MenuStateSnapshot.bWasPlayerHudVisible = HudActor->bShowHUD;
		HudActor->bShowHUD = false;
		MenuStateSnapshot.bAppliedAcfHudDisable = TrySetReflectedHudEnabled(HudActor, false);
	}
}

void UProjectInnerDoctrineMenuSubsystem::RestoreMenuHudSuppression()
{
	if (TrackedPlayerController && MenuStateSnapshot.bHasSavedPlayerHudVisibility)
	{
		if (AHUD* HudActor = TrackedPlayerController->GetHUD())
		{
			HudActor->bShowHUD = MenuStateSnapshot.bWasPlayerHudVisible;

			if (MenuStateSnapshot.bAppliedAcfHudDisable)
			{
				TrySetReflectedHudEnabled(HudActor, MenuStateSnapshot.bWasPlayerHudVisible);
			}
		}
	}

	if (MenuStateSnapshot.bHasSavedProjectHudVisibility)
	{
		if (UWorld* World = GetWorld())
		{
			if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
			{
				NeedsSubsystem->SetNeedsHudVisible(MenuStateSnapshot.bWasProjectHudVisible);
			}
		}
	}
}

bool UProjectInnerDoctrineMenuSubsystem::TrySetReflectedHudEnabled(AHUD* HudActor, const bool bEnabled) const
{
	if (!HudActor)
	{
		return false;
	}

	UFunction* SetHudEnabledFunction = HudActor->FindFunction(TEXT("SetHudEnabled"));
	if (!SetHudEnabledFunction)
	{
		return false;
	}

	ProjectInnerDoctrineMenuSubsystemPrivate::FSetHudEnabledParams Parameters;
	Parameters.bEnabled = bEnabled;
	HudActor->ProcessEvent(SetHudEnabledFunction, &Parameters);
	return true;
}

void UProjectInnerDoctrineMenuSubsystem::RefreshMenuFocusNextTick()
{
	if (!TrackedExchangeMenuWidget)
	{
		return;
	}

	TrackedExchangeMenuWidget->FocusMenuWidget();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (bMenuOpen && TrackedExchangeMenuWidget)
			{
				TrackedExchangeMenuWidget->FocusMenuWidget();
			}
		}));
	}
}

void UProjectInnerDoctrineMenuSubsystem::RefreshMenuDisplay()
{
	if (!TrackedExchangeMenuWidget)
	{
		return;
	}

	TrackedExchangeMenuWidget->SetInnerDoctrineComponent(TrackedInnerDoctrineComponent);
	TrackedExchangeMenuWidget->RefreshDisplay();
}

bool UProjectInnerDoctrineMenuSubsystem::ResolveLocalInteractionContext(AActor* InteractingActor)
{
	if (!InteractingActor)
	{
		return TrackedPlayerController != nullptr && TrackedPlayerPawn != nullptr;
	}

	APawn* CandidatePawn = Cast<APawn>(InteractingActor);
	if (!CandidatePawn)
	{
		if (const AController* CandidateController = Cast<AController>(InteractingActor))
		{
			CandidatePawn = CandidateController->GetPawn();
		}
	}

	if (!CandidatePawn)
	{
		return TrackedPlayerController != nullptr && TrackedPlayerPawn != nullptr;
	}

	if (CandidatePawn == TrackedPlayerPawn)
	{
		return true;
	}

	if (APlayerController* CandidatePlayerController = Cast<APlayerController>(CandidatePawn->GetController()))
	{
		if (CandidatePlayerController->IsLocalController())
		{
			AttachToPlayerController(CandidatePlayerController);
			EnsureInnerDoctrineComponent(CandidatePawn);
			return true;
		}
	}

	return false;
}

bool UProjectInnerDoctrineMenuSubsystem::IsGamePauseBlockingMenu() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld() && UGameplayStatics::IsGamePaused(World);
}

void UProjectInnerDoctrineMenuSubsystem::HandlePurchaseRequested(const EProjectDoctrineAttribute Attribute)
{
	if (!TrackedInnerDoctrineComponent)
	{
		return;
	}

	TrackedInnerDoctrineComponent->SpendDxpOnAttribute(Attribute);
	RefreshMenuDisplay();
}

void UProjectInnerDoctrineMenuSubsystem::HandleWithdrawRequested()
{
	if (!TrackedInnerDoctrineComponent)
	{
		return;
	}

	TrackedInnerDoctrineComponent->WithdrawMetaDxp(TrackedInnerDoctrineComponent->GetMetaBankDxp());
	RefreshMenuDisplay();
}

void UProjectInnerDoctrineMenuSubsystem::HandleCloseRequested()
{
	CloseMenu();
}
