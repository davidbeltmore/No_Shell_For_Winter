#include "Calysto/ProjectCalystoPatrolSettings.h"

UProjectCalystoPatrolSettings::UProjectCalystoPatrolSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("ProjectCalystoPatrol");
}

const UProjectCalystoPatrolSettings* UProjectCalystoPatrolSettings::Get()
{
	return GetDefault<UProjectCalystoPatrolSettings>();
}

FName UProjectCalystoPatrolSettings::GetCategoryName() const
{
	return TEXT("Game");
}
