#include "TattooShop/UI/ProjectTattooShopCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUIPalette.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace ProjectTattooShopCardWidgetPrivate
{
	FSlateFontInfo Font(const int32 Size, const bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	}
}

UProjectTattooShopCardWidget::UProjectTattooShopCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CardData.DisplayName = FText::FromString(TEXT("Tattoo"));
}

void UProjectTattooShopCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTattooShopCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectTattooShopCardWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	CardData.DisplayName = FText::FromString(TEXT("T_Bunny"));
	CardData.Subtitle = FText::FromString(TEXT("LIBRARY"));
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectTattooShopCardWidget::ApplyCardData(const FProjectTattooShopCardData& InCardData)
{
	CardData = InCardData;
	BuildWidgetTreeIfNeeded();
	RefreshVisuals();
	OnCardDataApplied(CardData);
}

void UProjectTattooShopCardWidget::SetSelected(const bool bInSelected)
{
	CardData.bSelected = bInSelected;
	RefreshVisuals();
}

void UProjectTattooShopCardWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectTattooShopCardWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(128.0f);
	RootSizeBox->SetHeightOverride(112.0f);
	TargetWidgetTree->RootWidget = RootSizeBox;

	UOverlay* RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->SetContent(RootOverlay);

	SelectionBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectionBorder"));
	SelectionBorder->SetPadding(FMargin(2.0f));
	if (UOverlaySlot* CardSlot = RootOverlay->AddChildToOverlay(SelectionBorder))
	{
		CardSlot->SetHorizontalAlignment(HAlign_Fill);
		CardSlot->SetVerticalAlignment(VAlign_Fill);
	}

	CardButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CardButton"));
	SelectionBorder->SetContent(CardButton);

	UBorder* ContentPadding = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardContentPaddingBorder"));
	ContentPadding->SetPadding(FMargin(4.0f, 4.0f, 4.0f, 3.0f));
	ContentPadding->SetBrushColor(FLinearColor::Transparent);
	CardButton->AddChild(ContentPadding);

	UVerticalBox* Content = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardContent"));
	ContentPadding->SetContent(Content);

	USizeBox* ThumbnailSize = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ThumbnailSizeBox"));
	ThumbnailSize->SetHeightOverride(70.0f);
	if (UVerticalBoxSlot* ThumbnailSlot = Content->AddChildToVerticalBox(ThumbnailSize))
	{
		ThumbnailSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	UBorder* ThumbnailFrame = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ThumbnailFrameBorder"));
	ThumbnailFrame->SetPadding(FMargin(2.0f));
	ThumbnailFrame->SetBrush(FSlateRoundedBoxBrush(
		EFProjectUIPalette::PanelFillDeep(0.78f),
		3.0f,
		FSlateColor(EFProjectUIPalette::OutlineDim(0.38f)),
		1.0f));
	ThumbnailSize->SetContent(ThumbnailFrame);

	UScaleBox* ThumbnailScale = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ThumbnailScaleBox"));
	ThumbnailScale->SetStretch(EStretch::ScaleToFit);
	ThumbnailScale->SetStretchDirection(EStretchDirection::Both);
	ThumbnailScale->SetUserSpecifiedScale(1.0f);
	ThumbnailFrame->SetContent(ThumbnailScale);

	ThumbnailImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ThumbnailImage"));
	ThumbnailScale->AddChild(ThumbnailImage);

	NameText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameText"));
	NameText->SetJustification(ETextJustify::Center);
	NameText->SetFont(ProjectTattooShopCardWidgetPrivate::Font(9, true));
	NameText->SetAutoWrapText(false);
	NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	NameText->SetClipping(EWidgetClipping::ClipToBounds);
	if (UVerticalBoxSlot* NameSlot = Content->AddChildToVerticalBox(NameText))
	{
		NameSlot->SetHorizontalAlignment(HAlign_Fill);
		NameSlot->SetPadding(FMargin(2.0f, 3.0f, 2.0f, 0.0f));
	}

	SubtitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SubtitleText"));
	SubtitleText->SetJustification(ETextJustify::Center);
	SubtitleText->SetFont(ProjectTattooShopCardWidgetPrivate::Font(7, true));
	SubtitleText->SetAutoWrapText(false);
	SubtitleText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	SubtitleText->SetClipping(EWidgetClipping::ClipToBounds);
	if (UVerticalBoxSlot* SubtitleSlot = Content->AddChildToVerticalBox(SubtitleText))
	{
		SubtitleSlot->SetHorizontalAlignment(HAlign_Fill);
		SubtitleSlot->SetPadding(FMargin(2.0f, 1.0f, 2.0f, 0.0f));
	}

	SelectedFrameBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectedFrameBorder"));
	SelectedFrameBorder->SetIsEnabled(false);
	SelectedFrameBorder->SetVisibility(ESlateVisibility::Collapsed);
	SelectedFrameBorder->SetBrush(FSlateRoundedBoxBrush(
		FLinearColor::Transparent,
		5.0f,
		FSlateColor(EFProjectUIPalette::AccentSoft()),
		2.0f,
		FVector2f(128.0f, 112.0f)));
	if (UOverlaySlot* SelectedSlot = RootOverlay->AddChildToOverlay(SelectedFrameBorder))
	{
		SelectedSlot->SetHorizontalAlignment(HAlign_Fill);
		SelectedSlot->SetVerticalAlignment(VAlign_Fill);
	}

	return true;
}

