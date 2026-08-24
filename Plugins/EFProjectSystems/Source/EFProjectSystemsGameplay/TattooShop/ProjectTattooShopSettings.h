#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "ProjectTattooShopSettings.generated.h"

class UUserWidget;

/**
 * Project-owned switch and safety limits for the Character Creation tattoo
 * integration.  The legacy Marketplace widgets remain available for rollback,
 * but only one renderer/UI route may run at a time.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Tattoo Shop"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectTattooShopSettings();

	static const UProjectTattooShopSettings* Get();
	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Routing")
	bool bUseSkinnedDecalTattooShop = true;

	/** Optional designer override. Empty means discover a WBP derived from the native class. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Interface")
	TSoftClassPtr<UUserWidget> LibraryWidgetClass;

	/** Optional designer override. Empty means discover a WBP derived from the native class. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Interface")
	TSoftClassPtr<UUserWidget> EditorWidgetClass;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Limits", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxManualTattoos = 32;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Limits", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxTotalTattooLayers = 64;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rendering", meta = (ClampMin = "64", ClampMax = "512"))
	int32 AtlasCellSize = 512;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rendering", meta = (ClampMin = "512", ClampMax = "4096"))
	int32 MaxAtlasSize = 4096;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rendering", meta = (ClampMin = "0.0", ClampMax = "0.25"))
	float PreviewDebounceSeconds = 0.05f;
};

