#include "EFBlinkPlayerSubsystem.h"

#include "EFBlinkMorphComponent.h"
#include "EFBlinkRuntime.h"
#include "EFBlinkSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void UEFBlinkPlayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	TrackedBlinkComponent = nullptr;
	bNeedsMaintenanceTick = true;
}

void UEFBlinkPlayerSubsystem::Deinitialize()
{
	DetachFromTrackedPlayerController();
	bNeedsMaintenanceTick = false;

	Super::Deinitialize();
}

void UEFBlinkPlayerSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	(void)DeltaTime;

	if (!bNeedsMaintenanceTick)
	{
		return;
	}

	bNeedsMaintenanceTick = !TryResolveRuntimeContext();
}

TStatId UEFBlinkPlayerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEFBlinkPlayerSubsystem, STATGROUP_Tickables);
}

bool UEFBlinkPlayerSubsystem::IsTickable() const
{
	const UEFBlinkSettings* Settings = GetDefault<UEFBlinkSettings>();
	const UWorld* World = GetWorld();
	return Settings
		&& Settings->bAutoAttachToLocalPlayer
		&& World
		&& World->IsGameWorld()
		&& bNeedsMaintenanceTick;
}

bool UEFBlinkPlayerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UEFBlinkPlayerSubsystem::TryResolveRuntimeContext()
{
	const UEFBlinkSettings* Settings = GetDefault<UEFBlinkSettings>();
	if (!Settings || !Settings->bAutoAttachToLocalPlayer)
	{
		DetachFromTrackedPlayerController();
		return true;
	}

	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	AttachToPlayerController(PlayerController);
	EnsureBlinkComponent(PlayerController->GetPawn());

	return TrackedPlayerController != nullptr
		&& TrackedPawn != nullptr
		&& TrackedBlinkComponent != nullptr;
}

void UEFBlinkPlayerSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		if (TrackedPawn != PlayerController->GetPawn())
		{
			HandlePossessedPawnChanged(TrackedPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController();
	TrackedPlayerController = PlayerController;
	TrackedPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandlePossessedPawnChanged);
}

void UEFBlinkPlayerSubsystem::DetachFromTrackedPlayerController()
{
	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	TrackedBlinkComponent = nullptr;
	TrackedPawn = nullptr;
	TrackedPlayerController = nullptr;
}

void UEFBlinkPlayerSubsystem::EnsureBlinkComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedPawn = nullptr;
		TrackedBlinkComponent = nullptr;
		return;
	}

	TrackedPawn = Pawn;

	UEFBlinkMorphComponent* BlinkComponent = Pawn->FindComponentByClass<UEFBlinkMorphComponent>();
	const bool bCreatedComponent = BlinkComponent == nullptr;
	if (!BlinkComponent)
	{
		BlinkComponent = NewObject<UEFBlinkMorphComponent>(
			Pawn,
			UEFBlinkMorphComponent::StaticClass(),
			TEXT("EFBlinkMorphComponent"));

		if (BlinkComponent)
		{
			BlinkComponent->ApplySettingsFromDefaults();
			Pawn->AddInstanceComponent(BlinkComponent);
			BlinkComponent->OnComponentCreated();
			BlinkComponent->RegisterComponent();
			BlinkComponent->Activate(true);

			UE_LOG(LogEFBlink, Log, TEXT("[Blink] Attached morph pulse component to pawn %s."), *GetNameSafe(Pawn));
		}
	}
	else
	{
		if (!BlinkComponent->IsRegistered())
		{
			BlinkComponent->RegisterComponent();
		}

		if (!BlinkComponent->IsActive())
		{
			BlinkComponent->Activate(true);
		}
	}

	TrackedBlinkComponent = BlinkComponent;
	if (TrackedBlinkComponent)
	{
		TrackedBlinkComponent->RefreshTargetMesh();
		if (TrackedBlinkComponent->ShouldStartEnabled() && !TrackedBlinkComponent->IsBlinkRunning())
		{
			TrackedBlinkComponent->StartBlink();
		}
	}
	else if (bCreatedComponent)
	{
		UE_LOG(LogEFBlink, Warning, TEXT("[Blink] Could not attach morph pulse component to pawn %s."), *GetNameSafe(Pawn));
	}
}

void UEFBlinkPlayerSubsystem::MarkMaintenanceRequired()
{
	bNeedsMaintenanceTick = true;
}

void UEFBlinkPlayerSubsystem::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	(void)OldPawn;

	TrackedPawn = NewPawn;
	TrackedBlinkComponent = nullptr;
	MarkMaintenanceRequired();
}
