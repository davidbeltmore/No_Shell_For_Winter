#include "UI/EFCharacterCreationRootWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFCharacterCreationSettings.h"
#include "EFCharacterCreationSubsystem.h"
#include "EFCharacterCustomizationComponent.h"
#include "UI/EFMorphSliderWidget.h"
#include "UI/EFCharacterCreationSectionButton.h"
#include "EFMorphPresentation.h"
#include "Components/ExpandableArea.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Misc/DefaultValueHelper.h"
#include "Components/PanelWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"

namespace CharacterCreationRootWidgetPrivate
{
	static bool UsesSkinnedDecalTattooShopLayout()
	{
		bool bUseSkinnedDecalTattooShop = false;
		if (GConfig)
		{
			GConfig->GetBool(
				TEXT("/Script/EFProjectSystemsGameplay.ProjectTattooShopSettings"),
				TEXT("bUseSkinnedDecalTattooShop"),
				bUseSkinnedDecalTattooShop,
				GGameIni);
		}
		return bUseSkinnedDecalTattooShop;
	}

	static void SetTextSize(UTextBlock* TextBlock, const int32 FontSize)
	{
		if (!IsValid(TextBlock))
		{
			return;
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		FontInfo.Size = FontSize;
		TextBlock->SetFont(FontInfo);
	}
}

void UEFCharacterCreationRootWidget::InitializeForSession(UEFCharacterCreationSubsystem* InSubsystem, UEFCharacterCustomizationComponent* InCustomizationComponent)
{
	CharacterCreationSubsystem = InSubsystem;
	CustomizationComponent = InCustomizationComponent;
	RefreshFromComponent();
}

void UEFCharacterCreationRootWidget::RefreshFromComponent()
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		if (IsValid(ShowClothesCheckBox))
		{
			ShowClothesCheckBox->SetIsChecked(Customization->GetShowClothes());
		}

		if (IsValid(PauseAnimationCheckBox))
		{
			PauseAnimationCheckBox->SetIsChecked(Customization->GetPauseAnimation());
		}

		if (ActiveCategory == TEXT("Hair") && !Customization->HasHairCustomization())
		{
			ActiveCategory = TEXT("Body");
		}
		else if (ActiveCategory != TEXT("Info") && ActiveCategory != TEXT("Skin") && ActiveCategory != TEXT("Hair") && ActiveCategory != TEXT("Tattoo") && !Customization->GetAvailableCategories().Contains(ActiveCategory))
		{
			ActiveCategory = TEXT("Body");
		}
	}

	RefreshPresetList();
	RefreshTabVisuals();
	RefreshMorphList();
	UpdateHairTransformControls();
}

bool UEFCharacterCreationRootWidget::TryGetThemeTabState(
	const UWidget* Widget,
	bool& bOutIsActive) const
{
	for (const UEFCharacterCreationSectionButton* Button : SectionButtons)
	{
		if (Button && (Widget == Button || Widget == Button->GetContent()))
		{
			const FString Selected = SelectedSections.FindRef(ActiveCategory);
			bOutIsActive = Button->Section == (Selected.IsEmpty() ? TEXT("All") : Selected);
			return true;
		}
	}
	if (Widget && (Widget == GenderMaleButton || Widget == GenderMaleLabel || Widget == GenderFemaleButton || Widget == GenderFemaleLabel))
	{
		bOutIsActive = CustomizationComponent.IsValid() &&
			((CustomizationComponent->GetGender() == ECharacterCreationGender::Male && (Widget == GenderMaleButton || Widget == GenderMaleLabel)) ||
			 (CustomizationComponent->GetGender() == ECharacterCreationGender::Female && (Widget == GenderFemaleButton || Widget == GenderFemaleLabel)));
		return true;
	}

	const auto MatchTab =
		[this, Widget, &bOutIsActive](
			const UWidget* Button,
			const UWidget* Label,
			const FName TabName)
		{
			if (Widget != Button && Widget != Label)
			{
				return false;
			}

			bOutIsActive = ActiveCategory == TabName;
			return true;
		};

	return MatchTab(InfoTabButton, InfoTabLabel, TEXT("Info"))
		|| MatchTab(HeadTabButton, HeadTabLabel, TEXT("Head"))
		|| MatchTab(BodyTabButton, BodyTabLabel, TEXT("Body"))
		|| MatchTab(SkinTabButton, SkinTabLabel, TEXT("Skin"))
		|| MatchTab(HairTabButton, HairTabLabel, TEXT("Hair"))
		|| MatchTab(TattooTabButton, TattooTabLabel, TEXT("Tattoo"));
}

void UEFCharacterCreationRootWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildWidgetTree();
}

void UEFCharacterCreationRootWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EFCharacterCreationGameplayHooks::OnWidgetReady().Broadcast(this);
}

void UEFCharacterCreationRootWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const APlayerController* Player = GetOwningPlayer();
	if (!EditorPanelSlot || !Player || !Player->PlayerCameraManager) return;
	int32 Width = 0, Height = 0;
	Player->GetViewportSize(Width, Height);
	const FMinimalViewInfo& View = Player->PlayerCameraManager->GetCameraCacheView();
	const float LetterboxPixels = View.bConstrainAspectRatio && View.AspectRatio > 0.0f
		? FMath::Max(0.0f, (Height - Width / View.AspectRatio) * 0.5f) : 0.0f;
	const float Scale = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	const float Inset = FMath::Max(48.0f, LetterboxPixels / Scale + 16.0f);
	if (!FMath::IsNearlyEqual(EditorPanelSlot->GetPadding().Top, Inset, 0.5f))
	{
		EditorPanelSlot->SetPadding(FMargin(0.0f, Inset, 24.0f, Inset));
	}
}

FReply UEFCharacterCreationRootWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Period)
	{
		if (UEFCharacterCreationSubsystem* Subsystem = CharacterCreationSubsystem.Get())
		{
			CloseTattooShopInCharacterCreation();
			Subsystem->ToggleCharacterCreationMode();
			return FReply::Handled();
		}
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UEFCharacterCreationRootWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
	if (!IsPointerOverCameraViewport(ScreenPosition))
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	const FKey EffectingButton = InMouseEvent.GetEffectingButton();
	if (EffectingButton != EKeys::LeftMouseButton && EffectingButton != EKeys::RightMouseButton && EffectingButton != EKeys::MiddleMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	bIsOrbitDragging = EffectingButton == EKeys::LeftMouseButton || EffectingButton == EKeys::RightMouseButton;
	bIsPanDragging = EffectingButton == EKeys::MiddleMouseButton;
	LastPointerScreenPosition = ScreenPosition;

	if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
	{
		return FReply::Handled().CaptureMouse(CachedWidget.ToSharedRef());
	}

	return FReply::Handled();
}

FReply UEFCharacterCreationRootWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bIsOrbitDragging && !bIsPanDragging)
	{
		return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	}

	const FKey EffectingButton = InMouseEvent.GetEffectingButton();
	if ((bIsOrbitDragging && (EffectingButton == EKeys::LeftMouseButton || EffectingButton == EKeys::RightMouseButton))
		|| (bIsPanDragging && EffectingButton == EKeys::MiddleMouseButton))
	{
		StopCameraInteraction();
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UEFCharacterCreationRootWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bIsOrbitDragging && !bIsPanDragging)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}

	const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
	const FVector2D PointerDelta = ScreenPosition - LastPointerScreenPosition;
	LastPointerScreenPosition = ScreenPosition;

	if (UEFCharacterCreationSubsystem* Subsystem = CharacterCreationSubsystem.Get())
	{
		if (bIsOrbitDragging)
		{
			Subsystem->OrbitPreviewCamera(PointerDelta);
		}
		else if (bIsPanDragging)
		{
			Subsystem->PanPreviewCamera(PointerDelta);
		}
	}

	return FReply::Handled();
}

FReply UEFCharacterCreationRootWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!IsPointerOverCameraViewport(InMouseEvent.GetScreenSpacePosition()))
	{
		return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
	}

	if (UEFCharacterCreationSubsystem* Subsystem = CharacterCreationSubsystem.Get())
	{
		Subsystem->ZoomPreviewCamera(InMouseEvent.GetWheelDelta());
		return FReply::Handled();
	}

	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UEFCharacterCreationRootWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);

	if (!HasMouseCapture())
	{
		StopCameraInteraction();
	}
}

bool UEFCharacterCreationRootWidget::IsPointerOverCameraViewport(const FVector2D& ScreenSpacePosition) const
{
	return IsValid(CameraInteractionBorder) && CameraInteractionBorder->GetCachedGeometry().IsUnderLocation(ScreenSpacePosition);
}

void UEFCharacterCreationRootWidget::StopCameraInteraction()
{
	bIsOrbitDragging = false;
	bIsPanDragging = false;
}

UButton* UEFCharacterCreationRootWidget::CreateTextButton(const FString& Label, FLinearColor BackgroundColor, UTextBlock*& OutLabel) const
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Button->SetBackgroundColor(FLinearColor::White);
	FButtonStyle Style = Button->GetStyle();
	Style.Normal = FSlateRoundedBoxBrush(FLinearColor(0.06f, 0.065f, 0.08f, 0.96f), 6.0f, FLinearColor(0.6f, 0.62f, 0.65f, 0.6f), 1.0f);
	Style.Hovered = Style.Normal; Style.Pressed = Style.Normal; Style.Disabled = Style.Normal;
	Style.NormalPadding = FMargin(10, 7); Style.PressedPadding = FMargin(10, 7);
	Button->SetStyle(Style);

	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	OutLabel->SetText(FText::FromString(Label));
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	OutLabel->SetJustification(ETextJustify::Center);
	CharacterCreationRootWidgetPrivate::SetTextSize(OutLabel, 13);
	OutLabel->SetAutoWrapText(false);
	Button->SetContent(OutLabel);
	return Button;
}

