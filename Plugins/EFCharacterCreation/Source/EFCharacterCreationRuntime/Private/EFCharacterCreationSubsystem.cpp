#include "EFCharacterCreationSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFCharacterCreationSettings.h"
#include "EFCharacterCustomizationComponent.h"
#include "Camera/CameraActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/GameViewportClient.h"
#include "UI/EFCharacterCreationRootWidget.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterCreation, Log, All);

namespace CharacterCreationSubsystemPrivate
{
	static FString NormalizeComponentName(const UObject* Object)
	{
		return IsValid(Object) ? Object->GetName().ToLower() : FString();
	}

	static int32 ScoreByHints(const UObject* Object, const TArray<FString>& PreferredHints)
	{
		const FString NormalizedName = NormalizeComponentName(Object);
		int32 BestScore = 0;

		for (int32 HintIndex = 0; HintIndex < PreferredHints.Num(); ++HintIndex)
		{
			const FString Hint = PreferredHints[HintIndex].ToLower();
			if (NormalizedName.Equals(Hint, ESearchCase::IgnoreCase))
			{
				return 1000 - HintIndex;
			}

			if (NormalizedName.Contains(Hint))
			{
				BestScore = FMath::Max(BestScore, 500 - HintIndex);
			}
		}

		return BestScore;
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

	static bool DoesMapMatchList(const FString& ShortMapName, const TArray<FString>& CandidateMapNames)
	{
		for (const FString& CandidateMapName : CandidateMapNames)
		{
			if (ShortMapName.Equals(CandidateMapName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static FVector CalculatePreviewFocusWorldLocation(const APawn* Pawn, const FCharacterCreationCameraSettings& CameraSettings)
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

	static FVector CalculatePreviewFocusRelativeLocation(const APawn* Pawn, const FCharacterCreationCameraSettings& CameraSettings)
	{
		if (!IsValid(Pawn))
		{
			return FVector::ZeroVector;
		}

		return Pawn->GetActorTransform().InverseTransformPosition(CalculatePreviewFocusWorldLocation(Pawn, CameraSettings));
	}

	static FRotator MakePreviewCameraRotation(const APawn* Pawn, const FCharacterCreationCameraSettings& CameraSettings)
	{
		const float PawnYaw = IsValid(Pawn) ? Pawn->GetActorRotation().Yaw : 0.0f;
		return FRotator(CameraSettings.PitchOffset, PawnYaw + 180.0f + CameraSettings.YawOffset, 0.0f);
	}

	static float GetPreviewCameraDistance(const FCharacterCreationCameraSettings& CameraSettings)
	{
		return FMath::Max(10.0f, CameraSettings.Distance);
	}
}

void UEFCharacterCreationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldBeginPlayHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UEFCharacterCreationSubsystem::HandlePostWorldInitialization);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UEFCharacterCreationSubsystem::HandleWorldCleanup);
}

void UEFCharacterCreationSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldBeginPlayHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	ReleaseToggleInputBinding();
	CleanupSessionObjects();
	Super::Deinitialize();
}

bool UEFCharacterCreationSubsystem::CanStartForPawn(APawn* Pawn, FString& OutReason)
{
	if (!IsValid(Pawn))
	{
		OutReason = TEXT("No valid pawn was found for character creation.");
		return false;
	}

	UEFCharacterCustomizationComponent* CustomizationComponent = FindOrCreateCustomizationComponent(Pawn);
	if (!IsValid(CustomizationComponent))
	{
		OutReason = TEXT("Failed to create the character customization component.");
		return false;
	}

	if (!CustomizationComponent->EvaluateCompatibilityForActor(Pawn, OutReason))
	{
		return false;
	}

	return true;
}

bool UEFCharacterCreationSubsystem::EnterCharacterCreationMode(APlayerController* PlayerController, APawn* Pawn)
{
	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		UE_LOG(LogCharacterCreation, Warning, TEXT("EnterCharacterCreationMode aborted: invalid PlayerController or Pawn."));
		return false;
	}

	if (bIsCharacterCreationActive)
	{
		UE_LOG(LogCharacterCreation, Verbose, TEXT("EnterCharacterCreationMode skipped because the session is already active."));
		return true;
	}

	FString CompatibilityFailureReason;
	if (!CanStartForPawn(Pawn, CompatibilityFailureReason))
	{
		if (UEFCharacterCreationSettings::Get()->bLogCompatibilityFailures)
		{
			UE_LOG(LogCharacterCreation, Log, TEXT("Character creation skipped for pawn %s: %s"), *GetNameSafe(Pawn), *CompatibilityFailureReason);
		}

		return false;
	}

	EnsureToggleInputBinding(PlayerController);
	ActivePlayerController = PlayerController;
	ActivePawn = Pawn;
	ActiveCustomizationComponent = FindOrCreateCustomizationComponent(Pawn);

