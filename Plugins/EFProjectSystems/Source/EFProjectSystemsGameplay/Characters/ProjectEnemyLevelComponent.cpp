#include "Characters/ProjectEnemyLevelComponent.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyScalingMath.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "Survival/ProjectRuntimeReflectionLibrary.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEnemyLevelComponent, Log, All);

namespace ProjectEnemyLevelComponentPrivate
{
	struct FResolvedAttributeBinding
	{
		FName LogicalAttributeName = NAME_None;
		TWeakObjectPtr<UAttributeSet> AttributeSet;
		FProperty* Property = nullptr;
		FGameplayAttribute GameplayAttribute;
		bool bIsGameplayAttribute = false;
	};

	struct FResolvedHealthBindings
	{
		FResolvedAttributeBinding CurrentHealthBinding;
		FResolvedAttributeBinding MaxHealthBinding;
		bool bHasCurrentHealthBinding = false;
		bool bHasSeparateMaxHealthBinding = false;
	};

	struct FTargetHealthSnapshot
	{
		bool bHasCurrentHealth = false;
		bool bHasMaxHealth = false;
		bool bHasHealthRatio = false;
		float CurrentHealth = 0.0f;
		float MaxHealth = 0.0f;
		float HealthRatio = 1.0f;
	};

	static const FGameplayTag& GetHealthStatisticTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("RPG.Statistics.Health")), false);
		return Tag;
	}

	static FString NormalizeName(const FString& Value)
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

	static TArray<FString> GetCandidatePropertyNames(const FName LogicalAttributeName)
	{
		if (LogicalAttributeName == TEXT("Health"))
		{
			return { TEXT("Health"), TEXT("CurrentHealth") };
		}

		if (LogicalAttributeName == TEXT("MaxHealth"))
		{
			return { TEXT("MaxHealth"), TEXT("MaximumHealth"), TEXT("HealthMax"), TEXT("HealthMaximum") };
		}

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

		return { LogicalAttributeName.ToString() };
	}

	static bool PropertyNameMatches(const FName PropertyName, const TArray<FString>& CandidateNames)
	{
		const FString NormalizedPropertyName = NormalizeName(PropertyName.ToString());
		for (const FString& CandidateName : CandidateNames)
		{
			if (NormalizedPropertyName == NormalizeName(CandidateName))
			{
				return true;
			}
		}

		return false;
	}

	static bool IsGameplayAttributeDataProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == FGameplayAttributeData::StaticStruct();
	}

	template <typename ComponentType>
	static ComponentType* FindComponentByClassHint(AActor* Owner, const TArray<FString>& ClassHints)
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

	static UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* AbilitySystemComponent = Owner->FindComponentByClass<UAbilitySystemComponent>())
		{
			return AbilitySystemComponent;
		}

		return FindComponentByClassHint<UAbilitySystemComponent>(Owner, { TEXT("AbilitySystemComponent"), TEXT("ACFAbilitySystemComponent") });
	}

	static UObject* ResolveStatisticsComponent(AActor* Owner)
	{
		return FindComponentByClassHint<UObject>(Owner, { TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent") });
	}

	static bool ResolveAttributeBinding(
		UAbilitySystemComponent* AbilitySystemComponent,
		const FName LogicalAttributeName,
		FResolvedAttributeBinding& OutBinding)
	{
		if (!AbilitySystemComponent)
		{
			return false;
		}

		const TArray<UAttributeSet*>& AttributeSets = AbilitySystemComponent->GetSpawnedAttributes();
		const TArray<FString> CandidateNames = GetCandidatePropertyNames(LogicalAttributeName);

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

				OutBinding.LogicalAttributeName = LogicalAttributeName;
				OutBinding.AttributeSet = AttributeSet;
				OutBinding.Property = Property;
				OutBinding.bIsGameplayAttribute = IsGameplayAttributeDataProperty(Property);
				if (OutBinding.bIsGameplayAttribute)
				{
					OutBinding.GameplayAttribute = FGameplayAttribute(Property);
				}

				return true;
			}
		}

		return false;
	}

	static bool ResolveHealthBindings(UAbilitySystemComponent* AbilitySystemComponent, FResolvedHealthBindings& OutBindings)
	{
		if (!AbilitySystemComponent)
		{
			return false;
		}

		OutBindings = FResolvedHealthBindings();
		OutBindings.bHasCurrentHealthBinding = ResolveAttributeBinding(AbilitySystemComponent, TEXT("Health"), OutBindings.CurrentHealthBinding);
		OutBindings.bHasSeparateMaxHealthBinding = ResolveAttributeBinding(AbilitySystemComponent, TEXT("MaxHealth"), OutBindings.MaxHealthBinding);

		if (OutBindings.bHasCurrentHealthBinding && OutBindings.bHasSeparateMaxHealthBinding
			&& OutBindings.CurrentHealthBinding.AttributeSet == OutBindings.MaxHealthBinding.AttributeSet
			&& OutBindings.CurrentHealthBinding.Property == OutBindings.MaxHealthBinding.Property)
		{
			OutBindings.bHasSeparateMaxHealthBinding = false;
		}

		return OutBindings.bHasCurrentHealthBinding;
	}

	static float ReadCurrentValue(UAbilitySystemComponent* AbilitySystemComponent, const FResolvedAttributeBinding& Binding)
	{
		if (Binding.bIsGameplayAttribute && AbilitySystemComponent)
		{
			return AbilitySystemComponent->GetNumericAttribute(Binding.GameplayAttribute);
		}

		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.Property))
		{
			if (Binding.AttributeSet.IsValid())
			{
				return FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
			}
		}

		return 0.0f;
	}

	static float ReadBaseValue(UAbilitySystemComponent* AbilitySystemComponent, const FResolvedAttributeBinding& Binding)
	{
		if (Binding.bIsGameplayAttribute && AbilitySystemComponent)
		{
			return AbilitySystemComponent->GetNumericAttributeBase(Binding.GameplayAttribute);
		}

		return ReadCurrentValue(AbilitySystemComponent, Binding);
	}

	static bool ApplyDelta(UAbilitySystemComponent* AbilitySystemComponent, AActor* Owner, const FResolvedAttributeBinding& Binding, const float DeltaValue)
	{
		if (FMath::IsNearlyZero(DeltaValue, KINDA_SMALL_NUMBER))
		{
			return true;
		}

		if (Binding.bIsGameplayAttribute && AbilitySystemComponent)
		{
			if (Owner && Owner->HasAuthority())
			{
				AbilitySystemComponent->ApplyModToAttribute(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaValue);
			}
			else
			{
				AbilitySystemComponent->ApplyModToAttributeUnsafe(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaValue);
			}

			return true;
		}

		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.Property))
		{
			if (Binding.AttributeSet.IsValid())
			{
				const float CurrentValue = FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
				FloatProperty->SetPropertyValue_InContainer(Binding.AttributeSet.Get(), CurrentValue + DeltaValue);
				return true;
			}
		}

		return false;
	}

	static bool SetAbsoluteBaseValue(UAbilitySystemComponent* AbilitySystemComponent, const FResolvedAttributeBinding& Binding, const float TargetValue)
	{
		if (Binding.bIsGameplayAttribute && AbilitySystemComponent)
		{
			AbilitySystemComponent->SetNumericAttributeBase(Binding.GameplayAttribute, TargetValue);
			return true;
		}

		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.Property))
		{
			if (Binding.AttributeSet.IsValid())
			{
				FloatProperty->SetPropertyValue_InContainer(Binding.AttributeSet.Get(), TargetValue);
				return true;
			}
		}

		return false;
	}

	static bool SetAbsoluteCurrentValue(
		UAbilitySystemComponent* AbilitySystemComponent,
		AActor* Owner,
		const FResolvedAttributeBinding& Binding,
		const float TargetValue)
	{
		if (Binding.bIsGameplayAttribute && AbilitySystemComponent)
		{
			return ApplyDelta(AbilitySystemComponent, Owner, Binding, TargetValue - ReadCurrentValue(AbilitySystemComponent, Binding));
		}

		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.Property))
		{
			if (Binding.AttributeSet.IsValid())
			{
				FloatProperty->SetPropertyValue_InContainer(Binding.AttributeSet.Get(), TargetValue);
				return true;
			}
		}

		return false;
	}

	static FProperty* FindReturnProperty(UFunction* Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (FProperty* Property = *It; Property && Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return Property;
			}
		}

		return nullptr;
	}

	static void GatherInputProperties(UFunction* Function, TArray<FProperty*>& OutInputProperties)
	{
		OutInputProperties.Reset();
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				OutInputProperties.Add(Property);
			}
		}
	}

	static bool SetIdentityValue(
		FProperty* InputProperty,
		void* Parms,
		const FGameplayTag& StatisticTag,
		const FName StatisticName,
		const FString& StatisticString)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(InputProperty))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				*StructProperty->ContainerPtrToValuePtr<FGameplayTag>(Parms) = StatisticTag;
				return true;
			}

			return false;
		}

		if (FNameProperty* NameProperty = CastField<FNameProperty>(InputProperty))
		{
			NameProperty->SetPropertyValue_InContainer(Parms, StatisticName);
			return true;
		}

		if (FStrProperty* StringProperty = CastField<FStrProperty>(InputProperty))
		{
			StringProperty->SetPropertyValue_InContainer(Parms, StatisticString);
			return true;
		}

		return false;
	}

	static TArray<FString> GetCandidateAttributeIdentityStrings(const FName LogicalAttributeName)
	{
		if (LogicalAttributeName == TEXT("MeleeDamage"))
		{
			return { TEXT("RPG.Attributes.MeleeDamage"), TEXT("RPG.Parameters.MeleeDamage") };
		}

		if (LogicalAttributeName == TEXT("RangedDamage"))
		{
			return { TEXT("RPG.Attributes.RangedDamage"), TEXT("RPG.Parameters.RangedDamage") };
		}

		if (LogicalAttributeName == TEXT("SpellDamage"))
		{
			return {
				TEXT("RPG.Attributes.SpellDamage"),
				TEXT("RPG.Parameters.SpellDamage"),
				TEXT("RPG.Attributes.MagicDamage"),
				TEXT("RPG.Parameters.MagicDamage")
			};
		}

		if (LogicalAttributeName == TEXT("PhysicalDefense"))
		{
			return {
				TEXT("RPG.Attributes.Defense"),
				TEXT("RPG.Parameters.Defense"),
				TEXT("RPG.Attributes.PhysicalDefense"),
				TEXT("RPG.Parameters.PhysicalDefense")
			};
		}

		if (LogicalAttributeName == TEXT("SpellDefense"))
		{
			return {
				TEXT("RPG.Attributes.SpellDefense"),
				TEXT("RPG.Parameters.SpellDefense"),
				TEXT("RPG.Attributes.MagicDefense"),
				TEXT("RPG.Parameters.MagicDefense"),
				TEXT("RPG.Attributes.MagicDefence"),
				TEXT("RPG.Parameters.MagicDefence")
			};
		}

		return {};
	}

	static bool SetNumericInputValue(FProperty* InputProperty, void* Parms, const float NumericValue)
	{
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(InputProperty))
		{
			FloatProperty->SetPropertyValue_InContainer(Parms, NumericValue);
			return true;
		}

		if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(InputProperty))
		{
			DoubleProperty->SetPropertyValue_InContainer(Parms, static_cast<double>(NumericValue));
			return true;
		}

		if (FIntProperty* IntProperty = CastField<FIntProperty>(InputProperty))
		{
			IntProperty->SetPropertyValue_InContainer(Parms, FMath::RoundToInt(NumericValue));
			return true;
		}

		return false;
	}

	static bool TryReadNumericReturnValue(FProperty* ReturnProperty, void* Parms, float& OutValue)
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(ReturnProperty))
		{
			OutValue = FloatProperty->GetPropertyValue_InContainer(Parms);
			return true;
		}

		if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(ReturnProperty))
		{
			OutValue = static_cast<float>(DoubleProperty->GetPropertyValue_InContainer(Parms));
			return true;
		}

		if (const FIntProperty* IntProperty = CastField<FIntProperty>(ReturnProperty))
		{
			OutValue = static_cast<float>(IntProperty->GetPropertyValue_InContainer(Parms));
			return true;
		}

		return false;
	}

	static bool TryInvokeStatisticFloatFunction(
		UObject* Target,
		const FName FunctionName,
		const FGameplayTag& StatisticTag,
		const FName StatisticName,
		const FString& StatisticString,
		float& OutReturnValue)
	{
		OutReturnValue = 0.0f;

		if (!Target || FunctionName.IsNone())
		{
			return false;
		}

		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function || Function->ParmsSize <= 0)
		{
			return false;
		}

		TArray<FProperty*> InputProperties;
		GatherInputProperties(Function, InputProperties);
		FProperty* ReturnProperty = FindReturnProperty(Function);
		if (InputProperties.Num() != 1 || !ReturnProperty)
		{
			return false;
		}

		void* Parms = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parms, Function->ParmsSize);
		if (!SetIdentityValue(InputProperties[0], Parms, StatisticTag, StatisticName, StatisticString))
		{
			return false;
		}

		Target->ProcessEvent(Function, Parms);
		return TryReadNumericReturnValue(ReturnProperty, Parms, OutReturnValue);
	}

	static bool TryInvokeStatisticMutationFunction(
		UObject* Target,
		const FName FunctionName,
		const FGameplayTag& StatisticTag,
		const FName StatisticName,
		const FString& StatisticString,
		const float NumericValue)
	{
		if (!Target || FunctionName.IsNone())
		{
			return false;
		}

		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function || Function->ParmsSize <= 0)
		{
			return false;
		}

		TArray<FProperty*> InputProperties;
		GatherInputProperties(Function, InputProperties);
		if (InputProperties.Num() != 2)
		{
			return false;
		}

		void* Parms = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parms, Function->ParmsSize);
		if (!SetIdentityValue(InputProperties[0], Parms, StatisticTag, StatisticName, StatisticString))
		{
			return false;
		}

		if (!SetNumericInputValue(InputProperties[1], Parms, NumericValue))
		{
			return false;
		}

		Target->ProcessEvent(Function, Parms);

		if (FBoolProperty* BoolReturn = CastField<FBoolProperty>(FindReturnProperty(Function)))
		{
			return BoolReturn->GetPropertyValue_InContainer(Parms);
		}

		return true;
	}

	static bool TryReadHealthFromStatisticsComponent(AActor* Owner, FTargetHealthSnapshot& OutSnapshot)
	{
		UObject* StatisticsComponent = ResolveStatisticsComponent(Owner);
		if (!StatisticsComponent)
		{
			return false;
		}

		const FGameplayTag& HealthTag = GetHealthStatisticTag();
		const FName HealthName(TEXT("Health"));
		const FString HealthString(TEXT("RPG.Statistics.Health"));

		float CurrentHealth = 0.0f;
		if (TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetCurrentValueForStatitstic"), HealthTag, HealthName, HealthString, CurrentHealth))
		{
			OutSnapshot.bHasCurrentHealth = true;
			OutSnapshot.CurrentHealth = CurrentHealth;
		}

		float MaxHealth = 0.0f;
		if (TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetMaxValueForStatitstic"), HealthTag, HealthName, HealthString, MaxHealth))
		{
			OutSnapshot.bHasMaxHealth = true;
			OutSnapshot.MaxHealth = MaxHealth;
		}

		float HealthRatio = 0.0f;
		if (TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetNormalizedValueForStatitstic"), HealthTag, HealthName, HealthString, HealthRatio))
		{
			OutSnapshot.bHasHealthRatio = true;
			OutSnapshot.HealthRatio = FMath::Clamp(HealthRatio, 0.0f, 1.0f);
		}

		if (!OutSnapshot.bHasHealthRatio && OutSnapshot.bHasCurrentHealth && OutSnapshot.bHasMaxHealth && OutSnapshot.MaxHealth > KINDA_SMALL_NUMBER)
		{
			OutSnapshot.bHasHealthRatio = true;
			OutSnapshot.HealthRatio = FMath::Clamp(OutSnapshot.CurrentHealth / OutSnapshot.MaxHealth, 0.0f, 1.0f);
		}

		return OutSnapshot.bHasCurrentHealth || OutSnapshot.bHasMaxHealth || OutSnapshot.bHasHealthRatio;
	}

	static bool TryReadCurrentAttributeFromStatisticsComponent(AActor* Owner, const FName LogicalAttributeName, float& OutValue)
	{
		OutValue = 0.0f;

		UObject* StatisticsComponent = ResolveStatisticsComponent(Owner);
		if (!StatisticsComponent)
		{
			return false;
		}

		const TArray<FString> CandidateStrings = GetCandidateAttributeIdentityStrings(LogicalAttributeName);
		for (const FString& CandidateString : CandidateStrings)
		{
			const FGameplayTag CandidateTag = FGameplayTag::RequestGameplayTag(FName(*CandidateString), false);
			const int32 FinalSeparatorIndex = CandidateString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			const FName CandidateName = FinalSeparatorIndex != INDEX_NONE
				? FName(*CandidateString.RightChop(FinalSeparatorIndex + 1))
				: FName(*CandidateString);

			if (TryInvokeStatisticFloatFunction(
				StatisticsComponent,
				TEXT("GetCurrentAttributeValue"),
				CandidateTag,
				CandidateName,
				CandidateString,
				OutValue))
			{
				return true;
			}

			if (TryInvokeStatisticFloatFunction(
				StatisticsComponent,
				TEXT("GetCurrentPrimaryAttributeValue"),
				CandidateTag,
				CandidateName,
				CandidateString,
				OutValue))
			{
				return true;
			}
		}

		return false;
	}

	static bool TryReadEffectiveCombatChannelValue(
		AActor* Owner,
		UAbilitySystemComponent* AbilitySystemComponent,
		const FName LogicalAttributeName,
		float& OutValue)
	{
		FResolvedAttributeBinding Binding;
		const bool bHasBinding = ResolveAttributeBinding(AbilitySystemComponent, LogicalAttributeName, Binding);
		if (TryReadCurrentAttributeFromStatisticsComponent(Owner, LogicalAttributeName, OutValue))
		{
			if (bHasBinding && FMath::IsNearlyZero(OutValue, KINDA_SMALL_NUMBER))
			{
				const float BindingValue = ReadCurrentValue(AbilitySystemComponent, Binding);
				if (!FMath::IsNearlyZero(BindingValue, KINDA_SMALL_NUMBER))
				{
					OutValue = BindingValue;
				}
			}

			return true;
		}

		if (!bHasBinding)
		{
			return false;
		}

		OutValue = ReadCurrentValue(AbilitySystemComponent, Binding);
		return true;
	}

	static bool TryCaptureHealthBaseline(
		AActor* Owner,
		UAbilitySystemComponent* AbilitySystemComponent,
		float& OutBaselineMaxHealth,
		float& OutBaselineHealthRatio)
	{
		OutBaselineMaxHealth = 0.0f;
		OutBaselineHealthRatio = 1.0f;

		FTargetHealthSnapshot Snapshot;
		TryReadHealthFromStatisticsComponent(Owner, Snapshot);

		FResolvedHealthBindings HealthBindings;
		if (ResolveHealthBindings(AbilitySystemComponent, HealthBindings))
		{
			if (!Snapshot.bHasMaxHealth)
			{
				const FResolvedAttributeBinding& MaxBinding = HealthBindings.bHasSeparateMaxHealthBinding
					? HealthBindings.MaxHealthBinding
					: HealthBindings.CurrentHealthBinding;
				Snapshot.bHasMaxHealth = true;
				Snapshot.MaxHealth = ReadBaseValue(AbilitySystemComponent, MaxBinding);
			}

			if (!Snapshot.bHasCurrentHealth)
			{
				Snapshot.bHasCurrentHealth = true;
				Snapshot.CurrentHealth = ReadCurrentValue(AbilitySystemComponent, HealthBindings.CurrentHealthBinding);
			}
		}

		if (!Snapshot.bHasHealthRatio && Snapshot.bHasCurrentHealth && Snapshot.bHasMaxHealth && Snapshot.MaxHealth > KINDA_SMALL_NUMBER)
		{
			Snapshot.bHasHealthRatio = true;
			Snapshot.HealthRatio = FMath::Clamp(Snapshot.CurrentHealth / Snapshot.MaxHealth, 0.0f, 1.0f);
		}

		if (!Snapshot.bHasMaxHealth || Snapshot.MaxHealth <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		OutBaselineMaxHealth = Snapshot.MaxHealth;
		OutBaselineHealthRatio = Snapshot.bHasHealthRatio ? FMath::Clamp(Snapshot.HealthRatio, 0.0f, 1.0f) : 1.0f;
		return true;
	}

	static bool TryNudgeStatisticsHealthCurrent(AActor* Owner, const float DesiredCurrentHealth)
	{
		UObject* StatisticsComponent = ResolveStatisticsComponent(Owner);
		if (!StatisticsComponent)
		{
			return false;
		}

		const FGameplayTag& HealthTag = GetHealthStatisticTag();
		const FName HealthName(TEXT("Health"));
		const FString HealthString(TEXT("RPG.Statistics.Health"));

		float CurrentHealth = 0.0f;
		if (!TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetCurrentValueForStatitstic"), HealthTag, HealthName, HealthString, CurrentHealth))
		{
			return false;
		}

		const float DeltaValue = DesiredCurrentHealth - CurrentHealth;
		if (FMath::IsNearlyZero(DeltaValue, KINDA_SMALL_NUMBER))
		{
			return true;
		}

		return TryInvokeStatisticMutationFunction(StatisticsComponent, TEXT("ModifyStat"), HealthTag, HealthName, HealthString, DeltaValue)
			|| TryInvokeStatisticMutationFunction(StatisticsComponent, TEXT("ModifyStatistic"), HealthTag, HealthName, HealthString, DeltaValue);
	}

	static bool IsDamageChannel(const FName LogicalAttributeName)
	{
		return LogicalAttributeName == TEXT("MeleeDamage")
			|| LogicalAttributeName == TEXT("RangedDamage")
			|| LogicalAttributeName == TEXT("SpellDamage");
	}

	static bool IsDefenseChannel(const FName LogicalAttributeName)
	{
		return LogicalAttributeName == TEXT("PhysicalDefense")
			|| LogicalAttributeName == TEXT("SpellDefense");
	}

	static float GetPerLevelMultiplierForChannel(const FName LogicalAttributeName, const UProjectEnemyLevelSettings& Settings)
	{
		if (LogicalAttributeName == TEXT("Health"))
		{
			return Settings.HealthPerLevel;
		}

		if (IsDamageChannel(LogicalAttributeName))
		{
			return Settings.DamagePerLevel;
		}

		if (IsDefenseChannel(LogicalAttributeName))
		{
			return Settings.DefensePerLevel;
		}

		return 0.0f;
	}

	static TArray<FName> GetPreferredOffensiveChannels(const AActor* Owner)
	{
		const FString OwnerClassName = Owner && Owner->GetClass() ? NormalizeName(Owner->GetClass()->GetName()) : FString();
		if (OwnerClassName.Contains(TEXT("mage")))
		{
			return { TEXT("SpellDamage"), TEXT("MeleeDamage") };
		}

		if (OwnerClassName.Contains(TEXT("ranged")))
		{
			return { TEXT("RangedDamage"), TEXT("MeleeDamage") };
		}

		return { TEXT("MeleeDamage") };
	}

	static UClass* ResolveTargetPointComponentClass()
	{
		return LoadClass<UActorComponent>(nullptr, TEXT("/Script/AscentTargetingSystem.ATSTargetPointComponent"));
	}

	static USceneComponent* ResolveAttachParent(AActor* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}

		if (ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (USkeletalMeshComponent* Mesh = Character->GetMesh())
			{
				return Mesh;
			}
		}

		if (USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
		{
			return Mesh;
		}

		return Owner->GetRootComponent();
	}

	static FName ResolveBestSocketName(USceneComponent* AttachParent, const UProjectEnemyLevelSettings& Settings)
	{
		const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(AttachParent);
		if (!SkeletalMeshComponent)
		{
			return NAME_None;
		}

		for (const FName SocketName : Settings.PreferredTargetPointSockets)
		{
			if (!SocketName.IsNone() && SkeletalMeshComponent->DoesSocketExist(SocketName))
			{
				return SocketName;
			}
		}

		return NAME_None;
	}
}

int32 UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(const int32 LogicalLevel)
{
	return LogicalLevel > 0 ? FMath::Min(LogicalLevel, 100) : 0;
}

UProjectEnemyLevelComponent::UProjectEnemyLevelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProjectEnemyLevelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, AssignedLevel);
	DOREPLIFETIME(ThisClass, WorldTier);
	DOREPLIFETIME(ThisClass, MinRolledLevel);
	DOREPLIFETIME(ThisClass, MaxRolledLevel);
	DOREPLIFETIME(ThisClass, NormalizedLevel);
	DOREPLIFETIME(ThisClass, bLevelAssigned);
}

void UProjectEnemyLevelComponent::SetAssignedLevelData(
	const int32 InWorldTier,
	const int32 InMinRolledLevel,
	const int32 InMaxRolledLevel,
	const int32 InAssignedLevel,
	const float InNormalizedLevel)
{
	WorldTier = FMath::Max(InWorldTier, 1);
	MinRolledLevel = FMath::Max(InMinRolledLevel, 1);
	MaxRolledLevel = FMath::Max(InMaxRolledLevel, MinRolledLevel);
	AssignedLevel = FMath::Clamp(InAssignedLevel, MinRolledLevel, MaxRolledLevel);
	NormalizedLevel = FMath::Clamp(InNormalizedLevel, 0.0f, 1.0f);
	bLevelAssigned = AssignedLevel > 0;
	bGameplayScalingApplied = false;
	GameplayScaledLevel = INDEX_NONE;
	bAscentSyncEvaluated = false;
	bAscentSyncSatisfied = false;
	LastPhysicalAscentLevel = INDEX_NONE;
}

bool UProjectEnemyLevelComponent::SyncAssignedLevelToAscent(FString& OutDiagnosticMessage)
{
	OutDiagnosticMessage.Reset();
	bAscentSyncEvaluated = true;
	bAscentSyncSatisfied = false;
	LastPhysicalAscentLevel = ResolvePhysicalAscentLevel(AssignedLevel);

	if (!bLevelAssigned || LastPhysicalAscentLevel <= 0)
	{
		OutDiagnosticMessage = TEXT("No assigned level is available yet.");
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutDiagnosticMessage = TEXT("Owning actor is null.");
		return false;
	}

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	if (!Settings)
	{
		OutDiagnosticMessage = TEXT("ProjectEnemyLevel settings are unavailable.");
		return false;
	}

	const bool bShouldSyncLevelingComponent = Settings->bSyncAssignedLevelToAscentLevelingComponent;
	const bool bShouldReinitializeStatistics = Settings->bReinitializeAscentStatisticsOnLevelSync;
	if (!bShouldSyncLevelingComponent && !bShouldReinitializeStatistics)
	{
		// The project-owned component remains authoritative when ACF synchronization
		// is disabled. Recording the capped physical value still lets the Director
		// verify that no Winter level could be forwarded above 100.
		bAscentSyncSatisfied = true;
		OutDiagnosticMessage = TEXT("Ascent level synchronization is disabled; using project-owned enemy level only.");
		return false;
	}

	UActorComponent* LevelingComponent = ProjectEnemyLevelComponentPrivate::FindComponentByClassHint<UActorComponent>(Owner, { TEXT("ARSLevelingComponent") });
	UActorComponent* StatisticsComponent = ProjectEnemyLevelComponentPrivate::FindComponentByClassHint<UActorComponent>(Owner, { TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent") });

	// V4 Winter levels remain authoritative in this project-owned component and
	// its linear scaling. ACF data/curves have only been certified through level
	// 100, so never pass a larger physical level into Marketplace components.
	const int32 PhysicalAscentLevel = LastPhysicalAscentLevel;
	bool bLevelingSyncSucceeded = !bShouldSyncLevelingComponent;
	if (bShouldSyncLevelingComponent && LevelingComponent)
	{
		bLevelingSyncSucceeded = UProjectRuntimeReflectionLibrary::InvokeInt32Function(
			LevelingComponent,
			TEXT("ForceSetLevel"),
			PhysicalAscentLevel);
	}

	bool bStatisticsSyncSucceeded = !bShouldReinitializeStatistics;
	if (bShouldReinitializeStatistics && StatisticsComponent)
	{
		bStatisticsSyncSucceeded = UProjectRuntimeReflectionLibrary::InvokeInt32Function(
				StatisticsComponent,
				TEXT("SetNewLevelAndReinitialize"),
				PhysicalAscentLevel);
		if (!bStatisticsSyncSucceeded)
		{
			bStatisticsSyncSucceeded = UProjectRuntimeReflectionLibrary::InvokeNoArgFunction(
				StatisticsComponent,
				TEXT("SetNewLevelAndReinitialize"));
		}
	}

	int32 ReflectedLevel = INDEX_NONE;
	bool bPhysicalLevelVerified = !bShouldSyncLevelingComponent;
	if (bShouldSyncLevelingComponent
		&& LevelingComponent
		&& bLevelingSyncSucceeded
		&& UProjectRuntimeReflectionLibrary::InvokeInt32ReturnFunction(
			LevelingComponent,
			TEXT("GetCurrentLevel"),
			ReflectedLevel))
	{
		bPhysicalLevelVerified = ReflectedLevel == PhysicalAscentLevel;
	}

	bAscentSyncSatisfied = bLevelingSyncSucceeded
		&& bStatisticsSyncSucceeded
		&& bPhysicalLevelVerified;
	if (bAscentSyncSatisfied)
	{
		OutDiagnosticMessage = FString::Printf(
			TEXT("ARS physical level synchronized to %d (project logical level %d)."),
			PhysicalAscentLevel,
			AssignedLevel);
		return true;
	}

	if (bShouldSyncLevelingComponent && ReflectedLevel != INDEX_NONE && ReflectedLevel != PhysicalAscentLevel)
	{
		OutDiagnosticMessage = FString::Printf(
			TEXT("ARS physical level mismatch: expected %d, observed %d (logical level %d)."),
			PhysicalAscentLevel,
			ReflectedLevel,
			AssignedLevel);
		return false;
	}

	OutDiagnosticMessage = FString::Printf(
		TEXT("Enabled ARS synchronization failed (leveling=%s, statistics=%s, readback=%s)."),
		bLevelingSyncSucceeded ? TEXT("ready") : TEXT("failed"),
		bStatisticsSyncSucceeded ? TEXT("ready") : TEXT("failed"),
		bPhysicalLevelVerified ? TEXT("ready") : TEXT("failed"));
	return false;
}

bool UProjectEnemyLevelComponent::CaptureGameplayScalingBaseline(const UProjectEnemyLevelSettings& Settings, FString& OutFailureReason)
{
	OutFailureReason.Reset();

	if (bScalingBaselineCaptured)
	{
		return true;
	}

	if (!bLevelAssigned)
	{
		OutFailureReason = TEXT("Enemy level data has not been assigned yet.");
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutFailureReason = TEXT("Owning actor is null.");
		return false;
	}

	UAbilitySystemComponent* AbilitySystemComponent = ProjectEnemyLevelComponentPrivate::ResolveAbilitySystemComponent(Owner);
	if (!AbilitySystemComponent)
	{
		OutFailureReason = TEXT("Could not resolve an AbilitySystemComponent on the enemy.");
		return false;
	}

	ScalingChannels.Reset();
	WarnedMissingChannels.Reset();
	AppliedSingleHealthCurrentDelta = 0.0f;
	BaselineHealthMax = 0.0f;
	BaselineHealthRatio = 1.0f;
	bHasHealthScalingBaseline = ProjectEnemyLevelComponentPrivate::TryCaptureHealthBaseline(
		Owner,
		AbilitySystemComponent,
		BaselineHealthMax,
		BaselineHealthRatio);

	const TArray<FName> ChannelsToCapture = {
		TEXT("MeleeDamage"),
		TEXT("RangedDamage"),
		TEXT("SpellDamage"),
		TEXT("PhysicalDefense"),
		TEXT("SpellDefense")
	};

	int32 CapturedChannelCount = 0;
	for (const FName LogicalAttributeName : ChannelsToCapture)
	{
		float BaselineValue = 0.0f;
		if (!ProjectEnemyLevelComponentPrivate::TryReadEffectiveCombatChannelValue(
			Owner,
			AbilitySystemComponent,
			LogicalAttributeName,
			BaselineValue))
		{
			continue;
		}

		FGameplayScalingChannelState& ChannelState = ScalingChannels.FindOrAdd(LogicalAttributeName);
		ChannelState.BaselineValue = BaselineValue;
		ChannelState.LastAppliedValue = ChannelState.BaselineValue;
		ChannelState.bCaptured = true;
		++CapturedChannelCount;
	}

	if (!bHasHealthScalingBaseline && CapturedChannelCount <= 0)
	{
		OutFailureReason = TEXT("Could not capture any baseline combat values for enemy level scaling.");
		return false;
	}

	bScalingBaselineCaptured = true;
	bGameplayScalingApplied = false;
	GameplayScaledLevel = INDEX_NONE;
	return true;
}

bool UProjectEnemyLevelComponent::ApplyGameplayScaling(const UProjectEnemyLevelSettings& Settings, FString& OutFailureReason)
{
	OutFailureReason.Reset();

	if (!bLevelAssigned)
	{
		OutFailureReason = TEXT("Enemy level data has not been assigned yet.");
		return false;
	}

	if (!bScalingBaselineCaptured && !CaptureGameplayScalingBaseline(Settings, OutFailureReason))
	{
		return false;
	}

	if (bGameplayScalingApplied && GameplayScaledLevel == AssignedLevel)
	{
		return true;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutFailureReason = TEXT("Owning actor is null.");
		return false;
	}

	UAbilitySystemComponent* AbilitySystemComponent = ProjectEnemyLevelComponentPrivate::ResolveAbilitySystemComponent(Owner);
	if (!AbilitySystemComponent)
	{
		OutFailureReason = TEXT("Could not resolve an AbilitySystemComponent on the enemy.");
		return false;
	}

	bool bAppliedHealth = false;
	bool bAppliedDefense = false;
	bool bAppliedPreferredOffense = false;

	if (bHasHealthScalingBaseline && BaselineHealthMax > KINDA_SMALL_NUMBER)
	{
		ProjectEnemyLevelComponentPrivate::FResolvedHealthBindings HealthBindings;
		if (ProjectEnemyLevelComponentPrivate::ResolveHealthBindings(AbilitySystemComponent, HealthBindings))
		{
			const FProjectEnemyScaledHealthValues HealthTargets = FProjectEnemyScalingMath::ScaleHealthFromBaseline(
				BaselineHealthMax,
				BaselineHealthRatio,
				AssignedLevel,
				Settings.HealthPerLevel);

			if (HealthBindings.bHasCurrentHealthBinding)
			{
				if (HealthBindings.CurrentHealthBinding.bIsGameplayAttribute
					&& !HealthBindings.bHasSeparateMaxHealthBinding
					&& !FMath::IsNearlyZero(AppliedSingleHealthCurrentDelta, KINDA_SMALL_NUMBER))
				{
					ProjectEnemyLevelComponentPrivate::ApplyDelta(
						AbilitySystemComponent,
						Owner,
						HealthBindings.CurrentHealthBinding,
						-AppliedSingleHealthCurrentDelta);
					AppliedSingleHealthCurrentDelta = 0.0f;
				}

				const ProjectEnemyLevelComponentPrivate::FResolvedAttributeBinding& MaxBinding = HealthBindings.bHasSeparateMaxHealthBinding
					? HealthBindings.MaxHealthBinding
					: HealthBindings.CurrentHealthBinding;

				const bool bAppliedMaxHealth = ProjectEnemyLevelComponentPrivate::SetAbsoluteBaseValue(
					AbilitySystemComponent,
					MaxBinding,
					HealthTargets.TargetMaxHealth);

				bool bAppliedCurrentHealth = false;
				float CurrentAdjustmentDelta = 0.0f;
				if (bAppliedMaxHealth)
				{
					const float CurrentAfterMaxUpdate = ProjectEnemyLevelComponentPrivate::ReadCurrentValue(
						AbilitySystemComponent,
						HealthBindings.CurrentHealthBinding);
					CurrentAdjustmentDelta = HealthTargets.TargetCurrentHealth - CurrentAfterMaxUpdate;
					bAppliedCurrentHealth = ProjectEnemyLevelComponentPrivate::SetAbsoluteCurrentValue(
						AbilitySystemComponent,
						Owner,
						HealthBindings.CurrentHealthBinding,
						HealthTargets.TargetCurrentHealth);
				}

				if (bAppliedMaxHealth && bAppliedCurrentHealth)
				{
					AppliedSingleHealthCurrentDelta = !HealthBindings.bHasSeparateMaxHealthBinding && HealthBindings.CurrentHealthBinding.bIsGameplayAttribute
						? CurrentAdjustmentDelta
						: 0.0f;
					ProjectEnemyLevelComponentPrivate::TryNudgeStatisticsHealthCurrent(Owner, HealthTargets.TargetCurrentHealth);
					bAppliedHealth = true;
				}
			}
		}
	}

	const TArray<FName> PreferredOffensiveChannels = ProjectEnemyLevelComponentPrivate::GetPreferredOffensiveChannels(Owner);
	for (TPair<FName, FGameplayScalingChannelState>& ChannelPair : ScalingChannels)
	{
		const FName LogicalAttributeName = ChannelPair.Key;
		FGameplayScalingChannelState& ChannelState = ChannelPair.Value;
		if (!ChannelState.bCaptured)
		{
			continue;
		}

		ProjectEnemyLevelComponentPrivate::FResolvedAttributeBinding Binding;
		if (!ProjectEnemyLevelComponentPrivate::ResolveAttributeBinding(AbilitySystemComponent, LogicalAttributeName, Binding))
		{
			if (!WarnedMissingChannels.Contains(LogicalAttributeName))
			{
				WarnedMissingChannels.Add(LogicalAttributeName);
				UE_LOG(
					LogProjectEnemyLevelComponent,
					Warning,
					TEXT("Enemy level scaling could not resolve %s on %s after baseline capture."),
					*LogicalAttributeName.ToString(),
					*GetNameSafe(Owner));
			}
			continue;
		}

		const float TargetValue = FProjectEnemyScalingMath::ScaleBaselineValue(
			ChannelState.BaselineValue,
			AssignedLevel,
			ProjectEnemyLevelComponentPrivate::GetPerLevelMultiplierForChannel(LogicalAttributeName, Settings));
		if (!ProjectEnemyLevelComponentPrivate::SetAbsoluteCurrentValue(AbilitySystemComponent, Owner, Binding, TargetValue))
		{
			continue;
		}

		float ObservedValue = TargetValue;
		ProjectEnemyLevelComponentPrivate::TryReadEffectiveCombatChannelValue(
			Owner,
			AbilitySystemComponent,
			LogicalAttributeName,
			ObservedValue);
		ChannelState.LastAppliedValue = ObservedValue;
		UE_LOG(
			LogProjectEnemyLevelComponent,
			VeryVerbose,
			TEXT("Enemy level scaling %s [%s] level=%d baseline=%.2f target=%.2f observed=%.2f"),
			*GetNameSafe(Owner),
			*LogicalAttributeName.ToString(),
			AssignedLevel,
			ChannelState.BaselineValue,
			TargetValue,
			ObservedValue);
		if (ProjectEnemyLevelComponentPrivate::IsDefenseChannel(LogicalAttributeName))
		{
			bAppliedDefense = true;
		}

		if (PreferredOffensiveChannels.Contains(LogicalAttributeName))
		{
			bAppliedPreferredOffense = true;
		}
	}

	TArray<FString> FailureReasons;
	if (!bAppliedHealth)
	{
		FailureReasons.Add(TEXT("health"));
	}

	if (!bAppliedPreferredOffense)
	{
		FailureReasons.Add(TEXT("damage"));
	}

	if (!bAppliedDefense)
	{
		FailureReasons.Add(TEXT("defense"));
	}

	if (FailureReasons.Num() > 0)
	{
		OutFailureReason = FString::Printf(
			TEXT("Enemy level scaling could not apply required channels (%s) for %s."),
			*FString::Join(FailureReasons, TEXT(", ")),
			*GetNameSafe(Owner));
		return false;
	}

	bGameplayScalingApplied = true;
	GameplayScaledLevel = AssignedLevel;
	return true;
}

void UProjectEnemyLevelComponent::ResetGameplayScalingState()
{
	ScalingChannels.Reset();
	WarnedMissingChannels.Reset();
	BaselineHealthMax = 0.0f;
	BaselineHealthRatio = 1.0f;
	AppliedSingleHealthCurrentDelta = 0.0f;
	bScalingBaselineCaptured = false;
	bHasHealthScalingBaseline = false;
	bGameplayScalingApplied = false;
	GameplayScaledLevel = INDEX_NONE;
}

USceneComponent* UProjectEnemyLevelComponent::EnsurePreferredTargetPoint(
	const UProjectEnemyLevelSettings& Settings,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutFailureReason = TEXT("Owning actor is null.");
		return nullptr;
	}

	USceneComponent* TargetPointSceneComponent = Cast<USceneComponent>(
		ProjectEnemyLevelComponentPrivate::FindComponentByClassHint<UActorComponent>(Owner, { TEXT("ATSTargetPointComponent") }));

	if (!TargetPointSceneComponent)
	{
		UClass* TargetPointComponentClass = ProjectEnemyLevelComponentPrivate::ResolveTargetPointComponentClass();
		if (!TargetPointComponentClass)
		{
			OutFailureReason = TEXT("Could not load /Script/AscentTargetingSystem.ATSTargetPointComponent.");
			return nullptr;
		}

		UActorComponent* NewComponent = NewObject<UActorComponent>(Owner, TargetPointComponentClass, TEXT("ProjectATSTargetPointComponent"));
		if (!NewComponent)
		{
			OutFailureReason = TEXT("Could not instantiate ATSTargetPointComponent.");
			return nullptr;
		}

		Owner->AddInstanceComponent(NewComponent);
		NewComponent->OnComponentCreated();
		NewComponent->RegisterComponent();
		TargetPointSceneComponent = Cast<USceneComponent>(NewComponent);
	}

	if (!TargetPointSceneComponent)
	{
		OutFailureReason = TEXT("The ATSTargetPointComponent is not a scene component.");
		return nullptr;
	}

	USceneComponent* AttachParent = ProjectEnemyLevelComponentPrivate::ResolveAttachParent(Owner);
	if (!AttachParent)
	{
		OutFailureReason = TEXT("Could not resolve an attach parent for the target point.");
		return nullptr;
	}

	const FName PreferredSocketName = ProjectEnemyLevelComponentPrivate::ResolveBestSocketName(AttachParent, Settings);
	TargetPointSceneComponent->AttachToComponent(
		AttachParent,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		PreferredSocketName);

	if (!PreferredSocketName.IsNone())
	{
		TargetPointSceneComponent->SetRelativeLocation(FVector::ZeroVector);
	}
	else
	{
		FVector Origin = FVector::ZeroVector;
		FVector BoxExtent = FVector(0.0f, 0.0f, 50.0f);
		Owner->GetActorBounds(true, Origin, BoxExtent);
		TargetPointSceneComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BoxExtent.Z * Settings.FallbackTargetHeightRatio));
	}

	PreferredTargetPointComponent = TargetPointSceneComponent;
	return PreferredTargetPointComponent.Get();
}

bool UProjectEnemyLevelComponent::HasAssignedLevel() const
{
	return bLevelAssigned;
}

bool UProjectEnemyLevelComponent::ValidateDirectorLevelState(
	const int32 ExpectedLogicalLevel,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (ExpectedLogicalLevel <= 0)
	{
		OutFailureReason = TEXT("Expected Director logical level must be positive.");
		return false;
	}

	if (!bLevelAssigned || AssignedLevel != ExpectedLogicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director logical level mismatch: expected %d, assigned %d."),
			ExpectedLogicalLevel,
			AssignedLevel);
		return false;
	}

	if (WorldTier != ExpectedLogicalLevel
		|| MinRolledLevel != ExpectedLogicalLevel
		|| MaxRolledLevel != ExpectedLogicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director level metadata is not exact: tier=%d range=[%d,%d], expected %d."),
			WorldTier,
			MinRolledLevel,
			MaxRolledLevel,
			ExpectedLogicalLevel);
		return false;
	}

	const int32 ExpectedPhysicalLevel = ResolvePhysicalAscentLevel(ExpectedLogicalLevel);
	if (!bAscentSyncEvaluated
		|| !bAscentSyncSatisfied
		|| LastPhysicalAscentLevel != ExpectedPhysicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director physical level is not ready: expected %d, recorded %d, evaluated=%s, satisfied=%s."),
			ExpectedPhysicalLevel,
			LastPhysicalAscentLevel,
			bAscentSyncEvaluated ? TEXT("true") : TEXT("false"),
			bAscentSyncSatisfied ? TEXT("true") : TEXT("false"));
		return false;
	}

	const float ExpectedNormalizedLevel = FMath::Clamp(
		(static_cast<float>(ExpectedPhysicalLevel) - 1.0f) / 99.0f,
		0.0f,
		1.0f);
	if (!FMath::IsFinite(NormalizedLevel)
		|| !FMath::IsNearlyEqual(NormalizedLevel, ExpectedNormalizedLevel, KINDA_SMALL_NUMBER))
	{
		OutFailureReason = FString::Printf(
			TEXT("Director normalized level mismatch: expected %.6f, assigned %.6f."),
			ExpectedNormalizedLevel,
			NormalizedLevel);
		return false;
	}

	if (!HasGameplayScalingAppliedForLevel(ExpectedLogicalLevel))
	{
		OutFailureReason = FString::Printf(
			TEXT("Gameplay scaling is not applied for logical level %d (scaled level %d)."),
			ExpectedLogicalLevel,
			GameplayScaledLevel);
		return false;
	}

	if (!IsValid(PreferredTargetPointComponent.Get())
		|| !PreferredTargetPointComponent->IsRegistered())
	{
		OutFailureReason = TEXT("The preferred target point is missing or unregistered.");
		return false;
	}

	return true;
}