UHorizontalBox* UEFCharacterCreationRootWidget::CreateColorSliderRow(const FString& Label, USlider*& OutSlider, float InitialValue)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	LabelText->SetText(FText::FromString(Label));
	CharacterCreationRootWidgetPrivate::SetTextSize(LabelText, 13);
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetPadding(FMargin(0.0f, 4.0f, 8.0f, 4.0f));
	}

	OutSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	OutSlider->SetMinValue(0.0f);
	OutSlider->SetMaxValue(1.0f);
	OutSlider->SetValue(InitialValue);
	if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(OutSlider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	return Row;
}

UHorizontalBox* UEFCharacterCreationRootWidget::CreateValueSliderRow(const FString& Label, TObjectPtr<USlider>& OutSlider, TObjectPtr<UEditableTextBox>& OutValueTextBox, float InitialMinValue, float InitialMaxValue, float InitialValue)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	LabelText->SetText(FText::FromString(Label));
	CharacterCreationRootWidgetPrivate::SetTextSize(LabelText, 13);
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText))
	{
		LabelSlot->SetPadding(FMargin(0.0f, 4.0f, 8.0f, 4.0f));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	OutSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	OutSlider->SetMinValue(InitialMinValue);
	OutSlider->SetMaxValue(InitialMaxValue);
	OutSlider->SetValue(InitialValue);
	if (UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(OutSlider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
	}

	OutValueTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	OutValueTextBox->SetText(FText::FromString(FString::SanitizeFloat(InitialValue)));
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(OutValueTextBox))
	{
		ValueSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 2.0f));
		ValueSlot->SetHorizontalAlignment(HAlign_Right);
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	}

	return Row;
}

UBorder* UEFCharacterCreationRootWidget::CreateColorPreviewSwatch(UVerticalBox* ParentBox)
{
	if (!IsValid(ParentBox))
	{
		return nullptr;
	}

	USizeBox* SwatchSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SwatchSize->SetWidthOverride(52.0f);
	SwatchSize->SetHeightOverride(52.0f);

	UBorder* SwatchBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), MakeUniqueObjectName(WidgetTree, UBorder::StaticClass(), TEXT("AppearanceSwatchNoTheme")));
	SwatchBorder->SetPadding(FMargin(0.0f));
	SwatchBorder->SetBrushColor(FLinearColor::White);
	SwatchSize->SetContent(SwatchBorder);

	if (UVerticalBoxSlot* SwatchSlot = ParentBox->AddChildToVerticalBox(SwatchSize))
	{
		SwatchSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 10.0f));
		SwatchSlot->SetHorizontalAlignment(HAlign_Left);
	}

	return SwatchBorder;
}

UTextBlock* UEFCharacterCreationRootWidget::CreateSectionHeader(const FString& SectionLabel) const
{
	UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Header->SetText(FText::FromString(SectionLabel));
	Header->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.92f, 0.96f, 1.0f)));
	FSlateFontInfo HeaderFont = Header->GetFont();
	HeaderFont.Size = 15;
	Header->SetFont(HeaderFont);
	return Header;
}

void UEFCharacterCreationRootWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UHorizontalBox* RootLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RootLayout"));
	WidgetTree->RootWidget = RootLayout;

	UVerticalBox* LeftPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftPanel"));
	if (UHorizontalBoxSlot* LeftSlot = RootLayout->AddChildToHorizontalBox(LeftPanel))
	{
		FSlateChildSize LeftSize(ESlateSizeRule::Fill);
		LeftSize.Value = 0.55f;
		LeftSlot->SetSize(LeftSize);
		LeftSlot->SetPadding(FMargin(32.0f));
	}

	CameraInteractionBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CameraInteractionBorder"));
	CameraInteractionBorder->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.001f));
	USpacer* CharacterSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("CharacterSpacer"));
	CameraInteractionBorder->SetContent(CharacterSpacer);
	if (UVerticalBoxSlot* SpacerSlot = LeftPanel->AddChildToVerticalBox(CameraInteractionBorder))
	{
		SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}


	USizeBox* RightPanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RightPanelSize"));

	if (UHorizontalBoxSlot* RightSlot = RootLayout->AddChildToHorizontalBox(RightPanelSize))
	{
		EditorPanelSlot = RightSlot;
		RightSlot->SetPadding(FMargin(0.0f, 48.0f, 24.0f, 48.0f));
		FSlateChildSize RightSize(ESlateSizeRule::Fill);
		RightSize.Value = 0.45f;
		RightSlot->SetSize(RightSize);
	}

	UBorder* RightPanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RightPanelBorder"));
	RightPanelBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.025f, 0.03f, 0.04f, 0.96f), 16.0f, FLinearColor(0.6f, 0.62f, 0.66f, 0.8f), 1.0f));
	RightPanelBorder->SetBrushColor(FLinearColor::White);
	RightPanelBorder->SetPadding(FMargin(24.0f));
	UOverlay* PanelOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CreationPanelOverlay"));
	RightPanelSize->SetContent(PanelOverlay);
	UOverlaySlot* PanelSlot = PanelOverlay->AddChildToOverlay(RightPanelBorder);
	PanelSlot->SetHorizontalAlignment(HAlign_Fill);
	PanelSlot->SetVerticalAlignment(VAlign_Fill);
	// A vector outline keeps a constant stroke and corner radius at every aspect
	// ratio. Stretching the landscape Chronicle frame caused doubled, jagged arcs.
	UImage* Frame = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CreationFrameOutlineImage"));
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	Frame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 11.0f,
		FLinearColor(0.6f, 0.62f, 0.66f, 0.38f), 1.0f));
	UOverlaySlot* FrameSlot = PanelOverlay->AddChildToOverlay(Frame);
	FrameSlot->SetHorizontalAlignment(HAlign_Fill);
	FrameSlot->SetVerticalAlignment(VAlign_Fill);
	FrameSlot->SetPadding(FMargin(7.0f));

	UVerticalBox* RightPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RightPanel"));
	RightPanelBorder->SetContent(RightPanel);
	UTextBlock* Title = CreateSectionHeader(TEXT("CHARACTER CREATION"));
	Title->Rename(TEXT("CreationTitle"), WidgetTree);
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 26;
	TitleFont.TypefaceFontName = TEXT("Regular");
	Title->SetFont(TitleFont);
	RightPanel->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 8));
	USizeBox* DividerSize = WidgetTree->ConstructWidget<USizeBox>();
	DividerSize->SetHeightOverride(8);
	UImage* Divider = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CreationDivider"));
	Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
	Divider->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Divider.T_Chronicle_Divider")));
	DividerSize->SetContent(Divider);
	RightPanel->AddChildToVerticalBox(DividerSize)->SetPadding(FMargin(0, 0, 0, 12));

	UHorizontalBox* ToggleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ToggleRow"));
	RightPanel->AddChildToVerticalBox(ToggleRow);

	ShowClothesCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("ShowClothesCheckBox"));
	ShowClothesCheckBox->OnCheckStateChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleShowClothesChanged);
	ToggleRow->AddChildToHorizontalBox(ShowClothesCheckBox);
	UTextBlock* ShowClothesLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ShowClothesLabel->SetText(FText::FromString(TEXT("Show clothes")));
	CharacterCreationRootWidgetPrivate::SetTextSize(ShowClothesLabel, 13);
	if (UHorizontalBoxSlot* LabelSlot = ToggleRow->AddChildToHorizontalBox(ShowClothesLabel))
	{
		LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 16.0f, 0.0f));
	}

	PauseAnimationCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("PauseAnimationCheckBox"));
	PauseAnimationCheckBox->OnCheckStateChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandlePauseAnimationChanged);
	ToggleRow->AddChildToHorizontalBox(PauseAnimationCheckBox);
	UTextBlock* PauseAnimationLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	PauseAnimationLabel->SetText(FText::FromString(TEXT("Pause animation")));
	CharacterCreationRootWidgetPrivate::SetTextSize(PauseAnimationLabel, 13);
	if (UHorizontalBoxSlot* LabelSlot = ToggleRow->AddChildToHorizontalBox(PauseAnimationLabel))
	{
		LabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
	}

	UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabRow"));
	if (UVerticalBoxSlot* TabRowSlot = RightPanel->AddChildToVerticalBox(TabRow))
	{
		TabRowSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 12.0f));
	}

	UTextBlock* InfoTabLabelRaw = nullptr;
	InfoTabButton = CreateTextButton(TEXT("Info"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), InfoTabLabelRaw);
	InfoTabLabel = InfoTabLabelRaw;
	InfoTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleInfoTabClicked);
	if (UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(InfoTabButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* HeadTabLabelRaw = nullptr;
	HeadTabButton = CreateTextButton(TEXT("Head"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), HeadTabLabelRaw);
	HeadTabLabel = HeadTabLabelRaw;
	HeadTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHeadTabClicked);
	if (UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(HeadTabButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* BodyTabLabelRaw = nullptr;
	BodyTabButton = CreateTextButton(TEXT("Body"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), BodyTabLabelRaw);
	BodyTabLabel = BodyTabLabelRaw;
	BodyTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleBodyTabClicked);
	if (UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(BodyTabButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* SkinTabLabelRaw = nullptr;
	SkinTabButton = CreateTextButton(TEXT("Skin"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), SkinTabLabelRaw);
	SkinTabLabel = SkinTabLabelRaw;
	SkinTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSkinTabClicked);
	if (UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(SkinTabButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* HairTabLabelRaw = nullptr;
	HairTabButton = CreateTextButton(TEXT("Hair"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), HairTabLabelRaw);
	HairTabLabel = HairTabLabelRaw;
	HairTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairTabClicked);
	if (UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(HairTabButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* TattooTabLabelRaw = nullptr;
	TattooTabButton = CreateTextButton(TEXT("Tattoo"), FLinearColor(0.16f, 0.16f, 0.16f, 1.0f), TattooTabLabelRaw);
	TattooTabLabel = TattooTabLabelRaw;
	TattooTabButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleTattooTabClicked);
	TabRow->AddChildToHorizontalBox(TattooTabButton);
	for (UWidget* Tab : TabRow->GetAllChildren())
	{
		if (UHorizontalBoxSlot* TabSlot = Cast<UHorizontalBoxSlot>(Tab->Slot))
		{
			TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TabSlot->SetPadding(FMargin(0, 0, 4, 0));
		}
	}

	SearchTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SearchTextBox"));
	SearchTextBox->SetHintText(FText::FromString(TEXT("Search this tab...")));
	SearchTextBox->OnTextChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSearchTextChanged);
	if (UVerticalBoxSlot* SearchSlot = RightPanel->AddChildToVerticalBox(SearchTextBox))
	{
		SearchSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	ErrorTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ErrorText"));
	ErrorTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.35f, 0.35f, 1.0f)));
	CharacterCreationRootWidgetPrivate::SetTextSize(ErrorTextBlock, 13);
	ErrorTextBlock->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ErrorSlot = RightPanel->AddChildToVerticalBox(ErrorTextBlock))
	{
		ErrorSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	MorphWorkspace = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MorphWorkspace"));
	RightPanel->AddChildToVerticalBox(MorphWorkspace)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SectionScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SectionNavigation"));
	SectionScrollBox->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	UHorizontalBoxSlot* NavigationSlot = MorphWorkspace->AddChildToHorizontalBox(SectionScrollBox);
	FSlateChildSize NavigationSize(ESlateSizeRule::Fill); NavigationSize.Value = 0.28f;
	NavigationSlot->SetSize(NavigationSize);
	NavigationSlot->SetPadding(FMargin(0, 0, 12, 0));
	MorphScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("MorphScrollBox"));
	MorphScrollBox->OnUserScrolled.AddDynamic(this, &ThisClass::HandleMorphScrolled);
	UHorizontalBoxSlot* ControlsSlot = MorphWorkspace->AddChildToHorizontalBox(MorphScrollBox);
	FSlateChildSize ControlsSize(ESlateSizeRule::Fill); ControlsSize.Value = 0.72f;
	ControlsSlot->SetSize(ControlsSize);
	MorphScrollBox->SetClipping(EWidgetClipping::ClipToBoundsAlways);

	TattooHostFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TattooHostFrame"));
	TattooHostFrame->SetVisibility(ESlateVisibility::Collapsed);
	RightPanel->AddChildToVerticalBox(TattooHostFrame)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	// The management and workspace widgets retain their internal behavior. Each
	// has an independent vertical scroll; a shared horizontal scroll keeps legacy
	// fixed-width children reachable without covering the character or footer.
	UScrollBox* TattooHorizontalScroll = WidgetTree->ConstructWidget<UScrollBox>();
	TattooHorizontalScroll->SetOrientation(Orient_Horizontal);
	TattooHostFrame->SetContent(TattooHorizontalScroll);
	UHorizontalBox* TattooColumns = WidgetTree->ConstructWidget<UHorizontalBox>();
	TattooHorizontalScroll->AddChild(TattooColumns);
	auto MakeTattooColumn = [this, TattooColumns](const TCHAR* Name, float Width)
	{
		USizeBox* ColumnSize = WidgetTree->ConstructWidget<USizeBox>();
		ColumnSize->SetWidthOverride(Width);
		TattooColumns->AddChildToHorizontalBox(ColumnSize)->SetPadding(FMargin(0, 0, 10, 0));
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		ColumnSize->SetContent(Scroll);
		UVerticalBox* Host = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), Name);
		Scroll->AddChild(Host);
		return Host;
	};
	TattooShopHostBox = MakeTattooColumn(TEXT("TattooShopHostBox"), 320);
	TattooAssetPreviewerHostBox = MakeTattooColumn(TEXT("TattooAssetPreviewerHostBox"), 400);
	ApplyTattooLayoutSettings();

	RandomDefaultsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RandomDefaultsRow"));
	RightPanel->AddChildToVerticalBox(RandomDefaultsRow);

	UTextBlock* RandomLabel = nullptr;
	RandomButton = CreateTextButton(TEXT("Randomize All"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), RandomLabel);
	RandomButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleRandomClicked);
	if (UHorizontalBoxSlot* ButtonSlot = RandomDefaultsRow->AddChildToHorizontalBox(RandomButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* DefaultsLabel = nullptr;
	DefaultsButton = CreateTextButton(TEXT("Reset All"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), DefaultsLabel);
	DefaultsButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleDefaultsClicked);
	if (UHorizontalBoxSlot* ButtonSlot = RandomDefaultsRow->AddChildToHorizontalBox(DefaultsButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	PresetsArea = WidgetTree->ConstructWidget<UExpandableArea>(UExpandableArea::StaticClass(), TEXT("PresetsArea"));
	PresetsArea->SetContentForSlot(TEXT("Header"), CreateSectionHeader(TEXT("PRESETS")));
	UVerticalBox* PresetsBody = WidgetTree->ConstructWidget<UVerticalBox>();
	PresetsArea->SetContentForSlot(TEXT("Body"), PresetsBody);
	PresetsArea->SetIsExpanded(false);
	PresetsArea->SetHeaderPadding(FMargin(0, 8));
	RightPanel->AddChildToVerticalBox(PresetsArea);
	PresetNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PresetNameTextBox"));
	PresetNameTextBox->SetHintText(FText::FromString(TEXT("Enter preset name")));
	if (UVerticalBoxSlot* PresetNameSlot = PresetsBody->AddChildToVerticalBox(PresetNameTextBox))
	{
		PresetNameSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 8.0f));
	}

	PresetActionsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PresetActionsRow"));
	PresetsBody->AddChildToVerticalBox(PresetActionsRow);

	UTextBlock* SavePresetLabel = nullptr;
	SavePresetButton = CreateTextButton(TEXT("Save as preset"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), SavePresetLabel);
	SavePresetButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSavePresetClicked);
	if (UHorizontalBoxSlot* ButtonSlot = PresetActionsRow->AddChildToHorizontalBox(SavePresetButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	UTextBlock* LoadPresetLabel = nullptr;
	LoadPresetButton = CreateTextButton(TEXT("Load Preset"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), LoadPresetLabel);
	LoadPresetButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleLoadPresetClicked);
	if (UHorizontalBoxSlot* ButtonSlot = PresetActionsRow->AddChildToHorizontalBox(LoadPresetButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	PresetComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("PresetComboBox"));
	PresetComboBox->OnSelectionChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandlePresetSelectionChanged);
	if (UVerticalBoxSlot* PresetComboSlot = PresetsBody->AddChildToVerticalBox(PresetComboBox))
	{
		PresetComboSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}
	UHorizontalBox* LeftActions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CreationFooterActions"));
	if (UVerticalBoxSlot* ActionsSlot = RightPanel->AddChildToVerticalBox(LeftActions))
	{
		ActionsSlot->SetHorizontalAlignment(HAlign_Fill);
		ActionsSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));
	}

	UTextBlock* StartGameLabel = nullptr;
	StartGameButton = CreateTextButton(TEXT("Apply"), FLinearColor(0.25f, 0.19f, 0.14f, 0.92f), StartGameLabel);
	StartGameButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleStartGameClicked);
	if (UHorizontalBoxSlot* ButtonSlot = LeftActions->AddChildToHorizontalBox(StartGameButton))
	{
		ButtonSlot->SetPadding(FMargin(0, 0, 8, 0));
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UTextBlock* BackLabel = nullptr;
	BackButton = CreateTextButton(TEXT("Cancel"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), BackLabel);
	BackButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleBackClicked);
	LeftActions->AddChildToHorizontalBox(BackButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

}

void UEFCharacterCreationRootWidget::RefreshPresetList()
{
	if (!IsValid(PresetComboBox) || !CustomizationComponent.IsValid())
	{
		return;
	}

	PresetComboBox->ClearOptions();
	for (const FString& PresetName : CustomizationComponent->GetPresetNames())
	{
		PresetComboBox->AddOption(PresetName);
	}

	if (!SelectedPresetName.IsEmpty())
	{
		PresetComboBox->SetSelectedOption(SelectedPresetName);
	}
}