	if (!IsValid(ActiveCustomizationComponent.Get()))
	{
		UE_LOG(LogCharacterCreation, Error, TEXT("EnterCharacterCreationMode failed: could not create or find UEFCharacterCustomizationComponent on %s."), *Pawn->GetName());
		return false;
	}

	ActiveCustomizationComponent->InitializeForActor(Pawn);
	UE_LOG(LogCharacterCreation, Log, TEXT("Character creation started for pawn %s. Compatible mesh: %s"), *Pawn->GetName(), ActiveCustomizationComponent->IsMeshCompatible() ? TEXT("true") : TEXT("false"));
	SessionSnapshot.OriginalState = ActiveCustomizationComponent->CaptureCurrentState();
	SessionSnapshot.PreviousViewTarget = PlayerController->GetViewTarget();

	const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
	if (!CreatePreviewCameraActor(PlayerController, Pawn))
	{
		UE_LOG(LogCharacterCreation, Error, TEXT("Failed to create a character creation preview camera for %s."), *Pawn->GetName());
		CleanupSessionObjects();
		return false;
	}

	PlayerController->SetViewTargetWithBlend(ActivePreviewCameraActor.Get(), Settings->FullBodyCamera.BlendTime);
	UE_LOG(LogCharacterCreation, Log, TEXT("Using runtime preview camera actor %s for character creation."), *GetNameSafe(ActivePreviewCameraActor.Get()));

	TSubclassOf<UEFCharacterCreationRootWidget> RootWidgetClass = UEFCharacterCreationRootWidget::StaticClass();
	if (Settings->RootWidgetClass.IsValid() || !Settings->RootWidgetClass.ToSoftObjectPath().IsNull())
	{
		if (UClass* LoadedRootWidgetClass = Settings->RootWidgetClass.LoadSynchronous())
		{
			RootWidgetClass = LoadedRootWidgetClass;
		}
	}

	ActiveRootWidget = CreateWidget<UEFCharacterCreationRootWidget>(PlayerController, RootWidgetClass);
	if (IsValid(ActiveRootWidget))
	{
		ActiveRootWidget->InitializeForSession(this, ActiveCustomizationComponent.Get());
		ActiveRootWidget->AddToViewport(1000);
		UE_LOG(LogCharacterCreation, Log, TEXT("Character creation widget %s added to viewport."), *GetNameSafe(ActiveRootWidget));
	}
	else
	{
		UE_LOG(LogCharacterCreation, Error, TEXT("Failed to create character creation root widget from class %s."), *GetNameSafe(RootWidgetClass));
	}

	ApplyCharacterCreationInputState(PlayerController, true);
	SuspendGameplayForPawn(Pawn, true);
	bIsCharacterCreationActive = true;
	return true;
}

void UEFCharacterCreationSubsystem::ExitCharacterCreationMode(bool bKeepCurrentChanges)
{
	if (!bIsCharacterCreationActive)
	{
		return;
	}

	if (UEFCharacterCustomizationComponent* CustomizationComponent = ActiveCustomizationComponent.Get())
	{
		if (bKeepCurrentChanges)
		{
			CacheRuntimeCustomizationState(CustomizationComponent->CaptureCurrentState());
			CustomizationComponent->SaveCurrentStateAsConfirmed();
		}
		else
		{
			CustomizationComponent->ApplyState(SessionSnapshot.OriginalState);
		}
	}

	if (APlayerController* PlayerController = ActivePlayerController.Get())
	{
		RestorePreviewCameraRig();

		if (AActor* PreviousViewTarget = SessionSnapshot.PreviousViewTarget.Get())
		{
			PlayerController->SetViewTargetWithBlend(PreviousViewTarget, 0.25f);
		}
		else if (APawn* ActivePlayerPawn = ActivePawn.Get())
		{
			PlayerController->SetViewTargetWithBlend(ActivePlayerPawn, 0.25f);
		}

		ApplyCharacterCreationInputState(PlayerController, false);
	}

	if (APawn* ActivePlayerPawn = ActivePawn.Get())
	{
		SuspendGameplayForPawn(ActivePlayerPawn, false);
	}

	CleanupSessionObjects();
	bIsCharacterCreationActive = false;
}

void UEFCharacterCreationSubsystem::HandleStartGameRequested()
{
	ExitCharacterCreationMode(true);
}

void UEFCharacterCreationSubsystem::HandleBackRequested()
{
	ExitCharacterCreationMode(false);
}

bool UEFCharacterCreationSubsystem::OpenCharacterCreationForAutomation()
{
	if (bIsCharacterCreationActive)
	{
		return true;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		UE_LOG(LogCharacterCreation, Warning, TEXT("OpenCharacterCreationForAutomation aborted: missing PlayerController or Pawn."));
		return false;
	}

	return EnterCharacterCreationMode(PlayerController, Pawn);
}

UEFCharacterCreationRootWidget* UEFCharacterCreationSubsystem::GetActiveRootWidgetForAutomation() const
{
	return ActiveRootWidget.Get();
}

