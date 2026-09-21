#include "EFLevelFlowSubsystem.h"

#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDirectorSubsystem.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFLevelFlowLoadingTheme.h"
#include "EFLevelFlowSettings.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "ACFAIController.h"
#include "Components/ACFThreatManagerComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFLevelFlow, Log, All);

namespace EFLevelFlowPrivate
{
	static TStrongObjectPtr<UUserWidget> ActiveLoadingWidget;
	static TSharedPtr<SWidget> ActiveLoadingOverlay;
	static TSharedPtr<SBorder> ActiveLoadingBorder;
	static TSharedPtr<STextBlock> ActiveLoadingTitle;
	static TSharedPtr<STextBlock> ActiveLoadingSubtitle;
	static TSharedPtr<SThrobber> ActiveLoadingThrobber;
	static TSharedPtr<FSlateBrush> ActiveLoadingThrobberBrush;
	static constexpr float DungeonEntryEnemyIgnoreDurationSeconds = 10.0f;

	static void ApplyLoadingTheme(const FEFLevelFlowLoadingTheme& Theme)
	{
		if (ActiveLoadingBorder.IsValid())
		{
			ActiveLoadingBorder->SetBorderBackgroundColor(FSlateColor(Theme.PanelBackground));
		}

		if (ActiveLoadingTitle.IsValid())
		{
			ActiveLoadingTitle->SetColorAndOpacity(FSlateColor(Theme.TitleText));
		}

		if (ActiveLoadingSubtitle.IsValid())
		{
			ActiveLoadingSubtitle->SetColorAndOpacity(FSlateColor(Theme.SecondaryText));
		}

		if (ActiveLoadingThrobberBrush.IsValid())
		{
			ActiveLoadingThrobberBrush->TintColor = FSlateColor(Theme.ActivityIndicator);
			if (ActiveLoadingThrobber.IsValid())
			{
				ActiveLoadingThrobber->InvalidatePieceImage();
			}
		}
	}

	static void ResetLoadingSlateReferences()
	{
		ActiveLoadingBorder.Reset();
		ActiveLoadingTitle.Reset();
		ActiveLoadingSubtitle.Reset();
		ActiveLoadingThrobber.Reset();
		ActiveLoadingThrobberBrush.Reset();
	}

	static FString NormalizeMapName(const FString& PackageName)
	{
		FString ShortMapName = FPackageName::GetShortName(PackageName);
		if (ShortMapName.StartsWith(TEXT("UEDPIE_")))
		{
			TArray<FString> NameParts;
			ShortMapName.ParseIntoArray(NameParts, TEXT("_"), true);
			if (NameParts.Num() >= 3)
			{
				NameParts.RemoveAt(0, 2);
				ShortMapName = FString::Join(NameParts, TEXT("_"));
			}
		}

		return ShortMapName;
	}

	static bool MatchesManagedMapName(const FString& ShortMapName, const FString& ManagedMapName)
	{
		return ShortMapName.Equals(ManagedMapName, ESearchCase::IgnoreCase)
			|| ShortMapName.StartsWith(ManagedMapName + TEXT("_"), ESearchCase::IgnoreCase);
	}

	static void ClearHeldCameraFade(APlayerController* PlayerController)
	{
		if (!IsValid(PlayerController))
		{
			return;
		}

		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StopCameraFade();
			CameraManager->StartCameraFade(0.0f, 0.0f, 0.0f, FLinearColor::Black, false, false);
		}
	}

}

void UEFLevelFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UEFProceduralRuntimeSubsystem>();
	Super::Initialize(Collection);
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		Collection.InitializeDependency<UEFCalystoDirectorSubsystem>();
		if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>())
		{
			Director->BeforeTravel.AddUObject(this, &ThisClass::HandleDirectorBeforeTravel);
			Director->RequestFailed.AddUObject(this, &ThisClass::HandleDirectorRequestFailed);
		}
	}
	WorldBeginPlayHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UEFLevelFlowSubsystem::HandlePostWorldInitialization);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UEFLevelFlowSubsystem::HandleWorldCleanup);
	LoadingThemeChangedHandle = EFLevelFlowLoadingTheme::OnThemeChanged().AddUObject(
		this,
		&ThisClass::HandleLoadingThemeChanged);
}

void UEFLevelFlowSubsystem::Deinitialize()
{
	if (GetGameInstance())
		if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>())
		{
			Director->BeforeTravel.RemoveAll(this);
			Director->RequestFailed.RemoveAll(this);
		}
	if (LoadingThemeChangedHandle.IsValid())
	{
		EFLevelFlowLoadingTheme::OnThemeChanged().Remove(LoadingThemeChangedHandle);
		LoadingThemeChangedHandle.Reset();
	}
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldBeginPlayHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	ClearDungeonEntryGracePeriod(false);
	ResetLevelLoadingSequence(false);
	Super::Deinitialize();
}

void UEFLevelFlowSubsystem::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	if (!IsValid(World) || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	if (ShouldDelaySpawnForWorld(World))
	{
		UE_LOG(LogEFLevelFlow, Log, TEXT("Scheduling level loading sequence for world %s."), *World->GetName());
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UEFLevelFlowSubsystem::TryStartLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0));
	}
}

void UEFLevelFlowSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (IsValid(World))
	{
		WarnedDerivedMapWorlds.Remove(TObjectKey<UWorld>(World));
	}

	if (LoadingSnapshot.bIsActive && LoadingSnapshot.ActiveWorld.Get() == World)
	{
		ResetLevelLoadingSequence(false);
	}

	if (DungeonEntryGraceWorld.Get() == World)
	{
		ClearDungeonEntryGracePeriod(false);
	}
}

void UEFLevelFlowSubsystem::TryStartLevelLoadingSequence(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex)
{
	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();

	if (!WorldPtr.IsValid() || LoadingSnapshot.bIsActive)
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;

	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		if (AttemptIndex < Settings->MaxResolveAttempts)
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UEFLevelFlowSubsystem::TryStartLevelLoadingSequence, WorldPtr, AttemptIndex + 1));
		}
		else
		{
			UE_LOG(LogEFLevelFlow, Warning, TEXT("Level loading sequence aborted for world %s: missing PlayerController or Pawn."), *World->GetName());
		}

		return;
	}

	StartLevelLoadingSequence(World, PlayerController, Pawn);
}

