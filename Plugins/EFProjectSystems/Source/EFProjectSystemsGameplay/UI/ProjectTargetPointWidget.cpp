#include "UI/ProjectTargetPointWidget.h"

#include "EFProjectUIPalette.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace
{
	const FVector2D ProjectTargetPointSize(16.0f, 16.0f);
	const FVector2D ProjectTargetPointOuterSize(12.0f, 12.0f);
	const FVector2D ProjectTargetPointInnerSize(4.0f, 4.0f);

	FLinearColor ProjectTargetPointOuterColor()
	{
		return EFProjectUIPalette::Outline(0.18f);
	}

	FLinearColor ProjectTargetPointInnerColor()
	{
		return EFProjectUIPalette::Accent();
	}
}

UProjectTargetPointWidget::UProjectTargetPointWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::Collapsed);
}

TSharedRef<SWidget> UProjectTargetPointWidget::RebuildWidget()
{
	TSharedRef<SWidget> Widget =
		SNew(SBox)
		.WidthOverride(ProjectTargetPointSize.X)
		.HeightOverride(ProjectTargetPointSize.Y)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(ProjectTargetPointOuterSize.X)
				.HeightOverride(ProjectTargetPointOuterSize.Y)
				[
					SAssignNew(OuterPointImage, SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(ProjectTargetPointOuterColor())
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(ProjectTargetPointInnerSize.X)
				.HeightOverride(ProjectTargetPointInnerSize.Y)
				[
					SAssignNew(InnerPointImage, SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(ProjectTargetPointInnerColor())
				]
			]
		];

	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	return Widget;
}

void UProjectTargetPointWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	OuterPointImage.Reset();
	InnerPointImage.Reset();
}

void UProjectTargetPointWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTargetPointWidget::SetOverlayVisible(const bool bVisible)
{
	bOverlayVisible = bVisible;
	SetVisibility(bOverlayVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

bool UProjectTargetPointWidget::IsOverlayVisible() const
{
	return bOverlayVisible;
}

void UProjectTargetPointWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (OuterPointImage.IsValid())
	{
		OuterPointImage->SetColorAndOpacity(ProjectTargetPointOuterColor());
	}
	if (InnerPointImage.IsValid())
	{
		InnerPointImage->SetColorAndOpacity(ProjectTargetPointInnerColor());
	}
}
