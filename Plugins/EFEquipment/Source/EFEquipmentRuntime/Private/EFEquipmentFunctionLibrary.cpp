#include "EFEquipmentFunctionLibrary.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "ANSNavbarFunctionLibrary.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentStoragePage.h"
#include "EFContainerInventoryComponent.h"
#include "ACFVendorComponent.h"
#include "ANSUIPlayerSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"

namespace EFEquipmentEntryPoints
{
    void NotifyFailure(APawn* Pawn, const FText& Message)
    {
        if (Pawn && Pawn->GetGameInstance())
            if (auto* UI = Pawn->GetGameInstance()->GetSubsystem<UANSUIPlayerSubsystem>())
                UI->BroadcastNotification(Message, 4.f);
    }
    UEFEquipmentCoordinatorComponent* Managed(UACFEquipmentComponent* Native, const FInventoryItem& Item)
    {
        if (!Native || !UEFEquipmentCoordinatorComponent::IsManagedPlayer(Native->GetOwner())) return nullptr;
        FInventoryItem Current;
        if (!Native->GetItemByGuid(Item.GetItemGuid(), Current) || !Current.Item
            || (!Current.Item->IsA<UEFEquipmentArmor>() && !Current.Item->IsA<UEFEquipmentAccessory>())) return nullptr;
        return UEFEquipmentCoordinatorComponent::FindOrCreate(Native->GetOwner());
    }
}
UEFEquipmentCoordinatorComponent* UEFEquipmentFunctionLibrary::GetEquipmentCoordinator(AActor* Actor)
{ return UEFEquipmentCoordinatorComponent::FindOrCreate(Actor); }

UTexture2D* UEFEquipmentFunctionLibrary::GetIconByTag(const UObject* WorldContextObject, const FGameplayTag& IconTag)
{
    if (auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous())
        for (const auto& Slot : Registry->Slots)
            if (Slot.Slot == IconTag && !Slot.Icon.IsNull()) return Slot.Icon.LoadSynchronous();
    return UANSNavbarFunctionLibrary::GetIconByTag(WorldContextObject, IconTag);
}

void UEFEquipmentFunctionLibrary::UseInventoryItem(UACFEquipmentComponent* Target, const FInventoryItem& item)
{
    if (!Target) return;
    if (auto* Coordinator = EFEquipmentEntryPoints::Managed(Target, item))
    {
        FInventoryItem Current; if (!Target->GetItemByGuid(item.GetItemGuid(), Current)) return;
        if (Current.bIsEquipped) Coordinator->RequestUnequip(Current.GetItemGuid());
        else Coordinator->RequestEquip(Current.GetItemGuid(), FGameplayTag());
    }
    else Target->UseInventoryItem(item);
}
void UEFEquipmentFunctionLibrary::EquipItemFromInventory(UACFEquipmentComponent* Target, const FInventoryItem& inItem)
{ EquipItemFromInventoryInSlot(Target, inItem, FGameplayTag()); }
void UEFEquipmentFunctionLibrary::EquipItemFromInventoryInSlot(UACFEquipmentComponent* Target, const FInventoryItem& inItem, FGameplayTag slot)
{
    if (!Target) return;
    if (auto* Coordinator = EFEquipmentEntryPoints::Managed(Target, inItem)) Coordinator->RequestEquip(inItem.GetItemGuid(), slot);
    else Target->EquipItemFromInventoryInSlot(inItem, slot);
}
void UEFEquipmentFunctionLibrary::DropItem(UACFEquipmentComponent* Target, const FInventoryItem& item, int32 count)
{
    if (!Target) return;
    if (auto* Coordinator = UEFEquipmentCoordinatorComponent::IsManagedPlayer(Target->GetOwner()) ? UEFEquipmentCoordinatorComponent::FindOrCreate(Target->GetOwner()) : nullptr)
        Coordinator->RequestDrop(item.GetItemGuid(), count);
    else Target->DropItem(item, count);
}
float UEFEquipmentFunctionLibrary::GetCurrentInventoryTotalWeight(UACFInventoryComponent* Target)
{
    if (!Target) return 0;
    if (auto* Coordinator = Target->GetOwner()->FindComponentByClass<UEFEquipmentCoordinatorComponent>(); Coordinator && Coordinator->GetNativeEquipment() == Target)
        return static_cast<float>(Coordinator->GetWeightSummary().EffectiveWeight);
    return Target->GetCurrentInventoryTotalWeight();
}
double UEFEquipmentFunctionLibrary::GetBaseCarryCapacity(UACFInventoryComponent* Target)
{
    if (!Target) return 0;
    if (auto* Coordinator = Target->GetOwner()->FindComponentByClass<UEFEquipmentCoordinatorComponent>(); Coordinator && Coordinator->GetNativeEquipment() == Target)
        return Coordinator->GetWeightSummary().BaseCapacity;
    double Capacity = 0; EFEquipmentACF::Capacity(Target, Capacity); return Capacity;
}

