#include "Debug/ProjectGameplayDebugSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/ProjectAutomaticTattooTunerWidget.h"
#include "Debug/ProjectGameplayDebugCommandExecutor.h"
#include "Debug/ProjectGameplayDebugMenuWidget.h"
#include "EFCharacterCreationSettings.h"
#include "EFProjectDynamicThemeSubsystem.h"
#include "EFProjectUISettings.h"
#include "EFProjectInputSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "UI/ProjectWidgetClassResolver.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectGameplayDebug, Log, All);

#define LOCTEXT_NAMESPACE "ProjectGameplayDebugSubsystem"

namespace ProjectGameplayDebugSubsystemPrivate
{
	static constexpr int32 InputPriority = 90;
	static constexpr int32 MenuZOrder = 10060;
	static constexpr int32 AutomaticTattooTunerZOrder = 1000000;
	static constexpr float AutomaticTattooTunerPreviewDebounceSeconds = 0.05f;

	struct FSetHudEnabledParams
	{
		bool bEnabled = false;
	};

	const FName RootDebugOptionId(TEXT("Root.Debug"));
	const FName RootTestOptionId(TEXT("Root.Test"));
	const FName RootAppearanceOptionId(TEXT("Root.Appearance"));
	const FName RootDungeonHarnessOptionId(TEXT("Root.DungeonHarness"));
	const FName RootCancelOptionId(TEXT("Root.Cancel"));
	const FName BackOptionId(TEXT("Navigation.Back"));

	const FName DebugImmediateDefeatOptionId(TEXT("Debug.ImmediateDefeat"));
	const FName DebugDownedModeOptionId(TEXT("Debug.DownedMode"));
	const FName DebugRestoreAcfHealthOptionId(TEXT("Debug.RestoreAcfHealth"));
	const FName DebugRestoreNeedsSensationsOptionId(TEXT("Debug.RestoreNeedsSensations"));
	const FName DebugSetTo100OptionId(TEXT("Debug.SetTo100"));
	const FName DebugSetTo50OptionId(TEXT("Debug.SetTo50"));
	const FName DebugSetTo0OptionId(TEXT("Debug.SetTo0"));
	const FName DebugApplyStatusOptionId(TEXT("Debug.ApplyStatus"));
	const FName DebugAutomaticTattoosOptionId(TEXT("Debug.AutomaticTattoos"));

	const FName TestLevel5OptionId(TEXT("Test.Level5"));
	const FName TestLevel10OptionId(TEXT("Test.Level10"));
	const FName TestRuntimeFpsBenchmarkOptionId(TEXT("Test.RuntimeFpsBenchmark"));
	const FName TestFullStackOverloadBenchmarkOptionId(TEXT("Test.FullStackOverloadBenchmark"));
	const FName AppearanceHudThemeOptionId(TEXT("Appearance.HUDTheme"));
	const FName DungeonHarnessStatusOptionId(TEXT("DungeonHarness.Status"));
	const FName DungeonHarnessNextFloorOptionId(TEXT("DungeonHarness.NextFloor"));
	const FName DungeonHarnessReplayOptionId(TEXT("DungeonHarness.Replay"));
	const FName DungeonHarnessRerollOptionId(TEXT("DungeonHarness.Reroll"));
	const FName DungeonHarnessNewRunOptionId(TEXT("DungeonHarness.NewRandomRun"));
	const FName DungeonHarnessSeed42OptionId(TEXT("DungeonHarness.NewSeed42"));
	const FName DungeonHarnessJumpFloorOptionId(TEXT("DungeonHarness.JumpFloor"));
	const FName DungeonHarnessStyleOptionId(TEXT("DungeonHarness.Style"));
	const FName DungeonHarnessThemeOptionId(TEXT("DungeonHarness.Theme"));
	const FName DungeonHarnessScaleBiasOptionId(TEXT("DungeonHarness.ScaleBias"));
	const FName DungeonHarnessBranchingBiasOptionId(TEXT("DungeonHarness.BranchingBias"));
	const FName DungeonHarnessDangerBiasOptionId(TEXT("DungeonHarness.DangerBias"));
	const FName DungeonHarnessSafeBiasOptionId(TEXT("DungeonHarness.SafeBias"));
	const FName DungeonHarnessAbundanceBiasOptionId(TEXT("DungeonHarness.AbundanceBias"));
	const FName DungeonHarnessMysteryBiasOptionId(TEXT("DungeonHarness.MysteryBias"));
	const FName DungeonHarnessClothingBiasOptionId(TEXT("DungeonHarness.ClothingBias"));
	const FName DungeonHarnessVolatilityOptionId(TEXT("DungeonHarness.Volatility"));
	const FName DungeonHarnessClearIntentOptionId(TEXT("DungeonHarness.ClearIntent"));
	const FName ScaleBiasName(TEXT("Scale"));
	const FName BranchingBiasName(TEXT("Branching"));
	const FName DangerBiasName(TEXT("Danger"));
	const FName SafeBiasName(TEXT("Safe"));
	const FName AbundanceBiasName(TEXT("Abundance"));
	const FName MysteryBiasName(TEXT("Mystery"));
	const FName ClothingBiasName(TEXT("Clothing"));

	const FString DebugSetTo100Prefix(TEXT("Debug.SetTo100."));
	const FString DebugSetTo50Prefix(TEXT("Debug.SetTo50."));
	const FString DebugSetTo0Prefix(TEXT("Debug.SetTo0."));
	const FString DebugStatusPrefix(TEXT("Debug.Status."));
	const FString DebugAutomaticTattooRowPrefix(TEXT("Debug.AutomaticTattoos.Row."));
	const FString DebugAutomaticTattooActionPrefix(TEXT("Debug.AutomaticTattoos.Action."));
	const FString TestLevel5Prefix(TEXT("Test.Level5."));
	const FString TestLevel10Prefix(TEXT("Test.Level10."));
	const FString AppearanceHudThemePrefix(TEXT("Appearance.HUDTheme."));
	const FString DungeonHarnessJumpFloorPrefix(TEXT("DungeonHarness.JumpFloor."));
	const FString DungeonHarnessStylePrefix(TEXT("DungeonHarness.Style."));
	const FString DungeonHarnessThemePrefix(TEXT("DungeonHarness.Theme."));
	const FString DungeonHarnessScaleBiasPrefix(TEXT("DungeonHarness.ScaleBias."));
	const FString DungeonHarnessBranchingBiasPrefix(TEXT("DungeonHarness.BranchingBias."));
	const FString DungeonHarnessDangerBiasPrefix(TEXT("DungeonHarness.DangerBias."));
	const FString DungeonHarnessSafeBiasPrefix(TEXT("DungeonHarness.SafeBias."));
	const FString DungeonHarnessAbundanceBiasPrefix(TEXT("DungeonHarness.AbundanceBias."));
	const FString DungeonHarnessMysteryBiasPrefix(TEXT("DungeonHarness.MysteryBias."));
	const FString DungeonHarnessClothingBiasPrefix(TEXT("DungeonHarness.ClothingBias."));
	const FString DungeonHarnessVolatilityPrefix(TEXT("DungeonHarness.Volatility."));

	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName CurseName(TEXT("Curse"));
	const FName PainName(TEXT("Pain"));

	static FName MakeChildOptionId(const FString& Prefix, const FName Name)
	{
		return FName(*(Prefix + Name.ToString()));
	}

	static FName MakeAutomaticTattooRowOptionId(const FName RowName)
	{
		return MakeChildOptionId(DebugAutomaticTattooRowPrefix, RowName);
	}

	static FName MakeAutomaticTattooActionOptionId(const TCHAR* ActionName, const FName RowName)
	{
		return FName(*(DebugAutomaticTattooActionPrefix + ActionName + TEXT(".") + RowName.ToString()));
	}

	static bool IsAutomaticTattooRowNode(const FName NodeId)
	{
		return NodeId.ToString().StartsWith(DebugAutomaticTattooRowPrefix);
	}

	static FName ExtractAutomaticTattooRowNameFromNode(const FName NodeId)
	{
		const FString NodeString = NodeId.ToString();
		return NodeString.StartsWith(DebugAutomaticTattooRowPrefix)
			? FName(*NodeString.RightChop(DebugAutomaticTattooRowPrefix.Len()))
			: NAME_None;
	}

	static bool ParseAutomaticTattooAction(const FName OptionId, FString& OutActionName, FName& OutRowName)
	{
		const FString OptionString = OptionId.ToString();
		static const TCHAR* ActionNames[] = {
			TEXT("Force"),
			TEXT("Copy"),
			TEXT("Reset"),
			TEXT("XMinus"),
			TEXT("XPlus"),
			TEXT("YMinus"),
			TEXT("YPlus"),
			TEXT("SizeMinus"),
			TEXT("SizePlus"),
			TEXT("RotationMinus"),
			TEXT("RotationPlus"),
			TEXT("ProjectionMinus"),
			TEXT("ProjectionPlus")
		};

		for (const TCHAR* ActionName : ActionNames)
		{
			const FString Prefix = DebugAutomaticTattooActionPrefix + ActionName + TEXT(".");
			if (OptionString.StartsWith(Prefix))
			{
				OutActionName = ActionName;
				OutRowName = FName(*OptionString.RightChop(Prefix.Len()));
				return !OutRowName.IsNone();
			}
		}

		return false;
	}

	static FVector CalculateAutomaticTattooTunerFocusWorldLocation(const APawn* Pawn, const FCharacterCreationCameraSettings& CameraSettings)
	{
		if (!IsValid(Pawn))
		{
			return FVector::ZeroVector;
		}

		FBox Bounds(ForceInit);
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Pawn->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (IsValid(PrimitiveComponent) && PrimitiveComponent->IsRegistered())
			{
				Bounds += PrimitiveComponent->Bounds.GetBox();
			}
		}

		if (!Bounds.IsValid)
		{
			Bounds = Pawn->GetComponentsBoundingBox(true);
		}

		FVector FocusLocation = Pawn->GetActorLocation();
		if (Bounds.IsValid)
		{
			const float FocusHeightFactor = FMath::Clamp(CameraSettings.FocusHeightFactor * 0.9f, 0.0f, 1.0f);
			FocusLocation.Z = Bounds.Min.Z + Bounds.GetSize().Z * FocusHeightFactor + CameraSettings.HeightOffset;
		}
		else
		{
			FocusLocation.Z += CameraSettings.HeightOffset;
		}

		if (!FMath::IsNearlyZero(CameraSettings.HorizontalOffset))
		{
			FocusLocation += Pawn->GetActorRightVector() * CameraSettings.HorizontalOffset;
		}

		return FocusLocation;
	}

	static FRotator MakeAutomaticTattooTunerCameraRotation(const APawn* Pawn, const FCharacterCreationCameraSettings& CameraSettings)
	{
		const float PawnYaw = IsValid(Pawn) ? Pawn->GetActorRotation().Yaw : 0.0f;
		return FRotator(CameraSettings.PitchOffset, PawnYaw + 180.0f + CameraSettings.YawOffset, 0.0f);
	}

	static bool IsAutomaticTattooTunerCommand(const FName OptionId)
	{
		FString ActionName;
		FName RowName;
		return ParseAutomaticTattooAction(OptionId, ActionName, RowName);
	}

	static bool IsHudThemeCommand(const FName OptionId)
	{
		return OptionId.ToString().StartsWith(AppearanceHudThemePrefix);
	}

	static FKey ResolveToggleKey()
	{
		if (const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get())
		{
			return InputSettings->ToggleGameplayDebugMenuKey;
		}

		return EKeys::L;
	}
}

void UProjectGameplayDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectGameplayDebugSubsystem::Deinitialize()
{
	CloseAutomaticTattooTuner(false);
	DetachFromTrackedPlayerController();
	Super::Deinitialize();
}

void UProjectGameplayDebugSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;

#if !UE_BUILD_SHIPPING
	TryResolveRuntimeContext();
#endif
}

TStatId UProjectGameplayDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectGameplayDebugSubsystem, STATGROUP_Tickables);
}

bool UProjectGameplayDebugSubsystem::IsTickable() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

bool UProjectGameplayDebugSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
#if UE_BUILD_SHIPPING
	(void)WorldType;
	return false;
#else
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
#endif
}

void UProjectGameplayDebugSubsystem::RequestToggleGameplayDebugMenu()
{
#if !UE_BUILD_SHIPPING
	HandleToggleMenuPressed();
#endif
}

bool UProjectGameplayDebugSubsystem::IsGameplayDebugMenuOpen() const
{
	return bMenuOpen || bAutomaticTattooTunerOpen;
}

void UProjectGameplayDebugSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		DetachFromTrackedPlayerController();
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (PlayerController != TrackedPlayerController)
	{
		DetachFromTrackedPlayerController();
		AttachToPlayerController(PlayerController);
	}

	TrackedPlayerPawn = TrackedPlayerController ? TrackedPlayerController->GetPawn() : nullptr;
	if (!TrackedPlayerController && bMenuOpen)
	{
		CloseMenu();
	}
	if (!TrackedPlayerController && bAutomaticTattooTunerOpen)
	{
		CloseAutomaticTattooTuner(false);
	}
}

void UProjectGameplayDebugSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	BindInputToTrackedPlayerController();
}

void UProjectGameplayDebugSubsystem::DetachFromTrackedPlayerController()
{
	if (bAutomaticTattooTunerOpen || TrackedAutomaticTattooTunerWidget)
	{
		CloseAutomaticTattooTuner(false);
	}

	if (bMenuOpen || TrackedDebugMenuWidget)
	{
		CloseMenu();
	}

	UnbindInputFromTrackedPlayerController();
	TrackedDebugMenuWidget = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedPlayerController = nullptr;
	CachedVisibleMenuNodes.Reset();
	MenuNodeStack.Reset();
}

void UProjectGameplayDebugSubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || TrackedInputComponent)
	{
		return;
	}

	TrackedInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectGameplayDebugInputComponent"));
	if (!TrackedInputComponent)
	{
		UE_LOG(LogProjectGameplayDebug, Warning, TEXT("[GameplayDebug] Failed to create input component for %s"), *GetNameSafe(TrackedPlayerController));
		return;
	}

	TrackedInputComponent->bBlockInput = false;
	TrackedInputComponent->Priority = ProjectGameplayDebugSubsystemPrivate::InputPriority;
	TrackedInputComponent->RegisterComponent();
	TrackedInputComponent->BindKey(ProjectGameplayDebugSubsystemPrivate::ResolveToggleKey(), IE_Pressed, this, &ThisClass::HandleToggleMenuPressed);
	TrackedPlayerController->PushInputComponent(TrackedInputComponent);
}

void UProjectGameplayDebugSubsystem::UnbindInputFromTrackedPlayerController()
{
	if (!TrackedInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(TrackedInputComponent);
	}

	if (TrackedInputComponent->IsRegistered())
	{
		TrackedInputComponent->DestroyComponent();
	}

	TrackedInputComponent = nullptr;
}

void UProjectGameplayDebugSubsystem::EnsureDebugMenuWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedDebugMenuWidget)
	{
		return;
	}

	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	const TSubclassOf<UProjectGameplayDebugMenuWidget> MenuWidgetClass =
		ProjectWidgetClassResolver::ResolveWidgetClass<UProjectGameplayDebugMenuWidget>(
			UISettings ? UISettings->GameplayDebugMenuWidgetClass : FSoftClassPath(),
			TEXT("ProjectGameplayDebugMenu"));

	TrackedDebugMenuWidget = CreateWidget<UProjectGameplayDebugMenuWidget>(PlayerController, MenuWidgetClass, TEXT("ProjectGameplayDebugMenuWidget"));
	if (!TrackedDebugMenuWidget)
	{
		UE_LOG(LogProjectGameplayDebug, Warning, TEXT("[GameplayDebug] Failed to create debug menu widget for %s"), *GetNameSafe(PlayerController));
		return;
	}

	TrackedDebugMenuWidget->SetAlternateCancelKey(ProjectGameplayDebugSubsystemPrivate::ResolveToggleKey());
	TrackedDebugMenuWidget->OnOptionConfirmed.AddUObject(this, &ThisClass::HandleMenuOptionConfirmed);
	TrackedDebugMenuWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleMenuCancelRequested);
	TrackedDebugMenuWidget->OnBackRequested.AddUObject(this, &ThisClass::HandleMenuBackRequested);
}

void UProjectGameplayDebugSubsystem::OpenMenu()
{
	if (bMenuOpen || !TrackedPlayerController || !TrackedPlayerPawn)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (const UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
		{
			if (EmoteSubsystem->IsEmoteMenuOpen())
			{
				return;
			}
		}
	}

	EnsureDebugMenuWidget(TrackedPlayerController);
	if (!TrackedDebugMenuWidget)
	{
		return;
	}

	if (!TrackedDebugMenuWidget->IsInViewport())
	{
		if (!TrackedDebugMenuWidget->AddToPlayerScreen(ProjectGameplayDebugSubsystemPrivate::MenuZOrder))
		{
			TrackedDebugMenuWidget->AddToViewport(ProjectGameplayDebugSubsystemPrivate::MenuZOrder);
		}
	}

	MenuNodeStack.Reset();
	RefreshCurrentMenuModel(ProjectGameplayDebugSubsystemPrivate::RootDebugOptionId);
	ApplyMenuInputCapture();
	bMenuOpen = true;
	RefreshMenuFocusNextTick();
}

void UProjectGameplayDebugSubsystem::CloseMenu()
{
	if (!bMenuOpen && !TrackedDebugMenuWidget)
	{
		return;
	}

	if (TrackedDebugMenuWidget)
	{
		TrackedDebugMenuWidget->RemoveFromParent();
	}

	RestoreMenuInputCapture();
	CachedVisibleMenuNodes.Reset();
	MenuNodeStack.Reset();
	bMenuOpen = false;
}

void UProjectGameplayDebugSubsystem::ApplyMenuInputCapture()
{
	MenuStateSnapshot = FProjectGameplayDebugMenuStateSnapshot();

	if (TrackedPlayerController)
	{
		MenuStateSnapshot.bHasSavedControllerState = true;
		MenuStateSnapshot.bWasMoveInputIgnored = TrackedPlayerController->IsMoveInputIgnored();
		MenuStateSnapshot.bWasLookInputIgnored = TrackedPlayerController->IsLookInputIgnored();
		MenuStateSnapshot.bWasMouseCursorVisible = TrackedPlayerController->bShowMouseCursor;
		MenuStateSnapshot.bWereClickEventsEnabled = TrackedPlayerController->bEnableClickEvents;
		MenuStateSnapshot.bWereMouseOverEventsEnabled = TrackedPlayerController->bEnableMouseOverEvents;

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
		InputMode.SetWidgetToFocus(TrackedDebugMenuWidget ? TrackedDebugMenuWidget->TakeWidget() : TSharedPtr<SWidget>());
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

void UProjectGameplayDebugSubsystem::RestoreMenuInputCapture()
{
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

		TrackedPlayerController->bShowMouseCursor = MenuStateSnapshot.bWasMouseCursorVisible;
		TrackedPlayerController->bEnableClickEvents = MenuStateSnapshot.bWereClickEventsEnabled;
		TrackedPlayerController->bEnableMouseOverEvents = MenuStateSnapshot.bWereMouseOverEventsEnabled;

		FInputModeGameOnly InputMode;
		TrackedPlayerController->SetInputMode(InputMode);
		TrackedPlayerController->FlushPressedKeys();
	}

	MenuStateSnapshot = FProjectGameplayDebugMenuStateSnapshot();
}

void UProjectGameplayDebugSubsystem::RefreshMenuFocusNextTick()
{
	if (!TrackedDebugMenuWidget)
	{
		return;
	}

	TrackedDebugMenuWidget->FocusInitialOption();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bMenuOpen || !TrackedDebugMenuWidget)
			{
				return;
			}

			TrackedDebugMenuWidget->FocusInitialOption();
		}));
	}
}

void UProjectGameplayDebugSubsystem::RefreshCurrentMenuModel(const FName PreferredOptionId)
{
	if (!TrackedDebugMenuWidget)
	{
		return;
	}

	CachedVisibleMenuNodes.Reset();

	TArray<FProjectEmoteMenuOption> MenuOptions;
	BuildMenuNodeOptions(MenuOptions);
	TrackedDebugMenuWidget->SetMenuContent(ResolveCurrentMenuTitle(), ResolveCurrentMenuHint(), MenuOptions, ResolveCurrentMenuVisualMode());

	if (PreferredOptionId != NAME_None)
	{
		for (int32 OptionIndex = 0; OptionIndex < MenuOptions.Num(); ++OptionIndex)
		{
			if (MenuOptions[OptionIndex].OptionId == PreferredOptionId)
			{
				TrackedDebugMenuWidget->SelectOptionByIndex(OptionIndex);
				TrackedDebugMenuWidget->FocusInitialOption();
				return;
			}
		}
	}

	TrackedDebugMenuWidget->FocusInitialOption();
}

FName UProjectGameplayDebugSubsystem::GetCurrentMenuParentNodeId() const
{
	return MenuNodeStack.IsEmpty() ? NAME_None : MenuNodeStack.Last();
}

bool UProjectGameplayDebugSubsystem::IsAtMenuRoot() const
{
	return MenuNodeStack.IsEmpty();
}

