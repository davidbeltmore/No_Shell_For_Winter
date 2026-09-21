#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

FACFInventoryList* EFEquipmentACF::Inventory(UACFInventoryComponent* Component)
{
    static FStructProperty* Property = FindFProperty<FStructProperty>(UACFInventoryComponent::StaticClass(), TEXT("InventoryList"));
    return Component && Property && Property->Struct == FACFInventoryList::StaticStruct()
        ? Property->ContainerPtrToValuePtr<FACFInventoryList>(Component) : nullptr;
}

bool EFEquipmentACF::Capacity(UACFInventoryComponent* Component, double& Out)
{
    static FFloatProperty* Property = FindFProperty<FFloatProperty>(UACFInventoryComponent::StaticClass(), TEXT("MaxInventoryWeight"));
    if (!Component || !Property) return false;
    Out = Property->GetPropertyValue_InContainer(Component);
    return FMath::IsFinite(Out);
}

bool EFEquipmentACF::SetCapacity(UACFInventoryComponent* Component, double Value)
{
    static FFloatProperty* Property = FindFProperty<FFloatProperty>(UACFInventoryComponent::StaticClass(), TEXT("MaxInventoryWeight"));
    if (!Component || !Property || !FMath::IsFinite(Value) || FMath::Abs(Value) > MAX_flt) return false;
    Property->SetPropertyValue_InContainer(Component, static_cast<float>(Value)); return true;
}

bool EFEquipmentACF::ValidateInventory(UACFInventoryComponent* Component, FString& Error)
{
    if (!Component || !Inventory(Component)) { Error = TEXT("The ACF inventory contract is unavailable."); return false; }
    TSet<FGuid> Ids; TSet<UACFItem*> Objects;
    for (const FInventoryItem& Entry : Component->GetInventory())
    {
        if (!Entry.GetItemGuid().IsValid() || Ids.Contains(Entry.GetItemGuid()) || Entry.Count <= 0 || !IsValid(Entry.Item)
            || Entry.Item->GetClass() != Entry.ItemClass.Get() || Objects.Contains(Entry.Item)
            || !FMath::IsFinite(Entry.Item->GetItemInfo().ItemWeight) || Entry.Item->GetItemInfo().ItemWeight < 0)
        { Error = TEXT("Inventory identity, quantity or weight is invalid."); return false; }
        Ids.Add(Entry.GetItemGuid()); Objects.Add(Entry.Item);
    }
    return true;
}

