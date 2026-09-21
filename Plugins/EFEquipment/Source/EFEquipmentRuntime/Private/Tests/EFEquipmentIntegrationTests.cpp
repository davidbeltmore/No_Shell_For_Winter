#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFEquipmentIntegrationTest, "EF.Equipment.Native.Vertical", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFEquipmentIntegrationTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("Native fixture world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    struct FCleanup { UWorld* World; ~FCleanup() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); } } Cleanup{World};
    auto* Pawn = World->SpawnActor<APawn>();
    auto* Native = NewObject<UACFEquipmentComponent>(Pawn); Pawn->AddInstanceComponent(Native); Native->RegisterComponent();
    EFEquipmentACF::Inventory(Native)->Init(Pawn); Native->SetIsInitialized(true);
    auto* Coordinator = NewObject<UEFEquipmentCoordinatorComponent>(Pawn); Pawn->AddInstanceComponent(Coordinator); Coordinator->RegisterComponent();
    if (!TestTrue(TEXT("Initialize authored registry"), Coordinator->InitializeEquipment())) return false;
    auto Add = [&](const TCHAR* Path, int32 Count = 1) -> FInventoryItem
    {
        UClass* Class = LoadClass<UACFItem>(nullptr, Path);
        if (!TestNotNull(Path, Class)) return FInventoryItem();
        FInventoryItem Entry; Entry.ItemClass = Class; Entry.Item = NewObject<UACFItem>(Pawn, Class); Entry.Item->SetItemOwner(Pawn);
        Entry.Count = Count; Entry.ResetGuid(); EFEquipmentACF::Inventory(Native)->AddEntry(Entry); Native->RegisterFragmentsForItem(Entry.Item);
        Coordinator->RefreshState(); return Entry;
    };
    auto Bra = Add(TEXT("/Game/_Game/Clothes/UnderWear01/Bra.Bra_C"));
    auto Panty = Add(TEXT("/Game/_Game/Clothes/UnderWear01/Panty.Panty_C"));
    auto Bikini = Add(TEXT("/Game/_Game/Clothes/Bikini01/Bikini.Bikini_C"));
    auto Shirt = Add(TEXT("/Game/_Game/Clothes/Rags01/RagShirt.RagShirt_C"));
    auto Pants = Add(TEXT("/Game/_Game/Clothes/Rags01/RagPants.RagPants_C"));
    auto Plate = Add(TEXT("/Game/_Game/Items/Equipment/BP_ExclusiveTorsoPrototype.BP_ExclusiveTorsoPrototype_C"));
    auto Bag = Add(TEXT("/Game/_Game/Items/Equipment/BP_Satchel2.BP_Satchel2_C"));
    if (HasAnyErrors()) return false;
    auto Equip = [&](const FInventoryItem& Entry) { return Coordinator->RequestEquip(Entry.GetItemGuid(), FGameplayTag()); };
    auto IsEquipped = [&](const FInventoryItem& Entry) { FInventoryItem Live; return Native->GetItemByGuid(Entry.GetItemGuid(), Live) && Live.bIsEquipped; };
    Native->EquipItemFromInventory(Bra);
    TestFalse(TEXT("Direct native caller is vetoed"), IsEquipped(Bra));
    TestTrue(TEXT("Bra equips through coordinator"), Equip(Bra).bSuccess);
    TestTrue(TEXT("Shirt layers over Bra"), Equip(Shirt).bSuccess && IsEquipped(Bra));
    TestTrue(TEXT("Panty equips"), Equip(Panty).bSuccess);
    TestTrue(TEXT("Pants layers over Panty"), Equip(Pants).bSuccess && IsEquipped(Panty));
    TestTrue(TEXT("Bikini replaces both underwear items"), Equip(Bikini).bSuccess && !IsEquipped(Bra) && !IsEquipped(Panty));
    TestTrue(TEXT("Bikini keeps exterior clothing"), IsEquipped(Shirt) && IsEquipped(Pants));
    TestTrue(TEXT("Bra removes whole Bikini"), Equip(Bra).bSuccess && !IsEquipped(Bikini));
    TestTrue(TEXT("Plate replaces Bra and Shirt"), Equip(Plate).bSuccess && !IsEquipped(Bra) && !IsEquipped(Shirt));
    TestTrue(TEXT("Reverse conflict Bra replaces Plate"), Equip(Bra).bSuccess && !IsEquipped(Plate));
    Equip(Shirt);
    for (int32 Fault : {1, 2})
    {
        Coordinator->TestFailurePoint = Fault;
        TestFalse(TEXT("Injected equip failure is reported"), Equip(Plate).bSuccess);
        TestTrue(TEXT("Failed equip restores both displaced pieces"), IsEquipped(Bra) && IsEquipped(Shirt) && !IsEquipped(Plate));
    }
    const int64 StableRevision = Coordinator->GetRevision(); Coordinator->RefreshState();
    TestEqual(TEXT("Read refresh does not invalidate a preview"), Coordinator->GetRevision(), StableRevision);
    const auto Preview = Coordinator->PreviewEquip(Bikini.GetItemGuid(), FGameplayTag());
    Equip(Panty);
    TestFalse(TEXT("Stale preview is rejected"), Coordinator->RequestEquip(Bikini.GetItemGuid(), Preview.SelectedSlot, Preview.Revision).bSuccess);
    TestTrue(TEXT("Satchel equips"), Equip(Bag).bSuccess);
    const double Before = Coordinator->GetWeightSummary().EffectiveWeight;
    TestTrue(TEXT("Move actual Bra instance into satchel"), Coordinator->MoveToContainer(Bra.GetItemGuid(), Bag.GetItemGuid(), 1).bSuccess);
    FInventoryItem Live;
    TestFalse(TEXT("Transferred GUID is absent from root"), Native->GetItemByGuid(Bra.GetItemGuid(), Live));
    auto Contents = Coordinator->GetContainerItems(Bag.GetItemGuid());
    TestTrue(TEXT("Container owns same GUID and object"), Contents.Num() == 1 && Contents[0].GetItemGuid() == Bra.GetItemGuid() && Contents[0].Item == Bra.Item);
    TestTrue(TEXT("Only contents receive 20 percent reduction"), FMath::IsNearlyEqual(Coordinator->GetWeightSummary().EffectiveWeight, Before - 0.1, 0.00001));
    TestFalse(TEXT("Container item cannot equip directly"), Equip(Bra).bSuccess);
    TestFalse(TEXT("Nested container rejected"), Coordinator->MoveToContainer(Bag.GetItemGuid(), Bag.GetItemGuid(), 1).bSuccess);
    Coordinator->SetBaseCarryCapacity(Coordinator->GetWeightSummary().EffectiveWeight);
    TestFalse(TEXT("Exact fractional capacity is not overloaded"), Coordinator->GetWeightSummary().bOverburdened);
    TestTrue(TEXT("Unequipping a full bag is allowed"), Coordinator->RequestUnequip(Bag.GetItemGuid()).bSuccess);
    TestTrue(TEXT("Unequipping exposes original content weight"), Coordinator->GetWeightSummary().bOverburdened);
    TestTrue(TEXT("Overburdened extraction is allowed"), Coordinator->MoveFromContainer(Bag.GetItemGuid(), Bra.GetItemGuid(), 1).bSuccess);
    TestTrue(TEXT("Extraction retains item identity"), Native->GetItemByGuid(Bra.GetItemGuid(), Live) && Live.Item == Bra.Item);
    TestEqual(TEXT("All root GUIDs conserved"), Native->GetInventory().Num(), 7);
    FString Error;
    TestTrue(TEXT("Reinsert while overloaded does not increase weight"), Coordinator->MoveToContainer(Bra.GetItemGuid(), Bag.GetItemGuid(), 1).bSuccess);
    auto* Container = Coordinator->ResolveContainer(Bag.GetItemGuid(), Error); TArray<uint8> Bytes;
    TestTrue(TEXT("ACF container serialization"), Container && Container->WritePayload(Bytes, Error));
    auto* Rehydrated = NewObject<UEFContainerInventoryComponent>(Pawn); Rehydrated->InitializeContainer(Bag.GetItemGuid(), 20);
    TestTrue(TEXT("ACF container load"), Rehydrated->ReadPayload(Bytes, Error));
    const auto Loaded = Rehydrated->GetInventory();
    TestTrue(TEXT("Save/load preserves GUID, class and count"), Loaded.Num() == 1 && Loaded[0].GetItemGuid() == Bra.GetItemGuid() && Loaded[0].ItemClass == Bra.ItemClass && Loaded[0].Count == 1);
    auto Corrupt = Bytes; Corrupt.Last() ^= 0x80;
    TestFalse(TEXT("Corrupt container envelope is rejected"), Rehydrated->ReadPayload(Corrupt, Error));
    TestTrue(TEXT("Rejected payload preserves the prior live contents"), Rehydrated->GetInventory().Num() == 1
        && Rehydrated->GetInventory()[0].GetItemGuid() == Bra.GetItemGuid());

    Coordinator->SetBaseCarryCapacity(180.125);
    auto Stack = Add(TEXT("/Script/InventorySystem.ACFItem"), 8);
    auto StackInfo = Stack.Item->GetItemInfo(); StackInfo.ItemWeight = 0.25f; StackInfo.MaxInventoryStack = 100;
    Stack.Item->SetItemDescriptor(StackInfo); Coordinator->RefreshState();
    const auto Split = Coordinator->MoveToContainer(Stack.GetItemGuid(), Bag.GetItemGuid(), 2);
    TestTrue(TEXT("Partial stack receipt records both GUIDs and quantities"), Split.bSuccess
        && Split.SourceGuid == Stack.GetItemGuid() && Split.DestinationGuid.IsValid()
        && Split.SourceGuid != Split.DestinationGuid && Split.MovedQuantity == 2 && Split.SourceRemainingQuantity == 6);
    FInventoryItem OriginalStack, MovedStack;
    TestTrue(TEXT("Split conserves root and container quantities"), Native->GetItemByGuid(Stack.GetItemGuid(), OriginalStack)
        && Container->GetItemByGuid(Split.DestinationGuid, MovedStack) && OriginalStack.Count == 6 && MovedStack.Count == 2);
    const auto SplitBack = Coordinator->MoveFromContainer(Bag.GetItemGuid(), Split.DestinationGuid, 1);
    TestTrue(TEXT("Partial extraction records another independent GUID"), SplitBack.bSuccess
        && SplitBack.DestinationGuid != Split.DestinationGuid && SplitBack.MovedQuantity == 1 && SplitBack.SourceRemainingQuantity == 1);
    auto Quantity = [&](UACFInventoryComponent* Inventory)
    {
        int32 Sum = 0; for (const auto& Item : Inventory->GetInventory()) if (Item.ItemClass == Stack.ItemClass) Sum += Item.Count;
        return Sum;
    };
    TestEqual(TEXT("Repeated splits conserve all eight units"), Quantity(Native) + Quantity(Container), 8);
    FEFEquipmentResult Rejected;
    TestFalse(TEXT("Late persistence validation rejects a transfer"), EFEquipmentACF::Transfer(Native, Container,
        Stack.GetItemGuid(), 3, false, Error, &Rejected, [](FString& Failure) { Failure = TEXT("Injected persistence failure."); return false; }));
    TestTrue(TEXT("Late rejection restores the original source stack"), Native->GetItemByGuid(Stack.GetItemGuid(), OriginalStack) && OriginalStack.Count == 6);
    TestEqual(TEXT("Late rejection preserves aggregate quantities"), Quantity(Native) + Quantity(Container), 8);
    TestFalse(TEXT("Rejected transfer publishes no destination receipt"), Rejected.DestinationGuid.IsValid());

    // ACF removes an already equipped item before calling its common veto.
    // An external alternative-slot call must never bypass authorization.
    Coordinator->MoveFromContainer(Bag.GetItemGuid(), Bra.GetItemGuid(), 1);
    auto BraInfo = Bra.Item->GetItemInfo();
    const auto OriginalSlots = BraInfo.ItemSlots;
    BraInfo.ItemSlots.Add(FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Underwear.Legs")));
    Bra.Item->SetItemDescriptor(BraInfo);
    Equip(Bra);
    Native->GetItemByGuid(Bra.GetItemGuid(), Live);
    Native->EquipItemFromInventoryInSlot(Live, FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Underwear.Legs")));
    TestFalse(TEXT("External alternative-slot change cannot apply unauthorized equipment"), IsEquipped(Bra));
    BraInfo.ItemSlots = OriginalSlots; Bra.Item->SetItemDescriptor(BraInfo);
    TestTrue(TEXT("Coordinator can recover after the external native removal"), Equip(Bra).bSuccess);
    return !HasAnyErrors();
}
#endif