bool UEFLevelFlowSubsystem::ShouldDelaySpawnForWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}

	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();
	if (UEFCalystoDirectorSettings::IsEnabled())
		if (const auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>())
			if (Director->IsDungeonWorld(World)) return true;
	const FString WorldPackageName = World->GetPackage()->GetName();
	const FString ShortMapName = EFLevelFlowPrivate::NormalizeMapName(WorldPackageName);

	for (const FString& MapName : Settings->DelayedSpawnMapNames)
	{
		if (EFLevelFlowPrivate::MatchesManagedMapName(ShortMapName, MapName))
		{
			if (!ShortMapName.Equals(MapName, ESearchCase::IgnoreCase))
			{
				const TObjectKey<UWorld> WorldKey(World);
				if (!WarnedDerivedMapWorlds.Contains(WorldKey))
				{
					WarnedDerivedMapWorlds.Add(WorldKey);
					UE_LOG(
						LogEFLevelFlow,
						Warning,
						TEXT("World %s matched delayed-spawn family %s via prefix. Consider moving inspection maps out of the runtime map folder."),
						*ShortMapName,
						*MapName);
				}
			}

			return true;
		}
	}

	return false;
}

void UEFLevelFlowSubsystem::StartLevelLoadingSequence(UWorld* World, APlayerController* PlayerController, APawn* Pawn)
{
	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();

	if (LoadingSnapshot.bIsActive || !IsValid(World) || !IsValid(PlayerController) || !IsValid(Pawn))
	{
		return;
	}

	LoadingSnapshot = FLevelLoadingSessionSnapshot();
	LoadingSnapshot.bIsActive = true;
	LoadingSnapshot.DirectorLoadingRequest = FGuid::NewGuid();
	LoadingSnapshot.ActiveWorld = World;
	LoadingSnapshot.PlayerController = PlayerController;
	LoadingSnapshot.Pawn = Pawn;
	LoadingSnapshot.LoadingStartTimeSeconds = FPlatformTime::Seconds();
	const auto* Director = UEFCalystoDirectorSettings::IsEnabled()
		? GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>() : nullptr;
	LoadingSnapshot.bDirectorManaged = UEFCalystoDirectorSettings::IsEnabled()
		&& ((!Director) || Director->IsDungeonWorld(World) || Director->IsTravelRequestPending()
			|| Director->GetSnapshot().State == EEFCalystoDirectorState::Failed
			|| Director->GetSnapshot().State == EEFCalystoDirectorState::Cancelled);

	EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);

	if (Settings->bBlockPlayerInputDuringLoading || LoadingSnapshot.bDirectorManaged)
	{
		ApplyLoadingInputState(PlayerController, true);
		LoadingSnapshot.bInputBlocked = true;
	}

	if (Settings->bFreezePawnDuringLoading || LoadingSnapshot.bDirectorManaged)
	{
		FreezePawn(Pawn, true);
		LoadingSnapshot.bPawnFrozen = true;
	}

	if (LoadingSnapshot.bDirectorManaged)
	{
		LoadingSnapshot.bPreviousCanBeDamaged = Pawn->CanBeDamaged();
		Pawn->SetCanBeDamaged(false);
		if (auto* DamageHandler = Pawn->FindComponentByClass<UACFDamageHandlerComponent>())
		{
			LoadingSnapshot.ProtectedDamageHandler = DamageHandler;
			LoadingSnapshot.bPreviousAcfImmortal = DamageHandler->GetIsImmortal();
			DamageHandler->SetIsImmortal(true);
		}
	}
	ShowLoadingScreen(PlayerController);

	World->GetTimerManager().SetTimer(
		LoadingTimerHandle,
		FTimerDelegate::CreateUObject(this, &UEFLevelFlowSubsystem::TryFinishLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0),
		Settings->LoadingPollIntervalSeconds,
		false);
}

