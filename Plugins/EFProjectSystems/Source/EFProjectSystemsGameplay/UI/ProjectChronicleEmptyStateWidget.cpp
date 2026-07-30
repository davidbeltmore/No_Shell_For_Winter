#include "UI/ProjectChronicleEmptyStateWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

namespace ProjectChronicleEmptyStateWidgetPrivate
{
	FLinearColor EmptyBoxTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.90f);
	}
	FLinearColor EmptyBoxOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.62f);
	}
	FLinearColor EmptyTextTint()
	{
		return EFProjectUIPalette::PrimaryText(0.96f);
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.16f);

	FSlateFontInfo MakeFallbackFont(const int32 Size)
	{
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
		FontInfo.Size = Size;
		FontInfo.LetterSpacing = 0;
		return FontInfo;
	}
}

UProjectChronicleEmptyStateWidget::UProjectChronicleEmptyStateWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CurrentMessage = FText::FromString(TEXT("No events yet. Your journey will be recorded here."));
}

void UProjectChronicleEmptyStateWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectChronicleEmptyStateWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectChronicleEmptyStateWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	CurrentMessage = FText::FromString(TEXT("No events yet. Your journey will be recorded here."));
	CurrentWidth = 520.0f;
	bExpanded = false;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshVisuals();
	}

	return bBuiltTree;
}

void UProjectChronicleEmptyStateWidget::ApplyEmptyState(const bool bInExpanded, const FText& InMessage, const float InWidth)
{
	bExpanded = bInExpanded;
	CurrentMessage = InMessage;
	CurrentWidth = FMath::Max(InWidth, 240.0f);

	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
}

void UProjectChronicleEmptyStateWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectChronicleEmptyStateWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(CurrentWidth);
	RootSizeBox->SetHeightOverride(34.0f);
	TargetWidgetTree->RootWidget = RootSizeBox;

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->AddChild(RootOverlay);

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(10.0f, 8.0f));
	if (UOverlaySlot* BackgroundSlot = RootOverlay->AddChildToOverlay(BackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	MessageText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetAutoWrapText(true);
	BackgroundBorder->SetContent(MessageText);

	return true;
}

void UProjectChronicleEmptyStateWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (!bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = true;
		return;
	}

	if (MessageText)
	{
		MessageText->SetFont(ProjectChronicleEmptyStateWidgetPrivate::MakeFallbackFont(14));
		MessageText->SetColorAndOpacity(FSlateColor(ProjectChronicleEmptyStateWidgetPrivate::EmptyTextTint()));
		MessageText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		MessageText->SetShadowColorAndOpacity(ProjectChronicleEmptyStateWidgetPrivate::ShadowTint);
	}

	bVisualTreeInitialized = true;
}

void UProjectChronicleEmptyStateWidget::RefreshVisuals()
{
	const float Height = bExpanded ? 42.0f : 34.0f;
	if (RootSizeBox)
	{
		RootSizeBox->SetWidthOverride(CurrentWidth);
		RootSizeBox->SetHeightOverride(Height);
	}

	if (BackgroundBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush EmptyBrush(
			ProjectChronicleEmptyStateWidgetPrivate::EmptyBoxTint(),
			8.0f,
			FSlateColor(ProjectChronicleEmptyStateWidgetPrivate::EmptyBoxOutlineTint()),
			1.0f,
			FVector2f(CurrentWidth, Height));
		BackgroundBorder->SetBrush(EmptyBrush);
	}

	if (MessageText)
	{
		MessageText->SetText(CurrentMessage);
	}

	OnChronicleEmptyStateApplied(bExpanded, CurrentMessage);
}
