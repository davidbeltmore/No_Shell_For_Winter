#include "Lockpicking/ProjectLockedInteractableActor.h"

#include "Components/ACFInteractionComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interfaces/ACFInteractableInterface.h"
#include "Lockpicking/ProjectLockpickingACFBridge.h"
#include "Lockpicking/ProjectLockpickableComponent.h"

#define LOCTEXT_NAMESPACE "ProjectLockedInteractableActor"

AProjectLockedInteractableActor::AProjectLockedInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	InteractionSphereOverlapChannels.Add(ECC_Pawn);
	InteractionSphereOverlapChannels.Add(ECC_WorldDynamic);

	DefaultSceneRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(DefaultSceneRootComponent);

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	if (StaticMeshComponent)
	{
		StaticMeshComponent->SetupAttachment(DefaultSceneRootComponent);
	}

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	if (InteractionSphere)
	{
		InteractionSphere->SetupAttachment(DefaultSceneRootComponent);
		ConfigureInteractionSphere();
	}

	LockpickableComponent = CreateDefaultSubobject<UProjectLockpickableComponent>(TEXT("ProjectLockpickableComponent"));
	if (LockpickableComponent)
	{
		LockpickableComponent->OnUnlockedInteractionRequested.AddDynamic(this, &ThisClass::HandleUnlockedInteractionRequested);
	}

	InteractableName = LOCTEXT("DefaultInteractableName", "Interact");
}

void AProjectLockedInteractableActor::BeginPlay()
{
	Super::BeginPlay();
	ConfigureInteractionSphere();

	if (InteractionSphere && bAutoRegisterWithPawnACFInteraction)
	{
		InteractionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleInteractionSphereBeginOverlap);
		InteractionSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::HandleInteractionSphereEndOverlap);
		if (bRefreshOverlapsOnBeginPlay)
		{
			RefreshCurrentInteractionOverlaps();
		}
	}
}

void AProjectLockedInteractableActor::ProcessEvent(UFunction* Function, void* Parms)
{
	if (TryConsumeACFInteractionProcessEvent(Function, Parms))
	{
		return;
	}

	Super::ProcessEvent(Function, Parms);
}

void AProjectLockedInteractableActor::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	if (LockpickableComponent)
	{
		EProjectLockpickInteractionGateResult GateResult = EProjectLockpickInteractionGateResult::RunOriginal;
		LockpickableComponent->HandleACFInteraction(Pawn, InteractionType, GateResult);
		if (GateResult == EProjectLockpickInteractionGateResult::Consumed)
		{
			return;
		}
	}

	OnOriginalInteractedByPawn(Pawn, InteractionType);
}

void AProjectLockedInteractableActor::OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	if (LockpickableComponent && LockpickableComponent->HandleACFLocalInteraction(Pawn, InteractionType))
	{
		return;
	}

	OnOriginalLocalInteractedByPawn(Pawn, InteractionType);
}

void AProjectLockedInteractableActor::OnInteractableRegisteredByPawn_Implementation(APawn* Pawn)
{
	OnOriginalInteractableRegisteredByPawn(Pawn);
}

void AProjectLockedInteractableActor::OnInteractableUnregisteredByPawn_Implementation(APawn* Pawn)
{
	OnOriginalInteractableUnregisteredByPawn(Pawn);
}

bool AProjectLockedInteractableActor::CanBeInteracted_Implementation(APawn* Pawn)
{
	return bIsEnabled && (!LockpickableComponent || LockpickableComponent->CanBeInteracted(Pawn));
}

FText AProjectLockedInteractableActor::GetInteractableName_Implementation()
{
	return InteractableName.IsEmpty() ? LOCTEXT("FallbackInteractableName", "Interact") : InteractableName;
}

void AProjectLockedInteractableActor::OnSaved_Implementation()
{
}

void AProjectLockedInteractableActor::OnLoaded_Implementation()
{
}

bool AProjectLockedInteractableActor::ShouldBeIgnored_Implementation()
{
	return false;
}

TArray<UActorComponent*> AProjectLockedInteractableActor::GetComponentsToSave_Implementation() const
{
	TArray<UActorComponent*> ComponentsToSave;
	if (LockpickableComponent)
	{
		ComponentsToSave.Add(LockpickableComponent);
	}
	return ComponentsToSave;
}

