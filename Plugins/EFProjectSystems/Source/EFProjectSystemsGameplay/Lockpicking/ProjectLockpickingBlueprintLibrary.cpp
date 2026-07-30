#include "Lockpicking/ProjectLockpickingBlueprintLibrary.h"

#include "GameFramework/Actor.h"
#include "Lockpicking/ProjectLockedWorldItem.h"
#include "Lockpicking/ProjectLockpickableComponent.h"

UProjectLockpickableComponent* UProjectLockpickingBlueprintLibrary::FindLockpickableComponent(AActor* Actor)
{
	if (const AProjectLockedWorldItem* LockedWorldItem = Cast<AProjectLockedWorldItem>(Actor))
	{
		return LockedWorldItem->GetLockpickableComponent();
	}

	return Actor ? Actor->FindComponentByClass<UProjectLockpickableComponent>() : nullptr;
}

bool UProjectLockpickingBlueprintLibrary::ConsumeActorACFInteractionIfLocked(
	AActor* Actor,
	APawn* Pawn,
	const FString& InteractionType)
{
	UProjectLockpickableComponent* LockpickableComponent = FindLockpickableComponent(Actor);
	return LockpickableComponent
		? LockpickableComponent->ConsumeACFInteractionIfLocked(Pawn, InteractionType)
		: false;
}

bool UProjectLockpickingBlueprintLibrary::ConsumeActorACFLocalInteractionIfLocked(
	AActor* Actor,
	APawn* Pawn,
	const FString& InteractionType)
{
	UProjectLockpickableComponent* LockpickableComponent = FindLockpickableComponent(Actor);
	return LockpickableComponent
		? LockpickableComponent->ConsumeACFLocalInteractionIfLocked(Pawn, InteractionType)
		: false;
}

bool UProjectLockpickingBlueprintLibrary::ShouldAllowActorACFInteraction(
	AActor* Actor,
	APawn* Pawn,
	const bool bOriginalCanInteract)
{
	UProjectLockpickableComponent* LockpickableComponent = FindLockpickableComponent(Actor);
	return LockpickableComponent
		? LockpickableComponent->ShouldAllowACFInteraction(Pawn, bOriginalCanInteract)
		: bOriginalCanInteract;
}
