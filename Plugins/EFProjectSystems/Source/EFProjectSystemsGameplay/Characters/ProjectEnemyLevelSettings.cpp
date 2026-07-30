#include "Characters/ProjectEnemyLevelSettings.h"

#include "GameFramework/Pawn.h"
#include "UObject/SoftObjectPath.h"

namespace ProjectEnemyLevelSettingsPrivate
{
	static TSoftClassPtr<APawn> MakeEnemyClass(const TCHAR* AssetPath)
	{
		return TSoftClassPtr<APawn>(FSoftObjectPath(AssetPath));
	}

	static FProjectEnemyMapLevelRule MakeMapRule(const TCHAR* MapPattern, const int32 WorldTier)
	{
		FProjectEnemyMapLevelRule Rule;
		Rule.MapNamePattern = MapPattern;
		Rule.WorldTier = WorldTier;
		return Rule;
	}
}

UProjectEnemyLevelSettings::UProjectEnemyLevelSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("ProjectEnemyLevel");

	TargetEnemyBaseClasses = {
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDummyAmbushEnemyBPFemale.ACFDummyAmbushEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFDummyEnemyBPFemale.ACFDummyEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C")),
		ProjectEnemyLevelSettingsPrivate::MakeEnemyClass(TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"))
	};

	MapLevelRules = {
		ProjectEnemyLevelSettingsPrivate::MakeMapRule(TEXT("DungeonGeneration"), 1)
	};

	LevelOffsetWeights = { 5, 4, 3, 2, 1 };
	PreferredTargetPointSockets = {
		TEXT("head"),
		TEXT("Head"),
		TEXT("neck_01"),
		TEXT("spine_03")
	};
	bSyncAssignedLevelToAscentLevelingComponent = false;
	bReinitializeAscentStatisticsOnLevelSync = false;
}

const UProjectEnemyLevelSettings* UProjectEnemyLevelSettings::Get()
{
	return GetDefault<UProjectEnemyLevelSettings>();
}

FName UProjectEnemyLevelSettings::GetCategoryName() const
{
	return TEXT("Game");
}
