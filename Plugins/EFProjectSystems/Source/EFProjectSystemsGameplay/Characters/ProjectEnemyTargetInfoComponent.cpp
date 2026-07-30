#include "Characters/ProjectEnemyTargetInfoComponent.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Blueprint/UserWidget.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Survival/ProjectRuntimeReflectionLibrary.h"
#include "UI/ProjectTargetLevelWidget.h"
#include "UI/ProjectTargetPointWidget.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEnemyTargetInfo, Log, All);

namespace ProjectEnemyTargetInfoPrivate
{
	constexpr float TargetInfoWidgetVerticalOffset = 176.0f;
	const FName TargetInfoWidgetSharedLayerName(TEXT("ProjectEnemyTargetInfoLayer"));
	constexpr int32 TargetInfoWidgetLayerZOrder = 10;
	constexpr float TargetPointFallbackHeightRatio = 0.38f;
	const FName TargetPointWidgetSharedLayerName(TEXT("ProjectEnemyTargetPointLayer"));
	constexpr int32 TargetPointWidgetLayerZOrder = 120;

	struct FResolvedFloatAttributeBinding
	{
		TWeakObjectPtr<UAttributeSet> AttributeSet;
		FProperty* Property = nullptr;
		FGameplayAttribute GameplayAttribute;
		bool bGameplayAttributeData = false;
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

	struct FResolvedHudGuide
	{
		USceneComponent* AttachParent = nullptr;
		FName SocketName = NAME_None;
		FVector WorldLocation = FVector::ZeroVector;
	};

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

	static bool StringContainsAnyHint(const FString& Value, const TArray<FString>& Hints)
	{
		const FString NormalizedValue = NormalizeName(Value);
		for (const FString& Hint : Hints)
		{
			if (NormalizedValue.Contains(NormalizeName(Hint)))
			{
				return true;
			}
		}

		return false;
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

	static bool ResolveAttributeBinding(
		UAbilitySystemComponent* AbilitySystemComponent,
		const TArray<FString>& CandidateNames,
		FResolvedFloatAttributeBinding& OutBinding)
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

	static float ReadAttributeValue(UAbilitySystemComponent* AbilitySystemComponent, const FResolvedFloatAttributeBinding& Binding)
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

		return 0.0f;
	}

	static float ReadAttributeBaseValue(UAbilitySystemComponent* AbilitySystemComponent, const FResolvedFloatAttributeBinding& Binding)
	{
		if (Binding.bGameplayAttributeData && AbilitySystemComponent)
		{
			return AbilitySystemComponent->GetNumericAttributeBase(Binding.GameplayAttribute);
		}

		return ReadAttributeValue(AbilitySystemComponent, Binding);
	}

	static UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* TargetActor)
	{
		if (!TargetActor)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* AbilitySystemComponent = TargetActor->FindComponentByClass<UAbilitySystemComponent>())
		{
			return AbilitySystemComponent;
		}

