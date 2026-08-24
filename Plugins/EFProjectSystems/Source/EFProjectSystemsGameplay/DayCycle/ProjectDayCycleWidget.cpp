#include "DayCycle/ProjectDayCycleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUIPalette.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ProjectDayCycleWidget"

namespace ProjectDayCycleWidgetPrivate
{
	FLinearColor MorningColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}

	FLinearColor AfternoonColor()
	{
		return EFProjectUIPalette::Accent();
	}

	FLinearColor NightColor()
	{
		return EFProjectUIPalette::AccentMuted();
	}

	FText Format24HourTime(const float NormalizedDayProgress)
	{
		constexpr int32 MinutesPerDay = 24 * 60;
		const int32 TotalMinutes = FMath::Clamp(
			FMath::FloorToInt(FMath::Clamp(NormalizedDayProgress, 0.0f, 1.0f) * static_cast<float>(MinutesPerDay)),
			0,
			MinutesPerDay - 1);
		const int32 Hours = TotalMinutes / 60;
		const int32 Minutes = TotalMinutes % 60;
		return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Hours, Minutes));
	}

	UTextBlock* AddPhaseLabel(UWidgetTree* WidgetTree, UHorizontalBox* Parent, const FName Name, const FText& Text)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(Text);
		Label->SetJustification(ETextJustify::Center);
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10));
		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Label))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Label;
	}

	UProgressBar* AddPhaseBar(UWidgetTree* WidgetTree, UHorizontalBox* Parent, const FName Name)
	{
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
		Bar->SetPercent(0.0f);
		Bar->SetBarFillType(EProgressBarFillType::LeftToRight);
		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Bar))
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(2.0f, 0.0f));
			Slot->SetVerticalAlignment(VAlign_Fill);
		}
		return Bar;
	}
}

UProjectDayCycleWidget::UProjectDayCycleWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UProjectDayCycleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UProjectDayCycleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectDayCycleWidget::ApplySnapshot(const FProjectDayCycleSnapshot& Snapshot)
{
	BuildWidgetTree();
	if (!DayText || !MorningBar || !AfternoonBar || !NightBar)
	{
		return;
	}

	DayText->SetText(FText::Format(
		LOCTEXT("DayAndFloorLabel", "DAY {0} - FLOOR {1}"),
		FText::AsNumber(Snapshot.DayNumber),
		FText::AsNumber(Snapshot.FloorNumber)));
	if (CurrentTimeText)
	{
		CurrentTimeText->SetText(ProjectDayCycleWidgetPrivate::Format24HourTime(Snapshot.NormalizedDayProgress));
	}

	const float SegmentPosition = FMath::Clamp(Snapshot.NormalizedDayProgress, 0.0f, 1.0f) * 3.0f;
	UpdateSegment(MorningBar, MorningLabel, FMath::Clamp(SegmentPosition, 0.0f, 1.0f), Snapshot.Phase == EProjectDayPhase::Morning, ProjectDayCycleWidgetPrivate::MorningColor());
	UpdateSegment(AfternoonBar, AfternoonLabel, FMath::Clamp(SegmentPosition - 1.0f, 0.0f, 1.0f), Snapshot.Phase == EProjectDayPhase::Afternoon, ProjectDayCycleWidgetPrivate::AfternoonColor());
	UpdateSegment(NightBar, NightLabel, FMath::Clamp(SegmentPosition - 2.0f, 0.0f, 1.0f), Snapshot.Phase == EProjectDayPhase::Night, ProjectDayCycleWidgetPrivate::NightColor());
}

void UProjectDayCycleWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DayCycleRoot"));
	WidgetTree->RootWidget = RootCanvas;

	USizeBox* FrameSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DayCycleFrameSize"));
	FrameSize->SetWidthOverride(420.0f);
	FrameSize->SetHeightOverride(104.0f);
	if (UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(FrameSize))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		FrameSlot->SetPosition(FVector2D(0.0f, 28.0f));
		FrameSlot->SetAutoSize(true);
	}

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DayCycleFrame"));
	Frame->SetBrushColor(EFProjectUIPalette::PanelFillDeep(0.90f));
	Frame->SetPadding(FMargin(14.0f, 8.0f));
	FrameSize->SetContent(Frame);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DayCycleContent"));
	Frame->SetContent(Content);

	DayText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DayText"));
	DayText->SetText(LOCTEXT("InitialDayLabel", "DAY 1"));
	DayText->SetJustification(ETextJustify::Center);
	DayText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::Title()));
	DayText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16));
	if (UVerticalBoxSlot* DayTextSlot = Content->AddChildToVerticalBox(DayText))
	{
		DayTextSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	CurrentTimeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CurrentTimeText"));
	CurrentTimeText->SetText(LOCTEXT("InitialTimeLabel", "00:00"));
	CurrentTimeText->SetJustification(ETextJustify::Center);
	CurrentTimeText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::MutedText(0.92f)));
	CurrentTimeText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11));
	if (UVerticalBoxSlot* CurrentTimeSlot = Content->AddChildToVerticalBox(CurrentTimeText))
	{
		CurrentTimeSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	UHorizontalBox* Labels = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PhaseLabels"));
	if (UVerticalBoxSlot* LabelsSlot = Content->AddChildToVerticalBox(Labels))
	{
		LabelsSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 2.0f));
		LabelsSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	MorningLabel = ProjectDayCycleWidgetPrivate::AddPhaseLabel(WidgetTree, Labels, TEXT("MorningLabel"), LOCTEXT("Morning", "MORNING"));
	AfternoonLabel = ProjectDayCycleWidgetPrivate::AddPhaseLabel(WidgetTree, Labels, TEXT("AfternoonLabel"), LOCTEXT("Afternoon", "AFTERNOON"));
	NightLabel = ProjectDayCycleWidgetPrivate::AddPhaseLabel(WidgetTree, Labels, TEXT("NightLabel"), LOCTEXT("Night", "NIGHT"));

	USizeBox* BarsHeight = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarsHeight"));
	BarsHeight->SetHeightOverride(14.0f);
	if (UVerticalBoxSlot* BarsSlot = Content->AddChildToVerticalBox(BarsHeight))
	{
		BarsSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	UHorizontalBox* Bars = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PhaseBars"));
	BarsHeight->SetContent(Bars);
	MorningBar = ProjectDayCycleWidgetPrivate::AddPhaseBar(WidgetTree, Bars, TEXT("MorningBar"));
	AfternoonBar = ProjectDayCycleWidgetPrivate::AddPhaseBar(WidgetTree, Bars, TEXT("AfternoonBar"));
	NightBar = ProjectDayCycleWidgetPrivate::AddPhaseBar(WidgetTree, Bars, TEXT("NightBar"));

	FProjectDayCycleSnapshot InitialSnapshot;
	ApplySnapshot(InitialSnapshot);
}

void UProjectDayCycleWidget::UpdateSegment(
	UProgressBar* Segment,
	UTextBlock* Label,
	const float Percent,
	const bool bIsActive,
	const FLinearColor& ActiveColor)
{
	if (Segment)
	{
		Segment->SetPercent(Percent);
		Segment->SetFillColorAndOpacity(bIsActive ? ActiveColor : ActiveColor * 0.62f);
	}

	if (Label)
	{
		Label->SetColorAndOpacity(FSlateColor(bIsActive ? ActiveColor : EFProjectUIPalette::MutedText(0.78f)));
	}
}

#undef LOCTEXT_NAMESPACE
