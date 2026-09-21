#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/EFCalystoFloorLoadCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace EFCalystoDungeonRuntimeV6Tests
{
FString Hash(const TCHAR* Text)
{
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Text);
}

void SetRange(
	FEFCalystoPertRangeV6& Range,
	const float Minimum,
	const float Mode,
	const float Maximum,
	const float Shape = 4.0f)
{
	Range.Minimum = Minimum;
	Range.Mode = Mode;
	Range.Maximum = Maximum;
	Range.Shape = Shape;
}

FEFCalystoResolvedFloorPlanV6 MakeFloorPlan()
{
	FEFCalystoResolvedFloorPlanV6 Plan;
	Plan.FloorSeed = 40102030;
	Plan.StyleId = TEXT("Branching");
	Plan.ThemeRoomChance = 0.25f;
	Plan.RoomIdentityQuantizationCm = 10.0f;
	Plan.MaximumRoomRecords = 2048;
	Plan.DecalComponentPoolCapacity = 24;
	SetRange(Plan.Layout.DungeonSize, 18.0f, 25.0f, 30.0f);
	SetRange(Plan.Layout.CandidateDensity, 0.20f, 0.34f, 0.50f);
	SetRange(Plan.Layout.SidePathChance, 0.30f, 0.56f, 0.70f);
	Plan.Layout.MinimumRoomSize = 4;
	Plan.Layout.MaximumRoomSize = 8;
	Plan.PolicyHash = Hash(TEXT("RuntimeV6.Policy"));
	Plan.StyleCatalogHash = Hash(TEXT("RuntimeV6.Catalog"));
	Plan.StyleMaterialHash = Hash(TEXT("RuntimeV6.Material"));
	Plan.StyleDecalHash = Hash(TEXT("RuntimeV6.Decal"));
	Plan.FloorPlanHash = Hash(TEXT("RuntimeV6.FloorPlan.Branching"));
	FEFCalystoResolvedThemeProfileV6& ReachableTheme = Plan.Themes.AddDefaulted_GetRef();
	ReachableTheme.ThemeId = TEXT("Forge");
	ReachableTheme.SelectionWeight = 1.0f;
	ReachableTheme.CatalogHash = Hash(TEXT("RuntimeV6.Forge.Catalog"));
	ReachableTheme.MaterialHash = Hash(TEXT("RuntimeV6.Forge.Material"));
	ReachableTheme.DecalHash = Hash(TEXT("RuntimeV6.Forge.Decal"));
	Plan.ThemeAliasProbability.Add(1.0f);
	Plan.ThemeAliasIndex.Add(0);
	return Plan;
}

FEFCalystoCompanionRosterSnapshotV6 MakeRoster()
{
	FEFCalystoCompanionRosterSnapshotV6 Roster;
	Roster.bIsValid = true;
	Roster.RunEpoch = 3;
	FEFCalystoCompanionRecordV6& Record = Roster.Records.AddDefaulted_GetRef();
	Record.StableCompanionId = FGuid(1, 2, 3, 4);
	Record.SourceSpawnId = TEXT("Spawn.Companion.One");
	Record.SourceCatalogId = TEXT("NPC.Companion");
	Record.SourceVariantId = TEXT("Companion.One");
	Record.ActorClass = TSoftClassPtr<AActor>(
		FSoftObjectPath(TEXT("/Game/Test/BP_CompanionOne.BP_CompanionOne_C")));
	Record.Archetype = TEXT("Defender");
	Record.Grade = EEFCalystoRarityTierV6::Rare;
	Record.State = EEFCalystoCompanionRosterStateV6::ActiveParty;
	Roster.ActiveParty.Add(Record.StableCompanionId);
	Roster.SnapshotHash = FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(Roster);
	return Roster;
}

