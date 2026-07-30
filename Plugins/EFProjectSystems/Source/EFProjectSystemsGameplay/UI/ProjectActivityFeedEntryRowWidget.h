#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "EFProjectUIPalette.h"
#include "UI/ProjectActivityFeedTypes.h"
#include "ProjectActivityFeedEntryRowWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UWidgetTree;
class UObject;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectActivityFeedRowDisplayData
{
	GENERATED_BODY()

	FProjectActivityFeedRowDisplayData()
		: Channel(EProjectActivityFeedChannel::System)
		, RenderStyle(EProjectActivityFeedRenderStyle::Standard)
		, BadgeLabel()
		, PrimaryText()
		, SecondaryText()
		, Message()
		, AccentTint(EFProjectUIPalette::Accent())
		, BadgeFillTint(EFProjectUIPalette::BadgeFill(0.96f))
		, BadgeTextTint(EFProjectUIPalette::BadgeText())
		, RowWidth(520.0f)
		, RowHeight(60.0f)
		, TextWrapWidth(390.0f)
		, InlinePrimaryWidthRatio(0.38f)
		, LineHeightPercentage(1.18f)
		, BodyFontSize(14)
		, BadgeFontSize(12)
		, PrimaryFontSize(18)
		, bExpanded(false)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	EProjectActivityFeedChannel Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	EProjectActivityFeedRenderStyle RenderStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FString BadgeLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FText PrimaryText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FText SecondaryText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor BadgeFillTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor BadgeTextTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	float RowWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	float RowHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	float TextWrapWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	float InlinePrimaryWidthRatio;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	float LineHeightPercentage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	int32 BodyFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	int32 BadgeFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	int32 PrimaryFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	bool bExpanded;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectActivityFeedEntryRowWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	UProjectActivityFeedEntryRowWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

	UFUNCTION(BlueprintCallable, Category = "Activity")
	void ApplyDisplayData(const FProjectActivityFeedRowDisplayData& InDisplayData);

	UFUNCTION(BlueprintImplementableEvent, Category = "Activity")
	void OnChronicleRowDataApplied(const FProjectActivityFeedRowDisplayData& DisplayData);

protected:
	void BuildWidgetTree();
	bool BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree);
	void InitializeVisualTree();
	void RefreshVisuals();
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const;
	UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const;
	UObject* ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const;
	FSlateFontInfo MakeTitleFont(int32 Size, int32 LetterSpacing = 0) const;
	FSlateFontInfo MakeBodyFont(int32 Size, int32 LetterSpacing = 0, bool bItalic = false) const;
	FSlateFontInfo MakeBodyEmphasisFont(int32 Size, int32 LetterSpacing = 0) const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> TitleFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Fonts")
	TSoftObjectPtr<UObject> BodyItalicFontAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> RowFrameTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> BadgeTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style|Textures")
	TSoftObjectPtr<UTexture2D> GlyphTexture;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> RootOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FrameImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ContentBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ContentBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> BadgeSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> BadgeOverlay;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BadgeBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> BadgeImage;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> BadgeScaleBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BadgeText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DividerSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> DividerBorder;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> InlineTextBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PrimaryTextBlock;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SecondaryTextBlock;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> GlyphSizeBox;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> GlyphImage;

private:
	FProjectActivityFeedRowDisplayData CurrentDisplayData;
	bool bVisualTreeInitialized = false;
	bool bUsingNativeFallbackTree = false;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleNormalStandardRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleNormalGainRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleNormalDialogueQuoteRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleExpandedStandardRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleExpandedGainRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectChronicleExpandedDialogueQuoteRowWidget : public UProjectActivityFeedEntryRowWidget
{
	GENERATED_BODY()

protected:
	virtual FProjectActivityFeedRowDisplayData MakeDesignerPreviewData() const override;
};
