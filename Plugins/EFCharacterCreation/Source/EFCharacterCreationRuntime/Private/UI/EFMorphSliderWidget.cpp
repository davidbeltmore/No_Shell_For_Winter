#include "UI/EFMorphSliderWidget.h"

#include "EFCharacterCreationGameplayHooks.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"

namespace EFMorphSliderWidgetPrivate
{
	static constexpr float SafeMorphMinValue = -1.0f;
	static constexpr float SafeMorphMaxValue = 1.0f;
}

void UEFMorphSliderWidget::InitializeFromEntry(const FMorphSliderEntry& InEntry, float InCurrentValue)
{
	Entry = InEntry;
	CurrentValue = InCurrentValue;
	RefreshVisuals();
}

void UEFMorphSliderWidget::SetCurrentValue(float InCurrentValue)
{
	CurrentValue = InCurrentValue;
	RefreshVisuals();
}

void UEFMorphSliderWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	BindDesignerWidgets();
	EnsureWarningTextWidget();
	BindCallbacks();
	ApplyLayoutStyling();
	RefreshVisuals();
}

void UEFMorphSliderWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindDesignerWidgets();
	EnsureWarningTextWidget();
	BindCallbacks();
	ApplyLayoutStyling();
	RefreshVisuals();
	EFCharacterCreationGameplayHooks::OnWidgetReady().Broadcast(this);
}

void UEFMorphSliderWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UVerticalBox* RootColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MorphColumn"));
	WidgetTree->RootWidget = RootColumn;

	UHorizontalBox* RootRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MorphRow"));
	if (UVerticalBoxSlot* RowSlot = RootColumn->AddChildToVerticalBox(RootRow))
	{
		RowSlot->SetPadding(FMargin(0.0f));
	}

	MorphNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MorphName"));
	MorphNameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UHorizontalBoxSlot* NameSlot = RootRow->AddChildToHorizontalBox(MorphNameText))
	{
		NameSlot->SetPadding(FMargin(0.0f, 4.0f, 8.0f, 4.0f));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	ValueSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("ValueSlider"));
	ValueSlider->OnValueChanged.AddDynamic(this, &UEFMorphSliderWidget::HandleSliderValueChanged);
	if (UHorizontalBoxSlot* SliderSlot = RootRow->AddChildToHorizontalBox(ValueSlider))
	{
		SliderSlot->SetPadding(FMargin(0.0f, 6.0f));
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ValueText"));
	ValueText->SetJustification(ETextJustify::Right);
	ValueText->SetMinDesiredWidth(54.0f);
	if (UHorizontalBoxSlot* ValueSlot = RootRow->AddChildToHorizontalBox(ValueText))
	{
		ValueSlot->SetPadding(FMargin(8.0f, 4.0f));
	}

	ResetButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ResetButton"));
	ResetButton->SetBackgroundColor(FLinearColor(0.2f, 0.16f, 0.13f, 1.0f));
	ResetButton->OnClicked.AddDynamic(this, &UEFMorphSliderWidget::HandleResetClicked);
	ResetButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResetButtonText"));
	ResetButtonText->SetText(FText::FromString(TEXT("X")));
	ResetButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ResetButton->SetContent(ResetButtonText);
	if (UHorizontalBoxSlot* ResetSlot = RootRow->AddChildToHorizontalBox(ResetButton))
	{
		ResetSlot->SetPadding(FMargin(8.0f, 0.0f));
	}

	WarningText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WarningText"));
	WarningText->SetText(FText::FromString(TEXT("Warning: the character may look deformed.")));
	WarningText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.68f, 0.22f, 1.0f)));
	WarningText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* WarningSlot = RootColumn->AddChildToVerticalBox(WarningText))
	{
		WarningSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}
}

