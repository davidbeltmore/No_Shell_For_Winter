#include "Defeat/ProjectDefeatInventoryBridge.h"

#include "Defeat/ProjectDefeatFlowSettings.h"
#include "GameFramework/Actor.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace ProjectDefeatInventoryBridgePrivate
{
	static constexpr TCHAR ACFUArmorSlotComponentClassPath[] = TEXT("/Script/InventorySystem.ACFArmorSlotComponent");

	template<typename TObjectType>
	TObjectType* FindComponentByClassHint(AActor* Owner, const TArray<FString>& ClassHints)
	{
		if (!Owner)
		{
			return nullptr;
		}

		TInlineComponentArray<TObjectType*> Components(Owner);
		for (TObjectType* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString ClassName = Component->GetClass()->GetName();
			for (const FString& ClassHint : ClassHints)
			{
				if (!ClassHint.IsEmpty() && ClassName.Contains(ClassHint, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
		}

		return nullptr;
	}

	FString MakePrettyName(const FString& RawName)
	{
		FString Name = RawName.TrimStartAndEnd();
		Name.RemoveFromEnd(TEXT("_C"));
		Name.ReplaceInline(TEXT("_"), TEXT(" "));

		FString Humanized;
		Humanized.Reserve(Name.Len() + 4);
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			const TCHAR Character = Name[Index];
			if (Index > 0 && FChar::IsUpper(Character))
			{
				const TCHAR PreviousCharacter = Name[Index - 1];
				if (FChar::IsLower(PreviousCharacter) || FChar::IsDigit(PreviousCharacter))
				{
					Humanized.AppendChar(TEXT(' '));
				}
			}

			Humanized.AppendChar(Character);
		}

		while (Humanized.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
		{
		}

		return Humanized.TrimStartAndEnd();
	}

	FText MakePrettyText(const FString& RawName)
	{
		return FText::FromString(MakePrettyName(RawName));
	}

	bool IsEmptyText(const FText& Text)
	{
		return Text.ToString().TrimStartAndEnd().IsEmpty();
	}

	bool TryReadNumericPropertyValue(const FProperty* Property, const void* ContainerPtr, double& OutValue)
	{
		if (!Property || !ContainerPtr)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		const FNumericProperty* NumericProperty = CastField<const FNumericProperty>(Property);
		if (!NumericProperty || !ValuePtr)
		{
			return false;
		}

		if (NumericProperty->IsFloatingPoint())
		{
			OutValue = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
			return true;
		}

		if (NumericProperty->IsInteger())
		{
			OutValue = static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
			return true;
		}

		return false;
	}

	bool TryWriteNumericPropertyValue(const FProperty* Property, void* ContainerPtr, const double NewValue)
	{
		if (!Property || !ContainerPtr)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(const_cast<FProperty*>(Property));
		if (!NumericProperty || !ValuePtr)
		{
			return false;
		}

		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, NewValue);
			return true;
		}

		if (NumericProperty->IsInteger())
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, FMath::RoundToInt(NewValue));
			return true;
		}

		return false;
	}

	bool TryReadTextPropertyValue(const FProperty* Property, const void* ContainerPtr, FText& OutText);

	const FProperty* FindBestNamedProperty(UStruct* Struct, const TArray<FString>& CandidateNames, TFunctionRef<bool(const FProperty*)> Predicate)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (const FString& CandidateName : CandidateNames)
		{
			for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (Property && Predicate(Property) && Property->GetName().Equals(CandidateName, ESearchCase::IgnoreCase))
				{
					return Property;
				}
			}
		}

		for (const FString& CandidateName : CandidateNames)
		{
			for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (Property && Predicate(Property) && Property->GetName().Contains(CandidateName, ESearchCase::IgnoreCase))
				{
					return Property;
				}
			}
		}

		return nullptr;
	}

	bool TryReadDisplayTextFromObject(const UObject* Object, FText& OutText)
	{
		if (!Object)
		{
			return false;
		}

		static const TArray<FString> DisplayPropertyCandidates = {
			TEXT("DisplayName"),
			TEXT("ItemName"),
			TEXT("Name"),
			TEXT("Label"),
			TEXT("Title")
		};

		const FProperty* DisplayProperty = FindBestNamedProperty(
			Object->GetClass(),
			DisplayPropertyCandidates,
			[](const FProperty* Property)
			{
				return CastField<const FTextProperty>(Property) || CastField<const FStrProperty>(Property) || CastField<const FNameProperty>(Property)
					|| CastField<const FObjectPropertyBase>(Property) || CastField<const FClassProperty>(Property);
			});
		if (DisplayProperty && TryReadTextPropertyValue(DisplayProperty, Object, OutText) && !IsEmptyText(OutText))
		{
			return true;
		}

		if (const AActor* Actor = Cast<AActor>(Object))
		{
			OutText = MakePrettyText(Actor->GetActorNameOrLabel());
			return true;
		}

		if (const UClass* ClassObject = Cast<UClass>(Object))
		{
			OutText = MakePrettyText(ClassObject->GetName());
			return true;
		}

		OutText = MakePrettyText(Object->GetName());
		return true;
	}

	bool TryReadTextPropertyValue(const FProperty* Property, const void* ContainerPtr, FText& OutText)
	{
		if (!Property || !ContainerPtr)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (!ValuePtr)
		{
			return false;
		}

		if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
		{
			OutText = TextProperty->GetPropertyValue(ValuePtr);
			return !IsEmptyText(OutText);
		}

		if (const FStrProperty* StringProperty = CastField<const FStrProperty>(Property))
		{
			const FString Value = StringProperty->GetPropertyValue(ValuePtr);
			if (!Value.TrimStartAndEnd().IsEmpty())
			{
				OutText = MakePrettyText(Value);
				return true;
			}

			return false;
		}

		if (const FNameProperty* NameProperty = CastField<const FNameProperty>(Property))
		{
			const FName Value = NameProperty->GetPropertyValue(ValuePtr);
			if (!Value.IsNone())
			{
				OutText = MakePrettyText(Value.ToString());
				return true;
			}

			return false;
		}

		if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(Property))
		{
			if (const UClass* ClassValue = Cast<UClass>(ClassProperty->GetPropertyValue(ValuePtr).Get()))
			{
				OutText = MakePrettyText(ClassValue->GetName());
				return true;
			}

			return false;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
		{
			return TryReadDisplayTextFromObject(ObjectProperty->GetObjectPropertyValue(ValuePtr), OutText);
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObjectValue = SoftObjectProperty->GetPropertyValue(ValuePtr);
			if (!SoftObjectValue.IsNull())
			{
				OutText = MakePrettyText(SoftObjectValue.ToString());
				return true;
			}
		}

		if (const FSoftClassProperty* SoftClassProperty = CastField<const FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr SoftClassValue = SoftClassProperty->GetPropertyValue(ValuePtr);
			if (!SoftClassValue.IsNull())
			{
				OutText = MakePrettyText(SoftClassValue.ToString());
				return true;
			}
		}

		return false;
	}

	bool TryReadNamedNumericValue(UStruct* Struct, const void* ContainerPtr, const TArray<FString>& CandidateNames, double& OutValue, FName* OutPropertyName = nullptr)
	{
		const FProperty* Property = FindBestNamedProperty(
			Struct,
			CandidateNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FNumericProperty>(CandidateProperty) != nullptr;
			});
		if (!Property || !TryReadNumericPropertyValue(Property, ContainerPtr, OutValue))
		{
			return false;
		}

		if (OutPropertyName)
		{
			*OutPropertyName = Property->GetFName();
		}

		return true;
	}

	bool TryReadNamedTextValue(UStruct* Struct, const void* ContainerPtr, const TArray<FString>& CandidateNames, FText& OutText)
	{
		const FProperty* Property = FindBestNamedProperty(
			Struct,
			CandidateNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FTextProperty>(CandidateProperty) || CastField<const FStrProperty>(CandidateProperty) || CastField<const FNameProperty>(CandidateProperty)
					|| CastField<const FObjectPropertyBase>(CandidateProperty) || CastField<const FClassProperty>(CandidateProperty)
					|| CastField<const FSoftObjectProperty>(CandidateProperty) || CastField<const FSoftClassProperty>(CandidateProperty);
			});
		return Property && TryReadTextPropertyValue(Property, ContainerPtr, OutText);
	}

	bool TryReadBoolNamedValue(UStruct* Struct, const void* ContainerPtr, const TArray<FString>& CandidateNames, bool& OutValue)
	{
		const FProperty* Property = FindBestNamedProperty(
			Struct,
			CandidateNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FBoolProperty>(CandidateProperty) != nullptr;
			});
		const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property);
		if (!BoolProperty || !ContainerPtr)
		{
			return false;
		}

		OutValue = BoolProperty->GetPropertyValue_InContainer(ContainerPtr);
		return true;
	}

	bool MatchesAnyHint(const FString& Value, const TArray<FString>& Hints)
	{
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && Value.Contains(Hint, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	UClass* ResolveClassByPath(const TCHAR* ClassPath)
	{
		if (ClassPath == nullptr || *ClassPath == TEXT('\0'))
		{
			return nullptr;
		}

		if (UClass* ExistingClass = FindObject<UClass>(nullptr, ClassPath))
		{
			return ExistingClass;
		}

		return LoadObject<UClass>(nullptr, ClassPath);
	}

	bool IsArmorSlotComponent(const UActorComponent* Component)
	{
		if (!IsValid(Component))
		{
			return false;
		}

		if (UClass* ArmorSlotClass = ResolveClassByPath(ACFUArmorSlotComponentClassPath))
		{
			return Component->IsA(ArmorSlotClass);
		}

		return Component->GetClass()->GetName().Contains(TEXT("ACFArmorSlotComponent"));
	}

	bool IsPropertyEmpty(const FProperty* Property, const void* ContainerPtr)
	{
		if (!Property || !ContainerPtr)
		{
			return true;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (!ValuePtr)
		{
			return true;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
		{
			return ObjectProperty->GetObjectPropertyValue(ValuePtr) == nullptr;
		}

		if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(Property))
		{
			return ClassProperty->GetPropertyValue(ValuePtr) == nullptr;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<const FSoftObjectProperty>(Property))
		{
			return SoftObjectProperty->GetPropertyValue(ValuePtr).IsNull();
		}

		if (const FSoftClassProperty* SoftClassProperty = CastField<const FSoftClassProperty>(Property))
		{
			return SoftClassProperty->GetPropertyValue(ValuePtr).IsNull();
		}

		if (const FNameProperty* NameProperty = CastField<const FNameProperty>(Property))
		{
			return NameProperty->GetPropertyValue(ValuePtr).IsNone();
		}

		if (const FStrProperty* StringProperty = CastField<const FStrProperty>(Property))
		{
			return StringProperty->GetPropertyValue(ValuePtr).TrimStartAndEnd().IsEmpty();
		}

		return false;
	}

	bool IsProtectedInventoryStruct(UStruct* Struct, const void* ContainerPtr, const UProjectDefeatFlowSettings* Settings, const FString& SearchString)
	{
		if (MatchesAnyHint(SearchString, Settings ? Settings->NonRemovableNameHints : TArray<FString>()))
		{
			return true;
		}

		bool bFlagValue = false;
		if (TryReadBoolNamedValue(Struct, ContainerPtr, { TEXT("Quest"), TEXT("IsQuestItem"), TEXT("bQuestItem"), TEXT("Locked"), TEXT("bLocked"), TEXT("NonRemovable"), TEXT("bNonRemovable"), TEXT("NoDrop"), TEXT("bNoDrop") }, bFlagValue) && bFlagValue)
		{
			return true;
		}

		if (TryReadBoolNamedValue(Struct, ContainerPtr, { TEXT("CanDrop"), TEXT("CanRemove"), TEXT("Removable") }, bFlagValue) && !bFlagValue)
		{
			return true;
		}

		return false;
	}

	bool CaptureInventoryEntry(
		const FArrayProperty* ArrayProperty,
		const void* ElementPtr,
		const int32 ArrayIndex,
		UObject* Component,
		const UProjectDefeatFlowSettings* Settings,
		FProjectDefeatInventoryEntry& OutEntry)
	{
		if (!ArrayProperty || !ElementPtr || !Component)
		{
			return false;
		}

		OutEntry.ComponentName = Component->GetName();
		OutEntry.ComponentClassName = Component->GetClass()->GetName();
		OutEntry.ContainerPropertyName = ArrayProperty->GetFName();
		OutEntry.ArrayIndex = ArrayIndex;
		OutEntry.bFromEquipment = false;
		OutEntry.bEligibleForRandomLoss = true;

		ArrayProperty->Inner->ExportTextItem_Direct(OutEntry.ExportText, ElementPtr, nullptr, Component, PPF_None);

		if (const FStructProperty* StructProperty = CastField<const FStructProperty>(ArrayProperty->Inner))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			double CountValue = 1.0;
			TryReadNamedNumericValue(const_cast<UScriptStruct*>(Struct), ElementPtr, { TEXT("Count"), TEXT("Amount"), TEXT("Quantity"), TEXT("Num") }, CountValue, &OutEntry.CountPropertyName);
			OutEntry.Count = FMath::Max(FMath::RoundToInt(static_cast<float>(CountValue)), 1);
			OutEntry.bStackable = OutEntry.Count > 1;

			FText DisplayName;
			TryReadNamedTextValue(const_cast<UScriptStruct*>(Struct), ElementPtr, { TEXT("DisplayName"), TEXT("ItemName"), TEXT("Name"), TEXT("Label"), TEXT("Title") }, DisplayName);
			FText KeyText;
			TryReadNamedTextValue(const_cast<UScriptStruct*>(Struct), ElementPtr, { TEXT("Guid"), TEXT("ItemGuid"), TEXT("UniqueID"), TEXT("UniqueId"), TEXT("ItemID"), TEXT("ItemId"), TEXT("ID"), TEXT("Id"), TEXT("Name") }, KeyText);

			if (IsEmptyText(DisplayName))
			{
				DisplayName = MakePrettyText(Struct->GetName());
			}

			OutEntry.DisplayName = DisplayName;
			OutEntry.EntryKey = IsEmptyText(KeyText) ? DisplayName.ToString() : KeyText.ToString();
			OutEntry.bEligibleForRandomLoss = !IsProtectedInventoryStruct(
				const_cast<UScriptStruct*>(Struct),
				ElementPtr,
				Settings,
				OutEntry.EntryKey + TEXT(" ") + OutEntry.DisplayName.ToString());
			return !OutEntry.EntryKey.IsEmpty();
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(ArrayProperty->Inner))
		{
			if (const UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ElementPtr))
			{
				OutEntry.EntryKey = ObjectValue->GetPathName();
				TryReadDisplayTextFromObject(ObjectValue, OutEntry.DisplayName);
				if (IsEmptyText(OutEntry.DisplayName))
				{
					OutEntry.DisplayName = MakePrettyText(ObjectValue->GetName());
				}

				OutEntry.bEligibleForRandomLoss = !MatchesAnyHint(OutEntry.EntryKey + TEXT(" ") + OutEntry.DisplayName.ToString(), Settings ? Settings->NonRemovableNameHints : TArray<FString>());
				return true;
			}
		}

		if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(ArrayProperty->Inner))
		{
			if (const UClass* ClassValue = Cast<UClass>(ClassProperty->GetPropertyValue(ElementPtr).Get()))
			{
				OutEntry.EntryKey = ClassValue->GetPathName();
				OutEntry.DisplayName = MakePrettyText(ClassValue->GetName());
				OutEntry.bEligibleForRandomLoss = !MatchesAnyHint(OutEntry.EntryKey + TEXT(" ") + OutEntry.DisplayName.ToString(), Settings ? Settings->NonRemovableNameHints : TArray<FString>());
				return true;
			}
		}

		return false;
	}

	bool CaptureEquipmentEntry(
		UActorComponent* Component,
		const FProperty* Property,
		const UProjectDefeatFlowSettings* Settings,
		FProjectDefeatInventoryEntry& OutEntry)
	{
		if (!Component || !Property || IsPropertyEmpty(Property, Component))
		{
			return false;
		}

		OutEntry.ComponentName = Component->GetName();
		OutEntry.ComponentClassName = Component->GetClass()->GetName();
		OutEntry.ContainerPropertyName = Property->GetFName();
		OutEntry.ArrayIndex = INDEX_NONE;
		OutEntry.Count = 1;
		OutEntry.bFromEquipment = true;
		OutEntry.bStackable = false;

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Component);
		if (!ValuePtr)
		{
			return false;
		}

		Property->ExportTextItem_Direct(OutEntry.ExportText, ValuePtr, nullptr, Component, PPF_None);
		TryReadTextPropertyValue(Property, Component, OutEntry.DisplayName);
		if (IsEmptyText(OutEntry.DisplayName))
		{
			OutEntry.DisplayName = MakePrettyText(Property->GetName());
		}

		OutEntry.EntryKey = OutEntry.ComponentName + TEXT(".") + Property->GetName() + TEXT(".") + OutEntry.DisplayName.ToString();
		OutEntry.bEligibleForRandomLoss = !MatchesAnyHint(OutEntry.EntryKey + TEXT(" ") + OutEntry.DisplayName.ToString(), Settings ? Settings->NonRemovableNameHints : TArray<FString>());
		return true;
	}

	UObject* ResolveComponentBySnapshot(AActor* OwnerActor, const FString& ComponentName, const FString& ComponentClassName, const bool bPreferInventoryHints)
	{
		if (!OwnerActor)
		{
			return nullptr;
		}

		TInlineComponentArray<UActorComponent*> Components(OwnerActor);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (!ComponentName.IsEmpty() && Component->GetName() == ComponentName)
			{
				return Component;
			}
		}

		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (!ComponentClassName.IsEmpty() && Component->GetClass()->GetName() == ComponentClassName)
			{
				return Component;
			}
		}

		if (bPreferInventoryHints)
		{
			return FindComponentByClassHint<UActorComponent>(OwnerActor, { TEXT("ACFInventoryComponent"), TEXT("ACFStorageComponent") });
		}

		return nullptr;
	}

	FArrayProperty* ResolveMutableArrayProperty(UObject* Component, const FName PropertyName)
	{
		if (!Component || PropertyName.IsNone())
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Component->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->GetFName() == PropertyName)
			{
				return CastField<FArrayProperty>(Property);
			}
		}

		return nullptr;
	}

	FProperty* ResolveMutableProperty(UObject* Component, const FName PropertyName)
	{
		if (!Component || PropertyName.IsNone())
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Component->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->GetFName() == PropertyName)
			{
				return Property;
			}
		}

		return nullptr;
	}

	FProperty* ResolveStructProperty(UStruct* Struct, const FName PropertyName)
	{
		if (!Struct || PropertyName.IsNone())
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->GetFName() == PropertyName)
			{
				return Property;
			}
		}

		return nullptr;
	}

	void EnumerateEquipmentEntries(AActor* OwnerActor, const UProjectDefeatFlowSettings* Settings, TArray<FProjectDefeatInventoryEntry>& OutEntries)
	{
		OutEntries.Reset();
		if (!OwnerActor)
		{
			return;
		}

		TInlineComponentArray<UActorComponent*> Components(OwnerActor);
		for (UActorComponent* Component : Components)
		{
			if (!IsArmorSlotComponent(Component))
			{
				continue;
			}

			for (TFieldIterator<FProperty> PropertyIt(Component->GetClass(), EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				const FProperty* Property = *PropertyIt;
				if (!Property)
				{
					continue;
				}

				const FString LowerName = Property->GetName().ToLower();
				const bool bLikelyItemProperty = LowerName.Contains(TEXT("armor"))
					|| LowerName.Contains(TEXT("item"))
					|| LowerName.Contains(TEXT("equip"))
					|| LowerName.Contains(TEXT("cloth"))
					|| LowerName.Contains(TEXT("wear"))
					|| LowerName.Contains(TEXT("skinned"));
				if (!bLikelyItemProperty)
				{
					continue;
				}

				if (!CastField<const FObjectPropertyBase>(Property)
					&& !CastField<const FClassProperty>(Property)
					&& !CastField<const FSoftObjectProperty>(Property)
					&& !CastField<const FSoftClassProperty>(Property)
					&& !CastField<const FNameProperty>(Property)
					&& !CastField<const FStrProperty>(Property))
				{
					continue;
				}

				FProjectDefeatInventoryEntry Entry;
				if (CaptureEquipmentEntry(Component, Property, Settings, Entry))
				{
					OutEntries.Add(MoveTemp(Entry));
				}
			}
		}
	}

	bool ResolveInventoryComponentAndArray(AActor* OwnerActor, UObject*& OutComponent, FArrayProperty*& OutArrayProperty)
	{
		OutComponent = FindComponentByClassHint<UActorComponent>(OwnerActor, { TEXT("ACFInventoryComponent"), TEXT("ACFStorageComponent") });
		if (!OutComponent)
		{
			return false;
		}

		const FProperty* Property = FindBestNamedProperty(
			OutComponent->GetClass(),
			{ TEXT("Inventory"), TEXT("Items"), TEXT("InventoryItems"), TEXT("StoredItems") },
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FArrayProperty>(CandidateProperty) != nullptr;
			});
		OutArrayProperty = CastField<FArrayProperty>(const_cast<FProperty*>(Property));
		return OutArrayProperty != nullptr;
	}

	bool TryDecrementArrayEntryCount(const FArrayProperty* ArrayProperty, void* EntryPtr, const FName CountPropertyName)
	{
		if (!ArrayProperty || !EntryPtr || CountPropertyName.IsNone())
		{
			return false;
		}

		const FStructProperty* StructProperty = CastField<const FStructProperty>(ArrayProperty->Inner);
		if (!StructProperty || !StructProperty->Struct)
		{
			return false;
		}

		FProperty* CountProperty = ResolveStructProperty(StructProperty->Struct, CountPropertyName);
		double CurrentValue = 0.0;
		if (!CountProperty || !TryReadNumericPropertyValue(CountProperty, EntryPtr, CurrentValue))
		{
			return false;
		}

		return TryWriteNumericPropertyValue(CountProperty, EntryPtr, FMath::Max(CurrentValue - 1.0, 1.0));
	}

	void ClearEquipmentCandidateProperties(AActor* OwnerActor)
	{
		if (!OwnerActor)
		{
			return;
		}

		TInlineComponentArray<UActorComponent*> Components(OwnerActor);
		for (UActorComponent* Component : Components)
		{
			if (!IsArmorSlotComponent(Component))
			{
				continue;
			}

			for (TFieldIterator<FProperty> PropertyIt(Component->GetClass(), EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (!Property)
				{
					continue;
				}

				const FString LowerName = Property->GetName().ToLower();
				if (!LowerName.Contains(TEXT("armor"))
					&& !LowerName.Contains(TEXT("item"))
					&& !LowerName.Contains(TEXT("equip"))
					&& !LowerName.Contains(TEXT("cloth"))
					&& !LowerName.Contains(TEXT("wear"))
					&& !LowerName.Contains(TEXT("skinned")))
				{
					continue;
				}

				Property->ClearValue_InContainer(Component);
			}
		}
	}
}

