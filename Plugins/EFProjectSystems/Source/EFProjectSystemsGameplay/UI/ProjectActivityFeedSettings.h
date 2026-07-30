#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UI/ProjectActivityFeedTypes.h"
#include "ProjectActivityFeedSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Activity Feed"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectActivityFeedSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectActivityFeedSettings();

	static const UProjectActivityFeedSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "History", meta = (ClampMin = "10", UIMin = "10"))
	int32 MaxStoredEntries;

	UPROPERTY(EditAnywhere, Config, Category = "History", meta = (ClampMin = "1", UIMin = "1"))
	int32 CompactVisibleEntries;

	UPROPERTY(EditAnywhere, Config, Category = "History", meta = (ClampMin = "1", UIMin = "1"))
	int32 ExpandedVisibleEntries;

	UPROPERTY(EditAnywhere, Config, Category = "History", meta = (ClampMin = "1", UIMin = "1"))
	int32 ExpandedViewportVisibleEntries;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	FVector2D CompactPanelSize;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	FVector2D ExpandedPanelSize;

	UPROPERTY(EditAnywhere, Config, Category = "HUD")
	FVector2D HudMargin;

	UPROPERTY(EditAnywhere, Config, Category = "HUD", meta = (ClampMin = "12.0", UIMin = "12.0"))
	float CompactRowHeight;

	UPROPERTY(EditAnywhere, Config, Category = "HUD", meta = (ClampMin = "18.0", UIMin = "18.0"))
	float ExpandedRowHeight;

	UPROPERTY(EditAnywhere, Config, Category = "HUD", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RowGap;

	UPROPERTY(EditAnywhere, Config, Category = "HUD|Text", meta = (ClampMin = "64.0", UIMin = "64.0"))
	float CompactMaximumTextWidth;

	UPROPERTY(EditAnywhere, Config, Category = "HUD|Text", meta = (ClampMin = "64.0", UIMin = "64.0"))
	float ExpandedMaximumTextWidth;

	UPROPERTY(EditAnywhere, Config, Category = "HUD|Text", meta = (ClampMin = "0.1", ClampMax = "0.9", UIMin = "0.1", UIMax = "0.9"))
	float InlinePrimaryWidthRatio;

	UPROPERTY(EditAnywhere, Config, Category = "HUD|Text", meta = (ClampMin = "1.0", ClampMax = "2.0", UIMin = "1.0", UIMax = "2.0"))
	float CompactLineHeightPercentage;

	UPROPERTY(EditAnywhere, Config, Category = "HUD|Text", meta = (ClampMin = "1.0", ClampMax = "2.0", UIMin = "1.0", UIMax = "2.0"))
	float ExpandedLineHeightPercentage;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 CompactBodyFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 ExpandedBodyFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 CompactBadgeFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 ExpandedBadgeFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 CompactPrimaryFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Fonts", meta = (ClampMin = "6", UIMin = "6"))
	int32 ExpandedPrimaryFontSize;

	UPROPERTY(EditAnywhere, Config, Category = "Filters")
	EProjectActivityFeedDetailMode DetailMode;

	UPROPERTY(EditAnywhere, Config, Category = "Aggregation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealMinToLog;

	UPROPERTY(EditAnywhere, Config, Category = "Aggregation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AggregationWindowHeal;

	UPROPERTY(EditAnywhere, Config, Category = "Aggregation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AggregationWindowXp;

	UPROPERTY(EditAnywhere, Config, Category = "Aggregation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AggregationWindowLoot;

	UPROPERTY(EditAnywhere, Config, Category = "Polling", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LevelPollInterval;

	UPROPERTY(EditAnywhere, Config, Category = "Polling", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float InventoryPollInterval;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SightRange;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "-1.0", UIMax = "1.0"))
	float SightDotThreshold;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LostSightResetSeconds;

	UPROPERTY(EditAnywhere, Config, Category = "Enemies", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BarkCooldownSeconds;
};
