#include "TattooShop/UI/ProjectTattooShopColorPickerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUIPalette.h"
#include "Styling/CoreStyle.h"

namespace ProjectTattooShopColorPickerWidgetPrivate
{
	FSlateFontInfo Font(const int32 Size, const bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	}

	FText FormatChannel(const float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%.2f"), Value));
	}
}

UProjectTattooShopColorPickerWidget::UProjectTattooShopColorPickerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectTattooShopColorPickerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTattooShopColorPickerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectTattooShopColorPickerWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectTattooShopColorPickerWidget::SetColor(FLinearColor InColor, const bool bNotify)
{
	InColor.R = FMath::Clamp(InColor.R, 0.0f, 1.0f);
	InColor.G = FMath::Clamp(InColor.G, 0.0f, 1.0f);
	InColor.B = FMath::Clamp(InColor.B, 0.0f, 1.0f);
	InColor.A = 1.0f;
	CurrentColor = InColor;
	RefreshVisuals();
	if (bNotify)
	{
		BroadcastColor();
	}
}

void UProjectTattooShopColorPickerWidget::SetReadOnly(const bool bInReadOnly)
{
	bReadOnly = bInReadOnly;
	RefreshVisuals();
}

void UProjectTattooShopColorPickerWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectTattooShopColorPickerWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootBox"));
	TargetWidgetTree->RootWidget = RootBox;

	UTextBlock* Title = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BaseColorLabel"));
	Title->SetText(FText::FromString(TEXT("TINT COLOR")));
	Title->SetFont(ProjectTattooShopColorPickerWidgetPrivate::Font(9, true));
	Title->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
	if (UVerticalBoxSlot* TitleSlot = RootBox->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 1.0f, 0.0f, 2.0f));
	}

	USizeBox* PreviewSize = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ColorPreviewSizeBox"));
	PreviewSize->SetHeightOverride(30.0f);
	if (UVerticalBoxSlot* PreviewSlot = RootBox->AddChildToVerticalBox(PreviewSize))
	{
		PreviewSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 4.0f));
		PreviewSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	ColorPreviewBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ColorPreviewBorder"));
	PreviewSize->SetContent(ColorPreviewBorder);

	USlider* RedSliderRaw = nullptr;
	UTextBlock* RedTextRaw = nullptr;
	AddChannelRow(TargetWidgetTree, RootBox, FText::FromString(TEXT("R")), TEXT("RedRow"), RedSliderRaw, RedTextRaw);
	RedSlider = RedSliderRaw;
	RedValueText = RedTextRaw;

	USlider* GreenSliderRaw = nullptr;
	UTextBlock* GreenTextRaw = nullptr;
	AddChannelRow(TargetWidgetTree, RootBox, FText::FromString(TEXT("G")), TEXT("GreenRow"), GreenSliderRaw, GreenTextRaw);
	GreenSlider = GreenSliderRaw;
	GreenValueText = GreenTextRaw;

	USlider* BlueSliderRaw = nullptr;
	UTextBlock* BlueTextRaw = nullptr;
	AddChannelRow(TargetWidgetTree, RootBox, FText::FromString(TEXT("B")), TEXT("BlueRow"), BlueSliderRaw, BlueTextRaw);
	BlueSlider = BlueSliderRaw;
	BlueValueText = BlueTextRaw;

	return true;
}

