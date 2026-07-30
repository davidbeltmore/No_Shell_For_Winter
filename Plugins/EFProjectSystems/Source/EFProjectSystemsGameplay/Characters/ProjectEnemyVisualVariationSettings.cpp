#include "Characters/ProjectEnemyVisualVariationSettings.h"

#include "GameFramework/Pawn.h"
#include "UObject/SoftObjectPath.h"

namespace ProjectEnemyVisualVariationSettingsPrivate
{
	static TSoftClassPtr<APawn> MakeEnemyClass(const TCHAR* AssetPath)
	{
		return TSoftClassPtr<APawn>(FSoftObjectPath(AssetPath));
	}

	static FProjectEnemyMorphBiasEntry MakeMorphBiasEntry(
		const TCHAR* MorphName,
		const float PositiveWeight,
		const float NegativeWeight,
		const float NearZeroWeight)
	{
		FProjectEnemyMorphBiasEntry Entry;
		Entry.MorphName = FName(MorphName);
		Entry.PositiveWeight = PositiveWeight;
		Entry.NegativeWeight = NegativeWeight;
		Entry.NearZeroWeight = NearZeroWeight;
		return Entry;
	}

}

UProjectEnemyVisualVariationSettings::UProjectEnemyVisualVariationSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("ProjectEnemyVisualVariation");

	TargetEnemyClasses = {
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDummyAmbushEnemyBPFemale.ACFDummyAmbushEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDummyEnemyBPFemale.ACFDummyEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"))
	};

	OptionalMatureMorphTargetEnemyClasses = {
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C")),
		ProjectEnemyVisualVariationSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C"))
	};

	AllowedMorphNames = {
		TEXT("Body Emaciated"),
		TEXT("Body Fitness Details"),
		TEXT("Body Fitness Mass"),
		TEXT("Body Heavy"),
		TEXT("Body Lithe"),
		TEXT("Body Muscular Details"),
		TEXT("Body Muscular Mass"),
		TEXT("Body Older"),
		TEXT("Body Portly"),
		TEXT("Body Stocky"),
		TEXT("Body Thin"),
		TEXT("Body Tone")
	};

	MorphBiasEntries = {
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Muscular Mass"), 28.0f, 0.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Muscular Details"), 22.0f, 0.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Portly"), 22.0f, 0.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Heavy"), 14.0f, 0.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Stocky"), 8.0f, 0.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Tone"), 4.0f, 2.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Fitness Mass"), 2.0f, 10.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Fitness Details"), 0.0f, 12.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Thin"), 0.0f, 26.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Emaciated"), 0.0f, 22.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Lithe"), 0.0f, 18.0f, 1.0f),
		ProjectEnemyVisualVariationSettingsPrivate::MakeMorphBiasEntry(TEXT("Body Older"), 0.0f, 0.0f, 1.0f)
	};

	OptionalMatureGroupOneMorphEntries.Reset();
	OptionalMatureGroupTwoMorphNames.Reset();
	OptionalMatureGroupThreeFixedMorphName = NAME_None;
	OptionalMatureGroupThreeConditionalMorphName = NAME_None;

	SkinBrightnessMin = 0.35f;
	SkinBrightnessMax = 1.70f;
	bApplySkinColorNatively = true;
	EnemySkinColorParameterNames = {
		TEXT("Diffuse Color")
	};
	EnemySkinMaterialHints = {
		TEXT("Genesis9_Body"),
		TEXT("Genesis9_Head"),
		TEXT("Genesis9_Arms"),
		TEXT("Genesis9_Legs"),
		TEXT("Genesis9_Fingernails")
	};

	bEnableOptionalMatureMorphPresentation = false;
	NeutralBaseMorphName = NAME_None;
	ActivePresentationMorphName = NAME_None;
	OptionalMatureMorphTransitionSpeed = 0.25f;
	OptionalMatureVisibilityCheckIntervalSeconds = 0.10f;
	OptionalMatureVisibilityRange = 2200.0f;
	OptionalMatureVisibilityDotThreshold = 0.40f;
}

const UProjectEnemyVisualVariationSettings* UProjectEnemyVisualVariationSettings::Get()
{
	return GetDefault<UProjectEnemyVisualVariationSettings>();
}

FName UProjectEnemyVisualVariationSettings::GetCategoryName() const
{
	return TEXT("Game");
}
