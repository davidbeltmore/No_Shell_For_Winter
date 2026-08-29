#include "UI/ProjectActivityFeedSubsystem.h"

#include "EFProjectUISettings.h"
#include "EFProjectInputSettings.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyTargetInfoComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Combat/ProjectCombatTypes.h"
#include "Dialogue/ProjectEnemyDialogueResolver.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "UI/ProjectActivityFeedSettings.h"
#include "UI/ProjectActivityFeedWidget.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ProjectActivityFeedSubsystem"

DEFINE_LOG_CATEGORY_STATIC(LogProjectActivityFeed, Log, All);

namespace ProjectActivityFeedPrivate
{
	constexpr double EnemyAwarenessPollIntervalSeconds = 0.10;
	constexpr double EnemyMaintenanceIntervalSeconds = 0.25;
	constexpr int32 ActivityFeedInputPriority = 75;

	FProjectActivityFeedEntry MakeStandardEntry(const EProjectActivityFeedChannel Channel, const FText& Message)
	{
		FProjectActivityFeedEntry Entry;
		Entry.Channel = Channel;
		Entry.Message = Message;
		Entry.RenderStyle = EProjectActivityFeedRenderStyle::Standard;
		return Entry;
	}

	FProjectActivityFeedEntry MakeGainEntry(
		const EProjectActivityFeedChannel Channel,
		const FText& Message,
		const FString& BadgeLabel,
		const FText& PrimaryText,
		const FText& SecondaryText)
	{
		FProjectActivityFeedEntry Entry = MakeStandardEntry(Channel, Message);
		Entry.RenderStyle = EProjectActivityFeedRenderStyle::Gain;
		Entry.BadgeLabelOverride = BadgeLabel;
		Entry.PrimaryText = PrimaryText;
		Entry.SecondaryText = SecondaryText;
		return Entry;
	}

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
				if (ClassName.Contains(ClassHint, ESearchCase::IgnoreCase))
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

		int32 SeparatorIndex = INDEX_NONE;
		while (Name.FindLastChar(TEXT('_'), SeparatorIndex))
		{
			bool bNumericSuffix = SeparatorIndex >= 0 && SeparatorIndex + 1 < Name.Len();
			for (int32 Index = SeparatorIndex + 1; bNumericSuffix && Index < Name.Len(); ++Index)
			{
				bNumericSuffix = FChar::IsDigit(Name[Index]);
			}

			if (!bNumericSuffix)
			{
				break;
			}

			Name.LeftInline(SeparatorIndex, EAllowShrinking::No);
		}

		Name.RemoveFromEnd(TEXT("_C"));
		if (Name.StartsWith(TEXT("BP_")))
		{
			Name.RightChopInline(3, EAllowShrinking::No);
		}

		Name.ReplaceInline(TEXT("_"), TEXT(" "));

		FString Humanized;
		Humanized.Reserve(Name.Len() + 8);
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

	bool TryReadDisplayTextFromObject(const UObject* Object, FText& OutText);

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

