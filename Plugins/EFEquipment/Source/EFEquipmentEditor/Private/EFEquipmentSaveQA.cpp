#include "EFEquipmentAuthoringLibrary.h"
#include "ALSSaveGameSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "EFEquipmentSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentPersistence.h"
#include "ALSFunctionLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/ScopeExit.h"
#include "EFEquipmentWorldItem.h"
#include "UObject/StrongObjectPtr.h"

namespace EFEquipmentSaveQA
{
    FString OriginalMetadata;
    FString Prefix;
}

FString UEFEquipmentAuthoringLibrary::BeginSaveFixture(UObject* WorldContextObject)
{
    using namespace EFEquipmentSaveQA;
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    auto* Property = FindFProperty<FStrProperty>(UALSSaveGameSettings::StaticClass(), TEXT("SavesMetadata"));
    if (!World || World->WorldType != EWorldType::PIE || !Property || !Prefix.IsEmpty()) return FString();
    Prefix = TEXT("EquipmentQA_PIE_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("_");
    auto* Settings = GetMutableDefault<UALSSaveGameSettings>();
    OriginalMetadata = Settings->GetSaveMetadataName();
    // Direct transient CDO assignment deliberately avoids PostEditChange/SaveConfig.
    Property->SetPropertyValue_InContainer(Settings, Prefix + TEXT("Metadata"));
    return Prefix;
}

bool UEFEquipmentAuthoringLibrary::EndSaveFixture()
{
    using namespace EFEquipmentSaveQA;
    if (Prefix.IsEmpty()) return true;
    auto* Property = FindFProperty<FStrProperty>(UALSSaveGameSettings::StaticClass(), TEXT("SavesMetadata"));
    if (!Property) return false;
    Property->SetPropertyValue_InContainer(GetMutableDefault<UALSSaveGameSettings>(), OriginalMetadata);
    bool bClean = true;
    for (const TCHAR* Suffix : {TEXT("Player"), TEXT("World"), TEXT("Legacy"), TEXT("Metadata")})
    {
        const FString Slot = Prefix + Suffix;
        if (UGameplayStatics::DoesSaveGameExist(Slot, 0)) bClean &= UGameplayStatics::DeleteGameInSlot(Slot, 0);
    }
    OriginalMetadata.Reset(); Prefix.Reset(); return bClean;
}

FString UEFEquipmentAuthoringLibrary::CreateLegacySaveFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return FString();
    auto* Controller = UGameplayStatics::GetPlayerController(World, 0);
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    auto* Coordinator = Pawn ? Pawn->FindComponentByClass<UEFEquipmentCoordinatorComponent>() : nullptr;
    auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    if (!Coordinator || !Registry) return FString();
    FString Error; TArray<uint8> Bytes;
    auto* Native = Coordinator->GetNativeEquipment();
    if (!Coordinator->FlushContainers(Error) || !EFEquipmentPersistence::SerializeInventory(Native, Bytes, Error)) return FString();
    FActorSpawnParameters Parameters; Parameters.ObjectFlags |= RF_Transient;
    auto* Owner = World->SpawnActor<AActor>(Parameters);
    if (!Owner) return FString();
    ON_SCOPE_EXIT { Owner->Destroy(); };
    auto* Scratch = NewObject<UACFEquipmentComponent>(Owner, Native->GetClass());
    auto* Property = FindFProperty<FStructProperty>(UACFInventoryComponent::StaticClass(), TEXT("InventoryList"));
    auto* Capacity = FindFProperty<FFloatProperty>(UACFInventoryComponent::StaticClass(), TEXT("MaxInventoryWeight"));
    if (!Property || Property->Struct != FACFInventoryList::StaticStruct() || !Capacity
        || !EFEquipmentPersistence::DeserializeInventory(Scratch, Bytes, Error)) return FString();
    Scratch->SetIsInitialized(true);
    auto* List = Property->ContainerPtrToValuePtr<FACFInventoryList>(Scratch);
    Capacity->SetPropertyValue_InContainer(Scratch, static_cast<float>(Coordinator->GetWeightSummary().BaseCapacity));
    int32 Fixtures = 0;
    for (const auto& Entry : Scratch->GetInventory())
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
        if (Rules && Rules->Profile && Rules->Profile->bContainer)
        { Scratch->RemoveItem(Entry, Entry.Count); continue; }
        List->MarkItemAsEquipped(Entry.GetItemGuid(), false, FGameplayTag());
        const FString ClassPath = Entry.ItemClass->GetPathName();
        if (ClassPath != TEXT("/Game/_Game/Clothes/UnderWear01/Bra.Bra_C")
            && ClassPath != TEXT("/Game/_Game/Clothes/Bikini01/Bikini.Bikini_C")
            && ClassPath != TEXT("/Game/_Game/Clothes/Rags01/RagShirt.RagShirt_C")) continue;
        FGameplayTag Slot = Entry.Item->GetPossibleItemSlots()[0];
        for (const auto& Rule : Registry->MigrationRules)
            if (Rule.ItemClass.ToSoftObjectPath() == FSoftObjectPath(Entry.ItemClass.Get())) { Slot = Rule.OldSlot; break; }
        List->MarkItemAsEquipped(Entry.GetItemGuid(), true, Slot); ++Fixtures;
    }
    if (Fixtures != 3 || !EFEquipmentPersistence::SerializeInventory(Scratch, Bytes, Error)) return FString();
    FALSPlayerData Player;
    Player.PlayerController = UALSFunctionLibrary::SerializeActor(Controller);
    Player.Pawn = UALSFunctionLibrary::SerializeActor(Pawn);
    auto* Records = EFEquipmentPersistence::ComponentRecords(Player.Pawn, Error);
    auto* Record = Records ? Records->FindByPredicate([&](const auto& Value) { return Value.alsName == Native->GetFName(); }) : nullptr;
    if (!Record) return FString();
    Record->Data = MoveTemp(Bytes);
    auto* Save = NewObject<UALSSaveGame>(); Save->StoreLocalPlayer(Player);
    const FString Slot = EFEquipmentSaveQA::Prefix + TEXT("Legacy");
    if (UGameplayStatics::DoesSaveGameExist(Slot, 0) || !UGameplayStatics::SaveGameToSlot(Save, Slot, 0)) return FString();
    return Slot;
}

