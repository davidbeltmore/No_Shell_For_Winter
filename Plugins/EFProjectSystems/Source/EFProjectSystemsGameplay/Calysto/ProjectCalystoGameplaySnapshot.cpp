#include "Calysto/ProjectCalystoGameplaySnapshot.h"

#include "Actors/ACFCharacter.h"
#include "Calysto/ProjectCalystoFloorOutcomeSubsystem.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Components/ACFEquipmentComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Items/ACFItem.h"
#include "Items/ACFItemFragment.h"
#include "ItemActors/ACFItemActor.h"
#include "Misc/SecureHash.h"
#include "Serialization/ArchiveProxy.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace ProjectCalystoSnapshot
{
	constexpr int32 MaximumInventories = 129, MaximumItems = 1024, MaximumFragments = 4096;
	constexpr int64 MaximumBytes = 16 * 1024 * 1024;
	bool Fail(FString& Error, const TCHAR* Message) { Error = Message; return false; }
	bool LiveActor(const AActor* Actor) { return IsValid(Actor) && !Actor->IsActorBeingDestroyed(); }

	/** Writes exact values with stable logical object keys, never addresses, request IDs or transient object names. */
	struct FCanonicalArchive final : FArchiveProxy
	{
		const TMap<const UObject*, FString>& Keys;
		TArray<TObjectPtr<UObject>>* References;
		FCanonicalArchive(FArchive& Inner, const TMap<const UObject*, FString>& InKeys, TArray<TObjectPtr<UObject>>* InReferences = nullptr)
			: FArchiveProxy(Inner), Keys(InKeys), References(InReferences)
		{ ArIsSaveGame = true; SetIsSaving(true); ArNoDelta = true; }
		virtual void Serialize(void* Data, int64 Num) override
		{
			if (Num < 0 || Tell() > MaximumBytes - Num) { SetError(); return; }
			FArchiveProxy::Serialize(Data, Num);
		}
		virtual FArchive& operator<<(FName& Name) override { FString Value = Name.ToString(); *this << Value; return *this; }
		virtual FArchive& operator<<(UObject*& Object) override
		{
			FString Key = TEXT("None");
			if (Object)
			{
				if (!IsValid(Object)) { SetError(); return *this; }
				if (const FString* Logical = Keys.Find(Object)) Key = *Logical;
				else if (Object->IsAsset() || Object->IsA<UClass>()) Key = Object->GetPathName();
				else { SetError(); return *this; }
				if (References) References->AddUnique(Object);
			}
			*this << Key; return *this;
		}
		virtual FArchive& operator<<(FObjectPtr& Object) override { UObject* Value = Object.Get(); return *this << Value; }
		virtual FArchive& operator<<(FWeakObjectPtr& Object) override { UObject* Value = Object.Get(); return *this << Value; }
		virtual FArchive& operator<<(FSoftObjectPtr& Object) override { FString Path = Object.ToSoftObjectPath().ToString(); *this << Path; return *this; }
		virtual FArchive& operator<<(FSoftObjectPath& Object) override { FString Path = Object.ToString(); *this << Path; return *this; }
		using FArchiveProxy::operator<<;
	};

	bool Bounded(const FProperty* Property, const void* Value, int64& Work, int32 Depth = 0)
	{
		if (++Work > 131072 || Depth > 16) return false;
		if (const auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper H(Array, Value); if (H.Num() > 4096) return false;
			for (int32 I = 0; I < H.Num(); ++I) if (!Bounded(Array->Inner, H.GetRawPtr(I), Work, Depth + 1)) return false;
		}
		else if (const auto* Map = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper H(Map, Value); if (H.Num() > 4096 || H.GetMaxIndex() > 8192) return false;
			for (int32 I = 0; I < H.GetMaxIndex(); ++I) if (H.IsValidIndex(I)
				&& (!Bounded(Map->KeyProp, H.GetKeyPtr(I), Work, Depth + 1) || !Bounded(Map->ValueProp, H.GetValuePtr(I), Work, Depth + 1))) return false;
		}
		else if (const auto* Set = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper H(Set, Value); if (H.Num() > 4096 || H.GetMaxIndex() > 8192) return false;
			for (int32 I = 0; I < H.GetMaxIndex(); ++I) if (H.IsValidIndex(I) && !Bounded(Set->ElementProp, H.GetElementPtr(I), Work, Depth + 1)) return false;
		}
		else if (const auto* Struct = CastField<FStructProperty>(Property))
		{
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
					if (!Bounded(*It, It->ContainerPtrToValuePtr<void>(Value, Index), Work, Depth + 1)) return false;
		}
		else if (const auto* String = CastField<FStrProperty>(Property))
		{
			const int32 Length = String->GetPropertyValue(Value).Len(); if (Length > 65536) return false;
			Work += Length; // Bound aggregate dynamic text before copying any reflected value.
		}
		return Work <= 131072;
	}

	struct FValue
	{
		FProperty* Property = nullptr;
		void* Data = nullptr;
		explicit FValue(FProperty* InProperty, const void* Source) : Property(InProperty)
		{
			Data = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Data); Property->CopyCompleteValue(Data, Source);
		}
		~FValue() { Property->DestroyValue(Data); FMemory::Free(Data); }
	};
	void WriteValue(FCanonicalArchive& Ar, FProperty* Property, void* Value)
	{
		if (Ar.IsError()) return;
		if (auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper H(Array, Value); int32 Count = H.Num(); Ar << Count;
			for (int32 Index = 0; Index < Count; ++Index) WriteValue(Ar, Array->Inner, H.GetRawPtr(Index));
			return;
		}
		auto* Map = CastField<FMapProperty>(Property); auto* Set = CastField<FSetProperty>(Property);
		if (Map || Set)
		{
			TArray<TArray<uint8>> Rows;
			int64 RowBytes = 0;
			auto Row = [&](FProperty* First, void* FirstValue, FProperty* Second = nullptr, void* SecondValue = nullptr)
			{
				if (Ar.IsError()) return;
				TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FCanonicalArchive Inner(Writer, Ar.Keys, Ar.References);
				Inner.ArIsSaveGame = false; WriteValue(Inner, First, FirstValue);
				if (Second) WriteValue(Inner, Second, SecondValue);
				if (Inner.IsError() || Writer.IsError() || Bytes.Num() > MaximumBytes - RowBytes) { Ar.SetError(); return; }
				RowBytes += Bytes.Num();
				Rows.Add(MoveTemp(Bytes));
			};
			if (Map)
			{
				FScriptMapHelper H(Map, Value);
				for (int32 Index = 0; Index < H.GetMaxIndex(); ++Index) if (H.IsValidIndex(Index))
					Row(Map->KeyProp, H.GetKeyPtr(Index), Map->ValueProp, H.GetValuePtr(Index));
			}
			else
			{
				FScriptSetHelper H(Set, Value);
				for (int32 Index = 0; Index < H.GetMaxIndex(); ++Index) if (H.IsValidIndex(Index)) Row(Set->ElementProp, H.GetElementPtr(Index));
			}
			Rows.Sort([](const auto& A, const auto& B)
			{
				const int32 Compare = FMemory::Memcmp(A.GetData(), B.GetData(), FMath::Min(A.Num(), B.Num()));
				return Compare == 0 ? A.Num() < B.Num() : Compare < 0;
			});
			int32 Count = Rows.Num(); Ar << Count;
			for (auto& Bytes : Rows) { int32 Length = Bytes.Num(); Ar << Length; Ar.Serialize(Bytes.GetData(), Length); }
			return;
		}
		if (auto* Struct = CastField<FStructProperty>(Property); Struct && !(Struct->Struct->StructFlags & STRUCT_SerializeNative))
		{
			TArray<FProperty*> Fields;
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It) Fields.Add(*It);
			Fields.Sort([](const FProperty& A, const FProperty& B) { return A.GetName() < B.GetName(); });
			for (FProperty* Field : Fields)
			{
				FString Name = Field->GetName(); Ar << Name;
				for (int32 Index = 0; Index < Field->ArrayDim; ++Index) WriteValue(Ar, Field, Field->ContainerPtrToValuePtr<void>(Value, Index));
			}
			return;
		}
		FStructuredArchiveFromArchive Structured(Ar); Property->SerializeItem(Structured.GetSlot(), Value, nullptr);
	}
	struct FObjectState
	{
		UObject* Object = nullptr; UObject* Outer = nullptr; UClass* Class = nullptr;
		TArray<TUniquePtr<FValue>> Values;
	};
	struct FInventoryState
	{
		UACFEquipmentComponent* Equipment = nullptr; AActor* Owner = nullptr;
		TArray<FInventoryItem> Items;
		TArray<FEquippedItem> Equipped;
		TArray<UACFItemFragment*> Fragments;
		FString Key;
	};
	struct FPortableValue { FName Property; TArray<uint8> Bytes; };
	struct FPortableObject
	{
		FString Key, OwnerKey;
		UClass* Class = nullptr;
		bool bEquipment = false;
		TArray<FPortableValue> Values;
	};
	/**
	 * Items and their realized equipment projection travel as separate bounded
	 * transport records.  The projection cannot be applied until ACF has created
	 * the destination equipment actor through its native equip lifecycle.
	 */
	struct FPortableInventory
	{
		FString Key;
		TArray<TArray<uint8>> Items;
		TArray<TArray<uint8>> Equipped;
		// ACF ranged weapons synchronously reload during their native equip
		// lifecycle.  Preserve the source realized-equipment sequence so a
		// selected projectile restored after its ranged weapon remains selected.
		// GUID sorting is not semantically valid for this native dependency.
		TArray<FGuid> EquipIntentOrder;
	};
	/** Internal transport only: native property encoding with logical references, never loads or source pointers. */
	struct FTransportArchive final : FArchiveProxy
	{
		TMap<FString, UObject*>& Bindings;
		const TMap<const UObject*, FString>& Keys;
		FTransportArchive(FArchive& Inner, TMap<FString, UObject*>& InBindings, const TMap<const UObject*, FString>& InKeys)
			: FArchiveProxy(Inner), Bindings(InBindings), Keys(InKeys) { ArNoDelta = true; }
		virtual void Serialize(void* Data, int64 Num) override
		{
			if (Num < 0 || Tell() > MaximumBytes - Num) { SetError(); return; }
			FArchiveProxy::Serialize(Data, Num);
		}
		virtual FArchive& operator<<(FName& Name) override
		{ FString Value = IsSaving() ? Name.ToString() : FString(); *this << Value; if (IsLoading()) Name = FName(*Value); return *this; }
		virtual FArchive& operator<<(UObject*& Object) override
		{
			FString Key;
			if (IsSaving() && Object)
			{
				if (!IsValid(Object)) { SetError(); return *this; }
				if (const FString* Logical = Keys.Find(Object)) Key = *Logical;
				else if (Object->IsAsset() || Object->IsA<UClass>()) { Key = Object->GetPathName(); Bindings.Add(Key, Object); }
				else { SetError(); return *this; }
			}
			*this << Key;
			if (IsLoading())
			{
				Object = Key.IsEmpty() ? nullptr : Bindings.FindRef(Key);
				if (!Key.IsEmpty() && !IsValid(Object)) SetError();
			}
			return *this;
		}
		virtual FArchive& operator<<(FObjectPtr& Object) override
		{ UObject* Value = Object.Get(); *this << Value; if (IsLoading()) Object = Value; return *this; }
		virtual FArchive& operator<<(FWeakObjectPtr& Object) override
		{ UObject* Value = Object.Get(); *this << Value; if (IsLoading()) Object = Value; return *this; }
		virtual FArchive& operator<<(FSoftObjectPath& Object) override
		{
			// Actor/subobject soft paths embed a world/object name. This bounded transport supports asset paths;
			// graph references must use the captured logical hard/weak reference contract instead.
			if (IsSaving() && !Object.GetSubPathString().IsEmpty()) { SetError(); return *this; }
			FString Path = IsSaving() ? Object.ToString() : FString(); *this << Path;
			if (IsLoading()) { Object = FSoftObjectPath(Path); if (!Object.GetSubPathString().IsEmpty()) SetError(); }
			return *this;
		}
		virtual FArchive& operator<<(FSoftObjectPtr& Object) override
		{ FSoftObjectPath Path = Object.ToSoftObjectPath(); *this << Path; if (IsLoading()) Object = FSoftObjectPtr(Path); return *this; }
		using FArchiveProxy::operator<<;
	};
	FArrayProperty* ItemsProperty()
	{
		auto* P = FindFProperty<FArrayProperty>(FACFInventoryList::StaticStruct(), TEXT("Inventory"));
		const auto* Inner = P ? CastField<FStructProperty>(P->Inner) : nullptr;
		return Inner && Inner->Struct == FInventoryItem::StaticStruct() ? P : nullptr;
	}
	FACFInventoryList* InventoryList(UACFEquipmentComponent* Equipment)
	{
		if (!IsValid(Equipment)) return nullptr;
		auto* P = FindFProperty<FStructProperty>(UACFInventoryComponent::StaticClass(), TEXT("InventoryList"));
		return P && P->Struct == FACFInventoryList::StaticStruct() ? P->ContainerPtrToValuePtr<FACFInventoryList>(Equipment) : nullptr;
	}
	TArray<FInventoryItem>* NativeItems(UACFEquipmentComponent* Equipment)
	{
		if (!IsValid(Equipment)) return nullptr;
		FACFInventoryList* List = InventoryList(Equipment);
		return List && ItemsProperty() ? ItemsProperty()->ContainerPtrToValuePtr<TArray<FInventoryItem>>(List) : nullptr;
	}
	TArray<FEquippedItem>* NativeEquipped(UACFEquipmentComponent* Equipment)
	{
		if (!IsValid(Equipment)) return nullptr;
		auto* P = FindFProperty<FStructProperty>(UACFEquipmentComponent::StaticClass(), TEXT("Equipment"));
		auto* A = FindFProperty<FArrayProperty>(FEquipment::StaticStruct(), TEXT("EquippedItems"));
		const auto* Inner = A ? CastField<FStructProperty>(A->Inner) : nullptr;
		return P && P->Struct == FEquipment::StaticStruct() && Inner && Inner->Struct == FEquippedItem::StaticStruct()
			? A->ContainerPtrToValuePtr<TArray<FEquippedItem>>(P->ContainerPtrToValuePtr<FEquipment>(Equipment)) : nullptr;
	}
	const TArray<TObjectPtr<UACFItemFragment>>* NativeFragments(UACFEquipmentComponent* Equipment);
	/**
	 * Clear a just-spawned destination's map-default inventory through ACF's
	 * public lifecycle. The portable source graph is the only authoritative
	 * gameplay state after travel; default map items are not a second input that
	 * may be appended to it.
	 */
	bool ClearNativeEquipment(UACFEquipmentComponent* Equipment, FString& Error)
	{
		Error.Reset();
		if (!IsValid(Equipment) || !IsValid(Equipment->GetOwner()) || !Equipment->GetOwner()->HasAuthority()
			|| !Equipment->GetIsInitialized())
			return Fail(Error, TEXT("Destination ACF equipment cannot be cleared through its native lifecycle."));
		TArray<FInventoryItem>* const Items = NativeItems(Equipment);
		TArray<FEquippedItem>* const Equipped = NativeEquipped(Equipment);
		const TArray<TObjectPtr<UACFItemFragment>>* const Fragments = NativeFragments(Equipment);
		if (!Items || !Equipped || !Fragments)
			return Fail(Error, TEXT("Destination ACF equipment reflection contract is unavailable."));
		Equipment->ClearAllInventoryAndEquipment();
		Equipment->RefreshTotalWeight();
		return Items->IsEmpty() && Equipped->IsEmpty() && Fragments->IsEmpty()
			&& FMath::IsNearlyZero(Equipment->GetCurrentInventoryTotalWeight())
			? true
			: Fail(Error, TEXT("ACF did not clear destination inventory, equipment, fragments and weight atomically."));
	}
	const TArray<TObjectPtr<UACFItemFragment>>* NativeFragments(UACFEquipmentComponent* Equipment)
	{
		if (!IsValid(Equipment)) return nullptr;
		auto* P = FindFProperty<FArrayProperty>(UACFInventoryComponent::StaticClass(), TEXT("RegisteredFragments"));
		const auto* Inner = P ? CastField<FObjectProperty>(P->Inner) : nullptr;
		return Inner && Inner->PropertyClass == UACFItemFragment::StaticClass()
			? P->ContainerPtrToValuePtr<TArray<TObjectPtr<UACFItemFragment>>>(Equipment) : nullptr;
	}
	/**
	 * ACF's public clear/equip lifecycle deliberately broadcasts per-item UI and
	 * equipment events.  During V7 reconstruction those intermediate states are
	 * neither committed gameplay nor a usable player state.  Keep every existing
	 * dynamic binding intact, suppress only this synchronous transaction's native
	 * broadcasts, and restore the exact bindings before the coordinator can
	 * publish or roll back the attempt.
	 */
	struct FScopedACFInventoryEventQuiescence final
	{
		struct FBindings final
		{
			FOnInventoryChanged InventoryChanged;
			FOnItemAdded ItemAdded;
			FOnItemRemoved ItemRemoved;
			FOnCurrencyValueChanged CurrencyChanged;
			FOnEquipmentChanged EquipmentChanged;
			FOnEquippedArmorChanged EquippedArmorChanged;
		};

		TMap<UACFEquipmentComponent*, FBindings> Saved;
		explicit FScopedACFInventoryEventQuiescence(const TMap<FString, UACFEquipmentComponent*>& Equipment)
		{
			Saved.Reserve(Equipment.Num());
			for (const auto& Pair : Equipment)
			{
				UACFEquipmentComponent* const Component = Pair.Value;
				if (!IsValid(Component) || Saved.Contains(Component)) continue;
				FBindings& Bindings = Saved.Add(Component);
				Bindings.InventoryChanged = Component->OnInventoryChanged;
				Bindings.ItemAdded = Component->OnItemAdded;
				Bindings.ItemRemoved = Component->OnItemRemoved;
				Bindings.CurrencyChanged = Component->OnCurrencyChanged;
				Bindings.EquipmentChanged = Component->OnEquipmentChanged;
				Bindings.EquippedArmorChanged = Component->OnEquippedArmorChanged;
				Component->OnInventoryChanged.Clear();
				Component->OnItemAdded.Clear();
				Component->OnItemRemoved.Clear();
				Component->OnCurrencyChanged.Clear();
				Component->OnEquipmentChanged.Clear();
				Component->OnEquippedArmorChanged.Clear();
			}
		}
		~FScopedACFInventoryEventQuiescence()
		{
			for (const auto& Pair : Saved)
			{
				UACFEquipmentComponent* const Component = Pair.Key;
				if (!IsValid(Component)) continue;
				const FBindings& Bindings = Pair.Value;
				Component->OnInventoryChanged = Bindings.InventoryChanged;
				Component->OnItemAdded = Bindings.ItemAdded;
				Component->OnItemRemoved = Bindings.ItemRemoved;
				Component->OnCurrencyChanged = Bindings.CurrencyChanged;
				Component->OnEquipmentChanged = Bindings.EquipmentChanged;
				Component->OnEquippedArmorChanged = Bindings.EquippedArmorChanged;
			}
		}
		FScopedACFInventoryEventQuiescence(const FScopedACFInventoryEventQuiescence&) = delete;
		FScopedACFInventoryEventQuiescence& operator=(const FScopedACFInventoryEventQuiescence&) = delete;
	};
	/**
	 * This is intentionally a diagnostic, not a second validation path.  The
	 * destination player is constructed by the map before the detached graph is
	 * reconstructed, so a generic "equipment contract" message made it
	 * impossible to distinguish normal component initialization from an
	 * unexpected native baseline.  Keep every value local and read-only: the
	 * caller still owns the actual fail-closed decision.
	 */
	FString DescribeDestinationEquipmentContract(const FString& Key, AActor* Actor,
		UACFEquipmentComponent* Component, const FPortableObject* Contract)
	{
		FACFInventoryList* const List = Component ? InventoryList(Component) : nullptr;
		TArray<FInventoryItem>* const Items = Component ? NativeItems(Component) : nullptr;
		TArray<FEquippedItem>* const Equipped = Component ? NativeEquipped(Component) : nullptr;
		const TArray<TObjectPtr<UACFItemFragment>>* const Fragments = Component ? NativeFragments(Component) : nullptr;
		const FString ExpectedClass = Contract && IsValid(Contract->Class) ? Contract->Class->GetPathName() : TEXT("<missing>");
		const FString ActualClass = Component && IsValid(Component->GetClass()) ? Component->GetClass()->GetPathName() : TEXT("<missing>");
		const FString ActorPath = IsValid(Actor) ? Actor->GetPathName() : TEXT("<missing>");
		const FString ListOwner = List && IsValid(List->GetActorOwner()) ? List->GetActorOwner()->GetPathName() : TEXT("<missing>");
		return FString::Printf(TEXT("Destination equipment contract invalid for %s: actor=%s expected_class=%s actual_class=%s initialized=%d replicated=%d inventory_list=%d list_owner=%s items=%d equipped=%d fragments=%d."),
			*Key, *ActorPath, *ExpectedClass, *ActualClass, Component && Component->GetIsInitialized(),
			Component && Component->GetIsReplicated(), List != nullptr, *ListOwner,
			Items ? Items->Num() : INDEX_NONE, Equipped ? Equipped->Num() : INDEX_NONE,
			Fragments ? Fragments->Num() : INDEX_NONE);
	}
	template <typename ItemType>
	void CopyIdentities(const TArray<ItemType>& Source, TArray<ItemType>& Destination)
	{
		// UE5.8's FFastArraySerializerItem copy deliberately resets these fields.
		Destination = Source;
		for (int32 Index = 0; Index < Source.Num(); ++Index)
		{
			Destination[Index].ReplicationID = Source[Index].ReplicationID;
			Destination[Index].ReplicationKey = Source[Index].ReplicationKey;
			Destination[Index].MostRecentArrayReplicationKey = Source[Index].MostRecentArrayReplicationKey;
		}
	}
}

