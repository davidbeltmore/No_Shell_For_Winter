#include "Calysto/EFCalystoFloorDoor.h"

#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDirectorSubsystem.h"
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
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		bIsEnabled = false;
		if (UGameInstance* GameInstance = GetGameInstance())
			if (auto* Director = GameInstance->GetSubsystem<UEFCalystoDirectorSubsystem>())
			{
				Director->FloorReady.AddUObject(this, &ThisClass::HandleDirectorStateChanged);
				Director->RequestFailed.AddUObject(this, &ThisClass::HandleDirectorStateChanged);
			}
		RefreshInteractionState();
		return;
	}
	if (const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get())
	{
		if (UStaticMesh* ConfiguredMesh = Settings->DungeonFloorDoorMesh.Get())
		{
			StaticMesh->SetStaticMesh(ConfiguredMesh);
		}
		else
		{
			UE_LOG(LogEFCalystoFloorDoor, Error,
				TEXT("%s could not use its Session Core door mesh because the asset was not resident."),
				*GetName());
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
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
			if (auto* Director = GameInstance->GetSubsystem<UEFCalystoDirectorSubsystem>())
			{
				Director->FloorReady.RemoveAll(this);
				Director->RequestFailed.RemoveAll(this);
			}
		Super::EndPlay(EndPlayReason);
		return;
	}
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

FVector AEFCalystoFloorDoor::GetDirectorApproachWorld() const
{
	// Director door artwork is upright on the marker with its width along local X.
	// Its -Y approach stays within the native 100 cm interaction sphere.
	return GetActorTransform().TransformPosition(FVector(0.0, -90.0, 0.0));
}

void AEFCalystoFloorDoor::SetDirectorInteractionEnabled(const bool bEnabled)
{
	SetEnabled(bEnabled);
}

bool AEFCalystoFloorDoor::SetDirectorAppearance(UStaticMesh* Mesh)
{
	if (!IsValid(Mesh) || !StaticMesh) return false;
	const FBox Bounds = Mesh->GetBoundingBox();
	if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
		|| Bounds.GetSize().GetMin() <= UE_SMALL_NUMBER) return false;
	if (StaticMesh->GetStaticMesh() != Mesh && !StaticMesh->SetStaticMesh(Mesh)) return false;
	const FVector Center = Bounds.GetCenter();
	// The native End marker is already at floor level. The old doorway transform
	// expected an elevated actor pivot and rotated its width into the approach.
	const FTransform Alignment(FQuat::Identity, FVector(-Center.X, -Center.Y, -Bounds.Min.Z));
	StaticMesh->SetRelativeTransform(Alignment);
	return StaticMesh->GetStaticMesh() == Mesh && StaticMesh->GetRelativeTransform().Equals(Alignment, 0.001);
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
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		auto* Director = GameInstance ? GameInstance->GetSubsystem<UEFCalystoDirectorSubsystem>() : nullptr;
		bTravelRequested = true;
		RefreshInteractionState();
		EndPawnInteraction(Pawn);
		if (!Director || !Director->RequestAdvanceFloor())
		{
			bTravelRequested = false;
			RefreshInteractionState();
			UE_LOG(LogEFCalystoFloorDoor, Warning, TEXT("%s could not advance the dungeon floor."), *GetName());
		}
		return;
	}
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
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		const auto* Director = GameInstance ? GameInstance->GetSubsystem<UEFCalystoDirectorSubsystem>() : nullptr;
		const int64 Floor = Director ? Director->GetSnapshot().FloorNumber : 0;
		return Floor > 0 && Floor < MAX_int64
			? FText::Format(LOCTEXT("DirectorNextFloorLabel", "Floor {0} \u2192 {1}"), FText::AsNumber(Floor), FText::AsNumber(Floor + 1))
			: LOCTEXT("DirectorNextFloorFallback", "Next Floor");
	}
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
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		const auto* Director = GameInstance ? GameInstance->GetSubsystem<UEFCalystoDirectorSubsystem>() : nullptr;
		if (!Director || !Director->IsDungeonWorld(GetWorld()) || Director->IsTravelRequestPending()) return false;
		const FEFCalystoDirectorSnapshot Snapshot = Director->GetSnapshot();
		return Snapshot.State == EEFCalystoDirectorState::Ready && Snapshot.bNativeFloorVerified
			&& Snapshot.FloorNumber > 0 && Snapshot.FloorNumber < MAX_int64;
	}
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

void AEFCalystoFloorDoor::HandleDirectorStateChanged(const FEFCalystoDirectorSnapshot& Snapshot)
{
	if (Snapshot.State == EEFCalystoDirectorState::Failed) bIsEnabled = false;
	bTravelRequested = false;
	RefreshInteractionState();
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
