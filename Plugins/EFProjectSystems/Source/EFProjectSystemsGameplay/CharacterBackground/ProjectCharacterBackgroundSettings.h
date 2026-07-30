#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectCharacterBackgroundSettings.generated.h"

class UDataTable;
class UProjectCharacterBackgroundCreationWidget;
class UTexture2D;

USTRUCT(BlueprintType)
struct FProjectCharacterBackgroundWidgetAdjustment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Transform")
	FVector2D Translation = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Transform", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float UniformScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Transform")
	FVector2D Scale = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Transform")
	FVector2D Shear = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Transform")
	FVector2D Pivot = FVector2D(0.5f, 0.5f);

	UPROPERTY(EditAnywhere, Category = "Transform")
	float RotationDegrees = 0.0f;
};

USTRUCT(BlueprintType)
struct FProjectCharacterBackgroundUILayoutTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Frame")
	FMargin FrameOffsets = FMargin(20.0f, 16.0f, 20.0f, 16.0f);

	UPROPERTY(EditAnywhere, Category = "Frame", meta = (ClampMin = "0"))
	float FramePadding = 14.0f;

	UPROPERTY(EditAnywhere, Category = "Frame", meta = (ClampMin = "0", ClampMax = "32"))
	float FrameCornerRadius = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Frame", meta = (ClampMin = "0"))
	float FrameOutlineWidth = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "160"))
	float SidePanelWidth = 340.0f;

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "0"))
	float ColumnGap = 12.0f;

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "0"))
	float HeaderBottomPadding = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "0"))
	float BodyBottomPadding = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Panels")
	FMargin PanelPadding = FMargin(12.0f);

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "0", ClampMax = "32"))
	float PanelCornerRadius = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Panels", meta = (ClampMin = "0"))
	float PanelOutlineWidth = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "80"))
	float OptionEntryWidth = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "32"))
	float OptionEntryHeight = 72.0f;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "32"))
	float ProfessionEntryHeight = 124.0f;

	UPROPERTY(EditAnywhere, Category = "Options")
	FMargin OptionEntryPadding = FMargin(12.0f, 7.0f);

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "0"))
	float OptionEntryGap = 7.0f;

	UPROPERTY(EditAnywhere, Category = "Effects", meta = (ClampMin = "80"))
	float EffectEntryWidth = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Effects", meta = (ClampMin = "24"))
	float EffectEntryHeight = 34.0f;

	UPROPERTY(EditAnywhere, Category = "Effects")
	FMargin EffectEntryPadding = FMargin(8.0f, 5.0f);

	UPROPERTY(EditAnywhere, Category = "Effects", meta = (ClampMin = "0"))
	float EffectEntryGap = 7.0f;

	UPROPERTY(EditAnywhere, Category = "Preview")
	FMargin PreviewContentPadding = FMargin(14.0f);

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "16"))
	float AttributeIconSize = 38.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0.10", UIMin = "0.10"))
	float PreviewIconSizeMultiplier = 1.875f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	float AttributeIconGap = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	float PreviewTitleGap = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "32"))
	float PreviewImageWidth = 440.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "32"))
	float PreviewImageHeight = 270.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0.10", UIMin = "0.10"))
	float PreviewImageSizeMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	float PreviewImageTopPadding = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "0"))
	float PreviewImageBottomPadding = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Description", meta = (ClampMin = "0"))
	float DescriptionPanelHeight = 250.0f;

	UPROPERTY(EditAnywhere, Category = "Description")
	FMargin DescriptionPadding = FMargin(12.0f, 10.0f);

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 TitleFontSize = 31;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 SectionTitleFontSize = 18;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 BodyFontSize = 15;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 ButtonFontSize = 15;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 OptionTitleFontSize = 15;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 OptionSubtitleFontSize = 13;

	UPROPERTY(EditAnywhere, Category = "Text", meta = (ClampMin = "8"))
	int32 EffectFontSize = 13;

	UPROPERTY(EditAnywhere, Category = "Buttons", meta = (ClampMin = "40"))
	float ButtonWidth = 116.0f;

	UPROPERTY(EditAnywhere, Category = "Buttons", meta = (ClampMin = "24"))
	float ButtonHeight = 38.0f;

	UPROPERTY(EditAnywhere, Category = "Buttons")
	FMargin ButtonPadding = FMargin(14.0f, 6.0f);

	UPROPERTY(EditAnywhere, Category = "Buttons", meta = (ClampMin = "0", ClampMax = "32"))
	float ButtonCornerRadius = 5.0f;
};