struct FProjectCalystoGameplaySnapshot::FState
{
	FGuid Request;
	TWeakObjectPtr<UWorld> World;
	AActor* Player = nullptr;
	UProjectRunCompanionSubsystem* RosterOwner = nullptr;
	UProjectCalystoFloorOutcomeSubsystem* OutcomeOwner = nullptr;
	TMap<FGuid, UProjectRunCompanionSubsystem::FRuntimeCompanionRecord> Roster;
	TArray<TObjectPtr<UClass>> RetainedClasses;
	int64 RunEpoch = 0, Floor = 0, Generation = 0;
	bool RosterReady = false, FloorReady = false, TravelActive = false;
	int64 OutcomeSeed = 0, OutcomeEpoch = 0, OutcomeFloor = 0;
	double ReadySeconds = -1;
	int32 Deaths = 0, Failures = 0;
	TArray<ProjectCalystoSnapshot::FInventoryState> Inventories;
	TArray<ProjectCalystoSnapshot::FObjectState> Objects;
	TArray<TObjectPtr<UObject>> References;
	TMap<const UObject*, FString> Keys;
	FString Hash;
	bool Released = false;
	bool Accepted = false, Prepared = false, Detached = false, CleanupFailed = false;
	TArray<ProjectCalystoSnapshot::FPortableObject> PortableObjects;
	TArray<ProjectCalystoSnapshot::FPortableInventory> PortableInventories;
	TMap<FString, UObject*> PortableAssets;
	TMap<FGuid, TPair<FString, FString>> PortableRosterActors;
	int64 Work = 0;

