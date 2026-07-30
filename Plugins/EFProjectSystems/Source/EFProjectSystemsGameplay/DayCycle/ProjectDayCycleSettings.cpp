#include "DayCycle/ProjectDayCycleSettings.h"

UProjectDayCycleSettings::UProjectDayCycleSettings() = default;

const UProjectDayCycleSettings* UProjectDayCycleSettings::Get()
{
	return GetDefault<UProjectDayCycleSettings>();
}

FName UProjectDayCycleSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}
