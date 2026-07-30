#include "Lockpicking/ProjectLockedWorldItem.h"

#include "Interfaces/ACFInteractableInterface.h"
#include "Lockpicking/ProjectLockpickingACFBridge.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ProjectLockedWorldItem"

AProjectLockedWorldItem::AProjectLockedWorldItem()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> LegacyPickupSound(
		TEXT("/Game/FullSample/Audio/Sounds/SFX/PickUp_Cue.PickUp_Cue"));
	if (LegacyPickupSound.Succeeded())
	{
		PickupSound = LegacyPickupSound.Object;
	}

	LockpickableComponent = CreateDefaultSubobject<UProjectLockpickableComponent>(TEXT("ProjectLockpickableComponent"));
	if (LockpickableComponent)
	{
		LockpickableComponent->OnUnlockedInteractionRequested.AddDynamic(this, &ThisClass::HandleUnlockedInteractionRequested);
	}
}

void AProjectLockedWorldItem::ProcessEvent(UFunction* Function, void* Parms)
{
	if (TryConsumeACFInteractionProcessEvent(Function, Parms))
	{
		return;
	}

	Super::ProcessEvent(Function, Parms);
}

void AProjectLockedWorldItem::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
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

	Super::OnInteractedByPawn_Implementation(Pawn, InteractionType);
}

void AProjectLockedWorldItem::OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
	if (LockpickableComponent && LockpickableComponent->HandleACFLocalInteraction(Pawn, InteractionType))
	{
		return;
	}

	Super::OnLocalInteractedByPawn_Implementation(Pawn, InteractionType);
}

bool AProjectLockedWorldItem::CanBeInteracted_Implementation(APawn* Pawn)
{
	return LockpickableComponent ? LockpickableComponent->CanBeInteracted(Pawn) : Super::CanBeInteracted_Implementation(Pawn);
}

FText AProjectLockedWorldItem::GetInteractableName_Implementation()
{
	if (LockpickableComponent && LockpickableComponent->IsLocked())
	{
		const FText BaseName = Super::GetInteractableName_Implementation();
		if (!BaseName.IsEmpty())
		{
			return FText::Format(LOCTEXT("LockedNameFormat", "{0} (Locked)"), BaseName);
		}

		return LOCTEXT("LockedWorldItemName", "Locked Item");
	}

	return Super::GetInteractableName_Implementation();
}

UProjectLockpickableComponent* AProjectLockedWorldItem::GetLockpickableComponent() const
{
	return LockpickableComponent;
}

void AProjectLockedWorldItem::HandleUnlockedInteractionRequested(APawn* Pawn, const FString& InteractionType)
{
	if (bReplayACFInteractionOnUnlock && Pawn && GetClass()->ImplementsInterface(UACFInteractableInterface::StaticClass()))
	{
		IACFInteractableInterface::Execute_OnInteractedByPawn(this, Pawn, InteractionType);
		return;
	}

	Super::OnInteractedByPawn_Implementation(Pawn, InteractionType);
}

bool AProjectLockedWorldItem::TryConsumeACFInteractionProcessEvent(UFunction* Function, void* Parms)
{
	return ProjectLockpickingACFBridge::TryConsumeACFInteractionProcessEvent(
		this,
		LockpickableComponent,
		Function,
		Parms,
		bLogLockpickGate);
}

#undef LOCTEXT_NAMESPACE
