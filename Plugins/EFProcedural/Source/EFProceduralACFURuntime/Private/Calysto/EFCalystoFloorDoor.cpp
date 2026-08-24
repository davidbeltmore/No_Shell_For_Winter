#include "Calysto/EFCalystoFloorDoor.h"

#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Components/ACFInteractableComponent.h"
#include "Components/ACFInteractionComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoFloorDoor, Log, All);

#define LOCTEXT_NAMESPACE "EFCalystoFloorDoor"

AEFCalystoFloorDoor::AEFCalystoFloorDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	SetRootComponent(Sphere);
	SceneRoot->SetupAttachment(Sphere);
	Sphere->InitSphereRadius(InteractionRadius);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionObjectType(ECC_Pawn);
	Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	Sphere->SetGenerateOverlapEvents(true);
	Sphere->SetCanEverAffectNavigation(false);

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(Sphere);
	StaticMesh->SetRelativeLocation(FVector(0.0, 60.0, -110.0));
	StaticMesh->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
	StaticMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	StaticMesh->SetGenerateOverlapEvents(true);
	// The endpoint must remain physically solid and interactable, but it must not
	// carve away the final NavMesh approach used to prove Start -> Door readiness.
	StaticMesh->SetCanEverAffectNavigation(false);

	if (InteractableComponent)
	{
		InteractableComponent->SetInteractionEnabled(false);
	}
}

void AEFCalystoFloorDoor::BeginPlay()
{
	Super::BeginPlay();

	bTravelRequested = false;
	Sphere->SetSphereRadius(FMath::Max(1.0f, InteractionRadius), true);
	if (const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get())
	{
		if (UStaticMesh* ConfiguredMesh = Settings->DungeonFloorDoorMesh.LoadSynchronous())
		{
			StaticMesh->SetStaticMesh(ConfiguredMesh);
		}
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
			GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>())
		{
			DungeonSubsystem->OnFloorTravelFailed().AddUObject(
				this,
				&AEFCalystoFloorDoor::HandleFloorTravelFailed);
		}
	}
	RefreshInteractionState();
}

void AEFCalystoFloorDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
			GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>())
		{
			DungeonSubsystem->OnFloorTravelFailed().RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AEFCalystoFloorDoor::SetEnabled(const bool bEnabled)
{
	bIsEnabled = bEnabled;
	RefreshInteractionState();
}

void AEFCalystoFloorDoor::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	Super::OnInteractedByPawn_Implementation(Pawn, InteractionType);

	if (!HasAuthority() || !CanBeInteracted_Implementation(Pawn))
	{
		EndPawnInteraction(Pawn);
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!DungeonSubsystem)
	{
		UE_LOG(LogEFCalystoFloorDoor, Error, TEXT("%s cannot find UEFCalystoDungeonSubsystem."), *GetName());
		EndPawnInteraction(Pawn);
		return;
	}

	bTravelRequested = true;
	RefreshInteractionState();
	EndPawnInteraction(Pawn);
	if (!DungeonSubsystem->RequestAdvanceFloor())
	{
		bTravelRequested = false;
		RefreshInteractionState();
		UE_LOG(LogEFCalystoFloorDoor, Warning, TEXT("%s rejected a duplicate or invalid floor advance."), *GetName());
	}
}

void AEFCalystoFloorDoor::OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	Super::OnLocalInteractedByPawn_Implementation(Pawn, InteractionType);
	EndPawnInteraction(Pawn);
}

FText AEFCalystoFloorDoor::GetInteractableName_Implementation()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!DungeonSubsystem)
	{
		return LOCTEXT("NextFloorFallback", "Next Floor");
	}

	const int64 Floor = DungeonSubsystem->GetCurrentFloor();
	if (Floor >= MAX_int64)
	{
		return FText::Format(
			LOCTEXT("FloorCounterLimitLabel", "Floor {0} complete"),
			FText::AsNumber(Floor));
	}

	return FText::Format(
		LOCTEXT("InfiniteNextFloorLabel", "Floor {0} \u2192 {1}"),
		FText::AsNumber(Floor),
		FText::AsNumber(Floor + 1));
}

bool AEFCalystoFloorDoor::CanBeInteracted_Implementation(APawn* Pawn)
{
	(void)Pawn;
	if (!bIsEnabled || bTravelRequested)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!DungeonSubsystem || DungeonSubsystem->IsTravelRequestPending())
	{
		return false;
	}

	const int64 Floor = DungeonSubsystem->GetCurrentFloor();
	return Floor > 0 && Floor < MAX_int64;
}

void AEFCalystoFloorDoor::EndPawnInteraction(APawn* Pawn) const
{
	if (Pawn)
	{
		if (UACFInteractionComponent* InteractionComponent =
			Pawn->FindComponentByClass<UACFInteractionComponent>())
		{
			InteractionComponent->EndInteraction();
		}
	}

	if (InteractableComponent)
	{
		InteractableComponent->EndInteraction();
	}
}

void AEFCalystoFloorDoor::HandleFloorTravelFailed()
{
	if (!bTravelRequested)
	{
		return;
	}

	bTravelRequested = false;
	RefreshInteractionState();
	UE_LOG(LogEFCalystoFloorDoor, Warning, TEXT("%s re-enabled after the requested floor travel failed."), *GetName());
}

void AEFCalystoFloorDoor::RefreshInteractionState()
{
	if (!InteractableComponent)
	{
		return;
	}

	InteractableComponent->EndInteraction();
	InteractableComponent->SetInteractionEnabled(CanBeInteracted_Implementation(nullptr));
}

#undef LOCTEXT_NAMESPACE
