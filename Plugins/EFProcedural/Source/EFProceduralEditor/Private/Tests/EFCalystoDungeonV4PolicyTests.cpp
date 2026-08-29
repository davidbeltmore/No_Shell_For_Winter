#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

#include <limits>

namespace EFCalystoV4Tests
{
	static constexpr float FloatTolerance = 0.0001f;

	static UEFCalystoDungeonDirectorPolicyV4* MakePolicy()
	{
		return NewObject<UEFCalystoDungeonDirectorPolicyV4>(GetTransientPackage());
	}

	static FEFCalystoResolveContextV4 MakeContext(
		const int64 RunSeed,
		const int64 FloorNumber,
		const int64 GenerationSerial = 1)
	{
		FEFCalystoResolveContextV4 Context;
		Context.RunSeed = RunSeed;
		Context.FloorNumber = FloorNumber;
		Context.GenerationSerial = GenerationSerial;
		Context.EcologyHash = TEXT("EFCalystoV4.NativeTest.Ecology");
		Context.PerformanceEMA = 0.5f;
		Context.CompanionSnapshotHash =
			FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
				Context.CompanionSnapshot);
		return Context;
	}

	static FEFCalystoCategoryProfileV4* FindMutableCategory(
		TArray<FEFCalystoCategoryProfileV4>& Categories,
		const EEFCalystoContentCategoryV4 Category)
	{
		return Categories.FindByPredicate(
			[Category](const FEFCalystoCategoryProfileV4& Candidate)
			{
				return Candidate.Category == Category;
			});
	}

	static FEFCalystoChestContentEntryV4* FindMutableWintersRecall(
		UEFCalystoDungeonDirectorPolicyV4* Policy)
	{
		if (!Policy || Policy->Styles.IsEmpty())
		{
			return nullptr;
		}
		FEFCalystoCategoryProfileV4* Chests = FindMutableCategory(
			Policy->Styles[0].Categories,
			EEFCalystoContentCategoryV4::Chest);
		if (!Chests)
		{
			return nullptr;
		}
		const FName RecallId(TEXT("Item.CompanionRevival.WintersRecall"));
		return Chests->ChestContentsCatalog.FindByPredicate(
			[RecallId](const FEFCalystoChestContentEntryV4& Entry)
			{
				return Entry.StableId == RecallId;
			});
	}

	static void ConfigureCompanionRecallLifecyclePolicy(
		UEFCalystoDungeonDirectorPolicyV4* Policy)
	{
		auto ConfigureCategories = [](TArray<FEFCalystoCategoryProfileV4>& Categories)
		{
			for (FEFCalystoCategoryProfileV4& Category : Categories)
			{
				if (Category.Category == EEFCalystoContentCategoryV4::Decoration
					|| Category.Category == EEFCalystoContentCategoryV4::Lighting)
				{
					continue;
				}
				const bool bNPC = Category.Category == EEFCalystoContentCategoryV4::NPC;
				const bool bChest = Category.Category == EEFCalystoContentCategoryV4::Chest;
				Category.bEnabled = bNPC || bChest;
				Category.bBlocked = !Category.bEnabled;
				Category.Chance.ChanceAtFloor1 = Category.bEnabled ? .90f : 0.0f;
				Category.Chance.ChanceAtFloor100 = Category.bEnabled ? .90f : 0.0f;
				if (!Category.bEnabled)
				{
					continue;
				}
				Category.Limits.MinimumWhenPresent = 1;
				Category.Limits.MaximumPerFloor = 1;
				Category.PityAfterEmptyFloors = 0;
				for (FEFCalystoCatalogEntryV4& Entry : Category.Catalog)
				{
					if (bNPC)
					{
						const bool bGeneralist =
							Entry.StableId == FName(TEXT("NPC.Companion.Generalist.Female"))
							|| Entry.StableId == FName(TEXT("NPC.Companion.Generalist.Male"));
						Entry.Rule = bGeneralist
							? EEFCalystoCatalogRuleV4::Allow
							: EEFCalystoCatalogRuleV4::Block;
					}
					if (Entry.Rule == EEFCalystoCatalogRuleV4::Allow)
					{
						Entry.FirstEligibleFloor = 1;
						Entry.MaxPerVariant = 1;
						Entry.CooldownFloors = 0;
					}
				}
				if (bChest)
				{
					Category.MinimumChestContentAttempts = 1;
					Category.MaximumChestContentAttempts = 1;
					for (FEFCalystoChestContentEntryV4& Content : Category.ChestContentsCatalog)
					{
						Content.Rule = Content.StableId ==
							FName(TEXT("Item.CompanionRevival.WintersRecall"))
							? EEFCalystoCatalogRuleV4::Allow
							: EEFCalystoCatalogRuleV4::Block;
					}
				}
			}
		};
		for (FEFCalystoStyleProfileV4& Style : Policy->Styles)
		{
			ConfigureCategories(Style.Categories);
		}
		for (FEFCalystoThemeProfileV4& Theme : Policy->Themes)
		{
			ConfigureCategories(Theme.Categories);
		}
	}

	static void ConfigureEnemyBundleFixture(
		UEFCalystoDungeonDirectorPolicyV4* Policy,
		const int32 MinimumWhenActive,
		const int32 MaximumPerFloor,
		const float ThreatBudget,
		const bool bOnlyTwoEntries,
		const float FirstEntryCost,
		const float OtherEntryCost,
		const float FirstEntryWeight,
		const float OtherEntryWeight)
	{
		if (!Policy)
		{
			return;
		}
		auto ConfigureCategories = [=](TArray<FEFCalystoCategoryProfileV4>& Categories)
		{
			for (FEFCalystoCategoryProfileV4& Category : Categories)
			{
				if (Category.Category == EEFCalystoContentCategoryV4::Decoration
					|| Category.Category == EEFCalystoContentCategoryV4::Lighting)
				{
					continue;
				}
				const bool bEnemy =
					Category.Category == EEFCalystoContentCategoryV4::Enemy;
				Category.bEnabled = bEnemy;
				Category.bBlocked = !bEnemy;
				Category.Chance.ChanceAtFloor1 = bEnemy ? .90f : 0.0f;
				Category.Chance.ChanceAtFloor100 = bEnemy ? .90f : 0.0f;
				if (!bEnemy)
				{
					continue;
				}
				Category.Limits.MinimumWhenPresent = MinimumWhenActive;
				Category.Limits.MaximumPerFloor = MaximumPerFloor;
				Category.Tiers.AtFloor1.Common = .90f;
				Category.Tiers.AtFloor1.Uncommon = 0.0f;
				Category.Tiers.AtFloor1.Rare = 0.0f;
				Category.Tiers.AtFloor1.Epic = 0.0f;
				Category.Tiers.AtFloor1.RefreshNothing();
				Category.Tiers.AtFloor100 = Category.Tiers.AtFloor1;
				for (int32 Index = 0; Index < Category.Catalog.Num(); ++Index)
				{
					FEFCalystoCatalogEntryV4& Entry = Category.Catalog[Index];
					Entry.Rule = !bOnlyTwoEntries || Index < 2
						? EEFCalystoCatalogRuleV4::Allow
						: EEFCalystoCatalogRuleV4::Block;
					Entry.bTierAgnostic = true;
					Entry.AllowedTiers = {EEFCalystoRarityTierV4::Common};
					Entry.FirstEligibleFloor = 1;
					Entry.MaxPerVariant = FMath::Max(1, MaximumPerFloor);
					Entry.CooldownFloors = 0;
					Entry.InitialFraction = 1.0f;
					Entry.DeepShare = Index == 0
						? FirstEntryWeight
						: OtherEntryWeight;
					Entry.BaseThreatCost = Index == 0
						? FirstEntryCost
						: OtherEntryCost;
				}
			}
		};

		for (FEFCalystoStyleProfileV4& Style : Policy->Styles)
		{
			Style.SelectionProbability =
				Style.Style == EEFCalystoStyleV4::Standard ? .999998f : .000001f;
			Style.Threat.EarlyBudget = ThreatBudget;
			Style.Threat.DeepBudget = ThreatBudget;
			Style.Threat.RelativeRange = 0.0f;
			ConfigureCategories(Style.Categories);
		}
		for (FEFCalystoThemeProfileV4& Theme : Policy->Themes)
		{
			Theme.SelectionProbability =
				Theme.Theme == EEFCalystoThemeV4::Default ? .999998f : .000001f;
			Theme.Threat.EarlyBudget = ThreatBudget;
			Theme.Threat.DeepBudget = ThreatBudget;
			Theme.Threat.RelativeRange = 0.0f;
			ConfigureCategories(Theme.Categories);
		}
	}

	static void ConfigureRepeatedNPCFixture(
		UEFCalystoDungeonDirectorPolicyV4* Policy)
	{
		if (!Policy)
		{
			return;
		}
		auto ConfigureCategories = [](TArray<FEFCalystoCategoryProfileV4>& Categories)
		{
			for (FEFCalystoCategoryProfileV4& Category : Categories)
			{
				if (Category.Category == EEFCalystoContentCategoryV4::Decoration
					|| Category.Category == EEFCalystoContentCategoryV4::Lighting)
				{
					continue;
				}
				const bool bNPC = Category.Category == EEFCalystoContentCategoryV4::NPC;
				Category.bEnabled = bNPC;
				Category.bBlocked = !bNPC;
				Category.Chance.ChanceAtFloor1 = bNPC ? .90f : 0.0f;
				Category.Chance.ChanceAtFloor100 = bNPC ? .90f : 0.0f;
				if (!bNPC)
				{
					continue;
				}
				Category.Limits.MinimumWhenPresent = 1;
				Category.Limits.MaximumPerFloor = 1;
				Category.Tiers.AtFloor1.Common = .90f;
				Category.Tiers.AtFloor1.Uncommon = 0.0f;
				Category.Tiers.AtFloor1.Rare = 0.0f;
				Category.Tiers.AtFloor1.Epic = 0.0f;
				Category.Tiers.AtFloor1.RefreshNothing();
				Category.Tiers.AtFloor100 = Category.Tiers.AtFloor1;
				for (int32 Index = 0; Index < Category.Catalog.Num(); ++Index)
				{
					FEFCalystoCatalogEntryV4& Entry = Category.Catalog[Index];
					Entry.Rule = Index == 0
						? EEFCalystoCatalogRuleV4::Allow
						: EEFCalystoCatalogRuleV4::Block;
					Entry.bTierAgnostic = true;
					Entry.AllowedTiers = {EEFCalystoRarityTierV4::Common};
					Entry.FirstEligibleFloor = 1;
					Entry.MaxPerVariant = 1;
					Entry.CooldownFloors = 0;
					Entry.InitialFraction = 1.0f;
					Entry.DeepShare = 1.0f;
				}
			}
		};
		for (FEFCalystoStyleProfileV4& Style : Policy->Styles)
		{
			Style.SelectionProbability =
				Style.Style == EEFCalystoStyleV4::Standard ? .999998f : .000001f;
			ConfigureCategories(Style.Categories);
		}
		for (FEFCalystoThemeProfileV4& Theme : Policy->Themes)
		{
			Theme.SelectionProbability =
				Theme.Theme == EEFCalystoThemeV4::Default ? .999998f : .000001f;
			ConfigureCategories(Theme.Categories);
		}
	}

	static const FEFCalystoResolvedCategoryV4* FindResolvedCategory(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const EEFCalystoContentCategoryV4 Category)
	{
		return Intent.Categories.FindByPredicate(
			[Category](const FEFCalystoResolvedCategoryV4& Candidate)
			{
				return Candidate.Category == Category;
			});
	}

	static bool Resolve(
		const UEFCalystoDungeonDirectorPolicyV4* Policy,
		const FEFCalystoResolveContextV4& Context,
		FEFCalystoResolvedFloorIntentV4& OutIntent,
		FString* OutError = nullptr)
	{
		FString Error;
		const bool bResolved = FEFCalystoDungeonDirectorResolverV4::Resolve(
			Policy, Context, OutIntent, Error);
		if (OutError)
		{
			*OutError = Error;
		}
		return bResolved;
	}

	static bool IsWithinSixSigma(
		const int32 Observed,
		const int32 Samples,
		const double Probability)
	{
		const double Expected = static_cast<double>(Samples) * Probability;
		const double Variance =
			static_cast<double>(Samples) * Probability * (1.0 - Probability);
		return FMath::Abs(static_cast<double>(Observed) - Expected)
			<= 6.0 * FMath::Sqrt(Variance) + 3.0;
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

	static float AuthoredEntryWeight(
		const FEFCalystoCatalogEntryV4& Entry,
		const int64 FloorNumber)
	{
		if (FloorNumber < Entry.FirstEligibleFloor
			|| Entry.Rule != EEFCalystoCatalogRuleV4::Allow)
		{
			return 0.0f;
		}
		const float T = Entry.RampFloors <= 0
			? 1.0f
			: FMath::Clamp(
				static_cast<float>(FloorNumber - Entry.FirstEligibleFloor)
					/ static_cast<float>(Entry.RampFloors),
				0.0f,
				1.0f);
		const float Smooth = T * T * (3.0f - 2.0f * T);
		return Entry.DeepShare
			* FMath::Lerp(Entry.InitialFraction, 1.0f, Smooth);
	}

	struct FCategoryExpectation
	{
		EEFCalystoContentCategoryV4 Category;
		float Floor1;
		float Floor100;
		int32 Maximum;
	};

	struct FCatalogExpectation
	{
		const TCHAR* StableId;
		const TCHAR* ClassPath;
		const TCHAR* Archetype;
		EEFCalystoGenderV4 Gender;
		float InitialFraction;
		float DeepShare;
		int32 RampFloors;
		int64 FirstEligibleFloor;
		float ThreatCost;
		int32 MaxPerVariant;
		int32 CooldownFloors;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4DefaultPolicyContractTest,
	"NoShellForWinter.CalystoDungeon.V4.Policy.DefaultProfilesCatalogsAndCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4DefaultPolicyContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	TestNotNull(TEXT("The native V4 policy is constructible."), Policy);
	if (!Policy)
	{
		return false;
	}

	FString ValidationError;
	TestTrue(
		*FString::Printf(TEXT("The native V4 defaults validate: %s"), *ValidationError),
		Policy->Validate(ValidationError));
	TestEqual(TEXT("SchemaVersion is exactly 4."), Policy->SchemaVersion, 4);
	TestEqual(TEXT("GeneratorVersion is exactly 4."), Policy->GeneratorVersion, 4);
	TestEqual(
		TEXT("PolicyId is stable."),
		Policy->PolicyId,
		FName(TEXT("CalystoDungeonDirectorV4")));

	const FEFCalystoSafetyCeilingsV4& Caps = Policy->SafetyCeilings;
	TestEqual(TEXT("Enemy hard cap is 25."), Caps.MaximumEnemies, 25);
	TestEqual(TEXT("NPC hard cap is 4 including party."), Caps.MaximumNPCs, 4);
	TestEqual(TEXT("Food hard cap is 30."), Caps.MaximumFood, 30);
	TestEqual(TEXT("Chest hard cap is 10."), Caps.MaximumChests, 10);
	TestEqual(TEXT("Loose-loot hard cap is 4."), Caps.MaximumLooseLoot, 4);
	TestEqual(TEXT("Clothing hard cap is 10."), Caps.MaximumClothing, 10);
	TestEqual(TEXT("Special-event hard cap is 6."), Caps.MaximumSpecialEvents, 6);
	TestEqual(TEXT("Initial Director actor hard cap is 89."), Caps.MaximumDirectorActors, 89);
	TestTrue(
		TEXT("Threat-budget hard cap is 60."),
		FMath::IsNearlyEqual(Caps.MaximumThreatBudget, 60.0f, FloatTolerance));

	const TArray<int32> ExpectedSizes = {26, 27, 28, 29, 30};
	TestEqual(TEXT("Only five Calysto sizes start certified."), Policy->ValidatedDungeonSizes.Num(), 5);
	for (const int32 Size : ExpectedSizes)
	{
		TestTrue(
			*FString::Printf(TEXT("Certified size %d is present."), Size),
			Policy->ValidatedDungeonSizes.Contains(Size));
	}
	for (int32 Size = 18; Size <= 25; ++Size)
	{
		TestFalse(
			*FString::Printf(TEXT("Uncertified size %d remains disabled."), Size),
			Policy->ValidatedDungeonSizes.Contains(Size));
	}

	TestEqual(TEXT("V4 defines exactly three Styles."), Policy->Styles.Num(), 3);
	TestEqual(TEXT("V4 defines exactly three Themes."), Policy->Themes.Num(), 3);
	float StyleSelectionMass = 0.0f;
	for (const FEFCalystoStyleProfileV4& Style : Policy->Styles)
	{
		StyleSelectionMass += Style.SelectionProbability;
	}
	float ThemeSelectionMass = 0.0f;
	for (const FEFCalystoThemeProfileV4& Theme : Policy->Themes)
	{
		ThemeSelectionMass += Theme.SelectionProbability;
	}
	TestTrue(TEXT("Style selection sums exactly to one."), FMath::IsNearlyEqual(StyleSelectionMass, 1.0f, FloatTolerance));
	TestTrue(TEXT("Theme selection sums exactly to one."), FMath::IsNearlyEqual(ThemeSelectionMass, 1.0f, FloatTolerance));

	auto CheckLayout = [this](
		const FString& Owner,
		const FEFCalystoLayoutProfileV4& Layout,
		const int32 MinSize,
		const int32 MaxSize,
		const float Size1,
		const float Size100,
		const float Branch1,
		const float Branch100,
		const FEFCalystoPertRangeV4& Anchors)
	{
		TestEqual(*FString::Printf(TEXT("%s minimum size."), *Owner), Layout.MinimumDungeonEdge, MinSize);
		TestEqual(*FString::Printf(TEXT("%s maximum size."), *Owner), Layout.MaximumDungeonEdge, MaxSize);
		TestTrue(*FString::Printf(TEXT("%s Floor-1 typical size."), *Owner), FMath::IsNearlyEqual(Layout.EarlySizeMode, Size1, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s Floor-100 typical size."), *Owner), FMath::IsNearlyEqual(Layout.DeepSizeMode, Size100, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s early branching."), *Owner), FMath::IsNearlyEqual(Layout.BranchingChance.ChanceAtFloor1, Branch1, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s deep branching."), *Owner), FMath::IsNearlyEqual(Layout.BranchingChance.ChanceAtFloor100, Branch100, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s anchor minimum."), *Owner), FMath::IsNearlyEqual(Layout.CandidateAnchorDensity.Min, Anchors.Min, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s anchor mode."), *Owner), FMath::IsNearlyEqual(Layout.CandidateAnchorDensity.Mode, Anchors.Mode, FloatTolerance));
		TestTrue(*FString::Printf(TEXT("%s anchor maximum."), *Owner), FMath::IsNearlyEqual(Layout.CandidateAnchorDensity.Max, Anchors.Max, FloatTolerance));
	};

	auto CheckCategories = [this](
		const FString& Owner,
		const TArray<FEFCalystoCategoryProfileV4>& Categories,
		const TArray<FCategoryExpectation>& Expected)
	{
		TestEqual(*FString::Printf(TEXT("%s defines all nine categories."), *Owner), Categories.Num(), 9);
		for (const FCategoryExpectation& Item : Expected)
		{
			const FEFCalystoCategoryProfileV4* Category =
				UEFCalystoDungeonDirectorPolicyV4::FindCategory(Categories, Item.Category);
			TestNotNull(*FString::Printf(TEXT("%s contains category %d."), *Owner, static_cast<int32>(Item.Category)), Category);
			if (!Category)
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-1 chance."), *Owner, static_cast<int32>(Item.Category)), FMath::IsNearlyEqual(Category->Chance.ChanceAtFloor1, Item.Floor1, FloatTolerance));
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-100 chance."), *Owner, static_cast<int32>(Item.Category)), FMath::IsNearlyEqual(Category->Chance.ChanceAtFloor100, Item.Floor100, FloatTolerance));
			TestEqual(*FString::Printf(TEXT("%s category %d maximum."), *Owner, static_cast<int32>(Item.Category)), Category->Limits.MaximumPerFloor, Item.Maximum);
			const int32 ExpectedMinimum =
				(Item.Category == EEFCalystoContentCategoryV4::Enemy
					|| Item.Category == EEFCalystoContentCategoryV4::Food
					|| Item.Category == EEFCalystoContentCategoryV4::Chest)
				? 1
				: 0;
			TestEqual(*FString::Printf(TEXT("%s category %d conditional minimum."), *Owner, static_cast<int32>(Item.Category)), Category->Limits.MinimumWhenPresent, ExpectedMinimum);
			TestEqual(*FString::Printf(TEXT("%s category %d pity threshold."), *Owner, static_cast<int32>(Item.Category)), Category->PityAfterEmptyFloors,
				Item.Category == EEFCalystoContentCategoryV4::Food ? 2
				: (Item.Category == EEFCalystoContentCategoryV4::Chest ? 4 : 0));
			FString TierError;
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-1 tiers are valid: %s"), *Owner, static_cast<int32>(Item.Category), *TierError), FEFCalystoDungeonDirectorMathV4::ValidateTierMix(Category->Tiers.AtFloor1, &TierError));
			TierError.Reset();
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-100 tiers are valid: %s"), *Owner, static_cast<int32>(Item.Category), *TierError), FEFCalystoDungeonDirectorMathV4::ValidateTierMix(Category->Tiers.AtFloor100, &TierError));
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-1 Nothing is derived."), *Owner, static_cast<int32>(Item.Category)), FMath::IsNearlyEqual(Category->Tiers.AtFloor1.Nothing, Category->Tiers.AtFloor1.GetCalculatedNothing(), FloatTolerance));
			TestTrue(*FString::Printf(TEXT("%s category %d Floor-100 Nothing is derived."), *Owner, static_cast<int32>(Item.Category)), FMath::IsNearlyEqual(Category->Tiers.AtFloor100.Nothing, Category->Tiers.AtFloor100.GetCalculatedNothing(), FloatTolerance));
		}
	};

	const TArray<FCategoryExpectation> StandardCategories = {
		{EEFCalystoContentCategoryV4::Enemy, .70f, .90f, 25},
		{EEFCalystoContentCategoryV4::NPC, .08f, .15f, 4},
		{EEFCalystoContentCategoryV4::Food, .75f, .55f, 30},
		{EEFCalystoContentCategoryV4::Chest, .25f, .45f, 10},
		{EEFCalystoContentCategoryV4::LooseLoot, .05f, .20f, 4},
		{EEFCalystoContentCategoryV4::Clothing, .08f, .18f, 10},
		{EEFCalystoContentCategoryV4::SpecialEvent, 0.0f, 0.0f, 6},
		{EEFCalystoContentCategoryV4::Decoration, 0.0f, 0.0f, 0},
		{EEFCalystoContentCategoryV4::Lighting, 0.0f, 0.0f, 0}};
	const TArray<FCategoryExpectation> CompactCategories = {
		{EEFCalystoContentCategoryV4::Enemy, .65f, .88f, 20},
		{EEFCalystoContentCategoryV4::NPC, .06f, .12f, 3},
		{EEFCalystoContentCategoryV4::Food, .65f, .45f, 20},
		{EEFCalystoContentCategoryV4::Chest, .20f, .35f, 6},
		{EEFCalystoContentCategoryV4::LooseLoot, .04f, .14f, 3},
		{EEFCalystoContentCategoryV4::Clothing, .06f, .14f, 6},
		{EEFCalystoContentCategoryV4::SpecialEvent, 0.0f, 0.0f, 4},
		{EEFCalystoContentCategoryV4::Decoration, 0.0f, 0.0f, 0},
		{EEFCalystoContentCategoryV4::Lighting, 0.0f, 0.0f, 0}};
	const TArray<FCategoryExpectation> BranchingCategories = {
		{EEFCalystoContentCategoryV4::Enemy, .75f, .90f, 25},
		{EEFCalystoContentCategoryV4::NPC, .10f, .20f, 4},
		{EEFCalystoContentCategoryV4::Food, .80f, .62f, 30},
		{EEFCalystoContentCategoryV4::Chest, .30f, .55f, 10},
		{EEFCalystoContentCategoryV4::LooseLoot, .08f, .25f, 4},
		{EEFCalystoContentCategoryV4::Clothing, .12f, .25f, 10},
		{EEFCalystoContentCategoryV4::SpecialEvent, 0.0f, 0.0f, 6},
		{EEFCalystoContentCategoryV4::Decoration, 0.0f, 0.0f, 0},
		{EEFCalystoContentCategoryV4::Lighting, 0.0f, 0.0f, 0}};
	const TArray<FCategoryExpectation> ForgeCategories = {
		{EEFCalystoContentCategoryV4::Enemy, .80f, .90f, 25},
		{EEFCalystoContentCategoryV4::NPC, .03f, .08f, 2},
		{EEFCalystoContentCategoryV4::Food, .55f, .35f, 18},
		{EEFCalystoContentCategoryV4::Chest, .30f, .55f, 10},
		{EEFCalystoContentCategoryV4::LooseLoot, .08f, .22f, 4},
		{EEFCalystoContentCategoryV4::Clothing, .10f, .20f, 8},
		{EEFCalystoContentCategoryV4::SpecialEvent, 0.0f, 0.0f, 6},
		{EEFCalystoContentCategoryV4::Decoration, 0.0f, 0.0f, 0},
		{EEFCalystoContentCategoryV4::Lighting, 0.0f, 0.0f, 0}};
	const TArray<FCategoryExpectation> ShrineCategories = {
		{EEFCalystoContentCategoryV4::Enemy, .55f, .78f, 18},
		{EEFCalystoContentCategoryV4::NPC, .15f, .30f, 4},
		{EEFCalystoContentCategoryV4::Food, .80f, .65f, 30},
		{EEFCalystoContentCategoryV4::Chest, .35f, .60f, 8},
		{EEFCalystoContentCategoryV4::LooseLoot, .05f, .18f, 4},
		{EEFCalystoContentCategoryV4::Clothing, .12f, .22f, 10},
		{EEFCalystoContentCategoryV4::SpecialEvent, 0.0f, 0.0f, 6},
		{EEFCalystoContentCategoryV4::Decoration, 0.0f, 0.0f, 0},
		{EEFCalystoContentCategoryV4::Lighting, 0.0f, 0.0f, 0}};

	const FEFCalystoStyleProfileV4* Standard = Policy->FindStyle(EEFCalystoStyleV4::Standard);
	const FEFCalystoStyleProfileV4* Compact = Policy->FindStyle(EEFCalystoStyleV4::Compact);
	const FEFCalystoStyleProfileV4* Branching = Policy->FindStyle(EEFCalystoStyleV4::Branching);
	TestNotNull(TEXT("Standard is an explicit Style."), Standard);
	TestNotNull(TEXT("Compact is an explicit Style."), Compact);
	TestNotNull(TEXT("Branching is an explicit Style."), Branching);
	if (Standard && Compact && Branching)
	{
		TestTrue(TEXT("Standard selection is .50."), FMath::IsNearlyEqual(Standard->SelectionProbability, .50f, FloatTolerance));
		TestTrue(TEXT("Compact selection is .25."), FMath::IsNearlyEqual(Compact->SelectionProbability, .25f, FloatTolerance));
		TestTrue(TEXT("Branching selection is .25."), FMath::IsNearlyEqual(Branching->SelectionProbability, .25f, FloatTolerance));
		CheckLayout(TEXT("Standard"), Standard->Layout, 18, 30, 20, 30, .45f, .55f, {.25f, .32f, .45f, 4.0f});
		CheckLayout(TEXT("Compact"), Compact->Layout, 18, 26, 19, 25, .30f, .40f, {.25f, .30f, .40f, 4.0f});
		CheckLayout(TEXT("Branching"), Branching->Layout, 20, 30, 21, 30, .60f, .70f, {.30f, .38f, .50f, 4.0f});
		CheckCategories(TEXT("Standard"), Standard->Categories, StandardCategories);
		CheckCategories(TEXT("Compact"), Compact->Categories, CompactCategories);
		CheckCategories(TEXT("Branching"), Branching->Categories, BranchingCategories);
		TestTrue(TEXT("Standard volatility is .67."), FMath::IsNearlyEqual(Standard->Volatility, .67f, FloatTolerance));
		TestTrue(TEXT("Compact volatility is .45."), FMath::IsNearlyEqual(Compact->Volatility, .45f, FloatTolerance));
		TestTrue(TEXT("Branching volatility is .80."), FMath::IsNearlyEqual(Branching->Volatility, .80f, FloatTolerance));
		const FEFCalystoContextTraitsV4 CompactTraits = Compact->GetAuthoredTraits();
		const FEFCalystoContextTraitsV4 BranchingTraits = Branching->GetAuthoredTraits();
		TestTrue(TEXT("Compact exposes exact Danger."), FMath::IsNearlyEqual(CompactTraits.Danger, .10f, FloatTolerance));
		TestTrue(TEXT("Compact exposes exact Safe."), FMath::IsNearlyEqual(CompactTraits.Safe, .05f, FloatTolerance));
		TestTrue(TEXT("Compact exposes exact Abundance."), FMath::IsNearlyEqual(CompactTraits.Abundance, -.10f, FloatTolerance));
		TestTrue(TEXT("Compact exposes exact Mystery."), FMath::IsNearlyEqual(CompactTraits.Mystery, -.10f, FloatTolerance));
		TestTrue(TEXT("Branching exposes exact Mystery."), FMath::IsNearlyEqual(BranchingTraits.Mystery, .25f, FloatTolerance));
		TestTrue(TEXT("Branching exposes exact Clothing influence."), FMath::IsNearlyEqual(BranchingTraits.ClothingInfluence, .10f, FloatTolerance));
	}

	const FEFCalystoThemeProfileV4* Default = Policy->FindTheme(EEFCalystoThemeV4::Default);
	const FEFCalystoThemeProfileV4* Forge = Policy->FindTheme(EEFCalystoThemeV4::Forge);
	const FEFCalystoThemeProfileV4* Shrine = Policy->FindTheme(EEFCalystoThemeV4::Shrine);
	TestNotNull(TEXT("Default is an explicit Theme."), Default);
	TestNotNull(TEXT("Forge is an explicit Theme."), Forge);
	TestNotNull(TEXT("Shrine is an explicit Theme."), Shrine);
	if (Default && Forge && Shrine)
	{
		TestTrue(TEXT("Default selection is .60."), FMath::IsNearlyEqual(Default->SelectionProbability, .60f, FloatTolerance));
		TestTrue(TEXT("Forge selection is .25."), FMath::IsNearlyEqual(Forge->SelectionProbability, .25f, FloatTolerance));
		TestTrue(TEXT("Shrine selection is .15."), FMath::IsNearlyEqual(Shrine->SelectionProbability, .15f, FloatTolerance));
		CheckLayout(TEXT("Default"), Default->Layout, 18, 30, 20, 30, .45f, .55f, {.25f, .32f, .45f, 4.0f});
		CheckLayout(TEXT("Forge"), Forge->Layout, 20, 30, 22, 30, .35f, .55f, {.30f, .38f, .50f, 4.0f});
		CheckLayout(TEXT("Shrine"), Shrine->Layout, 18, 28, 19, 27, .45f, .65f, {.25f, .34f, .45f, 4.0f});
		CheckCategories(TEXT("Default"), Default->Categories, StandardCategories);
		CheckCategories(TEXT("Forge"), Forge->Categories, ForgeCategories);
		CheckCategories(TEXT("Shrine"), Shrine->Categories, ShrineCategories);
		TestTrue(TEXT("Default is neutral topology, not a fallback asset."), Default->RoomType.IsNull());
		TestEqual(TEXT("Forge keeps its Calysto Room Type soft reference."), Forge->RoomType.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.DA_RoomForge")));
		TestEqual(TEXT("Shrine keeps its Calysto Room Type soft reference."), Shrine->RoomType.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.DA_RoomShrine")));
		const FEFCalystoContextTraitsV4 ForgeTraits = Forge->GetAuthoredTraits();
		const FEFCalystoContextTraitsV4 ShrineTraits = Shrine->GetAuthoredTraits();
		TestTrue(TEXT("Forge Danger is +.30."), FMath::IsNearlyEqual(ForgeTraits.Danger, .30f, FloatTolerance));
		TestTrue(TEXT("Forge Safe is -.25."), FMath::IsNearlyEqual(ForgeTraits.Safe, -.25f, FloatTolerance));
		TestTrue(TEXT("Forge Abundance is -.20."), FMath::IsNearlyEqual(ForgeTraits.Abundance, -.20f, FloatTolerance));
		TestTrue(TEXT("Forge Mystery is +.20."), FMath::IsNearlyEqual(ForgeTraits.Mystery, .20f, FloatTolerance));
		TestTrue(TEXT("Shrine Danger is -.20."), FMath::IsNearlyEqual(ShrineTraits.Danger, -.20f, FloatTolerance));
		TestTrue(TEXT("Shrine Safe is +.30."), FMath::IsNearlyEqual(ShrineTraits.Safe, .30f, FloatTolerance));
		TestTrue(TEXT("Shrine Abundance is +.20."), FMath::IsNearlyEqual(ShrineTraits.Abundance, .20f, FloatTolerance));
		TestTrue(TEXT("Shrine Mystery is +.35."), FMath::IsNearlyEqual(ShrineTraits.Mystery, .35f, FloatTolerance));
	}

	auto CheckNativeLighting = [this](const FString& Owner, const FEFCalystoLightingPolicyV4& Lighting)
	{
		TestEqual(*FString::Printf(TEXT("%s lighting remains Calysto Native."), *Owner), Lighting.Mode, EEFCalystoLightingModeV4::CalystoNative);
		TestEqual(*FString::Printf(TEXT("%s torch class remains native."), *Owner), Lighting.TorchClass.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.BP_WallTorch_C")));
		TestTrue(*FString::Printf(TEXT("%s torch height is native-safe."), *Owner), FMath::IsFinite(Lighting.Height) && Lighting.Height >= 0.0f && Lighting.Height <= 1000.0f);
		TestTrue(*FString::Printf(TEXT("%s tile interval is native-safe."), *Owner), Lighting.TileDistance >= 1 && Lighting.TileDistance <= 100);
	};
	for (const FEFCalystoStyleProfileV4& Style : Policy->Styles)
	{
		CheckNativeLighting(FString::Printf(TEXT("Style %d"), static_cast<int32>(Style.Style)), Style.Lighting);
	}
	for (const FEFCalystoThemeProfileV4& Theme : Policy->Themes)
	{
		CheckNativeLighting(FString::Printf(TEXT("Theme %d"), static_cast<int32>(Theme.Theme)), Theme.Lighting);
	}

	// A smaller native interval is a supported Calysto control, not a policy
	// failure. This guards the regression that previously redirected to HUB
	// whenever an author changed 10 to 5 in the V4 Data Asset.
	if (!Policy->Styles.IsEmpty())
	{
		Policy->Styles[0].Lighting.TileDistance = 5;
		FString LightingError;
		TestTrue(TEXT("Native torch tile interval 5 validates."), Policy->Validate(LightingError));
		TestTrue(TEXT("Native torch tile interval 5 has no validation error."), LightingError.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4ExactCatalogInventoryTest,
	"NoShellForWinter.CalystoDungeon.V4.Policy.ExactEnemyCompanionAndResourceCatalogs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4ExactCatalogInventoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	TestNotNull(TEXT("A V4 policy exists for catalog verification."), Policy);
	if (!Policy)
	{
		return false;
	}

	const TArray<FCatalogExpectation> Enemies = {
		{TEXT("Enemy.Melee.Female"), TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C"), TEXT("Melee"), EEFCalystoGenderV4::Female, 1.0f, .25f, 0, 1, 2.0f, 8, 0},
		{TEXT("Enemy.Melee.Male"), TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C"), TEXT("Melee"), EEFCalystoGenderV4::Male, 1.0f, .25f, 0, 1, 2.0f, 8, 0},
		{TEXT("Enemy.MM.Female"), TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C"), TEXT("MM"), EEFCalystoGenderV4::Female, .25f, .0333333f, 3, 1, 1.0f, 12, 0},
		{TEXT("Enemy.MM.Male"), TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C"), TEXT("MM"), EEFCalystoGenderV4::Male, .25f, .0666667f, 3, 1, 1.0f, 12, 0},
		{TEXT("Enemy.Defender.Female"), TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C"), TEXT("Defender"), EEFCalystoGenderV4::Female, .15f, .025f, 4, 1, 3.0f, 6, 0},
		{TEXT("Enemy.Defender.Male"), TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C"), TEXT("Defender"), EEFCalystoGenderV4::Male, .15f, .025f, 4, 1, 3.0f, 6, 0},
		{TEXT("Enemy.Ranged.Female"), TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"), TEXT("Ranged"), EEFCalystoGenderV4::Female, .15f, .12f, 4, 2, 3.0f, 6, 0},
		{TEXT("Enemy.Ranged.Male"), TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"), TEXT("Ranged"), EEFCalystoGenderV4::Male, .15f, .08f, 4, 2, 3.0f, 6, 0},
		{TEXT("Enemy.Gun.Female"), TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C"), TEXT("Gun"), EEFCalystoGenderV4::Female, .10f, .025f, 5, 3, 3.0f, 5, 1},
		{TEXT("Enemy.Gun.Male"), TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C"), TEXT("Gun"), EEFCalystoGenderV4::Male, .10f, .025f, 5, 3, 3.0f, 5, 1},
		{TEXT("Enemy.Mage.Female"), TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C"), TEXT("Mage"), EEFCalystoGenderV4::Female, .10f, .05f, 6, 3, 4.0f, 4, 2},
		{TEXT("Enemy.Mage.Male"), TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C"), TEXT("Mage"), EEFCalystoGenderV4::Male, .10f, .05f, 6, 3, 4.0f, 4, 2}};

	const TArray<FCatalogExpectation> Companions = {
		{TEXT("NPC.Companion.Generalist.Female"), TEXT("/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"), TEXT("Generalist"), EEFCalystoGenderV4::Female, 1.0f, .25f, 0, 1, 1.0f, 4, 0},
		{TEXT("NPC.Companion.Generalist.Male"), TEXT("/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C"), TEXT("Generalist"), EEFCalystoGenderV4::Male, 1.0f, .25f, 0, 1, 1.0f, 4, 0},
		{TEXT("NPC.Companion.Melee.Female"), TEXT("/Game/_Game/Characters/Female/ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C"), TEXT("Melee"), EEFCalystoGenderV4::Female, .20f, .175f, 5, 3, 1.0f, 4, 0},
		{TEXT("NPC.Companion.Melee.Male"), TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C"), TEXT("Melee"), EEFCalystoGenderV4::Male, .20f, .175f, 5, 3, 1.0f, 4, 0},
		{TEXT("NPC.Companion.Ranged.Female"), TEXT("/Game/_Game/Characters/Female/ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C"), TEXT("Ranged"), EEFCalystoGenderV4::Female, .10f, .075f, 6, 5, 1.0f, 4, 0},
		{TEXT("NPC.Companion.Ranged.Male"), TEXT("/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C"), TEXT("Ranged"), EEFCalystoGenderV4::Male, .10f, .075f, 6, 5, 1.0f, 4, 0}};

	const TArray<FCatalogExpectation> Clothing = {
		{TEXT("Clothing.Armor.ACF.Default"), TEXT("/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4.BP_CalystoArmorPickupV4_C"), TEXT("Armor"), EEFCalystoGenderV4::Any, 1.0f, 1.0f, 0, 1, 1.0f, 10, 0}};

	auto CheckExpectedEntries = [this](
		const FString& Owner,
		const TArray<FEFCalystoCatalogEntryV4>& Catalog,
		const TArray<FCatalogExpectation>& Expected,
		const EEFCalystoLifecycleV4 ExpectedLifecycle)
	{
		TestEqual(*FString::Printf(TEXT("%s exact entry count."), *Owner), Catalog.Num(), Expected.Num());
		TSet<FName> SeenIds;
		for (const FEFCalystoCatalogEntryV4& Entry : Catalog)
		{
			TestFalse(*FString::Printf(TEXT("%s has no duplicate ID %s."), *Owner, *Entry.StableId.ToString()), SeenIds.Contains(Entry.StableId));
			SeenIds.Add(Entry.StableId);
			TestFalse(*FString::Printf(TEXT("%s excludes Dummy entry %s."), *Owner, *Entry.StableId.ToString()), Entry.StableId.ToString().Contains(TEXT("Dummy"), ESearchCase::IgnoreCase));
		}
		for (const FCatalogExpectation& Item : Expected)
		{
			const FName ExpectedId(Item.StableId);
			const FEFCalystoCatalogEntryV4* Entry = Catalog.FindByPredicate(
				[ExpectedId](const FEFCalystoCatalogEntryV4& Candidate)
				{
					return Candidate.StableId == ExpectedId;
				});
			TestNotNull(*FString::Printf(TEXT("%s contains %s."), *Owner, Item.StableId), Entry);
			if (!Entry)
			{
				continue;
			}
			TestFalse(*FString::Printf(TEXT("%s %s has a readable name."), *Owner, Item.StableId), Entry->Name.IsEmpty());
			TestEqual(*FString::Printf(TEXT("%s %s exact class."), *Owner, Item.StableId), Entry->ActorClass.ToSoftObjectPath().ToString(), FString(Item.ClassPath));
			TestEqual(*FString::Printf(TEXT("%s %s archetype."), *Owner, Item.StableId), Entry->Archetype, FName(Item.Archetype));
			TestEqual(*FString::Printf(TEXT("%s %s gender."), *Owner, Item.StableId), Entry->Gender, Item.Gender);
			TestTrue(*FString::Printf(TEXT("%s %s initial fraction."), *Owner, Item.StableId), FMath::IsNearlyEqual(Entry->InitialFraction, Item.InitialFraction, FloatTolerance));
			TestTrue(*FString::Printf(TEXT("%s %s deep share."), *Owner, Item.StableId), FMath::IsNearlyEqual(Entry->DeepShare, Item.DeepShare, FloatTolerance));
			TestEqual(*FString::Printf(TEXT("%s %s ramp."), *Owner, Item.StableId), Entry->RampFloors, Item.RampFloors);
			TestEqual(*FString::Printf(TEXT("%s %s inclusive floor gate."), *Owner, Item.StableId), Entry->FirstEligibleFloor, Item.FirstEligibleFloor);
			TestTrue(*FString::Printf(TEXT("%s %s threat cost."), *Owner, Item.StableId), FMath::IsNearlyEqual(Entry->BaseThreatCost, Item.ThreatCost, FloatTolerance));
			TestEqual(*FString::Printf(TEXT("%s %s per-variant cap."), *Owner, Item.StableId), Entry->MaxPerVariant, Item.MaxPerVariant);
			TestEqual(*FString::Printf(TEXT("%s %s cooldown."), *Owner, Item.StableId), Entry->CooldownFloors, Item.CooldownFloors);
			TestEqual(*FString::Printf(TEXT("%s %s lifecycle."), *Owner, Item.StableId), Entry->Lifecycle, ExpectedLifecycle);
			TestTrue(*FString::Printf(TEXT("%s %s is tier-agnostic."), *Owner, Item.StableId), Entry->bTierAgnostic);
		}
	};

	auto CheckProfileCatalogs = [&](const FString& Owner, const TArray<FEFCalystoCategoryProfileV4>& Categories)
	{
		const FEFCalystoCategoryProfileV4* Enemy = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Categories, EEFCalystoContentCategoryV4::Enemy);
		const FEFCalystoCategoryProfileV4* NPC = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Categories, EEFCalystoContentCategoryV4::NPC);
		const FEFCalystoCategoryProfileV4* ClothingCategory = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Categories, EEFCalystoContentCategoryV4::Clothing);
		TestNotNull(*FString::Printf(TEXT("%s enemy category exists."), *Owner), Enemy);
		TestNotNull(*FString::Printf(TEXT("%s NPC category exists."), *Owner), NPC);
		TestNotNull(*FString::Printf(TEXT("%s Clothing category exists."), *Owner), ClothingCategory);
		if (Enemy)
		{
			CheckExpectedEntries(Owner + TEXT(" Enemies"), Enemy->Catalog, Enemies, EEFCalystoLifecycleV4::FloorLocal);
		}
		if (NPC)
		{
			CheckExpectedEntries(Owner + TEXT(" NPCs"), NPC->Catalog, Companions, EEFCalystoLifecycleV4::Recruitable);
		}
		if (ClothingCategory)
		{
			CheckExpectedEntries(Owner + TEXT(" Clothing"), ClothingCategory->Catalog, Clothing, EEFCalystoLifecycleV4::FloorLocal);
			if (ClothingCategory->Catalog.Num() == 1)
			{
				TestEqual(
					*FString::Printf(TEXT("%s starter Clothing entry is authored as Common."), *Owner),
					ClothingCategory->Catalog[0].Tier,
					EEFCalystoRarityTierV4::Common);
				TestEqual(
					*FString::Printf(TEXT("%s starter Clothing entry is eligible from Floor 1 inclusively."), *Owner),
					ClothingCategory->Catalog[0].FirstEligibleFloor,
					static_cast<int64>(1));
			}
		}
		for (const FEFCalystoCategoryProfileV4& Category : Categories)
		{
			for (const FEFCalystoCatalogEntryV4& Entry : Category.Catalog)
			{
				TestFalse(*FString::Printf(TEXT("%s contains no Dummy in any category (%s)."), *Owner, *Entry.StableId.ToString()), Entry.StableId.ToString().Contains(TEXT("Dummy"), ESearchCase::IgnoreCase));
			}
		}
	};

	const FName RecallId(TEXT("Item.CompanionRevival.WintersRecall"));
	const FSoftObjectPath RecallClassPath(
		TEXT("/Game/_Game/Items/Companions/"
			"BP_Item_WintersRecall.BP_Item_WintersRecall_C"));
	auto CheckWintersRecallContract = [this, RecallId, RecallClassPath](
		const FString& Owner,
		const TArray<FEFCalystoCategoryProfileV4>& Categories)
	{
		int32 RecallIdCount = 0;
		int32 RecallClassCount = 0;
		const FEFCalystoChestContentEntryV4* Recall = nullptr;
		for (const FEFCalystoCategoryProfileV4& Category : Categories)
		{
			for (const FEFCalystoCatalogEntryV4& Entry : Category.Catalog)
			{
				TestFalse(
					*FString::Printf(TEXT("%s never aliases Winter's Recall in category %d actor catalogs."), *Owner, static_cast<int32>(Category.Category)),
					Entry.StableId == RecallId || Entry.ActorClass.ToSoftObjectPath() == RecallClassPath);
			}
			for (const FEFCalystoChestContentEntryV4& Entry : Category.ChestContentsCatalog)
			{
				const bool bExactId = Entry.StableId == RecallId;
				const bool bExactClass = Entry.ContentClass.ToSoftObjectPath() == RecallClassPath;
				RecallIdCount += bExactId ? 1 : 0;
				RecallClassCount += bExactClass ? 1 : 0;
				if (bExactId || bExactClass)
				{
					TestEqual(
						*FString::Printf(TEXT("%s preserves the bidirectional Winter's Recall ID/class mapping."), *Owner),
						bExactId,
						bExactClass);
					TestEqual(
						*FString::Printf(TEXT("%s stores Winter's Recall only in Chest Contents."), *Owner),
						Category.Category,
						EEFCalystoContentCategoryV4::Chest);
					if (bExactId && bExactClass)
					{
						Recall = &Entry;
					}
				}
			}
		}
		TestEqual(*FString::Printf(TEXT("%s contains exactly one Winter's Recall Stable ID."), *Owner), RecallIdCount, 1);
		TestEqual(*FString::Printf(TEXT("%s contains exactly one Winter's Recall class."), *Owner), RecallClassCount, 1);
		TestNotNull(*FString::Printf(TEXT("%s contains the exact Winter's Recall entry."), *Owner), Recall);
		if (!Recall)
		{
			return;
		}
		TestEqual(*FString::Printf(TEXT("%s allows Winter's Recall."), *Owner), Recall->Rule, EEFCalystoCatalogRuleV4::Allow);
		TestEqual(*FString::Printf(TEXT("%s keeps Winter's Recall Epic."), *Owner), Recall->Tier, EEFCalystoRarityTierV4::Epic);
		TestEqual(*FString::Printf(TEXT("%s gates Winter's Recall from Floor 1 through graveyard eligibility."), *Owner), Recall->FirstEligibleFloor, static_cast<int64>(1));
		TestEqual(*FString::Printf(TEXT("%s caps Winter's Recall at one per floor."), *Owner), Recall->MaxPerFloor, 1);
		TestEqual(*FString::Printf(TEXT("%s gives Winter's Recall an eight-floor cooldown."), *Owner), Recall->CooldownFloors, 8);
		TestTrue(*FString::Printf(TEXT("%s requires confirmed graveyard eligibility for Winter's Recall."), *Owner), Recall->bRequiresGraveyardEligibility);
	};

	for (const FEFCalystoStyleProfileV4& Style : Policy->Styles)
	{
		const FString Owner = FString::Printf(TEXT("Style %d"), static_cast<int32>(Style.Style));
		CheckProfileCatalogs(Owner, Style.Categories);
		CheckWintersRecallContract(Owner, Style.Categories);
	}
	for (const FEFCalystoThemeProfileV4& Theme : Policy->Themes)
	{
		const FString Owner = FString::Printf(TEXT("Theme %d"), static_cast<int32>(Theme.Theme));
		CheckProfileCatalogs(Owner, Theme.Categories);
		CheckWintersRecallContract(Owner, Theme.Categories);
	}

	const FEFCalystoStyleProfileV4* Standard = Policy->FindStyle(EEFCalystoStyleV4::Standard);
	if (!Standard)
	{
		return false;
	}
	const FEFCalystoCategoryProfileV4* Food = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Standard->Categories, EEFCalystoContentCategoryV4::Food);
	const FEFCalystoCategoryProfileV4* Chests = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Standard->Categories, EEFCalystoContentCategoryV4::Chest);
	const FEFCalystoCategoryProfileV4* LooseLoot = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Standard->Categories, EEFCalystoContentCategoryV4::LooseLoot);
	TestNotNull(TEXT("Standard Food catalog exists."), Food);
	TestNotNull(TEXT("Standard Chest catalog exists."), Chests);
	TestNotNull(TEXT("Standard Loose Loot catalog exists."), LooseLoot);
	if (Food)
	{
		const TArray<FName> FoodIds = {TEXT("Apple"), TEXT("Bread"), TEXT("Water"), TEXT("CookedMeat")};
		TestEqual(TEXT("Initial Food catalog contains four exact entries."), Food->Catalog.Num(), FoodIds.Num());
		for (const FName Id : FoodIds)
		{
			TestTrue(*FString::Printf(TEXT("Food catalog contains %s."), *Id.ToString()), Food->Catalog.ContainsByPredicate([Id](const auto& Entry) { return Entry.StableId == Id; }));
		}
	}
	if (LooseLoot)
	{
		TestEqual(TEXT("Initial Loose Loot catalog contains one calibrated entry."), LooseLoot->Catalog.Num(), 1);
		if (!LooseLoot->Catalog.IsEmpty())
		{
			TestEqual(TEXT("Loose Loot starts with RareAlcohol07."), LooseLoot->Catalog[0].StableId, FName(TEXT("RareAlcohol07")));
			TestEqual(TEXT("RareAlcohol07 unlocks inclusively at Floor 4."), LooseLoot->Catalog[0].FirstEligibleFloor, static_cast<int64>(4));
		}
	}
	if (Chests)
	{
		TestEqual(TEXT("Two V4 project-owned chest containers are authored."), Chests->Catalog.Num(), 2);
		TestEqual(TEXT("Each chest can make at most three content attempts."), Chests->MaximumChestContentAttempts, 3);
		TestEqual(TEXT("Chest content catalog contains Health, Mana and Winter's Recall."), Chests->ChestContentsCatalog.Num(), 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4ProbabilityAndDomainMathTest,
	"NoShellForWinter.CalystoDungeon.V4.Math.ProbabilityCountsTiersAndDomains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4ProbabilityAndDomainMathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	TestNotNull(TEXT("A V4 policy exists for deterministic math."), Policy);
	if (!Policy)
	{
		return false;
	}
	const FString PolicyHash = Policy->GetPolicyHash();
	TestEqual(TEXT("A valid V4 policy hash is SHA-256 length."), PolicyHash.Len(), 64);
	if (PolicyHash.IsEmpty())
	{
		return false;
	}

	FString DomainError;
	TestTrue(
		*FString::Printf(TEXT("All V4 counter-RNG domains are unique: %s"), *DomainError),
		FEFCalystoDungeonDirectorMathV4::ValidateDomainUniqueness(&DomainError));
	const uint64 Domains[] = {
		EFCalystoDungeonDomainsV4::Style,
		EFCalystoDungeonDomainsV4::Theme,
		EFCalystoDungeonDomainsV4::Shape,
		EFCalystoDungeonDomainsV4::AnchorDensity,
		EFCalystoDungeonDomainsV4::ThreatBudget,
		EFCalystoDungeonDomainsV4::CategoryBlend,
		EFCalystoDungeonDomainsV4::CategoryPresence,
		EFCalystoDungeonDomainsV4::CategoryCount,
		EFCalystoDungeonDomainsV4::Tier,
		EFCalystoDungeonDomainsV4::CatalogSource,
		EFCalystoDungeonDomainsV4::CatalogEntry,
		EFCalystoDungeonDomainsV4::EnemyBundle,
		EFCalystoDungeonDomainsV4::EnemyBudgetBacktrack,
		EFCalystoDungeonDomainsV4::NPC,
		EFCalystoDungeonDomainsV4::ChestContents,
		EFCalystoDungeonDomainsV4::Winter,
		EFCalystoDungeonDomainsV4::Anchors,
		EFCalystoDungeonDomainsV4::RunDNA,
		EFCalystoDungeonDomainsV4::SmoothFloorNoise,
		EFCalystoDungeonDomainsV4::Jitter,
		EFCalystoDungeonDomainsV4::PerformanceOutcome};
	TSet<uint64> UniqueDomains;
	for (const uint64 Domain : Domains)
	{
		TestTrue(TEXT("No V4 RNG domain is zero."), Domain != 0);
		UniqueDomains.Add(Domain);
	}
	TestEqual(TEXT("All 21 domain IDs are distinct."), UniqueDomains.Num(), static_cast<int32>(UE_ARRAY_COUNT(Domains)));

	FEFCalystoResolveContextV4 Context = MakeContext(99173, 37, 4);
	const uint64 StableId = FEFCalystoDungeonDirectorMathV4::StableNameId(TEXT("V4.NativeTest.Entity"));
	const uint64 StyleA = FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(Context, PolicyHash, EFCalystoDungeonDomainsV4::Style, StableId, 0);
	const uint64 StyleReplay = FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(Context, PolicyHash, EFCalystoDungeonDomainsV4::Style, StableId, 0);
	const uint64 ThemeA = FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(Context, PolicyHash, EFCalystoDungeonDomainsV4::Theme, StableId, 0);
	const uint64 StyleNextDraw = FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(Context, PolicyHash, EFCalystoDungeonDomainsV4::Style, StableId, 1);
	TestEqual(TEXT("The same counter tuple replays bit-exactly."), StyleReplay, StyleA);
	TestTrue(TEXT("Style and Theme domains are independent."), StyleA != ThemeA);
	TestTrue(TEXT("DrawIndex advances only its requested counter."), StyleA != StyleNextDraw);
	TestEqual(
		TEXT("Querying another domain never perturbs the original counter."),
		FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(Context, PolicyHash, EFCalystoDungeonDomainsV4::Style, StableId, 0),
		StyleA);
	FEFCalystoResolveContextV4 RerollContext = Context;
	++RerollContext.GenerationSerial;
	TestTrue(
		TEXT("GenerationSerial produces a new deterministic stream."),
		FEFCalystoDungeonDirectorMathV4::DeriveCounterValue(RerollContext, PolicyHash, EFCalystoDungeonDomainsV4::Style, StableId, 0) != StyleA);

	static constexpr int32 BernoulliSamples = 20000;
	static constexpr double BernoulliProbability = 0.37;
	int32 BernoulliObserved = 0;
	for (int32 Index = 0; Index < BernoulliSamples; ++Index)
	{
		FEFCalystoResolveContextV4 SampleContext = MakeContext(100000 + Index, 12, 1);
		BernoulliObserved += FEFCalystoDungeonDirectorMathV4::Bernoulli(
			BernoulliProbability,
			SampleContext,
			PolicyHash,
			EFCalystoDungeonDomainsV4::CategoryPresence,
			StableId)
			? 1
			: 0;
	}
	TestTrue(
		TEXT("Counter-based Bernoulli stays within six-sigma binomial tolerance."),
		IsWithinSixSigma(BernoulliObserved, BernoulliSamples, BernoulliProbability));
	TestFalse(TEXT("NaN Bernoulli fails closed."), FEFCalystoDungeonDirectorMathV4::Bernoulli(std::numeric_limits<double>::quiet_NaN(), Context, PolicyHash, EFCalystoDungeonDomainsV4::CategoryPresence));

	const FEFCalystoPertRangeV4 Pert = {2.0f, 5.0f, 10.0f, 4.0f};
	static constexpr int32 PertSamples = 10000;
	double PertSum = 0.0;
	for (int32 Index = 0; Index < PertSamples; ++Index)
	{
		FEFCalystoResolveContextV4 SampleContext = MakeContext(200000 + Index, 20, 1);
		const float Sample = FEFCalystoDungeonDirectorMathV4::SamplePERT(
			Pert,
			0.5f,
			SampleContext,
			PolicyHash,
			EFCalystoDungeonDomainsV4::Shape,
			StableId);
		if (Sample < Pert.Min || Sample > Pert.Max || !FMath::IsFinite(Sample))
		{
			AddError(TEXT("A V4 PERT sample escaped its finite authored range."));
			return false;
		}
		PertSum += Sample;
	}
	const double ExpectedPertMean = (Pert.Min + 4.0 * Pert.Mode + Pert.Max) / 6.0;
	TestTrue(
		TEXT("PERT empirical mean tracks its analytic Beta-PERT mean."),
		FMath::Abs(PertSum / PertSamples - ExpectedPertMean) < 0.08);
	FEFCalystoPertRangeV4 InvalidPert = Pert;
	InvalidPert.Min = 8.0f;
	InvalidPert.Mode = 5.0f;
	TestTrue(
		TEXT("An inverted PERT fails closed to its minimum sentinel."),
		FMath::IsNearlyEqual(
			FEFCalystoDungeonDirectorMathV4::SamplePERT(InvalidPert, .5f, Context, PolicyHash, EFCalystoDungeonDomainsV4::Shape),
			InvalidPert.Min,
			FloatTolerance));

	int64 LowCountTotal = 0;
	int64 HighCountTotal = 0;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		FEFCalystoResolveContextV4 SampleContext = MakeContext(300000 + Index, 15, 1);
		const int32 Low = FEFCalystoDungeonDirectorMathV4::SampleCount(1, 10, .25f, SampleContext, PolicyHash, StableId);
		const int32 High = FEFCalystoDungeonDirectorMathV4::SampleCount(1, 10, .80f, SampleContext, PolicyHash, StableId);
		if (Low < 1 || Low > 10 || High < Low || High > 10)
		{
			AddError(TEXT("V4 count sampling violated limits or chance monotonicity."));
			return false;
		}
		LowCountTotal += Low;
		HighCountTotal += High;
	}
	TestTrue(TEXT("Higher category chance produces a higher mean amount."), HighCountTotal > LowCountTotal);
	TestEqual(TEXT("Zero chance produces zero attempts."), FEFCalystoDungeonDirectorMathV4::SampleCount(1, 10, 0.0f, Context, PolicyHash, StableId), 0);

	TestTrue(TEXT("Style endpoint survives probability blend."), FMath::IsNearlyEqual(FEFCalystoDungeonDirectorMathV4::BlendProbability(.15f, .70f, 0.0f), .15f, FloatTolerance));
	TestTrue(TEXT("Theme endpoint survives probability blend."), FMath::IsNearlyEqual(FEFCalystoDungeonDirectorMathV4::BlendProbability(.15f, .70f, 1.0f), .70f, FloatTolerance));
	const float MidBlend = FEFCalystoDungeonDirectorMathV4::BlendProbability(.15f, .70f, .37f);
	TestTrue(TEXT("A Style+Theme blend remains inside its endpoints."), MidBlend >= .15f && MidBlend <= .70f);

	FEFCalystoTierMixV4 StyleTiers;
	StyleTiers.Common = .60f;
	StyleTiers.Uncommon = .20f;
	StyleTiers.Rare = .08f;
	StyleTiers.Epic = .02f;
	StyleTiers.RefreshNothing();
	FEFCalystoTierMixV4 ThemeTiers;
	ThemeTiers.Common = .30f;
	ThemeTiers.Uncommon = .28f;
	ThemeTiers.Rare = .20f;
	ThemeTiers.Epic = .12f;
	ThemeTiers.RefreshNothing();
	const FEFCalystoTierMixV4 BlendedTiers = FEFCalystoDungeonDirectorMathV4::BlendTiers(StyleTiers, ThemeTiers, .42f);
	TestTrue(TEXT("Tier blending preserves the 0.90 selectable ceiling."), BlendedTiers.GetSelectableMass() <= .9001f);
	TestTrue(TEXT("Tier blending preserves at least 0.10 Nothing."), BlendedTiers.GetCalculatedNothing() >= .0999f);

	FEFCalystoTierMixV4 EqualTiers;
	EqualTiers.Common = EqualTiers.Uncommon = EqualTiers.Rare = EqualTiers.Epic = .225f;
	EqualTiers.RefreshNothing();
	const TSet<EEFCalystoRarityTierV4> AllNormalTiers = {
		EEFCalystoRarityTierV4::Common,
		EEFCalystoRarityTierV4::Uncommon,
		EEFCalystoRarityTierV4::Rare,
		EEFCalystoRarityTierV4::Epic};
	const FEFCalystoTierMixV4 HighMystery = FEFCalystoDungeonDirectorMathV4::ApplyMystery(EqualTiers, 1.0f, AllNormalTiers);
	const FEFCalystoTierMixV4 LowMystery = FEFCalystoDungeonDirectorMathV4::ApplyMystery(EqualTiers, -1.0f, AllNormalTiers);
	TestTrue(TEXT("Mystery preserves selectable mass."), FMath::IsNearlyEqual(HighMystery.GetSelectableMass(), .90f, FloatTolerance));
	TestTrue(TEXT("Mystery preserves permanent Nothing."), FMath::IsNearlyEqual(HighMystery.GetCalculatedNothing(), .10f, FloatTolerance));
	TestTrue(TEXT("Positive Mystery is capped at a 3:1 Epic/Common tilt."), HighMystery.Epic / HighMystery.Common <= 3.0001f);
	TestTrue(TEXT("Negative Mystery is capped at a 3:1 Common/Epic tilt."), LowMystery.Common / LowMystery.Epic <= 3.0001f);

	FEFCalystoTierMixV4 InvalidTiers = EqualTiers;
	InvalidTiers.Epic = .23f;
	TestFalse(TEXT("Tier mass above .90 fails closed."), FEFCalystoDungeonDirectorMathV4::ValidateTierMix(InvalidTiers));
	InvalidTiers = EqualTiers;
	InvalidTiers.Rare = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN tier mass fails closed."), FEFCalystoDungeonDirectorMathV4::ValidateTierMix(InvalidTiers));

	TestTrue(TEXT("Progression is zero on Floor 1."), FMath::IsNearlyEqual(FEFCalystoDungeonDirectorMathV4::Progression(1, 12.0), 0.0));
	TestTrue(TEXT("Progression increases with depth."), FEFCalystoDungeonDirectorMathV4::Progression(100, 12.0) > FEFCalystoDungeonDirectorMathV4::Progression(10, 12.0));
	TestTrue(TEXT("Progression remains below one."), FEFCalystoDungeonDirectorMathV4::Progression(1000, 12.0) < 1.0);
	TestTrue(TEXT("Invalid progression tau fails closed."), FMath::IsNearlyEqual(FEFCalystoDungeonDirectorMathV4::Progression(10, 0.0), 0.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4EnemyRampsAndLevelsTest,
	"NoShellForWinter.CalystoDungeon.V4.Enemies.RampsGenderTierBandsAndPERT",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4EnemyRampsAndLevelsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	const FEFCalystoStyleProfileV4* Standard = Policy ? Policy->FindStyle(EEFCalystoStyleV4::Standard) : nullptr;
	const FEFCalystoCategoryProfileV4* Enemy = Standard
		? UEFCalystoDungeonDirectorPolicyV4::FindCategory(Standard->Categories, EEFCalystoContentCategoryV4::Enemy)
		: nullptr;
	TestNotNull(TEXT("Standard enemy catalog exists for ramp verification."), Enemy);
	if (!Policy || !Enemy)
	{
		return false;
	}

	auto ArchetypeWeight = [&](const FName Archetype, const int64 Floor)
	{
		float Weight = 0.0f;
		for (const FEFCalystoCatalogEntryV4& Entry : Enemy->Catalog)
		{
			if (Entry.Archetype == Archetype)
			{
				Weight += AuthoredEntryWeight(Entry, Floor);
			}
		}
		return Weight;
	};
	auto ArchetypeShare = [&](const FName Archetype, const int64 Floor)
	{
		float Total = 0.0f;
		for (const FEFCalystoCatalogEntryV4& Entry : Enemy->Catalog)
		{
			Total += AuthoredEntryWeight(Entry, Floor);
		}
		return Total > 0.0f ? ArchetypeWeight(Archetype, Floor) / Total : 0.0f;
	};

	TestTrue(TEXT("Floor 1 Melee share is approximately 93.9%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("Melee"), 1), .939f, .002f));
	TestTrue(TEXT("Floor 1 MM share is approximately 4.7%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("MM"), 1), .047f, .002f));
	TestTrue(TEXT("Floor 1 Defender share is approximately 1.4%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("Defender"), 1), .014f, .002f));
	TestTrue(TEXT("Floor 2 Melee share is approximately 84.9%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("Melee"), 2), .849f, .004f));
	TestTrue(TEXT("Floor 2 Ranged share is approximately 5.1%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("Ranged"), 2), .051f, .003f));
	TestTrue(TEXT("Floor 3 Mage enters near 1.5%."), FMath::IsNearlyEqual(ArchetypeShare(TEXT("Mage"), 3), .015f, .004f));

	const TMap<FName, float> DeepShares = {
		{TEXT("Melee"), .50f},
		{TEXT("Ranged"), .20f},
		{TEXT("MM"), .10f},
		{TEXT("Mage"), .10f},
		{TEXT("Defender"), .05f},
		{TEXT("Gun"), .05f}};
	for (const TPair<FName, float>& Item : DeepShares)
	{
		TestTrue(*FString::Printf(TEXT("Deep %s share is exact."), *Item.Key.ToString()), FMath::IsNearlyEqual(ArchetypeWeight(Item.Key, 1000), Item.Value, .0002f));
	}

	auto GenderShare = [&](const FName Archetype, const EEFCalystoGenderV4 Gender)
	{
		float Matching = 0.0f;
		float Total = 0.0f;
		for (const FEFCalystoCatalogEntryV4& Entry : Enemy->Catalog)
		{
			if (Entry.Archetype == Archetype)
			{
				Total += Entry.DeepShare;
				Matching += Entry.Gender == Gender ? Entry.DeepShare : 0.0f;
			}
		}
		return Total > 0.0f ? Matching / Total : 0.0f;
	};
	TestTrue(TEXT("Melee gender split is 50/50."), FMath::IsNearlyEqual(GenderShare(TEXT("Melee"), EEFCalystoGenderV4::Female), .50f, FloatTolerance));
	TestTrue(TEXT("MM male share is 66.67%."), FMath::IsNearlyEqual(GenderShare(TEXT("MM"), EEFCalystoGenderV4::Male), 2.0f / 3.0f, .0002f));
	TestTrue(TEXT("Ranged female share is 60%."), FMath::IsNearlyEqual(GenderShare(TEXT("Ranged"), EEFCalystoGenderV4::Female), .60f, FloatTolerance));
	TestTrue(TEXT("Mage gender split is 50/50."), FMath::IsNearlyEqual(GenderShare(TEXT("Mage"), EEFCalystoGenderV4::Female), .50f, FloatTolerance));

	const FEFCalystoEnemyLevelBandV4 Common = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Common, 50, 0);
	const FEFCalystoEnemyLevelBandV4 Uncommon = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Uncommon, 50, 0);
	const FEFCalystoEnemyLevelBandV4 Rare = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Rare, 50, 0);
	const FEFCalystoEnemyLevelBandV4 Epic = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Epic, 50, 0);
	TestTrue(TEXT("Common band is A-3/A/A+2."), Common.Min == 47 && Common.Mode == 50 && Common.Max == 52);
	TestTrue(TEXT("Uncommon band is A-1/A+2/A+5."), Uncommon.Min == 49 && Uncommon.Mode == 52 && Uncommon.Max == 55);
	TestTrue(TEXT("Rare band is A+2/A+5/A+10."), Rare.Min == 52 && Rare.Mode == 55 && Rare.Max == 60);
	TestTrue(TEXT("Epic band is A+6/A+10/A+16."), Epic.Min == 56 && Epic.Mode == 60 && Epic.Max == 66);
	const FEFCalystoEnemyLevelBandV4 FloorOneCommon = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Common, 1, 0);
	TestTrue(TEXT("Normal bands clamp to 1..100."), FloorOneCommon.Min == 1 && FloorOneCommon.Mode == 1 && FloorOneCommon.Max == 3);
	const FEFCalystoEnemyLevelBandV4 Winter = FEFCalystoDungeonDirectorMathV4::ResolveEnemyLevelBand(EEFCalystoRarityTierV4::Winter, 100, 500);
	TestTrue(TEXT("Winter band is unbounded design level -2/+2/+6."), Winter.Min == 498 && Winter.Mode == 502 && Winter.Max == 506);

	const FString PolicyHash = Policy->GetPolicyHash();
	for (const FEFCalystoEnemyLevelBandV4 Band : {Common, Uncommon, Rare, Epic, Winter})
	{
		for (int32 Index = 0; Index < 256; ++Index)
		{
			FEFCalystoResolveContextV4 Context = MakeContext(400000 + Index, Band.Min > 100 ? 500 : 50, 1);
			const int32 Sample = FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(Band.Min, Band.Mode, Band.Max, 4.0f, .5f, Context, PolicyHash, EFCalystoDungeonDomainsV4::EnemyBundle, static_cast<uint64>(Band.Mode));
			if (Sample < Band.Min || Sample > Band.Max)
			{
				AddError(TEXT("Discrete level PERT escaped its tier band."));
				return false;
			}
			TestEqual(TEXT("Discrete level PERT is replay-exact."), FEFCalystoDungeonDirectorMathV4::SampleDiscretePERT(Band.Min, Band.Mode, Band.Max, 4.0f, .5f, Context, PolicyHash, EFCalystoDungeonDomainsV4::EnemyBundle, static_cast<uint64>(Band.Mode)), Sample);
		}
	}
	TestTrue(TEXT("Logical power scaling is monotonic beyond 100."), FEFCalystoDungeonDirectorMathV4::LevelCostMultiplier(500) > FEFCalystoDungeonDirectorMathV4::LevelCostMultiplier(100));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4EnemyBundleBacktrackingTest,
	"NoShellForWinter.CalystoDungeon.V4.Enemies.BundleDangerBacktrackingAndMinimumFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4EnemyBundleBacktrackingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	const double ExpectedWeight = .25 * FMath::Exp(
		(FMath::Loge(3.0) / 2.0) * .5 * (.60 * .75 + .40 * .80));
	const double ActualWeight =
		FEFCalystoDungeonDirectorMathV4::WeightEnemyBundleForDanger(
			.25, .5f, 3.0f, 4.0f, 80, 100);
	TestTrue(TEXT("Danger uses the exact single joint bundle formula."),
		FMath::IsNearlyEqual(ActualWeight, ExpectedWeight, 1.0e-12));
	TestTrue(TEXT("Invalid Danger bundle inputs fail closed."),
		FEFCalystoDungeonDirectorMathV4::WeightEnemyBundleForDanger(
			.25, 2.0f, 3.0f, 4.0f, 80, 100) == 0.0);

	UEFCalystoDungeonDirectorPolicyV4* BacktrackingPolicy = MakePolicy();
	ConfigureEnemyBundleFixture(
		BacktrackingPolicy, 2, 2, 2.4f, false,
		1.65f, .80f, 1.0f, .0001f);
	FString Error;
	TestTrue(*FString::Printf(TEXT("Backtracking fixture validates: %s"), *Error),
		BacktrackingPolicy && BacktrackingPolicy->Validate(Error));
	if (!BacktrackingPolicy || !Error.IsEmpty())
	{
		return false;
	}
	const FEFCalystoCategoryProfileV4* BacktrackingEnemy =
		UEFCalystoDungeonDirectorPolicyV4::FindCategory(
			BacktrackingPolicy->FindStyle(EEFCalystoStyleV4::Standard)->Categories,
			EEFCalystoContentCategoryV4::Enemy);
	const FName ExpensiveId = BacktrackingEnemy && !BacktrackingEnemy->Catalog.IsEmpty()
		? BacktrackingEnemy->Catalog[0].StableId
		: NAME_None;
	TestFalse(TEXT("Backtracking fixture exposes its expensive variant."),
		ExpensiveId.IsNone());
	const FString BacktrackingHash = BacktrackingPolicy->GetPolicyHash();
	int32 ActiveBacktrackingSamples = 0;
	for (int32 Sample = 0; Sample < 512; ++Sample)
	{
		FEFCalystoResolveContextV4 Context = MakeContext(6100000 + Sample, 1, 1);
		FEFCalystoResolvedFloorIntentV4 Intent;
		Error.Reset();
		if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
				BacktrackingPolicy, BacktrackingHash, Context, Intent, Error))
		{
			AddError(FString::Printf(
				TEXT("A feasible two-slot enemy bundle failed to backtrack: %s"),
				*Error));
			return false;
		}
		const FEFCalystoResolvedCategoryV4* Enemy = FindResolvedCategory(
			Intent, EEFCalystoContentCategoryV4::Enemy);
		if (!Enemy || !Enemy->bPresent)
		{
			continue;
		}
		++ActiveBacktrackingSamples;
		TestEqual(TEXT("A feasible active minimum resolves both enemy directives."),
			Enemy->DirectiveCount, 2);
		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive :
			Intent.SpawnDirectives)
		{
			if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
			{
				TestTrue(TEXT("The infeasible expensive first choice was backtracked."),
					Directive.CatalogId != ExpensiveId);
			}
		}
		TestTrue(TEXT("Backtracked composition remains inside threat budget."),
			Intent.PlannedThreatCost <= Intent.ThreatBudget + .0001f);
	}
	TestTrue(TEXT("Backtracking fixture exercised active enemy floors."),
		ActiveBacktrackingSamples > 300);

	UEFCalystoDungeonDirectorPolicyV4* DangerPolicy = MakePolicy();
	ConfigureEnemyBundleFixture(
		DangerPolicy, 1, 1, 10.0f, true,
		3.0f, 1.0f, 1.0f, 1.0f);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Joint Danger fixture validates: %s"), *Error),
		DangerPolicy && DangerPolicy->Validate(Error));
	if (!DangerPolicy || !Error.IsEmpty())
	{
		return false;
	}
	const FEFCalystoCategoryProfileV4* DangerEnemy =
		UEFCalystoDungeonDirectorPolicyV4::FindCategory(
			DangerPolicy->FindStyle(EEFCalystoStyleV4::Standard)->Categories,
			EEFCalystoContentCategoryV4::Enemy);
	TestNotNull(TEXT("Joint Danger fixture exposes its enemy profile."), DangerEnemy);
	if (!DangerEnemy || DangerEnemy->Catalog.Num() < 2)
	{
		return false;
	}
	const FName HighThreatId = DangerEnemy->Catalog[0].StableId;
	const FString DangerPolicyHash = DangerPolicy->GetPolicyHash();
	int32 DangerActiveSamples = 0;
	int32 HighThreatSelections = 0;
	for (int32 Sample = 0; Sample < 12000; ++Sample)
	{
		FEFCalystoResolveContextV4 Context = MakeContext(6200000 + Sample, 1, 1);
		Context.DirectorIntent.Danger = 1.0f;
		FEFCalystoResolvedFloorIntentV4 Intent;
		Error.Reset();
		if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
				DangerPolicy, DangerPolicyHash, Context, Intent, Error))
		{
			AddError(FString::Printf(TEXT("Joint Danger sample failed: %s"), *Error));
			return false;
		}
		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive :
			Intent.SpawnDirectives)
		{
			if (Directive.Category == EEFCalystoContentCategoryV4::Enemy)
			{
				++DangerActiveSamples;
				HighThreatSelections += Directive.CatalogId == HighThreatId ? 1 : 0;
			}
		}
	}
	const double DangerRatio = FMath::Exp((FMath::Loge(3.0) / 2.0) * .40);
	const double ExpectedHighThreatProbability = DangerRatio / (1.0 + DangerRatio);
	TestTrue(TEXT("Joint Danger statistical fixture exercised active floors."),
		DangerActiveSamples > 8000);
	TestTrue(TEXT("Observed Danger bundle selection matches one, not two, applications."),
		IsWithinSixSigma(HighThreatSelections, DangerActiveSamples,
			ExpectedHighThreatProbability));

	UEFCalystoDungeonDirectorPolicyV4* MinimumPolicy = MakePolicy();
	ConfigureEnemyBundleFixture(
		MinimumPolicy, 1, 1, 10.0f, true,
		1.0f, 1.0f, 1.0f, 1.0f);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Minimum fail-closed fixture validates: %s"), *Error),
		MinimumPolicy && MinimumPolicy->Validate(Error));
	if (!MinimumPolicy || !Error.IsEmpty())
	{
		return false;
	}
	FEFCalystoResolveContextV4 ExhaustedContext = MakeContext(6300000, 1, 1);
	const FEFCalystoCategoryProfileV4* MinimumEnemy =
		UEFCalystoDungeonDirectorPolicyV4::FindCategory(
			MinimumPolicy->FindStyle(EEFCalystoStyleV4::Standard)->Categories,
			EEFCalystoContentCategoryV4::Enemy);
	TestNotNull(TEXT("Minimum fixture exposes its enemy profile."), MinimumEnemy);
	if (!MinimumEnemy)
	{
		return false;
	}
	for (const FEFCalystoCatalogEntryV4& Entry : MinimumEnemy->Catalog)
	{
		if (Entry.Rule == EEFCalystoCatalogRuleV4::Allow)
		{
			ExhaustedContext.CooldownBlockedCatalogIds.AddUnique(Entry.StableId);
		}
	}
	bool bObservedMinimumFailure = false;
	for (int32 Attempt = 0; Attempt < 64 && !bObservedMinimumFailure; ++Attempt)
	{
		ExhaustedContext.RunSeed = 6300000 + Attempt;
		FEFCalystoResolvedFloorIntentV4 RejectedIntent;
		Error.Reset();
		if (!Resolve(MinimumPolicy, ExhaustedContext, RejectedIntent, &Error))
		{
			bObservedMinimumFailure = Error.Contains(TEXT("Minimum when Active"));
			TestFalse(TEXT("A minimum failure returns no valid partial intent."),
				RejectedIntent.bIsValid);
		}
	}
	TestTrue(TEXT("An active exhausted catalog fails its minimum closed."),
		bObservedMinimumFailure);

	UEFCalystoDungeonDirectorPolicyV4* RepeatedNPCPolicy = MakePolicy();
	ConfigureRepeatedNPCFixture(RepeatedNPCPolicy);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Repeated NPC-variant fixture validates: %s"), *Error),
		RepeatedNPCPolicy && RepeatedNPCPolicy->Validate(Error));
	if (!RepeatedNPCPolicy || !Error.IsEmpty())
	{
		return false;
	}
	const FEFCalystoCategoryProfileV4* NPCProfile =
		UEFCalystoDungeonDirectorPolicyV4::FindCategory(
			RepeatedNPCPolicy->FindStyle(EEFCalystoStyleV4::Standard)->Categories,
			EEFCalystoContentCategoryV4::NPC);
	const FEFCalystoCatalogEntryV4* RepeatedEntry = NPCProfile
		? NPCProfile->Catalog.FindByPredicate([](const FEFCalystoCatalogEntryV4& Entry)
			{
				return Entry.Rule == EEFCalystoCatalogRuleV4::Allow;
			})
		: nullptr;
	TestNotNull(TEXT("Repeated NPC fixture exposes one allowed variant."), RepeatedEntry);
	if (!RepeatedEntry)
	{
		return false;
	}
	FEFCalystoCompanionRecordV4 ExistingCompanion;
	ExistingCompanion.StableCompanionId = FGuid(0xA11CE001, 0xA11CE002, 0xA11CE003, 0xA11CE004);
	ExistingCompanion.SourceSpawnId = TEXT("V4.01.0000");
	ExistingCompanion.SourceCatalogId = RepeatedEntry->StableId;
	ExistingCompanion.SourceVariantId = RepeatedEntry->StableId;
	ExistingCompanion.ActorClass = RepeatedEntry->ActorClass;
	ExistingCompanion.Archetype = RepeatedEntry->Archetype;
	ExistingCompanion.Gender = RepeatedEntry->Gender;
	ExistingCompanion.Grade = EEFCalystoRarityTierV4::Common;
	ExistingCompanion.State = EEFCalystoCompanionRosterStateV4::RecruitedInactive;
	const FString RepeatedPolicyHash = RepeatedNPCPolicy->GetPolicyHash();
	int32 RepeatedVariantSpawns = 0;
	for (int32 Sample = 0; Sample < 256; ++Sample)
	{
		FEFCalystoResolveContextV4 Context = MakeContext(6400000 + Sample, 1, 1);
		Context.CompanionSnapshot.Records = {ExistingCompanion};
		Context.CompanionSnapshotHash =
			FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
				Context.CompanionSnapshot);
		FEFCalystoResolvedFloorIntentV4 Intent;
		Error.Reset();
		if (!FEFCalystoDungeonDirectorResolverV4::ResolvePrevalidatedForTesting(
				RepeatedNPCPolicy, RepeatedPolicyHash, Context, Intent, Error))
		{
			AddError(FString::Printf(
				TEXT("A catalog variant shared by a different StableCompanionId was incorrectly vetoed: %s"),
				*Error));
			return false;
		}
		for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
		{
			if (Directive.Category == EEFCalystoContentCategoryV4::NPC
				&& !Directive.StableCompanionId.IsValid()
				&& Directive.CatalogId == RepeatedEntry->StableId)
			{
				++RepeatedVariantSpawns;
			}
		}
	}
	TestTrue(TEXT("A recruited variant remains available to a new exact companion identity."),
		RepeatedVariantSpawns > 150);
	FEFCalystoResolveContextV4 MissingSpawnIdentity = MakeContext(6500000, 1, 1);
	FEFCalystoCompanionRecordV4 InvalidCompanion = ExistingCompanion;
	InvalidCompanion.SourceSpawnId = NAME_None;
	MissingSpawnIdentity.CompanionSnapshot.Records = {InvalidCompanion};
	MissingSpawnIdentity.CompanionSnapshotHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
			MissingSpawnIdentity.CompanionSnapshot);
	FEFCalystoResolvedFloorIntentV4 RejectedCompanionIntent;
	Error.Reset();
	TestFalse(TEXT("A companion snapshot without SourceSpawnId fails closed."),
		Resolve(RepeatedNPCPolicy, MissingSpawnIdentity,
			RejectedCompanionIntent, &Error));
	TestTrue(TEXT("Missing source identity reports an invalid snapshot record."),
		Error.Contains(TEXT("invalid or duplicated record")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4ResolverSelectionAndDeterminismTest,
	"NoShellForWinter.CalystoDungeon.V4.Director.SelectionBlendReplayRerollAndCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4ResolverSelectionAndDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	TestNotNull(TEXT("A V4 policy exists for resolver verification."), Policy);
	if (!Policy)
	{
		return false;
	}
	FString Error;
	if (!Policy->Validate(Error))
	{
		AddError(FString::Printf(TEXT("V4 default policy cannot resolve: %s"), *Error));
		return false;
	}

	static constexpr int32 SelectionSamples = 600;
	int32 StyleCounts[3] = {};
	int32 ThemeCounts[3] = {};
	for (int32 Index = 0; Index < SelectionSamples; ++Index)
	{
		const FEFCalystoResolveContextV4 Context = MakeContext(500000 + Index, 25, 1);
		FEFCalystoResolvedFloorIntentV4 Intent;
		if (!Resolve(Policy, Context, Intent, &Error))
		{
			AddError(FString::Printf(TEXT("V4 selection sample %d failed: %s"), Index, *Error));
			return false;
		}
		if (!Intent.bIsValid || Intent.GeneratorVersion != 4)
		{
			AddError(TEXT("V4 resolver returned an invalid or wrong-version intent."));
			return false;
		}
		++StyleCounts[static_cast<uint8>(Intent.Style)];
		++ThemeCounts[static_cast<uint8>(Intent.Theme)];
		const FEFCalystoStyleProfileV4* Style = Policy->FindStyle(Intent.Style);
		const FEFCalystoThemeProfileV4* Theme = Policy->FindTheme(Intent.Theme);
		if (!Style || !Theme)
		{
			AddError(TEXT("A resolved Style or Theme is absent from its policy."));
			return false;
		}
		const int32 MinEdge = FMath::Max(Style->Layout.MinimumDungeonEdge, Theme->Layout.MinimumDungeonEdge);
		const int32 MaxEdge = FMath::Min(Style->Layout.MaximumDungeonEdge, Theme->Layout.MaximumDungeonEdge);
		if (Intent.DungeonSize.X != Intent.DungeonSize.Y || Intent.DungeonSize.Z != 1
			|| Intent.DungeonSize.X < MinEdge || Intent.DungeonSize.X > MaxEdge
			|| !Policy->ValidatedDungeonSizes.Contains(Intent.DungeonSize.X)
			|| Intent.CandidateAnchorDensity < .20f || Intent.CandidateAnchorDensity > .50f
			|| Intent.SidePathChance < .30f || Intent.SidePathChance > .70f)
		{
			AddError(TEXT("A V4 resolved shape escaped its Style+Theme intersection or Calysto-safe range."));
			return false;
		}
		for (const FEFCalystoResolvedCategoryV4& Category : Intent.Categories)
		{
			const FEFCalystoCategoryProfileV4* StyleCategory = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Style->Categories, Category.Category);
			const FEFCalystoCategoryProfileV4* ThemeCategory = UEFCalystoDungeonDirectorPolicyV4::FindCategory(Theme->Categories, Category.Category);
			const FEFCalystoContextTraitsV4 StyleTraits = Style->GetAuthoredTraits();
			const FEFCalystoContextTraitsV4 ThemeTraits = Theme->GetAuthoredTraits();
			auto CategoryInfluence = [](const FEFCalystoContextTraitsV4& Traits, const EEFCalystoContentCategoryV4 Target)
			{
				switch (Target)
				{
				case EEFCalystoContentCategoryV4::Enemy: return Traits.Danger;
				case EEFCalystoContentCategoryV4::NPC: return Traits.Safe;
				case EEFCalystoContentCategoryV4::Food: return Traits.Abundance;
				case EEFCalystoContentCategoryV4::Chest: return Traits.Mystery;
				case EEFCalystoContentCategoryV4::Clothing: return Traits.ClothingInfluence;
				default: return 0.0f;
				}
			};
			const float ExpectedInfluence = FMath::Lerp(
				CategoryInfluence(StyleTraits, Category.Category),
				CategoryInfluence(ThemeTraits, Category.Category),
				Category.StyleThemeBlend);
			if (!StyleCategory || !ThemeCategory
				|| Category.MinimumWhenPresent != FMath::Max(StyleCategory->Limits.MinimumWhenPresent, ThemeCategory->Limits.MinimumWhenPresent)
				|| Category.MaximumPerFloor != FMath::Min(StyleCategory->Limits.MaximumPerFloor, ThemeCategory->Limits.MaximumPerFloor)
				|| !FMath::IsNearlyEqual(Category.ResolvedInfluence, ExpectedInfluence, FloatTolerance)
				|| Category.TargetCount < 0 || Category.TargetCount > Category.MaximumPerFloor
				|| Category.StyleThemeBlend < 0.0f || Category.StyleThemeBlend > 1.0f
				|| Category.OpportunityChance < 0.0f || Category.OpportunityChance > .9001f
				|| Category.ResolvedTiers.GetSelectableMass() > .9001f
				|| Category.ResolvedTiers.GetCalculatedNothing() < .0999f)
			{
				AddError(TEXT("A V4 category escaped its probabilistic or intersected limit contract."));
				return false;
			}
		}
		if (Intent.SpawnDirectives.Num() > Policy->SafetyCeilings.MaximumDirectorActors
			|| Intent.PlannedThreatCost > Intent.ThreatBudget + .001f)
		{
			AddError(TEXT("A V4 intent exceeded its actor or threat budget."));
			return false;
		}
	}
	TestTrue(TEXT("Standard selection follows .50 within six sigma."), IsWithinSixSigma(StyleCounts[static_cast<uint8>(EEFCalystoStyleV4::Standard)], SelectionSamples, .50));
	TestTrue(TEXT("Compact selection follows .25 within six sigma."), IsWithinSixSigma(StyleCounts[static_cast<uint8>(EEFCalystoStyleV4::Compact)], SelectionSamples, .25));
	TestTrue(TEXT("Branching selection follows .25 within six sigma."), IsWithinSixSigma(StyleCounts[static_cast<uint8>(EEFCalystoStyleV4::Branching)], SelectionSamples, .25));
	TestTrue(TEXT("Default selection follows .60 within six sigma."), IsWithinSixSigma(ThemeCounts[static_cast<uint8>(EEFCalystoThemeV4::Default)], SelectionSamples, .60));
	TestTrue(TEXT("Forge selection follows .25 within six sigma."), IsWithinSixSigma(ThemeCounts[static_cast<uint8>(EEFCalystoThemeV4::Forge)], SelectionSamples, .25));
	TestTrue(TEXT("Shrine selection follows .15 within six sigma."), IsWithinSixSigma(ThemeCounts[static_cast<uint8>(EEFCalystoThemeV4::Shrine)], SelectionSamples, .15));

	// Exact-edge certification must still use a weighted Style/Theme draw, while
	// excluding only profiles whose authored bounds cannot realize the edge.
	// This prevents a valid 30x30 PCG case from failing merely because Compact
	// (max 26) or Shrine (max 28) happened to win an unrelated random draw.
	for (int32 Index = 0; Index < 32; ++Index)
	{
		FEFCalystoResolveContextV4 ForcedEdgeContext =
			MakeContext(650000 + Index, 25, 1);
		ForcedEdgeContext.DevelopmentForcedDungeonEdge = 30;
		FEFCalystoResolvedFloorIntentV4 ForcedEdgeIntent;
		Error.Reset();
		if (!Resolve(Policy, ForcedEdgeContext, ForcedEdgeIntent, &Error))
		{
			AddError(FString::Printf(
				TEXT("V4 forced-edge sample %d failed: %s"), Index, *Error));
			return false;
		}
		if (ForcedEdgeIntent.DungeonSize != FIntVector(30, 30, 1)
			|| ForcedEdgeIntent.DevelopmentForcedDungeonEdge != 30
			|| ForcedEdgeIntent.Style == EEFCalystoStyleV4::Compact
			|| ForcedEdgeIntent.Theme == EEFCalystoThemeV4::Shrine)
		{
			AddError(TEXT("V4 exact-edge automation selected a profile that cannot realize 30x30."));
			return false;
		}
	}

	const FEFCalystoResolveContextV4 ReplayContext = MakeContext(777777, 50, 7);
	FEFCalystoResolvedFloorIntentV4 Original;
	FEFCalystoResolvedFloorIntentV4 Replay;
	TestTrue(*FString::Printf(TEXT("Original V4 intent resolves: %s"), *Error), Resolve(Policy, ReplayContext, Original, &Error));
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Replay V4 intent resolves: %s"), *Error), Resolve(Policy, ReplayContext, Replay, &Error));
	TestEqual(TEXT("Replay preserves exact IntentHash."), Replay.IntentHash, Original.IntentHash);
	TestEqual(TEXT("Replay preserves exact PCG seed."), Replay.PCGSeed, Original.PCGSeed);
	TestEqual(TEXT("Replay preserves exact directive count."), Replay.SpawnDirectives.Num(), Original.SpawnDirectives.Num());

	FEFCalystoResolveContextV4 RerollContext = ReplayContext;
	++RerollContext.GenerationSerial;
	FEFCalystoResolvedFloorIntentV4 Reroll;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Reroll V4 intent resolves: %s"), *Error), Resolve(Policy, RerollContext, Reroll, &Error));
	TestTrue(TEXT("Reroll changes IntentHash."), Reroll.IntentHash != Original.IntentHash);
	TestTrue(TEXT("Reroll changes the counter-derived PCG seed."), Reroll.PCGSeed != Original.PCGSeed);

	FEFCalystoResolveContextV4 AntiStreak = MakeContext(888888, 20, 1);
	AntiStreak.PreviousStyle = EEFCalystoStyleV4::Standard;
	AntiStreak.ConsecutiveStyleCount = AntiStreak.MaximumConsecutiveStyle;
	AntiStreak.PreviousTheme = EEFCalystoThemeV4::Default;
	AntiStreak.ConsecutiveThemeCount = AntiStreak.MaximumConsecutiveTheme;
	FEFCalystoResolvedFloorIntentV4 AntiStreakIntent;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Anti-streak context resolves: %s"), *Error), Resolve(Policy, AntiStreak, AntiStreakIntent, &Error));
	TestTrue(TEXT("A third Standard in sequence is excluded."), AntiStreakIntent.Style != EEFCalystoStyleV4::Standard);
	TestTrue(TEXT("A fourth Default Theme in sequence is excluded."), AntiStreakIntent.Theme != EEFCalystoThemeV4::Default);

	UEFCalystoDungeonDirectorPolicyV4* RecallPolicy = MakePolicy();
	ConfigureCompanionRecallLifecyclePolicy(RecallPolicy);
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Companion Recall transient policy validates: %s"), *Error), RecallPolicy->Validate(Error));
	FEFCalystoResolveContextV4 RecallFloorOne = MakeContext(202608210404, 1, 1);
	RecallFloorOne.DevelopmentPopulationScenario = TEXT("CompanionRecallLifecycle");
	FEFCalystoResolvedFloorIntentV4 RecallFloorOneIntent;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Companion Recall Floor 1 resolves: %s"), *Error), Resolve(RecallPolicy, RecallFloorOne, RecallFloorOneIntent, &Error));
	TestEqual(TEXT("Companion Recall Floor 1 has one actor."), RecallFloorOneIntent.SpawnDirectives.Num(), 1);
	TestTrue(TEXT("Companion Recall Floor 1 has no chest content."), RecallFloorOneIntent.ChestContentDirectives.IsEmpty());
	if (RecallFloorOneIntent.SpawnDirectives.Num() == 1)
	{
		const FEFCalystoSpawnInstanceDirectiveV4& NPC = RecallFloorOneIntent.SpawnDirectives[0];
		TestEqual(TEXT("Companion Recall Floor 1 selects Generalist Female."), NPC.CatalogId, FName(TEXT("NPC.Companion.Generalist.Female")));
		TestEqual(TEXT("Companion Recall Floor 1 actor is an NPC."), NPC.Category, EEFCalystoContentCategoryV4::NPC);
		TestFalse(TEXT("Companion Recall Floor 1 NPC is local, not a roster projection."), NPC.StableCompanionId.IsValid());
	}

	const FEFCalystoCategoryProfileV4* RecallNPCProfile =
		UEFCalystoDungeonDirectorPolicyV4::FindCategory(
			RecallPolicy->Styles[0].Categories,
			EEFCalystoContentCategoryV4::NPC);
	const FEFCalystoCatalogEntryV4* FemaleEntry = RecallNPCProfile
		? RecallNPCProfile->Catalog.FindByPredicate([](const FEFCalystoCatalogEntryV4& Entry)
			{
				return Entry.StableId == FName(TEXT("NPC.Companion.Generalist.Female"));
			})
		: nullptr;
	TestNotNull(TEXT("Companion Recall test policy contains Generalist Female."), FemaleEntry);
	if (!FemaleEntry)
	{
		return false;
	}
	FEFCalystoResolveContextV4 RecallFloorTwo = MakeContext(202608210404, 2, 2);
	RecallFloorTwo.DevelopmentPopulationScenario = TEXT("CompanionRecallLifecycle");
	FEFCalystoCompanionRecordV4 DeadFemale;
	DeadFemale.StableCompanionId = FGuid(0xC011AB04, 0xD1EEC704, 0xA11CE004, 0x00000001);
	DeadFemale.SourceSpawnId = TEXT("V4.01.0000");
	DeadFemale.SourceCatalogId = FemaleEntry->StableId;
	DeadFemale.SourceVariantId = FemaleEntry->StableId;
	DeadFemale.ActorClass = FemaleEntry->ActorClass;
	DeadFemale.Archetype = FemaleEntry->Archetype;
	DeadFemale.Gender = FemaleEntry->Gender;
	DeadFemale.Grade = EEFCalystoRarityTierV4::Common;
	DeadFemale.State = EEFCalystoCompanionRosterStateV4::Dead;
	RecallFloorTwo.CompanionSnapshot.Records.Add(DeadFemale);
	RecallFloorTwo.CompanionSnapshotHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
			RecallFloorTwo.CompanionSnapshot);
	FEFCalystoResolvedFloorIntentV4 RecallFloorTwoIntent;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Companion Recall Floor 2 resolves: %s"), *Error), Resolve(RecallPolicy, RecallFloorTwo, RecallFloorTwoIntent, &Error));
	TestEqual(TEXT("Companion Recall Floor 2 has local NPC plus chest."), RecallFloorTwoIntent.SpawnDirectives.Num(), 2);
	TestEqual(TEXT("Companion Recall Floor 2 has exactly one chest content."), RecallFloorTwoIntent.ChestContentDirectives.Num(), 1);
	const FEFCalystoSpawnInstanceDirectiveV4* MaleDirective =
		RecallFloorTwoIntent.SpawnDirectives.FindByPredicate([](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
			{
				return Directive.CatalogId == FName(TEXT("NPC.Companion.Generalist.Male"));
			});
	const FEFCalystoSpawnInstanceDirectiveV4* ChestDirective =
		RecallFloorTwoIntent.SpawnDirectives.FindByPredicate([](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
			{
				return Directive.Category == EEFCalystoContentCategoryV4::Chest;
			});
	TestNotNull(TEXT("Companion Recall Floor 2 selects Generalist Male."), MaleDirective);
	TestNotNull(TEXT("Companion Recall Floor 2 selects one chest."), ChestDirective);
	if (RecallFloorTwoIntent.ChestContentDirectives.Num() == 1 && ChestDirective)
	{
		const FEFCalystoChestContentDirectiveV4& Recall = RecallFloorTwoIntent.ChestContentDirectives[0];
		TestEqual(TEXT("Winter's Recall belongs to the resolved chest."), Recall.ContainerInstanceId, ChestDirective->StableInstanceId);
		TestEqual(TEXT("The exact Winter's Recall ID is frozen."), Recall.ContentCatalogId, FName(TEXT("Item.CompanionRevival.WintersRecall")));
		TestEqual(TEXT("Winter's Recall remains Epic."), Recall.Tier, EEFCalystoRarityTierV4::Epic);
		TestEqual(TEXT("Winter's Recall preserves its eight-floor cooldown."), Recall.CooldownFloors, 8);
	}
	FEFCalystoResolvedFloorIntentV4 RecallReplay;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Companion Recall Floor 2 replay resolves: %s"), *Error), Resolve(RecallPolicy, RecallFloorTwo, RecallReplay, &Error));
	TestEqual(TEXT("Companion Recall replay preserves the frozen intent hash."), RecallReplay.IntentHash, RecallFloorTwoIntent.IntentHash);
	FEFCalystoResolveContextV4 RecallAlreadyOwned = RecallFloorTwo;
	RecallAlreadyOwned.CompanionSnapshot.bPlayerOwnsWintersRecall = true;
	RecallAlreadyOwned.CompanionSnapshotHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
			RecallAlreadyOwned.CompanionSnapshot);
	FEFCalystoResolvedFloorIntentV4 RejectedRecallIntent;
	Error.Reset();
	TestFalse(TEXT("Companion Recall fixture rejects an already-owned Recall."), Resolve(RecallPolicy, RecallAlreadyOwned, RejectedRecallIntent, &Error));
	FEFCalystoResolveContextV4 RecallPendingDead = RecallFloorTwo;
	RecallPendingDead.CompanionSnapshot.Records[0].State =
		EEFCalystoCompanionRosterStateV4::PendingDead;
	RecallPendingDead.CompanionSnapshotHash =
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
			RecallPendingDead.CompanionSnapshot);
	Error.Reset();
	TestFalse(TEXT("Companion Recall fixture rejects PendingDead before Advance."), Resolve(RecallPolicy, RecallPendingDead, RejectedRecallIntent, &Error));
	FEFCalystoResolveContextV4 RecallOnCooldown = RecallFloorTwo;
	RecallOnCooldown.CooldownBlockedCatalogIds.Add(
		FName(TEXT("Item.CompanionRevival.WintersRecall")));
	Error.Reset();
	TestFalse(TEXT("Companion Recall fixture never bypasses its cooldown."), Resolve(RecallPolicy, RecallOnCooldown, RejectedRecallIntent, &Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4TierUnlockAndWinterTest,
	"NoShellForWinter.CalystoDungeon.V4.Enemies.InclusiveTierUnlocksAndWinterMass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4TierUnlockAndWinterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	if (!Policy)
	{
		AddError(TEXT("Could not construct V4 policy for tier gates."));
		return false;
	}
	FString Error;
	for (const int64 Floor : {static_cast<int64>(1), 2LL, 4LL, 5LL, 9LL, 10LL, 100LL, 101LL})
	{
		int32 ObservedEnemyDirectives = 0;
		for (int32 Sample = 0; Sample < 24; ++Sample)
		{
			FEFCalystoResolvedFloorIntentV4 Intent;
			const FEFCalystoResolveContextV4 Context = MakeContext(900000 + Floor * 100 + Sample, Floor, 1);
			if (!Resolve(Policy, Context, Intent, &Error))
			{
				AddError(FString::Printf(TEXT("Tier-gate resolve failed at Floor %lld: %s"), Floor, *Error));
				return false;
			}
			const FEFCalystoResolvedCategoryV4* EnemyCategory = FindResolvedCategory(Intent, EEFCalystoContentCategoryV4::Enemy);
			if (!EnemyCategory)
			{
				AddError(TEXT("Resolved intent omitted the Enemy category."));
				return false;
			}
			for (const EEFCalystoRarityTierV4 Tier : {
				EEFCalystoRarityTierV4::Common,
				EEFCalystoRarityTierV4::Uncommon,
				EEFCalystoRarityTierV4::Rare,
				EEFCalystoRarityTierV4::Epic})
			{
				if (Floor < TierUnlockFloor(Tier)
					&& EnemyCategory->ResolvedTiers.Get(Tier) > FloatTolerance)
				{
					AddError(FString::Printf(TEXT("Locked tier %d retained selectable mass on Floor %lld."), static_cast<int32>(Tier), Floor));
					return false;
				}
			}
			for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
			{
				if ((Directive.Category == EEFCalystoContentCategoryV4::Enemy
						|| Directive.Category == EEFCalystoContentCategoryV4::NPC)
					&& Floor < TierUnlockFloor(Directive.Tier))
				{
					AddError(FString::Printf(TEXT("Tier %d materialized before its inclusive Floor %lld unlock."), static_cast<int32>(Directive.Tier), TierUnlockFloor(Directive.Tier)));
					return false;
				}
				ObservedEnemyDirectives += Directive.Category == EEFCalystoContentCategoryV4::Enemy ? 1 : 0;
			}
		}
		TestTrue(*FString::Printf(TEXT("Tier gate at Floor %lld was exercised by real enemy directives."), Floor), ObservedEnemyDirectives > 0);
	}

	auto ResolveStandardDefault = [&](const int64 Floor, FEFCalystoResolvedFloorIntentV4& OutIntent)
	{
		for (int32 Attempt = 0; Attempt < 96; ++Attempt)
		{
			const FEFCalystoResolveContextV4 Context = MakeContext(1200000 + Floor * 100 + Attempt, Floor, 1);
			if (Resolve(Policy, Context, OutIntent, &Error)
				&& OutIntent.Style == EEFCalystoStyleV4::Standard
				&& OutIntent.Theme == EEFCalystoThemeV4::Default)
			{
				return true;
			}
		}
		return false;
	};
	for (const int64 Floor : {100LL, 101LL, 125LL, 200LL, 500LL, 1000LL})
	{
		FEFCalystoResolvedFloorIntentV4 Intent;
		TestTrue(*FString::Printf(TEXT("Found Standard+Default sample at Floor %lld."), Floor), ResolveStandardDefault(Floor, Intent));
		const FEFCalystoResolvedCategoryV4* EnemyCategory = FindResolvedCategory(Intent, EEFCalystoContentCategoryV4::Enemy);
		TestNotNull(*FString::Printf(TEXT("Floor %lld has an Enemy result."), Floor), EnemyCategory);
		if (!EnemyCategory)
		{
			continue;
		}
		const float ExpectedWinter = Floor <= 100
			? 0.0f
			: .90f * (1.0f - FMath::Exp(-static_cast<float>(Floor - 100) / 100.0f));
		TestTrue(*FString::Printf(TEXT("Floor %lld Standard+Default Winter mass is exact."), Floor), FMath::IsNearlyEqual(EnemyCategory->WinterChance, ExpectedWinter, .0002f));
		TestEqual(*FString::Printf(TEXT("Floor %lld freezes its logical Winter level."), Floor), Intent.LogicalWinterLevel, Floor <= 100 ? 0 : static_cast<int32>(Floor));
	}

	FEFCalystoResolveContextV4 OverflowContext = MakeContext(1300000, static_cast<int64>(MAX_int32) - 5, 1);
	FEFCalystoResolvedFloorIntentV4 OverflowIntent;
	Error.Reset();
	TestFalse(TEXT("A logical level that cannot fit int32 plus six fails closed."), Resolve(Policy, OverflowContext, OverflowIntent, &Error));
	TestFalse(TEXT("Overflow rejection includes a diagnostic."), Error.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4DisabledCategoryFailClosedTest,
	"NoShellForWinter.CalystoDungeon.V4.Policy.DisabledCategoryFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4DisabledCategoryFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Policy = MakePolicy();
	TestNotNull(TEXT("A V4 policy exists for disabled-category verification."), Policy);
	if (!Policy)
	{
		return false;
	}

	FEFCalystoThemeProfileV4* DefaultTheme = Policy->Themes.FindByPredicate(
		[](const FEFCalystoThemeProfileV4& Candidate)
		{
			return Candidate.Theme == EEFCalystoThemeV4::Default;
		});
	TestNotNull(TEXT("The Default Theme exists for the disabled-category fixture."), DefaultTheme);
	if (!DefaultTheme)
	{
		return false;
	}

	const TSet<EEFCalystoContentCategoryV4> DisabledCategories = {
		EEFCalystoContentCategoryV4::Enemy,
		EEFCalystoContentCategoryV4::Food,
		EEFCalystoContentCategoryV4::Chest};
	for (FEFCalystoCategoryProfileV4& Category : DefaultTheme->Categories)
	{
		if (DisabledCategories.Contains(Category.Category))
		{
			Category.bEnabled = false;
			Category.bBlocked = false;
			Category.Chance.ChanceAtFloor1 = .90f;
			Category.Chance.ChanceAtFloor100 = .90f;
			Category.PityAfterEmptyFloors =
				Category.Category == EEFCalystoContentCategoryV4::Food
					|| Category.Category == EEFCalystoContentCategoryV4::Chest
				? 1
				: 0;
		}
	}

	FString Error;
	TestTrue(
		*FString::Printf(TEXT("The mixed enabled/disabled profile remains valid: %s"), *Error),
		Policy->Validate(Error));
	if (!Error.IsEmpty())
	{
		return false;
	}
	const FString ThemeOwnedHash = Policy->GetPolicyHash();
	for (FEFCalystoStyleProfileV4& Style : Policy->Styles)
	{
		for (FEFCalystoCategoryProfileV4& Category : Style.Categories)
		{
			Category.bEnabled = false;
		}
	}
	TestEqual(
		TEXT("Legacy Style Enabled values do not participate in the V4 policy hash."),
		Policy->GetPolicyHash(),
		ThemeOwnedHash);

	int32 DefaultThemeSamples = 0;
	int32 EnabledThemeEnemySamples = 0;
	for (int32 Sample = 0; Sample < 512; ++Sample)
	{
		FEFCalystoResolveContextV4 Context = MakeContext(1400000 + Sample, 150, 1);
		Context.ConsecutiveFloorsWithoutFood = 100;
		Context.ConsecutiveFloorsWithoutChest = 100;
		FEFCalystoResolvedFloorIntentV4 Intent;
		Error.Reset();
		if (!Resolve(Policy, Context, Intent, &Error))
		{
			AddError(FString::Printf(
				TEXT("Disabled-category fixture failed to resolve sample %d: %s"),
				Sample,
				*Error));
			return false;
		}

		if (Intent.Theme != EEFCalystoThemeV4::Default)
		{
			EnabledThemeEnemySamples += Intent.SpawnDirectives.ContainsByPredicate(
				[](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
				{
					return Directive.Category == EEFCalystoContentCategoryV4::Enemy;
				}) ? 1 : 0;
			continue;
		}

		++DefaultThemeSamples;
		for (const EEFCalystoContentCategoryV4 Category : DisabledCategories)
		{
			const FEFCalystoResolvedCategoryV4* Resolved = FindResolvedCategory(Intent, Category);
			if (!Resolved)
			{
				AddError(TEXT("A disabled category was omitted from the frozen V4 intent."));
				return false;
			}
			const bool bZeroContract =
				FMath::IsNearlyZero(Resolved->OpportunityChance)
				&& FMath::IsNearlyZero(Resolved->WinterChance)
				&& FMath::IsNearlyZero(Resolved->EffectiveChance)
				&& !Resolved->bPresent
				&& Resolved->AttemptCount == 0
				&& Resolved->TargetCount == 0
				&& Resolved->DirectiveCount == 0;
			if (!bZeroContract)
			{
				AddError(FString::Printf(
					TEXT("Disabled V4 category %d escaped its exact zero contract."),
					static_cast<int32>(Category)));
				return false;
			}
			if (Intent.SpawnDirectives.ContainsByPredicate(
					[Category](const FEFCalystoSpawnInstanceDirectiveV4& Directive)
					{
						return Directive.Category == Category;
					}))
			{
				AddError(FString::Printf(
					TEXT("Disabled V4 category %d produced an actor directive."),
					static_cast<int32>(Category)));
				return false;
			}
		}
	}

	TestTrue(TEXT("The fixture sampled the disabled Default Theme repeatedly."), DefaultThemeSamples >= 192);
	TestTrue(TEXT("Other enabled Themes may still resolve enemies, proving Enabled is Theme-specific."), EnabledThemeEnemySamples > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV4CanonicalHashesAndFailClosedValidationTest,
	"NoShellForWinter.CalystoDungeon.V4.Policy.CanonicalHashesAndFailClosedValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV4CanonicalHashesAndFailClosedValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCalystoV4Tests;

	UEFCalystoDungeonDirectorPolicyV4* Baseline = MakePolicy();
	TestNotNull(TEXT("A baseline V4 policy exists for hash validation."), Baseline);
	if (!Baseline)
	{
		return false;
	}
	FString Error;
	if (!Baseline->Validate(Error))
	{
		AddError(FString::Printf(TEXT("Baseline V4 policy is invalid: %s"), *Error));
		return false;
	}
	const FString BaselineHash = Baseline->GetPolicyHash();
	TestEqual(TEXT("Canonical V4 policy hash is 64 hexadecimal characters."), BaselineHash.Len(), 64);

	UEFCalystoDungeonDirectorPolicyV4* Reordered = MakePolicy();
	Algo::Reverse(Reordered->Styles);
	Algo::Reverse(Reordered->Themes);
	for (FEFCalystoStyleProfileV4& Style : Reordered->Styles)
	{
		Algo::Reverse(Style.Categories);
		for (FEFCalystoCategoryProfileV4& Category : Style.Categories)
		{
			Algo::Reverse(Category.Catalog);
			Algo::Reverse(Category.ChestContentsCatalog);
		}
	}
	for (FEFCalystoThemeProfileV4& Theme : Reordered->Themes)
	{
		Algo::Reverse(Theme.Categories);
		for (FEFCalystoCategoryProfileV4& Category : Theme.Categories)
		{
			Algo::Reverse(Category.Catalog);
			Algo::Reverse(Category.ChestContentsCatalog);
		}
	}
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Reordered V4 policy remains valid: %s"), *Error), Reordered->Validate(Error));
	TestEqual(TEXT("Canonical policy hash ignores authored array order."), Reordered->GetPolicyHash(), BaselineHash);

	UEFCalystoDungeonDirectorPolicyV4* Edited = MakePolicy();
	FEFCalystoCategoryProfileV4* EditedEnemy = FindMutableCategory(Edited->Styles[0].Categories, EEFCalystoContentCategoryV4::Enemy);
	if (EditedEnemy && !EditedEnemy->Catalog.IsEmpty())
	{
		EditedEnemy->Catalog[0].Name += TEXT(" Edited");
	}
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("A valid readable-name edit remains valid: %s"), *Error), Edited->Validate(Error));
	TestTrue(TEXT("Any catalog authoring edit changes PolicyHash."), Edited->GetPolicyHash() != BaselineHash);

	FEFCalystoCompanionSnapshotV4 Snapshot;
	FEFCalystoCompanionRecordV4 First;
	First.StableCompanionId = FGuid(1, 2, 3, 4);
	First.SourceSpawnId = TEXT("V4.01.0000");
	First.SourceCatalogId = TEXT("NPC.Companion.Generalist.Female");
	First.SourceVariantId = TEXT("NPC.Companion.Generalist.Female");
	First.ActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Script/Engine.Actor")));
	First.Archetype = TEXT("Generalist");
	First.Gender = EEFCalystoGenderV4::Female;
	First.Grade = EEFCalystoRarityTierV4::Common;
	First.State = EEFCalystoCompanionRosterStateV4::ActiveParty;
	FEFCalystoCompanionRecordV4 Second = First;
	Second.StableCompanionId = FGuid(5, 6, 7, 8);
	Second.SourceSpawnId = TEXT("V4.01.0001");
	Second.SourceCatalogId = TEXT("NPC.Companion.Melee.Male");
	Second.SourceVariantId = TEXT("NPC.Companion.Melee.Male");
	Second.Archetype = TEXT("Melee");
	Second.Gender = EEFCalystoGenderV4::Male;
	Second.Grade = EEFCalystoRarityTierV4::Rare;
	Second.State = EEFCalystoCompanionRosterStateV4::Dead;
	Snapshot.Records = {First, Second};
	const FString SnapshotHash = FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Snapshot);
	Algo::Reverse(Snapshot.Records);
	TestEqual(TEXT("Companion snapshot hash is order-independent."), FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Snapshot), SnapshotHash);
	FEFCalystoCompanionSnapshotV4 ChangedSpawnIdentity = Snapshot;
	ChangedSpawnIdentity.Records[0].SourceSpawnId = TEXT("V4.01.9999");
	TestTrue(TEXT("SourceSpawnId changes the canonical companion snapshot hash."),
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(ChangedSpawnIdentity) != SnapshotHash);
	FEFCalystoCompanionSnapshotV4 ChangedVariantIdentity = Snapshot;
	ChangedVariantIdentity.Records[0].SourceVariantId = TEXT("NPC.Companion.Ranged.Male");
	TestTrue(TEXT("SourceVariantId changes the canonical companion snapshot hash."),
		FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(ChangedVariantIdentity) != SnapshotHash);
	Snapshot.Records[0].State = EEFCalystoCompanionRosterStateV4::PendingDead;
	TestTrue(TEXT("Companion state changes its canonical snapshot hash."), FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Snapshot) != SnapshotHash);

	auto ExpectInvalid = [this](UEFCalystoDungeonDirectorPolicyV4* Policy, const TCHAR* Label)
	{
		FString ValidationError;
		const bool bValid = Policy && Policy->Validate(ValidationError);
		TestFalse(Label, bValid);
		TestFalse(*FString::Printf(TEXT("%s returns a diagnostic."), Label), ValidationError.IsEmpty());
		if (Policy)
		{
			TestTrue(*FString::Printf(TEXT("%s cannot produce a policy hash."), Label), Policy->GetPolicyHash().IsEmpty());
		}
	};

	UEFCalystoDungeonDirectorPolicyV4* BadIdentity = MakePolicy();
	BadIdentity->SchemaVersion = 3;
	ExpectInvalid(BadIdentity, TEXT("Wrong schema fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadSelection = MakePolicy();
	BadSelection->Styles[0].SelectionProbability += .01f;
	ExpectInvalid(BadSelection, TEXT("Style selection mass other than 1.0 fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadTierMass = MakePolicy();
	FEFCalystoCategoryProfileV4* BadTierCategory = FindMutableCategory(BadTierMass->Styles[0].Categories, EEFCalystoContentCategoryV4::Chest);
	BadTierCategory->Tiers.AtFloor1.Common = .90f;
	BadTierCategory->Tiers.AtFloor1.Uncommon = .01f;
	ExpectInvalid(BadTierMass, TEXT("Tier mass above .90 fails closed at policy validation."));

	UEFCalystoDungeonDirectorPolicyV4* BadNaN = MakePolicy();
	FindMutableCategory(BadNaN->Styles[0].Categories, EEFCalystoContentCategoryV4::Food)->Chance.ChanceAtFloor1 = std::numeric_limits<float>::quiet_NaN();
	ExpectInvalid(BadNaN, TEXT("NaN gameplay chance fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadInfinity = MakePolicy();
	BadInfinity->Themes[0].Volatility = std::numeric_limits<float>::infinity();
	ExpectInvalid(BadInfinity, TEXT("Infinite profile volatility fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadLayout = MakePolicy();
	BadLayout->Styles[0].Layout.MinimumDungeonEdge = 30;
	BadLayout->Styles[0].Layout.MaximumDungeonEdge = 26;
	ExpectInvalid(BadLayout, TEXT("Inverted dungeon range fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadCategoryRange = MakePolicy();
	FEFCalystoCategoryProfileV4* BadRangeCategory = FindMutableCategory(BadCategoryRange->Styles[0].Categories, EEFCalystoContentCategoryV4::Food);
	BadRangeCategory->Limits.MinimumWhenPresent = 20;
	BadRangeCategory->Limits.MaximumPerFloor = 10;
	ExpectInvalid(BadCategoryRange, TEXT("Inverted category limits fail closed."));

	UEFCalystoDungeonDirectorPolicyV4* EmptyIntersection = MakePolicy();
	FEFCalystoCategoryProfileV4* StyleEnemy = FindMutableCategory(EmptyIntersection->Styles[0].Categories, EEFCalystoContentCategoryV4::Enemy);
	FEFCalystoCategoryProfileV4* ThemeEnemy = FindMutableCategory(EmptyIntersection->Themes[0].Categories, EEFCalystoContentCategoryV4::Enemy);
	StyleEnemy->Limits.MinimumWhenPresent = 25;
	ThemeEnemy->Limits.MaximumPerFloor = 20;
	ExpectInvalid(EmptyIntersection, TEXT("Empty Style+Theme limit intersection fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* DuplicateId = MakePolicy();
	FEFCalystoCategoryProfileV4* DuplicateEnemy = FindMutableCategory(DuplicateId->Styles[0].Categories, EEFCalystoContentCategoryV4::Enemy);
	DuplicateEnemy->Catalog[1].StableId = DuplicateEnemy->Catalog[0].StableId;
	ExpectInvalid(DuplicateId, TEXT("Duplicate stable catalog ID fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* EmptyName = MakePolicy();
	FindMutableCategory(EmptyName->Styles[0].Categories, EEFCalystoContentCategoryV4::Enemy)->Catalog[0].Name.Reset();
	ExpectInvalid(EmptyName, TEXT("Empty readable catalog name fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* MissingClass = MakePolicy();
	FindMutableCategory(MissingClass->Styles[0].Categories, EEFCalystoContentCategoryV4::Enemy)->Catalog[0].ActorClass =
		TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/_Missing/V4/Enemy.DoesNotExist_C")));
	ExpectInvalid(MissingClass, TEXT("Missing soft actor class fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* MissingClothingCatalog = MakePolicy();
	FindMutableCategory(
		MissingClothingCatalog->Styles[0].Categories,
		EEFCalystoContentCategoryV4::Clothing)->Catalog.Reset();
	ExpectInvalid(
		MissingClothingCatalog,
		TEXT("Enabled Clothing probability without an allowed pickup fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* BadFloorGate = MakePolicy();
	FindMutableCategory(BadFloorGate->Themes[0].Categories, EEFCalystoContentCategoryV4::NPC)->Catalog[0].FirstEligibleFloor = 0;
	ExpectInvalid(BadFloorGate, TEXT("First Eligible Floor below one fails closed."));

	const FSoftObjectPath RecallClassPath(
		TEXT("/Game/_Game/Items/Companions/"
			"BP_Item_WintersRecall.BP_Item_WintersRecall_C"));
	auto RequireMutableRecall = [this](UEFCalystoDungeonDirectorPolicyV4* Policy)
	{
		FEFCalystoChestContentEntryV4* Recall = FindMutableWintersRecall(Policy);
		TestNotNull(TEXT("The mutation fixture contains Winter's Recall."), Recall);
		return Recall;
	};

	UEFCalystoDungeonDirectorPolicyV4* RecallAliasId = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallAliasId))
	{
		Recall->StableId = TEXT("ChestContent.WintersRecallAlias");
	}
	ExpectInvalid(RecallAliasId, TEXT("Winter's Recall class under an alias Stable ID fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallWrongClass = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallWrongClass))
	{
		Recall->ContentClass = TSoftClassPtr<UObject>(FSoftObjectPath(
			TEXT("/Game/FullSample/Blueprints/Items/Consumable/"
				"ACFHealthPotionBP.ACFHealthPotionBP_C")));
	}
	ExpectInvalid(RecallWrongClass, TEXT("Winter's Recall Stable ID with another class fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallDuplicateClass = MakePolicy();
	if (FEFCalystoCategoryProfileV4* Chests = FindMutableCategory(
		RecallDuplicateClass->Styles[0].Categories,
		EEFCalystoContentCategoryV4::Chest))
	{
		FEFCalystoChestContentEntryV4* Alias =
			Chests->ChestContentsCatalog.FindByPredicate(
				[](const FEFCalystoChestContentEntryV4& Entry)
				{
					return Entry.StableId !=
						FName(TEXT("Item.CompanionRevival.WintersRecall"));
				});
		if (Alias)
		{
			Alias->ContentClass = TSoftClassPtr<UObject>(RecallClassPath);
		}
	}
	ExpectInvalid(RecallDuplicateClass, TEXT("A second chest-content alias of the Winter's Recall class fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallIdOutsideContents = MakePolicy();
	if (FEFCalystoCategoryProfileV4* FoodCategory = FindMutableCategory(
		RecallIdOutsideContents->Styles[0].Categories,
		EEFCalystoContentCategoryV4::Food))
	{
		if (!FoodCategory->Catalog.IsEmpty())
		{
			FEFCalystoCatalogEntryV4 Alias = FoodCategory->Catalog[0];
			Alias.Name = TEXT("Invalid Winter's Recall actor alias");
			Alias.StableId = TEXT("Item.CompanionRevival.WintersRecall");
			FoodCategory->Catalog.Add(MoveTemp(Alias));
		}
	}
	ExpectInvalid(RecallIdOutsideContents, TEXT("Winter's Recall Stable ID outside Chest Contents fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallClassOutsideContents = MakePolicy();
	if (FEFCalystoCategoryProfileV4* FoodCategory = FindMutableCategory(
		RecallClassOutsideContents->Styles[0].Categories,
		EEFCalystoContentCategoryV4::Food))
	{
		if (!FoodCategory->Catalog.IsEmpty())
		{
			FoodCategory->Catalog[0].ActorClass =
				TSoftClassPtr<AActor>(RecallClassPath);
		}
	}
	ExpectInvalid(RecallClassOutsideContents, TEXT("Winter's Recall class outside Chest Contents fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallNotAllowed = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallNotAllowed))
	{
		Recall->Rule = EEFCalystoCatalogRuleV4::Block;
	}
	ExpectInvalid(RecallNotAllowed, TEXT("A blocked Winter's Recall entry fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallWrongTier = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallWrongTier))
	{
		Recall->Tier = EEFCalystoRarityTierV4::Rare;
	}
	ExpectInvalid(RecallWrongTier, TEXT("A non-Epic Winter's Recall fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallDelayedFloor = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallDelayedFloor))
	{
		Recall->FirstEligibleFloor = 2;
	}
	ExpectInvalid(RecallDelayedFloor, TEXT("A Winter's Recall floor gate that competes with graveyard eligibility fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallWrongMaximum = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallWrongMaximum))
	{
		Recall->MaxPerFloor = 2;
	}
	ExpectInvalid(RecallWrongMaximum, TEXT("More than one Winter's Recall per floor fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallWrongCooldown = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallWrongCooldown))
	{
		Recall->CooldownFloors = 7;
	}
	ExpectInvalid(RecallWrongCooldown, TEXT("A Winter's Recall cooldown other than eight floors fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* RecallWithoutGraveyard = MakePolicy();
	if (FEFCalystoChestContentEntryV4* Recall = RequireMutableRecall(RecallWithoutGraveyard))
	{
		Recall->bRequiresGraveyardEligibility = false;
	}
	ExpectInvalid(RecallWithoutGraveyard, TEXT("Winter's Recall without graveyard eligibility fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* MissingRecall = MakePolicy();
	if (FEFCalystoCategoryProfileV4* Chests = FindMutableCategory(
		MissingRecall->Styles[0].Categories,
		EEFCalystoContentCategoryV4::Chest))
	{
		Chests->ChestContentsCatalog.RemoveAll([](const FEFCalystoChestContentEntryV4& Entry)
		{
			return Entry.StableId == FName(TEXT("Item.CompanionRevival.WintersRecall"));
		});
	}
	ExpectInvalid(MissingRecall, TEXT("A profile without exactly one Winter's Recall fails closed."));

	UEFCalystoDungeonDirectorPolicyV4* DuplicateSize = MakePolicy();
	DuplicateSize->ValidatedDungeonSizes.Add(26);
	ExpectInvalid(DuplicateSize, TEXT("Duplicate certified dungeon size fails closed."));

	FEFCalystoResolveContextV4 ValidContext = MakeContext(1400000, 20, 1);
	FEFCalystoResolvedFloorIntentV4 ValidIntent;
	Error.Reset();
	TestTrue(*FString::Printf(TEXT("Control V4 context resolves: %s"), *Error), Resolve(Baseline, ValidContext, ValidIntent, &Error));
	TestEqual(TEXT("A valid intent hash is SHA-256 length."), ValidIntent.IntentHash.Len(), 64);

	FEFCalystoResolveContextV4 WrongSnapshotHash = ValidContext;
	WrongSnapshotHash.CompanionSnapshotHash = TEXT("BAD");
	FEFCalystoResolvedFloorIntentV4 RejectedIntent;
	Error.Reset();
	TestFalse(TEXT("Mismatched companion snapshot hash fails closed."), Resolve(Baseline, WrongSnapshotHash, RejectedIntent, &Error));

	FEFCalystoResolveContextV4 NaNContext = ValidContext;
	NaNContext.DirectorIntent.Danger = std::numeric_limits<float>::quiet_NaN();
	Error.Reset();
	TestFalse(TEXT("NaN Director intent fails closed."), Resolve(Baseline, NaNContext, RejectedIntent, &Error));

	FEFCalystoResolveContextV4 BadOutcome = ValidContext;
	BadOutcome.bHasFrozenOutcome = true;
	BadOutcome.FrozenOutcome.Combat = std::numeric_limits<float>::infinity();
	Error.Reset();
	TestFalse(TEXT("Non-finite frozen outcome fails closed."), Resolve(Baseline, BadOutcome, RejectedIntent, &Error));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
