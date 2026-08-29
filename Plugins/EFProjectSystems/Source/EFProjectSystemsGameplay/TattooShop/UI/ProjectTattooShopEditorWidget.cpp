#include "TattooShop/UI/ProjectTattooShopEditorWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUIPalette.h"
#include "Styling/CoreStyle.h"
#include "TattooShop/UI/ProjectTattooShopColorPickerWidget.h"

namespace ProjectTattooShopEditorWidgetPrivate
{
	static const TCHAR* PlacementOptions[] =
	{
		TEXT("ChestFront"), TEXT("AbdomenFront"), TEXT("PelvisFront"),
		TEXT("UpperBack"), TEXT("LowerBack"),
		TEXT("LeftUpperArm"), TEXT("RightUpperArm"),
		TEXT("LeftForearm"), TEXT("RightForearm"),
		TEXT("LeftThigh"), TEXT("RightThigh"),
		TEXT("LeftBackThigh"), TEXT("RightBackThigh"),
		TEXT("LeftCalf"), TEXT("RightCalf"),
		TEXT("LeftBackCalf"), TEXT("RightBackCalf"),
		TEXT("LeftHand"), TEXT("RightHand"),
		TEXT("LeftFoot"), TEXT("RightFoot")
	};

	FSlateFontInfo Font(const int32 Size, const bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	}

	FText Number(const float Value)
	{
		return FText::FromString(FString::Printf(TEXT("%.2f"), Value));
	}

	UTextBlock* AddSectionHeading(
		UWidgetTree* Tree,
		UVerticalBox* Parent,
		const FName Name,
		const FText& Text,
		const float TopPadding = 8.0f)
	{
		UTextBlock* Heading = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Heading->SetText(Text);
		Heading->SetFont(Font(10, true));
		Heading->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
		if (UVerticalBoxSlot* HeadingSlot = Parent->AddChildToVerticalBox(Heading))
		{
			HeadingSlot->SetPadding(FMargin(0.0f, TopPadding, 0.0f, 3.0f));
		}
		return Heading;
	}
}

UProjectTattooShopEditorWidget::UProjectTattooShopEditorWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CurrentStatusText = FText::FromString(TEXT("Choose a tattoo, adjust it, then Accept."));
}

void UProjectTattooShopEditorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTreeIfNeeded();
	BindControls();
	PopulatePlacementOptions();
	RefreshControls();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTattooShopEditorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTreeIfNeeded();
	BindControls();
	PopulatePlacementOptions();
	RefreshControls();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectTattooShopEditorWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	EditorModel.DisplayName = FText::FromString(TEXT("T_Bunny"));
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectTattooShopEditorWidget::ApplyEditorModel(const FProjectTattooShopEditorModel& InModel)
{
	EditorModel = InModel;
	BuildWidgetTreeIfNeeded();
	RefreshControls();
	OnEditorModelApplied(EditorModel);
}

void UProjectTattooShopEditorWidget::SetStatusText(const FText& InStatusText)
{
	CurrentStatusText = InStatusText;
	if (StatusText)
	{
		StatusText->SetText(CurrentStatusText);
	}
}

void UProjectTattooShopEditorWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectTattooShopEditorWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	USizeBox* RootSize = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSize->SetWidthOverride(420.0f);
	RootSize->SetHeightOverride(680.0f);
	TargetWidgetTree->RootWidget = RootSize;

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(12.0f));
	RootSize->SetContent(BackgroundBorder);

	UVerticalBox* RootBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootBox"));
	BackgroundBorder->SetContent(RootBox);

	UTextBlock* EditorAccentText = TargetWidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("EditorAccentText"));
	EditorAccentText->SetText(FText::FromString(TEXT("EDIT TATTOO")));
	EditorAccentText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(10, true));
	EditorAccentText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
	RootBox->AddChildToVerticalBox(EditorAccentText);

	TitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(FText::FromString(TEXT("TATTOO SETTINGS")));
	TitleText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(18, true));
	TitleText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
	if (UVerticalBoxSlot* TitleSlot = RootBox->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
	}

	StatusText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetAutoWrapText(true);
	StatusText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(10));
	StatusText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
	if (UVerticalBoxSlot* StatusSlot = RootBox->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 8.0f));
	}

	ReadOnlyText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReadOnlyText"));
	ReadOnlyText->SetText(FText::FromString(TEXT("AUTOMATIC TATTOO - READ ONLY")));
	ReadOnlyText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(10, true));
	ReadOnlyText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
	if (UVerticalBoxSlot* ReadOnlySlot = RootBox->AddChildToVerticalBox(ReadOnlyText))
	{
		ReadOnlySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	EditorScrollBox = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("EditorScrollBox"));
	if (UVerticalBoxSlot* EditorScrollSlot = RootBox->AddChildToVerticalBox(EditorScrollBox))
	{
		EditorScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UVerticalBox* Controls = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsBox"));
	EditorScrollBox->AddChild(Controls);

	ProjectTattooShopEditorWidgetPrivate::AddSectionHeading(
		TargetWidgetTree,
		Controls,
		TEXT("LocationAccentText"),
		FText::FromString(TEXT("LOCATION")),
		0.0f);

	UHorizontalBox* PlacementRow = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlacementRow"));
	if (UVerticalBoxSlot* PlacementRowSlot = Controls->AddChildToVerticalBox(PlacementRow))
	{
		PlacementRowSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
	}

	UTextBlock* PlacementLabel = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlacementLabel"));
	PlacementLabel->SetText(FText::FromString(TEXT("Body area")));
	PlacementLabel->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(11, true));
	PlacementLabel->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
	if (UHorizontalBoxSlot* PlacementLabelSlot = PlacementRow->AddChildToHorizontalBox(PlacementLabel))
	{
		PlacementLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		PlacementLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		PlacementLabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	PlacementComboBox = TargetWidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("PlacementComboBox"));
	if (UHorizontalBoxSlot* PlacementComboSlot = PlacementRow->AddChildToHorizontalBox(PlacementComboBox))
	{
		PlacementComboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	USlider* SliderRaw = nullptr;
	UTextBlock* ValueRaw = nullptr;
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Offset X")), TEXT("OffsetXRow"), -30.0f, 30.0f, SliderRaw, ValueRaw);
	OffsetXSlider = SliderRaw;
	OffsetXValueText = ValueRaw;
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Offset Y")), TEXT("OffsetYRow"), -30.0f, 30.0f, SliderRaw, ValueRaw);
	OffsetYSlider = SliderRaw;
	OffsetYValueText = ValueRaw;

	ProjectTattooShopEditorWidgetPrivate::AddSectionHeading(
		TargetWidgetTree,
		Controls,
		TEXT("TransformAccentText"),
		FText::FromString(TEXT("TRANSFORM")));
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Size")), TEXT("SizeRow"), 1.0f, 50.0f, SliderRaw, ValueRaw);
	SizeSlider = SliderRaw;
	SizeValueText = ValueRaw;
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Rotation")), TEXT("RotationRow"), -180.0f, 180.0f, SliderRaw, ValueRaw);
	RotationSlider = SliderRaw;
	RotationValueText = ValueRaw;
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Projection")), TEXT("ProjectionRow"), 0.0f, 50.0f, SliderRaw, ValueRaw);
	ProjectionSlider = SliderRaw;
	ProjectionValueText = ValueRaw;

	ProjectTattooShopEditorWidgetPrivate::AddSectionHeading(
		TargetWidgetTree,
		Controls,
		TEXT("AppearanceAccentText"),
		FText::FromString(TEXT("APPEARANCE")));
	AddNumericRow(TargetWidgetTree, Controls, FText::FromString(TEXT("Opacity")), TEXT("OpacityRow"), 0.0f, 1.0f, SliderRaw, ValueRaw);
	OpacitySlider = SliderRaw;
	OpacityValueText = ValueRaw;

	TintEnabledCheckBox = TargetWidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("TintEnabledCheckBox"));
	TintEnabledCheckBox->SetContent(TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TintEnabledLabel")));
	if (UTextBlock* TintEnabledLabel = Cast<UTextBlock>(TintEnabledCheckBox->GetContent()))
	{
		TintEnabledLabel->SetText(FText::FromString(TEXT("Apply tint")));
		TintEnabledLabel->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(11));
		TintEnabledLabel->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
	}
	if (UVerticalBoxSlot* TintEnabledSlot = Controls->AddChildToVerticalBox(TintEnabledCheckBox))
	{
		TintEnabledSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 3.0f));
	}

	ColorPicker = TargetWidgetTree->ConstructWidget<UProjectTattooShopColorPickerWidget>(
		UProjectTattooShopColorPickerWidget::StaticClass(), TEXT("ColorPicker"));
	if (UVerticalBoxSlot* ColorPickerSlot = Controls->AddChildToVerticalBox(ColorPicker))
	{
		ColorPickerSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 7.0f));
	}

	EnabledCheckBox = TargetWidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("EnabledCheckBox"));
	EnabledCheckBox->SetContent(TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EnabledLabel")));
	if (UTextBlock* EnabledLabel = Cast<UTextBlock>(EnabledCheckBox->GetContent()))
	{
		EnabledLabel->SetText(FText::FromString(TEXT("Visible")));
		EnabledLabel->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(11));
		EnabledLabel->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
	}
	Controls->AddChildToVerticalBox(EnabledCheckBox);

	UTextBlock* FooterHintText = TargetWidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("EditorFooterHintText"));
	FooterHintText->SetText(FText::FromString(TEXT("Preview updates live. Accept to save these changes.")));
	FooterHintText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(9));
	FooterHintText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
	if (UVerticalBoxSlot* FooterHintSlot = RootBox->AddChildToVerticalBox(FooterHintText))
	{
		FooterHintSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 5.0f));
	}

	UHorizontalBox* Footer = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FooterBox"));
	if (UVerticalBoxSlot* FooterSlot = RootBox->AddChildToVerticalBox(Footer))
	{
		FooterSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	}

	USizeBox* AcceptSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AcceptButtonSizeBox"));
	AcceptSizeBox->SetHeightOverride(38.0f);
	AcceptButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AcceptButton"));
	UTextBlock* AcceptLabel = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AcceptLabel"));
	AcceptLabel->SetText(FText::FromString(TEXT("ACCEPT")));
	AcceptLabel->SetJustification(ETextJustify::Center);
	AcceptLabel->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(13, true));
	AcceptLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	AcceptButton->AddChild(AcceptLabel);
	AcceptButton->SetBackgroundColor(FLinearColor(0.0f, 0.72f, 0.10f, 1.0f));
	AcceptSizeBox->SetContent(AcceptButton);
	if (UHorizontalBoxSlot* AcceptSlot = Footer->AddChildToHorizontalBox(AcceptSizeBox))
	{
		AcceptSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		AcceptSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
	}

	USizeBox* CancelSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CancelButtonSizeBox"));
	CancelSizeBox->SetHeightOverride(38.0f);
	CancelButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CancelButton"));
	UTextBlock* CancelLabel = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CancelLabel"));
	CancelLabel->SetText(FText::FromString(TEXT("CANCEL")));
	CancelLabel->SetJustification(ETextJustify::Center);
	CancelLabel->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(13, true));
	CancelLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	CancelButton->AddChild(CancelLabel);
	CancelButton->SetBackgroundColor(EFProjectUIPalette::SectionFill(1.0f));
	CancelSizeBox->SetContent(CancelButton);
	if (UHorizontalBoxSlot* CancelSlot = Footer->AddChildToHorizontalBox(CancelSizeBox))
	{
		CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CancelSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
	}

	return true;
}