void UEFCharacterCreationRootWidget::RefreshTabVisuals()
{
	const UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get();
	auto ApplyTabStyle = [this](UButton* Button, UTextBlock* Label, const FName TabName)
	{
		if (!IsValid(Button) || !IsValid(Label))
		{
			return;
		}

		const bool bIsActive = ActiveCategory == TabName;
		Button->SetBackgroundColor(bIsActive ? FLinearColor(0.35f, 0.23f, 0.11f, 1.0f) : FLinearColor(0.16f, 0.16f, 0.16f, 1.0f));
		Label->SetColorAndOpacity(FSlateColor(bIsActive ? FLinearColor(1.0f, 0.9f, 0.6f, 1.0f) : FLinearColor::White));
	};

	ApplyTabStyle(InfoTabButton, InfoTabLabel, TEXT("Info"));
	ApplyTabStyle(HeadTabButton, HeadTabLabel, TEXT("Head"));
	ApplyTabStyle(BodyTabButton, BodyTabLabel, TEXT("Body"));
	ApplyTabStyle(SkinTabButton, SkinTabLabel, TEXT("Skin"));
	ApplyTabStyle(HairTabButton, HairTabLabel, TEXT("Hair"));
	ApplyTabStyle(TattooTabButton, TattooTabLabel, TEXT("Tattoo"));

	if (IsValid(HairTabButton))
	{
		HairTabButton->SetVisibility(Customization && Customization->HasHairCustomization() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UEFCharacterCreationRootWidget::BuildInfoPanel()
{
	if (!IsValid(MorphScrollBox) || !CustomizationComponent.IsValid())
	{
		return;
	}

	UBorder* InfoBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InfoPanelBorder"));
	InfoBorder->SetPadding(FMargin(12.0f));
	InfoBorder->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.12f, 0.82f));
	if (UScrollBoxSlot* InfoSlot = Cast<UScrollBoxSlot>(MorphScrollBox->AddChild(InfoBorder)))
	{
		InfoSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	UVerticalBox* InfoBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InfoPanelBox"));
	InfoBorder->SetContent(InfoBox);
	InfoBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Name")));

	CharacterNameTextBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("CharacterNameTextBox"));
	CharacterNameTextBox->SetHintText(FText::FromString(TEXT("Name")));
	bIsRefreshingIdentityControls = true;
	CharacterNameTextBox->SetText(FText::FromString(CustomizationComponent->GetCharacterName()));
	bIsRefreshingIdentityControls = false;
	CharacterNameTextBox->OnTextChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleCharacterNameTextChanged);
	if (UVerticalBoxSlot* NameSlot = InfoBox->AddChildToVerticalBox(CharacterNameTextBox))
	{
		NameSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 14.0f));
	}

	InfoBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Gender")));
	UHorizontalBox* GenderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("GenderRow"));
	if (UVerticalBoxSlot* GenderSlot = InfoBox->AddChildToVerticalBox(GenderRow))
	{
		GenderSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	const ECharacterCreationGender CurrentGender = CustomizationComponent->GetGender();
	const auto GenderButtonColor = [CurrentGender](ECharacterCreationGender Gender)
	{
		return CurrentGender == Gender ? FLinearColor(0.62f, 0.16f, 0.46f, 1.0f) : FLinearColor(0.16f, 0.12f, 0.20f, 1.0f);
	};

	UTextBlock* GenderMaleLabelRaw = nullptr;
	GenderMaleButton = CreateTextButton(TEXT("Male"), GenderButtonColor(ECharacterCreationGender::Male), GenderMaleLabelRaw);
	GenderMaleLabel = GenderMaleLabelRaw;
	GenderMaleButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleGenderMaleClicked);
	if (UHorizontalBoxSlot* ButtonSlot = GenderRow->AddChildToHorizontalBox(GenderMaleButton))
	{
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UTextBlock* GenderFemaleLabelRaw = nullptr;
	GenderFemaleButton = CreateTextButton(TEXT("Female"), GenderButtonColor(ECharacterCreationGender::Female), GenderFemaleLabelRaw);
	GenderFemaleLabel = GenderFemaleLabelRaw;
	GenderFemaleButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleGenderFemaleClicked);
	if (UHorizontalBoxSlot* ButtonSlot = GenderRow->AddChildToHorizontalBox(GenderFemaleButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
}

FString UEFCharacterCreationRootWidget::NavigationKey() const
{
	const FString Section = SelectedSections.FindRef(ActiveCategory);
	const FString Search = SearchTextBox ? SearchTextBox->GetText().ToString() : FString();
	return ActiveCategory.ToString() + TEXT("/") + (Search.IsEmpty() ? Section : TEXT("Search"));
}

void UEFCharacterCreationRootWidget::HandleMorphScrolled(float Offset)
{
	if (!bChangingNavigation) SectionScrollOffsets.Add(NavigationKey(), Offset);
}

void UEFCharacterCreationRootWidget::SelectPresentationCategory(FName Category)
{
	if (Category == TEXT("Info") || Category == TEXT("Head") || Category == TEXT("Body") || Category == TEXT("Skin") || Category == TEXT("Hair") || Category == TEXT("Tattoo"))
	{
		if (Category == TEXT("Tattoo")) HandleTattooTabClicked();
		else SetActiveCategory(Category);
	}
}

void UEFCharacterCreationRootWidget::SelectPresentationSection(const FString& Section)
{
	if (Section != TEXT("All") && !SectionButtons.ContainsByPredicate([&](const UEFCharacterCreationSectionButton* Button) { return Button && Button->Section == Section; })) return;
	SectionScrollOffsets.Add(NavigationKey(), MorphScrollBox->GetScrollOffset());
	SelectedSections.Add(ActiveCategory, Section);
	{
		TGuardValue<bool> Guard(bChangingNavigation, true);
		SearchTextBox->SetText(FText::GetEmpty());
	}
	RefreshMorphList();
}

TArray<FMorphSliderEntry> UEFCharacterCreationRootWidget::GetDisplayedMorphEntries() const
{
	if (!CustomizationComponent.IsValid() || (ActiveCategory != TEXT("Head") && ActiveCategory != TEXT("Body"))) return {};
	const FString Search = SearchTextBox ? SearchTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	TArray<FMorphSliderEntry> Entries = CustomizationComponent->GetAvailableMorphEntriesForCategory(ActiveCategory, Search);
	const FString Selected = SelectedSections.FindRef(ActiveCategory);
	if (Search.IsEmpty() && !Selected.IsEmpty() && Selected != TEXT("All"))
	{
		Entries.RemoveAll([&](const FMorphSliderEntry& Entry) { return Entry.Section != Selected; });
	}
	return Entries;
}

void UEFCharacterCreationRootWidget::BuildSectionNavigation()
{
	if (!SectionScrollBox) return;
	const bool bAnatomical = ActiveCategory == TEXT("Head") || ActiveCategory == TEXT("Body");
	SectionScrollBox->SetVisibility(bAnatomical ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SectionButtons.Reset();
	SectionScrollBox->ClearChildren();
	if (!bAnatomical || !CustomizationComponent.IsValid()) return;
	TMap<FString, int32> Counts;
	const TArray<FMorphSliderEntry> Entries = CustomizationComponent->GetAvailableMorphEntriesForCategory(ActiveCategory, FString());
	for (const FMorphSliderEntry& Entry : Entries) ++Counts.FindOrAdd(Entry.Section);
	FString& Selected = SelectedSections.FindOrAdd(ActiveCategory);
	if (Selected.IsEmpty() || (Selected != TEXT("All") && !Counts.Contains(Selected))) Selected = TEXT("All");
	TArray<FString> Ordered = {TEXT("All")};
	for (const FString& Section : EFMorphPresentation::Sections(ActiveCategory)) if (Counts.Contains(Section)) Ordered.Add(Section);
	TArray<FString> Extra;
	for (const auto& Pair : Counts) if (!Ordered.Contains(Pair.Key)) Extra.Add(Pair.Key);
	Extra.Sort(); Ordered.Append(Extra);
	for (const FString& Section : Ordered)
	{
		UEFCharacterCreationSectionButton* Button = WidgetTree->ConstructWidget<UEFCharacterCreationSectionButton>();
		Button->InitializeSection(Section);
		Button->OnSectionSelected.BindUObject(this, &ThisClass::SelectPresentationSection);
		Button->SetBackgroundColor(FLinearColor::White);
		FButtonStyle Style = Button->GetStyle();
		Style.Normal = FSlateRoundedBoxBrush(FLinearColor(0.04f, 0.045f, 0.06f, 0.98f), 6.0f, FLinearColor(0.4f, 0.42f, 0.46f, 0.5f), 1.0f);
		Style.Hovered = Style.Normal; Style.Pressed = Style.Normal;
		Style.NormalPadding = FMargin(10, 9); Style.PressedPadding = FMargin(10, 9);
		Button->SetStyle(Style);
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Section)); Label->SetAutoWrapText(true);
		CharacterCreationRootWidgetPrivate::SetTextSize(Label, 13);
		Button->SetContent(Label);
		Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s - %d morphs"), *Section, Section == TEXT("All") ? Entries.Num() : Counts.FindRef(Section))));
		Cast<UScrollBoxSlot>(SectionScrollBox->AddChild(Button))->SetPadding(FMargin(0, 0, 4, 5));
		SectionButtons.Add(Button);
	}
}

