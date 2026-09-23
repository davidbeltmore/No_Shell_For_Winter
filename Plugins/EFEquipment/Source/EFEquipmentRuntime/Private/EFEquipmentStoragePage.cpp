#include "EFEquipmentStoragePage.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFContainerInventoryComponent.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Pawn.h"

UEFEquipmentStoragePage* UEFEquipmentStoragePage::FindContext(const UObject* Widget)
{
    for (auto* Object = Widget; Object; Object = Object->GetOuter())
        if (auto* Page = Cast<UEFEquipmentStoragePage>(Object)) return const_cast<UEFEquipmentStoragePage*>(Page);
    return nullptr;
}

UACFVendorComponent* UEFEquipmentStoragePage::GetBagInventory()
{
    if (StorageContext != EEFStorageContext::Satchel) return nullptr;
    if (!Coordinator) Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(GetOwningPlayerPawn());
    if (!Coordinator || !Coordinator->GetNativeEquipment()) return nullptr;
    FInventoryItem Current;
    if (!BagGuid.IsValid() || !Coordinator->GetNativeEquipment()->GetItemByGuid(BagGuid, Current))
    {
        BagGuid.Invalidate();
        for (const auto& Item : Coordinator->GetNativeEquipment()->GetInventory())
            if (auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
            { BagGuid = Item.GetItemGuid(); if (Item.bIsEquipped) break; }
    }
    FString Error;
    return BagGuid.IsValid() ? Coordinator->ResolveContainer(BagGuid, Error) : nullptr;
}

void UEFEquipmentStoragePage::NativeConstruct()
{
    Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(GetOwningPlayerPawn());
    if (StorageContext == EEFStorageContext::Satchel) GetBagInventory();
    Super::NativeConstruct();
    if (Coordinator) Coordinator->OnStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshStorage);
    if (BagSelector) BagSelector->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::SelectBag);
    RefreshStorage();
}

void UEFEquipmentStoragePage::RefreshStorage()
{
    if (StorageContext != EEFStorageContext::Merchant)
        for (const TCHAR* Name : {TEXT("ACF_Buy_WB"), TEXT("ACF_Sell_WB")})
            if (auto* Panel = Cast<UUserWidget>(GetWidgetFromName(Name)); Panel && Panel->WidgetTree)
                if (auto* Title = Panel->WidgetTree->FindWidget(TEXT("Title"))) Title->SetVisibility(ESlateVisibility::Collapsed);
    if (bRefreshing || !Coordinator || !Coordinator->GetNativeEquipment()) return;
    TGuardValue<bool> Guard(bRefreshing, true);
    if (StorageMessage)
    {
        StorageMessage->SetText(Coordinator->GetLastMessage());
        StorageMessage->SetVisibility(Coordinator->GetLastMessage().IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    if (StorageContext != EEFStorageContext::Satchel) return;
    auto* Bag = Cast<UEFContainerInventoryComponent>(GetBagInventory());
    if (BagSelector)
    {
        Bags.Reset(); BagSelector->ClearOptions();
        for (const auto& Item : Coordinator->GetNativeEquipment()->GetInventory())
            if (auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
            {
                Bags.Add(Item.GetItemGuid());
                BagSelector->AddOption(FString::Printf(TEXT("%s (%d)"), *Item.Item->GetItemName().ToString(), Bags.Num()));
            }
        BagSelector->SetSelectedIndex(Bags.IndexOfByKey(BagGuid));
    }
    if (BagWeight)
    {
        FInventoryItem Owner; Coordinator->GetNativeEquipment()->GetItemByGuid(BagGuid, Owner);
        const auto* Rules = UEFEquipmentRulesFragment::Find(Owner.Item);
        const double Reduction = Rules && Rules->Profile ? Rules->Profile->WeightReduction : 0;
        const auto Weight = Coordinator->GetWeightSummary();
        BagWeight->SetText(FText::FromString(Bag ? FString::Printf(TEXT("Bag %.2f / %.2f kg · Reduction %.0f%% %s\nPlayer %.2f / %.2f kg"),
            Bag->GetCurrentInventoryTotalWeight(), Bag->GetContentCapacity(), Reduction * 100,
            Owner.bIsEquipped ? TEXT("active") : TEXT("inactive"), Weight.EffectiveWeight, Weight.BaseCapacity)
            : TEXT("Carry a bag to store items.")));
    }
    for (const TCHAR* Name : {TEXT("ACF_Buy_WB"), TEXT("ACF_Sell_WB")})
        if (auto* Child = GetWidgetFromName(Name))
        {
            if (auto* Property = FindFProperty<FObjectPropertyBase>(Child->GetClass(), TEXT("VendorComp")))
                Property->SetObjectPropertyValue_InContainer(Child, Bag);
            if (Bag) if (auto* Refresh = Child->FindFunction(TEXT("FillList"))) Child->ProcessEvent(Refresh, nullptr);
            Child->SetIsEnabled(Bag != nullptr);
        }
}

void UEFEquipmentStoragePage::SelectBag(FString Selected, ESelectInfo::Type Selection)
{
    if (bRefreshing || !BagSelector) return;
    const int32 Index = BagSelector->FindOptionIndex(Selected);
    if (Bags.IsValidIndex(Index)) { BagGuid = Bags[Index]; RefreshStorage(); }
}

void UEFEquipmentStoragePage::NativeDestruct()
{
    if (Coordinator) Coordinator->OnStateChanged.RemoveDynamic(this, &ThisClass::RefreshStorage);
    Super::NativeDestruct();
}
