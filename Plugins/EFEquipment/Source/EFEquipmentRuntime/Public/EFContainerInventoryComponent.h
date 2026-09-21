#pragma once

#include "CoreMinimal.h"
#include "Components/ACFInventoryComponent.h"
#include "EFContainerInventoryComponent.generated.h"

/** A real ACF inventory, not a second list of copies in the root inventory. */
UCLASS(ClassGroup=(Equipment))
class EFEQUIPMENTRUNTIME_API UEFContainerInventoryComponent : public UACFInventoryComponent
{
    GENERATED_BODY()
public:
    UEFContainerInventoryComponent();
    UPROPERTY(Transient) FGuid ContainerGuid;
    void InitializeContainer(FGuid Guid, double Capacity);
    bool WritePayload(TArray<uint8>& Bytes, FString& Error);
    bool ReadPayload(const TArray<uint8>& Bytes, FString& Error);
    void ReserveEntryCapacity(int32 AdditionalEntries);
    double GetContentCapacity() const { return ContentCapacity; }
private:
    double ContentCapacity = 0;
};