void UEFCharacterCreationSubsystem::ToggleCharacterCreationMode()
{
	if (bIsCharacterCreationActive)
	{
		ExitCharacterCreationMode(true);
		return;
	}

	APlayerController* PlayerController = ToggleInputPlayerController.Get();
	if (!IsValid(PlayerController))
	{
		if (UWorld* World = GetWorld())
		{
			PlayerController = UGameplayStatics::GetPlayerController(World, 0);
		}
	}

	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		UE_LOG(LogCharacterCreation, Warning, TEXT("ToggleCharacterCreationMode aborted: missing PlayerController or Pawn."));
		return;
	}

	EnterCharacterCreationMode(PlayerController, Pawn);
}

void UEFCharacterCreationSubsystem::HandleToggleCharacterCreationPressed()
{
	ToggleCharacterCreationMode();
}

void UEFCharacterCreationSubsystem::OrbitPreviewCamera(const FVector2D& PointerDelta)
{
	if (USpringArmComponent* PreviewSpringArm = ActivePreviewSpringArmComponent.Get())
	{
		FRotator RelativeRotation = PreviewSpringArm->GetRelativeRotation();
		RelativeRotation.Yaw += PointerDelta.X * 0.24f;
		RelativeRotation.Pitch = FMath::Clamp(RelativeRotation.Pitch - PointerDelta.Y * 0.18f, -80.0f, 35.0f);
		PreviewSpringArm->SetRelativeRotation(RelativeRotation);
		return;
	}

	if (ACameraActor* PreviewCameraActor = ActivePreviewCameraActor.Get())
	{
		FRotator CameraRotation = PreviewCameraActor->GetActorRotation();
		CameraRotation.Yaw += PointerDelta.X * 0.24f;
		CameraRotation.Pitch = FMath::Clamp(CameraRotation.Pitch - PointerDelta.Y * 0.18f, -80.0f, 35.0f);
		PreviewCameraActor->SetActorRotation(CameraRotation);
		UpdateDirectPreviewCameraTransform();
		return;
	}

	if (UCameraComponent* PreviewCamera = ActivePreviewCameraComponent.Get())
	{
		FRotator CameraRotation = PreviewCamera->GetComponentRotation();
		CameraRotation.Yaw += PointerDelta.X * 0.24f;
		CameraRotation.Pitch = FMath::Clamp(CameraRotation.Pitch - PointerDelta.Y * 0.18f, -80.0f, 35.0f);
		PreviewCamera->SetWorldRotation(CameraRotation);
		UpdateDirectPreviewCameraTransform();
	}
}

void UEFCharacterCreationSubsystem::PanPreviewCamera(const FVector2D& PointerDelta)
{
	if (USpringArmComponent* PreviewSpringArm = ActivePreviewSpringArmComponent.Get())
	{
		FVector SocketOffset = PreviewSpringArm->SocketOffset;
		SocketOffset.Y -= PointerDelta.X * 0.14f;
		SocketOffset.Z += PointerDelta.Y * 0.14f;
		PreviewSpringArm->SocketOffset = SocketOffset;
		return;
	}

	if (ACameraActor* PreviewCameraActor = ActivePreviewCameraActor.Get())
	{
		const FRotator CameraRotation = PreviewCameraActor->GetActorRotation();
		const FVector RightVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
		const FVector UpVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);
		DirectPreviewFocusWorldLocation += (-RightVector * PointerDelta.X + UpVector * PointerDelta.Y) * 0.14f;
		UpdateDirectPreviewCameraTransform();
		return;
	}

	if (UCameraComponent* PreviewCamera = ActivePreviewCameraComponent.Get())
	{
		const FRotator CameraRotation = PreviewCamera->GetComponentRotation();
		const FVector RightVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
		const FVector UpVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);
		DirectPreviewFocusWorldLocation += (-RightVector * PointerDelta.X + UpVector * PointerDelta.Y) * 0.14f;
		UpdateDirectPreviewCameraTransform();
	}
}

void UEFCharacterCreationSubsystem::ZoomPreviewCamera(float WheelDelta)
{
	if (USpringArmComponent* PreviewSpringArm = ActivePreviewSpringArmComponent.Get())
	{
		PreviewSpringArm->TargetArmLength = FMath::Clamp(PreviewSpringArm->TargetArmLength - WheelDelta * 26.0f, 80.0f, 800.0f);
		return;
	}

	if (ActivePreviewCameraComponent.IsValid())
	{
		DirectPreviewCameraDistance = FMath::Clamp(DirectPreviewCameraDistance - WheelDelta * 26.0f, 80.0f, 800.0f);
		UpdateDirectPreviewCameraTransform();
	}
}

