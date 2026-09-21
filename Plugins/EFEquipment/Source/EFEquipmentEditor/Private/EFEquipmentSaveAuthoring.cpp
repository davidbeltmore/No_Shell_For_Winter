#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentSaveLibrary.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

FString UEFEquipmentAuthoringLibrary::BuildSaveIntegration()
{
    const TCHAR* Paths[] = {
        TEXT("/Game/FullSample/Blueprints/Game/ACFSaveGameActorBP"),
        TEXT("/Game/FullSample/Integrations/SharedAssets/Save/ACF_SavePage_WB"),
        TEXT("/Game/FullSample/Integrations/SharedAssets/Save/ACF_LoadPage_WB"),
        TEXT("/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerControllerBP"),
        TEXT("/Game/FullSample/Integrations/ATSIntegrations/FSM/FSM_LoadGame_BP")};
    const TSet<FName> Names = {TEXT("SaveGameWorld"), TEXT("SaveGameWorldInCurrentSlot"), TEXT("SaveLocalPlayer"),
        TEXT("LoadLocalPlayer"), TEXT("LoadGameWorld"), TEXT("TravelLocalPlayer"), TEXT("LoadTraveledPlayer"), TEXT("GetCurrentSaveName")};
    int32 Routed = 0, Guards = 0;
    for (const TCHAR* Path : Paths)
    {
        auto* BP = LoadObject<UBlueprint>(nullptr, Path);
        if (!BP) return TEXT("FAIL: Missing save consumer ") + FString(Path);
        bool Changed = false; TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs)
        {
            const auto Nodes = Graph->Nodes;
            for (const auto& Node : Nodes)
            {
                if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Names.Contains(Call->FunctionReference.GetMemberName())
                    && Call->FunctionReference.GetMemberParentClass() == UALSLoadAndSaveSubsystem::StaticClass())
                {
                    auto* Function = UEFEquipmentSaveLibrary::StaticClass()->FindFunctionByName(Call->FunctionReference.GetMemberName());
                    if (!Function) return TEXT("FAIL: Save wrapper signature is missing.");
                    if (auto* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self)) Self->PinName = TEXT("Target");
                    Call->SetFromFunction(Function); Call->ReconstructNode(); Changed = true; ++Routed;
                }
                auto* Event = Cast<UK2Node_CustomEvent>(Node);
                if (!Event || !(FString(Path).EndsWith(TEXT("ACF_SavePage_WB")) || FString(Path).EndsWith(TEXT("ACF_LoadPage_WB")))
                    || (Event->CustomFunctionName != TEXT("OnSaved") && Event->CustomFunctionName != TEXT("OnLoaded"))) continue;
                auto* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then); auto* Success = Event->FindPin(TEXT("Success"));
                if (!Then || !Success) return TEXT("FAIL: The native save-result event contract changed.");
                if (Then->LinkedTo.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin->GetOwningNode()->NodeComment == TEXT("EFEquipment verified save result"); })) continue;
                auto* Report = NewObject<UK2Node_CallFunction>(Graph);
                Report->SetFromFunction(UEFEquipmentSaveLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UEFEquipmentSaveLibrary, ReportSaveResult)));
                Graph->AddNode(Report, false, false); Report->CreateNewGuid(); Report->AllocateDefaultPins();
                Report->NodeComment = TEXT("EFEquipment verified save result"); Report->NodePosX = Event->NodePosX + 240; Report->NodePosY = Event->NodePosY;
                auto* Branch = NewObject<UK2Node_IfThenElse>(Graph); Graph->AddNode(Branch, false, false); Branch->CreateNewGuid(); Branch->AllocateDefaultPins();
                Branch->NodePosX = Event->NodePosX + 480; Branch->NodePosY = Event->NodePosY;
                const auto Continuations = Then->LinkedTo; Then->BreakAllPinLinks(); Then->MakeLinkTo(Report->GetExecPin());
                Success->MakeLinkTo(Report->FindPinChecked(TEXT("bSuccess"))); Report->GetThenPin()->MakeLinkTo(Branch->GetExecPin());
                Report->GetReturnValuePin()->MakeLinkTo(Branch->GetConditionPin());
                for (auto* Pin : Continuations) Branch->GetThenPin()->MakeLinkTo(Pin);
                Changed = true; ++Guards;
            }
        }
        if (!Changed) continue;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error) return TEXT("FAIL: Save consumer compile failed: ") + FString(Path);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(BP->GetOutermost(), BP, *FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args))
            return TEXT("FAIL: Save consumer publication failed: ") + FString(Path);
    }
    return FString::Printf(TEXT("PASS: Routed %d native save/load calls and guarded %d native UI callbacks."), Routed, Guards);
}
