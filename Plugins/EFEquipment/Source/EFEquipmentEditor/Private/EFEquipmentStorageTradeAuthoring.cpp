#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentFunctionLibrary.h"
#include "ACFVendorComponent.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

FString UEFEquipmentAuthoringLibrary::BuildStorageTradeIntegration()
{
    const TCHAR* Paths[] = {
        TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_Storage_WB"),
        TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_IconStorage_WB"),
        TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_Buy_WB"),
        TEXT("/Game/FullSample/UI/WBP_Storage"),
        TEXT("/Game/FullSample/UI/Inventory/WBP_InventorySubMenu")};
    const TMap<FName, UClass*> Contracts = {
        {TEXT("MoveItemsFromInventory"), UACFInventoryComponent::StaticClass()},
        {TEXT("BuyItems"), UACFVendorComponent::StaticClass()},
        {TEXT("EquipItemFromInventory"), UACFEquipmentComponent::StaticClass()},
        {TEXT("DropItem"), UACFEquipmentComponent::StaticClass()}};
    int32 Routed = 0;
    for (const TCHAR* Path : Paths)
    {
        auto* BP = LoadObject<UBlueprint>(nullptr, Path);
        if (!BP) return TEXT("FAIL: Missing storage/trade consumer ") + FString(Path);
        bool Changed = false; TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs) for (const auto& Node : Graph->Nodes)
        {
            auto* Call = Cast<UK2Node_CallFunction>(Node); if (!Call) continue;
            const FName Name = Call->FunctionReference.GetMemberName();
            auto* const* Expected = Contracts.Find(Name);
            if (!Expected || Call->FunctionReference.GetMemberParentClass() == UEFEquipmentFunctionLibrary::StaticClass()) continue;
            UClass* Owner = Call->FunctionReference.GetMemberParentClass();
            if (!Owner || !Owner->IsChildOf(*Expected)) return TEXT("FAIL: Unexpected native call owner for ") + Name.ToString();
            UFunction* Function = UEFEquipmentFunctionLibrary::StaticClass()->FindFunctionByName(Name);
            if (!Function) return TEXT("FAIL: Missing storage/trade adapter.");
            if (auto* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self)) Self->PinName = TEXT("Target");
            Call->SetFromFunction(Function); Call->ReconstructNode(); Changed = true; ++Routed;
        }
        if (!Changed) continue;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error) return TEXT("FAIL: Storage/trade consumer compile failed: ") + FString(Path);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(BP->GetOutermost(), BP, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args))
            return TEXT("FAIL: Storage/trade consumer publication failed: ") + FString(Path);
    }
    return FString::Printf(TEXT("PASS: Routed %d native storage, purchase and alternate inventory calls; existing layouts retained."), Routed);
}