FEFCalystoRunEcologyStateV6 MakeEcology(const FEFCalystoCompanionRosterSnapshotV6& Roster)
{
	FEFCalystoRunEcologyStateV6 Ecology;
	Ecology.bInitialized = true;
	Ecology.RunDNAHash = Hash(TEXT("RuntimeV6.RunDNA"));
	Ecology.Scale = 0.15f;
	Ecology.Branching = 0.35f;
	Ecology.Threat = -0.10f;
	Ecology.Abundance = 0.20f;
	Ecology.Mystery = 0.40f;
	Ecology.PerformanceEMA = 0.55f;
	Ecology.LastCommittedFloor = 6;
	Ecology.Revision = 2;
	Ecology.RecentStyleIds = {TEXT("Standard"), TEXT("Compact")};
	FEFCalystoCooldownStateV6& Cooldown = Ecology.Cooldowns.AddDefaulted_GetRef();
	Cooldown.StableId = TEXT("Enemy.Melee.Defender");
	Cooldown.LastSelectedFloor = 6;
	Cooldown.CooldownFloors = 2;
	Ecology.CompanionRoster = Roster;
	Ecology.EcologyHash = FEFCalystoDungeonRuntimeMathV6::ComputeRunEcologyHash(Ecology);
	return Ecology;
}

FEFCalystoDungeonGenerationContextV6 MakeContext(const FEFCalystoResolvedFloorPlanV6& Plan)
{
	FEFCalystoDungeonGenerationContextV6 Context;
	Context.RunSeed = 9918273;
	Context.FloorNumber = 7;
	Context.GenerationSerial = 4;
	Context.RunEpoch = 3;
	Context.TravelKind = EEFCalystoDungeonTravelKindV6::Advance;
	Context.PolicyHash = Plan.PolicyHash;
	Context.ContextHash = FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(Context);
	return Context;
}

FEFCalystoDirectorIntentV6 MakeDirectorIntent()
{
	FEFCalystoDirectorIntentV6 Intent;
	Intent.Scale = 0.25f;
	Intent.Branching = 0.40f;
	Intent.Mystery = 0.10f;
	Intent.bHasPreferredStyle = true;
	Intent.PreferredStyleId = TEXT("Branching");
	Intent.IntentHash = FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(Intent);
	return Intent;
}

FEFCalystoFloorOutcomeV6 MakeOutcome()
{
	FEFCalystoFloorOutcomeV6 Outcome;
	Outcome.Combat = 0.70f;
	Outcome.Survival = 0.80f;
	Outcome.Resources = 0.30f;
	Outcome.Pace = 0.60f;
	Outcome.DeathsAndFailures = 0.10f;
	Outcome.OutcomeHash = FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome);
	return Outcome;
}

