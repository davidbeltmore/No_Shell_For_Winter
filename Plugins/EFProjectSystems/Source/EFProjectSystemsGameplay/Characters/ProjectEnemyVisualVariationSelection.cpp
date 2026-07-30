#include "Characters/ProjectEnemyVisualVariationSelection.h"

#include "Characters/ProjectEnemyVisualVariationSettings.h"

namespace ProjectEnemyVisualVariationSelectionPrivate
{
	struct FResolvedMorphCandidate
	{
		int32 EntryIndex = INDEX_NONE;
		FName MorphName = NAME_None;
		float PositiveWeight = 1.0f;
		float NegativeWeight = 1.0f;
		float NearZeroWeight = 1.0f;
		float EffectiveMinValue = -1.0f;
		float EffectiveMaxValue = 1.0f;
		bool bHasValidRange = false;
	};

	static float NextUnitRandom(FRandomStream* RandomStream)
	{
		return RandomStream ? RandomStream->FRand() : FMath::FRand();
	}

	static float NextRange(FRandomStream* RandomStream, const float MinValue, const float MaxValue)
	{
		return FMath::Lerp(MinValue, MaxValue, NextUnitRandom(RandomStream));
	}

	static int32 NextIntRange(FRandomStream* RandomStream, const int32 MinValue, const int32 MaxValue)
	{
		return RandomStream ? RandomStream->RandRange(MinValue, MaxValue) : FMath::RandRange(MinValue, MaxValue);
	}

	static bool IsMorphAvailable(const TArray<FName>& AvailableMorphNames, const FName MorphName)
	{
		return !MorphName.IsNone() && AvailableMorphNames.Contains(MorphName);
	}

	static bool IsWithinPreferredLevelRange(const FProjectEnemyLevelBiasedMorphEntry& Entry, const int32 EnemyLevel)
	{
		if (EnemyLevel < Entry.PreferredMinLevel)
		{
			return false;
		}

		return Entry.PreferredMaxLevel <= 0 || EnemyLevel <= Entry.PreferredMaxLevel;
	}

	static const FProjectEnemyMorphSelectionContext& GetNeutralSelectionContext()
	{
		static const FProjectEnemyMorphSelectionContext NeutralContext;
		return NeutralContext;
	}

	static float GetClampedEnemyLevel(const FProjectEnemyMorphSelectionContext& SelectionContext)
	{
		return SelectionContext.bHasNormalizedEnemyLevel
			? FMath::Clamp(SelectionContext.NormalizedEnemyLevel, 0.0f, 1.0f)
			: 0.5f;
	}

	static bool IsHeavyPositiveMorph(const FName MorphName)
	{
		return MorphName == TEXT("Body Muscular Mass")
			|| MorphName == TEXT("Body Muscular Details")
			|| MorphName == TEXT("Body Portly")
			|| MorphName == TEXT("Body Heavy")
			|| MorphName == TEXT("Body Stocky");
	}

	static bool IsLeanNegativeMorph(const FName MorphName)
	{
		return MorphName == TEXT("Body Thin")
			|| MorphName == TEXT("Body Emaciated")
			|| MorphName == TEXT("Body Lithe")
			|| MorphName == TEXT("Body Fitness Details");
	}

	static float ApplyFutureLevelBiasToBucketWeight(
		const float BaseWeight,
		const EProjectEnemyMorphBucket Bucket,
		const FProjectEnemyMorphSelectionContext& SelectionContext)
	{
		if (!SelectionContext.bHasNormalizedEnemyLevel)
		{
			return BaseWeight;
		}

		const float NormalizedEnemyLevel = GetClampedEnemyLevel(SelectionContext);
		float BiasedWeight = BaseWeight;

		switch (Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			BiasedWeight = FMath::Lerp(0.20f, 0.65f, NormalizedEnemyLevel);
			break;
		case EProjectEnemyMorphBucket::Negative:
			BiasedWeight = FMath::Lerp(0.65f, 0.20f, NormalizedEnemyLevel);
			break;
		case EProjectEnemyMorphBucket::NearZero:
			BiasedWeight = 0.15f;
			break;
		default:
			break;
		}

		return FMath::Max(BiasedWeight, 0.0f);
	}

