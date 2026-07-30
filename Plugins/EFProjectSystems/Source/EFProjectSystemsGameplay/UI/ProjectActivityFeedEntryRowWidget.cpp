#include "UI/ProjectActivityFeedEntryRowWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace ProjectActivityFeedEntryRowWidgetPrivate
{
	constexpr float DefaultRowWidth = 520.0f;
	constexpr float DefaultRowHeight = 32.0f;
	constexpr float ExpandedRowHeight = 34.0f;
	constexpr float CompactBadgeWidth = 72.0f;
	constexpr float ExpandedBadgeWidth = 72.0f;
	constexpr float DividerWidth = 1.0f;
	constexpr float CompactGlyphSize = 12.0f;
	constexpr float ExpandedGlyphSize = 12.0f;

	FLinearColor RowFillTint()
	{
		return EFProjectUIPalette::PanelFill(0.88f);
	}
	FLinearColor RowOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.34f);
	}
	FLinearColor DividerTint()
	{
		return EFProjectUIPalette::OutlineDim(0.42f);
	}
	FLinearColor MessageTint()
	{
		return EFProjectUIPalette::PrimaryText(0.98f);
	}
	FLinearColor SecondaryTint()
	{
		return EFProjectUIPalette::SecondaryText(0.94f);
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.36f);

	const TCHAR* TitleFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Bebas.F_Chronicle_Bebas");
	const TCHAR* BodyFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Cormorant.F_Chronicle_Cormorant");
	const TCHAR* BodyItalicFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_CormorantItalic.F_Chronicle_CormorantItalic");
	const TCHAR* RowFrameTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_RowFrame.T_Chronicle_RowFrame");
	const TCHAR* BadgeTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Badge.T_Chronicle_Badge");
	const TCHAR* GlyphTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Glyph.T_Chronicle_Glyph");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FProjectActivityFeedRowDisplayData MakePreviewData(
		const bool bExpanded,
		const EProjectActivityFeedRenderStyle RenderStyle,
		const FString& BadgeLabel,
		const FText& PrimaryText,
		const FText& SecondaryText,
		const FText& Message)
	{
		FProjectActivityFeedRowDisplayData Data;
		Data.RenderStyle = RenderStyle;
		Data.BadgeLabel = BadgeLabel;
		Data.PrimaryText = PrimaryText;
		Data.SecondaryText = SecondaryText;
		Data.Message = Message;
		Data.RowWidth = bExpanded ? 580.0f : 520.0f;
		Data.RowHeight = bExpanded ? 32.0f : 22.0f;
		Data.TextWrapWidth = bExpanded ? 450.0f : 390.0f;
		Data.InlinePrimaryWidthRatio = 0.38f;
		Data.LineHeightPercentage = bExpanded ? 1.18f : 1.22f;
		Data.BodyFontSize = bExpanded ? 15 : 13;
		Data.BadgeFontSize = bExpanded ? 15 : 13;
		Data.PrimaryFontSize = bExpanded ? 15 : 13;
		Data.bExpanded = bExpanded;
		return Data;
	}
}

UProjectActivityFeedEntryRowWidget::UProjectActivityFeedEntryRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::TitleFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::BodyFontPath);
	BodyItalicFontAsset = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::BodyItalicFontPath);
	RowFrameTexture = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::RowFrameTexturePath);
	BadgeTexture = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::BadgeTexturePath);
	GlyphTexture = FSoftObjectPath(ProjectActivityFeedEntryRowWidgetPrivate::GlyphTexturePath);
}

void UProjectActivityFeedEntryRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectActivityFeedEntryRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectActivityFeedEntryRowWidget::ApplyDisplayData(const FProjectActivityFeedRowDisplayData& InDisplayData)
{
	CurrentDisplayData = InDisplayData;
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
}

bool UProjectActivityFeedEntryRowWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	CurrentDisplayData = MakeDesignerPreviewData();
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshVisuals();
	}

	return bBuiltTree;
}

void UProjectActivityFeedEntryRowWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectActivityFeedEntryRowWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectActivityFeedEntryRowWidgetPrivate::DefaultRowWidth);
	RootSizeBox->SetMinDesiredHeight(ProjectActivityFeedEntryRowWidgetPrivate::DefaultRowHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->AddChild(RootOverlay);

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(0.0f));
	BackgroundBorder->SetBrushColor(FLinearColor::White);
	if (UOverlaySlot* BackgroundSlot = RootOverlay->AddChildToOverlay(BackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	FrameImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameImage"));
	if (UOverlaySlot* FrameSlot = RootOverlay->AddChildToOverlay(FrameImage))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(0.0f));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	BadgeSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BadgeSizeBox"));
	BadgeSizeBox->SetWidthOverride(ProjectActivityFeedEntryRowWidgetPrivate::CompactBadgeWidth);
	BadgeSizeBox->SetHeightOverride(26.0f);
	if (UHorizontalBoxSlot* BadgeSlot = ContentBox->AddChildToHorizontalBox(BadgeSizeBox))
	{
		BadgeSlot->SetHorizontalAlignment(HAlign_Left);
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
	}

	BadgeOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BadgeOverlay"));
	BadgeSizeBox->AddChild(BadgeOverlay);

	BadgeBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BadgeBorder"));
	BadgeBorder->SetPadding(FMargin(0.0f));
	BadgeBorder->SetBrushColor(FLinearColor::White);
	if (UOverlaySlot* BadgeBorderSlot = BadgeOverlay->AddChildToOverlay(BadgeBorder))
	{
		BadgeBorderSlot->SetHorizontalAlignment(HAlign_Fill);
		BadgeBorderSlot->SetVerticalAlignment(VAlign_Fill);
	}

	BadgeImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BadgeImage"));
	if (UOverlaySlot* BadgeImageSlot = BadgeOverlay->AddChildToOverlay(BadgeImage))
	{
		BadgeImageSlot->SetHorizontalAlignment(HAlign_Fill);
		BadgeImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	BadgeScaleBox = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("BadgeScaleBox"));
	BadgeScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UOverlaySlot* BadgeTextSlot = BadgeOverlay->AddChildToOverlay(BadgeScaleBox))
	{
		BadgeTextSlot->SetHorizontalAlignment(HAlign_Fill);
		BadgeTextSlot->SetVerticalAlignment(VAlign_Fill);
		BadgeTextSlot->SetPadding(FMargin(8.0f, 4.0f, 8.0f, 4.0f));
	}

	BadgeText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BadgeText"));
	BadgeScaleBox->AddChild(BadgeText);

	DividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DividerSizeBox"));
	DividerSizeBox->SetWidthOverride(ProjectActivityFeedEntryRowWidgetPrivate::DividerWidth);
	DividerSizeBox->SetHeightOverride(24.0f);
	if (UHorizontalBoxSlot* DividerSlot = ContentBox->AddChildToHorizontalBox(DividerSizeBox))
	{
		DividerSlot->SetHorizontalAlignment(HAlign_Center);
		DividerSlot->SetVerticalAlignment(VAlign_Center);
		DividerSlot->SetPadding(FMargin(10.0f, 0.0f, 10.0f, 0.0f));
	}

	DividerBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DividerBorder"));
	DividerBorder->SetPadding(FMargin(0.0f));
	DividerBorder->SetBrushColor(ProjectActivityFeedEntryRowWidgetPrivate::DividerTint());
	DividerSizeBox->AddChild(DividerBorder);

	MessageText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	if (UHorizontalBoxSlot* MessageSlot = ContentBox->AddChildToHorizontalBox(MessageText))
	{
		MessageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MessageSlot->SetHorizontalAlignment(HAlign_Fill);
		MessageSlot->SetVerticalAlignment(VAlign_Center);
	}

	InlineTextBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InlineTextBox"));
	if (UHorizontalBoxSlot* InlineSlot = ContentBox->AddChildToHorizontalBox(InlineTextBox))
	{
		InlineSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		InlineSlot->SetHorizontalAlignment(HAlign_Fill);
		InlineSlot->SetVerticalAlignment(VAlign_Center);
	}

	PrimaryTextBlock = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PrimaryTextBlock"));
	if (UHorizontalBoxSlot* PrimarySlot = InlineTextBox->AddChildToHorizontalBox(PrimaryTextBlock))
	{
		PrimarySlot->SetHorizontalAlignment(HAlign_Left);
		PrimarySlot->SetVerticalAlignment(VAlign_Center);
		PrimarySlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}

	SecondaryTextBlock = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SecondaryTextBlock"));
	if (UHorizontalBoxSlot* SecondarySlot = InlineTextBox->AddChildToHorizontalBox(SecondaryTextBlock))
	{
		SecondarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SecondarySlot->SetHorizontalAlignment(HAlign_Fill);
		SecondarySlot->SetVerticalAlignment(VAlign_Center);
	}

	GlyphSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GlyphSizeBox"));
	GlyphSizeBox->SetWidthOverride(ProjectActivityFeedEntryRowWidgetPrivate::CompactGlyphSize);
	GlyphSizeBox->SetHeightOverride(ProjectActivityFeedEntryRowWidgetPrivate::CompactGlyphSize);
	if (UHorizontalBoxSlot* GlyphSlot = ContentBox->AddChildToHorizontalBox(GlyphSizeBox))
	{
		GlyphSlot->SetHorizontalAlignment(HAlign_Center);
		GlyphSlot->SetVerticalAlignment(VAlign_Center);
		GlyphSlot->SetPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f));
	}

	GlyphImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GlyphImage"));
	GlyphSizeBox->AddChild(GlyphImage);

	return true;
}

void UProjectActivityFeedEntryRowWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (!bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = true;
		return;
	}

	if (FrameImage)
	{
		FrameImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(RowFrameTexture, ProjectActivityFeedEntryRowWidgetPrivate::RowFrameTexturePath)), false);
	}

	if (BadgeImage)
	{
		BadgeImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(BadgeTexture, ProjectActivityFeedEntryRowWidgetPrivate::BadgeTexturePath)), false);
	}

	if (GlyphImage)
	{
		GlyphImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(GlyphTexture, ProjectActivityFeedEntryRowWidgetPrivate::GlyphTexturePath)), false);
	}

	if (BadgeText)
	{
		BadgeText->SetFont(MakeTitleFont(12, 0));
		BadgeText->SetJustification(ETextJustify::Center);
		BadgeText->SetShadowOffset(FVector2D(0.0f, 0.25f));
		BadgeText->SetShadowColorAndOpacity(ProjectActivityFeedEntryRowWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.18f));
	}

	if (MessageText)
	{
		MessageText->SetFont(MakeBodyFont(14, 0));
		MessageText->SetJustification(ETextJustify::Left);
		MessageText->SetAutoWrapText(true);
		MessageText->SetShadowOffset(FVector2D(0.0f, 0.4f));
		MessageText->SetShadowColorAndOpacity(ProjectActivityFeedEntryRowWidgetPrivate::ShadowTint);
	}

	if (PrimaryTextBlock)
	{
		PrimaryTextBlock->SetFont(MakeBodyFont(18, 0));
		PrimaryTextBlock->SetJustification(ETextJustify::Left);
		PrimaryTextBlock->SetShadowOffset(FVector2D(0.0f, 0.45f));
		PrimaryTextBlock->SetShadowColorAndOpacity(ProjectActivityFeedEntryRowWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.24f));
	}

	if (SecondaryTextBlock)
	{
		SecondaryTextBlock->SetFont(MakeBodyFont(14, 0));
		SecondaryTextBlock->SetJustification(ETextJustify::Left);
		SecondaryTextBlock->SetAutoWrapText(true);
		SecondaryTextBlock->SetShadowOffset(FVector2D(0.0f, 0.35f));
		SecondaryTextBlock->SetShadowColorAndOpacity(ProjectActivityFeedEntryRowWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.22f));
	}

	bVisualTreeInitialized = true;
}

