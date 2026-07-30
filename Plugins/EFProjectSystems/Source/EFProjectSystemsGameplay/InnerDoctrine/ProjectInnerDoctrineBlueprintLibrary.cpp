#include "InnerDoctrine/ProjectInnerDoctrineBlueprintLibrary.h"

#include "Engine/World.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineMenuSubsystem.h"

UProjectInnerDoctrineComponent* UProjectInnerDoctrineBlueprintLibrary::FindInnerDoctrineComponent(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UProjectInnerDoctrineComponent>() : nullptr;
}

int32 UProjectInnerDoctrineBlueprintLibrary::GrantDoctrineDxp(
	AActor* Actor,
	const FName ReasonId,
	const int32 Amount,
	const EProjectDoctrineExperienceSource Source)
{
	UProjectInnerDoctrineComponent* Component = FindInnerDoctrineComponent(Actor);
	return Component ? Component->GrantDxp(ReasonId, Amount, Source) : 0;
}

bool UProjectInnerDoctrineBlueprintLibrary::SpendDoctrineDxpOnAttribute(AActor* Actor, const EProjectDoctrineAttribute Attribute)
{
	UProjectInnerDoctrineComponent* Component = FindInnerDoctrineComponent(Actor);
	return Component ? Component->SpendDxpOnAttribute(Attribute) : false;
}

int32 UProjectInnerDoctrineBlueprintLibrary::WithdrawDoctrineMetaDxp(AActor* Actor, const int32 RequestedAmount)
{
	UProjectInnerDoctrineComponent* Component = FindInnerDoctrineComponent(Actor);
	return Component ? Component->WithdrawMetaDxp(RequestedAmount) : 0;
}

bool UProjectInnerDoctrineBlueprintLibrary::OpenInnerDoctrineExchangeMenu(AActor* Actor)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	if (UProjectInnerDoctrineMenuSubsystem* MenuSubsystem = World->GetSubsystem<UProjectInnerDoctrineMenuSubsystem>())
	{
		return MenuSubsystem->OpenMenuForActor(Actor);
	}

	return false;
}

void UProjectInnerDoctrineBlueprintLibrary::CloseInnerDoctrineExchangeMenu(AActor* Actor)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (UProjectInnerDoctrineMenuSubsystem* MenuSubsystem = World->GetSubsystem<UProjectInnerDoctrineMenuSubsystem>())
	{
		MenuSubsystem->CloseMenu();
	}
}

bool UProjectInnerDoctrineBlueprintLibrary::IsInnerDoctrineExchangeMenuOpen(AActor* Actor)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	if (const UProjectInnerDoctrineMenuSubsystem* MenuSubsystem = World->GetSubsystem<UProjectInnerDoctrineMenuSubsystem>())
	{
		return MenuSubsystem->IsMenuOpen();
	}

	return false;
}
