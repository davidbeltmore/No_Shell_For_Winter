#include "Calysto/EFCalystoDungeonDirectorAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace EFCalystoDirectorFoundationTests
{
using FProbability = FEFCalystoDirectorProbability;

bool CountWithinSigma(const int64 Actual, const int64 Trials, const double Probability)
{
	const double Expected = Trials * Probability;
	const double Tolerance = FMath::CeilToDouble(6.0 * FMath::Sqrt(Trials * Probability * (1.0 - Probability)));
	return FMath::Abs(static_cast<double>(Actual) - Expected) <= Tolerance;
}

UEFCalystoDungeonDirectorAsset* MakeAsset()
{
	UEFCalystoDungeonDirectorAsset* Asset = NewObject<UEFCalystoDungeonDirectorAsset>();
	FEFCalystoStyle& Style = Asset->Styles.AddDefaulted_GetRef();
	Style.Selection.Id = FGuid(1, 0, 0, 1);
	Style.Selection.DisplayName = TEXT("General");
	const FSoftObjectPath Grey(TEXT("/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles"));
	Style.Materials.Floor = TSoftObjectPtr<UMaterialInterface>(Grey);
	Style.Materials.Wall = TSoftObjectPtr<UMaterialInterface>(Grey);
	Style.Materials.Roof = TSoftObjectPtr<UMaterialInterface>(Grey);
	FEFCalystoArchitectureEntry Entry;
	Entry.Selection.Id = FGuid(2, 0, 0, 1);
	Entry.Selection.DisplayName = TEXT("Floor");
	Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Floor.SM_Floor")));
	Style.Architecture.Floor.Add(Entry);
	Entry.Selection.Id.D = 2;
	Entry.Selection.DisplayName = TEXT("Wall");
	Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Wall.SM_Wall")));
	Style.Architecture.Wall.Add(Entry);
	Entry.Selection.Id.D = 3;
	Entry.Selection.DisplayName = TEXT("Roof");
	Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_Roof.SM_Roof")));
	Style.Architecture.Roof.Add(Entry);
	FEFCalystoTheme& Theme = Asset->RoomThemes.AddDefaulted_GetRef();
	Theme.Selection.Id = FGuid(3, 0, 0, 1);
	Theme.Selection.DisplayName = TEXT("Forge");
	Theme.Materials.Floor.Mode = EEFCalystoMaterialMode::Override;
	Theme.Materials.Floor.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange.MI_Template_BaseOrange")));
	return Asset;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorProbabilityLaws,
	"NoShellForWinter.CalystoDungeon.Director.Probability.100000Trials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorProbabilityLaws::RunTest(const FString& Parameters)
{
	using namespace EFCalystoDirectorFoundationTests;
	constexpr int32 Trials = 100000;
	const FGuid Forge(1, 0, 0, 1), Shrine(1, 0, 0, 2);
	TArray<FEFCalystoThemeOpportunity> Rooms;
	for (int64 RoomId = 1; RoomId <= 5; ++RoomId)
	{
		FEFCalystoThemeOpportunity& Room = Rooms.AddDefaulted_GetRef();
		Room.RoomId = RoomId;
		Room.EligibleThemes = { {Forge, 5.0}, {Shrine, 3.0} };
	}
	FEFCalystoThemeOpportunity& Start = Rooms.AddDefaulted_GetRef();
	Start.RoomId = 6;
	Start.Protected = EEFCalystoProtectedRoom::Start;
	Start.EligibleThemes = {{Forge, 1.0}};
	FEFCalystoAmountDistribution UniformAmount;
	UniformAmount.Distribution = EEFCalystoDistribution::Uniform;
	UniformAmount.Minimum = 1; UniformAmount.Maximum = 3;
	FEFCalystoAmountDistribution TriangleAmount = UniformAmount;
	TriangleAmount.Distribution = EEFCalystoDistribution::Triangular;
	TriangleAmount.Mode = 2;
	const TArray<int32> AllCounts = {1, 2, 3}, FeasibleCounts = {1, 3};
	const TArray<EEFCalystoRarity> AllTiers = { EEFCalystoRarity::Common, EEFCalystoRarity::Uncommon,
		EEFCalystoRarity::Rare, EEFCalystoRarity::Epic, EEFCalystoRarity::Winter };
	FEFCalystoRarityWeights Weights;
	Weights.Common = Weights.Uncommon = Weights.Rare = Weights.Epic = Weights.Winter = 1.0;
	FEFCalystoFloatDistribution Triangle;
	Triangle.Distribution = EEFCalystoDistribution::Triangular;
	Triangle.Minimum = 0.0; Triangle.Mode = 2.0; Triangle.Maximum = 6.0;
	int64 Presence = 0, ForgeCount = 0, TotalThemed = 0, FeasibleOnes = 0;
	int64 ThemeCounts[5] = {}, GuaranteedCounts[5] = {}, UniformCounts[3] = {}, TriangleCounts[3] = {}, RarityCounts[5] = {};
	double FloatSum = 0.0;
	bool bAllValid = true, bProtectedUnthemed = true;
	TArray<FEFCalystoThemeDecision> Decisions;
	FString Error;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		FEFCalystoRandomKey Key;
		Key.RunSeed = Trial + 104729;
		Key.StyleId = FGuid(9, 0, 0, 1);
		Presence += FProbability::RollChance(37.0, FProbability::Unit(Key, EEFCalystoRandomDomain::Chance)) ? 1 : 0;
		if (!FProbability::SelectThemedRooms(Key, Rooms, 25.0, Decisions, Error)) { bAllValid = false; break; }
		++ThemeCounts[Decisions.Num() - 1];
		for (const FEFCalystoThemeDecision& D : Decisions)
		{
			bProtectedUnthemed &= D.RoomId <= 5;
			ForgeCount += D.ThemeId == Forge ? 1 : 0;
			++TotalThemed;
			if (D.bGuaranteed) ++GuaranteedCounts[D.RoomId - 1];
		}
		int32 Amount = 0;
		double Mass = 0.0;
		if (!FProbability::SampleFeasibleAmount(UniformAmount, AllCounts,
			FProbability::Unit(Key, EEFCalystoRandomDomain::Amount), Amount, Mass, Error)) { bAllValid = false; break; }
		++UniformCounts[Amount - 1];
		if (!FProbability::SampleFeasibleAmount(TriangleAmount, AllCounts,
			FProbability::Unit(Key, EEFCalystoRandomDomain::Amount, FGuid(), 1), Amount, Mass, Error)) { bAllValid = false; break; }
		++TriangleCounts[Amount - 1];
		if (!FProbability::SampleFeasibleAmount(UniformAmount, FeasibleCounts,
			FProbability::Unit(Key, EEFCalystoRandomDomain::Amount, FGuid(), 2), Amount, Mass, Error) ||
			!FMath::IsNearlyEqual(Mass, 2.0 / 3.0, 1.e-12) || (Amount != 1 && Amount != 3)) { bAllValid = false; break; }
		FeasibleOnes += Amount == 1 ? 1 : 0;
		EEFCalystoRarity Tier;
		if (!FProbability::SelectPopulatedRarity(Weights, AllTiers,
			FProbability::Unit(Key, EEFCalystoRandomDomain::Rarity), Tier, Error)) { bAllValid = false; break; }
		++RarityCounts[static_cast<uint8>(Tier)];
		double FloatValue = 0.0;
		if (!FProbability::SampleFloat(Triangle, FProbability::Unit(Key, EEFCalystoRandomDomain::Topology), FloatValue, Error)) { bAllValid = false; break; }
		FloatSum += FloatValue;
	}
	TestTrue(TEXT("Every one of 100000 bounded decisions succeeds"), bAllValid);
	TestTrue(TEXT("Protected Start never receives a Theme"), bProtectedUnthemed);
	TestTrue(TEXT("37 percent opportunity presence"), CountWithinSigma(Presence, Trials, 0.37));
	TestTrue(TEXT("Conditional Forge weight 5 of 8"), CountWithinSigma(ForgeCount, TotalThemed, 5.0 / 8.0));
	const double Binomial[] = { 0.31640625, 0.421875, 0.2109375, 0.046875, 0.00390625 };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestTrue(FString::Printf(TEXT("1 + Binomial(4,.25), additional count %d"), Index), CountWithinSigma(ThemeCounts[Index], Trials, Binomial[Index]));
		TestTrue(FString::Printf(TEXT("Guaranteed rank is fair for room %d"), Index + 1), CountWithinSigma(GuaranteedCounts[Index], Trials, 0.2));
		TestTrue(FString::Printf(TEXT("Populated rarity tier %d includes Winter"), Index), CountWithinSigma(RarityCounts[Index], Trials, 0.2));
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestTrue(FString::Printf(TEXT("Inclusive uniform count %d"), Index + 1), CountWithinSigma(UniformCounts[Index], Trials, 1.0 / 3.0));
		TestTrue(FString::Printf(TEXT("Rounded triangular count %d"), Index + 1), CountWithinSigma(TriangleCounts[Index], Trials, Index == 1 ? 0.75 : 0.125));
	}
	TestTrue(TEXT("Feasible count probabilities condition requested mass"), CountWithinSigma(FeasibleOnes, Trials, 0.5));
	TestTrue(TEXT("Continuous triangular mean is mathematical, not PERT-like"), FMath::Abs(FloatSum / Trials - 8.0 / 3.0) < 7.0 * FMath::Sqrt((28.0 / 18.0) / Trials));
	AddInfo(FString::Printf(TEXT("In-memory trials=%d; requested chance=37%%; feasible Amount mass=2/3; selected themed rooms=%lld; rejected decisions=%d. World generation and accepted-world probabilities are separate PENDING gates."), Trials, TotalThemed, bAllValid ? 0 : 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorProbabilityBoundaries,
	"NoShellForWinter.CalystoDungeon.Director.Probability.BoundariesAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorProbabilityBoundaries::RunTest(const FString& Parameters)
{
	using namespace EFCalystoDirectorFoundationTests;
	FEFCalystoPercentageCurve Curve;
	Curve.FirstFloor = 3; Curve.LastFloor = 13; Curve.FirstPercent = 10.0; Curve.LastPercent = 90.0;
	TestEqual(TEXT("Exact first endpoint"), FProbability::EvaluateLinearCurve(Curve, 3), 10.0);
	TestEqual(TEXT("Exact last endpoint"), FProbability::EvaluateLinearCurve(Curve, 13), 90.0);
	TestEqual(TEXT("Linear midpoint"), FProbability::EvaluateLinearCurve(Curve, 8), 50.0);
	TestEqual(TEXT("Clamp below first endpoint"), FProbability::EvaluateLinearCurve(Curve, 1), 10.0);
	TestEqual(TEXT("Clamp above last endpoint"), FProbability::EvaluateLinearCurve(Curve, 1000), 90.0);
	Curve.LastFloor = Curve.FirstFloor;
	TestFalse(TEXT("Conflicting coincident endpoints are invalid"), FProbability::IsValid(Curve));
	TestFalse(TEXT("Zero chance never selects even zero draw"), FProbability::RollChance(0.0, 0.0));
	TestTrue(TEXT("100 percent selects the largest supported unit"), FProbability::RollChance(100.0, 0.9999999999999998));
	const FGuid A(1, 0, 0, 1), B(1, 0, 0, 2);
	TArray<FEFCalystoWeightedAlternative> Entries = {{A, 0.0}, {B, 1.0}};
	FGuid Selected;
	FString Error;
	TestTrue(TEXT("One eligible positive entry"), FProbability::SelectWeighted(Entries, 0.0, Selected, Error));
	TestEqual(TEXT("Zero weight cannot win"), Selected, B);
	Entries[1].Weight = 0.0;
	TestFalse(TEXT("All zero weights are infeasible"), FProbability::SelectWeighted(Entries, 0.5, Selected, Error));
	Entries.Reset();
	TestFalse(TEXT("Empty catalog is infeasible"), FProbability::SelectWeighted(Entries, 0.5, Selected, Error));
	FEFCalystoAmountDistribution Fixed;
	Fixed.Amount = 3;
	int32 Amount = 0; double Mass = 0.0;
	const TArray<int32> Impossible = {1, 2}, Feasible = {3};
	TestFalse(TEXT("Impossible fixed amount is not truncated"), FProbability::SampleFeasibleAmount(Fixed, Impossible, 0.5, Amount, Mass, Error));
	TestTrue(TEXT("Reserved fixed amount materializes exact count"), FProbability::SampleFeasibleAmount(Fixed, Feasible, 0.5, Amount, Mass, Error));
	TestEqual(TEXT("Fixed amount"), Amount, 3);
	FEFCalystoRarityWeights Weights;
	const TArray<EEFCalystoRarity> WinterOnly = {EEFCalystoRarity::Winter};
	EEFCalystoRarity Tier;
	TestTrue(TEXT("Only populated Winter tier has no implicit Nothing"), FProbability::SelectPopulatedRarity(Weights, WinterOnly, 0.9999, Tier, Error));
	TestTrue(TEXT("Winter remains Winter"), Tier == EEFCalystoRarity::Winter);
	FEFCalystoRandomKey Key;
	Key.RunSeed = 1779679224;
	Key.StyleId = A;
	TArray<FEFCalystoThemeOpportunity> Rooms;
	for (int64 RoomId = 1; RoomId <= 12; ++RoomId)
	{
		FEFCalystoThemeOpportunity& Room = Rooms.AddDefaulted_GetRef();
		Room.RoomId = RoomId; Room.EligibleThemes = {{A, 5.0}, {B, 3.0}};
	}
	TArray<FEFCalystoThemeDecision> Original, Changed;
	TestTrue(TEXT("Original Theme decisions"), FProbability::SelectThemedRooms(Key, Rooms, 25.0, Original, Error));
	Algo::Reverse(Rooms);
	for (FEFCalystoThemeOpportunity& Room : Rooms)
	{
		Algo::Reverse(Room.EligibleThemes);
		Room.EligibleThemes[0].Weight = 999.0;
	}
	TestTrue(TEXT("Reordered and reweighted Theme decisions"), FProbability::SelectThemedRooms(Key, Rooms, 25.0, Changed, Error));
	bool bSamePresence = Original.Num() == Changed.Num();
	for (int32 Index = 0; Index < Original.Num() && bSamePresence; ++Index)
		bSamePresence &= Original[Index].RoomId == Changed[Index].RoomId && Original[Index].bGuaranteed == Changed[Index].bGuaranteed;
	TestTrue(TEXT("Theme weights and array order cannot change presence or guarantee"), bSamePresence);
	Rooms.SetNum(1);
	TestTrue(TEXT("Single eligible room satisfies guarantee with zero additional chance"), FProbability::SelectThemedRooms(Key, Rooms, 0.0, Changed, Error));
	TestEqual(TEXT("Exactly one guaranteed room"), Changed.Num(), 1);
	Rooms[0].Protected = EEFCalystoProtectedRoom::Progression;
	TestFalse(TEXT("No eligible room rejects before content commitment"), FProbability::SelectThemedRooms(Key, Rooms, 25.0, Changed, Error));
	TestEqual(TEXT("Rejected Theme resolution leaves no partial decisions"), Changed.Num(), 0);
	const uint64 StyleBefore = FProbability::Hash(Key, EEFCalystoRandomDomain::Style);
	const uint64 TopologyBefore = FProbability::Hash(Key, EEFCalystoRandomDomain::Topology);
	++Key.AttemptIndex;
	TestEqual(TEXT("Reseeding preserves Style domain"), FProbability::Hash(Key, EEFCalystoRandomDomain::Style), StyleBefore);
	TestTrue(TEXT("Reseeding changes topology domain"), FProbability::Hash(Key, EEFCalystoRandomDomain::Topology) != TopologyBefore);
	FEFCalystoAdaptation Adaptation;
	TestEqual(TEXT("Disabled adaptation negative input exactly neutral"), FProbability::AdaptationMultiplier(Adaptation, -1.0), 1.0);
	TestEqual(TEXT("Disabled adaptation positive input exactly neutral"), FProbability::AdaptationMultiplier(Adaptation, 1.0), 1.0);
	Adaptation.bEnabled = true;
	TestEqual(TEXT("Enabled bounded negative effect"), FProbability::AdaptationMultiplier(Adaptation, -5.0), 0.8);
	TestEqual(TEXT("Enabled bounded positive effect"), FProbability::AdaptationMultiplier(Adaptation, 5.0), 1.2);
	FEFCalystoSelection Selection;
	Selection.Id = A; Selection.FirstEligibleFloor = 5; Selection.LastEligibleFloor = 10;
	const TArray<FGuid> NoCooldown, Cooldown = {A};
	TestFalse(TEXT("Depth ineligibility before draw"), FProbability::IsEligible(Selection, 4, NoCooldown));
	TestTrue(TEXT("Exact first eligible depth"), FProbability::IsEligible(Selection, 5, NoCooldown));
	TestFalse(TEXT("Exhausted cooldown before draw"), FProbability::IsEligible(Selection, 5, Cooldown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorCompilation,
	"NoShellForWinter.CalystoDungeon.Director.Authoring.CompilationAndPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorCompilation::RunTest(const FString& Parameters)
{
	using namespace EFCalystoDirectorFoundationTests;
	UEFCalystoDungeonDirectorAsset* Asset = MakeAsset();
	FEFCalystoCompiledDirector Compiled;
	TArray<FEFCalystoValidationIssue> Issues;
	TestTrue(TEXT("Direct schema 7 configuration compiles without legacy input"), Asset->Compile(Compiled, Issues));
	TestEqual(TEXT("Internal schema"), Asset->GetSchemaVersion(), 7);
	TestFalse(TEXT("Adaptation default is disabled"), Asset->Advanced.Adaptation.bEnabled);
	const FGuid StyleId = Asset->Styles[0].Selection.Id, ThemeId = Asset->RoomThemes[0].Selection.Id;
	FEFCalystoSurfaceMaterials Materials;
	FString Error;
	TestTrue(TEXT("Material precedence resolves"), Compiled.ResolveMaterials(StyleId, ThemeId, Materials, Error));
	TestTrue(TEXT("Theme wins Floor"), Materials.Floor == Asset->RoomThemes[0].Materials.Floor.Material);
	TestTrue(TEXT("Inherit retains Style Wall"), Materials.Wall == Asset->Styles[0].Materials.Wall);
	Asset->Styles[0].Materials.Wall.Reset();
	TestFalse(TEXT("Missing Style material fails authoring"), Asset->Compile(Compiled, Issues));
	TestFalse(TEXT("Failed recompile cannot retain a valid old configuration"), Compiled.IsValid());
	TestTrue(TEXT("Error identifies exact Style Wall field"), Issues.ContainsByPredicate([](const FEFCalystoValidationIssue& I) { return I.Field == TEXT("Styles[0].Materials.Wall"); }));
	TestTrue(TEXT("Scripting preserves exact errors for invalid authoring"), Asset->GetAuthoringErrors().ContainsByPredicate(
		[](const FString& Error) { return Error.StartsWith(TEXT("Styles[0].Materials.Wall:")); }));
	Asset = MakeAsset();
	Asset->RoomThemes[0].Materials.Floor.Material.Reset();
	TestFalse(TEXT("Empty explicit override cannot silently inherit"), Asset->Compile(Compiled, Issues));
	Asset = MakeAsset();
	Asset->Styles[0].Architecture.Floor[0].Payload = EEFCalystoArchitecturePayload::Empty;
	TestFalse(TEXT("Structural Floor cannot disappear through Empty"), Asset->Compile(Compiled, Issues));
	Asset = MakeAsset();
	FEFCalystoContentGroup& Base = Asset->Styles[0].Content.AddDefaulted_GetRef();
	Base.Id = FGuid(4, 0, 0, 1); Base.DisplayName = TEXT("Enemies");
	Base.Role = EEFCalystoGameplayRole::Enemy; Base.Budget = EEFCalystoBudgetMembership::Enemy;
	Base.MaximumPerFloor = 4; Base.Chance.FirstPercent = Base.Chance.LastPercent = 35.0;
	Base.Amount.Amount = 2;
	FEFCalystoContentEntry& Entry = Base.Entries.AddDefaulted_GetRef();
	Entry.Selection.Id = FGuid(5, 0, 0, 1); Entry.Selection.DisplayName = TEXT("Any editable label");
	Entry.ActorClass = AActor::StaticClass(); Entry.Gender = EEFCalystoGender::Female;
	Entry.Archetype = TEXT("Defender"); Entry.Lifecycle = EEFCalystoLifecycle::Recruitable;
	FEFCalystoContentGroup& Overlay = Asset->RoomThemes[0].Content.AddDefaulted_GetRef();
	Overlay.Id = FGuid(4, 0, 0, 2); Overlay.Role = EEFCalystoGameplayRole::Enemy;
	Overlay.Mode = EEFCalystoContentMode::Extend;
	Overlay.Chance.FirstPercent = Overlay.Chance.LastPercent = 99.0;
	Overlay.Amount.Amount = 9; Overlay.MaximumPerFloor = 999;
	TestTrue(TEXT("Explicit Extend configuration compiles"), Asset->Compile(Compiled, Issues));
	TArray<FEFCalystoResolvedContentGroup> Effective;
	TestTrue(TEXT("Extend resolves"), Compiled.ResolveContent(StyleId, ThemeId, Effective, Error));
	if (Effective.Num() != 1) { AddError(TEXT("Expected one typed content role.")); return false; }
	TestEqual(TEXT("Extend preserves inherited Chance without override"), Effective[0].Content.Chance.FirstPercent, 35.0);
	TestEqual(TEXT("Extend preserves inherited Amount without override"), Effective[0].Content.Amount.Amount, 2);
	TestEqual(TEXT("Extend preserves inherited category limit"), Effective[0].Content.MaximumPerFloor, 4);
	TestEqual(TEXT("Floor-wide category cap remains separate"), Effective[0].FloorMaximum, 4);
	Overlay.bOverrideChance = true;
	Overlay.bOverrideAmount = true;
	TestTrue(TEXT("Explicit overrides compile"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestEqual(TEXT("Explicit Chance override has an effect"), Effective[0].Content.Chance.FirstPercent, 99.0);
	TestEqual(TEXT("Explicit Amount override has an effect"), Effective[0].Content.Amount.Amount, 9);
	Overlay.RemovedEntryIds.Add(Entry.Selection.Id);
	TestTrue(TEXT("Typed removal compiles"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestEqual(TEXT("Extend can remove an identified entry"), Effective[0].Content.Entries.Num(), 0);
	Overlay.Mode = EEFCalystoContentMode::Block;
	TestTrue(TEXT("Block compiles"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestEqual(TEXT("Block removes only the local category"), Effective.Num(), 0);
	Overlay.Mode = EEFCalystoContentMode::Replace;
	TestTrue(TEXT("Replace compiles"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestEqual(TEXT("Replace owns local probability"), Effective[0].Content.Chance.FirstPercent, 99.0);
	TestEqual(TEXT("Replace cannot bypass Style floor cap"), Effective[0].FloorMaximum, 4);
	Overlay.Mode = EEFCalystoContentMode::Inherit;
	TestTrue(TEXT("Inherit compiles"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestEqual(TEXT("Inherit uses Style count"), Effective[0].Content.Amount.Amount, 2);
	const FEFCalystoCompiledDirector BeforeRename = Compiled;
	Entry.Selection.DisplayName = TEXT("NPC.Male.Mage is only a label");
	Asset->Styles[0].Selection.DisplayName = TEXT("Renamed Style");
	Asset->RoomThemes[0].Selection.DisplayName = TEXT("Renamed Theme");
	TestTrue(TEXT("Labels compile without semantic parsing"), Asset->Compile(Compiled, Issues));
	Compiled.ResolveContent(StyleId, ThemeId, Effective, Error);
	TestTrue(TEXT("Label cannot change gender"), Effective[0].Content.Entries[0].Gender == EEFCalystoGender::Female);
	TestEqual(TEXT("Label cannot change archetype"), Effective[0].Content.Entries[0].Archetype, FName(TEXT("Defender")));
	TestTrue(TEXT("Label cannot change lifecycle"), Effective[0].Content.Entries[0].Lifecycle == EEFCalystoLifecycle::Recruitable);
	TestTrue(TEXT("Compiled snapshot owns immutable copied data"), BeforeRename.GetStyles()[0].Selection.DisplayName == TEXT("General"));
#if WITH_EDITOR
	const FGuid OriginalId = Asset->Styles[0].Selection.Id;
	FEFCalystoStyle DuplicateStyle = Asset->Styles[0];
	Asset->Styles.Add(MoveTemp(DuplicateStyle));
	Asset->EnsureEditorIdentities();
	TestEqual(TEXT("Array duplication preserves original ID"), Asset->Styles[0].Selection.Id, OriginalId);
	TestTrue(TEXT("Array duplicate gets its own persisted ID"), Asset->Styles[1].Selection.Id != OriginalId);
	const FGuid DuplicateId = Asset->Styles[1].Selection.Id;
	Algo::Reverse(Asset->Styles);
	Asset->EnsureEditorIdentities();
	TestEqual(TEXT("Editor identity maintenance is stable after reorder"), Asset->Styles[0].Selection.Id, DuplicateId);
#endif
	AddInfo(TEXT("Authoring/decision tests do not establish material realization, native geometry, gameplay traversal or packaged validation."));
	return true;
}
#endif