void UEFLevelFlowSubsystem::TryFinishLevelLoadingSequence(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex)
{
	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();

	if (!LoadingSnapshot.bIsActive)
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	APawn* Pawn = LoadingSnapshot.Pawn.Get();
	APlayerController* PlayerController = LoadingSnapshot.PlayerController.Get();
	if (LoadingSnapshot.bDirectorManaged)
	{
		if (!IsValid(World) || World != LoadingSnapshot.ActiveWorld.Get()) return;
		if (!IsValid(Pawn) || !IsValid(PlayerController))
		{
			if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>()) Director->RequestCancel();
			ShowDirectorFailure(NSLOCTEXT("EFLevelFlow", "DirectorPlayerUnavailable", "The player is unavailable. Return to HUB to continue."));
			return;
		}
		TryFinishDirectorLoading(World, PlayerController, Pawn);
		return;
	}

	if (!IsValid(World) || !IsValid(Pawn) || !IsValid(PlayerController))
	{
		if (AttemptIndex < Settings->MaxResolveAttempts && IsValid(World))
		{
			World->GetTimerManager().SetTimer(
				LoadingTimerHandle,
				FTimerDelegate::CreateUObject(this, &UEFLevelFlowSubsystem::TryFinishLevelLoadingSequence, WorldPtr, AttemptIndex + 1),
				Settings->LoadingPollIntervalSeconds,
				false);
		}
		else
		{
			UE_LOG(LogEFLevelFlow, Warning, TEXT("Level loading sequence aborted in world %s because the PlayerController or Pawn became invalid."),
				IsValid(World) ? *World->GetName() : TEXT("None"));
			ResetLevelLoadingSequence(true);
		}

		return;
	}

	if (const UEFCalystoDungeonSubsystem* DungeonSubsystem = !UEFCalystoDirectorSettings::IsEnabled() && GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr)
	{
		const FEFCalystoDungeonSnapshotV6 DungeonSnapshot = DungeonSubsystem->GetSnapshot();
		if (DungeonSnapshot.State == EEFCalystoDungeonTravelStateV6::Failed)
		{
			const FString Diagnostic = FString::Printf(
				TEXT("Dungeon failure: %s | intent=%s | floor=%lld | serial=%lld"),
				*DungeonSnapshot.FailureReason,
				*DungeonSnapshot.FloorIntentHash,
				DungeonSnapshot.FloorNumber,
				DungeonSnapshot.GenerationSerial);
			UE_LOG(LogEFLevelFlow, Error, TEXT("%s"), *Diagnostic);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 12.0f, FColor::Red, Diagnostic);
			}
			ResetLevelLoadingSequence(true);
			return;
		}
	}

	UEFProceduralRuntimeSubsystem* ProceduralSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>() : nullptr;

	if (!LoadingSnapshot.bPawnPositioned && ProceduralSubsystem)
	{
		FTransform StartTransform;
		if (TryResolveDungeonEntryTransform(World, Pawn, StartTransform))
		{
			const FVector TargetLocation = StartTransform.GetLocation();
			const FRotator TargetRotation = StartTransform.Rotator();
			Pawn->SetActorLocationAndRotation(TargetLocation, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
			PlayerController->SetControlRotation(TargetRotation);
			LoadingSnapshot.bPawnPositioned = true;

			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
				{
					CharacterMovement->StopMovementImmediately();
				}
			}
		}
	}

	const bool bRuntimeReady = ProceduralSubsystem
		? ProceduralSubsystem->IsLevelRuntimeReady(World)
		: LoadingSnapshot.bPawnPositioned;
	const bool bVisualReady = LoadingSnapshot.bPawnPositioned
		&& bRuntimeReady
		&& IsDungeonEntryVisualReady(World, PlayerController, Pawn);

	const double LoadingElapsedSeconds = FPlatformTime::Seconds() - LoadingSnapshot.LoadingStartTimeSeconds;
	const bool bMinimumLoadingTimeReached = LoadingElapsedSeconds >= Settings->MinimumLoadingScreenSeconds;

	if (!bVisualReady && LoadingSnapshot.bPawnPositioned && bRuntimeReady)
	{
		if (LoadingSnapshot.VisualRepairAttemptCount < 3)
		{
			++LoadingSnapshot.VisualRepairAttemptCount;
			RepairDungeonEntryVisualState(World, PlayerController, Pawn);
		}
	}

	if (bVisualReady && bMinimumLoadingTimeReached)
	{
		BeginDungeonEntryGracePeriod(World, Pawn);
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
			this,
			&UEFLevelFlowSubsystem::RefreshDungeonEntryVisualState,
			TWeakObjectPtr<UWorld>(World),
			TWeakObjectPtr<APlayerController>(PlayerController),
			TWeakObjectPtr<APawn>(Pawn),
			0));
		ResetLevelLoadingSequence(true);
		return;
	}

	if (AttemptIndex < Settings->MaxResolveAttempts)
	{
		World->GetTimerManager().SetTimer(
			LoadingTimerHandle,
			FTimerDelegate::CreateUObject(this, &UEFLevelFlowSubsystem::TryFinishLevelLoadingSequence, WorldPtr, AttemptIndex + 1),
			Settings->LoadingPollIntervalSeconds,
			false);
		return;
	}

	UE_LOG(LogEFLevelFlow, Warning, TEXT("Level loading sequence timed out in world %s after %d attempts while waiting for start/runtime readiness."),
		*World->GetName(),
		AttemptIndex);
	ResetLevelLoadingSequence(true);
}

void UEFLevelFlowSubsystem::HandleDirectorBeforeTravel(const int64 FloorNumber)
{
	(void)FloorNumber;
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;
	if (LoadingSnapshot.bIsActive)
	{
		if (!LoadingSnapshot.bDirectorManaged)
		{
			// An existing HUB loading session may have started before the request.
			// Promote its ownership once, preserving every state we must restore.
			LoadingSnapshot.bDirectorManaged = true;
			if (auto* Controller = LoadingSnapshot.PlayerController.Get())
			{
				if (!LoadingSnapshot.bInputBlocked)
				{
					ApplyLoadingInputState(Controller, true);
					LoadingSnapshot.bInputBlocked = true;
				}
			}
			if (auto* Pawn = LoadingSnapshot.Pawn.Get())
			{
				if (!LoadingSnapshot.bPawnFrozen)
				{
					FreezePawn(Pawn, true);
					LoadingSnapshot.bPawnFrozen = true;
				}
				LoadingSnapshot.bPreviousCanBeDamaged = Pawn->CanBeDamaged();
				Pawn->SetCanBeDamaged(false);
				if (auto* DamageHandler = Pawn->FindComponentByClass<UACFDamageHandlerComponent>())
				{
					LoadingSnapshot.ProtectedDamageHandler = DamageHandler;
					LoadingSnapshot.bPreviousAcfImmortal = DamageHandler->GetIsImmortal();
					DamageHandler->SetIsImmortal(true);
				}
			}
		}
		LoadingSnapshot.DirectorLoadingRequest = FGuid::NewGuid();
		LoadingSnapshot.bPawnPositioned = false;
		LoadingSnapshot.bFailureVisible = false;
		LoadingSnapshot.VisualRepairAttemptCount = 0;
		LoadingSnapshot.LoadingStartTimeSeconds = FPlatformTime::Seconds();
		HideLoadingScreen();
		if (auto* Controller = LoadingSnapshot.PlayerController.Get())
		{
			Controller->bShowMouseCursor = false;
			ShowLoadingScreen(Controller);
		}
		World->GetTimerManager().SetTimer(LoadingTimerHandle,
			FTimerDelegate::CreateUObject(this, &ThisClass::TryFinishLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0), 0.1f, false);
		return;
	}
	ClearDungeonEntryGracePeriod(true);
	TryStartLevelLoadingSequence(World, 0);
}

