#include "Survival/ProjectSurvivalAttributeBridgeComponent.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "GameplayEffectTypes.h"
#include "Survival/ProjectSurvivalLog.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	FString NormalizeBridgeName(const FString& Value)
	{
		FString Normalized;
		Normalized.Reserve(Value.Len());
		for (const TCHAR Character : Value)
		{
			if (FChar::IsAlnum(Character))
			{
				Normalized.AppendChar(FChar::ToLower(Character));
			}
		}
		return Normalized;
	}

	TArray<FString> GetCandidatePropertyNames(const FName LogicalAttributeName)
	{
		if (LogicalAttributeName == TEXT("MeleeDamage"))
		{
			return { TEXT("MeleeDamage") };
		}

		if (LogicalAttributeName == TEXT("RangedDamage"))
		{
			return { TEXT("RangedDamage") };
		}

		if (LogicalAttributeName == TEXT("SpellDamage"))
		{
			return { TEXT("SpellDamage"), TEXT("MagicDamage") };
		}

		if (LogicalAttributeName == TEXT("PhysicalDefense"))
		{
			return { TEXT("PhysicalDefense"), TEXT("PhysicalDefence"), TEXT("Defense"), TEXT("Defence") };
		}

		if (LogicalAttributeName == TEXT("SpellDefense"))
		{
			return { TEXT("SpellDefense"), TEXT("SpellDefence"), TEXT("MagicDefense"), TEXT("MagicDefence") };
		}

		if (LogicalAttributeName == TEXT("CritChance"))
		{
			return { TEXT("CritChance"), TEXT("CriticalChance"), TEXT("CritRate"), TEXT("CritProbability") };
		}

		return { LogicalAttributeName.ToString() };
	}

	bool PropertyNameMatches(const FName PropertyName, const TArray<FString>& CandidateNames)
	{
		const FString NormalizedPropertyName = NormalizeBridgeName(PropertyName.ToString());
		for (const FString& CandidateName : CandidateNames)
		{
			if (NormalizedPropertyName == NormalizeBridgeName(CandidateName))
			{
				return true;
			}
		}

		return false;
	}

	bool IsGameplayAttributeDataProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == FGameplayAttributeData::StaticStruct();
	}

	template <typename ComponentType>
	ComponentType* FindComponentByClassHint(AActor* Owner, const TArray<FString>& ClassHints)
	{
		if (!Owner)
		{
			return nullptr;
		}

		TInlineComponentArray<UActorComponent*> Components(Owner);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString ComponentClassName = Component->GetClass()->GetName();
			for (const FString& Hint : ClassHints)
			{
				if (ComponentClassName.Contains(Hint))
				{
					return Cast<ComponentType>(Component);
				}
			}
		}

		return nullptr;
	}
}

UProjectSurvivalAttributeBridgeComponent::UProjectSurvivalAttributeBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bApplyPenaltyToAbilitySystem = true;
	bLogDiscoveryToOutputLog = true;
	bBridgeReady = false;
	bApplyingPenalty = false;

	BridgedAttributes.Add(TEXT("MeleeDamage"));
	BridgedAttributes.Add(TEXT("RangedDamage"));
	BridgedAttributes.Add(TEXT("SpellDamage"));
	BridgedAttributes.Add(TEXT("PhysicalDefense"));
	BridgedAttributes.Add(TEXT("SpellDefense"));
	BridgedAttributes.Add(TEXT("CritChance"));
}

void UProjectSurvivalAttributeBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	BindToNeedsComponent();
	ResolveBridge();
	ApplyCurrentPenalty();
}

void UProjectSurvivalAttributeBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAppliedPenalty();
	ClearAttributeDelegates();
	ExternalAttributeMultipliersBySource.Reset();

	if (NeedsComponent)
	{
		NeedsComponent->OnPenaltyMultiplierChanged.RemoveDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
	}

	Super::EndPlay(EndPlayReason);
}

bool UProjectSurvivalAttributeBridgeComponent::ForceResolveAndApplyBridge()
{
	BindToNeedsComponent();
	ResolveBridge();
	ApplyCurrentPenalty();
	return IsBridgeReady();
}

bool UProjectSurvivalAttributeBridgeComponent::IsBridgeReady() const
{
	return bBridgeReady;
}

