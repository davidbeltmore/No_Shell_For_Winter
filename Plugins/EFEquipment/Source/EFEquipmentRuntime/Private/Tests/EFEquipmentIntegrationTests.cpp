#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "ACFVendorComponent.h"
#include "EFEquipmentWorldItem.h"
#include "Components/ACFStorageComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/Crc.h"
#include "EFEquipmentPersistence.h"
#include "EFEquipmentSaveFiles.h"
#include "Misc/FileHelper.h"

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
    auto* InvalidContainer = NewObject<UEFContainerInventoryComponent>(Pawn);
    InvalidContainer->InitializeContainer(Bag.GetItemGuid(), 20);
    FInventoryItem InvalidChild = Bra; InvalidChild.bIsEquipped = true;
    EFEquipmentACF::Inventory(InvalidContainer)->AddEntry(InvalidChild);
    TArray<uint8> InvalidPayload;
    {
        FMemoryWriter Writer(InvalidPayload); FObjectAndNameAsStringProxyArchive Ar(Writer, false);
        Ar.ArIsSaveGame = true; Ar.ArNoDelta = true; InvalidContainer->Serialize(Ar);
    }
    TArray<uint8> SemanticCorrupt;
    {
        FMemoryWriter Envelope(SemanticCorrupt); int32 Version = 1, Length = InvalidPayload.Num();
        uint32 CRC = FCrc::MemCrc32(InvalidPayload.GetData(), Length);
        Envelope << Version << Length << CRC; Envelope.Serialize(InvalidPayload.GetData(), Length);
    }
    const auto* PreservedItem = Rehydrated->GetInventory()[0].Item;
    TestFalse(TEXT("Valid-checksum payload with equipped contents is rejected"), Rehydrated->ReadPayload(SemanticCorrupt, Error));
    TestTrue(TEXT("Late deserialization rejection retains the original live object"), Rehydrated->GetInventory().Num() == 1
        && Rehydrated->GetInventory()[0].Item == PreservedItem && Rehydrated->GetInventory()[0].GetItemGuid() == Bra.GetItemGuid());
    InvalidContainer->DestroyComponent();
    auto* BagRules = UEFEquipmentRulesFragment::Find(Bag.Item);
    const auto PreservedPayload = BagRules->ContainerPayload; BagRules->ContainerSchema = 99;
    TestNull(TEXT("Future container schema cannot resolve through an existing cache"), Coordinator->ResolveContainer(Bag.GetItemGuid(), Error));
    TestFalse(TEXT("Future container schema cannot be overwritten"), BagRules->FlushContainer(Error));
    TestTrue(TEXT("Unsupported container version retains its bytes"), BagRules->ContainerPayload == PreservedPayload);
    BagRules->ContainerSchema = 1;

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

    // Price authoring is scoped to this transient test and restored without saving.
    struct FRestoreDescriptor
    {
        UACFItem* Item; FItemDescriptor Info;
        explicit FRestoreDescriptor(UACFItem* In) : Item(In), Info(In->GetItemInfo()) {}
        ~FRestoreDescriptor() { Item->SetItemDescriptor(Info); }
    } RestoreBagPrice(Bag.ItemClass->GetDefaultObject<UACFItem>()), RestoreStackPrice(Stack.ItemClass->GetDefaultObject<UACFItem>());
    auto PriceBag = RestoreBagPrice.Info; PriceBag.CurrencyValue = 20; PriceBag.bSellable = true;
    RestoreBagPrice.Item->SetItemDescriptor(PriceBag);
    auto PriceStack = RestoreStackPrice.Info; PriceStack.CurrencyValue = 10; PriceStack.bSellable = true;
    RestoreStackPrice.Item->SetItemDescriptor(PriceStack);
    AActor* VendorOwner = World->SpawnActor<AActor>();
    auto* Vendor = NewObject<UACFVendorComponent>(VendorOwner);
    VendorOwner->AddInstanceComponent(Vendor); Vendor->RegisterComponent();
    VendorOwner->DispatchBeginPlay(); Vendor->SetCurrency(1000); Native->SetCurrency(100);
    const auto Cancelled = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Full satchel sale has native aggregate price and breakdown"), Cancelled.bValid && Cancelled.Lines.Num() == 2
        && FMath::IsNearlyEqual(Cancelled.TotalPrice, 30.0 * Vendor->GetVendorPriceMultiplierOnBuy()));
    Coordinator->CancelSale();
    TestFalse(TEXT("Cancelled quote cannot sell"), Coordinator->ConfirmSale(Cancelled.QuoteId).bSuccess);
    TestEqual(TEXT("Cancellation preserves player money"), Native->GetCurrentCurrencyAmount(), 100.f);
    TestEqual(TEXT("Cancellation preserves vendor money"), Vendor->GetCurrentCurrencyAmount(), 1000.f);
    const auto StaleSale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Contents may change while confirmation is open"), Coordinator->MoveToContainer(Stack.GetItemGuid(), Bag.GetItemGuid(), 1).bSuccess);
    TestFalse(TEXT("Changed contents invalidate the quoted sale"), Coordinator->ConfirmSale(StaleSale.QuoteId).bSuccess);
    auto SaleContents = Coordinator->GetContainerItems(Bag.GetItemGuid());
    TestEqual(TEXT("Satchel now owns two separate stack identities"), SaleContents.Num(), 2);
    auto ChildInfo = SaleContents[0].Item->GetItemInfo();
    ChildInfo.bSellable = false; SaleContents[0].Item->SetItemDescriptor(ChildInfo);
    TestFalse(TEXT("An unsellable child rejects the whole set"), Coordinator->PreviewSale(Vendor, Bag.GetItemGuid()).bValid);
    ChildInfo.bSellable = true; ChildInfo.bDroppable = false; SaleContents[0].Item->SetItemDescriptor(ChildInfo);
    TestFalse(TEXT("A non-droppable child rejects the whole drop"), Coordinator->RequestDrop(Bag.GetItemGuid()).bSuccess);
    ChildInfo.bDroppable = true; SaleContents[0].Item->SetItemDescriptor(ChildInfo);
    const auto FaultSale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    Coordinator->TestFailurePoint = 3;
    TestFalse(TEXT("Settlement fault rejects the sale"), Coordinator->ConfirmSale(FaultSale.QuoteId).bSuccess);
    TestTrue(TEXT("Settlement failure restores original bag ownership"), Native->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Settlement failure restores player money"), Native->GetCurrentCurrencyAmount(), 100.f);
    TestEqual(TEXT("Settlement failure restores vendor money"), Vendor->GetCurrentCurrencyAmount(), 1000.f);
    const auto Sale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Full bag can be sold after revalidation"), Sale.bValid && Coordinator->ConfirmSale(Sale.QuoteId).bSuccess);
    TestFalse(TEXT("Sold bag is absent from player"), Native->GetItemByGuid(Bag.GetItemGuid(), Live));
    TestTrue(TEXT("Vendor owns the original bag GUID and item instance"), Vendor->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Vendor receives no duplicate loose content loot"), Vendor->GetInventory().Num(), 1);
    TestEqual(TEXT("Sale credits player exactly once"), Native->GetCurrentCurrencyAmount(), 100.f + static_cast<float>(Sale.TotalPrice));
    TestEqual(TEXT("Sale debits vendor exactly once"), Vendor->GetCurrentCurrencyAmount(), 1000.f - static_cast<float>(Sale.TotalPrice));
    TestFalse(TEXT("Repeated confirmation cannot duplicate the sale"), Coordinator->ConfirmSale(Sale.QuoteId).bSuccess);
    TestTrue(TEXT("Reacquiring the bag preserves its payload"), Coordinator->TakeFromInventory(Vendor, Bag.GetItemGuid(), 1, false).bSuccess);
    TestEqual(TEXT("Rehosted satchel restores both content GUIDs"), Coordinator->GetContainerItems(Bag.GetItemGuid()).Num(), SaleContents.Num());

    auto* Controller = World->SpawnActor<APlayerController>();
    // This minimal world has no level/game-mode initialization. Register its
    // controller explicitly, as normal PostInitializeComponents would do.
    World->AddController(Controller); Controller->Possess(Pawn);
    TestEqual(TEXT("Native drop can resolve the fixture player controller"), UGameplayStatics::GetPlayerController(Pawn, 0), Controller);
    auto FindWorldItem = [&](FGuid Guid) -> AEFEquipmentWorldItem*
    {
        for (TActorIterator<AEFEquipmentWorldItem> It(World); It; ++It)
            if (!It->IsActorBeingDestroyed() && It->GetItems().ContainsByPredicate([&](const auto& Item) { return Item.GetItemGuid() == Guid; })) return *It;
        return nullptr;
    };
    const auto BagDrop = Coordinator->RequestDrop(Bag.GetItemGuid());
    TestTrue(TEXT("Full satchel drops through a verified native storage transfer: ") + BagDrop.Message.ToString(), BagDrop.bSuccess && BagDrop.DestinationGuid == Bag.GetItemGuid());
    auto* BagPickup = FindWorldItem(Bag.GetItemGuid());
    if (TestNotNull(TEXT("Dropped satchel has one project-owned native pickup"), BagPickup))
    {
        BagPickup->DispatchBeginPlay();
        TestEqual(TEXT("Only the bag is a world entry; contents remain attached"), BagPickup->GetItems().Num(), 1);
        TestFalse(TEXT("Dropped bag is absent from player inventory"), Native->GetItemByGuid(Bag.GetItemGuid(), Live));
        Coordinator->BaseCarryCapacity = Coordinator->GetWeightSummary().EffectiveWeight;
        Coordinator->RefreshState();
        BagPickup->OnInteractedByPawn_Implementation(Pawn, TEXT(""));
        TestTrue(TEXT("Rejected full-bag pickup remains available in the world"), !BagPickup->WasPickupCompleted() && !BagPickup->IsActorBeingDestroyed()
            && BagPickup->GetItems().Num() == 1 && BagPickup->GetItems()[0].GetItemGuid() == Bag.GetItemGuid());
        Coordinator->BaseCarryCapacity = 180.125; Coordinator->RefreshState();
        BagPickup->OnInteractedByPawn_Implementation(Pawn, TEXT(""));
        TestTrue(TEXT("Accepted pickup completes once and removes the empty world actor"), BagPickup->WasPickupCompleted() && BagPickup->IsActorBeingDestroyed());
        TestTrue(TEXT("Pickup retains the original bag GUID and object"), Native->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
        const auto RestoredContents = Coordinator->GetContainerItems(Bag.GetItemGuid());
        for (const auto& Child : SaleContents)
            TestTrue(TEXT("Every contained GUID and quantity survives the world round trip"), RestoredContents.ContainsByPredicate([&](const auto& Restored)
                { return Restored.GetItemGuid() == Child.GetItemGuid() && Restored.Count == Child.Count; }));
    }
    Native->GetItemByGuid(Stack.GetItemGuid(), Live);
    const int32 BeforeDrop = Live.Count;
    const auto StackDrop = Coordinator->RequestDrop(Stack.GetItemGuid(), 2);
    TestTrue(TEXT("Partial drop reports a new destination identity: ") + StackDrop.Message.ToString(), StackDrop.bSuccess && StackDrop.SourceGuid == Stack.GetItemGuid()
        && StackDrop.DestinationGuid != Stack.GetItemGuid() && StackDrop.MovedQuantity == 2 && StackDrop.SourceRemainingQuantity == BeforeDrop - 2);
    TestTrue(TEXT("Partial drop preserves the remaining source stack"), Native->GetItemByGuid(Stack.GetItemGuid(), Live) && Live.Count == BeforeDrop - 2);
    if (auto* StackPickup = FindWorldItem(StackDrop.DestinationGuid))
    {
        StackPickup->DispatchBeginPlay(); StackPickup->OnInteractedByPawn_Implementation(Pawn, TEXT(""));
        TestTrue(TEXT("Dropped stack returns with its recorded GUID"), Native->GetItemByGuid(StackDrop.DestinationGuid, Live) && Live.Count == 2);
        TestEqual(TEXT("World round trips conserve all original stack units"), Quantity(Native) + Quantity(Coordinator->ResolveContainer(Bag.GetItemGuid(), Error)), 8);
    }
    else AddError(TEXT("Partial stack drop did not create a world pickup."));

    Equip(Bikini); Equip(Shirt); Equip(Bag);
    Coordinator->SetBaseCarryCapacity(4.125);
    TestTrue(TEXT("Persistence fixture is legitimately overburdened"), Coordinator->GetWeightSummary().bOverburdened);
    FALSPlayerData PlayerData; PlayerData.Pawn.Class = Pawn->GetClass(); PlayerData.Pawn.alsName = Pawn->GetFName();
    FALSComponentData InventoryRecord; InventoryRecord.Class = Native->GetClass(); InventoryRecord.alsName = Native->GetFName();
    TestTrue(TEXT("Serialize native root inventory for ALS"), EFEquipmentPersistence::SerializeInventory(Native, InventoryRecord.Data, Error));
    PlayerData.Pawn.AddComponentData(InventoryRecord);
    auto* Saved = NewObject<UEFEquipmentSaveGame>();
    TestTrue(TEXT("Capture separate fractional base capacity and safe restoration intent: ") + Error,
        EFEquipmentPersistence::CapturePlayer(Saved, Coordinator, PlayerData, Error));
    TestEqual(TEXT("Saved base capacity is independent of the native projection"), Saved->EquipmentPlayer.BaseCarryCapacity, 4.125);
    TestTrue(TEXT("Saved snapshot includes the equipped bag and Bikini"), Saved->EquipmentPlayer.EquippedSlots.Contains(Bag.GetItemGuid())
        && Saved->EquipmentPlayer.EquippedSlots.Contains(Bikini.GetItemGuid()));
    TestTrue(TEXT("Validate a legitimately overloaded versioned slot: ") + Error, EFEquipmentPersistence::ValidateSlot(World, Saved, Error));
    auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    auto* Repeated = EFEquipmentPersistence::ConvertSlot(World, Saved, Registry, Error);
    TestTrue(TEXT("Versioned conversion is idempotent"), Repeated && Repeated->EquipmentPlayer.BaseCarryCapacity == Saved->EquipmentPlayer.BaseCarryCapacity
        && Repeated->EquipmentPlayer.EquippedSlots.OrderIndependentCompareEqual(Saved->EquipmentPlayer.EquippedSlots));
    const auto ChildrenBeforeLoad = Coordinator->GetContainerItems(Bag.GetItemGuid());
    Saved->GetLocalPlayer(PlayerData);
    const auto* SavedRecord = PlayerData.Pawn.GetComponentData(Native);
    if (TestNotNull(TEXT("ALS record retains native component identity"), SavedRecord))
    {
        TestTrue(TEXT("Restore the native equipment bytes"), EFEquipmentPersistence::DeserializeInventory(Native, SavedRecord->Data, Error));
        TestFalse(TEXT("Native restored entries cannot apply effects before conflict resolution"), Native->GetInventory().ContainsByPredicate([](const auto& Entry) { return Entry.bIsEquipped; }));
        Native->OnComponentLoaded_Implementation();
        TestTrue(TEXT("Restore equipment only after native component loading: ") + Error, EFEquipmentPersistence::RestorePlayer(Coordinator, Saved->EquipmentPlayer, Error));
        TestTrue(TEXT("Restore reconstructs equipped bag and reservations"), IsEquipped(Bag) && IsEquipped(Bikini)
            && Coordinator->GetSlotViews().ContainsByPredicate([](const auto& View) { return View.bReserved; }));
        TestEqual(TEXT("Overburdened load retains exact fractional base capacity"), Coordinator->GetWeightSummary().BaseCapacity, 4.125);
        const auto ChildrenAfterLoad = Coordinator->GetContainerItems(Bag.GetItemGuid());
        for (const auto& Child : ChildrenBeforeLoad)
            TestTrue(TEXT("Nested ACF payload preserves each child across root save/load"), ChildrenAfterLoad.ContainsByPredicate([&](const auto& Restored)
                { return Restored.GetItemGuid() == Child.GetItemGuid() && Restored.Count == Child.Count && Restored.ItemClass == Child.ItemClass; }));
    }

    auto* LegacyInventory = NewObject<UACFEquipmentComponent>(Pawn);
    EFEquipmentACF::Inventory(LegacyInventory)->Init(Pawn); LegacyInventory->SetIsInitialized(true);
    for (const auto& Item : {Bra, Bikini, Shirt})
    {
        FInventoryItem Entry = Item; Entry.bIsEquipped = true; Entry.EquipmentSlot = Item.Item->GetPossibleItemSlots()[0];
        for (const auto& Rule : Registry->MigrationRules) if (Rule.ItemClass.ToSoftObjectPath() == FSoftObjectPath(Item.ItemClass.Get()))
        { Entry.EquipmentSlot = Rule.OldSlot; break; }
        EFEquipmentACF::Inventory(LegacyInventory)->AddEntry(Entry);
    }
    FALSPlayerData LegacyPlayer; LegacyPlayer.Pawn.Class = Pawn->GetClass(); LegacyPlayer.Pawn.alsName = Pawn->GetFName();
    FALSComponentData LegacyRecord; LegacyRecord.Class = Native->GetClass(); LegacyRecord.alsName = Native->GetFName();
    EFEquipmentPersistence::SerializeInventory(LegacyInventory, LegacyRecord.Data, Error);
    const auto OriginalLegacyBytes = LegacyRecord.Data;
    LegacyPlayer.Pawn.AddComponentData(LegacyRecord);
    auto* Legacy = NewObject<UALSSaveGame>(); Legacy->StoreLocalPlayer(LegacyPlayer);
    auto* Converted = EFEquipmentPersistence::ConvertSlot(World, Legacy, Registry, Error);
    if (TestNotNull(TEXT("Legacy native slot converts without applying effects: ") + Error, Converted))
    {
        TestTrue(TEXT("Migration prioritizes Bikini reserves and keeps compatible outerwear"), Converted->EquipmentPlayer.EquippedSlots.Contains(Bikini.GetItemGuid())
            && Converted->EquipmentPlayer.EquippedSlots.Contains(Shirt.GetItemGuid()) && Converted->EquipmentPlayer.DisplacedItems.Contains(Bra.GetItemGuid()));
        Legacy->GetLocalPlayer(LegacyPlayer);
        TestTrue(TEXT("Candidate conversion leaves all original native bytes untouched"), LegacyPlayer.Pawn.GetComponentData(Native)->Data == OriginalLegacyBytes);
        const FString SlotName = TEXT("EquipmentQA_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        struct FTestSlotCleanup { FString Slot; ~FTestSlotCleanup() { UGameplayStatics::DeleteGameInSlot(Slot, 0); } } CleanupSlot{SlotName};
        TestTrue(TEXT("Create isolated native legacy save-file fixture"), UGameplayStatics::SaveGameToSlot(Legacy, SlotName, 0));
        FEFEquipmentSlotBackup Backup;
        TestTrue(TEXT("Back up original slot before conversion publication: ") + Error, EFEquipmentSaveFiles::Backup(SlotName, Backup, Error));
        TArray<uint8> BackedUp;
        TestTrue(TEXT("Backup preserves the original slot byte for byte"), FFileHelper::LoadFileToArray(BackedUp, *Backup.BackupPath) && BackedUp == Backup.OriginalBytes);
        auto Validate = [&](USaveGame* Object, FString& Failure) { return EFEquipmentPersistence::ValidateSlot(World, Cast<UEFEquipmentSaveGame>(Object), Failure); };
        EFEquipmentSaveFiles::FailBeforePublication = true;
        TestFalse(TEXT("Failure after staging cannot publish the converted slot"), EFEquipmentSaveFiles::Publish(Converted, Backup, Validate, Error));
        TArray<uint8> Retained;
        TestTrue(TEXT("Publication failure retains the original native file exactly"), UGameplayStatics::LoadDataFromSlot(Retained, SlotName, 0) && Retained == Backup.OriginalBytes);
        TestTrue(TEXT("Validated converted slot publishes successfully: ") + Error, EFEquipmentSaveFiles::Publish(Converted, Backup, Validate, Error));
        auto* DiskSave = Cast<UEFEquipmentSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
        TestTrue(TEXT("Published file loads as the versioned ALS subclass"), DiskSave && EFEquipmentPersistence::ValidateSlot(World, DiskSave, Error));
        TestFalse(TEXT("A stale backup cannot overwrite a newly published slot"), EFEquipmentSaveFiles::Publish(Converted, Backup, Validate, Error));
        TestTrue(TEXT("Rollback restores the retained original native save"), EFEquipmentSaveFiles::Recover(Backup, Error)
            && UGameplayStatics::LoadDataFromSlot(Retained, SlotName, 0) && Retained == Backup.OriginalBytes);
        Converted->EquipmentVersion = 99;
        TestNull(TEXT("A future save version is preserved and rejected"), EFEquipmentPersistence::ConvertSlot(World, Converted, Registry, Error));
    }
    LegacyInventory->DestroyComponent();
    return !HasAnyErrors();
}
#endif
