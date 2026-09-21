#pragma once
#include "CoreMinimal.h"

namespace EFEquipmentDeathRouting
{
    // Replaces one reflected callback at its original position, preserving all
    // other bindings. Uses the public multicast serialization contract, not layout offsets.
    bool Replace(UObject* Source, FName Event, UObject* Original, FName OriginalFunction, UObject* Replacement, FName ReplacementFunction);
    bool Contains(UObject* Source, FName Event, UObject* Receiver, FName Function);
}