void UProjectActivityFeedEntryRowWidget::RefreshVisuals()
{
	const bool bExpandedLayout = CurrentDisplayData.bExpanded;
	const float RowWidth = FMath::Max(CurrentDisplayData.RowWidth, 320.0f);
	const float MinimumRowHeight = FMath::Max(CurrentDisplayData.RowHeight, bExpandedLayout ? 18.0f : 12.0f);
	const float TextWrapWidth = FMath::Clamp(CurrentDisplayData.TextWrapWidth, 64.0f, RowWidth);
	const float InlinePrimaryWidthRatio = FMath::Clamp(CurrentDisplayData.InlinePrimaryWidthRatio, 0.1f, 0.9f);
	const float LineHeightPercentage = FMath::Clamp(CurrentDisplayData.LineHeightPercentage, 1.0f, 2.0f);
	const float BadgeHeight = FMath::Max(MinimumRowHeight - 6.0f, bExpandedLayout ? 22.0f : 16.0f);
	const float BadgeWidth = bExpandedLayout ? ProjectActivityFeedEntryRowWidgetPrivate::ExpandedBadgeWidth : ProjectActivityFeedEntryRowWidgetPrivate::CompactBadgeWidth;
	const float GlyphSize = bExpandedLayout ? ProjectActivityFeedEntryRowWidgetPrivate::ExpandedGlyphSize : ProjectActivityFeedEntryRowWidgetPrivate::CompactGlyphSize;
	const float CornerRadius = bExpandedLayout ? 8.0f : 8.0f;
	const float OutlineWidth = 1.0f;
	const int32 BodyFontSize = FMath::Max(CurrentDisplayData.BodyFontSize, 6);
	const int32 BadgeFontSize = FMath::Max(CurrentDisplayData.BadgeFontSize, 6);
	const int32 PrimaryFontSize = FMath::Max(CurrentDisplayData.PrimaryFontSize, 6);
	const FLinearColor AccentTint = CurrentDisplayData.AccentTint.GetClamped(0.0f, 1.0f);
	const FLinearColor BadgeFillTint = CurrentDisplayData.BadgeFillTint.GetClamped(0.0f, 1.0f);
	const FLinearColor BadgeTextTint = CurrentDisplayData.BadgeTextTint.GetClamped(0.0f, 1.0f);
	const bool bUseInlineLayout =
		CurrentDisplayData.RenderStyle == EProjectActivityFeedRenderStyle::Gain
		|| CurrentDisplayData.RenderStyle == EProjectActivityFeedRenderStyle::DialogueQuote
		|| (!CurrentDisplayData.PrimaryText.IsEmpty() && !CurrentDisplayData.SecondaryText.IsEmpty());

	if (RootSizeBox)
	{
		RootSizeBox->SetWidthOverride(RowWidth);
		RootSizeBox->ClearHeightOverride();
		RootSizeBox->SetMinDesiredHeight(MinimumRowHeight);
	}

	if (BackgroundBorder)
	{
		if (bUsingNativeFallbackTree)
		{
			const FLinearColor OutlineTint = FLinearColor(
				FMath::Max(ProjectActivityFeedEntryRowWidgetPrivate::RowOutlineTint().R, AccentTint.R * 0.82f),
				FMath::Max(ProjectActivityFeedEntryRowWidgetPrivate::RowOutlineTint().G, AccentTint.G * 0.68f),
				FMath::Max(ProjectActivityFeedEntryRowWidgetPrivate::RowOutlineTint().B, AccentTint.B * 0.86f),
				bExpandedLayout ? 0.40f : 0.34f);
			const FSlateRoundedBoxBrush BackgroundBrush(
				ProjectActivityFeedEntryRowWidgetPrivate::RowFillTint(),
				CornerRadius,
				FSlateColor(OutlineTint),
				OutlineWidth,
				FVector2f(RowWidth, MinimumRowHeight));
			BackgroundBorder->SetBrush(BackgroundBrush);
		}
	}

	if (FrameImage)
	{
		if (bUsingNativeFallbackTree)
		{
			FrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				FrameImage,
				AccentTint.CopyWithNewOpacity(bExpandedLayout ? 0.54f : 0.48f)));
		}
	}

	if (ContentBorder)
	{
		if (bUsingNativeFallbackTree)
		{
			ContentBorder->SetPadding(FMargin(0.0f));
		}
	}

	if (BadgeSizeBox)
	{
		if (bUsingNativeFallbackTree)
		{
			BadgeSizeBox->SetWidthOverride(BadgeWidth);
			BadgeSizeBox->SetHeightOverride(BadgeHeight);
		}
	}

	if (BadgeBorder)
	{
		if (bUsingNativeFallbackTree)
		{
			const FSlateRoundedBoxBrush BadgeBrush(
				BadgeFillTint.CopyWithNewOpacity(0.98f),
				7.0f,
				FSlateColor(AccentTint.CopyWithNewOpacity(0.30f)),
				1.0f,
				FVector2f(BadgeWidth, BadgeHeight));
			BadgeBorder->SetBrush(BadgeBrush);
		}
	}

	if (BadgeImage)
	{
		if (bUsingNativeFallbackTree)
		{
			BadgeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				BadgeImage,
				BadgeFillTint.CopyWithNewOpacity(0.96f)));
		}
	}

	if (BadgeText)
	{
		BadgeText->SetText(FText::FromString(CurrentDisplayData.BadgeLabel));
		if (bUsingNativeFallbackTree)
		{
			BadgeText->SetColorAndOpacity(FSlateColor(BadgeTextTint));
			BadgeText->SetFont(MakeBodyEmphasisFont(BadgeFontSize, 0));
		}
	}

	if (DividerSizeBox)
	{
		if (bUsingNativeFallbackTree)
		{
			DividerSizeBox->SetHeightOverride(FMath::Max(bExpandedLayout ? 20.0f : 14.0f, BadgeHeight - 2.0f));
		}
	}

	if (DividerBorder)
	{
		if (bUsingNativeFallbackTree)
		{
			const FSlateRoundedBoxBrush DividerBrush(
				AccentTint.CopyWithNewOpacity(0.34f),
				0.5f,
				FSlateColor(FLinearColor::Transparent),
				0.0f,
				FVector2f(ProjectActivityFeedEntryRowWidgetPrivate::DividerWidth, BadgeHeight));
			DividerBorder->SetBrush(DividerBrush);
		}
	}

	if (MessageText)
	{
		MessageText->SetVisibility(bUseInlineLayout ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		MessageText->SetText(CurrentDisplayData.Message);
		MessageText->SetAutoWrapText(true);
		MessageText->SetWrapTextAt(TextWrapWidth);
		MessageText->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
		MessageText->SetLineHeightPercentage(LineHeightPercentage);
		MessageText->SetApplyLineHeightToBottomLine(true);
		MessageText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
		MessageText->SetClipping(EWidgetClipping::ClipToBounds);
		MessageText->SetMinDesiredWidth(0.0f);
		if (UHorizontalBoxSlot* MessageSlot = Cast<UHorizontalBoxSlot>(MessageText->Slot))
		{
			MessageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			MessageSlot->SetVerticalAlignment(VAlign_Top);
		}
		if (bUsingNativeFallbackTree)
		{
			MessageText->SetFont(MakeBodyFont(BodyFontSize, 0));
			MessageText->SetColorAndOpacity(FSlateColor(ProjectActivityFeedEntryRowWidgetPrivate::MessageTint()));
		}
	}

	if (InlineTextBox)
	{
		InlineTextBox->SetVisibility(bUseInlineLayout ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (PrimaryTextBlock)
	{
		PrimaryTextBlock->SetText(CurrentDisplayData.PrimaryText);
		PrimaryTextBlock->SetAutoWrapText(true);
		PrimaryTextBlock->SetWrapTextAt(TextWrapWidth * InlinePrimaryWidthRatio);
		PrimaryTextBlock->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
		PrimaryTextBlock->SetLineHeightPercentage(LineHeightPercentage);
		PrimaryTextBlock->SetApplyLineHeightToBottomLine(true);
		PrimaryTextBlock->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
		PrimaryTextBlock->SetClipping(EWidgetClipping::ClipToBounds);
		PrimaryTextBlock->SetMinDesiredWidth(0.0f);
		if (UHorizontalBoxSlot* PrimarySlot = Cast<UHorizontalBoxSlot>(PrimaryTextBlock->Slot))
		{
			FSlateChildSize PrimarySize(ESlateSizeRule::Fill);
			PrimarySize.Value = InlinePrimaryWidthRatio;
			PrimarySlot->SetSize(PrimarySize);
			PrimarySlot->SetVerticalAlignment(VAlign_Top);
		}
		if (bUsingNativeFallbackTree)
		{
			PrimaryTextBlock->SetColorAndOpacity(FSlateColor(AccentTint.CopyWithNewOpacity(0.98f)));
			PrimaryTextBlock->SetFont(MakeBodyEmphasisFont(PrimaryFontSize, 0));
		}
	}

	if (SecondaryTextBlock)
	{
		SecondaryTextBlock->SetText(!CurrentDisplayData.SecondaryText.IsEmpty() ? CurrentDisplayData.SecondaryText : CurrentDisplayData.Message);
		SecondaryTextBlock->SetAutoWrapText(true);
		SecondaryTextBlock->SetWrapTextAt(TextWrapWidth * (1.0f - InlinePrimaryWidthRatio));
		SecondaryTextBlock->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
		SecondaryTextBlock->SetLineHeightPercentage(LineHeightPercentage);
		SecondaryTextBlock->SetApplyLineHeightToBottomLine(true);
		SecondaryTextBlock->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
		SecondaryTextBlock->SetClipping(EWidgetClipping::ClipToBounds);
		SecondaryTextBlock->SetMinDesiredWidth(0.0f);
		if (UHorizontalBoxSlot* SecondarySlot = Cast<UHorizontalBoxSlot>(SecondaryTextBlock->Slot))
		{
			FSlateChildSize SecondarySize(ESlateSizeRule::Fill);
			SecondarySize.Value = 1.0f - InlinePrimaryWidthRatio;
			SecondarySlot->SetSize(SecondarySize);
			SecondarySlot->SetVerticalAlignment(VAlign_Top);
		}
		if (bUsingNativeFallbackTree)
		{
			SecondaryTextBlock->SetColorAndOpacity(FSlateColor(
				CurrentDisplayData.RenderStyle == EProjectActivityFeedRenderStyle::DialogueQuote
					? ProjectActivityFeedEntryRowWidgetPrivate::MessageTint()
					: ProjectActivityFeedEntryRowWidgetPrivate::SecondaryTint()));
			SecondaryTextBlock->SetFont(MakeBodyFont(
				BodyFontSize,
				0,
				CurrentDisplayData.RenderStyle == EProjectActivityFeedRenderStyle::DialogueQuote));
		}
	}

	if (GlyphSizeBox)
	{
		if (bUsingNativeFallbackTree)
		{
			GlyphSizeBox->SetWidthOverride(GlyphSize);
			GlyphSizeBox->SetHeightOverride(GlyphSize);
		}
	}

	if (GlyphImage)
	{
		if (bUsingNativeFallbackTree)
		{
			GlyphImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				GlyphImage,
				AccentTint.CopyWithNewOpacity(0.88f)));
		}
	}

	InvalidateLayoutAndVolatility();
	OnChronicleRowDataApplied(CurrentDisplayData);
}

