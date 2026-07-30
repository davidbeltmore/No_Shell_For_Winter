#include "Locomotion/ProjectLocomotionOverrideSubsystem.h"

#include "EFProjectInputSettings.h"
#include "Engine/World.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"

void UProjectLocomotionOverrideSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	TrackedInputComponent = nullptr;
	TrackedLocomotionComponent = nullptr;
	bNeedsMaintenanceTick = true;
}

void UProjectLocomotionOverrideSubsystem::Deinitialize()
{
	DetachFromTrackedPlayerController();
	Super::Deinitialize();
}

void UProjectLocomotionOverrideSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bNeedsMaintenanceTick)
	{
		return;
	}

	bNeedsMaintenanceTick = !TryResolveRuntimeContext();
}

TStatId UProjectLocomotionOverrideSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectLocomotionOverrideSubsystem, STATGROUP_Tickables);
}

bool UProjectLocomotionOverrideSubsystem::IsTickable() const
{
	return bNeedsMaintenanceTick && !IsTemplate();
}

bool UProjectLocomotionOverrideSubsystem::TryResolveRuntimeContext()
{
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
	EnsureLocomotionComponent(PlayerController->GetPawn());

	return TrackedPlayerController != nullptr && TrackedPawn != nullptr && TrackedLocomotionComponent != nullptr && TrackedInputComponent != nullptr;
}

void UProjectLocomotionOverrideSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		return;
	}

	DetachFromTrackedPlayerController();
	TrackedPlayerController = PlayerController;
	TrackedPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	BindInputToTrackedPlayerController();
}

void UProjectLocomotionOverrideSubsystem::DetachFromTrackedPlayerController()
{
	if (TrackedLocomotionComponent)
	{
		TrackedLocomotionComponent->SetCrawlModeEnabled(false);
		TrackedLocomotionComponent->SetWalkModeEnabled(false);
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandlePossessedPawnChanged);
	}

	UnbindInputFromTrackedPlayerController();
	TrackedPlayerController = nullptr;
	TrackedPawn = nullptr;
	TrackedLocomotionComponent = nullptr;
}

void UProjectLocomotionOverrideSubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController || TrackedInputComponent)
	{
		return;
	}

	TrackedInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectLocomotionOverrideInputComponent"));
	if (!TrackedInputComponent)
	{
		return;
	}

	TrackedInputComponent->bBlockInput = false;
	TrackedInputComponent->Priority = 1;
	TrackedInputComponent->RegisterComponent();
	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	TrackedInputComponent->BindKey(InputSettings ? InputSettings->ToggleWalkKey : EKeys::N, IE_Pressed, this, &ThisClass::HandleWalkTogglePressed);
	TrackedInputComponent->BindKey(InputSettings ? InputSettings->ToggleCrawlKey : EKeys::C, IE_Pressed, this, &ThisClass::HandleCrawlTogglePressed);
	TrackedInputComponent->BindKey(InputSettings ? InputSettings->SurrenderKey : EKeys::Down, IE_Pressed, this, &ThisClass::HandleSurrenderPressed);
	TrackedPlayerController->PushInputComponent(TrackedInputComponent);
}

void UProjectLocomotionOverrideSubsystem::UnbindInputFromTrackedPlayerController()
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

void UProjectLocomotionOverrideSubsystem::EnsureLocomotionComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		if (TrackedLocomotionComponent)
		{
			TrackedLocomotionComponent->SetCrawlModeEnabled(false);
		}

		TrackedPawn = nullptr;
		TrackedLocomotionComponent = nullptr;
		return;
	}

	if (TrackedPawn != Pawn && TrackedLocomotionComponent)
	{
		TrackedLocomotionComponent->SetCrawlModeEnabled(false);
		TrackedLocomotionComponent->SetWalkModeEnabled(false);
	}

	TrackedPawn = Pawn;
	UProjectLocomotionOverrideComponent* LocomotionComponent = Pawn->FindComponentByClass<UProjectLocomotionOverrideComponent>();
	if (!LocomotionComponent)
	{
		LocomotionComponent = NewObject<UProjectLocomotionOverrideComponent>(Pawn, UProjectLocomotionOverrideComponent::StaticClass(), TEXT("ProjectLocomotionOverrideComponent"));
		if (LocomotionComponent)
		{
			Pawn->AddInstanceComponent(LocomotionComponent);
			LocomotionComponent->OnComponentCreated();
			LocomotionComponent->RegisterComponent();
			LocomotionComponent->Activate(true);
		}
	}

	TrackedLocomotionComponent = LocomotionComponent;
}

void UProjectLocomotionOverrideSubsystem::MarkMaintenanceRequired()
{
	bNeedsMaintenanceTick = true;
}

void UProjectLocomotionOverrideSubsystem::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (TrackedLocomotionComponent && OldPawn == TrackedPawn)
	{
		TrackedLocomotionComponent->SetCrawlModeEnabled(false);
		TrackedLocomotionComponent->SetWalkModeEnabled(false);
	}

	TrackedPawn = NewPawn;
	TrackedLocomotionComponent = nullptr;
	MarkMaintenanceRequired();
}

void UProjectLocomotionOverrideSubsystem::HandleWalkTogglePressed()
{
	if (TrackedLocomotionComponent)
	{
		TrackedLocomotionComponent->ToggleWalkMode();
	}
}

void UProjectLocomotionOverrideSubsystem::HandleCrawlTogglePressed()
{
	if (TrackedLocomotionComponent)
	{
		TrackedLocomotionComponent->ToggleCrawlMode();
	}
}

void UProjectLocomotionOverrideSubsystem::HandleSurrenderPressed()
{
	if (!TrackedPawn)
	{
		return;
	}

	if (UProjectDefeatFlowComponent* DefeatFlowComponent = TrackedPawn->FindComponentByClass<UProjectDefeatFlowComponent>())
	{
		if (DefeatFlowComponent->TryRecoverFromKnockoutIfNoNearbyEnemy(TEXT("Input.DownRecover")))
		{
			return;
		}
	}

	if (UProjectInnerDoctrineComponent* DoctrineComponent = TrackedPawn->FindComponentByClass<UProjectInnerDoctrineComponent>())
	{
		DoctrineComponent->TryActivateTacticalRetreat();
	}
}