		return false;
	}

	const FProperty* FindBestNamedProperty(UStruct* Struct, const TArray<FString>& CandidateNames, TFunctionRef<bool(const FProperty*)> Predicate)
	{
		if (!Struct)
		{
			return nullptr;
		}

		for (const FString& CandidateName : CandidateNames)
		{
			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
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
			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
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

		if (const FProperty* DisplayProperty = FindBestNamedProperty(
				Object->GetClass(),
				DisplayPropertyCandidates,
				[](const FProperty* Property)
				{
					return CastField<const FTextProperty>(Property) || CastField<const FStrProperty>(Property) || CastField<const FNameProperty>(Property)
						|| CastField<const FObjectPropertyBase>(Property) || CastField<const FClassProperty>(Property);
				}))
		{
			if (TryReadTextPropertyValue(DisplayProperty, Object, OutText) && !IsEmptyText(OutText))
			{
				return true;
			}
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

	bool TryReadNamedTextValue(UStruct* Struct, const void* ContainerPtr, const TArray<FString>& CandidateNames, FText& OutText)
	{
		const FProperty* Property = FindBestNamedProperty(
			Struct,
			CandidateNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FTextProperty>(CandidateProperty) || CastField<const FStrProperty>(CandidateProperty) || CastField<const FNameProperty>(CandidateProperty)
					|| CastField<const FObjectPropertyBase>(CandidateProperty) || CastField<const FClassProperty>(CandidateProperty);
			});

		return Property && TryReadTextPropertyValue(Property, ContainerPtr, OutText);
	}

	bool TryReadNamedNumericValue(UStruct* Struct, const void* ContainerPtr, const TArray<FString>& CandidateNames, double& OutValue)
	{
		const FProperty* Property = FindBestNamedProperty(
			Struct,
			CandidateNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FNumericProperty>(CandidateProperty) != nullptr;
			});

		return Property && TryReadNumericPropertyValue(Property, ContainerPtr, OutValue);
	}

	FProperty* FindReturnProperty(UFunction* Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property && Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return Property;
			}
		}

		return nullptr;
	}

	int32 CountInputProperties(UFunction* Function)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			const FProperty* Property = *It;
			if (Property && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				++Count;
			}
		}

		return Count;
	}

	bool TryInvokeZeroArgNumericFunction(UObject* Target, const TArray<FName>& CandidateFunctionNames, double& OutValue)
	{
		if (!Target)
		{
			return false;
		}

		for (const FName CandidateFunctionName : CandidateFunctionNames)
		{
			UFunction* Function = Target->FindFunction(CandidateFunctionName);
			if (!Function || CountInputProperties(Function) != 0)
			{
				continue;
			}

			FProperty* ReturnProperty = FindReturnProperty(Function);
			if (!ReturnProperty || !CastField<FNumericProperty>(ReturnProperty))
			{
				continue;
			}

			void* Parms = Function->ParmsSize > 0 ? FMemory_Alloca(Function->ParmsSize) : nullptr;
			if (Parms)
			{
				FMemory::Memzero(Parms, Function->ParmsSize);
			}

			Target->ProcessEvent(Function, Parms);
			if (TryReadNumericPropertyValue(ReturnProperty, Parms, OutValue))
			{
				return true;
			}
		}

		return false;
	}

	bool TryReadNamedNumericProperty(UObject* Target, const TArray<FString>& CandidatePropertyNames, double& OutValue)
	{
		return Target && TryReadNamedNumericValue(Target->GetClass(), Target, CandidatePropertyNames, OutValue);
	}

	bool TryConvertInventoryElement(const FProperty* ElementProperty, const void* ElementPtr, FProjectActivityFeedObservedInventoryItem& OutItem);

	void MergeInventorySnapshotItem(TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot, const FProjectActivityFeedObservedInventoryItem& Item)
	{
		if (Item.Key.IsEmpty() || Item.Count <= 0)
		{
			return;
		}

		FProjectActivityFeedObservedInventoryItem& ExistingItem = OutSnapshot.FindOrAdd(Item.Key);
		ExistingItem.Key = Item.Key;
		ExistingItem.Count += Item.Count;
		if (IsEmptyText(ExistingItem.DisplayName))
		{
			ExistingItem.DisplayName = Item.DisplayName;
		}
	}

	bool TryConvertInventoryStruct(UScriptStruct* Struct, const void* StructPtr, FProjectActivityFeedObservedInventoryItem& OutItem)
	{
		if (!Struct || !StructPtr)
		{
			return false;
		}

		static const TArray<FString> CountCandidates = {
			TEXT("Count"),
			TEXT("Amount"),
			TEXT("Quantity"),
			TEXT("Num")
		};

		static const TArray<FString> DisplayCandidates = {
			TEXT("DisplayName"),
			TEXT("ItemName"),
			TEXT("Name"),
			TEXT("Label"),
			TEXT("Title")
		};

		static const TArray<FString> KeyCandidates = {
			TEXT("Guid"),
			TEXT("ItemGuid"),
			TEXT("UniqueID"),
			TEXT("UniqueId"),
			TEXT("ItemID"),
			TEXT("ItemId"),
			TEXT("ID"),
			TEXT("Id"),
			TEXT("Name")
		};

		static const TArray<FString> ObjectCandidates = {
			TEXT("Item"),
			TEXT("ItemClass"),
			TEXT("ItemType"),
			TEXT("ItemData"),
			TEXT("ItemInfo"),
			TEXT("ItemObject"),
			TEXT("ItemDefinition")
		};

		double CountValue = 1.0;
		TryReadNamedNumericValue(Struct, StructPtr, CountCandidates, CountValue);

		FText DisplayName;
		TryReadNamedTextValue(Struct, StructPtr, DisplayCandidates, DisplayName);

		FText KeyText;
		TryReadNamedTextValue(Struct, StructPtr, KeyCandidates, KeyText);

		if (const FProperty* ObjectLikeProperty = FindBestNamedProperty(
				Struct,
				ObjectCandidates,
				[](const FProperty* Property)
				{
					return CastField<const FObjectPropertyBase>(Property) || CastField<const FClassProperty>(Property) || CastField<const FTextProperty>(Property)
						|| CastField<const FStrProperty>(Property) || CastField<const FNameProperty>(Property);
				}))
		{
			if (IsEmptyText(DisplayName))
			{
				TryReadTextPropertyValue(ObjectLikeProperty, StructPtr, DisplayName);
			}

			if (IsEmptyText(KeyText))
			{
				TryReadTextPropertyValue(ObjectLikeProperty, StructPtr, KeyText);
			}
		}

		if (IsEmptyText(DisplayName))
		{
			DisplayName = MakePrettyText(Struct->GetName());
		}

		OutItem.Key = IsEmptyText(KeyText) ? DisplayName.ToString() : KeyText.ToString();
		OutItem.DisplayName = DisplayName;
		OutItem.Count = FMath::Max(FMath::RoundToInt(static_cast<float>(CountValue)), 1);
		return !OutItem.Key.IsEmpty();
	}

	bool TryConvertInventoryElement(const FProperty* ElementProperty, const void* ElementPtr, FProjectActivityFeedObservedInventoryItem& OutItem)
	{
		if (!ElementProperty || !ElementPtr)
		{
			return false;
		}

		if (const FStructProperty* StructProperty = CastField<const FStructProperty>(ElementProperty))
		{
			return TryConvertInventoryStruct(StructProperty->Struct, ElementPtr, OutItem);
		}

		if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(ElementProperty))
		{
			if (const UClass* ClassValue = Cast<UClass>(ClassProperty->GetPropertyValue(ElementPtr).Get()))
			{
				OutItem.Key = ClassValue->GetPathName();
				OutItem.DisplayName = MakePrettyText(ClassValue->GetName());
				OutItem.Count = 1;
				return true;
			}

			return false;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(ElementProperty))
		{
			if (const UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ElementPtr))
			{
				OutItem.Key = ObjectValue->GetPathName();
				TryReadDisplayTextFromObject(ObjectValue, OutItem.DisplayName);
				if (IsEmptyText(OutItem.DisplayName))
				{
					OutItem.DisplayName = MakePrettyText(ObjectValue->GetName());
				}

				OutItem.Count = 1;
				return true;
			}

			return false;
		}

		return false;
	}

	bool TryBuildInventorySnapshotFromArrayProperty(
		const FArrayProperty* ArrayProperty,
		const void* ContainerPtr,
		TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot)
	{
		if (!ArrayProperty || !ContainerPtr)
		{
			return false;
		}

		const void* ArrayPtr = ArrayProperty->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (!ArrayPtr)
		{
			return false;
		}

		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayPtr);
		bool bResolvedAnyItem = false;
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			FProjectActivityFeedObservedInventoryItem Item;
			if (TryConvertInventoryElement(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), Item))
			{
				MergeInventorySnapshotItem(OutSnapshot, Item);
				bResolvedAnyItem = true;
			}
		}

		return bResolvedAnyItem || ArrayHelper.Num() == 0;
	}

	bool TryBuildInventorySnapshotFromFunction(
		UObject* Target,
		const TArray<FName>& CandidateFunctionNames,
		TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot)
	{
		if (!Target)
		{
			return false;
		}

		for (const FName CandidateFunctionName : CandidateFunctionNames)
		{
			UFunction* Function = Target->FindFunction(CandidateFunctionName);
			if (!Function || CountInputProperties(Function) != 0)
			{
				continue;
			}

			FArrayProperty* ReturnProperty = CastField<FArrayProperty>(FindReturnProperty(Function));
			if (!ReturnProperty)
			{
				continue;
			}

			void* Parms = Function->ParmsSize > 0 ? FMemory_Alloca(Function->ParmsSize) : nullptr;
			if (Parms)
			{
				FMemory::Memzero(Parms, Function->ParmsSize);
			}

			Target->ProcessEvent(Function, Parms);

			TMap<FString, FProjectActivityFeedObservedInventoryItem> FunctionSnapshot;
			if (TryBuildInventorySnapshotFromArrayProperty(ReturnProperty, Parms, FunctionSnapshot))
			{
				OutSnapshot = MoveTemp(FunctionSnapshot);
				return true;
			}
		}

		return false;
	}

	bool TryBuildInventorySnapshotFromProperty(
		UObject* Target,
		const TArray<FString>& CandidatePropertyNames,
		TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot)
	{
		if (!Target)
		{
			return false;
		}

		const FProperty* Property = FindBestNamedProperty(
			Target->GetClass(),
			CandidatePropertyNames,
			[](const FProperty* CandidateProperty)
			{
				return CastField<const FArrayProperty>(CandidateProperty) != nullptr;
			});

		const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property);
		return ArrayProperty && TryBuildInventorySnapshotFromArrayProperty(ArrayProperty, Target, OutSnapshot);
	}
}

void UProjectActivityFeedSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInputComponent = nullptr;
	TrackedFeedWidget = nullptr;
	TrackedCombatAttributeComponent = nullptr;
	TrackedStatusComponent = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	BoundCombatAttributeComponent = nullptr;
	BoundStatusComponent = nullptr;
	CachedLevelingComponent = nullptr;
	CachedInventoryComponent = nullptr;
	TargetEnemyBaseClasses.Reset();
	TrackedEnemies.Reset();
	StoredEntries.Reset();
	LastInventorySnapshot.Reset();
	PendingLootEntries.Reset();
	bInitialEnemyScanPending = true;
	bExpanded = false;
	ResetReflectionState();
	ResetInventoryState();
	ResetAggregates();
	bEnemyTargetClassesPending = !LoadEnemyTargetClasses();
	bHasPendingEnemyBindings = true;
	NextEnemyMaintenanceTimeSeconds = 0.0;

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	}
}

void UProjectActivityFeedSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	ClearTrackedEnemies();
	UnbindFromTrackedComponents();
	DetachFromTrackedPlayerController(true);
	TargetEnemyBaseClasses.Reset();
	TrackedEnemies.Reset();
	StoredEntries.Reset();
	LastInventorySnapshot.Reset();
	PendingLootEntries.Reset();
	ActorSpawnedHandle.Reset();
	bEnemyTargetClassesPending = false;
	bHasPendingEnemyBindings = false;
	NextEnemyMaintenanceTimeSeconds = 0.0;

	Super::Deinitialize();
}

void UProjectActivityFeedSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();

	if (bInitialEnemyScanPending)
	{
		ProcessExistingEnemies();
		bInitialEnemyScanPending = false;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	if (bHasPendingEnemyBindings)
	{
		RefreshEnemyBindings();
	}

	if (CurrentTimeSeconds >= NextEnemyMaintenanceTimeSeconds)
	{
		NextEnemyMaintenanceTimeSeconds = CurrentTimeSeconds + ProjectActivityFeedPrivate::EnemyMaintenanceIntervalSeconds;
		UnregisterInvalidEnemies();

		if (bEnemyTargetClassesPending && LoadEnemyTargetClasses())
		{
			bEnemyTargetClassesPending = false;
			ProcessExistingEnemies();
		}
	}

	PollLevelingBridge(CurrentTimeSeconds);
	PollInventoryBridge(CurrentTimeSeconds);
	EvaluateEnemyBarks(CurrentTimeSeconds);
	FlushPendingAggregates(CurrentTimeSeconds);
}