void UProjectTattooShopEditorWidget::AddNumericRow(
	UWidgetTree* TargetWidgetTree,
	UVerticalBox* Parent,
	const FText& Label,
	const FName RowName,
	const float MinValue,
	const float MaxValue,
	USlider*& OutSlider,
	UTextBlock*& OutValueText)
{
	UHorizontalBox* Row = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), RowName);
	if (UVerticalBoxSlot* RowSlot = Parent->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
	}

	USizeBox* LabelSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		*FString::Printf(TEXT("%sLabelSizeBox"), *RowName.ToString()));
	LabelSizeBox->SetWidthOverride(78.0f);
	UTextBlock* LabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		*FString::Printf(TEXT("%sLabel"), *RowName.ToString()));
	LabelText->SetText(Label);
	LabelText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(10, true));
	LabelText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
	LabelSizeBox->SetContent(LabelText);
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelSizeBox))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	FString ControlName = RowName.ToString();
	ControlName.RemoveFromEnd(TEXT("Row"));
	OutSlider = TargetWidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *FString::Printf(TEXT("%sSlider"), *ControlName));
	OutSlider->SetMinValue(MinValue);
	OutSlider->SetMaxValue(MaxValue);
	if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(OutSlider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
	}

	OutValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("%sValueText"), *ControlName));
	OutValueText->SetFont(ProjectTattooShopEditorWidgetPrivate::Font(10));
	OutValueText->SetJustification(ETextJustify::Right);
	OutValueText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
	USizeBox* ValueSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		*FString::Printf(TEXT("%sValueSizeBox"), *RowName.ToString()));
	ValueSizeBox->SetWidthOverride(42.0f);
	ValueSizeBox->SetContent(OutValueText);
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueSizeBox))
	{
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ValueSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UProjectTattooShopEditorWidget::BindControls()
{
	if (PlacementComboBox)
	{
		PlacementComboBox->OnSelectionChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandlePlacementChanged);
	}
	if (OffsetXSlider) OffsetXSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleOffsetXChanged);
	if (OffsetYSlider) OffsetYSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleOffsetYChanged);
	if (SizeSlider) SizeSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleSizeChanged);
	if (RotationSlider) RotationSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleRotationChanged);
	if (ProjectionSlider) ProjectionSlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleProjectionChanged);
	if (OpacitySlider) OpacitySlider->OnValueChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleOpacityChanged);
	if (EnabledCheckBox) EnabledCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleEnabledChanged);
	if (TintEnabledCheckBox) TintEnabledCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleTintEnabledChanged);
	if (ColorPicker) ColorPicker->OnColorChanged.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleColorChanged);
	if (AcceptButton) AcceptButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleAcceptClicked);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopEditorWidget::HandleCancelClicked);
}