void UProjectTattooShopColorPickerWidget::AddChannelRow(
	UWidgetTree* TargetWidgetTree,
	UVerticalBox* Parent,
	const FText& Label,
	const FName RowName,
	USlider*& OutSlider,
	UTextBlock*& OutValueText)
{
	UHorizontalBox* Row = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), RowName);
	if (UVerticalBoxSlot* RowSlot = Parent->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.0f, 1.0f));
	}

	USizeBox* LabelWidth = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("%sLabelSizeBox"), *RowName.ToString()));
	LabelWidth->SetWidthOverride(18.0f);
	if (UHorizontalBoxSlot* LabelWidthSlot = Row->AddChildToHorizontalBox(LabelWidth))
	{
		LabelWidthSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		LabelWidthSlot->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
		LabelWidthSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* LabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("%sLabelText"), *RowName.ToString()));
	LabelText->SetText(Label);
	LabelText->SetFont(ProjectTattooShopColorPickerWidgetPrivate::Font(9, true));
	LabelText->SetJustification(ETextJustify::Center);
	LabelText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
	LabelWidth->SetContent(LabelText);

	FString ControlName = RowName.ToString();
	ControlName.RemoveFromEnd(TEXT("Row"));
	OutSlider = TargetWidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *FString::Printf(TEXT("%sSlider"), *ControlName));
	OutSlider->SetMinValue(0.0f);
	OutSlider->SetMaxValue(1.0f);
	if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(OutSlider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetPadding(FMargin(0.0f, 1.0f));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
	}

	OutValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("%sValueText"), *ControlName));
	OutValueText->SetFont(ProjectTattooShopColorPickerWidgetPrivate::Font(9, true));
	OutValueText->SetJustification(ETextJustify::Right);
	OutValueText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(OutValueText))
	{
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ValueSlot->SetPadding(FMargin(7.0f, 0.0f, 1.0f, 0.0f));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UProjectTattooShopColorPickerWidget::BindControls()
{
	if (RedSlider)
	{
		RedSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopColorPickerWidget::HandleRedChanged);
	}
	if (GreenSlider)
	{
		GreenSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopColorPickerWidget::HandleGreenChanged);
	}
	if (BlueSlider)
	{
		BlueSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopColorPickerWidget::HandleBlueChanged);
	}
}

void UProjectTattooShopColorPickerWidget::RefreshVisuals()
{
	TGuardValue<bool> RefreshGuard(bRefreshingControls, true);

	if (RedSlider)
	{
		RedSlider->SetValue(CurrentColor.R);
		RedSlider->SetIsEnabled(!bReadOnly);
	}
	if (GreenSlider)
	{
		GreenSlider->SetValue(CurrentColor.G);
		GreenSlider->SetIsEnabled(!bReadOnly);
	}
	if (BlueSlider)
	{
		BlueSlider->SetValue(CurrentColor.B);
		BlueSlider->SetIsEnabled(!bReadOnly);
	}
	if (RedValueText)
	{
		RedValueText->SetText(ProjectTattooShopColorPickerWidgetPrivate::FormatChannel(CurrentColor.R));
	}
	if (GreenValueText)
	{
		GreenValueText->SetText(ProjectTattooShopColorPickerWidgetPrivate::FormatChannel(CurrentColor.G));
	}
	if (BlueValueText)
	{
		BlueValueText->SetText(ProjectTattooShopColorPickerWidgetPrivate::FormatChannel(CurrentColor.B));
	}
	if (ColorPreviewBorder)
	{
		if (bUsingNativeFallbackTree)
		{
			const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.0f);
			ColorPreviewBorder->SetBrush(Brush);
		}
		// The brush shape remains Designer-owned; only its data-driven tint changes.
		ColorPreviewBorder->SetBrushColor(CurrentColor);
	}

	OnColorApplied(CurrentColor, bReadOnly);
}

void UProjectTattooShopColorPickerWidget::BroadcastColor()
{
	if (!bRefreshingControls && !bReadOnly)
	{
		OnColorChanged.Broadcast(CurrentColor);
	}
}

void UProjectTattooShopColorPickerWidget::HandleRedChanged(const float Value)
{
	if (!bRefreshingControls && !bReadOnly)
	{
		CurrentColor.R = FMath::Clamp(Value, 0.0f, 1.0f);
		RefreshVisuals();
		BroadcastColor();
	}
}

void UProjectTattooShopColorPickerWidget::HandleGreenChanged(const float Value)
{
	if (!bRefreshingControls && !bReadOnly)
	{
		CurrentColor.G = FMath::Clamp(Value, 0.0f, 1.0f);
		RefreshVisuals();
		BroadcastColor();
	}
}

void UProjectTattooShopColorPickerWidget::HandleBlueChanged(const float Value)
{
	if (!bRefreshingControls && !bReadOnly)
	{
		CurrentColor.B = FMath::Clamp(Value, 0.0f, 1.0f);
		RefreshVisuals();
		BroadcastColor();
	}
}
