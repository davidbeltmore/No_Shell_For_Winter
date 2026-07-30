#include "EFLevelFlowSettings.h"

#include "UObject/SoftObjectPath.h"

UEFLevelFlowSettings::UEFLevelFlowSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("EFLevelFlow");
	DelayedSpawnMapNames = {
		TEXT("DungeonGeneration")
	};

	LoadingScreenWidgetClass = TSoftClassPtr<UUserWidget>(
		FSoftObjectPath(TEXT("/AscentCombatFramework/UITools/Widgets/ANS_LoadingScreen_WB.ANS_LoadingScreen_WB_C")));
}

const UEFLevelFlowSettings* UEFLevelFlowSettings::Get()
{
	return GetDefault<UEFLevelFlowSettings>();
}

FName UEFLevelFlowSettings::GetCategoryName() const
{
	return TEXT("Game");
}