bool FProjectDefeatInventoryBridge::CaptureSnapshot(AActor* OwnerActor, const UProjectDefeatFlowSettings* Settings, FProjectDefeatInventorySnapshot& OutSnapshot)
{
	OutSnapshot = FProjectDefeatInventorySnapshot();
	if (!OwnerActor)
	{
		return false;
	}

	UObject* InventoryComponent = nullptr;
	FArrayProperty* InventoryArrayProperty = nullptr;
	if (ProjectDefeatInventoryBridgePrivate::ResolveInventoryComponentAndArray(OwnerActor, InventoryComponent, InventoryArrayProperty))
	{
		OutSnapshot.InventoryComponentName = InventoryComponent->GetName();
		OutSnapshot.InventoryComponentClassName = InventoryComponent->GetClass()->GetName();
		OutSnapshot.InventoryPropertyName = InventoryArrayProperty->GetFName();

		void* ArrayPtr = InventoryArrayProperty->ContainerPtrToValuePtr<void>(InventoryComponent);
		FScriptArrayHelper ArrayHelper(InventoryArrayProperty, ArrayPtr);
		for (int32 ArrayIndex = 0; ArrayIndex < ArrayHelper.Num(); ++ArrayIndex)
		{
			FProjectDefeatInventoryEntry Entry;
			if (ProjectDefeatInventoryBridgePrivate::CaptureInventoryEntry(
				InventoryArrayProperty,
				ArrayHelper.GetRawPtr(ArrayIndex),
				ArrayIndex,
				InventoryComponent,
				Settings,
				Entry))
			{
				OutSnapshot.InventoryEntries.Add(MoveTemp(Entry));
			}
		}
	}

	ProjectDefeatInventoryBridgePrivate::EnumerateEquipmentEntries(OwnerActor, Settings, OutSnapshot.EquipmentEntries);
	return OutSnapshot.HasAnyEntries();
}