void UEFCharacterCreationRootWidget::RefreshMorphList()
{
	if (!IsValid(MorphScrollBox))
	{
		return;
	}

	ON_SCOPE_EXIT
	{
		// Dynamic panels and tab-state colors are rebuilt throughout the
		// session. Notify project-owned presentation adapters only after the
		// complete tree is stable so the selected native palette remains
		// authoritative without a polling/tint overlay.
		MorphScrollBox->SetScrollOffset(SectionScrollOffsets.FindRef(NavigationKey()));
		EFCharacterCreationGameplayHooks::OnWidgetReady().Broadcast(this);
	};

	MorphScrollBox->ClearChildren();
	BuildSectionNavigation();

	UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get();
	if (!Customization)
	{
		return;
	}

	const bool bIsInfoCategory = ActiveCategory == TEXT("Info");
	const bool bIsTattooCategory = ActiveCategory == TEXT("Tattoo");
	if (IsValid(TattooHostFrame))
	{
		TattooHostFrame->SetVisibility(bIsTattooCategory ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(MorphScrollBox))
	{
		MorphScrollBox->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		MorphWorkspace->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		PresetsArea->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(RandomDefaultsRow))
	{
		RandomDefaultsRow->SetVisibility((bIsTattooCategory || bIsInfoCategory) ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(PresetNameTextBox))
	{
		PresetNameTextBox->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(PresetActionsRow))
	{
		PresetActionsRow->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(PresetComboBox))
	{
		PresetComboBox->SetVisibility(bIsTattooCategory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (bIsTattooCategory)
	{
		if (IsValid(SearchTextBox))
		{
			SearchTextBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(ErrorTextBlock))
		{
			ErrorTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (bIsInfoCategory)
	{
		if (IsValid(SearchTextBox))
		{
			SearchTextBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(ErrorTextBlock))
		{
			ErrorTextBlock->SetVisibility(Customization->IsMeshCompatible() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
			ErrorTextBlock->SetText(FText::FromString(Customization->GetCompatibilityError()));
		}
		BuildInfoPanel();
		return;
	}

	if (!Customization->IsMeshCompatible())
	{
		ErrorTextBlock->SetVisibility(ESlateVisibility::Visible);
		ErrorTextBlock->SetText(FText::FromString(Customization->GetCompatibilityError()));
		return;
	}

	ErrorTextBlock->SetVisibility(ESlateVisibility::Collapsed);

	if (IsValid(SearchTextBox))
	{
		SearchTextBox->SetVisibility((ActiveCategory == TEXT("Head") || ActiveCategory == TEXT("Body")) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (ActiveCategory == TEXT("Hair"))
	{
		UBorder* HairBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		HairBorder->SetPadding(FMargin(12.0f));
		HairBorder->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.08f, 0.82f));
		MorphScrollBox->AddChild(HairBorder);

		UVerticalBox* HairBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		HairBorder->SetContent(HairBox);

		HairBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Hair Mesh")));

		UHorizontalBox* SelectorRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HairBox->AddChildToVerticalBox(SelectorRow);

		UTextBlock* PreviousHairLabel = nullptr;
		UButton* PreviousHairButton = CreateTextButton(TEXT("Previous"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), PreviousHairLabel);
		PreviousHairButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairPreviousClicked);
		if (UHorizontalBoxSlot* ButtonSlot = SelectorRow->AddChildToHorizontalBox(PreviousHairButton))
		{
			ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}

		UTextBlock* CurrentHairLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		CurrentHairLabel->SetText(FText::FromString(Customization->GetCurrentHairMeshDisplayName()));
		CharacterCreationRootWidgetPrivate::SetTextSize(CurrentHairLabel, 14);
		if (UHorizontalBoxSlot* LabelSlot = SelectorRow->AddChildToHorizontalBox(CurrentHairLabel))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}

		UTextBlock* NextHairLabel = nullptr;
		UButton* NextHairButton = CreateTextButton(TEXT("Next"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), NextHairLabel);
		NextHairButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairNextClicked);
		SelectorRow->AddChildToHorizontalBox(NextHairButton);

		UExpandableArea* AdvancedArea = WidgetTree->ConstructWidget<UExpandableArea>(UExpandableArea::StaticClass(), TEXT("HairAdvancedArea"));
		AdvancedArea->SetContentForSlot(TEXT("Header"), CreateSectionHeader(TEXT("Advanced")));
		AdvancedArea->SetHeaderPadding(FMargin(0, 12));
		AdvancedArea->SetIsExpanded(bHairEditRequested || bHairEditUnlocked);
		HairBox->AddChildToVerticalBox(AdvancedArea);
		UVerticalBox* AdvancedBox = WidgetTree->ConstructWidget<UVerticalBox>();
		AdvancedArea->SetContentForSlot(TEXT("Body"), AdvancedBox);
		UHorizontalBox* EditRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (UVerticalBoxSlot* EditRowSlot = AdvancedBox->AddChildToVerticalBox(EditRow))
		{
			EditRowSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 8.0f));
		}

		HairEditCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		HairEditCheckBox->OnCheckStateChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairEditChanged);
		bIsUpdatingHairEditCheckBox = true;
		HairEditCheckBox->SetIsChecked(bHairEditUnlocked || bHairEditRequested);
		bIsUpdatingHairEditCheckBox = false;
		EditRow->AddChildToHorizontalBox(HairEditCheckBox);

		UTextBlock* EditLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		EditLabel->SetText(FText::FromString(TEXT("Advanced transform")));
		CharacterCreationRootWidgetPrivate::SetTextSize(EditLabel, 13);
		if (UHorizontalBoxSlot* EditLabelSlot = EditRow->AddChildToHorizontalBox(EditLabel))
		{
			EditLabelSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			EditLabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (bHairEditRequested)
		{
			UBorder* WarningBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			WarningBorder->SetPadding(FMargin(10.0f));
			WarningBorder->SetBrushColor(FLinearColor(0.22f, 0.12f, 0.08f, 0.95f));
			if (UVerticalBoxSlot* WarningSlot = AdvancedBox->AddChildToVerticalBox(WarningBorder))
			{
				WarningSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
			}

			UVerticalBox* WarningBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			WarningBorder->SetContent(WarningBox);

			UTextBlock* WarningText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			WarningText->SetText(FText::FromString(TEXT("Editing the hair transform will probably damage the game's animations. Continue?")));
			WarningText->SetAutoWrapText(true);
			WarningBox->AddChildToVerticalBox(WarningText);

			UHorizontalBox* WarningButtons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* ButtonsSlot = WarningBox->AddChildToVerticalBox(WarningButtons))
			{
				ButtonsSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			}

			UTextBlock* WarningOkLabel = nullptr;
			UButton* WarningOkButton = CreateTextButton(TEXT("Ok"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), WarningOkLabel);
			WarningOkButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairEditWarningOkClicked);
			if (UHorizontalBoxSlot* OkSlot = WarningButtons->AddChildToHorizontalBox(WarningOkButton))
			{
				OkSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			}

			UTextBlock* WarningCancelLabel = nullptr;
			UButton* WarningCancelButton = CreateTextButton(TEXT("Cancel"), FLinearColor(0.20f, 0.17f, 0.12f, 1.0f), WarningCancelLabel);
			WarningCancelButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairEditWarningCancelClicked);
			WarningButtons->AddChildToHorizontalBox(WarningCancelButton);
		}

		AdvancedBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Location")));
		UHorizontalBox* LocationXRow = CreateValueSliderRow(TEXT("X"), HairLocationXSlider, HairLocationXTextBox, HairLocationXMinValue, HairLocationXMaxValue, 0.0f);
		HairLocationXSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationXChanged);
		HairLocationXTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationXTextCommitted);
		AdvancedBox->AddChildToVerticalBox(LocationXRow);

		UHorizontalBox* LocationYRow = CreateValueSliderRow(TEXT("Y"), HairLocationYSlider, HairLocationYTextBox, HairLocationYMinValue, HairLocationYMaxValue, 0.0f);
		HairLocationYSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationYChanged);
		HairLocationYTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationYTextCommitted);
		AdvancedBox->AddChildToVerticalBox(LocationYRow);

		UHorizontalBox* LocationZRow = CreateValueSliderRow(TEXT("Z"), HairLocationZSlider, HairLocationZTextBox, HairLocationZMinValue, HairLocationZMaxValue, 0.0f);
		HairLocationZSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationZChanged);
		HairLocationZTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairLocationZTextCommitted);
		AdvancedBox->AddChildToVerticalBox(LocationZRow);

		AdvancedBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Rotation")));
		UHorizontalBox* PitchRow = CreateValueSliderRow(TEXT("Pitch"), HairPitchSlider, HairPitchTextBox, HairPitchMinValue, HairPitchMaxValue, 0.0f);
		HairPitchSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairPitchChanged);
		HairPitchTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairPitchTextCommitted);
		AdvancedBox->AddChildToVerticalBox(PitchRow);

		UHorizontalBox* YawRow = CreateValueSliderRow(TEXT("Yaw"), HairYawSlider, HairYawTextBox, HairYawMinValue, HairYawMaxValue, 0.0f);
		HairYawSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairYawChanged);
		HairYawTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairYawTextCommitted);
		AdvancedBox->AddChildToVerticalBox(YawRow);

		UHorizontalBox* RollRow = CreateValueSliderRow(TEXT("Roll"), HairRollSlider, HairRollTextBox, HairRollMinValue, HairRollMaxValue, 0.0f);
		HairRollSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairRollChanged);
		HairRollTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairRollTextCommitted);
		AdvancedBox->AddChildToVerticalBox(RollRow);

		AdvancedBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Scale")));
		UHorizontalBox* ScaleXRow = CreateValueSliderRow(TEXT("X"), HairScaleXSlider, HairScaleXTextBox, HairScaleXMinValue, HairScaleXMaxValue, 1.0f);
		HairScaleXSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleXChanged);
		HairScaleXTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleXTextCommitted);
		AdvancedBox->AddChildToVerticalBox(ScaleXRow);

		UHorizontalBox* ScaleYRow = CreateValueSliderRow(TEXT("Y"), HairScaleYSlider, HairScaleYTextBox, HairScaleYMinValue, HairScaleYMaxValue, 1.0f);
		HairScaleYSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleYChanged);
		HairScaleYTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleYTextCommitted);
		AdvancedBox->AddChildToVerticalBox(ScaleYRow);

		UHorizontalBox* ScaleZRow = CreateValueSliderRow(TEXT("Z"), HairScaleZSlider, HairScaleZTextBox, HairScaleZMinValue, HairScaleZMaxValue, 1.0f);
		HairScaleZSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleZChanged);
		HairScaleZTextBox->OnTextCommitted.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleHairScaleZTextCommitted);
		AdvancedBox->AddChildToVerticalBox(ScaleZRow);

		UpdateHairTransformControls();
		return;
	}

	if (ActiveCategory == TEXT("Skin"))
	{
		UBorder* SkinControlsBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		SkinControlsBorder->SetPadding(FMargin(14.0f));
		SkinControlsBorder->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.08f, 0.82f));
		MorphScrollBox->AddChild(SkinControlsBorder);

		UVerticalBox* SkinBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		SkinControlsBorder->SetContent(SkinBox);

		UHorizontalBox* SkinHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (UVerticalBoxSlot* HeaderSlot = SkinBox->AddChildToVerticalBox(SkinHeaderRow))
		{
			HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		}

		UTextBlock* SkinHeader = CreateSectionHeader(TEXT("Skin Tone"));
		if (UHorizontalBoxSlot* HeaderTextSlot = SkinHeaderRow->AddChildToHorizontalBox(SkinHeader))
		{
			HeaderTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HeaderTextSlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* ResetSkinLabel = nullptr;
		UButton* ResetSkinButton = CreateTextButton(TEXT("Reset"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), ResetSkinLabel);
		ResetSkinButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleResetSkinColorClicked);
		if (UHorizontalBoxSlot* ResetButtonSlot = SkinHeaderRow->AddChildToHorizontalBox(ResetSkinButton))
		{
			ResetButtonSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
			ResetButtonSlot->SetVerticalAlignment(VAlign_Center);
		}

		SkinPreviewBorder = CreateColorPreviewSwatch(SkinBox);

		USlider* SkinHueSliderRaw = nullptr;
		UHorizontalBox* SkinHueRow = CreateColorSliderRow(TEXT("Hue"), SkinHueSliderRaw, 0.0f);
		SkinHueSlider = SkinHueSliderRaw;
		SkinHueSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSkinHueChanged);
		SkinBox->AddChildToVerticalBox(SkinHueRow);

		USlider* SkinSaturationSliderRaw = nullptr;
		UHorizontalBox* SkinSaturationRow = CreateColorSliderRow(TEXT("Saturation"), SkinSaturationSliderRaw, 0.0f);
		SkinSaturationSlider = SkinSaturationSliderRaw;
		SkinSaturationSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSkinSaturationChanged);
		SkinBox->AddChildToVerticalBox(SkinSaturationRow);

		USlider* SkinValueSliderRaw = nullptr;
		UHorizontalBox* SkinValueRow = CreateColorSliderRow(TEXT("Value"), SkinValueSliderRaw, 0.0f);
		SkinValueSlider = SkinValueSliderRaw;
		SkinValueSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleSkinValueChanged);
		SkinBox->AddChildToVerticalBox(SkinValueRow);

		UBorder* IrisBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("IrisSectionBorder"));
		IrisBorder->SetPadding(FMargin(14.0f));
		IrisBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.12f, 0.9f));
		Cast<UScrollBoxSlot>(MorphScrollBox->AddChild(IrisBorder))->SetPadding(FMargin(0, 12, 0, 0));
		UVerticalBox* IrisBox = WidgetTree->ConstructWidget<UVerticalBox>();
		IrisBorder->SetContent(IrisBox);
		UHorizontalBox* IrisHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (UVerticalBoxSlot* HeaderSlot = IrisBox->AddChildToVerticalBox(IrisHeaderRow))
		{
			HeaderSlot->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 4.0f));
		}

		UTextBlock* IrisHeader = CreateSectionHeader(TEXT("Iris Color"));
		if (UHorizontalBoxSlot* HeaderTextSlot = IrisHeaderRow->AddChildToHorizontalBox(IrisHeader))
		{
			HeaderTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HeaderTextSlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* ResetIrisLabel = nullptr;
		UButton* ResetIrisButton = CreateTextButton(TEXT("Reset"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), ResetIrisLabel);
		ResetIrisButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleResetIrisColorClicked);
		if (UHorizontalBoxSlot* ResetButtonSlot = IrisHeaderRow->AddChildToHorizontalBox(ResetIrisButton))
		{
			ResetButtonSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
			ResetButtonSlot->SetVerticalAlignment(VAlign_Center);
		}

		IrisPreviewBorder = CreateColorPreviewSwatch(IrisBox);

		USlider* IrisHueSliderRaw = nullptr;
		UHorizontalBox* IrisHueRow = CreateColorSliderRow(TEXT("Hue"), IrisHueSliderRaw, 0.0f);
		IrisHueSlider = IrisHueSliderRaw;
		IrisHueSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleIrisHueChanged);
		IrisBox->AddChildToVerticalBox(IrisHueRow);

		USlider* IrisSaturationSliderRaw = nullptr;
		UHorizontalBox* IrisSaturationRow = CreateColorSliderRow(TEXT("Saturation"), IrisSaturationSliderRaw, 0.0f);
		IrisSaturationSlider = IrisSaturationSliderRaw;
		IrisSaturationSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleIrisSaturationChanged);
		IrisBox->AddChildToVerticalBox(IrisSaturationRow);

		USlider* IrisValueSliderRaw = nullptr;
		UHorizontalBox* IrisValueRow = CreateColorSliderRow(TEXT("Value"), IrisValueSliderRaw, 0.0f);
		IrisValueSlider = IrisValueSliderRaw;
		IrisValueSlider->OnValueChanged.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleIrisValueChanged);
		IrisBox->AddChildToVerticalBox(IrisValueRow);

		UpdateColorControls();
	}

	if (ActiveCategory == TEXT("Body") && Customization->CanCustomizeBodyMesh())
	{
		UBorder* GenderBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		GenderBorder->SetPadding(FMargin(12.0f));
		GenderBorder->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.08f, 0.82f));
		if (UScrollBoxSlot* GenderSlot = Cast<UScrollBoxSlot>(MorphScrollBox->AddChild(GenderBorder)))
		{
			GenderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		}

		UVerticalBox* GenderBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		GenderBorder->SetContent(GenderBox);
		GenderBox->AddChildToVerticalBox(CreateSectionHeader(TEXT("Body mesh")));

		UHorizontalBox* GenderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		GenderBox->AddChildToVerticalBox(GenderRow);

		UTextBlock* PreviousBodyLabel = nullptr;
		UButton* PreviousBodyButton = CreateTextButton(TEXT("Previous"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), PreviousBodyLabel);
		PreviousBodyButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleBodyMeshPreviousClicked);
		if (UHorizontalBoxSlot* BodyButtonSlot = GenderRow->AddChildToHorizontalBox(PreviousBodyButton))
		{
			BodyButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}

		UTextBlock* CurrentBodyLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		CurrentBodyLabel->SetText(FText::FromString(Customization->GetCurrentBodyMeshDisplayName()));
		CharacterCreationRootWidgetPrivate::SetTextSize(CurrentBodyLabel, 14);
		if (UHorizontalBoxSlot* BodyLabelSlot = GenderRow->AddChildToHorizontalBox(CurrentBodyLabel))
		{
			BodyLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BodyLabelSlot->SetVerticalAlignment(VAlign_Center);
			BodyLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		}

		UTextBlock* NextBodyLabel = nullptr;
		UButton* NextBodyButton = CreateTextButton(TEXT("Next"), FLinearColor(0.18f, 0.14f, 0.12f, 0.92f), NextBodyLabel);
		NextBodyButton->OnClicked.AddDynamic(this, &UEFCharacterCreationRootWidget::HandleBodyMeshNextClicked);
		GenderRow->AddChildToHorizontalBox(NextBodyButton);
	}

	const FString SearchFilter = IsValid(SearchTextBox) ? SearchTextBox->GetText().ToString() : FString();
	TArray<FMorphSliderEntry> Entries = GetDisplayedMorphEntries();
	const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
	TSubclassOf<UEFMorphSliderWidget> MorphSliderClass = UEFMorphSliderWidget::StaticClass();
	if (Settings->MorphSliderWidgetClass.IsValid() || !Settings->MorphSliderWidgetClass.ToSoftObjectPath().IsNull())
	{
		if (UClass* LoadedMorphSliderClass = Settings->MorphSliderWidgetClass.LoadSynchronous())
		{
			MorphSliderClass = LoadedMorphSliderClass;
		}
	}

	if (Entries.IsEmpty() && (ActiveCategory == TEXT("Head") || ActiveCategory == TEXT("Body")))
	{
		UTextBlock* Empty = CreateSectionHeader(TEXT("No matching morphs. Try another search."));
		Empty->SetAutoWrapText(true); MorphScrollBox->AddChild(Empty);
	}
	FString LastSection;
	for (const FMorphSliderEntry& Entry : Entries)
	{
		if (!Entry.Section.IsEmpty() && Entry.Section != LastSection)
		{
			UTextBlock* SectionHeader = CreateSectionHeader(ActiveCategory.ToString() + TEXT(" / ") + Entry.Section);
			SectionHeader->SetAutoWrapText(true);
			Cast<UScrollBoxSlot>(MorphScrollBox->AddChild(SectionHeader))->SetPadding(FMargin(0, 8, 0, 8));
			LastSection = Entry.Section;
		}

		UEFMorphSliderWidget* SliderWidget = WidgetTree->ConstructWidget<UEFMorphSliderWidget>(MorphSliderClass);
		SliderWidget->InitializeFromEntry(Entry, Customization->GetCurrentMorphValue(Entry));
		SliderWidget->OnMorphValueChanged.AddUObject(this, &UEFCharacterCreationRootWidget::HandleMorphValueChanged);
		SliderWidget->OnMorphResetRequested.AddUObject(this, &UEFCharacterCreationRootWidget::HandleMorphResetRequested);
		Cast<UScrollBoxSlot>(MorphScrollBox->AddChild(SliderWidget))->SetPadding(FMargin(0, 0, 6, 8));
	}
}

