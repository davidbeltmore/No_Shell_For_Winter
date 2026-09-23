#include "Lockpicking/ProjectLockpickingEditorLibrary.h"
#if WITH_EDITOR
#include "Storage/ProjectChestInventoryComponent.h"
#include "Storage/ProjectProceduralChestComponent.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Lockpicking/ProjectLockpickingEditorLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node_Event.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"

#endif
bool UProjectLockpickingEditorLibrary::ShowProceduralChestDetails(UBlueprint* Blueprint)
{
#if WITH_EDITOR
    if (!Blueprint || !Blueprint->GeneratedClass) return false;
    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    Editors->OpenEditorForAsset(Blueprint);
    auto* Instance = Editors->FindEditorForAsset(Blueprint, true);
    if (!Instance || Instance->GetEditorName() != TEXT("BlueprintEditor")) return false;
    auto* Editor = static_cast<FBlueprintEditor*>(Instance);
    for (auto* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass); Class; Class = Cast<UBlueprintGeneratedClass>(Class->GetSuperClass()))
        if (Class->SimpleConstructionScript)
            for (auto* Node : Class->SimpleConstructionScript->GetAllNodes())
                if (Node->ComponentClass->IsChildOf(UProjectProceduralChestComponent::StaticClass()))
                    return Editor->FindAndSelectSubobjectEditorTreeNode(Node->ComponentTemplate, false).IsValid();
    return false;
#else
    return false;
#endif
}

FString UProjectLockpickingEditorLibrary::AddProceduralChestAuthoring()
{
#if WITH_EDITOR
    auto* BP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/_Game/Items/Chests/BP_EquipmentDeposit"));
    if (!BP || !BP->SimpleConstructionScript) return TEXT("FAIL: Project deposit is unavailable.");
    USCS_Node* Found = nullptr;
    for (auto* Node : BP->SimpleConstructionScript->GetAllNodes())
        if (Node->ComponentClass->IsChildOf(UProjectProceduralChestComponent::StaticClass())) Found = Node;
    if (!Found)
    {
        BP->Modify();
        Found = BP->SimpleConstructionScript->CreateNode(UProjectProceduralChestComponent::StaticClass(), TEXT("ProceduralChest"));
        BP->SimpleConstructionScript->AddNode(Found);
        auto* Settings = CastChecked<UProjectProceduralChestComponent>(Found->ComponentTemplate);
        Settings->Profile = LoadObject<UProjectChestLootProfile>(nullptr, TEXT("/Game/_Game/Data/Equipment/Chests/DA_Chest_Wealth"));
        Settings->ChestRarity = FGameplayTag::RequestGameplayTag(TEXT("Rarity.Common"));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    }
    for (const TCHAR* Path : {TEXT("/Game/_Game/Items/Chests/BP_EquipmentDeposit"), TEXT("/Game/_Game/Items/Chests/BP_EquipmentLockedChest")})
    {
        auto* Asset = LoadObject<UBlueprint>(nullptr, Path);
        if (!Asset) return TEXT("FAIL: Project chest missing.");
        FKismetEditorUtilities::CompileBlueprint(Asset);
        if (Asset->Status == BS_Error) return TEXT("FAIL: Chest compile failed.");
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Asset->GetPackage(), Asset, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args)) return TEXT("FAIL: Chest save failed.");
    }
    return TEXT("PASS: Added only the ProceduralChest authoring component; existing inventory, graphs and lock settings preserved.");
#else
    return TEXT("FAIL: Requires Editor.");
#endif
}

