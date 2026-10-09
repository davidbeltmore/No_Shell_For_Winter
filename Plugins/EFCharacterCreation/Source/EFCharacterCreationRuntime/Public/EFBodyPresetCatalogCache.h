#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "EFBodyPresetCatalogCache.generated.h"

class UEFBodyPresetCatalog;

/** Runtime catalog ownership must live outside the cooked disregard-for-GC CDOs. */
UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFBodyPresetCatalogCache : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	UEFBodyPresetCatalog* Resolve(const TSoftObjectPtr<UEFBodyPresetCatalog>& Reference);
	virtual void Deinitialize() override;
private:
	UPROPERTY(Transient)
	TObjectPtr<UEFBodyPresetCatalog> Catalog;
};
