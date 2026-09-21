#include "TattooShop/ProjectTattooShopSettings.h"

UProjectTattooShopSettings::UProjectTattooShopSettings()
{
	RagShirtSourceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/DazToUnreal/RagShirt/Materials/SYDFR9RagShirtG9/SYDFR9RagShirtG9_Shirt.SYDFR9RagShirtG9_Shirt")));
	RagShirtTattooOccluderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/EFProjectSystems/TattooShop/MI_RagShirt_TattooOccluder.MI_RagShirt_TattooOccluder")));
}

const UProjectTattooShopSettings* UProjectTattooShopSettings::Get()
{
	return GetDefault<UProjectTattooShopSettings>();
}

FName UProjectTattooShopSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}