void UProjectTattooShopEditorWidget::PopulatePlacementOptions()
{
	if (!PlacementComboBox || PlacementComboBox->GetOptionCount() > 0)
	{
		return;
	}

	for (const TCHAR* Option : ProjectTattooShopEditorWidgetPrivate::PlacementOptions)
	{
		PlacementComboBox->AddOption(Option);
	}
}

void UProjectTattooShopEditorWidget::RefreshControls()
{
	TGuardValue<bool> RefreshGuard(bRefreshingControls, true);
	PopulatePlacementOptions();

	const bool bEditable = !EditorModel.bReadOnly;
	if (TitleText)
	{
		TitleText->SetText(EditorModel.DisplayName.IsEmpty() ? FText::FromString(TEXT("TATTOO SETTINGS")) : EditorModel.DisplayName);
	}
	if (StatusText) StatusText->SetText(CurrentStatusText);
	if (ReadOnlyText) ReadOnlyText->SetVisibility(EditorModel.bReadOnly ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (PlacementComboBox)
	{
		PlacementComboBox->SetSelectedOption(PlacementPresetToString(EditorModel.PlacementPreset));
		PlacementComboBox->SetIsEnabled(bEditable);
	}

	auto ApplySlider = [bEditable](USlider* Slider, UTextBlock* ValueText, const float Value)
	{
		if (Slider)
		{
			Slider->SetValue(Value);
			Slider->SetIsEnabled(bEditable);
		}
		if (ValueText)
		{
			ValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(Value));
		}
	};

	ApplySlider(OffsetXSlider, OffsetXValueText, EditorModel.OffsetX);
	ApplySlider(OffsetYSlider, OffsetYValueText, EditorModel.OffsetY);
	ApplySlider(SizeSlider, SizeValueText, EditorModel.Size);
	ApplySlider(RotationSlider, RotationValueText, EditorModel.RotationDegrees);
	ApplySlider(ProjectionSlider, ProjectionValueText, EditorModel.ProjectionDistance);
	ApplySlider(OpacitySlider, OpacityValueText, EditorModel.Opacity);

	if (EnabledCheckBox)
	{
		EnabledCheckBox->SetIsChecked(EditorModel.bEnabled);
		EnabledCheckBox->SetIsEnabled(bEditable);
	}
	if (TintEnabledCheckBox)
	{
		TintEnabledCheckBox->SetIsChecked(EditorModel.bTintEnabled);
		TintEnabledCheckBox->SetIsEnabled(bEditable);
	}
	if (ColorPicker)
	{
		ColorPicker->SetColor(EditorModel.Color, false);
		ColorPicker->SetReadOnly(EditorModel.bReadOnly || !EditorModel.bTintEnabled);
		ColorPicker->SetVisibility(EditorModel.bTintEnabled
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	if (AcceptButton) AcceptButton->SetIsEnabled(bEditable && EditorModel.TattooId.IsValid());

	if (BackgroundBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush Brush(
			EFProjectUIPalette::PanelFill(0.94f), 2.0f,
			FSlateColor(EFProjectUIPalette::OutlineDim(0.66f)), 1.0f,
			FVector2f(420.0f, 680.0f));
		BackgroundBorder->SetBrush(Brush);
	}
}

void UProjectTattooShopEditorWidget::NotifyPreviewChanged()
{
	if (!bRefreshingControls && !EditorModel.bReadOnly && EditorModel.TattooId.IsValid())
	{
		OnPreviewChanged.Broadcast(EditorModel);
	}
}

void UProjectTattooShopEditorWidget::HandlePlacementChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingControls && !EditorModel.bReadOnly && TryStringToPlacementPreset(SelectedItem, EditorModel.PlacementPreset))
	{
		NotifyPreviewChanged();
	}
}

