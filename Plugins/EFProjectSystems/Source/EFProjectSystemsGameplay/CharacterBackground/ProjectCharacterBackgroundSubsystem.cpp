#include "CharacterBackground/ProjectCharacterBackgroundSubsystem.h"

#include "CharacterBackground/ProjectCharacterBackgroundComponent.h"
#include "CharacterBackground/ProjectCharacterBackgroundSaveGame.h"
#include "CharacterBackground/ProjectCharacterBackgroundSettings.h"
#include "CharacterBackground/UI/ProjectCharacterBackgroundCreationWidget.h"
#include "Camera/CameraActor.h"
#include "EFCharacterCreationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCharacterBackgroundSubsystem, Log, All);

namespace ProjectCharacterBackgroundSubsystemPrivate
{
	struct FSetHudEnabledParams
	{
		bool bEnabled = false;
	};
}

void UProjectCharacterBackgroundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::HandlePostWorldInitialization);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
}

void UProjectCharacterBackgroundSubsystem::Deinitialize()
{
	RestorePhysicalCreatorCameraOverride();
	CloseStoryMenu(true);
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializationHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	OpenedWorldIds.Reset();
	Super::Deinitialize();
}

void UProjectCharacterBackgroundSubsystem::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	if (!IsValid(World) || !World->IsGameWorld() || !DoesWorldMatchStorySelectionMap(World))
	{
		return;
	}

	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
		this,
		&ThisClass::TryOpenForWorld,
		TWeakObjectPtr<UWorld>(World),
		0));
}

void UProjectCharacterBackgroundSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!IsValid(World))
	{
		return;
	}

	if (StoryWidget && StoryWidget->GetWorld() == World)
	{
		CloseStoryMenu(true);
	}

	if (TrackedPlayerController && TrackedPlayerController->GetWorld() == World)
	{
		RestorePhysicalCreatorCameraOverride();
	}

	OpenedWorldIds.Remove(World->GetUniqueID());
}

void UProjectCharacterBackgroundSubsystem::TryOpenForWorld(TWeakObjectPtr<UWorld> WorldPtr, const int32 AttemptIndex)
{
	if (!WorldPtr.IsValid())
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	if (!DoesWorldMatchStorySelectionMap(World))
	{
		return;
	}

	if (OpenedWorldIds.Contains(World->GetUniqueID()))
	{
		return;
	}

	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController() || !IsValid(Pawn))
	{
		if (AttemptIndex < (Settings ? Settings->MaxAutoOpenAttempts : 120))
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
				this,
				&ThisClass::TryOpenForWorld,
				WorldPtr,
				AttemptIndex + 1));
		}
		else
		{
			UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Failed to resolve local PlayerController + Pawn for StorySelection."));
		}
		return;
	}

	if (OpenStoryMenu(PlayerController, Pawn))
	{
		OpenedWorldIds.Add(World->GetUniqueID());
	}
}

