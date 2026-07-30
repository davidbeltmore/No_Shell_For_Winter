#include "UI/ProjectTargetStatsWidget.h"

#include "EFProjectUIPalette.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float ProjectTargetStatsPanelWidth = 284.0f;
	constexpr float ProjectTargetStatsPanelMinHeight = 230.0f;
	const FVector2D ProjectTargetStatsViewportPosition(8.0f, -306.0f);
	FLinearColor ProjectTargetStatsOuterColor()
	{
		return EFProjectUIPalette::PanelFillDeep(0.96f);
	}
	FLinearColor ProjectTargetStatsInnerColor()
	{
		return EFProjectUIPalette::PanelFill(0.98f);
	}
	FLinearColor ProjectTargetStatsFrameColor()
	{
		return EFProjectUIPalette::Outline(0.82f);
	}
	FLinearColor ProjectTargetStatsTitleColor()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor ProjectTargetStatsLabelColor()
	{
		return EFProjectUIPalette::SecondaryText();
	}
	FLinearColor ProjectTargetStatsValueColor()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor ProjectTargetStatsMutedValueColor()
	{
		return EFProjectUIPalette::MutedText(0.74f);
	}

	FSlateFontInfo MakeTargetStatsFont(const TCHAR* Weight, const int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	FText BuildCombatValueText(const FProjectEnemyCombatStatRow& Row)
	{
		if (!Row.ValueOverride.IsEmpty())
		{
			return Row.ValueOverride;
		}

		if (!Row.bIsAvailable)
		{
			return FText::FromString(TEXT("--"));
		}

		return FText::FromString(FString::Printf(TEXT("%.0f -> %.0f"), Row.BaseValue, Row.FinalValue));
	}
}

class SProjectTargetStatsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectTargetStatsPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(ProjectTargetStatsPanelWidth)
			.MinDesiredHeight(ProjectTargetStatsPanelMinHeight)
			[
				SAssignNew(FrameBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ProjectTargetStatsFrameColor())
				.Padding(FMargin(2.0f))
				[
					SAssignNew(OuterBorder, SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ProjectTargetStatsOuterColor())
					.Padding(FMargin(5.0f))
					[
						SAssignNew(InnerBorder, SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(ProjectTargetStatsInnerColor())
						.Padding(FMargin(8.0f, 8.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(TitleText, STextBlock)
								.Text(FText::FromString(TEXT("Social Card")))
								.Font(MakeTargetStatsFont(TEXT("Bold"), 13))
								.ColorAndOpacity(ProjectTargetStatsTitleColor())
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
							[
								SAssignNew(RowsBox, SVerticalBox)
							]
						]
					]
				]
			]
		];
	}

	void SetCombatStatSnapshot(const FProjectEnemyCombatStatSnapshot& Snapshot)
	{
		if (!RowsBox.IsValid())
		{
			return;
		}

		RowsBox->ClearChildren();
		RowWidgets.Reset();
		for (const FProjectEnemyCombatStatRow& Row : Snapshot.Rows)
		{
			RowsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(6.0f, 1.0f))
			[
				BuildRow(Row)
			];
		}

		ApplyTheme();
	}

	void ApplyTheme()
	{
		if (FrameBorder.IsValid())
		{
			FrameBorder->SetBorderBackgroundColor(ProjectTargetStatsFrameColor());
		}
		if (OuterBorder.IsValid())
		{
			OuterBorder->SetBorderBackgroundColor(ProjectTargetStatsOuterColor());
		}
		if (InnerBorder.IsValid())
		{
			InnerBorder->SetBorderBackgroundColor(ProjectTargetStatsInnerColor());
		}
		if (TitleText.IsValid())
		{
			TitleText->SetColorAndOpacity(ProjectTargetStatsTitleColor());
		}
		for (const FRowWidgets& Row : RowWidgets)
		{
			if (Row.LabelText.IsValid())
			{
				Row.LabelText->SetColorAndOpacity(ProjectTargetStatsLabelColor());
			}
			if (Row.ValueText.IsValid())
			{
				Row.ValueText->SetColorAndOpacity(
					Row.bIsAvailable
						? ProjectTargetStatsValueColor()
						: ProjectTargetStatsMutedValueColor());
			}
		}
	}

