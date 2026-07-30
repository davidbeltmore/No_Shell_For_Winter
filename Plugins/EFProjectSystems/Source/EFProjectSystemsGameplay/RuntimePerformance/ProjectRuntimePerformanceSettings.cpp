#include "RuntimePerformance/ProjectRuntimePerformanceSettings.h"

UProjectRuntimePerformanceSettings::UProjectRuntimePerformanceSettings()
{
	DungeonCombatMap = FSoftObjectPath(TEXT("/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"));
}

const UProjectRuntimePerformanceSettings* UProjectRuntimePerformanceSettings::Get()
{
	return GetDefault<UProjectRuntimePerformanceSettings>();
}

FName UProjectRuntimePerformanceSettings::GetCategoryName() const
{
	return TEXT("Project");
}