FProjectActivityFeedRowDisplayData UProjectActivityFeedEntryRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		false,
		EProjectActivityFeedRenderStyle::Standard,
		TEXT("LOG"),
		FText::GetEmpty(),
		FText::GetEmpty(),
		FText::FromString(TEXT("Editable Chronicle row preview.")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleNormalStandardRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		false,
		EProjectActivityFeedRenderStyle::Standard,
		TEXT("LOG"),
		FText::GetEmpty(),
		FText::GetEmpty(),
		FText::FromString(TEXT("Normal standard Chronicle entry.")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleNormalGainRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		false,
		EProjectActivityFeedRenderStyle::Gain,
		TEXT("DXP"),
		FText::FromString(TEXT("+15")),
		FText::FromString(TEXT("Doctrine experience gained.")),
		FText::FromString(TEXT("+15 Doctrine experience gained.")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleNormalDialogueQuoteRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		false,
		EProjectActivityFeedRenderStyle::DialogueQuote,
		TEXT("ENEMY"),
		FText::FromString(TEXT("Cultist")),
		FText::FromString(TEXT("\"You should not be here.\"")),
		FText::FromString(TEXT("Cultist: \"You should not be here.\"")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleExpandedStandardRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		true,
		EProjectActivityFeedRenderStyle::Standard,
		TEXT("LOG"),
		FText::GetEmpty(),
		FText::GetEmpty(),
		FText::FromString(TEXT("Expanded standard Chronicle entry with more room.")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleExpandedGainRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		true,
		EProjectActivityFeedRenderStyle::Gain,
		TEXT("DXP"),
		FText::FromString(TEXT("+125")),
		FText::FromString(TEXT("Doctrine experience gathered from the encounter.")),
		FText::FromString(TEXT("+125 Doctrine experience gathered from the encounter.")));
}

FProjectActivityFeedRowDisplayData UProjectChronicleExpandedDialogueQuoteRowWidget::MakeDesignerPreviewData() const
{
	return ProjectActivityFeedEntryRowWidgetPrivate::MakePreviewData(
		true,
		EProjectActivityFeedRenderStyle::DialogueQuote,
		TEXT("ENEMY"),
		FText::FromString(TEXT("Watcher")),
		FText::FromString(TEXT("\"The Chronicle remembers everything.\"")),
		FText::FromString(TEXT("Watcher: \"The Chronicle remembers everything.\"")));
}

UTexture2D* UProjectActivityFeedEntryRowWidget::ResolveTexture(
	const TSoftObjectPtr<UTexture2D>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectActivityFeedEntryRowWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectActivityFeedEntryRowWidget::ResolveStyleAsset(
	const TSoftObjectPtr<UObject>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedObject = AssetPtr.LoadSynchronous())
		{
			return LoadedObject;
		}
	}

	return ProjectActivityFeedEntryRowWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectActivityFeedEntryRowWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectActivityFeedEntryRowWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectActivityFeedEntryRowWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing, const bool bItalic) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(bItalic ? TEXT("Italic") : TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;

	const TSoftObjectPtr<UObject>& PreferredAsset = bItalic ? BodyItalicFontAsset : BodyFontAsset;
	const TCHAR* FallbackPath = bItalic
		? ProjectActivityFeedEntryRowWidgetPrivate::BodyItalicFontPath
		: ProjectActivityFeedEntryRowWidgetPrivate::BodyFontPath;
	if (UObject* FontObject = ResolveStyleAsset(PreferredAsset, FallbackPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectActivityFeedEntryRowWidget::MakeBodyEmphasisFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;

	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectActivityFeedEntryRowWidgetPrivate::BodyFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = FName(TEXT("Bold"));
	}

	return FontInfo;
}
