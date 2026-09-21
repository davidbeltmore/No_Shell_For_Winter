#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentPrototypeActor.h"
#include "EFEquipmentWorldItem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "K2Node_Event.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace EFEquipmentWorldAuthoring
{
    bool Save(UBlueprint* BP)
    {
        if (!BP || BP->Status == BS_Error || !BP->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/Items/Equipment/"))) return false;
        BP->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(BP->GetOutermost(), BP, *FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    }
    UBlueprint* Visual(const TCHAR* Name, FVector Offset, FVector Scale)
    {
        const FString Path = TEXT("/Game/_Game/Items/Equipment/") + FString(Name);
        if (auto* Existing = LoadObject<UBlueprint>(nullptr, *Path)) return Existing;
        auto* BP = FKismetEditorUtilities::CreateBlueprint(AEFEquipmentPrototypeActor::StaticClass(), CreatePackage(*Path), Name,
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!BP) return nullptr;
        FAssetRegistryModule::AssetCreated(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        auto* Defaults = Cast<AEFEquipmentPrototypeActor>(BP->GeneratedClass->GetDefaultObject());
        if (!Defaults) return nullptr;
        Defaults->AttachmentOffset = Offset; Defaults->MeshScale = Scale;
        return Save(BP) ? BP : nullptr;
    }
}

FString UEFEquipmentAuthoringLibrary::BuildWorldActors()
{
    using namespace EFEquipmentWorldAuthoring;
    auto* BagVisual = Visual(TEXT("BP_SatchelPrototypeVisual"), FVector(-18, 0, -5), FVector(.16, .30, .35));
    auto* PlateVisual = Visual(TEXT("BP_BreastplatePrototypeVisual"), FVector(14, 0, -5), FVector(.10, .32, .42));
    if (!BagVisual || !PlateVisual) return TEXT("FAIL: Prototype visual Blueprint compile/save failed.");
    const TCHAR* Items[] = {TEXT("BP_Satchel1"), TEXT("BP_Satchel2"), TEXT("BP_Satchel3"), TEXT("BP_ExclusiveTorsoPrototype")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Items); ++Index)
    {
        auto* BP = LoadObject<UBlueprint>(nullptr, *(TEXT("/Game/_Game/Items/Equipment/") + FString(Items[Index])));
        auto* Item = BP && BP->GeneratedClass ? Cast<UACFItem>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
        if (!Item) return TEXT("FAIL: Prototype item is missing.");
        UClass* VisualClass = Index == 3 ? PlateVisual->GeneratedClass.Get() : BagVisual->GeneratedClass.Get();
        if (auto* Armor = Cast<UEFEquipmentArmor>(Item); Armor && !Armor->PrototypeActorClass) Armor->PrototypeActorClass = VisualClass;
        if (auto* Accessory = Cast<UEFEquipmentAccessory>(Item); Accessory && !Accessory->PrototypeActorClass) Accessory->PrototypeActorClass = VisualClass;
        FItemDescriptor Info = Item->GetItemInfo();
        if (Info.WorldMesh.IsNull()) Info.WorldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        Item->SetItemDescriptor(Info);
        if (!Save(BP)) return TEXT("FAIL: Prototype item assignment could not be saved.");
    }
    const TCHAR* Destination = TEXT("/Game/_Game/Items/Equipment/BP_EquipmentWorldItem");
    auto* Pickup = LoadObject<UBlueprint>(nullptr, Destination);
    if (!Pickup)
    {
        auto* Original = LoadObject<UBlueprint>(nullptr, TEXT("/AscentCombatFramework/Blueprints/Actors/ACF_WorldItem_BP"));
        if (!Original || Original->ParentClass != AACFWorldItem::StaticClass())
            return TEXT("FAIL: Native world pickup parent changed; inspect the inherited contract before adapting it.");
        auto& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        Pickup = Cast<UBlueprint>(AssetTools.DuplicateAsset(TEXT("BP_EquipmentWorldItem"), TEXT("/Game/_Game/Items/Equipment"), Original));
        if (!Pickup) return TEXT("FAIL: Native world pickup could not be duplicated into the project.");
        UBlueprintEditorLibrary::ReparentBlueprint(Pickup, AEFEquipmentWorldItem::StaticClass());
        // The native BP delegates storage to Parent, then emits the pickup cue and
        // unregisters interaction unconditionally. Retain those authored effects,
        // but let the project parent's verified result gate their continuation.
        TArray<UEdGraph*> Graphs; Pickup->GetAllGraphs(Graphs);
        UK2Node_CallParentFunction* ParentCall = nullptr;
        for (auto* Graph : Graphs) for (const auto& Node : Graph->Nodes)
            if (auto* Call = Cast<UK2Node_CallParentFunction>(Node); Call && Call->FunctionReference.GetMemberName() == TEXT("OnInteractedByPawn"))
            { if (ParentCall) return TEXT("FAIL: Multiple native pickup continuations need review."); ParentCall = Call; }
        if (!ParentCall || !ParentCall->GetThenPin()) return TEXT("FAIL: Native pickup parent continuation changed.");
        auto* Graph = ParentCall->GetGraph();
        auto* Complete = NewObject<UK2Node_CallFunction>(Graph);
        Complete->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(AEFEquipmentWorldItem, WasPickupCompleted), AEFEquipmentWorldItem::StaticClass());
        Graph->AddNode(Complete, false, false); Complete->CreateNewGuid(); Complete->AllocateDefaultPins();
        Complete->NodePosX = ParentCall->NodePosX + 240; Complete->NodePosY = ParentCall->NodePosY + 150;
        auto* Branch = NewObject<UK2Node_IfThenElse>(Graph);
        Graph->AddNode(Branch, false, false); Branch->CreateNewGuid(); Branch->AllocateDefaultPins();
        Branch->NodePosX = ParentCall->NodePosX + 480; Branch->NodePosY = ParentCall->NodePosY;
        Branch->NodeComment = TEXT("Keep rejected pickups available for interaction.");
        const auto Continuations = ParentCall->GetThenPin()->LinkedTo;
        ParentCall->GetThenPin()->BreakAllPinLinks();
        ParentCall->GetThenPin()->MakeLinkTo(Branch->GetExecPin());
        Complete->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
        for (auto* Pin : Continuations) Branch->GetThenPin()->MakeLinkTo(Pin);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Pickup);
        FKismetEditorUtilities::CompileBlueprint(Pickup);
    }
    if (!Pickup->GeneratedClass || !Pickup->GeneratedClass->IsChildOf(AEFEquipmentWorldItem::StaticClass()) || !Save(Pickup))
        return TEXT("FAIL: Project world pickup compile/save failed.");
    return TEXT("PASS: Prototype visuals assigned and native pickup duplicated with aggregate admission. Game-world integration resolves EFEquipmentSettings.WorldPickupClass.");
}
