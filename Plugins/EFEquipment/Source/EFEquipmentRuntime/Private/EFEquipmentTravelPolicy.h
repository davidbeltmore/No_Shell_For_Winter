#pragma once

#include "UObject/Class.h"

namespace EFEquipmentTravelPolicy
{
    // Only in-memory travel may restore inherited save fields into the exact
    // pawn class selected by the destination GameMode. Disk saves stay exact.
    inline bool IsPawnClassCompatible(const UClass* Saved, const UClass* Destination,
        const UClass* AuthoredDestination, const bool bTravel)
    {
        return Saved && Destination && (Saved == Destination ||
            (bTravel && Destination == AuthoredDestination && Destination->IsChildOf(Saved)));
    }
}