bool EFEquipmentACF::Transfer(UACFInventoryComponent* Source, UACFInventoryComponent* Destination,
    FGuid Guid, int32 Count, bool bIgnoreDestinationWeight, FString& Error)
{
    FInventoryItem Entry;
    if (!Source || !Destination || Source == Destination || !Source->GetOwner()->HasAuthority() || !Destination->GetOwner()->HasAuthority()
        || !Inventory(Source) || !Inventory(Destination) || !Source->GetItemByGuid(Guid, Entry) || !Entry.Item
        || Count <= 0 || Count > Entry.Count || Entry.bIsEquipped)
    { Error = TEXT("Transfer requires an owned, unequipped inventory item and a valid quantity."); return false; }
    if (Destination->GetInventoryListConst().Contains(Guid)) { Error = TEXT("The destination already owns this item."); return false; }
    if (Destination->GetAvailableSlots() < 1) { Error = TEXT("Inventory is full."); return false; }
    double Capacity = 0;
    if (!EFEquipmentACF::Capacity(Destination, Capacity)) { Error = TEXT("Inventory capacity is unavailable."); return false; }
    const double Weight = Entry.Item->GetItemInfo().ItemWeight * static_cast<double>(Count);
    if (!bIgnoreDestinationWeight && Destination->GetCurrentInventoryTotalWeight() + Weight > Capacity + 0.000001)
    { Error = TEXT("The destination cannot carry this weight."); return false; }
    // A partial stateful stack cannot be cloned without a fragment-specific split contract.
    if (Count < Entry.Count && !Entry.Item->Fragments.IsEmpty())
    { Error = TEXT("Move this stateful stack as a whole."); return false; }

    FInventoryItem Moved = Entry; Moved.Count = Count; Moved.bIsEquipped = false; Moved.EquipmentSlot = FGameplayTag();
    if (Count < Entry.Count)
    {
        Moved.ResetGuid();
        Moved.Item = DuplicateObject<UACFItem>(Entry.Item, Destination->GetOwner());
    }
    TStrongObjectPtr<UACFItem> KeepAlive(Moved.Item);
    const int32 OriginalCount = Entry.Count;
    // Commit both lists on the game thread before broadcasting. Calling the high-level
    // MoveItemsFromInventory would publish a duplicate GUID and reconstruct stateful
    // stacks by class. These native FastArray methods keep the same ACF storage.
    AActor* PreviousOwner = Moved.Item->GetItemOwner(); UObject* PreviousOuter = Moved.Item->GetOuter();
    const FName PreviousName = Moved.Item->GetFName();
    if (Moved.Item->GetOuter() != Destination->GetOwner()
        && !Moved.Item->Rename(*MakeUniqueObjectName(Destination->GetOwner(), Moved.Item->GetClass(), PreviousName).ToString(), Destination->GetOwner(), REN_DontCreateRedirectors | REN_DoNotDirty))
    { Error = TEXT("The item could not change inventory ownership."); return false; }
    if (Count == OriginalCount)
    { Source->UnregisterFragmentsForItem(Entry.Item); Inventory(Source)->RemoveEntry(Entry); }
    else
    { FInventoryItem Remaining = Entry; Remaining.Count -= Count; Inventory(Source)->ChangeEntry(Remaining); }

    Moved.Item->SetItemOwner(Destination->GetOwner());
    for (UACFItemFragment* Fragment : Moved.Item->Fragments)
        if (Fragment && Fragment->GetOuter() != Destination->GetOwner())
            Fragment->Rename(*MakeUniqueObjectName(Destination->GetOwner(), Fragment->GetClass(), Fragment->GetFName()).ToString(), Destination->GetOwner(), REN_DontCreateRedirectors | REN_DoNotDirty);
    // Use ACF's FastArray mutation API without its class-only stack reconstruction path.
    // No equipment effects are applied here; every entry is explicitly unequipped.
    Inventory(Destination)->AddEntry(Moved);
    Destination->RegisterFragmentsForItem(Moved.Item);
    FInventoryItem Verified, Remaining;
    const bool bFoundSource = Source->GetItemByGuid(Guid, Remaining);
    if (!Destination->GetItemByGuid(Moved.GetItemGuid(), Verified) || Verified.Item != Moved.Item || Verified.Count != Count
        || (Count == OriginalCount ? bFoundSource : !bFoundSource || Remaining.Count != OriginalCount - Count))
    {
        Destination->UnregisterFragmentsForItem(Moved.Item); Inventory(Destination)->RemoveEntry(Moved);
        Moved.Item->Rename(*PreviousName.ToString(), PreviousOuter, REN_DontCreateRedirectors | REN_DoNotDirty); Moved.Item->SetItemOwner(PreviousOwner);
        if (Count == OriginalCount)
        {
            for (UACFItemFragment* Fragment : Moved.Item->Fragments) if (Fragment) Fragment->Rename(nullptr, PreviousOuter, REN_DontCreateRedirectors | REN_DoNotDirty);
            Inventory(Source)->AddEntry(Entry); Source->RegisterFragmentsForItem(Entry.Item);
        }
        else Inventory(Source)->ChangeEntry(Entry);
        Source->RefreshTotalWeight(); Destination->RefreshTotalWeight();
        Error = TEXT("Transfer verification failed. The original inventory entry was restored."); return false;
    }
    Source->RefreshTotalWeight(); Destination->RefreshTotalWeight();
    Source->OnItemRemoved.Broadcast(FBaseItem(Moved.ItemClass, Count));
    Source->OnInventoryChanged.Broadcast();
    Destination->OnInventoryChanged.Broadcast();
    Destination->OnItemAdded.Broadcast(FBaseItem(Moved.ItemClass, Count));
    return true;
}
