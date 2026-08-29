// RETIRED V3 AUTOMATION EVIDENCE.
//
// These sweeps target the removed aggregate V3 model. They remain in source as
// historical evidence but cannot register alongside the V4 Style+Theme suite.
#if 0

#include "Calysto/EFCalystoDungeonDirectorPolicy.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoV3StressTests
{
	struct FBinomialAccumulator
	{
		double Expected = 0.0;
		double Variance = 0.0;
		int64 Observed = 0;

		void Add(const double Probability, const bool bObserved)
		{
			const double P = FMath::Clamp(Probability, 0.0, 1.0);
			Expected += P;
			Variance += P * (1.0 - P);
			Observed += bObserved ? 1 : 0;
		}

		bool IsWithinSixSigma() const
		{
			return FMath::Abs(static_cast<double>(Observed) - Expected)
				<= 6.0 * FMath::Sqrt(Variance) + 2.0;
		}
	};

	struct FDepthBand
	{
		FDepthBand(const int64 InMinFloor, const int64 InMaxFloor, const TCHAR* InLabel)
			: MinFloor(InMinFloor)
			, MaxFloor(InMaxFloor)
			, Label(InLabel)
		{
		}

		int64 MinFloor = 1;
		int64 MaxFloor = 1;
		FString Label;
		TArray<int32> DungeonEdges;
		TArray<int32> EnemyCounts;
		TArray<float> ThreatBudgets;
	};

	struct FOutcomeSummary
	{
		double MeanThreat = 0.0;
		double MeanAbundance = 0.0;
		double MeanThreatBudget = 0.0;
		double MeanEnemyCount = 0.0;
		double MeanFinalEMA = 0.0;
		FString CanonicalTrajectoryHash;
	};

	static UEFCalystoDungeonDirectorPolicy* MakePolicy()
	{
		UEFCalystoDungeonDirectorPolicy* Policy =
			NewObject<UEFCalystoDungeonDirectorPolicy>(GetTransientPackage());
		if (Policy)
		{
			// Logical native fixture only. These values are not certification evidence
			// and never mutate the authored create-only policy asset.
			Policy->ValidatedDungeonSizes = {26, 27, 28, 29, 30};
		}
		return Policy;
	}

	static FEFCalystoDungeonGenerationContext MakeContext(
		const int64 RunSeed,
		const int64 FloorNumber,
		const int64 GenerationSerial,
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FString& PolicyHash,
		const FString& EcologyHash)
	{
		FEFCalystoDungeonGenerationContext Context;
		Context.RunSeed = RunSeed;
		Context.FloorNumber = FloorNumber;
		Context.GenerationSerial = GenerationSerial;
		Context.PolicyHash = PolicyHash;
		Context.PCGSeed = Policy
			? EFCalystoDungeonDeterminism::DerivePCGSeed(
				RunSeed,
				FloorNumber,
				GenerationSerial,
				Policy->GeneratorVersion,
				PolicyHash,
				EcologyHash)
			: 0;
		return Context;
	}

	static bool Resolve(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FEFCalystoDungeonGenerationContext& Context,
		const FEFCalystoRunEcologyState& Ecology,
		const FEFCalystoFloorOutcome& FrozenOutcome,
		FEFCalystoResolvedFloorIntent& OutIntent,
		FString& OutError)
	{
		return UEFCalystoDungeonSubsystem::ResolveFloorIntentForTesting(
			Policy,
			Context,
			Ecology,
			FEFCalystoDirectorIntent(),
			FrozenOutcome,
			OutIntent,
			OutError);
	}

	static bool HasEligibleEntry(
		const TArray<FEFCalystoPopulationCatalogEntry>& Catalog,
		const int64 FloorNumber)
	{
		return Catalog.ContainsByPredicate([FloorNumber](const FEFCalystoPopulationCatalogEntry& Entry)
		{
			return Entry.bEnabled && Entry.BaseWeight > 0 && Entry.MinimumFloor <= FloorNumber;
		});
	}

	static int32 TotalActorCount(const FEFCalystoResolvedFloorIntent& Intent)
	{
		return Intent.EnemyCount
			+ Intent.FoodCount
			+ Intent.ChestCount
			+ Intent.LootCount
			+ Intent.SpecialEventCount;
	}

	static FEFCalystoRealizedFloorManifest MakeSyntheticManifest(
		const FEFCalystoResolvedFloorIntent& Intent)
	{
		FEFCalystoRealizedFloorManifest Manifest;
		Manifest.bIsValid = Intent.bIsValid;
		Manifest.RunSeed = Intent.RunSeed;
		Manifest.FloorNumber = Intent.FloorNumber;
		Manifest.GenerationSerial = Intent.GenerationSerial;
		Manifest.PCGSeed = Intent.PCGSeed;
		Manifest.IntentHash = Intent.IntentHash;
		Manifest.AnchorTopologyHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
			TEXT("EFCalystoNativeSweepTopologyV3|%s|%lld"),
			*Intent.IntentHash,
			Intent.FloorNumber));
		Manifest.PopulationHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
			TEXT("EFCalystoNativeSweepPopulationV3|%s|%d"),
			*Intent.IntentHash,
			Intent.EnemyCount));
		Manifest.ResourceHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
			TEXT("EFCalystoNativeSweepResourcesV3|%s|%d|%d|%d|%d"),
			*Intent.IntentHash,
			Intent.FoodCount,
			Intent.ChestCount,
			Intent.LootCount,
			Intent.SpecialEventCount));
		Manifest.EnemyCount = Intent.EnemyCount;
		Manifest.FoodCount = Intent.FoodCount;
		Manifest.ChestCount = Intent.ChestCount;
		Manifest.LootCount = Intent.LootCount;
		Manifest.SpecialEventCount = Intent.SpecialEventCount;
		Manifest.SpawnedActorCount = TotalActorCount(Intent);
		Manifest.CandidateAnchorCount = FMath::Max(Manifest.SpawnedActorCount, 1);
		Manifest.SpawnDirectives = Intent.SpawnDirectives;
		for (const FEFCalystoSpawnDirective& Directive : Intent.SpawnDirectives)
		{
			const float Cost = Directive.CostPerActor * static_cast<float>(Directive.Count);
			if (Directive.Category == EEFCalystoSpawnCategory::Enemy)
			{
				Manifest.RealizedThreatCost += Cost;
			}
			else
			{
				Manifest.RealizedResourceCost += Cost;
			}
		}
		Manifest.ManifestHash = UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest);
		return Manifest;
	}

	static double Median(TArray<int32>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return Values.Num() % 2 == 0
			? 0.5 * static_cast<double>(Values[Middle - 1] + Values[Middle])
			: static_cast<double>(Values[Middle]);
	}

	static double Median(TArray<float>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return Values.Num() % 2 == 0
			? 0.5 * static_cast<double>(Values[Middle - 1] + Values[Middle])
			: static_cast<double>(Values[Middle]);
	}

	static int32 FindBandIndex(const TArray<FDepthBand>& Bands, const int64 FloorNumber)
	{
		return Bands.IndexOfByPredicate([FloorNumber](const FDepthBand& Band)
		{
			return FloorNumber >= Band.MinFloor && FloorNumber <= Band.MaxFloor;
		});
	}

	static FEFCalystoFloorOutcome MakeOutcome(const float Score, const bool bStruggling)
	{
		FEFCalystoFloorOutcome Outcome;
		Outcome.bIsValid = true;
		Outcome.Combat = Score;
		Outcome.Survival = Score;
		Outcome.Resources = Score;
		Outcome.Pace = Score;
		Outcome.Deaths = bStruggling ? 1 : 0;
		Outcome.Failures = bStruggling ? 1 : 0;
		return Outcome;
	}

	static bool RunOutcomeCohort(
		const UEFCalystoDungeonDirectorPolicy* Policy,
		const FString& PolicyHash,
		const FEFCalystoFloorOutcome& Outcome,
		FOutcomeSummary& OutSummary,
		FString& OutError)
	{
		constexpr int32 RunCount = 16;
		constexpr int32 FloorCount = 40;
		double ThreatTotal = 0.0;
		double AbundanceTotal = 0.0;
		double ThreatBudgetTotal = 0.0;
		double EnemyTotal = 0.0;
		double FinalEMATotal = 0.0;
		int32 SampleCount = 0;
		FString Canonical;

		for (int32 RunIndex = 0; RunIndex < RunCount; ++RunIndex)
		{
			const int64 RunSeed = 8800000 + RunIndex;
			FEFCalystoRunEcologyState Ecology = UEFCalystoDungeonSubsystem::BuildInitialEcology(
				RunSeed, PolicyHash, Policy->GeneratorVersion);
			for (int64 FloorNumber = 1; FloorNumber <= FloorCount; ++FloorNumber)
			{
				const FEFCalystoDungeonGenerationContext Context = MakeContext(
					RunSeed, FloorNumber, FloorNumber, Policy, PolicyHash, Ecology.EcologyHash);
				FEFCalystoResolvedFloorIntent Intent;
				if (!Resolve(Policy, Context, Ecology, Outcome, Intent, OutError))
				{
					OutError = FString::Printf(
						TEXT("Outcome cohort failed for seed %lld floor %lld: %s"),
						RunSeed,
						FloorNumber,
						*OutError);
					return false;
				}
				if (FloorNumber > 1)
				{
					ThreatTotal += Intent.Threat;
					AbundanceTotal += Intent.Abundance;
					ThreatBudgetTotal += Intent.ThreatBudget;
					EnemyTotal += Intent.EnemyCount;
					++SampleCount;
				}
				Canonical += Intent.IntentHash;
				if (!UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(
						Policy, FloorNumber, Intent, Outcome, Ecology, OutError))
				{
					OutError = FString::Printf(
						TEXT("Outcome commit failed for seed %lld floor %lld: %s"),
						RunSeed,
						FloorNumber,
						*OutError);
					return false;
				}
			}
			FinalEMATotal += Ecology.PerformanceEMA;
			Canonical += Ecology.EcologyHash;
		}

		if (SampleCount <= 0)
		{
			OutError = TEXT("Outcome cohort produced no samples.");
			return false;
		}
		OutSummary.MeanThreat = ThreatTotal / SampleCount;
		OutSummary.MeanAbundance = AbundanceTotal / SampleCount;
		OutSummary.MeanThreatBudget = ThreatBudgetTotal / SampleCount;
		OutSummary.MeanEnemyCount = EnemyTotal / SampleCount;
		OutSummary.MeanFinalEMA = FinalEMATotal / RunCount;
		OutSummary.CanonicalTrajectoryHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Canonical);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3IntentManifest100KStressTest,
	"NoShellForWinter.CalystoDungeon.V3.Stress.IntentManifest100K",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3IntentManifest100KStressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3StressTests;
	constexpr int32 RunCount = 100;
	constexpr int32 MaxFloor = 1000;
	constexpr int32 ExpectedSampleCount = RunCount * MaxFloor;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicy();
	if (!Policy)
	{
		AddError(TEXT("Could not construct the native V3 policy."));
		return false;
	}
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	if (PolicyHash.IsEmpty())
	{
		AddError(TEXT("The native V3 policy did not validate before the 100k sweep."));
		return false;
	}

	TArray<FDepthBand> Bands;
	Bands.Emplace(1, 10, TEXT("01-10"));
	Bands.Emplace(11, 25, TEXT("11-25"));
	Bands.Emplace(26, 100, TEXT("26-100"));
	// The saturating curves are effectively flat beyond Floor 100. Keep that
	// plateau in one statistically stable band instead of inventing an expected
	// ordering between several samples from the same limiting distribution.
	Bands.Emplace(101, 1000, TEXT("101-1000"));
	for (FDepthBand& Band : Bands)
	{
		const int32 BandSampleCount = RunCount * static_cast<int32>(Band.MaxFloor - Band.MinFloor + 1);
		Band.DungeonEdges.Reserve(BandSampleCount);
		Band.EnemyCounts.Reserve(BandSampleCount);
		Band.ThreatBudgets.Reserve(BandSampleCount);
	}
	FBinomialAccumulator EnemyPresence;
	FBinomialAccumulator FoodPresence;
	FBinomialAccumulator ChestPresence;
	FBinomialAccumulator LootPresence;
	FBinomialAccumulator ForgeDominance;
	int32 IntentCount = 0;
	int32 ManifestCount = 0;
	const double StartSeconds = FPlatformTime::Seconds();

	for (int32 RunIndex = 0; RunIndex < RunCount; ++RunIndex)
	{
		const int64 RunSeed = 9100000 + RunIndex;
		// The 100k probability sweep intentionally freezes each run's initial
		// ecology. This isolates the authored Bernoulli probabilities from pity,
		// cooldown, and outcome feedback; the sequential 1-1000 test below owns
		// those mutable-memory gates.
		const FEFCalystoRunEcologyState Ecology = UEFCalystoDungeonSubsystem::BuildInitialEcology(
			RunSeed, PolicyHash, Policy->GeneratorVersion);
		if (!Ecology.bInitialized || Ecology.EcologyHash.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Initial ecology failed for sweep seed %lld."), RunSeed));
			return false;
		}

		for (int64 FloorNumber = 1; FloorNumber <= MaxFloor; ++FloorNumber)
		{
			const FEFCalystoDungeonGenerationContext Context = MakeContext(
				RunSeed, FloorNumber, FloorNumber, Policy, PolicyHash, Ecology.EcologyHash);
			FEFCalystoResolvedFloorIntent Intent;
			FString Error;
			if (!Resolve(Policy, Context, Ecology, FEFCalystoFloorOutcome(), Intent, Error))
			{
				AddError(FString::Printf(
					TEXT("100k intent failed for seed %lld floor %lld: %s"),
					RunSeed,
					FloorNumber,
					*Error));
				return false;
			}
			++IntentCount;

			const int32 TotalActors = TotalActorCount(Intent);
			if (!Intent.bIsValid || Intent.IntentHash.Len() != 64 || Intent.PCGSeed <= 0
				|| !Policy->ValidatedDungeonSizes.Contains(Intent.DungeonSize.X)
				|| Intent.DungeonSize.X != Intent.DungeonSize.Y || Intent.DungeonSize.Z != 1
				|| Intent.EnemyCount < 0 || Intent.EnemyCount > Policy->Limits.MaxEnemies
				|| Intent.FoodCount < 0 || Intent.FoodCount > Policy->Limits.MaxFood
				|| Intent.ChestCount < 0 || Intent.ChestCount > Policy->Limits.MaxChests
				|| Intent.LootCount < 0 || Intent.LootCount > Policy->Limits.MaxLoot
				|| Intent.SpecialEventCount < 0 || Intent.SpecialEventCount > Policy->Limits.MaxSpecialEvents
				|| TotalActors < 0 || TotalActors > Policy->Limits.MaxDirectorActors
				|| !FMath::IsFinite(Intent.ThreatBudget) || Intent.ThreatBudget < Policy->Progression.StartThreatBudget
				|| Intent.ThreatBudget > Policy->Progression.EndThreatBudget
				|| !FMath::IsFinite(Intent.ResourceBudget)
				|| !FMath::IsFinite(Intent.Pacing)
				|| FMath::Abs(Intent.Pacing) > Policy->Progression.PacingAmplitude + KINDA_SMALL_NUMBER)
			{
				AddError(FString::Printf(
					TEXT("100k intent violated a cap or finite-state invariant for seed %lld floor %lld."),
					RunSeed,
					FloorNumber));
				return false;
			}

			const FEFCalystoRealizedFloorManifest Manifest = MakeSyntheticManifest(Intent);
			++ManifestCount;
			if (!Manifest.bIsValid || Manifest.ManifestHash.Len() != 64
				|| Manifest.ManifestHash != UEFCalystoDungeonSubsystem::ComputeManifestHash(Manifest)
				|| Manifest.SpawnedActorCount != TotalActors
				|| Manifest.RealizedThreatCost > Intent.ThreatBudget + KINDA_SMALL_NUMBER
				|| Manifest.RealizedResourceCost > Intent.ResourceBudget + KINDA_SMALL_NUMBER)
			{
				AddError(FString::Printf(
					TEXT("Synthetic manifest contract failed for seed %lld floor %lld."),
					RunSeed,
					FloorNumber));
				return false;
			}

			EnemyPresence.Add(Intent.EnemyPresenceChance, Intent.EnemyCount > 0);
			FoodPresence.Add(Intent.FoodPresenceChance, Intent.FoodCount > 0);
			ChestPresence.Add(Intent.ChestPresenceChance, Intent.ChestCount > 0);
			const bool bLootEligible = HasEligibleEntry(Policy->LootCatalog, FloorNumber);
			LootPresence.Add(bLootEligible ? Intent.LootPresenceChance : 0.0, Intent.LootCount > 0);

			int32 ForgeWeight = 0;
			int32 ThemeWeightTotal = 0;
			for (const FEFCalystoThemeWeight& Theme : Intent.ThemeWeights)
			{
				ThemeWeightTotal += Theme.Weight;
				if (Theme.ThemeId == FName(TEXT("Forge")))
				{
					ForgeWeight = Theme.Weight;
				}
			}
			if (ThemeWeightTotal <= 0)
			{
				AddError(TEXT("Resolved theme weights had no positive total."));
				return false;
			}
			ForgeDominance.Add(
				static_cast<double>(ForgeWeight) / static_cast<double>(ThemeWeightTotal),
				Intent.DominantTheme == FName(TEXT("Forge")));

			const int32 BandIndex = FindBandIndex(Bands, FloorNumber);
			if (!Bands.IsValidIndex(BandIndex))
			{
				AddError(FString::Printf(TEXT("Floor %lld has no statistical depth band."), FloorNumber));
				return false;
			}
			Bands[BandIndex].DungeonEdges.Add(Intent.DungeonSize.X);
			Bands[BandIndex].EnemyCounts.Add(Intent.EnemyCount);
			Bands[BandIndex].ThreatBudgets.Add(Intent.ThreatBudget);

			if ((IntentCount % 10000) == 0)
			{
				FEFCalystoResolvedFloorIntent Replay;
				if (!Resolve(Policy, Context, Ecology, FEFCalystoFloorOutcome(), Replay, Error)
					|| Replay.IntentHash != Intent.IntentHash
					|| Replay.PCGSeed != Intent.PCGSeed)
				{
					AddError(FString::Printf(
						TEXT("Deterministic replay checkpoint failed at sample %d."), IntentCount));
					return false;
				}
			}
		}
	}

	TestEqual(TEXT("The stress sweep resolves exactly 100,000 intents."), IntentCount, ExpectedSampleCount);
	TestEqual(TEXT("The stress sweep hashes exactly 100,000 synthetic manifests."), ManifestCount, ExpectedSampleCount);
	TestTrue(TEXT("Enemy-presence frequency remains inside heterogeneous six-sigma bounds."),
		EnemyPresence.IsWithinSixSigma());
	TestTrue(TEXT("Food-presence frequency remains inside heterogeneous six-sigma bounds."),
		FoodPresence.IsWithinSixSigma());
	TestTrue(TEXT("Chest-presence frequency remains inside heterogeneous six-sigma bounds."),
		ChestPresence.IsWithinSixSigma());
	TestTrue(TEXT("Eligible loot-presence frequency remains inside heterogeneous six-sigma bounds."),
		LootPresence.IsWithinSixSigma());
	TestTrue(TEXT("Forge weighted dominance remains inside heterogeneous six-sigma bounds."),
		ForgeDominance.IsWithinSixSigma());

	double FirstSizeMedian = 0.0;
	double FirstEnemyMedian = 0.0;
	double FirstThreatMedian = 0.0;
	double PreviousSizeMedian = 0.0;
	double PreviousEnemyMedian = 0.0;
	double PreviousThreatMedian = 0.0;
	for (int32 BandIndex = 0; BandIndex < Bands.Num(); ++BandIndex)
	{
		FDepthBand& Band = Bands[BandIndex];
		const double SizeMedian = Median(Band.DungeonEdges);
		const double EnemyMedian = Median(Band.EnemyCounts);
		const double ThreatMedian = Median(Band.ThreatBudgets);
		AddInfo(FString::Printf(
			TEXT("Depth band %s medians: size=%.3f enemies=%.3f threat=%.3f samples=%d."),
			*Band.Label,
			SizeMedian,
			EnemyMedian,
			ThreatMedian,
			Band.DungeonEdges.Num()));
		if (BandIndex == 0)
		{
			FirstSizeMedian = SizeMedian;
			FirstEnemyMedian = EnemyMedian;
			FirstThreatMedian = ThreatMedian;
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("Size median does not regress in depth band %s."), *Band.Label),
				SizeMedian >= PreviousSizeMedian);
			TestTrue(*FString::Printf(TEXT("Enemy median does not regress in depth band %s."), *Band.Label),
				EnemyMedian >= PreviousEnemyMedian);
			TestTrue(*FString::Printf(TEXT("Threat median does not regress in depth band %s."), *Band.Label),
				ThreatMedian >= PreviousThreatMedian);
		}
		PreviousSizeMedian = SizeMedian;
		PreviousEnemyMedian = EnemyMedian;
		PreviousThreatMedian = ThreatMedian;
	}
	TestTrue(TEXT("Late-depth median size is greater than early-depth median size."),
		PreviousSizeMedian > FirstSizeMedian);
	TestTrue(TEXT("Late-depth median enemies are greater than early-depth median enemies."),
		PreviousEnemyMedian > FirstEnemyMedian);
	TestTrue(TEXT("Late-depth median threat budget is greater than early-depth median threat budget."),
		PreviousThreatMedian > FirstThreatMedian);

	AddInfo(FString::Printf(
		TEXT("V3 native 100k intent/synthetic-manifest sweep completed in %.2f seconds. "
			"This excludes PCG, NavMesh, actor spawning, and door readiness."),
		FPlatformTime::Seconds() - StartSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3Depth1000Test,
	"NoShellForWinter.CalystoDungeon.V3.Depth.Floors1To1000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3Depth1000Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3StressTests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicy();
	if (!Policy)
	{
		return false;
	}
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	const int64 RunSeed = 10001000;
	FEFCalystoRunEcologyState Ecology = UEFCalystoDungeonSubsystem::BuildInitialEcology(
		RunSeed, PolicyHash, Policy->GeneratorVersion);
	const FEFCalystoFloorOutcome Neutral = MakeOutcome(0.5f, false);
	TSet<FString> SeenIntentHashes;
	int32 ConsecutiveStyleCount = 0;
	int32 ConsecutiveThemeCount = 0;
	EEFCalystoDungeonStyle PreviousStyle = EEFCalystoDungeonStyle::Auto;
	FName PreviousTheme = NAME_None;
	FString Error;

	for (int64 FloorNumber = 1; FloorNumber <= 1000; ++FloorNumber)
	{
		const bool bFoodPityDue = Ecology.ConsecutiveFloorsWithoutFood
			>= Policy->Ecology.FoodPityAfterEmptyFloors;
		const bool bChestPityDue = Ecology.ConsecutiveFloorsWithoutChest
			>= Policy->Ecology.ChestPityAfterEmptyFloors;
		const FEFCalystoDungeonGenerationContext Context = MakeContext(
			RunSeed, FloorNumber, FloorNumber, Policy, PolicyHash, Ecology.EcologyHash);
		FEFCalystoResolvedFloorIntent Intent;
		if (!Resolve(Policy, Context, Ecology, Neutral, Intent, Error))
		{
			AddError(FString::Printf(
				TEXT("Sequential floor %lld failed: %s"), FloorNumber, *Error));
			return false;
		}
		if (SeenIntentHashes.Contains(Intent.IntentHash))
		{
			AddError(FString::Printf(TEXT("Illegal repeated IntentHash cycle at floor %lld."), FloorNumber));
			return false;
		}
		SeenIntentHashes.Add(Intent.IntentHash);
		if (bFoodPityDue && Intent.FoodCount <= 0)
		{
			AddError(FString::Printf(TEXT("Food pity failed at floor %lld."), FloorNumber));
			return false;
		}
		if (bChestPityDue && Intent.ChestCount <= 0)
		{
			AddError(FString::Printf(TEXT("Chest pity failed at floor %lld."), FloorNumber));
			return false;
		}

		ConsecutiveStyleCount = Intent.Style == PreviousStyle ? ConsecutiveStyleCount + 1 : 1;
		ConsecutiveThemeCount = Intent.DominantTheme == PreviousTheme ? ConsecutiveThemeCount + 1 : 1;
		if (ConsecutiveStyleCount > Policy->Ecology.MaxConsecutiveStyle
			|| ConsecutiveThemeCount > Policy->Ecology.MaxConsecutiveDominantTheme)
		{
			AddError(FString::Printf(TEXT("Anti-streak constraint failed at floor %lld."), FloorNumber));
			return false;
		}
		PreviousStyle = Intent.Style;
		PreviousTheme = Intent.DominantTheme;

		if (Intent.PCGSeed <= 0 || Intent.IntentHash.Len() != 64
			|| !FMath::IsFinite(Intent.ThreatBudget)
			|| Intent.ThreatBudget > Policy->Progression.EndThreatBudget
			|| TotalActorCount(Intent) > Policy->Limits.MaxDirectorActors)
		{
			AddError(FString::Printf(TEXT("Overflow/cap invariant failed at floor %lld."), FloorNumber));
			return false;
		}
		if (!UEFCalystoDungeonSubsystem::CommitOutcomeToEcology(
				Policy, FloorNumber, Intent, Neutral, Ecology, Error))
		{
			AddError(FString::Printf(
				TEXT("Sequential ecology commit failed at floor %lld: %s"), FloorNumber, *Error));
			return false;
		}
		if (Ecology.LastCommittedFloor != FloorNumber || Ecology.Revision != FloorNumber
			|| Ecology.EcologyHash.Len() != 64 || !FMath::IsFinite(Ecology.PerformanceEMA))
		{
			AddError(FString::Printf(TEXT("Ecology overflow/revision invariant failed at floor %lld."), FloorNumber));
			return false;
		}
	}

	TestEqual(TEXT("Floors 1-1000 produce 1,000 unique intent hashes."), SeenIntentHashes.Num(), 1000);
	TestEqual(TEXT("Floor 1000 is committed exactly once."), Ecology.LastCommittedFloor, int64(1000));
	TestEqual(TEXT("Ecology revision reaches 1000 without overflow."), Ecology.Revision, int64(1000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV3OutcomeTrajectoriesTest,
	"NoShellForWinter.CalystoDungeon.V3.Adaptation.OutcomeTrajectories",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV3OutcomeTrajectoriesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV3StressTests;
	UEFCalystoDungeonDirectorPolicy* Policy = MakePolicy();
	if (!Policy)
	{
		return false;
	}
	const FString PolicyHash = UEFCalystoDungeonSubsystem::ComputePolicyHash(Policy);
	if (PolicyHash.IsEmpty())
	{
		AddError(TEXT("Outcome trajectory policy did not validate."));
		return false;
	}

	FOutcomeSummary Thriving;
	FOutcomeSummary Neutral;
	FOutcomeSummary Struggling;
	FOutcomeSummary NeutralReplay;
	FOutcomeSummary MissingTelemetry;
	FString Error;
	auto RunCohort = [this, Policy, &PolicyHash, &Error](
		const TCHAR* Label,
		const FEFCalystoFloorOutcome& Outcome,
		FOutcomeSummary& Summary)
	{
		Error.Reset();
		if (RunOutcomeCohort(Policy, PolicyHash, Outcome, Summary, Error))
		{
			return true;
		}
		AddError(FString::Printf(TEXT("%s: %s"), Label, *Error));
		return false;
	};
	if (!RunCohort(TEXT("Thriving outcome cohort failed"), MakeOutcome(1.0f, false), Thriving)
		|| !RunCohort(TEXT("Neutral outcome cohort failed"), MakeOutcome(0.5f, false), Neutral)
		|| !RunCohort(TEXT("Struggling outcome cohort failed"), MakeOutcome(0.0f, true), Struggling)
		|| !RunCohort(TEXT("Neutral replay cohort failed"), MakeOutcome(0.5f, false), NeutralReplay)
		|| !RunCohort(TEXT("Missing-telemetry cohort failed"), FEFCalystoFloorOutcome(), MissingTelemetry))
	{
		return false;
	}

	TestTrue(TEXT("Thriving, neutral, and struggling EMA remain strictly ordered."),
		Thriving.MeanFinalEMA > Neutral.MeanFinalEMA
		&& Neutral.MeanFinalEMA > Struggling.MeanFinalEMA);
	TestTrue(TEXT("Thriving outcomes increase bounded mean threat relative to neutral and struggling."),
		Thriving.MeanThreat > Neutral.MeanThreat
		&& Neutral.MeanThreat > Struggling.MeanThreat);
	TestTrue(TEXT("Struggling outcomes increase abundance relative to neutral and thriving."),
		Struggling.MeanAbundance > Neutral.MeanAbundance
		&& Neutral.MeanAbundance > Thriving.MeanAbundance);
	TestTrue(TEXT("Threat-budget averages preserve the bounded outcome ordering."),
		Thriving.MeanThreatBudget > Neutral.MeanThreatBudget
		&& Neutral.MeanThreatBudget > Struggling.MeanThreatBudget);
	TestEqual(TEXT("Neutral outcome trajectories are exactly deterministic."),
		NeutralReplay.CanonicalTrajectoryHash, Neutral.CanonicalTrajectoryHash);
	TestTrue(TEXT("Neutral replay preserves exact aggregate threat."),
		FMath::IsNearlyEqual(NeutralReplay.MeanThreat, Neutral.MeanThreat));
	TestTrue(TEXT("Neutral replay preserves exact aggregate enemy count."),
		FMath::IsNearlyEqual(NeutralReplay.MeanEnemyCount, Neutral.MeanEnemyCount));
	TestTrue(TEXT("Missing telemetry remains behaviorally neutral."),
		FMath::IsNearlyEqual(MissingTelemetry.MeanFinalEMA, Neutral.MeanFinalEMA)
		&& FMath::IsNearlyEqual(MissingTelemetry.MeanThreat, Neutral.MeanThreat)
		&& FMath::IsNearlyEqual(MissingTelemetry.MeanAbundance, Neutral.MeanAbundance)
		&& FMath::IsNearlyEqual(MissingTelemetry.MeanThreatBudget, Neutral.MeanThreatBudget)
		&& FMath::IsNearlyEqual(MissingTelemetry.MeanEnemyCount, Neutral.MeanEnemyCount));
	return true;
}

#endif

#endif // RETIRED V3 AUTOMATION EVIDENCE
