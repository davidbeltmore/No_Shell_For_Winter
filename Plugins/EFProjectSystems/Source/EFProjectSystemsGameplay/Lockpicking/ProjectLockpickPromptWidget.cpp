#include "Lockpicking/ProjectLockpickPromptWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ProjectLockpickPromptWidget"

namespace ProjectLockpickPromptWidgetPrivate
{
	constexpr float PanelWidth = 360.0f;
	constexpr float PanelHeight = 224.0f;

	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.42f);
	FLinearColor PanelTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.92f);
	}
	FLinearColor OptionTint()
	{
		return EFProjectUIPalette::SectionFill(0.96f);
	}
	FLinearColor SelectedTint()
	{
		return EFProjectUIPalette::Outline(0.96f);
	}
	FLinearColor DisabledTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.82f);
	}
	FLinearColor TextTint()
	{
		return EFProjectUIPalette::PrimaryText(0.98f);
	}
	FLinearColor SelectedTextTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor SecondaryTextTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	FLinearColor DisabledTextTint()
	{
		return EFProjectUIPalette::MutedText(0.62f);
	}

	const UObject* GetDesignerSafeDefaultFontObject()
	{
		static TWeakObjectPtr<const UObject> CachedFontObject;
		if (!CachedFontObject.IsValid())
		{
			CachedFontObject = LoadObject<UObject>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		}
		return CachedFontObject.Get();
	}

	FSlateBrush MakeRoundedBrush(const FLinearColor& Color, const float Radius)
	{
		return FSlateRoundedBoxBrush(Color, Radius);
	}

	UCanvasPanelSlot* AddCanvasChild(
		UCanvasPanel* Canvas,
		UWidget* Child,
		const FAnchors& Anchors,
		const FVector2D& Alignment,
		const FVector2D& Position,
		const FVector2D& Size,
		const int32 ZOrder)
	{
		if (!Canvas || !Child)
		{
			return nullptr;
		}

		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		if (!Slot)
		{
			return nullptr;
		}

		Slot->SetAnchors(Anchors);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		Slot->SetZOrder(ZOrder);
		return Slot;
	}
}

UProjectLockpickPromptWidget::UProjectLockpickPromptWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	RequiredItemDisplayName = LOCTEXT("DefaultRequiredItemDisplayName", "Lockpick");
}

void UProjectLockpickPromptWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	RefreshOptionState();
}

void UProjectLockpickPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	if (LockpickButton)
	{
		LockpickButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleLockpickClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCancelClicked);
	}
	RefreshOptionState();
	FocusPromptWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectLockpickPromptWidget::NativeDestruct()
{
	if (LockpickButton)
	{
		LockpickButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLockpickClicked);
	}

	if (CancelButton)
	{
		CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelClicked);
	}

	Super::NativeDestruct();
}

FReply UProjectLockpickPromptWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;

	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::BackSpace)
	{
		OnCancelSelected.Broadcast();
		return FReply::Handled();
	}

	if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Tab)
	{
		SetSelectedOption(SelectedOption == 0 ? 1 : 0);
		return FReply::Handled();
	}

	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::E)
	{
		ConfirmSelectedOption();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectLockpickPromptWidget::Configure(
	const bool bInCanLockpick,
	const int32 InLockpickCount,
	const FText& InRequiredItemDisplayName)
{
	bCanLockpick = bInCanLockpick;
	LockpickCount = FMath::Max(0, InLockpickCount);
	RequiredItemDisplayName = InRequiredItemDisplayName.IsEmpty()
		? LOCTEXT("DefaultRequiredItemDisplayName", "Lockpick")
		: InRequiredItemDisplayName;
	SetSelectedOption(bCanLockpick ? SelectedOption : 1);
	RefreshOptionState();
}

void UProjectLockpickPromptWidget::FocusPromptWidget()
{
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		SetUserFocus(PlayerController);
	}

	SetKeyboardFocus();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(TakeWidget(), EFocusCause::SetDirectly);
	}
}

void UProjectLockpickPromptWidget::HandleLockpickClicked()
{
	if (bCanLockpick)
	{
		OnLockpickSelected.Broadcast();
	}
}

void UProjectLockpickPromptWidget::HandleCancelClicked()
{
	OnCancelSelected.Broadcast();
}

void UProjectLockpickPromptWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultPromptTree(WidgetTree);
}

bool UProjectLockpickPromptWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	TargetWidgetTree->RootWidget = nullptr;
	bUsingNativeFallbackTree = true;
	bCanLockpick = true;
	LockpickCount = 5;
	RequiredItemDisplayName = LOCTEXT("DesignerPreviewRequiredItemDisplayName", "Lockpick");
	const bool bBuiltTree = BuildDefaultPromptTree(TargetWidgetTree);
	RefreshOptionState();
	return bBuiltTree;
}