bool FProjectDefeatInventoryBridge::ApplyModerateLoss(AActor* OwnerActor, const UProjectDefeatFlowSettings* Settings, const int32 Seed, FProjectDefeatInventoryPenaltyResult& OutResult)
{
	OutResult = FProjectDefeatInventoryPenaltyResult();

	FProjectDefeatInventorySnapshot Snapshot;
	if (!CaptureSnapshot(OwnerActor, Settings, Snapshot))
	{
		return false;
	}

	struct FSelectionRef
	{
		bool bEquipment = false;
		int32 Index = INDEX_NONE;
	};

	TArray<FSelectionRef> EligibleSelections;
	for (int32 Index = 0; Index < Snapshot.InventoryEntries.Num(); ++Index)
	{
		if (Snapshot.InventoryEntries[Index].bEligibleForRandomLoss)
		{
			EligibleSelections.Add({ false, Index });
		}
	}
	for (int32 Index = 0; Index < Snapshot.EquipmentEntries.Num(); ++Index)
	{
		if (Snapshot.EquipmentEntries[Index].bEligibleForRandomLoss)
		{
			EligibleSelections.Add({ true, Index });
		}
	}

	if (EligibleSelections.Num() == 0)
	{
		return false;
	}

	const int32 DesiredRemovalCount = FMath::Min(
		Settings ? Settings->ModerateLossMaxEntries : 4,
		FMath::Max(
			Settings ? Settings->ModerateLossMinEntries : 2,
			FMath::CeilToInt(static_cast<float>(EligibleSelections.Num()) * (Settings ? Settings->ModerateLossRatio : 0.20f))));
	const int32 RemovalCount = FMath::Clamp(DesiredRemovalCount, 0, EligibleSelections.Num());
	if (RemovalCount <= 0)
	{
		return false;
	}

	FRandomStream RandomStream(Seed == 0 ? 17731 : Seed);
	for (int32 Index = EligibleSelections.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = RandomStream.RandRange(0, Index);
		EligibleSelections.Swap(Index, SwapIndex);
	}

	TMultiMap<FString, FProjectDefeatInventoryEntry> SelectedInventoryEntriesByContainer;
	TArray<FProjectDefeatInventoryEntry> SelectedEquipmentEntries;

	for (int32 SelectionIndex = 0; SelectionIndex < RemovalCount; ++SelectionIndex)
	{
		const FSelectionRef& Selection = EligibleSelections[SelectionIndex];
		if (Selection.bEquipment)
		{
			if (Snapshot.EquipmentEntries.IsValidIndex(Selection.Index))
			{
				SelectedEquipmentEntries.Add(Snapshot.EquipmentEntries[Selection.Index]);
				OutResult.PenalizedDisplayNames.Add(Snapshot.EquipmentEntries[Selection.Index].DisplayName);
			}
			continue;
		}

		if (Snapshot.InventoryEntries.IsValidIndex(Selection.Index))
		{
			const FProjectDefeatInventoryEntry& Entry = Snapshot.InventoryEntries[Selection.Index];
			SelectedInventoryEntriesByContainer.Add(Entry.ComponentName + TEXT("|") + Entry.ContainerPropertyName.ToString(), Entry);
			OutResult.PenalizedDisplayNames.Add(Entry.DisplayName);
		}
	}

	if (OwnerActor)
	{
		for (TPair<FString, FProjectDefeatInventoryEntry> Pair : SelectedInventoryEntriesByContainer)
		{
			(void)Pair;
		}
	}

	TSet<FString> ContainerKeySet;
	TArray<FString> ContainerKeys;
	SelectedInventoryEntriesByContainer.GenerateKeyArray(ContainerKeys);
	for (const FString& Key : ContainerKeys)
	{
		ContainerKeySet.Add(Key);
	}

	ContainerKeys = ContainerKeySet.Array();
	ContainerKeys.Sort();

	for (const FString& ContainerKey : ContainerKeys)
	{
		TArray<FProjectDefeatInventoryEntry> EntriesForContainer;
		SelectedInventoryEntriesByContainer.MultiFind(ContainerKey, EntriesForContainer);
		if (EntriesForContainer.Num() == 0)
		{
			continue;
		}

		UObject* ContainerComponent = ProjectDefeatInventoryBridgePrivate::ResolveComponentBySnapshot(
			OwnerActor,
			EntriesForContainer[0].ComponentName,
			EntriesForContainer[0].ComponentClassName,
			true);
		FArrayProperty* ArrayProperty = ProjectDefeatInventoryBridgePrivate::ResolveMutableArrayProperty(ContainerComponent, EntriesForContainer[0].ContainerPropertyName);
		if (!ContainerComponent || !ArrayProperty)
		{
			continue;
		}

		void* ArrayPtr = ArrayProperty->ContainerPtrToValuePtr<void>(ContainerComponent);
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayPtr);

		for (const FProjectDefeatInventoryEntry& Entry : EntriesForContainer)
		{
			if (Entry.ArrayIndex == INDEX_NONE || !ArrayHelper.IsValidIndex(Entry.ArrayIndex) || !Entry.bStackable || Entry.Count <= 1)
			{
				continue;
			}

			void* EntryPtr = ArrayHelper.GetRawPtr(Entry.ArrayIndex);
			ProjectDefeatInventoryBridgePrivate::TryDecrementArrayEntryCount(ArrayProperty, EntryPtr, Entry.CountPropertyName);
		}

		EntriesForContainer.Sort([](const FProjectDefeatInventoryEntry& Left, const FProjectDefeatInventoryEntry& Right)
		{
			return Left.ArrayIndex > Right.ArrayIndex;
		});

		for (const FProjectDefeatInventoryEntry& Entry : EntriesForContainer)
		{
			if (Entry.ArrayIndex == INDEX_NONE || !ArrayHelper.IsValidIndex(Entry.ArrayIndex))
			{
				continue;
			}

			if (Entry.bStackable && Entry.Count > 1)
			{
				continue;
			}

			ArrayHelper.RemoveValues(Entry.ArrayIndex, 1);
		}
	}

	for (const FProjectDefeatInventoryEntry& Entry : SelectedEquipmentEntries)
	{
		UObject* Component = ProjectDefeatInventoryBridgePrivate::ResolveComponentBySnapshot(
			OwnerActor,
			Entry.ComponentName,
			Entry.ComponentClassName,
			false);
		FProperty* Property = ProjectDefeatInventoryBridgePrivate::ResolveMutableProperty(Component, Entry.ContainerPropertyName);
		if (Component && Property)
		{
			Property->ClearValue_InContainer(Component);
		}
	}

	OutResult.PenalizedEntryCount = OutResult.PenalizedDisplayNames.Num();
	return OutResult.PenalizedEntryCount > 0;
}

