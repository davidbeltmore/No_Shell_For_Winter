#include "Survival/ProjectRealtimeSnapshotComponent.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectRealtimeSnapshotSettings.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr float HealthChangeTolerance = 0.1f;
	const FName HealthResourceName(TEXT("Health"));
	const FName StaminaResourceName(TEXT("Stamina"));
	const FName ManaResourceName(TEXT("Mana"));

	struct FProjectRealtimeResolvedFloatAttributeBinding
	{
		TWeakObjectPtr<UAttributeSet> AttributeSet;
		FProperty* Property = nullptr;
		FGameplayAttribute GameplayAttribute;
		bool bGameplayAttributeData = false;
	};

	struct FProjectRealtimeResourceQuery
	{
		FName ResourceName = NAME_None;
		FGameplayTag StatisticTag;
		FString StatisticTagString;
		TArray<FString> CurrentCandidateNames;
		TArray<FString> MaxCandidateNames;
	};

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
				if (!Hint.IsEmpty() && ComponentClassName.Contains(Hint, ESearchCase::IgnoreCase))
				{
					return Cast<ComponentType>(Component);
				}
			}
		}

		return nullptr;
	}

	FString NormalizeName(const FString& Value)
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

	bool PropertyNameMatches(const FName PropertyName, const TArray<FString>& CandidateNames)
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

	bool BuildResourceQuery(const FName ResourceName, FProjectRealtimeResourceQuery& OutQuery)
	{
		const FString NormalizedResourceName = NormalizeName(ResourceName.ToString());
		FString CanonicalName;
		if (NormalizedResourceName == TEXT("health") || NormalizedResourceName == TEXT("hp") || NormalizedResourceName == TEXT("currenthealth"))
		{
			OutQuery.ResourceName = HealthResourceName;
			CanonicalName = TEXT("Health");
			OutQuery.CurrentCandidateNames = { TEXT("Health"), TEXT("CurrentHealth") };
			OutQuery.MaxCandidateNames = { TEXT("MaxHealth"), TEXT("MaximumHealth"), TEXT("HealthMax"), TEXT("HealthMaximum") };
		}
		else if (NormalizedResourceName == TEXT("stamina") || NormalizedResourceName == TEXT("currentstamina"))
		{
			OutQuery.ResourceName = StaminaResourceName;
			CanonicalName = TEXT("Stamina");
			OutQuery.CurrentCandidateNames = { TEXT("Stamina"), TEXT("CurrentStamina") };
			OutQuery.MaxCandidateNames = { TEXT("MaxStamina"), TEXT("MaximumStamina"), TEXT("StaminaMax"), TEXT("StaminaMaximum") };
		}
		else if (NormalizedResourceName == TEXT("mana") || NormalizedResourceName == TEXT("currentmana"))
		{
			OutQuery.ResourceName = ManaResourceName;
			CanonicalName = TEXT("Mana");
			OutQuery.CurrentCandidateNames = { TEXT("Mana"), TEXT("CurrentMana") };
			OutQuery.MaxCandidateNames = { TEXT("MaxMana"), TEXT("MaximumMana"), TEXT("ManaMax"), TEXT("ManaMaximum") };
		}
		else
		{
			return false;
		}

		OutQuery.StatisticTagString = FString::Printf(TEXT("RPG.Statistics.%s"), *CanonicalName);
		OutQuery.StatisticTag = FGameplayTag::RequestGameplayTag(FName(*OutQuery.StatisticTagString), false);
		return true;
	}

	bool ResolveFloatAttributeBinding(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& CandidateNames,
		FProjectRealtimeResolvedFloatAttributeBinding& OutBinding)
	{
		if (!AbilitySystemComponent)
		{
			return false;
		}

		for (UAttributeSet* AttributeSet : AbilitySystemComponent->GetSpawnedAttributes())
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

				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				const bool bGameplayAttributeData = StructProperty && StructProperty->Struct == FGameplayAttributeData::StaticStruct();
				if (!bGameplayAttributeData && !CastField<FFloatProperty>(Property))
				{
					continue;
				}

				OutBinding.AttributeSet = AttributeSet;
				OutBinding.Property = Property;
				OutBinding.bGameplayAttributeData = bGameplayAttributeData;
				if (bGameplayAttributeData)
				{
					OutBinding.GameplayAttribute = FGameplayAttribute(Property);
				}

				return true;
			}
		}

		return false;
	}

	UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* TargetActor)
	{
		if (!TargetActor)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* AbilitySystemComponent = TargetActor->FindComponentByClass<UAbilitySystemComponent>())
		{
			return AbilitySystemComponent;
		}

		return FindComponentByClassHint<UAbilitySystemComponent>(
			TargetActor,
			{ TEXT("AbilitySystemComponent"), TEXT("ACFAbilitySystemComponent") });
	}

	float ReadFloatAttributeValue(UAbilitySystemComponent* AbilitySystemComponent, const FProjectRealtimeResolvedFloatAttributeBinding& Binding)
	{
		if (Binding.bGameplayAttributeData && AbilitySystemComponent)
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

		return 0.f;
	}

	float ReadFloatAttributeBaseValue(UAbilitySystemComponent* AbilitySystemComponent, const FProjectRealtimeResolvedFloatAttributeBinding& Binding)
	{
		if (Binding.bGameplayAttributeData && AbilitySystemComponent)
		{
			return AbilitySystemComponent->GetNumericAttributeBase(Binding.GameplayAttribute);
		}

		return ReadFloatAttributeValue(AbilitySystemComponent, Binding);
	}

	bool ApplyFloatAttributeDelta(
		UAbilitySystemComponent* AbilitySystemComponent,
		const FProjectRealtimeResolvedFloatAttributeBinding& Binding,
		const float DeltaAmount)
	{
		if (FMath::IsNearlyZero(DeltaAmount))
		{
			return false;
		}

		if (Binding.bGameplayAttributeData && AbilitySystemComponent)
		{
			if (AActor* Owner = AbilitySystemComponent->GetOwner(); Owner && Owner->HasAuthority())
			{
				AbilitySystemComponent->ApplyModToAttribute(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaAmount);
			}
			else
			{
				AbilitySystemComponent->ApplyModToAttributeUnsafe(Binding.GameplayAttribute, EGameplayModOp::Additive, DeltaAmount);
			}

			return true;
		}

		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Binding.Property))
		{
			if (Binding.AttributeSet.IsValid())
			{
				const float CurrentValue = FloatProperty->GetPropertyValue_InContainer(Binding.AttributeSet.Get());
				FloatProperty->SetPropertyValue_InContainer(Binding.AttributeSet.Get(), CurrentValue + DeltaAmount);
				return true;
			}
		}

		return false;
	}

	FProperty* FindSingleInputProperty(UFunction* Function)
	{
		FProperty* InputProperty = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			if (InputProperty)
			{
				return nullptr;
			}

			InputProperty = Property;
		}

		return InputProperty;
	}

	FProperty* FindReturnProperty(UFunction* Function)
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

	bool TryInvokeStatisticFloatFunction(
		UObject* Target,
		const FName FunctionName,
		const FGameplayTag ResourceTag,
		const FName ResourceName,
		const FString& ResourceString,
		float& OutReturnValue)
	{
		OutReturnValue = 0.f;
		if (!Target || FunctionName.IsNone())
		{
			return false;
		}

		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function || Function->ParmsSize <= 0)
		{
			return false;
		}

		FProperty* InputProperty = FindSingleInputProperty(Function);
		FProperty* ReturnProperty = FindReturnProperty(Function);
		if (!InputProperty || !ReturnProperty)
		{
			return false;
		}

		void* Parms = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parms, Function->ParmsSize);

		if (FStructProperty* StructProperty = CastField<FStructProperty>(InputProperty))
		{
			if (StructProperty->Struct != FGameplayTag::StaticStruct())
			{
				return false;
			}

			*StructProperty->ContainerPtrToValuePtr<FGameplayTag>(Parms) = ResourceTag;
		}
		else if (FNameProperty* NameProperty = CastField<FNameProperty>(InputProperty))
		{
			NameProperty->SetPropertyValue_InContainer(Parms, ResourceName);
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(InputProperty))
		{
			StringProperty->SetPropertyValue_InContainer(Parms, ResourceString);
		}
		else
		{
			return false;
		}

		Target->ProcessEvent(Function, Parms);

		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(ReturnProperty))
		{
			OutReturnValue = FloatProperty->GetPropertyValue_InContainer(Parms);
			return true;
		}

		if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(ReturnProperty))
		{
			OutReturnValue = static_cast<float>(DoubleProperty->GetPropertyValue_InContainer(Parms));
			return true;
		}

		if (const FIntProperty* IntProperty = CastField<FIntProperty>(ReturnProperty))
		{
			OutReturnValue = static_cast<float>(IntProperty->GetPropertyValue_InContainer(Parms));
			return true;
		}

		return false;
	}

	bool TryInvokeStatisticMutationFunction(
		UObject* Target,
		const FName FunctionName,
		const FGameplayTag ResourceTag,
		const FName ResourceName,
		const FString& ResourceString,
		const float DeltaAmount)
	{
		if (!Target || FunctionName.IsNone() || FMath::IsNearlyZero(DeltaAmount))
		{
			return false;
		}

		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function || Function->ParmsSize <= 0)
		{
			return false;
		}

		void* Parms = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parms, Function->ParmsSize);

		bool bSetResource = false;
		bool bSetDelta = false;
		FProperty* ReturnProperty = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnProperty = Property;
				continue;
			}

			if (!bSetResource)
			{
				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct == FGameplayTag::StaticStruct())
					{
						*StructProperty->ContainerPtrToValuePtr<FGameplayTag>(Parms) = ResourceTag;
						bSetResource = true;
						continue;
					}
				}
				else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
				{
					NameProperty->SetPropertyValue_InContainer(Parms, ResourceName);
					bSetResource = true;
					continue;
				}
				else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
				{
					StringProperty->SetPropertyValue_InContainer(Parms, ResourceString);
					bSetResource = true;
					continue;
				}
			}

			if (!bSetDelta)
			{
				if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
				{
					FloatProperty->SetPropertyValue_InContainer(Parms, DeltaAmount);
					bSetDelta = true;
					continue;
				}
				if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
				{
					DoubleProperty->SetPropertyValue_InContainer(Parms, static_cast<double>(DeltaAmount));
					bSetDelta = true;
					continue;
				}
				if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
				{
					IntProperty->SetPropertyValue_InContainer(Parms, FMath::RoundToInt(DeltaAmount));
					bSetDelta = true;
					continue;
				}
			}
		}

		if (!bSetResource || !bSetDelta)
		{
			return false;
		}

		Target->ProcessEvent(Function, Parms);

		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(ReturnProperty))
		{
			return BoolProperty->GetPropertyValue_InContainer(Parms);
		}

		return true;
	}

	bool TryReadResourceFromStatisticsComponent(
		AActor* TargetActor,
		const FProjectRealtimeResourceQuery& ResourceQuery,
		float& OutCurrentValue,
		float& OutMaxValue,
		bool& bOutHasMaxValue)
	{
		OutCurrentValue = 0.f;
		OutMaxValue = 0.f;
		bOutHasMaxValue = false;

		UObject* StatisticsComponent = FindComponentByClassHint<UObject>(
			TargetActor,
			{ TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent"), TEXT("StatisticsComponent") });
		if (!StatisticsComponent)
		{
			return false;
		}

		const bool bReadCurrent =
			TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetCurrentValueForStatitstic"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, OutCurrentValue)
			|| TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetCurrentValueForStatistic"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, OutCurrentValue);

		bOutHasMaxValue =
			TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetMaxValueForStatitstic"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, OutMaxValue)
			|| TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetMaxValueForStatistic"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, OutMaxValue);

		return bReadCurrent;
	}

	bool TryReadResourceFromAbilitySystem(
		AActor* TargetActor,
		const FProjectRealtimeResourceQuery& ResourceQuery,
		float& OutCurrentValue,
		float& OutMaxValue,
		bool& bOutHasMaxValue)
	{
		OutCurrentValue = 0.f;
		OutMaxValue = 0.f;
		bOutHasMaxValue = false;

		UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent(TargetActor);
		if (!AbilitySystemComponent)
		{
			return false;
		}

		FProjectRealtimeResolvedFloatAttributeBinding CurrentBinding;
		if (!ResolveFloatAttributeBinding(AbilitySystemComponent, ResourceQuery.CurrentCandidateNames, CurrentBinding))
		{
			return false;
		}

		OutCurrentValue = ReadFloatAttributeValue(AbilitySystemComponent, CurrentBinding);

		FProjectRealtimeResolvedFloatAttributeBinding MaxBinding;
		if (ResolveFloatAttributeBinding(AbilitySystemComponent, ResourceQuery.MaxCandidateNames, MaxBinding))
		{
			OutMaxValue = ReadFloatAttributeValue(AbilitySystemComponent, MaxBinding);
			bOutHasMaxValue = OutMaxValue > KINDA_SMALL_NUMBER;
		}
		else
		{
			OutMaxValue = ReadFloatAttributeBaseValue(AbilitySystemComponent, CurrentBinding);
			bOutHasMaxValue = OutMaxValue > KINDA_SMALL_NUMBER;
		}

		return true;
	}

	bool TryApplyResourceDeltaToStatisticsComponent(
		AActor* TargetActor,
		const FProjectRealtimeResourceQuery& ResourceQuery,
		const float DeltaAmount)
	{
		UObject* StatisticsComponent = FindComponentByClassHint<UObject>(
			TargetActor,
			{ TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent"), TEXT("StatisticsComponent") });
		if (!StatisticsComponent)
		{
			return false;
		}

		return TryInvokeStatisticMutationFunction(StatisticsComponent, TEXT("ModifyStat"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, DeltaAmount)
			|| TryInvokeStatisticMutationFunction(StatisticsComponent, TEXT("ModifyStatistic"), ResourceQuery.StatisticTag, ResourceQuery.ResourceName, ResourceQuery.StatisticTagString, DeltaAmount);
	}

	bool TryApplyResourceDeltaToAbilitySystem(
		AActor* TargetActor,
		const FProjectRealtimeResourceQuery& ResourceQuery,
		const float DeltaAmount)
	{
		UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent(TargetActor);
		if (!AbilitySystemComponent)
		{
			return false;
		}

		FProjectRealtimeResolvedFloatAttributeBinding CurrentBinding;
		if (!ResolveFloatAttributeBinding(AbilitySystemComponent, ResourceQuery.CurrentCandidateNames, CurrentBinding))
		{
			return false;
		}

		return ApplyFloatAttributeDelta(AbilitySystemComponent, CurrentBinding, DeltaAmount);
	}

	bool ClassNameMatchesEnemyHints(const FString& ClassName, const TArray<FString>& Hints)
	{
		if (ClassName.EndsWith(TEXT("Male"), ESearchCase::IgnoreCase)
			|| ClassName.EndsWith(TEXT("Male_C"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && ClassName.Contains(Hint, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	bool IsPlayerControlledActor(const AActor* Actor)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			return Pawn->IsPlayerControlled();
		}

		if (const AController* Controller = Cast<AController>(Actor))
		{
			const APawn* Pawn = Controller->GetPawn();
			return Pawn && Pawn->IsPlayerControlled();
		}

		return false;
	}
}

UProjectRealtimeSnapshotComponent::UProjectRealtimeSnapshotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.10f;
}

void UProjectRealtimeSnapshotComponent::BeginPlay()
{
	Super::BeginPlay();

	const UProjectRealtimeSnapshotSettings* Settings = UProjectRealtimeSnapshotSettings::Get();
	PrimaryComponentTick.TickInterval = FMath::Max(Settings ? Settings->HealthScanIntervalSeconds : 0.10f, 0.01f);

	RefreshCachedComponents();
	RefreshObservedEnemies();

	float CurrentOwnerHealth = 0.f;
	if (TryReadActorHealth(GetOwner(), CurrentOwnerHealth))
	{
		OwnerLastHealth = CurrentOwnerHealth;
		bHasOwnerLastHealth = true;
	}

	if (UWorld* World = GetWorld(); World && !ActorSpawnedHandle.IsValid())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	}

	BroadcastSnapshotChanged();
}

void UProjectRealtimeSnapshotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		ActorSpawnedHandle.Reset();
	}

	ObservedEnemies.Reset();
	Super::EndPlay(EndPlayReason);
}

void UProjectRealtimeSnapshotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.f)
	{
		return;
	}

	const UProjectRealtimeSnapshotSettings* Settings = UProjectRealtimeSnapshotSettings::Get();
	const float HealthScanInterval = FMath::Max(Settings ? Settings->HealthScanIntervalSeconds : 0.10f, 0.01f);
	const float EnemyRefreshInterval = FMath::Max(Settings ? Settings->EnemyRefreshIntervalSeconds : 1.f, 0.1f);

	HealthScanAccumulatorSeconds += DeltaTime;
	EnemyRefreshAccumulatorSeconds += DeltaTime;

	if (ObservedEnemies.IsEmpty() || EnemyRefreshAccumulatorSeconds >= EnemyRefreshInterval)
	{
		EnemyRefreshAccumulatorSeconds = 0.f;
		RefreshObservedEnemies();
	}

	if (HealthScanAccumulatorSeconds < HealthScanInterval)
	{
		return;
	}

	HealthScanAccumulatorSeconds = 0.f;
	RefreshCachedComponents();
	UpdateHealthSnapshots();
}

void UProjectRealtimeSnapshotComponent::ForceRefreshSnapshot()
{
	RefreshCachedComponents();
	RefreshObservedEnemies();
	BroadcastSnapshotChanged();
}

FProjectUnifiedRuntimeSnapshot UProjectRealtimeSnapshotComponent::BuildUnifiedRuntimeSnapshot() const
{
	FProjectUnifiedRuntimeSnapshot Snapshot;
	Snapshot.OwnerHealth = BuildActorHealthSnapshot(GetOwner());

	Snapshot.ObservedEnemies.Reserve(ObservedEnemies.Num());
	for (const FProjectObservedActorHealthRuntime& ObservedEnemy : ObservedEnemies)
	{
		if (AActor* EnemyActor = ObservedEnemy.Actor.Get())
		{
			Snapshot.ObservedEnemies.Add(BuildActorHealthSnapshot(EnemyActor));
		}
	}

	if (NeedsComponent)
	{
		Snapshot.bHasNeedsComponent = true;
		Snapshot.Needs = NeedsComponent->BuildNeedSnapshots();
		Snapshot.Sensations = NeedsComponent->BuildSensationSnapshots();
	}

	if (StatusComponent)
	{
		Snapshot.bHasStatusComponent = true;
		Snapshot.ActiveStatuses = StatusComponent->BuildActiveStatusSnapshots();
	}

	if (InnerDoctrineComponent)
	{
		Snapshot.bHasInnerDoctrineComponent = true;
		Snapshot.InnerDoctrine = InnerDoctrineComponent->BuildSnapshot();
	}

	if (AttributeBridgeComponent)
	{
		Snapshot.bAttributeBridgeReady = AttributeBridgeComponent->IsBridgeReady();
		Snapshot.AttributeBridgeSnapshots = AttributeBridgeComponent->BuildAttributeBindingSnapshots();
	}

	return Snapshot;
}

bool UProjectRealtimeSnapshotComponent::TryReadActorResource(AActor* Actor, const FName ResourceName, float& OutCurrentValue, float& OutMaxValue) const
{
	OutCurrentValue = 0.f;
	OutMaxValue = 0.f;
	if (!Actor)
	{
		return false;
	}

	FProjectRealtimeResourceQuery ResourceQuery;
	if (!BuildResourceQuery(ResourceName, ResourceQuery))
	{
		return false;
	}

	float CurrentValue = 0.f;
	float MaxValue = 0.f;
	bool bHasMaxValue = false;
	if (TryReadResourceFromStatisticsComponent(Actor, ResourceQuery, CurrentValue, MaxValue, bHasMaxValue))
	{
		OutCurrentValue = CurrentValue;
		if (bHasMaxValue)
		{
			OutMaxValue = MaxValue;
		}
		else
		{
			float AbilityCurrentValue = 0.f;
			float AbilityMaxValue = 0.f;
			bool bAbilityHasMaxValue = false;
			if (TryReadResourceFromAbilitySystem(Actor, ResourceQuery, AbilityCurrentValue, AbilityMaxValue, bAbilityHasMaxValue) && bAbilityHasMaxValue)
			{
				OutMaxValue = AbilityMaxValue;
			}
			else if (const UProjectCombatAttributeComponent* ProjectCombat = Actor->FindComponentByClass<UProjectCombatAttributeComponent>())
			{
				const FName CombatAttributeName = ResourceQuery.ResourceName == HealthResourceName ? ProjectCombat->HealthAttributeName : ResourceQuery.ResourceName;
				if (ProjectCombat->HasAttribute(CombatAttributeName))
				{
					OutMaxValue = ProjectCombat->GetAttributeMaxValue(CombatAttributeName);
				}
			}
		}

		if (OutMaxValue <= KINDA_SMALL_NUMBER)
		{
			OutMaxValue = FMath::Max(OutCurrentValue, 0.f);
		}

		return true;
	}

	if (TryReadResourceFromAbilitySystem(Actor, ResourceQuery, CurrentValue, MaxValue, bHasMaxValue))
	{
		OutCurrentValue = CurrentValue;
		OutMaxValue = bHasMaxValue ? MaxValue : FMath::Max(OutCurrentValue, 0.f);
		return true;
	}

	if (const UProjectCombatAttributeComponent* ProjectCombat = Actor->FindComponentByClass<UProjectCombatAttributeComponent>())
	{
		const FName CombatAttributeName = ResourceQuery.ResourceName == HealthResourceName ? ProjectCombat->HealthAttributeName : ResourceQuery.ResourceName;
		if (ProjectCombat->HasAttribute(CombatAttributeName))
		{
			OutCurrentValue = ProjectCombat->GetAttributeCurrentValue(CombatAttributeName);
			OutMaxValue = ProjectCombat->GetAttributeMaxValue(CombatAttributeName);
			return true;
		}
	}

	return false;
}

bool UProjectRealtimeSnapshotComponent::TryReadOwnerResource(const FName ResourceName, float& OutCurrentValue, float& OutMaxValue) const
{
	return TryReadActorResource(GetOwner(), ResourceName, OutCurrentValue, OutMaxValue);
}

float UProjectRealtimeSnapshotComponent::ApplyOwnerResourceDelta(const FName ResourceName, const float DeltaAmount, const bool bClampToMax)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || FMath::IsNearlyZero(DeltaAmount))
	{
		return 0.f;
	}

	FProjectRealtimeResourceQuery ResourceQuery;
	if (!BuildResourceQuery(ResourceName, ResourceQuery))
	{
		return 0.f;
	}

	float CurrentValue = 0.f;
	float MaxValue = 0.f;
	const bool bHadPreApplyValue = TryReadOwnerResource(ResourceQuery.ResourceName, CurrentValue, MaxValue);
	float EffectiveDelta = DeltaAmount;
	if (bClampToMax && DeltaAmount > 0.f && bHadPreApplyValue && MaxValue > KINDA_SMALL_NUMBER)
	{
		EffectiveDelta = FMath::Min(DeltaAmount, FMath::Max(0.f, MaxValue - CurrentValue));
	}

	if (FMath::IsNearlyZero(EffectiveDelta))
	{
		return 0.f;
	}

	bool bApplied = TryApplyResourceDeltaToStatisticsComponent(OwnerActor, ResourceQuery, EffectiveDelta)
		|| TryApplyResourceDeltaToAbilitySystem(OwnerActor, ResourceQuery, EffectiveDelta);

	if (!bApplied)
	{
		if (UProjectCombatAttributeComponent* ProjectCombat = OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>())
		{
			const FName CombatAttributeName = ResourceQuery.ResourceName == HealthResourceName ? ProjectCombat->HealthAttributeName : ResourceQuery.ResourceName;
			if (ProjectCombat->HasAttribute(CombatAttributeName))
			{
				EffectiveDelta = ProjectCombat->ModifyAttribute(CombatAttributeName, EffectiveDelta);
				bApplied = !FMath::IsNearlyZero(EffectiveDelta);
			}
		}
	}

	if (!bApplied)
	{
		return 0.f;
	}

	float NewCurrentValue = 0.f;
	float NewMaxValue = 0.f;
	if (bHadPreApplyValue && TryReadOwnerResource(ResourceQuery.ResourceName, NewCurrentValue, NewMaxValue))
	{
		BroadcastSnapshotChanged();
		return NewCurrentValue - CurrentValue;
	}

	BroadcastSnapshotChanged();
	return EffectiveDelta;
}