void UEFMorphSliderWidget::BindDesignerWidgets()
{
	if (!WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);

	auto MatchesAnyToken = [](const FString& WidgetName, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (WidgetName.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	};

	if (!IsValid(ValueSlider))
	{
		USlider* FallbackSlider = nullptr;
		for (UWidget* Widget : AllWidgets)
		{
			USlider* Slider = Cast<USlider>(Widget);
			if (!IsValid(Slider))
			{
				continue;
			}

			if (!FallbackSlider)
			{
				FallbackSlider = Slider;
			}

			if (MatchesAnyToken(Widget->GetName(), { TEXT("Value"), TEXT("Morph"), TEXT("Slider") }))
			{
				ValueSlider = Slider;
				break;
			}
		}

		if (!IsValid(ValueSlider))
		{
			ValueSlider = FallbackSlider;
		}
	}

	if (!IsValid(ResetButton))
	{
		UButton* FallbackButton = nullptr;
		for (UWidget* Widget : AllWidgets)
		{
			UButton* Button = Cast<UButton>(Widget);
			if (!IsValid(Button))
			{
				continue;
			}

			if (!FallbackButton)
			{
				FallbackButton = Button;
			}

			if (MatchesAnyToken(Widget->GetName(), { TEXT("Reset"), TEXT("Clear"), TEXT("Default"), TEXT("Button") }))
			{
				ResetButton = Button;
				break;
			}
		}

		if (!IsValid(ResetButton))
		{
			ResetButton = FallbackButton;
		}
	}

	if (!IsValid(MorphNameText) || !IsValid(ValueText) || !IsValid(WarningText) || !IsValid(ResetButtonText))
	{
		TArray<UTextBlock*> TextBlocks;
		for (UWidget* Widget : AllWidgets)
		{
			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				TextBlocks.Add(TextBlock);
			}
		}

		UTextBlock* FallbackNameText = nullptr;
		UTextBlock* FallbackValueText = nullptr;

		for (UTextBlock* TextBlock : TextBlocks)
		{
			if (!IsValid(TextBlock))
			{
				continue;
			}

			const FString WidgetName = TextBlock->GetName();
			if (!FallbackNameText)
			{
				FallbackNameText = TextBlock;
			}

			if (!FallbackValueText && TextBlock != FallbackNameText)
			{
				FallbackValueText = TextBlock;
			}

			if (!IsValid(MorphNameText) && MatchesAnyToken(WidgetName, { TEXT("Name"), TEXT("Label"), TEXT("Morph"), TEXT("Title") }))
			{
				MorphNameText = TextBlock;
				continue;
			}

			if (!IsValid(ValueText) && MatchesAnyToken(WidgetName, { TEXT("Value"), TEXT("Amount"), TEXT("Number") }))
			{
				ValueText = TextBlock;
				continue;
			}

			if (!IsValid(WarningText) && MatchesAnyToken(WidgetName, { TEXT("Warning"), TEXT("Aviso"), TEXT("Deform") }))
			{
				WarningText = TextBlock;
				continue;
			}

			if (!IsValid(ResetButtonText) && MatchesAnyToken(WidgetName, { TEXT("Reset"), TEXT("Clear"), TEXT("Button"), TEXT("X") }))
			{
				ResetButtonText = TextBlock;
			}
		}

		if (!IsValid(MorphNameText))
		{
			MorphNameText = FallbackNameText;
		}

		if (!IsValid(ValueText))
		{
			ValueText = FallbackValueText;
		}

		if (!IsValid(ResetButtonText) && IsValid(ResetButton))
		{
			ResetButtonText = Cast<UTextBlock>(ResetButton->GetContent());
		}
	}
}

void UEFMorphSliderWidget::EnsureWarningTextWidget()
{
	if (IsValid(WarningText) || !WidgetTree)
	{
		return;
	}

	UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (!IsValid(RootPanel))
	{
		return;
	}

	WarningText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WarningText"));
	WarningText->SetText(FText::FromString(TEXT("Warning: the character may look deformed.")));
	WarningText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.68f, 0.22f, 1.0f)));
	WarningText->SetVisibility(ESlateVisibility::Collapsed);
	RootPanel->AddChild(WarningText);
}

void UEFMorphSliderWidget::BindCallbacks()
{
	if (IsValid(ValueSlider))
	{
		ValueSlider->OnValueChanged.RemoveDynamic(this, &UEFMorphSliderWidget::HandleSliderValueChanged);
		ValueSlider->OnValueChanged.AddDynamic(this, &UEFMorphSliderWidget::HandleSliderValueChanged);
	}

	if (IsValid(ResetButton))
	{
		ResetButton->OnClicked.RemoveDynamic(this, &UEFMorphSliderWidget::HandleResetClicked);
		ResetButton->OnClicked.AddDynamic(this, &UEFMorphSliderWidget::HandleResetClicked);
	}
}