bool UProjectCharacterBackgroundSubsystem::DoesWorldMatchStorySelectionMap(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}

	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	const FString ShortMapName = NormalizeMapName(World->GetPackage()->GetName());
	const TArray<FString> CandidateMaps = Settings ? Settings->StorySelectionMapNames : TArray<FString>{ TEXT("StorySelection") };
	for (const FString& CandidateMap : CandidateMaps)
	{
		if (ShortMapName.Equals(CandidateMap, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

FString UProjectCharacterBackgroundSubsystem::NormalizeMapName(const FString& PackageName) const
{
	FString ShortMapName = FPackageName::GetShortName(PackageName);
	if (ShortMapName.StartsWith(TEXT("UEDPIE_")))
	{
		TArray<FString> Parts;
		ShortMapName.ParseIntoArray(Parts, TEXT("_"), true);
		if (Parts.Num() >= 3)
		{
			Parts.RemoveAt(0, 2);
			ShortMapName = FString::Join(Parts, TEXT("_"));
		}
	}

	return ShortMapName;
}

bool UProjectCharacterBackgroundSubsystem::OpenStoryMenu(APlayerController* PlayerController, APawn* Pawn)
{
	if (!IsValid(PlayerController) || !IsValid(Pawn) || bStoryMenuOpen)
	{
		return false;
	}

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = Pawn;
	TrackedBackgroundComponent = EnsureBackgroundComponent(Pawn);
	TrackedInnerDoctrineComponent = EnsureInnerDoctrineComponent(Pawn);
	if (!TrackedBackgroundComponent || !TrackedInnerDoctrineComponent)
	{
		return false;
	}

	bLoadedProfileConfirmed = false;
	LoadedProfileBackstoryID = NAME_None;
	LoadedProfileProfessionID = NAME_None;
	LoadedProfileRevision = 0;

	if (const UProjectCharacterBackgroundSaveGame* SaveGame = LoadProfileSave())
	{
		bLoadedProfileConfirmed = SaveGame->bHasConfirmedProfile;
		LoadedProfileBackstoryID = SaveGame->SelectedBackstoryID;
		LoadedProfileProfessionID = SaveGame->SelectedProfessionID;
		LoadedProfileRevision = FMath::Max(0, SaveGame->ProfileRevision);
		if (bLoadedProfileConfirmed)
		{
			const bool bBackstoryResolved = TrackedBackgroundComponent->SetBackstory(LoadedProfileBackstoryID);
			const bool bProfessionResolved = TrackedBackgroundComponent->SetProfession(LoadedProfileProfessionID);
			if (!bBackstoryResolved || !bProfessionResolved)
			{
				bLoadedProfileConfirmed = false;
				TrackedBackgroundComponent->ClearBackground();
			}
			TrackedBackgroundComponent->SetProfileRevision(LoadedProfileRevision);
		}
	}

	const TSubclassOf<UProjectCharacterBackgroundCreationWidget> WidgetClass = ResolveCreationWidgetClass();
	StoryWidget = CreateWidget<UProjectCharacterBackgroundCreationWidget>(
		PlayerController,
		WidgetClass,
		TEXT("ProjectCharacterBackgroundCreationWidget"));
	if (!StoryWidget)
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Failed to create story-selection widget."));
		return false;
	}

	StoryWidget->InitializeForBackground(
		TrackedBackgroundComponent,
		bLoadedProfileConfirmed,
		LoadedProfileBackstoryID,
		LoadedProfileProfessionID,
		false);
	StoryWidget->OnConfirmRequested.AddUObject(this, &ThisClass::HandleStoryConfirmed);

	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	const int32 ZOrder = Settings ? Settings->CreationWidgetZOrder : 995;
	if (!StoryWidget->AddToPlayerScreen(ZOrder))
	{
		StoryWidget->AddToViewport(ZOrder);
	}

	ApplyStoryInputCapture();
	bStoryMenuOpen = true;
	StoryWidget->RefreshDisplay();
	StoryWidget->FocusCreationWidget();
	return true;
}

#if WITH_EDITOR
bool UProjectCharacterBackgroundSubsystem::ConfirmFirstAvailableProfileForAutomation()
{
	UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : GetWorld();
	if (!IsValid(World) || !World->IsGameWorld())
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Automation confirmation failed: no valid game world."));
		return false;
	}

	APlayerController* PlayerController = TrackedPlayerController;
	if (!IsValid(PlayerController))
	{
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}

	APawn* Pawn = TrackedPlayerPawn;
	if (!IsValid(Pawn) && IsValid(PlayerController))
	{
		Pawn = PlayerController->GetPawn();
	}

	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Automation confirmation failed: missing PlayerController or Pawn."));
		return false;
	}

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = Pawn;
	TrackedBackgroundComponent = EnsureBackgroundComponent(Pawn);
	TrackedInnerDoctrineComponent = EnsureInnerDoctrineComponent(Pawn);
	if (!TrackedBackgroundComponent || !TrackedInnerDoctrineComponent)
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Automation confirmation failed: missing background or Inner Doctrine component."));
		return false;
	}

	const TArray<FProjectCharacterBackstoryData> Backstories = TrackedBackgroundComponent->GetAvailableBackstories();
	const TArray<FProjectCharacterProfessionData> Professions = TrackedBackgroundComponent->GetAvailableProfessions();
	if (Backstories.IsEmpty() || Professions.IsEmpty())
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Automation confirmation failed: no backstory/profession rows are available."));
		return false;
	}

	const FName BackstoryID = Backstories[0].BackstoryID;
	const FName ProfessionID = Professions[0].ProfessionID;
	if (BackstoryID.IsNone() || ProfessionID.IsNone())
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Automation confirmation failed: first backstory/profession row has no ID."));
		return false;
	}

	UE_LOG(LogProjectCharacterBackgroundSubsystem, Log, TEXT("[CharacterBackground] Automation confirming Backstory=%s Profession=%s."), *BackstoryID.ToString(), *ProfessionID.ToString());
	HandleStoryConfirmed(BackstoryID, ProfessionID, bLoadedProfileConfirmed);
	return true;
}

