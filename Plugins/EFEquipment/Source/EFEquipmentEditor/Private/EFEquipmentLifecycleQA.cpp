#include "EFEquipmentAuthoringLibrary.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "ACFVendorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"

UACFVendorComponent* UEFEquipmentAuthoringLibrary::CreateVendorFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE) return nullptr;
    FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
    auto* Owner = World->SpawnActor<AActor>(Params); if (!Owner) return nullptr;
    auto* Vendor = NewObject<UACFVendorComponent>(Owner); Owner->AddInstanceComponent(Vendor); Vendor->RegisterComponent();
    Vendor->SetCurrency(100000); return Vendor;
}

bool UEFEquipmentAuthoringLibrary::TriggerDeathFixture(UObject* WorldContextObject)
{
    auto* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World || World->WorldType != EWorldType::PIE) return false;
    auto* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
    auto* Coordinator = Pawn ? Pawn->FindComponentByClass<UEFEquipmentCoordinatorComponent>() : nullptr;
    if (!Coordinator || !Coordinator->HasNativeDeathAdapter()) return false;
    auto* Equipment = Coordinator->GetNativeEquipment();
    auto* Drop = FindFProperty<FBoolProperty>(Equipment->GetClass(), TEXT("bDropItemsOnDeath"));
    if (!Drop) return false;
    UActorComponent* Handler = nullptr;
    for (auto* Component : Pawn->GetComponents())
        for (auto* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
            if (Class->GetPathName() == TEXT("/Script/AscentCombatFramework.ACFDamageHandlerComponent")) { Handler = Component; break; }
    auto* Callback = Handler ? Handler->FindFunction(TEXT("HandleStatReachedZero")) : nullptr;
    auto* Alive = Handler ? FindFProperty<FBoolProperty>(Handler->GetClass(), TEXT("bIsAlive")) : nullptr;
    if (!Callback || Callback->NumParms != 0 || !Alive || !Alive->GetPropertyValue_InContainer(Handler)) return false;
    auto* ListProperty = FindFProperty<FStructProperty>(Equipment->GetClass(), TEXT("InventoryList"));
    if (!ListProperty || ListProperty->Struct != FACFInventoryList::StaticStruct()) return false;
    auto* List = ListProperty->ContainerPtrToValuePtr<FACFInventoryList>(Equipment);
    for (auto Entry : Equipment->GetInventory())
        if (auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
        { Entry.DropChancePercentage = 100; List->ChangeEntry(Entry); }
    Drop->SetPropertyValue_InContainer(Equipment, true);
    Handler->ProcessEvent(Callback, nullptr);
    return !Alive->GetPropertyValue_InContainer(Handler);
}
