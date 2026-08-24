#include "TattooShop/ProjectTattooShopSettings.h"

UProjectTattooShopSettings::UProjectTattooShopSettings() = default;

const UProjectTattooShopSettings* UProjectTattooShopSettings::Get()
{
	return GetDefault<UProjectTattooShopSettings>();
}

FName UProjectTattooShopSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}