bool HasDirectThemeProperty(const UScriptStruct* Struct)
{
	for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
	{
		if (It->GetName().Contains(TEXT("Theme"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
} // namespace EFCalystoDungeonRuntimeV6Tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoRuntimeContractsV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.Contracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoRuntimeContractsV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonRuntimeV6Tests;
	TestEqual(TEXT("Schema is exactly 6"), EFCalystoDungeonRuntimeSchemaV6::SchemaVersion, 6);
	TestEqual(TEXT("Generator is exactly 6"), EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion, 6);
	TestFalse(TEXT("Director Intent has no global Theme property"),
		HasDirectThemeProperty(FEFCalystoDirectorIntentV6::StaticStruct()));
	TestFalse(TEXT("Resolved Floor Intent has no global Theme property"),
		HasDirectThemeProperty(FEFCalystoResolvedFloorIntentV6::StaticStruct()));
	TestFalse(TEXT("Run Ecology has no global Theme property"),
		HasDirectThemeProperty(FEFCalystoRunEcologyStateV6::StaticStruct()));
	TestFalse(TEXT("Dungeon Snapshot has no global Theme property"),
		HasDirectThemeProperty(FEFCalystoDungeonSnapshotV6::StaticStruct()));
	const FProperty* PreferredStyle = FEFCalystoDirectorIntentV6::StaticStruct()->FindPropertyByName(
		GET_MEMBER_NAME_CHECKED(FEFCalystoDirectorIntentV6, PreferredStyleId));
	TestNotNull(TEXT("Preferred Style ID is reflected"), PreferredStyle);
	TestTrue(TEXT("Preferred Style ID is an FName property"), PreferredStyle && PreferredStyle->IsA<FNameProperty>());

	FEFCalystoFloorLoadCoordinator LoadCoordinator;
	FString LoadError;
	TestEqual(TEXT("An absent async phase is distinguishable from an in-flight phase"),
		LoadCoordinator.GetPhaseState(EEFCalystoLoadPhase::FloorVisual, LoadError),
		EEFCalystoLoadPhaseState::NotStarted);
	bool bEmptyPhaseCompleted = false;
	const TArray<FSoftObjectPath> EmptyPaths;
	TestTrue(TEXT("An empty phase starts without a synthetic handle"),
		LoadCoordinator.BeginPhase(
			EEFCalystoLoadPhase::FloorVisual,
			EmptyPaths,
			FStreamableDelegate::CreateLambda([&bEmptyPhaseCompleted]()
			{
				bEmptyPhaseCompleted = true;
			}),
			LoadError));
	TestTrue(TEXT("An empty phase completes its delegate immediately"), bEmptyPhaseCompleted);
	TestEqual(TEXT("An empty retained phase is ready"),
		LoadCoordinator.GetPhaseState(EEFCalystoLoadPhase::FloorVisual, LoadError),
		EEFCalystoLoadPhaseState::Ready);
	LoadCoordinator.ResetFloor();
	TestEqual(TEXT("ResetFloor releases floor-specific phase state"),
		LoadCoordinator.GetPhaseState(EEFCalystoLoadPhase::FloorVisual, LoadError),
		EEFCalystoLoadPhaseState::NotStarted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoRuntimeIntentDeterminismV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.IntentDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoRuntimeIntentDeterminismV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonRuntimeV6Tests;
	const FEFCalystoResolvedFloorPlanV6 Plan = MakeFloorPlan();
	const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeRoster();
	const FEFCalystoRunEcologyStateV6 Ecology = MakeEcology(Roster);
	const FEFCalystoDungeonGenerationContextV6 Context = MakeContext(Plan);
	const FEFCalystoDirectorIntentV6 DirectorIntent = MakeDirectorIntent();
	const FEFCalystoFloorOutcomeV6 Outcome = MakeOutcome();
	FEFCalystoLightingPolicyV6 Lighting;
	Lighting.Mode = EEFCalystoLightingModeV6::Cold;
	Lighting.IntensityMultiplier = 1.25f;

	FEFCalystoResolvedFloorIntentV6 A;
	FEFCalystoResolvedFloorIntentV6 B;
	FString Error;
	TestTrue(*FString::Printf(TEXT("First intent builds: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
			Context, DirectorIntent, Outcome, Ecology, Roster, Plan, Lighting, A, Error));
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Second intent builds: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
			Context, DirectorIntent, Outcome, Ecology, Roster, Plan, Lighting, B, Error));
	TestTrue(TEXT("Resolved intent hash is canonical"),
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(A.IntentHash));
	TestEqual(TEXT("Same immutable inputs produce the same intent hash"), A.IntentHash, B.IntentHash);
	TestEqual(TEXT("The selected Style remains an open FName"), A.StyleId, FName(TEXT("Branching")));
	TestTrue(TEXT("Dungeon edge remains certified"), A.DungeonSize.X >= 18 && A.DungeonSize.X <= 30);
	TestEqual(TEXT("Dungeon is square"), A.DungeonSize.Y, A.DungeonSize.X);
	TestEqual(TEXT("Dungeon Z is one"), A.DungeonSize.Z, 1);
	TestTrue(TEXT("Candidate density remains certified"), A.CandidateDensity >= 0.20f && A.CandidateDensity <= 0.50f);
	TestTrue(TEXT("Side path chance remains certified"), A.SidePathChance >= 0.30f && A.SidePathChance <= 0.70f);
	TestTrue(TEXT("PCG seed is positive"), A.PCGSeed > 0);
	TestEqual(TEXT("One level is frozen for every roster record"), A.ResolvedCompanionLevels.Num(), Roster.Records.Num());

	FEFCalystoLightingPolicyV6 DifferentLighting = Lighting;
	DifferentLighting.Mode = EEFCalystoLightingModeV6::Dark;
	DifferentLighting.IntensityMultiplier = 0.5f;
	FEFCalystoResolvedFloorIntentV6 LightingVariant;
	Error.Reset();
	TestTrue(TEXT("Lighting variant builds"),
		FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
			Context, DirectorIntent, Outcome, Ecology, Roster, Plan, DifferentLighting,
			LightingVariant, Error));
	TestEqual(TEXT("Lighting does not perturb dungeon-size lane"), LightingVariant.DungeonSize, A.DungeonSize);
	TestEqual(TEXT("Lighting does not perturb candidate-density lane"), LightingVariant.CandidateDensity, A.CandidateDensity);
	TestEqual(TEXT("Lighting does not perturb side-path lane"), LightingVariant.SidePathChance, A.SidePathChance);
	TestEqual(TEXT("Lighting does not perturb PCG-seed lane"), LightingVariant.PCGSeed, A.PCGSeed);
	TestNotEqual(TEXT("Lighting facts remain fingerprinted"), LightingVariant.IntentHash, A.IntentHash);

	FEFCalystoDungeonGenerationContextV6 RetryContext = Context;
	RetryContext.TravelKind = EEFCalystoDungeonTravelKindV6::Retry;
	RetryContext.RunEpoch += 100;
	TestEqual(TEXT("Travel kind and Run Epoch never perturb generation identity"),
		FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(RetryContext), Context.ContextHash);
	FEFCalystoResolvedFloorIntentV6 RetryIntent = A;
	RetryIntent.GenerationContext.TravelKind = EEFCalystoDungeonTravelKindV6::Retry;
	RetryIntent.GenerationContext.RunEpoch += 100;
	TestEqual(TEXT("Retry semantics do not perturb the frozen floor intent identity"),
		FEFCalystoDungeonRuntimeMathV6::ComputeResolvedFloorIntentHash(RetryIntent), A.IntentHash);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("An intent relabeled consistently as Retry remains valid: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(RetryIntent, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoRuntimeCanonicalHashesV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.CanonicalHashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoRuntimeCanonicalHashesV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonRuntimeV6Tests;
	const FEFCalystoResolvedFloorPlanV6 Plan = MakeFloorPlan();
	const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeRoster();
	FEFCalystoRunEcologyStateV6 Ecology = MakeEcology(Roster);
	TestTrue(TEXT("Roster hash is canonical"),
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Roster.SnapshotHash));
	TestTrue(TEXT("Ecology hash is canonical"),
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Ecology.EcologyHash));
	FString Error;
	TestTrue(*FString::Printf(TEXT("Ecology validates: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::ValidateRunEcology(Ecology, Error));

	FEFCalystoFloorOutcomeV6 Outcome = MakeOutcome();
	const FString OriginalOutcomeHash = Outcome.OutcomeHash;
	Outcome.Pace = 0.61f;
	TestNotEqual(TEXT("Outcome hash fingerprints every normalized field"),
		FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome), OriginalOutcomeHash);

	FEFCalystoRealizedFloorManifestV6 Manifest;
	Manifest.bIsValid = true;
	Manifest.RunSeed = 9918273;
	Manifest.FloorNumber = 7;
	Manifest.GenerationSerial = 4;
	Manifest.StyleId = Plan.StyleId;
	Manifest.IntentHash = Hash(TEXT("RuntimeV6.Intent"));
	Manifest.FloorPlanHash = Plan.FloorPlanHash;
	Manifest.RoomManifestHash = Hash(TEXT("RuntimeV6.RoomManifest"));
	Manifest.AnchorTopologyHash = Hash(TEXT("RuntimeV6.Anchors"));
	Manifest.PopulationPlanHash = Hash(TEXT("RuntimeV6.PopulationPlan"));
	Manifest.CompanionSnapshotHash = Roster.SnapshotHash;
	Manifest.CandidateAnchorCount = 4;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FEFCalystoRealizedPopulationActorRecordV6& Actor = Manifest.Actors.AddDefaulted_GetRef();
		Actor.StableActorId = FName(*FString::Printf(TEXT("Actor.%d"), Index));
		Actor.StableRoomId = 100 + Index;
		Actor.CategoryId = TEXT("Enemy");
		Actor.CatalogEntryId = TEXT("Enemy.Melee");
		Actor.ActorClass = TSoftClassPtr<AActor>(
			FSoftObjectPath(TEXT("/Game/Test/BP_Enemy.BP_Enemy_C")));
		Actor.Transform.SetTranslation(FVector(Index * 100.0, 0.0, 0.0));
		Actor.LogicalLevel = 7;
		Actor.ThreatCost = 1.5f;
	}
	Manifest.SpawnedActorCount = Manifest.Actors.Num();
	Manifest.RealizedThreatCost = 3.0f;
	Manifest.ManifestHash = FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(Manifest);
	TestTrue(TEXT("Realized manifest hash is canonical"),
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Manifest.ManifestHash));
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Realized manifest validates: %s"), *Error),
		FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(Manifest, Error));
	FEFCalystoRealizedFloorManifestV6 Reordered = Manifest;
	Algo::Reverse(Reordered.Actors);
	Reordered.ManifestHash.Reset();
	TestEqual(TEXT("Manifest actor input order is not identity"),
		FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(Reordered), Manifest.ManifestHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoRuntimeValidationBoundsV6Test,
	"NoShellForWinter.CalystoDungeon.V6.Runtime.ValidationBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoRuntimeValidationBoundsV6Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoDungeonRuntimeV6Tests;
	FString Error;
	FEFCalystoDirectorIntentV6 InvalidIntent = MakeDirectorIntent();
	InvalidIntent.Scale = 1.01f;
	InvalidIntent.IntentHash.Reset();
	TestFalse(TEXT("Director Intent rejects values above one"),
		FEFCalystoDungeonRuntimeMathV6::ValidateDirectorIntent(InvalidIntent, Error));

	FEFCalystoFloorOutcomeV6 InvalidOutcome = MakeOutcome();
	InvalidOutcome.DeathsAndFailures = -0.01f;
	InvalidOutcome.OutcomeHash.Reset();
	Error.Reset();
	TestFalse(TEXT("Floor Outcome rejects negative values"),
		FEFCalystoDungeonRuntimeMathV6::ValidateFloorOutcome(InvalidOutcome, Error));

	const FEFCalystoResolvedFloorPlanV6 Plan = MakeFloorPlan();
	const FEFCalystoCompanionRosterSnapshotV6 Roster = MakeRoster();
	const FEFCalystoRunEcologyStateV6 Ecology = MakeEcology(Roster);
	FEFCalystoDungeonGenerationContextV6 Context = MakeContext(Plan);
	Context.DevelopmentForcedDungeonEdge = 31;
	Context.ContextHash.Reset();
	FEFCalystoResolvedFloorIntentV6 Resolved;
	FEFCalystoLightingPolicyV6 Lighting;
	Error.Reset();
	TestFalse(TEXT("Floor builder rejects uncertified development size"),
		FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
			Context, MakeDirectorIntent(), MakeOutcome(), Ecology, Roster, Plan,
			Lighting, Resolved, Error));

	FEFCalystoDungeonSnapshotV6 Snapshot;
	Snapshot.State = EEFCalystoDungeonTravelStateV6::Ready;
	Snapshot.TravelKind = EEFCalystoDungeonTravelKindV6::Advance;
	Snapshot.bHasActiveRun = true;
	Snapshot.bPolicyValid = true;
	Snapshot.RunSeed = 9918273;
	Snapshot.FloorNumber = 7;
	Snapshot.GenerationSerial = 4;
	Snapshot.StyleId = Plan.StyleId;
	Snapshot.DungeonSize = FIntVector(24, 24, 1);
	Snapshot.PCGSeed = 12345;
	Snapshot.FloorPlanHash = Plan.FloorPlanHash;
	Snapshot.FloorIntentHash = Hash(TEXT("RuntimeV6.Intent"));
	Snapshot.RoomManifestHash = Hash(TEXT("RuntimeV6.RoomManifest"));
	Snapshot.PopulationManifestHash = Hash(TEXT("RuntimeV6.PopulationManifest"));
	Snapshot.bPCGComplete = true;
	Snapshot.bNavigationPathReady = true;
	Snapshot.bRoomManifestReady = true;
	Snapshot.bPopulationReady = true;
	Snapshot.bVisualsReady = true;
	Snapshot.bDoorEnabled = true;
	Snapshot.SnapshotHash = FEFCalystoDungeonRuntimeMathV6::ComputeDungeonSnapshotHash(Snapshot);
	TestTrue(TEXT("Dungeon snapshot hash is canonical"),
		FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Snapshot.SnapshotHash));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
