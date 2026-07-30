#include "InnerDoctrine/ProjectInnerDoctrineAltar.h"

#include "Components/ACFInteractableComponent.h"
#include "Components/ACFInteractionComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InnerDoctrine/ProjectInnerDoctrineBlueprintLibrary.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "UObject/ConstructorHelpers.h"

AProjectInnerDoctrineAltar::AProjectInnerDoctrineAltar()
{
	PrimaryActorTick.bCanEverTick = false;

	// Rebuild the exact UE 5.7 component hierarchy in project-owned C++ so the
	// Altar Blueprint can remain parented to the UE 5.8 compatibility actor.
	InteractionVolume = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	InteractionVolume->SetupAttachment(SceneRoot);
	InteractionVolume->InitSphereRadius(32.0f);
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionObjectType(ECC_Pawn);
	// The source Blueprint used Custom collision derived from the blocking
	// defaults, then changed the eight engine channels below to Overlap.
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Block);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Camera, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Destructible, ECR_Overlap);
	// Preserve the effective project-channel defaults from UE 5.7.
	InteractionVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);  // CharacterMesh
	InteractionVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel12, ECR_Overlap); // Water
	InteractionVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel13, ECR_Ignore);  // UI
	InteractionVolume->SetCollisionResponseToChannel(ECC_GameTraceChannel15, ECR_Ignore);  // IKTrace
	InteractionVolume->SetGenerateOverlapEvents(true);
	InteractionVolume->SetHiddenInGame(true);

	AltarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	AltarMesh->SetupAttachment(InteractionVolume);
	AltarMesh->SetRelativeLocation(FVector(0.0, 0.0, -30.0));
	AltarMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	AltarMesh->SetGenerateOverlapEvents(true);

	BookMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Book"));
	BookMesh->SetupAttachment(AltarMesh);
	BookMesh->SetRelativeLocation(FVector(0.0, 0.0, 80.0));
	BookMesh->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
	BookMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	BookMesh->SetGenerateOverlapEvents(true);

	ClothMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh1"));
	ClothMesh->SetupAttachment(AltarMesh);
	ClothMesh->SetRelativeLocation(FVector(0.0, 6.0, 80.0));
	ClothMesh->SetRelativeScale3D(FVector(1.0, 1.0125, 0.7725));
	ClothMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	ClothMesh->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> AltarMeshFinder(
		TEXT("/Game/Fantastic_Dungeon_Pack/meshes/props/furniture/SM_PROP_altar_dungeon_01.SM_PROP_altar_dungeon_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BookMeshFinder(
		TEXT("/Game/Fantastic_Dungeon_Pack/meshes/props/small_deco/SM_PROP_book_dungeon_07.SM_PROP_book_dungeon_07"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ClothMeshFinder(
		TEXT("/Game/Fantastic_Dungeon_Pack/meshes/props/fabrics/SM_PROP_altar_cloth_dungeon_02.SM_PROP_altar_cloth_dungeon_02"));

	AltarMesh->SetStaticMesh(AltarMeshFinder.Object);
	BookMesh->SetStaticMesh(BookMeshFinder.Object);
	ClothMesh->SetStaticMesh(ClothMeshFinder.Object);
}

void AProjectInnerDoctrineAltar::BeginPlay()
{
	Super::BeginPlay();

	if (InteractableComponent)
	{
		InteractableComponent->EndInteraction();
		InteractableComponent->SetInteractionEnabled(true);
	}
}

bool AProjectInnerDoctrineAltar::SpendDxpOnAttribute(AActor* InteractingActor, const EProjectDoctrineAttribute Attribute) const
{
	UProjectInnerDoctrineComponent* Component = ResolveInnerDoctrineComponent(InteractingActor);
	return Component ? Component->SpendDxpOnAttribute(Attribute) : false;
}

int32 AProjectInnerDoctrineAltar::WithdrawMetaDxp(AActor* InteractingActor, const int32 RequestedAmount) const
{
	UProjectInnerDoctrineComponent* Component = ResolveInnerDoctrineComponent(InteractingActor);
	return Component ? Component->WithdrawMetaDxp(RequestedAmount) : 0;
}

bool AProjectInnerDoctrineAltar::SetDoctrineMasteryMode(AActor* InteractingActor, const bool bEnabled) const
{
	UProjectInnerDoctrineComponent* Component = ResolveInnerDoctrineComponent(InteractingActor);
	if (!Component)
	{
		return false;
	}

	Component->SetDoctrineMasteryMode(bEnabled);
	return true;
}

UProjectInnerDoctrineComponent* AProjectInnerDoctrineAltar::ResolveInnerDoctrineComponent(AActor* InteractingActor) const
{
	if (!InteractingActor)
	{
		return nullptr;
	}

	if (UProjectInnerDoctrineComponent* DirectComponent = InteractingActor->FindComponentByClass<UProjectInnerDoctrineComponent>())
	{
		return DirectComponent;
	}

	if (const AController* Controller = Cast<AController>(InteractingActor))
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			return Pawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
		}
	}

	if (const APawn* Pawn = Cast<APawn>(InteractingActor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			return Controller->FindComponentByClass<UProjectInnerDoctrineComponent>();
		}
	}

	return nullptr;
}

void AProjectInnerDoctrineAltar::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	(void)InteractionType;
	OpenExchangeMenu(Pawn);
}

void AProjectInnerDoctrineAltar::OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	Super::OnLocalInteractedByPawn_Implementation(Pawn, InteractionType);
	(void)InteractionType;

	// ACF marks the pawn/altar interaction as active immediately before this
	// local callback. The exchange menu is a separate UI flow, so release the
	// ACF interaction now; otherwise the altar remains Busy after the menu is
	// closed and a second press of E is ignored.
	if (Pawn)
	{
		if (UACFInteractionComponent* InteractionComponent =
			Pawn->FindComponentByClass<UACFInteractionComponent>())
		{
			InteractionComponent->EndInteraction();
		}
	}

	// Keep the altar-side reset as a fallback for callers without the pawn
	// interaction component, matching the project door's cleanup contract.
	if (InteractableComponent)
	{
		InteractableComponent->EndInteraction();
	}
}

bool AProjectInnerDoctrineAltar::OpenExchangeMenu(AActor* InteractingActor) const
{
	return UProjectInnerDoctrineBlueprintLibrary::OpenInnerDoctrineExchangeMenu(InteractingActor);
}