void UEFCharacterCreationRootWidget::UpdateColorPreview(UBorder* PreviewBorder, const FLinearColor& Color)
{
	if (IsValid(PreviewBorder))
	{
		PreviewBorder->SetBrushColor(Color);
	}
}

FLinearColor UEFCharacterCreationRootWidget::MakeColorFromHSVSliders(USlider* HueSlider, USlider* SaturationSlider, USlider* ValueSlider) const
{
	const float HueDegrees = (IsValid(HueSlider) ? HueSlider->GetValue() : 0.0f) * 360.0f;
	const float Saturation = IsValid(SaturationSlider) ? SaturationSlider->GetValue() : 0.0f;
	const float Value = IsValid(ValueSlider) ? ValueSlider->GetValue() : 0.0f;
	return FLinearColor(HueDegrees, Saturation, Value, 1.0f).HSVToLinearRGB();
}

void UEFCharacterCreationRootWidget::UpdateColorControls()
{
	if (!CustomizationComponent.IsValid())
	{
		return;
	}

	bIsRefreshingColorControls = true;

	auto ApplyColorToControls = [this](const FLinearColor& Color, UBorder* PreviewBorder, USlider* HueSlider, USlider* SaturationSlider, USlider* ValueSlider)
	{
		const FLinearColor HSVColor = Color.LinearRGBToHSV();
		UpdateColorPreview(PreviewBorder, Color);

		if (IsValid(HueSlider))
		{
			HueSlider->SetValue(FMath::Clamp(HSVColor.R / 360.0f, 0.0f, 1.0f));
		}

		if (IsValid(SaturationSlider))
		{
			SaturationSlider->SetValue(FMath::Clamp(HSVColor.G, 0.0f, 1.0f));
		}

		if (IsValid(ValueSlider))
		{
			ValueSlider->SetValue(FMath::Clamp(HSVColor.B, 0.0f, 1.0f));
		}
	};

	ApplyColorToControls(CustomizationComponent->GetSkinColor(), SkinPreviewBorder, SkinHueSlider, SkinSaturationSlider, SkinValueSlider);
	ApplyColorToControls(CustomizationComponent->GetIrisColor(), IrisPreviewBorder, IrisHueSlider, IrisSaturationSlider, IrisValueSlider);
	bIsRefreshingColorControls = false;
}