TStatId UProjectActivityFeedSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectActivityFeedSubsystem, STATGROUP_Tickables);
}

bool UProjectActivityFeedSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectActivityFeedSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectActivityFeedSubsystem::HasFeedWidget() const
{
	return TrackedFeedWidget != nullptr;
}

bool UProjectActivityFeedSubsystem::IsFeedExpanded() const
{
	return bExpanded;
}

bool UProjectActivityFeedSubsystem::IsFeedHudVisible() const
{
	return TrackedFeedWidget && TrackedFeedWidget->IsHudVisible();
}

int32 UProjectActivityFeedSubsystem::GetStoredEntryCount() const
{
	return StoredEntries.Num();
}

int32 UProjectActivityFeedSubsystem::GetTrackedEnemyCount() const
{
	return TrackedEnemies.Num();
}

FProjectActivityFeedEntry UProjectActivityFeedSubsystem::GetLatestEntry() const
{
	return StoredEntries.IsEmpty() ? FProjectActivityFeedEntry() : StoredEntries.Last();
}

FText UProjectActivityFeedSubsystem::GetLatestEntryMessage() const
{
	return StoredEntries.IsEmpty() ? FText::GetEmpty() : StoredEntries.Last().Message;
}

FString UProjectActivityFeedSubsystem::GetLatestEntryMessageString() const
{
	return StoredEntries.IsEmpty() ? FString() : StoredEntries.Last().Message.ToString();
}

int32 UProjectActivityFeedSubsystem::GetLatestEntryChannelValue() const
{
	return StoredEntries.IsEmpty() ? static_cast<int32>(EProjectActivityFeedChannel::System) : static_cast<int32>(StoredEntries.Last().Channel);
}

FProjectChronicleLayoutPolicy UProjectActivityFeedSubsystem::GetChronicleLayoutPolicy(const bool bForExpanded) const
{
	FProjectChronicleLayoutPolicy Policy;
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	if (!Settings)
	{
		Policy.MinimumRowHeight = bForExpanded ? 32.0f : 22.0f;
		Policy.MaximumTextWidth = bForExpanded ? 450.0f : 390.0f;
		Policy.LineHeightPercentage = bForExpanded ? 1.18f : 1.22f;
		return Policy;
	}

	Policy.MinimumRowHeight = FMath::Max(
		bForExpanded ? Settings->ExpandedRowHeight : Settings->CompactRowHeight,
		bForExpanded ? 18.0f : 12.0f);
	Policy.MaximumTextWidth = FMath::Max(
		bForExpanded ? Settings->ExpandedMaximumTextWidth : Settings->CompactMaximumTextWidth,
		64.0f);
	Policy.RowGap = FMath::Max(Settings->RowGap, 0.0f);
	Policy.InlinePrimaryWidthRatio = FMath::Clamp(Settings->InlinePrimaryWidthRatio, 0.1f, 0.9f);
	Policy.LineHeightPercentage = FMath::Clamp(
		bForExpanded ? Settings->ExpandedLineHeightPercentage : Settings->CompactLineHeightPercentage,
		1.0f,
		2.0f);
	return Policy;
}

void UProjectActivityFeedSubsystem::RequestToggleExpanded()
{
	TryResolveRuntimeContext();
	RefreshFeedWidget();
	HandleToggleExpandedPressed();
}

void UProjectActivityFeedSubsystem::RequestScrollHistory(const int32 Direction)
{
	if (!TrackedFeedWidget || !TrackedFeedWidget->IsHudVisible() || !bExpanded)
	{
		return;
	}

	TrackedFeedWidget->ScrollHistoryByEntries(Direction);
}

void UProjectActivityFeedSubsystem::DebugAddSystemEntry(const FText& Message)
{
	AddFeedEntry(EProjectActivityFeedChannel::System, Message);
}

void UProjectActivityFeedSubsystem::DebugAddEnemyBarkEntry(AActor* EnemyActor, const bool bGroupBark)
{
	if (!EnemyActor)
	{
		return;
	}

	AddFeedEntry(BuildEnemyBarkEntry(EnemyActor, bGroupBark));
}

void UProjectActivityFeedSubsystem::AddSystemEntry(const FText& Message)
{
	AddFeedEntry(EProjectActivityFeedChannel::System, Message);
}

void UProjectActivityFeedSubsystem::AddDialogueEntry(const FText& Message)
{
	FProjectActivityFeedEntry Entry;
	Entry.Channel = EProjectActivityFeedChannel::Dialogue;
	Entry.Message = Message;
	Entry.RenderStyle = EProjectActivityFeedRenderStyle::DialogueQuote;
	Entry.BadgeLabelOverride = TEXT("PARTNER");
	Entry.PrimaryText = Message;
	AddFeedEntry(Entry);
}

void UProjectActivityFeedSubsystem::AddPartnerDialogueEntry(AActor* PartnerActor, const FText& Message)
{
	FText PartnerName;
	if (!TryGetEnemyDisplayName(PartnerActor, PartnerName))
	{
		PartnerName = LOCTEXT("GenericPartnerSpeaker", "Partner");
	}

	FProjectActivityFeedEntry Entry;
	Entry.Channel = EProjectActivityFeedChannel::Dialogue;
	Entry.Message = FText::Format(
		LOCTEXT("PartnerDialogueMessageFormat", "{0} \"{1}\""),
		PartnerName,
		Message);
	Entry.RenderStyle = EProjectActivityFeedRenderStyle::DialogueQuote;
	Entry.BadgeLabelOverride = TEXT("PARTNER");
	Entry.PrimaryText = PartnerName;
	Entry.SecondaryText = FText::Format(LOCTEXT("PartnerDialogueQuoteFormat", "\"{0}\""), Message);
	AddFeedEntry(Entry);
}

void UProjectActivityFeedSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	HandleTrackedPawnUpdated(NewPawn);
}

void UProjectActivityFeedSubsystem::HandlePlayerAttributeChanged(const FName AttributeName, const float OldValue, const float NewValue, const float MaxValue)
{
	if (!TrackedCombatAttributeComponent)
	{
		return;
	}

	if (AttributeName != TrackedCombatAttributeComponent->HealthAttributeName)
	{
		return;
	}

	const float DeltaAmount = NewValue - OldValue;
	if (DeltaAmount > 0.0f)
	{
		QueueHeal(DeltaAmount, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	}
}

void UProjectActivityFeedSubsystem::HandleStatusChanged(const FName StatusName, const bool bActive)
{
	const FText StatusText = ProjectActivityFeedPrivate::MakePrettyText(StatusName.ToString());
	AddFeedEntry(
		EProjectActivityFeedChannel::Status,
		bActive
			? FText::Format(LOCTEXT("StatusAppliedFormat", "Afflicted: {0}"), StatusText)
			: FText::Format(LOCTEXT("StatusClearedFormat", "Recovered from {0}"), StatusText));
}

void UProjectActivityFeedSubsystem::HandleBlackoutChanged(const bool bBlackoutActive)
{
	AddFeedEntry(
		EProjectActivityFeedChannel::Status,
		bBlackoutActive ? LOCTEXT("BlackoutActive", "The world goes black.") : LOCTEXT("BlackoutCleared", "You regain consciousness."));
}

void UProjectActivityFeedSubsystem::HandleEnemyDamageApplied(
	AActor* SourceActor,
	FName DamageType,
	const float RequestedDamage,
	const float AppliedDamage,
	const float RemainingValue,
	const bool bKilledTarget)
{
	if (!bKilledTarget)
	{
		return;
	}

	TryEmitEnemyDefeatFromPendingDamage(SourceActor);
}

bool UProjectActivityFeedSubsystem::LoadEnemyTargetClasses()
{
	TargetEnemyBaseClasses.Reset();

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	if (!Settings)
	{
		return true;
	}

	bool bAllClassesResolved = true;
	for (const TSoftClassPtr<APawn>& EnemyClass : Settings->TargetEnemyBaseClasses)
	{
		if (EnemyClass.IsNull())
		{
			continue;
		}

		if (UClass* ResolvedClass = EnemyClass.Get())
		{
			TargetEnemyBaseClasses.AddUnique(ResolvedClass);
		}
		else
		{
			bAllClassesResolved = false;
		}
	}

	return bAllClassesResolved;
}

void UProjectActivityFeedSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		if (TrackedPlayerController || TrackedFeedWidget)
		{
			UnbindFromTrackedComponents();
			HandleTrackedPawnUpdated(nullptr);
			DetachFromTrackedPlayerController(true);
		}

		return;
	}

	AttachToPlayerController(PlayerController);
	HandleTrackedPawnUpdated(PlayerController->GetPawn());
	ResolveTrackedPawnDependencies();
	BindToTrackedComponents();
	EnsureFeedWidget(PlayerController);
}

void UProjectActivityFeedSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		return;
	}

	DetachFromTrackedPlayerController(false);

	TrackedPlayerController = PlayerController;
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	BindInputToTrackedPlayerController();
}

void UProjectActivityFeedSubsystem::DetachFromTrackedPlayerController(const bool bRemoveWidget)
{
	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	UnbindInputFromTrackedPlayerController();

	if (bRemoveWidget && TrackedFeedWidget)
	{
		TrackedFeedWidget->RemoveFromParent();
		TrackedFeedWidget = nullptr;
	}

	TrackedPlayerController = nullptr;
}

void UProjectActivityFeedSubsystem::BindInputToTrackedPlayerController()
{
	if (!TrackedPlayerController)
	{
		return;
	}

	if (TrackedInputComponent)
	{
		if (TrackedInputComponent->Priority == ProjectActivityFeedPrivate::ActivityFeedInputPriority)
		{
			return;
		}

		UnbindInputFromTrackedPlayerController();
	}

	TrackedInputComponent = NewObject<UInputComponent>(TrackedPlayerController, TEXT("ProjectActivityFeedInputComponent"));
	if (!TrackedInputComponent)
	{
		UE_LOG(LogProjectActivityFeed, Warning, TEXT("[ProjectActivityFeed] Failed to create input component for %s"), *GetNameSafe(TrackedPlayerController));
		return;
	}

	TrackedInputComponent->bBlockInput = false;
	TrackedInputComponent->Priority = ProjectActivityFeedPrivate::ActivityFeedInputPriority;
	TrackedInputComponent->RegisterComponent();
	const UEFProjectInputSettings* InputSettings = UEFProjectInputSettings::Get();
	FInputKeyBinding& ToggleBinding = TrackedInputComponent->BindKey(
		InputSettings ? InputSettings->ToggleActivityFeedKey : EKeys::J,
		IE_Pressed,
		this,
		&ThisClass::HandleToggleExpandedPressed);
	ToggleBinding.bConsumeInput = true;
	ToggleBinding.bExecuteWhenPaused = true;
	TrackedPlayerController->PushInputComponent(TrackedInputComponent);
}

void UProjectActivityFeedSubsystem::UnbindInputFromTrackedPlayerController()
{
	if (!TrackedInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(TrackedInputComponent);
	}

	if (TrackedInputComponent->IsRegistered())
	{
		TrackedInputComponent->DestroyComponent();
	}

	TrackedInputComponent = nullptr;
}

void UProjectActivityFeedSubsystem::HandleToggleExpandedPressed()
{
	if (!TrackedFeedWidget || !TrackedFeedWidget->IsHudVisible())
	{
		return;
	}

	bExpanded = !bExpanded;
	RefreshFeedWidget();
}

void UProjectActivityFeedSubsystem::HandleTrackedPawnUpdated(APawn* NewPawn)
{
	if (TrackedPlayerPawn == NewPawn)
	{
		return;
	}

	UnbindFromTrackedComponents();
	TrackedPlayerPawn = NewPawn;
	TrackedCombatAttributeComponent = nullptr;
	TrackedStatusComponent = nullptr;
	TrackedInnerDoctrineComponent = nullptr;
	ResetReflectionState();
	ResetInventoryState();
	ResetAggregates();
}

void UProjectActivityFeedSubsystem::ResolveTrackedPawnDependencies()
{
	if (!TrackedPlayerPawn)
	{
		TrackedCombatAttributeComponent = nullptr;
		TrackedStatusComponent = nullptr;
		TrackedInnerDoctrineComponent = nullptr;
		return;
	}

	TrackedCombatAttributeComponent = TrackedPlayerPawn->FindComponentByClass<UProjectCombatAttributeComponent>();
	TrackedStatusComponent = TrackedPlayerPawn->FindComponentByClass<UProjectSurvivalStatusComponent>();
	TrackedInnerDoctrineComponent = TrackedPlayerPawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
}

void UProjectActivityFeedSubsystem::BindToTrackedComponents()
{
	if (BoundCombatAttributeComponent != TrackedCombatAttributeComponent)
	{
		if (BoundCombatAttributeComponent)
		{
			BoundCombatAttributeComponent->OnAttributeChanged.RemoveDynamic(this, &ThisClass::HandlePlayerAttributeChanged);
		}

		BoundCombatAttributeComponent = TrackedCombatAttributeComponent;
		if (BoundCombatAttributeComponent)
		{
			BoundCombatAttributeComponent->OnAttributeChanged.AddUniqueDynamic(this, &ThisClass::HandlePlayerAttributeChanged);
		}
	}

	if (BoundStatusComponent != TrackedStatusComponent)
	{
		if (BoundStatusComponent)
		{
			BoundStatusComponent->OnStatusChanged.RemoveDynamic(this, &ThisClass::HandleStatusChanged);
			BoundStatusComponent->OnBlackoutChanged.RemoveDynamic(this, &ThisClass::HandleBlackoutChanged);
		}

		BoundStatusComponent = TrackedStatusComponent;
		if (BoundStatusComponent)
		{
			BoundStatusComponent->OnStatusChanged.AddUniqueDynamic(this, &ThisClass::HandleStatusChanged);
			BoundStatusComponent->OnBlackoutChanged.AddUniqueDynamic(this, &ThisClass::HandleBlackoutChanged);
		}
	}
}

void UProjectActivityFeedSubsystem::UnbindFromTrackedComponents()
{
	if (BoundCombatAttributeComponent)
	{
		BoundCombatAttributeComponent->OnAttributeChanged.RemoveDynamic(this, &ThisClass::HandlePlayerAttributeChanged);
		BoundCombatAttributeComponent = nullptr;
	}

	if (BoundStatusComponent)
	{
		BoundStatusComponent->OnStatusChanged.RemoveDynamic(this, &ThisClass::HandleStatusChanged);
		BoundStatusComponent->OnBlackoutChanged.RemoveDynamic(this, &ThisClass::HandleBlackoutChanged);
		BoundStatusComponent = nullptr;
	}
}

void UProjectActivityFeedSubsystem::EnsureFeedWidget(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (!TrackedFeedWidget)
	{
		TrackedFeedWidget = CreateWidget<UProjectActivityFeedWidget>(PlayerController, ResolveFeedWidgetClass(), TEXT("ProjectActivityFeedWidget"));
		if (!TrackedFeedWidget)
		{
			UE_LOG(LogProjectActivityFeed, Warning, TEXT("[ProjectActivityFeed] Failed to create ProjectActivityFeedWidget for %s"), *GetNameSafe(PlayerController));
			return;
		}

		if (!TrackedFeedWidget->AddToPlayerScreen(170))
		{
			TrackedFeedWidget->AddToViewport(170);
		}
	}

	RefreshFeedWidget();
}

void UProjectActivityFeedSubsystem::RefreshFeedWidget()
{
	if (!TrackedFeedWidget)
	{
		return;
	}

	bool bHudVisible = false;
	if (UWorld* World = GetWorld())
	{
		if (const UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			bHudVisible = NeedsSubsystem->IsNeedsHudVisible();
		}
		if (!bHudVisible)
		{
			if (const UProjectIntimacySubsystem* IntimacySubsystem = World->GetSubsystem<UProjectIntimacySubsystem>())
			{
				bHudVisible = IntimacySubsystem->IsIntimacySessionActive();
			}
		}
	}

	TrackedFeedWidget->SetChronicleLayoutPolicy(GetChronicleLayoutPolicy(bExpanded));
	TrackedFeedWidget->SetHudVisible(bHudVisible);
	TrackedFeedWidget->SetExpanded(bExpanded);
	TrackedFeedWidget->SetFeedEntries(StoredEntries);
}

