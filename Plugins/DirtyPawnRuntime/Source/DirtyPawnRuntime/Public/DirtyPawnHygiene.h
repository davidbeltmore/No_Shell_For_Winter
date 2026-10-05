#pragma once
#include "CoreMinimal.h"

/** CPU-side surface intervals. Values are current opacity, not fade destinations.
 * Weight approximates body area; no material readback or mesh traversal. */
struct FDirtyPawnHygieneRegion
{
    double Weight=0, Mud=0, Sand=0, Snow=0, Dirt=0, Blood=0, Burn=0, CleanWater=0;
};

struct FDirtyPawnWaterContact
{
    float MinHeight=0, MaxHeight=0;
    bool bClean=false;
};