bool UEFCharacterCreationSubsystem::CreatePreviewCameraActor(APlayerController* PlayerController, APawn* Pawn)
{
	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		return false;
	}

	UWorld* World = Pawn->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	const FCharacterCreationCameraSettings& CameraSettings = UEFCharacterCreationSettings::Get()->FullBodyCamera;
	DirectPreviewFocusWorldLocation = CharacterCreationSubsystemPrivate::CalculatePreviewFocusWorldLocation(Pawn, CameraSettings);
	DirectPreviewCameraDistance = CharacterCreationSubsystemPrivate::GetPreviewCameraDistance(CameraSettings);

	const FRotator CameraRotation = CharacterCreationSubsystemPrivate::MakePreviewCameraRotation(Pawn, CameraSettings);
	const FVector CameraLocation = DirectPreviewFocusWorldLocation - CameraRotation.Vector() * DirectPreviewCameraDistance;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerController;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	ACameraActor* PreviewCameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation, CameraRotation, SpawnParameters);
	if (!IsValid(PreviewCameraActor))
	{
		return false;
	}

#if WITH_EDITOR
	PreviewCameraActor->SetActorLabel(TEXT("CharacterCreationPreviewCamera"));
#endif
	if (UCameraComponent* PreviewCameraComponent = PreviewCameraActor->GetCameraComponent())
	{
		PreviewCameraComponent->SetFieldOfView(CameraSettings.FieldOfView);
		PreviewCameraComponent->bUsePawnControlRotation = false;
		ActivePreviewCameraComponent = PreviewCameraComponent;
	}

	ActivePreviewCameraActor = PreviewCameraActor;
	ActivePreviewSpringArmComponent = nullptr;
	UpdateDirectPreviewCameraTransform();
	return true;
}

void UEFCharacterCreationSubsystem::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return;
	}

	UE_LOG(LogCharacterCreation, Log, TEXT("Scheduling character creation setup for world %s."), *World->GetName());
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UEFCharacterCreationSubsystem::TryInitializeCharacterCreation, TWeakObjectPtr<UWorld>(World), 0));
}

void UEFCharacterCreationSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return;
	}

	CaptureRuntimeCustomizationStateFromWorld(World);
}

void UEFCharacterCreationSubsystem::TryInitializeCharacterCreation(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex)
{
	if (!WorldPtr.IsValid())
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;

	if (!IsValid(PlayerController) || !IsValid(Pawn))
	{
		if (AttemptIndex < Settings->MaxAutoEnterAttempts)
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UEFCharacterCreationSubsystem::TryInitializeCharacterCreation, WorldPtr, AttemptIndex + 1));
		}
		return;
	}

	EnsureToggleInputBinding(PlayerController);
	ApplyRuntimeCustomizationStateToPawn(Pawn);

	if (ShouldAutoEnterForWorld(World) && !bIsCharacterCreationActive)
	{
		UE_LOG(LogCharacterCreation, Log, TEXT("Auto-entry resolved PlayerController %s and Pawn %s on attempt %d."), *GetNameSafe(PlayerController), *GetNameSafe(Pawn), AttemptIndex);
		EnterCharacterCreationMode(PlayerController, Pawn);
		return;
	}

	if (Settings->bAutoOpenOnCompatibleMainPawn && !bIsCharacterCreationActive)
	{
		FString CompatibilityFailureReason;
		if (CanStartForPawn(Pawn, CompatibilityFailureReason))
		{
			UE_LOG(LogCharacterCreation, Log, TEXT("Auto-opening character creation for compatible pawn %s on attempt %d."), *GetNameSafe(Pawn), AttemptIndex);
			EnterCharacterCreationMode(PlayerController, Pawn);
		}
		else if (Settings->bLogCompatibilityFailures)
		{
			UE_LOG(LogCharacterCreation, Verbose, TEXT("Compatible auto-open skipped for pawn %s: %s"), *GetNameSafe(Pawn), *CompatibilityFailureReason);
		}
	}
}

bool UEFCharacterCreationSubsystem::ShouldAutoEnterForWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}

	const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
	if (!Settings->bAutoEnterTestingMap)
	{
		return false;
	}

	const FString WorldPackageName = World->GetPackage()->GetName();
	const FString ShortMapName = CharacterCreationSubsystemPrivate::NormalizeMapName(WorldPackageName);

	if (CharacterCreationSubsystemPrivate::DoesMapMatchList(ShortMapName, Settings->AutoOpenMapNames))
	{
		return true;
	}

	// Legacy single-map auto-entry remains supported so older configs keep working.
	// Project config can disable it and rely only on AutoOpenMapNames.
	if (!Settings->bAutoEnterTestingMap)
	{
		return false;
	}

	return ShortMapName.Equals(Settings->TestingMapName, ESearchCase::IgnoreCase);
}

UEFCharacterCustomizationComponent* UEFCharacterCreationSubsystem::FindOrCreateCustomizationComponent(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	if (UEFCharacterCustomizationComponent* ExistingComponent = Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>())
	{
		return ExistingComponent;
	}

	UEFCharacterCustomizationComponent* NewComponent = NewObject<UEFCharacterCustomizationComponent>(Pawn, TEXT("CharacterCustomizationComponent"));
	if (!IsValid(NewComponent))
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(NewComponent);
	NewComponent->RegisterComponent();
	return NewComponent;
}