int32 UProjectEnemyLevelComponent::GetAssignedLevel() const
{
	return AssignedLevel;
}

int32 UProjectEnemyLevelComponent::GetPhysicalAscentLevel() const
{
	return bLevelAssigned ? ResolvePhysicalAscentLevel(AssignedLevel) : 0;
}

int32 UProjectEnemyLevelComponent::GetWorldTier() const
{
	return WorldTier;
}

int32 UProjectEnemyLevelComponent::GetMinRolledLevel() const
{
	return MinRolledLevel;
}

int32 UProjectEnemyLevelComponent::GetMaxRolledLevel() const
{
	return MaxRolledLevel;
}

float UProjectEnemyLevelComponent::GetNormalizedLevel() const
{
	return NormalizedLevel;
}

USceneComponent* UProjectEnemyLevelComponent::GetPreferredTargetPointComponent() const
{
	return PreferredTargetPointComponent.Get();
}

bool UProjectEnemyLevelComponent::HasGameplayScalingBaseline() const
{
	return bScalingBaselineCaptured;
}

bool UProjectEnemyLevelComponent::HasGameplayScalingAppliedForLevel(
	const int32 ExpectedLogicalLevel) const
{
	return ExpectedLogicalLevel > 0
		&& bScalingBaselineCaptured
		&& bGameplayScalingApplied
		&& GameplayScaledLevel == ExpectedLogicalLevel;
}

