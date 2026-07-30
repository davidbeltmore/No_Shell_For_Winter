#include "UI/ProjectTargetLevelWidget.h"

#include "EFProjectUIPalette.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FLinearColor ProjectTargetOuterColor()
	{
		return EFProjectUIPalette::PanelFillDeep(0.96f);
	}
	FLinearColor ProjectTargetInnerColor()
	{
		return EFProjectUIPalette::PanelFill(0.98f);
	}
	FLinearColor ProjectTargetFrameColor()
	{
		return EFProjectUIPalette::Outline(0.82f);
	}
	FLinearColor ProjectTargetTypeColor()
	{
		return EFProjectUIPalette::SecondaryText();
	}
	FLinearColor ProjectTargetNameColor()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor ProjectTargetLevelColor()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor ProjectTargetLevelBadgeColor()
	{
		return EFProjectUIPalette::BadgeFill();
	}
	FLinearColor ProjectTargetHealthFillColor()
	{
		return EFProjectUIPalette::Accent();
	}
	FLinearColor ProjectTargetHealthBackColor()
	{
		return EFProjectUIPalette::PanelFillDeep(0.92f);
	}
	FLinearColor ProjectTargetHealthTextColor()
	{
		return EFProjectUIPalette::PrimaryText(0.93f);
	}

	FSlateFontInfo MakeTargetFont(const TCHAR* Weight, const int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}
}

class SProjectTargetLevelPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SProjectTargetLevelPanel)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(286.0f)
			.MinDesiredHeight(98.0f)
			[
				SAssignNew(FrameBorder, SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ProjectTargetFrameColor())
				.Padding(FMargin(2.0f))
				[
					SAssignNew(OuterBorder, SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ProjectTargetOuterColor())
					.Padding(FMargin(10.0f))
					[
						SAssignNew(InnerBorder, SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(ProjectTargetInnerColor())
						.Padding(FMargin(12.0f, 9.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SAssignNew(TypeText, STextBlock)
									.Text(FText::FromString(TEXT("Enemy")))
									.Font(MakeTargetFont(TEXT("Bold"), 14))
									.ColorAndOpacity(ProjectTargetTypeColor())
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.HAlign(HAlign_Right)
								[
									SAssignNew(LevelBadgeBorder, SBorder)
									.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
									.BorderBackgroundColor(ProjectTargetLevelBadgeColor())
									.Padding(FMargin(8.0f, 3.0f))
									[
										SAssignNew(LevelText, STextBlock)
										.Text(FText::FromString(TEXT("Lv. 1")))
										.Font(MakeTargetFont(TEXT("Bold"), 13))
										.ColorAndOpacity(ProjectTargetLevelColor())
										.Justification(ETextJustify::Center)
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 5.0f, 0.0f, 7.0f))
							[
								SAssignNew(NameText, STextBlock)
								.Text(FText::FromString(TEXT("Enemy")))
								.Font(MakeTargetFont(TEXT("Bold"), 18))
								.ColorAndOpacity(ProjectTargetNameColor())
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SAssignNew(HealthBackBorder, SBorder)
									.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
									.BorderBackgroundColor(ProjectTargetHealthBackColor())
									.Padding(FMargin(1.0f))
									[
										SAssignNew(HealthBar, SProgressBar)
										.Percent(1.0f)
										.FillColorAndOpacity(ProjectTargetHealthFillColor())
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(FMargin(0.0f, 5.0f, 0.0f, 0.0f))
							[
								SAssignNew(HealthText, STextBlock)
								.Text(FText::FromString(TEXT("HP 100 (100%)")))
								.Font(MakeTargetFont(TEXT("Regular"), 13))
								.ColorAndOpacity(ProjectTargetHealthTextColor())
							]
						]
					]
				]
			]
		];
	}

	void SetDisplayData(
		const FText& InTypeText,
		const FText& InDisplayName,
		const int32 InLevel,
		const FText& InHealthText,
		const float InHealthRatio)
	{
		if (TypeText.IsValid())
		{
			TypeText->SetText(InTypeText);
		}

		if (NameText.IsValid())
		{
			NameText->SetText(InDisplayName);
		}

		if (LevelText.IsValid())
		{
			LevelText->SetText(FText::FromString(FString::Printf(TEXT("Lv. %d"), FMath::Max(InLevel, 1))));
		}

		if (HealthBar.IsValid())
		{
			HealthBar->SetPercent(FMath::Clamp(InHealthRatio, 0.0f, 1.0f));
		}

		if (HealthText.IsValid())
		{
			HealthText->SetText(InHealthText);
		}
	}

	void ApplyTheme()
	{
		if (FrameBorder.IsValid())
		{
			FrameBorder->SetBorderBackgroundColor(ProjectTargetFrameColor());
		}
		if (OuterBorder.IsValid())
		{
			OuterBorder->SetBorderBackgroundColor(ProjectTargetOuterColor());
		}
		if (InnerBorder.IsValid())
		{
			InnerBorder->SetBorderBackgroundColor(ProjectTargetInnerColor());
		}
		if (TypeText.IsValid())
		{
			TypeText->SetColorAndOpacity(ProjectTargetTypeColor());
		}
		if (LevelBadgeBorder.IsValid())
		{
			LevelBadgeBorder->SetBorderBackgroundColor(ProjectTargetLevelBadgeColor());
		}
		if (LevelText.IsValid())
		{
			LevelText->SetColorAndOpacity(ProjectTargetLevelColor());
		}
		if (NameText.IsValid())
		{
			NameText->SetColorAndOpacity(ProjectTargetNameColor());
		}
		if (HealthBackBorder.IsValid())
		{
			HealthBackBorder->SetBorderBackgroundColor(ProjectTargetHealthBackColor());
		}
		if (HealthBar.IsValid())
		{
			HealthBar->SetFillColorAndOpacity(ProjectTargetHealthFillColor());
		}
		if (HealthText.IsValid())
		{
			HealthText->SetColorAndOpacity(ProjectTargetHealthTextColor());
		}
	}

private:
	TSharedPtr<SBorder> FrameBorder;
	TSharedPtr<SBorder> OuterBorder;
	TSharedPtr<SBorder> InnerBorder;
	TSharedPtr<STextBlock> TypeText;
	TSharedPtr<SBorder> LevelBadgeBorder;
	TSharedPtr<STextBlock> NameText;
	TSharedPtr<STextBlock> LevelText;
	TSharedPtr<SBorder> HealthBackBorder;
	TSharedPtr<SProgressBar> HealthBar;
	TSharedPtr<STextBlock> HealthText;
};

UProjectTargetLevelWidget::UProjectTargetLevelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CachedTypeText = FText::FromString(TEXT("Enemy"));
	CachedDisplayName = FText::FromString(TEXT("Enemy"));
}