	static float ApplyFutureLevelBiasToMorphWeight(
		const float BaseWeight,
		const FName MorphName,
		const EProjectEnemyMorphBucket Bucket,
		const FProjectEnemyMorphSelectionContext& SelectionContext)
	{
		if (!SelectionContext.bHasNormalizedEnemyLevel)
		{
			return BaseWeight;
		}

		const float NormalizedEnemyLevel = GetClampedEnemyLevel(SelectionContext);
		float WeightMultiplier = 1.0f;

		if (IsHeavyPositiveMorph(MorphName))
		{
			WeightMultiplier = FMath::Lerp(0.35f, 2.20f, NormalizedEnemyLevel);
		}
		else if (IsLeanNegativeMorph(MorphName))
		{
			WeightMultiplier = FMath::Lerp(2.20f, 0.35f, NormalizedEnemyLevel);
		}

		switch (Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			if (IsLeanNegativeMorph(MorphName))
			{
				WeightMultiplier *= FMath::Lerp(1.50f, 0.40f, NormalizedEnemyLevel);
			}
			break;
		case EProjectEnemyMorphBucket::Negative:
			if (IsHeavyPositiveMorph(MorphName))
			{
				WeightMultiplier *= FMath::Lerp(0.40f, 1.50f, 1.0f - NormalizedEnemyLevel);
			}
			break;
		case EProjectEnemyMorphBucket::NearZero:
			WeightMultiplier = 1.0f;
			break;
		default:
			break;
		}

		return FMath::Max(BaseWeight * WeightMultiplier, 0.0f);
	}

	static const FProjectEnemyMorphBiasEntry* FindBiasEntry(
		const UProjectEnemyVisualVariationSettings& Settings,
		const FName MorphName)
	{
		return Settings.MorphBiasEntries.FindByPredicate([MorphName](const FProjectEnemyMorphBiasEntry& Entry)
		{
			return Entry.MorphName == MorphName;
		});
	}

	static bool ComputeEffectiveRange(
		const FMorphSliderEntry& Entry,
		const UProjectEnemyVisualVariationSettings& Settings,
		float& OutMinValue,
		float& OutMaxValue)
	{
		const float GlobalMinValue = FMath::Min(Settings.MorphMinValue, Settings.MorphMaxValue);
		const float GlobalMaxValue = FMath::Max(Settings.MorphMinValue, Settings.MorphMaxValue);
		OutMinValue = FMath::Max(Entry.MinValue, GlobalMinValue);
		OutMaxValue = FMath::Min(Entry.MaxValue, GlobalMaxValue);
		return OutMinValue <= OutMaxValue;
	}

	static FResolvedMorphCandidate ResolveCandidate(
		const FMorphSliderEntry& Entry,
		const int32 EntryIndex,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext)
	{
		FResolvedMorphCandidate Candidate;
		Candidate.EntryIndex = EntryIndex;
		Candidate.MorphName = Entry.MorphName;
		Candidate.bHasValidRange = ComputeEffectiveRange(Entry, Settings, Candidate.EffectiveMinValue, Candidate.EffectiveMaxValue);

		if (const FProjectEnemyMorphBiasEntry* BiasEntry = FindBiasEntry(Settings, Entry.MorphName))
		{
			Candidate.PositiveWeight = FMath::Max(0.0f, BiasEntry->PositiveWeight);
			Candidate.NegativeWeight = FMath::Max(0.0f, BiasEntry->NegativeWeight);
			Candidate.NearZeroWeight = FMath::Max(0.0f, BiasEntry->NearZeroWeight);
		}

		Candidate.PositiveWeight = ApplyFutureLevelBiasToMorphWeight(
			Candidate.PositiveWeight,
			Candidate.MorphName,
			EProjectEnemyMorphBucket::Positive,
			SelectionContext);
		Candidate.NegativeWeight = ApplyFutureLevelBiasToMorphWeight(
			Candidate.NegativeWeight,
			Candidate.MorphName,
			EProjectEnemyMorphBucket::Negative,
			SelectionContext);
		Candidate.NearZeroWeight = ApplyFutureLevelBiasToMorphWeight(
			Candidate.NearZeroWeight,
			Candidate.MorphName,
			EProjectEnemyMorphBucket::NearZero,
			SelectionContext);

		return Candidate;
	}

