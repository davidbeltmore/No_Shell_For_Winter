#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentWidgets.h"
#include "EFEquipmentNativeWidgets.h"
#include "BlueprintEditorLibrary.h"
#include "UObject/UnrealType.h"
#include "Engine/Texture2D.h"
#include "Animation/WidgetAnimation.h"
#include "EFEquipmentSaleWidget.h"
#include "EFEquipmentFunctionLibrary.h"
#include "EFEquipmentData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace EFEquipmentUIAuthoring
{
    const FString Root = TEXT("/Game/_Game/Widgets/Equipment/");
    void SynchronizeWidgetGuids(UWidgetBlueprint* BP)
    {
        TSet<FName> Names;
        BP->ForEachSourceWidget([&](UWidget* Entry) { Names.Add(Entry->GetFName()); });
        for (const auto& Animation : BP->Animations) if (Animation) Names.Add(Animation->GetFName());
        for (FName Name : Names) if (!BP->WidgetVariableNameToGuidMap.Contains(Name)) BP->OnVariableAdded(Name);
        TArray<FName> Removed;
        for (const auto& Pair : BP->WidgetVariableNameToGuidMap) if (!Names.Contains(Pair.Key)) Removed.Add(Pair.Key);
        for (FName Name : Removed) BP->OnVariableRemoved(Name);
    }
    UWidgetBlueprint* Create(const FString& Suffix, UClass* Parent, bool& bNew)
    {
        const FString Path = Root + Suffix;
        if (auto* Existing = LoadObject<UWidgetBlueprint>(nullptr, *Path)) { bNew = false; return Existing; }
        auto* BP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*Path), *FPackageName::GetLongPackageAssetName(Path), BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
        if (BP) FAssetRegistryModule::AssetCreated(BP);
        bNew = true; return BP;
    }
    bool Save(UWidgetBlueprint* BP)
    {
        if (!BP) return false;
        const FString Path = BP->GetOutermost()->GetName();
        if (!Path.StartsWith(Root) && !Path.StartsWith(TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/"))
            && Path != TEXT("/Game/FullSample/Integrations/SharedAssets/Items/ACF_ItemButtonInterface_WB")) return false;
        SynchronizeWidgetGuids(BP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error) return false;
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(BP->GetOutermost(), BP, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args);
    }
    template<class T> T* Widget(UWidgetBlueprint* BP, const TCHAR* Name)
    { auto* Result = BP->WidgetTree->ConstructWidget<T>(T::StaticClass(), Name); Result->bIsVariable = true; return Result; }
    UTextBlock* Label(UWidgetBlueprint* BP, const TCHAR* Name, const TCHAR* Text, int32 Size = 12)
    { auto* Result = Widget<UTextBlock>(BP, Name); Result->SetText(FText::FromString(Text)); auto Font = Result->GetFont(); Font.Size = Size; Result->SetFont(Font); Result->SetAutoWrapText(true); return Result; }
    UButton* DarkButton(UWidgetBlueprint* BP, const TCHAR* Name, const TCHAR* Caption)
    {
        auto* Button = Widget<UButton>(BP, Name);
        auto Style = Button->GetStyle();
        Style.Normal.TintColor = FSlateColor(FLinearColor(0.06f, 0.075f, 0.085f));
        Style.Hovered.TintColor = FSlateColor(FLinearColor(0.18f, 0.22f, 0.25f));
        Style.Pressed.TintColor = FSlateColor(FLinearColor(0.10f, 0.15f, 0.17f));
        Button->SetStyle(Style);
        Button->AddChild(Label(BP, *(FString(Name) + TEXT("Text")), Caption, 14));
        return Button;
    }
    void SlotTree(UWidgetBlueprint* BP)
    {
        auto* Box = Widget<USizeBox>(BP, TEXT("SlotSize")); Box->SetWidthOverride(200); Box->SetMinDesiredHeight(42); BP->WidgetTree->RootWidget = Box;
        auto* Row = Widget<UHorizontalBox>(BP, TEXT("SlotRow")); Box->AddChild(Row);
        auto* Select = Widget<UButton>(BP, TEXT("SelectButton")); Row->AddChildToHorizontalBox(Select)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Info = Widget<UVerticalBox>(BP, TEXT("SlotInfo")); Select->AddChild(Info);
        Info->AddChild(Label(BP, TEXT("SlotLabel"), TEXT("Slot"), 12)); Info->AddChild(Label(BP, TEXT("OccupantLabel"), TEXT("Empty"), 10));
        auto* Unequip = Widget<UButton>(BP, TEXT("UnequipButton")); Row->AddChild(Unequip); Unequip->SetToolTipText(FText::FromString(TEXT("Unequip the item that owns this slot")));
        Unequip->AddChild(Label(BP, TEXT("UnequipGlyph"), TEXT("×"), 14));
    }
    bool Route(UWidgetBlueprint* BP, FString& Error)
    {
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        const TSet<FName> Names = {TEXT("UseInventoryItem"), TEXT("EquipItemFromInventoryInSlot"), TEXT("EquipItemFromInventory"), TEXT("DropItem"), TEXT("UnequipItemBySlot"), TEXT("GetEquippedItemSlot"), TEXT("SellItemsToVendor"), TEXT("GetIconByTag")};
        for (auto* Graph : Graphs)
        {
            const auto Nodes = Graph->Nodes;
            for (const auto& Node : Nodes)
            {
                auto* Call = Cast<UK2Node_CallFunction>(Node); if (!Call) continue;
                const FName Name = Call->FunctionReference.GetMemberName(); if (!Names.Contains(Name)) continue;
                if (Call->FunctionReference.GetMemberParentClass() == UEFEquipmentFunctionLibrary::StaticClass()) continue;
                FString Replacement = Name.ToString();
                if (Call->FindPin(TEXT("targetComponent"))) Replacement = TEXT("Manager") + Replacement;
                UFunction* Function = UEFEquipmentFunctionLibrary::StaticClass()->FindFunctionByName(*Replacement);
                if (!Function) { Error = TEXT("Missing route for ") + Replacement; return false; }
                if (auto* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self)) Self->PinName = TEXT("Target");
                Call->SetFromFunction(Function); Call->ReconstructNode();
            }
        }
        return true;
    }
}