void UEFCharacterCreationRootWidget::ResetHairEditState()
{
	bHairEditRequested = false;
	bHairEditUnlocked = false;

	if (IsValid(HairEditCheckBox))
	{
		bIsUpdatingHairEditCheckBox = true;
		HairEditCheckBox->SetIsChecked(false);
		bIsUpdatingHairEditCheckBox = false;
	}
}

void UEFCharacterCreationRootWidget::UpdateHairTransformControl(USlider* Slider, UEditableTextBox* ValueTextBox, float Value, float& InOutMinValue, float& InOutMaxValue, bool bUpdateTextBox)
{
	if (Value < InOutMinValue)
	{
		InOutMinValue = Value;
	}

	if (Value > InOutMaxValue)
	{
		InOutMaxValue = Value;
	}

	if (FMath::IsNearlyEqual(InOutMinValue, InOutMaxValue))
	{
		InOutMaxValue = InOutMinValue + 0.01f;
	}

	if (IsValid(Slider))
	{
		Slider->SetMinValue(InOutMinValue);
		Slider->SetMaxValue(InOutMaxValue);
		Slider->SetValue(Value);
		Slider->SetIsEnabled(bHairEditUnlocked);
	}

	if (IsValid(ValueTextBox))
	{
		ValueTextBox->SetIsEnabled(bHairEditUnlocked);
		if (bUpdateTextBox)
		{
			ValueTextBox->SetText(FText::FromString(FString::SanitizeFloat(Value)));
		}
	}
}

void UEFCharacterCreationRootWidget::UpdateHairTransformControls()
{
	UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get();
	if (!Customization || !Customization->CanEditHairTransform())
	{
		return;
	}

	bIsRefreshingHairTransformControls = true;

	const FTransform HairTransform = Customization->GetHairRelativeTransform();
	const FVector Location = HairTransform.GetLocation();
	const FRotator Rotation = HairTransform.Rotator();
	const FVector Scale = HairTransform.GetScale3D();

	UpdateHairTransformControl(HairLocationXSlider, HairLocationXTextBox, Location.X, HairLocationXMinValue, HairLocationXMaxValue, true);
	UpdateHairTransformControl(HairLocationYSlider, HairLocationYTextBox, Location.Y, HairLocationYMinValue, HairLocationYMaxValue, true);
	UpdateHairTransformControl(HairLocationZSlider, HairLocationZTextBox, Location.Z, HairLocationZMinValue, HairLocationZMaxValue, true);
	UpdateHairTransformControl(HairPitchSlider, HairPitchTextBox, Rotation.Pitch, HairPitchMinValue, HairPitchMaxValue, true);
	UpdateHairTransformControl(HairYawSlider, HairYawTextBox, Rotation.Yaw, HairYawMinValue, HairYawMaxValue, true);
	UpdateHairTransformControl(HairRollSlider, HairRollTextBox, Rotation.Roll, HairRollMinValue, HairRollMaxValue, true);
	UpdateHairTransformControl(HairScaleXSlider, HairScaleXTextBox, Scale.X, HairScaleXMinValue, HairScaleXMaxValue, true);
	UpdateHairTransformControl(HairScaleYSlider, HairScaleYTextBox, Scale.Y, HairScaleYMinValue, HairScaleYMaxValue, true);
	UpdateHairTransformControl(HairScaleZSlider, HairScaleZTextBox, Scale.Z, HairScaleZMinValue, HairScaleZMaxValue, true);

	bIsRefreshingHairTransformControls = false;
}

void UEFCharacterCreationRootWidget::ApplyHairLocationX(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Location = HairTransform.GetLocation();
		Location.X = NewValue;
		HairTransform.SetLocation(Location);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairLocationY(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Location = HairTransform.GetLocation();
		Location.Y = NewValue;
		HairTransform.SetLocation(Location);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairLocationZ(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Location = HairTransform.GetLocation();
		Location.Z = NewValue;
		HairTransform.SetLocation(Location);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairPitch(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FRotator Rotation = HairTransform.Rotator();
		Rotation.Pitch = NewValue;
		HairTransform.SetRotation(Rotation.Quaternion());
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairYaw(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FRotator Rotation = HairTransform.Rotator();
		Rotation.Yaw = NewValue;
		HairTransform.SetRotation(Rotation.Quaternion());
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairRoll(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FRotator Rotation = HairTransform.Rotator();
		Rotation.Roll = NewValue;
		HairTransform.SetRotation(Rotation.Quaternion());
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairScaleX(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Scale = HairTransform.GetScale3D();
		Scale.X = NewValue;
		HairTransform.SetScale3D(Scale);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairScaleY(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Scale = HairTransform.GetScale3D();
		Scale.Y = NewValue;
		HairTransform.SetScale3D(Scale);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

void UEFCharacterCreationRootWidget::ApplyHairScaleZ(float NewValue)
{
	if (UEFCharacterCustomizationComponent* Customization = CustomizationComponent.Get())
	{
		FTransform HairTransform = Customization->GetHairRelativeTransform();
		FVector Scale = HairTransform.GetScale3D();
		Scale.Z = NewValue;
		HairTransform.SetScale3D(Scale);
		Customization->SetHairRelativeTransform(HairTransform);
		UpdateHairTransformControls();
	}
}

bool UEFCharacterCreationRootWidget::TryParseNumericText(const FText& InText, float& OutValue) const
{
	return FDefaultValueHelper::ParseFloat(InText.ToString(), OutValue);
}

void UEFCharacterCreationRootWidget::SetActiveCategory(FName NewCategory)
{
	if (MorphScrollBox) SectionScrollOffsets.Add(NavigationKey(), MorphScrollBox->GetScrollOffset());
	if (ActiveCategory == TEXT("Tattoo") && NewCategory != TEXT("Tattoo"))
	{
		CloseTattooShopInCharacterCreation();
	}

	ActiveCategory = NewCategory;
	{
		TGuardValue<bool> Guard(bChangingNavigation, true);
		if (SearchTextBox) SearchTextBox->SetText(FText::GetEmpty());
	}
	RefreshTabVisuals();
	RefreshMorphList();
}

void UEFCharacterCreationRootWidget::HandleShowClothesChanged(bool bIsChecked)
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->SetShowClothes(bIsChecked);
	}
}

void UEFCharacterCreationRootWidget::HandlePauseAnimationChanged(bool bIsChecked)
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->SetPauseAnimation(bIsChecked);
	}
}

void UEFCharacterCreationRootWidget::HandleInfoTabClicked()
{
	SetActiveCategory(TEXT("Info"));
}

void UEFCharacterCreationRootWidget::HandleHeadTabClicked()
{
	SetActiveCategory(TEXT("Head"));
}

void UEFCharacterCreationRootWidget::HandleBodyTabClicked()
{
	SetActiveCategory(TEXT("Body"));
}

void UEFCharacterCreationRootWidget::HandleSkinTabClicked()
{
	SetActiveCategory(TEXT("Skin"));
}

void UEFCharacterCreationRootWidget::HandleHairTabClicked()
{
	SetActiveCategory(TEXT("Hair"));
}

void UEFCharacterCreationRootWidget::HandleTattooTabClicked()
{
	SetActiveCategory(TEXT("Tattoo"));
	OpenTattooShopInCharacterCreation();
}

bool UEFCharacterCreationRootWidget::OpenTattooTabForAutomation()
{
	SetActiveCategory(TEXT("Tattoo"));
	OpenTattooShopInCharacterCreation();
	return IsValid(TattooHostFrame) && IsValid(TattooShopHostBox);
}

UObject* UEFCharacterCreationRootWidget::ResolveTattooShopSubsystem() const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	UClass* TattooSubsystemClass = LoadClass<UWorldSubsystem>(nullptr, TEXT("/Script/EFProjectSystemsGameplay.ProjectTattooShopInputSubsystem"));
	if (!IsValid(TattooSubsystemClass))
	{
		return nullptr;
	}

	return World->GetSubsystemBase(TattooSubsystemClass);
}

void UEFCharacterCreationRootWidget::OpenTattooShopInCharacterCreation()
{
	ApplyTattooLayoutSettings();

	UObject* TattooSubsystem = ResolveTattooShopSubsystem();
	if (!IsValid(TattooSubsystem) || !IsValid(TattooShopHostBox) || !IsValid(TattooAssetPreviewerHostBox))
	{
		return;
	}

	static const FName OpenInHostFunctionName(TEXT("RequestOpenTattooShopInHosts"));
	if (UFunction* OpenInHostFunction = TattooSubsystem->FindFunction(OpenInHostFunctionName))
	{
		struct FOpenTattooShopInHostParams
		{
			UPanelWidget* HostPanel = nullptr;
			UPanelWidget* AssetPreviewPanel = nullptr;
		};

		FOpenTattooShopInHostParams Params;
		Params.HostPanel = TattooShopHostBox;
		Params.AssetPreviewPanel = TattooAssetPreviewerHostBox;
		TattooSubsystem->ProcessEvent(OpenInHostFunction, &Params);
	}
}

void UEFCharacterCreationRootWidget::ApplyTattooLayoutSettings()
{
	if (TattooHostFrame)
	{
		TattooHostFrame->ClearWidthOverride();
		TattooHostFrame->ClearHeightOverride();
		TattooHostFrame->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	}
	for (UWidget* Host : {static_cast<UWidget*>(TattooShopHostBox.Get()), static_cast<UWidget*>(TattooAssetPreviewerHostBox.Get())})
	{
		if (Host) { Host->SetRenderScale(FVector2D::UnitVector); Host->SetRenderTranslation(FVector2D::ZeroVector); }
	}
}

void UEFCharacterCreationRootWidget::ApplyCenteredTattooWidgetLayout(
	UWidget* Widget,
	const FVector2D& OffsetFromCenter,
	const FVector2D& Size,
	const FVector2D& RenderScale,
	const FVector2D& RenderTranslation) const
{
	if (!IsValid(Widget))
	{
		return;
	}

	Widget->SetRenderScale(RenderScale);
	Widget->SetRenderTranslation(RenderTranslation);
	Widget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetPosition(OffsetFromCenter);
		CanvasSlot->SetSize(Size);
	}
}

void UEFCharacterCreationRootWidget::CloseTattooShopInCharacterCreation()
{
	UObject* TattooSubsystem = ResolveTattooShopSubsystem();
	if (!IsValid(TattooSubsystem))
	{
		return;
	}

	static const FName CloseFunctionName(TEXT("RequestCloseTattooShop"));
	if (UFunction* CloseFunction = TattooSubsystem->FindFunction(CloseFunctionName))
	{
		TattooSubsystem->ProcessEvent(CloseFunction, nullptr);
	}
}

void UEFCharacterCreationRootWidget::HandleResetSkinColorClicked()
{
	if (!CustomizationComponent.IsValid())
	{
		return;
	}

	CustomizationComponent->SetSkinColor(UEFCharacterCreationSettings::Get()->DefaultSkinColor);
	UpdateColorControls();
}

void UEFCharacterCreationRootWidget::HandleResetIrisColorClicked()
{
	if (!CustomizationComponent.IsValid())
	{
		return;
	}

	CustomizationComponent->SetIrisColor(UEFCharacterCreationSettings::Get()->DefaultIrisColor);
	UpdateColorControls();
}

void UEFCharacterCreationRootWidget::HandleRandomClicked()
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->RandomizeAll();
		RefreshMorphList();
	}
}