void UProjectActivityFeedSubsystem::AddFeedEntry(const EProjectActivityFeedChannel Channel, const FText& Message)
{
	AddFeedEntry(ProjectActivityFeedPrivate::MakeStandardEntry(Channel, Message));
}

void UProjectActivityFeedSubsystem::AddFeedEntry(const FProjectActivityFeedEntry& Entry)
{
	const FText Message = !ProjectActivityFeedPrivate::IsEmptyText(Entry.Message)
		? Entry.Message
		: (!ProjectActivityFeedPrivate::IsEmptyText(Entry.PrimaryText)
			? Entry.PrimaryText
			: Entry.SecondaryText);
	if (ProjectActivityFeedPrivate::IsEmptyText(Message))
	{
		return;
	}

	FProjectActivityFeedEntry& NewEntry = StoredEntries.AddDefaulted_GetRef();
	NewEntry = Entry;
	NewEntry.Message = Message;
	NewEntry.Sequence = NextEntrySequence++;

	TrimStoredEntries();
	RefreshFeedWidget();
}

void UProjectActivityFeedSubsystem::TrimStoredEntries()
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const int32 MaxStoredEntries = Settings ? FMath::Max(Settings->MaxStoredEntries, 10) : 80;
	if (StoredEntries.Num() > MaxStoredEntries)
	{
		const int32 ExcessCount = StoredEntries.Num() - MaxStoredEntries;
		StoredEntries.RemoveAt(0, ExcessCount, EAllowShrinking::No);
	}
}

void UProjectActivityFeedSubsystem::ResetReflectionState()
{
	CachedLevelingComponent = nullptr;
	CachedInventoryComponent = nullptr;
	LastObservedLevel = INDEX_NONE;
	LastObservedExperience = 0.0;
	LastLevelPollTimeSeconds = -1.0;
	LastInventoryPollTimeSeconds = -1.0;
	bHasObservedExperience = false;
	bWarnedLevelingBridgeFailure = false;
	bWarnedInventoryBridgeFailure = false;
	bWarnedMissingLevelingComponent = false;
	bWarnedMissingInventoryComponent = false;
}

void UProjectActivityFeedSubsystem::ResetInventoryState()
{
	LastInventorySnapshot.Reset();
	PendingLootEntries.Reset();
	bHasInventorySnapshot = false;
	PendingLootExpiryTimeSeconds = -1.0;
}

void UProjectActivityFeedSubsystem::ResetAggregates()
{
	bHasPendingHeal = false;
	PendingHealAmount = 0.0f;
	PendingHealExpiryTimeSeconds = -1.0;
	bHasPendingExperience = false;
	bPendingExperienceIsDoctrine = false;
	PendingExperienceAmount = 0;
	PendingExperienceExpiryTimeSeconds = -1.0;
	PendingLootEntries.Reset();
	PendingLootExpiryTimeSeconds = -1.0;
}

void UProjectActivityFeedSubsystem::ProcessExistingEnemies()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		RegisterEnemyPawn(*It);
	}
}

void UProjectActivityFeedSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	RegisterEnemyPawn(Cast<APawn>(SpawnedActor));
}

void UProjectActivityFeedSubsystem::RegisterEnemyPawn(APawn* EnemyPawn)
{
	if (!ShouldTrackEnemyPawn(EnemyPawn))
	{
		return;
	}

	const TObjectKey<APawn> EnemyKey(EnemyPawn);
	if (TrackedEnemies.Contains(EnemyKey))
	{
		return;
	}

	FProjectActivityFeedTrackedEnemyState& EnemyState = TrackedEnemies.Add(EnemyKey);
	EnemyState.EnemyPawn = EnemyPawn;
	bHasPendingEnemyBindings = true;
}

void UProjectActivityFeedSubsystem::RefreshEnemyBindings()
{
	bHasPendingEnemyBindings = false;
	for (TPair<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState>& EnemyPair : TrackedEnemies)
	{
		FProjectActivityFeedTrackedEnemyState& EnemyState = EnemyPair.Value;
		APawn* EnemyPawn = EnemyState.EnemyPawn.Get();
		if (!EnemyPawn)
		{
			continue;
		}

		if (!EnemyState.CombatComponent.IsValid())
		{
			EnemyState.CombatComponent = EnemyPawn->FindComponentByClass<UProjectCombatAttributeComponent>();
			if (EnemyState.CombatComponent.IsValid())
			{
				EnemyState.bWasDead = EnemyState.CombatComponent->IsDead();
			}
			else
			{
				bHasPendingEnemyBindings = true;
			}
		}

		if (EnemyState.CombatComponent.IsValid() && !EnemyState.bDamageBound)
		{
			EnemyState.CombatComponent->OnDamageApplied.AddUniqueDynamic(this, &ThisClass::HandleEnemyDamageApplied);
			EnemyState.bDamageBound = true;
		}
	}
}

void UProjectActivityFeedSubsystem::UnregisterInvalidEnemies()
{
	TArray<TObjectKey<APawn>> KeysToRemove;
	for (const TPair<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState>& EnemyPair : TrackedEnemies)
	{
		if (!EnemyPair.Value.EnemyPawn.IsValid())
		{
			KeysToRemove.Add(EnemyPair.Key);
		}
	}

	for (const TObjectKey<APawn>& EnemyKey : KeysToRemove)
	{
		if (FProjectActivityFeedTrackedEnemyState* EnemyState = TrackedEnemies.Find(EnemyKey))
		{
			if (EnemyState->bDamageBound && EnemyState->CombatComponent.IsValid())
			{
				EnemyState->CombatComponent->OnDamageApplied.RemoveDynamic(this, &ThisClass::HandleEnemyDamageApplied);
			}
		}

		TrackedEnemies.Remove(EnemyKey);
	}
}

void UProjectActivityFeedSubsystem::ClearTrackedEnemies()
{
	for (TPair<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState>& EnemyPair : TrackedEnemies)
	{
		FProjectActivityFeedTrackedEnemyState& EnemyState = EnemyPair.Value;
		if (EnemyState.bDamageBound && EnemyState.CombatComponent.IsValid())
		{
			EnemyState.CombatComponent->OnDamageApplied.RemoveDynamic(this, &ThisClass::HandleEnemyDamageApplied);
		}
	}

	TrackedEnemies.Reset();
}