bool UProjectLockpickPromptWidget::BuildDefaultPromptTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	UBorder* BackdropBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackdropBorder"));
	BackdropBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::BackdropTint, 0.0f));
	ProjectLockpickPromptWidgetPrivate::AddCanvasChild(
		RootCanvas,
		BackdropBorder,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D(0.0f, 0.0f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		0);

	PanelBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
	PanelBorder->SetPadding(FMargin(18.0f, 16.0f));
	PanelBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::PanelTint(), 8.0f));
	ProjectLockpickPromptWidgetPrivate::AddCanvasChild(
		RootCanvas,
		PanelBorder,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D::ZeroVector,
		FVector2D(ProjectLockpickPromptWidgetPrivate::PanelWidth, ProjectLockpickPromptWidgetPrivate::PanelHeight),
		1);

	UVerticalBox* PanelBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelBox"));
	PanelBorder->SetContent(PanelBox);

	TitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetText(LOCTEXT("LockedAnnouncement", "Locked"));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetFont(MakeTitleFont(26));
	TitleText->SetColorAndOpacity(ProjectLockpickPromptWidgetPrivate::TextTint());
	UVerticalBoxSlot* TitleSlot = PanelBox->AddChildToVerticalBox(TitleText);
	if (TitleSlot)
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	StatusText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	StatusText->SetFont(MakeBodyFont(14));
	StatusText->SetColorAndOpacity(ProjectLockpickPromptWidgetPrivate::SecondaryTextTint());
	UVerticalBoxSlot* StatusSlot = PanelBox->AddChildToVerticalBox(StatusText);
	if (StatusSlot)
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Fill);
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	LockpickOptionBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickOptionBorder"));
	LockpickOptionBorder->SetPadding(FMargin(0.0f));
	LockpickOptionBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::OptionTint(), 7.0f));
	UOverlay* LockpickOptionOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LockpickOptionOverlay"));
	LockpickOptionBorder->SetContent(LockpickOptionOverlay);
	LockpickSelectionFrame = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LockpickSelectionFrame"));
	LockpickSelectionFrame->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::SelectedTint(), 7.0f));
	LockpickSelectionFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* LockpickFrameSlot = LockpickOptionOverlay->AddChildToOverlay(LockpickSelectionFrame);
	if (LockpickFrameSlot)
	{
		LockpickFrameSlot->SetHorizontalAlignment(HAlign_Fill);
		LockpickFrameSlot->SetVerticalAlignment(VAlign_Fill);
	}
	LockpickButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("LockpickButton"));
	LockpickText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LockpickText"));
	LockpickText->SetText(LOCTEXT("LockpickOption", "Try Lockpick"));
	LockpickText->SetJustification(ETextJustify::Center);
	LockpickText->SetFont(MakeBodyFont(18));
	LockpickButton->SetContent(LockpickText);
	UOverlaySlot* LockpickButtonSlot = LockpickOptionOverlay->AddChildToOverlay(LockpickButton);
	if (LockpickButtonSlot)
	{
		LockpickButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		LockpickButtonSlot->SetVerticalAlignment(VAlign_Fill);
		LockpickButtonSlot->SetPadding(FMargin(2.0f));
	}
	UVerticalBoxSlot* LockpickSlot = PanelBox->AddChildToVerticalBox(LockpickOptionBorder);
	if (LockpickSlot)
	{
		LockpickSlot->SetHorizontalAlignment(HAlign_Fill);
		LockpickSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	CancelOptionBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CancelOptionBorder"));
	CancelOptionBorder->SetPadding(FMargin(0.0f));
	CancelOptionBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::OptionTint(), 7.0f));
	UOverlay* CancelOptionOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CancelOptionOverlay"));
	CancelOptionBorder->SetContent(CancelOptionOverlay);
	CancelSelectionFrame = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CancelSelectionFrame"));
	CancelSelectionFrame->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(ProjectLockpickPromptWidgetPrivate::SelectedTint(), 7.0f));
	CancelSelectionFrame->SetVisibility(ESlateVisibility::Hidden);
	UOverlaySlot* CancelFrameSlot = CancelOptionOverlay->AddChildToOverlay(CancelSelectionFrame);
	if (CancelFrameSlot)
	{
		CancelFrameSlot->SetHorizontalAlignment(HAlign_Fill);
		CancelFrameSlot->SetVerticalAlignment(VAlign_Fill);
	}
	CancelButton = TargetWidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CancelButton"));
	CancelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CancelText"));
	CancelText->SetText(LOCTEXT("CancelOption", "Cancel"));
	CancelText->SetJustification(ETextJustify::Center);
	CancelText->SetFont(MakeBodyFont(18));
	CancelButton->SetContent(CancelText);
	UOverlaySlot* CancelButtonSlot = CancelOptionOverlay->AddChildToOverlay(CancelButton);
	if (CancelButtonSlot)
	{
		CancelButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		CancelButtonSlot->SetVerticalAlignment(VAlign_Fill);
		CancelButtonSlot->SetPadding(FMargin(2.0f));
	}
	UVerticalBoxSlot* CancelSlot = PanelBox->AddChildToVerticalBox(CancelOptionBorder);
	if (CancelSlot)
	{
		CancelSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	return true;
}