FString UProjectSurvivalAttributeBridgeComponent::GetResolvedStatisticsComponentClassName() const
{
	return StatisticsComponent ? StatisticsComponent->GetClass()->GetName() : FString();
}

FString UProjectSurvivalAttributeBridgeComponent::GetResolvedAbilitySystemComponentClassName() const
{
	return AbilitySystemComponent ? AbilitySystemComponent->GetClass()->GetName() : FString();
}

TArray<FProjectSurvivalBridgeAttributeSnapshot> UProjectSurvivalAttributeBridgeComponent::BuildAttributeBindingSnapshots() const
{
	TArray<FProjectSurvivalBridgeAttributeSnapshot> Snapshots;
	Snapshots.Reserve(ResolvedBindings.Num());

	for (const FProjectSurvivalResolvedAttributeBinding& Binding : ResolvedBindings)
	{
		FProjectSurvivalBridgeAttributeSnapshot Snapshot;
		Snapshot.LogicalAttributeName = Binding.LogicalAttributeName;
		Snapshot.PropertyName = Binding.ResolvedPropertyName.ToString();
		Snapshot.AttributeSetClassName = Binding.AttributeSet.IsValid() ? Binding.AttributeSet->GetClass()->GetName() : FString();
		Snapshot.BaselineValue = Binding.BaselineValue;
		Snapshot.CurrentBaseValue = Binding.bGameplayAttributeData ? (ReadBindingCurrentValue(Binding) - Binding.AppliedModifierValue) : ReadBindingBaseValue(Binding);
		Snapshot.CurrentValue = ReadBindingCurrentValue(Binding);
		Snapshot.ExpectedEffectiveValue = Binding.BaselineValue * GetCombinedAttributeMultiplier(Binding.LogicalAttributeName);
		Snapshot.AppliedModifierValue = Binding.AppliedModifierValue;
		Snapshot.bResolved = Binding.bResolved;
		Snapshot.bGameplayAttribute = Binding.bGameplayAttributeData;
		Snapshots.Add(Snapshot);
	}

	return Snapshots;
}

void UProjectSurvivalAttributeBridgeComponent::HandlePenaltyMultiplierChanged(float OldPenaltyMultiplier, float NewPenaltyMultiplier)
{
	ApplyCurrentPenalty();
}

void UProjectSurvivalAttributeBridgeComponent::SetExternalAttributeMultiplier(
	const FName SourceName,
	const FName LogicalAttributeName,
	const float Multiplier)
{
	if (SourceName.IsNone() || LogicalAttributeName.IsNone())
	{
		return;
	}

	const float ClampedMultiplier = FMath::Max(0.f, Multiplier);
	TMap<FName, float>& SourceMultipliers = ExternalAttributeMultipliersBySource.FindOrAdd(SourceName);
	const float* ExistingMultiplier = SourceMultipliers.Find(LogicalAttributeName);
	if (ExistingMultiplier && FMath::IsNearlyEqual(*ExistingMultiplier, ClampedMultiplier, KINDA_SMALL_NUMBER))
	{
		return;
	}

	SourceMultipliers.Add(LogicalAttributeName, ClampedMultiplier);
	ApplyCurrentPenalty();
}

void UProjectSurvivalAttributeBridgeComponent::ClearExternalAttributeMultiplier(
	const FName SourceName,
	const FName LogicalAttributeName)
{
	if (SourceName.IsNone() || LogicalAttributeName.IsNone())
	{
		return;
	}

	TMap<FName, float>* SourceMultipliers = ExternalAttributeMultipliersBySource.Find(SourceName);
	if (!SourceMultipliers || SourceMultipliers->Remove(LogicalAttributeName) == 0)
	{
		return;
	}

	if (SourceMultipliers->Num() == 0)
	{
		ExternalAttributeMultipliersBySource.Remove(SourceName);
	}

	ApplyCurrentPenalty();
}

void UProjectSurvivalAttributeBridgeComponent::ClearAllExternalAttributeMultipliersForSource(const FName SourceName)
{
	if (SourceName.IsNone())
	{
		return;
	}

	if (ExternalAttributeMultipliersBySource.Remove(SourceName) > 0)
	{
		ApplyCurrentPenalty();
	}
}

