#include "Calysto/EFCalystoPopulationPlannerV6.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoPopulationV6Tests
{
FEFCalystoCatalogEntryV6 MakeActorEntry(
	const TCHAR* EntryId,
	const TCHAR* ClassPath,
	const EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common)
{
	FEFCalystoCatalogEntryV6 Entry;
	Entry.StableId = FName(EntryId);
	Entry.DisplayName = EntryId;
	Entry.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(ClassPath));
	Entry.SelectionWeight = 1.0f;
	Entry.Tier = Tier;
	Entry.BaseThreatCost = 1.0f;
	Entry.MaximumPerVariant = 36;
	return Entry;
}

FEFCalystoCatalogOverlayV6 MakeActorCategory(
	const TCHAR* CategoryId,
	const TCHAR* EntryId,
	const TCHAR* ClassPath,
	const int32 MaximumPerFloor = 36)
{
	FEFCalystoCatalogOverlayV6 Category;
	Category.CategoryId = FName(CategoryId);
	Category.Mode = EEFCalystoCatalogOverlayModeV6::Replace;
	Category.Presence.ChanceAtFloor1 = 1.0f;
	Category.Presence.ChanceAtFloor100 = 1.0f;
	Category.Presence.Tau = 12.0f;
	Category.Tiers.AtFloor1.Common = 1.0f;
	Category.Tiers.AtFloor1.Uncommon = 0.0f;
	Category.Tiers.AtFloor1.Rare = 0.0f;
	Category.Tiers.AtFloor1.Epic = 0.0f;
	Category.Tiers.AtFloor1.Nothing = 0.0f;
	Category.Tiers.AtFloor100 = Category.Tiers.AtFloor1;
	Category.Limits.MinimumWhenPresent = 1;
	Category.Limits.MaximumPerFloor = MaximumPerFloor;
	Category.Catalog.Add(MakeActorEntry(EntryId, ClassPath));
	return Category;
}

FEFCalystoCatalogOverlayV6 MakeContentCategory()
{
	FEFCalystoCatalogOverlayV6 Category;
	Category.CategoryId = TEXT("ChestContents");
	Category.Mode = EEFCalystoCatalogOverlayModeV6::Replace;
	Category.Presence.ChanceAtFloor1 = 1.0f;
	Category.Presence.ChanceAtFloor100 = 1.0f;
	Category.Presence.Tau = 12.0f;
	Category.Tiers.AtFloor1.Common = 1.0f;
	Category.Tiers.AtFloor1.Uncommon = 0.0f;
	Category.Tiers.AtFloor1.Rare = 0.0f;
	Category.Tiers.AtFloor1.Epic = 0.0f;
	Category.Tiers.AtFloor1.Nothing = 0.0f;
	Category.Tiers.AtFloor100 = Category.Tiers.AtFloor1;
	Category.Limits.MinimumWhenPresent = 1;
	Category.Limits.MaximumPerFloor = 36;
	FEFCalystoChestContentEntryV6& Entry = Category.ChestContentsCatalog.AddDefaulted_GetRef();
	Entry.StableId = TEXT("ChestContent.Cap");
	Entry.DisplayName = TEXT("Capped Chest Content");
	Entry.ContentClass = TSoftClassPtr<UObject>(
		FSoftObjectPath(TEXT("/Game/Test/BP_ChestContent.BP_ChestContent_C")));
	Entry.SelectionWeight = 1.0f;
	Entry.Tier = EEFCalystoRarityTierV6::Common;
	Entry.MaximumPerFloor = 36;
	return Category;
}

FEFCalystoResolvedFloorPlanV6 MakeFloorPlan(
	const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
	const FString& Identity = TEXT("Default"))
{
	FEFCalystoResolvedFloorPlanV6 Plan;
	Plan.FloorSeed = 902104;
	Plan.StyleId = TEXT("Standard");
	Plan.MaximumRoomRecords = 2048;
	Plan.StyleCatalogs = StyleCatalogs;
	Plan.StyleCatalogHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		TEXT("StyleCatalog|") + Identity);
	Plan.FloorPlanHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		TEXT("FloorPlan|") + Identity);
	Plan.PolicyHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("Policy"));
	Plan.GlobalBudgets.MaximumEnemies = 25;
	Plan.GlobalBudgets.MaximumLooseFood = 8;
	Plan.GlobalBudgets.MaximumChests = 3;
	Plan.GlobalBudgets.MaximumLootActors = 4;
	Plan.GlobalBudgets.MaximumSpecialEvents = 4;
	Plan.GlobalBudgets.MaximumDirectorActors = 36;
	Plan.Threat.BudgetAtFloor1 = 1000.0f;
	Plan.Threat.BudgetAtFloor100 = 1000.0f;
	Plan.Threat.Tau = 12.0f;
	return Plan;
}

