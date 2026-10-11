#include "Characters/ProjectNPCOutfit.h"

bool UProjectNPCOutfit::Validate(FString& Error) const
{
    if (Id.IsNone() || Items.IsEmpty() || Items.Num() > 16)
    { Error = TEXT("Outfit needs a stable ID and 1..16 items."); return false; }
    TSet<FSoftObjectPath> Classes;
    TArray<FGameplayTag> Slots;
    for (const auto& Row : Items)
    {
        if (Row.Item.IsNull() || !Row.Destination.IsValid() || Classes.Contains(Row.Item.ToSoftObjectPath()))
        { Error = TEXT("Outfit contains a missing or duplicate item or destination."); return false; }
        for (const auto Slot : Slots)
            if (Slot.MatchesTag(Row.Destination) || Row.Destination.MatchesTag(Slot))
            { Error = TEXT("Outfit destinations overlap."); return false; }
        Classes.Add(Row.Item.ToSoftObjectPath()); Slots.Add(Row.Destination);
    }
    return true;
}