bool UProjectEnemyLevelComponent::TryGetDisplayHealthSnapshot(
	float& OutCurrentHealth,
	float& OutDisplayMaxHealth,
	float& OutDisplayRatio) const
{
	OutCurrentHealth = -1.0f;
	OutDisplayMaxHealth = -1.0f;
	OutDisplayRatio = 0.0f;

	if (!bLevelAssigned)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	ProjectEnemyLevelComponentPrivate::FTargetHealthSnapshot HealthSnapshot;
	const bool bHasHealthData = ProjectEnemyLevelComponentPrivate::TryReadHealthFromStatisticsComponent(const_cast<AActor*>(Owner), HealthSnapshot)
		|| ([](AActor* MutableOwner, ProjectEnemyLevelComponentPrivate::FTargetHealthSnapshot& MutableSnapshot)
		{
			UAbilitySystemComponent* AbilitySystemComponent = ProjectEnemyLevelComponentPrivate::ResolveAbilitySystemComponent(MutableOwner);
			if (!AbilitySystemComponent)
			{
				return false;
			}

			ProjectEnemyLevelComponentPrivate::FResolvedHealthBindings HealthBindings;
			if (!ProjectEnemyLevelComponentPrivate::ResolveHealthBindings(AbilitySystemComponent, HealthBindings))
			{
				return false;
			}

			MutableSnapshot.bHasCurrentHealth = true;
			MutableSnapshot.CurrentHealth = ProjectEnemyLevelComponentPrivate::ReadCurrentValue(AbilitySystemComponent, HealthBindings.CurrentHealthBinding);

			const ProjectEnemyLevelComponentPrivate::FResolvedAttributeBinding& MaxBinding = HealthBindings.bHasSeparateMaxHealthBinding
				? HealthBindings.MaxHealthBinding
				: HealthBindings.CurrentHealthBinding;
			MutableSnapshot.bHasMaxHealth = true;
			MutableSnapshot.MaxHealth = ProjectEnemyLevelComponentPrivate::ReadBaseValue(AbilitySystemComponent, MaxBinding);

			if (MutableSnapshot.MaxHealth > KINDA_SMALL_NUMBER)
			{
				MutableSnapshot.bHasHealthRatio = true;
				MutableSnapshot.HealthRatio = FMath::Clamp(MutableSnapshot.CurrentHealth / MutableSnapshot.MaxHealth, 0.0f, 1.0f);
			}

			return true;
		})(const_cast<AActor*>(Owner), HealthSnapshot);

	if (!bHasHealthData)
	{
		return false;
	}

	if (!HealthSnapshot.bHasCurrentHealth)
	{
		return false;
	}

	OutCurrentHealth = HealthSnapshot.CurrentHealth;

	if (bHasHealthScalingBaseline && BaselineHealthMax > KINDA_SMALL_NUMBER)
	{
		const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
		const float HealthPerLevel = Settings ? Settings->HealthPerLevel : 0.12f;
		OutDisplayMaxHealth = FProjectEnemyScalingMath::ScaleBaselineValue(BaselineHealthMax, AssignedLevel, HealthPerLevel);
	}
	else if (HealthSnapshot.bHasMaxHealth)
	{
		OutDisplayMaxHealth = HealthSnapshot.MaxHealth;
	}
	else
	{
		OutDisplayMaxHealth = OutCurrentHealth;
	}

	if (OutDisplayMaxHealth <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutDisplayRatio = FMath::Clamp(OutCurrentHealth / OutDisplayMaxHealth, 0.0f, 1.0f);
	if (OutDisplayRatio >= 0.995f)
	{
		OutDisplayRatio = 1.0f;
	}

	return true;
}

bool UProjectEnemyLevelComponent::TryGetCombatStatSnapshot(FProjectEnemyCombatStatSnapshot& OutSnapshot) const
{
	OutSnapshot.Rows.Reset();
	OutSnapshot.Rows.Reserve(4);

	if (!bLevelAssigned)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	auto AddUnavailableRow = [&OutSnapshot](const TCHAR* LabelText)
	{
		FProjectEnemyCombatStatRow Row;
		Row.Label = FText::FromString(LabelText);
		Row.bIsAvailable = false;
		OutSnapshot.Rows.Add(MoveTemp(Row));
	};

	auto AddAvailableRow = [&OutSnapshot](const TCHAR* LabelText, const float BaseValue, const float FinalValue)
	{
		FProjectEnemyCombatStatRow Row;
		Row.Label = FText::FromString(LabelText);
		Row.BaseValue = BaseValue;
		Row.FinalValue = FinalValue;
		Row.bIsAvailable = true;
		OutSnapshot.Rows.Add(MoveTemp(Row));
	};

	if (bHasHealthScalingBaseline && BaselineHealthMax > KINDA_SMALL_NUMBER)
	{
		float CurrentHealth = 0.0f;
		float DisplayMaxHealth = 0.0f;
		float DisplayRatio = 0.0f;
		if (!TryGetDisplayHealthSnapshot(CurrentHealth, DisplayMaxHealth, DisplayRatio))
		{
			const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
			const float HealthPerLevel = Settings ? Settings->HealthPerLevel : 0.12f;
			DisplayMaxHealth = FProjectEnemyScalingMath::ScaleBaselineValue(BaselineHealthMax, AssignedLevel, HealthPerLevel);
		}

		AddAvailableRow(TEXT("Max HP"), BaselineHealthMax, DisplayMaxHealth);
	}
	else
	{
		AddUnavailableRow(TEXT("Max HP"));
	}

	AActor* MutableOwner = const_cast<AActor*>(Owner);
	UAbilitySystemComponent* AbilitySystemComponent = ProjectEnemyLevelComponentPrivate::ResolveAbilitySystemComponent(MutableOwner);

	auto TryBuildChannelRow = [this, AbilitySystemComponent](const FName LogicalAttributeName, const TCHAR* LabelText, FProjectEnemyCombatStatRow& OutRow) -> bool
	{
		OutRow = FProjectEnemyCombatStatRow();
		OutRow.Label = FText::FromString(LabelText);

		const FGameplayScalingChannelState* ChannelState = ScalingChannels.Find(LogicalAttributeName);
		if (!ChannelState || !ChannelState->bCaptured)
		{
			return false;
		}

		float FinalValue = 0.0f;
		if (!ProjectEnemyLevelComponentPrivate::TryReadEffectiveCombatChannelValue(
			GetOwner(),
			AbilitySystemComponent,
			LogicalAttributeName,
			FinalValue))
		{
			return false;
		}

		OutRow.BaseValue = ChannelState->BaselineValue;
		OutRow.FinalValue = FinalValue;
		OutRow.bIsAvailable = true;
		return true;
	};

	FProjectEnemyCombatStatRow DamageRow;
	bool bHasDamageRow = false;
	for (const FName LogicalAttributeName : ProjectEnemyLevelComponentPrivate::GetPreferredOffensiveChannels(MutableOwner))
	{
		if (TryBuildChannelRow(LogicalAttributeName, TEXT("Damage"), DamageRow))
		{
			bHasDamageRow = true;
			break;
		}
	}

	if (bHasDamageRow)
	{
		OutSnapshot.Rows.Add(MoveTemp(DamageRow));
	}
	else
	{
		AddUnavailableRow(TEXT("Damage"));
	}

	FProjectEnemyCombatStatRow PhysicalDefenseRow;
	if (TryBuildChannelRow(TEXT("PhysicalDefense"), TEXT("Phys Def"), PhysicalDefenseRow))
	{
		OutSnapshot.Rows.Add(MoveTemp(PhysicalDefenseRow));
	}
	else
	{
		AddUnavailableRow(TEXT("Phys Def"));
	}

	FProjectEnemyCombatStatRow SpellDefenseRow;
	if (TryBuildChannelRow(TEXT("SpellDefense"), TEXT("Spell Def"), SpellDefenseRow))
	{
		OutSnapshot.Rows.Add(MoveTemp(SpellDefenseRow));
	}
	else
	{
		AddUnavailableRow(TEXT("Spell Def"));
	}

	return OutSnapshot.Rows.Num() > 0;
}

void UProjectEnemyLevelComponent::OnRep_LevelData()
{
}
