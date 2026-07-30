#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectDayCycleSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Project Day Cycle"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDayCycleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectDayCycleSettings();

	static const UProjectDayCycleSettings* Get();
	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Day Cycle")
	bool bEnableDayCycle = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Day Cycle", meta = (ClampMin = "1.0", UIMin = "60.0"))
	float DayLengthSeconds = 600.0f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Day Cycle", meta = (ClampMin = "1"))
	int32 InitialDayNumber = 1;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "HUD")
	bool bShowDayCycleHud = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "HUD", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float HudRefreshIntervalSeconds = 0.1f;
};
