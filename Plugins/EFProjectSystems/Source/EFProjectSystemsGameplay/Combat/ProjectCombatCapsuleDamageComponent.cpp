#include "Combat/ProjectCombatCapsuleDamageComponent.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"

UProjectCombatCapsuleDamageComponent::UProjectCombatCapsuleDamageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	HitPolicy = EProjectCombatHitPolicy::OncePerDamageWindow;
	PreferredCapsuleNames = { TEXT("CollisionCapsule") };
	PreferredCapsuleNameContains = { TEXT("DamageCapsule"), TEXT("HitCapsule"), TEXT("WeaponCapsule") };
	CollisionManagerClassNameContains = { TEXT("ACMCollisionManagerComponent") };
	bAutoRegisterCapsules = true;
	bSearchAttachedActorsForCapsules = true;
	bFallbackToAllNonRootCapsulesWhenACFManagerExists = true;
	bOnlyApplyDamageOnAuthority = true;
	bIgnoreOwner = true;
	bIgnoreInstigator = true;
	bDamageEnabled = false;
	ActiveDamageSpec = DefaultDamageSpec;
}

void UProjectCombatCapsuleDamageComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegisterCapsules)
	{
		RefreshTrackedCapsules();
	}
}

void UProjectCombatCapsuleDamageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindAllCapsules();
	Super::EndPlay(EndPlayReason);
}

void UProjectCombatCapsuleDamageComponent::RefreshTrackedCapsules()
{
	UnbindAllCapsules();
	TrackedCapsules.Reset();

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	TArray<AActor*> ActorsToScan;
	ActorsToScan.Add(OwnerActor);
	if (bSearchAttachedActorsForCapsules)
	{
		TArray<AActor*> AttachedActors;
		OwnerActor->GetAttachedActors(AttachedActors, true);
		ActorsToScan.Append(AttachedActors);
	}

	TArray<UCapsuleComponent*> OwnerCapsules;
	for (AActor* ActorToScan : ActorsToScan)
	{
		if (!ActorToScan)
		{
			continue;
		}

		OwnerCapsules.Reset();
		ActorToScan->GetComponents(OwnerCapsules);

		for (UCapsuleComponent* CapsuleComponent : OwnerCapsules)
		{
			if (!ShouldTrackCapsule(CapsuleComponent))
			{
				continue;
			}

			BindCapsule(CapsuleComponent);
		}
	}
}

void UProjectCombatCapsuleDamageComponent::BeginDamageWindow(const FProjectCombatDamageSpec& DamageSpec, bool bResetHitCache)
{
	ActiveDamageSpec = DamageSpec;
	SetDamageEnabled(true, bResetHitCache);
}

void UProjectCombatCapsuleDamageComponent::EndDamageWindow()
{
	bDamageEnabled = false;
}

void UProjectCombatCapsuleDamageComponent::SetDamageEnabled(bool bEnabled, bool bResetHitCache)
{
	bDamageEnabled = bEnabled;

	if (bResetHitCache)
	{
		ClearHitCache();
	}
}

void UProjectCombatCapsuleDamageComponent::ClearHitCache()
{
	HitActors.Reset();
}

bool UProjectCombatCapsuleDamageComponent::IsDamageEnabled() const
{
	return bDamageEnabled;
}

void UProjectCombatCapsuleDamageComponent::HandleCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bDamageEnabled)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OtherActor)
	{
		return;
	}

	if (bOnlyApplyDamageOnAuthority && !OwnerActor->HasAuthority() && OwnerActor->GetNetMode() != NM_Standalone)
	{
		return;
	}

	if (!ShouldDamageActor(OtherActor))
	{
		return;
	}

	if (IsActorAlreadyHit(OtherActor))
	{
		return;
	}

	UProjectCombatAttributeComponent* AttributeComponent = OtherActor->FindComponentByClass<UProjectCombatAttributeComponent>();
	if (!AttributeComponent)
	{
		return;
	}

	const FProjectCombatDamageSpec DamageSpec = BuildDamageSpecForHit(OtherActor, OtherComp);
	const FProjectCombatDamageResult DamageResult = AttributeComponent->ApplyDamage(DamageSpec);

	if (HitPolicy == EProjectCombatHitPolicy::OncePerDamageWindow)
	{
		HitActors.Add(OtherActor);
	}

	HandleDamageDealt(OtherActor, OtherComp, DamageResult);
}