	bool CaptureObject(UObject* Object, bool Equipment, FString& Error)
	{
		using namespace ProjectCalystoSnapshot;
		if (!IsValid(Object) || Objects.Num() >= MaximumItems + MaximumFragments + MaximumInventories)
			return Fail(Error, TEXT("Snapshot object identity/count is invalid."));
		FObjectState Record; Record.Object = Object; Record.Outer = Object->GetOuter(); Record.Class = Object->GetClass();
		References.AddUnique(Object); References.AddUnique(Record.Class); References.AddUnique(Record.Outer);
		TArray<FProperty*> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			if ((It->HasAnyPropertyFlags(CPF_SaveGame) && !(Equipment && It->GetFName() == TEXT("InventoryList")))
				|| (Equipment && It->GetFName() == TEXT("currentInventoryWeight"))
				|| (Object->IsA<UACFItem>() && (It->GetFName() == TEXT("Fragments") || It->GetFName() == TEXT("ItemInfo")))) Properties.Add(*It);
		Properties.Sort([](const FProperty& A, const FProperty& B) { return A.GetName() < B.GetName(); });
		if (Properties.Num() > 1024) return Fail(Error, TEXT("Snapshot property count exceeds its finite bound."));
		for (FProperty* Property : Properties)
		{
			const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (!Bounded(Property, static_cast<const uint8*>(Value) + Index * Property->GetElementSize(), Work))
					return Fail(Error, TEXT("Saved property graph exceeds its finite bound."));
			Record.Values.Add(MakeUnique<FValue>(Property, Value));
		}
		Objects.Add(MoveTemp(Record)); return true;
	}
	bool CaptureInventory(AActor* Actor, const FString& Key, FString& Error)
	{
		using namespace ProjectCalystoSnapshot;
		if (!LiveActor(Actor) || Actor->GetWorld() != World.Get() || !Actor->HasAuthority() || Inventories.Num() >= MaximumInventories)
			return Fail(Error, TEXT("Snapshot requires bounded authoritative original inventory actors in one world."));
		auto* Equipment = Actor->FindComponentByClass<UACFEquipmentComponent>();
		if (!Equipment || !Equipment->GetIsInitialized() || !Equipment->GetIsReplicated() || !NativeItems(Equipment)
			|| !NativeEquipped(Equipment) || !NativeFragments(Equipment) || InventoryList(Equipment)->GetActorOwner() != Actor)
			return Fail(Error, TEXT("Actual initialized ACF equipment and exact reflected inventory contract are required."));
		if (Keys.Contains(Equipment)) return Fail(Error, TEXT("Two roster identities cannot own the same equipment."));
		FInventoryState I; I.Equipment = Equipment; I.Owner = Actor; I.Key = Key;
		if (NativeItems(Equipment)->Num() > MaximumItems || NativeEquipped(Equipment)->Num() > MaximumItems
			|| NativeFragments(Equipment)->Num() > MaximumFragments) return Fail(Error, TEXT("Native inventory graph exceeds snapshot bounds."));
		CopyIdentities(*NativeItems(Equipment), I.Items); CopyIdentities(*NativeEquipped(Equipment), I.Equipped);
		for (auto Fragment : *NativeFragments(Equipment)) I.Fragments.Add(Fragment.Get());
		int32 TotalItems = I.Items.Num(), TotalFragments = I.Fragments.Num();
		for (const auto& Prior : Inventories) { TotalItems += Prior.Items.Num(); TotalFragments += Prior.Fragments.Num(); }
		if (TotalItems > MaximumItems || TotalFragments > MaximumFragments || I.Equipped.Num() > I.Items.Num())
			return Fail(Error, TEXT("Actual inventory/equipment graph exceeds its finite snapshot bound."));
		Keys.Add(Actor, Key); Keys.Add(Equipment, Key + TEXT("/Equipment")); References.AddUnique(Actor);
		TSet<FGuid> ItemIds; TSet<UACFItemFragment*> ExpectedFragments;
		for (const auto& Item : I.Items)
		{
			if (!Item.GetItemGuid().IsValid() || ItemIds.Contains(Item.GetItemGuid()) || Item.Count <= 0
				|| !IsValid(Item.Item) || Item.Item->GetClass() != Item.ItemClass.Get() || Item.Item->GetItemOwner() != Actor
				|| Keys.Contains(Item.Item) || Item.Item->Fragments.Num() > MaximumFragments)
				return Fail(Error, TEXT("Actual item GUID/count/class/owner or shared instance is invalid."));
			ItemIds.Add(Item.GetItemGuid()); const FString ItemKey = Key + TEXT("/Item/") + Item.GetItemGuid().ToString(EGuidFormats::Digits);
			Keys.Add(Item.Item, ItemKey);
			for (int32 Index = 0; Index < Item.Item->Fragments.Num(); ++Index)
			{
				UACFItemFragment* Fragment = Item.Item->Fragments[Index];
				if (!IsValid(Fragment) || Fragment->GetOuter() != Actor || ExpectedFragments.Contains(Fragment) || Keys.Contains(Fragment))
					return Fail(Error, TEXT("Actual fragment identity/owner is invalid or shared across items."));
				ExpectedFragments.Add(Fragment); Keys.Add(Fragment, ItemKey + FString::Printf(TEXT("/Fragment/%d"), Index));
				if (!CaptureObject(Fragment, false, Error)) return false;
			}
			if (!CaptureObject(Item.Item, false, Error)) return false;
		}
		if (ExpectedFragments.Num() != I.Fragments.Num()) return Fail(Error, TEXT("Registered fragments differ from the actual item graph."));
		for (auto* Fragment : I.Fragments) if (!ExpectedFragments.Remove(Fragment)) return Fail(Error, TEXT("Fragment registry has missing or duplicate entries."));
		for (const auto& Equipped : I.Equipped)
		{
			const auto* Item = I.Items.FindByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Equipped.ItemGuid; });
			if (!Item || !Item->bIsEquipped || Item->EquipmentSlot != Equipped.ItemSlot || Item->Item != Equipped.Item
				|| Item->ItemClass != Equipped.ItemClass) return Fail(Error, TEXT("Realized equipment differs from the actual inventory flags."));
			if (Equipped.ItemActor)
			{
				if (!LiveActor(Equipped.ItemActor) || Equipped.ItemActor->GetWorld() != World.Get()
					|| Equipped.ItemActor->GetItemOwner() != Actor || Equipped.ItemActor->GetItemDefinition() != Item->Item
					|| Keys.Contains(Equipped.ItemActor)) return Fail(Error, TEXT("Realized equipment actor has an invalid or shared owner/definition."));
				Keys.Add(Equipped.ItemActor, Key + TEXT("/Equipped/") + Equipped.ItemGuid.ToString(EGuidFormats::Digits));
				References.AddUnique(Equipped.ItemActor);
			}
		}
		for (const auto& Item : I.Items) if (Item.bIsEquipped && !I.Equipped.ContainsByPredicate([&](const auto& E) { return E.ItemGuid == Item.GetItemGuid(); }))
			return Fail(Error, TEXT("An equipped inventory item lacks realized equipment."));
		if (!CaptureObject(Equipment, true, Error)) return false;
		Inventories.Add(MoveTemp(I)); return true;
	}
	bool OwnersValid(FGuid Id, FString& Error) const
	{
		using namespace ProjectCalystoSnapshot;
		if (!IsInGameThread() || Released || Id != Request || !World.IsValid() || !LiveActor(Player)
			|| Player->GetWorld() != World.Get() || !IsValid(RosterOwner) || !IsValid(OutcomeOwner)
			|| RosterOwner->GameplaySnapshotRequest != Request || OutcomeOwner->GameplaySnapshotRequest != Request
			|| RosterOwner->RunEpoch != RunEpoch || RosterOwner->CurrentGenerationSerial != Generation
			|| OutcomeOwner->TrackedRunEpoch != OutcomeEpoch || RosterOwner->RevivalTransaction.bActive
			|| RosterOwner->GetGameInstance() != World->GetGameInstance() || OutcomeOwner->GetGameInstance() != World->GetGameInstance())
			return Fail(Error, TEXT("Unrecoverable snapshot ownership/world loss; rollback is not clean."));
		return true;
	}
	bool TopologyValid(FString& Error) const
	{
		using namespace ProjectCalystoSnapshot;
		for (const auto& O : Objects) if (!IsValid(O.Object) || O.Object->GetOuter() != O.Outer || O.Object->GetClass() != O.Class)
			return Fail(Error, TEXT("An original item/fragment object was destroyed or changed owner/class; rollback is not clean."));
		for (const auto& I : Inventories)
		{
			if (!LiveActor(I.Owner) || I.Owner->GetWorld() != World.Get() || !IsValid(I.Equipment)
				|| I.Owner->FindComponentByClass<UACFEquipmentComponent>() != I.Equipment || !I.Equipment->GetIsInitialized()
				|| !I.Owner->HasAuthority() || !I.Equipment->GetIsReplicated() || !NativeItems(I.Equipment)
				|| !NativeEquipped(I.Equipment) || !NativeFragments(I.Equipment) || InventoryList(I.Equipment)->GetActorOwner() != I.Owner)
				return Fail(Error, TEXT("An original authoritative equipment owner is unavailable; rollback is not clean."));
			const auto& Current = *NativeEquipped(I.Equipment);
			if (Current.Num() != I.Equipped.Num()) return Fail(Error, TEXT("Realized equipment topology changed; same-world rollback cannot recreate equipment actors."));
			for (int32 Index = 0; Index < Current.Num(); ++Index)
			{
				const auto& A = Current[Index]; const auto& B = I.Equipped[Index];
				if (A.ItemGuid != B.ItemGuid || A.ItemSlot != B.ItemSlot || A.Item != B.Item || A.ItemClass != B.ItemClass
					|| A.ItemActor != B.ItemActor || A.ReplicationID != B.ReplicationID
					|| (B.ItemActor && (!LiveActor(B.ItemActor) || B.ItemActor->GetItemOwner() != I.Owner
						|| B.ItemActor->GetWorld() != World.Get() || B.ItemActor->GetItemDefinition() != B.Item)))
					return Fail(Error, TEXT("Realized equipment identity changed; rollback is not clean."));
			}
			for (const auto& Item : I.Items) if (Item.Item->GetItemOwner() != I.Owner)
				return Fail(Error, TEXT("An original item owner changed; rollback is not clean."));
			if (NativeItems(I.Equipment)->Num() > MaximumItems || NativeFragments(I.Equipment)->Num() > MaximumFragments)
				return Fail(Error, TEXT("Current inventory graph exceeds rollback bounds."));
			for (auto Fragment : *NativeFragments(I.Equipment)) if (!IsValid(Fragment) || Fragment->GetOuter() != I.Owner)
				return Fail(Error, TEXT("Current fragment registry cannot be safely restored."));
		}
		return true;
	}
	FString Canonical(bool Frozen, FString& Error)
	{
		using namespace ProjectCalystoSnapshot;
		TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FCanonicalArchive Ar(Writer, Keys, Frozen ? &References : nullptr);
		FString Schema = TEXT("ProjectCalystoGameplaySnapshot"); Ar << Schema;
		// Request, run epoch and generation serial are lease/routing metadata, never canonical gameplay inputs.
		int64 ActiveFloor = Frozen ? Floor : RosterOwner->CurrentFloor; Ar << ActiveFloor;
		// Operational readiness and world-time origins are checked separately, never RNG/gameplay hash inputs.
		const auto& Records = Frozen ? Roster : RosterOwner->Roster;
		TArray<FGuid> Ids; Records.GetKeys(Ids); Ids.Sort([](const auto& A, const auto& B) { return A.ToString() < B.ToString(); });
		int32 Count = Ids.Num(); if (Count > 128) { Fail(Error, TEXT("Current roster exceeds snapshot bound.")); return {}; } Ar << Count;
		for (FGuid Id : Ids)
		{
			const auto& Record = Records.FindChecked(Id); Ar << Id;
			// Project roster fields are intentionally not SaveGame-tagged. They still belong to this typed snapshot.
			auto Snapshot = Record.Snapshot; Ar.ArIsSaveGame = false;
			FProjectCompanionRunEntrySnapshot::StaticStruct()->SerializeItem(Ar, &Snapshot, nullptr); Ar.ArIsSaveGame = true;
			bool Desired = Record.bDesiredActiveParty; Ar << Desired;
			UObject* Live = Record.LiveActor.Get(); UObject* Corpse = Record.CorpseActor.Get(); Ar << Live << Corpse;
		}
		int64 Seed = Frozen ? OutcomeSeed : OutcomeOwner->TrackedRunSeed, OutcomeFloorValue = Frozen ? OutcomeFloor : OutcomeOwner->TrackedFloorNumber;
		int32 DeathCount = Frozen ? Deaths : OutcomeOwner->FloorDeaths, FailureCount = Frozen ? Failures : OutcomeOwner->FloorFailures;
		Ar << Seed << OutcomeFloorValue << DeathCount << FailureCount;
		for (const auto& I : Inventories)
		{
			FString Key = I.Key; Ar << Key;
			auto Items = Frozen ? I.Items : *NativeItems(I.Equipment);
			if (Items.Num() > MaximumItems) { Fail(Error, TEXT("Current inventory exceeds snapshot bound.")); return {}; }
			Items.Sort([](const auto& A, const auto& B) { return A.GetItemGuid().ToString() < B.GetItemGuid().ToString(); });
			int32 ItemCount = Items.Num(); Ar << ItemCount;
			for (auto& Item : Items) FInventoryItem::StaticStruct()->SerializeItem(Ar, &Item, nullptr);
			// Registry order is not semantic, but missing/extra/duplicate actual registrations are.
			TArray<FString> FragmentKeys;
			if (Frozen) for (auto* Fragment : I.Fragments) FragmentKeys.Add(Keys.FindChecked(Fragment));
			else for (auto Fragment : *NativeFragments(I.Equipment))
			{
				const FString* FragmentKey = Keys.Find(Fragment.Get());
				if (!FragmentKey) { Fail(Error, TEXT("Actual fragment registry differs from the captured object graph.")); return {}; }
				FragmentKeys.Add(*FragmentKey);
			}
			FragmentKeys.Sort(); Ar << FragmentKeys;
		}
		int64 CurrentWork = 0;
		for (const auto& O : Objects)
		{
			FString Key = Keys.FindChecked(O.Object), ClassPath = O.Class->GetPathName(); Ar << Key << ClassPath;
			for (const auto& V : O.Values)
			{
				if (O.Object->IsA<UACFEquipmentComponent>() && V->Property->GetFName() == TEXT("bIsInitialized")) continue;
				void* Value = Frozen ? V->Data : V->Property->ContainerPtrToValuePtr<void>(O.Object);
				for (int32 Index = 0; Index < V->Property->ArrayDim; ++Index)
					if (!Bounded(V->Property, static_cast<uint8*>(Value) + Index * V->Property->GetElementSize(), CurrentWork))
					{ Fail(Error, TEXT("Current saved state exceeds its finite bound.")); return {}; }
				FString Name = V->Property->GetName(); Ar << Name;
				// Each complete top-level value was selected explicitly; include every nested field that restoration copies.
				Ar.ArIsSaveGame = false;
				for (int32 Index = 0; Index < V->Property->ArrayDim; ++Index)
				{
					WriteValue(Ar, V->Property, static_cast<uint8*>(Value) + Index * V->Property->GetElementSize());
				}
				Ar.ArIsSaveGame = true;
			}
		}
		if (Ar.IsError() || Writer.IsError() || Bytes.IsEmpty()) { Fail(Error, TEXT("Canonical snapshot contains unsupported object references or exceeds its byte bound.")); return {}; }
		return FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString();
	}
	bool PersistentOwnersValid(FGuid Id, FString& Error) const
	{
		using namespace ProjectCalystoSnapshot;
		if (!IsInGameThread() || Released || CleanupFailed || Id != Request || !IsValid(RosterOwner) || !IsValid(OutcomeOwner)
			|| RosterOwner->GameplaySnapshotRequest != Request || OutcomeOwner->GameplaySnapshotRequest != Request
			|| RosterOwner->RunEpoch != RunEpoch || OutcomeOwner->TrackedRunEpoch != OutcomeEpoch
			|| RosterOwner->RevivalTransaction.bActive || RosterOwner->GetGameInstance() != OutcomeOwner->GetGameInstance())
			return Fail(Error, TEXT("Portable snapshot lost its persistent native ownership lease."));
		if (RosterOwner->Roster.Num() != Roster.Num() || RosterOwner->CurrentFloor != Floor
			|| RosterOwner->RetainedCompanionClasses != RetainedClasses || OutcomeOwner->TrackedRunSeed != OutcomeSeed
			|| OutcomeOwner->TrackedFloorNumber != OutcomeFloor || OutcomeOwner->FloorDeaths != Deaths || OutcomeOwner->FloorFailures != Failures)
			return Fail(Error, TEXT("Persistent roster/outcomes changed during unpublished travel."));
		for (const auto& Pair : Roster)
		{
			const auto* Actual = RosterOwner->Roster.Find(Pair.Key);
			if (!Actual || Actual->bDesiredActiveParty != Pair.Value.bDesiredActiveParty
				|| !FProjectCompanionRunEntrySnapshot::StaticStruct()->CompareScriptStruct(&Actual->Snapshot, &Pair.Value.Snapshot, 0))
				return Fail(Error, TEXT("Persistent companion definition changed during unpublished travel."));
		}
		return true;
	}
	void ClearGraph()
	{
		Objects.Reset(); Inventories.Reset(); References.Reset(); Keys.Reset();
		PortableObjects.Reset(); PortableInventories.Reset(); PortableAssets.Reset(); PortableRosterActors.Reset();
		Roster.Reset(); RetainedClasses.Reset(); Player = nullptr; World.Reset();
	}
};

