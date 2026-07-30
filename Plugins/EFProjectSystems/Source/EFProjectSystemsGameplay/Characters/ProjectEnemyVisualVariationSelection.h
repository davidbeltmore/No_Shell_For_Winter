#pragma once

#include "CoreMinimal.h"
#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "EFCharacterCreationTypes.h"

enum class EProjectEnemyMorphBucket : uint8
{
	Positive,
	Negative,
	NearZero
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyMorphSelectionContext
{
	bool bHasNormalizedEnemyLevel = false;
	float NormalizedEnemyLevel = 0.5f;
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyMorphRollResult
{
	bool bIsValid = false;
	EProjectEnemyMorphBucket Bucket = EProjectEnemyMorphBucket::Positive;
	int32 SelectedEntryIndex = INDEX_NONE;
	FName MorphName = NAME_None;
	float MorphValue = 0.0f;
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyNamedMorphRollResult
{
	bool bIsValid = false;
	FName MorphName = NAME_None;
	float MorphValue = 0.0f;
};

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectEnemyVisualVariationSelection
{
public:
	static bool RollMorphVariation(
		const TArray<FMorphSliderEntry>& AllowedEntries,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext,
		FProjectEnemyMorphRollResult& OutResult);

	static bool RollMorphVariation(
		const TArray<FMorphSliderEntry>& AllowedEntries,
		const UProjectEnemyVisualVariationSettings& Settings,
		FProjectEnemyMorphRollResult& OutResult);

	static bool RollMorphVariation(
		const TArray<FMorphSliderEntry>& AllowedEntries,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FProjectEnemyMorphSelectionContext& SelectionContext,
		FRandomStream& RandomStream,
		FProjectEnemyMorphRollResult& OutResult);

	static bool RollMorphVariation(
		const TArray<FMorphSliderEntry>& AllowedEntries,
		const UProjectEnemyVisualVariationSettings& Settings,
		FRandomStream& RandomStream,
		FProjectEnemyMorphRollResult& OutResult);

	static bool RollLevelBiasedPositiveMorph(
		const TArray<FName>& AvailableMorphNames,
		const TArray<FProjectEnemyLevelBiasedMorphEntry>& CandidateSettings,
		int32 EnemyLevel,
		FProjectEnemyNamedMorphRollResult& OutResult);

	static bool RollLevelBiasedPositiveMorph(
		const TArray<FName>& AvailableMorphNames,
		const TArray<FProjectEnemyLevelBiasedMorphEntry>& CandidateSettings,
		int32 EnemyLevel,
		FRandomStream& RandomStream,
		FProjectEnemyNamedMorphRollResult& OutResult);

	static bool RollBandBiasedPositiveMorph(
		const TArray<FName>& AvailableMorphNames,
		float HighBandChance,
		float HighBandMinValue,
		float HighBandMaxValue,
		float LowBandMinValue,
		float LowBandMaxValue,
		FProjectEnemyNamedMorphRollResult& OutResult);

	static bool RollBandBiasedPositiveMorph(
		const TArray<FName>& AvailableMorphNames,
		float HighBandChance,
		float HighBandMinValue,
		float HighBandMaxValue,
		float LowBandMinValue,
		float LowBandMaxValue,
		FRandomStream& RandomStream,
		FProjectEnemyNamedMorphRollResult& OutResult);

	static float EvaluateLinearLevelAlpha(int32 EnemyLevel, int32 StartLevel, int32 MaxLevel);
};
