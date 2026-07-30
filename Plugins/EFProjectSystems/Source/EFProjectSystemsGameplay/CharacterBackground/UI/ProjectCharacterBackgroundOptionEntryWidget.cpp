#include "CharacterBackground/UI/ProjectCharacterBackgroundOptionEntryWidget.h"

#include "EFProjectUIPalette.h"
#include "CharacterBackground/ProjectCharacterBackgroundSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Math/UnrealMathUtility.h"
#include "Styling/CoreStyle.h"

namespace ProjectCharacterBackgroundOptionEntryPrivate
{
	FLinearColor SelectedFillTint()
	{
		return EFProjectUIPalette::SectionFill(0.98f);
	}
	FLinearColor NormalFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.94f);
	}
	FLinearColor SelectedOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.95f);
	}
	FLinearColor NormalOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.35f);
	}
	FLinearColor TitleTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor SubtitleTint()
	{
		return EFProjectUIPalette::SecondaryText(0.98f);
	}

	const FProjectCharacterBackgroundUILayoutTuning& GetLayoutTuning()
	{
		if (const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get())
		{
			return Settings->UILayout;
		}

		static const FProjectCharacterBackgroundUILayoutTuning DefaultLayout;
		return DefaultLayout;
	}

	float ResolveEntryHeight(const float OverrideValue)
	{
		return OverrideValue > 0.0f ? OverrideValue : GetLayoutTuning().OptionEntryHeight;
	}
}

UProjectCharacterBackgroundOptionEntryWidget::UProjectCharacterBackgroundOptionEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UProjectCharacterBackgroundOptionEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
}

void UProjectCharacterBackgroundOptionEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectCharacterBackgroundOptionEntryWidget::SetOption(
	const FName InOptionID,
	const FText& InTitle,
	const FText& InSubtitle,
	const bool bInSelected)
{
	BuildWidgetTree();
	InitializeVisualTree();

	OptionID = InOptionID;
	bSelected = bInSelected;

	if (TitleText)
	{
		TitleText->SetText(InTitle);
	}

	if (SubtitleText)
	{
		SubtitleText->SetText(InSubtitle);
	}

	SetSelected(bSelected);
}

void UProjectCharacterBackgroundOptionEntryWidget::SetSelected(const bool bInSelected)
{
	bSelected = bInSelected;
	if (!ContentBorder)
	{
		return;
	}

	const FSlateRoundedBoxBrush Brush(
		bSelected ? ProjectCharacterBackgroundOptionEntryPrivate::SelectedFillTint() : ProjectCharacterBackgroundOptionEntryPrivate::NormalFillTint(),
		ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().PanelCornerRadius,
		FSlateColor(bSelected ? ProjectCharacterBackgroundOptionEntryPrivate::SelectedOutlineTint() : ProjectCharacterBackgroundOptionEntryPrivate::NormalOutlineTint()),
		bSelected ? 1.4f : 0.8f,
		FVector2f(
			ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionEntryWidth,
			ProjectCharacterBackgroundOptionEntryPrivate::ResolveEntryHeight(EntryHeightOverride)));
	ContentBorder->SetBrush(Brush);
}

void UProjectCharacterBackgroundOptionEntryWidget::SetEntryHeight(const float InEntryHeight)
{
	EntryHeightOverride = FMath::Max(0.0f, InEntryHeight);
	if (RootSizeBox)
	{
		RootSizeBox->SetHeightOverride(ProjectCharacterBackgroundOptionEntryPrivate::ResolveEntryHeight(EntryHeightOverride));
	}
	SetSelected(bSelected);
}

void UProjectCharacterBackgroundOptionEntryWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BackgroundOptionRootSize"));
	WidgetTree->RootWidget = RootSizeBox;
	RootSizeBox->SetWidthOverride(ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionEntryWidth);
	RootSizeBox->SetHeightOverride(ProjectCharacterBackgroundOptionEntryPrivate::ResolveEntryHeight(EntryHeightOverride));

	RootButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BackgroundOptionButton"));
	if (USizeBoxSlot* RootButtonSlot = Cast<USizeBoxSlot>(RootSizeBox->AddChild(RootButton)))
	{
		RootButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		RootButtonSlot->SetVerticalAlignment(VAlign_Fill);
	}
	RootButton->SetBackgroundColor(FLinearColor::Transparent);
	RootButton->OnClicked.AddDynamic(this, &ThisClass::HandleClicked);

	ContentBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundOptionBorder"));
	ContentBorder->SetPadding(ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionEntryPadding);
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(RootButton->AddChild(ContentBorder)))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	TextBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BackgroundOptionTextBox"));
	ContentBorder->SetContent(TextBox);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BackgroundOptionTitleText"));
	if (UVerticalBoxSlot* TitleSlot = TextBox->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
	}

	SubtitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BackgroundOptionSubtitleText"));
	if (UVerticalBoxSlot* SubtitleSlot = TextBox->AddChildToVerticalBox(SubtitleText))
	{
		SubtitleSlot->SetPadding(FMargin(0.0f));
	}
}

void UProjectCharacterBackgroundOptionEntryWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (RootSizeBox)
	{
		RootSizeBox->SetWidthOverride(ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionEntryWidth);
		RootSizeBox->SetHeightOverride(ProjectCharacterBackgroundOptionEntryPrivate::ResolveEntryHeight(EntryHeightOverride));
	}

	if (TitleText)
	{
		TitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionTitleFontSize));
		TitleText->SetColorAndOpacity(ProjectCharacterBackgroundOptionEntryPrivate::TitleTint());
		TitleText->SetAutoWrapText(false);
		TitleText->SetClipping(EWidgetClipping::ClipToBounds);
	}

	if (SubtitleText)
	{
		SubtitleText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", ProjectCharacterBackgroundOptionEntryPrivate::GetLayoutTuning().OptionSubtitleFontSize));
		SubtitleText->SetColorAndOpacity(ProjectCharacterBackgroundOptionEntryPrivate::SubtitleTint());
		SubtitleText->SetAutoWrapText(true);
	}

	SetSelected(bSelected);
	bVisualTreeInitialized = true;
}

void UProjectCharacterBackgroundOptionEntryWidget::HandleClicked()
{
	OnOptionSelected.Broadcast(OptionID);
}