void UProjectTattooShopEditorWidget::HandleOffsetXChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.OffsetX = FMath::Clamp(Value, -30.0f, 30.0f); if (OffsetXValueText) OffsetXValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.OffsetX)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleOffsetYChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.OffsetY = FMath::Clamp(Value, -30.0f, 30.0f); if (OffsetYValueText) OffsetYValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.OffsetY)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleSizeChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.Size = FMath::Clamp(Value, 1.0f, 50.0f); if (SizeValueText) SizeValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.Size)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleRotationChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.RotationDegrees = FMath::Clamp(Value, -180.0f, 180.0f); if (RotationValueText) RotationValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.RotationDegrees)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleProjectionChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.ProjectionDistance = FMath::Clamp(Value, 0.0f, 50.0f); if (ProjectionValueText) ProjectionValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.ProjectionDistance)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleOpacityChanged(const float Value)
{
	if (!bRefreshingControls) { EditorModel.Opacity = FMath::Clamp(Value, 0.0f, 1.0f); if (OpacityValueText) OpacityValueText->SetText(ProjectTattooShopEditorWidgetPrivate::Number(EditorModel.Opacity)); NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleEnabledChanged(const bool bIsChecked)
{
	if (!bRefreshingControls) { EditorModel.bEnabled = bIsChecked; NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleTintEnabledChanged(const bool bIsChecked)
{
	if (!bRefreshingControls)
	{
		EditorModel.bTintEnabled = bIsChecked;
		if (ColorPicker)
		{
			ColorPicker->SetReadOnly(EditorModel.bReadOnly || !EditorModel.bTintEnabled);
			ColorPicker->SetVisibility(EditorModel.bTintEnabled
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
		}
		NotifyPreviewChanged();
	}
}
void UProjectTattooShopEditorWidget::HandleColorChanged(const FLinearColor Color)
{
	if (!bRefreshingControls) { EditorModel.Color = Color; NotifyPreviewChanged(); }
}
void UProjectTattooShopEditorWidget::HandleAcceptClicked()
{
	if (!EditorModel.bReadOnly && EditorModel.TattooId.IsValid()) OnAcceptRequested.Broadcast(EditorModel.TattooId);
}
void UProjectTattooShopEditorWidget::HandleCancelClicked()
{
	OnCancelRequested.Broadcast(EditorModel.TattooId);
}

FString UProjectTattooShopEditorWidget::PlacementPresetToString(const EProjectAutomaticTattooPlacementPreset Preset)
{
	const UEnum* Enum = StaticEnum<EProjectAutomaticTattooPlacementPreset>();
	if (!Enum)
	{
		return TEXT("ChestFront");
	}
	FString Name = Enum->GetNameStringByValue(static_cast<int64>(Preset));
	if (Name == TEXT("LeftUpperThigh")) Name = TEXT("LeftThigh");
	if (Name == TEXT("RightUpperThigh")) Name = TEXT("RightThigh");
	return Name;
}

bool UProjectTattooShopEditorWidget::TryStringToPlacementPreset(const FString& Value, EProjectAutomaticTattooPlacementPreset& OutPreset)
{
	const UEnum* Enum = StaticEnum<EProjectAutomaticTattooPlacementPreset>();
	if (!Enum)
	{
		return false;
	}
	const int64 EnumValue = Enum->GetValueByNameString(Value);
	if (EnumValue == INDEX_NONE)
	{
		return false;
	}
	OutPreset = static_cast<EProjectAutomaticTattooPlacementPreset>(EnumValue);
	return true;
}
