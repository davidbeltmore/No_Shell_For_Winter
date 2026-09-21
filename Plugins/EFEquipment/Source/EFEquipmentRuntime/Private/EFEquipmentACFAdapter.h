#pragma once
#include "CoreMinimal.h"
#include "Components/ACFInventoryComponent.h"

/** The single bounded compatibility surface for ACF's non-virtual storage/weight APIs. */
namespace EFEquipmentACF
{
    FACFInventoryList* Inventory(UACFInventoryComponent* Component);
    bool Capacity(UACFInventoryComponent* Component, double& Out);
    bool SetCapacity(UACFInventoryComponent* Component, double Value);
    bool ValidateInventory(UACFInventoryComponent* Component, FString& Error);
    bool Transfer(UACFInventoryComponent* Source, UACFInventoryComponent* Destination,
        FGuid Guid, int32 Count, bool bIgnoreDestinationWeight, FString& Error);
}
