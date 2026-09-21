#pragma once
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentDeathTestTypes.generated.h"

/** Transient native delegate regression fixture, never used by authored content. */
UCLASS(Transient, NotBlueprintable)
class UEFEquipmentDeathTestObserver final : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() FEFEquipmentChanged OnOwnerDeath;
    TArray<int32> Calls;
    UFUNCTION() void First() { Calls.Add(1); }
    UFUNCTION() void Native() { Calls.Add(2); }
    UFUNCTION() void Last() { Calls.Add(3); }
    UFUNCTION() void Routed() { Calls.Add(8); Native(); Calls.Add(9); }
};