bool UEFCharacterCreationSubsystem::ResolvePreviewCameraRig(APawn* Pawn, UCameraComponent*& OutCameraComponent, USpringArmComponent*& OutSpringArmComponent, TArray<UCameraComponent*>& OutAllCameraComponents) const
{
	OutCameraComponent = nullptr;
	OutSpringArmComponent = nullptr;
	OutAllCameraComponents.Reset();

	if (!IsValid(Pawn))
	{
		return false;
	}

	TArray<UCameraComponent*> CameraComponents;
	Pawn->GetComponents<UCameraComponent>(CameraComponents);
	OutAllCameraComponents = CameraComponents;
	if (CameraComponents.Num() == 0)
	{
		return false;
	}

	TArray<USpringArmComponent*> SpringArmComponents;
	Pawn->GetComponents<USpringArmComponent>(SpringArmComponents);

	const TArray<FString> PreferredSpringArmHints = {
		TEXT("CharacterCreationPreviewSpringArm"),
		TEXT("PreviewSpringArm"),
		TEXT("SpringArm"),
		TEXT("CameraBoom")
	};

	const TArray<FString> PreferredCameraHints = {
		TEXT("CharacterCreationPreviewCamera"),
		TEXT("PreviewCamera"),
		TEXT("Camera"),
		TEXT("FollowCamera")
	};

	int32 BestSpringArmScore = 0;
	for (USpringArmComponent* SpringArmComponent : SpringArmComponents)
	{
		const int32 Score = CharacterCreationSubsystemPrivate::ScoreByHints(SpringArmComponent, PreferredSpringArmHints);
		if (Score > BestSpringArmScore)
		{
			BestSpringArmScore = Score;
			OutSpringArmComponent = SpringArmComponent;
		}
	}

	int32 BestCameraScore = 0;
	for (UCameraComponent* CameraComponent : CameraComponents)
	{
		int32 Score = CharacterCreationSubsystemPrivate::ScoreByHints(CameraComponent, PreferredCameraHints);
		if (OutSpringArmComponent && CameraComponent->GetAttachParent() == OutSpringArmComponent)
		{
			Score += 750;
		}

		if (Score > BestCameraScore)
		{
			BestCameraScore = Score;
			OutCameraComponent = CameraComponent;
		}
	}

	if (!OutCameraComponent)
	{
		OutCameraComponent = CameraComponents[0];
	}

	if (!OutSpringArmComponent)
	{
		OutSpringArmComponent = Cast<USpringArmComponent>(OutCameraComponent->GetAttachParent());
	}

	return IsValid(OutCameraComponent);
}

void UEFCharacterCreationSubsystem::CachePreviewCameraRig(UCameraComponent* CameraComponent, USpringArmComponent* SpringArmComponent, const TArray<UCameraComponent*>& AllCameraComponents)
{
	CachedPawnCameraComponents.Reset();
	CachedPawnCameraActiveStates.Reset();

	for (UCameraComponent* PawnCameraComponent : AllCameraComponents)
	{
		CachedPawnCameraComponents.Add(PawnCameraComponent);
		CachedPawnCameraActiveStates.Add(IsValid(PawnCameraComponent) && PawnCameraComponent->IsActive());
	}

	ActivePreviewCameraComponent = CameraComponent;
	ActivePreviewSpringArmComponent = SpringArmComponent;
	bHasCachedPreviewRig = IsValid(CameraComponent);

	if (IsValid(CameraComponent))
	{
		CachedPreviewCameraRelativeLocation = CameraComponent->GetRelativeLocation();
		CachedPreviewCameraRelativeRotation = CameraComponent->GetRelativeRotation();
		CachedPreviewCameraFieldOfView = CameraComponent->FieldOfView;
		bCachedPreviewCameraUsePawnControlRotation = CameraComponent->bUsePawnControlRotation;
	}

	if (IsValid(SpringArmComponent))
	{
		CachedPreviewSpringArmRelativeLocation = SpringArmComponent->GetRelativeLocation();
		CachedPreviewSpringArmRelativeRotation = SpringArmComponent->GetRelativeRotation();
		CachedPreviewSpringArmSocketOffset = SpringArmComponent->SocketOffset;
		CachedPreviewSpringArmTargetOffset = SpringArmComponent->TargetOffset;
		CachedPreviewSpringArmLength = SpringArmComponent->TargetArmLength;
		bCachedPreviewSpringArmCollisionTest = SpringArmComponent->bDoCollisionTest;
		bCachedPreviewSpringArmUsePawnControlRotation = SpringArmComponent->bUsePawnControlRotation;
	}
}