void UEFLevelFlowSubsystem::HandleDirectorRequestFailed(const FEFCalystoDirectorSnapshot& Snapshot)
{
	(void)Snapshot;
	if (!LoadingSnapshot.bIsActive) TryStartLevelLoadingSequence(GetWorld(), 0);
	if (LoadingSnapshot.bDirectorManaged)
		ShowDirectorFailure(NSLOCTEXT("EFLevelFlow", "DirectorRequestFailed", "The dungeon could not be prepared. Retry or return to HUB."));
}

void UEFLevelFlowSubsystem::TryFinishDirectorLoading(UWorld* World, APlayerController* PlayerController, APawn* Pawn)
{
	auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
	if (!Director)
	{
		ShowDirectorFailure(NSLOCTEXT("EFLevelFlow", "DirectorUnavailable", "The dungeon is unavailable. Return to HUB to continue."));
		return;
	}
	const auto State = Director->GetSnapshot().State;
	if (State == EEFCalystoDirectorState::Failed || State == EEFCalystoDirectorState::Cancelled || State == EEFCalystoDirectorState::Idle)
	{
		ShowDirectorFailure(NSLOCTEXT("EFLevelFlow", "DirectorRequestFailed", "The dungeon could not be prepared. Retry or return to HUB."));
		return;
	}
	const FGuid LoadingRequest = LoadingSnapshot.DirectorLoadingRequest;
	const FEFCalystoAttemptToken ObservedToken = Director->GetTransaction()
		? Director->GetTransaction()->GetToken() : FEFCalystoAttemptToken();
	const auto StillOwnsLoading = [this, Director, World, Pawn, LoadingRequest, ObservedToken]
	{
		return LoadingSnapshot.bIsActive && LoadingSnapshot.DirectorLoadingRequest == LoadingRequest
			&& LoadingSnapshot.ActiveWorld.Get() == World && LoadingSnapshot.Pawn.Get() == Pawn
			&& Director->GetTransaction() && Director->GetTransaction()->Owns(ObservedToken);
	};
	const auto RejectEntry = [this, Director, World, StillOwnsLoading](const FName Code, const FString& Message)
	{
		if (!StillOwnsLoading()) return;
		Director->RejectPlayerRelease(World, Code, Message);
		if (!StillOwnsLoading()) return;
		LoadingSnapshot.bPawnPositioned = false;
		LoadingSnapshot.VisualRepairAttemptCount = 0;
		World->GetTimerManager().SetTimer(LoadingTimerHandle,
			FTimerDelegate::CreateUObject(this, &ThisClass::TryFinishLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0), 0.1f, false);
	};
	if (State == EEFCalystoDirectorState::Generating || State == EEFCalystoDirectorState::Recovering)
	{
		LoadingSnapshot.bPawnPositioned = false;
		LoadingSnapshot.VisualRepairAttemptCount = 0;
	}
	// The transaction publishes one exact capsule-center transform after native
	// verification. Position and view must succeed before atomic commitment.
	if ((State == EEFCalystoDirectorState::AwaitingPlayerRelease || State == EEFCalystoDirectorState::Ready)
		&& Director->IsDungeonWorld(World))
	{
		FTransform Entry;
		if (!Director->ResolvePlayerStartTransform(World, Entry))
		{
			RejectEntry(TEXT("EntryUnavailable"), TEXT("The dungeon entrance is unavailable."));
			return;
		}
		if (!LoadingSnapshot.bPawnPositioned)
		{
			Pawn->SetActorLocationAndRotation(Entry.GetLocation(), Entry.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
			if (!Pawn->GetActorLocation().Equals(Entry.GetLocation(), 0.1)
				|| !Pawn->GetActorQuat().Equals(Entry.GetRotation(), 0.001))
			{
				RejectEntry(TEXT("EntryPlacementFailed"), TEXT("The player could not enter the dungeon."));
				return;
			}
			PlayerController->SetControlRotation(Entry.Rotator());
			LoadingSnapshot.bPawnPositioned = true;
		}
		if (IsDungeonEntryVisualReady(World, PlayerController, Pawn))
		{
			FString Error;
			if (State == EEFCalystoDirectorState::AwaitingPlayerRelease)
			{
				const auto Release = Director->ConfirmPlayerRelease(World, Pawn, Error);
				if (Release == EEFCalystoPlayerReleaseResult::Pending)
				{
					// This observer uses a one-shot timer. Pending must schedule its next
					// observation while retaining the same protected pawn/request ownership.
					if (StillOwnsLoading()) World->GetTimerManager().SetTimer(LoadingTimerHandle,
						FTimerDelegate::CreateUObject(this, &ThisClass::TryFinishLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0), 0.1f, false);
					return;
				}
				if (Release == EEFCalystoPlayerReleaseResult::Rejected)
				{
					RejectEntry(TEXT("PlayerReleaseRejected"), Error);
					return;
				}
			}
			// FloorReady listeners may synchronously request another floor or HUB.
			// The old confirmation must never release the new request's protection.
			if (!StillOwnsLoading() || Director->IsTravelRequestPending()
				|| Director->GetSnapshot().State != EEFCalystoDirectorState::Ready
				|| !Director->IsLevelRuntimeReady(World)) return;
			BeginDungeonEntryGracePeriod(World, Pawn);
			ResetLevelLoadingSequence(true);
			return;
		}
		if (++LoadingSnapshot.VisualRepairAttemptCount > 3)
		{
			RejectEntry(TEXT("PlayerViewUnavailable"), TEXT("The player view is unavailable."));
			return;
		}
		RepairDungeonEntryVisualState(World, PlayerController, Pawn);
	}
	// The Director owns the shared 30-second deadline and cleanup. This watchdog
	// catches a disconnected owner without releasing the protected pawn.
	if (FPlatformTime::Seconds() - LoadingSnapshot.LoadingStartTimeSeconds > 35.0)
	{
		if (State == EEFCalystoDirectorState::AwaitingPlayerRelease)
			Director->RejectPlayerRelease(World, TEXT("PlayerReleaseTimeout"), TEXT("The player could not enter before the loading deadline."));
		else Director->RequestCancel();
		ShowDirectorFailure(NSLOCTEXT("EFLevelFlow", "DirectorLoadingTimeout", "The dungeon did not finish loading. Return to HUB to continue."));
		return;
	}
	World->GetTimerManager().SetTimer(LoadingTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::TryFinishLevelLoadingSequence, TWeakObjectPtr<UWorld>(World), 0), 0.1f, false);
}

