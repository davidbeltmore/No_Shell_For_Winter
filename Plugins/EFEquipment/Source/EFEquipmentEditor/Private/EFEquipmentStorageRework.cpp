#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentStoragePage.h"
#include "EFEquipmentNativeWidgets.h"
#include "EFEquipmentFunctionLibrary.h"
#include "BlueprintEditorLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Items/ACFItem.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

FString UEFEquipmentAuthoringLibrary::BuildStorageRework()
{
    const FString Root = TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/");
    auto Save = [](UWidgetBlueprint* BP)
    {
        BP->ForEachSourceWidget([&](UWidget* W) { if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName())) BP->OnVariableAdded(W->GetFName()); });
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error) return false;
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(BP->GetPackage(), BP, *FPackageName::LongPackageNameToFilename(BP->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    };
    auto FullscreenBackground = [](UWidgetBlueprint* BP)
    {
        auto* Canvas = Cast<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("CanvasPanel_0")));
        if (!Canvas) return false;
        auto* Background = Cast<UBorder>(BP->WidgetTree->FindWidget(TEXT("StorageBackdrop")));
        if (!Background)
        {
            Background = BP->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StorageBackdrop"));
            auto* Slot = Canvas->AddChildToCanvas(Background);
            Slot->SetAnchors(FAnchors(0, 0, 1, 1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(-100);
        }
        Background->SetBrush(FSlateColorBrush(FLinearColor::Black));
        Background->SetVisibility(ESlateVisibility::HitTestInvisible);
        return true;
    };
    for (const TCHAR* PanelName : {TEXT("ACF_Buy_WB"), TEXT("ACF_Sell_WB")})
    {
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, *(Root + PanelName)); if (!BP) return TEXT("FAIL: Missing native trade panel.");
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs)
        {
            const auto Nodes = Graph->Nodes;
            for (const auto& Node : Nodes)
            {
                auto* Call = Cast<UK2Node_CallFunction>(Node); if (!Call) continue;
                FName Name = Call->FunctionReference.GetMemberName();
                if (Name == TEXT("GetComponentByClass"))
                {
                    auto* ClassPin = Call->FindPin(TEXT("ComponentClass"));
                    if (!ClassPin || !ClassPin->DefaultObject || ClassPin->DefaultObject->GetName() != TEXT("ACFVendorComponent")) continue;
                    Name = TEXT("GetStorageComponent");
                }
                if (Name != TEXT("SellItemsToVendor") && Name != TEXT("BuyItems") && Name != TEXT("CanPawnSellItemFromHisInventory") && Name != TEXT("GetStorageComponent")) continue;
                auto* Function = UEFEquipmentFunctionLibrary::StaticClass()->FindFunctionByName(Name);
                if (!Function) return TEXT("FAIL: Missing storage context adapter.");
                if (auto* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self)) Self->PinName = TEXT("Target");
                Call->SetFromFunction(Function); Call->ReconstructNode();
                auto* Context = Call->FindPin(TEXT("WidgetContext"));
                if (Context && Context->LinkedTo.IsEmpty())
                {
                    auto* SelfNode = NewObject<UK2Node_Self>(Graph); Graph->AddNode(SelfNode); SelfNode->CreateNewGuid(); SelfNode->AllocateDefaultPins();
                    if (!Graph->GetSchema()->TryCreateConnection(SelfNode->FindPin(UEdGraphSchema_K2::PN_Self), Context)) return TEXT("FAIL: Could not bind storage context.");
                }
            }
        }
        if (!Save(BP)) return TEXT("FAIL: Native panel compile failed.");
    }
    auto* Deposit = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("ACF_Deposit_WB")));
    if (!Deposit) return TEXT("FAIL: Native deposit widget missing.");
    UBlueprintEditorLibrary::ReparentBlueprint(Deposit, UEFEquipmentStoragePage::StaticClass());
    if (!FullscreenBackground(Deposit)) return TEXT("FAIL: Deposit canvas missing.");
    if (!Save(Deposit)) return TEXT("FAIL: Deposit page compile failed.");
    const FString BagPath = TEXT("/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentBag");
    auto* Bag = LoadObject<UWidgetBlueprint>(nullptr, *BagPath);
    if (!Bag)
    {
        auto& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        Bag = Cast<UWidgetBlueprint>(Assets.DuplicateAsset(TEXT("WBP_EquipmentBag"), FPackageName::GetLongPackagePath(BagPath), Deposit));
    }
    if (!Bag) return TEXT("FAIL: Native bag page duplication failed.");
    if (!FullscreenBackground(Bag)) return TEXT("FAIL: Bag canvas missing.");
    // Keep the native full-screen canvas, background, tab bar and footer intact.
    // Metadata belongs inside the existing fill-sized body, never above the page.
    auto* NativeRoot = Bag->WidgetTree->FindWidget(TEXT("CanvasPanel_0"));
    auto* Body = Cast<USizeBox>(Bag->WidgetTree->FindWidget(TEXT("SizeBox_67")));
    auto* Switcher = Bag->WidgetTree->FindWidget(TEXT("ANSWidgetSwitcher_47"));
    if (!NativeRoot || !Body || !Switcher) return TEXT("FAIL: Native deposit layout contract changed.");
    auto* Selector = Cast<UComboBoxString>(Bag->WidgetTree->FindWidget(TEXT("BagSelector")));
    if (!Selector) Selector = Bag->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("BagSelector"));
    auto* Weight = Cast<UTextBlock>(Bag->WidgetTree->FindWidget(TEXT("BagWeight")));
    if (!Weight) Weight = Bag->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BagWeight"));
    auto* Message = Cast<UTextBlock>(Bag->WidgetTree->FindWidget(TEXT("StorageMessage")));
    if (!Message) Message = Bag->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StorageMessage"));
    auto* Column = Cast<UVerticalBox>(Bag->WidgetTree->FindWidget(TEXT("BagPageColumn")));
    if (!Column) Column = Bag->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BagPageColumn"));
    auto* Summary = Cast<UHorizontalBox>(Bag->WidgetTree->FindWidget(TEXT("BagSummary")));
    if (!Summary) Summary = Bag->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BagSummary"));
    NativeRoot->RemoveFromParent(); Switcher->RemoveFromParent(); Selector->RemoveFromParent(); Weight->RemoveFromParent(); Message->RemoveFromParent();
    Column->RemoveFromParent(); Column->ClearChildren(); Summary->RemoveFromParent(); Summary->ClearChildren();
    Bag->WidgetTree->RootWidget = NativeRoot;
    auto* SelectorSlot = Summary->AddChildToHorizontalBox(Selector);
    SelectorSlot->SetVerticalAlignment(VAlign_Center);
    SelectorSlot->SetPadding(FMargin(0, 0, 24, 0));
    auto* WeightSlot = Summary->AddChildToHorizontalBox(Weight);
    WeightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    WeightSlot->SetHorizontalAlignment(HAlign_Right); WeightSlot->SetVerticalAlignment(VAlign_Center);
    Weight->SetJustification(ETextJustify::Right);
    auto* SummaryBackground = Cast<UBorder>(Bag->WidgetTree->FindWidget(TEXT("BagSummaryBackground")));
    if (!SummaryBackground) SummaryBackground = Bag->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BagSummaryBackground"));
    SummaryBackground->SetBrush(FSlateColorBrush(FLinearColor::Black));
    SummaryBackground->SetPadding(FMargin(48, 14, 48, 14));
    SummaryBackground->SetContent(Summary);
    Column->AddChildToVerticalBox(SummaryBackground);
    Message->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChildToVerticalBox(Switcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Body->SetContent(Column);
    for (const TCHAR* Name : {TEXT("BagWeight"), TEXT("StorageMessage")})
        if (auto* Label = Cast<UTextBlock>(Bag->WidgetTree->FindWidget(Name)))
        {
            auto Font = Label->GetFont(); Font.Size = 14; Font.TypefaceFontName = TEXT("Regular"); Label->SetFont(Font);
            Label->SetColorAndOpacity(FSlateColor(FLinearColor(.72f, .72f, .72f)));
            Label->SetAutoWrapText(true);
        }
    if (auto* Combo = Cast<UComboBoxString>(Bag->WidgetTree->FindWidget(TEXT("BagSelector"))))
    {
        auto Style = Combo->GetWidgetStyle();
        Style.ComboButtonStyle.ButtonStyle.Normal = FSlateColorBrush(FLinearColor(.025f, .025f, .025f));
        Style.ComboButtonStyle.ButtonStyle.Hovered = FSlateColorBrush(FLinearColor(.08f, .08f, .08f));
        Style.ComboButtonStyle.ButtonStyle.Pressed = FSlateColorBrush(FLinearColor(.12f, .12f, .12f));
        Combo->SetWidgetStyle(Style); Combo->SetContentPadding(FMargin(12, 6));
        if (auto* Slot = Cast<UVerticalBoxSlot>(Combo->Slot)) Slot->SetHorizontalAlignment(HAlign_Left);
        if (auto* Property = FindFProperty<FStructProperty>(Combo->GetClass(), TEXT("ForegroundColor")))
            *Property->ContainerPtrToValuePtr<FSlateColor>(Combo) = FSlateColor(FLinearColor::White);
        if (auto* Property = FindFProperty<FStructProperty>(Combo->GetClass(), TEXT("Font")))
        { auto* Font = Property->ContainerPtrToValuePtr<FSlateFontInfo>(Combo); Font->Size = 16; Font->TypefaceFontName = TEXT("Regular"); }
    }
    if (!Save(Bag)) return TEXT("FAIL: Bag page compile failed.");
    CastChecked<UEFEquipmentStoragePage>(Bag->GeneratedClass->GetDefaultObject())->StorageContext = EEFStorageContext::Satchel;
    if (!Save(Bag)) return TEXT("FAIL: Bag defaults publication failed.");
    auto* Layout = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("ACF_CharacterEquipment_WB")));
    if (!Layout) return TEXT("FAIL: Equipment layout missing.");
    CastChecked<UEFEquipmentNativeLayoutWidget>(Layout->GeneratedClass->GetDefaultObject())->SatchelWindowClass = Bag->GeneratedClass;
    if (auto* Text = Cast<UTextBlock>(Layout->WidgetTree->FindWidget(TEXT("EFOpenSatchelButtonText")))) Text->SetText(FText::FromString(TEXT("Open bag")));
    if (!Save(Layout)) return TEXT("FAIL: Open bag binding failed.");
    // A separate editable starting item reuses the existing tier-one container
    // profile, icon and visual. No protected Player Blueprint is modified.
    const FString StarterPath = TEXT("/Game/_Game/Items/Equipment/BP_StarterBag");
    if (!LoadObject<UBlueprint>(nullptr, *StarterPath))
    {
        auto* Template = LoadObject<UBlueprint>(nullptr, TEXT("/Game/_Game/Items/Equipment/BP_Satchel1"));
        if (!Template) return TEXT("FAIL: Starter bag template missing.");
        auto& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        auto* Starter = Cast<UBlueprint>(Assets.DuplicateAsset(TEXT("BP_StarterBag"), FPackageName::GetLongPackagePath(StarterPath), Template));
        auto* Item = Starter && Starter->GeneratedClass ? Cast<UACFItem>(Starter->GeneratedClass->GetDefaultObject()) : nullptr;
        if (!Item) return TEXT("FAIL: Starter bag creation failed.");
        auto Info = Item->GetItemInfo(); Info.Name = FText::FromString(TEXT("Travel Bag"));
        Info.Description = FText::FromString(TEXT("A small bag for supplies. Reduces the weight of its contents while equipped."));
        Item->SetItemDescriptor(Info);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Starter); FKismetEditorUtilities::CompileBlueprint(Starter);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (Starter->Status == BS_Error || !UPackage::SavePackage(Starter->GetPackage(), Starter,
            *FPackageName::LongPackageNameToFilename(StarterPath, FPackageName::GetAssetPackageExtension()), Args)) return TEXT("FAIL: Starter bag publication failed.");
    }
    return TEXT("PASS: Native deposit panels bound to explicit storage contexts and native bag page.");
}