FString UEFEquipmentAuthoringLibrary::BuildInventoryUI()
{
    using namespace EFEquipmentUIAuthoring;
    bool bNew = false; FString Error;
    auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    if (!Registry) return TEXT("FAIL: Equipment registry is missing.");
    // Keep serialized slot IDs compatible with existing items; labels describe generic accessories.
    for (auto& Slot : Registry->Slots)
    {
        if (Slot.Slot.ToString() == TEXT("ItemSlot.Ring")) Slot.Label = FText::FromString(TEXT("Accessory 1"));
        if (Slot.Slot.ToString() == TEXT("ItemSlot.Ring2")) Slot.Label = FText::FromString(TEXT("Accessory 2"));
    }
    const TMap<FString, FString> Icons = {
        {TEXT("ItemSlot.Armor.Helmet"), TEXT("/Game/FullSample/Assets/UI/Icons/helmet")},
        {TEXT("ItemSlot.Armor.Chest"), TEXT("/Game/FullSample/Assets/UI/Icons/armor")},
        {TEXT("ItemSlot.Armor.Legs"), TEXT("/Game/FullSample/Assets/UI/Icons/men-legs")},
        {TEXT("ItemSlot.Armor.Feet"), Root + TEXT("Assets/Icons/T_Equipment_Feet")},
        {TEXT("ItemSlot.Necklace"), Root + TEXT("Assets/Icons/T_Equipment_Necklace")},
        {TEXT("ItemSlot.Armor.UpperArms"), Root + TEXT("Assets/Icons/T_Equipment_UpperArms")},
        {TEXT("ItemSlot.Armor.Forearms"), Root + TEXT("Assets/Icons/T_Equipment_Forearms")},
        {TEXT("ItemSlot.Armor.Gloves"), Root + TEXT("Assets/Icons/T_Equipment_Gloves")},
        {TEXT("ItemSlot.Ring"), Root + TEXT("Assets/Icons/T_Equipment_Accessory")},
        {TEXT("ItemSlot.Ring2"), Root + TEXT("Assets/Icons/T_Equipment_Accessory")},
        {TEXT("ItemSlot.Underwear.Torso"), Root + TEXT("Assets/Icons/T_Equipment_UnderwearTorso")},
        {TEXT("ItemSlot.Underwear.Legs"), Root + TEXT("Assets/Icons/T_Equipment_UnderwearLegs")},
        {TEXT("ItemSlot.Satchel"), Root + TEXT("Assets/Icons/T_Equipment_Satchel")}
    };
    for (auto& Slot : Registry->Slots) if (const FString* Path = Icons.Find(Slot.Slot.ToString()))
    {
        auto* Texture = LoadObject<UTexture2D>(nullptr, **Path);
        if (!Texture) return TEXT("FAIL: Missing slot icon ") + *Path;
        if (Slot.Icon.IsNull()) Slot.Icon = Texture;
    }
    Registry->MarkPackageDirty();
    FSavePackageArgs RegistryArgs; RegistryArgs.TopLevelFlags = RF_Public | RF_Standalone;
    if (!UPackage::SavePackage(Registry->GetOutermost(), Registry,
        *FPackageName::LongPackageNameToFilename(Registry->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), RegistryArgs))
        return TEXT("FAIL: Accessory labels could not be saved.");
    auto* ItemIcon = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/FullSample/Integrations/SharedAssets/Items/ACF_ItemButtonInterface_WB"));
    if (!ItemIcon) return TEXT("FAIL: Native inventory item icon is missing.");
    if (ItemIcon->ParentClass != UEFEquipmentInventoryIconWidget::StaticClass())
        UBlueprintEditorLibrary::ReparentBlueprint(ItemIcon, UEFEquipmentInventoryIconWidget::StaticClass());
    if (!Save(ItemIcon)) return TEXT("FAIL: Native inventory drag adapter compile failed.");
    const TCHAR* Existing[] = {TEXT("ACF_Inventory_WB"), TEXT("ACF_ListInventory_WB"), TEXT("ACF_CharacterEquipment_WB"), TEXT("ACF_EquipmentIcon_WB"), TEXT("ACF_Weight_WB"), TEXT("ACF_Sell_WB")};
    for (const TCHAR* Name : Existing)
    {
        const FString Path = FString(TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/")) + Name;
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, *Path); if (!BP || !Route(BP, Error)) return TEXT("FAIL: ") + Path + TEXT(" ") + Error;
        if (FString(Name) == TEXT("ACF_EquipmentIcon_WB") && BP->ParentClass != UEFEquipmentNativeIconWidget::StaticClass())
            UBlueprintEditorLibrary::ReparentBlueprint(BP, UEFEquipmentNativeIconWidget::StaticClass());
        if (FString(Name) == TEXT("ACF_CharacterEquipment_WB"))
        {
            if (auto* Preview = Cast<USizeBox>(BP->WidgetTree->FindWidget(TEXT("SizeBox_2"))); Preview && Preview->GetWidthOverride() == 300)
                Preview->SetWidthOverride(420);
            if (BP->ParentClass != UEFEquipmentNativeLayoutWidget::StaticClass())
                UBlueprintEditorLibrary::ReparentBlueprint(BP, UEFEquipmentNativeLayoutWidget::StaticClass());
            if (!BP->WidgetTree->FindWidget(TEXT("EFOuterwearScroll")))
            {
                auto* OldList = BP->WidgetTree->FindWidget(TEXT("VerticalList"));
                auto* Host = Cast<USizeBox>(BP->WidgetTree->FindWidget(TEXT("SizeBox_1")));
                auto* Row = Cast<UHorizontalBox>(BP->WidgetTree->FindWidget(TEXT("HorizontalBox_71")));
                if (!OldList || !Host || !Row || Host->GetContent() != OldList)
                    return TEXT("FAIL: Restore the original character equipment tree before authoring this layout.");
                OldList->RemoveFromParent(); OldList->SetVisibility(ESlateVisibility::Visible);
                auto* Column = Widget<UVerticalBox>(BP, TEXT("EFOuterwearColumn")); Host->AddChild(Column);
                auto* Heading = Label(BP, TEXT("EFOuterwearHeading"), TEXT("Outerwear"), 12);
                Column->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0, 0, 0, 10));
                auto* Viewport = Widget<USizeBox>(BP, TEXT("EFOuterwearViewport"));
                Viewport->SetWidthOverride(94); Viewport->SetHeightOverride(400);
                Column->AddChild(Viewport);
                auto* Scroll = Widget<UScrollBox>(BP, TEXT("EFOuterwearScroll")); Viewport->AddChild(Scroll);
                Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
                Scroll->SetAllowOverscroll(false); Scroll->SetWheelScrollMultiplier(1.0f);
                Scroll->SetScrollBarVisibility(ESlateVisibility::Visible); Scroll->SetAnimateWheelScrolling(true);
                Scroll->AddChild(OldList);
                Host->SetWidthOverride(100); Host->SetHeightOverride(600);
                auto* AdditionalSize = Widget<USizeBox>(BP, TEXT("EFAdditionalSize"));
                AdditionalSize->SetWidthOverride(108); AdditionalSize->SetHeightOverride(600);
                Row->AddChildToHorizontalBox(AdditionalSize)->SetPadding(FMargin(12, 0, 0, 0));
                auto* Additional = Widget<UVerticalBox>(BP, TEXT("EFAdditionalColumn")); AdditionalSize->AddChild(Additional);
                Additional->AddChildToVerticalBox(Label(BP, TEXT("EFAdditionalHeading"), TEXT("Underwear"), 12))->SetPadding(FMargin(0, 0, 0, 10));
                Additional->AddChild(Widget<UVerticalBox>(BP, TEXT("EFAdditionalList")));
            }
            if (!BP->WidgetTree->FindWidget(TEXT("EFOpenSatchelButton")))
                if (auto* Additional = Cast<UVerticalBox>(BP->WidgetTree->FindWidget(TEXT("EFAdditionalColumn"))))
                    Additional->AddChildToVerticalBox(DarkButton(BP, TEXT("EFOpenSatchelButton"), TEXT("Open bag")))->SetPadding(FMargin(0, 8, 12, 0));
            SynchronizeWidgetGuids(BP);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
            FKismetEditorUtilities::CompileBlueprint(BP);
            auto* Defaults = Cast<UEFEquipmentNativeLayoutWidget>(BP->GeneratedClass->GetDefaultObject());
            if (!Defaults) return TEXT("FAIL: Native layout parent could not be composed.");
            // Native bag page is bound by BuildStorageRework below.
            if (Defaults->OuterwearSlots.IsEmpty())
            for (const TCHAR* Tag : {TEXT("ItemSlot.Armor.Helmet"), TEXT("ItemSlot.Armor.Chest"), TEXT("ItemSlot.Armor.Legs"),
                TEXT("ItemSlot.Armor.Feet"), TEXT("ItemSlot.Necklace"), TEXT("ItemSlot.Armor.UpperArms"), TEXT("ItemSlot.Armor.Forearms"),
                TEXT("ItemSlot.Armor.Gloves"), TEXT("ItemSlot.Ring"), TEXT("ItemSlot.Ring2")})
                Defaults->OuterwearSlots.Add(FGameplayTag::RequestGameplayTag(Tag));
            if (Defaults->AdditionalSlots.IsEmpty())
            for (const TCHAR* Tag : {TEXT("ItemSlot.Underwear.Torso"), TEXT("ItemSlot.Underwear.Legs"), TEXT("ItemSlot.Satchel")})
                Defaults->AdditionalSlots.Add(FGameplayTag::RequestGameplayTag(Tag));
        }
        if (FString(Name) == TEXT("ACF_Inventory_WB"))
        {
            auto* EquipmentColumn = BP->WidgetTree->FindWidget(TEXT("Equipment"));
            auto* EquipmentAllocation = EquipmentColumn ? Cast<UHorizontalBoxSlot>(EquipmentColumn->Slot) : nullptr;
            if (EquipmentAllocation && FMath::IsNearlyEqual(EquipmentAllocation->GetSize().Value, 0.25f))
            {
                const TPair<FName, float> Weights[] = {{TEXT("Inventory"), .50f}, {TEXT("Equipment"), .36f}, {TEXT("ItemInfo"), .14f}};
                for (const auto& Pair : Weights)
                    if (auto* Column = BP->WidgetTree->FindWidget(Pair.Key))
                        if (auto* Allocation = Cast<UHorizontalBoxSlot>(Column->Slot))
                        { FSlateChildSize Size(ESlateSizeRule::Fill); Size.Value = Pair.Value; Allocation->SetSize(Size); }
            }
            // The original zero-width override hides the character preview in this target.
            auto* EquipmentSize = Cast<USizeBox>(BP->WidgetTree->FindWidget(TEXT("SizeBox")));
            if (!EquipmentSize) return TEXT("FAIL: Inventory equipment host is missing.");
            if (EquipmentSize->GetWidthOverride() <= 540) EquipmentSize->SetWidthOverride(660);
            for (const TCHAR* InfoName : {TEXT("SizeBox_2"), TEXT("SizeBox_3")})
                if (auto* Info = Cast<USizeBox>(BP->WidgetTree->FindWidget(InfoName)); Info && Info->GetWidthOverride() == 350)
                    Info->SetWidthOverride(240);
        }
        if (FString(Name) == TEXT("ACF_Weight_WB"))
        {
            TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
            for (auto* Graph : Graphs)
            {
                UK2Node_CallFunction* WeightCall = nullptr; UK2Node_CallFunction* SetText = nullptr; bool bAlreadyRouted = false;
                for (const auto& Node : Graph->Nodes) if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                {
                    const auto Function = Call->FunctionReference.GetMemberName();
                    if (Function == TEXT("GetCurrentInventoryTotalWeight")) WeightCall = Call;
                    if (Function == TEXT("SetText")) SetText = Call;
                    if (Function == TEXT("GetInventoryWeightText")) bAlreadyRouted = true;
                }
                if (WeightCall && SetText && !bAlreadyRouted)
                {
                    auto* New = NewObject<UK2Node_CallFunction>(Graph); Graph->AddNode(New); New->CreateNewGuid();
                    New->SetFromFunction(UEFEquipmentFunctionLibrary::StaticClass()->FindFunctionByName(TEXT("GetInventoryWeightText"))); New->AllocateDefaultPins();
                    New->NodePosX = WeightCall->NodePosX; New->NodePosY = WeightCall->NodePosY - 180;
                    for (auto* Link : WeightCall->FindPinChecked(UEdGraphSchema_K2::PN_Self)->LinkedTo) New->FindPinChecked(TEXT("Target"))->MakeLinkTo(Link);
                    auto* TextPin = SetText->FindPinChecked(TEXT("InText")); TextPin->BreakAllPinLinks(); New->GetReturnValuePin()->MakeLinkTo(TextPin);
                }
            }
        }
        if (!Save(BP)) return TEXT("FAIL: Existing widget compile/save failed: ") + Path;
    }
    return BuildStorageRework();
}
