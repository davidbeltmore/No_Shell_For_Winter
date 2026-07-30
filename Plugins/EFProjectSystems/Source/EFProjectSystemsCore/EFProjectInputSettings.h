#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "EFProjectInputSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Project Input"))
class EFPROJECTSYSTEMSCORE_API UEFProjectInputSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFProjectInputSettings();

	static const UEFProjectInputSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleWalkKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleCrawlKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleInteractionMenuKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleNeedsHudKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleActivityFeedKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleGameplayDebugMenuKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey ToggleGameplayFreeCameraKey;

	UPROPERTY(EditAnywhere, Config, Category = "Bindings")
	FKey SurrenderKey;
};