void UEFLevelFlowSubsystem::ShowDirectorFailure(const FText& Message)
{
	if (LoadingSnapshot.bFailureVisible) return;
	LoadingSnapshot.bFailureVisible = true;
	if (UWorld* World = LoadingSnapshot.ActiveWorld.Get()) World->GetTimerManager().ClearTimer(LoadingTimerHandle);
	HideLoadingScreen();
	if (!GEngine || !GEngine->GameViewport) return;
	const TWeakObjectPtr<UEFCalystoDirectorSubsystem> Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
	TSharedPtr<SButton> ReturnHubButton;
	EFLevelFlowPrivate::ActiveLoadingOverlay = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.035f, 1.0f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(32.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 20.0f)
			[SNew(STextBlock).Text(Message).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f)
				[SNew(SButton).Text(NSLOCTEXT("EFLevelFlow", "DirectorRetry", "Retry"))
					.IsEnabled_Lambda([Director] { return Director.IsValid() && Director->GetSnapshot().bCanRetry; })
					.OnClicked_UObject(this, &ThisClass::RetryDirectorLoading)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f)
				[SAssignNew(ReturnHubButton, SButton).Text(NSLOCTEXT("EFLevelFlow", "DirectorReturnHub", "Return to HUB"))
					.IsEnabled_Lambda([Director] { return Director.IsValid() && Director->GetSnapshot().bCanReturnToHub; })
					.OnClicked_UObject(this, &ThisClass::ReturnDirectorToHub)]
			]
		];
	GEngine->GameViewport->AddViewportWidgetContent(EFLevelFlowPrivate::ActiveLoadingOverlay.ToSharedRef(), 10000);
	if (auto* Controller = LoadingSnapshot.PlayerController.Get())
	{
		Controller->bShowMouseCursor = true;
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ReturnHubButton);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(InputMode);
	}
}

FReply UEFLevelFlowSubsystem::RetryDirectorLoading()
{
	if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>()) Director->RequestRetry();
	return FReply::Handled();
}

FReply UEFLevelFlowSubsystem::ReturnDirectorToHub()
{
	if (auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>())
		if (Director->RequestReturnToHub())
		{
			HideLoadingScreen();
			if (auto* Controller = LoadingSnapshot.PlayerController.Get()) ShowLoadingScreen(Controller);
		}
	return FReply::Handled();
}

bool UEFLevelFlowSubsystem::TryResolveDungeonEntryTransform(UWorld* World, APawn* Pawn, FTransform& OutTransform) const
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return false;
	}

	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		const auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
		return Director && Director->ResolvePlayerStartTransform(World, OutTransform);
	}
	const UEFProceduralRuntimeSubsystem* ProceduralSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>() : nullptr;
	if (!ProceduralSubsystem)
	{
		return false;
	}

	FTransform CandidateTransform;
	if (!ProceduralSubsystem->ResolvePlayerStartTransform(World, CandidateTransform))
	{
		return false;
	}

	if (FindFloorAdjustedDungeonTransform(World, Pawn, CandidateTransform, OutTransform))
	{
		return true;
	}

	UE_LOG(
		LogEFLevelFlow,
		Warning,
		TEXT("Dungeon start transform for %s resolved at %s but no nearby blocking floor was found. Waiting for a safe procedural start."),
		*GetNameSafe(Pawn),
		*CandidateTransform.GetLocation().ToCompactString());
	return false;
}

bool UEFLevelFlowSubsystem::FindFloorAdjustedDungeonTransform(
	UWorld* World,
	APawn* Pawn,
	const FTransform& CandidateTransform,
	FTransform& OutTransform) const
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return false;
	}

	float HalfHeight = 88.0f;
	if (const UCapsuleComponent* CapsuleComponent = Pawn->FindComponentByClass<UCapsuleComponent>())
	{
		HalfHeight = FMath::Max(CapsuleComponent->GetScaledCapsuleHalfHeight(), 1.0f);
	}

	const FVector CandidateLocation = CandidateTransform.GetLocation();
	TArray<FVector> SampleOffsets;
	SampleOffsets.Reserve(41);
	SampleOffsets.Add(FVector::ZeroVector);
	for (const float Radius : { 160.0f, 320.0f, 640.0f, 960.0f, 1280.0f })
	{
		SampleOffsets.Add(FVector(Radius, 0.0f, 0.0f));
		SampleOffsets.Add(FVector(-Radius, 0.0f, 0.0f));
		SampleOffsets.Add(FVector(0.0f, Radius, 0.0f));
		SampleOffsets.Add(FVector(0.0f, -Radius, 0.0f));
		SampleOffsets.Add(FVector(Radius, Radius, 0.0f));
		SampleOffsets.Add(FVector(-Radius, Radius, 0.0f));
		SampleOffsets.Add(FVector(Radius, -Radius, 0.0f));
		SampleOffsets.Add(FVector(-Radius, -Radius, 0.0f));
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFLevelFlowDungeonEntryFloor), false);
	QueryParams.AddIgnoredActor(Pawn);

	for (const FVector& Offset : SampleOffsets)
	{
		const FVector TraceOrigin = CandidateLocation + Offset;
		const FVector TraceStart = TraceOrigin + FVector(0.0f, 0.0f, 1000.0f);
		const FVector TraceEnd = TraceOrigin - FVector(0.0f, 0.0f, 6000.0f);

		FHitResult HitResult;
		bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
		if (!bHit)
		{
			bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);
		}
		if (!bHit)
		{
			bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Camera, QueryParams);
		}
		if (!bHit)
		{
			bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Pawn, QueryParams);
		}

		if (!bHit || !HitResult.bBlockingHit)
		{
			continue;
		}

		const float VerticalDelta = CandidateLocation.Z - HitResult.ImpactPoint.Z;
		if (VerticalDelta < -250.0f || VerticalDelta > 1600.0f)
		{
			continue;
		}

		OutTransform = CandidateTransform;
		FVector AdjustedLocation = HitResult.ImpactPoint;
		AdjustedLocation.Z += HalfHeight + 4.0f;
		OutTransform.SetLocation(AdjustedLocation);
		return true;
	}

	return false;
}

