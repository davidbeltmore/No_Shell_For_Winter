#include "Curse/ProjectCurseSourceComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"

namespace ProjectCurseGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(
		Source,
		"Project.Curse.Source",
		"Canonical marker for an enemy or hazard that is a source of Curse.");
}

UProjectCurseSourceComponent::UProjectCurseSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProjectCurseSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	SynchronizeAbilitySystemTag();
}

void UProjectCurseSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveOwnedAbilitySystemTag();
	Super::EndPlay(EndPlayReason);
}

bool UProjectCurseSourceComponent::IsCurseSourceEnabled() const
{
	return bCurseSourceEnabled;
}

void UProjectCurseSourceComponent::SetCurseSourceEnabled(const bool bEnabled)
{
	if (bCurseSourceEnabled == bEnabled)
	{
		return;
	}

	bCurseSourceEnabled = bEnabled;
	if (bCurseSourceEnabled)
	{
		SynchronizeAbilitySystemTag();
	}
	else
	{
		RemoveOwnedAbilitySystemTag();
	}
}

void UProjectCurseSourceComponent::SynchronizeAbilitySystemTag()
{
	if (!bCurseSourceEnabled || bAddedLooseTagToAbilitySystem)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(
			OwnerActor,
			true);
	if (!AbilitySystem
		|| AbilitySystem->HasMatchingGameplayTag(ProjectCurseGameplayTags::Source))
	{
		return;
	}

	AbilitySystem->AddLooseGameplayTag(ProjectCurseGameplayTags::Source);
	bAddedLooseTagToAbilitySystem = true;
}

void UProjectCurseSourceComponent::RemoveOwnedAbilitySystemTag()
{
	if (!bAddedLooseTagToAbilitySystem)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(
			OwnerActor,
			true))
	{
		AbilitySystem->RemoveLooseGameplayTag(ProjectCurseGameplayTags::Source);
	}
	bAddedLooseTagToAbilitySystem = false;
}