void UProjectTattooShopCardWidget::BindControls()
{
	if (CardButton)
	{
		CardButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopCardWidget::HandleCardClicked);
	}
}

void UProjectTattooShopCardWidget::RefreshVisuals()
{
	if (NameText)
	{
		NameText->SetText(CardData.DisplayName);
		NameText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
		NameText->SetToolTipText(CardData.DisplayName);
	}

	if (SubtitleText)
	{
		FText EffectiveSubtitle = CardData.Subtitle;
		if (EffectiveSubtitle.IsEmpty() && CardData.Kind == EProjectTattooShopCardKind::Automatic)
		{
			EffectiveSubtitle = FText::FromString(TEXT("READ ONLY"));
		}
		SubtitleText->SetText(EffectiveSubtitle);
		SubtitleText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
		SubtitleText->SetVisibility(EffectiveSubtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		SubtitleText->SetToolTipText(EffectiveSubtitle);
	}

	if (ThumbnailImage)
	{
		UTexture2D* Texture = CardData.RuntimeTexture;
		if (!Texture && !CardData.ThumbnailTexture.IsNull())
		{
			Texture = CardData.ThumbnailTexture.LoadSynchronous();
		}
		if (Texture)
		{
			ThumbnailImage->SetBrushFromTexture(Texture, true);
		}
		ThumbnailImage->SetColorAndOpacity(CardData.bEnabled ? FLinearColor::White : FLinearColor(0.35f, 0.35f, 0.35f, 0.72f));
	}

	if (CardButton)
	{
		CardButton->SetIsEnabled(CardData.bEnabled);
		if (bUsingNativeFallbackTree)
		{
			CardButton->SetBackgroundColor(EFProjectUIPalette::PanelFillDeep(0.94f));
		}
		CardButton->SetToolTipText(CardData.DisplayName);
	}

	if (SelectedFrameBorder)
	{
		SelectedFrameBorder->SetVisibility(CardData.bSelected
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (SelectionBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush Brush(
			EFProjectUIPalette::PanelFill(0.96f),
			4.0f,
			FSlateColor(EFProjectUIPalette::OutlineDim(0.52f)),
			1.0f,
			FVector2f(128.0f, 112.0f));
		SelectionBorder->SetBrush(Brush);
	}
}

void UProjectTattooShopCardWidget::HandleCardClicked()
{
	OnSelected.Broadcast(CardData);
}
