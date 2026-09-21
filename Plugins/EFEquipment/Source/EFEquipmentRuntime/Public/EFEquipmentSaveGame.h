#pragma once
#include "CoreMinimal.h"
#include "ALSSaveGame.h"
#include "GameplayTagContainer.h"
#include "EFEquipmentSaveGame.generated.h"

/** Restoration intent, not a second inventory. ACF retains all object/fragment bytes. */
USTRUCT()
struct EFEQUIPMENTRUNTIME_API FEFEquipmentSavedPlayer
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Version = 1;
    UPROPERTY(SaveGame) double BaseCarryCapacity = -1;
    UPROPERTY(SaveGame) FName InventoryComponent;
    UPROPERTY(SaveGame) TMap<FGuid, FGameplayTag> EquippedSlots;
    /** Native weapon/projectile order matters because ranged equip may reload. */
    UPROPERTY(SaveGame) TArray<FGuid> EquipmentOrder;
    UPROPERTY(SaveGame) TArray<FGuid> DisplacedItems;
};

/** Extends the existing ALS slot; all native level/controller/state records remain intact. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentSaveGame : public UALSSaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 EquipmentVersion = 1;
    UPROPERTY(SaveGame) FEFEquipmentSavedPlayer EquipmentPlayer;
};
