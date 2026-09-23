#include "EFEquipmentData.h"
#include "EFEquipmentTypes.h"
#include "EFEquipmentTravelPolicy.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFEquipmentPolicyTest, "EF.Equipment.Policy.Contracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEFEquipmentPolicyTest::RunTest(const FString&)
{
    const auto Torso = FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Underwear.Torso"));
    const auto Legs = FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Underwear.Legs"));
    const auto Parent = FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Underwear"));
    TArray<FEFEquipmentSlotDefinition> Slots;
    FEFEquipmentSlotDefinition A; A.Slot = Torso; A.Label = FText::FromString(TEXT("Torso")); Slots.Add(A);
    FEFEquipmentSlotDefinition B; B.Slot = Legs; B.Label = FText::FromString(TEXT("Legs")); Slots.Add(B);
    FString Error;
    TestTrue(TEXT("Sibling destinations are valid"), FEFEquipmentPolicy::ValidateSlots(Slots, Error));
    Slots[1].Slot = Parent;
    TestFalse(TEXT("An equip destination cannot be an ancestor of another"), FEFEquipmentPolicy::ValidateSlots(Slots, Error));
    Slots[1].Slot = Torso;
    TestFalse(TEXT("Duplicate destinations are rejected"), FEFEquipmentPolicy::ValidateSlots(Slots, Error));
    const TArray<FGameplayTag> Bikini{Torso, Legs}, Bra{Torso}, Panty{Legs};
    TestTrue(TEXT("Bikini displaces Bra"), FEFEquipmentPolicy::SlotsConflict(Bikini, Bra));
    TestTrue(TEXT("Bra displaces the complete Bikini"), FEFEquipmentPolicy::SlotsConflict(Bra, Bikini));
    TestTrue(TEXT("Bikini displaces Panty"), FEFEquipmentPolicy::SlotsConflict(Bikini, Panty));
    TestFalse(TEXT("Independent underwear can coexist"), FEFEquipmentPolicy::SlotsConflict(Bra, Panty));
    TestEqual(TEXT("Equipped satchel reduces only content"), FEFEquipmentPolicy::EffectiveContentWeight(20, 0.2, true), 16.0);
    TestEqual(TEXT("Unequipped satchel content has full weight"), FEFEquipmentPolicy::EffectiveContentWeight(20, 0.2, false), 20.0);
    TestEqual(TEXT("Reduction ceiling is enforced"), FEFEquipmentPolicy::EffectiveContentWeight(20, 0.9, true), 10.0);
    auto* Profile = NewObject<UEFEquipmentRuleProfile>(); Profile->bContainer = true;
    TestTrue(TEXT("Default container profile is valid"), Profile->Validate(Error));
    Profile->WeightReduction = 0.51;
    TestFalse(TEXT("Authoring beyond ceiling is rejected, not silently certified"), Profile->Validate(Error));
    Profile->WeightReduction = 0.2; Profile->ContentCapacity = 0;
    TestFalse(TEXT("Zero capacity is rejected"), Profile->Validate(Error));
    using EFEquipmentTravelPolicy::IsPawnClassCompatible;
    const UClass* Pawn = APawn::StaticClass(); const UClass* Character = ACharacter::StaticClass();
    TestTrue(TEXT("Travel accepts the authored derived destination pawn"), IsPawnClassCompatible(Pawn, Character, Character, true));
    TestTrue(TEXT("Exact saved pawn class remains valid"), IsPawnClassCompatible(Pawn, Pawn, nullptr, false));
    TestFalse(TEXT("Disk save loading still rejects a different pawn class"), IsPawnClassCompatible(Pawn, Character, Character, false));
    TestFalse(TEXT("Travel rejects an unauthored subclass"), IsPawnClassCompatible(Pawn, Character, Pawn, true));
    TestFalse(TEXT("Travel cannot narrow a saved subclass to its parent"), IsPawnClassCompatible(Character, Pawn, Pawn, true));
    TestFalse(TEXT("Travel rejects a missing saved class"), IsPawnClassCompatible(nullptr, Character, Character, true));
    return true;
}
#endif
