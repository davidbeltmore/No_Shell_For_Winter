#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoDecalReservation.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace EFCalystoDecalReservationTests
{
	using FPlanner = FEFCalystoDecalReservationPlanner;

	constexpr TCHAR FloorMaterial[] = TEXT("/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor.MI_CalystoBloodDecal_Floor");
	constexpr TCHAR ColorTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04");
	constexpr TCHAR NormalTexture[] = TEXT("/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04");

	FGuid Id(uint32 Value) { return FGuid(0x44454341, 0x4C525356, 0, Value); }

	FEFCalystoDecalVariant Variant(uint32 Value, double Weight)
	{
		FEFCalystoDecalVariant Result;
		Result.Selection.Id = Id(Value); Result.Selection.DisplayName = FString::Printf(TEXT("Stable decal %u"), Value);
		Result.Selection.bEnabled = true; Result.Selection.Weight = Weight;
		Result.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FloorMaterial));
		Result.ColorTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(ColorTexture));
		Result.NormalTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(NormalTexture));
		Result.bFloor = true; Result.bWall = Result.bRoof = false;
		return Result;
	}

	FEFCalystoDecals Decals(uint32 FirstVariantId, double ChancePercent, int32 ActiveFloorBudget = 2, int32 FloorLimit = 2)
	{
		FEFCalystoDecals Result;
		Result.Mode = EEFCalystoDecalMode::Replace; Result.ChancePercent = ChancePercent;
		Result.MaximumPerRoom = 1; Result.ActiveFloorBudget = ActiveFloorBudget; Result.FloorLimit = FloorLimit;
		Result.WallLimit = Result.RoofLimit = 0;
		Result.SizeCm.Distribution = EEFCalystoDistribution::Fixed; Result.SizeCm.Value = 64.0;
		Result.Variants = {Variant(FirstVariantId, 1.0), Variant(FirstVariantId + 1, 3.0)};
		return Result;
	}

	UEFCalystoDungeonDirectorAsset* Asset()
	{
		auto* Result = NewObject<UEFCalystoDungeonDirectorAsset>();
		FEFCalystoStyle& Style = Result->Styles.AddDefaulted_GetRef();
		Style.Selection.Id = Id(1); Style.Selection.DisplayName = TEXT("General reservation style");
		Style.Layout.DungeonSize.Value = 24; Style.Layout.CandidateDensity.Value = 0.32; Style.Layout.SidePathPercent.Value = 50;
		const TSoftObjectPtr<UMaterialInterface> Material(FSoftObjectPath(TEXT("/Game/Test/DecalReservationMaterial.DecalReservationMaterial")));
		Style.Materials.Floor = Style.Materials.Wall = Style.Materials.Roof = Material;
		FEFCalystoArchitectureEntry Mesh;
		Mesh.Selection.Id = Id(2); Mesh.Selection.DisplayName = TEXT("Required floor");
		Mesh.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Test/DecalReservationMesh.DecalReservationMesh")));
		Style.Architecture.Floor.Add(Mesh); Mesh.Selection.Id = Id(3); Style.Architecture.Wall.Add(Mesh);
		Mesh.Selection.Id = Id(4); Style.Architecture.Roof.Add(Mesh);
		Style.Decals = Decals(10, 10.0);

		FEFCalystoTheme& Forge = Result->RoomThemes.AddDefaulted_GetRef();
		Forge.Selection.Id = Id(5); Forge.Selection.DisplayName = TEXT("Forge"); Forge.Decals = Decals(20, 25.0);
		FEFCalystoTheme& Shrine = Result->RoomThemes.AddDefaulted_GetRef();
		Shrine.Selection.Id = Id(6); Shrine.Selection.DisplayName = TEXT("Shrine"); Shrine.Decals.Mode = EEFCalystoDecalMode::Block;
		return Result;
	}

	FEFCalystoRandomKey Random(int64 Seed = 1779679224)
	{
		FEFCalystoRandomKey Result;
		Result.RunSeed = Seed; Result.FloorNumber = 5; Result.StyleId = Id(1);
		return Result;
	}

	FEFCalystoDecalOpportunity Opportunity(uint32 Value, int64 RoomId, const FGuid& ThemeId, UPrimitiveComponent* Support)
	{
		FEFCalystoDecalOpportunity Result;
		Result.Id = Id(Value); Result.RoomId = RoomId; Result.NativeOpportunityId = Value; Result.ThemeId = ThemeId;
		Result.Surface = EEFCalystoDecalSurface::Floor; Result.SurfaceNormal = FVector::ForwardVector;
		Result.MaximumSizeCm = 128.0; Result.Support = Support;
		FEFCalystoDecalCoverageSupport& Coverage = Result.CoverageSupports.AddDefaulted_GetRef();
		Coverage.Support = Support; Coverage.Normal = FVector::ForwardVector;
		return Result;
	}

	bool Compile(FAutomationTestBase& Test, const UEFCalystoDungeonDirectorAsset& Asset, FEFCalystoCompiledDirector& OutConfiguration)
	{
		TArray<FEFCalystoValidationIssue> Issues;
		if (Asset.Compile(OutConfiguration, Issues)) return true;
		for (const FEFCalystoValidationIssue& Issue : Issues) Test.AddError(Issue.Field + TEXT(": ") + Issue.Message);
		return false;
	}

	int32 Count(const FEFCalystoDecalPlanningReport& Report, EEFCalystoDecalOpportunityOutcome Outcome)
	{
		int32 Result = 0;
		for (const FEFCalystoDecalOpportunityReport& Opportunity : Report.Opportunities) Result += Opportunity.Outcome == Outcome;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalReservationProbabilityTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.ReservationProbability100000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalReservationProbabilityTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalReservationTests;
	auto* A = Asset(); FEFCalystoCompiledDirector Configuration;
	if (!Compile(*this, *A, Configuration)) return false;
	UStaticMeshComponent* Support = NewObject<UStaticMeshComponent>(GetTransientPackage());
	TArray<FEFCalystoDecalOpportunity> Opportunities;
	Opportunities.Add(Opportunity(100, 101, FGuid(), Support));
	Opportunities.Add(Opportunity(101, 102, Id(5), Support));
	constexpr int32 Trials = 100000;
	int32 GeneralSelected = 0, ForgeSelected = 0;
	const double Started = FPlatformTime::Seconds();
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		FEFCalystoReservedDecalManifest Manifest; FEFCalystoDecalPlanningReport Report;
		if (!FPlanner::Build(Configuration, Random(Trial + 71923), Opportunities, Manifest, Report))
		{ AddError(Report.Message); return false; }
		if (!Manifest.IsValid() || Report.FeasibleRooms != 2 || Report.CapacityReservedRooms != 2 || Report.Opportunities.Num() != 2)
		{ AddError(TEXT("A fixed two-room decal trial lost bounded feasibility or capacity before Chance.")); return false; }
		for (const FEFCalystoDecalOpportunityReport& Result : Report.Opportunities)
		{
			if (Result.RoomId == 101) GeneralSelected += Result.Outcome == EEFCalystoDecalOpportunityOutcome::Reserved;
			else if (Result.RoomId == 102) ForgeSelected += Result.Outcome == EEFCalystoDecalOpportunityOutcome::Reserved;
			else { AddError(TEXT("Planner reported an unknown probability fixture room.")); return false; }
		}
	}
	const auto WithinSixSigma = [=](int32 Count, double Probability)
	{
		const double Expected = Trials * Probability;
		return FMath::Abs(Count - Expected) <= 6.0 * FMath::Sqrt(Trials * Probability * (1.0 - Probability));
	};
	TestTrue(TEXT("100000 planner-only trials retain the General 10% Chance law"), WithinSixSigma(GeneralSelected, 0.10));
	TestTrue(TEXT("100000 planner-only trials retain the Forge 25% Chance law"), WithinSixSigma(ForgeSelected, 0.25));
	AddInfo(FString::Printf(TEXT("Planner-only probability: General %d / %d, Forge %d / %d, %.3f seconds; no world generation."),
		GeneralSelected, Trials, ForgeSelected, Trials, FPlatformTime::Seconds() - Started));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalReservationSemanticsTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.ShrineBlockCapacityAndNoBackfill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalReservationSemanticsTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalReservationTests;
	auto* A = Asset(); FEFCalystoCompiledDirector Configuration;
	if (!Compile(*this, *A, Configuration)) return false;
	UStaticMeshComponent* Support = NewObject<UStaticMeshComponent>(GetTransientPackage());
	TArray<FEFCalystoDecalOpportunity> ShrineOnly;
	ShrineOnly.Add(Opportunity(110, 201, Id(6), Support));
	FEFCalystoReservedDecalManifest Manifest; FEFCalystoDecalPlanningReport Report;
	if (!TestTrue(TEXT("Shrine Block is a successful no-decal plan"), FPlanner::Build(Configuration, Random(), ShrineOnly, Manifest, Report)))
	{ AddError(Report.Message); return false; }
	if (!TestEqual(TEXT("Shrine Block reports its one source opportunity"), Report.Opportunities.Num(), 1)) return false;
	TestTrue(TEXT("Shrine Block returns a valid empty manifest"), Manifest.IsValid() && Manifest.GetElements().IsEmpty());
	TestEqual(TEXT("Shrine Block has no feasible decal rooms"), Report.FeasibleRooms, 0);
	TestEqual(TEXT("Shrine Block reserves no capacity"), Report.CapacityReservedRooms, 0);
	TestEqual(TEXT("Shrine Block remains explicitly ineligible"), Report.Opportunities[0].Outcome, EEFCalystoDecalOpportunityOutcome::Ineligible);
	TestFalse(TEXT("Shrine Block never rolls Chance"), Report.Opportunities[0].bChanceRolled);

	A->Styles[0].Decals.ChancePercent = 0.0;
	A->Styles[0].Decals.ActiveFloorBudget = 1; A->Styles[0].Decals.FloorLimit = 1;
	if (!Compile(*this, *A, Configuration)) return false;
	TArray<FEFCalystoDecalOpportunity> CapacityCandidates;
	CapacityCandidates.Add(Opportunity(120, 301, FGuid(), Support));
	CapacityCandidates.Add(Opportunity(121, 302, FGuid(), Support));
	if (!TestTrue(TEXT("Zero Chance capacity reservation succeeds"), FPlanner::Build(Configuration, Random(99), CapacityCandidates, Manifest, Report)))
	{ AddError(Report.Message); return false; }
	TestTrue(TEXT("A Chance miss does not backfill another room"), Manifest.IsValid() && Manifest.GetElements().IsEmpty());
	TestEqual(TEXT("Both source rooms remain feasible before capacity"), Report.FeasibleRooms, 2);
	TestEqual(TEXT("Shared active and floor capacities reserve exactly one room"), Report.CapacityReservedRooms, 1);
	TestEqual(TEXT("Only the capacity-reserved room rolls its explicit zero Chance"), Count(Report, EEFCalystoDecalOpportunityOutcome::ChanceAbsent), 1);
	TestEqual(TEXT("The other feasible room stays capacity-excluded instead of being backfilled"), Count(Report, EEFCalystoDecalOpportunityOutcome::CapacityExcluded), 1);
	for (const FEFCalystoDecalOpportunityReport& Result : Report.Opportunities)
	{
		if (Result.Outcome == EEFCalystoDecalOpportunityOutcome::ChanceAbsent)
			TestTrue(TEXT("The reserved Chance-miss room recorded its draw"), Result.bChanceRolled);
		if (Result.Outcome == EEFCalystoDecalOpportunityOutcome::CapacityExcluded)
			TestFalse(TEXT("Capacity-excluded rooms have no hidden Chance draw"), Result.bChanceRolled);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalReservationIdentityTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.ReorderAndDisplayLabelsPreserveIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalReservationIdentityTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalReservationTests;
	auto* A = Asset();
	A->Styles[0].Decals.ChancePercent = 100.0; A->RoomThemes[0].Decals.ChancePercent = 100.0;
	FEFCalystoCompiledDirector Configuration;
	if (!Compile(*this, *A, Configuration)) return false;
	UStaticMeshComponent* Support = NewObject<UStaticMeshComponent>(GetTransientPackage());
	TArray<FEFCalystoDecalOpportunity> Opportunities;
	Opportunities.Add(Opportunity(130, 401, FGuid(), Support));
	Opportunities.Add(Opportunity(131, 402, Id(5), Support));
	FEFCalystoReservedDecalManifest Before; FEFCalystoDecalPlanningReport Report;
	if (!TestTrue(TEXT("Initial canonical reservation succeeds"), FPlanner::Build(Configuration, Random(551), Opportunities, Before, Report)))
	{ AddError(Report.Message); return false; }
	const FString BeforeHash = Before.GetHash();
	const TArray<FEFCalystoReservedDecal> BeforeElements = Before.GetElements();

	A->Styles[0].Selection.DisplayName = TEXT("Renamed General Style");
	A->Styles[0].Decals.Variants[0].Selection.DisplayName = TEXT("Renamed General One");
	A->Styles[0].Decals.Variants[1].Selection.DisplayName = TEXT("Renamed General Two");
	A->Styles[0].Decals.Variants.Swap(0, 1);
	A->RoomThemes[0].Selection.DisplayName = TEXT("Renamed Forge");
	A->RoomThemes[0].Decals.Variants[0].Selection.DisplayName = TEXT("Renamed Forge One");
	A->RoomThemes[0].Decals.Variants[1].Selection.DisplayName = TEXT("Renamed Forge Two");
	A->RoomThemes[1].Selection.DisplayName = TEXT("Renamed Shrine");
	A->RoomThemes[0].Decals.Variants.Swap(0, 1);
	A->RoomThemes.Swap(0, 1); Opportunities.Swap(0, 1);
	if (!Compile(*this, *A, Configuration)) return false;
	FEFCalystoReservedDecalManifest After;
	if (!TestTrue(TEXT("Reordered and relabelled reservation succeeds"), FPlanner::Build(Configuration, Random(551), Opportunities, After, Report)))
	{ AddError(Report.Message); return false; }
	if (!TestEqual(TEXT("Display labels and array order preserve the manifest hash"), After.GetHash(), BeforeHash)) return false;
	if (!TestEqual(TEXT("Display labels and array order preserve selected multiplicity"), After.GetElements().Num(), BeforeElements.Num())) return false;
	for (int32 Index = 0; Index < BeforeElements.Num(); ++Index)
	{
		const FEFCalystoReservedDecal& Expected = BeforeElements[Index];
		const FEFCalystoReservedDecal& Actual = After.GetElements()[Index];
		TestEqual(TEXT("Canonical selected decal identity is stable"), Actual.Id, Expected.Id);
		TestEqual(TEXT("Canonical selected room identity is stable"), Actual.RoomId, Expected.RoomId);
		TestEqual(TEXT("Canonical native opportunity identity is stable"), Actual.OpportunityId, Expected.OpportunityId);
		TestEqual(TEXT("Canonical Theme identity is stable"), Actual.ThemeId, Expected.ThemeId);
		TestEqual(TEXT("Canonical Variant identity is stable"), Actual.VariantId, Expected.VariantId);
		TestTrue(TEXT("Canonical decal transform is stable"), Actual.WorldTransform.Equals(Expected.WorldTransform));
		TestTrue(TEXT("Canonical decal size is stable"), Actual.Size.Equals(Expected.Size));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDecalReservationClosureTest,
	"NoShellForWinter.CalystoDungeon.Director.Decals.ConfigurationClosureRejectsUnexpectedPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoDecalReservationClosureTest::RunTest(const FString&)
{
	using namespace EFCalystoDecalReservationTests;
	auto* A = Asset();
	A->Styles[0].Decals.Variants[0].ColorTexture = TSoftObjectPtr<UTexture2D>(
		FSoftObjectPath(TEXT("/Game/Test/UnexpectedDecalColor.UnexpectedDecalColor")));
	FEFCalystoCompiledDirector Configuration;
	if (!Compile(*this, *A, Configuration)) return false;
	FString Error;
	TestFalse(TEXT("The planner rejects a decal texture outside its dependency closure"),
		FPlanner::ValidateConfiguration(Configuration, Id(1), Error));
	TestTrue(TEXT("Closure rejection names the retained RealisticBlood resource contract"), Error.Contains(TEXT("RealisticBlood")));
	TArray<FEFCalystoDecalOpportunity> NoOpportunities;
	FEFCalystoReservedDecalManifest Manifest; FEFCalystoDecalPlanningReport Report;
	TestFalse(TEXT("Configuration closure rejection prevents a manifest before planning"),
		FPlanner::Build(Configuration, Random(), NoOpportunities, Manifest, Report));
	TestEqual(TEXT("Closure failures have the explicit planner failure code"), Report.FailureCode, FName(TEXT("DecalConfigurationInvalid")));
	TestFalse(TEXT("Closure failure returns no valid partial manifest"), Manifest.IsValid());
	TestTrue(TEXT("Closure failure returns no selected dependencies"), Manifest.GetSelectedDependencies().IsEmpty());
	return true;
}

#endif
