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
#include "EFEquipmentDeathRouting.h"
#include "Tests/EFEquipmentDeathTestTypes.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFEquipmentIntegrationTest, "EF.Equipment.Native.Vertical", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FEFEquipmentIntegrationTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
    // Native drop/pickup requires game-world actor construction (None does not
    // preserve that lifecycle). This synchronous fixture has no local input.
    // Suppress only this world's input subsystem during creation, restoring the
    // setting before any assertion or native operation. Never save the CDO.
    UWorld* World = nullptr;
    {
        auto* Settings = GetMutableDefault<UEnhancedInputDeveloperSettings>();
        struct FInputScope
        {
            UEnhancedInputDeveloperSettings* Settings;
            bool Original;
            ~FInputScope() { Settings->bEnableWorldSubsystem = Original; }
        } InputScope{Settings, Settings->bEnableWorldSubsystem != 0};
        Settings->bEnableWorldSubsystem = false;
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    }
    if (!TestNotNull(TEXT("Native fixture world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    struct FCleanup { UWorld* World; ~FCleanup() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->MarkObjectsPendingKill(); } } Cleanup{World};
    TestNull(TEXT("Isolated native fixture has no Enhanced Input world subsystem"), World->GetSubsystem<UEnhancedInputWorldSubsystem>());
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
    const auto Potion = Add(TEXT("/Game/FullSample/Blueprints/Items/Consumable/ACFHealthPotionBP.ACFHealthPotionBP_C"), 3);
    const auto QuickSlot = FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Consumable1"));
    auto Available = Native->GetAvailableEquipmentSlot(); Available.AddUnique(QuickSlot); Native->SetAvailableEquipmentSlot(Available);
    TestTrue(TEXT("Native quick-slot consumable equips without an equippable subclass"), Coordinator->RequestEquip(Potion.GetItemGuid(), QuickSlot).bSuccess);
    FInventoryItem PotionAfter;
    TestTrue(TEXT("Assigning a quick slot preserves potion GUID and quantity"), Native->GetItemByGuid(Potion.GetItemGuid(), PotionAfter) && PotionAfter.Count == 3);
    TestTrue(TEXT("Native quick-slot consumable unequips through coordinator"), Coordinator->RequestUnequip(Potion.GetItemGuid()).bSuccess);
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
    TestEqual(TEXT("All root GUIDs conserved, including native quick-slot consumable"), Native->GetInventory().Num(), 8);
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
    TestFalse(TEXT("Full bags cannot be sold"), Coordinator->PreviewSale(Vendor, Bag.GetItemGuid()).bValid);
    TestEqual(TEXT("Rejected full bag sale preserves player money"), Native->GetCurrentCurrencyAmount(), 100.f);
    const auto BeforeEmptying = Coordinator->GetContainerItems(Bag.GetItemGuid());
    for (const auto& Child : BeforeEmptying) TestTrue(TEXT("Extract before selling"), Coordinator->MoveFromContainer(Bag.GetItemGuid(), Child.GetItemGuid(), Child.Count).bSuccess);
    const auto Cancelled = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Empty bag has native price"), Cancelled.bValid && Cancelled.Lines.Num() == 1
        && FMath::IsNearlyEqual(Cancelled.TotalPrice, 20.0 * Vendor->GetVendorPriceMultiplierOnBuy()));
    Coordinator->CancelSale();
    TestFalse(TEXT("Cancelled quote cannot sell"), Coordinator->ConfirmSale(Cancelled.QuoteId).bSuccess);
    TestEqual(TEXT("Cancellation preserves player money"), Native->GetCurrentCurrencyAmount(), 100.f);
    TestEqual(TEXT("Cancellation preserves vendor money"), Vendor->GetCurrentCurrencyAmount(), 1000.f);
    const auto StaleSale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    for (const auto& Child : BeforeEmptying) TestTrue(TEXT("Bag can be refilled after quoting"), Coordinator->MoveToContainer(Child.GetItemGuid(), Bag.GetItemGuid(), Child.Count).bSuccess);
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
    for (const auto& Child : SaleContents) TestTrue(TEXT("All content extracted before native sale"), Coordinator->MoveFromContainer(Bag.GetItemGuid(), Child.GetItemGuid(), Child.Count).bSuccess);
    const auto FaultSale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    Coordinator->TestFailurePoint = 3;
    TestFalse(TEXT("Settlement fault rejects the sale"), Coordinator->ConfirmSale(FaultSale.QuoteId).bSuccess);
    TestTrue(TEXT("Settlement failure restores original bag ownership"), Native->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Settlement failure restores player money"), Native->GetCurrentCurrencyAmount(), 100.f);
    TestEqual(TEXT("Settlement failure restores vendor money"), Vendor->GetCurrentCurrencyAmount(), 1000.f);
    const auto Sale = Coordinator->PreviewSale(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Empty bag can be sold after revalidation"), Sale.bValid && Coordinator->ConfirmSale(Sale.QuoteId).bSuccess);
    TestFalse(TEXT("Sold bag is absent from player"), Native->GetItemByGuid(Bag.GetItemGuid(), Live));
    TestTrue(TEXT("Vendor owns the original bag GUID and item instance"), Vendor->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Vendor receives no duplicate loose content loot"), Vendor->GetInventory().Num(), 1);
    TestEqual(TEXT("Sale credits player exactly once"), Native->GetCurrentCurrencyAmount(), 100.f + static_cast<float>(Sale.TotalPrice));
    TestEqual(TEXT("Sale debits vendor exactly once"), Vendor->GetCurrentCurrencyAmount(), 1000.f - static_cast<float>(Sale.TotalPrice));
    TestFalse(TEXT("Repeated confirmation cannot duplicate the sale"), Coordinator->ConfirmSale(Sale.QuoteId).bSuccess);
    const auto CancelPurchase = Coordinator->PreviewPurchase(Vendor, Bag.GetItemGuid());
    TestTrue(TEXT("Empty bag purchase quotes the bag"), CancelPurchase.bValid && CancelPurchase.bPurchase
        && CancelPurchase.Lines.Num() == 1);
    Coordinator->CancelSale();
    TestFalse(TEXT("Cancelled purchase cannot move inventory or money"), Coordinator->ConfirmPurchase(CancelPurchase.QuoteId).bSuccess);
    const auto StalePurchase = Coordinator->PreviewPurchase(Vendor, Bag.GetItemGuid());
    PriceBag.CurrencyValue = 21; RestoreBagPrice.Item->SetItemDescriptor(PriceBag);
    TestFalse(TEXT("Changed vendor price invalidates purchase confirmation"), Coordinator->ConfirmPurchase(StalePurchase.QuoteId).bSuccess);
    PriceBag.CurrencyValue = 20; RestoreBagPrice.Item->SetItemDescriptor(PriceBag);
    Coordinator->SetBaseCarryCapacity(Coordinator->GetWeightSummary().EffectiveWeight);
    TestFalse(TEXT("Buying an empty bag checks its weight"), Coordinator->PreviewPurchase(Vendor, Bag.GetItemGuid()).bValid);
    Coordinator->SetBaseCarryCapacity(180.125);
    const float PlayerBeforePurchase = Native->GetCurrentCurrencyAmount(), VendorBeforePurchase = Vendor->GetCurrentCurrencyAmount();
    const auto FaultPurchase = Coordinator->PreviewPurchase(Vendor, Bag.GetItemGuid());
    Coordinator->TestFailurePoint = 4;
    TestFalse(TEXT("Purchase settlement fault rejects the operation"), Coordinator->ConfirmPurchase(FaultPurchase.QuoteId).bSuccess);
    TestTrue(TEXT("Purchase fault restores original vendor ownership"), Vendor->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Purchase fault restores player money"), Native->GetCurrentCurrencyAmount(), PlayerBeforePurchase);
    TestEqual(TEXT("Purchase fault restores vendor money"), Vendor->GetCurrentCurrencyAmount(), VendorBeforePurchase);
    const auto Purchase = Coordinator->PreviewPurchase(Vendor, Bag.GetItemGuid());
    const auto Purchased = Coordinator->ConfirmPurchase(Purchase.QuoteId);
    TestTrue(TEXT("Reacquiring the bag preserves its payload: ") + Purchased.Message.ToString(), Purchased.bSuccess);
    TestTrue(TEXT("Purchase retains original satchel GUID and object"), Native->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    TestEqual(TEXT("Purchase charges the aggregated price once"), Native->GetCurrentCurrencyAmount(), PlayerBeforePurchase - static_cast<float>(Purchase.TotalPrice));
    TestEqual(TEXT("Purchase credits the vendor once"), Vendor->GetCurrentCurrencyAmount(), VendorBeforePurchase + static_cast<float>(Purchase.TotalPrice));
    TestFalse(TEXT("Repeated purchase confirmation cannot charge twice"), Coordinator->ConfirmPurchase(Purchase.QuoteId).bSuccess);
    for (const auto& Child : SaleContents) TestTrue(TEXT("Purchased bag accepts the same extracted contents"), Coordinator->MoveToContainer(Child.GetItemGuid(), Bag.GetItemGuid(), Child.Count).bSuccess);
    TestEqual(TEXT("Refilled satchel retains both content GUIDs"), Coordinator->GetContainerItems(Bag.GetItemGuid()).Num(), SaleContents.Num());

    auto* CompanionPawn = World->SpawnActor<APawn>();
    auto* CompanionInventory = NewObject<UACFEquipmentComponent>(CompanionPawn);
    CompanionPawn->AddInstanceComponent(CompanionInventory); CompanionInventory->RegisterComponent();
    EFEquipmentACF::Inventory(CompanionInventory)->Init(CompanionPawn); CompanionInventory->SetIsInitialized(true);
    TestFalse(TEXT("Filled bag cannot be transferred to an unextended companion"), Coordinator->GiveToInventory(CompanionInventory, Bag.GetItemGuid(), 1).bSuccess);
    auto* StorageOwner = World->SpawnActor<AActor>(); auto* Storage = NewObject<UACFStorageComponent>(StorageOwner);
    StorageOwner->AddInstanceComponent(Storage); Storage->RegisterComponent(); EFEquipmentACF::Inventory(Storage)->Init(StorageOwner); Storage->SetIsInitialized(true);
    TestTrue(TEXT("Full bag moves intact into non-pawn storage"), Coordinator->GiveToInventory(Storage, Bag.GetItemGuid(), 1).bSuccess);
    TestTrue(TEXT("Storage owns one original bag and no duplicate children"), Storage->GetInventory().Num() == 1 && Storage->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.Item == Bag.Item);
    Coordinator->SetBaseCarryCapacity(Coordinator->GetWeightSummary().EffectiveWeight);
    TestFalse(TEXT("Storage retrieval checks bag contents against player weight"), Coordinator->TakeFromInventory(Storage, Bag.GetItemGuid(), 1, false).bSuccess);
    TestEqual(TEXT("Rejected retrieval preserves stored bag"), Storage->GetInventory().Num(), 1);
    Coordinator->SetBaseCarryCapacity(180.125);
    TestTrue(TEXT("Storage retrieval succeeds with capacity"), Coordinator->TakeFromInventory(Storage, Bag.GetItemGuid(), 1, false).bSuccess);
    for (const auto& Child : SaleContents) TestTrue(TEXT("Storage round trip preserves child identity and quantity"), Coordinator->GetContainerItems(Bag.GetItemGuid()).ContainsByPredicate(
        [&](const auto& Current) { return Current.GetItemGuid() == Child.GetItemGuid() && Current.Count == Child.Count; }));

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
        const auto BeforeSummary = LegacyInventory->GetInventory();
        const FString MigrationSummary = EFEquipmentPersistence::DescribeMigration(LegacyInventory, Converted->EquipmentPlayer, true).ToString();
        TestTrue(TEXT("Migration summary identifies displaced clothing and its retained ownership"),
            MigrationSummary.Contains(Bra.Item->GetItemName().ToString()) && MigrationSummary.Contains(TEXT("remain in your inventory")));
        TestEqual(TEXT("Migration summary does not mutate inventory"), LegacyInventory->GetInventory().Num(), BeforeSummary.Num());
        TestTrue(TEXT("Ordinary versioned load does not claim a migration"), EFEquipmentPersistence::DescribeMigration(LegacyInventory, FEFEquipmentSavedPlayer(), false).IsEmpty());
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
    auto* Observer = NewObject<UEFEquipmentDeathTestObserver>();
    Observer->OnOwnerDeath.AddDynamic(Observer, &UEFEquipmentDeathTestObserver::First);
    Observer->OnOwnerDeath.AddDynamic(Observer, &UEFEquipmentDeathTestObserver::Native);
    Observer->OnOwnerDeath.AddDynamic(Observer, &UEFEquipmentDeathTestObserver::Last);
    TestTrue(TEXT("Death route replaces exactly the native callback"), EFEquipmentDeathRouting::Replace(Observer, TEXT("OnOwnerDeath"), Observer, TEXT("Native"), Observer, TEXT("Routed")));
    Observer->OnOwnerDeath.Broadcast();
    TestTrue(TEXT("Death route preserves callback position and all other observers"), Observer->Calls == TArray<int32>{1, 8, 2, 9, 3});
    Observer->Calls.Reset();
    TestTrue(TEXT("Death route is reversible at component teardown"), EFEquipmentDeathRouting::Replace(Observer, TEXT("OnOwnerDeath"), Observer, TEXT("Routed"), Observer, TEXT("Native")));
    Observer->OnOwnerDeath.Broadcast(); TestTrue(TEXT("Original event order is restored"), Observer->Calls == TArray<int32>{1, 2, 3});

    Coordinator->SetBaseCarryCapacity(180.125);
    auto* DropPolicy = FindFProperty<FBoolProperty>(Native->GetClass(), TEXT("bDropItemsOnDeath"));
    if (!TestNotNull(TEXT("Native death policy contract"), DropPolicy)) return false;
    Native->GetItemByGuid(Bag.GetItemGuid(), Live); Live.DropChancePercentage = 100; EFEquipmentACF::Inventory(Native)->ChangeEntry(Live);
    const auto DeathChildren = Coordinator->GetContainerItems(Bag.GetItemGuid());
    int32 NativeDeathCalls = 0;
    auto DeathCallback = [&]() { ++NativeDeathCalls; Native->OnEntityOwnerDeath(); };
    DropPolicy->SetPropertyValue_InContainer(Native, false);
    Coordinator->ProcessOwnerDeath(DeathCallback);
    TestEqual(TEXT("Death policy keep executes native lifecycle once"), NativeDeathCalls, 1);
    TestTrue(TEXT("Death keep retains bag GUID, payload and original chance"), Native->GetItemByGuid(Bag.GetItemGuid(), Live)
        && Live.DropChancePercentage == 100 && Coordinator->GetContainerItems(Bag.GetItemGuid()).Num() == DeathChildren.Num());
    TestFalse(TEXT("Death clears stale managed reservations"), Coordinator->GetSlotViews().ContainsByPredicate([](const auto& View) { return View.bReserved; }));
    DropPolicy->SetPropertyValue_InContainer(Native, true);
    auto* DeathContainer = Coordinator->ResolveContainer(Bag.GetItemGuid(), Error);
    if (!TestNotNull(TEXT("Death policy retains a resolvable bag: ") + Error, DeathContainer)) return false;
    if (!TestTrue(TEXT("Death policy retains contents"), !DeathContainer->GetInventory().IsEmpty())) return false;
    const auto ProtectedChild = DeathContainer->GetInventory()[0]; auto ProtectedInfo = ProtectedChild.Item->GetItemInfo();
    const auto OriginalInfo = ProtectedInfo; ProtectedInfo.bDroppable = false; ProtectedChild.Item->SetItemDescriptor(ProtectedInfo);
    Coordinator->ProcessOwnerDeath(DeathCallback);
    TestTrue(TEXT("Protected child prevents the whole death drop"), Native->GetItemByGuid(Bag.GetItemGuid(), Live) && Live.DropChancePercentage == 100);
    ProtectedChild.Item->SetItemDescriptor(OriginalInfo);
    Live.DropChancePercentage = 0; EFEquipmentACF::Inventory(Native)->ChangeEntry(Live);
    Coordinator->ProcessOwnerDeath(DeathCallback);
    TestTrue(TEXT("Zero native death chance retains a full bag"), Native->GetItemByGuid(Bag.GetItemGuid(), Live));
    Live.DropChancePercentage = 100; EFEquipmentACF::Inventory(Native)->ChangeEntry(Live);
    Coordinator->RequestEquip(Bag.GetItemGuid(), FGameplayTag());
    Coordinator->ProcessOwnerDeath(DeathCallback);
    TestEqual(TEXT("Each death invokes the native pipeline exactly once"), NativeDeathCalls, 4);
    TestFalse(TEXT("Death drop removes the bag from player inventory"), Native->GetItemByGuid(Bag.GetItemGuid(), Live));
    auto* DeathPickup = FindWorldItem(Bag.GetItemGuid());
    if (TestNotNull(TEXT("Death emits one original full satchel pickup"), DeathPickup))
    {
        TestEqual(TEXT("Death does not create separate loose content entries"), DeathPickup->GetItems().Num(), 1);
        DeathPickup->DispatchBeginPlay(); DeathPickup->OnInteractedByPawn_Implementation(Pawn, TEXT(""));
        const auto AfterDeath = Coordinator->GetContainerItems(Bag.GetItemGuid());
        for (const auto& Child : DeathChildren) TestTrue(TEXT("Every contained GUID and quantity survives death and pickup"), AfterDeath.ContainsByPredicate(
            [&](const auto& Current) { return Current.GetItemGuid() == Child.GetItemGuid() && Current.Count == Child.Count; }));
    }
    return !HasAnyErrors();
}
#endif