		return FindComponentByClassHint<UAbilitySystemComponent>(TargetActor, { TEXT("AbilitySystemComponent"), TEXT("ACFAbilitySystemComponent") });
	}

	static FProperty* FindSingleInputProperty(UFunction* Function)
	{
		FProperty* InputProperty = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
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

	static bool TryInvokeStatisticFloatFunction(
		UObject* Target,
		const FName FunctionName,
		const FGameplayTag HealthTag,
		const FName HealthName,
		const FString& HealthString,
		float& OutReturnValue)
	{
		OutReturnValue = 0.0f;

		if (!Target || FunctionName.IsNone())
		{
			return false;
		}

		UFunction* Function = Target->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		FProperty* InputProperty = FindSingleInputProperty(Function);
		FProperty* ReturnProperty = FindReturnProperty(Function);
		if (!InputProperty || !ReturnProperty || Function->ParmsSize <= 0)
		{
			return false;
		}

		void* Parms = FMemory_Alloca(Function->ParmsSize);
		FMemory::Memzero(Parms, Function->ParmsSize);

		if (FStructProperty* StructProperty = CastField<FStructProperty>(InputProperty))
		{
			if (StructProperty->Struct == FGameplayTag::StaticStruct())
			{
				*StructProperty->ContainerPtrToValuePtr<FGameplayTag>(Parms) = HealthTag;
			}
			else
			{
				return false;
			}
		}
		else if (FNameProperty* NameProperty = CastField<FNameProperty>(InputProperty))
		{
			NameProperty->SetPropertyValue_InContainer(Parms, HealthName);
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(InputProperty))
		{
			StringProperty->SetPropertyValue_InContainer(Parms, HealthString);
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

	static bool TryReadHealthFromStatisticsComponent(AActor* TargetActor, FTargetHealthSnapshot& OutSnapshot)
	{
		UObject* StatisticsComponent = FindComponentByClassHint<UObject>(TargetActor, { TEXT("ACFGASStatisticsComponent"), TEXT("ARSStatisticsComponent") });
		if (!StatisticsComponent)
		{
			return false;
		}

		const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(FName(TEXT("RPG.Statistics.Health")), false);
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

		float NormalizedHealth = 0.0f;
		if (TryInvokeStatisticFloatFunction(StatisticsComponent, TEXT("GetNormalizedValueForStatitstic"), HealthTag, HealthName, HealthString, NormalizedHealth))
		{
			OutSnapshot.bHasHealthRatio = true;
			OutSnapshot.HealthRatio = FMath::Clamp(NormalizedHealth, 0.0f, 1.0f);
		}

		if (!OutSnapshot.bHasHealthRatio && OutSnapshot.bHasCurrentHealth && OutSnapshot.bHasMaxHealth && OutSnapshot.MaxHealth > KINDA_SMALL_NUMBER)
		{
			OutSnapshot.bHasHealthRatio = true;
			OutSnapshot.HealthRatio = FMath::Clamp(OutSnapshot.CurrentHealth / OutSnapshot.MaxHealth, 0.0f, 1.0f);
		}

		if (!OutSnapshot.bHasCurrentHealth && OutSnapshot.bHasMaxHealth && OutSnapshot.bHasHealthRatio)
		{
			OutSnapshot.bHasCurrentHealth = true;
			OutSnapshot.CurrentHealth = OutSnapshot.MaxHealth * OutSnapshot.HealthRatio;
		}

		if (!OutSnapshot.bHasMaxHealth && OutSnapshot.bHasCurrentHealth && OutSnapshot.bHasHealthRatio && OutSnapshot.HealthRatio > KINDA_SMALL_NUMBER)
		{
			OutSnapshot.bHasMaxHealth = true;
			OutSnapshot.MaxHealth = OutSnapshot.CurrentHealth / OutSnapshot.HealthRatio;
		}

		return OutSnapshot.bHasCurrentHealth || OutSnapshot.bHasMaxHealth || OutSnapshot.bHasHealthRatio;
	}

	static bool TryReadHealthFromAbilitySystem(AActor* TargetActor, FTargetHealthSnapshot& OutSnapshot)
	{
		UAbilitySystemComponent* AbilitySystemComponent = ResolveAbilitySystemComponent(TargetActor);
		if (!AbilitySystemComponent)
		{
			return false;
		}

		FResolvedFloatAttributeBinding CurrentHealthBinding;
		if (!ResolveAttributeBinding(AbilitySystemComponent, { TEXT("Health"), TEXT("CurrentHealth") }, CurrentHealthBinding))
		{
			return false;
		}

		OutSnapshot.bHasCurrentHealth = true;
		OutSnapshot.CurrentHealth = ReadAttributeValue(AbilitySystemComponent, CurrentHealthBinding);

		FResolvedFloatAttributeBinding MaxHealthBinding;
		if (ResolveAttributeBinding(AbilitySystemComponent, { TEXT("MaxHealth"), TEXT("MaximumHealth"), TEXT("HealthMax"), TEXT("HealthMaximum") }, MaxHealthBinding))
		{
			OutSnapshot.bHasMaxHealth = true;
			OutSnapshot.MaxHealth = ReadAttributeValue(AbilitySystemComponent, MaxHealthBinding);
		}
		else
		{
			const float BaseHealthValue = ReadAttributeBaseValue(AbilitySystemComponent, CurrentHealthBinding);
			if (BaseHealthValue > KINDA_SMALL_NUMBER)
			{
				OutSnapshot.bHasMaxHealth = true;
				OutSnapshot.MaxHealth = BaseHealthValue;
			}
		}

		if (OutSnapshot.bHasCurrentHealth && OutSnapshot.bHasMaxHealth && OutSnapshot.MaxHealth > KINDA_SMALL_NUMBER)
		{
			OutSnapshot.bHasHealthRatio = true;
			OutSnapshot.HealthRatio = FMath::Clamp(OutSnapshot.CurrentHealth / OutSnapshot.MaxHealth, 0.0f, 1.0f);
		}

		return OutSnapshot.bHasCurrentHealth;
	}

	static FString GetClassDisplayName(const UClass* ActorClass)
	{
		if (!ActorClass)
		{
			return TEXT("Enemy");
		}

		FString DisplayName = ActorClass->GetName();
		DisplayName.RemoveFromEnd(TEXT("_C"));

		FString SpacedName;
		SpacedName.Reserve(DisplayName.Len() * 2);
		for (int32 Index = 0; Index < DisplayName.Len(); ++Index)
		{
			const TCHAR Character = DisplayName[Index];
			const bool bShouldInsertSpace = Index > 0
				&& FChar::IsUpper(Character)
				&& (FChar::IsLower(DisplayName[Index - 1]) || FChar::IsDigit(DisplayName[Index - 1]));
			if (bShouldInsertSpace)
			{
				SpacedName.AppendChar(TEXT(' '));
			}

			SpacedName.AppendChar(Character);
		}

		return SpacedName;
	}

	static bool ClassLineageContains(const UClass* ActorClass, const FString& Token)
	{
		for (const UClass* CurrentClass = ActorClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			if (CurrentClass->GetName().Contains(Token))
			{
				return true;
			}
		}

		return false;
	}

	static FText ResolveEnemyTypeText(const UClass* ActorClass)
	{
		if (!ActorClass)
		{
			return FText::FromString(TEXT("Enemy"));
		}

		if (ClassLineageContains(ActorClass, TEXT("DummyMale")) || ClassLineageContains(ActorClass, TEXT("ACFDummyEnemyBP")))
		{
			return FText::FromString(TEXT("Dummy"));
		}

		if (ClassLineageContains(ActorClass, TEXT("MageMale")) || ClassLineageContains(ActorClass, TEXT("ACFMageEnemyBP")))
		{
			return FText::FromString(TEXT("Mage"));
		}

		if (ClassLineageContains(ActorClass, TEXT("RangedMale")) || ClassLineageContains(ActorClass, TEXT("ACFRangedEnemyBP")))
		{
			return FText::FromString(TEXT("Ranged"));
		}

		if (ClassLineageContains(ActorClass, TEXT("MeleeMale")) || ClassLineageContains(ActorClass, TEXT("ACFMeleeEnemyBP")))
		{
			return FText::FromString(TEXT("Melee"));
		}

		return FText::FromString(GetClassDisplayName(ActorClass));
	}

	static bool IsLegacyTargetWidgetComponent(const UWidgetComponent* WidgetComponent)
	{
		if (!WidgetComponent)
		{
			return false;
		}

		const FString ComponentName = WidgetComponent->GetName();
		if (StringContainsAnyHint(ComponentName, { TEXT("ProjectTarget"), TEXT("ProjectEnemyTarget"), TEXT("ProjectTargetInfo") }))
		{
			return false;
		}

		if (const UClass* WidgetClass = WidgetComponent->GetWidgetClass())
		{
			if (StringContainsAnyHint(WidgetClass->GetName(), { TEXT("ProjectTarget"), TEXT("ProjectEnemyTarget"), TEXT("ProjectTargetInfo") }))
			{
				return false;
			}
		}

		if (const UUserWidget* WidgetObject = WidgetComponent->GetUserWidgetObject())
		{
			if (StringContainsAnyHint(WidgetObject->GetClass()->GetName(), { TEXT("ProjectTarget"), TEXT("ProjectEnemyTarget"), TEXT("ProjectTargetInfo") }))
			{
				return false;
			}
		}

		FString CombinedHints = ComponentName;
		if (const UClass* WidgetClass = WidgetComponent->GetWidgetClass())
		{
			CombinedHints += WidgetClass->GetName();
		}
		if (const UUserWidget* WidgetObject = WidgetComponent->GetUserWidgetObject())
		{
			CombinedHints += WidgetObject->GetClass()->GetName();
			CombinedHints += WidgetObject->GetName();
		}

		return StringContainsAnyHint(
			CombinedHints,
			{
				TEXT("ACFHealthWidget"),
				TEXT("HealthWidget"),
				TEXT("TargetedWidget"),
				TEXT("TargetWidget"),
				TEXT("HealthBar"),
				TEXT("TargetHealth"),
				TEXT("LockOn"),
				TEXT("FocusedHealth"),
				TEXT("ACFGASHealth")
			});
	}

	static USceneComponent* ResolvePointAttachParent(AActor* Owner, FName& OutSocketName)
	{
		OutSocketName = NAME_None;

		if (!Owner)
		{
			return nullptr;
		}

		auto TryResolveOnMesh = [&OutSocketName](USkeletalMeshComponent* MeshComponent) -> USceneComponent*
		{
			if (!MeshComponent)
			{
				return nullptr;
			}

			static const TArray<FName> PreferredChestSockets = {
				TEXT("spine_03"),
				TEXT("Spine_03"),
				TEXT("spine_02"),
				TEXT("Spine_02"),
				TEXT("spine_01"),
				TEXT("Spine_01"),
				TEXT("chest"),
				TEXT("Chest")
			};

			for (const FName SocketName : PreferredChestSockets)
			{
				if (!SocketName.IsNone() && MeshComponent->DoesSocketExist(SocketName))
				{
					OutSocketName = SocketName;
					return MeshComponent;
				}
			}

			return MeshComponent;
		};

		if (ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (USceneComponent* AttachParent = TryResolveOnMesh(Character->GetMesh()))
			{
				return AttachParent;
			}
		}

		if (USceneComponent* AttachParent = TryResolveOnMesh(Owner->FindComponentByClass<USkeletalMeshComponent>()))
		{
			return AttachParent;
		}

		return Owner->GetRootComponent();
	}

	static bool ResolveSharedHudGuide(AActor* Owner, FResolvedHudGuide& OutGuide)
	{
		OutGuide = FResolvedHudGuide();

		if (!Owner)
		{
			return false;
		}

		FVector Origin = FVector::ZeroVector;
		FVector BoxExtent = FVector(0.0f, 0.0f, 80.0f);
		Owner->GetActorBounds(true, Origin, BoxExtent);

		OutGuide.AttachParent = ResolvePointAttachParent(Owner, OutGuide.SocketName);
		if (!OutGuide.AttachParent)
		{
			return false;
		}

		if (!OutGuide.SocketName.IsNone())
		{
			OutGuide.WorldLocation = OutGuide.AttachParent->GetSocketLocation(OutGuide.SocketName);
			return true;
		}

		const FVector LocalFallbackLocation(0.0f, 0.0f, BoxExtent.Z * TargetPointFallbackHeightRatio);
		OutGuide.WorldLocation = OutGuide.AttachParent->GetComponentTransform().TransformPosition(LocalFallbackLocation);
		return true;
	}
}

UProjectEnemyTargetInfoComponent::UProjectEnemyTargetInfoComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UProjectEnemyTargetInfoComponent::BeginPlay()
{
	Super::BeginPlay();
	HideTargetInfo();
}

void UProjectEnemyTargetInfoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideTargetInfo();
	TargetWidgetComponent = nullptr;
	StatsWidgetComponent = nullptr;
	PointWidgetComponent = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UProjectEnemyTargetInfoComponent::ShowTargetInfo()
{
	EnsureWidgetComponents();
	DisableLegacyTargetWidgets();
	RefreshTargetInfo();

	if (TargetWidgetComponent)
	{
		TargetWidgetComponent->SetHiddenInGame(false);
		TargetWidgetComponent->SetVisibility(true, true);
		TargetWidgetComponent->Activate(true);
	}

	if (PointWidgetComponent)
	{
		if (UProjectTargetPointWidget* Widget = ResolvePointWidget())
		{
			Widget->SetOverlayVisible(true);
		}

		PointWidgetComponent->SetHiddenInGame(false);
		PointWidgetComponent->SetVisibility(true, true);
		PointWidgetComponent->Activate(true);
	}
}

void UProjectEnemyTargetInfoComponent::HideTargetInfo()
{
	EnsureWidgetComponents();
	DisableLegacyTargetWidgets();

	if (UProjectTargetLevelWidget* Widget = ResolveTargetWidget())
	{
		Widget->SetOverlayVisible(false);
	}

	if (UProjectTargetPointWidget* Widget = ResolvePointWidget())
	{
		Widget->SetOverlayVisible(false);
	}

	if (PointWidgetComponent)
	{
		PointWidgetComponent->SetHiddenInGame(true);
		PointWidgetComponent->SetVisibility(false, true);
		PointWidgetComponent->Deactivate();
	}

	if (StatsWidgetComponent)
	{
		StatsWidgetComponent->SetHiddenInGame(true);
		StatsWidgetComponent->SetVisibility(false, true);
		StatsWidgetComponent->Deactivate();
	}

	if (TargetWidgetComponent)
	{
		TargetWidgetComponent->SetHiddenInGame(true);
		TargetWidgetComponent->SetVisibility(false, true);
		TargetWidgetComponent->Deactivate();
	}
}

void UProjectEnemyTargetInfoComponent::RefreshTargetInfo()
{
	EnsureWidgetComponents();
	DisableLegacyTargetWidgets();
	UpdateWidgetAttachment();

	UProjectTargetLevelWidget* Widget = ResolveTargetWidget();
	if (!TargetWidgetComponent || !Widget)
	{
		return;
	}

	FProjectEnemyTargetDisplayData DisplayData;
	if (!TryBuildDisplayData(DisplayData))
	{
		return;
	}

	Widget->SetTargetDisplayData(
		DisplayData.EnemyType,
		DisplayData.EnemyName,
		DisplayData.Level,
		DisplayData.CurrentHealth,
		DisplayData.MaxHealth,
		DisplayData.HealthRatio);
	Widget->SetOverlayVisible(true);
	TargetWidgetComponent->SetHiddenInGame(false);
	TargetWidgetComponent->SetVisibility(true, true);
}

bool UProjectEnemyTargetInfoComponent::HasConstructedTargetWidgets() const
{
	return TargetWidgetComponent != nullptr || PointWidgetComponent != nullptr;
}

bool UProjectEnemyTargetInfoComponent::IsTargetLevelOverlayVisible() const
{
	if (const UProjectTargetLevelWidget* Widget = ResolveTargetWidget())
	{
		return Widget->IsOverlayVisible();
	}

	return false;
}

bool UProjectEnemyTargetInfoComponent::IsTargetStatsOverlayVisible() const
{
	return false;
}

bool UProjectEnemyTargetInfoComponent::IsTargetPointOverlayVisible() const
{
	if (const UProjectTargetPointWidget* Widget = ResolvePointWidget())
	{
		return Widget->IsOverlayVisible();
	}

	return false;
}

FText UProjectEnemyTargetInfoComponent::GetEnemyTypeText() const
{
	const AActor* Owner = GetOwner();
	return ProjectEnemyTargetInfoPrivate::ResolveEnemyTypeText(Owner ? Owner->GetClass() : nullptr);
}

bool UProjectEnemyTargetInfoComponent::TryBuildDisplayData(FProjectEnemyTargetDisplayData& OutDisplayData) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	OutDisplayData.EnemyType = GetEnemyTypeText();
	OutDisplayData.EnemyName = GetEnemyNameText();

	const UProjectEnemyLevelComponent* LevelComponent = Owner->FindComponentByClass<UProjectEnemyLevelComponent>();
	OutDisplayData.Level = (LevelComponent && LevelComponent->HasAssignedLevel()) ? LevelComponent->GetAssignedLevel() : 1;

	if (LevelComponent)
	{
		float DisplayCurrentHealth = -1.0f;
		float DisplayMaxHealth = -1.0f;
		float DisplayHealthRatio = 0.0f;
		if (LevelComponent->TryGetDisplayHealthSnapshot(DisplayCurrentHealth, DisplayMaxHealth, DisplayHealthRatio))
		{
			OutDisplayData.CurrentHealth = DisplayCurrentHealth;
			OutDisplayData.MaxHealth = DisplayMaxHealth;
			OutDisplayData.HealthRatio = DisplayHealthRatio;
			return true;
		}
	}

	ProjectEnemyTargetInfoPrivate::FTargetHealthSnapshot HealthSnapshot;
	const bool bHasHealthData = ProjectEnemyTargetInfoPrivate::TryReadHealthFromStatisticsComponent(const_cast<AActor*>(Owner), HealthSnapshot)
		|| ProjectEnemyTargetInfoPrivate::TryReadHealthFromAbilitySystem(const_cast<AActor*>(Owner), HealthSnapshot);

	OutDisplayData.CurrentHealth = bHasHealthData && HealthSnapshot.bHasCurrentHealth ? HealthSnapshot.CurrentHealth : -1.0f;
	OutDisplayData.MaxHealth = bHasHealthData && HealthSnapshot.bHasMaxHealth ? HealthSnapshot.MaxHealth : (bHasHealthData ? OutDisplayData.CurrentHealth : -1.0f);
	OutDisplayData.HealthRatio = bHasHealthData && HealthSnapshot.bHasHealthRatio ? HealthSnapshot.HealthRatio : 0.0f;
	return true;
}

bool UProjectEnemyTargetInfoComponent::TryGetTargetInfoAnchorWorldLocation(FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	ProjectEnemyTargetInfoPrivate::FResolvedHudGuide HudGuide;
	if (!ProjectEnemyTargetInfoPrivate::ResolveSharedHudGuide(Owner, HudGuide))
	{
		return false;
	}

	OutWorldLocation = HudGuide.WorldLocation + FVector(0.0f, 0.0f, ProjectEnemyTargetInfoPrivate::TargetInfoWidgetVerticalOffset);
	return true;
}

void UProjectEnemyTargetInfoComponent::EnsureWidgetComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!TargetWidgetComponent)
	{
		TInlineComponentArray<UWidgetComponent*> WidgetComponents(Owner);
		for (UWidgetComponent* Candidate : WidgetComponents)
		{
			if (Candidate && Candidate->GetFName() == TEXT("ProjectTargetInfoWidgetComponent"))
			{
				TargetWidgetComponent = Candidate;
				break;
			}
		}
	}

	if (!StatsWidgetComponent)
	{
		TInlineComponentArray<UWidgetComponent*> WidgetComponents(Owner);
		for (UWidgetComponent* Candidate : WidgetComponents)
		{
			if (Candidate && Candidate->GetFName() == TEXT("ProjectTargetStatsWidgetComponent"))
			{
				StatsWidgetComponent = Candidate;
				break;
			}
		}
	}

	if (!PointWidgetComponent)
	{
		TInlineComponentArray<UWidgetComponent*> WidgetComponents(Owner);
		for (UWidgetComponent* Candidate : WidgetComponents)
		{
			if (Candidate && Candidate->GetFName() == TEXT("ProjectTargetPointWidgetComponent"))
			{
				PointWidgetComponent = Candidate;
				break;
			}
		}
	}

	if (!TargetWidgetComponent)
	{
		TargetWidgetComponent = NewObject<UWidgetComponent>(Owner, UWidgetComponent::StaticClass(), TEXT("ProjectTargetInfoWidgetComponent"));
		if (!TargetWidgetComponent)
		{
			UE_LOG(LogProjectEnemyTargetInfo, Warning, TEXT("Could not create ProjectTargetInfoWidgetComponent for %s."), *GetNameSafe(Owner));
			return;
		}

		Owner->AddInstanceComponent(TargetWidgetComponent);
		TargetWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
		TargetWidgetComponent->SetInitialSharedLayerName(ProjectEnemyTargetInfoPrivate::TargetInfoWidgetSharedLayerName);
		TargetWidgetComponent->SetInitialLayerZOrder(ProjectEnemyTargetInfoPrivate::TargetInfoWidgetLayerZOrder);
		TargetWidgetComponent->SetDrawAtDesiredSize(true);
		TargetWidgetComponent->SetDrawSize(FVector2D(286.0f, 98.0f));
		TargetWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetWidgetComponent->SetGenerateOverlapEvents(false);
		TargetWidgetComponent->SetWidgetClass(UProjectTargetLevelWidget::StaticClass());
		TargetWidgetComponent->SetTwoSided(true);
		TargetWidgetComponent->SetPivot(FVector2D(0.5f, 1.0f));
		TargetWidgetComponent->SetHiddenInGame(true);
		TargetWidgetComponent->SetVisibility(false, true);
		TargetWidgetComponent->OnComponentCreated();
		TargetWidgetComponent->RegisterComponent();
	}

	if (!PointWidgetComponent)
	{
		PointWidgetComponent = NewObject<UWidgetComponent>(Owner, UWidgetComponent::StaticClass(), TEXT("ProjectTargetPointWidgetComponent"));
		if (!PointWidgetComponent)
		{
			UE_LOG(LogProjectEnemyTargetInfo, Warning, TEXT("Could not create ProjectTargetPointWidgetComponent for %s."), *GetNameSafe(Owner));
			return;
		}

		Owner->AddInstanceComponent(PointWidgetComponent);
		PointWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
		PointWidgetComponent->SetInitialSharedLayerName(ProjectEnemyTargetInfoPrivate::TargetPointWidgetSharedLayerName);
		PointWidgetComponent->SetInitialLayerZOrder(ProjectEnemyTargetInfoPrivate::TargetPointWidgetLayerZOrder);
		PointWidgetComponent->SetDrawAtDesiredSize(true);
		PointWidgetComponent->SetDrawSize(FVector2D(16.0f, 16.0f));
		PointWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PointWidgetComponent->SetGenerateOverlapEvents(false);
		PointWidgetComponent->SetWidgetClass(UProjectTargetPointWidget::StaticClass());
		PointWidgetComponent->SetTwoSided(true);
		PointWidgetComponent->SetPivot(FVector2D(0.5f, 0.5f));
		PointWidgetComponent->SetHiddenInGame(true);
		PointWidgetComponent->SetVisibility(false, true);
		PointWidgetComponent->OnComponentCreated();
		PointWidgetComponent->RegisterComponent();
	}

	if (UWorld* World = GetWorld())
	{
		if (ULocalPlayer* LocalPlayer = World->GetFirstLocalPlayerFromController())
		{
			if (TargetWidgetComponent)
			{
				TargetWidgetComponent->SetOwnerPlayer(LocalPlayer);
			}

			if (PointWidgetComponent)
			{
				PointWidgetComponent->SetOwnerPlayer(LocalPlayer);
			}
		}
	}

	if (TargetWidgetComponent)
	{
		if (TargetWidgetComponent->GetWidgetClass() != UProjectTargetLevelWidget::StaticClass())
		{
			TargetWidgetComponent->SetWidgetClass(UProjectTargetLevelWidget::StaticClass());
		}

		TargetWidgetComponent->SetInitialSharedLayerName(ProjectEnemyTargetInfoPrivate::TargetInfoWidgetSharedLayerName);
		TargetWidgetComponent->SetInitialLayerZOrder(ProjectEnemyTargetInfoPrivate::TargetInfoWidgetLayerZOrder);
		TargetWidgetComponent->SetDrawAtDesiredSize(true);
		TargetWidgetComponent->SetDrawSize(FVector2D(286.0f, 98.0f));
	}

	if (StatsWidgetComponent)
	{
		if (UUserWidget* LegacyStatsWidget = StatsWidgetComponent->GetUserWidgetObject())
		{
			LegacyStatsWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		StatsWidgetComponent->SetHiddenInGame(true);
		StatsWidgetComponent->SetVisibility(false, true);
		StatsWidgetComponent->Deactivate();
		StatsWidgetComponent->SetComponentTickEnabled(false);
	}

	if (PointWidgetComponent && PointWidgetComponent->GetWidgetClass() != UProjectTargetPointWidget::StaticClass())
	{
		PointWidgetComponent->SetWidgetClass(UProjectTargetPointWidget::StaticClass());
	}
	if (PointWidgetComponent)
	{
		PointWidgetComponent->SetInitialSharedLayerName(ProjectEnemyTargetInfoPrivate::TargetPointWidgetSharedLayerName);
		PointWidgetComponent->SetInitialLayerZOrder(ProjectEnemyTargetInfoPrivate::TargetPointWidgetLayerZOrder);
		PointWidgetComponent->SetDrawAtDesiredSize(true);
		PointWidgetComponent->SetDrawSize(FVector2D(16.0f, 16.0f));
	}

	UpdateWidgetAttachment();

	if (TargetWidgetComponent && !TargetWidgetComponent->GetUserWidgetObject())
	{
		TargetWidgetComponent->InitWidget();
	}

	if (PointWidgetComponent && !PointWidgetComponent->GetUserWidgetObject())
	{
		PointWidgetComponent->InitWidget();
	}
}