UProjectLockpickableComponent* AProjectLockedInteractableActor::GetLockpickableComponent() const
{
	return LockpickableComponent;
}

void AProjectLockedInteractableActor::OnOriginalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	(void)Pawn;
	(void)InteractionType;
}

void AProjectLockedInteractableActor::OnOriginalLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	(void)Pawn;
	(void)InteractionType;
}

void AProjectLockedInteractableActor::OnOriginalInteractableRegisteredByPawn_Implementation(APawn* Pawn)
{
	(void)Pawn;
}

void AProjectLockedInteractableActor::OnOriginalInteractableUnregisteredByPawn_Implementation(APawn* Pawn)
{
	(void)Pawn;
}

void AProjectLockedInteractableActor::HandleUnlockedInteractionRequested(APawn* Pawn, const FString& InteractionType)
{
	if (!bReplayACFInteractionOnUnlock || !Pawn || !GetClass()->ImplementsInterface(UACFInteractableInterface::StaticClass()))
	{
		OnOriginalInteractedByPawn(Pawn, InteractionType);
		return;
	}

	IACFInteractableInterface::Execute_OnInteractedByPawn(this, Pawn, InteractionType);
}

void AProjectLockedInteractableActor::HandleInteractionSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	RegisterWithPawnInteractionComponent(OtherActor, OtherComp);
}

void AProjectLockedInteractableActor::HandleInteractionSphereEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	(void)OverlappedComponent;
	(void)OtherBodyIndex;

	UnregisterFromPawnInteractionComponent(OtherActor, OtherComp);
}

void AProjectLockedInteractableActor::ConfigureInteractionSphere()
{
	if (!InteractionSphere || !bConfigureInteractionSphereForACF)
	{
		return;
	}

	InteractionSphere->SetSphereRadius(FMath::Max(0.0f, InteractionSphereRadius));
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionObjectType(InteractionSphereObjectChannel.GetValue());
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);

	if (InteractionSphereOverlapChannels.IsEmpty())
	{
		InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
	else
	{
		for (const TEnumAsByte<ECollisionChannel> Channel : InteractionSphereOverlapChannels)
		{
			InteractionSphere->SetCollisionResponseToChannel(Channel.GetValue(), ECR_Overlap);
		}
	}

	InteractionSphere->SetGenerateOverlapEvents(true);
}

void AProjectLockedInteractableActor::RefreshCurrentInteractionOverlaps()
{
	if (!InteractionSphere)
	{
		return;
	}

	InteractionSphere->UpdateOverlaps();

	TArray<AActor*> OverlappingActors;
	InteractionSphere->GetOverlappingActors(OverlappingActors, APawn::StaticClass());
	for (AActor* OverlappingActor : OverlappingActors)
	{
		RegisterWithPawnInteractionComponent(OverlappingActor, nullptr);
	}
}

void AProjectLockedInteractableActor::RegisterWithPawnInteractionComponent(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn && OtherComp)
	{
		Pawn = Cast<APawn>(OtherComp->GetOwner());
	}

	if (!Pawn)
	{
		return;
	}

	if (UACFInteractionComponent* InteractionComponent = Pawn->FindComponentByClass<UACFInteractionComponent>())
	{
		InteractionComponent->RegisterInteractable(this);
	}
}

void AProjectLockedInteractableActor::UnregisterFromPawnInteractionComponent(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn && OtherComp)
	{
		Pawn = Cast<APawn>(OtherComp->GetOwner());
	}

	if (!Pawn || (InteractionSphere && InteractionSphere->IsOverlappingActor(Pawn)))
	{
		return;
	}

	if (UACFInteractionComponent* InteractionComponent = Pawn->FindComponentByClass<UACFInteractionComponent>())
	{
		InteractionComponent->UnregisterInteractable(this);
	}
}

bool AProjectLockedInteractableActor::TryConsumeACFInteractionProcessEvent(UFunction* Function, void* Parms)
{
	return ProjectLockpickingACFBridge::TryConsumeACFInteractionProcessEvent(
		this,
		LockpickableComponent,
		Function,
		Parms,
		bLogLockpickGate);
}

#undef LOCTEXT_NAMESPACE