FProjectCalystoGameplaySnapshot::FProjectCalystoGameplaySnapshot() : State(MakeUnique<FState>()) {}
FProjectCalystoGameplaySnapshot::~FProjectCalystoGameplaySnapshot()
{
	// Lease completion is an explicit provider operation while its world and
	// persistent owners are still alive.  A shared snapshot can outlive PIE and
	// be destroyed after UObject teardown, where querying a TWeakObjectPtr would
	// assert inside FUObjectArray.  Destruction therefore only releases native
	// memory; it never observes or mutates UObjects.
}

TSharedPtr<FProjectCalystoGameplaySnapshot> FProjectCalystoGameplaySnapshot::Capture(FGuid RequestId, UWorld* World, AActor* Player,
	UProjectRunCompanionSubsystem* Roster, UProjectCalystoFloorOutcomeSubsystem* Outcomes, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!IsInGameThread() || !RequestId.IsValid() || !IsValid(World) || !World->IsGameWorld() || !World->GetGameInstance()
		|| !LiveActor(Player) || Player->GetWorld() != World || !Player->HasAuthority() || !IsValid(Roster) || !IsValid(Outcomes)
		|| Roster->GetGameInstance() != World->GetGameInstance() || Outcomes->GetGameInstance() != World->GetGameInstance()
		|| Roster->GameplaySnapshotRequest.IsValid() || Outcomes->GameplaySnapshotRequest.IsValid() || Roster->Roster.Num() > 128
		|| Roster->RevivalTransaction.bActive)
	{ Fail(Error, TEXT("Snapshot requires exact authoritative world/player/owners, no competing lease or revival, and a bounded roster.")); return nullptr; }
	TSharedPtr<FProjectCalystoGameplaySnapshot> Result = MakeShareable(new FProjectCalystoGameplaySnapshot()); auto& S = *Result->State;
	S.Request = RequestId; S.World = World; S.Player = Player; S.RosterOwner = Roster; S.OutcomeOwner = Outcomes;
	S.References = {Player, Roster, Outcomes}; S.Roster = Roster->Roster; S.RetainedClasses = Roster->RetainedCompanionClasses;
	S.RunEpoch = Roster->RunEpoch; S.Floor = Roster->CurrentFloor; S.Generation = Roster->CurrentGenerationSerial;
	S.RosterReady = Roster->bCompanionRosterReady; S.FloorReady = Roster->bFloorReady; S.TravelActive = Roster->bGenerationOrTravelActive;
	S.OutcomeSeed = Outcomes->TrackedRunSeed; S.OutcomeEpoch = Outcomes->TrackedRunEpoch; S.OutcomeFloor = Outcomes->TrackedFloorNumber;
	S.ReadySeconds = Outcomes->FloorReadySeconds; S.Deaths = Outcomes->FloorDeaths; S.Failures = Outcomes->FloorFailures;
	if (!FMath::IsFinite(S.ReadySeconds) || S.Deaths < 0 || S.Failures < 0 || !S.CaptureInventory(Player, TEXT("Player"), Error)) return nullptr;
	TArray<FGuid> Ids; S.Roster.GetKeys(Ids); Ids.Sort([](const auto& A, const auto& B) { return A.ToString() < B.ToString(); });
	for (FGuid Id : Ids)
	{
		const auto& Record = S.Roster.FindChecked(Id);
		if (!Id.IsValid() || Record.Snapshot.Definition.StableCompanionId != Id
			|| (Record.bDesiredActiveParty && Record.Snapshot.State == EProjectCompanionRunState::Alive && !Record.LiveActor.IsValid()))
		{ Fail(Error, TEXT("Actual roster identity or desired active projection is missing.")); return nullptr; }
		const FString Key = TEXT("Companion/") + Id.ToString(EGuidFormats::Digits);
		if (Record.LiveActor.IsValid() && !S.CaptureInventory(Record.LiveActor.Get(), Key + TEXT("/Live"), Error)) return nullptr;
		if (Record.CorpseActor.IsValid() && Record.CorpseActor.Get() != Record.LiveActor.Get()
			&& !S.CaptureInventory(Record.CorpseActor.Get(), Key + TEXT("/Corpse"), Error)) return nullptr;
	}
	// Inventory array order is not an object identity. Sort the bounded records in place,
	// preserving their captured values and replication identities without another property copy.
	S.Objects.Sort([&S](const FObjectState& A, const FObjectState& B)
	{
		return S.Keys.FindChecked(A.Object) < S.Keys.FindChecked(B.Object);
	});
	S.Hash = S.Canonical(true, Error); if (S.Hash.IsEmpty()) return nullptr;
	Roster->GameplaySnapshotRequest = Outcomes->GameplaySnapshotRequest = RequestId;
	return Result;
}

FString FProjectCalystoGameplaySnapshot::GetCanonicalHash() const { return State ? State->Hash : FString(); }
bool FProjectCalystoGameplaySnapshot::IsDetachedForTravel() const { return State && State->Detached; }
bool FProjectCalystoGameplaySnapshot::VerifyUnchanged(FGuid RequestId, FString& Error) const
{
	Error.Reset();
	if (!State || !State->OwnersValid(RequestId, Error) || !State->TopologyValid(Error)) return false;
	if (State->RosterOwner->bCompanionRosterReady != State->RosterReady || State->RosterOwner->bFloorReady != State->FloorReady
		|| State->RosterOwner->bGenerationOrTravelActive != State->TravelActive || State->OutcomeOwner->FloorReadySeconds != State->ReadySeconds)
		return ProjectCalystoSnapshot::Fail(Error, TEXT("Operational readiness changed; canonical gameplay identity is unaffected."));
	if (State->RosterOwner->RetainedCompanionClasses != State->RetainedClasses)
		return ProjectCalystoSnapshot::Fail(Error, TEXT("Captured companion class retention differs."));
	for (const auto& I : State->Inventories)
	{
		const auto& Current = *ProjectCalystoSnapshot::NativeItems(I.Equipment);
		if (Current.Num() != I.Items.Num()) return ProjectCalystoSnapshot::Fail(Error, TEXT("Actual inventory membership differs."));
		for (const auto& Item : I.Items)
		{
			const auto* Actual = Current.FindByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Item.GetItemGuid(); });
			if (!Actual || Actual->Item != Item.Item || Actual->ReplicationID != Item.ReplicationID
				|| Actual->LastObservedCount != Item.LastObservedCount)
				return ProjectCalystoSnapshot::Fail(Error, TEXT("Actual inventory object/FastArray identity or observed count differs."));
		}
	}
	return State->Canonical(false, Error) == State->Hash ? true
		: ProjectCalystoSnapshot::Fail(Error, TEXT("Actual pre-floor inventory/roster/outcome state differs from the immutable snapshot."));
}

bool FProjectCalystoGameplaySnapshot::Restore(FGuid RequestId, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!State || !State->OwnersValid(RequestId, Error) || !State->TopologyValid(Error)) return false;
	if (VerifyUnchanged(RequestId, Error)) return true;
	// Validate all original/current owners and reflection contracts before the first mutation.
	for (const auto& I : State->Inventories) if (!InventoryList(I.Equipment) || !ItemsProperty())
		return Fail(Error, TEXT("Native inventory property contract changed; rollback is not clean."));
	for (const auto& O : State->Objects) for (const auto& V : O.Values)
		V->Property->CopyCompleteValue(V->Property->ContainerPtrToValuePtr<void>(O.Object), V->Data);
	for (const auto& I : State->Inventories)
	{
		for (auto* Fragment : I.Equipment->GetRegisteredFragments()) if (!I.Equipment->UnregisterFragment(Fragment))
			return Fail(Error, TEXT("Unrecoverable native fragment unregister failure during rollback."));
		FACFInventoryList* List = InventoryList(I.Equipment);
		CopyIdentities(I.Items, *NativeItems(I.Equipment));
		List->MarkArrayDirty();
		// Assigned replication IDs survive rollback. Unassigned entries must remain unassigned until native publication.
		for (auto& Item : *NativeItems(I.Equipment)) if (Item.ReplicationID != INDEX_NONE) List->MarkItemDirty(Item);
		for (auto* Fragment : I.Fragments) if (!I.Equipment->RegisterFragment(Fragment))
			return Fail(Error, TEXT("Unrecoverable native fragment register failure during rollback."));
	}
	auto* R = State->RosterOwner; auto* O = State->OutcomeOwner;
	R->Roster = State->Roster; R->RetainedCompanionClasses = State->RetainedClasses;
	R->RunEpoch = State->RunEpoch; R->CurrentFloor = State->Floor; R->CurrentGenerationSerial = State->Generation;
	R->bCompanionRosterReady = State->RosterReady; R->bFloorReady = State->FloorReady; R->bGenerationOrTravelActive = State->TravelActive;
	O->TrackedRunSeed = State->OutcomeSeed; O->TrackedRunEpoch = State->OutcomeEpoch; O->TrackedFloorNumber = State->OutcomeFloor;
	O->FloorReadySeconds = State->ReadySeconds; O->FloorDeaths = State->Deaths; O->FloorFailures = State->Failures;
	if (VerifyUnchanged(RequestId, Error)) return true;
	Error = TEXT("Unrecoverable rollback post-verification failure: ") + Error;
	return false;
}

