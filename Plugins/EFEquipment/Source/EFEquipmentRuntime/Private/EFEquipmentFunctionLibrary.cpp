#include "EFEquipmentFunctionLibrary.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "ANSNavbarFunctionLibrary.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentSaleWidget.h"
#include "ACFVendorComponent.h"
#include "ANSUIPlayerSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"

namespace EFEquipmentEntryPoints
{
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

void UEFEquipmentFunctionLibrary::SellItemsToVendor(UACFVendorComponent* Target, const FInventoryItem& itemTobeSold, APawn* instigator, int32 count)
{
    if (!Target || !instigator) return;
    if (!UEFEquipmentCoordinatorComponent::IsManagedPlayer(instigator)) { Target->SellItemsToVendor(itemTobeSold, instigator, count); return; }
    auto* Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(instigator); if (!Coordinator) return;
    const auto Quote = Coordinator->PreviewSale(Target, itemTobeSold.GetItemGuid(), count);
    auto* Instance = instigator->GetGameInstance();
    auto* UI = Instance ? Instance->GetSubsystem<UANSUIPlayerSubsystem>() : nullptr;
    UClass* Class = LoadClass<UEFEquipmentSaleWidget>(nullptr, TEXT("/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentSaleConfirmation.WBP_EquipmentSaleConfirmation_C"));
    if (UI && Class)
    {
        if (auto* Widget = Cast<UEFEquipmentSaleWidget>(UI->SpawnInGameWidget(Class, false, true)))
        { Widget->SetQuote(Coordinator, Quote); return; }
    }
    Coordinator->CancelSale();
}

void UEFEquipmentFunctionLibrary::BuyItems(UACFVendorComponent* Target, const FInventoryItem& item, APawn* instigator)
{
    if (!Target || !instigator) return;
    if (!UEFEquipmentCoordinatorComponent::IsManagedPlayer(instigator)) { Target->BuyItems(item, instigator); return; }
    auto* Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(instigator); if (!Coordinator) return;
    const auto Quote = Coordinator->PreviewPurchase(Target, item.GetItemGuid(), item.Count);
    auto* Game = instigator->GetGameInstance(); auto* UI = Game ? Game->GetSubsystem<UANSUIPlayerSubsystem>() : nullptr;
    UClass* Class = LoadClass<UEFEquipmentSaleWidget>(nullptr, TEXT("/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentSaleConfirmation.WBP_EquipmentSaleConfirmation_C"));
    if (UI && Class) if (auto* Widget = Cast<UEFEquipmentSaleWidget>(UI->SpawnInGameWidget(Class, false, true)))
    { Widget->SetQuote(Coordinator, Quote); return; }
    Coordinator->CancelSale();
}
