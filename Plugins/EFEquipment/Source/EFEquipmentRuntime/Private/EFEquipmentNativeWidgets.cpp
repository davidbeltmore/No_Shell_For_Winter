#include "EFEquipmentNativeWidgets.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Components/Button.h"
#include "ANSUIPlayerSubsystem.h"
#include "Engine/GameInstance.h"

void UEFEquipmentSatchelWindowWidget::NativeConstruct()
{
    SetIsFocusable(true);
    Super::NativeConstruct();
    if (CloseButton) CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseWindow);
}
void UEFEquipmentSatchelWindowWidget::CloseWindow() { DeactivateWidget(); }
UWidget* UEFEquipmentSatchelWindowWidget::NativeGetDesiredFocusTarget() const { return CloseButton; }

void UEFEquipmentNativeLayoutWidget::OpenSatchel()
{
    if (SatchelWindowClass && GetGameInstance())
        if (auto* UI = GetGameInstance()->GetSubsystem<UANSUIPlayerSubsystem>())
        {
            auto* Window = UI->SpawnInGameWidget(SatchelWindowClass, false, true);
            UE_LOG(LogTemp, Display, TEXT("EFEquipmentUI satchel window=%s"), *GetNameSafe(Window));
        }
}

FReply UEFEquipmentInventoryIconWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    DragSource = nullptr; DragGuid.Invalidate();
    if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
    // ACF's item button already carries FBaseItem's stable GUID, including in
    // filtered grids. Never infer identity from selection or a list index.
    auto* SourceProperty = FindFProperty<FStructProperty>(GetClass(), TEXT("Item"));
    auto* Pawn = GetOwningPlayerPawn();
    auto* Equipment = Pawn ? Pawn->FindComponentByClass<UACFEquipmentComponent>() : nullptr;
    if (Equipment && SourceProperty && SourceProperty->Struct == FBaseItem::StaticStruct())
    {
        const auto& SourceItem = *SourceProperty->ContainerPtrToValuePtr<FBaseItem>(this);
        FInventoryItem Actual;
        if (SourceItem.GetItemGuid().IsValid() && Equipment->GetItemByGuid(SourceItem.GetItemGuid(), Actual)
            && Actual.ItemClass == SourceItem.ItemClass)
        {
            if (auto* State = UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn))
            {
                if (UFunction* Hovered = FindFunction(TEXT("HandleHovered"))) ProcessEvent(Hovered, nullptr);
                DragSource = Equipment; DragGuid = Actual.GetItemGuid(); DragRevision = State->GetRevision();
                return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton).CaptureMouse(TakeWidget());
            }
        }
    }
    return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}

FReply UEFEquipmentInventoryIconWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (DragSource && Event.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        if (UFunction* Clicked = FindFunction(TEXT("HandleClicked"))) ProcessEvent(Clicked, nullptr);
        DragSource = nullptr; DragGuid.Invalidate(); return FReply::Handled().ReleaseMouseCapture();
    }
    return Super::NativeOnMouseButtonUp(Geometry, Event);
}

void UEFEquipmentInventoryIconWidget::NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& Operation)
{
    if (!DragSource || !DragGuid.IsValid()) { Super::NativeOnDragDetected(Geometry, Event, Operation); return; }
    UE_LOG(LogTemp, Display, TEXT("EFEquipmentDrag begin guid=%s revision=%lld"), *DragGuid.ToString(), DragRevision);
    auto* Drag = NewObject<UEFEquipmentDragOperation>();
    Drag->Source = DragSource; Drag->ItemGuid = DragGuid; Drag->Revision = DragRevision;
    if (auto* Visual = CreateWidget<UUserWidget>(GetOwningPlayer(), GetClass()))
    {
        for (const TCHAR* Name : {TEXT("Item"), TEXT("Equipped")})
            if (auto* Property = FindFProperty<FProperty>(GetClass(), Name))
                Property->CopyCompleteValue_InContainer(Visual, this);
        Drag->DefaultDragVisual = Visual;
    }
    Drag->Pivot = EDragPivot::CenterCenter; Operation = Drag;
    DragSource = nullptr; DragGuid.Invalidate();
}

bool UEFEquipmentNativeIconWidget::NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
    auto* Drag = Cast<UEFEquipmentDragOperation>(Operation);
    if (!Drag || !Drag->Source || Drag->Source->GetOwner() != GetOwningPlayerPawn()) return Super::NativeOnDrop(Geometry, Event, Operation);
    const auto* SlotProperty = FindFProperty<FStructProperty>(GetClass(), TEXT("SlotTag"));
    if (!SlotProperty || SlotProperty->Struct != FGameplayTag::StaticStruct()) return false;
    if (auto* State = UEFEquipmentCoordinatorComponent::FindOrCreate(Drag->Source->GetOwner()))
    {
        const auto Result = State->RequestEquip(Drag->ItemGuid, *SlotProperty->ContainerPtrToValuePtr<FGameplayTag>(this), Drag->Revision);
        UE_LOG(LogTemp, Display, TEXT("EFEquipmentDrag drop guid=%s success=%d message=%s"), *Drag->ItemGuid.ToString(), Result.bSuccess, *Result.Message.ToString());
        if (!Result.bSuccess) SetToolTipText(Result.Message);
        return true; // Consume a rejected drop without falling through into another inventory.
    }
    return false;
}

