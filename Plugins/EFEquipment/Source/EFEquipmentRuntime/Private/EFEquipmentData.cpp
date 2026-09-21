#include "EFEquipmentData.h"
#include "NativeGameplayTags.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFUnderwearTorso, "ItemSlot.Underwear.Torso");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFUnderwearLegs, "ItemSlot.Underwear.Legs");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFUpperArms, "ItemSlot.Armor.UpperArms");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFForearms, "ItemSlot.Armor.Forearms");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFGloves, "ItemSlot.Armor.Gloves");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFFeet, "ItemSlot.Armor.Feet");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFNecklace, "ItemSlot.Necklace");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFRing2, "ItemSlot.Ring2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EFSatchel, "ItemSlot.Satchel");

bool FEFEquipmentPolicy::SlotsConflict(TConstArrayView<FGameplayTag> A, TConstArrayView<FGameplayTag> B)
{
    for (const FGameplayTag Tag : A) if (B.Contains(Tag)) return true;
    return false;
}

bool FEFEquipmentPolicy::ValidateSlots(TConstArrayView<FEFEquipmentSlotDefinition> Slots, FString& Error)
{
    Error.Reset();
    if (Slots.IsEmpty()) { Error = TEXT("At least one equipment slot is required."); return false; }
    for (int32 I = 0; I < Slots.Num(); ++I)
    {
        if (!Slots[I].Slot.IsValid() || Slots[I].Label.IsEmpty())
        { Error = FString::Printf(TEXT("Slot %d requires a registered tag and label."), I); return false; }
        for (int32 J = I + 1; J < Slots.Num(); ++J)
            if (Slots[I].Slot.MatchesTag(Slots[J].Slot) || Slots[J].Slot.MatchesTag(Slots[I].Slot))
            { Error = FString::Printf(TEXT("Equipment slots must be distinct leaves: %s / %s."), *Slots[I].Slot.ToString(), *Slots[J].Slot.ToString()); return false; }
    }
    return true;
}

double FEFEquipmentPolicy::EffectiveContentWeight(double Original, double Reduction, bool bEquipped)
{
    return Original * (bEquipped ? 1.0 - FMath::Clamp(Reduction, 0.0, 0.5) : 1.0);
}

const FEFEquipmentSlotDefinition* UEFEquipmentSlotRegistry::Find(FGameplayTag Slot) const
{ return Slots.FindByPredicate([Slot](const auto& Definition) { return Definition.Slot == Slot; }); }

bool UEFEquipmentSlotRegistry::Validate(FString& Error) const
{
    if (!FEFEquipmentPolicy::ValidateSlots(Slots, Error)) return false;
    TSet<FString> Keys;
    for (const auto& Rule : MigrationRules)
    {
        const FString Key = Rule.ItemClass.ToString() + TEXT("|") + Rule.OldSlot.ToString();
        if (Rule.ItemClass.IsNull() || !Rule.OldSlot.IsValid() || !Find(Rule.NewSlot) || Keys.Contains(Key))
        { Error = TEXT("Migration rules require unique class/old-slot keys and registered destinations."); return false; }
        Keys.Add(Key);
    }
    return true;
}

bool UEFEquipmentRuleProfile::Validate(FString& Error) const
{
    Error.Reset(); TSet<FGameplayTag> Seen;
    for (FGameplayTag Tag : AdditionalReservations)
    {
        if (!Tag.IsValid() || Seen.Contains(Tag)) { Error = TEXT("Reservations must be distinct registered tags."); return false; }
        Seen.Add(Tag);
    }
    if (bContainer && (!FMath::IsFinite(ContentCapacity) || ContentCapacity <= 0 || !FMath::IsFinite(WeightReduction) || WeightReduction < 0 || WeightReduction > 0.5))
    { Error = TEXT("Container capacity must be positive and weight reduction must be between 0% and 50%."); return false; }
    return true;
}

#if WITH_EDITOR
EDataValidationResult UEFEquipmentSlotRegistry::IsDataValid(FDataValidationContext& Context) const
{
    FString Error; if (Validate(Error)) return EDataValidationResult::Valid;
    Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid;
}
EDataValidationResult UEFEquipmentRuleProfile::IsDataValid(FDataValidationContext& Context) const
{
    FString Error; if (Validate(Error)) return EDataValidationResult::Valid;
    Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid;
}
#endif