bool FProjectCalystoGameplaySnapshot::Release(FGuid RequestId, FString& Error)
{
	if (!State) return ProjectCalystoSnapshot::Fail(Error, TEXT("Snapshot lease is absent."));
	if (State->Released) return RequestId == State->Request && !State->Accepted;
	if (!VerifyUnchanged(RequestId, Error)) return false;
	State->RosterOwner->GameplaySnapshotRequest.Invalidate(); State->OutcomeOwner->GameplaySnapshotRequest.Invalidate();
	State->Released = true; State->ClearGraph(); State->RosterOwner = nullptr; State->OutcomeOwner = nullptr;
	return true;
}

bool FProjectCalystoGameplaySnapshot::PrepareForTravel(FGuid RequestId, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!VerifyUnchanged(RequestId, Error)) return false;
	if (State->Prepared) return true;
	// Equipped state is frozen as an intent plus its realized ACF projection.  Reconstruction stages
	// the items unequipped, invokes ACF's native lifecycle, then reapplies only the already realized
	// transport values.  This keeps fragments, item actors and equipment delegates authoritative.
	for (const auto& I : State->Inventories)
	{
		if (!FMath::IsFinite(I.Equipment->GetCurrentCurrencyAmount()) || I.Equipment->GetCurrentCurrencyAmount() < 0)
			return Fail(Error, TEXT("Portable native currency is malformed."));
		TSet<FGuid> SelectedIds, RealizedIds;
		for (const auto& Item : I.Items)
		{
			if (Item.bIsEquipped != Item.EquipmentSlot.IsValid())
				return Fail(Error, TEXT("Portable equipped item flags and slots must agree exactly."));
			if (Item.bIsEquipped)
			{
				if (!Item.GetItemGuid().IsValid() || SelectedIds.Contains(Item.GetItemGuid()) || !Item.ItemClass || !IsValid(Item.Item))
					return Fail(Error, TEXT("Portable equipped inventory identity/class is malformed."));
				SelectedIds.Add(Item.GetItemGuid());
			}
			if (!FMath::IsFinite(Item.Item->GetItemInfo().ItemWeight) || Item.Item->GetItemInfo().ItemWeight < 0
				|| !FMath::IsFinite(Item.DropChancePercentage) || Item.DropChancePercentage < 0 || Item.DropChancePercentage > 100)
				return Fail(Error, TEXT("Portable native item weight/drop state is malformed before source teardown."));
		}
		for (const FEquippedItem& Equipped : I.Equipped)
		{
			const FInventoryItem* Selected = I.Items.FindByPredicate([&](const FInventoryItem& Item)
			{
				return Item.GetItemGuid() == Equipped.ItemGuid;
			});
			if (!Equipped.ItemGuid.IsValid() || RealizedIds.Contains(Equipped.ItemGuid) || !Selected || !Selected->bIsEquipped
				|| Selected->EquipmentSlot != Equipped.ItemSlot || Selected->Item != Equipped.Item
				|| Selected->ItemClass != Equipped.ItemClass || !Equipped.Item || !Equipped.ItemClass
				|| Equipped.Item->GetClass() != Equipped.ItemClass.Get())
				return Fail(Error, TEXT("Portable realized equipment does not exactly match its selected inventory item."));
			RealizedIds.Add(Equipped.ItemGuid);
			if (Equipped.ItemActor && (!LiveActor(Equipped.ItemActor) || Equipped.ItemActor->GetWorld() != State->World.Get()
				|| Equipped.ItemActor->GetItemOwner() != I.Owner || Equipped.ItemActor->GetItemDefinition() != Equipped.Item))
				return Fail(Error, TEXT("Portable realized equipment actor is malformed before source teardown."));
		}
		if (SelectedIds.Num() != RealizedIds.Num())
			return Fail(Error, TEXT("Every selected equipped inventory item requires one realized native equipment entry."));
	}
	TArray<FPortableObject> Records; TArray<FPortableInventory> Inventories; TMap<FString, UObject*> Assets;
	int64 TotalBytes = 0;
	for (const auto& O : State->Objects)
	{
		FPortableObject Record; Record.Key = State->Keys.FindChecked(O.Object); Record.Class = O.Class;
		Record.bEquipment = O.Object->IsA<UACFEquipmentComponent>();
		const FString* OwnerKey = State->Keys.Find(O.Outer);
		if (!OwnerKey || O.Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			return Fail(Error, TEXT("Portable object requires an exact concrete loaded class and logical actor outer."));
		Record.OwnerKey = *OwnerKey; Assets.Add(O.Class->GetPathName(), O.Class);
		for (const auto& V : O.Values)
		{
			FPortableValue Value; Value.Property = V->Property->GetFName();
			FMemoryWriter Writer(Value.Bytes); FTransportArchive Ar(Writer, Assets, State->Keys);
			FStructuredArchiveFromArchive Structured(Ar);
			for (int32 Index = 0; Index < V->Property->ArrayDim; ++Index)
				V->Property->SerializeItem(Structured.GetSlot(), static_cast<uint8*>(V->Data) + Index * V->Property->GetElementSize(), nullptr);
			TotalBytes += Value.Bytes.Num();
			if (Ar.IsError() || Writer.IsError() || TotalBytes > MaximumBytes)
				return Fail(Error, TEXT("Portable property closure is unsupported or exceeds the aggregate 16 MiB bound."));
			Record.Values.Add(MoveTemp(Value));
		}
		Records.Add(MoveTemp(Record));
	}
	for (const auto& I : State->Inventories)
	{
		FPortableInventory Record; Record.Key = I.Key;
		for (const auto& Item : I.Items)
		{
			auto Copy = Item; TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FTransportArchive Ar(Writer, Assets, State->Keys);
			Ar.ArIsSaveGame = true; FInventoryItem::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
			TotalBytes += Bytes.Num();
			if (Ar.IsError() || Writer.IsError() || TotalBytes > MaximumBytes)
				return Fail(Error, TEXT("Portable inventory closure exceeds its aggregate bound."));
			Record.Items.Add(MoveTemp(Bytes));
		}
		for (const FEquippedItem& Equipped : I.Equipped)
		{
			auto Copy = Equipped; TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FTransportArchive Ar(Writer, Assets, State->Keys);
			Ar.ArIsSaveGame = false; FEquippedItem::StaticStruct()->SerializeItem(Ar, &Copy, nullptr);
			TotalBytes += Bytes.Num();
			if (Ar.IsError() || Writer.IsError() || TotalBytes > MaximumBytes)
				return Fail(Error, TEXT("Portable realized equipment closure exceeds its aggregate bound."));
			Record.Equipped.Add(MoveTemp(Bytes));
			Record.EquipIntentOrder.Add(Equipped.ItemGuid);
		}
		Inventories.Add(MoveTemp(Record));
	}
	State->PortableObjects = MoveTemp(Records); State->PortableInventories = MoveTemp(Inventories); State->PortableAssets = MoveTemp(Assets);
	for (const auto& Pair : State->Roster)
		State->PortableRosterActors.Add(Pair.Key, {State->Keys.FindRef(Pair.Value.LiveActor.Get()), State->Keys.FindRef(Pair.Value.CorpseActor.Get())});
	for (const auto& Asset : State->PortableAssets) State->References.AddUnique(Asset.Value);
	State->Prepared = true; return true;
}

bool FProjectCalystoGameplaySnapshot::DetachForTravel(FGuid RequestId, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!State || !State->Prepared) return Fail(Error, TEXT("Prepare the immutable portable graph before the actual travel boundary."));
	if (State->Detached) return State->PersistentOwnersValid(RequestId, Error);
	if (!State->PersistentOwnersValid(RequestId, Error) || !State->TopologyValid(Error)) return false;
	for (const auto& I : State->Inventories)
	{
		const auto& Current = *NativeItems(I.Equipment);
		if (Current.Num() != I.Items.Num()) return Fail(Error, TEXT("Actual inventory membership changed before travel."));
		for (const auto& Item : I.Items)
		{
			const auto* Actual = Current.FindByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Item.GetItemGuid(); });
			if (!Actual || Actual->Item != Item.Item || Actual->ReplicationID != Item.ReplicationID || Actual->LastObservedCount != Item.LastObservedCount)
				return Fail(Error, TEXT("Actual native item identity changed before travel."));
		}
	}
	if (State->Canonical(false, Error) != State->Hash) return Fail(Error, TEXT("Canonical gameplay changed before actual world teardown."));
	// No source inventory mutation. The caller owns actual world travel; cancellation before this handshake
	// retains the original same-world Verify/Restore API. After it, the immutable graph can reconstruct only.
	State->Objects.Reset(); State->Inventories.Reset(); State->Keys.Reset(); State->References.Reset();
	State->References.Add(State->RosterOwner); State->References.Add(State->OutcomeOwner);
	for (const auto& Asset : State->PortableAssets) State->References.AddUnique(Asset.Value);
	for (auto& Pair : State->Roster) { Pair.Value.LiveActor.Reset(); Pair.Value.CorpseActor.Reset(); }
	State->Player = nullptr; State->World.Reset(); State->Detached = true; return true;
}

bool FProjectCalystoGameplaySnapshot::ValidateDestinationLease(FGuid RequestId, FString& Error) const
{
	using namespace ProjectCalystoSnapshot;
	Error.Reset();
	if (!State || !State->Detached || !State->PersistentOwnersValid(RequestId, Error))
		return Fail(Error, Error.IsEmpty()
			? TEXT("Destination reconstruction requires an owned detached portable graph.") : *Error);
	if (State->PortableInventories.IsEmpty() || !State->PortableInventories.ContainsByPredicate(
		[](const FPortableInventory& Inventory) { return Inventory.Key == TEXT("Player"); }))
		return Fail(Error, TEXT("The detached portable graph does not retain its exact player inventory binding."));
	return true;
}

bool FProjectCalystoGameplaySnapshot::GetDestinationRequirements(FGuid RequestId,
	TArray<FProjectCalystoInventoryDestinationRequirement>& OutRequirements, FString& Error) const
{
	using namespace ProjectCalystoSnapshot;
	OutRequirements.Reset();
	if (!ValidateDestinationLease(RequestId, Error)) return false;
	TSet<FString> InventoryKeys;
	for (const FPortableInventory& Inventory : State->PortableInventories)
	{
		if (Inventory.Key.IsEmpty() || InventoryKeys.Contains(Inventory.Key))
			return Fail(Error, TEXT("The detached portable inventory graph has an empty or duplicated logical owner."));
		InventoryKeys.Add(Inventory.Key);
	}
	OutRequirements.Add({FGuid(), false});
	TArray<FGuid> Ids;
	State->Roster.GetKeys(Ids);
	Ids.Sort([](const FGuid& A, const FGuid& B) { return A.ToString(EGuidFormats::Digits) < B.ToString(EGuidFormats::Digits); });
	for (const FGuid& Id : Ids)
	{
		const TPair<FString, FString>* Projection = State->PortableRosterActors.Find(Id);
		if (!Projection) return Fail(Error, TEXT("The detached portable roster projection contract is absent."));
		if (!Projection->Key.IsEmpty())
		{
			if (!InventoryKeys.Contains(Projection->Key)) return Fail(Error, TEXT("The detached live companion projection lacks its exact inventory binding."));
			OutRequirements.Add({Id, false});
		}
		if (!Projection->Value.IsEmpty() && Projection->Value != Projection->Key)
		{
			if (!InventoryKeys.Contains(Projection->Value)) return Fail(Error, TEXT("The detached corpse projection lacks its exact inventory binding."));
			OutRequirements.Add({Id, true});
		}
	}
	if (OutRequirements.Num() != State->PortableInventories.Num())
		return Fail(Error, TEXT("The detached inventory owner count does not match its exact player/companion projection requirements."));
	return true;
}

