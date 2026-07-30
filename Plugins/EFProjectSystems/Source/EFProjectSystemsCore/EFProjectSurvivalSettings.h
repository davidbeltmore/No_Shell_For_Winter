#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFProjectSurvivalSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Project Survival"))
class EFPROJECTSYSTEMSCORE_API UEFProjectSurvivalSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFProjectSurvivalSettings();

	static const UEFProjectSurvivalSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Assets")
	FSoftObjectPath ConsumableRegistry;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	bool bBootstrapNeedsHud;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	bool bBootstrapStatusHud;
};
