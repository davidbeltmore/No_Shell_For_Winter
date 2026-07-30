#include "Characters/ProjectEnemyVisualVariationSelection.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace ProjectEnemyVisualVariationTestsPrivate
{
	static TArray<FMorphSliderEntry> MakeAllowedEntries(const UProjectEnemyVisualVariationSettings& Settings)
	{
		TArray<FMorphSliderEntry> Entries;
		Entries.Reserve(Settings.AllowedMorphNames.Num());

		for (const FName MorphName : Settings.AllowedMorphNames)
		{
			FMorphSliderEntry Entry;
			Entry.MorphName = MorphName;
			Entry.MinValue = -1.0f;
			Entry.MaxValue = 1.0f;
			Entry.DefaultValue = 0.0f;
			Entries.Add(Entry);
		}

		return Entries;
	}

	struct FMorphCount
	{
		FName MorphName = NAME_None;
		int32 Count = 0;
	};

	static TArray<FMorphCount> BuildSortedCounts(const TMap<FName, int32>& MorphCounts)
	{
		TArray<FMorphCount> SortedCounts;
		SortedCounts.Reserve(MorphCounts.Num());

		for (const TPair<FName, int32>& Pair : MorphCounts)
		{
			FMorphCount Count;
			Count.MorphName = Pair.Key;
			Count.Count = Pair.Value;
			SortedCounts.Add(Count);
		}

		SortedCounts.Sort([](const FMorphCount& Left, const FMorphCount& Right)
		{
			return Left.Count > Right.Count;
		});

		return SortedCounts;
	}

	static TArray<FName> MakeGroupOneMorphNames(const UProjectEnemyVisualVariationSettings& Settings)
	{
		TArray<FName> MorphNames;
		MorphNames.Reserve(Settings.OptionalMatureGroupOneMorphEntries.Num());

		for (const FProjectEnemyLevelBiasedMorphEntry& Entry : Settings.OptionalMatureGroupOneMorphEntries)
		{
			if (!Entry.MorphName.IsNone())
			{
				MorphNames.AddUnique(Entry.MorphName);
			}
		}

		return MorphNames;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationUniformDistributionTest,
	"NoShellForWinter.Enemies.VisualVariation.SelectionDistribution.Uniform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationUniformDistributionTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* Settings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), Settings))
	{
		return false;
	}

	TestEqual(
		TEXT("The project default should use the uniform morph distribution mode"),
		Settings->DistributionMode,
		EProjectEnemyMorphDistributionMode::Uniform);

	const TArray<FMorphSliderEntry> AllowedEntries = ProjectEnemyVisualVariationTestsPrivate::MakeAllowedEntries(*Settings);
	if (!TestTrue(TEXT("Allowed morph entries should be available for the uniform distribution test"), AllowedEntries.Num() > 0))
	{
		return false;
	}

	FRandomStream RandomStream(12345);
	TMap<FName, int32> MorphCounts;
	int32 PositiveValueCount = 0;
	int32 NegativeValueCount = 0;
	int32 NearZeroValueCount = 0;
	bool bAllValuesWithinRange = true;

	constexpr int32 RollCount = 10000;
	for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
	{
		FProjectEnemyMorphRollResult Result;
		if (!FProjectEnemyVisualVariationSelection::RollMorphVariation(AllowedEntries, *Settings, RandomStream, Result))
		{
			AddError(FString::Printf(TEXT("Uniform roll failed at iteration %d."), RollIndex));
			return false;
		}

		bAllValuesWithinRange &= Result.MorphValue >= -1.0f && Result.MorphValue <= 1.0f;
		MorphCounts.FindOrAdd(Result.MorphName)++;

		if (Result.MorphValue > 0.0f)
		{
			++PositiveValueCount;
		}
		else if (Result.MorphValue < 0.0f)
		{
			++NegativeValueCount;
		}
		else
		{
			++NearZeroValueCount;
		}
	}

	const float PositiveRatio = static_cast<float>(PositiveValueCount) / RollCount;
	const float NegativeRatio = static_cast<float>(NegativeValueCount) / RollCount;
	const float NearZeroRatio = static_cast<float>(NearZeroValueCount) / RollCount;
	const float AverageMorphCount = static_cast<float>(RollCount) / AllowedEntries.Num();

	TestTrue(TEXT("All uniform rolls should stay inside [-1, 1]"), bAllValuesWithinRange);
	TestTrue(TEXT("Uniform distribution should keep positive values near 50%"), PositiveRatio > 0.47f && PositiveRatio < 0.53f);
	TestTrue(TEXT("Uniform distribution should keep negative values near 50%"), NegativeRatio > 0.47f && NegativeRatio < 0.53f);
	TestTrue(TEXT("Uniform distribution should almost never land exactly on zero"), NearZeroRatio < 0.005f);

	int32 HighestMorphCount = 0;
	int32 LowestMorphCount = TNumericLimits<int32>::Max();
	for (const FName MorphName : Settings->AllowedMorphNames)
	{
		const int32 Count = MorphCounts.FindRef(MorphName);
		HighestMorphCount = FMath::Max(HighestMorphCount, Count);
		LowestMorphCount = FMath::Min(LowestMorphCount, Count);

		TestTrue(
			*FString::Printf(TEXT("Morph %s should remain close to the uniform average"), *MorphName.ToString()),
			Count >= AverageMorphCount * 0.75f && Count <= AverageMorphCount * 1.25f);
	}

	TestTrue(TEXT("Uniform mode should not create strong morph leaders"), HighestMorphCount - LowestMorphCount < AverageMorphCount * 0.35f);
	TestTrue(TEXT("Body Muscular Mass should not dominate in uniform mode"), MorphCounts.FindRef(TEXT("Body Muscular Mass")) < AverageMorphCount * 1.25f);
	TestTrue(TEXT("Body Muscular Details should not dominate in uniform mode"), MorphCounts.FindRef(TEXT("Body Muscular Details")) < AverageMorphCount * 1.25f);
	TestTrue(TEXT("Body Portly should not dominate in uniform mode"), MorphCounts.FindRef(TEXT("Body Portly")) < AverageMorphCount * 1.25f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationWeightedCompatibilityTest,
	"NoShellForWinter.Enemies.VisualVariation.SelectionDistribution.WeightedCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationWeightedCompatibilityTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* ProjectSettings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), ProjectSettings))
	{
		return false;
	}

	UProjectEnemyVisualVariationSettings* WeightedSettings =
		DuplicateObject<UProjectEnemyVisualVariationSettings>(ProjectSettings, GetTransientPackage());
	if (!TestNotNull(TEXT("Weighted test settings clone should exist"), WeightedSettings))
	{
		return false;
	}

	WeightedSettings->DistributionMode = EProjectEnemyMorphDistributionMode::Weighted;

	const TArray<FMorphSliderEntry> AllowedEntries = ProjectEnemyVisualVariationTestsPrivate::MakeAllowedEntries(*WeightedSettings);
	if (!TestTrue(TEXT("Allowed morph entries should be available for the weighted compatibility test"), AllowedEntries.Num() > 0))
	{
		return false;
	}

	const FProjectEnemyMorphSelectionContext SelectionContext;
	FRandomStream RandomStream(12345);
	TMap<FName, int32> PositiveMorphCounts;
	int32 PositiveBucketCount = 0;
	int32 NegativeBucketCount = 0;
	int32 NearZeroBucketCount = 0;
	int32 SmallMagnitudeCount = 0;
	bool bAllValuesWithinRange = true;
	bool bPositiveBucketStayedPositive = true;
	bool bNegativeBucketStayedNegative = true;
	bool bNearZeroBucketStayedSmall = true;

	constexpr int32 RollCount = 10000;
	for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
	{
		FProjectEnemyMorphRollResult Result;
		if (!FProjectEnemyVisualVariationSelection::RollMorphVariation(AllowedEntries, *WeightedSettings, SelectionContext, RandomStream, Result))
		{
			AddError(FString::Printf(TEXT("Weighted roll failed at iteration %d."), RollIndex));
			return false;
		}

		bAllValuesWithinRange &= Result.MorphValue >= -1.0f && Result.MorphValue <= 1.0f;
		if (FMath::Abs(Result.MorphValue) < WeightedSettings->NearZeroMaxAbsValue)
		{
			++SmallMagnitudeCount;
		}

		switch (Result.Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			++PositiveBucketCount;
			PositiveMorphCounts.FindOrAdd(Result.MorphName)++;
			bPositiveBucketStayedPositive &= Result.MorphValue >= WeightedSettings->PositiveMinAbsValue;
			break;

		case EProjectEnemyMorphBucket::Negative:
			++NegativeBucketCount;
			bNegativeBucketStayedNegative &= Result.MorphValue <= -WeightedSettings->NegativeMinAbsValue;
			break;

		case EProjectEnemyMorphBucket::NearZero:
			++NearZeroBucketCount;
			bNearZeroBucketStayedSmall &= FMath::Abs(Result.MorphValue) <= WeightedSettings->NearZeroMaxAbsValue;
			break;
		}
	}

	const float PositiveRatio = static_cast<float>(PositiveBucketCount) / RollCount;
	const float NegativeRatio = static_cast<float>(NegativeBucketCount) / RollCount;
	const float NearZeroRatio = static_cast<float>(NearZeroBucketCount) / RollCount;
	const float SmallMagnitudeRatio = static_cast<float>(SmallMagnitudeCount) / RollCount;

	TestTrue(TEXT("All weighted rolls should stay inside [-1, 1]"), bAllValuesWithinRange);
	TestTrue(TEXT("Positive weighted bucket values should stay near the positive extreme"), bPositiveBucketStayedPositive);
	TestTrue(TEXT("Negative weighted bucket values should stay near the negative extreme"), bNegativeBucketStayedNegative);
	TestTrue(TEXT("Near-zero weighted bucket values should stay small"), bNearZeroBucketStayedSmall);

	TestTrue(TEXT("Weighted positive bucket ratio should stay around 70%"), PositiveRatio > 0.66f && PositiveRatio < 0.74f);
	TestTrue(TEXT("Weighted negative bucket ratio should stay around 25%"), NegativeRatio > 0.21f && NegativeRatio < 0.29f);
	TestTrue(TEXT("Weighted near-zero bucket ratio should stay around 5%"), NearZeroRatio > 0.03f && NearZeroRatio < 0.07f);
	TestTrue(TEXT("Weighted low-magnitude values should remain a clear minority"), SmallMagnitudeRatio < 0.08f);

	TArray<ProjectEnemyVisualVariationTestsPrivate::FMorphCount> SortedPositiveMorphCounts =
		ProjectEnemyVisualVariationTestsPrivate::BuildSortedCounts(PositiveMorphCounts);

	if (!TestTrue(TEXT("Weighted compatibility mode should produce at least three morph leaders"), SortedPositiveMorphCounts.Num() >= 3))
	{
		return false;
	}

	const TSet<FName> TopPositiveMorphs = {
		SortedPositiveMorphCounts[0].MorphName,
		SortedPositiveMorphCounts[1].MorphName,
		SortedPositiveMorphCounts[2].MorphName
	};

	TestTrue(TEXT("Weighted leaders should still be muscular mass, muscular details, and portly"),
		TopPositiveMorphs.Contains(TEXT("Body Muscular Mass"))
		&& TopPositiveMorphs.Contains(TEXT("Body Muscular Details"))
		&& TopPositiveMorphs.Contains(TEXT("Body Portly")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationLevelBiasTest,
	"NoShellForWinter.Enemies.VisualVariation.SelectionDistribution.LevelBias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationLevelBiasTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* Settings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), Settings))
	{
		return false;
	}

	const TArray<FMorphSliderEntry> AllowedEntries = ProjectEnemyVisualVariationTestsPrivate::MakeAllowedEntries(*Settings);
	if (!TestTrue(TEXT("Allowed morph entries should be available for the level bias test"), AllowedEntries.Num() > 0))
	{
		return false;
	}

	FProjectEnemyMorphSelectionContext LowLevelContext;
	LowLevelContext.bHasNormalizedEnemyLevel = true;
	LowLevelContext.NormalizedEnemyLevel = 0.1f;

	FProjectEnemyMorphSelectionContext HighLevelContext;
	HighLevelContext.bHasNormalizedEnemyLevel = true;
	HighLevelContext.NormalizedEnemyLevel = 0.9f;

	FRandomStream LowRandomStream(4444);
	FRandomStream HighRandomStream(9999);
	int32 LowNegativeCount = 0;
	int32 HighPositiveCount = 0;
	TMap<FName, int32> LowMorphCounts;
	TMap<FName, int32> HighMorphCounts;

	constexpr int32 RollCount = 5000;
	for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
	{
		FProjectEnemyMorphRollResult LowResult;
		FProjectEnemyMorphRollResult HighResult;
		if (!FProjectEnemyVisualVariationSelection::RollMorphVariation(AllowedEntries, *Settings, LowLevelContext, LowRandomStream, LowResult)
			|| !FProjectEnemyVisualVariationSelection::RollMorphVariation(AllowedEntries, *Settings, HighLevelContext, HighRandomStream, HighResult))
		{
			AddError(TEXT("Level-biased morph rolls should succeed."));
			return false;
		}

		if (LowResult.Bucket == EProjectEnemyMorphBucket::Negative)
		{
			++LowNegativeCount;
		}

		if (HighResult.Bucket == EProjectEnemyMorphBucket::Positive)
		{
			++HighPositiveCount;
		}

		LowMorphCounts.FindOrAdd(LowResult.MorphName)++;
		HighMorphCounts.FindOrAdd(HighResult.MorphName)++;
	}

	TestTrue(TEXT("Low-level enemies should lean negative more often than not"), LowNegativeCount > RollCount * 0.45f);
	TestTrue(TEXT("High-level enemies should lean positive more often than not"), HighPositiveCount > RollCount * 0.45f);
	TestTrue(TEXT("Low-level enemies should favor thin"), LowMorphCounts.FindRef(TEXT("Body Thin")) > LowMorphCounts.FindRef(TEXT("Body Muscular Mass")));
	TestTrue(TEXT("High-level enemies should favor muscular mass"), HighMorphCounts.FindRef(TEXT("Body Muscular Mass")) > HighMorphCounts.FindRef(TEXT("Body Thin")));
	TestTrue(TEXT("High-level enemies should favor portly"), HighMorphCounts.FindRef(TEXT("Body Portly")) > HighMorphCounts.FindRef(TEXT("Body Emaciated")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationGroupOneBiasTest,
	"NoShellForWinter.Enemies.VisualVariation.OptionalMatureMorphs.GroupOneBias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationGroupOneBiasTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* Settings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), Settings))
	{
		return false;
	}

	const TArray<FName> AvailableMorphNames = ProjectEnemyVisualVariationTestsPrivate::MakeGroupOneMorphNames(*Settings);
	if (!TestTrue(TEXT("Group one morph names should be configured"), AvailableMorphNames.Num() == 3))
	{
		return false;
	}

	auto RunLevelBiasSample =
		[this, Settings, &AvailableMorphNames](const int32 EnemyLevel, const int32 Seed, TMap<FName, int32>& OutCounts)
	{
		OutCounts.Reset();
		FRandomStream RandomStream(Seed);

		constexpr int32 RollCount = 20000;
		for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
		{
			FProjectEnemyNamedMorphRollResult Result;
			if (!FProjectEnemyVisualVariationSelection::RollLevelBiasedPositiveMorph(
				AvailableMorphNames,
				Settings->OptionalMatureGroupOneMorphEntries,
				EnemyLevel,
				RandomStream,
				Result))
			{
				AddError(FString::Printf(TEXT("Group one roll failed at level %d."), EnemyLevel));
				return false;
			}

			OutCounts.FindOrAdd(Result.MorphName)++;
		}

		return true;
	};

	TMap<FName, int32> LowLevelCounts;
	TMap<FName, int32> MidLevelCounts;
	TMap<FName, int32> HighLevelCounts;
	if (!RunLevelBiasSample(5, 1105, LowLevelCounts)
		|| !RunLevelBiasSample(15, 1515, MidLevelCounts)
		|| !RunLevelBiasSample(60, 6060, HighLevelCounts))
	{
		return false;
	}

	TestTrue(TEXT("Cornelius should be favored at low level"), LowLevelCounts.FindRef(TEXT("DK_29-Cornelius")) > LowLevelCounts.FindRef(TEXT("DK_32-Caligula")));
	TestTrue(TEXT("Cornelius should still allow the others at low level"), LowLevelCounts.FindRef(TEXT("DK_40-MagnaMater")) > 0);
	TestTrue(TEXT("Caligula should be favored at mid level"), MidLevelCounts.FindRef(TEXT("DK_32-Caligula")) > MidLevelCounts.FindRef(TEXT("DK_29-Cornelius")));
	TestTrue(TEXT("MagnaMater should be favored at high level"), HighLevelCounts.FindRef(TEXT("DK_40-MagnaMater")) > HighLevelCounts.FindRef(TEXT("DK_32-Caligula")));
	TestTrue(TEXT("High level should still allow Cornelius sometimes"), HighLevelCounts.FindRef(TEXT("DK_29-Cornelius")) > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationGroupTwoValueBandTest,
	"NoShellForWinter.Enemies.VisualVariation.OptionalMatureMorphs.GroupTwoValueBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationGroupTwoValueBandTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* Settings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), Settings))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("Optional mature group two morph names should be configured"),
		Settings->OptionalMatureGroupTwoMorphNames.Num() > 0))
	{
		return false;
	}

	FRandomStream RandomStream(7020);
	int32 HighBandCount = 0;
	bool bAllValuesInRange = true;

	constexpr int32 RollCount = 10000;
	for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
	{
		FProjectEnemyNamedMorphRollResult Result;
		if (!FProjectEnemyVisualVariationSelection::RollBandBiasedPositiveMorph(
			Settings->OptionalMatureGroupTwoMorphNames,
			Settings->OptionalMatureGroupTwoHighValueChance,
			Settings->OptionalMatureGroupTwoHighValueMin,
			Settings->OptionalMatureGroupTwoHighValueMax,
			Settings->OptionalMatureGroupTwoLowValueMin,
			Settings->OptionalMatureGroupTwoLowValueMax,
			RandomStream,
			Result))
		{
			AddError(TEXT("Group two roll should always succeed when morph names are configured."));
			return false;
		}

		bAllValuesInRange &= Result.MorphValue >= 0.0f && Result.MorphValue <= 1.0f;
		if (Result.MorphValue >= Settings->OptionalMatureGroupTwoHighValueMin)
		{
			++HighBandCount;
		}
	}

	const float HighBandRatio = static_cast<float>(HighBandCount) / RollCount;
	TestTrue(TEXT("Group two values should stay inside [0, 1]"), bAllValuesInRange);
	TestTrue(TEXT("Group two should land in the high band around 70% of the time"), HighBandRatio > 0.66f && HighBandRatio < 0.74f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyVisualVariationGroupThreeScalingTest,
	"NoShellForWinter.Enemies.VisualVariation.OptionalMatureMorphs.GroupThreeScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyVisualVariationGroupThreeScalingTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyVisualVariationSettings* Settings = GetDefault<UProjectEnemyVisualVariationSettings>();
	if (!TestNotNull(TEXT("Project enemy visual variation settings should exist"), Settings))
	{
		return false;
	}

	const float BelowStartAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		1,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);
	const float PreStartAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		24,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);
	const float StartAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		25,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);
	const float MidAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		75,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);
	const float MaxAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		125,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);
	const float BeyondMaxAlpha = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		160,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);

	TestTrue(TEXT("Level one should not contribute to group three"), FMath::IsNearlyZero(BelowStartAlpha));
	TestTrue(TEXT("Levels before 25 should stay at zero"), FMath::IsNearlyZero(PreStartAlpha));
	TestTrue(TEXT("Level 25 should start at zero"), FMath::IsNearlyZero(StartAlpha));
	TestTrue(TEXT("Level 75 should land around the midpoint"), FMath::IsNearlyEqual(MidAlpha, 0.5f));
	TestTrue(TEXT("Level 125 should clamp to one"), FMath::IsNearlyEqual(MaxAlpha, 1.0f));
	TestTrue(TEXT("Levels above 125 should remain clamped"), FMath::IsNearlyEqual(BeyondMaxAlpha, 1.0f));

	return true;
}

#endif
