#include "UI/ProjectSocialCardWidget.h"

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
	constexpr float ProjectSocialCardPanelWidth = 400.0f;
	constexpr float ProjectSocialCardPanelMinHeight = 276.0f;
	const FVector2D ProjectSocialCardViewportPosition(8.0f, 270.0f);

	FLinearColor ProjectSocialCardOuterColor()
	{
		return EFProjectUIPalette::PanelFillDeep(0.96f);
	}
	FLinearColor ProjectSocialCardInnerColor()
	{
		return EFProjectUIPalette::PanelFill(0.98f);
	}
	FLinearColor ProjectSocialCardFrameColor()
	{
		return EFProjectUIPalette::Outline(0.82f);
	}
	FLinearColor ProjectSocialCardTitleColor()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor ProjectSocialCardLabelColor()
	{
		return EFProjectUIPalette::SecondaryText();
	}
	FLinearColor ProjectSocialCardValueColor()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor ProjectSocialCardMutedValueColor()
	{
		return EFProjectUIPalette::MutedText(0.74f);
	}

	FSlateFontInfo MakeSocialCardFont(const TCHAR* Weight, const int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	FText BuildSocialCardValueText(const FProjectEnemyCombatStatRow& Row)
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

class SProjectSocialCardPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectSocialCardPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(ProjectSocialCardPanelWidth)
			.MinDesiredHeight(ProjectSocialCardPanelMinHeight)
			[
				SAssignNew(FrameBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ProjectSocialCardFrameColor())
				.Padding(FMargin(2.0f))
				[
					SAssignNew(OuterBorder, SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ProjectSocialCardOuterColor())
					.Padding(FMargin(4.0f))
					[
						SAssignNew(InnerBorder, SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(ProjectSocialCardInnerColor())
						.Padding(FMargin(6.0f, 6.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(TitleText, STextBlock)
								.Text(FText::FromString(TEXT("Social Card")))
								.Font(MakeSocialCardFont(TEXT("Bold"), 14))
								.ColorAndOpacity(ProjectSocialCardTitleColor())
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
							[
								SAssignNew(RowsBox, SVerticalBox)
							]
						]
					]
				]
			]
		];
	}

	void SetSocialCardSnapshot(const FProjectSocialCardSnapshot& Snapshot)
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
			.Padding(FMargin(3.0f, 0.0f))
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
			FrameBorder->SetBorderBackgroundColor(ProjectSocialCardFrameColor());
		}
		if (OuterBorder.IsValid())
		{
			OuterBorder->SetBorderBackgroundColor(ProjectSocialCardOuterColor());
		}
		if (InnerBorder.IsValid())
		{
			InnerBorder->SetBorderBackgroundColor(ProjectSocialCardInnerColor());
		}
		if (TitleText.IsValid())
		{
			TitleText->SetColorAndOpacity(ProjectSocialCardTitleColor());
		}
		for (const FRowWidgets& Row : RowWidgets)
		{
			if (Row.LabelText.IsValid())
			{
				Row.LabelText->SetColorAndOpacity(ProjectSocialCardLabelColor());
			}
			if (Row.ValueText.IsValid())
			{
				Row.ValueText->SetColorAndOpacity(
					Row.bIsAvailable
						? ProjectSocialCardValueColor()
						: ProjectSocialCardMutedValueColor());
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
			.FillWidth(0.61f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
			[
				SAssignNew(LabelText, STextBlock)
				.Text(Row.Label.IsEmpty() ? FText::FromString(TEXT("--")) : Row.Label)
				.Font(MakeSocialCardFont(TEXT("Bold"), 12))
				.ColorAndOpacity(ProjectSocialCardLabelColor())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.39f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ValueText, STextBlock)
				.Text(BuildSocialCardValueText(Row))
				.Font(MakeSocialCardFont(TEXT("Regular"), 12))
				.ColorAndOpacity(Row.bIsAvailable ? ProjectSocialCardValueColor() : ProjectSocialCardMutedValueColor())
				.Justification(ETextJustify::Right)
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

UProjectSocialCardWidget::UProjectSocialCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CachedSnapshot.Rows.Reserve(10);
}

TSharedRef<SWidget> UProjectSocialCardWidget::RebuildWidget()
{
	SAssignNew(SocialCardPanel, SProjectSocialCardPanel);
	ApplyCachedSnapshot();
	SetVisibility(bHudVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	return SocialCardPanel.ToSharedRef();
}

void UProjectSocialCardWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SocialCardPanel.Reset();
}

void UProjectSocialCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyFixedViewportPlacement();
	SetVisibility(bHudVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ApplyCachedSnapshot();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectSocialCardWidget::SetSocialCardSnapshot(const FProjectSocialCardSnapshot& InSnapshot)
{
	CachedSnapshot = InSnapshot;
	ApplyCachedSnapshot();
}

void UProjectSocialCardWidget::SetHudVisible(const bool bVisible)
{
	bHudVisible = bVisible;
	SetVisibility(bHudVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (SocialCardPanel.IsValid())
	{
		SocialCardPanel->SetVisibility(bHudVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

bool UProjectSocialCardWidget::IsHudVisible() const
{
	return bHudVisible;
}

void UProjectSocialCardWidget::ApplyCachedSnapshot()
{
	if (SocialCardPanel.IsValid())
	{
		SocialCardPanel->SetSocialCardSnapshot(CachedSnapshot);
	}
}

void UProjectSocialCardWidget::ApplyFixedViewportPlacement()
{
	SetDesiredSizeInViewport(FVector2D(ProjectSocialCardPanelWidth, ProjectSocialCardPanelMinHeight));
	SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
	SetAlignmentInViewport(FVector2D(0.0f, 0.0f));
	SetPositionInViewport(ProjectSocialCardViewportPosition, false);
}

void UProjectSocialCardWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (SocialCardPanel.IsValid())
	{
		SocialCardPanel->ApplyTheme();
	}
}