void FProjectDefeatInventoryBridge::BuildRetainedSubset(
	const FProjectDefeatInventorySnapshot& SourceSnapshot,
	const UProjectDefeatFlowSettings* Settings,
	const int32 Seed,
	FProjectDefeatInventorySnapshot& OutRetainedSnapshot)
{
	OutRetainedSnapshot = FProjectDefeatInventorySnapshot();
	OutRetainedSnapshot.InventoryComponentName = SourceSnapshot.InventoryComponentName;
	OutRetainedSnapshot.InventoryComponentClassName = SourceSnapshot.InventoryComponentClassName;
	OutRetainedSnapshot.InventoryPropertyName = SourceSnapshot.InventoryPropertyName;

	struct FSelectionRef
	{
		bool bEquipment = false;
		int32 Index = INDEX_NONE;
	};

	TArray<FSelectionRef> RemovableSelections;
	for (int32 Index = 0; Index < SourceSnapshot.InventoryEntries.Num(); ++Index)
	{
		const FProjectDefeatInventoryEntry& Entry = SourceSnapshot.InventoryEntries[Index];
		if (Entry.bEligibleForRandomLoss)
		{
			RemovableSelections.Add({ false, Index });
		}
		else
		{
			OutRetainedSnapshot.InventoryEntries.Add(Entry);
		}
	}

	for (int32 Index = 0; Index < SourceSnapshot.EquipmentEntries.Num(); ++Index)
	{
		const FProjectDefeatInventoryEntry& Entry = SourceSnapshot.EquipmentEntries[Index];
		if (Entry.bEligibleForRandomLoss)
		{
			RemovableSelections.Add({ true, Index });
		}
		else
		{
			OutRetainedSnapshot.EquipmentEntries.Add(Entry);
		}
	}

	if (RemovableSelections.Num() <= 0)
	{
		return;
	}

	const int32 DesiredRetainedCount = FMath::Min(
		Settings ? Settings->DefeatedRetainedMaxEntries : 2,
		FMath::Max(
			Settings ? Settings->DefeatedRetainedMinEntries : 1,
			FMath::CeilToInt(static_cast<float>(RemovableSelections.Num()) * (Settings ? Settings->DefeatedRetainedRatio : 0.10f))));
	const int32 RetainedCount = FMath::Clamp(DesiredRetainedCount, 0, RemovableSelections.Num());
	if (RetainedCount <= 0)
	{
		return;
	}

	FRandomStream RandomStream(Seed == 0 ? 98317 : Seed);
	for (int32 Index = RemovableSelections.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = RandomStream.RandRange(0, Index);
		RemovableSelections.Swap(Index, SwapIndex);
	}

	for (int32 SelectionIndex = 0; SelectionIndex < RetainedCount; ++SelectionIndex)
	{
		const FSelectionRef& Selection = RemovableSelections[SelectionIndex];
		if (Selection.bEquipment)
		{
			if (SourceSnapshot.EquipmentEntries.IsValidIndex(Selection.Index))
			{
				OutRetainedSnapshot.EquipmentEntries.Add(SourceSnapshot.EquipmentEntries[Selection.Index]);
			}
		}
		else if (SourceSnapshot.InventoryEntries.IsValidIndex(Selection.Index))
		{
			OutRetainedSnapshot.InventoryEntries.Add(SourceSnapshot.InventoryEntries[Selection.Index]);
		}
	}
}

