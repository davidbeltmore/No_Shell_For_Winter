#include "Survival/ProjectSurvivalNeedsSettings.h"

namespace
{
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName PainName(TEXT("Pain"));
	const FName CurseName(TEXT("Curse"));
	const FName AlcoholName(TEXT("Alcohol"));
}

UProjectSurvivalNeedsSettings::UProjectSurvivalNeedsSettings()
{
	BarsPerNeed = 10;
	PenaltyPerNeedAtZero = 0.25f;
	bEnableAutoDecay = true;
	bClampToRangeByDefault = true;
	DebugNeedsDecayMultiplier = 1.f;

	DefaultNeeds = {
		FProjectSurvivalNeedState(HungerName, 100.f, 100.f, 0.16f, 0.f),
		FProjectSurvivalNeedState(ThirstName, 100.f, 100.f, 0.30f, 0.f),
		FProjectSurvivalNeedState(SleepName, 100.f, 100.f, 0.105f, 0.f),
	};

	DefaultSensations = {
		FProjectSurvivalSensationState(MadnessName, 0.f, 100.f),
		FProjectSurvivalSensationState(PainName, 0.f, 100.f),
		FProjectSurvivalSensationState(CurseName, 0.f, 100.f),
		FProjectSurvivalSensationState(AlcoholName, 0.f, 100.f, -0.25f),
	};

	HiddenHudEntryNames = {
		AlcoholName,
	};

	AffectedSecondaryAttributes = {
		TEXT("MeleeDamage"),
		TEXT("RangedDamage"),
		TEXT("SpellDamage"),
		TEXT("PhysicalDefense"),
		TEXT("SpellDefense"),
		TEXT("CritChance"),
	};
}

const UProjectSurvivalNeedsSettings* UProjectSurvivalNeedsSettings::Get()
{
	return GetDefault<UProjectSurvivalNeedsSettings>();
}

FName UProjectSurvivalNeedsSettings::GetCategoryName() const
{
	return TEXT("Game");
}
