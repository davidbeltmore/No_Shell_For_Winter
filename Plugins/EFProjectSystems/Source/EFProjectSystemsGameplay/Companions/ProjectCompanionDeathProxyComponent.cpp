#include "Companions/ProjectCompanionDeathProxyComponent.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "GameFramework/Actor.h"

UProjectCompanionDeathProxyComponent::UProjectCompanionDeathProxyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UProjectCompanionDeathProxyComponent::InitializeProxy(
	const FGuid& InStableCompanionId,
	UProjectRunCompanionSubsystem* InSubsystem)
{
	UnbindDeathSources();
	StableCompanionId.Invalidate();
	CompanionSubsystem = nullptr;
	bForwardedDeath = false;

	if (!InStableCompanionId.IsValid() || !InSubsystem || !GetOwner())
	{
		return false;
	}

	StableCompanionId = InStableCompanionId;
	CompanionSubsystem = InSubsystem;
	if (!BindDeathSources())
	{
		UnbindDeathSources();
		StableCompanionId.Invalidate();
		CompanionSubsystem = nullptr;
		return false;
	}
	return true;
}

void UProjectCompanionDeathProxyComponent::OnUnregister()
{
	UnbindDeathSources();
	Super::OnUnregister();
}

bool UProjectCompanionDeathProxyComponent::BindDeathSources()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	bool bBoundAtLeastOneSource = false;
	if (UACFDamageHandlerComponent* ACFDamage = Owner->FindComponentByClass<UACFDamageHandlerComponent>())
	{
		ACFDamage->OnOwnerDeath.AddUniqueDynamic(this, &ThisClass::HandleACFOwnerDeath);
		bBoundAtLeastOneSource |=
			ACFDamage->OnOwnerDeath.IsAlreadyBound(this, &ThisClass::HandleACFOwnerDeath);
	}
	if (UProjectCombatAttributeComponent* ProjectCombat = Owner->FindComponentByClass<UProjectCombatAttributeComponent>())
	{
		ProjectCombat->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleProjectCombatDeath);
		bBoundAtLeastOneSource |=
			ProjectCombat->OnDeath.IsAlreadyBound(this, &ThisClass::HandleProjectCombatDeath);
	}
	return bBoundAtLeastOneSource;
}

void UProjectCompanionDeathProxyComponent::UnbindDeathSources()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (UACFDamageHandlerComponent* ACFDamage = Owner->FindComponentByClass<UACFDamageHandlerComponent>())
	{
		ACFDamage->OnOwnerDeath.RemoveAll(this);
	}
	if (UProjectCombatAttributeComponent* ProjectCombat = Owner->FindComponentByClass<UProjectCombatAttributeComponent>())
	{
		ProjectCombat->OnDeath.RemoveAll(this);
	}
}

void UProjectCompanionDeathProxyComponent::ForwardDeath(const FName SourceDomain)
{
	if (bForwardedDeath || !CompanionSubsystem || !StableCompanionId.IsValid())
	{
		return;
	}
	bForwardedDeath = true;
	CompanionSubsystem->ReportCompanionDeath(StableCompanionId, GetOwner(), SourceDomain);
}

void UProjectCompanionDeathProxyComponent::HandleACFOwnerDeath()
{
	ForwardDeath(TEXT("ACF"));
}

void UProjectCompanionDeathProxyComponent::HandleProjectCombatDeath(AActor* SourceActor)
{
	(void)SourceActor;
	ForwardDeath(TEXT("ProjectCombat"));
}