bool FProjectCalystoGameplaySnapshot::ReconstructInWorld(FGuid RequestId, UWorld* DestinationWorld,
	TConstArrayView<FProjectCalystoInventoryDestination> Destinations, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!State || !State->Detached || !State->PersistentOwnersValid(RequestId, Error))
		return Fail(Error, Error.IsEmpty() ? TEXT("Reconstruction requires an owned detached portable graph.") : *Error);
	if (!IsValid(DestinationWorld) || !DestinationWorld->IsGameWorld()
		|| DestinationWorld->GetGameInstance() != State->RosterOwner->GetGameInstance()
		|| Destinations.Num() != State->PortableInventories.Num() || Destinations.Num() > MaximumInventories)
		return Fail(Error, TEXT("Destination world, persistent game instance or exact inventory binding count is invalid."));
	TMap<FString, UObject*> Bindings = State->PortableAssets;
	TMap<const UObject*, FString> Keys;
	TMap<FString, AActor*> Actors;
	TMap<FString, UACFEquipmentComponent*> Equipment;
	struct FDestinationEquipmentBaseline
	{
		FString Key;
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UACFEquipmentComponent> Component;
		TWeakObjectPtr<UClass> ComponentClass;
		FName ComponentName = NAME_None;
		FString CanonicalHash;
		TArray<uint8> Capsule;
		bool bMutationStarted = false;
	};
	TMap<FString, FDestinationEquipmentBaseline> DestinationBaselines;
	for (const auto& Destination : Destinations)
	{
		const FString Key = Destination.CompanionId.IsValid()
			? TEXT("Companion/") + Destination.CompanionId.ToString(EGuidFormats::Digits) + (Destination.bCorpse ? TEXT("/Corpse") : TEXT("/Live")) : TEXT("Player");
		AActor* Actor = Destination.Actor;
		if ((!Destination.CompanionId.IsValid() && Destination.bCorpse) || !LiveActor(Actor) || !Actor->HasAuthority()
			|| Actor->GetWorld() != DestinationWorld || Keys.Contains(Actor) || Actors.Contains(Key)
			|| (Destination.CompanionId.IsValid() && (!State->Roster.Contains(Destination.CompanionId) || (!Destination.bCorpse && !Cast<AACFCharacter>(Actor))))
			|| !State->PortableInventories.ContainsByPredicate([&](const auto& I) { return I.Key == Key; }))
			return Fail(Error, TEXT("Destination actor identity/authority is missing, duplicated or not owned by the captured roster."));
		auto* Component = Actor->FindComponentByClass<UACFEquipmentComponent>();
		const auto* Contract = State->PortableObjects.FindByPredicate([&](const auto& O) { return O.bEquipment && O.OwnerKey == Key; });
		if (!Component || !Contract || Component->GetClass() != Contract->Class || !Component->GetIsInitialized()
			|| !Component->GetIsReplicated() || !NativeItems(Component) || !NativeEquipped(Component) || !NativeFragments(Component)
			|| InventoryList(Component)->GetActorOwner() != Actor)
		{
			Error = DescribeDestinationEquipmentContract(Key, Actor, Component, Contract);
			return false;
		}
		Actors.Add(Key, Actor); Equipment.Add(Key, Component);
		Bindings.Add(Key, Actor); Keys.Add(Actor, Key);
		Bindings.Add(Key + TEXT("/Equipment"), Component); Keys.Add(Component, Key + TEXT("/Equipment"));
	}
	if (!Actors.Contains(TEXT("Player"))) return Fail(Error, TEXT("The sole captured player binding is required."));
	// DungeonGeneration initializes the Player blueprint's default equipment
	// before V7 receives the detached run. Freeze that destination baseline now,
	// while it is still intact, so a later realization failure can restore it
	// exactly rather than leaving a cleared or partially reconstructed player.
	for (const auto& Pair : Equipment)
	{
		AActor* const Actor = Actors.FindChecked(Pair.Key);
		UACFEquipmentComponent* const Component = Pair.Value;
		FDestinationEquipmentBaseline& Baseline = DestinationBaselines.Add(Pair.Key);
		Baseline.Key = Pair.Key;
		Baseline.Actor = Actor;
		Baseline.Component = Component;
		Baseline.ComponentClass = Component->GetClass();
		Baseline.ComponentName = Component->GetFName();
		Baseline.CanonicalHash = State->RosterOwner->ComputeInventoryHash(Component);
		FString BaselineError;
		if (Baseline.CanonicalHash.IsEmpty()
			|| !State->RosterOwner->VerifyRestoredEquipment(Component, Baseline.CanonicalHash, BaselineError)
			|| !State->RosterOwner->SerializeEquipmentCapsule(Component, Baseline.Capsule, BaselineError)
			|| Baseline.Capsule.IsEmpty() || Baseline.Capsule.Num() > MaximumBytes)
		{
			if (BaselineError.IsEmpty())
			{
				BaselineError = Baseline.Capsule.IsEmpty()
					? TEXT("The destination ACF baseline capsule is empty.")
					: TEXT("The destination ACF baseline capsule exceeds the finite V7 transport bound.");
			}
			Error = TEXT("Destination ACF baseline cannot be frozen before V7 reconstruction: ") + BaselineError;
			return false;
		}
	}
	// Allocate the entire graph before reading references. Only retained, already loaded native classes/assets are used.
	TArray<TStrongObjectPtr<UObject>> StagedOwnership;
	for (const auto& O : State->PortableObjects)
	{
		if (!IsValid(O.Class) || O.Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| !Actors.Contains(O.OwnerKey)) return Fail(Error, TEXT("Portable class/outer dependency is unavailable."));
		if (O.bEquipment) continue;
		if (!O.Class->IsChildOf(UACFItem::StaticClass()) && !O.Class->IsChildOf(UACFItemFragment::StaticClass()))
			return Fail(Error, TEXT("Portable object is outside the native item/fragment contract."));
		if (Bindings.Contains(O.Key)) return Fail(Error, TEXT("Portable logical identity is duplicated."));
		UObject* Object = NewObject<UObject>(Actors.FindChecked(O.OwnerKey), O.Class);
		if (!Object) return Fail(Error, TEXT("Native destination object allocation failed before publication."));
		StagedOwnership.Emplace(Object); Bindings.Add(O.Key, Object); Keys.Add(Object, O.Key);
		if (auto* Item = Cast<UACFItem>(Object)) Item->SetItemOwner(Actors.FindChecked(O.OwnerKey));
	}
	struct FEquipmentValue
	{
		UObject* Object = nullptr;
		FProperty* Property = nullptr;
		const TArray<uint8>* Bytes = nullptr;
		TUniquePtr<FValue> After;
	};
	TArray<FEquipmentValue> EquipmentValues;
	int64 DecodeWork = 0, TotalBytes = 0;
	for (const auto& O : State->PortableObjects)
	{
		UObject* Object = Bindings.FindRef(O.Key);
		if (!IsValid(Object) || Object->GetClass() != O.Class) return Fail(Error, TEXT("Portable class identity mismatch."));
		for (const auto& V : O.Values)
		{
			FProperty* Property = FindFProperty<FProperty>(O.Class, V.Property);
			TotalBytes += V.Bytes.Num();
			if (!Property || TotalBytes > MaximumBytes || V.Bytes.IsEmpty()) return Fail(Error, TEXT("Portable property schema/size is invalid."));
			void* Current = Property->ContainerPtrToValuePtr<void>(Object);
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (!Bounded(Property, static_cast<uint8*>(Current) + Index * Property->GetElementSize(), DecodeWork))
					return Fail(Error, TEXT("Destination baseline exceeds the finite property bound."));
			if (O.bEquipment)
			{
				// FEquipment can contain the source equipment actor references.  Defer decoding it
				// until ACF has created and bound the destination actor through native equip.
				FEquipmentValue& Deferred = EquipmentValues.AddDefaulted_GetRef();
				Deferred.Object = Object; Deferred.Property = Property; Deferred.Bytes = &V.Bytes;
				continue;
			}
			auto Value = MakeUnique<FValue>(Property, Current);
			FMemoryReader Reader(V.Bytes); FTransportArchive Ar(Reader, Bindings, Keys); FStructuredArchiveFromArchive Structured(Ar);
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				Property->SerializeItem(Structured.GetSlot(), static_cast<uint8*>(Value->Data) + Index * Property->GetElementSize(), nullptr);
			if (Ar.IsError() || Reader.IsError() || Reader.Tell() != V.Bytes.Num()) return Fail(Error, TEXT("Portable property graph could not be decoded exactly."));
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (!Bounded(Property, static_cast<uint8*>(Value->Data) + Index * Property->GetElementSize(), DecodeWork))
					return Fail(Error, TEXT("Decoded portable graph exceeds its finite property bound."));
			Property->CopyCompleteValue(Current, Value->Data);
		}
	}
	TMap<FString, TArray<FInventoryItem>> Inventories, EquipIntents;
	// Keep a bounded, local trace of the exact native equip handoff.  ACF owns
	// the actual FEquippedItem creation, so a later mismatch must show whether
	// the divergence occurred at that API call or after the complete handoff.
	TMap<FString, FString> FrozenEquipIntentTrace, StagedNativeInventoryTrace, ImmediateNativeEquipTrace;
	const auto DescribeEquipmentProjection = [](const TArray<FEquippedItem>* Projection)
	{
		if (!Projection) return FString(TEXT("<unavailable>"));
		TArray<FString> Rows;
		Rows.Reserve(Projection->Num());
		for (const FEquippedItem& Entry : *Projection)
		{
			Rows.Add(FString::Printf(TEXT("guid=%s slot=%s item=%s class=%s actor=%s"),
				*Entry.ItemGuid.ToString(EGuidFormats::Digits), *Entry.ItemSlot.ToString(),
				*GetNameSafe(Entry.Item), *GetNameSafe(Entry.ItemClass.Get()), *GetNameSafe(Entry.ItemActor)));
		}
		Rows.Sort();
		return FString::Join(Rows, TEXT(" | "));
	};
	const auto DescribeInventoryProjection = [](const TArray<FInventoryItem>* Projection)
	{
		if (!Projection) return FString(TEXT("<unavailable>"));
		TArray<FString> Rows;
		Rows.Reserve(Projection->Num());
		for (const FInventoryItem& Entry : *Projection)
		{
			Rows.Add(FString::Printf(TEXT("guid=%s equipped=%d slot=%s item=%s class=%s"),
				*Entry.GetItemGuid().ToString(EGuidFormats::Digits), Entry.bIsEquipped,
				*Entry.EquipmentSlot.ToString(), *GetNameSafe(Entry.Item), *GetNameSafe(Entry.ItemClass.Get())));
		}
		Rows.Sort();
		return FString::Join(Rows, TEXT(" | "));
	};
	TSet<UACFItemFragment*> Fragments;
	int32 TotalItems = 0;
	for (const auto& I : State->PortableInventories)
	{
		auto* Component = Equipment.FindRef(I.Key); AActor* Actor = Actors.FindRef(I.Key);
		auto* SlotsProperty = FindFProperty<FIntProperty>(UACFInventoryComponent::StaticClass(), TEXT("MaxInventorySlots"));
		auto* WeightProperty = FindFProperty<FFloatProperty>(UACFInventoryComponent::StaticClass(), TEXT("MaxInventoryWeight"));
		if (!Component || !Actor || !SlotsProperty || !WeightProperty || I.Items.Num() > SlotsProperty->GetPropertyValue_InContainer(Component))
			return Fail(Error, TEXT("Destination inventory slot capacity is unavailable."));
		const float Capacity = WeightProperty->GetPropertyValue_InContainer(Component);
		double Weight = 0.; TSet<FGuid> Ids; TSet<UACFItem*> ItemObjects; TArray<FInventoryItem> Items, Intents;
		for (const auto& Bytes : I.Items)
		{
			TotalBytes += Bytes.Num();
			if (++TotalItems > MaximumItems || TotalBytes > MaximumBytes) return Fail(Error, TEXT("Portable item/byte count exceeds its aggregate bound."));
			FInventoryItem Item; FMemoryReader Reader(Bytes); FTransportArchive Ar(Reader, Bindings, Keys);
			Ar.ArIsSaveGame = true; FInventoryItem::StaticStruct()->SerializeItem(Ar, &Item, nullptr);
			if (Ar.IsError() || Reader.IsError() || Reader.Tell() != Bytes.Num() || !Item.GetItemGuid().IsValid()
				|| Ids.Contains(Item.GetItemGuid()) || Item.Count <= 0 || !IsValid(Item.Item) || ItemObjects.Contains(Item.Item)
				|| Item.Item->GetOuter() != Actor || Item.Item->GetItemOwner() != Actor || Item.Item->GetClass() != Item.ItemClass.Get()
				|| Item.bIsEquipped != Item.EquipmentSlot.IsValid()
				|| Keys.FindRef(Item.Item) != I.Key + TEXT("/Item/") + Item.GetItemGuid().ToString(EGuidFormats::Digits))
				return Fail(Error, TEXT("Portable item GUID/class/count/equipped intent or logical ownership is malformed."));
			Ids.Add(Item.GetItemGuid()); ItemObjects.Add(Item.Item);
			const float UnitWeight = Item.Item->GetItemInfo().ItemWeight;
			if (!FMath::IsFinite(UnitWeight) || UnitWeight < 0) return Fail(Error, TEXT("Portable native item weight is invalid."));
			Weight += static_cast<double>(UnitWeight) * Item.Count;
			for (int32 Index = 0; Index < Item.Item->Fragments.Num(); ++Index)
			{
				auto* Fragment = Item.Item->Fragments[Index];
				if (!IsValid(Fragment) || Fragment->GetOuter() != Actor || Fragments.Contains(Fragment) || Fragments.Num() >= MaximumFragments
					|| Keys.FindRef(Fragment) != Keys.FindChecked(Item.Item) + FString::Printf(TEXT("/Fragment/%d"), Index))
					return Fail(Error, TEXT("Portable fragment ownership/order is missing or duplicated."));
				Fragments.Add(Fragment);
			}
			if (Item.bIsEquipped) Intents.Add(Item);
			// The native ACF call below owns the flags, slot, actor construction and fragment effects.
			// Do not copy the source selected state directly into an uninitialized destination component.
			Item.bIsEquipped = false; Item.EquipmentSlot = FGameplayTag(); Item.LastObservedCount = Item.Count;
			Items.Add(MoveTemp(Item));
		}
		if (!FMath::IsFinite(Capacity) || Capacity < 0 || !FMath::IsFinite(Weight) || Weight > Capacity)
			return Fail(Error, TEXT("Destination native inventory weight capacity is unavailable."));
		Inventories.Add(I.Key, MoveTemp(Items)); EquipIntents.Add(I.Key, MoveTemp(Intents));
	}
	if (TotalItems + Fragments.Num() + Equipment.Num() != State->PortableObjects.Num())
		return Fail(Error, TEXT("Portable object graph contains orphan or unaccounted native identities."));
	// Baselines are copied before publication. RegisterFragment only registers replication subobjects in ACF;
	// neither it nor direct property writes invoke ApplyFragment, inventory/equipment delegates or outcomes.
	const auto RosterBefore = State->RosterOwner->Roster;
	auto Candidate = MakeUnique<FState>(); Candidate->Request = RequestId; Candidate->World = DestinationWorld;
	Candidate->Player = Actors.FindChecked(TEXT("Player")); Candidate->RosterOwner = State->RosterOwner; Candidate->OutcomeOwner = State->OutcomeOwner;
	Candidate->Roster = State->Roster; Candidate->RetainedClasses = State->RetainedClasses;
	Candidate->RunEpoch = State->RunEpoch; Candidate->Floor = State->Floor; Candidate->Generation = State->RosterOwner->CurrentGenerationSerial;
	Candidate->RosterReady = State->RosterOwner->bCompanionRosterReady; Candidate->FloorReady = State->RosterOwner->bFloorReady;
	Candidate->TravelActive = State->RosterOwner->bGenerationOrTravelActive;
	Candidate->OutcomeSeed = State->OutcomeSeed; Candidate->OutcomeEpoch = State->OutcomeEpoch; Candidate->OutcomeFloor = State->OutcomeFloor;
	Candidate->ReadySeconds = State->OutcomeOwner->FloorReadySeconds; Candidate->Deaths = State->Deaths; Candidate->Failures = State->Failures;
	Candidate->References = {Candidate->Player, Candidate->RosterOwner, Candidate->OutcomeOwner};
	for (auto& Pair : Candidate->Roster)
	{
		const auto* Projection = State->PortableRosterActors.Find(Pair.Key);
		if (!Projection) return Fail(Error, TEXT("Portable roster projection edges are absent."));
		Pair.Value.LiveActor = Cast<AACFCharacter>(Actors.FindRef(Projection->Key));
		Pair.Value.CorpseActor = Actors.FindRef(Projection->Value);
	}
	auto Undo = [&]()
	{
		bool Clean = true;
		for (const auto& Pair : DestinationBaselines)
		{
			const FDestinationEquipmentBaseline& Baseline = Pair.Value;
			if (!Baseline.bMutationStarted) continue;
			AActor* const ExpectedActor = Actors.FindRef(Baseline.Key);
			UACFEquipmentComponent* const ExpectedComponent = Equipment.FindRef(Baseline.Key);
			FString CleanupError;
			if (!Baseline.Actor.IsValid() || !Baseline.Component.IsValid()
				|| !IsValid(ExpectedActor) || !IsValid(ExpectedComponent)
				|| Baseline.Actor.Get() != ExpectedActor || Baseline.Component.Get() != ExpectedComponent
				|| ExpectedComponent->GetOwner() != ExpectedActor
				|| ExpectedComponent->GetClass() != Baseline.ComponentClass.Get()
				|| ExpectedComponent->GetFName() != Baseline.ComponentName
				|| !State->RosterOwner->RestoreTypedEquipmentCapsule(
					ExpectedComponent, Baseline.Capsule, Baseline.CanonicalHash, CleanupError, false)
				|| !State->RosterOwner->VerifyRestoredEquipment(ExpectedComponent, Baseline.CanonicalHash, CleanupError))
			{
				Clean = false;
				if (Error.IsEmpty())
					Error = CleanupError.IsEmpty()
						? TEXT("Destination ACF baseline identity changed during rollback.") : CleanupError;
				if (IsValid(ExpectedComponent))
				{
					if (const TArray<FEquippedItem>* FailedProjection = NativeEquipped(ExpectedComponent))
						for (const FEquippedItem& Entry : *FailedProjection)
							if (Entry.ItemActor) State->References.AddUnique(Entry.ItemActor);
				}
			}
		}
		State->RosterOwner->Roster = RosterBefore;
		for (const auto& Pair : DestinationBaselines)
		{
			const FDestinationEquipmentBaseline& Baseline = Pair.Value;
			if (!Baseline.bMutationStarted) continue;
			UACFEquipmentComponent* const Component = Equipment.FindRef(Baseline.Key);
			FString VerifyError;
			const bool bVerified = IsValid(Component)
				&& State->RosterOwner->VerifyRestoredEquipment(Component, Baseline.CanonicalHash, VerifyError);
			Clean &= bVerified;
			if (!bVerified && Error.IsEmpty())
				Error = VerifyError.IsEmpty()
					? TEXT("Destination ACF baseline verification failed after rollback.") : VerifyError;
		}
		if (!Clean)
		{
			State->CleanupFailed = true;
			for (const auto& Pair : Actors) State->References.AddUnique(Pair.Value);
			for (const auto& Object : StagedOwnership) State->References.AddUnique(Object.Get());
			Error = TEXT("Unrecoverable destination rollback failure; cleanup is not clean: ") + Error;
		}
		return false;
	};
	FScopedACFInventoryEventQuiescence EventQuiescence(Equipment);
	// A map default is never a second inventory. Clear it only after every
	// portable input, capacity and destination owner has passed preflight. Undo
	// restores the frozen baseline capsule before it reports any late failure.
	for (auto& Pair : DestinationBaselines)
	{
		FDestinationEquipmentBaseline& Baseline = Pair.Value;
		UACFEquipmentComponent* const Component = Equipment.FindChecked(Baseline.Key);
		const TArray<FInventoryItem>* ExistingItems = NativeItems(Component);
		const TArray<FEquippedItem>* ExistingEquipped = NativeEquipped(Component);
		const TArray<TObjectPtr<UACFItemFragment>>* ExistingFragments = NativeFragments(Component);
		if (!ExistingItems || !ExistingEquipped || !ExistingFragments)
		{
			Error = TEXT("Destination ACF equipment reflection contract disappeared before native baseline clearing.");
			return Undo();
		}
		// Even an empty map-default component will receive staged V7 objects
		// below. Mark it before the first possible write so every late failure
		// restores the exact frozen baseline rather than leaving a partial graph.
		Baseline.bMutationStarted = true;
		if (!ExistingItems->IsEmpty() || !ExistingEquipped->IsEmpty() || !ExistingFragments->IsEmpty())
		{
			FString ClearError;
			if (!ClearNativeEquipment(Component, ClearError))
			{
				Error = TEXT("Destination map-default inventory could not be cleared before V7 reconstruction: ") + ClearError;
				return Undo();
			}
		}
	}
	// Stage all inventory/fragment objects first.  Their equipment flags remain clear until the
	// public ACF lifecycle has accepted each frozen selection below.
	for (const auto& I : State->PortableInventories)
	{
		auto* Component = Equipment.FindChecked(I.Key); CopyIdentities(Inventories.FindChecked(I.Key), *NativeItems(Component));
		for (const auto& Item : *NativeItems(Component)) for (auto* Fragment : Item.Item->Fragments)
			if (!Component->RegisterFragment(Fragment)) { Error = TEXT("Native destination fragment registration failed."); return Undo(); }
		Component->RefreshTotalWeight();
	}
	// Recreate equipped items only through ACF.  This is the same typed lifecycle used by the
	// companion travel capsule: it constructs item actors and applies/cleans fragments itself.
	for (const auto& I : State->PortableInventories)
	{
		auto* Component = Equipment.FindChecked(I.Key);
		TArray<FInventoryItem> Intents = EquipIntents.FindChecked(I.Key);
		if (I.EquipIntentOrder.Num() != Intents.Num())
		{
			Error = TEXT("Captured native equipment order does not match the frozen selected inventory set.");
			return Undo();
		}
		TArray<FInventoryItem> OrderedIntents;
		OrderedIntents.Reserve(Intents.Num());
		TSet<FGuid> OrderedIds;
		for (const FGuid& CapturedId : I.EquipIntentOrder)
		{
			const FInventoryItem* const Intent = Intents.FindByPredicate([&](const FInventoryItem& Candidate)
			{
				return Candidate.GetItemGuid() == CapturedId;
			});
			if (!CapturedId.IsValid() || OrderedIds.Contains(CapturedId) || !Intent)
			{
				Error = TEXT("Captured native equipment order contains an absent or duplicate selected inventory identity.");
				return Undo();
			}
			OrderedIds.Add(CapturedId);
			OrderedIntents.Add(*Intent);
		}
		Intents = MoveTemp(OrderedIntents);
		TArray<FString> IntentRows;
		IntentRows.Reserve(Intents.Num());
		for (const FInventoryItem& Intent : Intents)
		{
			IntentRows.Add(FString::Printf(TEXT("guid=%s slot=%s item=%s class=%s"),
				*Intent.GetItemGuid().ToString(EGuidFormats::Digits), *Intent.EquipmentSlot.ToString(),
				*GetNameSafe(Intent.Item), *GetNameSafe(Intent.ItemClass.Get())));
		}
		IntentRows.Sort();
		FrozenEquipIntentTrace.Add(I.Key, FString::Join(IntentRows, TEXT(" | ")));
		StagedNativeInventoryTrace.Add(I.Key, DescribeInventoryProjection(NativeItems(Component)));
		for (const FInventoryItem& Intent : Intents)
		{
			const FInventoryItem* Item = NativeItems(Component)->FindByPredicate([&](const FInventoryItem& Entry)
			{
				return Entry.GetItemGuid() == Intent.GetItemGuid();
			});
			if (!Item || Item->bIsEquipped || Item->EquipmentSlot.IsValid())
			{
				Error = TEXT("Native destination inventory cannot stage the frozen equipment selection.");
				return Undo();
			}
			Component->EquipItemFromInventoryInSlot(*Item, Intent.EquipmentSlot);
		}
		ImmediateNativeEquipTrace.Add(I.Key, DescribeEquipmentProjection(NativeEquipped(Component)));
	}
	// Bind only actor keys for transport decoding. Candidate capture deliberately owns its own
	// logical-key map and will validate the same actors before publishing the transaction.
	for (const auto& I : State->PortableInventories)
	{
		auto* Component = Equipment.FindChecked(I.Key); const TArray<FInventoryItem>& Intents = EquipIntents.FindChecked(I.Key);
		const TArray<FEquippedItem>* Realized = NativeEquipped(Component);
		if (!Realized || Realized->Num() != Intents.Num())
		{
			Error = TEXT("ACF realized equipment count differs from the frozen selected inventory set.");
			return Undo();
		}
		TSet<FGuid> RealizedIds;
		for (const FEquippedItem& Entry : *Realized)
		{
			const FInventoryItem* Actual = NativeItems(Component)->FindByPredicate([&](const FInventoryItem& Item)
			{
				return Item.GetItemGuid() == Entry.ItemGuid;
			});
			// EquipItemFromInventoryInSlot owns the intermediate FInventoryItem copy and
			// the item actor it creates.  Verify that native result against its destination
			// inventory here; the immutable source projection is decoded and compared after
			// actor bindings exist below.  Comparing a transient intent copy at this point
			// is invalid because ACF may replace that copy while retaining the exact frozen
			// GUID in its native inventory and equipped projection.
			if (!Entry.ItemGuid.IsValid() || RealizedIds.Contains(Entry.ItemGuid) || !Actual
				|| !Actual->bIsEquipped || Actual->EquipmentSlot != Entry.ItemSlot
				|| Entry.Item != Actual->Item || Entry.ItemClass != Actual->ItemClass || !Entry.Item || !Entry.ItemClass
				|| Entry.Item->GetClass() != Entry.ItemClass.Get())
			{
				const FString ActualGuid = Actual ? Actual->GetItemGuid().ToString(EGuidFormats::Digits) : TEXT("<missing>");
				Error = FString::Printf(TEXT("ACF did not realize a self-consistent native equipment projection: key=%s entry_guid=%s actual_guid=%s entry_slot=%s actual_slot=%s actual_equipped=%d entry_item=%s actual_item=%s entry_class=%s actual_class=%s."),
					*I.Key, *Entry.ItemGuid.ToString(EGuidFormats::Digits), *ActualGuid,
					*Entry.ItemSlot.ToString(), Actual ? *Actual->EquipmentSlot.ToString() : TEXT("<missing>"), Actual && Actual->bIsEquipped,
					*GetNameSafe(Entry.Item), Actual ? *GetNameSafe(Actual->Item) : TEXT("<missing>"),
					*GetNameSafe(Entry.ItemClass.Get()), Actual ? *GetNameSafe(Actual->ItemClass.Get()) : TEXT("<missing>"));
				return Undo();
			}
			RealizedIds.Add(Entry.ItemGuid);
			if (Entry.ItemActor)
			{
				const FString ActorKey = I.Key + TEXT("/Equipped/") + Entry.ItemGuid.ToString(EGuidFormats::Digits);
				if (!LiveActor(Entry.ItemActor) || Entry.ItemActor->GetWorld() != DestinationWorld
					|| Entry.ItemActor->GetItemOwner() != Actors.FindChecked(I.Key)
					|| Entry.ItemActor->GetItemDefinition() != Entry.Item
					|| (Bindings.Contains(ActorKey) && Bindings.FindChecked(ActorKey) != Entry.ItemActor))
				{
					Error = TEXT("ACF realized equipment actor is unavailable or has an invalid owner/definition.");
					return Undo();
				}
				Bindings.Add(ActorKey, Entry.ItemActor);
			}
		}
		for (const FInventoryItem& Item : *NativeItems(Component))
		{
			const FEquippedItem* Projection = Realized->FindByPredicate([&](const FEquippedItem& Entry)
			{
				return Entry.ItemGuid == Item.GetItemGuid();
			});
			if ((Projection != nullptr) != Item.bIsEquipped || (Projection && Item.EquipmentSlot != Projection->ItemSlot))
			{
				Error = TEXT("ACF inventory equipped flags differ from its realized native projection.");
				return Undo();
			}
		}
	}
	// Decode the source projection only after the native actors have stable destination bindings.
	// This preserves fast-array identities without ever treating a copied flag as an equip operation.
	TMap<FString, TArray<FEquippedItem>> ExpectedEquipment;
	for (const auto& I : State->PortableInventories)
	{
		auto* Component = Equipment.FindChecked(I.Key);
		const TArray<FInventoryItem>& Intents = EquipIntents.FindChecked(I.Key);
		if (I.Equipped.Num() != Intents.Num())
		{
			Error = TEXT("Portable realized equipment count differs from its frozen equipped inventory intent.");
			return Undo();
		}
		TSet<FGuid> ExpectedIds; TArray<FEquippedItem> Expected;
		for (const TArray<uint8>& Bytes : I.Equipped)
		{
			TotalBytes += Bytes.Num();
			if (TotalBytes > MaximumBytes) { Error = TEXT("Portable realized equipment bytes exceed the aggregate bound."); return Undo(); }
			FEquippedItem Saved; FMemoryReader Reader(Bytes); FTransportArchive Ar(Reader, Bindings, Keys);
			Ar.ArIsSaveGame = false; FEquippedItem::StaticStruct()->SerializeItem(Ar, &Saved, nullptr);
			if (Ar.IsError() || Reader.IsError() || Reader.Tell() != Bytes.Num())
			{
				Error = TEXT("Portable realized equipment could not be decoded after native actor construction.");
				return Undo();
			}
			const FEquippedItem* Actual = NativeEquipped(Component)->FindByPredicate([&](const FEquippedItem& Entry)
			{
				return Entry.ItemGuid == Saved.ItemGuid;
			});
			// The source projection is now decoded through the logical destination
			// bindings, including the native actor created by ACF. It is the immutable
			// authority for GUID, slot, item, class and actor identity. Do not compare
			// it with the temporary inventory copy used only to invoke ACF above.
			if (!Saved.ItemGuid.IsValid() || ExpectedIds.Contains(Saved.ItemGuid) || !Actual
				|| Saved.ItemSlot != Actual->ItemSlot || Saved.Item != Actual->Item
				|| Saved.ItemClass != Actual->ItemClass || Saved.ItemActor != Actual->ItemActor)
			{
				TArray<FString> ActualEntries;
				ActualEntries.Reserve(NativeEquipped(Component)->Num());
				for (const FEquippedItem& Entry : *NativeEquipped(Component))
				{
					ActualEntries.Add(FString::Printf(TEXT("guid=%s slot=%s item=%s class=%s actor=%s"),
						*Entry.ItemGuid.ToString(EGuidFormats::Digits), *Entry.ItemSlot.ToString(), *GetNameSafe(Entry.Item),
						*GetNameSafe(Entry.ItemClass.Get()), *GetNameSafe(Entry.ItemActor)));
				}
				ActualEntries.Sort();
				Error = FString::Printf(TEXT("ACF equipment actor/result differs from the exact frozen realized projection: key=%s guid=%s saved_slot=%s actual_slot=%s saved_item=%s actual_item=%s saved_class=%s actual_class=%s saved_actor=%s actual_actor=%s frozen_intents=[%s] staged_inventory=[%s] immediate_native_projection=[%s] actual_projection=[%s]."),
					*I.Key, *Saved.ItemGuid.ToString(EGuidFormats::Digits), *Saved.ItemSlot.ToString(),
					Actual ? *Actual->ItemSlot.ToString() : TEXT("<missing>"), *GetNameSafe(Saved.Item),
					Actual ? *GetNameSafe(Actual->Item) : TEXT("<missing>"), *GetNameSafe(Saved.ItemClass.Get()),
					Actual ? *GetNameSafe(Actual->ItemClass.Get()) : TEXT("<missing>"), *GetNameSafe(Saved.ItemActor),
					Actual ? *GetNameSafe(Actual->ItemActor) : TEXT("<missing>"), *FrozenEquipIntentTrace.FindRef(I.Key), *StagedNativeInventoryTrace.FindRef(I.Key),
					*ImmediateNativeEquipTrace.FindRef(I.Key), *FString::Join(ActualEntries, TEXT(" | ")));
				return Undo();
			}
			ExpectedIds.Add(Saved.ItemGuid); Expected.Add(MoveTemp(Saved));
		}
		ExpectedEquipment.Add(I.Key, MoveTemp(Expected));
	}
	bool bDeferredEquipmentProjection = false;
	for (const FEquipmentValue& Value : EquipmentValues)
	{
		if (Value.Property->GetFName() == TEXT("Equipment"))
		{
			const FStructProperty* Projection = CastField<FStructProperty>(Value.Property);
			if (!Projection || Projection->Struct != FEquipment::StaticStruct())
			{
				Error = TEXT("Portable equipment projection property changed its native FEquipment contract.");
				return Undo();
			}
			bDeferredEquipmentProjection = true;
		}
	}
	if (!bDeferredEquipmentProjection)
	{
		for (const auto& I : State->PortableInventories)
			CopyIdentities(ExpectedEquipment.FindChecked(I.Key), *NativeEquipped(Equipment.FindChecked(I.Key)));
	}
	for (FEquipmentValue& Value : EquipmentValues)
	{
		auto Decoded = MakeUnique<FValue>(Value.Property, Value.Property->ContainerPtrToValuePtr<void>(Value.Object));
		FMemoryReader Reader(*Value.Bytes); FTransportArchive Ar(Reader, Bindings, Keys); FStructuredArchiveFromArchive Structured(Ar);
		for (int32 Index = 0; Index < Value.Property->ArrayDim; ++Index)
			Value.Property->SerializeItem(Structured.GetSlot(), static_cast<uint8*>(Decoded->Data) + Index * Value.Property->GetElementSize(), nullptr);
		if (Ar.IsError() || Reader.IsError() || Reader.Tell() != Value.Bytes->Num())
		{
			Error = TEXT("Deferred native equipment property could not be decoded after actor binding.");
			return Undo();
		}
		for (int32 Index = 0; Index < Value.Property->ArrayDim; ++Index)
			if (!Bounded(Value.Property, static_cast<uint8*>(Decoded->Data) + Index * Value.Property->GetElementSize(), DecodeWork))
			{
				Error = TEXT("Deferred native equipment property exceeds the finite destination bound.");
				return Undo();
			}
		Value.After = MoveTemp(Decoded);
	}
	for (const FEquipmentValue& Value : EquipmentValues)
		Value.After->Property->CopyCompleteValue(Value.After->Property->ContainerPtrToValuePtr<void>(Value.Object), Value.After->Data);
	for (const auto& I : State->PortableInventories)
	{
		const TArray<FEquippedItem>* Actual = NativeEquipped(Equipment.FindChecked(I.Key));
		const TArray<FEquippedItem>& Expected = ExpectedEquipment.FindChecked(I.Key);
		if (!Actual || Actual->Num() != Expected.Num())
		{
			Error = TEXT("Deferred equipment projection changed the native equipped-item count.");
			return Undo();
		}
		for (const FEquippedItem& Saved : Expected)
		{
			const FEquippedItem* Realized = Actual->FindByPredicate([&](const FEquippedItem& Entry)
			{
				return Entry.ItemGuid == Saved.ItemGuid;
			});
			if (!Realized || !FEquippedItem::StaticStruct()->CompareScriptStruct(Realized, &Saved, PPF_None))
			{
				Error = TEXT("Deferred equipment projection did not retain the exact native fast-array identity.");
				return Undo();
			}
		}
	}
	State->RosterOwner->Roster = Candidate->Roster;
	for (const auto& I : State->PortableInventories) if (!Candidate->CaptureInventory(Actors.FindChecked(I.Key), I.Key, Error)) return Undo();
	Candidate->Objects.Sort([&](const FObjectState& A, const FObjectState& B) { return Candidate->Keys.FindChecked(A.Object) < Candidate->Keys.FindChecked(B.Object); });
	Candidate->Hash = Candidate->Canonical(true, Error);
	if (Candidate->Hash != State->Hash || !Candidate->OwnersValid(RequestId, Error) || !Candidate->TopologyValid(Error))
	{ Error = TEXT("Destination canonical graph differs from its immutable source snapshot: ") + Error; return Undo(); }
	// Only a verified graph receives fresh destination-local FastArray replication identities.
	for (auto& I : Candidate->Inventories)
	{
		FACFInventoryList* List = InventoryList(I.Equipment); List->MarkArrayDirty();
		for (auto& Item : *NativeItems(I.Equipment)) List->MarkItemDirty(Item);
		CopyIdentities(*NativeItems(I.Equipment), I.Items);
	}
	State = MoveTemp(Candidate); return true;
}

bool FProjectCalystoGameplaySnapshot::ReleaseAccepted(FGuid RequestId, FString& Error)
{
	using namespace ProjectCalystoSnapshot; Error.Reset();
	if (!State) return Fail(Error, TEXT("Snapshot lease is absent."));
	if (State->Released) return RequestId == State->Request && State->Accepted;
	if (!State->OwnersValid(RequestId, Error) || !State->TopologyValid(Error)) return false;
	// This explicit coordinator boundary validates the actual owned post-acceptance state, not old-state equality.
	// Unknown/new object topology still needs a separately captured bridge graph; do not silently release it.
	if (State->Canonical(false, Error).IsEmpty()) return false;
	State->Accepted = true; State->Released = true;
	State->RosterOwner->GameplaySnapshotRequest.Invalidate(); State->OutcomeOwner->GameplaySnapshotRequest.Invalidate();
	State->ClearGraph(); State->RosterOwner = nullptr; State->OutcomeOwner = nullptr; return true;
}

void FProjectCalystoGameplaySnapshot::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (!State) return;
	Collector.AddReferencedObjects(State->References); Collector.AddReferencedObjects(State->RetainedClasses);
}
