#include "DayCycle/ProjectWaitWidget.h"

#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"

UProjectWaitWidget::UProjectWaitWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	bIsBackHandler = true;
	SetIsFocusable(true);
}

void UProjectWaitWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (auto* PC = GetOwningPlayer())
	{
		bPreviousCursorVisible = PC->bShowMouseCursor;
		PC->SetShowMouseCursor(true);
	}
}

void UProjectWaitWidget::NativeOnDeactivated()
{
	if (auto* PC = GetOwningPlayer()) PC->SetShowMouseCursor(bPreviousCursorVisible);
	Super::NativeOnDeactivated();
}

TSharedRef<SWidget> UProjectWaitWidget::RebuildWidget()
{
	auto Clock = [this]() { return GetWorld() ? GetWorld()->GetSubsystem<UProjectDayCycleSubsystem>() : nullptr; };
	return SNew(SOverlay)
	+ SOverlay::Slot()[ SNew(SBackgroundBlur).BlurStrength(5.f) ]
	+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(520)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.03f,.035f,.97f)).Padding(40)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,24)
				[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).Text_Lambda([Clock]() {
					const int32 Minute = Clock() ? FMath::FloorToInt(FMath::Fmod(Clock()->GetCalendarHours(),24.)*60.) : 0;
					return FText::FromString(FString::Printf(TEXT("It's %02d:%02d"),Minute/60,Minute%60)); }) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,24)
				[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",22)).Text(FText::FromString(TEXT("How long do you want to wait?"))) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
				[ SNew(STextBlock).Justification(ETextJustify::Center).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Hours to wait: %d"),FMath::RoundToInt(SelectedHours))); }) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,24)
				[ SNew(SSlider).MinValue(1).MaxValue(24).StepSize(1).Value_Lambda([this]() { return SelectedHours; }).OnValueChanged_Lambda([this](float Value) { SelectedHours = FMath::RoundToFloat(Value); }) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,24)
				[ SNew(STextBlock).Text(FText::FromString(TEXT("Advances at x4. Press / to cancel while waiting."))) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)
					[ SNew(SButton).HAlign(HAlign_Center).ContentPadding(12).Text(FText::FromString(TEXT("Cancel"))).OnClicked_Lambda([Clock]() { if (Clock()) Clock()->CloseWaitMenu(); return FReply::Handled(); }) ]
					+ SHorizontalBox::Slot().FillWidth(1)
					[ SNew(SButton).HAlign(HAlign_Center).ContentPadding(12).Text(FText::FromString(TEXT("Confirm"))).OnClicked_Lambda([this,Clock]() { if (Clock()) Clock()->ConfirmWait(SelectedHours); return FReply::Handled(); }) ]
				]
			]
		]
	];
}
