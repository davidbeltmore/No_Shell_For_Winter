#include "EFBodyPresetCatalogCache.h"
#include "EFBodyPresetCatalog.h"

UEFBodyPresetCatalog* UEFBodyPresetCatalogCache::Resolve(const TSoftObjectPtr<UEFBodyPresetCatalog>& Reference)
{
	if (Reference.IsNull()) { Catalog=nullptr; return nullptr; }
	if (!IsValid(Catalog) || FSoftObjectPath(Catalog)!=Reference.ToSoftObjectPath())
		Catalog=Reference.LoadSynchronous();
	return Catalog;
}

void UEFBodyPresetCatalogCache::Deinitialize()
{
	Catalog=nullptr;
	Super::Deinitialize();
}