bool UProjectCharacterBackgroundSubsystem::IsPhysicalCreatorActiveForAutomation() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>()
		: nullptr;
	return CharacterCreationSubsystem && CharacterCreationSubsystem->IsCharacterCreationActive();
}

bool UProjectCharacterBackgroundSubsystem::ApplyPhysicalCreatorPreviewCameraAutomationInputForAutomation(
	const FVector2D OrbitDelta,
	const float WheelDelta)
{
	UGameInstance* GameInstance = GetGameInstance();
	UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>()
		: nullptr;
	if (!CharacterCreationSubsystem || !CharacterCreationSubsystem->IsCharacterCreationActive())
	{
		return false;
	}

	const ACameraActor* PreviewCameraBefore = FindOwnedPhysicalCreatorPreviewCamera();
	const FVector BeforeLocation = PreviewCameraBefore ? PreviewCameraBefore->GetActorLocation() : FVector::ZeroVector;
	const FRotator BeforeRotation = PreviewCameraBefore ? PreviewCameraBefore->GetActorRotation() : FRotator::ZeroRotator;

	if (!OrbitDelta.IsNearlyZero())
	{
		CharacterCreationSubsystem->OrbitPreviewCamera(OrbitDelta);
	}

	if (!FMath::IsNearlyZero(WheelDelta))
	{
		CharacterCreationSubsystem->ZoomPreviewCamera(WheelDelta);
	}

	ForcePhysicalCreatorPreviewCameraViewTarget();

	const ACameraActor* PreviewCameraAfter = FindOwnedPhysicalCreatorPreviewCamera();
	if (!PreviewCameraBefore || !PreviewCameraAfter)
	{
		return PreviewCameraAfter != nullptr;
	}

	return !BeforeLocation.Equals(PreviewCameraAfter->GetActorLocation(), 0.1f)
		|| !BeforeRotation.Equals(PreviewCameraAfter->GetActorRotation(), 0.1f);
}
#endif

void UProjectCharacterBackgroundSubsystem::CloseStoryMenu(const bool bRestoreInput)
{
	if (StoryWidget)
	{
		StoryWidget->OnConfirmRequested.RemoveAll(this);
		StoryWidget->RemoveFromParent();
		StoryWidget = nullptr;
	}

	if (bStoryMenuOpen && bRestoreInput)
	{
		RestoreStoryInputCapture();
	}

	bStoryMenuOpen = false;
}