void UEFCharacterCreationSubsystem::ActivatePreviewCameraRig(APawn* Pawn, UCameraComponent* CameraComponent, USpringArmComponent* SpringArmComponent)
{
	if (!IsValid(CameraComponent))
	{
		return;
	}

	const FCharacterCreationCameraSettings& CameraSettings = UEFCharacterCreationSettings::Get()->FullBodyCamera;
	const FRotator InitialCameraRotation = CharacterCreationSubsystemPrivate::MakePreviewCameraRotation(Pawn, CameraSettings);
	const float PreviewDistance = CharacterCreationSubsystemPrivate::GetPreviewCameraDistance(CameraSettings);

	for (const TWeakObjectPtr<UCameraComponent>& PawnCameraComponent : CachedPawnCameraComponents)
	{
		if (UCameraComponent* ResolvedCameraComponent = PawnCameraComponent.Get())
		{
			ResolvedCameraComponent->SetActive(ResolvedCameraComponent == CameraComponent);
		}
	}

	if (const UCameraComponent* CameraTemplate = Cast<UCameraComponent>(CameraComponent->GetArchetype()))
	{
		CameraComponent->SetRelativeLocation(CameraTemplate->GetRelativeLocation());
		CameraComponent->SetRelativeRotation(CameraTemplate->GetRelativeRotation());
	}
	CameraComponent->bUsePawnControlRotation = false;
	CameraComponent->SetFieldOfView(CameraSettings.FieldOfView);

	if (IsValid(SpringArmComponent))
	{
		SpringArmComponent->SetRelativeLocation(CharacterCreationSubsystemPrivate::CalculatePreviewFocusRelativeLocation(Pawn, CameraSettings));
		SpringArmComponent->SetRelativeRotation(FRotator(CameraSettings.PitchOffset, 180.0f + CameraSettings.YawOffset, 0.0f));
		SpringArmComponent->SocketOffset = FVector::ZeroVector;
		SpringArmComponent->TargetOffset = FVector::ZeroVector;
		SpringArmComponent->TargetArmLength = PreviewDistance;
		SpringArmComponent->bDoCollisionTest = false;
		SpringArmComponent->bUsePawnControlRotation = false;
		return;
	}

	DirectPreviewFocusWorldLocation = CharacterCreationSubsystemPrivate::CalculatePreviewFocusWorldLocation(Pawn, CameraSettings);
	DirectPreviewCameraDistance = PreviewDistance;
	CameraComponent->SetWorldRotation(InitialCameraRotation);
	UpdateDirectPreviewCameraTransform();
}

void UEFCharacterCreationSubsystem::UpdateDirectPreviewCameraTransform()
{
	if (ActivePreviewSpringArmComponent.IsValid())
	{
		return;
	}

	if (DirectPreviewCameraDistance <= 0.0f)
	{
		DirectPreviewCameraDistance = CharacterCreationSubsystemPrivate::GetPreviewCameraDistance(UEFCharacterCreationSettings::Get()->FullBodyCamera);
	}

	if (DirectPreviewFocusWorldLocation.IsNearlyZero())
	{
		DirectPreviewFocusWorldLocation = CharacterCreationSubsystemPrivate::CalculatePreviewFocusWorldLocation(ActivePawn.Get(), UEFCharacterCreationSettings::Get()->FullBodyCamera);
	}

	if (ACameraActor* PreviewCameraActor = ActivePreviewCameraActor.Get())
	{
		const FRotator CameraRotation = PreviewCameraActor->GetActorRotation();
		PreviewCameraActor->SetActorLocation(DirectPreviewFocusWorldLocation - CameraRotation.Vector() * DirectPreviewCameraDistance);
		return;
	}

	UCameraComponent* PreviewCamera = ActivePreviewCameraComponent.Get();
	if (!IsValid(PreviewCamera))
	{
		return;
	}

	const FRotator CameraRotation = PreviewCamera->GetComponentRotation();
	PreviewCamera->SetWorldLocation(DirectPreviewFocusWorldLocation - CameraRotation.Vector() * DirectPreviewCameraDistance);
}