FText UProjectGameplayDebugSubsystem::ResolveCurrentMenuTitle() const
{
	using namespace ProjectGameplayDebugSubsystemPrivate;

	const FName CurrentNodeId = GetCurrentMenuParentNodeId();
	if (CurrentNodeId == RootDebugOptionId)
	{
		return LOCTEXT("DebugTitle", "Debug");
	}
	if (CurrentNodeId == RootTestOptionId)
	{
		return LOCTEXT("TestTitle", "Test");
	}
	if (CurrentNodeId == RootAppearanceOptionId)
	{
		return LOCTEXT("AppearanceTitle", "Appearance");
	}
	if (CurrentNodeId == RootDungeonHarnessOptionId)
	{
		return LOCTEXT("DungeonHarnessTitle", "Dungeon Harness");
	}
	if (CurrentNodeId == AppearanceHudThemeOptionId)
	{
		return LOCTEXT("HudThemeTitle", "HUD Theme");
	}
	if (CurrentNodeId == DungeonHarnessJumpFloorOptionId)
	{
		return LOCTEXT("DungeonHarnessJumpFloorTitle", "Jump Floor");
	}
	if (CurrentNodeId == DungeonHarnessStyleOptionId)
	{
		return LOCTEXT("DungeonHarnessStyleTitle", "Preferred Style");
	}
	if (CurrentNodeId == DungeonHarnessThemeOptionId)
	{
		return LOCTEXT("DungeonHarnessThemeTitle", "Preferred Theme");
	}
	if (CurrentNodeId == DungeonHarnessScaleBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessScaleBiasTitle", "Scale Bias");
	}
	if (CurrentNodeId == DungeonHarnessBranchingBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessBranchingBiasTitle", "Branching Bias");
	}
	if (CurrentNodeId == DungeonHarnessDangerBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessDangerBiasTitle", "Danger Bias");
	}
	if (CurrentNodeId == DungeonHarnessSafeBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessSafeBiasTitle", "Safe Bias");
	}
	if (CurrentNodeId == DungeonHarnessAbundanceBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessAbundanceBiasTitle", "Abundance Bias");
	}
	if (CurrentNodeId == DungeonHarnessMysteryBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessMysteryBiasTitle", "Mystery Bias");
	}
	if (CurrentNodeId == DungeonHarnessClothingBiasOptionId)
	{
		return LOCTEXT("DungeonHarnessClothingBiasTitle", "Clothing Bias");
	}
	if (CurrentNodeId == DungeonHarnessVolatilityOptionId)
	{
		return LOCTEXT("DungeonHarnessVolatilityTitle", "Volatility");
	}
	if (CurrentNodeId == DebugSetTo100OptionId)
	{
		return LOCTEXT("SetTo100Title", "Set to 100%");
	}
	if (CurrentNodeId == DebugSetTo50OptionId)
	{
		return LOCTEXT("SetTo50Title", "Set to 50%");
	}
	if (CurrentNodeId == DebugSetTo0OptionId)
	{
		return LOCTEXT("SetTo0Title", "Set to 0%");
	}
	if (CurrentNodeId == DebugApplyStatusOptionId)
	{
		return LOCTEXT("ApplyStatusTitle", "Apply Status");
	}
	if (CurrentNodeId == DebugAutomaticTattoosOptionId)
	{
		return LOCTEXT("AutomaticTattoosTitle", "[AT] Automatic Tattoos");
	}
	if (ProjectGameplayDebugSubsystemPrivate::IsAutomaticTattooRowNode(CurrentNodeId))
	{
		return LOCTEXT("AutomaticTattooTunerTitle", "[AT] Tattoo Tuner");
	}
	if (CurrentNodeId == TestLevel5OptionId)
	{
		return LOCTEXT("Level5Title", "Raise to Level 5");
	}
	if (CurrentNodeId == TestLevel10OptionId)
	{
		return LOCTEXT("Level10Title", "Raise to Level 10");
	}

	return LOCTEXT("RootTitle", "Gameplay Debug");
}

FText UProjectGameplayDebugSubsystem::ResolveCurrentMenuHint() const
{
	if (ProjectGameplayDebugSubsystemPrivate::IsAutomaticTattooRowNode(GetCurrentMenuParentNodeId()))
	{
		return LOCTEXT("AutomaticTattooTunerHint", "Tune AT runtime-only values, then copy the final DataTable line.");
	}

	return IsAtMenuRoot()
		? LOCTEXT("RootHint", "Runtime-only commands for fast gameplay verification.")
		: LOCTEXT("BranchHint", "Select a command to execute it immediately.");
}

EProjectEmoteMenuVisualMode UProjectGameplayDebugSubsystem::ResolveCurrentMenuVisualMode() const
{
	return IsAtMenuRoot() ? EProjectEmoteMenuVisualMode::Root : EProjectEmoteMenuVisualMode::Category;
}

