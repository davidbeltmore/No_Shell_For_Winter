#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentSaveSubsystem.h"
#include "EFEquipmentWorldItem.h"
#include "ANSUIPlayerSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace EFEquipmentPackagedQA
{
    // Explicit Development-only harness. Normal sessions never create this state.
    struct FRun
    {
        double Started = FPlatformTime::Seconds(), OutfitStarted = 0, Due = 0;
        int32 Phase = 0;
        FString Directory;
        FGuid Bag, Child;
        TWeakObjectPtr<APawn> Pawn;
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Checks;

        void Write(const FString& Status)
        {
            Report->SetStringField(TEXT("status"), Status);
            Report->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds() - Started);
            Report->SetArrayField(TEXT("checks"), Checks);
            FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
            FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("result.json")));
        }
        bool Check(bool Value, const FString& Name)
        {
            auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"), Name); Row->SetBoolField(TEXT("pass"), Value);
            Checks.Add(MakeShared<FJsonValueObject>(Row));
            if (!Value) { Report->SetStringField(TEXT("error"), Name); Write(TEXT("FAIL")); FPlatformMisc::RequestExit(false); }
            return Value;
        }
        FString ClothingSummary() const
        {
            if (!Pawn.IsValid()) return FString();
            for (auto* Component : Pawn->GetComponents())
            {
                if (!Component->GetClass()->GetPathName().StartsWith(TEXT("/Script/EFClothingMorphRuntime."))) continue;
                auto* Function = Component->FindFunction(TEXT("GetDebugSummary"));
                auto* Return = Function ? CastField<FStrProperty>(Function->GetReturnProperty()) : nullptr;
                if (!Function || Function->NumParms != 1 || !Return) continue;
                FStructOnScope Parameters(Function); Component->ProcessEvent(Function, Parameters.GetStructMemory());
                return Return->GetPropertyValue_InContainer(Parameters.GetStructMemory());
            }
            return FString();
        }
        bool Tick(float)
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - Started > 240) { Check(false, TEXT("Packaged equipment QA exceeded its bounded deadline.")); return false; }
            if (Now < Due) return true;
            if (Phase == 0)
            {
                if (!GEngine) return true;
                for (const auto& Context : GEngine->GetWorldContexts())
                    if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->HasBegunPlay())
                        if (auto* Player = UGameplayStatics::GetPlayerPawn(Context.World(), 0)) { Pawn = Player; break; }
                if (!Pawn.IsValid()) return true;
                Phase = 1; Due = Now + 3; return true;
            }
            if (!Pawn.IsValid()) { Check(false, TEXT("Packaged HUB player is no longer available.")); return false; }
            auto* World = Pawn->GetWorld(); auto* Game = World->GetGameInstance();
            auto* Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn.Get());
            if (!Coordinator || !Coordinator->InitializeEquipment()) { Check(false, TEXT("Cooked equipment registry could not initialize.")); return false; }
            auto* Native = Coordinator->GetNativeEquipment();
            if (Phase == 1)
            {
                OutfitStarted = FPlatformTime::Seconds();
                Check(true, TEXT("Cooked player uses the authored equipment registry."));
                for (const auto& Entry : Native->GetInventory()) if (Entry.bIsEquipped) Coordinator->RequestUnequip(Entry.GetItemGuid());
                const TCHAR* Paths[] = {
                    TEXT("/Game/_Game/Clothes/Rags01/RagShirt.RagShirt_C"), TEXT("/Game/_Game/Clothes/Rags01/RagPants.RagPants_C"),
                    TEXT("/Game/_Game/Clothes/Bikini01/Bikini.Bikini_C"), TEXT("/Game/_Game/Items/Equipment/BP_Satchel2.BP_Satchel2_C"),
                    TEXT("/Game/_Game/Clothes/UnderWear01/Panty.Panty_C")};
                for (int32 Index = 0; Index < UE_ARRAY_COUNT(Paths); ++Index)
                {
                    auto* Class = LoadClass<UACFItem>(nullptr, Paths[Index]);
                    if (!Check(Class != nullptr, FString(TEXT("Cooked item exists: ")) + Paths[Index])) return false;
                    TArray<FInventoryItem> Entries; Native->GetAllItemsOfClassInInventory(Class, Entries);
                    if (Entries.IsEmpty()) { Native->AddItemToInventoryByClass(Class, 1, false); Native->GetAllItemsOfClassInInventory(Class, Entries); }
                    if (!Check(!Entries.IsEmpty(), TEXT("Packaged fixture owns its requested item."))) return false;
                    const FGuid Guid = Entries[0].GetItemGuid();
                    if (Index < 4 && !Check(Coordinator->RequestEquip(Guid, FGameplayTag()).bSuccess, TEXT("Cooked item equips through native coordinator."))) return false;
                    if (Index == 3) Bag = Guid; else if (Index == 4) Child = Guid;
                }
                Phase = 2; return true;
            }
            if (Phase == 2)
            {
                const FString Summary = ClothingSummary(); Report->SetStringField(TEXT("clothing"), Summary);
                if (!Summary.Contains(TEXT("managed=3 ready=3 warming=0 passthrough=0 issues=0")))
                {
                    if (Now - OutfitStarted > 30) { Check(false, TEXT("First packaged outfit did not reach Ready: ") + Summary); return false; }
                    return true;
                }
                Report->SetNumberField(TEXT("first_process_outfit_ready_s"), Now - OutfitStarted);
                Report->SetStringField(TEXT("latency_scope"), TEXT("First outfit in a fresh packaged process, including item-class loading; OS caches are not flushed."));
                if (!Check(Coordinator->MoveToContainer(Child, Bag, 1).bSuccess, TEXT("Packaged satchel owns the transferred item."))) return false;
                const double Capacity = Coordinator->GetWeightSummary().EffectiveWeight - .125;
                Coordinator->SetBaseCarryCapacity(Capacity);
                auto* Save = Game->GetSubsystem<UEFEquipmentSaveSubsystem>();
                if (!Check(Save != nullptr, TEXT("Packaged equipment save subsystem is available."))) return false;
                if (!Check(Coordinator->GetWeightSummary().bOverburdened && Save->Save(TEXT("EquipmentQA_Packaged"), false, true, false, TEXT("Isolated packaged equipment QA")),
                    TEXT("Packaged player saves a full satchel while overburdened: ") + Save->GetLastError().ToString())) return false;
                if (!Check(Coordinator->MoveFromContainer(Bag, Child, 1).bSuccess, TEXT("Packaged extraction is allowed while overburdened."))) return false;
                Coordinator->SetBaseCarryCapacity(180);
                if (!Check(Save->LoadLocal(TEXT("EquipmentQA_Packaged"), false), TEXT("Packaged save restores: ") + Save->GetLastError().ToString())) return false;
                if (!Check(Coordinator->GetWeightSummary().bOverburdened && FMath::IsNearlyEqual(Coordinator->GetWeightSummary().BaseCapacity, Capacity, .000001)
                    && Coordinator->GetContainerItems(Bag).ContainsByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Child; }),
                    TEXT("Packaged load conserves child GUID, separate capacity and legitimate overload."))) return false;
                Coordinator->SetBaseCarryCapacity(180);
                if (!Check(Coordinator->RequestDrop(Bag).bSuccess, TEXT("Cooked full-bag world drop succeeds."))) return false;
                TArray<AEFEquipmentWorldItem*> Pickups;
                for (TActorIterator<AEFEquipmentWorldItem> It(World); It; ++It)
                    if (It->GetItems().ContainsByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Bag; })) Pickups.Add(*It);
                if (!Check(Pickups.Num() == 1 && Pickups[0]->GetItems().Num() == 1, TEXT("Cooked drop has one bag and no duplicate child loot."))) return false;
                IACFInteractableInterface::Execute_OnInteractedByPawn(Pickups[0], Pawn.Get(), TEXT(""));
                if (!Check(Coordinator->GetContainerItems(Bag).ContainsByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Child; })
                    && Coordinator->RequestEquip(Bag, FGameplayTag()).bSuccess, TEXT("Cooked Blueprint pickup preserves contained GUID and reequips."))) return false;
                auto* MenuClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_InGameMenu_WB.ACF_InGameMenu_WB_C"));
                auto* UI = Game->GetSubsystem<UANSUIPlayerSubsystem>();
                if (!Check(UI && MenuClass && UI->SpawnInGameWidget(MenuClass, false, true), TEXT("Original cooked inventory menu opens."))) return false;
                Phase = 3; Due = Now + 3; Write(TEXT("PENDING")); return true;
            }
            if (Phase == 3)
            {
                FScreenshotRequest::RequestScreenshot(Directory / TEXT("inventory.png"), true, false);
                Phase = 4; Due = Now + 2; return true;
            }
            if (Phase == 4)
            {
                if (!Check(IFileManager::Get().FileSize(*(Directory / TEXT("inventory.png"))) > 0, TEXT("Real packaged inventory screenshot was captured."))) return false;
                Phase = 5; Write(TEXT("AWAITING_VISUAL_REVIEW")); return true;
            }
            FString Review;
            if (FFileHelper::LoadFileToString(Review, *(Directory / TEXT("visual_done.txt"))))
            {
                if (!Check(Review.StartsWith(TEXT("PASS:")), TEXT("Packaged screenshot was visually reviewed: ") + Review)) return false;
                Write(TEXT("PASS")); FPlatformMisc::RequestExit(false); return false;
            }
            return true;
        }
    };
    TUniquePtr<FRun> Run;
    FTSTicker::FDelegateHandle Handle;
}
#endif

void StartEFEquipmentPackagedQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    using namespace EFEquipmentPackagedQA;
    if (GIsEditor || !FParse::Param(FCommandLine::Get(), TEXT("EFEquipmentQA"))) return;
    FString UserDirectory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory) || UserDirectory.IsEmpty()
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), FPaths::ConvertRelativePathToFull(UserDirectory)))
    { UE_LOG(LogTemp, Error, TEXT("Equipment packaged QA requires an explicit isolated UserDir.")); FPlatformMisc::RequestExit(false); return; }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("EquipmentQA/Packaged");
    if (IFileManager::Get().DirectoryExists(*Directory))
    { UE_LOG(LogTemp, Error, TEXT("Equipment packaged QA requires a fresh UserDir.")); FPlatformMisc::RequestExit(false); return; }
    IFileManager::Get().MakeDirectory(*Directory, true);
    Run = MakeUnique<FRun>(); Run->Directory = Directory; Run->Write(TEXT("PENDING"));
    Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Delta) { return Run && Run->Tick(Delta); }), .1f);
#endif
}
void StopEFEquipmentPackagedQA()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
    FTSTicker::GetCoreTicker().RemoveTicker(EFEquipmentPackagedQA::Handle); EFEquipmentPackagedQA::Run.Reset();
#endif
}