FEFCalystoRoomContextV6 MakeRoom(
	const FEFCalystoResolvedFloorPlanV6& Plan,
	const int64 StableRoomId,
	const FName ThemeId,
	const FString& CatalogHash)
{
	FEFCalystoRoomContextV6 Room;
	Room.StableRoomId = StableRoomId;
	Room.StyleId = Plan.StyleId;
	Room.bIsThemed = !ThemeId.IsEqual(
		UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase);
	Room.ThemeId = ThemeId;
	Room.CatalogHash = CatalogHash;
	Room.RoomContextHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		FString::Printf(TEXT("Room|%lld|%s"), StableRoomId, *ThemeId.ToString()));
	return Room;
}

FEFCalystoRoomManifestV6 MakeManifest(
	const FEFCalystoResolvedFloorPlanV6& Plan,
	TArray<FEFCalystoRoomContextV6> Rooms,
	const FString& Identity = TEXT("Default"))
{
	FEFCalystoRoomManifestV6 Manifest;
	Manifest.FloorSeed = Plan.FloorSeed;
	Manifest.StyleId = Plan.StyleId;
	Manifest.FloorPlanHash = Plan.FloorPlanHash;
	Manifest.Rooms = MoveTemp(Rooms);
	Manifest.ManifestHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		TEXT("Manifest|") + Identity);
	return Manifest;
}

const FEFCalystoCatalogOverlayV6* FindCategory(
	const TArray<FEFCalystoCatalogOverlayV6>& Catalogs,
	const TCHAR* CategoryId)
{
	return Catalogs.FindByPredicate([CategoryId](const FEFCalystoCatalogOverlayV6& Candidate)
	{
		return Candidate.CategoryId.IsEqual(FName(CategoryId), ENameCase::IgnoreCase);
	});
}

