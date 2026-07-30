#include "Debug/ProjectAutomaticTattooTunerWidget.h"

#include "EFProjectUIPalette.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/DefaultValueHelper.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace ProjectAutomaticTattooTunerWidgetPrivate
{
	static constexpr float PanelWidth = 470.0f;
	static constexpr float PanelRightPadding = 32.0f;
	static constexpr float CameraZonePadding = PanelWidth + PanelRightPadding + 32.0f;

	static FSlateFontInfo TitleFont()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22);
	}

	static FSlateFontInfo SectionFont()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
	}

	static FSlateFontInfo BodyFont()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12);
	}

	static FSlateFontInfo SmallFont()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
	}

	static FSlateColor TextColor()
	{
		return FSlateColor(EFProjectUIPalette::PrimaryText());
	}

	static FSlateColor MutedTextColor()
	{
		return FSlateColor(EFProjectUIPalette::SecondaryText());
	}

	static FLinearColor PanelColor()
	{
		return EFProjectUIPalette::PanelFill(0.88f);
	}

	static FLinearColor AccentColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}

	static FSlateColor DeepPanelColor()
	{
		return FSlateColor(EFProjectUIPalette::PanelFillDeep(0.72f));
	}

	static FSlateColor PanelSlateColor()
	{
		return FSlateColor(PanelColor());
	}

	static FSlateColor AccentSlateColor()
	{
		return FSlateColor(AccentColor());
	}

	static FSlateColor SeparatorColor()
	{
		return FSlateColor(EFProjectUIPalette::OutlineDim(0.58f));
	}

	static FSlateColor ButtonColor()
	{
		return FSlateColor(EFProjectUIPalette::SectionFill(0.96f));
	}

	static FSlateColor SliderBarColor()
	{
		return FSlateColor(EFProjectUIPalette::OutlineDim(0.72f));
	}

	static FSlateColor SliderHandleColor()
	{
		return FSlateColor(EFProjectUIPalette::AccentSoft());
	}

	static FSlateColor InputBackgroundColor()
	{
		return FSlateColor(EFProjectUIPalette::PanelFillDeep(0.94f));
	}

	static FSlateColor CheckBoxBorderColor()
	{
		return FSlateColor(EFProjectUIPalette::Outline(0.82f));
	}

	static FString FormatFloat(const float Value)
	{
		return FString::Printf(TEXT("%.3f"), Value);
	}
}

UProjectAutomaticTattooTunerWidget::UProjectAutomaticTattooTunerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	StatusText = FText::FromString(TEXT("AT preview changes are temporary until Apply."));
}

void UProjectAutomaticTattooTunerWidget::InitializeForTattoo(const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot)
{
	InitialSnapshot = Snapshot;
	RowName = Snapshot.RowName;
	EditedRow = Snapshot.EffectiveRow;
	bHasTattoo = !RowName.IsNone();
	PopulatePresetOptions();
	RefreshAllControls();
}

void UProjectAutomaticTattooTunerWidget::SetStatusText(const FText& NewStatusText)
{
	StatusText = NewStatusText;
	if (StatusTextBlock)
	{
		StatusTextBlock->SetText(StatusText);
	}
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::RebuildWidget()
{
	PopulatePresetOptions();

	return SAssignNew(ThemeRootWidget, SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f))
			.Padding(0.0f)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(32.0f, 0.0f, 0.0f, 32.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
			.BorderBackgroundColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::DeepPanelColor();
				})
			.Padding(FMargin(12.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					BuildViewButton(FText::FromString(TEXT("Front")), EProjectAutomaticTattooTunerCameraView::Front)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					BuildViewButton(FText::FromString(TEXT("Back")), EProjectAutomaticTattooTunerCameraView::Back)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					BuildViewButton(FText::FromString(TEXT("Left")), EProjectAutomaticTattooTunerCameraView::Left)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					BuildViewButton(FText::FromString(TEXT("Right")), EProjectAutomaticTattooTunerCameraView::Right)
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.0f, 32.0f, ProjectAutomaticTattooTunerWidgetPrivate::PanelRightPadding, 32.0f))
		[
			SNew(SBox)
			.WidthOverride(ProjectAutomaticTattooTunerWidgetPrivate::PanelWidth)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
				.BorderBackgroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::PanelSlateColor();
					})
				.Padding(FMargin(22.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildHeader()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 16.0f, 0.0f, 16.0f)
					[
						SNew(SSeparator)
						.ColorAndOpacity_Lambda([]()
							{
								return ProjectAutomaticTattooTunerWidgetPrivate::SeparatorColor();
							})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildReadOnlyDetails()
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 18.0f, 0.0f, 18.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							BuildPlacementControls()
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildFooter()
					]
				]
			]
		];
}