USTRUCT(BlueprintType)
struct FProjectCharacterBackgroundUIAdjustments
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Main")
	FProjectCharacterBackgroundWidgetAdjustment Frame;

	UPROPERTY(EditAnywhere, Category = "Main")
	FProjectCharacterBackgroundWidgetAdjustment Title;

	UPROPERTY(EditAnywhere, Category = "Main")
	FProjectCharacterBackgroundWidgetAdjustment StepLabel;

	UPROPERTY(EditAnywhere, Category = "Panels")
	FProjectCharacterBackgroundWidgetAdjustment OptionPanel;

	UPROPERTY(EditAnywhere, Category = "Panels")
	FProjectCharacterBackgroundWidgetAdjustment PreviewPanel;

	UPROPERTY(EditAnywhere, Category = "Panels")
	FProjectCharacterBackgroundWidgetAdjustment EffectPanel;

	UPROPERTY(EditAnywhere, Category = "Panels")
	FProjectCharacterBackgroundWidgetAdjustment DescriptionPanel;

	UPROPERTY(EditAnywhere, Category = "Description")
	FProjectCharacterBackgroundWidgetAdjustment DescriptionText;

	UPROPERTY(EditAnywhere, Category = "Preview")
	FProjectCharacterBackgroundWidgetAdjustment PreviewImage;

	UPROPERTY(EditAnywhere, Category = "Preview")
	FProjectCharacterBackgroundWidgetAdjustment PreviewWatermark;

	UPROPERTY(EditAnywhere, Category = "Preview")
	FProjectCharacterBackgroundWidgetAdjustment PreviewIcons;

	UPROPERTY(EditAnywhere, Category = "Footer")
	FProjectCharacterBackgroundWidgetAdjustment Footer;

	UPROPERTY(EditAnywhere, Category = "Footer")
	FProjectCharacterBackgroundWidgetAdjustment BackButton;

	UPROPERTY(EditAnywhere, Category = "Footer")
	FProjectCharacterBackgroundWidgetAdjustment PrimaryButton;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Character Background"))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProjectCharacterBackgroundSettings();

	static const UProjectCharacterBackgroundSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Flow")
	TArray<FString> StorySelectionMapNames;

	UPROPERTY(EditAnywhere, Config, Category = "Flow", meta = (ClampMin = "1"))
	int32 MaxAutoOpenAttempts = 120;

	UPROPERTY(EditAnywhere, Config, Category = "Flow")
	bool bAlwaysLaunchPhysicalCreatorAfterStory = true;

	UPROPERTY(EditAnywhere, Config, Category = "Data")
	TSoftObjectPtr<UDataTable> BackstoryDataTable;

	UPROPERTY(EditAnywhere, Config, Category = "Data")
	TSoftObjectPtr<UDataTable> ProfessionDataTable;

	UPROPERTY(EditAnywhere, Config, Category = "UI")
	TSoftClassPtr<UProjectCharacterBackgroundCreationWidget> CreationWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (ClampMin = "0"))
	int32 CreationWidgetZOrder = 995;

	UPROPERTY(EditAnywhere, Config, Category = "UI|Layout")
	FProjectCharacterBackgroundUILayoutTuning UILayout;

	UPROPERTY(EditAnywhere, Config, Category = "UI|Adjustments")
	FProjectCharacterBackgroundUIAdjustments UIAdjustments;

	UPROPERTY(EditAnywhere, Config, Category = "UI|Images")
	TSoftObjectPtr<UTexture2D> PreviewImageTexture;

	UPROPERTY(EditAnywhere, Config, Category = "UI|Images")
	FString PreviewImagePngPath = TEXT("Content/_Game/Images/preview.png");

	UPROPERTY(EditAnywhere, Config, Category = "Save")
	FString SaveSlotName = TEXT("ProjectCharacterBackground");

	UPROPERTY(EditAnywhere, Config, Category = "Save", meta = (ClampMin = "0"))
	int32 SaveUserIndex = 0;
};
