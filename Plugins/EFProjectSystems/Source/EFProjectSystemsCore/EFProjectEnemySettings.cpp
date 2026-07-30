#include "EFProjectEnemySettings.h"

namespace EFProjectEnemySettingsPrivate
{
	static FSoftClassPath CharacterClass(const TCHAR* AssetPath)
	{
		return FSoftClassPath(AssetPath);
	}
}

UEFProjectEnemySettings::UEFProjectEnemySettings()
{
	using namespace EFProjectEnemySettingsPrivate;

	RuntimeEnemyClasses = {
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFDummyAmbushEnemyBPFemale.ACFDummyAmbushEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFDummyEnemyBPFemale.ACFDummyEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C"))
	};

	MaleCharacterClasses = RuntimeEnemyClasses.FilterByPredicate([](const FSoftClassPath& Path)
	{
		return Path.ToString().Contains(TEXT("/Male/"));
	});
	MaleCharacterClasses.Append({
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C"))
	});

	FemaleCharacterClasses = RuntimeEnemyClasses.FilterByPredicate([](const FSoftClassPath& Path)
	{
		return Path.ToString().Contains(TEXT("/Female/"));
	});
	FemaleCharacterClasses.Append({
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C")),
		CharacterClass(TEXT("/Game/_Game/Characters/Female/ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C"))
	});
	bEnableExtendedTargetStatsWhenNeedsHudVisible = true;
}

const UEFProjectEnemySettings* UEFProjectEnemySettings::Get()
{
	return GetDefault<UEFProjectEnemySettings>();
}

FName UEFProjectEnemySettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}