void UProjectAutomaticTattooTunerWidget::ReleaseSlateResources(
	const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	ThemeRootWidget.Reset();
	PresetComboBox.Reset();
	PresetComboText.Reset();
	StatusTextBlock.Reset();
	OffsetXSlider.Reset();
	OffsetYSlider.Reset();
	SizeSlider.Reset();
	RotationSlider.Reset();
	ProjectionSlider.Reset();
	OffsetXTextBox.Reset();
	OffsetYTextBox.Reset();
	SizeTextBox.Reset();
	RotationTextBox.Reset();
	ProjectionTextBox.Reset();
	EnabledCheckBox.Reset();
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildHeader()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("AT AUTOMATIC TATTOO TUNER")))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::TitleFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromName(RowName))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SectionFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::AccentSlateColor();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SAssignNew(StatusTextBlock, STextBlock)
			.Text(StatusText)
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildReadOnlyDetails() const
{
	const FString TexturePath = InitialSnapshot.TattooTexturePath.IsEmpty()
		? TEXT("None")
		: InitialSnapshot.TattooTexturePath;
	const FString UnlockState = InitialSnapshot.bActive ? TEXT("Active") : TEXT("Locked");
	const FString ForcedState = InitialSnapshot.bForcedActiveForDebug ? TEXT("Forced") : TEXT("Normal");

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Read-only AT row data")))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SectionFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Texture: %s"), *TexturePath)))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("State: %s | Debug force: %s"), *UnlockState, *ForcedState)))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(InitialSnapshot.DataTableRow.UnlockDescription))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildPlacementControls()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Placement")))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SectionFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 10.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("PlacementPreset")))
				.Font(ProjectAutomaticTattooTunerWidgetPrivate::BodyFont())
				.ColorAndOpacity_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(PresetComboBox, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&PresetOptions)
				.InitiallySelectedItem(FindPresetOption(EditedRow.PlacementPreset))
				.ForegroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
				{
					return SNew(STextBlock)
						.Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
						.Font(ProjectAutomaticTattooTunerWidgetPrivate::BodyFont())
						.ColorAndOpacity_Lambda([]()
							{
								return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
							});
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo)
				{
					HandlePresetSelectionChanged(SelectedItem, SelectInfo);
				})
				[
					SAssignNew(PresetComboText, STextBlock)
					.Text(FText::FromString(PlacementPresetToString(EditedRow.PlacementPreset)))
					.Font(ProjectAutomaticTattooTunerWidgetPrivate::BodyFont())
					.ColorAndOpacity_Lambda([]()
						{
							return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
						})
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildNumericRow(FText::FromString(TEXT("OffsetX")), ENumericField::OffsetX, -30.0f, 30.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildNumericRow(FText::FromString(TEXT("OffsetY")), ENumericField::OffsetY, -30.0f, 30.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildNumericRow(FText::FromString(TEXT("Size")), ENumericField::Size, 1.0f, 50.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildNumericRow(FText::FromString(TEXT("RotationDegrees")), ENumericField::RotationDegrees, -180.0f, 180.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildNumericRow(FText::FromString(TEXT("ProjectionDistance")), ENumericField::ProjectionDistance, 0.0f, 50.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(EnabledCheckBox, SCheckBox)
				.IsChecked(EditedRow.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.ForegroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::AccentSlateColor();
					})
				.BorderBackgroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::CheckBoxBorderColor();
					})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					HandleEnabledChanged(NewState);
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Enabled")))
				.Font(ProjectAutomaticTattooTunerWidgetPrivate::BodyFont())
				.ColorAndOpacity_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
			]
		];
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildNumericRow(
	const FText& Label,
	const ENumericField Field,
	const float MinValue,
	const float MaxValue)
{
	TSharedPtr<SSlider>* TargetSlider = nullptr;
	TSharedPtr<SEditableTextBox>* TargetTextBox = nullptr;
	switch (Field)
	{
	case ENumericField::OffsetX:
		TargetSlider = &OffsetXSlider;
		TargetTextBox = &OffsetXTextBox;
		break;
	case ENumericField::OffsetY:
		TargetSlider = &OffsetYSlider;
		TargetTextBox = &OffsetYTextBox;
		break;
	case ENumericField::Size:
		TargetSlider = &SizeSlider;
		TargetTextBox = &SizeTextBox;
		break;
	case ENumericField::RotationDegrees:
		TargetSlider = &RotationSlider;
		TargetTextBox = &RotationTextBox;
		break;
	case ENumericField::ProjectionDistance:
		TargetSlider = &ProjectionSlider;
		TargetTextBox = &ProjectionTextBox;
		break;
	default:
		break;
	}

	const float InitialValue = ClampNumericFieldValue(Field, GetNumericFieldValue(Field));

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 6.0f, 12.0f, 6.0f)
		[
			SNew(SBox)
			.WidthOverride(132.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(ProjectAutomaticTattooTunerWidgetPrivate::BodyFont())
				.ColorAndOpacity_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SAssignNew(*TargetSlider, SSlider)
			.MinValue(MinValue)
			.MaxValue(MaxValue)
			.Value(InitialValue)
			.SliderBarColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::SliderBarColor();
				})
			.SliderHandleColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::SliderHandleColor();
				})
			.OnValueChanged_Lambda([this, Field](float NewValue)
			{
				if (!bRefreshingControls)
				{
					SetNumericField(Field, NewValue, true);
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(*TargetTextBox, SEditableTextBox)
			.MinDesiredWidth(82.0f)
			.Text(FText::FromString(ProjectAutomaticTattooTunerWidgetPrivate::FormatFloat(InitialValue)))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ForegroundColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
				})
			.FocusedForegroundColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
				})
			.ReadOnlyForegroundColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
			.BackgroundColor_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::InputBackgroundColor();
				})
			.OnTextCommitted_Lambda([this, Field](const FText& Text, ETextCommit::Type CommitType)
			{
				HandleNumericTextCommitted(Text, CommitType, Field);
			})
		];
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildFooter()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Drag empty viewport to orbit. Right-drag to pan. Mouse wheel zooms.")))
			.Font(ProjectAutomaticTattooTunerWidgetPrivate::SmallFont())
			.ColorAndOpacity_Lambda([]()
				{
					return ProjectAutomaticTattooTunerWidgetPrivate::MutedTextColor();
				})
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(4.0f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(FText::FromString(TEXT("Apply")))
				.ButtonColorAndOpacity_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::ButtonColor();
					})
				.ForegroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
				.OnClicked_Lambda([this]()
				{
					OnApplyRequested.Broadcast(EditedRow);
					return FReply::Handled();
				})
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(FText::FromString(TEXT("Cancel")))
				.ButtonColorAndOpacity_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::ButtonColor();
					})
				.ForegroundColor_Lambda([]()
					{
						return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
					})
				.OnClicked_Lambda([this]()
				{
					OnCancelRequested.Broadcast();
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> UProjectAutomaticTattooTunerWidget::BuildViewButton(
	const FText& Label,
	const EProjectAutomaticTattooTunerCameraView View)
{
	return SNew(SButton)
		.Text(Label)
		.ButtonColorAndOpacity_Lambda([]()
			{
				return ProjectAutomaticTattooTunerWidgetPrivate::ButtonColor();
			})
		.ForegroundColor_Lambda([]()
			{
				return ProjectAutomaticTattooTunerWidgetPrivate::TextColor();
			})
		.OnClicked_Lambda([this, View]()
		{
			OnViewRequested.Broadcast(View);
			return FReply::Handled();
	});
}

void UProjectAutomaticTattooTunerWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (ThemeRootWidget.IsValid())
	{
		ThemeRootWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply UProjectAutomaticTattooTunerWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (!IsPointerInCameraZone(InGeometry, InMouseEvent))
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bDraggingCamera = true;
		bPanningCamera = InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
		LastPointerScreenPosition = InMouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UProjectAutomaticTattooTunerWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (bDraggingCamera && (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
	{
		bDraggingCamera = false;
		bPanningCamera = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UProjectAutomaticTattooTunerWidget::NativeOnMouseMove(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (!bDraggingCamera)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}

	const FVector2D CurrentPointerPosition = InMouseEvent.GetScreenSpacePosition();
	const FVector2D PointerDelta = CurrentPointerPosition - LastPointerScreenPosition;
	LastPointerScreenPosition = CurrentPointerPosition;

	if (bPanningCamera)
	{
		OnPanRequested.Broadcast(PointerDelta);
	}
	else
	{
		OnOrbitRequested.Broadcast(PointerDelta);
	}

	return FReply::Handled();
}

FReply UProjectAutomaticTattooTunerWidget::NativeOnMouseWheel(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (IsPointerInCameraZone(InGeometry, InMouseEvent))
	{
		OnZoomRequested.Broadcast(InMouseEvent.GetWheelDelta());
		return FReply::Handled();
	}

	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UProjectAutomaticTattooTunerWidget::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		OnCancelRequested.Broadcast();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectAutomaticTattooTunerWidget::RefreshAllControls()
{
	if (!bHasTattoo)
	{
		return;
	}

	TGuardValue<bool> RefreshGuard(bRefreshingControls, true);
	if (PresetComboBox)
	{
		PresetComboBox->SetSelectedItem(FindPresetOption(EditedRow.PlacementPreset));
	}
	if (PresetComboText)
	{
		PresetComboText->SetText(FText::FromString(PlacementPresetToString(EditedRow.PlacementPreset)));
	}
	if (EnabledCheckBox)
	{
		EnabledCheckBox->SetIsChecked(EditedRow.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
	}

	RefreshNumericControl(ENumericField::OffsetX);
	RefreshNumericControl(ENumericField::OffsetY);
	RefreshNumericControl(ENumericField::Size);
	RefreshNumericControl(ENumericField::RotationDegrees);
	RefreshNumericControl(ENumericField::ProjectionDistance);
}

void UProjectAutomaticTattooTunerWidget::RefreshNumericControl(const ENumericField Field)
{
	const float Value = ClampNumericFieldValue(Field, GetNumericFieldValue(Field));
	TSharedPtr<SSlider> Slider;
	TSharedPtr<SEditableTextBox> TextBox;

	switch (Field)
	{
	case ENumericField::OffsetX:
		Slider = OffsetXSlider;
		TextBox = OffsetXTextBox;
		break;
	case ENumericField::OffsetY:
		Slider = OffsetYSlider;
		TextBox = OffsetYTextBox;
		break;
	case ENumericField::Size:
		Slider = SizeSlider;
		TextBox = SizeTextBox;
		break;
	case ENumericField::RotationDegrees:
		Slider = RotationSlider;
		TextBox = RotationTextBox;
		break;
	case ENumericField::ProjectionDistance:
		Slider = ProjectionSlider;
		TextBox = ProjectionTextBox;
		break;
	default:
		break;
	}

	if (Slider)
	{
		Slider->SetValue(Value);
	}
	if (TextBox)
	{
		TextBox->SetText(FText::FromString(ProjectAutomaticTattooTunerWidgetPrivate::FormatFloat(Value)));
	}
}

void UProjectAutomaticTattooTunerWidget::SetNumericField(
	const ENumericField Field,
	const float NewValue,
	const bool bUpdateControl)
{
	const float ClampedValue = ClampNumericFieldValue(Field, NewValue);
	switch (Field)
	{
	case ENumericField::OffsetX:
		EditedRow.OffsetX = ClampedValue;
		break;
	case ENumericField::OffsetY:
		EditedRow.OffsetY = ClampedValue;
		break;
	case ENumericField::Size:
		EditedRow.Size = ClampedValue;
		break;
	case ENumericField::RotationDegrees:
		EditedRow.RotationDegrees = ClampedValue;
		break;
	case ENumericField::ProjectionDistance:
		EditedRow.ProjectionDistance = ClampedValue;
		break;
	default:
		break;
	}

	if (bUpdateControl)
	{
		TGuardValue<bool> RefreshGuard(bRefreshingControls, true);
		RefreshNumericControl(Field);
	}

	NotifyPreviewChanged();
}

float UProjectAutomaticTattooTunerWidget::GetNumericFieldValue(const ENumericField Field) const
{
	switch (Field)
	{
	case ENumericField::OffsetX:
		return EditedRow.OffsetX;
	case ENumericField::OffsetY:
		return EditedRow.OffsetY;
	case ENumericField::Size:
		return EditedRow.Size;
	case ENumericField::RotationDegrees:
		return EditedRow.RotationDegrees;
	case ENumericField::ProjectionDistance:
		return EditedRow.ProjectionDistance;
	default:
		return 0.0f;
	}
}

float UProjectAutomaticTattooTunerWidget::ClampNumericFieldValue(const ENumericField Field, const float Value) const
{
	switch (Field)
	{
	case ENumericField::OffsetX:
	case ENumericField::OffsetY:
		return FMath::Clamp(Value, -30.0f, 30.0f);
	case ENumericField::Size:
		return FMath::Clamp(Value, 1.0f, 50.0f);
	case ENumericField::RotationDegrees:
		return FMath::Clamp(Value, -180.0f, 180.0f);
	case ENumericField::ProjectionDistance:
		return FMath::Clamp(Value, 0.0f, 50.0f);
	default:
		return Value;
	}
}

void UProjectAutomaticTattooTunerWidget::HandleNumericTextCommitted(
	const FText& Text,
	const ETextCommit::Type CommitType,
	const ENumericField Field)
{
	if (CommitType == ETextCommit::OnCleared)
	{
		RefreshNumericControl(Field);
		return;
	}

	float ParsedValue = 0.0f;
	if (!FDefaultValueHelper::ParseFloat(Text.ToString(), ParsedValue))
	{
		RefreshNumericControl(Field);
		return;
	}

	SetNumericField(Field, ParsedValue, true);
}

void UProjectAutomaticTattooTunerWidget::HandlePresetSelectionChanged(
	TSharedPtr<FString> SelectedItem,
	ESelectInfo::Type SelectInfo)
{
	if (bRefreshingControls || !SelectedItem.IsValid())
	{
		return;
	}

	EProjectAutomaticTattooPlacementPreset ParsedPreset = EditedRow.PlacementPreset;
	if (TryStringToPlacementPreset(*SelectedItem, ParsedPreset))
	{
		EditedRow.PlacementPreset = ParsedPreset;
		if (PresetComboText)
		{
			PresetComboText->SetText(FText::FromString(PlacementPresetToString(EditedRow.PlacementPreset)));
		}
		NotifyPreviewChanged();
	}
}

void UProjectAutomaticTattooTunerWidget::HandleEnabledChanged(const ECheckBoxState NewState)
{
	if (bRefreshingControls)
	{
		return;
	}

	EditedRow.bEnabled = NewState == ECheckBoxState::Checked;
	NotifyPreviewChanged();
}

void UProjectAutomaticTattooTunerWidget::NotifyPreviewChanged()
{
	if (bHasTattoo && !bRefreshingControls)
	{
		OnPreviewRowChanged.Broadcast(EditedRow);
	}
}

bool UProjectAutomaticTattooTunerWidget::IsPointerInCameraZone(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent) const
{
	const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const FVector2D LocalSize = InGeometry.GetLocalSize();
	return LocalPosition.X >= 0.0f
		&& LocalPosition.Y >= 0.0f
		&& LocalPosition.X <= LocalSize.X - ProjectAutomaticTattooTunerWidgetPrivate::CameraZonePadding
		&& LocalPosition.Y <= LocalSize.Y;
}

void UProjectAutomaticTattooTunerWidget::PopulatePresetOptions()
{
	if (!PresetOptions.IsEmpty())
	{
		return;
	}

	PresetOptions.Add(MakeShared<FString>(TEXT("ChestFront")));
	PresetOptions.Add(MakeShared<FString>(TEXT("AbdomenFront")));
	PresetOptions.Add(MakeShared<FString>(TEXT("PelvisFront")));
	PresetOptions.Add(MakeShared<FString>(TEXT("UpperBack")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LowerBack")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftUpperArm")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightUpperArm")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftForearm")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightForearm")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftThigh")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightThigh")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftBackThigh")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightBackThigh")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftCalf")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightCalf")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftBackCalf")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightBackCalf")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftHand")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightHand")));
	PresetOptions.Add(MakeShared<FString>(TEXT("LeftFoot")));
	PresetOptions.Add(MakeShared<FString>(TEXT("RightFoot")));
}

TSharedPtr<FString> UProjectAutomaticTattooTunerWidget::FindPresetOption(
	const EProjectAutomaticTattooPlacementPreset Preset) const
{
	const FString PresetString = PlacementPresetToString(Preset);
	for (const TSharedPtr<FString>& Option : PresetOptions)
	{
		if (Option.IsValid() && *Option == PresetString)
		{
			return Option;
		}
	}

	return PresetOptions.IsEmpty() ? nullptr : PresetOptions[0];
}

FString UProjectAutomaticTattooTunerWidget::PlacementPresetToString(
	const EProjectAutomaticTattooPlacementPreset Preset)
{
	switch (Preset)
	{
	case EProjectAutomaticTattooPlacementPreset::ChestFront:
		return TEXT("ChestFront");
	case EProjectAutomaticTattooPlacementPreset::AbdomenFront:
		return TEXT("AbdomenFront");
	case EProjectAutomaticTattooPlacementPreset::PelvisFront:
		return TEXT("PelvisFront");
	case EProjectAutomaticTattooPlacementPreset::UpperBack:
		return TEXT("UpperBack");
	case EProjectAutomaticTattooPlacementPreset::LowerBack:
		return TEXT("LowerBack");
	case EProjectAutomaticTattooPlacementPreset::LeftUpperArm:
		return TEXT("LeftUpperArm");
	case EProjectAutomaticTattooPlacementPreset::RightUpperArm:
		return TEXT("RightUpperArm");
	case EProjectAutomaticTattooPlacementPreset::LeftForearm:
		return TEXT("LeftForearm");
	case EProjectAutomaticTattooPlacementPreset::RightForearm:
		return TEXT("RightForearm");
	case EProjectAutomaticTattooPlacementPreset::LeftThigh:
		return TEXT("LeftThigh");
	case EProjectAutomaticTattooPlacementPreset::RightThigh:
		return TEXT("RightThigh");
	case EProjectAutomaticTattooPlacementPreset::LeftUpperThigh:
		return TEXT("LeftThigh");
	case EProjectAutomaticTattooPlacementPreset::RightUpperThigh:
		return TEXT("RightThigh");
	case EProjectAutomaticTattooPlacementPreset::LeftBackThigh:
		return TEXT("LeftBackThigh");
	case EProjectAutomaticTattooPlacementPreset::RightBackThigh:
		return TEXT("RightBackThigh");
	case EProjectAutomaticTattooPlacementPreset::LeftCalf:
		return TEXT("LeftCalf");
	case EProjectAutomaticTattooPlacementPreset::RightCalf:
		return TEXT("RightCalf");
	case EProjectAutomaticTattooPlacementPreset::LeftBackCalf:
		return TEXT("LeftBackCalf");
	case EProjectAutomaticTattooPlacementPreset::RightBackCalf:
		return TEXT("RightBackCalf");
	case EProjectAutomaticTattooPlacementPreset::LeftHand:
		return TEXT("LeftHand");
	case EProjectAutomaticTattooPlacementPreset::RightHand:
		return TEXT("RightHand");
	case EProjectAutomaticTattooPlacementPreset::LeftFoot:
		return TEXT("LeftFoot");
	case EProjectAutomaticTattooPlacementPreset::RightFoot:
		return TEXT("RightFoot");
	default:
		return TEXT("ChestFront");
	}
}

bool UProjectAutomaticTattooTunerWidget::TryStringToPlacementPreset(
	const FString& Value,
	EProjectAutomaticTattooPlacementPreset& OutPreset)
{
	if (Value == TEXT("ChestFront"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;
		return true;
	}
	if (Value == TEXT("AbdomenFront"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::AbdomenFront;
		return true;
	}
	if (Value == TEXT("PelvisFront"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::PelvisFront;
		return true;
	}
	if (Value == TEXT("UpperBack"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::UpperBack;
		return true;
	}
	if (Value == TEXT("LowerBack"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LowerBack;
		return true;
	}
	if (Value == TEXT("LeftUpperArm"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftUpperArm;
		return true;
	}
	if (Value == TEXT("RightUpperArm"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightUpperArm;
		return true;
	}
	if (Value == TEXT("LeftForearm"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftForearm;
		return true;
	}
	if (Value == TEXT("RightForearm"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightForearm;
		return true;
	}
	if (Value == TEXT("LeftThigh"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftThigh;
		return true;
	}
	if (Value == TEXT("RightThigh"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightThigh;
		return true;
	}
	if (Value == TEXT("LeftBackThigh"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftBackThigh;
		return true;
	}
	if (Value == TEXT("RightBackThigh"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightBackThigh;
		return true;
	}
	if (Value == TEXT("LeftCalf"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftCalf;
		return true;
	}
	if (Value == TEXT("RightCalf"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightCalf;
		return true;
	}
	if (Value == TEXT("LeftBackCalf"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftBackCalf;
		return true;
	}
	if (Value == TEXT("RightBackCalf"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightBackCalf;
		return true;
	}
	if (Value == TEXT("LeftHand"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftHand;
		return true;
	}
	if (Value == TEXT("RightHand"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightHand;
		return true;
	}
	if (Value == TEXT("LeftFoot"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::LeftFoot;
		return true;
	}
	if (Value == TEXT("RightFoot"))
	{
		OutPreset = EProjectAutomaticTattooPlacementPreset::RightFoot;
		return true;
	}

	return false;
}