bool UEFEquipmentNativeLayoutWidget::InitializeList(UPanelWidget* List, const TArray<FGameplayTag>& Slots)
{
    // Retain ACF's Blueprint factory, style defaults, owning player and slot bindings.
    UFunction* Function = FindFunction(TEXT("InitList"));
    if (!Function || !List) return false;
    auto* ListProperty = FindFProperty<FObjectPropertyBase>(Function, TEXT("IconList"));
    auto* ArrayProperty = FindFProperty<FArrayProperty>(Function, TEXT("Array"));
    auto* Inner = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
    if (!ListProperty || !Inner || Inner->Struct != FGameplayTag::StaticStruct()) return false;
    FStructOnScope Parameters(Function);
    ListProperty->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), List);
    *ArrayProperty->ContainerPtrToValuePtr<TArray<FGameplayTag>>(Parameters.GetStructMemory()) = Slots;
    ProcessEvent(Function, Parameters.GetStructMemory());
    return List->GetChildrenCount() == Slots.Num();
}

void UEFEquipmentNativeLayoutWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    if (Coordinator || !WidgetTree) return;
    const auto* Property = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("EquipComp"));
    auto* Equipment = Property ? Cast<UACFEquipmentComponent>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
    if (!Equipment) return; // ACF InitCharacter supplies this after Construct.
    auto* Outer = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("VerticalList")));
    auto* Additional = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("EFAdditionalList")));
    if (!InitializeList(Outer, OuterwearSlots) || !InitializeList(Additional, AdditionalSlots)) return;
    Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(Equipment->GetOwner());
    if (Coordinator)
    {
        Coordinator->OnStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshAdditionalSlots);
        if (auto* Button = Cast<UButton>(WidgetTree->FindWidget(TEXT("EFOpenSatchelButton"))))
            Button->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenSatchel);
        RefreshAdditionalSlots();
    }
}

void UEFEquipmentNativeLayoutWidget::RefreshAdditionalSlots()
{
    if (!Coordinator || !WidgetTree) return;
    const auto Views = Coordinator->GetSlotViews();
    for (const TCHAR* Name : {TEXT("VerticalList"), TEXT("EFAdditionalList")})
        if (auto* List = Cast<UPanelWidget>(WidgetTree->FindWidget(Name)))
            for (auto* Child : List->GetAllChildren())
            {
                if (UFunction* Refresh = Child->FindFunction(TEXT("Refresh"))) Child->ProcessEvent(Refresh, nullptr);
                const auto* Property = FindFProperty<FStructProperty>(Child->GetClass(), TEXT("SlotTag"));
                if (!Property || Property->Struct != FGameplayTag::StaticStruct()) continue;
                const auto Tag = *Property->ContainerPtrToValuePtr<FGameplayTag>(Child);
                for (const auto& View : Views) if (View.Definition.Slot == Tag)
                {
                    FString Tip = View.Definition.Label.ToString();
                    Tip += View.bReserved ? TEXT("\nReserved by ") + View.ItemName.ToString()
                        : View.OwnerGuid.IsValid() ? TEXT("\n") + View.ItemName.ToString() : TEXT("\nEmpty");
                    Child->SetToolTipText(FText::FromString(Tip));
                    break;
                }
            }
    // Native inventory refreshes its grid on equipment changes but only refreshes
    // the comparison card on hover. A drag can change equipment without a new hover.
    if (auto* Inventory = GetTypedOuter<UUserWidget>())
    {
        auto* SelectedClass = FindFProperty<FClassProperty>(Inventory->GetClass(), TEXT("CurrentitemClass"));
        auto* RefreshInfo = Inventory->FindFunction(TEXT("RefreshItemInfo"));
        auto* ClassInput = RefreshInfo ? FindFProperty<FClassProperty>(RefreshInfo, TEXT("ItemClass")) : nullptr;
        if (SelectedClass && ClassInput)
        {
            FStructOnScope Parameters(RefreshInfo);
            ClassInput->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), SelectedClass->GetObjectPropertyValue_InContainer(Inventory));
            Inventory->ProcessEvent(RefreshInfo, Parameters.GetStructMemory());
        }
    }
}

void UEFEquipmentNativeLayoutWidget::NativeDestruct()
{
    if (Coordinator) Coordinator->OnStateChanged.RemoveDynamic(this, &ThisClass::RefreshAdditionalSlots);
    Coordinator = nullptr;
    Super::NativeDestruct();
}