TSharedRef<SWidget> UProjectTargetLevelWidget::RebuildWidget()
{
	SAssignNew(TargetPanel, SProjectTargetLevelPanel);
	ApplyCachedDisplayData();
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	return TargetPanel.ToSharedRef();
}

void UProjectTargetLevelWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TargetPanel.Reset();
}

void UProjectTargetLevelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetDesiredSizeInViewport(FVector2D(286.0f, 98.0f));
	SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ApplyCachedDisplayData();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTargetLevelWidget::SetTargetDisplayData(
	const FText& InTypeText,
	const FText& InDisplayName,
	const int32 InLevel,
	const float InCurrentHealth,
	const float InMaxHealth,
	const float InHealthRatio)
{
	CachedTypeText = InTypeText;
	CachedDisplayName = InDisplayName;
	CachedLevel = FMath::Max(InLevel, 1);
	CachedCurrentHealth = InCurrentHealth;
	CachedMaxHealth = InMaxHealth;
	CachedHealthRatio = FMath::Clamp(InHealthRatio, 0.0f, 1.0f);
	ApplyCachedDisplayData();
}

void UProjectTargetLevelWidget::SetScreenPosition(const FVector2D& InScreenPosition)
{
	SetPositionInViewport(InScreenPosition, false);
}

void UProjectTargetLevelWidget::SetOverlayVisible(const bool bVisible)
{
	bOverlayVisible = bVisible;
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (TargetPanel.IsValid())
	{
		TargetPanel->SetVisibility(bOverlayVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

bool UProjectTargetLevelWidget::IsOverlayVisible() const
{
	return bOverlayVisible;
}

void UProjectTargetLevelWidget::ApplyCachedDisplayData()
{
	if (TargetPanel.IsValid())
	{
		TargetPanel->SetDisplayData(
			CachedTypeText,
			CachedDisplayName,
			CachedLevel,
			BuildHealthText(),
			CachedHealthRatio);
	}
}

FText UProjectTargetLevelWidget::BuildHealthText() const
{
	if (CachedCurrentHealth < 0.0f)
	{
		return FText::FromString(TEXT("HP --"));
	}

	const float SafeCurrentHealth = FMath::Max(CachedCurrentHealth, 0.0f);
	const int32 PercentValue = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(CachedHealthRatio, 0.0f, 1.0f) * 100.0f), 0, 100);
	return FText::FromString(FString::Printf(TEXT("HP %.0f (%d%%)"), SafeCurrentHealth, PercentValue));
}

void UProjectTargetLevelWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (TargetPanel.IsValid())
	{
		TargetPanel->ApplyTheme();
	}
}
