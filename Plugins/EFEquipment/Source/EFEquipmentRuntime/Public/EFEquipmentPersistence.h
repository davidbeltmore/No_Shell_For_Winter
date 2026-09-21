#pragma once
#include "CoreMinimal.h"
#include "EFEquipmentSaveGame.h"

class UACFEquipmentComponent;
class UACFInventoryComponent;
class UEFEquipmentCoordinatorComponent;
class UEFEquipmentSlotRegistry;

/** Game-thread adapters around native ALS records. No disk publication occurs here. */
namespace EFEquipmentPersistence
{
    EFEQUIPMENTRUNTIME_API bool SerializeInventory(UACFEquipmentComponent* Inventory, TArray<uint8>& Bytes, FString& Error);
    EFEQUIPMENTRUNTIME_API bool DeserializeInventory(UACFEquipmentComponent* Inventory, const TArray<uint8>& Bytes, FString& Error);
    EFEQUIPMENTRUNTIME_API bool ValidateContents(UACFInventoryComponent* Inventory, FString& Error);
    EFEQUIPMENTRUNTIME_API bool ValidateActorInventories(AActor* Actor, FALSActorData& Record, FString& Error);
    EFEQUIPMENTRUNTIME_API TArray<FALSComponentData>* ComponentRecords(FALSActorData& Actor, FString& Error);
    /** Convert on isolated objects; only publish the candidate after validation succeeds. */
    EFEQUIPMENTRUNTIME_API UEFEquipmentSaveGame* ConvertSlot(UWorld* World, UALSSaveGame* Original, UEFEquipmentSlotRegistry* Registry, FString& Error);
    EFEQUIPMENTRUNTIME_API bool ValidateSlot(UWorld* World, UEFEquipmentSaveGame* Candidate, FString& Error);
    /** Replace just the native equipment record and versioned player metadata in a candidate slot. */
    EFEQUIPMENTRUNTIME_API bool CapturePlayer(UEFEquipmentSaveGame* Candidate, UEFEquipmentCoordinatorComponent* Coordinator, FALSPlayerData& Player, FString& Error);
    EFEQUIPMENTRUNTIME_API bool RestorePlayer(UEFEquipmentCoordinatorComponent* Coordinator, const FEFEquipmentSavedPlayer& Saved, FString& Error);
}
