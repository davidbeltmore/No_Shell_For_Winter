#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "UObject/UnrealType.h"

namespace EFEquipmentAuthoring
{
    template<class T> T* Asset(const FString& Path)
    {
        if (T* Existing = LoadObject<T>(nullptr, *Path)) return Existing;
        UPackage* Package = CreatePackage(*Path);
        T* Result = NewObject<T>(Package, *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Result); return Result;
    }
    bool Save(UObject* Object)
    {
        const FString Name = Object->GetOutermost()->GetName();
        if (!Name.StartsWith(TEXT("/Game/_Game/Data/Equipment/")) && !Name.StartsWith(TEXT("/Game/_Game/Items/Equipment/"))
            && !Name.StartsWith(TEXT("/Game/_Game/Clothes/")) && Name != TEXT("/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP")) return false;
        Object->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Object->GetOutermost(), Object, *FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension()), Args);
    }
    UBlueprint* Blueprint(const FString& Path, UClass* Parent)
    {
        if (auto* Existing = LoadObject<UBlueprint>(nullptr, *Path)) return Existing;
        UBlueprint* Result = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*Path), *FPackageName::GetLongPackageAssetName(Path), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (Result) FAssetRegistryModule::AssetCreated(Result); return Result;
    }
    FGameplayTag Tag(const TCHAR* Value) { return FGameplayTag::RequestGameplayTag(FName(Value)); }
}

