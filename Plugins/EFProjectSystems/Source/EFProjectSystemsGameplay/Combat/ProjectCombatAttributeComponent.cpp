#include "Combat/ProjectCombatAttributeComponent.h"

#include "Combat/ProjectCombatTypes.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "Defeat/ProjectDefeatHitResolver.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCombatAttribute, Log, All);

namespace
{
	const FName PainName(TEXT("Pain"));
}

UProjectCombatAttributeComponent::UProjectCombatAttributeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.10f;

	HealthAttributeName = TEXT("Health");
	ArmorAttributeName = TEXT("Armor");
	bPauseRegenerationWhenDead = true;
	bIsDead = false;

	Attributes.Add(FProjectCombatAttribute(TEXT("Health"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("Armor"), 0.f, 0.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("Stamina"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("MeleeDamage"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("RangedDamage"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("SpellDamage"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("PhysicalDefense"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("SpellDefense"), 100.f, 100.f));
	Attributes.Add(FProjectCombatAttribute(TEXT("CritChance"), 100.f, 100.f));
}

void UProjectCombatAttributeComponent::BeginPlay()
{
	Super::BeginPlay();

	SanitizeAttributes();

	if (const FProjectCombatAttribute* HealthAttribute = FindAttribute(HealthAttributeName))
	{
		bIsDead = HealthAttribute->CurrentValue <= HealthAttribute->MinValue;
	}
}

void UProjectCombatAttributeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (FMath::IsNearlyZero(DeltaTime))
	{
		return;
	}

	if (bIsDead && bPauseRegenerationWhenDead)
	{
		return;
	}

	for (FProjectCombatAttribute& Attribute : Attributes)
	{
		if (FMath::IsNearlyZero(Attribute.RegenPerSecond))
		{
			continue;
		}

		if (RecoveryBlockedAttributes.Contains(Attribute.AttributeName))
		{
			continue;
		}

		const float OldValue = Attribute.CurrentValue;
		const float NewValue = ClampAttributeValue(Attribute, Attribute.CurrentValue + (Attribute.RegenPerSecond * DeltaTime));
		if (FMath::IsNearlyEqual(OldValue, NewValue))
		{
			continue;
		}

		Attribute.CurrentValue = NewValue;
		OnAttributeChanged.Broadcast(Attribute.AttributeName, OldValue, Attribute.CurrentValue, Attribute.MaxValue);
	}
}

FProjectCombatDamageResult UProjectCombatAttributeComponent::ApplyDamage(const FProjectCombatDamageSpec& DamageSpec)
{
	FProjectCombatDamageResult DamageResult;
	DamageResult.DamageType = DamageSpec.DamageType;
	DamageResult.TargetAttribute = DamageSpec.TargetAttribute;
	DamageResult.SecondaryAttribute = DamageSpec.SecondaryAttribute;

	AActor* OwnerActor = GetOwner();
	UProjectInnerDoctrineComponent* TargetDoctrineComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>()
		: nullptr;

	FProjectCombatAttribute* TargetAttribute = FindMutableAttribute(DamageSpec.TargetAttribute);
	if (!TargetAttribute)
	{
		return DamageResult;
	}

	const float RequestedDamage = FMath::Max(0.f, DamageSpec.BaseDamage + DamageSpec.FlatBonusDamage);
	const float ResistanceMultiplier = GetResistanceMultiplier(DamageSpec.DamageType);
	const float ArmorMitigation = DamageSpec.bIgnoreArmor ? 0.f : GetArmorMitigation(DamageSpec);
	const float ReducedDamage = RequestedDamage * ResistanceMultiplier;
	float AppliedDamage = FMath::Max(0.f, ReducedDamage - ArmorMitigation);
	float DefensiveFlatNegatedDamage = 0.f;
	float GuardRecoveryAbsorbedDamage = 0.f;
	if (TargetAttribute->AttributeName == HealthAttributeName && TargetDoctrineComponent)
	{
		AppliedDamage = TargetDoctrineComponent->ModifyIncomingHealthDamage(
			DamageSpec.SourceActor,
			DamageSpec.DamageType,
			AppliedDamage,
			DefensiveFlatNegatedDamage,
			GuardRecoveryAbsorbedDamage);
	}

	DamageResult.RequestedDamage = RequestedDamage;
	DamageResult.DefensiveFlatNegatedDamage = DefensiveFlatNegatedDamage;
	DamageResult.GuardRecoveryAbsorbedDamage = GuardRecoveryAbsorbedDamage;
	DamageResult.PreDamageTargetValue = TargetAttribute->CurrentValue;
	DamageResult.TargetMaxValue = TargetAttribute->MaxValue;

	const float OldTargetValue = TargetAttribute->CurrentValue;
	float NewTargetValue = ClampAttributeValue(*TargetAttribute, TargetAttribute->CurrentValue - AppliedDamage);
	if (TargetAttribute->AttributeName == HealthAttributeName
		&& TargetDoctrineComponent
		&& NewTargetValue <= TargetAttribute->MinValue)
	{
		const FProjectIncomingHitContext LethalHitContext = BuildIncomingHitContext(
			DamageSpec,
			RequestedDamage,
			AppliedDamage,
			NewTargetValue,
			true,
			DefensiveFlatNegatedDamage,
			GuardRecoveryAbsorbedDamage);
		float SurvivingHealth = 0.f;
		if (TargetDoctrineComponent->TryPreventDeathFromDamage(LethalHitContext, OldTargetValue, SurvivingHealth))
		{
			NewTargetValue = ClampAttributeValue(*TargetAttribute, FMath::Max(SurvivingHealth, TargetAttribute->MinValue));
			AppliedDamage = FMath::Max(0.f, OldTargetValue - NewTargetValue);
		}
	}

	DamageResult.AppliedDamage = AppliedDamage;
	DamageResult.MitigatedDamage = FMath::Max(0.f, RequestedDamage - AppliedDamage);
	TargetAttribute->CurrentValue = NewTargetValue;
	DamageResult.RemainingValue = TargetAttribute->CurrentValue;

	if (DamageSpec.SecondaryDamage > 0.f)
	{
		if (FProjectCombatAttribute* SecondaryAttribute = FindMutableAttribute(DamageSpec.SecondaryAttribute))
		{
			const float OldSecondaryValue = SecondaryAttribute->CurrentValue;
			SecondaryAttribute->CurrentValue = ClampAttributeValue(*SecondaryAttribute, SecondaryAttribute->CurrentValue - DamageSpec.SecondaryDamage);
			DamageResult.SecondaryAppliedDamage = OldSecondaryValue - SecondaryAttribute->CurrentValue;
			OnAttributeChanged.Broadcast(SecondaryAttribute->AttributeName, OldSecondaryValue, SecondaryAttribute->CurrentValue, SecondaryAttribute->MaxValue);
		}
	}

	OnAttributeChanged.Broadcast(TargetAttribute->AttributeName, OldTargetValue, TargetAttribute->CurrentValue, TargetAttribute->MaxValue);

	if (TargetAttribute->AttributeName == HealthAttributeName)
	{
		DamageResult.bKilledTarget = ShouldDieFromDamage(DamageResult);
		if (DamageResult.bKilledTarget && !bIsDead)
		{
			bIsDead = true;
			HandleDeath(DamageSpec.SourceActor);
		}
	}

	OnDamageApplied.Broadcast(DamageSpec.SourceActor, DamageResult.DamageType, DamageResult.RequestedDamage, DamageResult.AppliedDamage, DamageResult.RemainingValue, DamageResult.bKilledTarget);
	HandlePostDamageApplied(DamageSpec, DamageResult);

	if (TargetDoctrineComponent)
	{
		FProjectIncomingHitContext FinalHitContext = BuildIncomingHitContext(
			DamageSpec,
			DamageResult.RequestedDamage,
			DamageResult.AppliedDamage,
			DamageResult.RemainingValue,
			DamageResult.bKilledTarget,
			DamageResult.DefensiveFlatNegatedDamage,
			DamageResult.GuardRecoveryAbsorbedDamage);
		if (TargetAttribute->AttributeName == HealthAttributeName)
		{
			ApplyQualifiedPainHit(FinalHitContext);
		}

		TargetDoctrineComponent->NotifyDamageReceived(FinalHitContext);
	}

	if (DamageSpec.SourceActor && DamageSpec.SourceActor != GetOwner())
	{
		if (UProjectInnerDoctrineComponent* SourceDoctrineComponent = DamageSpec.SourceActor->FindComponentByClass<UProjectInnerDoctrineComponent>())
		{
			SourceDoctrineComponent->NotifyDamageDealt(GetOwner(), DamageResult);
		}
	}

	return DamageResult;
}

FProjectIncomingHitContext UProjectCombatAttributeComponent::BuildIncomingHitContext(
	const FProjectCombatDamageSpec& DamageSpec,
	const float RequestedDamage,
	const float AppliedDamage,
	const float RemainingHealth,
	const bool bKilledTarget,
	const float DefensiveFlatNegatedDamage,
	const float GuardRecoveryAbsorbedDamage) const
{
	FProjectIncomingHitContext HitContext;
	HitContext.TargetActor = GetOwner();
	HitContext.SourceActor = DamageSpec.SourceActor;
	HitContext.DamageCauser = DamageSpec.DamageCauser;
	HitContext.DamageType = DamageSpec.DamageType;
	HitContext.RequestedDamage = RequestedDamage;
	HitContext.AppliedDamage = AppliedDamage;
	HitContext.DefensiveFlatNegatedDamage = DefensiveFlatNegatedDamage;
	HitContext.GuardRecoveryAbsorbedDamage = GuardRecoveryAbsorbedDamage;
	HitContext.RemainingHealth = RemainingHealth;
	HitContext.bKilledTarget = bKilledTarget;

	const AActor* PrimarySourceActor = DamageSpec.SourceActor
		? DamageSpec.SourceActor.Get()
		: DamageSpec.DamageCauser.Get();
	const AActor* SecondarySourceActor = DamageSpec.DamageCauser && DamageSpec.DamageCauser.Get() != PrimarySourceActor
		? DamageSpec.DamageCauser.Get()
		: nullptr;

	HitContext.InstigatorActor = PrimarySourceActor ? PrimarySourceActor->GetInstigator() : nullptr;
	if (!HitContext.InstigatorActor && SecondarySourceActor)
	{
		HitContext.InstigatorActor = SecondarySourceActor->GetInstigator();
	}

	HitContext.OwnerActor = PrimarySourceActor ? PrimarySourceActor->GetOwner() : nullptr;
	if (!HitContext.OwnerActor && SecondarySourceActor)
	{
		HitContext.OwnerActor = SecondarySourceActor->GetOwner();
	}

	HitContext.AttachParentActor = PrimarySourceActor ? PrimarySourceActor->GetAttachParentActor() : nullptr;
	if (!HitContext.AttachParentActor && SecondarySourceActor)
	{
		HitContext.AttachParentActor = SecondarySourceActor->GetAttachParentActor();
	}

	if (const UWorld* World = GetWorld())
	{
		HitContext.WorldTimeSeconds = World->GetTimeSeconds();
	}

	return HitContext;
}

float UProjectCombatAttributeComponent::ApplyQualifiedPainHit(FProjectIncomingHitContext& HitContext)
{
	HitContext.PainAppliedDelta = 0.f;

	if (HitContext.AppliedDamage <= 0.f)
	{
		return 0.f;
	}

	AActor* OwnerActor = GetOwner();
	UProjectSurvivalNeedsComponent* NeedsComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>()
		: nullptr;
	if (!NeedsComponent)
	{
		return 0.f;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	FProjectDefeatHitResolution HitResolution;
	if (!FProjectDefeatHitResolver::ResolveQualifiedEnemyActor(
		HitContext,
		Settings ? Settings->QualifiedEnemyClassNameHints : TArray<FString>(),
		HitResolution))
	{
		return 0.f;
	}

	const float RequestedPainDelta = UProjectDefeatFlowSettings::ComputePainFromAppliedDamage(
		HitContext.AppliedDamage,
		Settings ? Settings->PainPerAppliedDamage : 0.01f);
	if (RequestedPainDelta <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	HitContext.PainAppliedDelta = NeedsComponent->ModifySensationValue(PainName, RequestedPainDelta, true);
	return HitContext.PainAppliedDelta;
}

float UProjectCombatAttributeComponent::HealAttribute(FName AttributeName, float HealAmount)
{
	if (HealAmount <= 0.f)
	{
		return 0.f;
	}

	FProjectCombatAttribute* Attribute = FindMutableAttribute(AttributeName);
	if (!Attribute)
	{
		return 0.f;
	}

	if (RecoveryBlockedAttributes.Contains(Attribute->AttributeName))
	{
		return 0.f;
	}

	const float OldValue = Attribute->CurrentValue;
	Attribute->CurrentValue = ClampAttributeValue(*Attribute, Attribute->CurrentValue + HealAmount);
	const float AppliedHeal = Attribute->CurrentValue - OldValue;

	if (!FMath::IsNearlyZero(AppliedHeal))
	{
		OnAttributeChanged.Broadcast(Attribute->AttributeName, OldValue, Attribute->CurrentValue, Attribute->MaxValue);
	}

	if (Attribute->AttributeName == HealthAttributeName && Attribute->CurrentValue > Attribute->MinValue)
	{
		bIsDead = false;
	}

	return AppliedHeal;
}

float UProjectCombatAttributeComponent::ModifyAttribute(FName AttributeName, float DeltaAmount)
{
	if (FMath::IsNearlyZero(DeltaAmount))
	{
		return 0.f;
	}

	FProjectCombatAttribute* Attribute = FindMutableAttribute(AttributeName);
	if (!Attribute)
	{
		return 0.f;
	}

	if (DeltaAmount > 0.f && RecoveryBlockedAttributes.Contains(Attribute->AttributeName))
	{
		return 0.f;
	}

	const float OldValue = Attribute->CurrentValue;
	Attribute->CurrentValue = ClampAttributeValue(*Attribute, Attribute->CurrentValue + DeltaAmount);
	const float AppliedDelta = Attribute->CurrentValue - OldValue;
	if (FMath::IsNearlyZero(AppliedDelta))
	{
		return 0.f;
	}

	OnAttributeChanged.Broadcast(Attribute->AttributeName, OldValue, Attribute->CurrentValue, Attribute->MaxValue);

	if (Attribute->AttributeName == HealthAttributeName)
	{
		const bool bWasDead = bIsDead;
		bIsDead = Attribute->CurrentValue <= Attribute->MinValue;
		if (bIsDead && !bWasDead)
		{
			HandleDeath(nullptr);
		}
	}

	return AppliedDelta;
}

bool UProjectCombatAttributeComponent::SetAttributeCurrentValue(FName AttributeName, float NewValue)
{
	if (FProjectCombatAttribute* Attribute = FindMutableAttribute(AttributeName))
	{
		if (RecoveryBlockedAttributes.Contains(Attribute->AttributeName) && NewValue > Attribute->CurrentValue)
		{
			return false;
		}

		return InternalSetAttributeCurrentValue(*Attribute, NewValue);
	}

	return false;
}

void UProjectCombatAttributeComponent::SetAttributeRecoveryBlocked(FName AttributeName, const bool bBlocked)
{
	if (AttributeName.IsNone())
	{
		return;
	}

	if (bBlocked)
	{
		RecoveryBlockedAttributes.Add(AttributeName);
	}
	else
	{
		RecoveryBlockedAttributes.Remove(AttributeName);
	}
}

bool UProjectCombatAttributeComponent::IsAttributeRecoveryBlocked(FName AttributeName) const
{
	return RecoveryBlockedAttributes.Contains(AttributeName);
}

bool UProjectCombatAttributeComponent::SetAttributeMaxValue(FName AttributeName, float NewMaxValue, bool bClampCurrentValue)
{
	FProjectCombatAttribute* Attribute = FindMutableAttribute(AttributeName);
	if (!Attribute)
	{
		return false;
	}

	Attribute->MaxValue = FMath::Max(Attribute->MinValue, NewMaxValue);
	if (bClampCurrentValue)
	{
		Attribute->CurrentValue = ClampAttributeValue(*Attribute, Attribute->CurrentValue);
	}

	return true;
}

float UProjectCombatAttributeComponent::GetAttributeCurrentValue(FName AttributeName) const
{
	if (const FProjectCombatAttribute* Attribute = FindAttribute(AttributeName))
	{
		return Attribute->CurrentValue;
	}

	return 0.f;
}

float UProjectCombatAttributeComponent::GetAttributeMaxValue(FName AttributeName) const
{
	if (const FProjectCombatAttribute* Attribute = FindAttribute(AttributeName))
	{
		return Attribute->MaxValue;
	}

	return 0.f;
}

bool UProjectCombatAttributeComponent::HasAttribute(FName AttributeName) const
{
	return FindAttribute(AttributeName) != nullptr;
}

bool UProjectCombatAttributeComponent::IsDead() const
{
	return bIsDead;
}

float UProjectCombatAttributeComponent::GetResistanceMultiplier(FName DamageType) const
{
	for (const FProjectCombatResistance& Resistance : Resistances)
	{
		if (Resistance.DamageType == DamageType)
		{
			return Resistance.Multiplier;
		}
	}

	return 1.f;
}

float UProjectCombatAttributeComponent::GetArmorMitigation(const FProjectCombatDamageSpec& DamageSpec) const
{
	const float ArmorValue = GetAttributeCurrentValue(ArmorAttributeName);
	return FMath::Max(0.f, ArmorValue - DamageSpec.ArmorPenetration);
}

bool UProjectCombatAttributeComponent::ShouldDieFromDamage(const FProjectCombatDamageResult& DamageResult) const
{
	return DamageResult.TargetAttribute == HealthAttributeName && DamageResult.RemainingValue <= 0.f;
}

void UProjectCombatAttributeComponent::HandlePostDamageApplied(const FProjectCombatDamageSpec& DamageSpec, const FProjectCombatDamageResult& DamageResult)
{
}

void UProjectCombatAttributeComponent::HandleDeath(AActor* SourceActor)
{
	OnDeath.Broadcast(SourceActor);
}

FProjectCombatAttribute* UProjectCombatAttributeComponent::FindMutableAttribute(FName AttributeName)
{
	return Attributes.FindByPredicate([AttributeName](const FProjectCombatAttribute& Attribute)
	{
		return Attribute.AttributeName == AttributeName;
	});
}

const FProjectCombatAttribute* UProjectCombatAttributeComponent::FindAttribute(FName AttributeName) const
{
	return Attributes.FindByPredicate([AttributeName](const FProjectCombatAttribute& Attribute)
	{
		return Attribute.AttributeName == AttributeName;
	});
}

float UProjectCombatAttributeComponent::ClampAttributeValue(const FProjectCombatAttribute& Attribute, float Value) const
{
	const float MaxValue = FMath::Max(Attribute.MinValue, Attribute.MaxValue);
	return FMath::Clamp(Value, Attribute.MinValue, MaxValue);
}

void UProjectCombatAttributeComponent::SanitizeAttributes()
{
	TSet<FName> SeenAttributes;
	Attributes.RemoveAll([&SeenAttributes](const FProjectCombatAttribute& Attribute)
	{
		if (Attribute.AttributeName.IsNone() || SeenAttributes.Contains(Attribute.AttributeName))
		{
			return true;
		}

		SeenAttributes.Add(Attribute.AttributeName);
		return false;
	});

	for (FProjectCombatAttribute& Attribute : Attributes)
	{
		Attribute.MaxValue = FMath::Max(Attribute.MinValue, Attribute.MaxValue);
		Attribute.CurrentValue = ClampAttributeValue(Attribute, Attribute.CurrentValue);
	}
}

bool UProjectCombatAttributeComponent::InternalSetAttributeCurrentValue(FProjectCombatAttribute& Attribute, float NewValue)
{
	const float OldValue = Attribute.CurrentValue;
	Attribute.CurrentValue = ClampAttributeValue(Attribute, NewValue);

	if (FMath::IsNearlyEqual(OldValue, Attribute.CurrentValue))
	{
		return false;
	}

	OnAttributeChanged.Broadcast(Attribute.AttributeName, OldValue, Attribute.CurrentValue, Attribute.MaxValue);

	if (Attribute.AttributeName == HealthAttributeName)
	{
		const bool bWasDead = bIsDead;
		bIsDead = Attribute.CurrentValue <= Attribute.MinValue;
		if (bIsDead && !bWasDead)
		{
			HandleDeath(nullptr);
		}
	}

	return true;
}