bool UProjectRealtimeSnapshotComponent::SetOwnerResourceFloor(const FName ResourceName, const float FloorValue)
{
	float CurrentValue = 0.f;
	float MaxValue = 0.f;
	if (!TryReadOwnerResource(ResourceName, CurrentValue, MaxValue))
	{
		return false;
	}

	const float ClampedFloor = MaxValue > KINDA_SMALL_NUMBER
		? FMath::Clamp(FloorValue, 0.f, MaxValue)
		: FMath::Max(FloorValue, 0.f);
	if (CurrentValue >= ClampedFloor - KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const bool bApplied = ApplyOwnerResourceDelta(ResourceName, ClampedFloor - CurrentValue, true) > KINDA_SMALL_NUMBER;
	if (bApplied && ResourceName == HealthResourceName)
	{
		float NewCurrentValue = 0.f;
		float NewMaxValue = 0.f;
		if (TryReadOwnerResource(ResourceName, NewCurrentValue, NewMaxValue))
		{
			OwnerLastHealth = NewCurrentValue;
			bHasOwnerLastHealth = true;
		}
	}

	return bApplied;
}

bool UProjectRealtimeSnapshotComponent::TryReadActorHealth(const AActor* Actor, float& OutCurrentHealth) const
{
	float MaxHealth = 0.f;
	return TryReadActorResource(const_cast<AActor*>(Actor), HealthResourceName, OutCurrentHealth, MaxHealth);
}

bool UProjectRealtimeSnapshotComponent::IsRelevantCombatEnemy(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	const UProjectRealtimeSnapshotSettings* Settings = UProjectRealtimeSnapshotSettings::Get();
	const TArray<FString>& Hints = Settings ? Settings->RelevantEnemyClassNameHints : TArray<FString>();

	TArray<const AActor*> PendingActors;
	TSet<const AActor*> VisitedActors;
	PendingActors.Add(Actor);

	while (PendingActors.Num() > 0)
	{
		const AActor* Candidate = PendingActors.Pop(EAllowShrinking::No);
		if (!Candidate || VisitedActors.Contains(Candidate))
		{
			continue;
		}

		VisitedActors.Add(Candidate);
		if (IsPlayerControlledActor(Candidate))
		{
			continue;
		}

		for (const UClass* CurrentClass = Candidate->GetClass(); CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			if (ClassNameMatchesEnemyHints(CurrentClass->GetName(), Hints))
			{
				return true;
			}
		}

		if (const AController* Controller = Cast<AController>(Candidate))
		{
			PendingActors.Add(Controller->GetPawn());
		}

		PendingActors.Add(Candidate->GetOwner());
		PendingActors.Add(Candidate->GetInstigator());
		PendingActors.Add(Candidate->GetAttachParentActor());
	}

	return false;
}

AActor* UProjectRealtimeSnapshotComponent::FindNearestRelevantEnemyToOwner() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	AActor* BestEnemy = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const FProjectObservedActorHealthRuntime& ObservedEnemy : ObservedEnemies)
	{
		AActor* EnemyActor = ObservedEnemy.Actor.Get();
		if (!EnemyActor || !IsRelevantCombatEnemy(EnemyActor) || !IsEnemyWithinRelevantRadius(EnemyActor))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(OwnerActor->GetActorLocation(), EnemyActor->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestEnemy = EnemyActor;
		}
	}

	return BestEnemy;
}