private:
	struct FRowWidgets
	{
		TSharedPtr<STextBlock> LabelText;
		TSharedPtr<STextBlock> ValueText;
		bool bIsAvailable = false;
	};

	TSharedRef<SWidget> BuildRow(const FProjectEnemyCombatStatRow& Row)
	{
		TSharedPtr<STextBlock> LabelText;
		TSharedPtr<STextBlock> ValueText;

		TSharedRef<SWidget> RowWidget =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.55f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 5.0f, 0.0f))
			[
				SAssignNew(LabelText, STextBlock)
				.Text(Row.Label.IsEmpty() ? FText::FromString(TEXT("--")) : Row.Label)
				.Font(MakeTargetStatsFont(TEXT("Bold"), 12))
				.ColorAndOpacity(ProjectTargetStatsLabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
			[
				SAssignNew(ValueText, STextBlock)
				.Text(BuildCombatValueText(Row))
				.Font(MakeTargetStatsFont(TEXT("Regular"), 12))
				.ColorAndOpacity(Row.bIsAvailable ? ProjectTargetStatsValueColor() : ProjectTargetStatsMutedValueColor())
				.Justification(ETextJustify::Left)
			];

		FRowWidgets& WidgetRefs = RowWidgets.AddDefaulted_GetRef();
		WidgetRefs.LabelText = MoveTemp(LabelText);
		WidgetRefs.ValueText = MoveTemp(ValueText);
		WidgetRefs.bIsAvailable = Row.bIsAvailable;
		return RowWidget;
	}

private:
	TSharedPtr<SBorder> FrameBorder;
	TSharedPtr<SBorder> OuterBorder;
	TSharedPtr<SBorder> InnerBorder;
	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<SVerticalBox> RowsBox;
	TArray<FRowWidgets> RowWidgets;
};

UProjectTargetStatsWidget::UProjectTargetStatsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CachedSnapshot.Rows.Reserve(16);
}

TSharedRef<SWidget> UProjectTargetStatsWidget::RebuildWidget()
{
	SAssignNew(StatsPanel, SProjectTargetStatsPanel);
	ApplyCachedSnapshot();
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	return StatsPanel.ToSharedRef();
}

void UProjectTargetStatsWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	StatsPanel.Reset();
}

void UProjectTargetStatsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyFixedViewportPlacement();
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ApplyCachedSnapshot();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTargetStatsWidget::SetCombatStatSnapshot(const FProjectEnemyCombatStatSnapshot& InSnapshot)
{
	CachedSnapshot = InSnapshot;
	ApplyCachedSnapshot();
}

void UProjectTargetStatsWidget::SetScreenPosition(const FVector2D& InScreenPosition)
{
	SetPositionInViewport(InScreenPosition, false);
}

void UProjectTargetStatsWidget::SetOverlayVisible(const bool bVisible)
{
	bOverlayVisible = bVisible;
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (StatsPanel.IsValid())
	{
		StatsPanel->SetVisibility(bOverlayVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

bool UProjectTargetStatsWidget::IsOverlayVisible() const
{
	return bOverlayVisible;
}

void UProjectTargetStatsWidget::ApplyCachedSnapshot()
{
	if (StatsPanel.IsValid())
	{
		StatsPanel->SetCombatStatSnapshot(CachedSnapshot);
	}
}

void UProjectTargetStatsWidget::ApplyFixedViewportPlacement()
{
	SetDesiredSizeInViewport(FVector2D(ProjectTargetStatsPanelWidth, ProjectTargetStatsPanelMinHeight));
	SetAnchorsInViewport(FAnchors(0.0f, 1.0f));
	SetAlignmentInViewport(FVector2D(0.0f, 1.0f));
	SetPositionInViewport(ProjectTargetStatsViewportPosition, false);
}

void UProjectTargetStatsWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (StatsPanel.IsValid())
	{
		StatsPanel->ApplyTheme();
	}
}
