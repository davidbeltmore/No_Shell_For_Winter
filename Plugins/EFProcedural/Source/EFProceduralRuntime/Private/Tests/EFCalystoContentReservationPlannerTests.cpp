#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoContentReservationPlanner.h"
#include "Algo/Reverse.h"
#include <limits>
#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace EFCalystoContentReservationTests
{
	using FPlanner = FEFCalystoContentReservationPlanner;
	FGuid Id(uint32 Value) { return FGuid(0x434F4E54, 0x454E5454, 0, Value); }

	UEFCalystoDungeonDirectorAsset* Asset()
	{
		auto* A = NewObject<UEFCalystoDungeonDirectorAsset>();
		FEFCalystoStyle& S = A->Styles.AddDefaulted_GetRef();
		S.Selection.Id = Id(1); S.Selection.DisplayName = TEXT("Reservation Test Style");
		S.Layout.DungeonSize.Value = 24; S.Layout.CandidateDensity.Value = 0.32; S.Layout.SidePathPercent.Value = 50;
		const TSoftObjectPtr<UMaterialInterface> Material(FSoftObjectPath(TEXT("/Game/Test/ReservationMaterial.ReservationMaterial")));
		S.Materials.Floor = S.Materials.Wall = S.Materials.Roof = Material;
		FEFCalystoArchitectureEntry Mesh;
		Mesh.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Test/ReservationMesh.ReservationMesh")));
		Mesh.Selection.Id = Id(2); Mesh.Selection.DisplayName = TEXT("Required Surface");
		S.Architecture.Floor.Add(Mesh); Mesh.Selection.Id = Id(3); S.Architecture.Wall.Add(Mesh);
		Mesh.Selection.Id = Id(4); S.Architecture.Roof.Add(Mesh);
		S.Decals.Mode = EEFCalystoDecalMode::Block;
		FEFCalystoTheme& T = A->RoomThemes.AddDefaulted_GetRef();
		T.Selection.Id = Id(5); T.Selection.DisplayName = TEXT("Reservation Test Theme"); T.Decals.Mode = EEFCalystoDecalMode::Block;
		return A;
	}
	FEFCalystoContentEntry Entry(uint32 Value, int32 Maximum = 8)
	{
		FEFCalystoContentEntry E;
		E.Selection.Id = Id(Value); E.Selection.DisplayName = TEXT("Editable label");
		E.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/Test/ReservationActor.ReservationActor_C")));
		E.MaximumPerFloor = Maximum; E.Placement.FootprintHalfExtent = FVector(20,20,40);
		E.Placement.Spacing = 40; E.Placement.Clearance = 5; E.Placement.PositionVariationCm = 2;
		return E;
	}
	FEFCalystoContentGroup Group(EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Enemy)
	{
		FEFCalystoContentGroup G; G.Id = Id(100 + uint32(Role)); G.DisplayName = TEXT("Any category label"); G.Role = Role;
		G.Budget = Role == EEFCalystoGameplayRole::Enemy ? EEFCalystoBudgetMembership::Enemy
			: Role == EEFCalystoGameplayRole::Container ? EEFCalystoBudgetMembership::Chest : EEFCalystoBudgetMembership::None;
		G.Chance.FirstPercent = G.Chance.LastPercent = 100; G.MaximumPerFloor = 8;
		return G;
	}
	FEFCalystoContentRoom Room(int64 RoomId, EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Enemy)
	{
		FEFCalystoContentRoom R; R.RoomId = RoomId; R.AllowedRoles.Add(Role); return R;
	}
	FEFCalystoContentSurface Surface(uint32 Value, int64 RoomId, FVector Position, TArray<FGuid> Entries,
		EEFCalystoGameplayRole Role = EEFCalystoGameplayRole::Enemy)
	{
		FEFCalystoContentSurface S;
		S.Id = Id(Value); S.RoomId = RoomId; S.Transform.SetLocation(Position);
		S.AvailableHalfExtent = FVector(200); S.AvailableClearanceCm = 20;
		S.bCollisionValidated = S.bCollisionContractValidated = S.bNavigationValidated = true;
		S.ReservedLocalBounds = FBox(FVector(-40,-40,-40),FVector(40,40,40));
		S.CollisionContractHash = TEXT("PlannerFixtureCollisionContract"); S.AllowedRoles.Add(Role);
		for (const FGuid& EntryId : Entries) S.CompatibleEntryIds.Add(EntryId);
		return S;
	}
	FEFCalystoContentReservationRequest Request()
	{
		FEFCalystoContentReservationRequest R; R.Random.StyleId = Id(1); R.Random.RunSeed = 1779679224;
		R.Random.FloorNumber = 5; return R;
	}
	bool Compile(FAutomationTestBase& Test, const UEFCalystoDungeonDirectorAsset& A, FEFCalystoCompiledDirector& C)
	{
		TArray<FEFCalystoValidationIssue> Issues;
		if (A.Compile(C, Issues)) return true;
		for (const auto& I : Issues) Test.AddError(I.Field + TEXT(": ") + I.Message);
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentFeasibleCountsTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.FeasibleCountsAndAtomicFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentFeasibleCountsTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A = Asset(); auto G = Group(); G.Entries.Add(Entry(10));
	G.Amount.Distribution = EEFCalystoDistribution::Uniform; G.Amount.Minimum = 1; G.Amount.Maximum = 3;
	A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this, *A, C)) return false;
	auto R = Request(); R.Rooms.Add(Room(1));
	R.Surfaces = {Surface(200,1,FVector(0,0,0),{Id(10)}), Surface(201,1,FVector(400,0,0),{Id(10)})};
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	for (int32 Seed = 0; Seed < 24; ++Seed)
	{
		R.Random.RunSeed = Seed;
		if (!TestTrue(TEXT("Bounded conditional reservation succeeds"), FPlanner::Build(C,R,M,Report))) { AddError(Report.Message); return false; }
		if (!TestEqual(TEXT("Exactly one content opportunity"), Report.Opportunities.Num(), 1)) return false;
		const auto& O = Report.Opportunities[0];
		TestTrue(TEXT("Only feasible requested counts are selected"), O.SelectedAmount >= 1 && O.SelectedAmount <= 2);
		TestEqual(TEXT("Selected count is reserved exactly"), M.GetElements().Num(), O.SelectedAmount);
		TestTrue(TEXT("Requested amount mass is reported separately"), FMath::IsNearlyEqual(O.FeasibleAmountProbabilityMass, 2.0/3.0, 1e-10));
	}
	A->Styles[0].Content[0].Amount.Distribution = EEFCalystoDistribution::Fixed;
	A->Styles[0].Content[0].Amount.Amount = 3;
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("An impossible opportunity is ineligible before Chance"), FPlanner::Build(C,R,M,Report));
	TestTrue(TEXT("No selected payload was dropped"), M.GetElements().IsEmpty());
	TestFalse(TEXT("Impossible fixed amount does not roll Chance"), Report.Opportunities[0].bChanceRolled);
	TestEqual(TEXT("Impossible fixed amount is explicitly classified"), Report.Opportunities[0].Outcome, EEFCalystoContentOpportunityOutcome::NoFeasibleAmount);
	R.Limits.MaximumWorkUnits = 1;
	TestFalse(TEXT("Work exhaustion rejects the complete manifest"), FPlanner::Build(C,R,M,Report));
	TestFalse(TEXT("A work failure cannot return a valid manifest"), M.IsValid());
	TestTrue(TEXT("A work failure exposes no partial dependencies"), M.GetSelectedDependencies().IsEmpty());
	TestEqual(TEXT("Unknown bounded feasibility is not called impossible"), Report.Status, EEFCalystoContentPlanningStatus::WorkLimitExceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentCompletionTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.CapacityPreservingWeightedChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentCompletionTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A = Asset(); auto G = Group(); G.Amount.Amount = 2;
	auto Flexible = Entry(10,1); Flexible.Selection.Weight = 1000000;
	const auto Restricted = Entry(11,1); G.Entries = {Flexible,Restricted}; A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R = Request(); R.Rooms.Add(Room(1));
	R.Surfaces = {Surface(200,1,FVector(0,0,0),{Id(10),Id(11)}), Surface(201,1,FVector(400,0,0),{Id(10)})};
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	for (int32 Seed=0; Seed<64; ++Seed)
	{
		R.Random.RunSeed=Seed;
		if (!TestTrue(TEXT("Weighted selection preserves an exact completion"), FPlanner::Build(C,R,M,Report))) { AddError(Report.Message); return false; }
		if (!TestEqual(TEXT("Both requested elements reserve"), M.GetElements().Num(), 2)) return false;
		for (const auto& E : M.GetElements())
			TestEqual(TEXT("Flexible entry cannot consume the restricted entry's only site"), E.CandidateId,
				E.Entry.Selection.Id == Id(10) ? Id(201) : Id(200));
	}
	// Every source array and editable label can change without changing decisions.
	const FString Expected = M.GetHash();
	A->Styles[0].Selection.DisplayName = TEXT("Renamed Style");
	A->Styles[0].Content[0].DisplayName = TEXT("This label is not a role");
	Swap(A->Styles[0].Content[0].Entries[0], A->Styles[0].Content[0].Entries[1]);
	for (auto& E : A->Styles[0].Content[0].Entries) E.Selection.DisplayName = TEXT("Renamed entry");
	Swap(R.Surfaces[0],R.Surfaces[1]);
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Renamed and reordered input still plans"), FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Labels/order do not change frozen decisions"), M.GetHash(), Expected);
	R.TraitSnapshot = FEFCalystoTraits{};
	TestTrue(TEXT("Disabled adaptation accepts an independent explicit input"), FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Disabled adaptation has exactly zero influence"), M.GetHash(), Expected);
	A->Advanced.Adaptation.bEnabled=true; A->Advanced.Adaptation.MaximumEffectPercent=100;
	FEFCalystoTraitBinding Binding; Binding.Source = EEFCalystoTraitSource::Snapshot;
	Binding.Trait = EEFCalystoTrait::Danger; Binding.Role = EEFCalystoGameplayRole::Enemy;
	Binding.EffectAtZeroPercent = -100; Binding.EffectAtOnePercent = 100;
	A->Advanced.Adaptation.Bindings.Add(Binding);
	A->Styles[0].Content[0].Chance.FirstPercent=A->Styles[0].Content[0].Chance.LastPercent=50;
	R.TraitSnapshot.GetValue().Danger = 1;
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Explicit bounded adaptation can change Chance"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Requested Chance remains independently reported"),Report.Opportunities[0].RequestedChancePercent,50.0);
	TestEqual(TEXT("Enabled normalized input applies the authored bound"),Report.Opportunities[0].EffectiveChancePercent,100.0);
	TestEqual(TEXT("Adaptation changes no committed count"),M.GetElements().Num(),2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentFloorBudgetsTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.SharedFloorBudgetsAndThemeModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentFloorBudgetsTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A = Asset(); auto G=Group(); G.MaximumPerFloor=2; G.Entries.Add(Entry(10,8)); A->Styles[0].Content.Add(G);
	auto Overlay=G; Overlay.Mode=EEFCalystoContentMode::Replace; Overlay.MaximumPerFloor=8; Overlay.Entries={Entry(11,8)};
	A->RoomThemes[0].Content.Add(Overlay);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request();
	for (int32 Index=1; Index<=3; ++Index)
	{
		auto CurrentRoom=Room(Index); if (Index>1) CurrentRoom.ThemeId=Id(5); R.Rooms.Add(CurrentRoom);
		R.Surfaces.Add(Surface(200+Index,Index,FVector(Index*1000,0,0),{Id(10),Id(11)}));
	}
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	if (!TestTrue(TEXT("Theme Replace respects shared Style category capacity"),FPlanner::Build(C,R,M,Report))) return false;
	TestEqual(TEXT("Two across all rooms, never two per room"),M.GetElements().Num(),2);
	TestEqual(TEXT("Shared category usage is returned once"),M.GetFinalUsage().Categories.FindRef(EEFCalystoGameplayRole::Enemy),2);
	Overlay.Mode=EEFCalystoContentMode::Extend; Overlay.Chance.FirstPercent=Overlay.Chance.LastPercent=0;
	Overlay.Amount.Amount=7; Overlay.MaximumPerFloor=8;
	A->RoomThemes[0].Content[0]=Overlay;
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Extend uses explicit inherited probability/count settings"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Unselected overrides do not change inherited Chance or Amount"),M.GetElements().Num(),2);
	const FString Hash=M.GetHash(); Swap(R.Rooms[0],R.Rooms[2]); Swap(R.Surfaces[0],R.Surfaces[2]);
	TestTrue(TEXT("Room/source order does not affect budget priority"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Reordered room requests produce identical reservations"),M.GetHash(),Hash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentEligibilityTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.WinterEligibilityAndSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentEligibilityTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A=Asset(); auto G=Group(); G.Amount.Amount=2;
	auto Winter=Entry(10,8); Winter.Rarity=EEFCalystoRarity::Winter; Winter.bRequiresGraveyardEligibility=true;
	Winter.Selection.CooldownFloors=1; Winter.Placement.bRequiresNavigation=true; Winter.Placement.Spacing=80;
	G.Entries.Add(Winter); A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request(); R.Rooms.Add(Room(1)); R.GraveyardEligibleEntryIds.Add(Id(10));
	R.Surfaces={Surface(200,1,FVector(0,0,0),{Id(10)}),Surface(201,1,FVector(100,0,0),{Id(10)}),Surface(202,1,FVector(500,0,0),{Id(10)})};
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	if (!TestTrue(TEXT("Populated Winter has no implicit Nothing outcome"),FPlanner::Build(C,R,M,Report))) return false;
	TestEqual(TEXT("Spacing still permits exactly two reservations"),M.GetElements().Num(),2);
	bool Far=false;
	for (const auto& E:M.GetElements()) { Far|=E.CandidateId==Id(202); TestEqual(TEXT("Winter tier is retained"),E.Entry.Rarity,EEFCalystoRarity::Winter); }
	TestTrue(TEXT("Overlapping spacing cannot consume both adjacent candidates"),Far);
	R.LastSelectedFloor.Add(Id(10),4);
	TestTrue(TEXT("Cooldown is eligibility before Chance"),FPlanner::Build(C,R,M,Report));
	TestFalse(TEXT("Cooldown exhaustion does not roll Chance"),Report.Opportunities[0].bChanceRolled);
	R.LastSelectedFloor.Reset(); R.GraveyardEligibleEntryIds.Reset();
	TestTrue(TEXT("Graveyard eligibility is explicit"),FPlanner::Build(C,R,M,Report));
	TestTrue(TEXT("No unqualified recall payload is reserved"),M.GetElements().IsEmpty());
	R.GraveyardEligibleEntryIds.Add(Id(10)); R.Rooms[0].Protection=EEFCalystoProtectedRoom::Start;
	TestTrue(TEXT("Protected rooms are excluded"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Protected-room absence is eligibility, not probability"),Report.Opportunities[0].Outcome,EEFCalystoContentOpportunityOutcome::IneligibleRoom);
	R.Rooms[0].Protection=EEFCalystoProtectedRoom::None;
	for (auto& S:R.Surfaces) S.bNavigationValidated=false;
	TestTrue(TEXT("Required navigation precedes selection"),FPlanner::Build(C,R,M,Report));
	TestFalse(TEXT("Unverified navigation does not roll Chance"),Report.Opportunities[0].bChanceRolled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentSafetyCeilingsTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.SafetyCeilingsAndSharedUsage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentSafetyCeilingsTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	struct FCase { int32 FEFCalystoSafetyCeilings::* Ceiling; int32 FEFCalystoFloorBudgets::* Budget;
		EEFCalystoBudgetMembership Bucket; EEFCalystoGameplayRole Role; };
	const FCase Cases[] = {
		{&FEFCalystoSafetyCeilings::Enemies, &FEFCalystoFloorBudgets::Enemies, EEFCalystoBudgetMembership::Enemy, EEFCalystoGameplayRole::Enemy},
		{&FEFCalystoSafetyCeilings::LooseFood, &FEFCalystoFloorBudgets::LooseFood, EEFCalystoBudgetMembership::LooseFood, EEFCalystoGameplayRole::Food},
		{&FEFCalystoSafetyCeilings::Chests, &FEFCalystoFloorBudgets::Chests, EEFCalystoBudgetMembership::Chest, EEFCalystoGameplayRole::Container},
		{&FEFCalystoSafetyCeilings::LootActors, &FEFCalystoFloorBudgets::LootActors, EEFCalystoBudgetMembership::LootActor, EEFCalystoGameplayRole::LooseLoot},
		{&FEFCalystoSafetyCeilings::SpecialEvents, &FEFCalystoFloorBudgets::SpecialEvents, EEFCalystoBudgetMembership::SpecialEvent, EEFCalystoGameplayRole::SpecialEvent},
		{&FEFCalystoSafetyCeilings::TotalActors, &FEFCalystoFloorBudgets::TotalActors, EEFCalystoBudgetMembership::None, EEFCalystoGameplayRole::Prop}};
	for (const auto& Case : Cases)
	{
		auto* A = Asset(); auto G = Group(Case.Role); G.Budget = Case.Bucket; G.Entries.Add(Entry(10));
		G.Amount.Distribution = EEFCalystoDistribution::Uniform; G.Amount.Minimum = 1; G.Amount.Maximum = 3;
		A->Styles[0].Content.Add(G);
		auto Overlay = G; Overlay.Entries = {Entry(11)}; A->RoomThemes[0].Content.Add(Overlay);
		A->Advanced.HardCeilings.*Case.Ceiling = A->Styles[0].FloorBudgets.*Case.Budget = 2;
		auto R = Request(); R.InitialUsage.Actors = 1;
		if (Case.Bucket != EEFCalystoBudgetMembership::None) R.InitialUsage.Buckets.Add(Case.Bucket, 1);
		for (int32 Index = 1; Index <= 3; ++Index)
		{
			auto CurrentRoom = Room(Index, Case.Role); if (Index > 1) CurrentRoom.ThemeId = Id(5); R.Rooms.Add(CurrentRoom);
			R.Surfaces.Add(Surface(200 + Index, Index, FVector(Index * 1000, 0, 0), {Id(10), Id(11)}, Case.Role));
		}
		if (Case.Role == EEFCalystoGameplayRole::Container)
		{ R.ContainerCapacities.Add(Id(10), FEFCalystoContainerCapacity()); R.ContainerCapacities.Add(Id(11), FEFCalystoContainerCapacity()); }
		for (const auto Mode : {EEFCalystoContentMode::Replace, EEFCalystoContentMode::Extend})
		{
			A->RoomThemes[0].Content[0].Mode = Mode;
			FEFCalystoCompiledDirector C; if (!Compile(*this, *A, C)) return false;
			FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
			if (!TestTrue(TEXT("Global ceiling and existing usage admit a complete bounded plan"), FPlanner::Build(C, R, M, Report)))
			{ AddError(Report.Message); return false; }
			TestEqual(TEXT("Three rooms share exactly one remaining actor capacity"), M.GetElements().Num(), 1);
			TestEqual(TEXT("Existing actor usage counts toward final total"), M.GetFinalUsage().Actors, 2);
			if (Case.Bucket != EEFCalystoBudgetMembership::None)
				TestEqual(TEXT("Existing typed usage counts toward global ceiling"), M.GetFinalUsage().Buckets.FindRef(Case.Bucket), 2);
			int32 ChanceRolls = 0;
			for (const auto& Opportunity : Report.Opportunities)
				if (Opportunity.bChanceRolled)
				{
					++ChanceRolls;
					TestEqual(TEXT("Capacity does not alter authored Chance"), Opportunity.EffectiveChancePercent, 100.0);
					TestTrue(TEXT("Amount conditions on one feasible count, without truncating a draw"), FMath::IsNearlyEqual(Opportunity.FeasibleAmountProbabilityMass, 1.0 / 3.0, 1e-10));
				}
			TestEqual(TEXT("Exhausted shared capacity prevents later Chance rolls"), ChanceRolls, 1);
			const FString Hash = M.GetHash();
			A->Advanced.HardCeilings.*Case.Ceiling = 3;
			if (!Compile(*this, *A, C)) return false;
			TestTrue(TEXT("An unused higher ceiling still plans"), FPlanner::Build(C, R, M, Report));
			TestEqual(TEXT("Unchanged effective capacities preserve exact selected manifest identity"), M.GetHash(), Hash);
			A->Advanced.HardCeilings.*Case.Ceiling = 2;
			auto Excess = R; Excess.InitialUsage.Actors = 3;
			if (Case.Bucket != EEFCalystoBudgetMembership::None) Excess.InitialUsage.Buckets.Add(Case.Bucket, 3);
			TestFalse(TEXT("Already exceeded safety capacity rejects atomically"), FPlanner::Build(C, Excess, M, Report));
			TestFalse(TEXT("Invalid initial usage cannot expose a partial manifest"), M.IsValid());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentContainerTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.PerContainerInventoryReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentContainerTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A=Asset();
	auto Chests=Group(EEFCalystoGameplayRole::Container); Chests.Entries.Add(Entry(10,2)); Chests.MaximumPerFloor=2;
	auto Contents=Group(EEFCalystoGameplayRole::ContainerContent); Contents.Amount.Amount=2; Contents.MaximumPerFloor=4;
	auto Recall=Entry(11,4); Recall.ActorClass.Reset();
	Recall.InventoryClass=TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/Test/RecallInventory.RecallInventory_C")));
	Recall.Rarity=EEFCalystoRarity::Winter; Recall.bRequiresGraveyardEligibility=true; Contents.Entries.Add(Recall);
	A->Styles[0].Content={Chests,Contents};
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request(); R.GraveyardEligibleEntryIds.Add(Id(11));
	FEFCalystoContainerCapacity Capacity; Capacity.Slots=2; Capacity.CompatibleEntryIds.Add(Id(11)); R.ContainerCapacities.Add(Id(10),Capacity);
	for (int32 Index=1; Index<=2; ++Index)
	{
		auto CurrentRoom=Room(Index,EEFCalystoGameplayRole::Container); CurrentRoom.AllowedRoles.Add(EEFCalystoGameplayRole::ContainerContent); R.Rooms.Add(CurrentRoom);
		R.Surfaces.Add(Surface(200+Index,Index,FVector(Index*1000,0,0),{Id(10)},EEFCalystoGameplayRole::Container));
	}
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	if (!TestTrue(TEXT("Containers and finite contents reserve transactionally"),FPlanner::Build(C,R,M,Report))) {AddError(Report.Message);return false;}
	TestEqual(TEXT("Two world actors plus four inventory entries"),M.GetElements().Num(),6);
	TestEqual(TEXT("Inventory does not inflate world actor budget"),M.GetFinalUsage().Actors,2);
	TestEqual(TEXT("Only frozen actor/inventory payloads are dependencies"),M.GetSelectedDependencies().Num(),2);
	TMap<FGuid,int32> Counts; TSet<FGuid> Containers;
	for (const auto& E:M.GetElements()) if (E.Role==EEFCalystoGameplayRole::Container) Containers.Add(E.Id);
	for (const auto& E:M.GetElements()) if (E.Role==EEFCalystoGameplayRole::ContainerContent)
	{
		TestTrue(TEXT("Content belongs to an exact reserved container"),Containers.Contains(E.ParentContainerId));
		TestTrue(TEXT("Inventory slots remain inside validated capacity"),E.InventorySlot>=0 && E.InventorySlot<2);
		++Counts.FindOrAdd(E.ParentContainerId);
	}
	for (const auto& Count:Counts) TestEqual(TEXT("Amount trial is once per container"),Count.Value,2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentInputValidationTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.ExistingUsageAndInputBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentInputValidationTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A=Asset(); auto G=Group(); G.MaximumPerFloor=2; G.Entries.Add(Entry(10,2)); A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request(); R.Rooms.Add(Room(1));
	const FGuid Scope=FPlanner::ScopeIdentity(Id(1),FGuid(),G.Id);
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	for (int32 Case=0; Case<4; ++Case)
	{
		R.InitialUsage={};
		if (Case==0) R.InitialUsage.Categories.Add(EEFCalystoGameplayRole::Enemy,3);
		if (Case==1) R.InitialUsage.Entries.Add(Id(10),3);
		if (Case==2) R.InitialUsage.ScopedCategories.Add(Scope,3);
		if (Case==3) R.InitialUsage.ScopedEntries.Add(FPlanner::ScopedEntryIdentity(Scope,Id(10)),3);
		TestFalse(TEXT("Already exceeded authored usage rejects before any opportunity"),FPlanner::Build(C,R,M,Report));
		TestEqual(TEXT("Capacity error is explicit"),Report.FailureCode,FName(TEXT("ReservationUsageInvalid")));
		TestFalse(TEXT("An over-budget input cannot publish a manifest"),M.IsValid());
		TestTrue(TEXT("No partial objects escape validation"),M.GetElements().IsEmpty());
	}
	R.InitialUsage={}; R.InitialUsage.Resources=1;
	TestFalse(TEXT("Existing resource usage requires an explicit resource capacity"),FPlanner::Build(C,R,M,Report));
	R.InitialUsage={}; R.InitialUsage.Entries.Add(Id(999),1);
	TestFalse(TEXT("Unknown capacity identities cannot be accepted"),FPlanner::Build(C,R,M,Report));
	R.InitialUsage={}; R.Limits.MaximumCompatiblePairs=2;
	R.BlockedEntryIds={Id(10),Id(11),Id(12)};
	TestFalse(TEXT("Oversized caller-owned metadata is rejected before copying"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Metadata bound is distinct from infeasible content"),Report.FailureCode,FName(TEXT("ReservationMetadataBound")));
	R = Request();
	auto Inactive = A->Styles[0]; Inactive.Selection.Id = Id(999); Inactive.Selection.bEnabled = false;
	Inactive.FloorBudgets.Enemies = 26; A->Styles.Add(Inactive); R.Random.StyleId = Id(999);
	if (!Compile(*this, *A, C)) return false;
	TestFalse(TEXT("Direct request cannot consume an inactive unvalidated Style"), FPlanner::Build(C, R, M, Report));
	TestEqual(TEXT("Inactive Style rejection is explicit"), Report.FailureCode, FName(TEXT("ReservationStyleInactive")));
	A->Styles[1].Selection.bEnabled = true; A->Styles[1].Selection.Weight = 0;
	if (!Compile(*this, *A, C)) return false;
	TestFalse(TEXT("Direct request cannot consume a zero-weight Style"), FPlanner::Build(C, R, M, Report));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentTransformBoundsTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.TransformedPlacementEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentTransformBoundsTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A=Asset(); auto G=Group(); G.Entries.Add(Entry(10)); A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request(); R.Rooms.Add(Room(1)); R.Surfaces.Add(Surface(200,1,FVector::ZeroVector,{Id(10)}));
	R.Surfaces[0].Transform.SetScale3D(FVector(0.1)); R.Surfaces[0].AvailableHalfExtent=FVector(50);
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	TestTrue(TEXT("Settled incompatible scale remains an unselected opportunity"),FPlanner::Build(C,R,M,Report));
	TestTrue(TEXT("World clearance is never shrunk by payload scale"),M.GetElements().IsEmpty());
	TestFalse(TEXT("Out-of-envelope candidates are removed before Chance"),Report.Opportunities[0].bChanceRolled);
	R.Surfaces[0].Transform.SetScale3D(FVector(UE_SMALL_NUMBER*0.5));
	TestFalse(TEXT("Near-zero scale cannot collapse the inverse-transform proof"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Unsafe inverse scale is a configuration error"),Report.FailureCode,FName(TEXT("ReservationSurfaceInvalid")));
	R.Surfaces[0].Transform.SetScale3D(FVector(0.5,2,1));
	R.Surfaces[0].Transform.SetRotation(FQuat(FVector::UpVector,FMath::DegreesToRadians(45.0)));
	// AvailableHalfExtent is a proven local support envelope that includes the
	// deterministic jitter region.  The rotated nonuniform fixture needs enough
	// room for its exact +/-2 cm world-space placement variation.
	R.Surfaces[0].AvailableHalfExtent=FVector(220);
	for (int32 Seed=0; Seed<24; ++Seed)
	{
		R.Random.RunSeed=Seed;
		if (!TestTrue(TEXT("Validated rotated nonuniform scale supports deterministic variation"),FPlanner::Build(C,R,M,Report))) return false;
		if (!TestEqual(TEXT("A supported transformed candidate reserves exactly once"),M.GetElements().Num(),1)) return false;
		const auto& E=M.GetElements()[0]; const auto& S=R.Surfaces[0];
		for (int32 Corner=0; Corner<8; ++Corner)
		{
			const FVector World((Corner&1)?E.ReservedBounds.Max.X:E.ReservedBounds.Min.X,
				(Corner&2)?E.ReservedBounds.Max.Y:E.ReservedBounds.Min.Y,
				(Corner&4)?E.ReservedBounds.Max.Z:E.ReservedBounds.Min.Z);
			const FVector Local=S.Transform.InverseTransformPosition(World);
			TestTrue(TEXT("Every padded world corner fits the validated surface envelope"),
				FMath::Abs(Local.X)<=S.AvailableHalfExtent.X && FMath::Abs(Local.Y)<=S.AvailableHalfExtent.Y && FMath::Abs(Local.Z)<=S.AvailableHalfExtent.Z);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentExactJitterPreflightTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.ExactJitterPreflightBeforeChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentExactJitterPreflightTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A=Asset(); auto G=Group(); auto E=Entry(10); E.Placement.PositionVariationCm=37; G.Entries.Add(E); A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	auto R=Request(); R.Rooms.Add(Room(1)); R.Surfaces.Add(Surface(200,1,FVector(100,200,300),{Id(10)}));
	R.bRequireExactPlacementPreflight=true;
	int32 Calls=0; bool bObservedJitter=false;
	R.ExactPlacementPreflight=[&](const FEFCalystoContentEntry&,const FTransform& Transform,const FString& Hash,FString& Error)
	{
		++Calls; Error.Reset();
		bObservedJitter|=!Transform.GetLocation().Equals(R.Surfaces[0].Transform.GetLocation(),0.001);
		return Hash==R.Surfaces[0].CollisionContractHash
			? EEFCalystoExactPlacementPreflight::SpatiallyBlocked : EEFCalystoExactPlacementPreflight::Invalid;
	};
	FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	for (int32 Seed=0; Seed<16; ++Seed)
	{
		R.Random.RunSeed=Seed;
		if (!TestTrue(TEXT("A blocked exact jitter pose remains an ordinary pre-Chance infeasibility"),FPlanner::Build(C,R,M,Report)))
		{ AddError(Report.Message); return false; }
		if (!TestEqual(TEXT("One content opportunity is still reported"),Report.Opportunities.Num(),1)) return false;
		TestFalse(TEXT("A CDO-blocked jitter pose cannot roll Chance"),Report.Opportunities[0].bChanceRolled);
		TestTrue(TEXT("A CDO-blocked jitter pose cannot reserve an actor"),M.GetElements().IsEmpty());
	}
	TestTrue(TEXT("The predicate receives the planner's actual deterministic jittered transform"),Calls>0 && bObservedJitter);
	R.ExactPlacementPreflight=[](const FEFCalystoContentEntry&,const FTransform&,const FString&,FString& Error)
	{
		Error=TEXT("Fixture CDO contract became unavailable."); return EEFCalystoExactPlacementPreflight::Invalid;
	};
	TestFalse(TEXT("An unavailable exact CDO predicate rejects the whole manifest"),FPlanner::Build(C,R,M,Report));
	TestEqual(TEXT("Exact CDO predicate failure is diagnosed precisely"),Report.FailureCode,FName(TEXT("ExactPlacementPreflightInvalid")));
	TestFalse(TEXT("An invalid exact predicate exposes no manifest"),M.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoContentTraitBindingsTest,
	"NoShellForWinter.CalystoDungeon.Director.Content.TypedTraitBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoContentTraitBindingsTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	using FProbability = FEFCalystoDirectorProbability;
	double FEFCalystoTraits::* Members[] = {&FEFCalystoTraits::Mystery, &FEFCalystoTraits::Danger,
		&FEFCalystoTraits::Safe, &FEFCalystoTraits::Abundance, &FEFCalystoTraits::ClothingInfluence};
	auto* A = Asset(); auto G = Group(); G.Chance.FirstPercent = G.Chance.LastPercent = 50;
	G.Entries = {Entry(10), Entry(11)}; A->Styles[0].Content.Add(G);
	auto R = Request(); R.Rooms.Add(Room(1)); R.Rooms[0].ThemeId = Id(5);
	R.Surfaces.Add(Surface(200, 1, FVector::ZeroVector, {Id(10), Id(11)}));
	FEFCalystoCompiledDirector C; FEFCalystoReservedContentManifest M; FEFCalystoContentPlanningReport Report;
	auto& Policy = A->Advanced.Adaptation; Policy.bEnabled = true; Policy.MaximumEffectPercent = 100;
	for (auto Source : {EEFCalystoTraitSource::Style, EEFCalystoTraitSource::Theme, EEFCalystoTraitSource::Snapshot})
		for (int32 Trait = 0; Trait < UE_ARRAY_COUNT(Members); ++Trait)
		{
			FEFCalystoTraitBinding Chance; Chance.Source = Source; Chance.Trait = EEFCalystoTrait(Trait);
			Chance.Role = EEFCalystoGameplayRole::Enemy; Chance.EffectAtZeroPercent = -100; Chance.EffectAtOnePercent = 100;
			auto Weight = Chance; Weight.Control = EEFCalystoTraitControl::EntryWeight; Weight.EntryId = Id(10);
			Weight.EffectAtZeroPercent = 100; Weight.EffectAtOnePercent = -100; Policy.Bindings = {Chance, Weight};
			for (double Value : {0.0, 0.5, 1.0})
			{
				A->Styles[0].Traits = {}; A->RoomThemes[0].Traits = {}; R.TraitSnapshot = FEFCalystoTraits{};
				auto& Values = Source == EEFCalystoTraitSource::Style ? A->Styles[0].Traits
					: Source == EEFCalystoTraitSource::Theme ? A->RoomThemes[0].Traits : R.TraitSnapshot.GetValue();
				Values.*Members[Trait] = Value;
				if (!Compile(*this, *A, C) || !TestTrue(TEXT("Each named trait drives actual bounded planner behavior"), FPlanner::Build(C, R, M, Report)))
				{ AddError(Report.Message); return false; }
				if (!TestEqual(TEXT("One explicit opportunity"), Report.Opportunities.Num(), 1)) return false;
				const auto& Detail = Report.Opportunities[0];
				TestEqual(TEXT("Exact linear Chance endpoint/midpoint"), Detail.EffectiveChancePercent, Value * 100.0);
				const auto* Adjusted = Detail.EffectiveEntryWeights.FindByPredicate([&](const auto& E) { return E.Id == Id(10); });
				const auto* Unchanged = Detail.EffectiveEntryWeights.FindByPredicate([&](const auto& E) { return E.Id == Id(11); });
				if (!Adjusted || !Unchanged) return false;
				TestEqual(TEXT("Exact entry Weight endpoint/midpoint"), Adjusted->Weight, 2.0 * (1.0 - Value));
				TestEqual(TEXT("Unbound entry retains its authored weight"), Unchanged->Weight, 1.0);
				if (Value == 0) TestTrue(TEXT("Zero Chance produces no reservation"), M.GetElements().IsEmpty());
				if (Value == 1)
				{
					if (!TestEqual(TEXT("Certain Chance reserves exact Amount"), M.GetElements().Num(), 1)) return false;
					TestEqual(TEXT("Zero adjusted weight was excluded before feasibility/selection"), M.GetElements()[0].Entry.Selection.Id, Id(11));
					TestEqual(TEXT("Frozen payload retains authored selection data"), M.GetElements()[0].Entry.Selection.Weight, 1.0);
				}
			}
		}
	// Last explicit Snapshot binding uses ClothingInfluence. Missing context is an error, never an inferred zero.
	R.TraitSnapshot.Reset();
	TestFalse(TEXT("Missing referenced enabled snapshot rejects atomically"), FPlanner::Build(C, R, M, Report));
	TestEqual(TEXT("Missing input has an explicit failure code"), Report.FailureCode, FName(TEXT("TraitContextInvalid")));
	TestFalse(TEXT("Rejected context exposes no accepted manifest"), M.IsValid());
	R.TraitSnapshot = FEFCalystoTraits{}; R.TraitSnapshot.GetValue().ClothingInfluence = 1;
	R.Surfaces[0].CompatibleEntryIds = {Id(10)};
	TestTrue(TEXT("Zero-weight only site is resolved as ineligible before Chance"), FPlanner::Build(C, R, M, Report));
	TestFalse(TEXT("Feasibility never relies on an entry later removed by a modifier"), Report.Opportunities[0].bChanceRolled);
	R.Surfaces[0].CompatibleEntryIds.Add(Id(11));
	Policy.bEnabled = false;
	if (!Compile(*this, *A, C) || !FPlanner::Build(C, R, M, Report)) return false;
	const FString Baseline = M.GetHash(); const int64 BaselineWork = Report.WorkUnits;
	R.TraitSnapshot.GetValue().ClothingInfluence = std::numeric_limits<double>::quiet_NaN();
	A->Styles[0].Traits.Mystery = .71; A->RoomThemes[0].Traits.Danger = .91;
	Algo::Reverse(Policy.Bindings);
	if (!Compile(*this, *A, C) || !FPlanner::Build(C, R, M, Report)) return false;
	TestEqual(TEXT("Disabled bindings ignore context including nonfinite unused input exactly"), M.GetHash(), Baseline);
	TestEqual(TEXT("Disabled binding context costs no extra decision work"), Report.WorkUnits, BaselineWork);
	Policy.Bindings.Reset(); Policy.bEnabled = true;
	if (!Compile(*this, *A, C) || !FPlanner::Build(C, R, M, Report)) return false;
	TestEqual(TEXT("Enabled with no authored bindings has no hidden mapping"), M.GetHash(), Baseline);
	FEFCalystoTraitBinding First; First.Role = EEFCalystoGameplayRole::Enemy;
	First.EffectAtZeroPercent = 80; First.EffectAtOnePercent = 80;
	auto Second = First; Second.Source = EEFCalystoTraitSource::Theme; Second.EffectAtZeroPercent = -20; Second.EffectAtOnePercent = -20;
	Policy.Bindings = {First, Second}; Policy.MaximumEffectPercent = 20;
	double Multiplier, Reordered; FString Error;
	TestTrue(TEXT("Explicit mixed-source effects resolve"), FProbability::ResolveTraitMultiplier(Policy, {}, &A->RoomThemes[0].Traits,
		nullptr, EEFCalystoGameplayRole::Enemy, EEFCalystoTraitControl::Chance, {}, Multiplier, Error));
	TestEqual(TEXT("Sum is bounded once, not each contribution"), Multiplier, 1.2);
	Algo::Reverse(Policy.Bindings);
	FProbability::ResolveTraitMultiplier(Policy, {}, &A->RoomThemes[0].Traits, nullptr,
		EEFCalystoGameplayRole::Enemy, EEFCalystoTraitControl::Chance, {}, Reordered, Error);
	TestEqual(TEXT("Binding array order is mathematically irrelevant"), Reordered, Multiplier);
	FProbability::ResolveTraitMultiplier(Policy, {}, nullptr, nullptr,
		EEFCalystoGameplayRole::Food, EEFCalystoTraitControl::Chance, {}, Multiplier, Error);
	TestEqual(TEXT("A different typed role is unaffected"), Multiplier, 1.0);
	Policy.MaximumEffectPercent = 0;
	TestTrue(TEXT("Zero maximum requires no input snapshot"), FProbability::ResolveTraitMultiplier(Policy, {}, nullptr, nullptr,
		EEFCalystoGameplayRole::Enemy, EEFCalystoTraitControl::Chance, {}, Multiplier, Error));
	TestEqual(TEXT("Zero maximum has exactly no effect"), Multiplier, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTraitAuthoringTest,
	"NoShellForWinter.CalystoDungeon.Director.Authoring.TraitBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTraitAuthoringTest::RunTest(const FString&)
{
	using namespace EFCalystoContentReservationTests;
	auto* A = Asset(); auto G = Group(); G.Entries.Add(Entry(10)); A->Styles[0].Content.Add(G);
	FEFCalystoCompiledDirector C; TArray<FEFCalystoValidationIssue> Issues;
	const auto Rejects = [&](const FString& Field)
	{
		TestFalse(TEXT("Malformed trait authoring rejects compilation"), A->Compile(C, Issues));
		TestTrue(TEXT("Error names the exact authored field"), Issues.ContainsByPredicate([&](const auto& I) { return I.Field == Field; }));
	};
	double FEFCalystoTraits::* Members[] = {&FEFCalystoTraits::Mystery, &FEFCalystoTraits::Danger,
		&FEFCalystoTraits::Safe, &FEFCalystoTraits::Abundance, &FEFCalystoTraits::ClothingInfluence};
	const TCHAR* Names[] = {TEXT("Mystery"), TEXT("Danger"), TEXT("Safe"), TEXT("Abundance"), TEXT("ClothingInfluence")};
	for (int32 Profile = 0; Profile < 2; ++Profile) for (int32 Trait = 0; Trait < UE_ARRAY_COUNT(Members); ++Trait)
	{
		auto& Values = Profile == 0 ? A->Styles[0].Traits : A->RoomThemes[0].Traits;
		for (double Bad : {-0.01, 1.01, std::numeric_limits<double>::infinity()})
		{
			Values.*Members[Trait] = Bad;
			Rejects(FString(Profile == 0 ? TEXT("Styles[0].Traits.") : TEXT("RoomThemes[0].Traits.")) + Names[Trait]);
		}
		Values.*Members[Trait] = 1.0;
		if (!Compile(*this, *A, C)) return false;
		TestEqual(TEXT("Every normalized trait freezes exactly"),
			(Profile == 0 ? C.FindStyle(Id(1))->Traits : C.FindTheme(Id(5))->Traits).*Members[Trait], 1.0);
		Values.*Members[Trait] = 0.0;
	}
	FEFCalystoTraitBinding B; B.Role = EEFCalystoGameplayRole::Enemy;
	A->Advanced.Adaptation.Bindings = {B};
	const auto BadBinding = [&](FEFCalystoTraitBinding Bad, const TCHAR* Field)
	{ A->Advanced.Adaptation.Bindings = {Bad}; Rejects(FString(TEXT("Advanced.Adaptation.Bindings[0].")) + Field); };
	auto Bad = B; Bad.Source = EEFCalystoTraitSource(255); BadBinding(Bad, TEXT("Source"));
	Bad = B; Bad.Trait = EEFCalystoTrait(255); BadBinding(Bad, TEXT("Trait"));
	Bad = B; Bad.Role = EEFCalystoGameplayRole(255); BadBinding(Bad, TEXT("Role"));
	Bad = B; Bad.Control = EEFCalystoTraitControl(255); BadBinding(Bad, TEXT("Control"));
	Bad = B; Bad.EntryId = Id(10); BadBinding(Bad, TEXT("EntryId"));
	Bad = B; Bad.Control = EEFCalystoTraitControl::EntryWeight; BadBinding(Bad, TEXT("EntryId"));
	Bad.EntryId = Id(999); BadBinding(Bad, TEXT("EntryId"));
	Bad = B; Bad.EffectAtZeroPercent = -101; BadBinding(Bad, TEXT("EffectAtZeroPercent"));
	Bad = B; Bad.EffectAtOnePercent = std::numeric_limits<double>::quiet_NaN(); BadBinding(Bad, TEXT("EffectAtOnePercent"));
	A->Advanced.Adaptation.Bindings = {B, B}; Rejects(TEXT("Advanced.Adaptation.Bindings[1]"));
	A->Advanced.Adaptation.Bindings.Init(B, 65); Rejects(TEXT("Advanced.Adaptation.Bindings"));
	A->Advanced.Adaptation.Bindings = {B}; B.Control = EEFCalystoTraitControl::EntryWeight; B.EntryId = Id(10);
	A->Advanced.Adaptation.Bindings.Add(B);
	if (!Compile(*this, *A, C)) return false;
	A->Advanced.Adaptation.Bindings[0].EffectAtOnePercent = 20;
	TestEqual(TEXT("Compiled bindings are immutable copies"), C.GetAdvanced().Adaptation.Bindings[0].EffectAtOnePercent, 0.0);
	return true;
}

#endif