	static EProjectEnemyMorphBucket BucketFromValue(const float MorphValue)
	{
		if (MorphValue > 0.0f)
		{
			return EProjectEnemyMorphBucket::Positive;
		}

		if (MorphValue < 0.0f)
		{
			return EProjectEnemyMorphBucket::Negative;
		}

		return EProjectEnemyMorphBucket::NearZero;
	}

	static float GetCandidateWeight(const FResolvedMorphCandidate& Candidate, const EProjectEnemyMorphBucket Bucket)
	{
		if (!Candidate.bHasValidRange)
		{
			return 0.0f;
		}

		switch (Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			return Candidate.PositiveWeight;
		case EProjectEnemyMorphBucket::Negative:
			return Candidate.NegativeWeight;
		case EProjectEnemyMorphBucket::NearZero:
			return Candidate.NearZeroWeight;
		default:
			return 0.0f;
		}
	}

	static float GetBucketWeight(
		const UProjectEnemyVisualVariationSettings& Settings,
		const EProjectEnemyMorphBucket Bucket,
		const FProjectEnemyMorphSelectionContext& SelectionContext)
	{
		float BaseWeight = 0.0f;
		switch (Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			BaseWeight = FMath::Max(0.0f, Settings.PositiveBucketChance);
			break;
		case EProjectEnemyMorphBucket::Negative:
			BaseWeight = FMath::Max(0.0f, Settings.NegativeBucketChance);
			break;
		case EProjectEnemyMorphBucket::NearZero:
			BaseWeight = FMath::Max(0.0f, Settings.NearZeroBucketChance);
			break;
		default:
			break;
		}

		return ApplyFutureLevelBiasToBucketWeight(BaseWeight, Bucket, SelectionContext);
	}

	static int32 PickWeightedCandidateIndex(
		const TArray<FResolvedMorphCandidate>& Candidates,
		const EProjectEnemyMorphBucket Bucket,
		FRandomStream* RandomStream)
	{
		float TotalWeight = 0.0f;
		for (const FResolvedMorphCandidate& Candidate : Candidates)
		{
			TotalWeight += GetCandidateWeight(Candidate, Bucket);
		}

		if (TotalWeight <= 0.0f)
		{
			return INDEX_NONE;
		}

		const float Roll = NextUnitRandom(RandomStream) * TotalWeight;
		float RunningWeight = 0.0f;
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			RunningWeight += GetCandidateWeight(Candidates[CandidateIndex], Bucket);
			if (Roll <= RunningWeight)
			{
				return CandidateIndex;
			}
		}

