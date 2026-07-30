#include "Actors/ProjectLevelDoor.h"

#include "Components/ACFInteractableComponent.h"
#include "Components/ACFInteractionComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectLevelDoor, Log, All);

#define LOCTEXT_NAMESPACE "ProjectLevelDoor"

AProjectLevelDoor::AProjectLevelDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	// Preserve the source Blueprint hierarchy: Sphere is the actor root and the
	// visible door is its child. Keep ACF's inherited SceneRoot underneath it so
	// the native InteractableComponent remains fully supported by ACFU 4.3.5.
	SetRootComponent(Sphere);
	SceneRoot->SetupAttachment(Sphere);
	Sphere->InitSphereRadius(100.0f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// The UE 5.7 source DoorToLevel is detected by ACF through a Pawn object.
	// UACFInteractionComponent only overlaps Pawn objects by default, so using
	// WorldStatic here makes the door invisible to the interaction system.
	Sphere->SetCollisionObjectType(ECC_Pawn);
	Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	Sphere->SetGenerateOverlapEvents(true);

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(Sphere);
	StaticMesh->SetRelativeLocation(FVector(0.0, 60.0, -110.0));
	StaticMesh->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
	StaticMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	StaticMesh->SetGenerateOverlapEvents(true);

	InteractableName = LOCTEXT("DefaultInteractableName", "Interact");
	DestinationLevel = TSoftObjectPtr<UWorld>(
		FSoftObjectPath(TEXT("/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration")));

	if (InteractableComponent)
	{
		// The destination gate is refreshed in BeginPlay, after package mounts exist.
		InteractableComponent->SetInteractionEnabled(false);
	}
}

void AProjectLevelDoor::BeginPlay()
{
	Super::BeginPlay();

	bTravelRequested = false;
	SetEnabled(IsEnabled);
}

void AProjectLevelDoor::SetEnabled(bool bEnabled)
{
	IsEnabled = bEnabled;
	bDestinationAvailable = IsDestinationAvailable();

	if (InteractableComponent)
	{
		// ACF can leave the component in Busy while dispatching the actor interface.
		// Always close the previous interaction before applying the new gate state.
		InteractableComponent->EndInteraction();
		InteractableComponent->SetInteractionEnabled(
			IsEnabled && !bTravelRequested && bDestinationAvailable);
	}
}

void AProjectLevelDoor::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	Super::OnInteractedByPawn_Implementation(Pawn, InteractionType);

	if (!HasAuthority() || !IsEnabled || bTravelRequested)
	{
		EndPawnInteraction(Pawn);
		return;
	}

	if (!IsDestinationAvailable())
	{
		bDestinationAvailable = false;
		UE_LOG(
			LogProjectLevelDoor,
			Warning,
			TEXT("%s cannot travel because DestinationLevel is null, invalid, or absent: %s."),
			*GetName(),
			*DestinationLevel.ToSoftObjectPath().ToString());
		SetEnabled(IsEnabled);
		EndPawnInteraction(Pawn);
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World) || !World->IsGameWorld())
	{
		UE_LOG(LogProjectLevelDoor, Warning, TEXT("%s ignored a level-travel request outside a game world."), *GetName());
		EndPawnInteraction(Pawn);
		return;
	}

	bTravelRequested = true;
	SetEnabled(IsEnabled);
	EndPawnInteraction(Pawn);
	UE_LOG(
		LogProjectLevelDoor,
		Log,
		TEXT("%s is opening %s (absolute=%s, options='%s')."),
		*GetName(),
		*DestinationLevel.ToSoftObjectPath().ToString(),
		bAbsoluteTravel ? TEXT("true") : TEXT("false"),
		*TravelOptions);

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, DestinationLevel, bAbsoluteTravel, TravelOptions);
}

void AProjectLevelDoor::OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	Super::OnLocalInteractedByPawn_Implementation(Pawn, InteractionType);

	// ACF assigns CurrentInteractingActor immediately before this local callback.
	// Use its supported API so a failed or delayed travel cannot leave stale input state.
	if (Pawn)
	{
		if (UACFInteractionComponent* InteractionComponent =
			Pawn->FindComponentByClass<UACFInteractionComponent>())
		{
			InteractionComponent->EndInteraction();
		}
	}
}

FText AProjectLevelDoor::GetInteractableName_Implementation()
{
	return InteractableName.IsEmpty() ? LOCTEXT("FallbackInteractableName", "Interact") : InteractableName;
}

bool AProjectLevelDoor::CanBeInteracted_Implementation(APawn* Pawn)
{
	(void)Pawn;
	return IsEnabled && !bTravelRequested && bDestinationAvailable;
}

void AProjectLevelDoor::OnSaved_Implementation()
{
}

void AProjectLevelDoor::OnLoaded_Implementation()
{
	bTravelRequested = false;
	SetEnabled(IsEnabled);
}

bool AProjectLevelDoor::ShouldBeIgnored_Implementation()
{
	return false;
}

TArray<UActorComponent*> AProjectLevelDoor::GetComponentsToSave_Implementation() const
{
	return {};
}

void AProjectLevelDoor::EndPawnInteraction(APawn* Pawn) const
{
	if (Pawn)
	{
		if (UACFInteractionComponent* InteractionComponent =
			Pawn->FindComponentByClass<UACFInteractionComponent>())
		{
			InteractionComponent->EndInteraction();
		}
	}

	// Keep this direct reset as a fallback for callers without an ACF pawn component.
	if (InteractableComponent)
	{
		InteractableComponent->EndInteraction();
	}
}

bool AProjectLevelDoor::IsDestinationAvailable() const
{
	if (DestinationLevel.IsNull())
	{
		return false;
	}

	const FString LongPackageName = DestinationLevel.ToSoftObjectPath().GetLongPackageName();
	return FPackageName::IsValidLongPackageName(LongPackageName)
		&& FPackageName::DoesPackageExist(LongPackageName);
}

#undef LOCTEXT_NAMESPACE