bool UEFLevelFlowSubsystem::IsDungeonEntryVisualReady(UWorld* World, APlayerController* PlayerController, APawn* Pawn) const
{
	if (!IsValid(World) || !IsValid(PlayerController) || !IsValid(Pawn))
	{
		return false;
	}

	if (PlayerController->GetPawn() != Pawn)
	{
		return false;
	}

	APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
	if (!IsValid(CameraManager))
	{
		return false;
	}

	AActor* ViewTarget = PlayerController->GetViewTarget();
	if (!IsValid(ViewTarget) || ViewTarget->GetWorld() != World)
	{
		return false;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	const FVector CameraLocation = CameraManager->GetCameraLocation();
	if (PawnLocation.ContainsNaN() || CameraLocation.ContainsNaN())
	{
		return false;
	}

	if (FVector::DistSquared(PawnLocation, CameraLocation) > FMath::Square(20000.0f))
	{
		return false;
	}

	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
		FTransform Entry;
		// Structural floor, capsule and route evidence belongs to the Director.
		// Visual readiness checks the actual protected player at its published entry.
		return Director && World == GetWorld() && World->GetGameInstance() == GetGameInstance()
			&& PlayerController->GetWorld() == World && Pawn->GetWorld() == World
			&& CameraManager->GetWorld() == World && ViewTarget == Pawn
			&& UGameplayStatics::GetPlayerController(World, 0) == PlayerController
			&& Pawn->GetController() == PlayerController
			&& LoadingSnapshot.bIsActive && LoadingSnapshot.bDirectorManaged
			&& LoadingSnapshot.ActiveWorld.Get() == World && LoadingSnapshot.Pawn.Get() == Pawn
			&& LoadingSnapshot.PlayerController.Get() == PlayerController
			&& Director->IsLevelRuntimeReady(World)
			&& Director->ResolvePlayerStartTransform(World, Entry)
			&& PawnLocation.Equals(Entry.GetLocation(), 0.1)
			&& Pawn->GetActorQuat().Equals(Entry.GetRotation(), 0.001);
	}

	FTransform FloorAdjustedTransform;
	if (FindFloorAdjustedDungeonTransform(World, Pawn, Pawn->GetActorTransform(), FloorAdjustedTransform))
	{
		return true;
	}

	UE_LOG(
		LogEFLevelFlow,
		Verbose,
		TEXT("Dungeon visual readiness rejected %s because no blocking floor was found under or near the pawn."),
		*GetNameSafe(Pawn));
	return false;
}

void UEFLevelFlowSubsystem::RepairDungeonEntryVisualState(UWorld* World, APlayerController* PlayerController, APawn* Pawn)
{
	if (!IsValid(World) || !IsValid(PlayerController) || !IsValid(Pawn))
	{
		return;
	}

	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		auto* Director = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
		FTransform Entry;
		if (!Director || World != GetWorld() || !LoadingSnapshot.bIsActive || !LoadingSnapshot.bDirectorManaged
			|| LoadingSnapshot.ActiveWorld.Get() != World || LoadingSnapshot.Pawn.Get() != Pawn
			|| LoadingSnapshot.PlayerController.Get() != PlayerController
			|| PlayerController->GetWorld() != World || Pawn->GetWorld() != World
			|| UGameplayStatics::GetPlayerController(World, 0) != PlayerController
			|| PlayerController->GetPawn() != Pawn || Pawn->GetController() != PlayerController
			|| !Director->IsLevelRuntimeReady(World) || !Director->ResolvePlayerStartTransform(World, Entry)) return;
		// Positioning happens once in the current attempt's release handshake.
		// Camera repair must never move the pawn or resolve another floor location.
		EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);
		PlayerController->SetControlRotation(Entry.Rotator());
		PlayerController->SetViewTarget(Pawn);
		return;
	}

	EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);

	if (PlayerController->GetPawn() != Pawn && !Pawn->GetController())
	{
		PlayerController->Possess(Pawn);
	}

	if (!IsValid(PlayerController->GetViewTarget()) || PlayerController->GetViewTarget()->GetWorld() != World)
	{
		PlayerController->SetViewTarget(Pawn);
	}

	FTransform StartTransform;
	if (TryResolveDungeonEntryTransform(World, Pawn, StartTransform))
	{
		Pawn->SetActorLocationAndRotation(
			StartTransform.GetLocation(),
			StartTransform.Rotator(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		PlayerController->SetControlRotation(StartTransform.Rotator());
		PlayerController->SetViewTarget(Pawn);

		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->StopMovementImmediately();
			}
		}

		UE_LOG(
			LogEFLevelFlow,
			Log,
			TEXT("Dungeon entry visual state repaired for %s at %s."),
			*GetNameSafe(Pawn),
			*StartTransform.GetLocation().ToCompactString());
	}
}