void UEFCharacterCreationRootWidget::HandleDefaultsClicked()
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->ResetAllToDefaults();
		ResetHairEditState();
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleStartGameClicked()
{
	CloseTattooShopInCharacterCreation();

	if (UEFCharacterCreationSubsystem* Subsystem = CharacterCreationSubsystem.Get())
	{
		Subsystem->HandleStartGameRequested();
	}
}

void UEFCharacterCreationRootWidget::HandleBackClicked()
{
	CloseTattooShopInCharacterCreation();

	if (UEFCharacterCreationSubsystem* Subsystem = CharacterCreationSubsystem.Get())
	{
		Subsystem->HandleBackRequested();
	}
}

void UEFCharacterCreationRootWidget::HandleSavePresetClicked()
{
	if (!CustomizationComponent.IsValid() || !IsValid(PresetNameTextBox))
	{
		return;
	}

	const FString PresetName = PresetNameTextBox->GetText().ToString().TrimStartAndEnd();
	if (PresetName.IsEmpty())
	{
		return;
	}

	if (CustomizationComponent->SavePreset(PresetName))
	{
		SelectedPresetName = PresetName;
		RefreshPresetList();
	}
}

void UEFCharacterCreationRootWidget::HandleLoadPresetClicked()
{
	if (CustomizationComponent.IsValid() && !SelectedPresetName.IsEmpty())
	{
		if (CustomizationComponent->LoadPreset(SelectedPresetName))
		{
			ResetHairEditState();
			RefreshFromComponent();
		}
	}
}

void UEFCharacterCreationRootWidget::HandlePresetSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	SelectedPresetName = SelectedItem;
}

void UEFCharacterCreationRootWidget::HandleSearchTextChanged(const FText& NewText)
{
	if (bChangingNavigation) return;
	RefreshMorphList();
}

void UEFCharacterCreationRootWidget::HandleCharacterNameTextChanged(const FText& NewText)
{
	if (bIsRefreshingIdentityControls || !CustomizationComponent.IsValid())
	{
		return;
	}

	CustomizationComponent->SetCharacterName(NewText.ToString());
	const FString SanitizedName = CustomizationComponent->GetCharacterName();
	if (IsValid(CharacterNameTextBox) && CharacterNameTextBox->GetText().ToString() != SanitizedName)
	{
		bIsRefreshingIdentityControls = true;
		CharacterNameTextBox->SetText(FText::FromString(SanitizedName));
		bIsRefreshingIdentityControls = false;
	}
}

void UEFCharacterCreationRootWidget::HandleGenderMaleClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SetGender(ECharacterCreationGender::Male))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleGenderFemaleClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SetGender(ECharacterCreationGender::Female))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleSkinHueChanged(float NewValue)
{
	if (!CustomizationComponent.IsValid() || bIsRefreshingColorControls)
	{
		return;
	}

	const FLinearColor SkinColor = MakeColorFromHSVSliders(SkinHueSlider, SkinSaturationSlider, SkinValueSlider);
	CustomizationComponent->SetSkinColor(SkinColor);
	UpdateColorPreview(SkinPreviewBorder, SkinColor);
}

void UEFCharacterCreationRootWidget::HandleSkinSaturationChanged(float NewValue)
{
	HandleSkinHueChanged(NewValue);
}

void UEFCharacterCreationRootWidget::HandleSkinValueChanged(float NewValue)
{
	HandleSkinHueChanged(NewValue);
}

void UEFCharacterCreationRootWidget::HandleIrisHueChanged(float NewValue)
{
	if (!CustomizationComponent.IsValid() || bIsRefreshingColorControls)
	{
		return;
	}

	const FLinearColor IrisColor = MakeColorFromHSVSliders(IrisHueSlider, IrisSaturationSlider, IrisValueSlider);
	CustomizationComponent->SetIrisColor(IrisColor);
	UpdateColorPreview(IrisPreviewBorder, IrisColor);
}

void UEFCharacterCreationRootWidget::HandleIrisSaturationChanged(float NewValue)
{
	HandleIrisHueChanged(NewValue);
}

void UEFCharacterCreationRootWidget::HandleIrisValueChanged(float NewValue)
{
	HandleIrisHueChanged(NewValue);
}

void UEFCharacterCreationRootWidget::HandleBodyMeshPreviousClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SelectRelativeBodyMeshOption(-1))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleBodyMeshNextClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SelectRelativeBodyMeshOption(1))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleHairPreviousClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SelectRelativeHairMeshOption(-1))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleHairNextClicked()
{
	if (CustomizationComponent.IsValid() && CustomizationComponent->SelectRelativeHairMeshOption(1))
	{
		RefreshFromComponent();
	}
}

void UEFCharacterCreationRootWidget::HandleHairEditChanged(bool bIsChecked)
{
	if (bIsUpdatingHairEditCheckBox)
	{
		return;
	}

	if (!bIsChecked)
	{
		ResetHairEditState();
		UpdateHairTransformControls();
		RefreshMorphList();
		return;
	}

	if (bHairEditUnlocked)
	{
		UpdateHairTransformControls();
		return;
	}

	bHairEditRequested = true;
	bHairEditUnlocked = false;
	RefreshMorphList();
}

void UEFCharacterCreationRootWidget::HandleHairEditWarningOkClicked()
{
	bHairEditRequested = false;
	bHairEditUnlocked = true;

	if (IsValid(HairEditCheckBox))
	{
		bIsUpdatingHairEditCheckBox = true;
		HairEditCheckBox->SetIsChecked(true);
		bIsUpdatingHairEditCheckBox = false;
	}

	RefreshMorphList();
}

void UEFCharacterCreationRootWidget::HandleHairEditWarningCancelClicked()
{
	ResetHairEditState();
	RefreshMorphList();
}

void UEFCharacterCreationRootWidget::HandleHairLocationXChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairLocationX(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairLocationYChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairLocationY(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairLocationZChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairLocationZ(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairPitchChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairPitch(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairYawChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairYaw(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairRollChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairRoll(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleXChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairScaleX(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleYChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairScaleY(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleZChanged(float NewValue)
{
	if (!bIsRefreshingHairTransformControls && bHairEditUnlocked)
	{
		ApplyHairScaleZ(NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairLocationXTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairLocationX(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairLocationYTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairLocationY(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairLocationZTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairLocationZ(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairPitchTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairPitch(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairYawTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairYaw(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairRollTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairRoll(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleXTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairScaleX(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleYTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairScaleY(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleHairScaleZTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	float ParsedValue = 0.0f;
	if (bHairEditUnlocked && TryParseNumericText(NewText, ParsedValue))
	{
		ApplyHairScaleZ(ParsedValue);
	}
}

void UEFCharacterCreationRootWidget::HandleMorphValueChanged(const FMorphSliderEntry& Entry, float NewValue)
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->ApplyMorph(Entry, NewValue);
	}
}

void UEFCharacterCreationRootWidget::HandleMorphResetRequested(const FMorphSliderEntry& Entry)
{
	if (CustomizationComponent.IsValid())
	{
		CustomizationComponent->ResetMorph(Entry);
		RefreshMorphList();
	}
}