void UProjectRealtimeSnapshotComponent::RefreshCachedComponents()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		NeedsComponent = nullptr;
		StatusComponent = nullptr;
		InnerDoctrineComponent = nullptr;
		AttributeBridgeComponent = nullptr;
		CombatAttributeComponent = nullptr;
		return;
	}

	NeedsComponent = OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	StatusComponent = OwnerActor->FindComponentByClass<UProjectSurvivalStatusComponent>();
	InnerDoctrineComponent = OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>();
	AttributeBridgeComponent = OwnerActor->FindComponentByClass<UProjectSurvivalAttributeBridgeComponent>();
	CombatAttributeComponent = OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>();
}

void UProjectRealtimeSnapshotComponent::RefreshObservedEnemies()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor && Actor != GetOwner() && IsRelevantCombatEnemy(Actor))
		{
			ObserveEnemy(Actor);
		}
	}
}

void UProjectRealtimeSnapshotComponent::ObserveEnemy(AActor* EnemyActor)
{
	if (!EnemyActor || EnemyActor == GetOwner() || !IsRelevantCombatEnemy(EnemyActor))
	{
		return;
	}

	for (const FProjectObservedActorHealthRuntime& ObservedEnemy : ObservedEnemies)
	{
		if (ObservedEnemy.Actor.Get() == EnemyActor)
		{
			return;
		}
	}

	FProjectObservedActorHealthRuntime NewObservedEnemy;
	NewObservedEnemy.Actor = EnemyActor;

	float CurrentHealth = 0.f;
	if (TryReadActorHealth(EnemyActor, CurrentHealth))
	{
		NewObservedEnemy.LastHealth = CurrentHealth;
		NewObservedEnemy.bHasLastHealth = true;
		NewObservedEnemy.bHasBroadcastKill = CurrentHealth <= HealthChangeTolerance;
	}

	ObservedEnemies.Add(NewObservedEnemy);
}