void UProjectLockpickPromptWidget::RefreshOptionState()
{
	const FText ItemName = RequiredItemDisplayName.IsEmpty()
		? LOCTEXT("DefaultRequiredItemDisplayName", "Lockpick")
		: RequiredItemDisplayName;

	if (StatusText)
	{
		StatusText->SetText(bCanLockpick
			? FText::Format(LOCTEXT("RequiredItemAvailable", "{0} x{1}"), ItemName, FText::AsNumber(LockpickCount))
			: FText::Format(LOCTEXT("RequiredItemUnavailable", "You need a {0}"), ItemName));
	}

	if (LockpickText)
	{
		LockpickText->SetText(FText::Format(LOCTEXT("LockpickOptionFormat", "Try {0}"), ItemName));
	}

	if (LockpickButton)
	{
		LockpickButton->SetIsEnabled(bCanLockpick);
	}

	RefreshSelectionVisualState();

	if (LockpickOptionBorder)
	{
		if (bUsingNativeFallbackTree && !LockpickSelectionFrame && !CancelSelectionFrame)
		{
			const FLinearColor Tint = !bCanLockpick
				? ProjectLockpickPromptWidgetPrivate::DisabledTint()
				: (SelectedOption == 0 ? ProjectLockpickPromptWidgetPrivate::SelectedTint() : ProjectLockpickPromptWidgetPrivate::OptionTint());
			LockpickOptionBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(Tint, 7.0f));
		}
	}

	if (CancelOptionBorder)
	{
		if (bUsingNativeFallbackTree && !LockpickSelectionFrame && !CancelSelectionFrame)
		{
			const FLinearColor Tint = SelectedOption == 1
				? ProjectLockpickPromptWidgetPrivate::SelectedTint()
				: ProjectLockpickPromptWidgetPrivate::OptionTint();
			CancelOptionBorder->SetBrush(ProjectLockpickPromptWidgetPrivate::MakeRoundedBrush(Tint, 7.0f));
		}
	}

	if (LockpickText)
	{
		if (bUsingNativeFallbackTree)
		{
			LockpickText->SetColorAndOpacity(!bCanLockpick
				? ProjectLockpickPromptWidgetPrivate::DisabledTextTint()
				: (SelectedOption == 0 ? ProjectLockpickPromptWidgetPrivate::SelectedTextTint() : ProjectLockpickPromptWidgetPrivate::TextTint()));
		}
	}

	if (CancelText)
	{
		if (bUsingNativeFallbackTree)
		{
			CancelText->SetColorAndOpacity(SelectedOption == 1
				? ProjectLockpickPromptWidgetPrivate::SelectedTextTint()
				: ProjectLockpickPromptWidgetPrivate::TextTint());
		}
	}
}

void UProjectLockpickPromptWidget::RefreshSelectionVisualState()
{
	const bool bLockpickSelected = bCanLockpick && SelectedOption == 0;
	const bool bCancelSelected = !bLockpickSelected;

	if (LockpickSelectionFrame)
	{
		LockpickSelectionFrame->SetVisibility(bLockpickSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (CancelSelectionFrame)
	{
		CancelSelectionFrame->SetVisibility(bCancelSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	OnPromptSelectionChanged(SelectedOption, bCanLockpick);
}

void UProjectLockpickPromptWidget::SetSelectedOption(const int32 InSelectedOption)
{
	SelectedOption = FMath::Clamp(InSelectedOption, 0, 1);
	if (!bCanLockpick && SelectedOption == 0)
	{
		SelectedOption = 1;
	}

	RefreshOptionState();
}

void UProjectLockpickPromptWidget::ConfirmSelectedOption()
{
	if (SelectedOption == 0 && bCanLockpick)
	{
		OnLockpickSelected.Broadcast();
		return;
	}

	OnCancelSelected.Broadcast();
}

FSlateFontInfo UProjectLockpickPromptWidget::MakeTitleFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectLockpickPromptWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, TEXT("Bold"));
	}

	return FCoreStyle::GetDefaultFontStyle("Bold", Size);
}

FSlateFontInfo UProjectLockpickPromptWidget::MakeBodyFont(const int32 Size) const
{
	if (const UObject* FontObject = ProjectLockpickPromptWidgetPrivate::GetDesignerSafeDefaultFontObject())
	{
		return FSlateFontInfo(FontObject, Size, TEXT("Regular"));
	}

	return FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

#undef LOCTEXT_NAMESPACE
