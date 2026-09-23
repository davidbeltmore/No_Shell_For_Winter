#include "Storage/ProjectChestContent.h"
#include "Storage/ProjectProceduralChestComponent.h"
#include "Storage/ProjectChestInventoryComponent.h"
#include "Engine/DataTable.h"
#include "Items/ACFConsumable.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "EFWorldStoragePersistence.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputDeveloperSettings.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectChestProbabilityTest, "EF.Chests.Selection.100000Trials", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FProjectChestProbabilityTest::RunTest(const FString&)
{
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FItemGenerationSlot::StaticStruct();
    const auto Common = FGameplayTag::RequestGameplayTag(TEXT("Rarity.Common"));
    const auto Rare = FGameplayTag::RequestGameplayTag(TEXT("Rarity.Rare"));
    const auto Category = FGameplayTag::RequestGameplayTag(TEXT("Item.Material"));
    FItemGenerationSlot Row; Row.Category = Category; Row.Rarity = Common; Row.ItemClass = UACFItem::StaticClass(); Table->AddRow(TEXT("A"), Row);
    Row.Rarity = Rare; Row.ItemClass = UACFConsumable::StaticClass(); Table->AddRow(TEXT("B"), Row);
    FProjectChestContentSettings S; S.Catalog = Table; S.Categories.AddTag(Category);
    FProjectChestRarityChance A; A.Rarity = Common; A.Percentage = 75;
    FProjectChestRarityChance B; B.Rarity = Rare; B.Percentage = 25; S.ItemRarities = {A, B};
    const FGuid Id(1,2,3,4); FString Error; TArray<FBaseItem> Items, Replay;
    int32 Rares = 0;
    for (int32 Seed = 0; Seed < 100000; ++Seed)
    {
        if (!FProjectChestSelector::Generate(S, Id, Seed, Items, Error)) { AddError(Error); return false; }
        if (Items.Num() != 1 || Items[0].Count != 1) { AddError(TEXT("Fixed count or units violated.")); return false; }
        Rares += Items[0].ItemClass == UACFConsumable::StaticClass();
        if (Seed < 100)
        {
            if (!FProjectChestSelector::Generate(S, Id, Seed, Replay, Error) || Replay[0].ItemClass != Items[0].ItemClass)
            { AddError(TEXT("Deterministic replay differs.")); return false; }
        }
    }
    TestTrue(TEXT("25% rarity within predeclared 0.6 percentage point tolerance"), FMath::Abs(Rares - 25000) <= 600);
    AddInfo(FString::Printf(TEXT("Rare selections: %d / 100000; expected 25000 +/- 600."), Rares));
    S.bUseSelectionRange = true; S.MinSelections = 1; S.MaxSelections = 3;
    int32 Counts[4] = {};
    for (int32 Seed = 0; Seed < 100000; ++Seed)
    {
        if (!FProjectChestSelector::Generate(S, Id, Seed, Items, Error)) { AddError(Error); return false; }
        if (Items.Num() < 1 || Items.Num() > 3) { AddError(TEXT("Inclusive range violated.")); return false; }
        ++Counts[Items.Num()];
    }
    for (int32 I = 1; I <= 3; ++I) TestTrue(TEXT("Uniform selection count within 0.7 percentage points"), FMath::Abs(Counts[I] - 100000.0/3) <= 700);
    S.ItemRarities[0].Percentage = 70;
    TestFalse(TEXT("Invalid percentage total rejected"), FProjectChestSelector::Generate(S, Id, 1, Items, Error));
    TestTrue(TEXT("Failure publishes no partial results"), Items.IsEmpty());
    S.ItemRarities[0].Percentage = 75; S.bAllowDuplicates = false;
    TestFalse(TEXT("Impossible unique maximum rejected before sampling"), FProjectChestSelector::Generate(S, Id, 1, Items, Error));
    S.bUseSelectionRange = false; S.FixedSelections = 1;
    TestTrue(TEXT("Feasible unique selection"), FProjectChestSelector::Generate(S, Id, 1, Items, Error));
    S.ExcludedRows.Add(TEXT("B"));
    TestFalse(TEXT("Positive empty tier is an explicit error"), FProjectChestSelector::Generate(S, Id, 1, Items, Error));
    S.ExcludedRows.Reset(); S.MaxUnitsPerStack = 255;
    TestFalse(TEXT("Impossible native stack amount rejected"), FProjectChestSelector::Generate(S, Id, 1, Items, Error));
    S.MaxUnitsPerStack = 1;
    TestFalse(TEXT("Missing stable identity rejected"), FProjectChestSelector::Generate(S, FGuid(), 1, Items, Error));
    S.ItemRarities.Swap(0, 1);
    FProjectChestSelector::Generate(S, Id, 42, Items, Error);
    S.ItemRarities.Swap(0, 1);
    FProjectChestSelector::Generate(S, Id, 42, Replay, Error);
    TestTrue(TEXT("Rarity array reordering preserves selection"), Items.Num()==1 && Replay.Num()==1 && Items[0].ItemClass==Replay[0].ItemClass);
    Row.Rarity = Common; Row.ItemClass = UACFItem::StaticClass(); Table->AddRow(TEXT("Duplicate"), Row);
    FProjectChestSelector::Generate(S, Id, 42, Items, Error);
    TestTrue(TEXT("Duplicate rows do not change probability or selection"), Items.Num()==1 && Replay.Num()==1 && Items[0].ItemClass==Replay[0].ItemClass);
    auto* Component = NewObject<UProjectProceduralChestComponent>();
    TestFalse(TEXT("Procedural Mode defaults off"), Component->bProceduralMode);
    TestFalse(TEXT("Legacy Procedural Mode defaults off"), GetDefault<UProjectChestInventoryComponent>()->bProceduralMode);
    for (const TCHAR* Kind : {TEXT("Wealth"), TEXT("Armament"), TEXT("Ammunition")})
    {
        const FString Path = FString::Printf(TEXT("/Game/_Game/Data/Equipment/Chests/DA_Chest_%s"), Kind);
        const auto* Authored = LoadObject<UProjectChestLootProfile>(nullptr, *Path);
        if (!TestNotNull(*Path, Authored)) return false;
        if (!FProjectChestSelector::Generate(Authored->Content, Id, 12345, Items, Error)) { AddError(Error); return false; }
        TestTrue(TEXT("Authored content is real and nonempty"), !Items.IsEmpty());
        if (Authored->Content.ContentType == TEXT("Ammunition"))
        {
            auto Ammo = Authored->Content; Ammo.bUseSelectionRange = false; Ammo.FixedSelections = 1;
            int64 TotalUnits = 0; int32 Lowest = MAX_int32, Highest = 0;
            for (int32 Seed = 0; Seed < 100000; ++Seed)
            {
                if (!FProjectChestSelector::Generate(Ammo, Id, Seed, Items, Error)) { AddError(Error); return false; }
                const int32 Units = Items[0].Count; TotalUnits += Units; Lowest = FMath::Min(Lowest, Units); Highest = FMath::Max(Highest, Units);
            }
            TestEqual(TEXT("Inclusive minimum stack units observed"), Lowest, 5);
            TestEqual(TEXT("Inclusive maximum stack units observed"), Highest, 15);
            TestTrue(TEXT("Uniform stack mean within predeclared 0.05 units"), FMath::Abs(TotalUnits / 100000.0 - 10) <= .05);
        }
    }
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectChestLifecycleTest, "EF.Chests.Lifecycle.NativeStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FProjectChestLifecycleTest::RunTest(const FString&)
{
    auto* Input = GetMutableDefault<UEnhancedInputDeveloperSettings>(); const bool Previous = Input->bEnableWorldSubsystem;
    Input->bEnableWorldSubsystem = false;
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    Input->bEnableWorldSubsystem = Previous;
    if (!TestNotNull(TEXT("Generator-free test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    struct FCleanup { UWorld* W; ~FCleanup() { W->DestroyWorld(false); GEngine->DestroyWorldContext(W); W->MarkObjectsPendingKill(); } } Cleanup{World};
    auto* GI = NewObject<UGameInstance>(); auto* Persistence = NewObject<UEFWorldStoragePersistence>(GI);
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FItemGenerationSlot::StaticStruct();
    FItemGenerationSlot Row; Row.Category = FGameplayTag::RequestGameplayTag(TEXT("Item.Material"));
    Row.Rarity = FGameplayTag::RequestGameplayTag(TEXT("Rarity.Common")); Row.ItemClass = UACFItem::StaticClass(); Table->AddRow(TEXT("Item"), Row);
    auto* Profile = NewObject<UProjectChestLootProfile>(); Profile->Content.Catalog = Table; Profile->Content.Categories.AddTag(Row.Category);
    FProjectChestRarityChance Chance; Chance.Rarity = Row.Rarity; Profile->Content.ItemRarities.Add(Chance);
    auto Spawn = [&]()
    {
        auto* Actor = World->SpawnActor<AActor>();
        auto* Config = NewObject<UProjectProceduralChestComponent>(Actor); Actor->AddInstanceComponent(Config); Config->RegisterComponent();
        Config->StableIdentity = FGuid(5,6,7,8); Config->bProceduralMode = true; Config->Profile = Profile;
        auto* Storage = NewObject<UProjectChestInventoryComponent>(Actor); Actor->AddInstanceComponent(Storage); Storage->RegisterComponent();
        return Storage;
    };
    FString Error; auto* Storage = Spawn();
    TestTrue(TEXT("Stable registration"), Persistence->RegisterStorage(TEXT("Map:Chest"), Storage, Error));
    TestTrue(TEXT("Generate without player, Calysto or Dungeon Architect"), Storage->EnsureContents(Error));
    if (!TestTrue(TEXT("Exactly one item"), Storage->GetInventory().Num() == 1)) { AddError(Error); return false; }
    const auto Identity = Storage->GetInventory()[0].GetItemGuid();
    TestTrue(TEXT("Second opening succeeds"), Storage->EnsureContents(Error));
    TestTrue(TEXT("Second opening preserves GUID and count"), Storage->GetInventory().Num()==1 && Storage->GetInventory()[0].GetItemGuid()==Identity);
    auto* Duplicate = Spawn();
    TestFalse(TEXT("Duplicate chest identity rejected"), Persistence->RegisterStorage(TEXT("Map:Chest"), Duplicate, Error));
    TestTrue(TEXT("Unload snapshot"), Persistence->ReleaseStorage(TEXT("Map:Chest"), Storage, Error));
    Storage->GetOwner()->Destroy();
    auto* Restored = Spawn();
    TestTrue(TEXT("Reload snapshot"), Persistence->RegisterStorage(TEXT("Map:Chest"), Restored, Error));
    TestTrue(TEXT("Generated marker restored"), Restored->HasGeneratedContents());
    TestTrue(TEXT("Native item identity restored"), Restored->GetInventory().Num()==1 && Restored->GetInventory()[0].GetItemGuid()==Identity);
    Restored->RemoveItem(Restored->GetInventory()[0], 1);
    TestTrue(TEXT("Inventory emptied"), Restored->GetInventory().IsEmpty());
    TestTrue(TEXT("Empty generated chest opens"), Restored->EnsureContents(Error));
    TestTrue(TEXT("Empty chest never refills"), Restored->GetInventory().IsEmpty());
    TMap<FString, FEFWorldStorageRecord> Saved;
    TestTrue(TEXT("Capture disk-slot payload"), Persistence->Capture(Saved, Error));
    TestTrue(TEXT("Unload empty chest"), Persistence->ReleaseStorage(TEXT("Map:Chest"), Restored, Error));
    Restored->GetOwner()->Destroy();
    auto* NextSession = NewObject<UEFWorldStoragePersistence>(GI);
    TestTrue(TEXT("Restore slot payload in fresh persistence session"), NextSession->Restore(Saved, Error));
    auto* Empty = Spawn();
    TestTrue(TEXT("Load empty chest into recreated actor"), NextSession->RegisterStorage(TEXT("Map:Chest"), Empty, Error));
    TestTrue(TEXT("Recreated empty chest opens without refill"), Empty->EnsureContents(Error) && Empty->GetInventory().IsEmpty() && Empty->HasGeneratedContents());
    auto* Locked = Spawn(); auto* Lock = NewObject<UProjectLockpickableComponent>(Locked->GetOwner()); Locked->GetOwner()->AddInstanceComponent(Lock); Lock->RegisterComponent(); Lock->SetLocked(true);
    TestFalse(TEXT("Locked opening cannot generate"), Locked->EnsureContents(Error));
    TestTrue(TEXT("Failed locked opening leaves empty ungenerated storage"), !Locked->HasGeneratedContents() && Locked->GetInventory().IsEmpty());
    Lock->SetLocked(false); TestTrue(TEXT("Unlocked opening generates once"), Locked->EnsureContents(Error));
    auto* Manual = Spawn(); Manual->GetOwner()->FindComponentByClass<UProjectProceduralChestComponent>()->bProceduralMode = false;
    Manual->AddItem(FBaseItem(UACFItem::StaticClass(), 1));
    const auto ManualId = Manual->GetInventory()[0].GetItemGuid();
    TestTrue(TEXT("Manual mode opens"), Manual->EnsureContents(Error));
    TestTrue(TEXT("Manual contents untouched"), Manual->GetInventory().Num()==1 && Manual->GetInventory()[0].GetItemGuid()==ManualId && !Manual->HasGeneratedContents());
    Manual->GetOwner()->FindComponentByClass<UProjectProceduralChestComponent>()->bProceduralMode = true;
    TestFalse(TEXT("Toggling mode never overwrites existing items"), Manual->EnsureContents(Error));
    TestTrue(TEXT("Rejected toggle retains manual GUID"), Manual->GetInventory()[0].GetItemGuid()==ManualId);
    // The transition adapter owns a frozen manifest. Both procedural switches
    // and authored manual stock must remain subordinate to that one owner.
    auto* Resolved = Spawn(); Resolved->bProceduralMode = true;
    auto* AuthoredItems = FindFProperty<FArrayProperty>(Resolved->GetClass(), TEXT("Items"));
    if (!TestNotNull(TEXT("Native authored stock property"), AuthoredItems)) return false;
    auto& ManualSeed = *AuthoredItems->ContainerPtrToValuePtr<TArray<FBaseItem>>(Resolved);
    ManualSeed.Add(FBaseItem(UACFConsumable::StaticClass(), 1));
    FProjectChestResolvedEntry Entry; Entry.StableAttemptId = TEXT("Attempt.Item");
    Entry.ContentCatalogId = TEXT("Catalog.Item"); Entry.ItemClass = UACFItem::StaticClass();
    TArray<FProjectChestResolvedEntry> Manifest{Entry};
    if (!TestTrue(TEXT("External manifest configured before BeginPlay"), Resolved->ConfigureResolvedLoot(Manifest, Error))) return false;
    const auto ResolvedId = Resolved->GetInventory()[0].GetItemGuid();
    Resolved->GetOwner()->DispatchBeginPlay();
    TArray<FName> Verified;
    TestTrue(TEXT("External manifest survives native BeginPlay without manual seeding"), Resolved->FinalizeAndVerifyResolvedLoot(Verified, Error));
    TestTrue(TEXT("External manifest publishes exact catalog identity once"), Verified.Num()==1 && Verified[0]==Entry.ContentCatalogId);
    TestFalse(TEXT("External manifest cannot finalize twice"), Resolved->FinalizeAndVerifyResolvedLoot(Verified, Error));
    TestFalse(TEXT("External manifest cannot configure twice"), Resolved->ConfigureResolvedLoot(Manifest, Error));
    TestTrue(TEXT("External ownership suppresses both procedural generators"), Resolved->EnsureContents(Error)
        && Resolved->GetInventory().Num()==1 && Resolved->GetInventory()[0].GetItemGuid()==ResolvedId);
    Resolved->RemoveItem(Resolved->GetInventory()[0], 1);
    TestTrue(TEXT("Empty external manifest never regenerates"), Resolved->EnsureContents(Error)
        && Resolved->HasGeneratedContents() && Resolved->GetInventory().IsEmpty());
    TestEqual(TEXT("External generation retains authored manual settings"), ManualSeed.Num(), 1);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectChestMapTest, "EF.Chests.Authored.MapInstances", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FProjectChestMapTest::RunTest(const FString&)
{
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && UWorld::RemovePIEPrefix(Context.World()->GetOutermost()->GetName()) == TEXT("/Game/_Game/Tests/ChestProcedural")) World = Context.World();
    if (!TestNotNull(TEXT("Authored QA map is loaded"), World)) return false;
    int32 Count = 0; TSet<FGuid> Identities; TSet<FName> Types;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        auto* Config = It->FindComponentByClass<UProjectProceduralChestComponent>();
        if (!Config) continue;
        ++Count;
        if (!TestNotNull(TEXT("Authored inherited storage"), It->FindComponentByClass<UProjectChestInventoryComponent>()) || !TestNotNull(TEXT("Authored profile"), Config->Profile.Get())) return false;
        TestTrue(TEXT("Instance mode is enabled in QA map"), Config->bProceduralMode);
        TestTrue(TEXT("Placed identity survives map serialization/cooking"), Config->StableIdentity.IsValid() && !Identities.Contains(Config->StableIdentity));
        Identities.Add(Config->StableIdentity); Types.Add(Config->Profile->Content.ContentType);
        if (World->IsGameWorld())
        {
            auto* Storage = It->FindComponentByClass<UProjectChestInventoryComponent>(); FString Error;
            TestFalse(TEXT("Cooked chest does not generate at BeginPlay"), Storage->HasGeneratedContents());
            if (!Storage->EnsureContents(Error)) { AddError(Error); return false; }
            TestTrue(TEXT("Cooked placed chest generates real inventory on opening"), Storage->HasGeneratedContents() && !Storage->GetInventory().IsEmpty());
            const auto Before = Storage->GetInventory(); Storage->EnsureContents(Error);
            const auto After = Storage->GetInventory();
            TestEqual(TEXT("Cooked repeat opening preserves entry count"), Before.Num(), After.Num());
            for (const auto& Item : Before)
            {
                FInventoryItem Restored;
                TestTrue(TEXT("Cooked repeat opening preserves native GUID and quantity"), Storage->GetItemByGuid(Item.GetItemGuid(), Restored) && Restored.Count == Item.Count);
            }
        }
    }
    TestEqual(TEXT("Three placed chests"), Count, 3);
    TestEqual(TEXT("Three distinct authored content types"), Types.Num(), 3);
    return !HasAnyErrors();
}
#endif