void UProjectRealtimeSnapshotComponent::UpdateHealthSnapshots()
{
	bool bAnyHealthChanged = false;
	AActor* OwnerActor = GetOwner();

	float OwnerHealth = 0.f;
	if (TryReadActorHealth(OwnerActor, OwnerHealth))
	{
		if (bHasOwnerLastHealth && !FMath::IsNearlyEqual(OwnerHealth, OwnerLastHealth, HealthChangeTolerance))
		{
			FProjectRealtimeCombatImpact HealthChange;
			HealthChange.Actor = OwnerActor;
			HealthChange.LikelySourceActor = FindNearestRelevantEnemyToOwner();
			HealthChange.OldHealth = OwnerLastHealth;
			HealthChange.NewHealth = OwnerHealth;
			HealthChange.DamageDelta = FMath::Max(0.f, OwnerLastHealth - OwnerHealth);
			HealthChange.bOwnerImpact = HealthChange.DamageDelta > HealthChangeTolerance;
			HealthChange.ImpactType = HealthChange.bOwnerImpact
				? EProjectRealtimeCombatImpactType::OwnerHealthLost
				: EProjectRealtimeCombatImpactType::None;
			EmitHealthChange(HealthChange);
			bAnyHealthChanged = true;
		}

		OwnerLastHealth = OwnerHealth;
		bHasOwnerLastHealth = true;
	}
	else
	{
		bHasOwnerLastHealth = false;
	}

	for (int32 Index = ObservedEnemies.Num() - 1; Index >= 0; --Index)
	{
		FProjectObservedActorHealthRuntime& ObservedEnemy = ObservedEnemies[Index];
		AActor* EnemyActor = ObservedEnemy.Actor.Get();
		if (!EnemyActor || EnemyActor->IsActorBeingDestroyed() || !IsRelevantCombatEnemy(EnemyActor))
		{
			ObservedEnemies.RemoveAtSwap(Index);
			continue;
		}

		float CurrentHealth = 0.f;
		if (!TryReadActorHealth(EnemyActor, CurrentHealth))
		{
			ObservedEnemy.bHasLastHealth = false;
			continue;
		}

		if (CurrentHealth > HealthChangeTolerance)
		{
			ObservedEnemy.bHasBroadcastKill = false;
		}

		if (ObservedEnemy.bHasLastHealth && !FMath::IsNearlyEqual(CurrentHealth, ObservedEnemy.LastHealth, HealthChangeTolerance))
		{
			const float DamageDelta = FMath::Max(0.f, ObservedEnemy.LastHealth - CurrentHealth);
			const bool bEnemyLostHealth = DamageDelta > HealthChangeTolerance && IsEnemyWithinRelevantRadius(EnemyActor);
			const bool bKilledTarget = bEnemyLostHealth
				&& !ObservedEnemy.bHasBroadcastKill
				&& ObservedEnemy.LastHealth > HealthChangeTolerance
				&& CurrentHealth <= HealthChangeTolerance;

			FProjectRealtimeCombatImpact HealthChange;
			HealthChange.Actor = EnemyActor;
			HealthChange.LikelySourceActor = OwnerActor;
			HealthChange.OldHealth = ObservedEnemy.LastHealth;
			HealthChange.NewHealth = CurrentHealth;
			HealthChange.DamageDelta = DamageDelta;
			HealthChange.bEnemyImpact = bEnemyLostHealth;
			HealthChange.bKilledTarget = bKilledTarget;
			HealthChange.ImpactType = bKilledTarget
				? EProjectRealtimeCombatImpactType::EnemyKilled
				: (bEnemyLostHealth ? EProjectRealtimeCombatImpactType::EnemyHealthLost : EProjectRealtimeCombatImpactType::None);

			if (bKilledTarget)
			{
				ObservedEnemy.bHasBroadcastKill = true;
			}

			EmitHealthChange(HealthChange);
			bAnyHealthChanged = true;
		}

		ObservedEnemy.LastHealth = CurrentHealth;
		ObservedEnemy.bHasLastHealth = true;
	}

	if (bAnyHealthChanged)
	{
		BroadcastSnapshotChanged();
	}
}