FString UProjectLockpickingEditorLibrary::BuildChestFamily()
{
#if WITH_EDITOR
    const FString Root = TEXT("/Game/_Game/Items/Chests/");
    auto* Native = LoadObject<UBlueprint>(nullptr, TEXT("/AscentCombatFramework/Integrations/Actors/ACF_Deposit_BP"));
    if (!Native || !Native->GeneratedClass) return TEXT("FAIL: Native deposit base missing.");
    auto Save = [](UBlueprint* BP)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error) return false;
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(BP->GetPackage(), BP, *FPackageName::LongPackageNameToFilename(BP->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    };
    auto Create = [&](const TCHAR* Name, UClass* Parent)
    {
        if (auto* BP = LoadObject<UBlueprint>(nullptr, *(Root + Name))) return BP;
        auto* BP = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*(Root + Name)), Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (BP) FAssetRegistryModule::AssetCreated(BP);
        return BP;
    };
    auto* Deposit = Create(TEXT("BP_EquipmentDeposit"), Native->GeneratedClass);
    if (!Deposit) return TEXT("FAIL: Deposit creation failed.");
    bool bHasProcedural = false;
    for (auto* Node : Deposit->SimpleConstructionScript->GetAllNodes())
        bHasProcedural |= Node->ComponentClass->IsChildOf(UProjectProceduralChestComponent::StaticClass());
    if (!bHasProcedural)
    {
        auto* Node = Deposit->SimpleConstructionScript->CreateNode(UProjectProceduralChestComponent::StaticClass(), TEXT("ProceduralChest"));
        Deposit->SimpleConstructionScript->AddNode(Node);
    }
    Deposit->ComponentClassOverrides.RemoveAll([](const FBPComponentClassOverride& Row) { return Row.ComponentName == TEXT("StorageComponent"); });
    Deposit->ComponentClassOverrides.Add(FBPComponentClassOverride(TEXT("StorageComponent"), UProjectChestInventoryComponent::StaticClass()));
    if (!FBlueprintEditorUtils::FindEventGraph(Deposit)) FKismetEditorUtilities::CreateDefaultEventGraphs(Deposit);
    auto* Graph = FBlueprintEditorUtils::FindEventGraph(Deposit);
    for (const FName Name : {FName(TEXT("OnInteractedByPawn")), FName(TEXT("OnLocalInteractedByPawn"))})
    {
        bool Exists = false;
        for (const auto& Node : Graph->Nodes)
            if (auto* Event = Cast<UK2Node_Event>(Node); Event && Event->EventReference.GetMemberName() == Name) Exists = true;
        if (Exists) continue;
        auto* Event = NewObject<UK2Node_Event>(Graph); Graph->AddNode(Event); Event->CreateNewGuid();
        Event->EventReference.SetExternalMember(Name, Native->GeneratedClass); Event->bOverrideFunction = true; Event->AllocateDefaultPins();
        auto* Prepare = NewObject<UK2Node_CallFunction>(Graph); Graph->AddNode(Prepare); Prepare->CreateNewGuid();
        Prepare->SetFromFunction(UProjectChestInventoryComponent::StaticClass()->FindFunctionByName(TEXT("PrepareInteraction"))); Prepare->AllocateDefaultPins();
        auto* Branch = NewObject<UK2Node_IfThenElse>(Graph); Graph->AddNode(Branch); Branch->CreateNewGuid(); Branch->AllocateDefaultPins();
        auto* Parent = NewObject<UK2Node_CallParentFunction>(Graph); Graph->AddNode(Parent); Parent->CreateNewGuid();
        Parent->SetFromFunction(Native->GeneratedClass->FindFunctionByName(Name)); Parent->AllocateDefaultPins();
        Event->FindPinChecked(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Prepare->GetExecPin());
        Prepare->GetThenPin()->MakeLinkTo(Branch->GetExecPin()); Prepare->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
        Branch->GetThenPin()->MakeLinkTo(Parent->GetExecPin());
        for (auto* Pin : Event->Pins)
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                if (auto* Destination = Parent->FindPin(Pin->PinName)) Pin->MakeLinkTo(Destination);
    }
    if (!Save(Deposit)) return TEXT("FAIL: Deposit compile/save failed.");
    auto* CDO = Cast<AActor>(Deposit->GeneratedClass->GetDefaultObject());
    if (!CDO || !Cast<UProjectChestInventoryComponent>(CDO->GetDefaultSubobjectByName(TEXT("StorageComponent")))) return TEXT("FAIL: Native storage override was not applied.");
    const TCHAR* ProfilePath = TEXT("/Game/_Game/Data/Equipment/DA_Chest_Default");
    auto* Profile = LoadObject<UProjectChestProfile>(nullptr, ProfilePath);
    if (!Profile)
    {
        auto* Catalog = LoadObject<UDataTable>(nullptr, TEXT("/Game/FullSample/GameplayTagTables/ItemsDB"));
        if (!Catalog || Catalog->GetRowStruct() != FItemGenerationSlot::StaticStruct()) return TEXT("FAIL: Native ACF item catalog missing.");
        TArray<FName> Names = Catalog->GetRowNames(); Names.Sort(FNameLexicalLess());
        if (Names.IsEmpty()) return TEXT("FAIL: Native ACF item catalog is empty.");
        const auto* Entry = Catalog->FindRow<FItemGenerationSlot>(Names[0], TEXT("Chest default profile"));
        Profile = NewObject<UProjectChestProfile>(CreatePackage(ProfilePath), TEXT("DA_Chest_Default"), RF_Public | RF_Standalone);
        Profile->ChestRarity = Entry->Rarity;
        FACFItemGenerationRule Rule; Rule.Category = Entry->Category; Rule.MinItemCount = 1; Rule.MaxItemCount = 3;
        Profile->ContentRules.Add(Rule); FAssetRegistryModule::AssetCreated(Profile);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Profile->GetPackage(), Profile, *FPackageName::LongPackageNameToFilename(ProfilePath, FPackageName::GetAssetPackageExtension()), Args)) return TEXT("FAIL: Chest profile save failed.");
    }
    CastChecked<UProjectChestInventoryComponent>(CDO->GetDefaultSubobjectByName(TEXT("StorageComponent")))->Profile = Profile;
    if (!Save(Deposit)) return TEXT("FAIL: Chest profile binding failed.");
    auto* Locked = Create(TEXT("BP_EquipmentLockedChest"), Deposit->GeneratedClass);
    if (!Locked) return TEXT("FAIL: Locked deposit creation failed.");
    USCS_Node* LockNode = nullptr;
    for (auto* Node : Locked->SimpleConstructionScript->GetAllNodes())
        if (Node->ComponentClass->IsChildOf(UProjectLockpickableComponent::StaticClass())) LockNode = Node;
    if (!LockNode)
    {
        LockNode = Locked->SimpleConstructionScript->CreateNode(UProjectLockpickableComponent::StaticClass(), TEXT("Lockpickable"));
        Locked->SimpleConstructionScript->AddNode(LockNode);
    }
    auto* Lock = CastChecked<UProjectLockpickableComponent>(LockNode->ComponentTemplate);
    Lock->bStartsLocked = true; Lock->bReplayOwnerACFInteractionOnSuccess = false; Lock->bExecuteOriginalOnSuccess = true;
    if (!UProjectLockpickingEditorLibrary::ConfigureBlueprintForComponentLockpicking(Locked) || !Save(Locked)) return TEXT("FAIL: Lock interaction gate compile/save failed.");
    return TEXT("PASS: Native deposit family authored with reusable storage and existing lockpicking gates.");