void UEFLevelFlowSubsystem::RefreshDungeonEntryVisualState(
	TWeakObjectPtr<UWorld> WorldPtr,
	TWeakObjectPtr<APlayerController> PlayerControllerPtr,
	TWeakObjectPtr<APawn> PawnPtr,
	int32 AttemptIndex)
{
	// This delayed legacy callback has no attempt token. Candidate visual work is
	// completed synchronously by its owned loading poll before player release.
	if (UEFCalystoDirectorSettings::IsEnabled()) return;
	UWorld* World = WorldPtr.Get();
	APlayerController* PlayerController = PlayerControllerPtr.Get();
	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(World) || !IsValid(PlayerController) || !IsValid(Pawn)
		|| LoadingSnapshot.bIsActive || World != GetWorld() || PlayerController->GetWorld() != World
		|| Pawn->GetWorld() != World || PlayerController->GetPawn() != Pawn)
	{
		return;
	}

	EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);
	if (!IsDungeonEntryVisualReady(World, PlayerController, Pawn) && AttemptIndex < 2)
	{
		RepairDungeonEntryVisualState(World, PlayerController, Pawn);

		FTimerHandle RetryHandle;
		World->GetTimerManager().SetTimer(
			RetryHandle,
			FTimerDelegate::CreateUObject(
				this,
				&UEFLevelFlowSubsystem::RefreshDungeonEntryVisualState,
				WorldPtr,
				PlayerControllerPtr,
				PawnPtr,
				AttemptIndex + 1),
			0.5f,
			false);
		return;
	}

	if (!IsDungeonEntryVisualReady(World, PlayerController, Pawn))
	{
		UE_LOG(
			LogEFLevelFlow,
			Warning,
			TEXT("Dungeon entry visual state remained invalid after refresh attempts. World=%s Pawn=%s ViewTarget=%s"),
			*GetNameSafe(World),
			*GetNameSafe(Pawn),
			*GetNameSafe(PlayerController->GetViewTarget()));
	}
}

void UEFLevelFlowSubsystem::ResetLevelLoadingSequence(bool bRestoreGameplayState)
{
	if (UWorld* World = LoadingSnapshot.ActiveWorld.Get())
	{
		World->GetTimerManager().ClearTimer(LoadingTimerHandle);
	}

	if (bRestoreGameplayState)
	{
		if (APlayerController* PlayerController = LoadingSnapshot.PlayerController.Get())
		{
			EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);

			if (LoadingSnapshot.bInputBlocked)
			{
				ApplyLoadingInputState(PlayerController, false);
			}
		}

		if (APawn* Pawn = LoadingSnapshot.Pawn.Get())
		{
			if (LoadingSnapshot.bPawnFrozen)
			{
				FreezePawn(Pawn, false);
			}
			if (LoadingSnapshot.bDirectorManaged) Pawn->SetCanBeDamaged(LoadingSnapshot.bPreviousCanBeDamaged);
		}
	}

	if (bRestoreGameplayState)
		if (auto* DamageHandler = LoadingSnapshot.ProtectedDamageHandler.Get())
			DamageHandler->SetIsImmortal(LoadingSnapshot.bPreviousAcfImmortal);
	HideLoadingScreen();
	LoadingTimerHandle.Invalidate();
	LoadingSnapshot = FLevelLoadingSessionSnapshot();
}

void UEFLevelFlowSubsystem::BeginDungeonEntryGracePeriod(UWorld* World, APawn* Pawn)
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return;
	}

	ClearDungeonEntryGracePeriod(true);

	UAIPerceptionStimuliSourceComponent* StimuliSource = Pawn->FindComponentByClass<UAIPerceptionStimuliSourceComponent>();
	if (!IsValid(StimuliSource))
	{
		UE_LOG(LogEFLevelFlow, Warning, TEXT("Dungeon entry grace period skipped for %s because no AI perception stimuli source was found."), *Pawn->GetName());
		return;
	}

	DungeonEntryGracePawn = Pawn;
	DungeonEntryGraceWorld = World;
	DungeonEntryGraceStimuliSource = StimuliSource;
	StimuliSource->UnregisterFromSense(UAISense_Sight::StaticClass());
	ClearEnemyAwarenessOfPawn(World, Pawn);

	World->GetTimerManager().SetTimer(
		DungeonEntryGraceTimerHandle,
		this,
		&UEFLevelFlowSubsystem::EndDungeonEntryGracePeriod,
		EFLevelFlowPrivate::DungeonEntryEnemyIgnoreDurationSeconds,
		false);

	UE_LOG(
		LogEFLevelFlow,
		Log,
		TEXT("Dungeon entry grace period started for %s. Enemies will ignore the player for %.1f seconds."),
		*Pawn->GetName(),
		EFLevelFlowPrivate::DungeonEntryEnemyIgnoreDurationSeconds);
}

void UEFLevelFlowSubsystem::EndDungeonEntryGracePeriod()
{
	ClearDungeonEntryGracePeriod(true);
}

void UEFLevelFlowSubsystem::ClearDungeonEntryGracePeriod(bool bRestoreSight)
{
	if (UWorld* GraceWorld = DungeonEntryGraceWorld.Get())
	{
		GraceWorld->GetTimerManager().ClearTimer(DungeonEntryGraceTimerHandle);
	}

	if (bRestoreSight)
	{
		if (UAIPerceptionStimuliSourceComponent* StimuliSource = DungeonEntryGraceStimuliSource.Get())
		{
			StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
		}
	}

	DungeonEntryGraceTimerHandle.Invalidate();
	DungeonEntryGraceWorld.Reset();
	DungeonEntryGraceStimuliSource.Reset();
	DungeonEntryGracePawn.Reset();
}

void UEFLevelFlowSubsystem::ClearEnemyAwarenessOfPawn(UWorld* World, APawn* Pawn)
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return;
	}

	for (TActorIterator<AACFAIController> It(World); It; ++It)
	{
		AACFAIController* AIController = *It;
		if (!IsValid(AIController))
		{
			continue;
		}

		UACFThreatManagerComponent* ThreatManager = AIController->GetThreatManager();
		if (AIController->GetTarget() == Pawn)
		{
			AIController->SetTarget(nullptr);
			if (ThreatManager)
			{
				if (AActor* NextTarget = ThreatManager->GetActorWithHigherThreat())
				{
					AIController->SetTarget(NextTarget);
				}
				else
				{
					AIController->ResetToDefaultState();
				}
			}
			else
			{
				AIController->ResetToDefaultState();
			}
			continue;
		}

		if (ThreatManager && ThreatManager->IsThreatening(Pawn))
		{
			ThreatManager->RemoveThreatening(Pawn);
		}
	}
}