void UProjectRealtimeSnapshotComponent::BroadcastSnapshotChanged()
{
	OnUnifiedRuntimeSnapshotChanged.Broadcast(BuildUnifiedRuntimeSnapshot());
}

FProjectRealtimeActorHealthSnapshot UProjectRealtimeSnapshotComponent::BuildActorHealthSnapshot(AActor* Actor) const
{
	FProjectRealtimeActorHealthSnapshot Snapshot;
	Snapshot.Actor = Actor;
	Snapshot.ActorName = GetNameSafe(Actor);
	Snapshot.ActorClassName = Actor ? Actor->GetClass()->GetName() : FString();
	Snapshot.bRelevantEnemy = IsRelevantCombatEnemy(Actor);

	if (TryReadActorHealth(Actor, Snapshot.CurrentHealth))
	{
		Snapshot.bHasHealth = true;
	}

	return Snapshot;
}

bool UProjectRealtimeSnapshotComponent::IsEnemyWithinRelevantRadius(const AActor* EnemyActor) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !EnemyActor)
	{
		return false;
	}

	const UProjectRealtimeSnapshotSettings* Settings = UProjectRealtimeSnapshotSettings::Get();
	const float RelevantRadius = Settings ? Settings->RelevantEnemyRadius : 10000.f;
	if (RelevantRadius <= 0.f)
	{
		return true;
	}

	return FVector::DistSquared(OwnerActor->GetActorLocation(), EnemyActor->GetActorLocation()) <= FMath::Square(RelevantRadius);
}

void UProjectRealtimeSnapshotComponent::EmitHealthChange(const FProjectRealtimeCombatImpact& Impact)
{
	OnRealtimeActorHealthChanged.Broadcast(Impact);
	if (Impact.ImpactType != EProjectRealtimeCombatImpactType::None)
	{
		OnRealtimeCombatImpact.Broadcast(Impact);
	}
}

void UProjectRealtimeSnapshotComponent::HandleActorSpawned(AActor* SpawnedActor)
{
	if (SpawnedActor && SpawnedActor != GetOwner() && IsRelevantCombatEnemy(SpawnedActor))
	{
		ObserveEnemy(SpawnedActor);
	}
}
