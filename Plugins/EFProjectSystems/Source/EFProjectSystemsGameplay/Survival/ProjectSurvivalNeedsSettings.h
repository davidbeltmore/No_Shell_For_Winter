#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Survival/ProjectSurvivalNeedsTypes.h"
#include "ProjectSurvivalNeedsSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Survival Needs"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalNeedsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectSurvivalNeedsSettings();

	static const UProjectSurvivalNeedsSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Needs")
	int32 BarsPerNeed;

	UPROPERTY(EditAnywhere, Config, Category = "Needs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PenaltyPerNeedAtZero;

	UPROPERTY(EditAnywhere, Config, Category = "Needs")
	bool bEnableAutoDecay;

	UPROPERTY(EditAnywhere, Config, Category = "Needs")
	bool bClampToRangeByDefault;

	UPROPERTY(EditAnywhere, Config, Category = "Debug", meta = (ClampMin = "0.0"))
	float DebugNeedsDecayMultiplier;

	UPROPERTY(EditAnywhere, Config, Category = "Needs")
	TArray<FProjectSurvivalNeedState> DefaultNeeds;

	UPROPERTY(EditAnywhere, Config, Category = "Sensations")
	TArray<FProjectSurvivalSensationState> DefaultSensations;

	UPROPERTY(EditAnywhere, Config, Category = "Attributes")
	TArray<FName> AffectedSecondaryAttributes;
};