void UProjectEnemyTargetInfoComponent::UpdateWidgetAttachment()
{
	if (!TargetWidgetComponent && !PointWidgetComponent)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	ProjectEnemyTargetInfoPrivate::FResolvedHudGuide HudGuide;
	if (!ProjectEnemyTargetInfoPrivate::ResolveSharedHudGuide(Owner, HudGuide))
	{
		return;
	}

	auto AttachWidgetToGuide = [&HudGuide](UWidgetComponent* WidgetComponent)
	{
		if (!WidgetComponent || !HudGuide.AttachParent)
		{
			return;
		}

		if (WidgetComponent->GetAttachParent() != HudGuide.AttachParent
			|| WidgetComponent->GetAttachSocketName() != HudGuide.SocketName)
		{
			WidgetComponent->AttachToComponent(
				HudGuide.AttachParent,
				FAttachmentTransformRules::KeepRelativeTransform,
				HudGuide.SocketName);
		}
	};

	const FVector LevelWorldLocation = HudGuide.WorldLocation + FVector(0.0f, 0.0f, ProjectEnemyTargetInfoPrivate::TargetInfoWidgetVerticalOffset);

	if (TargetWidgetComponent)
	{
		AttachWidgetToGuide(TargetWidgetComponent);
		TargetWidgetComponent->SetWorldLocation(LevelWorldLocation);
	}

	if (PointWidgetComponent)
	{
		AttachWidgetToGuide(PointWidgetComponent);
		PointWidgetComponent->SetWorldLocation(HudGuide.WorldLocation);
	}
}