void UProjectActivityFeedSubsystem::EvaluateEnemyBarks(const float CurrentTimeSeconds)
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	if (!Settings || !TrackedPlayerPawn || (CurrentTimeSeconds - LastEnemyAwarenessPollTimeSeconds) < ProjectActivityFeedPrivate::EnemyAwarenessPollIntervalSeconds)
	{
		return;
	}

	LastEnemyAwarenessPollTimeSeconds = CurrentTimeSeconds;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<APawn*> BarkCandidates;
	int32 AwarenessCount = 0;
	FVector PlayerViewLocation = TrackedPlayerPawn->GetActorLocation();
	FRotator PlayerViewRotation = TrackedPlayerPawn->GetActorRotation();
	TrackedPlayerPawn->GetActorEyesViewPoint(PlayerViewLocation, PlayerViewRotation);
	const float MaxSightRangeSquared = FMath::Square(FMath::Max(Settings->SightRange, 0.0f));

	for (TPair<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState>& EnemyPair : TrackedEnemies)
	{
		FProjectActivityFeedTrackedEnemyState& EnemyState = EnemyPair.Value;
		APawn* EnemyPawn = EnemyState.EnemyPawn.Get();
		if (!EnemyPawn || EnemyPawn == TrackedPlayerPawn)
		{
			continue;
		}

		UProjectCombatAttributeComponent* EnemyCombatComponent = EnemyState.CombatComponent.Get();
		if (EnemyCombatComponent && EnemyCombatComponent->IsDead())
		{
			EnemyState.bAwarenessActive = false;
			EnemyState.bWasDead = true;
			continue;
		}

		FVector EnemyViewLocation = EnemyPawn->GetActorLocation();
		FRotator EnemyViewRotation = EnemyPawn->GetActorRotation();
		EnemyPawn->GetActorEyesViewPoint(EnemyViewLocation, EnemyViewRotation);

		const FVector ToPlayer = PlayerViewLocation - EnemyViewLocation;
		bool bCanSeePlayer = ToPlayer.SizeSquared() <= MaxSightRangeSquared;

		if (bCanSeePlayer)
		{
			const float FacingDot = FVector::DotProduct(EnemyViewRotation.Vector().GetSafeNormal(), ToPlayer.GetSafeNormal());
			bCanSeePlayer = FacingDot >= Settings->SightDotThreshold;
		}

		if (bCanSeePlayer)
		{
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ProjectActivityFeedSight), false);
			QueryParams.AddIgnoredActor(EnemyPawn);
			QueryParams.AddIgnoredActor(TrackedPlayerPawn);

			FHitResult HitResult;
			bCanSeePlayer = !World->LineTraceSingleByChannel(HitResult, EnemyViewLocation, PlayerViewLocation, ECC_Visibility, QueryParams)
				|| HitResult.GetActor() == nullptr;
		}

		if (bCanSeePlayer)
		{
			EnemyState.LastSeenTimeSeconds = CurrentTimeSeconds;
			if (!EnemyState.bAwarenessActive)
			{
				EnemyState.bAwarenessActive = true;
				if ((CurrentTimeSeconds - EnemyState.LastBarkTimeSeconds) >= Settings->BarkCooldownSeconds)
				{
					BarkCandidates.Add(EnemyPawn);
				}
			}
		}
		else if (EnemyState.bAwarenessActive)
		{
			const double LastSeenTime = EnemyState.LastSeenTimeSeconds >= 0.0 ? EnemyState.LastSeenTimeSeconds : CurrentTimeSeconds;
			if ((CurrentTimeSeconds - LastSeenTime) >= Settings->LostSightResetSeconds)
			{
				EnemyState.bAwarenessActive = false;
			}
		}

		if (EnemyState.bAwarenessActive)
		{
			++AwarenessCount;
		}
	}

	if (BarkCandidates.Num() == 0)
	{
		return;
	}

	const bool bGroupBark = AwarenessCount > 1;
	const int32 SpeakerIndex = FMath::RandHelper(BarkCandidates.Num());
	APawn* BarkSpeaker = BarkCandidates.IsValidIndex(SpeakerIndex) ? BarkCandidates[SpeakerIndex] : BarkCandidates[0];
	AddFeedEntry(BuildEnemyBarkEntry(BarkSpeaker, bGroupBark));

	for (APawn* BarkCandidate : BarkCandidates)
	{
		if (FProjectActivityFeedTrackedEnemyState* EnemyState = TrackedEnemies.Find(BarkCandidate))
		{
			EnemyState->LastBarkTimeSeconds = CurrentTimeSeconds;
		}
	}
}

void UProjectActivityFeedSubsystem::FlushPendingAggregates(const float CurrentTimeSeconds)
{
	if (bHasPendingHeal && CurrentTimeSeconds >= PendingHealExpiryTimeSeconds)
	{
		FlushPendingHeal();
	}

	if (bHasPendingExperience && CurrentTimeSeconds >= PendingExperienceExpiryTimeSeconds)
	{
		FlushPendingExperience();
	}

	if (PendingLootEntries.Num() > 0 && CurrentTimeSeconds >= PendingLootExpiryTimeSeconds)
	{
		FlushPendingLoot();
	}
}

void UProjectActivityFeedSubsystem::FlushPendingExperience()
{
	if (!bHasPendingExperience || PendingExperienceAmount <= 0)
	{
		bHasPendingExperience = false;
		PendingExperienceAmount = 0;
		return;
	}

	AddFeedEntry(
		ProjectActivityFeedPrivate::MakeGainEntry(
			EProjectActivityFeedChannel::Experience,
			bPendingExperienceIsDoctrine
				? FText::Format(LOCTEXT("DoctrineExperienceGainFormat", "+{0} DXP"), FText::AsNumber(PendingExperienceAmount))
				: FText::Format(LOCTEXT("ExperienceGainFormat", "+{0} EXP"), FText::AsNumber(PendingExperienceAmount)),
			bPendingExperienceIsDoctrine ? TEXT("DXP") : TEXT("EXP"),
			FText::Format(LOCTEXT("ExperienceGainPrimaryFormat", "+{0}"), FText::AsNumber(PendingExperienceAmount)),
			FText::FromString(bPendingExperienceIsDoctrine ? TEXT("DXP") : TEXT("EXP"))));

	bHasPendingExperience = false;
	bPendingExperienceIsDoctrine = false;
	PendingExperienceAmount = 0;
	PendingExperienceExpiryTimeSeconds = -1.0;
}

void UProjectActivityFeedSubsystem::FlushPendingHeal()
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float HealThreshold = Settings ? FMath::Max(Settings->HealMinToLog, 0.0f) : 5.0f;
	if (bHasPendingHeal && PendingHealAmount >= HealThreshold)
	{
		AddFeedEntry(
			ProjectActivityFeedPrivate::MakeGainEntry(
				EProjectActivityFeedChannel::Combat,
				FText::Format(LOCTEXT("HealGainFormat", "Recovered {0} HP"), FText::AsNumber(FMath::RoundToInt(PendingHealAmount))),
				TEXT("HP"),
				FText::Format(LOCTEXT("HealGainPrimaryFormat", "+{0}"), FText::AsNumber(FMath::RoundToInt(PendingHealAmount))),
				FText::FromString(TEXT("HP"))));
	}

	bHasPendingHeal = false;
	PendingHealAmount = 0.0f;
	PendingHealExpiryTimeSeconds = -1.0;
}

void UProjectActivityFeedSubsystem::FlushPendingLoot()
{
	if (PendingLootEntries.Num() == 0)
	{
		PendingLootExpiryTimeSeconds = -1.0;
		return;
	}

	TArray<FString> ItemKeys;
	PendingLootEntries.GenerateKeyArray(ItemKeys);
	ItemKeys.Sort();

	for (const FString& ItemKey : ItemKeys)
	{
		const FProjectActivityFeedPendingLootEntry* LootEntry = PendingLootEntries.Find(ItemKey);
		if (!LootEntry || LootEntry->Count <= 0)
		{
			continue;
		}

		if (LootEntry->Count > 1)
		{
			AddFeedEntry(
				EProjectActivityFeedChannel::Loot,
				FText::Format(LOCTEXT("LootFoundManyFormat", "Found {0} x{1}"), LootEntry->DisplayName, FText::AsNumber(LootEntry->Count)));
		}
		else
		{
			AddFeedEntry(
				EProjectActivityFeedChannel::Loot,
				FText::Format(LOCTEXT("LootFoundSingleFormat", "Found {0}"), LootEntry->DisplayName));
		}
	}

	PendingLootEntries.Reset();
	PendingLootExpiryTimeSeconds = -1.0;
}

