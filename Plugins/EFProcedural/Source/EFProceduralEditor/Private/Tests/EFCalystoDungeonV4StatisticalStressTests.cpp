#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace EFCalystoV4StatisticalStressTests
{
	static constexpr int32 SamplesPerRepresentativeFloor = 10000;
	static constexpr int32 RepresentativeFloorCount = 10;
	static constexpr int32 StressIntentCount =
		SamplesPerRepresentativeFloor * RepresentativeFloorCount;
	static constexpr float FloatTolerance = 0.001f;
	// Adjacent deep-floor bands sample different seeded populations after the
	// threat curve has effectively plateaued. Permit only a sub-percent median
	// wobble while still catching a meaningful downward trend.
	static constexpr float ThreatMedianTrendTolerance = 0.10f;

	static constexpr int64 RepresentativeFloors[RepresentativeFloorCount] = {
		1, 10, 25, 50, 100, 101, 125, 200, 500, 1000};

	struct FBinomialAccumulator
	{
		double Expected = 0.0;
		double Variance = 0.0;
		int64 Observed = 0;
		int64 Samples = 0;
		bool bInputsValid = true;

		void Add(const double Probability, const bool bOutcome)
		{
			if (!FMath::IsFinite(Probability)
				|| Probability < 0.0
				|| Probability > 1.0)
			{
				bInputsValid = false;
				return;
			}
			Expected += Probability;
			Variance += Probability * (1.0 - Probability);
			Observed += bOutcome ? 1 : 0;
			++Samples;
		}

		bool IsWithinSixSigma() const
		{
			if (!bInputsValid || Samples <= 0
				|| !FMath::IsFinite(Expected)
				|| !FMath::IsFinite(Variance)
				|| Variance < 0.0)
			{
				return false;
			}
			const double ContinuityAllowance = 3.0;
			return FMath::Abs(static_cast<double>(Observed) - Expected)
				<= 6.0 * FMath::Sqrt(Variance) + ContinuityAllowance;
		}
	};

	struct FDepthBand
	{
		FString Label;
		TArray<int32> DungeonEdges;
		TArray<float> ThreatBudgets;

		explicit FDepthBand(const TCHAR* InLabel)
			: Label(InLabel)
		{
		}
	};

	struct FCooldownMemory
	{
		int64 LastSelectedFloor = 0;
		int32 CooldownFloors = 0;
	};

	static UEFCalystoDungeonDirectorPolicyV4* MakePolicy()
	{
		return NewObject<UEFCalystoDungeonDirectorPolicyV4>(
			GetTransientPackage());
	}

	static FEFCalystoResolveContextV4 MakeContext(
		const int64 RunSeed,
		const int64 FloorNumber,
		const int64 GenerationSerial,
		const FString& EcologyHash)
	{
		FEFCalystoResolveContextV4 Context;
		Context.RunSeed = RunSeed;
		Context.FloorNumber = FloorNumber;
		Context.GenerationSerial = GenerationSerial;
		Context.EcologyHash = EcologyHash;
		Context.PerformanceEMA = 0.5f;
		Context.CompanionSnapshotHash =
			FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
				Context.CompanionSnapshot);
		return Context;
	}

	static const FEFCalystoResolvedCategoryV4* FindCategory(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const EEFCalystoContentCategoryV4 Category)
	{
		return Intent.Categories.FindByPredicate(
			[Category](const FEFCalystoResolvedCategoryV4& Candidate)
			{
				return Candidate.Category == Category;
			});
	}

	static int32 HardCapForCategory(
		const FEFCalystoSafetyCeilingsV4& Caps,
		const EEFCalystoContentCategoryV4 Category)
	{
		switch (Category)
		{
		case EEFCalystoContentCategoryV4::Enemy:
			return Caps.MaximumEnemies;
		case EEFCalystoContentCategoryV4::NPC:
			return Caps.MaximumNPCs;
		case EEFCalystoContentCategoryV4::Food:
			return Caps.MaximumFood;
		case EEFCalystoContentCategoryV4::Chest:
			return Caps.MaximumChests;
		case EEFCalystoContentCategoryV4::LooseLoot:
			return Caps.MaximumLooseLoot;
		case EEFCalystoContentCategoryV4::Clothing:
			return Caps.MaximumClothing;
		case EEFCalystoContentCategoryV4::SpecialEvent:
			return Caps.MaximumSpecialEvents;
		case EEFCalystoContentCategoryV4::Decoration:
		case EEFCalystoContentCategoryV4::Lighting:
			return 0;
		default:
			return INDEX_NONE;
		}
	}

	static int64 TierUnlockFloor(const EEFCalystoRarityTierV4 Tier)
	{
		switch (Tier)
		{
		case EEFCalystoRarityTierV4::Common:
			return 1;
		case EEFCalystoRarityTierV4::Uncommon:
			return 2;
		case EEFCalystoRarityTierV4::Rare:
			return 5;
		case EEFCalystoRarityTierV4::Epic:
			return 10;
		case EEFCalystoRarityTierV4::Winter:
			return 101;
		default:
			return MAX_int64;
		}
	}

	static bool ValidateAuthoredTierContract(
		const UEFCalystoDungeonDirectorPolicyV4* Policy,
		FString& OutError)
	{
		if (!Policy)
		{
			OutError = TEXT("The V4 stress policy is null.");
			return false;
		}

		auto ValidateCategories = [&OutError](
			const FString& Owner,
			const TArray<FEFCalystoCategoryProfileV4>& Categories)
		{
			for (const FEFCalystoCategoryProfileV4& Category : Categories)
			{
				const FEFCalystoTierMixV4* Mixes[] = {
					&Category.Tiers.AtFloor1,
					&Category.Tiers.AtFloor100};
				for (int32 MixIndex = 0; MixIndex < 2; ++MixIndex)
				{
					FString TierError;
					const FEFCalystoTierMixV4& Mix = *Mixes[MixIndex];
					if (!FEFCalystoDungeonDirectorMathV4::ValidateTierMix(
							Mix, &TierError)
						|| Mix.GetCalculatedNothing() < .10f - FloatTolerance
						|| !FMath::IsNearlyEqual(
							Mix.Nothing,
							Mix.GetCalculatedNothing(),
							FloatTolerance))
					{
						OutError = FString::Printf(
							TEXT("%s category %d tier endpoint %d violates "
								"the permanent Nothing contract: %s"),
							*Owner,
							static_cast<int32>(Category.Category),
							MixIndex,
							*TierError);
						return false;
					}
				}
			}
			return true;
		};

		for (const FEFCalystoStyleProfileV4& Style : Policy->Styles)
		{
			if (!ValidateCategories(
					FString::Printf(
						TEXT("Style %d"), static_cast<int32>(Style.Style)),
					Style.Categories))
			{
				return false;
			}
		}
		for (const FEFCalystoThemeProfileV4& Theme : Policy->Themes)
		{
			if (!ValidateCategories(
					FString::Printf(
						TEXT("Theme %d"), static_cast<int32>(Theme.Theme)),
					Theme.Categories))
			{
				return false;
			}
		}
		return true;
	}

	static bool ValidateIntent(
		const UEFCalystoDungeonDirectorPolicyV4* Policy,
		const FString& ExpectedPolicyHash,
		const FEFCalystoResolveContextV4& Context,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		FString& OutError)
	{
		if (!Policy || !Intent.bIsValid || Intent.GeneratorVersion != 4
			|| Intent.RunSeed != Context.RunSeed
			|| Intent.FloorNumber != Context.FloorNumber
			|| Intent.GenerationSerial != Context.GenerationSerial
			|| Intent.PolicyHash != ExpectedPolicyHash
			|| Intent.EcologyHash != Context.EcologyHash
			|| Intent.IntentHash.Len() != 64
			|| Intent.CompanionSnapshotHash.Len() != 64
			|| Intent.PCGSeed <= 0)
		{
			OutError = TEXT("The V4 intent has incomplete or mismatched identity.");
			return false;
		}

		if (Intent.DungeonSize.X != Intent.DungeonSize.Y
			|| Intent.DungeonSize.Z != 1
			|| Intent.DungeonSize.X < 18
			|| Intent.DungeonSize.X > 30
			|| !Policy->ValidatedDungeonSizes.Contains(Intent.DungeonSize.X)
			|| !FMath::IsFinite(Intent.StyleSelectionDraw)
			|| Intent.StyleSelectionDraw < 0.0f
			|| Intent.StyleSelectionDraw >= 1.0f
			|| !FMath::IsFinite(Intent.ThemeSelectionDraw)
			|| Intent.ThemeSelectionDraw < 0.0f
			|| Intent.ThemeSelectionDraw >= 1.0f
			|| !FMath::IsFinite(Intent.ShapeBlend)
			|| Intent.ShapeBlend < 0.0f
			|| Intent.ShapeBlend > 1.0f
			|| !FMath::IsFinite(Intent.CandidateAnchorDensity)
			|| Intent.CandidateAnchorDensity < .20f - FloatTolerance
			|| Intent.CandidateAnchorDensity > .50f + FloatTolerance
			|| !FMath::IsFinite(Intent.SidePathChance)
			|| Intent.SidePathChance < .30f - FloatTolerance
			|| Intent.SidePathChance > .70f + FloatTolerance)
		{
			OutError = TEXT("The V4 intent violated a certified shape invariant.");
			return false;
		}
		const int32 ExpectedDirectorLevel = FMath::Clamp(
			static_cast<int32>(Context.FloorNumber), 1, 100);
		const int32 ExpectedWinterLevel = Context.FloorNumber <= 100
			? 0
			: static_cast<int32>(Context.FloorNumber);
		const FEFCalystoContextTraitsV4& Traits = Intent.ResolvedTraits;
		auto IsFiniteBias = [](const float Value)
		{
			return FMath::IsFinite(Value) && Value >= -1.0f && Value <= 1.0f;
		};
		if (Intent.DirectorLevel != ExpectedDirectorLevel
			|| Intent.LogicalWinterLevel != ExpectedWinterLevel
			|| !IsFiniteBias(Traits.Scale)
			|| !IsFiniteBias(Traits.Branching)
			|| !IsFiniteBias(Traits.Mystery)
			|| !IsFiniteBias(Traits.Danger)
			|| !IsFiniteBias(Traits.Safe)
			|| !IsFiniteBias(Traits.Abundance)
			|| !IsFiniteBias(Traits.ClothingInfluence)
			|| !FMath::IsFinite(Traits.Volatility)
			|| Traits.Volatility < 0.0f
			|| Traits.Volatility > 1.0f)
		{
			OutError = TEXT("The V4 intent violated a level or trait invariant.");
			return false;
		}

		if (!FMath::IsFinite(Intent.ThreatBudget)
			|| !FMath::IsFinite(Intent.PlannedThreatCost)
			|| !FMath::IsFinite(Intent.ResourceBudget)
			|| !FMath::IsFinite(Intent.PlannedResourceCost)
			|| Intent.ThreatBudget < 0.0f
			|| Intent.ThreatBudget
				> Policy->SafetyCeilings.MaximumThreatBudget + FloatTolerance
			|| Intent.PlannedThreatCost < 0.0f
			|| Intent.PlannedThreatCost > Intent.ThreatBudget + FloatTolerance
			|| Intent.ResourceBudget < 0.0f
			|| Intent.PlannedResourceCost < 0.0f
			|| Intent.PlannedResourceCost
				> Intent.ResourceBudget + FloatTolerance)
		{
			OutError = TEXT("The V4 intent violated a finite budget invariant.");
			return false;
		}

		if (Intent.Categories.Num() != 9)
		{
			OutError = TEXT("The V4 intent did not resolve all nine categories.");
			return false;
		}

		TSet<EEFCalystoContentCategoryV4> SeenCategories;
		int32 SumDirectiveCounts = 0;
		for (const FEFCalystoResolvedCategoryV4& Category : Intent.Categories)
		{
			const int32 HardCap =
				HardCapForCategory(Policy->SafetyCeilings, Category.Category);
			if (HardCap == INDEX_NONE
				|| SeenCategories.Contains(Category.Category)
				|| !FMath::IsFinite(Category.StyleThemeBlend)
				|| Category.StyleThemeBlend < 0.0f
				|| Category.StyleThemeBlend > 1.0f
				|| !FMath::IsFinite(Category.ResolvedInfluence)
				|| Category.ResolvedInfluence < -1.0f
				|| Category.ResolvedInfluence > 1.0f
				|| !FMath::IsFinite(Category.OpportunityChance)
				|| !FMath::IsFinite(Category.EffectiveChance)
				|| Category.OpportunityChance < 0.0f
				|| Category.OpportunityChance > .90f + FloatTolerance
				|| Category.EffectiveChance < 0.0f
				|| Category.EffectiveChance > .90f + FloatTolerance
				|| Category.MinimumWhenPresent < 0
				|| Category.MaximumPerFloor < Category.MinimumWhenPresent
				|| Category.MaximumPerFloor > HardCap
				|| Category.TargetCount < 0
				|| Category.TargetCount > Category.MaximumPerFloor
				|| Category.AttemptCount < Category.TargetCount
				|| Category.AttemptCount > Category.MaximumPerFloor
				|| Category.DirectiveCount != Category.TargetCount
				|| (!Category.bPresent
					&& (Category.AttemptCount != 0 || Category.TargetCount != 0))
				|| (Category.bPresent
					&& Category.MaximumPerFloor > 0
					&& Category.AttemptCount < 1))
			{
				OutError = FString::Printf(
					TEXT("Resolved category %d violates chance, limit or count "
						"invariants."),
					static_cast<int32>(Category.Category));
				return false;
			}
			SeenCategories.Add(Category.Category);
			SumDirectiveCounts += Category.DirectiveCount;

			FString TierError;
			const float EffectiveNormalMass = Category.WinterChance > 0.0f
				? FMath::Max(0.0f, .90f - Category.WinterChance)
				: Category.ResolvedTiers.GetSelectableMass();
			if (!FEFCalystoDungeonDirectorMathV4::ValidateTierMix(
					Category.ResolvedTiers, &TierError)
				|| Category.ResolvedTiers.GetCalculatedNothing()
					< .10f - FloatTolerance
				|| !FMath::IsNearlyEqual(
					Category.ResolvedTiers.Nothing,
					Category.ResolvedTiers.GetCalculatedNothing(),
					FloatTolerance)
				|| !FMath::IsNearlyEqual(
					Category.SelectableTierMass,
					Category.ResolvedTiers.GetSelectableMass(),
					FloatTolerance)
				|| !FMath::IsFinite(Category.WinterChance)
				|| Category.WinterChance < 0.0f
				|| Category.WinterChance > .90f + FloatTolerance
				|| EffectiveNormalMass + Category.WinterChance
					> .90f + FloatTolerance)
			{
				OutError = FString::Printf(
					TEXT("Resolved category %d violates tier/Nothing invariants: %s"),
					static_cast<int32>(Category.Category),
					*TierError);
				return false;
			}

			for (const EEFCalystoRarityTierV4 Tier : {
					 EEFCalystoRarityTierV4::Common,
					 EEFCalystoRarityTierV4::Uncommon,
					 EEFCalystoRarityTierV4::Rare,
					 EEFCalystoRarityTierV4::Epic})
			{
				if (Context.FloorNumber < TierUnlockFloor(Tier)
					&& Category.ResolvedTiers.Get(Tier) > FloatTolerance)
				{
					OutError = FString::Printf(
						TEXT("Tier %d retained mass before Floor %lld unlock."),
						static_cast<int32>(Tier),
						TierUnlockFloor(Tier));
					return false;
				}
			}
		}

		if (SumDirectiveCounts != Intent.SpawnDirectives.Num()
			|| SumDirectiveCounts > Policy->SafetyCeilings.MaximumDirectorActors)
		{
			OutError = TEXT("The V4 intent actor total violates its hard ceiling.");
			return false;
		}

		TSet<FName> SeenInstances;
		int32 ActualCounts[7] = {};
		double SummedThreatCost = 0.0;
		double SummedResourceCost = 0.0;
		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive :
			Intent.SpawnDirectives)
		{
			const int32 CategoryIndex = static_cast<int32>(Directive.Category);
			if (Directive.StableInstanceId.IsNone()
				|| SeenInstances.Contains(Directive.StableInstanceId)
				|| Directive.CatalogId.IsNone()
				|| !Directive.ActorClass.ToSoftObjectPath().IsValid()
				|| CategoryIndex < 0
				|| CategoryIndex >= 7
				|| Directive.CategorySlotIndex < 0
				|| Directive.CooldownFloors < 0
				|| Directive.LogicalLevel < 0
				|| Directive.LogicalLevel
					> Context.FloorNumber + 16
				|| !FMath::IsFinite(Directive.EffectiveThreatCost)
				|| Directive.EffectiveThreatCost < 0.0f
				|| (Directive.Category == EEFCalystoContentCategoryV4::Enemy
					&& (Directive.LogicalLevel < 1
						|| Directive.PhysicalACFLevel < 1
						|| Directive.PhysicalACFLevel > 100))
				|| (Directive.Category == EEFCalystoContentCategoryV4::NPC
					&& (Directive.LogicalLevel < 1
						|| Directive.PhysicalACFLevel < 1
						|| Directive.PhysicalACFLevel > 100))
				|| Context.FloorNumber < TierUnlockFloor(Directive.Tier))
			{
				OutError = TEXT("A V4 per-instance directive is invalid or overflowed.");
				return false;
			}
			SeenInstances.Add(Directive.StableInstanceId);
			++ActualCounts[CategoryIndex];
			if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
			{
				SummedThreatCost += Directive.EffectiveThreatCost;
			}
			else if (Directive.Category != EEFCalystoContentCategoryV4::NPC)
			{
				SummedResourceCost += 1.0;
			}
		}

		for (int32 CategoryIndex = 0; CategoryIndex < 7; ++CategoryIndex)
		{
			const auto Category =
				static_cast<EEFCalystoContentCategoryV4>(CategoryIndex);
			const FEFCalystoResolvedCategoryV4* Resolved =
				FindCategory(Intent, Category);
			if (!Resolved || ActualCounts[CategoryIndex] != Resolved->DirectiveCount)
			{
				OutError = TEXT("A resolved category count differs from its directives.");
				return false;
			}
		}

		if (!FMath::IsNearlyEqual(
				static_cast<float>(SummedThreatCost),
				Intent.PlannedThreatCost,
				FloatTolerance)
			|| !FMath::IsNearlyEqual(
				static_cast<float>(SummedResourceCost),
				Intent.PlannedResourceCost,
				FloatTolerance))
		{
			OutError = TEXT("V4 planned costs do not equal their per-instance sums.");
			return false;
		}

		TSet<FName> SeenContentAttempts;
		for (const FEFCalystoChestContentDirectiveV4& Content :
			Intent.ChestContentDirectives)
		{
			if (Content.ContainerInstanceId.IsNone()
				|| !SeenInstances.Contains(Content.ContainerInstanceId)
				|| Content.StableAttemptId.IsNone()
				|| SeenContentAttempts.Contains(Content.StableAttemptId)
				|| Content.ContentCatalogId.IsNone()
				|| !Content.ContentClass.ToSoftObjectPath().IsValid()
				|| Content.CooldownFloors < 0
				|| Context.FloorNumber < TierUnlockFloor(Content.Tier))
			{
				OutError = TEXT("A V4 chest-content directive is invalid.");
				return false;
			}
			const FEFCalystoSpawnInstanceDirectiveV4* Container =
				Intent.SpawnDirectives.FindByPredicate(
					[&Content](const FEFCalystoSpawnInstanceDirectiveV4& Candidate)
					{
						return Candidate.StableInstanceId
							== Content.ContainerInstanceId;
					});
			if (!Container
				|| Container->Category != EEFCalystoContentCategoryV4::Chest)
			{
				OutError = TEXT("Chest content references a non-chest container.");
				return false;
			}
			SeenContentAttempts.Add(Content.StableAttemptId);
		}
		return true;
	}

	static void IncrementManifestCount(
		FEFCalystoRealizedFloorManifestV4& Manifest,
		const EEFCalystoContentCategoryV4 Category)
	{
		switch (Category)
		{
		case EEFCalystoContentCategoryV4::Enemy:
			++Manifest.EnemyCount;
			break;
		case EEFCalystoContentCategoryV4::NPC:
			++Manifest.NPCCount;
			break;
		case EEFCalystoContentCategoryV4::Food:
			++Manifest.FoodCount;
			break;
		case EEFCalystoContentCategoryV4::Chest:
			++Manifest.ChestCount;
			break;
		case EEFCalystoContentCategoryV4::LooseLoot:
			++Manifest.LooseLootCount;
			break;
		case EEFCalystoContentCategoryV4::Clothing:
			++Manifest.ClothingCount;
			break;
		case EEFCalystoContentCategoryV4::SpecialEvent:
			++Manifest.SpecialEventCount;
			break;
		default:
			break;
		}
	}

	static FEFCalystoRealizedFloorManifestV4 MakeSyntheticManifest(
		const FEFCalystoResolvedFloorIntentV4& Intent)
	{
		FEFCalystoRealizedFloorManifestV4 Manifest;
		Manifest.RunSeed = Intent.RunSeed;
		Manifest.FloorNumber = Intent.FloorNumber;
		Manifest.GenerationSerial = Intent.GenerationSerial;
		Manifest.IntentHash = Intent.IntentHash;
		Manifest.CompanionSnapshotHash = Intent.CompanionSnapshotHash;
		Manifest.RealizedThreatCost = Intent.PlannedThreatCost;
		Manifest.RealizedResourceCost = Intent.PlannedResourceCost;

		FString PopulationCanonical = TEXT("V4SyntheticPopulation|");
		FString ResourceCanonical = TEXT("V4SyntheticResources|");
		for (int32 Index = 0; Index < Intent.SpawnDirectives.Num(); ++Index)
		{
			const FEFCalystoSpawnInstanceDirectiveV4& Directive =
				Intent.SpawnDirectives[Index];
			FEFCalystoRealizedInstanceV4 Instance;
			Instance.StableInstanceId = Directive.StableInstanceId;
			Instance.StableCompanionId = Directive.StableCompanionId;
			Instance.CatalogId = Directive.CatalogId;
			Instance.VariantId = Directive.VariantId;
			Instance.Archetype = Directive.Archetype;
			Instance.Gender = Directive.Gender;
			Instance.Lifecycle = Directive.Lifecycle;
			Instance.Category = Directive.Category;
			Instance.ActorClass = Directive.ActorClass;
			Instance.Transform = FTransform(
				FRotator(0.0, static_cast<double>((Index * 37) % 360), 0.0),
				FVector(
					static_cast<double>((Index % 10) * 200),
					static_cast<double>((Index / 10) * 200),
					0.0),
				FVector::OneVector);
			Instance.Tier = Directive.Tier;
			Instance.LogicalLevel = Directive.LogicalLevel;
			Instance.EffectiveThreatCost = Directive.EffectiveThreatCost;
			Instance.CooldownFloors = Directive.CooldownFloors;

			for (const FEFCalystoChestContentDirectiveV4& Content :
				Intent.ChestContentDirectives)
			{
				if (Content.ContainerInstanceId == Directive.StableInstanceId)
				{
					Instance.VerifiedChestContentIds.Add(Content.ContentCatalogId);
					Instance.VerifiedChestContents.Add(Content);
					ResourceCanonical += FString::Printf(
						TEXT("%s:%s:%s|"),
						*Content.StableAttemptId.ToString(),
						*Content.ContentCatalogId.ToString(),
						*Content.ContentClass.ToSoftObjectPath().ToString());
				}
			}

			PopulationCanonical += FString::Printf(
				TEXT("%s:%d:%s:%d|"),
				*Instance.StableInstanceId.ToString(),
				static_cast<int32>(Instance.Category),
				*Instance.ActorClass.ToSoftObjectPath().ToString(),
				Instance.LogicalLevel);
			if (Instance.Category != EEFCalystoContentCategoryV4::Enemy
				&& Instance.Category != EEFCalystoContentCategoryV4::NPC)
			{
				ResourceCanonical +=
					Instance.StableInstanceId.ToString() + TEXT("|");
			}
			IncrementManifestCount(Manifest, Instance.Category);
			Manifest.Instances.Add(MoveTemp(Instance));
		}

		Manifest.SpawnedActorCount = Manifest.Instances.Num();
		Manifest.CandidateAnchorCount =
			FMath::Max(Manifest.SpawnedActorCount, 1);
		Manifest.AnchorTopologyHash =
			FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
				FString::Printf(
					TEXT("V4SyntheticAnchors|%s|%d"),
					*Intent.IntentHash,
					Manifest.CandidateAnchorCount));
		Manifest.PopulationHash =
			FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
				PopulationCanonical);
		Manifest.ResourceHash =
			FEFCalystoDungeonDirectorMathV4::HashCanonicalText(ResourceCanonical);
		Manifest.ManifestHash =
			UEFCalystoDungeonSubsystem::ComputeManifestHashV4(Manifest);
		Manifest.bIsValid = Manifest.ManifestHash.Len() == 64;
		return Manifest;
	}

	static bool ValidateSyntheticManifest(
		const UEFCalystoDungeonDirectorPolicyV4* Policy,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest,
		FString& OutError)
	{
		const auto CountForCategory = [&Intent](
			const EEFCalystoContentCategoryV4 Category)
		{
			const FEFCalystoResolvedCategoryV4* Resolved =
				FindCategory(Intent, Category);
			return Resolved ? Resolved->DirectiveCount : INDEX_NONE;
		};

		if (!Policy || !Manifest.bIsValid
			|| Manifest.RunSeed != Intent.RunSeed
			|| Manifest.FloorNumber != Intent.FloorNumber
			|| Manifest.GenerationSerial != Intent.GenerationSerial
			|| Manifest.IntentHash != Intent.IntentHash
			|| Manifest.CompanionSnapshotHash != Intent.CompanionSnapshotHash
			|| Manifest.AnchorTopologyHash.Len() != 64
			|| Manifest.PopulationHash.Len() != 64
			|| Manifest.ResourceHash.Len() != 64
			|| Manifest.ManifestHash.Len() != 64
			|| Manifest.ManifestHash
				!= UEFCalystoDungeonSubsystem::ComputeManifestHashV4(Manifest)
			|| Manifest.EnemyCount
				!= CountForCategory(EEFCalystoContentCategoryV4::Enemy)
			|| Manifest.NPCCount
				!= CountForCategory(EEFCalystoContentCategoryV4::NPC)
			|| Manifest.FoodCount
				!= CountForCategory(EEFCalystoContentCategoryV4::Food)
			|| Manifest.ChestCount
				!= CountForCategory(EEFCalystoContentCategoryV4::Chest)
			|| Manifest.LooseLootCount
				!= CountForCategory(EEFCalystoContentCategoryV4::LooseLoot)
			|| Manifest.ClothingCount
				!= CountForCategory(EEFCalystoContentCategoryV4::Clothing)
			|| Manifest.SpecialEventCount
				!= CountForCategory(EEFCalystoContentCategoryV4::SpecialEvent)
			|| Manifest.SpawnedActorCount != Intent.SpawnDirectives.Num()
			|| Manifest.SpawnedActorCount != Manifest.Instances.Num()
			|| Manifest.SpawnedActorCount
				> Policy->SafetyCeilings.MaximumDirectorActors
			|| !FMath::IsNearlyEqual(
				Manifest.RealizedThreatCost,
				Intent.PlannedThreatCost,
				FloatTolerance)
			|| !FMath::IsNearlyEqual(
				Manifest.RealizedResourceCost,
				Intent.PlannedResourceCost,
				FloatTolerance))
		{
			OutError = TEXT("The synthetic V4 manifest is not coherent with its intent.");
			return false;
		}
		return true;
	}

	static int32 FindBandIndex(const int64 FloorNumber)
	{
		if (FloorNumber <= 10)
		{
			return 0;
		}
		if (FloorNumber <= 50)
		{
			return 1;
		}
		if (FloorNumber <= 125)
		{
			return 2;
		}
		return 3;
	}

	template <typename ValueType>
	static double Median(TArray<ValueType>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		if ((Values.Num() & 1) != 0)
		{
			return static_cast<double>(Values[Middle]);
		}
		return (static_cast<double>(Values[Middle - 1])
			+ static_cast<double>(Values[Middle]))
			/ 2.0;
	}

	static bool IsFixedBinomialWithinSixSigma(
		const int64 Observed,
		const int64 Samples,
		const double Probability)
	{
		if (Samples <= 0 || !FMath::IsFinite(Probability)
			|| Probability < 0.0 || Probability > 1.0)
		{
			return false;
		}
		const double Expected = static_cast<double>(Samples) * Probability;
		const double Variance =
			static_cast<double>(Samples) * Probability * (1.0 - Probability);
		return FMath::Abs(static_cast<double>(Observed) - Expected)
			<= 6.0 * FMath::Sqrt(Variance) + 3.0;
	}

	static bool RunSequentialFloorsOneToOneThousand(
		const UEFCalystoDungeonDirectorPolicyV4* Policy,
		FString& OutError)
	{
		const FString ExpectedPolicyHash =
			FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(Policy);
		if (ExpectedPolicyHash.Len() != 64)
		{
			OutError = TEXT("The sequential V4 policy hash is invalid.");
			return false;
		}
		const int64 RunSeed = 53000001;
		FString EcologyHash =
			FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
				TEXT("V4SequentialEcology|Initial"));
		EEFCalystoStyleV4 PreviousStyle = EEFCalystoStyleV4::Standard;
		EEFCalystoThemeV4 PreviousTheme = EEFCalystoThemeV4::Default;
		int32 ConsecutiveStyleCount = 0;
		int32 ConsecutiveThemeCount = 0;
		int32 FloorsWithoutFood = 0;
		int32 FloorsWithoutChest = 0;
		TMap<FName, FCooldownMemory> Cooldowns;
		TSet<FString> SeenIntentHashes;

		for (int64 FloorNumber = 1; FloorNumber <= 1000; ++FloorNumber)
		{
			FEFCalystoResolveContextV4 Context = MakeContext(
				RunSeed,
				FloorNumber,
				FloorNumber,
				EcologyHash);
			Context.PreviousStyle = PreviousStyle;
			Context.PreviousTheme = PreviousTheme;
			Context.ConsecutiveStyleCount = ConsecutiveStyleCount;
			Context.ConsecutiveThemeCount = ConsecutiveThemeCount;
			Context.ConsecutiveFloorsWithoutFood = FloorsWithoutFood;
			Context.ConsecutiveFloorsWithoutChest = FloorsWithoutChest;
			for (const TPair<FName, FCooldownMemory>& Cooldown : Cooldowns)
			{
				const int64 Delta =
					FloorNumber - Cooldown.Value.LastSelectedFloor;
				if (Delta > 0 && Delta <= Cooldown.Value.CooldownFloors)
				{
					Context.CooldownBlockedCatalogIds.Add(Cooldown.Key);
				}
			}
			Context.CooldownBlockedCatalogIds.Sort(
				[](const FName Left, const FName Right)
				{
					return Left.LexicalLess(Right);
				});

			FEFCalystoResolvedFloorIntentV4 Intent;
			if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
					Policy, ExpectedPolicyHash, Context, Intent, OutError))
			{
				OutError = FString::Printf(
					TEXT("Sequential V4 resolve failed at Floor %lld: %s"),
					FloorNumber,
					*OutError);
				return false;
			}
			if (!ValidateIntent(
					Policy, ExpectedPolicyHash, Context, Intent, OutError))
			{
				OutError = FString::Printf(
					TEXT("Sequential Floor %lld failed invariants: %s"),
					FloorNumber,
					*OutError);
				return false;
			}
			if (SeenIntentHashes.Contains(Intent.IntentHash))
			{
				OutError = FString::Printf(
					TEXT("Illegal IntentHash cycle detected at Floor %lld."),
					FloorNumber);
				return false;
			}
			SeenIntentHashes.Add(Intent.IntentHash);

			if ((FloorNumber % 100) == 0)
			{
				FEFCalystoResolvedFloorIntentV4 Replay;
				FString ReplayError;
				if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
						Policy, ExpectedPolicyHash, Context, Replay, ReplayError)
					|| Replay.IntentHash != Intent.IntentHash
					|| Replay.PCGSeed != Intent.PCGSeed)
				{
					OutError = FString::Printf(
						TEXT("Sequential deterministic replay failed at Floor %lld: %s"),
						FloorNumber,
						*ReplayError);
					return false;
				}
			}

			ConsecutiveStyleCount =
				ConsecutiveStyleCount > 0 && Intent.Style == PreviousStyle
					? ConsecutiveStyleCount + 1
					: 1;
			ConsecutiveThemeCount =
				ConsecutiveThemeCount > 0 && Intent.Theme == PreviousTheme
					? ConsecutiveThemeCount + 1
					: 1;
			if (ConsecutiveStyleCount > Context.MaximumConsecutiveStyle
				|| ConsecutiveThemeCount > Context.MaximumConsecutiveTheme)
			{
				OutError = FString::Printf(
					TEXT("V4 anti-streak contract failed at Floor %lld."),
					FloorNumber);
				return false;
			}
			PreviousStyle = Intent.Style;
			PreviousTheme = Intent.Theme;

			const FEFCalystoResolvedCategoryV4* Food =
				FindCategory(Intent, EEFCalystoContentCategoryV4::Food);
			const FEFCalystoResolvedCategoryV4* Chest =
				FindCategory(Intent, EEFCalystoContentCategoryV4::Chest);
			FloorsWithoutFood = Food && Food->DirectiveCount > 0
				? 0
				: FloorsWithoutFood + 1;
			FloorsWithoutChest = Chest && Chest->DirectiveCount > 0
				? 0
				: FloorsWithoutChest + 1;

			auto CommitCooldown = [&Cooldowns, FloorNumber](
				const FName StableId,
				const int32 CooldownFloors)
			{
				if (StableId.IsNone() || CooldownFloors <= 0)
				{
					return;
				}
				FCooldownMemory& Memory = Cooldowns.FindOrAdd(StableId);
				Memory.LastSelectedFloor = FloorNumber;
				Memory.CooldownFloors =
					FMath::Max(Memory.CooldownFloors, CooldownFloors);
			};
			for (const FEFCalystoSpawnInstanceDirectiveV4& Directive :
				Intent.SpawnDirectives)
			{
				CommitCooldown(Directive.CatalogId, Directive.CooldownFloors);
			}
			for (const FEFCalystoChestContentDirectiveV4& Content :
				Intent.ChestContentDirectives)
			{
				CommitCooldown(Content.ContentCatalogId, Content.CooldownFloors);
			}

			EcologyHash =
				FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
					FString::Printf(
						TEXT("V4SequentialEcology|%s|%lld|%s"),
						*EcologyHash,
						FloorNumber,
						*Intent.IntentHash));
		}

		if (SeenIntentHashes.Num() != 1000)
		{
			OutError = TEXT("The V4 sequential sweep did not produce 1,000 unique intents.");
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4IntentManifest100KStatisticalStressTest,
	"NoShellForWinter.CalystoDungeon.V4.Stress.IntentManifest100KAndFloors1To1000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4IntentManifest100KStatisticalStressTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4StatisticalStressTests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	if (!Policy)
	{
		AddError(TEXT("Could not construct the native V4 policy."));
		return false;
	}
	FString Error;
	if (!Policy->Validate(Error))
	{
		AddError(FString::Printf(
			TEXT("The native V4 policy failed before stress: %s"), *Error));
		return false;
	}
	if (!ValidateAuthoredTierContract(Policy, Error))
	{
		AddError(Error);
		return false;
	}

	const FString PolicyHash =
		FEFCalystoDungeonDirectorResolverV4::GetPolicyHash(Policy);
	if (PolicyHash.Len() != 64)
	{
		AddError(TEXT("The validated V4 policy did not produce a SHA-256 hash."));
		return false;
	}

	int64 StyleCounts[3] = {};
	int64 ThemeCounts[3] = {};
	FBinomialAccumulator Presence[7];
	TArray<FDepthBand> Bands;
	Bands.Emplace(TEXT("Early 1-10"));
	Bands.Emplace(TEXT("Rising 25-50"));
	Bands.Emplace(TEXT("Deep 100-125"));
	Bands.Emplace(TEXT("Winter 200-1000"));
	Bands[0].DungeonEdges.Reserve(20000);
	Bands[0].ThreatBudgets.Reserve(20000);
	Bands[1].DungeonEdges.Reserve(20000);
	Bands[1].ThreatBudgets.Reserve(20000);
	Bands[2].DungeonEdges.Reserve(30000);
	Bands[2].ThreatBudgets.Reserve(30000);
	Bands[3].DungeonEdges.Reserve(30000);
	Bands[3].ThreatBudgets.Reserve(30000);

	int32 IntentCount = 0;
	int32 ManifestCount = 0;
	const double StartSeconds = FPlatformTime::Seconds();
	for (int32 FloorIndex = 0;
		FloorIndex < RepresentativeFloorCount;
		++FloorIndex)
	{
		const int64 FloorNumber = RepresentativeFloors[FloorIndex];
		for (int32 SampleIndex = 0;
			SampleIndex < SamplesPerRepresentativeFloor;
			++SampleIndex)
		{
			const int32 GlobalIndex =
				FloorIndex * SamplesPerRepresentativeFloor + SampleIndex;
			const int64 RunSeed = 41000001LL + GlobalIndex;
			const FString EcologyHash =
				FEFCalystoDungeonDirectorMathV4::HashCanonicalText(
					FString::Printf(
						TEXT("V4StressEcology|%lld|%lld"),
						RunSeed,
						FloorNumber));
			const FEFCalystoResolveContextV4 Context = MakeContext(
				RunSeed, FloorNumber, 1, EcologyHash);
			FEFCalystoResolvedFloorIntentV4 Intent;
			Error.Reset();
			if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
					Policy, PolicyHash, Context, Intent, Error))
			{
				AddError(FString::Printf(
					TEXT("V4 stress resolve %d failed at Floor %lld: %s"),
					GlobalIndex,
					FloorNumber,
					*Error));
				return false;
			}
			++IntentCount;
			if (!ValidateIntent(
					Policy, PolicyHash, Context, Intent, Error))
			{
				AddError(FString::Printf(
					TEXT("V4 stress intent %d at Floor %lld is invalid: %s"),
					GlobalIndex,
					FloorNumber,
					*Error));
				return false;
			}

			const int32 StyleIndex = static_cast<int32>(Intent.Style);
			const int32 ThemeIndex = static_cast<int32>(Intent.Theme);
			if (StyleIndex < 0 || StyleIndex >= 3
				|| ThemeIndex < 0 || ThemeIndex >= 3)
			{
				AddError(TEXT("V4 resolved an unknown Style or Theme."));
				return false;
			}
			++StyleCounts[StyleIndex];
			++ThemeCounts[ThemeIndex];
			for (int32 CategoryIndex = 0; CategoryIndex < 7; ++CategoryIndex)
			{
				const auto Category =
					static_cast<EEFCalystoContentCategoryV4>(CategoryIndex);
				const FEFCalystoResolvedCategoryV4* Resolved =
					FindCategory(Intent, Category);
				if (!Resolved)
				{
					AddError(TEXT("A probability accumulator could not find its category."));
					return false;
				}
				Presence[CategoryIndex].Add(
					Resolved->EffectiveChance, Resolved->bPresent);
			}

			const int32 BandIndex = FindBandIndex(FloorNumber);
			if (!Bands.IsValidIndex(BandIndex))
			{
				AddError(TEXT("A representative V4 floor has no depth band."));
				return false;
			}
			Bands[BandIndex].DungeonEdges.Add(Intent.DungeonSize.X);
			Bands[BandIndex].ThreatBudgets.Add(Intent.ThreatBudget);

			const FEFCalystoRealizedFloorManifestV4 Manifest =
				MakeSyntheticManifest(Intent);
			++ManifestCount;
			if (!ValidateSyntheticManifest(Policy, Intent, Manifest, Error))
			{
				AddError(FString::Printf(
					TEXT("V4 synthetic manifest %d at Floor %lld is invalid: %s"),
					GlobalIndex,
					FloorNumber,
					*Error));
				return false;
			}

			if (((GlobalIndex + 1) % 10000) == 0)
			{
				FEFCalystoResolvedFloorIntentV4 Replay;
				FString ReplayError;
				if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
						Policy, PolicyHash, Context, Replay, ReplayError)
					|| Replay.IntentHash != Intent.IntentHash
					|| Replay.PCGSeed != Intent.PCGSeed)
				{
					AddError(FString::Printf(
						TEXT("Deterministic V4 replay checkpoint failed at sample %d: %s"),
						GlobalIndex + 1,
						*ReplayError));
					return false;
				}
			}
		}
	}

	TestEqual(
		TEXT("The primary V4 stress sweep resolves exactly 100,000 intents."),
		IntentCount,
		StressIntentCount);
	TestEqual(
		TEXT("The primary V4 stress sweep hashes exactly 100,000 manifests."),
		ManifestCount,
		StressIntentCount);

	for (const FEFCalystoStyleProfileV4& Style : Policy->Styles)
	{
		const int32 Index = static_cast<int32>(Style.Style);
		TestTrue(
			*FString::Printf(
				TEXT("Style %d selection frequency remains within six sigma."),
				Index),
			Index >= 0 && Index < 3
				&& IsFixedBinomialWithinSixSigma(
					StyleCounts[Index],
					StressIntentCount,
					Style.SelectionProbability));
	}
	for (const FEFCalystoThemeProfileV4& Theme : Policy->Themes)
	{
		const int32 Index = static_cast<int32>(Theme.Theme);
		TestTrue(
			*FString::Printf(
				TEXT("Theme %d selection frequency remains within six sigma."),
				Index),
			Index >= 0 && Index < 3
				&& IsFixedBinomialWithinSixSigma(
					ThemeCounts[Index],
					StressIntentCount,
					Theme.SelectionProbability));
	}
	for (int32 CategoryIndex = 0; CategoryIndex < 7; ++CategoryIndex)
	{
		TestEqual(
			*FString::Printf(
				TEXT("Category %d contributes exactly 100,000 presence samples."),
				CategoryIndex),
			Presence[CategoryIndex].Samples,
			static_cast<int64>(StressIntentCount));
		TestTrue(
			*FString::Printf(
				TEXT("Category %d presence remains inside heterogeneous six-sigma bounds."),
				CategoryIndex),
			Presence[CategoryIndex].IsWithinSixSigma());
	}

	double FirstSizeMedian = 0.0;
	double FirstThreatMedian = 0.0;
	double PreviousSizeMedian = 0.0;
	double PreviousThreatMedian = 0.0;
	for (int32 BandIndex = 0; BandIndex < Bands.Num(); ++BandIndex)
	{
		FDepthBand& Band = Bands[BandIndex];
		const double SizeMedian = Median(Band.DungeonEdges);
		const double ThreatMedian = Median(Band.ThreatBudgets);
		AddInfo(FString::Printf(
			TEXT("V4 depth band %s: size median %.3f, threat median %.3f, "
				"samples %d."),
			*Band.Label,
			SizeMedian,
			ThreatMedian,
			Band.DungeonEdges.Num()));
		if (BandIndex == 0)
		{
			FirstSizeMedian = SizeMedian;
			FirstThreatMedian = ThreatMedian;
		}
		else
		{
			TestTrue(
				*FString::Printf(
					TEXT("Size median does not regress in band %s."),
					*Band.Label),
				SizeMedian >= PreviousSizeMedian);
			TestTrue(
				*FString::Printf(
					TEXT("Threat median does not regress in band %s."),
					*Band.Label),
				ThreatMedian + ThreatMedianTrendTolerance >= PreviousThreatMedian);
		}
		PreviousSizeMedian = SizeMedian;
		PreviousThreatMedian = ThreatMedian;
	}
	TestTrue(
		TEXT("Deep V4 median size is strictly greater than early median size."),
		PreviousSizeMedian > FirstSizeMedian);
	TestTrue(
		TEXT("Deep V4 median threat is strictly greater than early median threat."),
		PreviousThreatMedian > FirstThreatMedian);

	Error.Reset();
	if (!RunSequentialFloorsOneToOneThousand(Policy, Error))
	{
		AddError(Error);
		return false;
	}

	AddInfo(FString::Printf(
		TEXT("V4 resolved 100,000 representative intents, hashed 100,000 "
			"synthetic manifests and swept sequential Floors 1-1000 in %.2f "
			"seconds. PCG, NavMesh and actor spawning are intentionally outside "
			"this world-free native gate."),
		FPlatformTime::Seconds() - StartSeconds));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
