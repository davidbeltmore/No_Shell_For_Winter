#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectEnemyVisualVariationSettings.generated.h"

class APawn;

UENUM()
enum class EProjectEnemyMorphDistributionMode : uint8
{
	Uniform,
	Weighted
};

USTRUCT()
struct FProjectEnemyMorphBiasEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Morph")
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PositiveWeight = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NegativeWeight = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NearZeroWeight = 1.0f;
};

USTRUCT()
struct FProjectEnemyLevelBiasedMorphEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Morph")
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "1", UIMin = "1"))
	int32 PreferredMinLevel = 1;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "0", UIMin = "0"))
	int32 PreferredMaxLevel = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Morph", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PreferredRangeWeightMultiplier = 1.25f;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Enemy Visual Variation"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyVisualVariationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectEnemyVisualVariationSettings();

	static const UProjectEnemyVisualVariationSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Targets", meta = (AllowedClasses = "/Script/Engine.Pawn"))
	TArray<TSoftClassPtr<APawn>> TargetEnemyClasses;

	/**
	 * Subset eligible for optional mature cosmetic morphs. Runtime application
	 * remains fail-closed unless the central content policy allows Intimacy.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Targets", meta = (AllowedClasses = "/Script/Engine.Pawn"))
	TArray<TSoftClassPtr<APawn>> OptionalMatureMorphTargetEnemyClasses;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs")
	TArray<FName> AllowedMorphNames;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs")
	EProjectEnemyMorphDistributionMode DistributionMode = EProjectEnemyMorphDistributionMode::Uniform;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs", meta = (TitleProperty = "MorphName"))
	TArray<FProjectEnemyMorphBiasEntry> MorphBiasEntries;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float PositiveBucketChance = 0.70f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float NegativeBucketChance = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float NearZeroBucketChance = 0.05f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float PositiveExtremeExponent = 0.35f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float NegativeExtremeExponent = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PositiveMinAbsValue = 0.55f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float NegativeMinAbsValue = 0.45f;

	UPROPERTY(EditAnywhere, Config, Category = "Distribution", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float NearZeroMaxAbsValue = 0.12f;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float MorphMinValue = -1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Morphs", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float MorphMaxValue = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group1", meta = (TitleProperty = "MorphName"))
	TArray<FProjectEnemyLevelBiasedMorphEntry> OptionalMatureGroupOneMorphEntries;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2")
	TArray<FName> OptionalMatureGroupTwoMorphNames;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupTwoHighValueChance = 0.70f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupTwoHighValueMin = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupTwoHighValueMax = 1.00f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupTwoLowValueMin = 0.00f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group2", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupTwoLowValueMax = 0.49f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group3")
	FName OptionalMatureGroupThreeFixedMorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group3")
	FName OptionalMatureGroupThreeConditionalMorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group3", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OptionalMatureGroupThreeConditionalMorphChance = 0.50f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group3", meta = (ClampMin = "1", UIMin = "1"))
	int32 OptionalMatureGroupThreeStartLevel = 25;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Group3", meta = (ClampMin = "1", UIMin = "1"))
	int32 OptionalMatureGroupThreeMaxLevel = 125;

	UPROPERTY(EditAnywhere, Config, Category = "Skin", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SkinBrightnessMin = 0.35f;

	UPROPERTY(EditAnywhere, Config, Category = "Skin", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SkinBrightnessMax = 1.70f;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	bool bApplySkinColorNatively = true;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FName> EnemySkinColorParameterNames;

	UPROPERTY(EditAnywhere, Config, Category = "Skin")
	TArray<FString> EnemySkinMaterialHints;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime", meta = (ClampMin = "0", UIMin = "0"))
	int32 InitializationRetryCount = 3;

	/**
	 * Capability switch only. The feature is still denied until the player has
	 * Charisma level ten, and Streamer Safe always suppresses it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime")
	bool bEnableOptionalMatureMorphPresentation = false;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime")
	FName NeutralBaseMorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime")
	FName ActivePresentationMorphName = NAME_None;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float OptionalMatureMorphTransitionSpeed = 0.25f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float OptionalMatureVisibilityCheckIntervalSeconds = 0.10f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float OptionalMatureVisibilityRange = 2200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Optional Mature Morphs|Runtime", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float OptionalMatureVisibilityDotThreshold = 0.40f;
};