void UProjectActivityFeedSubsystem::PollLevelingBridge(const float CurrentTimeSeconds)
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float PollInterval = Settings ? FMath::Max(Settings->LevelPollInterval, 0.05f) : 0.25f;
	if ((CurrentTimeSeconds - LastLevelPollTimeSeconds) < PollInterval)
	{
		return;
	}

	LastLevelPollTimeSeconds = CurrentTimeSeconds;

	if (!TrackedPlayerPawn)
	{
		return;
	}

	if (!TrackedInnerDoctrineComponent)
	{
		TrackedInnerDoctrineComponent = TrackedPlayerPawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
	}

	if (TrackedInnerDoctrineComponent)
	{
		const double NumericExperience = static_cast<double>(TrackedInnerDoctrineComponent->GetCurrentRunDxp());
		if (!bHasObservedExperience)
		{
			LastObservedExperience = NumericExperience;
			bHasObservedExperience = true;
		}
		else if (NumericExperience > LastObservedExperience)
		{
			const int32 DeltaExperience = FMath::Max(FMath::RoundToInt(static_cast<float>(NumericExperience - LastObservedExperience)), 0);
			if (DeltaExperience > 0)
			{
				QueueExperienceGain(DeltaExperience, CurrentTimeSeconds, true);
			}
			LastObservedExperience = NumericExperience;
		}
		else
		{
			LastObservedExperience = NumericExperience;
		}
		return;
	}

	if (!TryResolveLevelingComponent())
	{
		return;
	}

	static const TArray<FName> LevelFunctionCandidates = {
		TEXT("GetCurrentLevel"),
		TEXT("GetLevel")
	};

	static const TArray<FString> LevelPropertyCandidates = {
		TEXT("CurrentLevel"),
		TEXT("Level")
	};

	static const TArray<FName> ExperienceFunctionCandidates = {
		TEXT("GetCurrentExp"),
		TEXT("GetCurrentExperience"),
		TEXT("GetCurrentExpValue"),
		TEXT("GetExperience")
	};

	static const TArray<FString> ExperiencePropertyCandidates = {
		TEXT("CurrentExp"),
		TEXT("CurrentExperience"),
		TEXT("CurrentExpValue"),
		TEXT("Experience")
	};

	double NumericLevel = 0.0;
	double NumericExperience = 0.0;
	const bool bResolvedLevel = ProjectActivityFeedPrivate::TryInvokeZeroArgNumericFunction(CachedLevelingComponent, LevelFunctionCandidates, NumericLevel)
		|| ProjectActivityFeedPrivate::TryReadNamedNumericProperty(CachedLevelingComponent, LevelPropertyCandidates, NumericLevel);
	const bool bResolvedExperience = ProjectActivityFeedPrivate::TryInvokeZeroArgNumericFunction(CachedLevelingComponent, ExperienceFunctionCandidates, NumericExperience)
		|| ProjectActivityFeedPrivate::TryReadNamedNumericProperty(CachedLevelingComponent, ExperiencePropertyCandidates, NumericExperience);

	if (!bResolvedLevel && !bResolvedExperience)
	{
		if (!bWarnedLevelingBridgeFailure)
		{
			bWarnedLevelingBridgeFailure = true;
			UE_LOG(LogProjectActivityFeed, Warning, TEXT("[ProjectActivityFeed] Could not read level or experience values from %s."), *GetNameSafe(CachedLevelingComponent));
		}

		return;
	}

	if (bResolvedLevel)
	{
		const int32 CurrentLevel = FMath::Max(FMath::RoundToInt(static_cast<float>(NumericLevel)), 1);
		if (LastObservedLevel == INDEX_NONE)
		{
			LastObservedLevel = CurrentLevel;
		}
		else if (CurrentLevel > LastObservedLevel)
		{
			if (bHasPendingExperience)
			{
				FlushPendingExperience();
			}

			AddFeedEntry(
				EProjectActivityFeedChannel::Experience,
				FText::Format(LOCTEXT("LevelUpFormat", "Level Up: {0}"), FText::AsNumber(CurrentLevel)));
			LastObservedLevel = CurrentLevel;
		}
		else
		{
			LastObservedLevel = CurrentLevel;
		}
	}

	if (bResolvedExperience)
	{
		if (!bHasObservedExperience)
		{
			LastObservedExperience = NumericExperience;
			bHasObservedExperience = true;
		}
		else if (NumericExperience > LastObservedExperience)
		{
			const int32 DeltaExperience = FMath::Max(FMath::RoundToInt(static_cast<float>(NumericExperience - LastObservedExperience)), 0);
			if (DeltaExperience > 0)
			{
				QueueExperienceGain(DeltaExperience, CurrentTimeSeconds, false);
			}

			LastObservedExperience = NumericExperience;
		}
		else
		{
			LastObservedExperience = NumericExperience;
		}
	}
}

void UProjectActivityFeedSubsystem::PollInventoryBridge(const float CurrentTimeSeconds)
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float PollInterval = Settings ? FMath::Max(Settings->InventoryPollInterval, 0.05f) : 0.35f;
	if ((CurrentTimeSeconds - LastInventoryPollTimeSeconds) < PollInterval)
	{
		return;
	}

	LastInventoryPollTimeSeconds = CurrentTimeSeconds;

	if (!TrackedPlayerPawn)
	{
		return;
	}

	if (!TryResolveInventoryComponent())
	{
		return;
	}

	TMap<FString, FProjectActivityFeedObservedInventoryItem> NewSnapshot;
	if (!TryBuildInventorySnapshot(NewSnapshot))
	{
		if (!bWarnedInventoryBridgeFailure)
		{
			bWarnedInventoryBridgeFailure = true;
			UE_LOG(LogProjectActivityFeed, Warning, TEXT("[ProjectActivityFeed] Could not build an inventory snapshot from %s."), *GetNameSafe(CachedInventoryComponent));
		}

		return;
	}

	if (!bHasInventorySnapshot)
	{
		LastInventorySnapshot = MoveTemp(NewSnapshot);
		bHasInventorySnapshot = true;
		return;
	}

	for (const TPair<FString, FProjectActivityFeedObservedInventoryItem>& ItemPair : NewSnapshot)
	{
		const FProjectActivityFeedObservedInventoryItem* OldItem = LastInventorySnapshot.Find(ItemPair.Key);
		const int32 OldCount = OldItem ? OldItem->Count : 0;
		if (ItemPair.Value.Count > OldCount)
		{
			QueueLootGain(ItemPair.Value.DisplayName, ItemPair.Value.Count - OldCount, CurrentTimeSeconds);
		}
	}

	LastInventorySnapshot = MoveTemp(NewSnapshot);
}

bool UProjectActivityFeedSubsystem::TryResolveLevelingComponent()
{
	if (CachedLevelingComponent && CachedLevelingComponent->IsRegistered())
	{
		return true;
	}

	CachedLevelingComponent = nullptr;
	if (TrackedPlayerPawn)
	{
		CachedLevelingComponent = ProjectActivityFeedPrivate::FindComponentByClassHint<UActorComponent>(TrackedPlayerPawn, { TEXT("ARSLevelingComponent") });
	}

	if (!CachedLevelingComponent && TrackedPlayerController)
	{
		CachedLevelingComponent = ProjectActivityFeedPrivate::FindComponentByClassHint<UActorComponent>(TrackedPlayerController, { TEXT("ARSLevelingComponent") });
	}

	return CachedLevelingComponent != nullptr;
}

bool UProjectActivityFeedSubsystem::TryResolveInventoryComponent()
{
	if (CachedInventoryComponent && CachedInventoryComponent->IsRegistered())
	{
		return true;
	}

	CachedInventoryComponent = nullptr;
	if (TrackedPlayerPawn)
	{
		CachedInventoryComponent = ProjectActivityFeedPrivate::FindComponentByClassHint<UActorComponent>(
			TrackedPlayerPawn,
			{ TEXT("ACFInventoryComponent"), TEXT("ACFStorageComponent") });
	}

	if (!CachedInventoryComponent && TrackedPlayerController)
	{
		CachedInventoryComponent = ProjectActivityFeedPrivate::FindComponentByClassHint<UActorComponent>(
			TrackedPlayerController,
			{ TEXT("ACFInventoryComponent"), TEXT("ACFStorageComponent") });
	}

	return CachedInventoryComponent != nullptr;
}

bool UProjectActivityFeedSubsystem::TryBuildInventorySnapshot(TMap<FString, FProjectActivityFeedObservedInventoryItem>& OutSnapshot) const
{
	if (!CachedInventoryComponent)
	{
		return false;
	}

	static const TArray<FName> InventoryFunctionCandidates = {
		TEXT("GetInventory")
	};

	static const TArray<FString> InventoryPropertyCandidates = {
		TEXT("Inventory"),
		TEXT("Items"),
		TEXT("InventoryItems"),
		TEXT("StoredItems")
	};

	return ProjectActivityFeedPrivate::TryBuildInventorySnapshotFromFunction(CachedInventoryComponent, InventoryFunctionCandidates, OutSnapshot)
		|| ProjectActivityFeedPrivate::TryBuildInventorySnapshotFromProperty(CachedInventoryComponent, InventoryPropertyCandidates, OutSnapshot);
}

