#include "EFLevelFlowSubsystem.h"

#include "Calysto/EFCalystoDungeonSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFLevelFlowLoadingTheme.h"
#include "EFLevelFlowSettings.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "ACFAIController.h"
#include "Components/ACFThreatManagerComponent.h"
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
	WorldBeginPlayHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UEFLevelFlowSubsystem::HandlePostWorldInitialization);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UEFLevelFlowSubsystem::HandleWorldCleanup);
	LoadingThemeChangedHandle = EFLevelFlowLoadingTheme::OnThemeChanged().AddUObject(
		this,
		&ThisClass::HandleLoadingThemeChanged);
}

void UEFLevelFlowSubsystem::Deinitialize()
{
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
	if (!IsValid(World) || !World->IsGameWorld())
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
	LoadingSnapshot.ActiveWorld = World;
	LoadingSnapshot.PlayerController = PlayerController;
	LoadingSnapshot.Pawn = Pawn;
	LoadingSnapshot.LoadingStartTimeSeconds = World->GetTimeSeconds();

	EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);

	if (Settings->bBlockPlayerInputDuringLoading)
	{
		ApplyLoadingInputState(PlayerController, true);
	}

	if (Settings->bFreezePawnDuringLoading)
	{
		FreezePawn(Pawn, true);
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

	if (const UEFCalystoDungeonSubsystem* DungeonSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr)
	{
		const FEFCalystoDungeonSnapshotV4 DungeonSnapshot = DungeonSubsystem->GetSnapshot();
		if (DungeonSnapshot.State == EEFCalystoDungeonRunStateV4::Failed)
		{
			const FString Diagnostic = FString::Printf(
				TEXT("Dungeon failure [%s] %s | intent=%s | attempt=%d/%d"),
				*DungeonSnapshot.FailureCode.ToString(),
				*DungeonSnapshot.FailureMessage,
				*DungeonSnapshot.IntentHash,
				DungeonSnapshot.CurrentAttempt,
				DungeonSnapshot.MaximumAttempts);
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

	const double LoadingElapsedSeconds = World->GetTimeSeconds() - LoadingSnapshot.LoadingStartTimeSeconds;
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

bool UEFLevelFlowSubsystem::TryResolveDungeonEntryTransform(UWorld* World, APawn* Pawn, FTransform& OutTransform) const
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return false;
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
	UWorld* World = WorldPtr.Get();
	APlayerController* PlayerController = PlayerControllerPtr.Get();
	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(World) || !IsValid(PlayerController) || !IsValid(Pawn))
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
	const UEFLevelFlowSettings* Settings = UEFLevelFlowSettings::Get();

	if (UWorld* World = LoadingSnapshot.ActiveWorld.Get())
	{
		World->GetTimerManager().ClearTimer(LoadingTimerHandle);
	}

	if (bRestoreGameplayState)
	{
		if (APlayerController* PlayerController = LoadingSnapshot.PlayerController.Get())
		{
			EFLevelFlowPrivate::ClearHeldCameraFade(PlayerController);

			if (Settings->bBlockPlayerInputDuringLoading)
			{
				ApplyLoadingInputState(PlayerController, false);
			}
		}

		if (APawn* Pawn = LoadingSnapshot.Pawn.Get())
		{
			if (Settings->bFreezePawnDuringLoading)
			{
				FreezePawn(Pawn, false);
			}
		}
	}

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
	PlayerController->SetIgnoreMoveInput(LoadingSnapshot.bWasMoveInputIgnored);
	PlayerController->SetIgnoreLookInput(LoadingSnapshot.bWasLookInputIgnored);
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

	if (UClass* LoadingScreenClass = Settings->LoadingScreenWidgetClass.LoadSynchronous())
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
						.Text(NSLOCTEXT("EFLevelFlow", "DungeonLoadingTitle", "Generando dungeon..."))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
						.ColorAndOpacity(LoadingTheme.TitleText)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(24.0f, 0.0f, 24.0f, 16.0f))
					[
						SAssignNew(EFLevelFlowPrivate::ActiveLoadingSubtitle, STextBlock)
						.Text(NSLOCTEXT("EFLevelFlow", "DungeonLoadingSubtitle", "Preparando el punto de inicio"))
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