void UProjectCharacterBackgroundSubsystem::ApplyStoryInputCapture()
{
	InputSnapshot = FProjectCharacterBackgroundInputSnapshot();
	ApplyHudSuppression();

	UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : nullptr;
	if (World)
	{
		InputSnapshot.bHasSavedPauseState = true;
		InputSnapshot.bWasPaused = UGameplayStatics::IsGamePaused(World);
		if (!InputSnapshot.bWasPaused)
		{
			UGameplayStatics::SetGamePaused(World, true);
		}
	}

	if (TrackedPlayerController)
	{
		InputSnapshot.bHasSavedControllerState = true;
		InputSnapshot.bWasMouseCursorVisible = TrackedPlayerController->bShowMouseCursor;
		InputSnapshot.bWereClickEventsEnabled = TrackedPlayerController->bEnableClickEvents;
		InputSnapshot.bWereMouseOverEventsEnabled = TrackedPlayerController->bEnableMouseOverEvents;
		InputSnapshot.bWasMoveInputIgnored = TrackedPlayerController->IsMoveInputIgnored();
		InputSnapshot.bWasLookInputIgnored = TrackedPlayerController->IsLookInputIgnored();

		TrackedPlayerController->FlushPressedKeys();
		if (!InputSnapshot.bWasMoveInputIgnored)
		{
			TrackedPlayerController->SetIgnoreMoveInput(true);
			InputSnapshot.bAppliedMoveInputIgnore = true;
		}
		if (!InputSnapshot.bWasLookInputIgnored)
		{
			TrackedPlayerController->SetIgnoreLookInput(true);
			InputSnapshot.bAppliedLookInputIgnore = true;
		}

		TrackedPlayerController->DisableInput(TrackedPlayerController);
		TrackedPlayerController->bShowMouseCursor = true;
		TrackedPlayerController->bEnableClickEvents = true;
		TrackedPlayerController->bEnableMouseOverEvents = true;

		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(StoryWidget ? StoryWidget->TakeWidget() : TSharedPtr<SWidget>());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		TrackedPlayerController->SetInputMode(InputMode);
	}

	if (TrackedPlayerPawn)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			Character->StopJumping();
			if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
			{
				InputSnapshot.bHasSavedMovementState = true;
				InputSnapshot.PreviousMovementMode = MovementComponent->MovementMode;
				InputSnapshot.PreviousCustomMovementMode = MovementComponent->CustomMovementMode;
				MovementComponent->StopMovementImmediately();
				MovementComponent->DisableMovement();
			}
		}

		if (TrackedPlayerController)
		{
			TrackedPlayerPawn->DisableInput(TrackedPlayerController);
			InputSnapshot.bPawnInputSuspended = true;
		}
	}
}