FString UEFEquipmentAuthoringLibrary::BuildFoundation()
{
    using namespace EFEquipmentAuthoring;
    auto* Registry = Asset<UEFEquipmentSlotRegistry>(TEXT("/Game/_Game/Data/Equipment/DA_EquipmentSlots"));
    if (Registry->Slots.IsEmpty())
    {
        struct FSlot { const TCHAR* Tag; const TCHAR* Label; int32 Order; EEFEquipmentColumn Column = EEFEquipmentColumn::Outerwear; };
        const FSlot Definitions[] = {
            {TEXT("ItemSlot.Armor.Helmet"), TEXT("Head"), 0}, {TEXT("ItemSlot.Necklace"), TEXT("Necklace"), 10},
            {TEXT("ItemSlot.Armor.Cape"), TEXT("Cape"), 20}, {TEXT("ItemSlot.Armor.Chest"), TEXT("Torso"), 30},
            {TEXT("ItemSlot.Underwear.Torso"), TEXT("Torso"), 30, EEFEquipmentColumn::Underwear},
            {TEXT("ItemSlot.Armor.UpperArms"), TEXT("Upper Arms"), 40}, {TEXT("ItemSlot.Armor.Forearms"), TEXT("Forearms"), 50},
            {TEXT("ItemSlot.Armor.Gloves"), TEXT("Gloves"), 60}, {TEXT("ItemSlot.Ring"), TEXT("Accessory 1"), 70},
            {TEXT("ItemSlot.Ring2"), TEXT("Accessory 2"), 80}, {TEXT("ItemSlot.Armor.Legs"), TEXT("Legs"), 90},
            {TEXT("ItemSlot.Underwear.Legs"), TEXT("Legs"), 90, EEFEquipmentColumn::Underwear},
            {TEXT("ItemSlot.Armor.Feet"), TEXT("Feet"), 100}, {TEXT("ItemSlot.Satchel"), TEXT("Satchel"), 110}
        };
        for (const auto& Definition : Definitions)
        { FEFEquipmentSlotDefinition Slot; Slot.Slot = Tag(Definition.Tag); Slot.Label = FText::FromString(Definition.Label); Slot.Order = Definition.Order; Slot.Column = Definition.Column; Registry->Slots.Add(Slot); }
    }
    auto* Bikini = Asset<UEFEquipmentRuleProfile>(TEXT("/Game/_Game/Data/Equipment/DA_BikiniRules"));
    Bikini->AdditionalReservations = {Tag(TEXT("ItemSlot.Underwear.Torso"))};
    auto* Exclusive = Asset<UEFEquipmentRuleProfile>(TEXT("/Game/_Game/Data/Equipment/DA_ExclusiveTorsoRules"));
    Exclusive->AdditionalReservations = {Tag(TEXT("ItemSlot.Underwear.Torso"))};
    if (!Save(Bikini) || !Save(Exclusive)) return TEXT("FAIL: Cannot save equipment rule profiles.");

    auto* ArmorParent = LoadObject<UBlueprint>(nullptr, TEXT("/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP"));
    if (!ArmorParent) return TEXT("FAIL: ACFArmorBP is missing.");
    if (ArmorParent->ParentClass != UEFEquipmentArmor::StaticClass()) UBlueprintEditorLibrary::ReparentBlueprint(ArmorParent, UEFEquipmentArmor::StaticClass());
    FKismetEditorUtilities::CompileBlueprint(ArmorParent);
    if (ArmorParent->Status == BS_Error || !Save(ArmorParent)) return TEXT("FAIL: ACFArmorBP compile/save failed.");

    struct FGarment { const TCHAR* Path; const TCHAR* NewSlot; const TCHAR* Name; UEFEquipmentRuleProfile* Profile; };
    const FGarment Garments[] = {
        {TEXT("/Game/_Game/Clothes/UnderWear01/Bra"), TEXT("ItemSlot.Underwear.Torso"), TEXT("Bra"), nullptr},
        {TEXT("/Game/_Game/Clothes/UnderWear01/Panty"), TEXT("ItemSlot.Underwear.Legs"), TEXT("Panty"), nullptr},
        {TEXT("/Game/_Game/Clothes/Bikini01/Bikini"), TEXT("ItemSlot.Underwear.Legs"), TEXT("Bikini"), Bikini},
        {TEXT("/Game/_Game/Clothes/Rags01/RagShirt"), TEXT("ItemSlot.Armor.Chest"), TEXT("Rag Shirt"), nullptr},
        {TEXT("/Game/_Game/Clothes/Rags01/RagPants"), TEXT("ItemSlot.Armor.Legs"), TEXT("Rag Pants"), nullptr},
        {TEXT("/Game/_Game/Clothes/Rags01/RagFootWrap"), TEXT("ItemSlot.Armor.Feet"), TEXT("Rag Foot Wraps"), nullptr}
    };
    auto Configure = [](UBlueprint* BP, FGameplayTag Slot, const TCHAR* Name, UEFEquipmentRuleProfile* Profile) -> bool
    {
        if (!BP || !BP->GeneratedClass) return false;
        auto* Item = Cast<UACFItem>(BP->GeneratedClass->GetDefaultObject()); if (!Item) return false;
        FItemDescriptor Info = Item->GetItemInfo(); Info.ItemSlots = {Slot}; Info.Name = FText::FromString(Name);
        if (Info.Description.IsEmpty() || Info.Description.ToString() == TEXT("Panty")) Info.Description = Info.Name;
        Item->SetItemDescriptor(Info);
        if (Profile)
        {
            auto* Rules = UEFEquipmentRulesFragment::Find(Item);
            if (!Rules) { Rules = NewObject<UEFEquipmentRulesFragment>(Item, NAME_None, RF_Transactional); Item->Fragments.Add(Rules); }
            Rules->Profile = Profile;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        return BP->Status != BS_Error && EFEquipmentAuthoring::Save(BP);
    };
    for (const auto& Garment : Garments)
    {
        auto* BP = LoadObject<UBlueprint>(nullptr, Garment.Path);
        if (!BP || !BP->GeneratedClass) return FString::Printf(TEXT("FAIL: Missing %s."), Garment.Path);
        auto* Item = Cast<UACFItem>(BP->GeneratedClass->GetDefaultObject());
        const FGameplayTag NewSlot = Tag(Garment.NewSlot);
        for (FGameplayTag Old : Item->GetPossibleItemSlots()) if (Old != NewSlot)
        {
            const FSoftObjectPath ClassPath(BP->GeneratedClass);
            if (!Registry->MigrationRules.ContainsByPredicate([&](const auto& Rule) { return Rule.ItemClass.ToSoftObjectPath() == ClassPath && Rule.OldSlot == Old; }))
            { FEFEquipmentMigrationRule Rule; Rule.ItemClass = TSoftClassPtr<UACFItem>(ClassPath); Rule.OldSlot = Old; Rule.NewSlot = NewSlot; Registry->MigrationRules.Add(Rule); }
        }
        if (!Configure(BP, NewSlot, Garment.Name, Garment.Profile)) return FString::Printf(TEXT("FAIL: Cannot configure %s."), Garment.Path);
    }
    auto* Plate = Blueprint(TEXT("/Game/_Game/Items/Equipment/BP_ExclusiveTorsoPrototype"), ArmorParent->GeneratedClass);
    if (!Configure(Plate, Tag(TEXT("ItemSlot.Armor.Chest")), TEXT("Exclusive Breastplate (Prototype)"), Exclusive)) return TEXT("FAIL: Exclusive prototype failed.");
    for (int32 Tier = 1; Tier <= 3; ++Tier)
    {
        auto* Profile = Asset<UEFEquipmentRuleProfile>(FString::Printf(TEXT("/Game/_Game/Data/Equipment/DA_Satchel%dRules"), Tier));
        Profile->bContainer = true; Profile->ContentCapacity = Tier * 10; Profile->WeightReduction = Tier * 0.1;
        auto* BP = Blueprint(FString::Printf(TEXT("/Game/_Game/Items/Equipment/BP_Satchel%d"), Tier), UEFEquipmentAccessory::StaticClass());
        if (!Save(Profile) || !Configure(BP, Tag(TEXT("ItemSlot.Satchel")), *FString::Printf(TEXT("Satchel %d (Prototype)"), Tier), Profile)) return TEXT("FAIL: Satchel prototype failed.");
    }
    FString Error;
    if (!Registry->Validate(Error) || !Save(Registry)) return TEXT("FAIL: ") + Error;
    return TEXT("PASS: Registry, six garments, common armor veto, exclusive prototype and three satchels authored. Player and body assets were not saved.");
}

FString UEFEquipmentAuthoringLibrary::AuditConsumers()
{
    FString Result;
    auto& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter; Filter.PackagePaths.Add(TEXT("/Game")); Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName()); Filter.bRecursiveClasses = true;
    TArray<FAssetData> Assets; AssetRegistry.GetAssets(Filter, Assets);
    const TSet<FName> Names = { TEXT("EquipItemFromInventory"), TEXT("EquipItemFromInventoryInSlot"), TEXT("UseInventoryItem"), TEXT("DropItem"), TEXT("GetCurrentInventoryTotalWeight"), TEXT("GetMaxInventoryWeight"), TEXT("MoveItemsFromInventory") };
    for (const auto& Data : Assets)
    {
        if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/FullSample/Integrations/UIIntegrations"))) continue;
        auto* BP = Cast<UBlueprint>(Data.GetAsset()); if (!BP) continue;
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs) for (const auto& Node : Graph->Nodes)
            if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Names.Contains(Call->FunctionReference.GetMemberName()))
                Result += BP->GetPathName() + TEXT("|") + Graph->GetName() + TEXT("|") + Call->FunctionReference.GetMemberName().ToString() + TEXT("\n");
    }
    return Result;
}

