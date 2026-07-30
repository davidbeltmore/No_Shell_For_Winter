#include "EFProjectWorldFlowSettings.h"

UEFProjectWorldFlowSettings::UEFProjectWorldFlowSettings()
{
	ManagedDungeonMapName = TEXT("DungeonGeneration");
	DungeonGenerationMap = FSoftObjectPath(TEXT("/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"));
	DoorToLevelBlueprint = FSoftObjectPath(TEXT("/Game/Procedural/DoorToLevel.DoorToLevel_C"));
}

const UEFProjectWorldFlowSettings* UEFProjectWorldFlowSettings::Get()
{
	return GetDefault<UEFProjectWorldFlowSettings>();
}

FName UEFProjectWorldFlowSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}
