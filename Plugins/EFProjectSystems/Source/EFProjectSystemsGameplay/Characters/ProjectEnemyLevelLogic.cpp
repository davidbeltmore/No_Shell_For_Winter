#include "Characters/ProjectEnemyLevelLogic.h"

#include "Characters/ProjectEnemyLevelSettings.h"

namespace ProjectEnemyLevelLogicPrivate
{
	static int32 ClampWorldTier(const int32 WorldTier)
	{
		return FMath::Max(WorldTier, 1);
	}

	static int32 GetSanitizedRangeWidth(const UProjectEnemyLevelSettings& Settings)
	{
		return FMath::Max(Settings.LevelRangeWidth, 1);
	}

	static int32 GetWeightForOffset(const UProjectEnemyLevelSettings& Settings, const int32 OffsetIndex)
	{
		if (Settings.LevelOffsetWeights.IsValidIndex(OffsetIndex))
		{
			return FMath::Max(Settings.LevelOffsetWeights[OffsetIndex], 0);
		}

		return 1;
	}

	static float NextUnitRandom(FRandomStream* RandomStream)
	{
		return RandomStream ? RandomStream->FRand() : FMath::FRand();
	}

	static bool MatchesRulePattern(const FString& MapName, const FString& RulePattern)
	{
		if (RulePattern.IsEmpty())
		{
			return false;
		}

		if (RulePattern.Contains(TEXT("*")) || RulePattern.Contains(TEXT("?")))
		{
			return MapName.MatchesWildcard(RulePattern, ESearchCase::IgnoreCase);
		}

		return MapName.Contains(RulePattern, ESearchCase::IgnoreCase);
	}
}

bool FProjectEnemyLevelLogic::ResolveWorldTierForMapName(
	const FString& MapName,
	const UProjectEnemyLevelSettings& Settings,
	int32& OutWorldTier)
{
	for (const FProjectEnemyMapLevelRule& Rule : Settings.MapLevelRules)
	{
		if (!ProjectEnemyLevelLogicPrivate::MatchesRulePattern(MapName, Rule.MapNamePattern))
		{
			continue;
		}

		OutWorldTier = ProjectEnemyLevelLogicPrivate::ClampWorldTier(Rule.WorldTier);
		return true;
	}

	OutWorldTier = ProjectEnemyLevelLogicPrivate::ClampWorldTier(Settings.DefaultWorldTier);
	return false;
}

FProjectEnemyLevelContext FProjectEnemyLevelLogic::BuildLevelContext(const int32 WorldTier, const UProjectEnemyLevelSettings& Settings)
{
	FProjectEnemyLevelContext Context;
	Context.WorldTier = ProjectEnemyLevelLogicPrivate::ClampWorldTier(WorldTier);
	Context.MinLevel = Context.WorldTier;
	Context.MaxLevel = Context.WorldTier + ProjectEnemyLevelLogicPrivate::GetSanitizedRangeWidth(Settings) - 1;
	return Context;
}

bool FProjectEnemyLevelLogic::RollEnemyLevel(
	const FProjectEnemyLevelContext& Context,
	const UProjectEnemyLevelSettings& Settings,
	FProjectEnemyLevelRollResult& OutResult)
{
	FRandomStream RandomStream(FMath::Rand());
	return RollEnemyLevel(Context, Settings, RandomStream, OutResult);
}

bool FProjectEnemyLevelLogic::RollEnemyLevel(
	const FProjectEnemyLevelContext& Context,
	const UProjectEnemyLevelSettings& Settings,
	FRandomStream& RandomStream,
	FProjectEnemyLevelRollResult& OutResult)
{
	const int32 Width = FMath::Max(Context.MaxLevel - Context.MinLevel + 1, 1);
	float TotalWeight = 0.0f;
	for (int32 OffsetIndex = 0; OffsetIndex < Width; ++OffsetIndex)
	{
		TotalWeight += ProjectEnemyLevelLogicPrivate::GetWeightForOffset(Settings, OffsetIndex);
	}

	if (TotalWeight <= 0.0f)
	{
		return false;
	}

	const float Roll = RandomStream.FRand() * TotalWeight;
	float RunningWeight = 0.0f;
	int32 SelectedOffset = Width - 1;
	for (int32 OffsetIndex = 0; OffsetIndex < Width; ++OffsetIndex)
	{
		RunningWeight += ProjectEnemyLevelLogicPrivate::GetWeightForOffset(Settings, OffsetIndex);
		if (Roll <= RunningWeight)
		{
			SelectedOffset = OffsetIndex;
			break;
		}
	}

	OutResult.WorldTier = Context.WorldTier;
	OutResult.MinRolledLevel = Context.MinLevel;
	OutResult.MaxRolledLevel = Context.MaxLevel;
	OutResult.SelectedOffset = SelectedOffset;
	OutResult.AssignedLevel = Context.MinLevel + SelectedOffset;
	OutResult.NormalizedLevel = NormalizeEnemyLevel(OutResult.AssignedLevel, Settings);
	return true;
}

float FProjectEnemyLevelLogic::NormalizeEnemyLevel(const int32 AssignedLevel, const UProjectEnemyLevelSettings& Settings)
{
	const int32 MaxMorphBiasLevel = FMath::Max(Settings.MorphBiasMaxLevel, 2);
	const float NormalizedLevel = static_cast<float>(AssignedLevel - 1) / static_cast<float>(MaxMorphBiasLevel - 1);
	return FMath::Clamp(NormalizedLevel, 0.0f, 1.0f);
}