void UProjectCharacterBackgroundSubsystem::RestoreStoryInputCapture()
{
	RestoreHudSuppression();

	if (TrackedPlayerPawn && InputSnapshot.bPawnInputSuspended)
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

	if (TrackedPlayerPawn && InputSnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
			{
				MovementComponent->SetMovementMode(InputSnapshot.PreviousMovementMode, InputSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (TrackedPlayerController && InputSnapshot.bHasSavedControllerState)
	{
		TrackedPlayerController->EnableInput(TrackedPlayerController);
		if (InputSnapshot.bAppliedMoveInputIgnore)
		{
			TrackedPlayerController->SetIgnoreMoveInput(false);
		}
		if (InputSnapshot.bAppliedLookInputIgnore)
		{
			TrackedPlayerController->SetIgnoreLookInput(false);
		}

		TrackedPlayerController->bShowMouseCursor = InputSnapshot.bWasMouseCursorVisible;
		TrackedPlayerController->bEnableClickEvents = InputSnapshot.bWereClickEventsEnabled;
		TrackedPlayerController->bEnableMouseOverEvents = InputSnapshot.bWereMouseOverEventsEnabled;
		FInputModeGameOnly InputMode;
		TrackedPlayerController->SetInputMode(InputMode);
		TrackedPlayerController->FlushPressedKeys();
	}

	if (InputSnapshot.bHasSavedPauseState)
	{
		if (UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : nullptr)
		{
			UGameplayStatics::SetGamePaused(World, InputSnapshot.bWasPaused);
		}
	}

	InputSnapshot = FProjectCharacterBackgroundInputSnapshot();
}

void UProjectCharacterBackgroundSubsystem::ApplyHudSuppression()
{
	if (UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : nullptr)
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			InputSnapshot.bHasSavedProjectHudVisibility = true;
			InputSnapshot.bWasProjectHudVisible = NeedsSubsystem->IsNeedsHudVisible();
			NeedsSubsystem->SetNeedsHudVisible(false);
		}
	}

	if (!TrackedPlayerController)
	{
		return;
	}

	if (AHUD* HudActor = TrackedPlayerController->GetHUD())
	{
		InputSnapshot.bHasSavedPlayerHudVisibility = true;
		InputSnapshot.bWasPlayerHudVisible = HudActor->bShowHUD;
		HudActor->bShowHUD = false;
		InputSnapshot.bAppliedAcfHudDisable = TrySetReflectedHudEnabled(HudActor, false);
	}
}

void UProjectCharacterBackgroundSubsystem::RestoreHudSuppression()
{
	if (TrackedPlayerController && InputSnapshot.bHasSavedPlayerHudVisibility)
	{
		if (AHUD* HudActor = TrackedPlayerController->GetHUD())
		{
			HudActor->bShowHUD = InputSnapshot.bWasPlayerHudVisible;
			if (InputSnapshot.bAppliedAcfHudDisable)
			{
				TrySetReflectedHudEnabled(HudActor, InputSnapshot.bWasPlayerHudVisible);
			}
		}
	}

	if (InputSnapshot.bHasSavedProjectHudVisibility)
	{
		if (UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : nullptr)
		{
			if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
			{
				NeedsSubsystem->SetNeedsHudVisible(InputSnapshot.bWasProjectHudVisible);
			}
		}
	}
}

bool UProjectCharacterBackgroundSubsystem::TrySetReflectedHudEnabled(AHUD* HudActor, const bool bEnabled) const
{
	if (!HudActor)
	{
		return false;
	}

	UFunction* Function = HudActor->FindFunction(TEXT("SetHudEnabled"));
	if (!Function)
	{
		return false;
	}

	ProjectCharacterBackgroundSubsystemPrivate::FSetHudEnabledParams Params;
	Params.bEnabled = bEnabled;
	HudActor->ProcessEvent(Function, &Params);
	return true;
}

UProjectCharacterBackgroundComponent* UProjectCharacterBackgroundSubsystem::EnsureBackgroundComponent(APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}

	UProjectCharacterBackgroundComponent* Component = Pawn->FindComponentByClass<UProjectCharacterBackgroundComponent>();
	if (!Component)
	{
		Component = NewObject<UProjectCharacterBackgroundComponent>(Pawn, UProjectCharacterBackgroundComponent::StaticClass(), TEXT("ProjectCharacterBackgroundComponent"));
		if (Component)
		{
			Pawn->AddInstanceComponent(Component);
			Component->OnComponentCreated();
			Component->RegisterComponent();
			Component->Activate(true);
		}
	}

	return Component;
}

UProjectInnerDoctrineComponent* UProjectCharacterBackgroundSubsystem::EnsureInnerDoctrineComponent(APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}

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

	return Component;
}

