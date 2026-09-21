#include "Calysto/EFCalystoPopulationMaterializerV6.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Calysto/EFCalystoPCGRuntimeGraphV6.h"
#include "Data/PCGPointData.h"
#include "PCGData.h"
#include "PCGPoint.h"

namespace EFCalystoPopulationMaterializerV6Tests
{
	FString Hash(const TCHAR* Value)
	{
		return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Value);
	}

	void BuildValidPlan(
		FEFCalystoRoomManifestV6& OutManifest,
		FEFCalystoPopulationPlanV6& OutPlan)
	{
		const FString FloorPlanHash = Hash(TEXT("MaterializerFloor"));
		const FString ManifestHash = Hash(TEXT("MaterializerManifest"));
		const FString CatalogHash = Hash(TEXT("MaterializerCatalog"));

		FEFCalystoRoomContextV6 Room;
		Room.StableRoomId = 4101;
		Room.LocalCenter = FVector(100.0, 200.0, 0.0);
		Room.Extents = FVector(500.0, 500.0, 250.0);
		Room.TopologyKind = TEXT("Ordinary");
		Room.StyleId = TEXT("Standard");
		Room.ThemeId = UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId;
		Room.CatalogHash = CatalogHash;
		Room.RoomContextHash = Hash(TEXT("MaterializerRoom"));

		OutManifest.FloorSeed = 9191;
		OutManifest.StyleId = Room.StyleId;
		OutManifest.FloorPlanHash = FloorPlanHash;
		OutManifest.Rooms = {Room};
		OutManifest.ManifestHash = ManifestHash;

		FEFCalystoPopulationDecisionV6 Chest;
		Chest.Kind = EEFCalystoPopulationDecisionKindV6::Actor;
		Chest.StableRoomId = Room.StableRoomId;
		Chest.StyleId = Room.StyleId;
		Chest.ThemeId = Room.ThemeId;
		Chest.CategoryId = TEXT("Chest");
		Chest.EntryId = TEXT("Chest.Test");
		Chest.ClassPath = FSoftObjectPath(TEXT("/Script/Engine.Actor"));
		Chest.DecisionId = Hash(TEXT("MaterializerChest"));

		FEFCalystoPopulationDecisionV6 Content;
		Content.Kind = EEFCalystoPopulationDecisionKindV6::ChestContent;
		Content.StableRoomId = Room.StableRoomId;
		Content.StyleId = Room.StyleId;
		Content.ThemeId = Room.ThemeId;
		Content.CategoryId = TEXT("ChestContents");
		Content.EntryId = TEXT("Content.Test");
		Content.ClassPath = FSoftObjectPath(TEXT("/Script/CoreUObject.Object"));
		Content.ParentDecisionId = Chest.DecisionId;
		Content.DecisionId = Hash(TEXT("MaterializerContent"));

		FEFCalystoRoomPopulationPlanV6 RoomPlan;
		RoomPlan.StableRoomId = Room.StableRoomId;
		RoomPlan.ThemeId = Room.ThemeId;
		RoomPlan.EffectiveCatalogHash = CatalogHash;
		RoomPlan.Decisions = {Chest, Content};
		RoomPlan.RoomPopulationHash = Hash(TEXT("MaterializerRoomPopulation"));

		OutPlan.FloorSeed = OutManifest.FloorSeed;
		OutPlan.FloorNumber = 1;
		OutPlan.StyleId = OutManifest.StyleId;
		OutPlan.FloorPlanHash = FloorPlanHash;
		OutPlan.RoomManifestHash = ManifestHash;
		OutPlan.Rooms = {RoomPlan};
		OutPlan.PreloadClassPaths = {Chest.ClassPath, Content.ClassPath};
		OutPlan.ActorDecisionCount = 1;
		OutPlan.ChestContentDecisionCount = 1;
		OutPlan.PopulationHash = Hash(TEXT("MaterializerPopulation"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationMaterializerPlanValidationV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.MaterializerPlanValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationMaterializerPlanValidationV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationMaterializerV6Tests;
	FEFCalystoRoomManifestV6 Manifest;
	FEFCalystoPopulationPlanV6 Plan;
	BuildValidPlan(Manifest, Plan);
	FString Error;
	TestTrue(*FString::Printf(TEXT("Valid frozen plan: %s"), *Error),
		FEFCalystoPopulationMaterializerV6::ValidatePlan(Manifest, Plan, Error));

	FEFCalystoPopulationPlanV6 Orphaned = Plan;
	Orphaned.Rooms[0].Decisions[1].ParentDecisionId = Hash(TEXT("MissingParent"));
	Error.Reset();
	TestFalse(TEXT("Chest content cannot escape its frozen parent"),
		FEFCalystoPopulationMaterializerV6::ValidatePlan(Manifest, Orphaned, Error));
	TestTrue(TEXT("Orphan rejection is diagnostic"), !Error.IsEmpty());

	FEFCalystoRoomManifestV6 ProtectedManifest = Manifest;
	ProtectedManifest.Rooms[0].RoomFlags = static_cast<int32>(EEFCalystoRoomFlagsV6::Start);
	Error.Reset();
	TestFalse(TEXT("Protected rooms cannot materialize population"),
		FEFCalystoPopulationMaterializerV6::ValidatePlan(ProtectedManifest, Plan, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoPopulationPreloadClosureV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.PreloadClosureIsCanonicalAndDeduplicated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoPopulationPreloadClosureV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoPopulationMaterializerV6Tests;
	FEFCalystoPopulationPlanV6 Plan;
	Plan.PopulationHash = Hash(TEXT("PreloadClosure"));
	const FSoftObjectPath ActorPath(TEXT("/Script/Engine.Actor"));
	const FSoftObjectPath ObjectPath(TEXT("/Script/CoreUObject.Object"));
	Plan.PreloadClassPaths = {ObjectPath, ActorPath, ObjectPath};

	TArray<FSoftObjectPath> Paths;
	FString Error;
	TestTrue(*FString::Printf(TEXT("Canonical preload closure: %s"), *Error),
		FEFCalystoPopulationMaterializerV6::GatherRequiredPreloadPaths(
			Plan, Paths, Error));
	TestEqual(TEXT("Duplicate selected classes collapse into one retained path"), Paths.Num(), 2);
	TestTrue(TEXT("Actor class remains in the exact closure"), Paths.Contains(ActorPath));
	TestTrue(TEXT("Object class remains in the exact closure"), Paths.Contains(ObjectPath));
	for (int32 Index = 1; Index < Paths.Num(); ++Index)
	{
		TestTrue(
			TEXT("Preload paths use stable canonical ordering"),
			Paths[Index - 1].ToString().ToLower() < Paths[Index].ToString().ToLower());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoNativeFloorCandidatesV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Population.NativeFloorCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeFloorCandidatesV6Test::RunTest(const FString& Parameters)
{
	using namespace EFCalystoPopulationMaterializerV6Tests;
	FEFCalystoRoomManifestV6 Manifest;
	FEFCalystoPopulationPlanV6 Plan;
	BuildValidPlan(Manifest, Plan);
	FEFCalystoRoomContextV6 Protected = Manifest.Rooms[0];
	Protected.StableRoomId = 4102;
	Protected.LocalCenter = FVector(1000, 200, 0);
	Protected.RoomFlags = static_cast<int32>(EEFCalystoRoomFlagsV6::Start);
	Manifest.Rooms.Add(Protected);
	UPCGPointData* Points = NewObject<UPCGPointData>();
	const FTransform DungeonTransform(FRotator(0, 90, 0), FVector(5000, -2000, 50));
	for (const FVector Local : {Manifest.Rooms[0].LocalCenter, Protected.LocalCenter})
	{
		FPCGPoint Point;
		Point.Transform.SetLocation(DungeonTransform.TransformPosition(Local));
		Points->GetMutablePoints().Add(Point);
	}
	// Duplicate native points collapse deterministically, not into extra capacity.
	const FPCGPoint Duplicate = Points->GetPoints()[0];
	Points->GetMutablePoints().Add(Duplicate);
	FPCGDataCollection Output;
	FPCGTaggedData& Tagged = Output.TaggedData.AddDefaulted_GetRef();
	Tagged.Pin = FEFCalystoPlacementCandidatePinsV6::Floor;
	Tagged.Data = Points;
	TArray<FEFCalystoPlacementCandidateV6> Candidates;
	FString Error;
	TestTrue(TEXT("Native floor stream is accepted"),
		FEFCalystoPopulationMaterializerV6::GatherNativePlacementCandidates(
			Output, DungeonTransform, Manifest, Plan, Candidates, Error));
	TestEqual(TEXT("Floor candidate retained; protected neighbor and duplicate excluded"), Candidates.Num(), 1);
	if (Candidates.Num() == 1)
	{
		TestEqual(TEXT("Floor ownership retained"), Candidates[0].StableRoomId, int64(4101));
		TestTrue(TEXT("World-space transform retained"), Candidates[0].Transform.GetLocation().Equals(
			DungeonTransform.TransformPosition(Manifest.Rooms[0].LocalCenter)));
		TestTrue(TEXT("Floor zone retained"), Candidates[0].Zone == EEFCalystoPlacementZoneV6::Floor);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
