#include "EFProjectSurvivalSettings.h"

UEFProjectSurvivalSettings::UEFProjectSurvivalSettings()
{
	ConsumableRegistry = FSoftObjectPath(TEXT("/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry.DA_FoodConsumableRegistry"));
	bBootstrapNeedsHud = true;
	bBootstrapStatusHud = true;
}

const UEFProjectSurvivalSettings* UEFProjectSurvivalSettings::Get()
{
	return GetDefault<UEFProjectSurvivalSettings>();
}

FName UEFProjectSurvivalSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}