void UEFCharacterCreationSubsystem::RestorePreviewCameraRig()
{
	if (!bHasCachedPreviewRig)
	{
		return;
	}

	for (int32 CameraIndex = 0; CameraIndex < CachedPawnCameraComponents.Num(); ++CameraIndex)
	{
		if (UCameraComponent* PawnCameraComponent = CachedPawnCameraComponents[CameraIndex].Get())
		{
			PawnCameraComponent->SetActive(CachedPawnCameraActiveStates.IsValidIndex(CameraIndex) ? CachedPawnCameraActiveStates[CameraIndex] : false);
		}
	}

	if (UCameraComponent* PreviewCameraComponent = ActivePreviewCameraComponent.Get())
	{
		PreviewCameraComponent->SetRelativeLocation(CachedPreviewCameraRelativeLocation);
		PreviewCameraComponent->SetRelativeRotation(CachedPreviewCameraRelativeRotation);
		PreviewCameraComponent->SetFieldOfView(CachedPreviewCameraFieldOfView);
		PreviewCameraComponent->bUsePawnControlRotation = bCachedPreviewCameraUsePawnControlRotation;
	}

	if (USpringArmComponent* PreviewSpringArmComponent = ActivePreviewSpringArmComponent.Get())
	{
		PreviewSpringArmComponent->SetRelativeLocation(CachedPreviewSpringArmRelativeLocation);
		PreviewSpringArmComponent->SetRelativeRotation(CachedPreviewSpringArmRelativeRotation);
		PreviewSpringArmComponent->SocketOffset = CachedPreviewSpringArmSocketOffset;
		PreviewSpringArmComponent->TargetOffset = CachedPreviewSpringArmTargetOffset;
		PreviewSpringArmComponent->TargetArmLength = CachedPreviewSpringArmLength;
		PreviewSpringArmComponent->bDoCollisionTest = bCachedPreviewSpringArmCollisionTest;
		PreviewSpringArmComponent->bUsePawnControlRotation = bCachedPreviewSpringArmUsePawnControlRotation;
	}

	bHasCachedPreviewRig = false;
	DirectPreviewFocusWorldLocation = FVector::ZeroVector;
	DirectPreviewCameraDistance = 0.0f;
}

void UEFCharacterCreationSubsystem::EnsureToggleInputBinding(APlayerController* PlayerController)
{
	if (!IsValid(PlayerController))
	{
		return;
	}

	if (ToggleInputPlayerController.Get() == PlayerController && IsValid(ToggleInputComponent))
	{
		return;
	}

	ReleaseToggleInputBinding();

	ToggleInputPlayerController = PlayerController;
	ToggleInputComponent = NewObject<UInputComponent>(PlayerController, TEXT("CharacterCreationToggleInput"));
	if (!IsValid(ToggleInputComponent))
	{
		return;
	}

	ToggleInputComponent->RegisterComponent();
	ToggleInputComponent->Priority = 9999;
	ToggleInputComponent->bBlockInput = false;
	ToggleInputComponent->BindKey(EKeys::Period, IE_Pressed, this, &UEFCharacterCreationSubsystem::HandleToggleCharacterCreationPressed);
	PlayerController->PushInputComponent(ToggleInputComponent);
}

void UEFCharacterCreationSubsystem::ReleaseToggleInputBinding()
{
	if (APlayerController* PlayerController = ToggleInputPlayerController.Get())
	{
		if (IsValid(ToggleInputComponent))
		{
			PlayerController->PopInputComponent(ToggleInputComponent);
		}
	}

	ToggleInputComponent = nullptr;
	ToggleInputPlayerController = nullptr;
}

void UEFCharacterCreationSubsystem::ApplyCharacterCreationInputState(APlayerController* PlayerController, bool bEnableCharacterCreation)
{
	if (!IsValid(PlayerController))
	{
		return;
	}

	if (bEnableCharacterCreation)
	{
		SessionSnapshot.bWasMouseCursorVisible = PlayerController->bShowMouseCursor;
		SessionSnapshot.bWereClickEventsEnabled = PlayerController->bEnableClickEvents;
		SessionSnapshot.bWereMouseOverEventsEnabled = PlayerController->bEnableMouseOverEvents;
		SessionSnapshot.bWasMoveInputIgnored = PlayerController->IsMoveInputIgnored();
		SessionSnapshot.bWasLookInputIgnored = PlayerController->IsLookInputIgnored();

		PlayerController->SetCinematicMode(true, false, true, true, true);
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
		PlayerController->DisableInput(PlayerController);
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;

		FInputModeUIOnly InputMode;
		if (IsValid(ActiveRootWidget))
		{
			InputMode.SetWidgetToFocus(ActiveRootWidget->TakeWidget());
		}
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
		return;
	}

	PlayerController->SetCinematicMode(false, false, true, true, true);
	PlayerController->EnableInput(PlayerController);
	PlayerController->SetIgnoreMoveInput(SessionSnapshot.bWasMoveInputIgnored);
	PlayerController->SetIgnoreLookInput(SessionSnapshot.bWasLookInputIgnored);
	PlayerController->bShowMouseCursor = SessionSnapshot.bWasMouseCursorVisible;
	PlayerController->bEnableClickEvents = SessionSnapshot.bWereClickEventsEnabled;
	PlayerController->bEnableMouseOverEvents = SessionSnapshot.bWereMouseOverEventsEnabled;

	FInputModeGameOnly InputMode;
	PlayerController->SetInputMode(InputMode);
}