FText UEFEquipmentFunctionLibrary::GetInventoryWeightText(UACFInventoryComponent* Target)
{
    const double Weight = GetCurrentInventoryTotalWeight(Target), Capacity = GetBaseCarryCapacity(Target);
    return FText::FromString(FString::Printf(TEXT("%s%.2f / %.2f"), Weight > Capacity + 0.000001 ? TEXT("Overburdened · ") : TEXT(""), Weight, Capacity));
}
void UEFEquipmentFunctionLibrary::ManagerUseInventoryItem(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToUse)
{ UseInventoryItem(targetComponent, itemToUse); }
void UEFEquipmentFunctionLibrary::ManagerEquipItemFromInventoryInSlot(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToUse, FGameplayTag slot)
{ EquipItemFromInventoryInSlot(targetComponent, itemToUse, slot); }
void UEFEquipmentFunctionLibrary::ManagerDropItem(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToDrop, int32 count)
{ DropItem(targetComponent, itemToDrop, count); }
bool UEFEquipmentFunctionLibrary::GetEquippedItemSlot(UACFEquipmentComponent* Target, FGameplayTag itemSlot, FEquippedItem& outSlot)
{
    if (!Target) return false;
    if (auto* Coordinator = Target->GetOwner()->FindComponentByClass<UEFEquipmentCoordinatorComponent>())
        for (const auto& View : Coordinator->GetSlotViews())
            if (View.Definition.Slot == itemSlot && View.OwnerGuid.IsValid())
                for (const auto& Equipped : Target->GetCurrentEquipment().GetEquippedItems())
                    if (Equipped.ItemGuid == View.OwnerGuid) { outSlot = Equipped; return true; }
    return Target->GetEquippedItemSlot(itemSlot, outSlot);
}
void UEFEquipmentFunctionLibrary::UnequipItemBySlot(UACFEquipmentComponent* Target, FGameplayTag itemSlot)
{
    if (!Target) return;
    FEquippedItem Item;
    if (GetEquippedItemSlot(Target, itemSlot, Item))
    {
        if (auto* Coordinator = Target->GetOwner()->FindComponentByClass<UEFEquipmentCoordinatorComponent>()) Coordinator->RequestUnequip(Item.ItemGuid);
        else Target->UnequipItemBySlot(itemSlot);
    }
}


UActorComponent* UEFEquipmentFunctionLibrary::GetStorageComponent(AActor* Target, TSubclassOf<UActorComponent> ComponentClass, UObject* WidgetContext)
{
    if (ComponentClass && ComponentClass->IsChildOf(UACFVendorComponent::StaticClass()))
        if (auto* Page = UEFEquipmentStoragePage::FindContext(WidgetContext); Page && Page->StorageContext == EEFStorageContext::Satchel)
            return Page->GetBagInventory();
    return Target ? Target->GetComponentByClass(ComponentClass) : nullptr;
}

bool UEFEquipmentFunctionLibrary::CanPawnSellItemFromHisInventory(UACFVendorComponent* Target, const FInventoryItem& itemTobeSold, const APawn* seller, int32 count, UObject* WidgetContext)
{
    if (auto* Page = UEFEquipmentStoragePage::FindContext(WidgetContext); Page && Page->StorageContext != EEFStorageContext::Merchant)
        return Target && seller && count > 0 && count <= itemTobeSold.Count;
    return Target && Target->CanPawnSellItemFromHisInventory(itemTobeSold, seller, count);
}

void UEFEquipmentFunctionLibrary::SellItemsToVendor(UACFVendorComponent* Target, const FInventoryItem& itemTobeSold, APawn* instigator, int32 count, UObject* WidgetContext)
{
    if (!Target || !instigator) return;
    auto* Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(instigator);
    if (!Coordinator) { Target->SellItemsToVendor(itemTobeSold, instigator, count); return; }
    if (auto* Page = UEFEquipmentStoragePage::FindContext(WidgetContext); Page && Page->StorageContext != EEFStorageContext::Merchant)
    {
        const auto Result = Page->StorageContext == EEFStorageContext::Satchel
            ? Coordinator->MoveToContainer(itemTobeSold.GetItemGuid(), Page->GetBagGuid(), count)
            : Coordinator->GiveToInventory(Target, itemTobeSold.GetItemGuid(), count);
        if (!Result.bSuccess) EFEquipmentEntryPoints::NotifyFailure(instigator, Result.Message);
        return;
    }
    const auto Quote = Coordinator->PreviewSale(Target, itemTobeSold.GetItemGuid(), count);
    if (Quote.bValid)
    {
        const auto Result = Coordinator->ConfirmSale(Quote.QuoteId);
        if (!Result.bSuccess) EFEquipmentEntryPoints::NotifyFailure(instigator, Result.Message);
    }
    else EFEquipmentEntryPoints::NotifyFailure(instigator, Quote.Message);
}

void UEFEquipmentFunctionLibrary::BuyItems(UACFVendorComponent* Target, const FInventoryItem& item, APawn* instigator, UObject* WidgetContext)
{
    if (!Target || !instigator) return;
    auto* Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(instigator);
    if (!Coordinator) { Target->BuyItems(item, instigator); return; }
    if (auto* Page = UEFEquipmentStoragePage::FindContext(WidgetContext); Page && Page->StorageContext != EEFStorageContext::Merchant)
    {
        const auto Result = Page->StorageContext == EEFStorageContext::Satchel
            ? Coordinator->MoveFromContainer(Page->GetBagGuid(), item.GetItemGuid(), item.Count)
            : Coordinator->TakeFromInventory(Target, item.GetItemGuid(), item.Count, false);
        if (!Result.bSuccess) EFEquipmentEntryPoints::NotifyFailure(instigator, Result.Message);
        return;
    }
    const auto Quote = Coordinator->PreviewPurchase(Target, item.GetItemGuid(), item.Count);
    if (Quote.bValid)
    {
        const auto Result = Coordinator->ConfirmPurchase(Quote.QuoteId);
        if (!Result.bSuccess) EFEquipmentEntryPoints::NotifyFailure(instigator, Result.Message);
    }
    else EFEquipmentEntryPoints::NotifyFailure(instigator, Quote.Message);
}