		return Candidates.Num() - 1;
	}

	static EProjectEnemyMorphBucket PickInitialBucket(
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext,
		FRandomStream* RandomStream,
		bool& bOutHasAnyBucket)
	{
		constexpr EProjectEnemyMorphBucket Buckets[] = {
			EProjectEnemyMorphBucket::Positive,
			EProjectEnemyMorphBucket::Negative,
			EProjectEnemyMorphBucket::NearZero
		};

		float TotalWeight = 0.0f;
		for (const EProjectEnemyMorphBucket Bucket : Buckets)
		{
			TotalWeight += GetBucketWeight(Settings, Bucket, SelectionContext);
		}

		if (TotalWeight <= 0.0f)
		{
			bOutHasAnyBucket = false;
			return EProjectEnemyMorphBucket::Positive;
		}

		const float Roll = NextUnitRandom(RandomStream) * TotalWeight;
		float RunningWeight = 0.0f;
		for (const EProjectEnemyMorphBucket Bucket : Buckets)
		{
			RunningWeight += GetBucketWeight(Settings, Bucket, SelectionContext);
			if (Roll <= RunningWeight)
			{
				bOutHasAnyBucket = true;
				return Bucket;
			}
		}

		bOutHasAnyBucket = true;
		return EProjectEnemyMorphBucket::NearZero;
	}

	static void BuildBucketFallbackOrder(
		const EProjectEnemyMorphBucket InitialBucket,
		TArray<EProjectEnemyMorphBucket, TInlineAllocator<3>>& OutBuckets)
	{
		OutBuckets.Reset();
		OutBuckets.Add(InitialBucket);

		switch (InitialBucket)
		{
		case EProjectEnemyMorphBucket::Positive:
			OutBuckets.Add(EProjectEnemyMorphBucket::Negative);
			OutBuckets.Add(EProjectEnemyMorphBucket::NearZero);
			break;
		case EProjectEnemyMorphBucket::Negative:
			OutBuckets.Add(EProjectEnemyMorphBucket::Positive);
			OutBuckets.Add(EProjectEnemyMorphBucket::NearZero);
			break;
		case EProjectEnemyMorphBucket::NearZero:
			OutBuckets.Add(EProjectEnemyMorphBucket::Positive);
			OutBuckets.Add(EProjectEnemyMorphBucket::Negative);
			break;
		default:
			break;
		}
	}

	static float GenerateWeightedMorphValue(
		const UProjectEnemyVisualVariationSettings& Settings,
		const FResolvedMorphCandidate& Candidate,
		const EProjectEnemyMorphBucket Bucket,
		FRandomStream* RandomStream,
		bool& bOutIsValid)
	{
		if (!Candidate.bHasValidRange)
		{
			bOutIsValid = false;
			return 0.0f;
		}

		float RawValue = 0.0f;
		switch (Bucket)
		{
		case EProjectEnemyMorphBucket::Positive:
		{
			const float Exponent = FMath::Max(Settings.PositiveExtremeExponent, 0.01f);
			const float MinimumMagnitude = FMath::Clamp(Settings.PositiveMinAbsValue, 0.0f, 1.0f);
			const float Alpha = FMath::Pow(NextUnitRandom(RandomStream), Exponent);
			RawValue = FMath::Lerp(MinimumMagnitude, 1.0f, Alpha);
			break;
		}
		case EProjectEnemyMorphBucket::Negative:
		{
			const float Exponent = FMath::Max(Settings.NegativeExtremeExponent, 0.01f);
			const float MinimumMagnitude = FMath::Clamp(Settings.NegativeMinAbsValue, 0.0f, 1.0f);
			const float Alpha = FMath::Pow(NextUnitRandom(RandomStream), Exponent);
			RawValue = -FMath::Lerp(MinimumMagnitude, 1.0f, Alpha);
			break;
		}
		case EProjectEnemyMorphBucket::NearZero:
		{
			const float MaxMagnitude = FMath::Clamp(Settings.NearZeroMaxAbsValue, 0.0f, 1.0f);
			const float Magnitude = NextRange(RandomStream, 0.0f, MaxMagnitude);
			const float Sign = NextUnitRandom(RandomStream) < 0.5f ? -1.0f : 1.0f;
			RawValue = Magnitude * Sign;
			break;
		}
		default:
			bOutIsValid = false;
			return 0.0f;
		}

		bOutIsValid = true;
		return FMath::Clamp(RawValue, Candidate.EffectiveMinValue, Candidate.EffectiveMaxValue);
	}

	static bool RollUniformMorphVariation(
		const TArray<FResolvedMorphCandidate>& Candidates,
		FRandomStream* RandomStream,
		FProjectEnemyMorphRollResult& OutResult)
	{
		TArray<int32, TInlineAllocator<32>> ValidCandidateIndices;
		ValidCandidateIndices.Reserve(Candidates.Num());

		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (Candidates[CandidateIndex].bHasValidRange)
			{
				ValidCandidateIndices.Add(CandidateIndex);
			}
		}

		if (ValidCandidateIndices.IsEmpty())
		{
			return false;
		}

		const int32 ChosenValidIndex = NextIntRange(RandomStream, 0, ValidCandidateIndices.Num() - 1);
		const FResolvedMorphCandidate& Candidate = Candidates[ValidCandidateIndices[ChosenValidIndex]];
		const float MorphValue = NextRange(RandomStream, Candidate.EffectiveMinValue, Candidate.EffectiveMaxValue);

		OutResult.bIsValid = true;
		OutResult.Bucket = BucketFromValue(MorphValue);
		OutResult.SelectedEntryIndex = Candidate.EntryIndex;
		OutResult.MorphName = Candidate.MorphName;
		OutResult.MorphValue = MorphValue;
		return true;
	}

	static bool RollWeightedMorphVariation(
		const TArray<FResolvedMorphCandidate>& Candidates,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext,
		FRandomStream* RandomStream,
		FProjectEnemyMorphRollResult& OutResult)
	{
		bool bHasAnyBucket = false;
		const EProjectEnemyMorphBucket InitialBucket = PickInitialBucket(Settings, SelectionContext, RandomStream, bHasAnyBucket);
		if (!bHasAnyBucket)
		{
			return false;
		}

		TArray<EProjectEnemyMorphBucket, TInlineAllocator<3>> BucketOrder;
		BuildBucketFallbackOrder(InitialBucket, BucketOrder);

		for (const EProjectEnemyMorphBucket Bucket : BucketOrder)
		{
			const int32 CandidateIndex = PickWeightedCandidateIndex(Candidates, Bucket, RandomStream);
			if (CandidateIndex == INDEX_NONE || !Candidates.IsValidIndex(CandidateIndex))
			{
				continue;
			}

			const FResolvedMorphCandidate& Candidate = Candidates[CandidateIndex];

			bool bIsValueValid = false;
			const float MorphValue = GenerateWeightedMorphValue(Settings, Candidate, Bucket, RandomStream, bIsValueValid);
			if (!bIsValueValid)
			{
				continue;
			}

			OutResult.bIsValid = true;
			OutResult.Bucket = Bucket;
			OutResult.SelectedEntryIndex = Candidate.EntryIndex;
			OutResult.MorphName = Candidate.MorphName;
			OutResult.MorphValue = MorphValue;
			return true;
		}

		return false;
	}

	static bool RollMorphVariationInternal(
		const TArray<FMorphSliderEntry>& AllowedEntries,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext,
		FRandomStream* RandomStream,
		FProjectEnemyMorphRollResult& OutResult)
	{
		OutResult = FProjectEnemyMorphRollResult();
		if (AllowedEntries.IsEmpty())
		{
			return false;
		}

		TArray<FResolvedMorphCandidate> Candidates;
		Candidates.Reserve(AllowedEntries.Num());

		for (int32 EntryIndex = 0; EntryIndex < AllowedEntries.Num(); ++EntryIndex)
		{
			Candidates.Add(ResolveCandidate(AllowedEntries[EntryIndex], EntryIndex, Settings, SelectionContext));
		}

		const EProjectEnemyMorphDistributionMode EffectiveDistributionMode =
			SelectionContext.bHasNormalizedEnemyLevel
			? EProjectEnemyMorphDistributionMode::Weighted
			: Settings.DistributionMode;

		switch (EffectiveDistributionMode)
		{
		case EProjectEnemyMorphDistributionMode::Uniform:
			return RollUniformMorphVariation(Candidates, RandomStream, OutResult);

		case EProjectEnemyMorphDistributionMode::Weighted:
			return RollWeightedMorphVariation(Candidates, Settings, SelectionContext, RandomStream, OutResult);

		default:
			return false;
		}
	}

	static bool RollLevelBiasedPositiveMorphInternal(
		const TArray<FName>& AvailableMorphNames,
		const TArray<FProjectEnemyLevelBiasedMorphEntry>& CandidateSettings,
		const int32 EnemyLevel,
		FRandomStream* RandomStream,
		FProjectEnemyNamedMorphRollResult& OutResult)
	{
		OutResult = FProjectEnemyNamedMorphRollResult();
		if (AvailableMorphNames.IsEmpty() || CandidateSettings.IsEmpty())
		{
			return false;
		}

		struct FResolvedNamedCandidate
		{
			FName MorphName = NAME_None;
			float Weight = 1.0f;
		};

		TArray<FResolvedNamedCandidate> Candidates;
		Candidates.Reserve(CandidateSettings.Num());

		for (const FProjectEnemyLevelBiasedMorphEntry& CandidateSetting : CandidateSettings)
		{
			if (!IsMorphAvailable(AvailableMorphNames, CandidateSetting.MorphName))
			{
				continue;
			}

			FResolvedNamedCandidate Candidate;
			Candidate.MorphName = CandidateSetting.MorphName;
			Candidate.Weight = 1.0f;

			if (IsWithinPreferredLevelRange(CandidateSetting, EnemyLevel))
			{
				Candidate.Weight *= FMath::Max(0.0f, CandidateSetting.PreferredRangeWeightMultiplier);
			}

			Candidates.Add(Candidate);
		}

		if (Candidates.IsEmpty())
		{
			return false;
		}

		float TotalWeight = 0.0f;
		for (const FResolvedNamedCandidate& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(0.0f, Candidate.Weight);
		}

		if (TotalWeight <= 0.0f)
		{
			return false;
		}

		const float Roll = NextUnitRandom(RandomStream) * TotalWeight;
		float RunningWeight = 0.0f;
		for (const FResolvedNamedCandidate& Candidate : Candidates)
		{
			RunningWeight += FMath::Max(0.0f, Candidate.Weight);
			if (Roll <= RunningWeight)
			{
				OutResult.bIsValid = true;
				OutResult.MorphName = Candidate.MorphName;
				OutResult.MorphValue = NextRange(RandomStream, 0.0f, 1.0f);
				return true;
			}
		}

		OutResult.bIsValid = true;
		OutResult.MorphName = Candidates.Last().MorphName;
		OutResult.MorphValue = NextRange(RandomStream, 0.0f, 1.0f);
		return true;
	}

	static bool RollBandBiasedPositiveMorphInternal(
		const TArray<FName>& AvailableMorphNames,
		const float HighBandChance,
		const float HighBandMinValue,
		const float HighBandMaxValue,
		const float LowBandMinValue,
		const float LowBandMaxValue,
		FRandomStream* RandomStream,
		FProjectEnemyNamedMorphRollResult& OutResult)
	{
		OutResult = FProjectEnemyNamedMorphRollResult();
		if (AvailableMorphNames.IsEmpty())
		{
			return false;
		}

		const int32 SelectedMorphIndex = NextIntRange(RandomStream, 0, AvailableMorphNames.Num() - 1);
		const bool bUseHighBand = NextUnitRandom(RandomStream) <= FMath::Clamp(HighBandChance, 0.0f, 1.0f);
		const float SelectedMinValue = bUseHighBand ? FMath::Min(HighBandMinValue, HighBandMaxValue) : FMath::Min(LowBandMinValue, LowBandMaxValue);
		const float SelectedMaxValue = bUseHighBand ? FMath::Max(HighBandMinValue, HighBandMaxValue) : FMath::Max(LowBandMinValue, LowBandMaxValue);

		OutResult.bIsValid = true;
		OutResult.MorphName = AvailableMorphNames[SelectedMorphIndex];
		OutResult.MorphValue = NextRange(RandomStream, SelectedMinValue, SelectedMaxValue);
		return true;
	}
}

