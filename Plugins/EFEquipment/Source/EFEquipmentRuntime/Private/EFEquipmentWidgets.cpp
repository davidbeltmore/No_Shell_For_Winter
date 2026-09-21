#include "EFEquipmentWidgets.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/GridPanel.h"
#include "Components/SpinBox.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void UEFEquipmentSlotWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (SelectButton) SelectButton->OnClicked.AddUniqueDynamic(this, &UEFEquipmentSlotWidget::SelectOwner);
    if (UnequipButton) UnequipButton->OnClicked.AddUniqueDynamic(this, &UEFEquipmentSlotWidget::UnequipOwner);
}
void UEFEquipmentSlotWidget::SetSlotView(UEFEquipmentPanelWidget* Panel, const FEFEquipmentSlotView& View)
{
    OwnerPanel = Panel; SlotView = View;
    if (SlotLabel) SlotLabel->SetText(View.Definition.Label);
    if (OccupantLabel) OccupantLabel->SetText(!View.OwnerGuid.IsValid() ? FText::FromString(TEXT("Empty"))
        : View.bReserved ? FText::Format(FText::FromString(TEXT("Reserved by {0}")), View.ItemName) : View.ItemName);
    if (SelectButton) SelectButton->SetIsEnabled(View.OwnerGuid.IsValid());
    if (UnequipButton) UnequipButton->SetIsEnabled(View.OwnerGuid.IsValid());
}
void UEFEquipmentSlotWidget::SelectOwner() { if (OwnerPanel) OwnerPanel->SelectItem(SlotView.OwnerGuid); }
void UEFEquipmentSlotWidget::UnequipOwner() { if (OwnerPanel) OwnerPanel->UnequipItem(SlotView.OwnerGuid); }
void UEFEquipmentTransferRowWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (TransferButton) TransferButton->OnClicked.AddUniqueDynamic(this, &UEFEquipmentTransferRowWidget::Transfer);
}
void UEFEquipmentTransferRowWidget::Configure(UEFEquipmentPanelWidget* Panel, FGuid Guid, const FText& Name, int32 Count, bool bExtract)
{
    OwnerPanel = Panel; ItemGuid = Guid; AvailableCount = Count; bExtractItem = bExtract;
    if (ItemLabel) ItemLabel->SetText(FText::Format(FText::FromString(TEXT("{0} × {1}")), Name, Count));
    if (ActionLabel) ActionLabel->SetText(FText::FromString(bExtract ? TEXT("Extract") : TEXT("Store")));
}
void UEFEquipmentTransferRowWidget::Transfer() { if (OwnerPanel) OwnerPanel->TransferItem(ItemGuid, AvailableCount, bExtractItem); }

void UEFEquipmentPanelWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (ContainerPicker) ContainerPicker->OnSelectionChanged.AddUniqueDynamic(this, &UEFEquipmentPanelWidget::SelectContainer);
    BindPlayer();
    if (!Coordinator && GetWorld()) GetWorld()->GetTimerManager().SetTimer(BindTimer, this, &UEFEquipmentPanelWidget::BindPlayer, 0.2f, true);
}
void UEFEquipmentPanelWidget::NativeDestruct()
{
    if (Coordinator) Coordinator->OnStateChanged.RemoveDynamic(this, &UEFEquipmentPanelWidget::RefreshEquipment);
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(BindTimer);
    Super::NativeDestruct();
}
void UEFEquipmentPanelWidget::BindPlayer()
{
    if (auto* Player = GetOwningPlayer()) Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(Player->GetPawn());
    if (!Coordinator) return;
    Coordinator->OnStateChanged.AddUniqueDynamic(this, &UEFEquipmentPanelWidget::RefreshEquipment);
    GetWorld()->GetTimerManager().ClearTimer(BindTimer); RefreshEquipment();
}
void UEFEquipmentPanelWidget::SelectItem(FGuid Guid)
{
    SelectedItem = Guid;
    if (Coordinator && SelectedLabel)
    { FInventoryItem Item; if (Coordinator->GetNativeEquipment()->GetItemByGuid(Guid, Item)) SelectedLabel->SetText(Item.Item->GetItemName()); }
    OnItemSelected(Guid);
}
void UEFEquipmentPanelWidget::UnequipItem(FGuid Guid) { if (Coordinator) Coordinator->RequestUnequip(Guid); }
void UEFEquipmentPanelWidget::TransferItem(FGuid Guid, int32 AvailableCount, bool bExtract)
{
    if (!Coordinator || !SelectedContainer.IsValid()) return;
    const int32 Count = FMath::Clamp(TransferQuantity ? FMath::FloorToInt(TransferQuantity->GetValue()) : 1, 1, AvailableCount);
    if (bExtract) Coordinator->MoveFromContainer(SelectedContainer, Guid, Count);
    else Coordinator->MoveToContainer(Guid, SelectedContainer, Count);
}
void UEFEquipmentPanelWidget::SelectContainer(FString Selected, ESelectInfo::Type Type)
{
    if (bRefreshing) return;
    SelectedContainer = ContainerOptions.FindRef(Selected); RefreshEquipment();
}
void UEFEquipmentPanelWidget::RefreshEquipment()
{
    if (!Coordinator || !Coordinator->GetNativeEquipment() || bRefreshing) return;
    TGuardValue<bool> Refreshing(bRefreshing, true);
    const auto Weight = Coordinator->GetWeightSummary();
    if (WeightLabel) WeightLabel->SetText(FText::FromString(FString::Printf(TEXT("%s%.2f / %.2f"), Weight.bOverburdened ? TEXT("Overburdened · ") : TEXT("Weight · "), Weight.EffectiveWeight, Weight.BaseCapacity)));
    if (MessageLabel) MessageLabel->SetText(Coordinator->GetLastMessage());
    const auto Views = Coordinator->GetSlotViews();
    TArray<int32> Orders; for (const auto& View : Views) Orders.AddUnique(View.Definition.Order); Orders.Sort();
    for (const auto& View : Views)
    {
        auto* Row = SlotWidgets.FindRef(View.Definition.Slot).Get();
        if (!Row && SlotGrid && SlotWidgetClass)
        {
            const auto* Override = SlotWidgetOverrides.Find(View.Definition.Slot);
            Row = CreateWidget<UEFEquipmentSlotWidget>(GetOwningPlayer(), Override && *Override ? *Override : SlotWidgetClass);
            if (!Row) continue;
            SlotWidgets.Add(View.Definition.Slot, Row);
            SlotGrid->AddChildToGrid(Row, Orders.IndexOfByKey(View.Definition.Order), View.Definition.Column == EEFEquipmentColumn::Underwear ? 1 : 0);
        }
        if (Row) Row->SetSlotView(this, View);
    }
    const auto Root = Coordinator->GetNativeEquipment()->GetInventory();
    ContainerOptions.Empty(); FString SelectedOption;
    if (ContainerPicker) ContainerPicker->ClearOptions();
    for (const auto& Item : Root)
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
        if (!Rules || !Rules->Profile || !Rules->Profile->bContainer) continue;
        const FString Option = Item.Item->GetItemName().ToString() + TEXT(" · ") + Item.GetItemGuid().ToString().Left(8);
        ContainerOptions.Add(Option, Item.GetItemGuid());
        if (!SelectedContainer.IsValid()) SelectedContainer = Item.GetItemGuid();
        if (SelectedContainer == Item.GetItemGuid()) SelectedOption = Option;
        if (ContainerPicker) ContainerPicker->AddOption(Option);
    }
    if (SelectedOption.IsEmpty()) SelectedContainer.Invalidate();
    if (ContainerPicker && !SelectedOption.IsEmpty()) ContainerPicker->SetSelectedOption(SelectedOption);
    if (ContainerRows) ContainerRows->ClearChildren();
    if (InventoryRows) InventoryRows->ClearChildren();
    if (!SelectedContainer.IsValid())
    { if (ContainerLabel) ContainerLabel->SetText(FText::FromString(TEXT("Carry a satchel to store items."))); return; }
    const auto Contents = Coordinator->GetContainerItems(SelectedContainer); double OriginalWeight = 0;
    for (const auto& Item : Contents)
    {
        OriginalWeight += static_cast<double>(Item.Item->GetItemInfo().ItemWeight) * Item.Count;
        if (ContainerRows && TransferRowClass)
        { auto* Row = CreateWidget<UEFEquipmentTransferRowWidget>(GetOwningPlayer(), TransferRowClass); ContainerRows->AddChild(Row); Row->Configure(this, Item.GetItemGuid(), Item.Item->GetItemName(), Item.Count, true); }
    }
    FInventoryItem Bag;
    if (Coordinator->GetNativeEquipment()->GetItemByGuid(SelectedContainer, Bag))
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Bag.Item);
        if (ContainerLabel && Rules && Rules->Profile) ContainerLabel->SetText(FText::FromString(FString::Printf(TEXT("Contents %.2f / %.2f · Reduction %.0f%% %s\nExtract items before using or equipping them."), OriginalWeight, Rules->Profile->ContentCapacity, Rules->Profile->WeightReduction * 100, Bag.bIsEquipped ? TEXT("active") : TEXT("inactive"))));
    }
    for (const auto& Item : Root)
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
        if (Rules && Rules->Profile && Rules->Profile->bContainer) continue;
        if (InventoryRows && TransferRowClass)
        { auto* Row = CreateWidget<UEFEquipmentTransferRowWidget>(GetOwningPlayer(), TransferRowClass); InventoryRows->AddChild(Row); Row->Configure(this, Item.GetItemGuid(), Item.Item->GetItemName(), Item.Count, false); }
    }
}