TSubclassOf<UProjectCharacterBackgroundCreationWidget> UProjectCharacterBackgroundSubsystem::ResolveCreationWidgetClass() const
{
	if (const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get())
	{
		if (UClass* LoadedClass = Settings->CreationWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	return UProjectCharacterBackgroundCreationWidget::StaticClass();
}

UProjectCharacterBackgroundSaveGame* UProjectCharacterBackgroundSubsystem::LoadProfileSave() const
{
	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : TEXT("ProjectCharacterBackground");
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return nullptr;
	}

	return Cast<UProjectCharacterBackgroundSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
}

bool UProjectCharacterBackgroundSubsystem::SaveProfile(
	const FName BackstoryID,
	const FName ProfessionID,
	const int32 ProfileRevision) const
{
	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	const FString SlotName = Settings ? Settings->SaveSlotName : TEXT("ProjectCharacterBackground");
	const int32 UserIndex = Settings ? Settings->SaveUserIndex : 0;

	UProjectCharacterBackgroundSaveGame* SaveGame = Cast<UProjectCharacterBackgroundSaveGame>(UGameplayStatics::CreateSaveGameObject(UProjectCharacterBackgroundSaveGame::StaticClass()));
	if (!SaveGame)
	{
		return false;
	}

	SaveGame->bHasConfirmedProfile = true;
	SaveGame->SelectedBackstoryID = BackstoryID;
	SaveGame->SelectedProfessionID = ProfessionID;
	SaveGame->ProfileRevision = FMath::Max(0, ProfileRevision);
	SaveGame->SavedAtUtc = FDateTime::UtcNow().ToIso8601();
	return UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, UserIndex);
}

void UProjectCharacterBackgroundSubsystem::QueuePhysicalCharacterCreatorLaunch()
{
	UWorld* World = TrackedPlayerController ? TrackedPlayerController->GetWorld() : nullptr;
	if (!IsValid(World))
	{
		LaunchPhysicalCharacterCreator();
		return;
	}

	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
		this,
		&ThisClass::LaunchPhysicalCharacterCreator));
}

void UProjectCharacterBackgroundSubsystem::LaunchPhysicalCharacterCreator()
{
	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	if (!Settings || !Settings->bAlwaysLaunchPhysicalCreatorAfterStory)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>()
		: nullptr;
	if (!CharacterCreationSubsystem || !TrackedPlayerController || !TrackedPlayerPawn)
	{
		return;
	}

	ApplyPhysicalCreatorCameraOverride();

	if (!CharacterCreationSubsystem->EnterCharacterCreationMode(TrackedPlayerController, TrackedPlayerPawn))
	{
		RestorePhysicalCreatorCameraOverride();
		return;
	}

	ApplyPhysicalCreatorCameraOverride();
	if (ACameraActor* PreviewCamera = FindOwnedPhysicalCreatorPreviewCamera())
	{
		if (TrackedPlayerController->GetViewTarget() != PreviewCamera)
		{
			FinalizePhysicalCreatorPreviewCameraForStorySelection();
		}
	}

	if (UWorld* World = TrackedPlayerController->GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this,
			&ThisClass::FinalizePhysicalCreatorPreviewCameraForStorySelection));
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this,
			&ThisClass::MonitorPhysicalCreatorCameraOverride));
	}
}

void UProjectCharacterBackgroundSubsystem::ApplyPhysicalCreatorCameraOverride()
{
	if (!IsValid(TrackedPlayerController))
	{
		return;
	}

	if (!bHasSavedPhysicalCreatorAutoManageCameraTarget)
	{
		bWasPhysicalCreatorAutoManageCameraTarget = TrackedPlayerController->bAutoManageActiveCameraTarget;
		bHasSavedPhysicalCreatorAutoManageCameraTarget = true;
	}

	TrackedPlayerController->bAutoManageActiveCameraTarget = false;
}

void UProjectCharacterBackgroundSubsystem::RestorePhysicalCreatorCameraOverride()
{
	if (TrackedPlayerController && bHasSavedPhysicalCreatorAutoManageCameraTarget)
	{
		TrackedPlayerController->bAutoManageActiveCameraTarget = bWasPhysicalCreatorAutoManageCameraTarget;
	}

	bHasSavedPhysicalCreatorAutoManageCameraTarget = false;
	bWasPhysicalCreatorAutoManageCameraTarget = true;
}

