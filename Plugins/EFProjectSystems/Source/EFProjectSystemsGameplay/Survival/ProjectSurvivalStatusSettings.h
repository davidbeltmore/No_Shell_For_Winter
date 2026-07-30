#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Survival/ProjectSurvivalStatusTypes.h"
#include "ProjectSurvivalStatusSettings.generated.h"

class UDataTable;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Survival Status"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalStatusSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectSurvivalStatusSettings();

	static const UProjectSurvivalStatusSettings* Get();

	virtual FName GetCategoryName() const override;

	UFUNCTION(BlueprintCallable, Category = "Status")
	TArray<FProjectSurvivalStatusDefinition> BuildResolvedStatusDefinitions() const;

	static TArray<FProjectSurvivalStatusDefinition> BuildDefinitionsFromTable(
		const UDataTable* DefinitionsTable,
		const TArray<FProjectSurvivalStatusDefinition>& FallbackDefinitions);

	UPROPERTY(EditAnywhere, Config, Category = "Status")
	TSoftObjectPtr<UDataTable> StatusDefinitionsTable;

	UPROPERTY(EditAnywhere, Config, Category = "Status")
	TArray<FProjectSurvivalStatusDefinition> StatusDefinitions;

	UPROPERTY(EditAnywhere, Config, Category = "Exhausted", meta = (ClampMin = "0.1"))
	float ExhaustedBlackoutSeconds;

	UPROPERTY(EditAnywhere, Config, Category = "Exhausted", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExhaustedSleepRestorePercent;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	FVector2D StatusHudOffset;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	FVector2D StatusIconSize;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	float StatusIconSpacing;

	UPROPERTY(EditAnywhere, Config, Category = "HUD", meta = (ClampMin = "1", UIMin = "1"))
	int32 StatusIconsPerRow;

	UPROPERTY(EditAnywhere, Config, Category = "HUD", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxVisibleStatuses;
};