void UEFMorphSliderWidget::ApplyLayoutStyling()
{
	auto SetTextSize = [](UTextBlock* TextBlock, int32 FontSize)
	{
		if (!IsValid(TextBlock))
		{
			return;
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		FontInfo.Size = FontSize;
		TextBlock->SetFont(FontInfo);
	};

	auto SetFillSlot = [](UWidget* Widget, float FillValue, const FMargin& SlotPadding)
	{
		if (!IsValid(Widget))
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize SlotSize;
			SlotSize.SizeRule = ESlateSizeRule::Fill;
			SlotSize.Value = FillValue;
			HorizontalSlot->SetSize(SlotSize);
			HorizontalSlot->SetPadding(SlotPadding);
		}
	};

	auto SetAutoSlot = [](UWidget* Widget, const FMargin& SlotPadding)
	{
		if (!IsValid(Widget))
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize SlotSize;
			SlotSize.SizeRule = ESlateSizeRule::Automatic;
			SlotSize.Value = 0.0f;
			HorizontalSlot->SetSize(SlotSize);
			HorizontalSlot->SetPadding(SlotPadding);
		}

		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			VerticalSlot->SetPadding(SlotPadding);
		}
	};

	SetTextSize(MorphNameText, 12);
	SetTextSize(ValueText, 12);
	SetTextSize(WarningText, 10);
	SetTextSize(ResetButtonText, 12);

	if (IsValid(MorphNameText))
	{
		MorphNameText->SetMinDesiredWidth(272.0f);
	}

	if (IsValid(ValueText))
	{
		ValueText->SetMinDesiredWidth(52.0f);
	}

	SetFillSlot(MorphNameText, 1.85f, FMargin(0.0f, 3.0f, 16.0f, 3.0f));
	SetFillSlot(ValueSlider, 0.95f, FMargin(0.0f, 5.0f, 0.0f, 5.0f));
	SetAutoSlot(ValueText, FMargin(10.0f, 3.0f, 0.0f, 3.0f));
	SetAutoSlot(WarningText, FMargin(8.0f, 3.0f, 0.0f, 3.0f));
	SetAutoSlot(ResetButton, FMargin(8.0f, 0.0f, 0.0f, 0.0f));
}

void UEFMorphSliderWidget::RefreshVisuals()
{
	bIsRefreshingVisuals = true;

	if (IsValid(MorphNameText))
	{
		const FString DisplayName = Entry.DisplayName.IsEmpty() ? Entry.MorphName.ToString() : Entry.DisplayName;
		MorphNameText->SetText(FText::FromString(DisplayName));
	}

	if (IsValid(ValueSlider))
	{
		ValueSlider->SetMinValue(Entry.MinValue);
		ValueSlider->SetMaxValue(Entry.MaxValue);
		ValueSlider->SetValue(CurrentValue);
	}

	if (IsValid(ValueText))
	{
		ValueText->SetText(FText::FromString(FString::Printf(TEXT("%0.2f"), CurrentValue)));
	}

	if (IsValid(WarningText))
	{
		WarningText->SetText(FText::FromString(TEXT("Warning: the character may look deformed.")));
	}

	RefreshDeformationWarning();

	if (IsValid(ResetButtonText) && ResetButtonText->GetText().IsEmpty())
	{
		ResetButtonText->SetText(FText::FromString(TEXT("X")));
	}

	bIsRefreshingVisuals = false;
}

void UEFMorphSliderWidget::HandleSliderValueChanged(float NewValue)
{
	if (bIsRefreshingVisuals)
	{
		return;
	}

	CurrentValue = NewValue;

	if (IsValid(ValueText))
	{
		ValueText->SetText(FText::FromString(FString::Printf(TEXT("%0.2f"), CurrentValue)));
	}

	RefreshDeformationWarning();

	OnMorphValueChanged.Broadcast(Entry, CurrentValue);
}

void UEFMorphSliderWidget::RefreshDeformationWarning()
{
	if (!IsValid(WarningText))
	{
		return;
	}

	const bool bOutsideSafeRange = CurrentValue < EFMorphSliderWidgetPrivate::SafeMorphMinValue
		|| CurrentValue > EFMorphSliderWidgetPrivate::SafeMorphMaxValue;
	WarningText->SetVisibility(bOutsideSafeRange ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UEFMorphSliderWidget::HandleResetClicked()
{
	CurrentValue = Entry.DefaultValue;
	RefreshVisuals();
	OnMorphResetRequested.Broadcast(Entry);
}