bool UEFEquipmentAuthoringLibrary::SaveWorldFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return false;
    return World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>()->Save(EFEquipmentSaveQA::Prefix + TEXT("World"), true, true, false, TEXT("Equipment QA world"));
}

bool UEFEquipmentAuthoringLibrary::LoadWorldFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return false;
    auto* Save = World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>();
    Save->LoadWorld(EFEquipmentSaveQA::Prefix + TEXT("World"), FOnLoadFinished(), true);
    return Save->IsBusy();
}

bool UEFEquipmentAuthoringLibrary::IsSaveFixtureBusy(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    return !World || World->WorldType != EWorldType::PIE || World->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>()->IsBusy();
}

FString UEFEquipmentAuthoringLibrary::ProbePickupNameReuse(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE || EFEquipmentSaveQA::Prefix.IsEmpty()) return TEXT("FAIL: PIE save fixture required.");
    auto* Native = World->GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>();
    auto* Property = FindFProperty<FObjectPropertyBase>(UALSLoadAndSaveSubsystem::StaticClass(), TEXT("currentSavegame"));
    if (!Native || Native->GetSystemState() != ELoadingState::EIdle || !Property || Property->PropertyClass != UALSSaveGame::StaticClass())
        return TEXT("FAIL: Native save subsystem is not idle.");
    TStrongObjectPtr<UALSSaveGame> Original(Native->GetCurrentSaveGame());
    FALSLevelData Level;
    const FString LevelName = UGameplayStatics::GetCurrentLevelName(World, true);
    if (!Original || !Original->TryGetLevelData(LevelName, Level)) return TEXT("FAIL: Saved world record is missing.");
    auto Records = Level.GetActorsCopy();
    auto* Record = Records.FindByPredicate([](const auto& Entry) { return Entry.Class && Entry.Class->IsChildOf(AEFEquipmentWorldItem::StaticClass()); });
    if (!Record) return TEXT("FAIL: Saved world pickup record is missing.");
    Record->alsName = FName(*(TEXT("EquipmentQA_NameReuse_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Level.CleanActors(); Level.AddActorRecord(*Record);
    TStrongObjectPtr<UALSSaveGame> Candidate(DuplicateObject<UALSSaveGame>(Original.Get(), GetTransientPackage()));
    Candidate->AddLevel(LevelName, Level);
    Property->SetObjectPropertyValue_InContainer(Native, Candidate.Get());
    ON_SCOPE_EXIT { Property->SetObjectPropertyValue_InContainer(Native, Original.Get()); };
    FActorSpawnParameters Parameters; Parameters.ObjectFlags |= RF_Transient; Parameters.Name = Record->alsName;
    Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
    auto* Pickup = World->SpawnActor<AEFEquipmentWorldItem>(Record->Class, FTransform::Identity, Parameters);
    if (!Pickup) return TEXT("FAIL: Exact-name pickup spawn failed.");
    ON_SCOPE_EXIT { Pickup->Destroy(); };
    const int32 AutomaticItems = Pickup->GetItems().Num();
    const bool ExplicitLoad = Native->LoadActor(Pickup);
    if (!ExplicitLoad || Pickup->GetItems().IsEmpty()) return TEXT("FAIL: Name collision fixture did not match the saved native record.");
    return AutomaticItems == 0 ? TEXT("PASS: Fresh pickup stayed empty; explicit ALS load still restores the exact saved record.")
        : FString::Printf(TEXT("FAIL: Fresh pickup automatically replayed %d saved entries by actor name."), AutomaticItems);
}