void UProjectActivityFeedSubsystem::QueueLootGain(const FText& DisplayName, const int32 Count, const float CurrentTimeSeconds)
{
	if (Count <= 0)
	{
		return;
	}

	FProjectActivityFeedPendingLootEntry& PendingEntry = PendingLootEntries.FindOrAdd(DisplayName.ToString());
	PendingEntry.DisplayName = DisplayName;
	PendingEntry.Count += Count;

	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float AggregationWindow = Settings ? FMath::Max(Settings->AggregationWindowLoot, 0.0f) : 1.0f;
	PendingLootExpiryTimeSeconds = CurrentTimeSeconds + AggregationWindow;
}

void UProjectActivityFeedSubsystem::QueueExperienceGain(const int32 DeltaExperience, const float CurrentTimeSeconds, const bool bDoctrineExperience)
{
	if (DeltaExperience <= 0)
	{
		return;
	}

	if (bHasPendingExperience && bPendingExperienceIsDoctrine != bDoctrineExperience)
	{
		FlushPendingExperience();
	}

	bHasPendingExperience = true;
	bPendingExperienceIsDoctrine = bDoctrineExperience;
	PendingExperienceAmount += DeltaExperience;

	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float AggregationWindow = Settings ? FMath::Max(Settings->AggregationWindowXp, 0.0f) : 1.0f;
	PendingExperienceExpiryTimeSeconds = CurrentTimeSeconds + AggregationWindow;
}

void UProjectActivityFeedSubsystem::QueueHeal(const float Amount, const float CurrentTimeSeconds)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	bHasPendingHeal = true;
	PendingHealAmount += Amount;

	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const float AggregationWindow = Settings ? FMath::Max(Settings->AggregationWindowHeal, 0.0f) : 0.75f;
	PendingHealExpiryTimeSeconds = CurrentTimeSeconds + AggregationWindow;
}

void UProjectActivityFeedSubsystem::TryEmitEnemyDefeatFromPendingDamage(AActor* SourceActor)
{
	if (!DoesActorBelongToTrackedPlayer(SourceActor))
	{
		return;
	}

	for (TPair<TObjectKey<APawn>, FProjectActivityFeedTrackedEnemyState>& EnemyPair : TrackedEnemies)
	{
		FProjectActivityFeedTrackedEnemyState& EnemyState = EnemyPair.Value;
		UProjectCombatAttributeComponent* EnemyCombatComponent = EnemyState.CombatComponent.Get();
		if (!EnemyCombatComponent || EnemyState.bWasDead || !EnemyCombatComponent->IsDead())
		{
			continue;
		}

		EnemyState.bWasDead = true;

		FText EnemyName;
		if (!TryGetEnemyDisplayName(EnemyState.EnemyPawn.Get(), EnemyName))
		{
			EnemyName = LOCTEXT("UnknownEnemy", "Enemy");
		}

		AddFeedEntry(EProjectActivityFeedChannel::Combat, FText::Format(LOCTEXT("EnemyDefeatedFormat", "Defeated {0}"), EnemyName));
		return;
	}
}

bool UProjectActivityFeedSubsystem::ShouldTrackEnemyPawn(const APawn* Pawn) const
{
	if (!Pawn || Pawn == TrackedPlayerPawn)
	{
		return false;
	}

	for (const TSubclassOf<APawn>& EnemyClass : TargetEnemyBaseClasses)
	{
		if (EnemyClass && Pawn->IsA(EnemyClass))
		{
			return true;
		}
	}

	return Pawn->FindComponentByClass<UProjectEnemyLevelComponent>() != nullptr
		|| Pawn->FindComponentByClass<UProjectEnemyTargetInfoComponent>() != nullptr;
}

bool UProjectActivityFeedSubsystem::DoesActorBelongToTrackedPlayer(const AActor* Actor) const
{
	if (!Actor || !TrackedPlayerPawn)
	{
		return false;
	}

	const AActor* CurrentActor = Actor;
	for (int32 Depth = 0; CurrentActor && Depth < 8; ++Depth)
	{
		if (CurrentActor == TrackedPlayerPawn || CurrentActor == TrackedPlayerController)
		{
			return true;
		}

		if (const APawn* CandidatePawn = Cast<APawn>(CurrentActor))
		{
			if (CandidatePawn == TrackedPlayerPawn || CandidatePawn->GetController() == TrackedPlayerController || CandidatePawn->GetInstigator() == TrackedPlayerPawn)
			{
				return true;
			}
		}

		if (CurrentActor->GetInstigator() == TrackedPlayerPawn)
		{
			return true;
		}

		CurrentActor = CurrentActor->GetOwner();
	}

	return false;
}

bool UProjectActivityFeedSubsystem::TryGetEnemyDisplayName(const AActor* EnemyActor, FText& OutDisplayName) const
{
	if (!EnemyActor)
	{
		return false;
	}

	if (const UProjectEnemyTargetInfoComponent* TargetInfoComponent = EnemyActor->FindComponentByClass<UProjectEnemyTargetInfoComponent>())
	{
		FProjectEnemyTargetDisplayData DisplayData;
		if (TargetInfoComponent->TryBuildDisplayData(DisplayData) && !ProjectActivityFeedPrivate::IsEmptyText(DisplayData.EnemyName))
		{
			OutDisplayName = DisplayData.EnemyName;
			return true;
		}
	}

	OutDisplayName = ProjectActivityFeedPrivate::MakePrettyText(EnemyActor->GetActorNameOrLabel());
	return !ProjectActivityFeedPrivate::IsEmptyText(OutDisplayName);
}

FText UProjectActivityFeedSubsystem::BuildEnemyBarkText(const AActor* EnemyActor, const bool bGroupBark) const
{
	FText EnemyName;
	if (!TryGetEnemyDisplayName(EnemyActor, EnemyName))
	{
		EnemyName = LOCTEXT("GenericEnemySpeaker", "Enemy");
	}

	const FString BarkLine = ProjectEnemyDialogueResolver::PickSightBark(EnemyActor, bGroupBark);

	return FText::Format(LOCTEXT("EnemyBarkFormat", "{0}: \"{1}\""), EnemyName, FText::FromString(BarkLine));
}

FProjectActivityFeedEntry UProjectActivityFeedSubsystem::BuildEnemyBarkEntry(const AActor* EnemyActor, const bool bGroupBark) const
{
	FText EnemyName;
	if (!TryGetEnemyDisplayName(EnemyActor, EnemyName))
	{
		EnemyName = LOCTEXT("GenericEnemySpeaker", "Enemy");
	}

	const FString BarkLine = ProjectEnemyDialogueResolver::PickSightBark(EnemyActor, bGroupBark);

	FProjectActivityFeedEntry Entry = ProjectActivityFeedPrivate::MakeStandardEntry(
		EProjectActivityFeedChannel::Dialogue,
		FText::Format(LOCTEXT("EnemyBarkFormat", "{0}: \"{1}\""), EnemyName, FText::FromString(BarkLine)));
	Entry.RenderStyle = EProjectActivityFeedRenderStyle::DialogueQuote;
	Entry.BadgeLabelOverride = TEXT("ENEMY");
	Entry.PrimaryText = EnemyName;
	Entry.SecondaryText = FText::FromString(FString::Printf(TEXT("\"%s\""), *BarkLine));
	return Entry;
}

TSubclassOf<UProjectActivityFeedWidget> UProjectActivityFeedSubsystem::ResolveFeedWidgetClass() const
{
	if (const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get())
	{
		if (UClass* LoadedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
			UISettings->ActivityFeedWidgetClass,
			UProjectActivityFeedWidget::StaticClass(),
			TEXT("ProjectChronicleWidget")))
		{
			return LoadedClass;
		}
	}

	return ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectActivityFeedWidget>(TEXT("ProjectChronicleWidget"));
}

#undef LOCTEXT_NAMESPACE