void UProjectSurvivalAttributeBridgeComponent::HandleGameplayAttributeValueChanged(const FOnAttributeChangeData& ChangeData)
{
	if (bApplyingPenalty)
	{
		return;
	}

	FProjectSurvivalResolvedAttributeBinding* Binding = FindBindingByAttribute(ChangeData.Attribute);
	if (!Binding)
	{
		return;
	}

	Binding->BaselineValue = ChangeData.NewValue - Binding->AppliedModifierValue;
	Binding->bHasBaselineValue = true;

	if (bLogDiscoveryToOutputLog)
	{
		UE_LOG(LogProjectSurvival, Verbose, TEXT("[ProjectSurvivalBridge] External change detected for %s -> current=%.3f, inferred baseline=%.3f, appliedModifier=%.3f"),
			*Binding->LogicalAttributeName.ToString(),
			ChangeData.NewValue,
			Binding->BaselineValue,
			Binding->AppliedModifierValue);
	}
}

void UProjectSurvivalAttributeBridgeComponent::BindToNeedsComponent()
{
	if (NeedsComponent)
	{
		NeedsComponent->OnPenaltyMultiplierChanged.RemoveDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
	}

	AActor* Owner = GetOwner();
	NeedsComponent = Owner ? Owner->FindComponentByClass<UProjectSurvivalNeedsComponent>() : nullptr;
	if (NeedsComponent)
	{
		NeedsComponent->OnPenaltyMultiplierChanged.AddUniqueDynamic(this, &ThisClass::HandlePenaltyMultiplierChanged);
	}
}

void UProjectSurvivalAttributeBridgeComponent::ResolveBridge()
{
	ResetAppliedPenalty();
	ClearAttributeDelegates();
	ResolvedBindings.Reset();
	bBridgeReady = false;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	AbilitySystemComponent = Owner->FindComponentByClass<UAbilitySystemComponent>();
	if (!AbilitySystemComponent)
	{
		AbilitySystemComponent = FindComponentByClassHint<UAbilitySystemComponent>(Owner, { TEXT("AbilitySystemComponent"), TEXT("ACFAbilitySystemComponent") });
	}

	StatisticsComponent = FindComponentByClassHint<UActorComponent>(Owner, { TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent") });
	if (!AbilitySystemComponent || !bApplyPenaltyToAbilitySystem)
	{
		return;
	}

	for (const FName LogicalAttributeName : BridgedAttributes)
	{
		FProjectSurvivalResolvedAttributeBinding Binding;
		Binding.LogicalAttributeName = LogicalAttributeName;
		ResolveAttributeBinding(Binding);
		if (Binding.bResolved && Binding.bGameplayAttributeData)
		{
			Binding.ValueChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Binding.GameplayAttribute).AddUObject(this, &ThisClass::HandleGameplayAttributeValueChanged);
		}

		ResolvedBindings.Add(MoveTemp(Binding));
	}

	bBridgeReady = ResolvedBindings.ContainsByPredicate([](const FProjectSurvivalResolvedAttributeBinding& Binding)
	{
		return Binding.bResolved;
	});

	if (bLogDiscoveryToOutputLog)
	{
		UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalBridge] Ready=%s ASC=%s Stats=%s Resolved=%d/%d"),
			bBridgeReady ? TEXT("true") : TEXT("false"),
			AbilitySystemComponent ? *AbilitySystemComponent->GetClass()->GetName() : TEXT("None"),
			StatisticsComponent ? *StatisticsComponent->GetClass()->GetName() : TEXT("None"),
			ResolvedBindings.FilterByPredicate([](const FProjectSurvivalResolvedAttributeBinding& Binding) { return Binding.bResolved; }).Num(),
			ResolvedBindings.Num());
	}
}

void UProjectSurvivalAttributeBridgeComponent::ClearAttributeDelegates()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	for (FProjectSurvivalResolvedAttributeBinding& Binding : ResolvedBindings)
	{
		if (!Binding.ValueChangedHandle.IsValid() || !Binding.bGameplayAttributeData)
		{
			continue;
		}

		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Binding.GameplayAttribute).Remove(Binding.ValueChangedHandle);
		Binding.ValueChangedHandle.Reset();
	}
}