void UEFLevelFlowSubsystem::ApplyLoadingInputState(APlayerController* PlayerController, bool bEnableLoadingScreen)
{
	if (!IsValid(PlayerController))
	{
		return;
	}

	if (bEnableLoadingScreen)
	{
		LoadingSnapshot.bWasMouseCursorVisible = PlayerController->bShowMouseCursor;
		LoadingSnapshot.bWasMoveInputIgnored = PlayerController->IsMoveInputIgnored();
		LoadingSnapshot.bWasLookInputIgnored = PlayerController->IsLookInputIgnored();

		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
		PlayerController->DisableInput(PlayerController);
		PlayerController->bShowMouseCursor = false;

		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
		return;
	}

	PlayerController->EnableInput(PlayerController);
	PlayerController->SetIgnoreMoveInput(false);
	PlayerController->SetIgnoreLookInput(false);
	PlayerController->bShowMouseCursor = LoadingSnapshot.bWasMouseCursorVisible;

	FInputModeGameOnly InputMode;
	PlayerController->SetInputMode(InputMode);
}

void UEFLevelFlowSubsystem::FreezePawn(APawn* Pawn, bool bFreeze)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController());
	if (bFreeze)
	{
		if (IsValid(PlayerController))
		{
			Pawn->DisableInput(PlayerController);
		}

		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				LoadingSnapshot.bHadSavedMovementState = true;
				LoadingSnapshot.PreviousMovementMode = CharacterMovement->MovementMode;
				LoadingSnapshot.PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
				LoadingSnapshot.PreviousGravityScale = CharacterMovement->GravityScale;

				Character->StopJumping();
				CharacterMovement->StopMovementImmediately();
				CharacterMovement->GravityScale = 0.0f;
				CharacterMovement->SetMovementMode(MOVE_None);
			}
		}
	}
	else
	{
		if (IsValid(PlayerController))
		{
			Pawn->EnableInput(PlayerController);
		}

		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
			{
				CharacterMovement->GravityScale = LoadingSnapshot.PreviousGravityScale;
				CharacterMovement->StopMovementImmediately();
				if (LoadingSnapshot.bHadSavedMovementState)
				{
					const EMovementMode RestoredMovementMode =
						LoadingSnapshot.PreviousMovementMode == MOVE_None || LoadingSnapshot.PreviousMovementMode == MOVE_Falling
							? MOVE_Walking
							: static_cast<EMovementMode>(LoadingSnapshot.PreviousMovementMode.GetValue());

					CharacterMovement->SetMovementMode(RestoredMovementMode, LoadingSnapshot.PreviousCustomMovementMode);
				}
			}
		}
	}

	EFCharacterCreationGameplayHooks::OnSetPawnCanMove().Broadcast(Pawn, !bFreeze);
}

void UEFLevelFlowSubsystem::ShowLoadingScreen(APlayerController* PlayerController)
{
	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();

	if (!IsValid(PlayerController))
	{
		return;
	}

	// Travel must never synchronously load UI. Use the configured class when it
	// is already resident and fall back to the lightweight Slate overlay below
	// while any broader session preload is still completing.
	if (UClass* LoadingScreenClass = Settings && !LoadingSnapshot.bDirectorManaged
		? Settings->LoadingScreenWidgetClass.Get()
		: nullptr)
	{
		if (UUserWidget* LoadingWidget = CreateWidget<UUserWidget>(PlayerController, LoadingScreenClass))
		{
			LoadingWidget->AddToViewport(10000);
			EFLevelFlowPrivate::ActiveLoadingWidget.Reset(LoadingWidget);
			return;
		}
	}

	if (GEngine && GEngine->GameViewport)
	{
		const FEFLevelFlowLoadingTheme LoadingTheme =
			EFLevelFlowLoadingTheme::ResolveTheme();
		EFLevelFlowPrivate::ActiveLoadingThrobberBrush = MakeShared<FSlateBrush>(
			*FCoreStyle::Get().GetBrush("Throbber.Chunk"));
		EFLevelFlowPrivate::ActiveLoadingThrobberBrush->TintColor =
			FSlateColor(LoadingTheme.ActivityIndicator);

		EFLevelFlowPrivate::ActiveLoadingOverlay =
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(EFLevelFlowPrivate::ActiveLoadingBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(LoadingTheme.PanelBackground)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(24.0f, 18.0f, 24.0f, 8.0f))
					[
						SAssignNew(EFLevelFlowPrivate::ActiveLoadingTitle, STextBlock)
						.Text(NSLOCTEXT("EFLevelFlow", "DungeonLoadingTitle", "Generating dungeon..."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
						.ColorAndOpacity(LoadingTheme.TitleText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(24.0f, 0.0f, 24.0f, 16.0f))
					[
						SAssignNew(EFLevelFlowPrivate::ActiveLoadingSubtitle, STextBlock)
						.Text(NSLOCTEXT("EFLevelFlow", "DungeonLoadingSubtitle", "Preparing your arrival"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						.ColorAndOpacity(LoadingTheme.SecondaryText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 0.0f, 18.0f))
					[
						SAssignNew(EFLevelFlowPrivate::ActiveLoadingThrobber, SThrobber)
						.PieceImage(EFLevelFlowPrivate::ActiveLoadingThrobberBrush.Get())
					]
				]
			];

		GEngine->GameViewport->AddViewportWidgetContent(EFLevelFlowPrivate::ActiveLoadingOverlay.ToSharedRef(), 10000);
	}
}

void UEFLevelFlowSubsystem::HideLoadingScreen()
{
	if (EFLevelFlowPrivate::ActiveLoadingWidget.IsValid())
	{
		EFLevelFlowPrivate::ActiveLoadingWidget->RemoveFromParent();
		EFLevelFlowPrivate::ActiveLoadingWidget.Reset();
	}

	if (EFLevelFlowPrivate::ActiveLoadingOverlay.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(
				EFLevelFlowPrivate::ActiveLoadingOverlay.ToSharedRef());
		}
		EFLevelFlowPrivate::ActiveLoadingOverlay.Reset();
	}

	EFLevelFlowPrivate::ResetLoadingSlateReferences();
}

void UEFLevelFlowSubsystem::HandleLoadingThemeChanged(
	const FEFLevelFlowLoadingTheme& Theme)
{
	EFLevelFlowPrivate::ApplyLoadingTheme(Theme);
}
