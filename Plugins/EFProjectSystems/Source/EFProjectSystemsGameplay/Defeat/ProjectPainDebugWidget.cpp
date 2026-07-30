#include "Defeat/ProjectPainDebugWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Styling/CoreStyle.h"

namespace ProjectPainDebugWidgetPrivate
{
	const FLinearColor FrameColor(0.02f, 0.02f, 0.02f, 0.84f);
	const FLinearColor TextColor(0.86f, 0.90f, 0.82f, 1.0f);
}

UProjectPainDebugWidget::UProjectPainDebugWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UProjectPainDebugWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UProjectPainDebugWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	RefreshText();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectPainDebugWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	(void)MyGeometry;
	(void)InDeltaTime;
	RefreshText();
}

void UProjectPainDebugWidget::SetObservedFlowComponent(UProjectDefeatFlowComponent* InObservedFlowComponent)
{
	ObservedFlowComponent = InObservedFlowComponent;
	RefreshText();
}

void UProjectPainDebugWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PainDebugRoot"));
	WidgetTree->RootWidget = RootCanvas;

	FrameBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PainDebugFrame"));
	FrameBorder->SetBrushColor(ProjectPainDebugWidgetPrivate::FrameColor);
	FrameBorder->SetPadding(FMargin(10.f));
	if (UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(FrameBorder))
	{
		FrameSlot->SetAnchors(FAnchors(0.f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.f, 0.f));
		FrameSlot->SetPosition(FVector2D(24.f, 24.f));
		FrameSlot->SetAutoSize(true);
	}

	DebugText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PainDebugText"));
	DebugText->SetColorAndOpacity(FSlateColor(ProjectPainDebugWidgetPrivate::TextColor));
	DebugText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Monospaced"), 10));
	DebugText->SetAutoWrapText(false);
	FrameBorder->SetContent(DebugText);
}

void UProjectPainDebugWidget::RefreshText()
{
	if (!DebugText)
	{
		return;
	}

	if (!ObservedFlowComponent)
	{
		DebugText->SetText(FText::FromString(TEXT("Pain Debug\nNo observed defeat flow component.")));
		return;
	}

	DebugText->SetText(ObservedFlowComponent->BuildPainDebugText());
}