void UProjectSurvivalAttributeBridgeComponent::ResetAppliedPenalty()
{
	TGuardValue<bool> ApplyingPenaltyGuard(bApplyingPenalty, true);
	for (FProjectSurvivalResolvedAttributeBinding& Binding : ResolvedBindings)
	{
		if (!Binding.bResolved || FMath::IsNearlyZero(Binding.AppliedModifierValue, KINDA_SMALL_NUMBER))
		{
			continue;
		}

		if (ApplyBindingModifierDelta(Binding, -Binding.AppliedModifierValue))
		{
			Binding.AppliedModifierValue = 0.f;
		}
	}
}

void UProjectSurvivalAttributeBridgeComponent::ApplyCurrentPenalty()
{
	if (!NeedsComponent || !AbilitySystemComponent || !bApplyPenaltyToAbilitySystem)
	{
		return;
	}

	constexpr float WriteTolerance = 0.001f;

	TGuardValue<bool> ApplyingPenaltyGuard(bApplyingPenalty, true);
	for (FProjectSurvivalResolvedAttributeBinding& Binding : ResolvedBindings)
	{
		if (!Binding.bResolved)
		{
			continue;
		}

		const float CurrentValue = ReadBindingCurrentValue(Binding);
		if (!Binding.bHasBaselineValue)
		{
			Binding.BaselineValue = CurrentValue - Binding.AppliedModifierValue;
			Binding.bHasBaselineValue = true;
		}

		const float TargetValue = Binding.BaselineValue * GetCombinedAttributeMultiplier(Binding.LogicalAttributeName);

		if (Binding.bGameplayAttributeData)
		{
			const float DesiredModifierValue = TargetValue - Binding.BaselineValue;
			const float ModifierDelta = DesiredModifierValue - Binding.AppliedModifierValue;
			if (!FMath::IsNearlyEqual(ModifierDelta, 0.f, WriteTolerance))
			{
				ApplyBindingModifierDelta(Binding, ModifierDelta);
			}

			Binding.AppliedModifierValue = DesiredModifierValue;
		}
		else if (Binding.bFloatProperty && Binding.AttributeSet.IsValid())
		{
			const float DesiredModifierValue = TargetValue - Binding.BaselineValue;
			const float ModifierDelta = DesiredModifierValue - Binding.AppliedModifierValue;
			if (!FMath::IsNearlyEqual(ModifierDelta, 0.f, WriteTolerance))
			{
				ApplyBindingModifierDelta(Binding, ModifierDelta);
			}

			Binding.AppliedModifierValue = DesiredModifierValue;
		}
	}
}

float UProjectSurvivalAttributeBridgeComponent::GetCurrentPenaltyMultiplier() const
{
	return NeedsComponent ? NeedsComponent->GetNeedsPenaltyMultiplier() : 1.f;
}

float UProjectSurvivalAttributeBridgeComponent::GetCombinedAttributeMultiplier(const FName LogicalAttributeName) const
{
	float CombinedMultiplier = GetCurrentPenaltyMultiplier();

	for (const TPair<FName, TMap<FName, float>>& SourcePair : ExternalAttributeMultipliersBySource)
	{
		if (const float* Multiplier = SourcePair.Value.Find(LogicalAttributeName))
		{
			CombinedMultiplier *= *Multiplier;
		}
	}

	return CombinedMultiplier;
}

