#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/ACFEquipmentComponent.h"
#include "EFEquipmentFunctionLibrary.generated.h"

class UEFEquipmentCoordinatorComponent;
class UACFVendorComponent;

/** Blueprint-compatible entry points for the existing ACF inventory UI. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Equipment") static UEFEquipmentCoordinatorComponent* GetEquipmentCoordinator(AActor* Actor);
    UFUNCTION(BlueprintPure, Category="Equipment", meta=(WorldContext="WorldContextObject")) static UTexture2D* GetIconByTag(const UObject* WorldContextObject, const FGameplayTag& IconTag);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void UseInventoryItem(UACFEquipmentComponent* Target, const FInventoryItem& item);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void EquipItemFromInventory(UACFEquipmentComponent* Target, const FInventoryItem& inItem);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void EquipItemFromInventoryInSlot(UACFEquipmentComponent* Target, const FInventoryItem& inItem, FGameplayTag slot);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void DropItem(UACFEquipmentComponent* Target, const FInventoryItem& item, int32 count = 1);
    UFUNCTION(BlueprintPure, Category="Equipment") static float GetCurrentInventoryTotalWeight(UACFInventoryComponent* Target);
    UFUNCTION(BlueprintPure, Category="Equipment") static double GetBaseCarryCapacity(UACFInventoryComponent* Target);
    UFUNCTION(BlueprintPure, Category="Equipment") static FText GetInventoryWeightText(UACFInventoryComponent* Target);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void ManagerUseInventoryItem(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToUse);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void ManagerEquipItemFromInventoryInSlot(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToUse, FGameplayTag slot);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void ManagerDropItem(UObject* Target, UACFEquipmentComponent* targetComponent, const FInventoryItem& itemToDrop, int32 count = 1);
    UFUNCTION(BlueprintCallable, Category="Equipment") static void UnequipItemBySlot(UACFEquipmentComponent* Target, FGameplayTag itemSlot);
    UFUNCTION(BlueprintPure, Category="Equipment") static bool GetEquippedItemSlot(UACFEquipmentComponent* Target, FGameplayTag itemSlot, FEquippedItem& outSlot);
    UFUNCTION(BlueprintCallable, Category="Equipment|Trade") static void SellItemsToVendor(UACFVendorComponent* Target, const FInventoryItem& itemTobeSold, APawn* instigator, int32 count = 1);
};
