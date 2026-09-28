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
	float DayLengthSeconds = 1800.0f;

	// Legacy need rates were authored against a 600-second day. Convert once,
	// independently of global time dilation, to preserve calendar-hour consumption.
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Day Cycle")
	bool bPreserveCalendarNeedRates = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Time Control")
	TArray<FSoftObjectPath> FastForwardMaps = {
		FSoftObjectPath(TEXT("/Game/_Game/Hub/HUB.HUB")),
		FSoftObjectPath(TEXT("/Game/_Game/Locations/StorySelection.StorySelection")) };

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Time Control", meta = (ClampMin = "100"))
	float ThreatRadius = 3000.f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Time Control", meta = (ClampMin = "0", ClampMax = "1"))
	float CriticalNeedFraction = 0.1f;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Day Cycle", meta = (ClampMin = "1"))
	int32 InitialDayNumber = 1;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "HUD")
	bool bShowDayCycleHud = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "HUD", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float HudRefreshIntervalSeconds = 0.1f;
};