void FProjectDefeatInventoryBridge::BuildDefeatedRetainedSnapshotForCunning(
	const FProjectDefeatInventorySnapshot& SourceSnapshot,
	const UProjectDefeatFlowSettings* Settings,
	const int32 Seed,
	const int32 CunningLevel,
	const int32 FullRetentionLevel,
	FProjectDefeatInventorySnapshot& OutRetainedSnapshot)
{
	if (FMath::Max(CunningLevel, 0) >= FMath::Max(FullRetentionLevel, 0))
	{
		OutRetainedSnapshot = SourceSnapshot;
		return;
	}

	BuildRetainedSubset(SourceSnapshot, Settings, Seed, OutRetainedSnapshot);
	OutRetainedSnapshot.EquipmentEntries.Reset();
}

bool FProjectDefeatInventoryBridge::ApplyRetainedSubset(AActor* OwnerActor, const FProjectDefeatInventorySnapshot& RetainedSnapshot, const UProjectDefeatFlowSettings* Settings)
{
	(void)Settings;

	if (!OwnerActor)
	{
		return false;
	}

	bool bAppliedAnyChange = false;

	UObject* InventoryComponent = ProjectDefeatInventoryBridgePrivate::ResolveComponentBySnapshot(
		OwnerActor,
		RetainedSnapshot.InventoryComponentName,
		RetainedSnapshot.InventoryComponentClassName,
		true);
	FArrayProperty* ArrayProperty = ProjectDefeatInventoryBridgePrivate::ResolveMutableArrayProperty(InventoryComponent, RetainedSnapshot.InventoryPropertyName);
	if (InventoryComponent && ArrayProperty)
	{
		void* ArrayPtr = ArrayProperty->ContainerPtrToValuePtr<void>(InventoryComponent);
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayPtr);
		ArrayHelper.Resize(0);

		for (const FProjectDefeatInventoryEntry& Entry : RetainedSnapshot.InventoryEntries)
		{
			const int32 NewIndex = ArrayHelper.AddValue();
			void* NewEntryPtr = ArrayHelper.GetRawPtr(NewIndex);
			ArrayProperty->Inner->ImportText_Direct(*Entry.ExportText, NewEntryPtr, InventoryComponent, PPF_None);
		}

		bAppliedAnyChange = true;
	}

	ProjectDefeatInventoryBridgePrivate::ClearEquipmentCandidateProperties(OwnerActor);
	for (const FProjectDefeatInventoryEntry& Entry : RetainedSnapshot.EquipmentEntries)
	{
		UObject* Component = ProjectDefeatInventoryBridgePrivate::ResolveComponentBySnapshot(
			OwnerActor,
			Entry.ComponentName,
			Entry.ComponentClassName,
			false);
		FProperty* Property = ProjectDefeatInventoryBridgePrivate::ResolveMutableProperty(Component, Entry.ContainerPropertyName);
		if (!Component || !Property)
		{
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Component);
		if (!ValuePtr)
		{
			continue;
		}

		Property->ImportText_Direct(*Entry.ExportText, ValuePtr, Component, PPF_None);
		bAppliedAnyChange = true;
	}

	return bAppliedAnyChange;
}