void UProjectCharacterBackgroundSubsystem::MonitorPhysicalCreatorCameraOverride()
{
	if (!IsValid(TrackedPlayerController))
	{
		RestorePhysicalCreatorCameraOverride();
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>()
		: nullptr;
	if (!CharacterCreationSubsystem || !CharacterCreationSubsystem->IsCharacterCreationActive())
	{
		RestorePhysicalCreatorCameraOverride();
		return;
	}

	FinalizePhysicalCreatorPreviewCameraForStorySelection();

	if (UWorld* World = TrackedPlayerController->GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this,
			&ThisClass::MonitorPhysicalCreatorCameraOverride));
	}
}

void UProjectCharacterBackgroundSubsystem::FinalizePhysicalCreatorPreviewCameraForStorySelection()
{
	ForcePhysicalCreatorPreviewCameraViewTarget();
}

void UProjectCharacterBackgroundSubsystem::ForcePhysicalCreatorPreviewCameraViewTarget()
{
	if (!IsValid(TrackedPlayerController))
	{
		return;
	}

	ACameraActor* PreviewCamera = FindOwnedPhysicalCreatorPreviewCamera();
	if (!IsValid(PreviewCamera))
	{
		return;
	}

	TrackedPlayerController->SetViewTarget(PreviewCamera);
	UE_LOG(
		LogProjectCharacterBackgroundSubsystem,
		Log,
		TEXT("[CharacterBackground] StorySelection physical creator view target set to %s."),
		*GetNameSafe(PreviewCamera));
}

ACameraActor* UProjectCharacterBackgroundSubsystem::FindOwnedPhysicalCreatorPreviewCamera() const
{
	if (!IsValid(TrackedPlayerController))
	{
		return nullptr;
	}

	UWorld* World = TrackedPlayerController->GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	TArray<AActor*> CameraActors;
	UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), CameraActors);
	for (AActor* CameraActor : CameraActors)
	{
		ACameraActor* PreviewCamera = Cast<ACameraActor>(CameraActor);
		if (IsValid(PreviewCamera) && PreviewCamera->GetOwner() == TrackedPlayerController)
		{
			return PreviewCamera;
		}
	}

	return nullptr;
}

void UProjectCharacterBackgroundSubsystem::HandleStoryConfirmed(
	const FName BackstoryID,
	const FName ProfessionID,
	const bool bChangingExistingProfile)
{
	if (!TrackedBackgroundComponent || !TrackedInnerDoctrineComponent)
	{
		CloseStoryMenu(true);
		return;
	}

	const bool bBackstoryResolved = TrackedBackgroundComponent->SetBackstory(BackstoryID);
	const bool bProfessionResolved = TrackedBackgroundComponent->SetProfession(ProfessionID);
	if (!bBackstoryResolved || !bProfessionResolved || !TrackedBackgroundComponent->IsSelectionValid())
	{
		UE_LOG(LogProjectCharacterBackgroundSubsystem, Warning, TEXT("[CharacterBackground] Confirmation rejected because the selected profile is invalid."));
		return;
	}

	const bool bSameConfirmedProfile = bLoadedProfileConfirmed
		&& BackstoryID == LoadedProfileBackstoryID
		&& ProfessionID == LoadedProfileProfessionID;
	const int32 NewRevision = bSameConfirmedProfile
		? LoadedProfileRevision
		: FMath::Max(LoadedProfileRevision + 1, 1);

	if (bChangingExistingProfile)
	{
		TrackedInnerDoctrineComponent->ResetRunProgressForBackgroundChange();
	}

	TrackedBackgroundComponent->SetProfileRevision(NewRevision);
	SaveProfile(BackstoryID, ProfessionID, NewRevision);
	TrackedBackgroundComponent->ApplyBackgroundToInnerDoctrine();

	CloseStoryMenu(true);
	QueuePhysicalCharacterCreatorLaunch();
}