bool UProjectSurvivalAttributeBridgeComponent::ResolveAttributeBinding(FProjectSurvivalResolvedAttributeBinding& Binding)
{
	if (!AbilitySystemComponent)
	{
		return false;
	}

	const TArray<UAttributeSet*>& AttributeSets = AbilitySystemComponent->GetSpawnedAttributes();
	const TArray<FString> CandidateNames = GetCandidatePropertyNames(Binding.LogicalAttributeName);

	for (UAttributeSet* AttributeSet : AttributeSets)
	{
		if (!AttributeSet)
		{
			continue;
		}

		for (TFieldIterator<FProperty> It(AttributeSet->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !PropertyNameMatches(Property->GetFName(), CandidateNames))
			{
				continue;
			}

			Binding.AttributeSet = AttributeSet;
			Binding.ResolvedPropertyName = Property->GetFName();
			Binding.ResolvedProperty = Property;
			Binding.bResolved = true;

			if (IsGameplayAttributeDataProperty(Property))
			{
				Binding.bGameplayAttributeData = true;
				Binding.GameplayAttribute = FGameplayAttribute(Property);
				Binding.BaselineValue = AbilitySystemComponent->GetNumericAttribute(Binding.GameplayAttribute);
			}
			else if (CastField<FFloatProperty>(Property))
			{
				Binding.bFloatProperty = true;
				Binding.BaselineValue = CastFieldChecked<FFloatProperty>(Property)->GetPropertyValue_InContainer(AttributeSet);
			}

			Binding.bHasBaselineValue = true;
			Binding.AppliedModifierValue = 0.f;

			if (bLogDiscoveryToOutputLog)
			{
				UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalBridge] Resolved %s -> %s.%s (%s) baseline=%.3f"),
					*Binding.LogicalAttributeName.ToString(),
					*AttributeSet->GetClass()->GetName(),
					*Binding.ResolvedPropertyName.ToString(),
					Binding.bGameplayAttributeData ? TEXT("GameplayAttributeData") : TEXT("Float"),
					Binding.BaselineValue);
			}

			return true;
		}
	}

	if (bLogDiscoveryToOutputLog)
	{
		UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalBridge] Could not resolve attribute binding for %s"), *Binding.LogicalAttributeName.ToString());
	}

	return false;
}

float UProjectSurvivalAttributeBridgeComponent::ReadBindingBaseValue(const FProjectSurvivalResolvedAttributeBinding& Binding) const
{
	if (!Binding.bResolved)
	{
		return 0.f;
	}

	if (Binding.bGameplayAttributeData && AbilitySystemComponent)
	{
		return AbilitySystemComponent->GetNumericAttributeBase(Binding.GameplayAttribute);
	}

	if (Binding.bFloatProperty && Binding.AttributeSet.IsValid())
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.ResolvedProperty))
		{
			return FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
		}
	}

	return 0.f;
}

float UProjectSurvivalAttributeBridgeComponent::ReadBindingCurrentValue(const FProjectSurvivalResolvedAttributeBinding& Binding) const
{
	if (!Binding.bResolved)
	{
		return 0.f;
	}

	if (Binding.bGameplayAttributeData && AbilitySystemComponent)
	{
		return AbilitySystemComponent->GetNumericAttribute(Binding.GameplayAttribute);
	}

	if (Binding.bFloatProperty && Binding.AttributeSet.IsValid())
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.ResolvedProperty))
		{
			return FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
		}
	}

	return 0.f;
}

bool UProjectSurvivalAttributeBridgeComponent::ApplyBindingModifierDelta(FProjectSurvivalResolvedAttributeBinding& Binding, float DeltaValue)
{
	if (!Binding.bResolved || FMath::IsNearlyZero(DeltaValue, KINDA_SMALL_NUMBER))
	{
		return false;
	}

	if (Binding.bGameplayAttributeData && AbilitySystemComponent)
	{
		if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
		{
			AbilitySystemComponent->ApplyModToAttribute(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaValue);
		}
		else
		{
			AbilitySystemComponent->ApplyModToAttributeUnsafe(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaValue);
		}

		return true;
	}

	if (Binding.bFloatProperty && Binding.AttributeSet.IsValid())
	{
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.ResolvedProperty))
		{
			const float CurrentValue = FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
			FloatProperty->SetPropertyValue_InContainer(Binding.AttributeSet.Get(), CurrentValue + DeltaValue);
			return true;
		}
	}

	return false;
}

FProjectSurvivalResolvedAttributeBinding* UProjectSurvivalAttributeBridgeComponent::FindBindingByAttribute(const FGameplayAttribute& Attribute)
{
	return ResolvedBindings.FindByPredicate([&Attribute](const FProjectSurvivalResolvedAttributeBinding& Binding)
	{
		return Binding.bGameplayAttributeData && Binding.GameplayAttribute == Attribute;
	});
}

const FProjectSurvivalResolvedAttributeBinding* UProjectSurvivalAttributeBridgeComponent::FindBindingByAttribute(const FGameplayAttribute& Attribute) const
{
	return ResolvedBindings.FindByPredicate([&Attribute](const FProjectSurvivalResolvedAttributeBinding& Binding)
	{
		return Binding.bGameplayAttributeData && Binding.GameplayAttribute == Attribute;
	});
}
