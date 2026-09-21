#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoArchitecturePlanner.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace EFCalystoArchitectureTests
{
	FGuid Id(uint32 Value) { return FGuid(0x41524348,0x54455354,0,Value); }
	UEFCalystoDungeonDirectorAsset* Asset()
	{
		auto* A=NewObject<UEFCalystoDungeonDirectorAsset>();
		auto& S=A->Styles.AddDefaulted_GetRef(); S.Selection.Id=Id(1);
		S.Layout.DungeonSize.Value=24; S.Layout.CandidateDensity.Value=.32; S.Layout.SidePathPercent.Value=50;
		S.Materials.Floor=S.Materials.Wall=S.Materials.Roof=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Test/ArchitectureMaterial.ArchitectureMaterial")));
		FEFCalystoArchitectureEntry E; E.Selection.Id=Id(2);
		E.Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Test/ArchitectureMesh.ArchitectureMesh")));
		S.Architecture.Floor.Add(E); E.Selection.Id=Id(3); S.Architecture.Wall.Add(E);
		E.Selection.Id=Id(4); S.Architecture.Roof.Add(E); S.Decals.Mode=EEFCalystoDecalMode::Block;
		S.Architecture.DecorationChance.FirstPercent=S.Architecture.DecorationChance.LastPercent=100;
		for (uint32 Zone=0;Zone<8;++Zone)
		{
			auto& Rule=S.Architecture.Decoration.AddDefaulted_GetRef(); Rule.Zone=EEFCalystoPlacementZone(Zone);
			E.Selection.Id=Id(20+Zone); Rule.Alternatives.Add(E);
		}
		auto& T=A->RoomThemes.AddDefaulted_GetRef(); T.Selection.Id=Id(5); T.Decals.Mode=EEFCalystoDecalMode::Block;
		return A;
	}
	FEFCalystoArchitectureRequest Request()
	{
		FEFCalystoArchitectureRequest R; R.Random.StyleId=Id(1); R.Random.RunSeed=42;
		auto& Room=R.Rooms.AddDefaulted_GetRef(); Room.RoomId=1;
		for (uint32 Zone=0;Zone<8;++Zone)
		{
			auto& O=R.Opportunities.AddDefaulted_GetRef(); O.Id=Id(100+Zone); O.RoomId=1; O.Zone=EEFCalystoPlacementZone(Zone);
			const FVector Position(1000*Zone,0,0); O.NativeTransform.SetLocation(Position);
			O.CompatibleEntryBounds.Add(Id(20+Zone),FBox(Position-FVector(50),Position+FVector(50)));
		}
		return R;
	}
	bool Compile(FAutomationTestBase& Test, const UEFCalystoDungeonDirectorAsset& A, FEFCalystoCompiledDirector& C)
	{
		TArray<FEFCalystoValidationIssue> Issues;
		if (A.Compile(C,Issues)) return true;
		for (const auto& I:Issues) Test.AddError(I.Field+TEXT(": ")+I.Message);
		return false;
	}
	int32 Count(const TArray<FEFCalystoArchitectureDecision>& D, EEFCalystoArchitectureOutcome Outcome)
	{ int32 N=0; for (const auto& V:D) N+=V.Outcome==Outcome; return N; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoArchitectureCapacityTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.FeasibilityAndSharedCapacity",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoArchitectureCapacityTest::RunTest(const FString&)
{
	using namespace EFCalystoArchitectureTests;
	auto* A=Asset(); auto R=Request(); FEFCalystoCompiledDirector C; FString Error;
	TArray<FEFCalystoArchitectureDecision> D;
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Plan all native zones"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestEqual(TEXT("Four across all eight zones, not four per zone"),Count(D,EEFCalystoArchitectureOutcome::Reserved),4);
	TestEqual(TEXT("Other opportunities see exhausted capacity before Chance"),Count(D,EEFCalystoArchitectureOutcome::CapacityExhausted),4);
	for (const auto& V:D) if (V.Outcome==EEFCalystoArchitectureOutcome::CapacityExhausted) TestFalse(TEXT("Capacity has no hidden Chance roll"),V.bChanceRolled);
	A->Styles[0].Architecture.MaximumDecorationsPerRoom=8;
	A->RoomThemes[0].Architecture.Add(A->Styles[0].Architecture.Decoration[0]);
	auto& ThemeRule=A->RoomThemes[0].Architecture[0]; ThemeRule.bOverrideDefaultChance=true;
	ThemeRule.Chance.FirstPercent=ThemeRule.Chance.LastPercent=0;
	R.Rooms[0].ThemeId=Id(5);
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Theme exact zone chance override"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestEqual(TEXT("Theme suppresses only its local Floor zone"),Count(D,EEFCalystoArchitectureOutcome::Reserved),7);
	TestEqual(TEXT("Zero Chance is explicit absence"),Count(D,EEFCalystoArchitectureOutcome::ChanceAbsent),1);
	R.Opportunities[1].CompatibleEntryBounds.Reset(); R.CoolingDownIds.Add(Id(22));
	R.ExistingReservedSpace.Add(R.Opportunities[3].CompatibleEntryBounds.FindChecked(Id(23)));
	TestTrue(TEXT("Finite compatibility, cooldown and spacing"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestEqual(TEXT("Three infeasible alternatives precede Chance"),Count(D,EEFCalystoArchitectureOutcome::NoCompatibleAlternative),3);
	for (const auto& V:D) if (V.Outcome==EEFCalystoArchitectureOutcome::NoCompatibleAlternative) TestFalse(TEXT("No compatible means no roll"),V.bChanceRolled);
	R.Rooms[0].Protection=EEFCalystoProtectedRoom::Start;
	TestTrue(TEXT("Protected room accepted as ineligible"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestEqual(TEXT("Every protected opportunity excluded"),Count(D,EEFCalystoArchitectureOutcome::Ineligible),8);
	const FEFCalystoArchitectureOpportunity Duplicate=R.Opportunities[0];
	R.Opportunities.Add(Duplicate);
	TestFalse(TEXT("Duplicate native identity is rejected atomically"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestTrue(TEXT("No partial decisions on failure"),D.IsEmpty());
	A->Styles[0].Architecture.MaximumDecorationsPerRoom=-1;
	TArray<FEFCalystoValidationIssue> Issues;
	TestFalse(TEXT("Negative room capacity rejects authoring"),A->Compile(C,Issues));
	TestTrue(TEXT("Exact field identifies invalid control"),Issues.ContainsByPredicate([](const auto& I){return I.Field==TEXT("Styles[0].Architecture.MaximumDecorationsPerRoom");}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoArchitectureProbabilityTest,
	"NoShellForWinter.CalystoDungeon.Director.Architecture.ProbabilityIdentityAndVariation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEFCalystoArchitectureProbabilityTest::RunTest(const FString&)
{
	using namespace EFCalystoArchitectureTests;
	auto* A=Asset(); auto R=Request(); R.Opportunities.SetNum(1);
	auto& S=A->Styles[0]; S.Architecture.DecorationChance.FirstPercent=S.Architecture.DecorationChance.LastPercent=35;
	auto& Rule=S.Architecture.Decoration[0]; Rule.Alternatives[0].Selection.Weight=3;
	FEFCalystoArchitectureEntry Empty; Empty.Payload=EEFCalystoArchitecturePayload::Empty;
	Empty.Selection.Id=Id(200); Rule.Alternatives.Add(Empty);
	FEFCalystoCompiledDirector C; if (!Compile(*this,*A,C)) return false;
	TArray<FEFCalystoArchitectureDecision> D; FString Error; int32 Present=0,EmptyCount=0;
	constexpr int32 Trials=100000;
	for (int32 I=0;I<Trials;++I)
	{
		R.Random.RunSeed=I;
		if (!FEFCalystoArchitecturePlanner::Build(C,R,D,Error)) { AddError(Error); return false; }
		Present+=D[0].Outcome==EEFCalystoArchitectureOutcome::Reserved;
		EmptyCount+=D[0].Outcome==EEFCalystoArchitectureOutcome::ExplicitEmpty;
	}
	const auto Within=[&](int32 N,double P) { return FMath::Abs(N-Trials*P)<=6*FMath::Sqrt(Trials*P*(1-P)); };
	TestTrue(TEXT("100000 architecture Chance trials match explicit 35%"),Within(Present+EmptyCount,.35));
	TestTrue(TEXT("Authored Empty has 1/4 conditional weight, no implicit Nothing"),Within(EmptyCount,.35*.25));
	TestTrue(TEXT("Real mesh has 3/4 conditional weight"),Within(Present,.35*.75));
	S.Architecture.DecorationChance.FirstPercent=S.Architecture.DecorationChance.LastPercent=100;
	Rule.Alternatives.SetNum(1); auto& E=Rule.Alternatives[0];
	E.Transform.LocationOffset=FVector(1,2,3); E.Transform.bUniformScale=false; E.Transform.Scale=FVector(2,3,4);
	E.Transform.RotationOffset=FRotator(0,10,0); E.Variation.LocationMinimum=E.Variation.LocationMaximum=FVector(4,5,6);
	E.Variation.RotationMinimum=E.Variation.RotationMaximum=FRotator(0,20,0);
	E.Variation.bUniformScale=false; E.Variation.ScaleMinimum=E.Variation.ScaleMaximum=FVector(2,3,4);
	S.Architecture.MaximumDecorationsPerRoom=8; R=Request();
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Fixed variation build"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	const auto* First=D.FindByPredicate([](const auto& V){return V.Zone==EEFCalystoPlacementZone::Floor;});
	if (!First) return false;
	TestTrue(TEXT("Local offset plus native transform"),First->WorldTransform.GetLocation().Equals(FVector(5,7,9)));
	TestTrue(TEXT("Independent fixed and variation scales"),First->WorldTransform.GetScale3D().Equals(FVector(4,9,16)));
	TestTrue(TEXT("Rotation offset and variation compose"),First->WorldTransform.Rotator().Equals(FRotator(0,30,0)));
	const auto Before=D;
	R.Opportunities.Swap(0,7); S.Selection.DisplayName=TEXT("Renamed");
	S.Architecture.Decoration.Swap(0,7);
	if (!Compile(*this,*A,C)) return false;
	TestTrue(TEXT("Reordered build"),FEFCalystoArchitecturePlanner::Build(C,R,D,Error));
	TestEqual(TEXT("Same decision inventory"),D.Num(),Before.Num());
	for (int32 I=0;I<D.Num();++I)
	{
		TestEqual(TEXT("Canonical opportunity ordering"),D[I].OpportunityId,Before[I].OpportunityId);
		TestEqual(TEXT("Names and arrays preserve identity"),D[I].Entry.Selection.Id,Before[I].Entry.Selection.Id);
		TestTrue(TEXT("Names and arrays preserve transforms"),D[I].WorldTransform.Equals(Before[I].WorldTransform));
	}
	return true;
}
#endif