void UProjectGameplayDebugSubsystem::BuildMenuNodeOptions(TArray<FProjectEmoteMenuOption>& OutOptions)
{
	using namespace ProjectGameplayDebugSubsystemPrivate;

	const FName CurrentNodeId = GetCurrentMenuParentNodeId();
	if (IsAtMenuRoot())
	{
		AddVisibleOption(OutOptions, RootDebugOptionId, NAME_None, LOCTEXT("DebugLabel", "Debug"), LOCTEXT("DebugDescription", "Gameplay state, survival, combat, and status commands."), EProjectEmoteMenuNodeType::Folder, 0, TEXT("Combat"));
		AddVisibleOption(OutOptions, RootTestOptionId, NAME_None, LOCTEXT("TestLabel", "Test"), LOCTEXT("TestDescription", "Progression shortcuts for targeted validation."), EProjectEmoteMenuNodeType::Folder, 10, TEXT("Special"));
		AddVisibleOption(OutOptions, RootAppearanceOptionId, NAME_None, LOCTEXT("AppearanceLabel", "Appearance"), LOCTEXT("AppearanceDescription", "Preview and validate the native project HUD color profiles."), EProjectEmoteMenuNodeType::Folder, 20, TEXT("Social"));
		AddVisibleOption(
			OutOptions,
			RootDungeonHarnessOptionId,
			NAME_None,
			LOCTEXT("DungeonHarnessLabel", "Dungeon Harness"),
			FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusDescription(TrackedPlayerPawn.Get()),
			EProjectEmoteMenuNodeType::Folder,
			30,
			TEXT("Special"));
		AddVisibleOption(OutOptions, RootCancelOptionId, NAME_None, LOCTEXT("CancelLabel", "Cancel"), LOCTEXT("CancelDescription", "Close the gameplay debug menu."), EProjectEmoteMenuNodeType::Cancel, 40, TEXT("Cancel"));
		return;
	}

	if (CurrentNodeId == RootDebugOptionId)
	{
		AddVisibleOption(OutOptions, DebugImmediateDefeatOptionId, CurrentNodeId, LOCTEXT("ImmediateDefeatLabel", "Immediate Defeat"), LOCTEXT("ImmediateDefeatDescription", "Trigger defeated travel and respawn in the dungeon now."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Combat"));
		AddVisibleOption(OutOptions, DebugDownedModeOptionId, CurrentNodeId, LOCTEXT("DownedModeLabel", "Downed Mode"), LOCTEXT("DownedModeDescription", "Force knockout or pending crawl; nearby enemies can start the struggle minigame."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Combat"));
		AddVisibleOption(OutOptions, DebugRestoreAcfHealthOptionId, CurrentNodeId, LOCTEXT("RestoreAcfHealthLabel", "Restore ACF Health"), LOCTEXT("RestoreAcfHealthDescription", "Restore the owner health resource to its current maximum."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Basic"));
		AddVisibleOption(OutOptions, DebugRestoreNeedsSensationsOptionId, CurrentNodeId, LOCTEXT("RestoreNeedsSensationsLabel", "Restore Needs & Pressures"), LOCTEXT("RestoreNeedsSensationsDescription", "Set Hunger, Thirst, and Sleep to max; Madness, Curse, and Pain to zero."), EProjectEmoteMenuNodeType::Action, 30, TEXT("Basic"));
		AddVisibleOption(OutOptions, DebugSetTo100OptionId, CurrentNodeId, LOCTEXT("SetTo100Label", "Set to 100%"), LOCTEXT("SetTo100Description", "Set a negative sensation to its current maximum."), EProjectEmoteMenuNodeType::Folder, 40, TEXT("Special"));
		AddVisibleOption(OutOptions, DebugSetTo50OptionId, CurrentNodeId, LOCTEXT("SetTo50Label", "Set to 50%"), LOCTEXT("SetTo50Description", "Set a need or sensation to half of its current maximum."), EProjectEmoteMenuNodeType::Folder, 50, TEXT("Basic"));
		AddVisibleOption(OutOptions, DebugSetTo0OptionId, CurrentNodeId, LOCTEXT("SetTo0Label", "Set to 0%"), LOCTEXT("SetTo0Description", "Set a survival need to zero."), EProjectEmoteMenuNodeType::Folder, 60, TEXT("Objects"));
		AddVisibleOption(OutOptions, DebugApplyStatusOptionId, CurrentNodeId, LOCTEXT("ApplyStatusLabel", "Apply Status"), LOCTEXT("ApplyStatusDescription", "Force-apply a configured status, bypassing immunity for debug."), EProjectEmoteMenuNodeType::Folder, 70, TEXT("Social"));
		AddVisibleOption(OutOptions, DebugAutomaticTattoosOptionId, CurrentNodeId, LOCTEXT("AutomaticTattoosLabel", "[AT] Automatic Tattoos"), LOCTEXT("AutomaticTattoosDescription", "Tune Automatic Tattoos: development/gameplay SkinnedDecal rows driven by actions, rewards, and DataTable values."), EProjectEmoteMenuNodeType::Folder, 80, TEXT("Special"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DebugSetTo100OptionId)
	{
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo100Prefix, MadnessName), CurrentNodeId, LOCTEXT("MadnessLabel", "Madness"), LOCTEXT("Madness100Description", "Set Madness to its current maximum."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Special"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo100Prefix, CurseName), CurrentNodeId, LOCTEXT("CurseLabel", "Curse"), LOCTEXT("Curse100Description", "Set Curse to its current maximum."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Special"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo100Prefix, PainName), CurrentNodeId, LOCTEXT("PainLabel", "Pain"), LOCTEXT("Pain100Description", "Set Pain to its current maximum."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Special"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DebugSetTo50OptionId)
	{
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, HungerName), CurrentNodeId, LOCTEXT("Hunger50Label", "Hunger"), LOCTEXT("Hunger50Description", "Set Hunger to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, ThirstName), CurrentNodeId, LOCTEXT("Thirst50Label", "Thirst"), LOCTEXT("Thirst50Description", "Set Thirst to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, SleepName), CurrentNodeId, LOCTEXT("Sleep50Label", "Sleep"), LOCTEXT("Sleep50Description", "Set Sleep to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, MadnessName), CurrentNodeId, LOCTEXT("Madness50Label", "Madness"), LOCTEXT("Madness50Description", "Set Madness to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 30, TEXT("Special"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, CurseName), CurrentNodeId, LOCTEXT("Curse50Label", "Curse"), LOCTEXT("Curse50Description", "Set Curse to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 40, TEXT("Special"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo50Prefix, PainName), CurrentNodeId, LOCTEXT("Pain50Label", "Pain"), LOCTEXT("Pain50Description", "Set Pain to 50% of its current maximum."), EProjectEmoteMenuNodeType::Action, 50, TEXT("Special"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DebugSetTo0OptionId)
	{
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo0Prefix, HungerName), CurrentNodeId, LOCTEXT("HungerLabel", "Hunger"), LOCTEXT("Hunger0Description", "Set Hunger to zero."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo0Prefix, ThirstName), CurrentNodeId, LOCTEXT("ThirstLabel", "Thirst"), LOCTEXT("Thirst0Description", "Set Thirst to zero."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeChildOptionId(DebugSetTo0Prefix, SleepName), CurrentNodeId, LOCTEXT("SleepLabel", "Sleep"), LOCTEXT("Sleep0Description", "Set Sleep to zero."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Objects"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DebugApplyStatusOptionId)
	{
		int32 SortOrder = 0;
		for (const FName StatusName : FProjectGameplayDebugCommandExecutor::GetAvailableStatusNames())
		{
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(DebugStatusPrefix, StatusName),
				CurrentNodeId,
				FText::FromName(StatusName),
				LOCTEXT("ApplyStatusItemDescription", "Force-apply this status through the debug bypass path."),
				EProjectEmoteMenuNodeType::Action,
				SortOrder,
				TEXT("Social"));
			SortOrder += 10;
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DebugAutomaticTattoosOptionId)
	{
		int32 SortOrder = 0;
		for (const FName RowName : FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowNames(TrackedPlayerPawn.Get()))
		{
			AddVisibleOption(
				OutOptions,
				MakeAutomaticTattooRowOptionId(RowName),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowLabel(TrackedPlayerPawn.Get(), RowName),
				FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowDescription(TrackedPlayerPawn.Get(), RowName),
				EProjectEmoteMenuNodeType::Folder,
				SortOrder,
				TEXT("Special"));
			SortOrder += 10;
		}
		AddBackOption(OutOptions);
		return;
	}

	if (ProjectGameplayDebugSubsystemPrivate::IsAutomaticTattooRowNode(CurrentNodeId))
	{
		const FName RowName = ProjectGameplayDebugSubsystemPrivate::ExtractAutomaticTattooRowNameFromNode(CurrentNodeId);
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("Force"), RowName), CurrentNodeId, LOCTEXT("TattooForceLabel", "Toggle Preview Active"), LOCTEXT("TattooForceDescription", "Force this automatic tattoo active for this run so it can be tuned before its real unlock event."), EProjectEmoteMenuNodeType::Action, 0, TEXT("Special"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("Copy"), RowName), CurrentNodeId, LOCTEXT("TattooCopyLabel", "Copy DataTable Values"), LOCTEXT("TattooCopyDescription", "Copy the current runtime placement numbers to the clipboard and log."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Objects"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("XMinus"), RowName), CurrentNodeId, LOCTEXT("TattooXMinusLabel", "Offset X -0.25"), LOCTEXT("TattooXMinusDescription", "Move left on the current body placement frame."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Back"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("XPlus"), RowName), CurrentNodeId, LOCTEXT("TattooXPlusLabel", "Offset X +0.25"), LOCTEXT("TattooXPlusDescription", "Move right on the current body placement frame."), EProjectEmoteMenuNodeType::Action, 30, TEXT("Basic"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("YMinus"), RowName), CurrentNodeId, LOCTEXT("TattooYMinusLabel", "Offset Y -0.25"), LOCTEXT("TattooYMinusDescription", "Move down on the current body placement frame."), EProjectEmoteMenuNodeType::Action, 40, TEXT("Back"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("YPlus"), RowName), CurrentNodeId, LOCTEXT("TattooYPlusLabel", "Offset Y +0.25"), LOCTEXT("TattooYPlusDescription", "Move up on the current body placement frame."), EProjectEmoteMenuNodeType::Action, 50, TEXT("Basic"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("SizeMinus"), RowName), CurrentNodeId, LOCTEXT("TattooSizeMinusLabel", "Size -0.50"), LOCTEXT("TattooSizeMinusDescription", "Reduce the projected tattoo size."), EProjectEmoteMenuNodeType::Action, 60, TEXT("Back"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("SizePlus"), RowName), CurrentNodeId, LOCTEXT("TattooSizePlusLabel", "Size +0.50"), LOCTEXT("TattooSizePlusDescription", "Increase the projected tattoo size."), EProjectEmoteMenuNodeType::Action, 70, TEXT("Basic"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("RotationMinus"), RowName), CurrentNodeId, LOCTEXT("TattooRotationMinusLabel", "Rotation -5"), LOCTEXT("TattooRotationMinusDescription", "Rotate counter-clockwise around the tattoo normal."), EProjectEmoteMenuNodeType::Action, 80, TEXT("Back"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("RotationPlus"), RowName), CurrentNodeId, LOCTEXT("TattooRotationPlusLabel", "Rotation +5"), LOCTEXT("TattooRotationPlusDescription", "Rotate clockwise around the tattoo normal."), EProjectEmoteMenuNodeType::Action, 90, TEXT("Basic"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("ProjectionMinus"), RowName), CurrentNodeId, LOCTEXT("TattooProjectionMinusLabel", "Projection -0.50"), LOCTEXT("TattooProjectionMinusDescription", "Move the projection origin closer to the reference surface."), EProjectEmoteMenuNodeType::Action, 100, TEXT("Back"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("ProjectionPlus"), RowName), CurrentNodeId, LOCTEXT("TattooProjectionPlusLabel", "Projection +0.50"), LOCTEXT("TattooProjectionPlusDescription", "Move the projection origin farther from the reference surface."), EProjectEmoteMenuNodeType::Action, 110, TEXT("Basic"));
		AddVisibleOption(OutOptions, MakeAutomaticTattooActionOptionId(TEXT("Reset"), RowName), CurrentNodeId, LOCTEXT("TattooResetLabel", "Reset Runtime Override"), LOCTEXT("TattooResetDescription", "Discard runtime tuning values and return to the DataTable row values."), EProjectEmoteMenuNodeType::Action, 120, TEXT("Cancel"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == RootTestOptionId)
	{
		AddVisibleOption(OutOptions, TestLevel5OptionId, CurrentNodeId, LOCTEXT("RaiseLevel5Label", "Raise to Level 5"), LOCTEXT("RaiseLevel5Description", "Raise one Inner Doctrine attribute to level 5."), EProjectEmoteMenuNodeType::Folder, 0, TEXT("Special"));
		AddVisibleOption(OutOptions, TestLevel10OptionId, CurrentNodeId, LOCTEXT("RaiseLevel10Label", "Raise to Level 10"), LOCTEXT("RaiseLevel10Description", "Raise one Inner Doctrine attribute to level 10."), EProjectEmoteMenuNodeType::Folder, 10, TEXT("Special"));
		AddVisibleOption(OutOptions, TestRuntimeFpsBenchmarkOptionId, CurrentNodeId, LOCTEXT("RuntimeFpsBenchmarkLabel", "Runtime FPS Benchmark"), LOCTEXT("RuntimeFpsBenchmarkDescription", "Run the dungeon combat performance benchmark and write FPS artifacts."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Combat"));
		AddVisibleOption(OutOptions, TestFullStackOverloadBenchmarkOptionId, CurrentNodeId, LOCTEXT("FullStackOverloadBenchmarkLabel", "Full Stack Overload Benchmark"), LOCTEXT("FullStackOverloadBenchmarkDescription", "Run the full-stack dungeon gameplay overload benchmark and write segmented FPS artifacts."), EProjectEmoteMenuNodeType::Action, 30, TEXT("Combat"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == RootDungeonHarnessOptionId)
	{
		AddVisibleOption(
			OutOptions,
			DungeonHarnessStatusOptionId,
			CurrentNodeId,
			FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusLabel(TrackedPlayerPawn.Get()),
			FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusDescription(TrackedPlayerPawn.Get()),
			EProjectEmoteMenuNodeType::Action,
			0,
			TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessNextFloorOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessNextFloorLabel", "Next Floor"), LOCTEXT("DungeonHarnessNextFloorDescription", "Advance the Floor counter and generate a completely new seeded dungeon."), EProjectEmoteMenuNodeType::Action, 10, TEXT("Combat"));
		AddVisibleOption(OutOptions, DungeonHarnessReplayOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessReplayLabel", "Replay Same Floor"), LOCTEXT("DungeonHarnessReplayDescription", "Restore the exact frozen intent, manifest, seed, layout, enemies, and resources for this Floor."), EProjectEmoteMenuNodeType::Action, 20, TEXT("Combat"));
		AddVisibleOption(OutOptions, DungeonHarnessRerollOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessRerollLabel", "Reroll Current Floor"), LOCTEXT("DungeonHarnessRerollDescription", "Keep the Floor counter but advance GenerationSerial and generate a new dungeon."), EProjectEmoteMenuNodeType::Action, 30, TEXT("Combat"));
		AddVisibleOption(OutOptions, DungeonHarnessNewRunOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessNewRunLabel", "New Random Run"), LOCTEXT("DungeonHarnessNewRunDescription", "Start Floor 1 with a new positive GUID-derived RunSeed."), EProjectEmoteMenuNodeType::Action, 40, TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessSeed42OptionId, CurrentNodeId, LOCTEXT("DungeonHarnessSeed42Label", "New Test Run Seed 42"), LOCTEXT("DungeonHarnessSeed42Description", "Start a reproducible Floor 1 run using RunSeed 42."), EProjectEmoteMenuNodeType::Action, 50, TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessJumpFloorOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessJumpFloorLabel", "Jump Floor"), LOCTEXT("DungeonHarnessJumpFloorDescription", "Development-only jump to Floors 1, 10, 25, 50, 100, 101, 125, 500, or 1000 with neutral synthetic history."), EProjectEmoteMenuNodeType::Folder, 60, TEXT("Objects"));
		AddVisibleOption(OutOptions, DungeonHarnessStyleOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessStyleLabel", "Preferred Style"), LOCTEXT("DungeonHarnessStyleDescription", "Queue Auto, Standard, Compact, or Branching as a probability bias for the next floor boundary."), EProjectEmoteMenuNodeType::Folder, 70, TEXT("Objects"));
		AddVisibleOption(OutOptions, DungeonHarnessThemeOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessThemeLabel", "Preferred Theme"), LOCTEXT("DungeonHarnessThemeDescription", "Queue Auto, Default, Forge, or Shrine as a probability boost. It never forces the resolved Theme."), EProjectEmoteMenuNodeType::Folder, 80, TEXT("Objects"));
		AddVisibleOption(OutOptions, DungeonHarnessScaleBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessScaleBiasLabel", "Scale Bias"), LOCTEXT("DungeonHarnessScaleBiasDescription", "Bias the Director toward smaller or larger validated layouts without selecting an exact size."), EProjectEmoteMenuNodeType::Folder, 90, TEXT("Objects"));
		AddVisibleOption(OutOptions, DungeonHarnessBranchingBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessBranchingBiasLabel", "Branching Bias"), LOCTEXT("DungeonHarnessBranchingBiasDescription", "Bias side-path personality while preserving the V4 Style/Theme intersection and hard ranges."), EProjectEmoteMenuNodeType::Folder, 100, TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessDangerBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessDangerBiasLabel", "Danger Bias"), LOCTEXT("DungeonHarnessDangerBiasDescription", "Bias enemy presence and difficulty once through the V4 enemy bundle; the 25-enemy cap remains authoritative."), EProjectEmoteMenuNodeType::Folder, 110, TEXT("Combat"));
		AddVisibleOption(OutOptions, DungeonHarnessSafeBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessSafeBiasLabel", "Safe Bias"), LOCTEXT("DungeonHarnessSafeBiasDescription", "Bias NPC opportunity without requesting an exact count or bypassing the party and floor caps."), EProjectEmoteMenuNodeType::Folder, 120, TEXT("Basic"));
		AddVisibleOption(OutOptions, DungeonHarnessAbundanceBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessAbundanceBiasLabel", "Abundance Bias"), LOCTEXT("DungeonHarnessAbundanceBiasDescription", "Bias food opportunity through log-odds while preserving pity, Nothing, and the 30-food cap."), EProjectEmoteMenuNodeType::Folder, 130, TEXT("Basic"));
		AddVisibleOption(OutOptions, DungeonHarnessMysteryBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessMysteryBiasLabel", "Mystery Bias"), LOCTEXT("DungeonHarnessMysteryBiasDescription", "Bias chest opportunity and tier tilt while preserving the permanent Nothing mass and 10-chest cap."), EProjectEmoteMenuNodeType::Folder, 140, TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessClothingBiasOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessClothingBiasLabel", "Clothing Bias"), LOCTEXT("DungeonHarnessClothingBiasDescription", "Bias clothing opportunity and tiers without selecting exact items or counts."), EProjectEmoteMenuNodeType::Folder, 150, TEXT("Objects"));
		AddVisibleOption(OutOptions, DungeonHarnessVolatilityOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessVolatilityLabel", "Volatility"), LOCTEXT("DungeonHarnessVolatilityDescription", "Control Style/Theme blend dispersion without bypassing profile intersections or hard limits."), EProjectEmoteMenuNodeType::Folder, 160, TEXT("Special"));
		AddVisibleOption(OutOptions, DungeonHarnessClearIntentOptionId, CurrentNodeId, LOCTEXT("DungeonHarnessClearIntentLabel", "Clear Next-Floor Intent"), LOCTEXT("DungeonHarnessClearIntentDescription", "Return the next generation to the autonomous Dungeon Director V4."), EProjectEmoteMenuNodeType::Action, 170, TEXT("Cancel"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DungeonHarnessJumpFloorOptionId)
	{
		static constexpr int64 FloorNumbers[] = { 1, 10, 25, 50, 100, 101, 125, 500, 1000 };
		for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(FloorNumbers); ++OptionIndex)
		{
			const int64 FloorNumber = FloorNumbers[OptionIndex];
			const FName FloorId(*FString::Printf(TEXT("%lld"), static_cast<long long>(FloorNumber)));
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(DungeonHarnessJumpFloorPrefix, FloorId),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetDungeonHarnessFloorChoiceLabel(TrackedPlayerPawn.Get(), FloorNumber),
				LOCTEXT("DungeonHarnessFloorChoiceDescription", "Debug-jump through the normal loading and controlled Calysto generation path."),
				EProjectEmoteMenuNodeType::Action,
				OptionIndex * 10,
				TEXT("Objects"));
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DungeonHarnessStyleOptionId)
	{
		struct FStyleChoice
		{
			const TCHAR* Id;
			bool bAuto;
			EEFCalystoStyleV4 Style;
		};
		static constexpr FStyleChoice Choices[] = {
			{ TEXT("Auto"), true, EEFCalystoStyleV4::Standard },
			{ TEXT("Standard"), false, EEFCalystoStyleV4::Standard },
			{ TEXT("Compact"), false, EEFCalystoStyleV4::Compact },
			{ TEXT("Branching"), false, EEFCalystoStyleV4::Branching }
		};
		for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(Choices); ++OptionIndex)
		{
			const FStyleChoice& Choice = Choices[OptionIndex];
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(DungeonHarnessStylePrefix, FName(Choice.Id)),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStyleChoiceLabel(
					TrackedPlayerPawn.Get(), Choice.bAuto, Choice.Style),
				LOCTEXT("DungeonHarnessStyleChoiceDescription", "Queue this style as a probability bias; the Director still resolves a procedural floor."),
				EProjectEmoteMenuNodeType::Action,
				OptionIndex * 10,
				TEXT("Objects"));
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DungeonHarnessThemeOptionId)
	{
		struct FThemeChoice
		{
			const TCHAR* Id;
			bool bAuto;
			EEFCalystoThemeV4 Theme;
		};
		static constexpr FThemeChoice Choices[] = {
			{ TEXT("Auto"), true, EEFCalystoThemeV4::Default },
			{ TEXT("Default"), false, EEFCalystoThemeV4::Default },
			{ TEXT("Forge"), false, EEFCalystoThemeV4::Forge },
			{ TEXT("Shrine"), false, EEFCalystoThemeV4::Shrine }
		};
		for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(Choices); ++OptionIndex)
		{
			const FThemeChoice& Choice = Choices[OptionIndex];
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(DungeonHarnessThemePrefix, FName(Choice.Id)),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetDungeonHarnessThemeChoiceLabel(
					TrackedPlayerPawn.Get(), Choice.bAuto, Choice.Theme),
				LOCTEXT("DungeonHarnessThemeChoiceDescription", "Queue this Theme as a probability boost; anti-streak and the deterministic draw remain authoritative."),
				EProjectEmoteMenuNodeType::Action,
				OptionIndex * 10,
				TEXT("Objects"));
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DungeonHarnessScaleBiasOptionId
		|| CurrentNodeId == DungeonHarnessBranchingBiasOptionId
		|| CurrentNodeId == DungeonHarnessDangerBiasOptionId
		|| CurrentNodeId == DungeonHarnessSafeBiasOptionId
		|| CurrentNodeId == DungeonHarnessAbundanceBiasOptionId
		|| CurrentNodeId == DungeonHarnessMysteryBiasOptionId
		|| CurrentNodeId == DungeonHarnessClothingBiasOptionId)
	{
		const FName BiasName = CurrentNodeId == DungeonHarnessScaleBiasOptionId ? ScaleBiasName
			: CurrentNodeId == DungeonHarnessBranchingBiasOptionId ? BranchingBiasName
			: CurrentNodeId == DungeonHarnessDangerBiasOptionId ? DangerBiasName
			: CurrentNodeId == DungeonHarnessSafeBiasOptionId ? SafeBiasName
			: CurrentNodeId == DungeonHarnessAbundanceBiasOptionId ? AbundanceBiasName
			: CurrentNodeId == DungeonHarnessMysteryBiasOptionId ? MysteryBiasName
			: ClothingBiasName;
		const FString& Prefix = CurrentNodeId == DungeonHarnessScaleBiasOptionId ? DungeonHarnessScaleBiasPrefix
			: CurrentNodeId == DungeonHarnessBranchingBiasOptionId ? DungeonHarnessBranchingBiasPrefix
			: CurrentNodeId == DungeonHarnessDangerBiasOptionId ? DungeonHarnessDangerBiasPrefix
			: CurrentNodeId == DungeonHarnessSafeBiasOptionId ? DungeonHarnessSafeBiasPrefix
			: CurrentNodeId == DungeonHarnessAbundanceBiasOptionId ? DungeonHarnessAbundanceBiasPrefix
			: CurrentNodeId == DungeonHarnessMysteryBiasOptionId ? DungeonHarnessMysteryBiasPrefix
			: DungeonHarnessClothingBiasPrefix;
		static constexpr float Biases[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
		for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(Biases); ++OptionIndex)
		{
			const float Bias = Biases[OptionIndex];
			const FName BiasId(*FString::Printf(TEXT("%.2f"), Bias));
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(Prefix, BiasId),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetDungeonHarnessBiasChoiceLabel(TrackedPlayerPawn.Get(), BiasName, Bias),
				LOCTEXT("DungeonHarnessBiasChoiceDescription", "Queue this normalized bias for the next floor boundary; it never requests an exact value."),
				EProjectEmoteMenuNodeType::Action,
				OptionIndex * 10,
				TEXT("Special"));
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == DungeonHarnessVolatilityOptionId)
	{
		static constexpr float Values[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
		for (int32 OptionIndex = 0; OptionIndex < UE_ARRAY_COUNT(Values); ++OptionIndex)
		{
			const float Volatility = Values[OptionIndex];
			const FName VolatilityId(*FString::Printf(TEXT("%.2f"), Volatility));
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(DungeonHarnessVolatilityPrefix, VolatilityId),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetDungeonHarnessVolatilityChoiceLabel(TrackedPlayerPawn.Get(), Volatility),
				LOCTEXT("DungeonHarnessVolatilityChoiceDescription", "Queue a normalized volatility bias: negative is more focused, positive is more dispersed, and zero keeps the Style + Theme value."),
				EProjectEmoteMenuNodeType::Action,
				OptionIndex * 10,
				TEXT("Special"));
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == RootAppearanceOptionId)
	{
		FText ThemeDescription = LOCTEXT(
			"HudThemeDescriptionUnavailable",
			"Select Auto, Red, Blue, Purple, Green, or Black.");
		if (UGameInstance* GameInstance =
			GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (const UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				ThemeDescription = FText::Format(
					LOCTEXT(
						"HudThemeDescription",
						"Selection: {0}. Resolved profile: {1}. Changes remain live while this menu is open."),
					FText::FromString(EFProjectUITheme::GetPresetName(ThemeSubsystem->GetSelectionPreset())),
					FText::FromString(EFProjectUITheme::GetPresetName(ThemeSubsystem->GetResolvedPreset())));
			}
		}
		AddVisibleOption(
			OutOptions,
			AppearanceHudThemeOptionId,
			CurrentNodeId,
			LOCTEXT("HudThemeLabel", "HUD Theme"),
			ThemeDescription,
			EProjectEmoteMenuNodeType::Folder,
			0,
			TEXT("Social"));
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == AppearanceHudThemeOptionId)
	{
		static constexpr EEFProjectHUDThemePreset ThemePresets[] = {
			EEFProjectHUDThemePreset::Auto,
			EEFProjectHUDThemePreset::Red,
			EEFProjectHUDThemePreset::Blue,
			EEFProjectHUDThemePreset::Purple,
			EEFProjectHUDThemePreset::Green,
			EEFProjectHUDThemePreset::Black
		};

		UGameInstance* GameInstance =
			GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		const UEFProjectDynamicThemeSubsystem* ThemeSubsystem = GameInstance
			? GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>()
			: nullptr;
		int32 SortOrder = 0;
		for (const EEFProjectHUDThemePreset Preset : ThemePresets)
		{
			const FString PresetName = EFProjectUITheme::GetPresetName(Preset);
			const bool bSelected = ThemeSubsystem
				&& ThemeSubsystem->GetSelectionPreset() == Preset;
			const FText Label = FText::FromString(
				bSelected
					? FString::Printf(TEXT("[Selected] %s"), *PresetName)
					: PresetName);
			const FText Description = Preset == EEFProjectHUDThemePreset::Auto
				? LOCTEXT(
					"HudThemeAutoDescription",
					"Use the neutral Black profile. Character identity does not alter HUD color.")
				: FText::Format(
					LOCTEXT(
						"HudThemeManualDescription",
						"Temporarily override Auto and preview the complete {0} texture and color profile."),
					FText::FromString(PresetName));
			AddVisibleOption(
				OutOptions,
				FName(*(AppearanceHudThemePrefix + PresetName)),
				CurrentNodeId,
				Label,
				Description,
				EProjectEmoteMenuNodeType::Action,
				SortOrder,
				TEXT("Special"));
			SortOrder += 10;
		}
		AddBackOption(OutOptions);
		return;
	}

	if (CurrentNodeId == TestLevel5OptionId || CurrentNodeId == TestLevel10OptionId)
	{
		const bool bLevel5 = CurrentNodeId == TestLevel5OptionId;
		const FString& Prefix = bLevel5 ? TestLevel5Prefix : TestLevel10Prefix;
		int32 SortOrder = 0;
		for (const EProjectDoctrineAttribute Attribute : FProjectGameplayDebugCommandExecutor::GetDebugAttributes())
		{
			const FName AttributeId = FProjectGameplayDebugCommandExecutor::GetAttributeId(Attribute);
			AddVisibleOption(
				OutOptions,
				MakeChildOptionId(Prefix, AttributeId),
				CurrentNodeId,
				FProjectGameplayDebugCommandExecutor::GetAttributeDisplayName(Attribute),
				bLevel5 ? LOCTEXT("RaiseAttribute5Description", "Raise this attribute to level 5 without downgrading it.") : LOCTEXT("RaiseAttribute10Description", "Raise this attribute to level 10 without downgrading it."),
				EProjectEmoteMenuNodeType::Action,
				SortOrder,
				NAME_None,
				AttributeId);
			SortOrder += 10;
		}
		AddBackOption(OutOptions);
		return;
	}

	AddBackOption(OutOptions);
}

void UProjectGameplayDebugSubsystem::AddVisibleOption(
	TArray<FProjectEmoteMenuOption>& OutOptions,
	const FName NodeId,
	const FName ParentNodeId,
	const FText& Label,
	const FText& Description,
	const EProjectEmoteMenuNodeType NodeType,
	const int32 SortOrder,
	const FName VisualIconId,
	const FName VisualAttribute)
{
	FProjectEmoteMenuNodeDefinition MenuNode;
	MenuNode.NodeId = NodeId;
	MenuNode.ParentNodeId = ParentNodeId;
	MenuNode.DisplayName = Label;
	MenuNode.Description = Description;
	MenuNode.NodeType = NodeType;
	MenuNode.SortOrder = SortOrder;
	MenuNode.VisualIconId = VisualIconId;
	MenuNode.VisualAttribute = VisualAttribute;
	CachedVisibleMenuNodes.Add(NodeId, MenuNode);

	FProjectEmoteMenuOption Option;
	Option.OptionId = NodeId;
	Option.Label = Label;
	Option.Description = Description;
	Option.NodeType = NodeType;
	Option.VisualIconId = VisualIconId;
	Option.VisualAttribute = VisualAttribute;
	OutOptions.Add(Option);
}

void UProjectGameplayDebugSubsystem::AddBackOption(TArray<FProjectEmoteMenuOption>& OutOptions)
{
	AddVisibleOption(
		OutOptions,
		ProjectGameplayDebugSubsystemPrivate::BackOptionId,
		GetCurrentMenuParentNodeId(),
		LOCTEXT("BackLabel", "Back"),
		LOCTEXT("BackDescription", "Return to the previous debug menu."),
		EProjectEmoteMenuNodeType::Back,
		10000,
		TEXT("Back"));
}

bool UProjectGameplayDebugSubsystem::ExecuteCommand(const FName OptionId)
{
	using namespace ProjectGameplayDebugSubsystemPrivate;

	AActor* CommandOwner = TrackedPlayerPawn.Get();
	if (!CommandOwner)
	{
		return false;
	}

	if (OptionId == DebugImmediateDefeatOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::TriggerImmediateDefeat(CommandOwner);
	}
	if (OptionId == DebugDownedModeOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::TriggerDownedMode(CommandOwner);
	}
	if (OptionId == DebugRestoreAcfHealthOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(CommandOwner);
	}
	if (OptionId == DebugRestoreNeedsSensationsOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RestoreNeedsAndSensations(CommandOwner);
	}
	if (OptionId == TestRuntimeFpsBenchmarkOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::StartRuntimeFpsBenchmark(CommandOwner);
	}
	if (OptionId == TestFullStackOverloadBenchmarkOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::StartFullStackOverloadBenchmark(CommandOwner);
	}
	if (OptionId == DungeonHarnessStatusOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RefreshDungeonHarnessStatus(CommandOwner);
	}
	if (OptionId == DungeonHarnessNextFloorOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RequestAdvanceDungeonFloor(CommandOwner);
	}
	if (OptionId == DungeonHarnessReplayOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RequestReplayDungeonFloor(CommandOwner);
	}
	if (OptionId == DungeonHarnessRerollOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RequestRerollDungeonFloor(CommandOwner);
	}
	if (OptionId == DungeonHarnessNewRunOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RequestStartNewDungeonRun(CommandOwner);
	}
	if (OptionId == DungeonHarnessSeed42OptionId)
	{
		return FProjectGameplayDebugCommandExecutor::RequestStartDungeonTestRun(CommandOwner);
	}
	if (OptionId == DungeonHarnessClearIntentOptionId)
	{
		return FProjectGameplayDebugCommandExecutor::ClearDungeonHarnessDirectorIntent(CommandOwner);
	}

	const FString OptionString = OptionId.ToString();
	if (OptionString.StartsWith(DungeonHarnessJumpFloorPrefix))
	{
		const int64 FloorNumber = FCString::Atoi64(*OptionString.RightChop(DungeonHarnessJumpFloorPrefix.Len()));
		return FProjectGameplayDebugCommandExecutor::RequestTravelToDungeonFloor(CommandOwner, FloorNumber);
	}
	if (OptionString.StartsWith(DungeonHarnessStylePrefix))
	{
		const FString StyleName = OptionString.RightChop(DungeonHarnessStylePrefix.Len());
		if (StyleName.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredStyle(
				CommandOwner, true, EEFCalystoStyleV4::Standard);
		}
		const int64 StyleValue = StaticEnum<EEFCalystoStyleV4>()->GetValueByNameString(StyleName);
		return StyleValue != INDEX_NONE
			&& FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredStyle(
				CommandOwner, false, static_cast<EEFCalystoStyleV4>(StyleValue));
	}
	if (OptionString.StartsWith(DungeonHarnessThemePrefix))
	{
		const FString ThemeName = OptionString.RightChop(DungeonHarnessThemePrefix.Len());
		if (ThemeName.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredTheme(
				CommandOwner, true, EEFCalystoThemeV4::Default);
		}
		const int64 ThemeValue = StaticEnum<EEFCalystoThemeV4>()->GetValueByNameString(ThemeName);
		return ThemeValue != INDEX_NONE
			&& FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredTheme(
				CommandOwner, false, static_cast<EEFCalystoThemeV4>(ThemeValue));
	}
	if (OptionString.StartsWith(DungeonHarnessScaleBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, ScaleBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessScaleBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessBranchingBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, BranchingBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessBranchingBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessDangerBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, DangerBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessDangerBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessSafeBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, SafeBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessSafeBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessAbundanceBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, AbundanceBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessAbundanceBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessMysteryBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, MysteryBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessMysteryBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessClothingBiasPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			CommandOwner, ClothingBiasName,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessClothingBiasPrefix.Len())));
	}
	if (OptionString.StartsWith(DungeonHarnessVolatilityPrefix))
	{
		return FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentVolatility(
			CommandOwner,
			FCString::Atof(*OptionString.RightChop(DungeonHarnessVolatilityPrefix.Len())));
	}
	if (OptionString.StartsWith(AppearanceHudThemePrefix))
	{
		EEFProjectHUDThemePreset Preset = EEFProjectHUDThemePreset::Auto;
		const FString PresetName =
			OptionString.RightChop(AppearanceHudThemePrefix.Len());
		if (!EFProjectUITheme::TryParsePreset(PresetName, Preset))
		{
			return false;
		}

		if (UGameInstance* GameInstance =
			GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			if (UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				ThemeSubsystem->SetThemePreset(Preset);
				return true;
			}
		}
		return false;
	}
	if (OptionString.StartsWith(DebugSetTo100Prefix))
	{
		const FName SensationName(*OptionString.RightChop(DebugSetTo100Prefix.Len()));
		return FProjectGameplayDebugCommandExecutor::SetSensationToMax(CommandOwner, SensationName);
	}
	if (OptionString.StartsWith(DebugSetTo50Prefix))
	{
		const FName EntryName(*OptionString.RightChop(DebugSetTo50Prefix.Len()));
		return FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(CommandOwner, EntryName, 0.5f);
	}
	if (OptionString.StartsWith(DebugSetTo0Prefix))
	{
		const FName NeedName(*OptionString.RightChop(DebugSetTo0Prefix.Len()));
		return FProjectGameplayDebugCommandExecutor::SetNeedToZero(CommandOwner, NeedName);
	}
	if (OptionString.StartsWith(DebugStatusPrefix))
	{
		const FName StatusName(*OptionString.RightChop(DebugStatusPrefix.Len()));
		return FProjectGameplayDebugCommandExecutor::ForceApplyStatus(CommandOwner, StatusName);
	}
	FString AutomaticTattooActionName;
	FName AutomaticTattooRowName;
	if (ParseAutomaticTattooAction(OptionId, AutomaticTattooActionName, AutomaticTattooRowName))
	{
		if (AutomaticTattooActionName == TEXT("Force"))
		{
			return FProjectGameplayDebugCommandExecutor::ToggleAutomaticTattooForcedActive(CommandOwner, AutomaticTattooRowName);
		}
		if (AutomaticTattooActionName == TEXT("Copy"))
		{
			return FProjectGameplayDebugCommandExecutor::CopyAutomaticTattooPlacementValues(CommandOwner, AutomaticTattooRowName);
		}
		if (AutomaticTattooActionName == TEXT("Reset"))
		{
			return FProjectGameplayDebugCommandExecutor::ResetAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName);
		}
		if (AutomaticTattooActionName == TEXT("XMinus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, -0.25f, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("XPlus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("YMinus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, -0.25f, 0.0f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("YPlus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.25f, 0.0f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("SizeMinus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, -0.5f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("SizePlus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("RotationMinus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, 0.0f, -5.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("RotationPlus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f);
		}
		if (AutomaticTattooActionName == TEXT("ProjectionMinus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, 0.0f, 0.0f, -0.5f);
		}
		if (AutomaticTattooActionName == TEXT("ProjectionPlus"))
		{
			return FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(CommandOwner, AutomaticTattooRowName, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f);
		}
	}

	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Count;
	if (TryResolveAttributeFromCommandId(OptionId, 5, Attribute))
	{
		return FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(CommandOwner, Attribute, 5);
	}
	if (TryResolveAttributeFromCommandId(OptionId, 10, Attribute))
	{
		return FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(CommandOwner, Attribute, 10);
	}

	return false;
}

bool UProjectGameplayDebugSubsystem::TryResolveAttributeFromCommandId(
	const FName OptionId,
	const int32 TargetLevel,
	EProjectDoctrineAttribute& OutAttribute) const
{
	using namespace ProjectGameplayDebugSubsystemPrivate;

	const FString OptionString = OptionId.ToString();
	const FString& Prefix = TargetLevel == 5 ? TestLevel5Prefix : TestLevel10Prefix;
	if (!OptionString.StartsWith(Prefix))
	{
		return false;
	}

	const FName AttributeId(*OptionString.RightChop(Prefix.Len()));
	for (const EProjectDoctrineAttribute Attribute : FProjectGameplayDebugCommandExecutor::GetDebugAttributes())
	{
		if (FProjectGameplayDebugCommandExecutor::GetAttributeId(Attribute) == AttributeId)
		{
			OutAttribute = Attribute;
			return true;
		}
	}

	return false;
}

void UProjectGameplayDebugSubsystem::HandleToggleMenuPressed()
{
	if (bAutomaticTattooTunerOpen)
	{
		CloseAutomaticTattooTuner(false);
		return;
	}

	if (bMenuOpen)
	{
		CloseMenu();
		return;
	}

	OpenMenu();
}

void UProjectGameplayDebugSubsystem::HandleMenuOptionConfirmed(const FName OptionId)
{
	using namespace ProjectGameplayDebugSubsystemPrivate;

	if (OptionId == BackOptionId)
	{
		HandleMenuBackRequested();
		return;
	}

	if (OptionId == RootCancelOptionId)
	{
		CloseMenu();
		return;
	}

	const FProjectEmoteMenuNodeDefinition* MenuNode = CachedVisibleMenuNodes.Find(OptionId);
	if (!MenuNode)
	{
		return;
	}

	if (ProjectGameplayDebugSubsystemPrivate::IsAutomaticTattooRowNode(MenuNode->NodeId))
	{
		OpenAutomaticTattooTuner(ProjectGameplayDebugSubsystemPrivate::ExtractAutomaticTattooRowNameFromNode(MenuNode->NodeId));
		return;
	}

	switch (MenuNode->NodeType)
	{
	case EProjectEmoteMenuNodeType::Folder:
		MenuNodeStack.Add(MenuNode->NodeId);
		RefreshCurrentMenuModel();
		return;
	case EProjectEmoteMenuNodeType::Action:
		ExecuteCommand(OptionId);
		if (IsAutomaticTattooTunerCommand(OptionId)
			|| IsHudThemeCommand(OptionId)
			|| FProjectGameplayDebugCommandExecutor::IsDungeonHarnessPersistentCommand(OptionId))
		{
			RefreshCurrentMenuModel(OptionId);
			return;
		}
		CloseMenu();
		return;
	case EProjectEmoteMenuNodeType::Cancel:
		CloseMenu();
		return;
	case EProjectEmoteMenuNodeType::Back:
		HandleMenuBackRequested();
		return;
	default:
		return;
	}
}

void UProjectGameplayDebugSubsystem::HandleMenuCancelRequested()
{
	CloseMenu();
}

void UProjectGameplayDebugSubsystem::HandleMenuBackRequested()
{
	if (!MenuNodeStack.IsEmpty())
	{
		const FName PreviousNodeId = MenuNodeStack.Pop(EAllowShrinking::No);
		RefreshCurrentMenuModel(PreviousNodeId);
		return;
	}

	CloseMenu();
}

void UProjectGameplayDebugSubsystem::OpenAutomaticTattooTuner(const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)RowName;
#else
	if (RowName.IsNone() || !TrackedPlayerController || !TrackedPlayerPawn)
	{
		return;
	}

	UWorld* World = GetWorld();
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	if (!TattooSubsystem)
	{
		return;
	}

	FProjectAutomaticTattooRuntimeDebugSnapshot Snapshot;
	if (!TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshot(RowName, Snapshot))
	{
		return;
	}

	CloseMenu();

	ActiveAutomaticTattooTunerRow = RowName;
	AutomaticTattooTunerInitialState = TattooSubsystem->CaptureAutomaticTattooRuntimeDebugState(RowName);
	TattooSubsystem->SetAutomaticTattooRuntimeDebugForcedActive(TrackedPlayerPawn, RowName, true);
	TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshot(RowName, Snapshot);

	if (!CreateAutomaticTattooTunerPreviewCamera())
	{
		TattooSubsystem->RestoreAutomaticTattooRuntimeDebugState(TrackedPlayerPawn, RowName, AutomaticTattooTunerInitialState);
		ActiveAutomaticTattooTunerRow = NAME_None;
		return;
	}

	TrackedAutomaticTattooTunerWidget = CreateWidget<UProjectAutomaticTattooTunerWidget>(
		TrackedPlayerController,
		UProjectAutomaticTattooTunerWidget::StaticClass(),
		TEXT("ProjectAutomaticTattooTunerWidget"));
	if (!TrackedAutomaticTattooTunerWidget)
	{
		DestroyAutomaticTattooTunerPreviewCamera();
		TattooSubsystem->RestoreAutomaticTattooRuntimeDebugState(TrackedPlayerPawn, RowName, AutomaticTattooTunerInitialState);
		ActiveAutomaticTattooTunerRow = NAME_None;
		return;
	}

	TrackedAutomaticTattooTunerWidget->InitializeForTattoo(Snapshot);
	TrackedAutomaticTattooTunerWidget->OnPreviewRowChanged.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerPreviewChanged);
	TrackedAutomaticTattooTunerWidget->OnApplyRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerApplyRequested);
	TrackedAutomaticTattooTunerWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerCancelRequested);
	TrackedAutomaticTattooTunerWidget->OnViewRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerViewRequested);
	TrackedAutomaticTattooTunerWidget->OnOrbitRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerOrbitRequested);
	TrackedAutomaticTattooTunerWidget->OnPanRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerPanRequested);
	TrackedAutomaticTattooTunerWidget->OnZoomRequested.AddUObject(this, &ThisClass::HandleAutomaticTattooTunerZoomRequested);

	TrackedAutomaticTattooTunerWidget->SetVisibility(ESlateVisibility::Visible);
	TrackedAutomaticTattooTunerWidget->AddToViewport(ProjectGameplayDebugSubsystemPrivate::AutomaticTattooTunerZOrder);

	bAutomaticTattooTunerOpen = true;
	bHasPendingAutomaticTattooTunerPreview = false;
	ApplyAutomaticTattooTunerInputCapture();
	TrackedAutomaticTattooTunerWidget->SetKeyboardFocus();
#endif
}

void UProjectGameplayDebugSubsystem::CloseAutomaticTattooTuner(const bool bApply)
{
#if UE_BUILD_SHIPPING
	(void)bApply;
#else
	if (!bAutomaticTattooTunerOpen && !TrackedAutomaticTattooTunerWidget && !AutomaticTattooTunerPreviewCameraActor)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutomaticTattooTunerPreviewTimerHandle);
	}
	bHasPendingAutomaticTattooTunerPreview = false;

	if (TrackedAutomaticTattooTunerWidget)
	{
		TrackedAutomaticTattooTunerWidget->OnPreviewRowChanged.Clear();
		TrackedAutomaticTattooTunerWidget->OnApplyRequested.Clear();
		TrackedAutomaticTattooTunerWidget->OnCancelRequested.Clear();
		TrackedAutomaticTattooTunerWidget->OnViewRequested.Clear();
		TrackedAutomaticTattooTunerWidget->OnOrbitRequested.Clear();
		TrackedAutomaticTattooTunerWidget->OnPanRequested.Clear();
		TrackedAutomaticTattooTunerWidget->OnZoomRequested.Clear();
		TrackedAutomaticTattooTunerWidget->RemoveFromParent();
	}

	if (UWorld* World = GetWorld())
	{
		if (UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem = World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>())
		{
			if (bApply)
			{
				TattooSubsystem->SetAutomaticTattooRuntimeDebugForcedActive(
					TrackedPlayerPawn,
					ActiveAutomaticTattooTunerRow,
					AutomaticTattooTunerInitialState.bForcedActiveForDebug);
			}
			else
			{
				TattooSubsystem->RestoreAutomaticTattooRuntimeDebugState(
					TrackedPlayerPawn,
					ActiveAutomaticTattooTunerRow,
					AutomaticTattooTunerInitialState);
			}
		}
	}

	TrackedAutomaticTattooTunerWidget = nullptr;
	bAutomaticTattooTunerOpen = false;
	ActiveAutomaticTattooTunerRow = NAME_None;
	AutomaticTattooTunerInitialState = FProjectAutomaticTattooRuntimeDebugState();
	DestroyAutomaticTattooTunerPreviewCamera();
	RestoreAutomaticTattooTunerInputCapture();
#endif
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerPreviewChanged(
	const FProjectAutomaticTattooTableRow& EditedRow)
{
#if !UE_BUILD_SHIPPING
	PendingAutomaticTattooTunerPreviewRow = EditedRow;
	bHasPendingAutomaticTattooTunerPreview = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		FlushAutomaticTattooTunerPreview();
		return;
	}

	if (!World->GetTimerManager().IsTimerActive(AutomaticTattooTunerPreviewTimerHandle))
	{
		World->GetTimerManager().SetTimer(
			AutomaticTattooTunerPreviewTimerHandle,
			this,
			&ThisClass::FlushAutomaticTattooTunerPreview,
			ProjectGameplayDebugSubsystemPrivate::AutomaticTattooTunerPreviewDebounceSeconds,
			false);
	}
#else
	(void)EditedRow;
#endif
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerApplyRequested(
	const FProjectAutomaticTattooTableRow& EditedRow)
{
#if UE_BUILD_SHIPPING
	(void)EditedRow;
#else
	UWorld* World = GetWorld();
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	if (!TattooSubsystem || ActiveAutomaticTattooTunerRow.IsNone())
	{
		CloseAutomaticTattooTuner(false);
		return;
	}

	if (World)
	{
		World->GetTimerManager().ClearTimer(AutomaticTattooTunerPreviewTimerHandle);
	}
	bHasPendingAutomaticTattooTunerPreview = false;
	TattooSubsystem->SetAutomaticTattooRuntimeDebugPlacement(TrackedPlayerPawn, ActiveAutomaticTattooTunerRow, EditedRow);
	FProjectGameplayDebugCommandExecutor::CopyAutomaticTattooPlacementValues(TrackedPlayerPawn, ActiveAutomaticTattooTunerRow);
	CloseAutomaticTattooTuner(true);
#endif
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerCancelRequested()
{
	CloseAutomaticTattooTuner(false);
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerViewRequested(
	const EProjectAutomaticTattooTunerCameraView View)
{
	SetAutomaticTattooTunerCameraView(View);
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerOrbitRequested(const FVector2D& PointerDelta)
{
	AutomaticTattooTunerCameraRotation.Yaw += PointerDelta.X * 0.24f;
	AutomaticTattooTunerCameraRotation.Pitch = FMath::Clamp(
		AutomaticTattooTunerCameraRotation.Pitch - PointerDelta.Y * 0.18f,
		-80.0f,
		35.0f);
	UpdateAutomaticTattooTunerPreviewCameraTransform();
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerPanRequested(const FVector2D& PointerDelta)
{
	const FVector RightVector = FRotationMatrix(AutomaticTattooTunerCameraRotation).GetUnitAxis(EAxis::Y);
	const FVector UpVector = FRotationMatrix(AutomaticTattooTunerCameraRotation).GetUnitAxis(EAxis::Z);
	AutomaticTattooTunerCameraFocusWorldLocation += (-RightVector * PointerDelta.X + UpVector * PointerDelta.Y) * 0.14f;
	UpdateAutomaticTattooTunerPreviewCameraTransform();
}

void UProjectGameplayDebugSubsystem::HandleAutomaticTattooTunerZoomRequested(const float WheelDelta)
{
	AutomaticTattooTunerCameraDistance = FMath::Clamp(
		AutomaticTattooTunerCameraDistance - WheelDelta * 26.0f,
		80.0f,
		800.0f);
	UpdateAutomaticTattooTunerPreviewCameraTransform();
}

void UProjectGameplayDebugSubsystem::FlushAutomaticTattooTunerPreview()
{
#if !UE_BUILD_SHIPPING
	if (!bAutomaticTattooTunerOpen || !bHasPendingAutomaticTattooTunerPreview || ActiveAutomaticTattooTunerRow.IsNone())
	{
		return;
	}

	bHasPendingAutomaticTattooTunerPreview = false;
	UWorld* World = GetWorld();
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	if (TattooSubsystem)
	{
		TattooSubsystem->SetAutomaticTattooRuntimeDebugPlacement(
			TrackedPlayerPawn,
			ActiveAutomaticTattooTunerRow,
			PendingAutomaticTattooTunerPreviewRow);
	}
#endif
}

bool UProjectGameplayDebugSubsystem::CreateAutomaticTattooTunerPreviewCamera()
{
	if (!TrackedPlayerController || !TrackedPlayerPawn)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	DestroyAutomaticTattooTunerPreviewCamera();

	const FCharacterCreationCameraSettings& CameraSettings = UEFCharacterCreationSettings::Get()->FullBodyCamera;
	AutomaticTattooTunerCameraFocusWorldLocation =
		ProjectGameplayDebugSubsystemPrivate::CalculateAutomaticTattooTunerFocusWorldLocation(TrackedPlayerPawn, CameraSettings);
	AutomaticTattooTunerCameraDistance = FMath::Max(10.0f, CameraSettings.Distance);
	AutomaticTattooTunerCameraRotation =
		ProjectGameplayDebugSubsystemPrivate::MakeAutomaticTattooTunerCameraRotation(TrackedPlayerPawn, CameraSettings);

	const FVector CameraLocation =
		AutomaticTattooTunerCameraFocusWorldLocation - AutomaticTattooTunerCameraRotation.Vector() * AutomaticTattooTunerCameraDistance;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = TrackedPlayerController;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	AutomaticTattooTunerPreviewCameraActor = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(),
		CameraLocation,
		AutomaticTattooTunerCameraRotation,
		SpawnParameters);
	if (!AutomaticTattooTunerPreviewCameraActor)
	{
		return false;
	}

#if WITH_EDITOR
	AutomaticTattooTunerPreviewCameraActor->SetActorLabel(TEXT("AutomaticTattooTunerPreviewCamera"));
#endif

	if (UCameraComponent* CameraComponent = AutomaticTattooTunerPreviewCameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(CameraSettings.FieldOfView);
		CameraComponent->bUsePawnControlRotation = false;
	}

	AutomaticTattooTunerPreviousViewTarget = TrackedPlayerController->GetViewTarget();
	TrackedPlayerController->SetViewTargetWithBlend(AutomaticTattooTunerPreviewCameraActor, CameraSettings.BlendTime);
	UpdateAutomaticTattooTunerPreviewCameraTransform();
	return true;
}

void UProjectGameplayDebugSubsystem::DestroyAutomaticTattooTunerPreviewCamera()
{
	if (TrackedPlayerController && AutomaticTattooTunerPreviousViewTarget)
	{
		TrackedPlayerController->SetViewTargetWithBlend(AutomaticTattooTunerPreviousViewTarget, 0.20f);
	}
	else if (TrackedPlayerController && TrackedPlayerPawn)
	{
		TrackedPlayerController->SetViewTargetWithBlend(TrackedPlayerPawn, 0.20f);
	}

	if (AutomaticTattooTunerPreviewCameraActor)
	{
		AutomaticTattooTunerPreviewCameraActor->Destroy();
		AutomaticTattooTunerPreviewCameraActor = nullptr;
	}
	AutomaticTattooTunerPreviousViewTarget = nullptr;
}

void UProjectGameplayDebugSubsystem::UpdateAutomaticTattooTunerPreviewCameraTransform()
{
	if (!AutomaticTattooTunerPreviewCameraActor)
	{
		return;
	}

	const FVector CameraLocation =
		AutomaticTattooTunerCameraFocusWorldLocation - AutomaticTattooTunerCameraRotation.Vector() * AutomaticTattooTunerCameraDistance;
	AutomaticTattooTunerPreviewCameraActor->SetActorLocation(CameraLocation);
	AutomaticTattooTunerPreviewCameraActor->SetActorRotation(AutomaticTattooTunerCameraRotation);
}

void UProjectGameplayDebugSubsystem::SetAutomaticTattooTunerCameraView(
	const EProjectAutomaticTattooTunerCameraView View)
{
	if (!TrackedPlayerPawn)
	{
		return;
	}

	const float PawnYaw = TrackedPlayerPawn->GetActorRotation().Yaw;
	const float Pitch = UEFCharacterCreationSettings::Get()->FullBodyCamera.PitchOffset;
	switch (View)
	{
	case EProjectAutomaticTattooTunerCameraView::Front:
		AutomaticTattooTunerCameraRotation = FRotator(Pitch, PawnYaw + 180.0f, 0.0f);
		break;
	case EProjectAutomaticTattooTunerCameraView::Back:
		AutomaticTattooTunerCameraRotation = FRotator(Pitch, PawnYaw, 0.0f);
		break;
	case EProjectAutomaticTattooTunerCameraView::Left:
		AutomaticTattooTunerCameraRotation = FRotator(Pitch, PawnYaw + 90.0f, 0.0f);
		break;
	case EProjectAutomaticTattooTunerCameraView::Right:
		AutomaticTattooTunerCameraRotation = FRotator(Pitch, PawnYaw - 90.0f, 0.0f);
		break;
	default:
		break;
	}

	UpdateAutomaticTattooTunerPreviewCameraTransform();
}

void UProjectGameplayDebugSubsystem::ApplyAutomaticTattooTunerHudSuppression()
{
	if (UWorld* World = GetWorld())
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			AutomaticTattooTunerStateSnapshot.bHasSavedProjectHudVisibility = true;
			AutomaticTattooTunerStateSnapshot.bWasProjectHudVisible = NeedsSubsystem->IsNeedsHudVisible();
			NeedsSubsystem->SetNeedsHudVisible(false);
		}
	}

	if (!TrackedPlayerController)
	{
		return;
	}

	if (AHUD* HudActor = TrackedPlayerController->GetHUD())
	{
		AutomaticTattooTunerStateSnapshot.bHasSavedPlayerHudVisibility = true;
		AutomaticTattooTunerStateSnapshot.bWasPlayerHudVisible = HudActor->bShowHUD;
		HudActor->bShowHUD = false;
		AutomaticTattooTunerStateSnapshot.bAppliedAcfHudDisable = TrySetReflectedHudEnabled(HudActor, false);
	}
}

void UProjectGameplayDebugSubsystem::RestoreAutomaticTattooTunerHudSuppression()
{
	if (TrackedPlayerController && AutomaticTattooTunerStateSnapshot.bHasSavedPlayerHudVisibility)
	{
		if (AHUD* HudActor = TrackedPlayerController->GetHUD())
		{
			HudActor->bShowHUD = AutomaticTattooTunerStateSnapshot.bWasPlayerHudVisible;

			if (AutomaticTattooTunerStateSnapshot.bAppliedAcfHudDisable)
			{
				TrySetReflectedHudEnabled(HudActor, AutomaticTattooTunerStateSnapshot.bWasPlayerHudVisible);
			}
		}
	}

	if (AutomaticTattooTunerStateSnapshot.bHasSavedProjectHudVisibility)
	{
		if (UWorld* World = GetWorld())
		{
			if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
			{
				NeedsSubsystem->SetNeedsHudVisible(AutomaticTattooTunerStateSnapshot.bWasProjectHudVisible);
			}
		}
	}
}

bool UProjectGameplayDebugSubsystem::TrySetReflectedHudEnabled(AHUD* HudActor, const bool bEnabled) const
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

	ProjectGameplayDebugSubsystemPrivate::FSetHudEnabledParams Parameters;
	Parameters.bEnabled = bEnabled;
	HudActor->ProcessEvent(SetHudEnabledFunction, &Parameters);
	return true;
}

void UProjectGameplayDebugSubsystem::ApplyAutomaticTattooTunerInputCapture()
{
	AutomaticTattooTunerStateSnapshot = FProjectGameplayDebugMenuStateSnapshot();
	ApplyAutomaticTattooTunerHudSuppression();

	if (TrackedPlayerController)
	{
		AutomaticTattooTunerStateSnapshot.bHasSavedControllerState = true;
		AutomaticTattooTunerStateSnapshot.bWasMoveInputIgnored = TrackedPlayerController->IsMoveInputIgnored();
		AutomaticTattooTunerStateSnapshot.bWasLookInputIgnored = TrackedPlayerController->IsLookInputIgnored();
		AutomaticTattooTunerStateSnapshot.bWasMouseCursorVisible = TrackedPlayerController->bShowMouseCursor;
		AutomaticTattooTunerStateSnapshot.bWereClickEventsEnabled = TrackedPlayerController->bEnableClickEvents;
		AutomaticTattooTunerStateSnapshot.bWereMouseOverEventsEnabled = TrackedPlayerController->bEnableMouseOverEvents;

		TrackedPlayerController->FlushPressedKeys();
		if (!AutomaticTattooTunerStateSnapshot.bWasMoveInputIgnored)
		{
			TrackedPlayerController->SetIgnoreMoveInput(true);
			AutomaticTattooTunerStateSnapshot.bAppliedMoveInputIgnore = true;
		}
		if (!AutomaticTattooTunerStateSnapshot.bWasLookInputIgnored)
		{
			TrackedPlayerController->SetIgnoreLookInput(true);
			AutomaticTattooTunerStateSnapshot.bAppliedLookInputIgnore = true;
		}

		TrackedPlayerController->DisableInput(TrackedPlayerController);
		TrackedPlayerController->bShowMouseCursor = true;
		TrackedPlayerController->bEnableClickEvents = true;
		TrackedPlayerController->bEnableMouseOverEvents = true;

		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(TrackedAutomaticTattooTunerWidget ? TrackedAutomaticTattooTunerWidget->TakeWidget() : TSharedPtr<SWidget>());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		TrackedPlayerController->SetInputMode(InputMode);
	}

	if (TrackedPlayerPawn)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			Character->StopJumping();
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				AutomaticTattooTunerStateSnapshot.bHasSavedMovementState = true;
				AutomaticTattooTunerStateSnapshot.PreviousMovementMode = CharacterMovement->MovementMode;
				AutomaticTattooTunerStateSnapshot.PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
				CharacterMovement->StopMovementImmediately();
				CharacterMovement->DisableMovement();
			}
		}

		if (TrackedPlayerController)
		{
			TrackedPlayerPawn->DisableInput(TrackedPlayerController);
			AutomaticTattooTunerStateSnapshot.bPawnInputSuspended = true;
		}
	}
}

void UProjectGameplayDebugSubsystem::RestoreAutomaticTattooTunerInputCapture()
{
	RestoreAutomaticTattooTunerHudSuppression();

	if (TrackedPlayerPawn && AutomaticTattooTunerStateSnapshot.bPawnInputSuspended)
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

	if (TrackedPlayerPawn && AutomaticTattooTunerStateSnapshot.bHasSavedMovementState)
	{
		if (ACharacter* Character = Cast<ACharacter>(TrackedPlayerPawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->SetMovementMode(
					AutomaticTattooTunerStateSnapshot.PreviousMovementMode,
					AutomaticTattooTunerStateSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (TrackedPlayerController && AutomaticTattooTunerStateSnapshot.bHasSavedControllerState)
	{
		TrackedPlayerController->EnableInput(TrackedPlayerController);

		if (AutomaticTattooTunerStateSnapshot.bAppliedMoveInputIgnore)
		{
			TrackedPlayerController->SetIgnoreMoveInput(false);
		}
		if (AutomaticTattooTunerStateSnapshot.bAppliedLookInputIgnore)
		{
			TrackedPlayerController->SetIgnoreLookInput(false);
		}

		TrackedPlayerController->bShowMouseCursor = AutomaticTattooTunerStateSnapshot.bWasMouseCursorVisible;
		TrackedPlayerController->bEnableClickEvents = AutomaticTattooTunerStateSnapshot.bWereClickEventsEnabled;
		TrackedPlayerController->bEnableMouseOverEvents = AutomaticTattooTunerStateSnapshot.bWereMouseOverEventsEnabled;

		FInputModeGameOnly InputMode;
		TrackedPlayerController->SetInputMode(InputMode);
		TrackedPlayerController->FlushPressedKeys();
	}

	AutomaticTattooTunerStateSnapshot = FProjectGameplayDebugMenuStateSnapshot();
}

#undef LOCTEXT_NAMESPACE
