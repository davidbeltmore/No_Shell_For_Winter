#include "UI/ProjectActivityFeedSettings.h"

UProjectActivityFeedSettings::UProjectActivityFeedSettings()
{
	MaxStoredEntries = 80;
	CompactVisibleEntries = 6;
	ExpandedVisibleEntries = 20;
	ExpandedViewportVisibleEntries = 9;
	CompactPanelSize = FVector2D(560.f, 260.f);
	ExpandedPanelSize = FVector2D(620.f, 470.f);
	HudMargin = FVector2D(16.f, 240.f);
	CompactRowHeight = 22.f;
	ExpandedRowHeight = 32.f;
	RowGap = 3.f;
	CompactMaximumTextWidth = 390.f;
	ExpandedMaximumTextWidth = 450.f;
	InlinePrimaryWidthRatio = 0.30f;
	CompactLineHeightPercentage = 1.08f;
	ExpandedLineHeightPercentage = 1.10f;
	CompactBodyFontSize = 10;
	ExpandedBodyFontSize = 11;
	CompactBadgeFontSize = 9;
	ExpandedBadgeFontSize = 10;
	CompactPrimaryFontSize = 8;
	ExpandedPrimaryFontSize = 9;
	DetailMode = EProjectActivityFeedDetailMode::Minimal;
	HealMinToLog = 5.f;
	AggregationWindowHeal = 0.75f;
	AggregationWindowXp = 1.f;
	AggregationWindowLoot = 1.f;
	LevelPollInterval = 0.25f;
	InventoryPollInterval = 0.35f;
	SightRange = 2200.f;
	SightDotThreshold = 0.40f;
	LostSightResetSeconds = 2.f;
	BarkCooldownSeconds = 8.f;
}

const UProjectActivityFeedSettings* UProjectActivityFeedSettings::Get()
{
	return GetDefault<UProjectActivityFeedSettings>();
}

FName UProjectActivityFeedSettings::GetCategoryName() const
{
	return TEXT("Game");
}