FString UEFEquipmentAuthoringLibrary::InspectInventoryUI()
{
    FString Result;
    const TCHAR* Names[] = {TEXT("ACF_Inventory_WB"), TEXT("ACF_CharacterEquipment_WB"), TEXT("ACF_EquipmentIcon_WB"), TEXT("ACF_Weight_WB"), TEXT("ACF_Sell_WB"), TEXT("ACF_Vendor_WB"), TEXT("ACF_ItemInfo_WB")};
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString(FString(Name) == TEXT("ACF_ItemInfo_WB") ? TEXT("/Game/FullSample/Integrations/SharedAssets/Items/") : TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/")) + Name;
        auto* BP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
        if (!BP) continue;
        Result += Path + TEXT(" parent=") + GetPathNameSafe(BP->ParentClass) + TEXT("\n");
        for (const auto& Variable : BP->NewVariables)
            Result += TEXT("  Variable ") + Variable.VarName.ToString() + TEXT(" type=") + Variable.VarType.PinCategory.ToString() + TEXT(" ") + GetPathNameSafe(Variable.VarType.PinSubCategoryObject.Get()) + TEXT(" default=") + Variable.DefaultValue + TEXT("\n");
        if (BP->WidgetTree) BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            Result += FString::Printf(TEXT("  %s (%s) parent=%s"), *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()));
            for (const TCHAR* Name : {TEXT("WidthOverride"), TEXT("HeightOverride"), TEXT("bOverride_WidthOverride"), TEXT("bOverride_HeightOverride"), TEXT("MaxDesiredHeight")})
                if (const FProperty* Property = FindFProperty<FProperty>(Widget->GetClass(), Name))
                { FString Value; Property->ExportText_Direct(Value, Property->ContainerPtrToValuePtr<void>(Widget), nullptr, Widget, PPF_None); Result += FString(TEXT(" ")) + Name + TEXT("=") + Value; }
            if (Widget->Slot)
                for (const TCHAR* SlotName : {TEXT("Size"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"), TEXT("Padding")})
                    if (const FProperty* Property = FindFProperty<FProperty>(Widget->Slot->GetClass(), SlotName))
                    { FString Value; Property->ExportText_Direct(Value, Property->ContainerPtrToValuePtr<void>(Widget->Slot), nullptr, Widget->Slot, PPF_None); Result += FString(TEXT(" slot.")) + SlotName + TEXT("=") + Value; }
            Result += TEXT("\n");
        });
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs)
        {
            Result += TEXT("  Graph ") + Graph->GetName() + TEXT("\n");
            for (const auto& Node : Graph->Nodes)
                if (auto* Call = Cast<UK2Node_CallFunction>(Node))
                { Result += TEXT("    ") + Call->FunctionReference.GetMemberName().ToString(); for (const auto* Pin : Call->Pins) Result += TEXT(" ") + Pin->PinName.ToString() + TEXT("=") + Pin->DefaultValue; Result += TEXT("\n"); }
        }
    }
    return Result;
}