#else
    return TEXT("FAIL: Chest authoring requires the editor.");
#endif
}

FString UProjectLockpickingEditorLibrary::RemapChestCatalogs()
{
#if WITH_EDITOR
    const FString Target = TEXT("/Game/_Game/Items/Chests/BP_EquipmentLockedChest.BP_EquipmentLockedChest_C");
    int32 Changed = 0;
    for (const TCHAR* Path : {TEXT("/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector"), TEXT("/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy")})
    {
        auto* Asset = LoadObject<UObject>(nullptr, Path);
        if (!Asset) return TEXT("FAIL: Chest catalog referencer missing.");
        bool bChanged = false;
        for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
        {
            FString Before; It->ExportText_InContainer(0, Before, Asset, Asset, Asset, PPF_None);
            FString After = Before;
            for (const TCHAR* Name : {TEXT("BP_CalystoLockedChest"), TEXT("BP_CalystoLockPickChest")})
            {
                const FString Source = FString::Printf(TEXT("/Game/_Game/Items/Chests/%s.%s_C"), Name, Name);
                After.ReplaceInline(*Source, *Target, ESearchCase::CaseSensitive);
            }
            if (After == Before) continue;
            if (!It->ImportText_Direct(*After, It->ContainerPtrToValuePtr<void>(Asset), Asset, PPF_None)) return TEXT("FAIL: Chest catalog property import failed.");
            bChanged = true;
        }
        if (bChanged)
        {
            Asset->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!UPackage::SavePackage(Asset->GetPackage(), Asset, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args)) return TEXT("FAIL: Chest catalog save failed.");
            ++Changed;
        }
    }
    return FString::Printf(TEXT("PASS: Remapped chest references in %d catalogs; all other property text retained."), Changed);
#else
    return TEXT("FAIL: Chest catalog authoring requires the editor.");
#endif
}