bool FProjectEnemyVisualVariationSelection::RollMorphVariation(
	const TArray<FMorphSliderEntry>& AllowedEntries,
	const UProjectEnemyVisualVariationSettings& Settings,
	const FProjectEnemyMorphSelectionContext& SelectionContext,
	FProjectEnemyMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollMorphVariationInternal(
		AllowedEntries,
		Settings,
		SelectionContext,
		nullptr,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollMorphVariation(
	const TArray<FMorphSliderEntry>& AllowedEntries,
	const UProjectEnemyVisualVariationSettings& Settings,
	FProjectEnemyMorphRollResult& OutResult)
{
	return RollMorphVariation(
		AllowedEntries,
		Settings,
		ProjectEnemyVisualVariationSelectionPrivate::GetNeutralSelectionContext(),
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollMorphVariation(
	const TArray<FMorphSliderEntry>& AllowedEntries,
	const UProjectEnemyVisualVariationSettings& Settings,
	const FProjectEnemyMorphSelectionContext& SelectionContext,
	FRandomStream& RandomStream,
	FProjectEnemyMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollMorphVariationInternal(
		AllowedEntries,
		Settings,
		SelectionContext,
		&RandomStream,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollMorphVariation(
	const TArray<FMorphSliderEntry>& AllowedEntries,
	const UProjectEnemyVisualVariationSettings& Settings,
	FRandomStream& RandomStream,
	FProjectEnemyMorphRollResult& OutResult)
{
	return RollMorphVariation(
		AllowedEntries,
		Settings,
		ProjectEnemyVisualVariationSelectionPrivate::GetNeutralSelectionContext(),
		RandomStream,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollLevelBiasedPositiveMorph(
	const TArray<FName>& AvailableMorphNames,
	const TArray<FProjectEnemyLevelBiasedMorphEntry>& CandidateSettings,
	const int32 EnemyLevel,
	FProjectEnemyNamedMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollLevelBiasedPositiveMorphInternal(
		AvailableMorphNames,
		CandidateSettings,
		EnemyLevel,
		nullptr,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollLevelBiasedPositiveMorph(
	const TArray<FName>& AvailableMorphNames,
	const TArray<FProjectEnemyLevelBiasedMorphEntry>& CandidateSettings,
	const int32 EnemyLevel,
	FRandomStream& RandomStream,
	FProjectEnemyNamedMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollLevelBiasedPositiveMorphInternal(
		AvailableMorphNames,
		CandidateSettings,
		EnemyLevel,
		&RandomStream,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollBandBiasedPositiveMorph(
	const TArray<FName>& AvailableMorphNames,
	const float HighBandChance,
	const float HighBandMinValue,
	const float HighBandMaxValue,
	const float LowBandMinValue,
	const float LowBandMaxValue,
	FProjectEnemyNamedMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollBandBiasedPositiveMorphInternal(
		AvailableMorphNames,
		HighBandChance,
		HighBandMinValue,
		HighBandMaxValue,
		LowBandMinValue,
		LowBandMaxValue,
		nullptr,
		OutResult);
}

bool FProjectEnemyVisualVariationSelection::RollBandBiasedPositiveMorph(
	const TArray<FName>& AvailableMorphNames,
	const float HighBandChance,
	const float HighBandMinValue,
	const float HighBandMaxValue,
	const float LowBandMinValue,
	const float LowBandMaxValue,
	FRandomStream& RandomStream,
	FProjectEnemyNamedMorphRollResult& OutResult)
{
	return ProjectEnemyVisualVariationSelectionPrivate::RollBandBiasedPositiveMorphInternal(
		AvailableMorphNames,
		HighBandChance,
		HighBandMinValue,
		HighBandMaxValue,
		LowBandMinValue,
		LowBandMaxValue,
		&RandomStream,
		OutResult);
}

float FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(const int32 EnemyLevel, const int32 StartLevel, const int32 MaxLevel)
{
	if (EnemyLevel <= StartLevel)
	{
		return 0.0f;
	}

	if (MaxLevel <= StartLevel)
	{
		return EnemyLevel > StartLevel ? 1.0f : 0.0f;
	}

	return FMath::Clamp(
		static_cast<float>(EnemyLevel - StartLevel) / static_cast<float>(MaxLevel - StartLevel),
		0.0f,
		1.0f);
}