void UEFCharacterCreationSubsystem::SuspendGameplayForPawn(APawn* Pawn, bool bSuspendGameplay)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		SessionSnapshot.bWasPawnUseControllerRotationYaw = Character->bUseControllerRotationYaw;
		if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement())
		{
			if (bSuspendGameplay)
			{
				SessionSnapshot.bHadSavedMovementState = true;
				SessionSnapshot.PreviousMovementMode = CharacterMovement->MovementMode;
				SessionSnapshot.PreviousCustomMovementMode = CharacterMovement->CustomMovementMode;
				Character->bUseControllerRotationYaw = false;
				Character->StopJumping();
				CharacterMovement->StopMovementImmediately();
				CharacterMovement->DisableMovement();
			}
			else if (SessionSnapshot.bHadSavedMovementState)
			{
				Character->bUseControllerRotationYaw = SessionSnapshot.bWasPawnUseControllerRotationYaw;
				CharacterMovement->SetMovementMode(SessionSnapshot.PreviousMovementMode, SessionSnapshot.PreviousCustomMovementMode);
			}
		}
	}

	if (bSuspendGameplay)
	{
		Pawn->DisableInput(Cast<APlayerController>(Pawn->GetController()));
	}
	else if (APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
	{
		Pawn->EnableInput(PlayerController);
	}

	EFCharacterCreationGameplayHooks::OnSetPawnCanMove().Broadcast(Pawn, !bSuspendGameplay);

	if (bSuspendGameplay)
	{
		EFCharacterCreationGameplayHooks::OnCancelPawnAbilities().Broadcast(Pawn);
	}
}

void UEFCharacterCreationSubsystem::CleanupSessionObjects()
{
	if (IsValid(ActiveRootWidget))
	{
		ActiveRootWidget->RemoveFromParent();
		ActiveRootWidget = nullptr;
	}

	if (ACameraActor* PreviewCameraActor = ActivePreviewCameraActor.Get())
	{
		PreviewCameraActor->Destroy();
	}
	ActivePreviewCameraActor = nullptr;
	ActivePreviewCameraComponent = nullptr;
	ActivePreviewSpringArmComponent = nullptr;
	CachedPawnCameraComponents.Reset();
	CachedPawnCameraActiveStates.Reset();
	CachedPreviewCameraRelativeLocation = FVector::ZeroVector;
	CachedPreviewCameraRelativeRotation = FRotator::ZeroRotator;
	CachedPreviewCameraFieldOfView = 0.0f;
	bCachedPreviewCameraUsePawnControlRotation = false;
	CachedPreviewSpringArmRelativeLocation = FVector::ZeroVector;
	CachedPreviewSpringArmRelativeRotation = FRotator::ZeroRotator;
	CachedPreviewSpringArmSocketOffset = FVector::ZeroVector;
	CachedPreviewSpringArmTargetOffset = FVector::ZeroVector;
	CachedPreviewSpringArmLength = 0.0f;
	bCachedPreviewSpringArmCollisionTest = false;
	bCachedPreviewSpringArmUsePawnControlRotation = false;
	bHasCachedPreviewRig = false;
	DirectPreviewFocusWorldLocation = FVector::ZeroVector;
	DirectPreviewCameraDistance = 0.0f;

	ActiveCustomizationComponent = nullptr;
	ActivePlayerController = nullptr;
	ActivePawn = nullptr;
	SessionSnapshot = FCharacterCreationSessionSnapshot();
}

void UEFCharacterCreationSubsystem::CacheRuntimeCustomizationState(const FCharacterCustomizationState& State)
{
	RuntimeCustomizationState = State;
	bHasRuntimeCustomizationState = true;
}

void UEFCharacterCreationSubsystem::CaptureRuntimeCustomizationStateFromWorld(UWorld* World)
{
	if (!IsValid(World))
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		return;
	}

	UEFCharacterCustomizationComponent* CustomizationComponent = Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>();
	if (!IsValid(CustomizationComponent))
	{
		return;
	}

	if (CustomizationComponent->GetAvailableMorphEntries().Num() == 0)
	{
		CustomizationComponent->InitializeForActor(Pawn);
	}

	if (!CustomizationComponent->IsMeshCompatible())
	{
		return;
	}

	CacheRuntimeCustomizationState(CustomizationComponent->CaptureCurrentState());
	UE_LOG(LogCharacterCreation, Log, TEXT("Cached runtime customization state from pawn %s while cleaning up world %s."), *GetNameSafe(Pawn), *World->GetName());
}

bool UEFCharacterCreationSubsystem::ApplyRuntimeCustomizationStateToPawn(APawn* Pawn)
{
	if (!bHasRuntimeCustomizationState || !IsValid(Pawn))
	{
		return false;
	}

	UEFCharacterCustomizationComponent* CustomizationComponent = FindOrCreateCustomizationComponent(Pawn);
	if (!IsValid(CustomizationComponent))
	{
		return false;
	}

	CustomizationComponent->InitializeForActor(Pawn);
	if (!CustomizationComponent->IsMeshCompatible())
	{
		UE_LOG(LogCharacterCreation, Warning, TEXT("Skipped runtime customization restore for pawn %s because its mesh is not compatible."), *GetNameSafe(Pawn));
		return false;
	}

	CustomizationComponent->ApplyState(RuntimeCustomizationState);
	UE_LOG(LogCharacterCreation, Log, TEXT("Applied cached runtime customization state to pawn %s."), *GetNameSafe(Pawn));
	return true;
}