const FEFCalystoRoomPopulationPlanV6* FindRoom(
	const FEFCalystoPopulationPlanV6& Plan,
	const int64 StableRoomId)
{
	return Plan.Rooms.FindByPredicate([StableRoomId](const FEFCalystoRoomPopulationPlanV6& Room)
	{
		return Room.StableRoomId == StableRoomId;
	});
}
} // namespace EFCalystoPopulationV6Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationOverlaySemanticsV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.OverlaySemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationOverlaySemanticsV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationV6Tests;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient V6 policy"), Policy);
	if (!Policy) return false;
	Policy->InitializeV6Defaults();
	const FEFCalystoStyleProfileV6* Style = Policy->FindStyle(TEXT("Standard"));
	const FEFCalystoRoomThemeProfileV6* Forge = Policy->FindRoomTheme(TEXT("Forge"));
	TestNotNull(TEXT("Standard Style"), Style);
	TestNotNull(TEXT("Forge Theme"), Forge);
	if (!Style || !Forge) return false;

	FString Error;
	TArray<FEFCalystoCatalogOverlayV6> Effective;
	TestTrue(*FString::Printf(TEXT("Forge overlays resolve: %s"), *Error),
		FEFCalystoPopulationPlannerV6::ResolveEffectiveCatalogs(
			Style->Catalogs, Forge->Catalogs, Effective, Error));
	const FEFCalystoCatalogOverlayV6* Enemy = FindCategory(Effective, TEXT("Enemy"));
	TestNotNull(TEXT("Replace retains Forge Enemy"), Enemy);
	if (Enemy)
	{
		TestTrue(TEXT("Replace removes ranged and mage enemies"),
			Enemy->Catalog.ContainsByPredicate([](const FEFCalystoCatalogEntryV6& Entry)
			{
				return Entry.Archetype == FName(TEXT("Melee"));
			}) && !Enemy->Catalog.ContainsByPredicate([](const FEFCalystoCatalogEntryV6& Entry)
			{
				return Entry.Archetype == FName(TEXT("Mage")) || Entry.Archetype == FName(TEXT("Ranged"));
			}));
	}
	TestNull(TEXT("Block removes NPC instead of inheriting it"), FindCategory(Effective, TEXT("NPC")));
	TestNotNull(TEXT("Unmentioned ChestContents inherits from Style"), FindCategory(Effective, TEXT("ChestContents")));

	FEFCalystoCatalogOverlayV6 Extension = *FindCategory(Style->Catalogs, TEXT("Enemy"));
	Extension.Mode = EEFCalystoCatalogOverlayModeV6::Extend;
	Extension.Catalog.Reset();
	Extension.Catalog.Add(MakeActorEntry(
		TEXT("Enemy.Extension.Unique"), TEXT("/Game/Test/BP_Extension.BP_Extension_C")));
	Effective.Reset();
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Extend resolves: %s"), *Error),
		FEFCalystoPopulationPlannerV6::ResolveEffectiveCatalogs(
			Style->Catalogs, {Extension}, Effective, Error));
	Enemy = FindCategory(Effective, TEXT("Enemy"));
	TestTrue(TEXT("Extend keeps Style entries and adds the Theme entry"), Enemy &&
		Enemy->Catalog.Num() > Style->Catalogs[0].Catalog.Num() &&
		Enemy->Catalog.ContainsByPredicate([](const FEFCalystoCatalogEntryV6& Entry)
		{
			return Entry.StableId == FName(TEXT("Enemy.Extension.Unique"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationDeterminismV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.DeterminismAndInputOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationDeterminismV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationV6Tests;
	TArray<FEFCalystoCatalogOverlayV6> Catalogs = {
		MakeActorCategory(TEXT("Enemy"), TEXT("Enemy.Style"), TEXT("/Game/Test/BP_Enemy.BP_Enemy_C")),
		MakeActorCategory(TEXT("Food"), TEXT("Food.Style"), TEXT("/Game/Test/BP_Food.BP_Food_C"))};
	FEFCalystoCatalogEntryV6 EnemyB = MakeActorEntry(
		TEXT("Enemy.Style.B"), TEXT("/Game/Test/BP_EnemyB.BP_EnemyB_C"));
	EnemyB.SelectionWeight = 2.0f;
	FEFCalystoCatalogEntryV6 EnemyC = MakeActorEntry(
		TEXT("Enemy.Style.C"), TEXT("/Game/Test/BP_EnemyC.BP_EnemyC_C"));
	EnemyC.SelectionWeight = 3.0f;
	Catalogs[0].Catalog.Add(EnemyB);
	Catalogs[0].Catalog.Add(EnemyC);
	const FEFCalystoResolvedFloorPlanV6 FloorPlan = MakeFloorPlan(Catalogs, TEXT("Determinism"));
	FEFCalystoResolvedFloorPlanV6 ReorderedFloorPlan = FloorPlan;
	Algo::Reverse(ReorderedFloorPlan.StyleCatalogs);
	for (FEFCalystoCatalogOverlayV6& Category : ReorderedFloorPlan.StyleCatalogs)
	{
		Algo::Reverse(Category.Catalog);
	}
	TArray<FEFCalystoRoomContextV6> Rooms;
	for (int64 RoomId = 1; RoomId <= 32; ++RoomId)
	{
		Rooms.Add(MakeRoom(FloorPlan, RoomId,
			UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, FloorPlan.StyleCatalogHash));
	}
	FEFCalystoRoomManifestV6 Forward = MakeManifest(FloorPlan, Rooms, TEXT("Determinism"));
	Algo::Reverse(Rooms);
	FEFCalystoRoomManifestV6 Reverse = MakeManifest(FloorPlan, Rooms, TEXT("Determinism"));
	FEFCalystoPopulationBuildOptionsV6 Options;
	FEFCalystoPopulationPlanV6 A;
	FEFCalystoPopulationPlanV6 B;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Forward plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(FloorPlan, Forward, Options, A, Error));
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Reverse plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(ReorderedFloorPlan, Reverse, Options, B, Error));
	TestEqual(TEXT("Room, category, and weighted-entry order cannot change population"),
		A.PopulationHash, B.PopulationHash);
	TestEqual(TEXT("Two effective category/tier actor tables are compiled once"),
		A.CompiledActorSelectionTableCount, 2);
	TestEqual(TEXT("Every eligible category roll performs one constant-time actor draw"),
		A.ActorSelectionDrawCount, 64);
	TestEqual(TEXT("Canonical room plan count"), A.Rooms.Num(), 32);
	for (int32 Index = 1; Index < A.Rooms.Num(); ++Index)
	{
		TestTrue(TEXT("Room plans are sorted by Stable Room ID"),
			A.Rooms[Index - 1].StableRoomId < A.Rooms[Index].StableRoomId);
	}
	TestTrue(TEXT("Selected class closure is soft and non-empty"), !A.PreloadClassPaths.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationThemeIsolationV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.ThemeCatalogIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationThemeIsolationV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationV6Tests;
	const FEFCalystoCatalogOverlayV6 StyleEnemy = MakeActorCategory(
		TEXT("Enemy"), TEXT("Enemy.Style.Only"), TEXT("/Game/Test/BP_Style.BP_Style_C"));
	FEFCalystoResolvedFloorPlanV6 FloorPlan = MakeFloorPlan({StyleEnemy}, TEXT("Isolation"));
	FEFCalystoResolvedThemeProfileV6 Forge;
	Forge.ThemeId = TEXT("Forge");
	Forge.Catalogs = {MakeActorCategory(
		TEXT("Enemy"), TEXT("Enemy.Forge.Only"), TEXT("/Game/Test/BP_Forge.BP_Forge_C"))};
	Forge.CatalogHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("ForgeCatalog"));
	FEFCalystoResolvedThemeProfileV6 Shrine;
	Shrine.ThemeId = TEXT("Shrine");
	Shrine.Catalogs = {MakeActorCategory(
		TEXT("Enemy"), TEXT("Enemy.Shrine.Only"), TEXT("/Game/Test/BP_Shrine.BP_Shrine_C"))};
	Shrine.CatalogHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("ShrineCatalog"));
	FloorPlan.Themes = {Forge, Shrine};
	const FEFCalystoRoomManifestV6 Manifest = MakeManifest(FloorPlan, {
		MakeRoom(FloorPlan, 11, TEXT("Forge"), Forge.CatalogHash),
		MakeRoom(FloorPlan, 22, TEXT("Shrine"), Shrine.CatalogHash),
		MakeRoom(FloorPlan, 33, UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, FloorPlan.StyleCatalogHash)},
		TEXT("Isolation"));
	FEFCalystoPopulationPlanV6 Population;
	FEFCalystoPopulationBuildOptionsV6 Options;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Isolation plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(FloorPlan, Manifest, Options, Population, Error));
	const FEFCalystoRoomPopulationPlanV6* ForgeRoom = FindRoom(Population, 11);
	const FEFCalystoRoomPopulationPlanV6* ShrineRoom = FindRoom(Population, 22);
	const FEFCalystoRoomPopulationPlanV6* StyleRoom = FindRoom(Population, 33);
	auto HasOnlyEntry = [](const FEFCalystoRoomPopulationPlanV6* Room, const FName Expected)
	{
		return Room && Room->Decisions.Num() == 1 && Room->Decisions[0].EntryId == Expected;
	};
	TestTrue(TEXT("Forge receives only the frozen Forge catalog"),
		HasOnlyEntry(ForgeRoom, TEXT("Enemy.Forge.Only")));
	TestTrue(TEXT("Shrine receives only the frozen Shrine catalog"),
		HasOnlyEntry(ShrineRoom, TEXT("Enemy.Shrine.Only")));
	TestTrue(TEXT("NoTheme receives only the Style catalog"),
		HasOnlyEntry(StyleRoom, TEXT("Enemy.Style.Only")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationPlacementContractV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.PlacementIsFrozenAndHashed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationPlacementContractV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationV6Tests;

	FEFCalystoCatalogOverlayV6 WallCatalog = MakeActorCategory(
		TEXT("LooseLoot"), TEXT("Decoration.Wall.Test"),
		TEXT("/Game/Test/BP_WallDecoration.BP_WallDecoration_C"));
	WallCatalog.Catalog[0].PlacementZone = EEFCalystoPlacementZoneV6::WallMiddle;
	WallCatalog.Catalog[0].PositionJitterCm = 2.0f;

	const FEFCalystoResolvedFloorPlanV6 WallFloorPlan =
		MakeFloorPlan({WallCatalog}, TEXT("PlacementContract"));
	const FEFCalystoRoomManifestV6 Manifest = MakeManifest(WallFloorPlan, {
		MakeRoom(WallFloorPlan, 7401,
			UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId,
			WallFloorPlan.StyleCatalogHash)}, TEXT("PlacementContract"));
	FEFCalystoPopulationBuildOptionsV6 Options;
	FEFCalystoPopulationPlanV6 WallPlan;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Wall placement plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(
			WallFloorPlan, Manifest, Options, WallPlan, Error));
	const FEFCalystoRoomPopulationPlanV6* WallRoom = FindRoom(WallPlan, 7401);
	TestTrue(TEXT("Wall placement emits exactly one actor decision"),
		WallRoom && WallRoom->Decisions.Num() == 1);
	if (!WallRoom || WallRoom->Decisions.Num() != 1)
	{
		return false;
	}
	const FEFCalystoPopulationDecisionV6& WallDecision = WallRoom->Decisions[0];
	TestEqual(TEXT("Authored placement zone is frozen into the decision"),
		WallDecision.PlacementZone, EEFCalystoPlacementZoneV6::WallMiddle);
	TestTrue(TEXT("Authored position jitter is frozen exactly"),
		FMath::IsNearlyEqual(WallDecision.PositionJitterCm, 2.0f));

	FEFCalystoCatalogOverlayV6 FloorCatalog = WallCatalog;
	FloorCatalog.Catalog[0].PlacementZone = EEFCalystoPlacementZoneV6::Floor;
	const FEFCalystoResolvedFloorPlanV6 FloorFloorPlan =
		MakeFloorPlan({FloorCatalog}, TEXT("PlacementContract"));
	FEFCalystoPopulationPlanV6 FloorPlan;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Floor placement plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(
			FloorFloorPlan, Manifest, Options, FloorPlan, Error));
	const FEFCalystoRoomPopulationPlanV6* FloorRoom = FindRoom(FloorPlan, 7401);
	TestTrue(TEXT("Changing only Placement Zone changes Decision ID"),
		FloorRoom && FloorRoom->Decisions.Num() == 1
		&& FloorRoom->Decisions[0].DecisionId != WallDecision.DecisionId);
	TestTrue(TEXT("Changing only Placement Zone changes Room Population Hash"),
		FloorRoom && FloorRoom->RoomPopulationHash != WallRoom->RoomPopulationHash);
	TestNotEqual(TEXT("Changing only Placement Zone changes Population Hash"),
		FloorPlan.PopulationHash, WallPlan.PopulationHash);

	FEFCalystoCatalogOverlayV6 JitterCatalog = WallCatalog;
	JitterCatalog.Catalog[0].PositionJitterCm = 3.0f;
	const FEFCalystoResolvedFloorPlanV6 JitterFloorPlan =
		MakeFloorPlan({JitterCatalog}, TEXT("PlacementContract"));
	FEFCalystoPopulationPlanV6 JitterPlan;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Jitter placement plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(
			JitterFloorPlan, Manifest, Options, JitterPlan, Error));
	const FEFCalystoRoomPopulationPlanV6* JitterRoom = FindRoom(JitterPlan, 7401);
	TestTrue(TEXT("Changing only Position Jitter changes Decision ID"),
		JitterRoom && JitterRoom->Decisions.Num() == 1
		&& JitterRoom->Decisions[0].DecisionId != WallDecision.DecisionId);
	TestTrue(TEXT("Changing only Position Jitter changes Room Population Hash"),
		JitterRoom && JitterRoom->RoomPopulationHash != WallRoom->RoomPopulationHash);
	TestNotEqual(TEXT("Changing only Position Jitter changes Population Hash"),
		JitterPlan.PopulationHash, WallPlan.PopulationHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationGlobalCapsV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.GlobalBudgetCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationGlobalCapsV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationV6Tests;
	const TArray<FEFCalystoCatalogOverlayV6> Catalogs = {
		MakeActorCategory(TEXT("Enemy"), TEXT("Enemy.Cap"), TEXT("/Game/Test/BP_Enemy.BP_Enemy_C")),
		MakeActorCategory(TEXT("Food"), TEXT("Food.Cap"), TEXT("/Game/Test/BP_Food.BP_Food_C")),
		MakeActorCategory(TEXT("Chest"), TEXT("Chest.Cap"), TEXT("/Game/Test/BP_Chest.BP_Chest_C")),
		MakeActorCategory(TEXT("LooseLoot"), TEXT("Loot.Cap"), TEXT("/Game/Test/BP_Loot.BP_Loot_C")),
		MakeActorCategory(TEXT("Clothing"), TEXT("Clothing.Cap"), TEXT("/Game/Test/BP_Clothing.BP_Clothing_C")),
		MakeActorCategory(TEXT("NPC"), TEXT("NPC.Cap"), TEXT("/Game/Test/BP_NPC.BP_NPC_C")),
		MakeContentCategory()};
	FEFCalystoResolvedFloorPlanV6 FloorPlan = MakeFloorPlan(Catalogs, TEXT("Caps"));
	FloorPlan.GlobalBudgets.MaximumEnemies = 3;
	FloorPlan.GlobalBudgets.MaximumLooseFood = 2;
	FloorPlan.GlobalBudgets.MaximumChests = 1;
	FloorPlan.GlobalBudgets.MaximumLootActors = 2;
	FloorPlan.GlobalBudgets.MaximumSpecialEvents = 1;
	FloorPlan.GlobalBudgets.MaximumDirectorActors = 20;
	TArray<FEFCalystoRoomContextV6> Rooms;
	for (int64 RoomId = 1; RoomId <= 64; ++RoomId)
	{
		Rooms.Add(MakeRoom(FloorPlan, RoomId,
			UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, FloorPlan.StyleCatalogHash));
	}
	FEFCalystoRoomManifestV6 Manifest = MakeManifest(FloorPlan, Rooms, TEXT("Caps"));
	FEFCalystoPopulationBuildOptionsV6 Options;
	FEFCalystoPopulationPlanV6 Population;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Capped plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(FloorPlan, Manifest, Options, Population, Error));
	TestEqual(TEXT("Enemy cap is applied once per floor"), Population.EnemyCount, 3);
	TestEqual(TEXT("Loose-food cap is applied once per floor"), Population.LooseFoodCount, 2);
	TestEqual(TEXT("Chest cap is applied once per floor"), Population.ChestCount, 1);
	TestEqual(TEXT("LooseLoot and Clothing share one floor-wide loot cap"), Population.LootActorCount, 2);
	TestEqual(TEXT("NPC uses the floor-wide special-event cap"), Population.SpecialEventCount, 1);
	TestEqual(TEXT("All accepted actors account for the bucket totals"), Population.ActorDecisionCount, 9);
	TestEqual(TEXT("Six actor category/tier tables are compiled once for all rooms"),
		Population.CompiledActorSelectionTableCount, 6);
	TestEqual(TEXT("Actor draws remain one O(1) lookup per room/category"),
		Population.ActorSelectionDrawCount, 64 * 6);
	TestEqual(TEXT("Chest contents compile one immutable table"),
		Population.CompiledContentSelectionTableCount, 1);
	TestEqual(TEXT("The accepted chest performs one constant-time content draw"),
		Population.ContentSelectionDrawCount, 1);
	TestEqual(TEXT("The accepted chest receives one content instruction"),
		Population.ChestContentDecisionCount, 1);

	FloorPlan.GlobalBudgets.MaximumDirectorActors = 4;
	FloorPlan.FloorPlanHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("FloorPlan|DirectorCap"));
	Manifest.FloorPlanHash = FloorPlan.FloorPlanHash;
	Manifest.ManifestHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(TEXT("Manifest|DirectorCap"));
	for (FEFCalystoRoomContextV6& Room : Manifest.Rooms)
	{
		Room.StyleId = FloorPlan.StyleId;
	}
	FEFCalystoPopulationPlanV6 DirectorCapped;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Director-capped plan builds: %s"), *Error),
		FEFCalystoPopulationPlannerV6::BuildPlan(
			FloorPlan, Manifest, Options, DirectorCapped, Error));
	TestEqual(TEXT("Maximum Director Actors is a final hard ceiling"),
		DirectorCapped.ActorDecisionCount, 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