void UProjectEnemyTargetInfoComponent::DisableLegacyTargetWidgets()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TInlineComponentArray<UWidgetComponent*> WidgetComponents(Owner);
	for (UWidgetComponent* WidgetComponent : WidgetComponents)
	{
		if (!WidgetComponent || !ProjectEnemyTargetInfoPrivate::IsLegacyTargetWidgetComponent(WidgetComponent))
		{
			continue;
		}

		if (UUserWidget* LegacyWidget = WidgetComponent->GetUserWidgetObject())
		{
			LegacyWidget->SetVisibility(ESlateVisibility::Collapsed);
		}

		WidgetComponent->SetHiddenInGame(true);
		WidgetComponent->SetVisibility(false, true);
		WidgetComponent->Deactivate();
		WidgetComponent->SetComponentTickEnabled(false);
	}
}

UProjectTargetLevelWidget* UProjectEnemyTargetInfoComponent::ResolveTargetWidget() const
{
	return TargetWidgetComponent ? Cast<UProjectTargetLevelWidget>(TargetWidgetComponent->GetUserWidgetObject()) : nullptr;
}

UProjectTargetPointWidget* UProjectEnemyTargetInfoComponent::ResolvePointWidget() const
{
	return PointWidgetComponent ? Cast<UProjectTargetPointWidget>(PointWidgetComponent->GetUserWidgetObject()) : nullptr;
}

FText UProjectEnemyTargetInfoComponent::GetEnemyNameText() const
{
	const AActor* Owner = GetOwner();
	return FText::FromString(ProjectEnemyTargetInfoPrivate::GetClassDisplayName(Owner ? Owner->GetClass() : nullptr));
}
