// RETIRED V3 AUTOMATION EVIDENCE.
//
// V4 has incompatible policy, intent, ecology and manifest semantics. Keep the
// original source byte-visible for migration archaeology, but do not compile or
// register these V3 tests after the V4 cutover.
#if 0

#include "Calysto/EFCalystoDungeonDirectorPolicy.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformMath.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include <limits>

namespace EFCalystoV3Tests
{
	static constexpr TCHAR PolicyPath[] =
		TEXT("/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy");

	static UEFCalystoDungeonDirectorPolicy* MakeDraftPolicy()
	{
		return NewObject<UEFCalystoDungeonDirectorPolicy>(GetTransientPackage());
	}

	/** Test-only valid policy. Size 25 is a logical fixture, not certification evidence. */
	static UEFCalystoDungeonDirectorPolicy* MakePolicyFixture(
		const TArray<int32>& ValidatedSizes = TArray<int32>{25})
	{
		UEFCalystoDungeonDirectorPolicy* Policy = MakeDraftPolicy();
		if (Policy)
		{
			Policy->ValidatedDungeonSizes = ValidatedSizes;
		}
		return Policy;
	}

	static FEFCalystoDungeonGenerationContext MakeContext(
		const int64 RunSeed,
		const int64 Floor,
		const int64 Serial,
		const FString& PolicyHash,
		const FString& EcologyHash,
		const int32 GeneratorVersion = 3)
	{
		FEFCalystoDungeonGenerationContext Context;
		Context.RunSeed = RunSeed;
		Context.FloorNumber = Floor;
		Context.GenerationSerial = Serial;
		Context.PolicyHash = PolicyHash;
		Context.PCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
			RunSeed,
			Floor,
			Serial,
			GeneratorVersion,
			PolicyHash,
			EcologyHash);
		return Context;
	}

	static bool Resolve(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoDungeonGenerationContext& Context,
		const FEFCalystoRunEcologyState& Ecology,
		FEFCalystoResolvedFloorIntent& OutIntent,
		FString& OutError,
		const FEFCalystoDirectorIntent& DirectorIntent = FEFCalystoDirectorIntent(),
		const FEFCalystoFloorOutcome& Outcome = FEFCalystoFloorOutcome())
	{
		return UEFCalystoDungeonSubsystem::ResolveFloorIntentForTesting(
			Policy,
			Context,
			Ecology,
			DirectorIntent,
			Outcome,
			OutIntent,
			OutError);
	}

	static int32 DirectiveCount(const FEFCalystoResolvedFloorIntent& Intent, const EEFCalystoSpawnCategory Category)
	{
		int32 Count = 0;
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			if (Directive.Category == Category)
			{
				Count += Directive.Count;
			}
		}
		return Count;
	}

	static int32 TotalActorCount(const FEFCalystoResolvedFloorIntent& Intent)
	{
		return Intent.EnemyCount
			+ Intent.FoodCount
			+ Intent.ChestCount
			+ Intent.LootCount
			+ Intent.SpecialEventCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3DefaultPolicyTest,
	"NoShellForWinter.CalystoDungeon.V3.Policy.DefaultContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3DefaultPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakeDraftPolicy();
	TestNotNull(TEXT("The native V3 policy is constructible."), Policy);
	if (!Policy)
	{
		return false;
	}

	FString Error;
	TestFalse(TEXT("An uncertified native V3 draft fails closed."),
		UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Policy, Error));
	TestEqual(TEXT("The draft fails for the explicit missing-certification gate."), Error,
		FString(TEXT("At least one validated dungeon size is required.")));
	TestTrue(TEXT("The native CDO never claims a certified dungeon size."),
		Policy->ValidatedDungeonSizes.IsEmpty());
	TestEqual(TEXT("SchemaVersion is three."), Policy->SchemaVersion, 3);
	TestEqual(TEXT("GeneratorVersion is three."), Policy->GeneratorVersion, 3);
	TestEqual(TEXT("Dungeon minimum is 18."), Policy->Limits.MinDungeonEdge, 18);
	TestEqual(TEXT("Dungeon maximum is 30."), Policy->Limits.MaxDungeonEdge, 30);
	TestEqual(TEXT("Enemy cap is 25."), Policy->Limits.MaxEnemies, 25);
	TestEqual(TEXT("Food cap is 8."), Policy->Limits.MaxFood, 8);
	TestEqual(TEXT("Chest cap is 3."), Policy->Limits.MaxChests, 3);
	TestEqual(TEXT("Director actor cap is 36."), Policy->Limits.MaxDirectorActors, 36);
	TestTrue(TEXT("Threat budget saturates from 6 to 50."),
		FMath::IsNearlyEqual(Policy->Progression.StartThreatBudget, 6.0f)
		&& FMath::IsNearlyEqual(Policy->Progression.EndThreatBudget, 50.0f));
	TestTrue(TEXT("Candidate density default is PERT 0.25/0.32/0.45."),
		FMath::IsNearlyEqual(Policy->Progression.CandidateAnchorDensity.Min, 0.25f)
		&& FMath::IsNearlyEqual(Policy->Progression.CandidateAnchorDensity.Mode, 0.32f)
		&& FMath::IsNearlyEqual(Policy->Progression.CandidateAnchorDensity.Max, 0.45f));
	TestEqual(TEXT("The audited enemy catalog starts with 16 entries."), Policy->EnemyCatalog.Num(), 16);
	TestEqual(TEXT("The initial food catalog has Apple, Bread, Cooked Meat and Water."), Policy->FoodCatalog.Num(), 4);
	TestEqual(TEXT("The initial chest catalog has Locked and LockPick chests."), Policy->ChestCatalog.Num(), 2);
	TestTrue(TEXT("Rare loot has an explicit initial catalog entry."), !Policy->LootCatalog.IsEmpty());
	if (!Policy->LootCatalog.IsEmpty())
	{
		TestTrue(TEXT("Rare loot carries authored rarity and cooldown memory."),
			FMath::IsNearlyEqual(Policy->LootCatalog[0].Rarity, 1.0f)
			&& Policy->LootCatalog[0].CooldownFloors == 4);
	}
	TestEqual(TEXT("Forge and Shrine are the initial Calysto-compatible theme topology."), Policy->ThemeCatalog.Num(), 2);

	int32 DisabledDummyCount = 0;
	for (const FEFCalystoPopulationCatalogEntry& Entry : Policy->EnemyCatalog)
	{
		if (Entry.StableId.ToString().Contains(TEXT("Dummy")))
		{
			++DisabledDummyCount;
			TestFalse(*FString::Printf(TEXT("%s remains disabled until PIE calibration."), *Entry.StableId.ToString()), Entry.bEnabled);
		}
	}
	TestEqual(TEXT("Both Dummy and Ambush variants for both bodies are present but disabled."), DisabledDummyCount, 4);

	const FString DraftHash = Policy->GetPolicyHash();
	const TArray<int32> FixtureSizes = {25};
	const FString CandidateHash = Policy->GetPolicyHashWithValidatedDungeonSizes(FixtureSizes);
	UEFCalystoDungeonDirectorPolicy* Fixture = MakePolicyFixture(FixtureSizes);
	TestEqual(TEXT("Transient candidate hashing matches the exact post-promotion fixture hash."),
		CandidateHash, UEFCalystoDungeonSubsystem::ComputePolicyHash(Fixture));
	TestNotEqual(TEXT("Allowlist membership participates in PolicyHash."), CandidateHash, DraftHash);
	if (CandidateHash.Len() != 64)
	{
		AddError(TEXT("A valid candidate policy must produce a SHA-256 hash."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3PolicyValidationAndHashTest,
	"NoShellForWinter.CalystoDungeon.V3.Policy.ValidationAndCanonicalHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3PolicyValidationAndHashTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	TestEqual(
		TEXT("The project-owned SHA-256 implementation matches the FIPS 180-4 abc vector."),
		UEFCalystoDungeonSubsystem::ComputeCanonicalHash(TEXT("abc")),
		FString(TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")));
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicyFixture();
	if (!Policy)
	{
		return false;
	}

	const FString BaselineHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	TestEqual(TEXT("PolicyHash is a SHA-256 string."), BaselineHash.Len(), 64);

	UEFCalystoDungeonDirectorPolicy* Reordered = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Algo::Reverse(Reordered->EnemyCatalog);
	Algo::Reverse(Reordered->FoodCatalog);
	Algo::Reverse(Reordered->Styles);
	Algo::Reverse(Reordered->ValidatedDungeonSizes);
	TestEqual(TEXT("Physical array order does not change the canonical PolicyHash."),
		UEFCalystoDungeonSubsystem::ComputePolicyHash(Reordered), BaselineHash);

	auto TestHashMutation = [this, Policy, &BaselineHash](const TCHAR* Label, auto Mutator)
	{
		UEFCalystoDungeonDirectorPolicy* Mutated = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
		Mutator(*Mutated);
		TestNotEqual(Label, UEFCalystoDungeonSubsystem::ComputePolicyHash(Mutated), BaselineHash);
	};
	TestHashMutation(TEXT("Distribution edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.Progression.CandidateAnchorDensity.Mode += 0.01f; });
	TestHashMutation(TEXT("Ecology edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.Ecology.JitterWeight -= 0.01f; P.Ecology.SmoothNoiseWeight += 0.01f; });
	TestHashMutation(TEXT("Adaptation edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.Adaptation.EMAAlpha += 0.01f; });
	TestHashMutation(TEXT("Catalog cost edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.EnemyCatalog[2].Cost += 0.25f; });
	TestHashMutation(TEXT("Threat-budget range edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.Progression.ThreatBudgetRelativeRange += 0.01f; });
	TestHashMutation(TEXT("Pacing-domain edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.Progression.PacingAmplitude += 0.01f; });
	TestHashMutation(TEXT("Rarity edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.LootCatalog[0].Rarity -= 0.01f; });
	TestHashMutation(TEXT("Cooldown edits change PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { ++P.LootCatalog[0].CooldownFloors; });
	TestHashMutation(TEXT("Validated-size membership changes PolicyHash."), [](UEFCalystoDungeonDirectorPolicy& P) { P.ValidatedDungeonSizes.Add(26); });

	FString Error;
	UEFCalystoDungeonDirectorPolicy* Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->Progression.CandidateAnchorDensity.Mode = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN fails policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->Progression.CandidateAnchorDensity.Mode = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinity fails policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->Progression.FoodCount.Min = 4;
	Invalid->Progression.FoodCount.Max = 1;
	TestFalse(TEXT("Inverted distributions fail policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->FoodCatalog[1].StableId = Invalid->FoodCatalog[0].StableId;
	TestFalse(TEXT("Duplicate stable IDs fail policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->FoodCatalog[0].ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/_Missing/V3Invalid.V3Invalid_C")));
	TestFalse(TEXT("Missing catalog assets fail policy validation without becoming runtime fallbacks."),
		UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->FoodCatalog[0].ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(
		TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.DoesNotExist_C")));
	TestFalse(TEXT("A valid package with a nonexistent generated class object fails policy validation."),
		UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->EnemyCatalog[2].ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(
		TEXT("/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C")));
	TestFalse(TEXT("Enemy catalog entries must resolve to Pawn-derived Blueprint classes."),
		UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->Limits.MaxDirectorActors = Invalid->Limits.MaxEnemies - 1;
	TestFalse(TEXT("The total Director actor cap cannot be smaller than the enemy cap."),
		UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->LootCatalog[0].Rarity = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN rarity fails policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->LootCatalog[0].CooldownFloors = -1;
	TestFalse(TEXT("Negative cooldown memory fails policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));

	Invalid = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	Invalid->Progression.PacingAmplitude = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN pacing amplitude fails policy validation."), UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Invalid, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3DeterministicDomainsTest,
	"NoShellForWinter.CalystoDungeon.V3.Determinism.CounterDomains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3DeterministicDomainsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	constexpr int32 SampleCount = 1000000;
	constexpr int64 RunSeed = 0x123456789LL;
	TSet<int32> Seen;
	Seen.Reserve(SampleCount);
	for (int64 Serial = 1; Serial <= SampleCount; ++Serial)
	{
		const int32 Seed = EFCalystoDungeonDeterminism::DerivePCGSeed(
			RunSeed, 17, Serial, 3, TEXT("POLICY"), TEXT("ECOLOGY"));
		if (Seed <= 0 || Seen.Contains(Seed))
		{
			AddError(FString::Printf(TEXT("PCG seed collision/invalid value at serial %lld."), Serial));
			break;
		}
		Seen.Add(Seed);
	}
	TestEqual(TEXT("The first million generation serials have unique positive PCG seeds."), Seen.Num(), SampleCount);
	TestEqual(TEXT("The representable PCG permutation fails closed after int32 serial space."),
		EFCalystoDungeonDeterminism::DerivePCGSeed(
			RunSeed, 17, 2147483648LL, 3, TEXT("POLICY"), TEXT("ECOLOGY")), 0);

	const int32 BaselinePCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
		42, 17, 91, 3, TEXT("POLICY"), TEXT("ECOLOGY"));
	TestEqual(TEXT("The complete PCG seed tuple replays exactly."), BaselinePCGSeed,
		EFCalystoDungeonDeterminism::DerivePCGSeed(42, 17, 91, 3, TEXT("POLICY"), TEXT("ECOLOGY")));
	TestNotEqual(TEXT("FloorNumber separates PCG seed streams."), BaselinePCGSeed,
		EFCalystoDungeonDeterminism::DerivePCGSeed(42, 18, 91, 3, TEXT("POLICY"), TEXT("ECOLOGY")));
	TestNotEqual(TEXT("PolicyHash separates PCG seed streams."), BaselinePCGSeed,
		EFCalystoDungeonDeterminism::DerivePCGSeed(42, 17, 91, 3, TEXT("POLICY_B"), TEXT("ECOLOGY")));
	TestNotEqual(TEXT("EcologyHash separates PCG seed streams."), BaselinePCGSeed,
		EFCalystoDungeonDeterminism::DerivePCGSeed(42, 17, 91, 3, TEXT("POLICY"), TEXT("ECOLOGY_B")));

	const FEFCalystoDungeonGenerationContext Context = MakeContext(
		42, 17, 91, TEXT("POLICY"), TEXT("ECOLOGY"));
	TSet<uint64> DomainValues;
	for (uint64 Domain = 1; Domain <= 13; ++Domain)
	{
		const uint64 Value = EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), Domain, 7, 2);
		TestEqual(TEXT("A counter-based draw replays exactly."), Value,
			EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), Domain, 7, 2));
		DomainValues.Add(Value);
	}
	TestEqual(TEXT("All thirteen tested domains remain separated."), DomainValues.Num(), 13);
	TestNotEqual(TEXT("StableEntityId creates an independent substream."),
		EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), 1, 7, 2),
		EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), 1, 8, 2));
	TestNotEqual(TEXT("DrawIndex creates an independent counter draw."),
		EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), 1, 7, 2),
		EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), 1, 7, 3));
	TSet<uint64> NamedDomainValues;
	const uint64 NamedDomains[] = {
		EFCalystoDungeonDomains::ThreatBudget,
		EFCalystoDungeonDomains::Pacing,
		EFCalystoDungeonDomains::Rarity};
	for (const uint64 Domain : NamedDomains)
	{
		const uint64 Value = EFCalystoDungeonDeterminism::DeriveDomainValue(
			Context, 3, TEXT("ECOLOGY"), Domain, 0, 0);
		TestEqual(TEXT("Each named domain replays exactly."), Value,
			EFCalystoDungeonDeterminism::DeriveDomainValue(Context, 3, TEXT("ECOLOGY"), Domain, 0, 0));
		NamedDomainValues.Add(Value);
	}
	TestEqual(TEXT("ThreatBudget, Pacing, and Rarity use independent domain streams."), NamedDomainValues.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3ProbabilityTest,
	"NoShellForWinter.CalystoDungeon.V3.Probability.PERTAndBernoulli",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3ProbabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	const FEFCalystoDungeonGenerationContext BaseContext = MakeContext(
		99887766, 12, 1, TEXT("POLICY"), TEXT("ECOLOGY"));
	constexpr int32 SampleCount = 100000;
	constexpr double Probability = 0.37;
	int32 PresentCount = 0;
	double FloatTotal = 0.0;
	FEFCalystoFloatDistribution FloatDistribution;
	FloatDistribution.Min = 0.0f;
	FloatDistribution.Mode = 5.0f;
	FloatDistribution.Max = 10.0f;
	FloatDistribution.Concentration = 4.0f;
	FEFCalystoIntDistribution IntDistribution;
	IntDistribution.Min = 1;
	IntDistribution.Mode = 3;
	IntDistribution.Max = 7;
	IntDistribution.Concentration = 4.0f;

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		FEFCalystoDungeonGenerationContext Context = BaseContext;
		Context.GenerationSerial = Index + 1;
		Context.PCGSeed = EFCalystoDungeonDeterminism::DerivePCGSeed(
			Context.RunSeed,
			Context.FloorNumber,
			Context.GenerationSerial,
			3,
			Context.PolicyHash,
			TEXT("ECOLOGY"));
		PresentCount += EFCalystoDungeonDeterminism::Bernoulli(Probability, Context, 3, TEXT("ECOLOGY"), 0xB001ULL) ? 1 : 0;
		const float FloatSample = EFCalystoDungeonDeterminism::SamplePERT(FloatDistribution, 0.5f, Context, 3, TEXT("ECOLOGY"), 0xB002ULL);
		const int32 IntSample = EFCalystoDungeonDeterminism::SamplePERT(IntDistribution, 0.5f, Context, 3, TEXT("ECOLOGY"), 0xB003ULL);
		if (FloatSample < FloatDistribution.Min || FloatSample > FloatDistribution.Max)
		{
			AddError(TEXT("Float PERT escaped its range."));
			break;
		}
		if (IntSample < IntDistribution.Min || IntSample > IntDistribution.Max)
		{
			AddError(TEXT("Integer PERT escaped its range."));
			break;
		}
		FloatTotal += FloatSample;
	}

	const double Expected = Probability * SampleCount;
	const double Sigma = FMath::Sqrt(SampleCount * Probability * (1.0 - Probability));
	TestTrue(TEXT("Bernoulli frequency remains within six sigma."), FMath::Abs(PresentCount - Expected) <= 6.0 * Sigma + 1.0);
	TestTrue(TEXT("Symmetric PERT mean stays near its mode."), FMath::Abs(FloatTotal / SampleCount - 5.0) < 0.08);
	TestTrue(TEXT("Floor 1 progression begins at zero."), FMath::IsNearlyZero(EFCalystoDungeonDeterminism::Progression(1, 12.0)));
	TestTrue(TEXT("Progression saturates without exceeding one."),
		EFCalystoDungeonDeterminism::Progression(1000, 12.0) > 0.999
		&& EFCalystoDungeonDeterminism::Progression(1000, 12.0) <= 1.0);
	TestTrue(TEXT("Authored concentration 4 maps volatility 0/0.5/1 to 8/4/2."),
		FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(4.0f, 0.0f), 8.0f)
		&& FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(4.0f, 0.5f), 4.0f)
		&& FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(4.0f, 1.0f), 2.0f));
	TestTrue(TEXT("Authored concentration 8 maps volatility 0/0.5/1 to 8/8/2."),
		FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(8.0f, 0.0f), 8.0f)
		&& FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(8.0f, 0.5f), 8.0f)
		&& FMath::IsNearlyEqual(EFCalystoDungeonDeterminism::EffectivePERTConcentration(8.0f, 1.0f), 2.0f));
	TestEqual(TEXT("Invalid authored concentration fails closed."),
		EFCalystoDungeonDeterminism::EffectivePERTConcentration(
			std::numeric_limits<float>::quiet_NaN(), 0.5f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3ResolverTest,
	"NoShellForWinter.CalystoDungeon.V3.Director.ResolveReplayRerollAndCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3ResolverTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicyFixture();
	if (!Policy)
	{
		return false;
	}
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	const FEFCalystoRunEcologyState Ecology = UEFCalystoDungeonSubsystem::BuildInitialEcology(2026081401, PolicyHash, 3);
	const FEFCalystoDungeonGenerationContext Context = MakeContext(
		2026081401, 1, 1, PolicyHash, Ecology.EcologyHash);
	FEFCalystoResolvedFloorIntent First;
	FEFCalystoResolvedFloorIntent Replay;
	FString Error;
	TestTrue(TEXT("A default Floor 1 intent resolves."), Resolve(Policy, Context, Ecology, First, Error));
	TestTrue(TEXT("The same frozen inputs replay exactly."), Resolve(Policy, Context, Ecology, Replay, Error));
	if (!First.bIsValid || !Replay.bIsValid)
	{
		AddError(FString::Printf(TEXT("Unexpected resolver failure: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("Replay preserves IntentHash."), Replay.IntentHash, First.IntentHash);
	TestEqual(TEXT("Replay preserves PCG seed."), Replay.PCGSeed, First.PCGSeed);
	TestEqual(TEXT("Replay preserves exact counts."), TotalActorCount(Replay), TotalActorCount(First));
	TestTrue(TEXT("Resolved size is square and Z is one."), First.DungeonSize.X == First.DungeonSize.Y && First.DungeonSize.Z == 1);
	TestTrue(TEXT("Resolved size is in the validated allowlist."), Policy->ValidatedDungeonSizes.Contains(First.DungeonSize.X));
	TestTrue(TEXT("Candidate anchors never use zero density."), First.CandidateAnchorDensity >= 0.20f && First.CandidateAnchorDensity <= 0.50f);
	TestTrue(TEXT("Side paths remain in the Shipping interval."), First.SidePathChance >= 0.30f && First.SidePathChance <= 0.70f);
	TestTrue(TEXT("Enemy count respects cap 25."), First.EnemyCount >= 0 && First.EnemyCount <= 25);
	TestTrue(TEXT("Food count respects cap 8."), First.FoodCount >= 0 && First.FoodCount <= 8);
	TestTrue(TEXT("Chest count respects cap 3."), First.ChestCount >= 0 && First.ChestCount <= 3);
	TestEqual(TEXT("Loot remains absent before any catalog entry becomes floor-eligible."), First.LootCount, 0);
	TestTrue(TEXT("Total Director population respects cap 36."), TotalActorCount(First) <= 36);
	TestEqual(TEXT("Enemy directives match the frozen count."), DirectiveCount(First, EEFCalystoSpawnCategory::Enemy), First.EnemyCount);
	TestEqual(TEXT("Food directives match the frozen count."), DirectiveCount(First, EEFCalystoSpawnCategory::Food), First.FoodCount);
	TestEqual(TEXT("Chest directives match the frozen count."), DirectiveCount(First, EEFCalystoSpawnCategory::Chest), First.ChestCount);
	TestEqual(TEXT("Loot directives match the frozen count."), DirectiveCount(First, EEFCalystoSpawnCategory::Loot), First.LootCount);

	UEFCalystoDungeonDirectorPolicy* LootCapacityPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	LootCapacityPolicy->Progression.StartLootPresence = 1.0f;
	LootCapacityPolicy->Progression.EndLootPresence = 1.0f;
	LootCapacityPolicy->Progression.LootCount.Min = 2;
	LootCapacityPolicy->Progression.LootCount.Mode = 2;
	LootCapacityPolicy->Progression.LootCount.Max = 2;
	const FString LootCapacityHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(LootCapacityPolicy);
	const FEFCalystoRunEcologyState LootCapacityEcology =
		UEFCalystoDungeonSubsystem::BuildInitialEcology(404, LootCapacityHash, 3);
	FEFCalystoResolvedFloorIntent LootCapacityIntent;
	TestTrue(TEXT("An eligible loot roll resolves when requested count exceeds current catalog capacity."),
		Resolve(LootCapacityPolicy, MakeContext(
			404, 4, 1, LootCapacityHash, LootCapacityEcology.EcologyHash),
			LootCapacityEcology, LootCapacityIntent, Error));
	TestEqual(TEXT("Resolved loot count is capped to eligible per-floor catalog capacity."),
		LootCapacityIntent.LootCount, 1);

	const FEFCalystoDungeonGenerationContext RerollContext = MakeContext(
		2026081401, 1, 2, PolicyHash, Ecology.EcologyHash);
	FEFCalystoResolvedFloorIntent Reroll;
	TestTrue(TEXT("Reroll resolves with the same confirmed ecology."), Resolve(Policy, RerollContext, Ecology, Reroll, Error));
	TestNotEqual(TEXT("Reroll changes IntentHash."), Reroll.IntentHash, First.IntentHash);
	TestNotEqual(TEXT("Reroll changes PCG seed."), Reroll.PCGSeed, First.PCGSeed);

	UEFCalystoDungeonDirectorPolicy* ZeroPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	ZeroPolicy->Progression.StartEnemyPresence = 0.0f;
	ZeroPolicy->Progression.EndEnemyPresence = 0.0f;
	const FString ZeroHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(ZeroPolicy);
	const FEFCalystoRunEcologyState ZeroEcology = UEFCalystoDungeonSubsystem::BuildInitialEcology(88, ZeroHash, 3);
	FEFCalystoResolvedFloorIntent ZeroIntent;
	TestTrue(TEXT("A zero-enemy floor remains a valid structural generation."),
		Resolve(ZeroPolicy, MakeContext(88, 1, 1, ZeroHash, ZeroEcology.EcologyHash),
			ZeroEcology, ZeroIntent, Error));
	TestEqual(TEXT("Zero enemy presence produces zero enemies."), ZeroIntent.EnemyCount, 0);
	TestTrue(TEXT("Zero enemies still preserve candidate anchors."), ZeroIntent.CandidateAnchorDensity >= 0.20f);

	UEFCalystoDungeonDirectorPolicy* CapPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	CapPolicy->Progression.StartEnemyPresence = 1.0f;
	CapPolicy->Progression.EndEnemyPresence = 1.0f;
	CapPolicy->Progression.StartEnemyCountMode = 25.0f;
	CapPolicy->Progression.EndEnemyCountMode = 25.0f;
	CapPolicy->Progression.EnemyCountLowerOffset = 0.0f;
	CapPolicy->Progression.EnemyCountUpperOffset = 0.0f;
	CapPolicy->Progression.StartThreatBudget = 50.0f;
	CapPolicy->Progression.EndThreatBudget = 50.0f;
	CapPolicy->Progression.ThreatBudgetRelativeRange = 0.0f;
	CapPolicy->Progression.PacingAmplitude = 0.0f;
	CapPolicy->Ecology.RunDNAWeight = 1.0f;
	CapPolicy->Ecology.SmoothNoiseWeight = 0.0f;
	CapPolicy->Ecology.JitterWeight = 0.0f;
	CapPolicy->Progression.StartFoodPresence = 0.0f;
	CapPolicy->Progression.EndFoodPresence = 0.0f;
	CapPolicy->Progression.StartChestPresence = 0.0f;
	CapPolicy->Progression.EndChestPresence = 0.0f;
	CapPolicy->Progression.StartLootPresence = 0.0f;
	CapPolicy->Progression.EndLootPresence = 0.0f;
	CapPolicy->Progression.StartSpecialEventPresence = 0.0f;
	CapPolicy->Progression.EndSpecialEventPresence = 0.0f;
	for (FEFCalystoPopulationCatalogEntry& Entry : CapPolicy->EnemyCatalog)
	{
		if (Entry.bEnabled)
		{
			Entry.Cost = 1.0f;
			Entry.MaxPerFloor = 25;
		}
	}
	for (FEFCalystoStylePolicy& Style : CapPolicy->Styles)
	{
		Style.ThreatBias = 0.0f;
	}
	const FString CapHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(CapPolicy);
	FEFCalystoRunEcologyState CapEcology = UEFCalystoDungeonSubsystem::BuildInitialEcology(99, CapHash, 3);
	CapEcology.Threat = 0.0f;
	CapEcology.EcologyHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(CapEcology);
	FEFCalystoResolvedFloorIntent CapIntent;
	TestTrue(TEXT("The exact cap-25 scenario resolves."),
		Resolve(CapPolicy, MakeContext(99, 100, 1, CapHash, CapEcology.EcologyHash),
			CapEcology, CapIntent, Error));
	TestEqual(TEXT("The Director can intentionally realize exactly 25 enemies without exceeding it."), CapIntent.EnemyCount, 25);
	TestTrue(TEXT("Threat budget never exceeds its authored saturation cap."),
		CapIntent.ThreatBudget <= CapPolicy->Progression.EndThreatBudget);
	float RealizedEnemyCost = 0.0f;
	for (const FEFCalystoSpawnDirective& Directive : CapIntent.SpawnDirectives)
	{
		if (Directive.Category != EEFCalystoSpawnCategory::Enemy)
		{
			continue;
		}
		const FEFCalystoPopulationCatalogEntry* CatalogEntry = CapPolicy->EnemyCatalog.FindByPredicate(
			[&Directive](const FEFCalystoPopulationCatalogEntry& Entry)
			{
				return Entry.StableId == Directive.StableId;
			});
		TestNotNull(TEXT("Every enemy directive resolves to one stable catalog entry."), CatalogEntry);
		if (CatalogEntry)
		{
			TestTrue(TEXT("Every enemy directive respects its per-class cap."), Directive.Count <= CatalogEntry->MaxPerFloor);
			RealizedEnemyCost += Directive.Count * CatalogEntry->Cost;
		}
	}
	TestTrue(TEXT("Enemy directive costs remain inside the frozen threat budget."),
		RealizedEnemyCost <= CapIntent.ThreatBudget + KINDA_SMALL_NUMBER);

	UEFCalystoDungeonDirectorPolicy* BudgetPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(CapPolicy, GetTransientPackage());
	for (FEFCalystoPopulationCatalogEntry& Entry : BudgetPolicy->EnemyCatalog)
	{
		if (Entry.bEnabled)
		{
			Entry.Cost = 3.0f;
		}
	}
	const FString BudgetHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(BudgetPolicy);
	const FEFCalystoRunEcologyState BudgetEcology =
		UEFCalystoDungeonSubsystem::BuildInitialEcology(100, BudgetHash, 3);
	FEFCalystoResolvedFloorIntent BudgetIntent;
	TestTrue(TEXT("An unaffordable requested count is deterministically reduced instead of raising the budget cap."),
		Resolve(BudgetPolicy, MakeContext(100, 100, 1, BudgetHash, BudgetEcology.EcologyHash),
			BudgetEcology, BudgetIntent, Error));
	TestEqual(TEXT("A threat cap of 50 funds at most sixteen cost-three enemies."), BudgetIntent.EnemyCount, 16);
	TestTrue(TEXT("Reduced composition still cannot exceed EndThreatBudget."),
		BudgetIntent.ThreatBudget <= BudgetPolicy->Progression.EndThreatBudget);

	UEFCalystoDungeonDirectorPolicy* ResourceCapPolicy = DuplicateObject<UEFCalystoDungeonDirectorPolicy>(Policy, GetTransientPackage());
	ResourceCapPolicy->Progression.StartEnemyPresence = 0.0f;
	ResourceCapPolicy->Progression.EndEnemyPresence = 0.0f;
	ResourceCapPolicy->Progression.StartFoodPresence = 1.0f;
	ResourceCapPolicy->Progression.EndFoodPresence = 1.0f;
	ResourceCapPolicy->Progression.FoodCount.Min = 8;
	ResourceCapPolicy->Progression.FoodCount.Mode = 8;
	ResourceCapPolicy->Progression.FoodCount.Max = 8;
	ResourceCapPolicy->Progression.StartChestPresence = 1.0f;
	ResourceCapPolicy->Progression.EndChestPresence = 1.0f;
	ResourceCapPolicy->Progression.ChestCount.Min = 3;
	ResourceCapPolicy->Progression.ChestCount.Mode = 3;
	ResourceCapPolicy->Progression.ChestCount.Max = 3;
	ResourceCapPolicy->Progression.StartLootPresence = 0.0f;
	ResourceCapPolicy->Progression.EndLootPresence = 0.0f;
	ResourceCapPolicy->Progression.StartSpecialEventPresence = 0.0f;
	ResourceCapPolicy->Progression.EndSpecialEventPresence = 0.0f;
	const FString ResourceCapHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(ResourceCapPolicy);
	const FEFCalystoRunEcologyState ResourceCapEcology =
		UEFCalystoDungeonSubsystem::BuildInitialEcology(8083, ResourceCapHash, 3);
	FEFCalystoResolvedFloorIntent ResourceCapIntent;
	TestTrue(TEXT("The exact food-eight/chest-three scenario resolves."),
		Resolve(ResourceCapPolicy, MakeContext(8083, 100, 1, ResourceCapHash, ResourceCapEcology.EcologyHash),
			ResourceCapEcology, ResourceCapIntent, Error));
	TestEqual(TEXT("The Director can intentionally realize exactly eight food actors."), ResourceCapIntent.FoodCount, 8);
	TestEqual(TEXT("The Director can intentionally realize exactly three chests."), ResourceCapIntent.ChestCount, 3);
	TestTrue(TEXT("Exact resource maxima still respect the total Director actor cap."), TotalActorCount(ResourceCapIntent) <= 36);

	int32 ResolvedDepthFloors = 0;
	for (int64 Floor = 1; Floor <= 1000; ++Floor)
	{
		FEFCalystoResolvedFloorIntent DepthIntent;
		if (!Resolve(Policy, MakeContext(9090, Floor, Floor, PolicyHash, Ecology.EcologyHash),
			Ecology, DepthIntent, Error))
		{
			AddError(FString::Printf(TEXT("Depth stress failed at Floor %lld: %s"), Floor, *Error));
			break;
		}
		if (DepthIntent.FloorNumber != Floor || DepthIntent.DungeonSize.X < 18 || DepthIntent.DungeonSize.X > 30
			|| DepthIntent.EnemyCount < 0 || DepthIntent.EnemyCount > 25 || TotalActorCount(DepthIntent) > 36)
		{
			AddError(FString::Printf(TEXT("Depth stress produced an invalid capped intent at Floor %lld."), Floor));
			break;
		}
		++ResolvedDepthFloors;
	}
	TestEqual(TEXT("Floors 1-1000 resolve without overflow or illegal caps."), ResolvedDepthFloors, 1000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3EcologyTest,
	"NoShellForWinter.CalystoDungeon.V3.Ecology.AdaptationPityAndSingleCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3EcologyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicyFixture();
	if (!Policy)
	{
		return false;
	}
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	const int64 RunSeed = 12345;
	FEFCalystoRunEcologyState Initial = UEFCalystoDungeonSubsystem::BuildInitialEcology(RunSeed, PolicyHash, 3);
	FEFCalystoResolvedFloorIntent Completed;
	FString Error;
	TestTrue(TEXT("A completed intent can be prepared for ecology tests."),
		Resolve(Policy, MakeContext(RunSeed, 1, 1, PolicyHash, Initial.EcologyHash),
			Initial, Completed, Error));

	FEFCalystoFloorOutcome Thriving;
	Thriving.bIsValid = true;
	Thriving.Combat = 1.0f;
	Thriving.Survival = 1.0f;
	Thriving.Resources = 1.0f;
	Thriving.Pace = 1.0f;
	FEFCalystoRunEcologyState ThrivingEcology = Initial;
	TestTrue(TEXT("Advance commits a thriving outcome once."),
		UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(Policy, 1, Completed, Thriving, ThrivingEcology, Error));
	TestTrue(TEXT("Thriving raises the EMA above neutral."), ThrivingEcology.PerformanceEMA > 0.5f);
	const int64 RevisionAfterFirstCommit = ThrivingEcology.Revision;
	TestFalse(TEXT("Retry/double Advance cannot commit the same floor twice."),
		UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(Policy, 1, Completed, Thriving, ThrivingEcology, Error));
	TestEqual(TEXT("Rejected double commit preserves ecology revision."), ThrivingEcology.Revision, RevisionAfterFirstCommit);

	FEFCalystoFloorOutcome Struggling;
	Struggling.bIsValid = true;
	Struggling.Combat = 0.0f;
	Struggling.Survival = 0.0f;
	Struggling.Resources = 0.0f;
	Struggling.Pace = 0.0f;
	Struggling.Deaths = 2;
	Struggling.Failures = 2;
	FEFCalystoRunEcologyState StrugglingEcology = Initial;
	TestTrue(TEXT("Advance commits a struggling outcome once."),
		UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(Policy, 1, Completed, Struggling, StrugglingEcology, Error));
	TestTrue(TEXT("Struggling lowers the EMA below neutral."), StrugglingEcology.PerformanceEMA < 0.5f);

	FEFCalystoRunEcologyState PityEcology = Initial;
	PityEcology.ConsecutiveFloorsWithoutFood = Policy->Ecology.FoodPityAfterEmptyFloors;
	PityEcology.ConsecutiveFloorsWithoutChest = Policy->Ecology.ChestPityAfterEmptyFloors;
	PityEcology.EcologyHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(PityEcology);
	FEFCalystoResolvedFloorIntent PityIntent;
	TestTrue(TEXT("A pity-state floor resolves."),
		Resolve(Policy, MakeContext(RunSeed, 8, 2, PolicyHash, PityEcology.EcologyHash),
			PityEcology, PityIntent, Error));
	TestTrue(TEXT("Food pity guarantees at least one food actor."), PityIntent.FoodCount >= 1);
	TestTrue(TEXT("Chest pity guarantees at least one chest actor."), PityIntent.ChestCount >= 1);

	FEFCalystoRunEcologyState AntiStreakEcology = Initial;
	AntiStreakEcology.RecentStyles = {
		EEFCalystoDungeonStyle::Standard,
		EEFCalystoDungeonStyle::Standard};
	AntiStreakEcology.RecentDominantThemes = {
		FName(TEXT("Forge")),
		FName(TEXT("Forge")),
		FName(TEXT("Forge"))};
	AntiStreakEcology.EcologyHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(AntiStreakEcology);
	FEFCalystoDirectorIntent PreferredRepeat;
	PreferredRepeat.PreferredStyle = EEFCalystoDungeonStyle::Standard;
	PreferredRepeat.ThemeBias = -1.0f;
	FEFCalystoResolvedFloorIntent AntiStreakIntent;
	TestTrue(TEXT("A maxed anti-streak ecology still resolves."),
		Resolve(Policy, MakeContext(RunSeed, 9, 3, PolicyHash, AntiStreakEcology.EcologyHash),
			AntiStreakEcology, AntiStreakIntent, Error, PreferredRepeat));
	TestNotEqual(TEXT("Two accepted Standard floors force a different next style despite preference."),
		AntiStreakIntent.Style, EEFCalystoDungeonStyle::Standard);
	TestNotEqual(TEXT("Three accepted Forge dominances force a different next theme despite bias."),
		AntiStreakIntent.DominantTheme, FName(TEXT("Forge")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3RarityCooldownTest,
	"NoShellForWinter.CalystoDungeon.V3.Ecology.RarityAndCooldownAntiRepeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3RarityCooldownTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicyFixture();
	if (!Policy)
	{
		return false;
	}

	Policy->Progression.StartEnemyPresence = 0.0f;
	Policy->Progression.EndEnemyPresence = 0.0f;
	Policy->Progression.StartFoodPresence = 0.0f;
	Policy->Progression.EndFoodPresence = 0.0f;
	Policy->Progression.StartChestPresence = 0.0f;
	Policy->Progression.EndChestPresence = 0.0f;
	Policy->Progression.StartSpecialEventPresence = 0.0f;
	Policy->Progression.EndSpecialEventPresence = 0.0f;
	Policy->Progression.StartLootPresence = 1.0f;
	Policy->Progression.EndLootPresence = 1.0f;
	Policy->Progression.LootCount.Min = 1;
	Policy->Progression.LootCount.Mode = 1;
	Policy->Progression.LootCount.Max = 1;

	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	FEFCalystoRunEcologyState Ecology = UEFCalystoDungeonSubsystem::BuildInitialEcology(7007, PolicyHash, 3);
	const FEFCalystoDungeonGenerationContext FloorFourContext = MakeContext(
		7007, 4, 1, PolicyHash, Ecology.EcologyHash);
	FEFCalystoResolvedFloorIntent FloorFour;
	FEFCalystoResolvedFloorIntent Replay;
	FString Error;
	TestTrue(TEXT("The first eligible rare-loot floor resolves."),
		Resolve(Policy, FloorFourContext, Ecology, FloorFour, Error));
	TestTrue(TEXT("Rare-loot resolution replays exactly before ecology commit."),
		Resolve(Policy, FloorFourContext, Ecology, Replay, Error));
	TestEqual(TEXT("Rare-loot replay preserves IntentHash."), Replay.IntentHash, FloorFour.IntentHash);
	TestEqual(TEXT("The sole eligible rare-loot entry is selected."), FloorFour.LootCount, 1);
	TestTrue(TEXT("The selected loot directive uses the stable rare ID."),
		FloorFour.SpawnDirectives.ContainsByPredicate([](const FEFCalystoSpawnDirective& Directive)
		{
			return Directive.Category == EEFCalystoSpawnCategory::Loot
				&& Directive.StableId == FName(TEXT("RareAlcohol07"))
				&& Directive.Count == 1;
		}));
	TestTrue(TEXT("Resolve and Replay never commit cooldown memory."), Ecology.PopulationCooldowns.IsEmpty());

	const FString PreCommitEcologyHash = Ecology.EcologyHash;
	TestTrue(TEXT("Advance commit records the selected entry's cooldown memory."),
		UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(
			Policy, 4, FloorFour, FEFCalystoFloorOutcome(), Ecology, Error));
	TestNotEqual(TEXT("Cooldown memory changes EcologyHash."), Ecology.EcologyHash, PreCommitEcologyHash);
	const FEFCalystoPopulationCooldownState* RareCooldown = Ecology.PopulationCooldowns.FindByPredicate(
		[](const FEFCalystoPopulationCooldownState& State)
		{
			return State.StableId == FName(TEXT("RareAlcohol07"));
		});
	TestNotNull(TEXT("Committed ecology contains RareAlcohol07 cooldown state."), RareCooldown);
	if (RareCooldown)
	{
		TestEqual(TEXT("Cooldown state records the accepted completed floor."), RareCooldown->LastSelectedFloor, int64(4));
	}

	for (int64 Floor = 5; Floor <= 8; ++Floor)
	{
		FEFCalystoResolvedFloorIntent BlockedIntent;
		const FEFCalystoDungeonGenerationContext BlockedContext = MakeContext(
			7007, Floor, Floor - 3, PolicyHash, Ecology.EcologyHash);
		TestTrue(*FString::Printf(TEXT("Cooldown floor %lld still resolves structurally."), Floor),
			Resolve(Policy, BlockedContext, Ecology, BlockedIntent, Error));
		TestEqual(*FString::Printf(TEXT("Rare loot is blocked on cooldown floor %lld."), Floor),
			BlockedIntent.LootCount, 0);
	}

	FEFCalystoResolvedFloorIntent BlockedReroll;
	TestTrue(TEXT("Reroll resolves against the same confirmed cooldown ecology."),
		Resolve(Policy, MakeContext(7007, 5, 99, PolicyHash, Ecology.EcologyHash),
			Ecology, BlockedReroll, Error));
	TestEqual(TEXT("Reroll cannot bypass committed cooldown memory."), BlockedReroll.LootCount, 0);
	if (!Ecology.PopulationCooldowns.IsEmpty())
	{
		TestEqual(TEXT("Reroll does not mutate the accepted cooldown floor."),
			Ecology.PopulationCooldowns[0].LastSelectedFloor, int64(4));
	}

	FEFCalystoResolvedFloorIntent EligibleAgain;
	TestTrue(TEXT("Rare loot becomes eligible after four complete intervening floors."),
		Resolve(Policy, MakeContext(7007, 9, 6, PolicyHash, Ecology.EcologyHash),
			Ecology, EligibleAgain, Error));
	TestEqual(TEXT("Rare loot returns on its first post-cooldown eligible floor."), EligibleAgain.LootCount, 1);

	FEFCalystoRunEcologyState OrderedA = Ecology;
	FEFCalystoPopulationCooldownState& MageCooldown = OrderedA.PopulationCooldowns.AddDefaulted_GetRef();
	MageCooldown.StableId = TEXT("FemaleMage");
	MageCooldown.LastSelectedFloor = 4;
	const FString OrderedHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(OrderedA);
	FEFCalystoRunEcologyState OrderedB = OrderedA;
	Algo::Reverse(OrderedB.PopulationCooldowns);
	TestEqual(TEXT("Cooldown storage order does not change canonical EcologyHash."),
		UEFCalystoDungeonSubsystem::ComputeEcologyHash(OrderedB), OrderedHash);

	FEFCalystoRunEcologyState DuplicateCooldown = OrderedA;
	const FEFCalystoPopulationCooldownState DuplicateState = DuplicateCooldown.PopulationCooldowns[0];
	DuplicateCooldown.PopulationCooldowns.Add(DuplicateState);
	DuplicateCooldown.EcologyHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(DuplicateCooldown);
	FEFCalystoResolvedFloorIntent Rejected;
	TestFalse(TEXT("Duplicate cooldown IDs fail closed during resolution."),
		Resolve(Policy, MakeContext(7007, 9, 7, PolicyHash, DuplicateCooldown.EcologyHash),
			DuplicateCooldown, Rejected, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3RarityCompositionTest,
	"NoShellForWinter.CalystoDungeon.V3.Director.DeterministicRarityComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3RarityCompositionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicyFixture();
	if (!Policy)
	{
		return false;
	}

	Policy->Progression.StartEnemyPresence = 1.0f;
	Policy->Progression.EndEnemyPresence = 1.0f;
	Policy->Progression.StartEnemyCountMode = 1.0f;
	Policy->Progression.EndEnemyCountMode = 1.0f;
	Policy->Progression.EnemyCountLowerOffset = 0.0f;
	Policy->Progression.EnemyCountUpperOffset = 0.0f;
	Policy->Progression.StartThreatBudget = 10.0f;
	Policy->Progression.EndThreatBudget = 10.0f;
	Policy->Progression.ThreatBudgetRelativeRange = 0.0f;
	Policy->Progression.PacingAmplitude = 0.0f;
	Policy->Ecology.RunDNAWeight = 1.0f;
	Policy->Ecology.SmoothNoiseWeight = 0.0f;
	Policy->Ecology.JitterWeight = 0.0f;
	Policy->Progression.StartFoodPresence = 0.0f;
	Policy->Progression.EndFoodPresence = 0.0f;
	Policy->Progression.StartChestPresence = 0.0f;
	Policy->Progression.EndChestPresence = 0.0f;
	Policy->Progression.StartLootPresence = 0.0f;
	Policy->Progression.EndLootPresence = 0.0f;
	Policy->Progression.StartSpecialEventPresence = 0.0f;
	Policy->Progression.EndSpecialEventPresence = 0.0f;
	Policy->Ecology.RaritySelectionStrength = 1.0f;
	for (FEFCalystoPopulationCatalogEntry& Entry : Policy->EnemyCatalog)
	{
		const bool bLowRarity = Entry.StableId == FName(TEXT("FemaleMM"));
		const bool bHighRarity = Entry.StableId == FName(TEXT("MaleMM"));
		Entry.bEnabled = bLowRarity || bHighRarity;
		if (Entry.bEnabled)
		{
			Entry.BaseWeight = 100;
			Entry.Cost = 1.0f;
			Entry.MinimumFloor = 1;
			Entry.MaxPerFloor = 1;
			Entry.CooldownFloors = 0;
			Entry.Rarity = bLowRarity ? 0.0f : 1.0f;
		}
	}
	for (FEFCalystoStylePolicy& Style : Policy->Styles)
	{
		Style.ThreatBias = 0.0f;
	}

	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	TSet<FName> SelectedIds;
	FEFCalystoResolvedFloorIntent First;
	FEFCalystoRunEcologyState FirstEcology;
	FEFCalystoDungeonGenerationContext FirstContext;
	FString Error;
	for (int64 RunSeed = 1; RunSeed <= 64; ++RunSeed)
	{
		FEFCalystoRunEcologyState Ecology =
			UEFCalystoDungeonSubsystem::BuildInitialEcology(RunSeed, PolicyHash, 3);
		Ecology.Threat = 0.0f;
		Ecology.EcologyHash = UEFCalystoDungeonSubsystem::ComputeEcologyHash(Ecology);
		const FEFCalystoDungeonGenerationContext Context = MakeContext(
			RunSeed, 1, 1, PolicyHash, Ecology.EcologyHash);
		FEFCalystoResolvedFloorIntent Intent;
		if (!Resolve(Policy, Context, Ecology, Intent, Error))
		{
			AddError(FString::Printf(TEXT("Rarity composition failed for seed %lld: %s"), RunSeed, *Error));
			return false;
		}
		TestEqual(TEXT("Rarity composition respects the exact one-enemy count."), Intent.EnemyCount, 1);
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			if (Directive.Category == EEFCalystoSpawnCategory::Enemy)
			{
				SelectedIds.Add(Directive.StableId);
			}
		}
		if (RunSeed == 1)
		{
			First = Intent;
			FirstEcology = Ecology;
			FirstContext = Context;
		}
	}
	TestEqual(TEXT("Independent rarity targets can select both authored rarity extremes."), SelectedIds.Num(), 2);

	FEFCalystoResolvedFloorIntent Replay;
	TestTrue(TEXT("Rarity composition replays exactly for the full frozen tuple."),
		Resolve(Policy, FirstContext, FirstEcology, Replay, Error));
	TestEqual(TEXT("Rarity replay preserves IntentHash."), Replay.IntentHash, First.IntentHash);
	FEFCalystoResolvedFloorIntent Reroll;
	TestTrue(TEXT("Rarity composition remains valid on reroll."),
		Resolve(Policy, MakeContext(1, 1, 2, PolicyHash, FirstEcology.EcologyHash),
			FirstEcology, Reroll, Error));
	TestNotEqual(TEXT("Reroll advances the complete deterministic stream."), Reroll.PCGSeed, First.PCGSeed);
	TestEqual(TEXT("Reroll still respects the exact one-enemy cap."), Reroll.EnemyCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3TravelTransactionTest,
	"NoShellForWinter.CalystoDungeon.V3.Travel.TransactionalCommitBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3TravelTransactionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UEFCalystoDungeonSubsystem* Subsystem = NewObject<UEFCalystoDungeonSubsystem>(GameInstance);
	TestNotNull(TEXT("A V3 subsystem can be constructed for transaction-state testing."), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	FEFCalystoRunEcologyState SourceEcology;
	SourceEcology.bInitialized = true;
	SourceEcology.RunDNAHash = TEXT("SOURCE_DNA");
	SourceEcology.EcologyHash = TEXT("SOURCE_ECOLOGY");
	SourceEcology.LastCommittedFloor = 7;
	SourceEcology.Revision = 11;
	Subsystem->RunEcology = SourceEcology;
	Subsystem->ActiveContext.RunSeed = 4444;
	Subsystem->ActiveContext.FloorNumber = 7;
	Subsystem->ActiveContext.GenerationSerial = 9;
	Subsystem->ActiveContext.PCGSeed = 17;
	Subsystem->ActiveContext.PolicyHash = TEXT("POLICY");
	Subsystem->ActiveIntent.bIsValid = true;
	Subsystem->ActiveIntent.IntentHash = TEXT("SOURCE_INTENT");
	Subsystem->ActiveManifest.bIsValid = true;
	Subsystem->ActiveManifest.ManifestHash = TEXT("SOURCE_MANIFEST");

	Subsystem->bHasSubmittedOutcome = true;
	Subsystem->SubmittedOutcome.bIsValid = true;
	Subsystem->SubmittedOutcome.Combat = 0.875f;
	Subsystem->SubmittedOutcome.Deaths = 2;
	Subsystem->bHasQueuedDirectorIntent = true;
	Subsystem->QueuedDirectorIntent.PreferredStyle = EEFCalystoDungeonStyle::Branching;
	Subsystem->QueuedDirectorIntent.ScaleBias = 0.35f;
	Subsystem->QueuedDirectorIntent.Volatility = 0.8f;

	FEFCalystoRunEcologyState CandidateEcology = SourceEcology;
	CandidateEcology.LastCommittedFloor = 8;
	CandidateEcology.Revision = 12;
	CandidateEcology.EcologyHash = TEXT("CANDIDATE_ECOLOGY");
	Subsystem->PendingEcology = CandidateEcology;
	Subsystem->bTravelRequestPending = true;
	Subsystem->PendingTravelRequestId = 91;
	Subsystem->PendingResolvedFloorAssetPaths.Add(FSoftObjectPath(TEXT("/Script/Engine.Actor")));
	Subsystem->bPendingConsumesSubmittedOutcome = true;
	Subsystem->bPendingConsumesQueuedDirectorIntent = true;

	// Both preload-start failure and OpenLevel rejection discard this same staged
	// transaction. Canonical source state and captured inputs must remain untouched.
	Subsystem->ResetPendingTravelTransaction();
	TestEqual(TEXT("Reject keeps the source ecology hash."), Subsystem->RunEcology.EcologyHash, FString(TEXT("SOURCE_ECOLOGY")));
	TestEqual(TEXT("Reject keeps the source ecology revision."), Subsystem->RunEcology.Revision, int64(11));
	TestEqual(TEXT("Reject keeps the source floor uncommitted."), Subsystem->RunEcology.LastCommittedFloor, int64(7));
	TestEqual(TEXT("Reject keeps the active context."), Subsystem->ActiveContext.FloorNumber, int64(7));
	TestEqual(TEXT("Reject keeps the active intent."), Subsystem->ActiveIntent.IntentHash, FString(TEXT("SOURCE_INTENT")));
	TestEqual(TEXT("Reject keeps the active manifest."), Subsystem->ActiveManifest.ManifestHash, FString(TEXT("SOURCE_MANIFEST")));
	TestTrue(TEXT("Reject preserves the submitted outcome."), Subsystem->bHasSubmittedOutcome);
	TestTrue(TEXT("Reject preserves the queued Director intent."), Subsystem->bHasQueuedDirectorIntent);
	TestTrue(TEXT("Reject preserves exact outcome data."),
		FMath::IsNearlyEqual(Subsystem->SubmittedOutcome.Combat, 0.875f) && Subsystem->SubmittedOutcome.Deaths == 2);
	TestTrue(TEXT("Reject preserves exact queued intent data."),
		Subsystem->QueuedDirectorIntent.PreferredStyle == EEFCalystoDungeonStyle::Branching
		&& FMath::IsNearlyEqual(Subsystem->QueuedDirectorIntent.ScaleBias, 0.35f)
		&& FMath::IsNearlyEqual(Subsystem->QueuedDirectorIntent.Volatility, 0.8f));
	TestFalse(TEXT("Reject clears the pending flag."), Subsystem->bTravelRequestPending);
	TestEqual(TEXT("Reject clears the pending token."), Subsystem->PendingTravelRequestId, int64(0));
	TestTrue(TEXT("Reject clears the pending preload path set."), Subsystem->PendingResolvedFloorAssetPaths.IsEmpty());

	// Destination acceptance is the only commit point. The accepted candidate is
	// promoted first, then the exact captured inputs are consumed once.
	Subsystem->PendingEcology = CandidateEcology;
	Subsystem->RunEcology = Subsystem->PendingEcology;
	Subsystem->bPendingConsumesSubmittedOutcome = true;
	Subsystem->bPendingConsumesQueuedDirectorIntent = true;
	Subsystem->CommitAcceptedPendingInputs();
	Subsystem->ResetPendingTravelTransaction();
	TestEqual(TEXT("Accept commits the candidate ecology exactly once."),
		Subsystem->RunEcology.EcologyHash, FString(TEXT("CANDIDATE_ECOLOGY")));
	TestEqual(TEXT("Accept commits the candidate revision."), Subsystem->RunEcology.Revision, int64(12));
	TestFalse(TEXT("Accept consumes the submitted outcome."), Subsystem->bHasSubmittedOutcome);
	TestFalse(TEXT("Accept consumes the queued Director intent."), Subsystem->bHasQueuedDirectorIntent);

	// Replay/recovery do not own the queued next-floor intent or current outcome.
	Subsystem->bHasSubmittedOutcome = true;
	Subsystem->SubmittedOutcome.bIsValid = true;
	Subsystem->SubmittedOutcome.Pace = 0.2f;
	Subsystem->bHasQueuedDirectorIntent = true;
	Subsystem->QueuedDirectorIntent.ThreatBias = -0.4f;
	Subsystem->bPendingConsumesSubmittedOutcome = false;
	Subsystem->bPendingConsumesQueuedDirectorIntent = false;
	Subsystem->CommitAcceptedPendingInputs();
	TestTrue(TEXT("Replay preserves the submitted outcome."), Subsystem->bHasSubmittedOutcome);
	TestTrue(TEXT("Replay preserves the queued next-floor intent."), Subsystem->bHasQueuedDirectorIntent);
	TestTrue(TEXT("Replay preserves exact pending input values."),
		FMath::IsNearlyEqual(Subsystem->SubmittedOutcome.Pace, 0.2f)
		&& FMath::IsNearlyEqual(Subsystem->QueuedDirectorIntent.ThreatBias, -0.4f));

	Subsystem->bHasActiveRun = true;
	Subsystem->TravelState = EEFCalystoDungeonTravelState::Idle;
	Subsystem->GenerationState = EEFCalystoGenerationState::Failed;
	int32 BeforeAdvanceCalls = 0;
	bool bNestedAdvanceAccepted = true;
	const FDelegateHandle BeforeAdvanceHandle = Subsystem->OnBeforeFloorAdvance().AddLambda(
		[&BeforeAdvanceCalls, &bNestedAdvanceAccepted, Subsystem](
			const int64 CompletedFloor, const FEFCalystoResolvedFloorIntent& CompletedIntent)
		{
			(void)CompletedFloor;
			(void)CompletedIntent;
			++BeforeAdvanceCalls;
			bNestedAdvanceAccepted = Subsystem->RequestAdvanceFloor();
		});
	TestFalse(TEXT("A failed/unready floor cannot Advance."), Subsystem->RequestAdvanceFloor());
	TestEqual(TEXT("An unready floor never emits BeforeAdvance."), BeforeAdvanceCalls, 0);

	Subsystem->GenerationState = EEFCalystoGenerationState::Ready;
	Subsystem->ActiveManifest.bIsValid = true;
	TestFalse(TEXT("The fixture's outer Advance stops at its intentionally absent runtime policy/world."),
		Subsystem->RequestAdvanceFloor());
	TestEqual(TEXT("A ready floor emits BeforeAdvance once."), BeforeAdvanceCalls, 1);
	TestFalse(TEXT("BeforeAdvance cannot reenter a second Advance transaction."), bNestedAdvanceAccepted);
	Subsystem->OnBeforeFloorAdvance().Remove(BeforeAdvanceHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3ManifestHashTest,
	"NoShellForWinter.CalystoDungeon.V3.Manifest.CanonicalHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3ManifestHashTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEFCalystoRealizedFloorManifest Manifest;
	Manifest.bIsValid = true;
	Manifest.RunSeed = 77;
	Manifest.FloorNumber = 4;
	Manifest.GenerationSerial = 9;
	Manifest.PCGSeed = 123;
	Manifest.IntentHash = FString::ChrN(64, TEXT('A'));
	Manifest.AnchorTopologyHash = FString::ChrN(64, TEXT('B'));
	Manifest.PopulationHash = FString::ChrN(64, TEXT('C'));
	Manifest.ResourceHash = FString::ChrN(64, TEXT('D'));
	Manifest.CandidateAnchorCount = 50;
	Manifest.EnemyCount = 2;
	Manifest.SpawnedActorCount = 2;
	Manifest.RealizedThreatCost = 2.0f;

	FEFCalystoSpawnDirective A;
	A.StableId = TEXT("A");
	A.Category = EEFCalystoSpawnCategory::Enemy;
	A.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Script/Engine.Character")));
	A.Count = 1;
	A.CostPerActor = 1.0f;
	FEFCalystoSpawnDirective B = A;
	B.StableId = TEXT("B");
	Manifest.SpawnDirectives = {A, B};

	const FString Hash = UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest);
	TestEqual(TEXT("ManifestHash is SHA-256."), Hash.Len(), 64);
	Algo::Reverse(Manifest.SpawnDirectives);
	TestEqual(TEXT("Directive storage order does not change ManifestHash."),
		UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest), Hash);
	Manifest.RealizedThreatCost += 1.0f;
	TestNotEqual(TEXT("A realized budget change changes ManifestHash."),
		UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest), Hash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3AuthoredAssetAndLegacyAbsenceTest,
	"NoShellForWinter.CalystoDungeon.V3.Cutover.AssetAndLegacyAbsence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3AuthoredAssetAndLegacyAbsenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3Tests;
	UEFCalystoDungeonDirectorPolicy* Policy = LoadObject<UEFCalystoDungeonDirectorPolicy>(nullptr, PolicyPath);
	TestNotNull(TEXT("The sole authored V3 Primary Data Asset exists."), Policy);
	if (Policy)
	{
		FString Error;
		TestTrue(TEXT("The authored V3 policy validates through the runtime path."),
			UEFCalystoDungeonSubsystem::ValidateDirectorPolicy(Policy, Error));
	}

	const UClass* SubsystemClass = UEFCalystoDungeonSubsystem::StaticClass();
	TestNull(TEXT("RequestRegenerateFloor was removed."), SubsystemClass->FindFunctionByName(TEXT("RequestRegenerateFloor")));
	TestNull(TEXT("SetRunSeed was removed."), SubsystemClass->FindFunctionByName(TEXT("SetRunSeed")));
	TestNull(TEXT("SetForcedLayoutPreset was removed."), SubsystemClass->FindFunctionByName(TEXT("SetForcedLayoutPreset")));
	TestNull(TEXT("SetForcedSpawnerPreset was removed."), SubsystemClass->FindFunctionByName(TEXT("SetForcedSpawnerPreset")));
	TestNull(TEXT("SetForcedThemePreset was removed."), SubsystemClass->FindFunctionByName(TEXT("SetForcedThemePreset")));
	TestNull(TEXT("ClearForcedPresets was removed."), SubsystemClass->FindFunctionByName(TEXT("ClearForcedPresets")));
	TestNull(TEXT("The legacy generation row struct no longer exists."),
		FindObject<UScriptStruct>(nullptr, TEXT("/Script/EFProceduralRuntime.EFCalystoGenerationOptionRow")));
	TestNull(TEXT("The legacy spawner row struct no longer exists."),
		FindObject<UScriptStruct>(nullptr, TEXT("/Script/EFProceduralRuntime.EFCalystoSpawnerPresetRow")));
	TestNull(TEXT("The legacy theme row struct no longer exists."),
		FindObject<UScriptStruct>(nullptr, TEXT("/Script/EFProceduralRuntime.EFCalystoThemePresetRow")));
	return true;
}

#endif

#endif // RETIRED V3 AUTOMATION EVIDENCE
