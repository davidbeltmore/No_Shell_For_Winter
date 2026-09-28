#include "Training/ProjectPracticeWidget.h"
#include "Training/ProjectPracticeComponent.h"
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void UProjectPracticeWidget::Configure(UProjectPracticeComponent* InPractice)
{
    Practice = InPractice;
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UProjectPracticeWidget::RebuildWidget()
{
    BarStyle.SetBackgroundImage(FSlateColorBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f)));
    BarStyle.SetFillImage(FSlateColorBrush(FLinearColor::White));
    FFontOutlineSettings Outline;
    Outline.OutlineSize = 1;
    Outline.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.4f);
    const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Light", 11, Outline);
    const FLinearColor Shadow(0.f, 0.f, 0.f, 0.35f);
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 28.f, 24.f))
        [
            SNew(SBox).WidthOverride(330.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text_UObject(this, &ThisClass::ExerciseText).Font(Font)
                        .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0.f, 1.f)).ShadowColorAndOpacity(Shadow) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
                    [ SNew(STextBlock).Text_UObject(this, &ThisClass::ElapsedText).Font(Font)
                        .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0.f, 1.f)).ShadowColorAndOpacity(Shadow) ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
                    [ SNew(STextBlock).Text_UObject(this, &ThisClass::CurrentRankText).Font(Font)
                        .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0.f, 1.f)).ShadowColorAndOpacity(Shadow) ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.4f)).Padding(1.f)
                        [
                            SNew(SBox).HeightOverride(3.f)
                            [ SNew(SProgressBar).Style(&BarStyle).Percent_UObject(this, &ThisClass::RankProgress)
                                .FillColorAndOpacity(FLinearColor::White).BorderPadding(FVector2D::ZeroVector) ]
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
                    [ SNew(STextBlock).Text_UObject(this, &ThisClass::NextRankText).Font(Font)
                        .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0.f, 1.f)).ShadowColorAndOpacity(Shadow) ]
                ]
            ]
        ];
}

FText UProjectPracticeWidget::ExerciseText() const
{
    const auto* Definition = Practice.IsValid() ? GetDefault<UProjectPracticeSettings>()->Find(Practice->GetActiveTrainingId()) : nullptr;
    return Definition ? FText::Format(NSLOCTEXT("ProjectPractice", "ExerciseAttribute", "{0} · {1}"),
        Definition->Name, FText::FromName(Definition->Attribute)) : FText::GetEmpty();
}

FText UProjectPracticeWidget::ElapsedText() const
{
    const int32 Minutes = Practice.IsValid() ? FMath::Max(0, FMath::FloorToInt(Practice->GetSessionHours() * 60.0)) : 0;
    return FText::FromString(FString::Printf(TEXT("%dh %02dm"), Minutes / 60, Minutes % 60));
}

int32 UProjectPracticeWidget::CurrentRank() const
{
    const auto* Progression = Practice.IsValid() && Practice->GetOwner()
        ? Practice->GetOwner()->FindComponentByClass<UProjectPracticeProgressionComponent>() : nullptr;
    return Progression ? FMath::FloorToInt(Progression->GetLearnedRank(Practice->GetPracticedAttribute())) : 0;
}

FText UProjectPracticeWidget::CurrentRankText() const { return FText::AsNumber(CurrentRank()); }
FText UProjectPracticeWidget::NextRankText() const
{
    return FText::AsNumber(FMath::Min(CurrentRank() + 1, GetDefault<UProjectPracticeSettings>()->MaximumAttributeRank));
}
TOptional<float> UProjectPracticeWidget::RankProgress() const
{
    const auto* Progression = Practice.IsValid() && Practice->GetOwner()
        ? Practice->GetOwner()->FindComponentByClass<UProjectPracticeProgressionComponent>() : nullptr;
    return Progression ? Progression->GetRankProgressNormalized(Practice->GetPracticedAttribute()) : 0.f;
}