bool UProjectCombatCapsuleDamageComponent::ShouldTrackCapsule(const UCapsuleComponent* CapsuleComponent) const
{
	if (!CapsuleComponent)
	{
		return false;
	}

	const AActor* CapsuleOwner = CapsuleComponent->GetOwner();
	const UActorComponent* RootComponent = CapsuleOwner ? Cast<UActorComponent>(CapsuleOwner->GetRootComponent()) : nullptr;
	const FString CapsuleName = CapsuleComponent->GetName();

	for (const FName& PreferredName : PreferredCapsuleNames)
	{
		if (CapsuleComponent->GetFName() == PreferredName)
		{
			return true;
		}
	}

	for (const FString& PartialName : PreferredCapsuleNameContains)
	{
		if (!PartialName.IsEmpty() && CapsuleName.Contains(PartialName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return bFallbackToAllNonRootCapsulesWhenACFManagerExists && CapsuleComponent != RootComponent && OwnerHasACFCollisionManager(CapsuleOwner);
}

bool UProjectCombatCapsuleDamageComponent::ShouldDamageActor(AActor* CandidateActor) const
{
	if (!CandidateActor)
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (bIgnoreOwner && CandidateActor == OwnerActor)
	{
		return false;
	}

	if (const AActor* SourceActor = ResolveSourceActor())
	{
		if (CandidateActor == SourceActor)
		{
			return false;
		}
	}

	if (bIgnoreInstigator && OwnerActor)
	{
		if (const AActor* InstigatorActor = OwnerActor->GetInstigator())
		{
			if (CandidateActor == InstigatorActor)
			{
				return false;
			}
		}
	}

	return true;
}

FProjectCombatDamageSpec UProjectCombatCapsuleDamageComponent::BuildDamageSpecForHit(AActor* HitActor, UPrimitiveComponent* HitComponent) const
{
	FProjectCombatDamageSpec DamageSpec = ActiveDamageSpec;
	if (FMath::IsNearlyZero(DamageSpec.BaseDamage) && FMath::IsNearlyZero(DamageSpec.FlatBonusDamage))
	{
		DamageSpec = DefaultDamageSpec;
	}

	if (!DamageSpec.SourceActor)
	{
		DamageSpec.SourceActor = ResolveSourceActor();
	}

	if (!DamageSpec.DamageCauser)
	{
		DamageSpec.DamageCauser = GetOwner();
	}

	if (UProjectInnerDoctrineComponent* DoctrineComponent = DamageSpec.SourceActor ? DamageSpec.SourceActor->FindComponentByClass<UProjectInnerDoctrineComponent>() : nullptr)
	{
		DoctrineComponent->ModifyOutgoingDamageSpec(HitActor, DamageSpec);
	}

	return DamageSpec;
}

void UProjectCombatCapsuleDamageComponent::HandleDamageDealt(AActor* HitActor, UPrimitiveComponent* HitComponent, const FProjectCombatDamageResult& DamageResult)
{
	OnSuccessfulHit.Broadcast(HitActor, DamageResult.DamageType, DamageResult.AppliedDamage, DamageResult.bKilledTarget, HitComponent);
}

AActor* UProjectCombatCapsuleDamageComponent::ResolveSourceActor() const
{
	if (ExplicitDamageSourceActor)
	{
		return ExplicitDamageSourceActor;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	if (AActor* InstigatorActor = OwnerActor->GetInstigator())
	{
		return InstigatorActor;
	}

	return OwnerActor;
}

void UProjectCombatCapsuleDamageComponent::BindCapsule(UCapsuleComponent* CapsuleComponent)
{
	if (!CapsuleComponent)
	{
		return;
	}

	CapsuleComponent->OnComponentBeginOverlap.RemoveDynamic(this, &UProjectCombatCapsuleDamageComponent::HandleCapsuleBeginOverlap);
	CapsuleComponent->OnComponentBeginOverlap.AddDynamic(this, &UProjectCombatCapsuleDamageComponent::HandleCapsuleBeginOverlap);
	TrackedCapsules.Add(CapsuleComponent);
}

void UProjectCombatCapsuleDamageComponent::UnbindAllCapsules()
{
	for (UCapsuleComponent* CapsuleComponent : TrackedCapsules)
	{
		if (!CapsuleComponent)
		{
			continue;
		}

		CapsuleComponent->OnComponentBeginOverlap.RemoveDynamic(this, &UProjectCombatCapsuleDamageComponent::HandleCapsuleBeginOverlap);
	}
}

bool UProjectCombatCapsuleDamageComponent::OwnerHasACFCollisionManager(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	TArray<UActorComponent*> OwnerComponents;
	Actor->GetComponents(OwnerComponents);

	for (const UActorComponent* Component : OwnerComponents)
	{
		if (!Component)
		{
			continue;
		}

		const FString ClassName = Component->GetClass()->GetName();
		for (const FString& Filter : CollisionManagerClassNameContains)
		{
			if (!Filter.IsEmpty() && ClassName.Contains(Filter, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}

	return false;
}

bool UProjectCombatCapsuleDamageComponent::IsActorAlreadyHit(AActor* OtherActor) const
{
	if (HitPolicy == EProjectCombatHitPolicy::AllowRepeatedHits)
	{
		return false;
	}

	return HitActors.Contains(OtherActor);
}
