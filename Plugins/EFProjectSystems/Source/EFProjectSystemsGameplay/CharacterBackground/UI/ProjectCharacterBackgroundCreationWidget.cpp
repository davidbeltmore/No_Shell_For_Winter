#include "CharacterBackground/UI/ProjectCharacterBackgroundCreationWidget.h"

#include "EFProjectUIPalette.h"
#include "CharacterBackground/ProjectCharacterBackgroundComponent.h"
#include "CharacterBackground/ProjectCharacterBackgroundSettings.h"
#include "CharacterBackground/UI/ProjectCharacterBackgroundEffectEntryWidget.h"
#include "CharacterBackground/UI/ProjectCharacterBackgroundOptionEntryWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "EFProjectUISettings.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Input/Reply.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ProjectCharacterBackgroundCreationWidget"

namespace ProjectCharacterBackgroundCreationPrivate
{
	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.76f);
	FLinearColor FrameFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.98f);
	}
	FLinearColor FrameOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.88f);
	}
	FLinearColor SectionFillTint()
	{
		return EFProjectUIPalette::SectionFill(0.95f);
	}
	FLinearColor SectionOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.44f);
	}
	FLinearColor TitleTint()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor PrimaryTextTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor SecondaryTextTint()
	{
		return EFProjectUIPalette::SecondaryText(0.96f);
	}
	FLinearColor WarningTint()
	{
		return EFProjectUIPalette::Warning();
	}
	FLinearColor ButtonFillTint()
	{
		return EFProjectUIPalette::SectionFill(0.98f);
	}
	FLinearColor DisabledButtonFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.80f);
	}
	FLinearColor ButtonOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.86f);
	}
	FLinearColor WatermarkTint()
	{
		return EFProjectUIPalette::AccentSoft(0.12f);
	}

	const TCHAR* DefaultWatermarkTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Watermark_Default.T_InnerDoctrine_Altar_Watermark_Default");
	const TCHAR* DefaultIconTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Default.T_InnerDoctrine_Altar_Icon_Default");

	const FProjectCharacterBackgroundUILayoutTuning& GetLayoutTuning()
	{
		if (const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get())
		{
			return Settings->UILayout;
		}

		static const FProjectCharacterBackgroundUILayoutTuning DefaultLayout;
		return DefaultLayout;
	}

	const FProjectCharacterBackgroundUIAdjustments& GetUIAdjustments()
	{
		if (const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get())
		{
			return Settings->UIAdjustments;
		}

		static const FProjectCharacterBackgroundUIAdjustments DefaultAdjustments;
		return DefaultAdjustments;
	}

	FVector2D GetPreviewImageFrameSize(const FProjectCharacterBackgroundUILayoutTuning& Layout)
	{
		const float SizeMultiplier = FMath::Max(0.10f, Layout.PreviewImageSizeMultiplier);
		return FVector2D(Layout.PreviewImageWidth * SizeMultiplier, Layout.PreviewImageHeight * SizeMultiplier);
	}

	FString JoinTexts(const TArray<FText>& Texts)
	{
		TArray<FString> Strings;
		Strings.Reserve(Texts.Num());
		for (const FText& Text : Texts)
		{
			if (!Text.IsEmpty())
			{
				Strings.Add(Text.ToString());
			}
		}
		return FString::Join(Strings, TEXT("\n"));
	}
}

UProjectCharacterBackgroundCreationWidget::UProjectCharacterBackgroundCreationWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UProjectCharacterBackgroundCreationWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshDisplay();
}

void UProjectCharacterBackgroundCreationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshDisplay();
	FocusCreationWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

FReply UProjectCharacterBackgroundCreationWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (HandleMenuKey(InKeyEvent.GetKey()))
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectCharacterBackgroundCreationWidget::InitializeForBackground(
	UProjectCharacterBackgroundComponent* InBackgroundComponent,
	const bool bInHasExistingProfile,
	const FName InExistingBackstoryID,
	const FName InExistingProfessionID,
	const bool bOpenAtSummary)
{
	BackgroundComponent = InBackgroundComponent;
	bHasExistingProfile = bInHasExistingProfile;
	ExistingBackstoryID = InExistingBackstoryID;
	ExistingProfessionID = InExistingProfessionID;
	CurrentStep = bOpenAtSummary ? EProjectCharacterBackgroundCreationStep::Summary : EProjectCharacterBackgroundCreationStep::Backstory;
	CurrentOptionIndex = INDEX_NONE;
	RefreshDisplay();
}

void UProjectCharacterBackgroundCreationWidget::RefreshDisplay()
{
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshStepHeader();
	RebuildOptionList();
	RefreshPreviewPanel();
	RefreshEffectPanel();
	RefreshDescriptionPanel();
	RefreshFooter();
}

void UProjectCharacterBackgroundCreationWidget::FocusCreationWidget()
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

FName UProjectCharacterBackgroundCreationWidget::GetSelectedBackstoryID() const
{
	return BackgroundComponent.IsValid() ? BackgroundComponent->GetSelectedBackstoryID() : NAME_None;
}

FName UProjectCharacterBackgroundCreationWidget::GetSelectedProfessionID() const
{
	return BackgroundComponent.IsValid() ? BackgroundComponent->GetSelectedProfessionID() : NAME_None;
}

bool UProjectCharacterBackgroundCreationWidget::IsChangingExistingProfile() const
{
	return bHasExistingProfile
		&& (GetSelectedBackstoryID() != ExistingBackstoryID || GetSelectedProfessionID() != ExistingProfessionID);
}

void UProjectCharacterBackgroundCreationWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const FProjectCharacterBackgroundUILayoutTuning& Layout = ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning();

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CharacterBackgroundRootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	BackdropBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundBackdrop"));
	if (UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(BackdropBorder))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(0.0f));
	}

	FrameBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundFrame"));
	FrameBorder->SetPadding(FMargin(Layout.FramePadding));
	if (UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(FrameBorder))
	{
		FrameSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		FrameSlot->SetOffsets(Layout.FrameOffsets);
	}

	ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CharacterBackgroundContent"));
	FrameBorder->SetContent(ContentBox);

	HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterBackgroundHeader"));
	if (UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(HeaderRow))
	{
		HeaderSlot->SetSize(ESlateSizeRule::Automatic);
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Layout.HeaderBottomPadding));
	}

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundTitle"));
	if (UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(TitleText))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}

	StepText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundStepText"));
	if (UHorizontalBoxSlot* StepSlot = HeaderRow->AddChildToHorizontalBox(StepText))
	{
		StepSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		StepSlot->SetVerticalAlignment(VAlign_Center);
	}

	BodyRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterBackgroundBody"));
	if (UVerticalBoxSlot* BodySlot = ContentBox->AddChildToVerticalBox(BodyRow))
	{
		BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BodySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Layout.BodyBottomPadding));
	}

	OptionSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CharacterBackgroundOptionSize"));
	OptionSizeBox->SetWidthOverride(Layout.SidePanelWidth);
	if (UHorizontalBoxSlot* OptionSlot = BodyRow->AddChildToHorizontalBox(OptionSizeBox))
	{
		OptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		OptionSlot->SetPadding(FMargin(0.0f, 0.0f, Layout.ColumnGap, 0.0f));
	}

	OptionListBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundOptionBorder"));
	OptionListBorder->SetPadding(Layout.PanelPadding);
	OptionSizeBox->AddChild(OptionListBorder);

	OptionScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CharacterBackgroundOptionScroll"));
	OptionListBorder->SetContent(OptionScrollBox);
	OptionListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CharacterBackgroundOptionList"));
	OptionScrollBox->AddChild(OptionListBox);

	PreviewBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundPreviewBorder"));
	PreviewBorder->SetPadding(Layout.PanelPadding);
	if (UHorizontalBoxSlot* PreviewSlot = BodyRow->AddChildToHorizontalBox(PreviewBorder))
	{
		PreviewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PreviewSlot->SetPadding(FMargin(0.0f, 0.0f, Layout.ColumnGap, 0.0f));
	}

	PreviewOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CharacterBackgroundPreviewOverlay"));
	PreviewBorder->SetContent(PreviewOverlay);

	PreviewWatermarkImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CharacterBackgroundPreviewWatermark"));
	if (UOverlaySlot* WatermarkSlot = PreviewOverlay->AddChildToOverlay(PreviewWatermarkImage))
	{
		WatermarkSlot->SetHorizontalAlignment(HAlign_Center);
		WatermarkSlot->SetVerticalAlignment(VAlign_Center);
	}

	PreviewContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CharacterBackgroundPreviewContent"));
	if (UOverlaySlot* PreviewContentSlot = PreviewOverlay->AddChildToOverlay(PreviewContentBox))
	{
		PreviewContentSlot->SetHorizontalAlignment(HAlign_Fill);
		PreviewContentSlot->SetVerticalAlignment(VAlign_Fill);
		PreviewContentSlot->SetPadding(Layout.PreviewContentPadding);
	}

	PreviewTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundPreviewTitle"));
	if (UVerticalBoxSlot* PreviewTitleSlot = PreviewContentBox->AddChildToVerticalBox(PreviewTitleText))
	{
		PreviewTitleSlot->SetSize(ESlateSizeRule::Automatic);
	}

	PreviewSubtitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundPreviewSubtitle"));
	PreviewSubtitleText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* PreviewSubtitleSlot = PreviewContentBox->AddChildToVerticalBox(PreviewSubtitleText))
	{
		PreviewSubtitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		PreviewSubtitleSlot->SetPadding(FMargin(0.0f, Layout.PreviewTitleGap, 0.0f, Layout.PreviewTitleGap));
	}

	PreviewImageSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CharacterBackgroundPreviewImageSize"));
	const FVector2D PreviewImageFrameSize = ProjectCharacterBackgroundCreationPrivate::GetPreviewImageFrameSize(Layout);
	PreviewImageSizeBox->SetWidthOverride(PreviewImageFrameSize.X);
	PreviewImageSizeBox->SetHeightOverride(PreviewImageFrameSize.Y);
	if (UVerticalBoxSlot* PreviewImageSlot = PreviewContentBox->AddChildToVerticalBox(PreviewImageSizeBox))
	{
		PreviewImageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PreviewImageSlot->SetHorizontalAlignment(HAlign_Center);
		PreviewImageSlot->SetVerticalAlignment(VAlign_Center);
		PreviewImageSlot->SetPadding(FMargin(0.0f, Layout.PreviewImageTopPadding, 0.0f, Layout.PreviewImageBottomPadding));
	}

	PreviewImageBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundPreviewImageBorder"));
	PreviewImageBorder->SetPadding(FMargin(0.0f));
	PreviewImageSizeBox->AddChild(PreviewImageBorder);

	PreviewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CharacterBackgroundPreviewImage"));
	PreviewImageBorder->SetContent(PreviewImage);

	PreviewIconBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterBackgroundPreviewIcons"));
	if (UVerticalBoxSlot* IconSlot = PreviewContentBox->AddChildToVerticalBox(PreviewIconBox))
	{
		IconSlot->SetSize(ESlateSizeRule::Automatic);
		IconSlot->SetHorizontalAlignment(HAlign_Center);
	}

	EffectSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CharacterBackgroundEffectSize"));
	EffectSizeBox->SetWidthOverride(Layout.SidePanelWidth);
	if (UHorizontalBoxSlot* EffectSlot = BodyRow->AddChildToHorizontalBox(EffectSizeBox))
	{
		EffectSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	EffectBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundEffectBorder"));
	EffectBorder->SetPadding(Layout.PanelPadding);
	EffectSizeBox->AddChild(EffectBorder);

	EffectBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CharacterBackgroundEffectBox"));
	EffectBorder->SetContent(EffectBox);

	EffectTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundEffectTitle"));
	if (UVerticalBoxSlot* EffectTitleSlot = EffectBox->AddChildToVerticalBox(EffectTitleText))
	{
		EffectTitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	DescriptionSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CharacterBackgroundDescriptionSize"));
	if (Layout.DescriptionPanelHeight > 0.0f)
	{
		DescriptionSizeBox->SetHeightOverride(Layout.DescriptionPanelHeight);
	}
	if (UVerticalBoxSlot* DescriptionSlot = ContentBox->AddChildToVerticalBox(DescriptionSizeBox))
	{
		DescriptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		DescriptionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Layout.BodyBottomPadding));
	}

	DescriptionBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterBackgroundDescriptionBorder"));
	DescriptionBorder->SetPadding(Layout.DescriptionPadding);
	DescriptionSizeBox->AddChild(DescriptionBorder);

	DescriptionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterBackgroundDescriptionRow"));
	DescriptionBorder->SetContent(DescriptionRow);

	UScrollBox* DescriptionScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CharacterBackgroundDescriptionScroll"));
	if (UHorizontalBoxSlot* TextSlot = DescriptionRow->AddChildToHorizontalBox(DescriptionScrollBox))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetPadding(FMargin(0.0f));
	}

	DescriptionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundDescriptionText"));
	DescriptionText->SetAutoWrapText(true);
	DescriptionScrollBox->AddChild(DescriptionText);

	FooterRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterBackgroundFooter"));
	if (UVerticalBoxSlot* FooterSlot = ContentBox->AddChildToVerticalBox(FooterRow))
	{
		FooterSlot->SetSize(ESlateSizeRule::Automatic);
	}

	FooterStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundFooterStatus"));
	if (UHorizontalBoxSlot* StatusSlot = FooterRow->AddChildToHorizontalBox(FooterStatusText))
	{
		StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatusSlot->SetVerticalAlignment(VAlign_Center);
	}

	BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CharacterBackgroundBackButton"));
	BackButton->OnClicked.AddDynamic(this, &ThisClass::HandleBackClicked);
	if (UHorizontalBoxSlot* BackSlot = FooterRow->AddChildToHorizontalBox(BackButton))
	{
		BackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		BackSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	}
	BackButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundBackButtonText"));
	BackButtonText->SetText(LOCTEXT("BackButton", "Back"));
	BackButton->AddChild(BackButtonText);

	PrimaryButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CharacterBackgroundPrimaryButton"));
	PrimaryButton->OnClicked.AddDynamic(this, &ThisClass::HandlePrimaryClicked);
	if (UHorizontalBoxSlot* PrimarySlot = FooterRow->AddChildToHorizontalBox(PrimaryButton))
	{
		PrimarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		PrimarySlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
	}
	PrimaryButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundPrimaryButtonText"));
	PrimaryButtonText->SetText(LOCTEXT("ContinueButton", "Continue"));
	PrimaryButton->AddChild(PrimaryButtonText);
}

void UProjectCharacterBackgroundCreationWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	const FProjectCharacterBackgroundUILayoutTuning& Layout = ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning();
	const FProjectCharacterBackgroundUIAdjustments& Adjustments = ProjectCharacterBackgroundCreationPrivate::GetUIAdjustments();

	if (BackdropBorder)
	{
		BackdropBorder->SetBrush(FSlateRoundedBoxBrush(
			ProjectCharacterBackgroundCreationPrivate::BackdropTint,
			0.0f,
			FSlateColor(FLinearColor::Transparent),
			0.0f,
			FVector2f(1920.0f, 1080.0f)));
	}

	const FSlateRoundedBoxBrush FrameBrush(
		ProjectCharacterBackgroundCreationPrivate::FrameFillTint(),
		Layout.FrameCornerRadius,
		FSlateColor(ProjectCharacterBackgroundCreationPrivate::FrameOutlineTint()),
		Layout.FrameOutlineWidth,
		FVector2f(1800.0f, 1000.0f));
	const FSlateRoundedBoxBrush SectionBrush(
		ProjectCharacterBackgroundCreationPrivate::SectionFillTint(),
		Layout.PanelCornerRadius,
		FSlateColor(ProjectCharacterBackgroundCreationPrivate::SectionOutlineTint()),
		Layout.PanelOutlineWidth,
		FVector2f(600.0f, 400.0f));
	const FSlateRoundedBoxBrush ImageFrameBrush(
		EFProjectUIPalette::PanelFillDeep(0.42f),
		Layout.PanelCornerRadius,
		FSlateColor(ProjectCharacterBackgroundCreationPrivate::SectionOutlineTint()),
		Layout.PanelOutlineWidth,
		FVector2f(ProjectCharacterBackgroundCreationPrivate::GetPreviewImageFrameSize(Layout)));

	if (FrameBorder)
	{
		FrameBorder->SetBrush(FrameBrush);
	}
	if (OptionListBorder)
	{
		OptionListBorder->SetBrush(SectionBrush);
	}
	if (PreviewBorder)
	{
		PreviewBorder->SetBrush(SectionBrush);
	}
	if (EffectBorder)
	{
		EffectBorder->SetBrush(SectionBrush);
	}
	if (DescriptionBorder)
	{
		DescriptionBorder->SetBrush(SectionBrush);
	}
	if (PreviewImageBorder)
	{
		PreviewImageBorder->SetBrush(ImageFrameBrush);
	}

	if (TitleText)
	{
		TitleText->SetText(LOCTEXT("CreationTitle", "Story Selection"));
		TitleText->SetFont(MakeTitleFont(Layout.TitleFontSize));
		TitleText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::TitleTint());
	}

	for (UTextBlock* Text : { StepText.Get(), PreviewTitleText.Get(), EffectTitleText.Get() })
	{
		if (Text)
		{
			Text->SetFont(MakeTitleFont(Layout.SectionTitleFontSize));
			Text->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::PrimaryTextTint());
		}
	}

	for (UTextBlock* Text : { BackButtonText.Get(), PrimaryButtonText.Get() })
	{
		if (Text)
		{
			Text->SetFont(MakeBodyFont(Layout.ButtonFontSize));
			Text->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::PrimaryTextTint());
		}
	}

	for (UTextBlock* Text : { PreviewSubtitleText.Get(), DescriptionText.Get(), FooterStatusText.Get() })
	{
		if (Text)
		{
			Text->SetFont(MakeBodyFont(Layout.BodyFontSize));
			Text->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::SecondaryTextTint());
		}
	}

	const FSlateRoundedBoxBrush ButtonBrush(
		ProjectCharacterBackgroundCreationPrivate::ButtonFillTint(),
		Layout.ButtonCornerRadius,
		FSlateColor(ProjectCharacterBackgroundCreationPrivate::ButtonOutlineTint()),
		1.0f,
		FVector2f(Layout.ButtonWidth, Layout.ButtonHeight));
	const FSlateRoundedBoxBrush DisabledButtonBrush(
		ProjectCharacterBackgroundCreationPrivate::DisabledButtonFillTint(),
		Layout.ButtonCornerRadius,
		FSlateColor(ProjectCharacterBackgroundCreationPrivate::SectionOutlineTint()),
		0.7f,
		FVector2f(Layout.ButtonWidth, Layout.ButtonHeight));
	for (UButton* Button : { BackButton.Get(), PrimaryButton.Get() })
	{
		if (Button)
		{
			FButtonStyle Style = Button->GetStyle();
			Style.SetNormal(ButtonBrush);
			Style.SetHovered(ButtonBrush);
			Style.SetPressed(ButtonBrush);
			Style.SetDisabled(DisabledButtonBrush);
			Style.NormalPadding = Layout.ButtonPadding;
			Style.PressedPadding = FMargin(
				Layout.ButtonPadding.Left,
				Layout.ButtonPadding.Top + 1.0f,
				Layout.ButtonPadding.Right,
				FMath::Max(0.0f, Layout.ButtonPadding.Bottom - 1.0f));
			Button->SetStyle(Style);
		}
	}

	if (PreviewImage)
	{
		PreviewImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			PreviewImage,
			FLinearColor(1.0f, 1.0f, 1.0f, 0.98f)));
	}

	ApplyWidgetAdjustment(FrameBorder, Adjustments.Frame);
	ApplyWidgetAdjustment(TitleText, Adjustments.Title);
	ApplyWidgetAdjustment(StepText, Adjustments.StepLabel);
	ApplyWidgetAdjustment(OptionSizeBox, Adjustments.OptionPanel);
	ApplyWidgetAdjustment(PreviewBorder, Adjustments.PreviewPanel);
	ApplyWidgetAdjustment(EffectSizeBox, Adjustments.EffectPanel);
	ApplyWidgetAdjustment(DescriptionSizeBox, Adjustments.DescriptionPanel);
	ApplyWidgetAdjustment(DescriptionText, Adjustments.DescriptionText);
	ApplyWidgetAdjustment(PreviewImageSizeBox, Adjustments.PreviewImage);
	ApplyWidgetAdjustment(PreviewWatermarkImage, Adjustments.PreviewWatermark);
	ApplyWidgetAdjustment(PreviewIconBox, Adjustments.PreviewIcons);
	ApplyWidgetAdjustment(FooterRow, Adjustments.Footer);
	ApplyWidgetAdjustment(BackButton, Adjustments.BackButton);
	ApplyWidgetAdjustment(PrimaryButton, Adjustments.PrimaryButton);

	bVisualTreeInitialized = true;
}

void UProjectCharacterBackgroundCreationWidget::RebuildOptionList()
{
	if (!OptionListBox || !BackgroundComponent.IsValid())
	{
		return;
	}

	OptionListBox->ClearChildren();
	CurrentOptionIDs.Reset();

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Summary)
	{
		const FProjectCharacterBackgroundUILayoutTuning& Layout = ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning();
		UTextBlock* SummaryListText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CharacterBackgroundSummaryListText"));
		SummaryListText->SetFont(MakeBodyFont(Layout.BodyFontSize));
		SummaryListText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::PrimaryTextTint());
		SummaryListText->SetAutoWrapText(true);
		const FProjectCharacterBackgroundSummary Summary = BackgroundComponent->BuildSummary();
		SummaryListText->SetText(FText::Format(
			LOCTEXT("SummaryListFormat", "Backstory\n{0}\n\nProfession\n{1}"),
			Summary.BackstoryName,
			Summary.ProfessionName));
		OptionListBox->AddChildToVerticalBox(SummaryListText);
		CurrentOptionIndex = INDEX_NONE;
		return;
	}

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		const TArray<FProjectCharacterBackstoryData> Backstories = BackgroundComponent->GetAvailableBackstories();
		for (const FProjectCharacterBackstoryData& Backstory : Backstories)
		{
			CurrentOptionIDs.Add(Backstory.BackstoryID);
			UProjectCharacterBackgroundOptionEntryWidget* Entry = CreateWidget<UProjectCharacterBackgroundOptionEntryWidget>(
				this,
				UProjectCharacterBackgroundOptionEntryWidget::StaticClass());
			if (!Entry)
			{
				continue;
			}
			const bool bSelected = Backstory.BackstoryID == BackgroundComponent->GetSelectedBackstoryID();
			Entry->SetEntryHeight(ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning().OptionEntryHeight);
			Entry->SetOption(Backstory.BackstoryID, Backstory.DisplayName, BuildBackstorySubtitle(Backstory), bSelected);
			Entry->OnOptionSelected.AddUObject(this, &ThisClass::HandleOptionSelected);
			if (UVerticalBoxSlot* EntrySlot = OptionListBox->AddChildToVerticalBox(Entry))
			{
				EntrySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning().OptionEntryGap));
			}
		}
	}
	else
	{
		const TArray<FProjectCharacterProfessionData> Professions = BackgroundComponent->GetAvailableProfessions();
		for (const FProjectCharacterProfessionData& Profession : Professions)
		{
			CurrentOptionIDs.Add(Profession.ProfessionID);
			UProjectCharacterBackgroundOptionEntryWidget* Entry = CreateWidget<UProjectCharacterBackgroundOptionEntryWidget>(
				this,
				UProjectCharacterBackgroundOptionEntryWidget::StaticClass());
			if (!Entry)
			{
				continue;
			}
			const bool bSelected = Profession.ProfessionID == BackgroundComponent->GetSelectedProfessionID();
			Entry->SetEntryHeight(ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning().ProfessionEntryHeight);
			Entry->SetOption(Profession.ProfessionID, Profession.DisplayName, BuildProfessionSubtitle(Profession), bSelected);
			Entry->OnOptionSelected.AddUObject(this, &ThisClass::HandleOptionSelected);
			if (UVerticalBoxSlot* EntrySlot = OptionListBox->AddChildToVerticalBox(Entry))
			{
				EntrySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning().OptionEntryGap));
			}
		}
	}

	const FName SelectedID = CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory
		? BackgroundComponent->GetSelectedBackstoryID()
		: BackgroundComponent->GetSelectedProfessionID();
	CurrentOptionIndex = CurrentOptionIDs.IndexOfByKey(SelectedID);
}

void UProjectCharacterBackgroundCreationWidget::RefreshStepHeader()
{
	if (!StepText)
	{
		return;
	}

	switch (CurrentStep)
	{
	case EProjectCharacterBackgroundCreationStep::Backstory:
		StepText->SetText(LOCTEXT("BackstoryStep", "1 / 3  Backstory"));
		break;
	case EProjectCharacterBackgroundCreationStep::Profession:
		StepText->SetText(LOCTEXT("ProfessionStep", "2 / 3  Profession"));
		break;
	case EProjectCharacterBackgroundCreationStep::Summary:
		StepText->SetText(LOCTEXT("SummaryStep", "3 / 3  Summary"));
		break;
	default:
		break;
	}
}

void UProjectCharacterBackgroundCreationWidget::RefreshPreviewPanel()
{
	if (!BackgroundComponent.IsValid())
	{
		return;
	}

	FText PreviewTitle = LOCTEXT("PreviewChoose", "Choose a Profile");
	FText PreviewSubtitle = LOCTEXT("PreviewChooseSubtitle", "Select a backstory and profession to define the first shape of this run.");
	TArray<FName> AttributeIDs;
	FName WatermarkAttribute = NAME_None;
	bool bHasExplicitSelection = false;

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		if (!BackgroundComponent->GetSelectedBackstoryID().IsNone())
		{
			bHasExplicitSelection = true;
			const FProjectCharacterBackstoryData Data = BackgroundComponent->GetSelectedBackstoryData();
			PreviewTitle = Data.DisplayName.IsEmpty() ? LOCTEXT("PreviewBackstory", "Backstory") : Data.DisplayName;
			PreviewSubtitle = Data.Description;
			for (const FProjectDoctrineStartingLevelModifier& Modifier : Data.StartingLevels)
			{
				AttributeIDs.Add(Modifier.AttributeID);
			}
			WatermarkAttribute = AttributeIDs.Num() > 0 ? AttributeIDs[0] : NAME_None;
		}
		else
		{
			PreviewTitle = LOCTEXT("PreviewBackstoryPendingTitle", "Backstory");
			PreviewSubtitle = LOCTEXT("PreviewBackstoryPendingSubtitle", "Select a backstory to preview its image and effects.");
		}
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		if (!BackgroundComponent->GetSelectedProfessionID().IsNone())
		{
			bHasExplicitSelection = true;
			const FProjectCharacterProfessionData Data = BackgroundComponent->GetSelectedProfessionData();
			PreviewTitle = Data.DisplayName.IsEmpty() ? LOCTEXT("PreviewProfession", "Profession") : Data.DisplayName;
			PreviewSubtitle = Data.Description;
			for (const FProjectDxpGainModifier& Modifier : Data.GainModifiers)
			{
				AttributeIDs.Add(Modifier.AttributeID);
			}
			WatermarkAttribute = AttributeIDs.Num() > 0 ? AttributeIDs[0] : NAME_None;
		}
		else
		{
			PreviewTitle = LOCTEXT("PreviewProfessionPendingTitle", "Profession");
			PreviewSubtitle = LOCTEXT("PreviewProfessionPendingSubtitle", "Select a profession to preview its image and effects.");
		}
	}
	else
	{
		PreviewTitle = LOCTEXT("PreviewSummaryTitle", "Final Background");
		if (BackgroundComponent->IsSelectionValid())
		{
			bHasExplicitSelection = true;
			const FProjectCharacterBackgroundSummary Summary = BackgroundComponent->BuildSummary();
			PreviewSubtitle = FText::Format(LOCTEXT("PreviewSummarySubtitle", "{0} / {1}"), Summary.BackstoryName, Summary.ProfessionName);
			for (const FProjectDoctrineStartingLevelModifier& Modifier : BackgroundComponent->GetFinalStartingLevelModifiers())
			{
				AttributeIDs.AddUnique(Modifier.AttributeID);
			}
			for (const FProjectDxpGainModifier& Modifier : BackgroundComponent->GetFinalGainModifiers())
			{
				AttributeIDs.AddUnique(Modifier.AttributeID);
			}
			WatermarkAttribute = AttributeIDs.Num() > 0 ? AttributeIDs[0] : NAME_None;
		}
		else
		{
			PreviewSubtitle = LOCTEXT("PreviewSummaryPendingSubtitle", "Complete both selections to preview the final image.");
		}
	}

	if (PreviewTitleText)
	{
		PreviewTitleText->SetText(PreviewTitle);
	}
	if (PreviewSubtitleText)
	{
		PreviewSubtitleText->SetText(PreviewSubtitle);
	}
	if (PreviewWatermarkImage)
	{
		PreviewWatermarkImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTextureForAttribute(WatermarkAttribute, true)), false);
		PreviewWatermarkImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			PreviewWatermarkImage,
			ProjectCharacterBackgroundCreationPrivate::WatermarkTint()));
	}
	if (PreviewImage && PreviewImageBorder && PreviewImageSizeBox)
	{
		if (bHasExplicitSelection)
		{
			UTexture2D* PreviewTexture = ResolvePreviewImageTexture();
			PreviewImage->SetBrushFromTexture(ResolveProjectThemeTexture(PreviewTexture), false);
			PreviewImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				PreviewImage,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.98f)));

			const ESlateVisibility NewVisibility = PreviewTexture ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
			PreviewImageSizeBox->SetVisibility(NewVisibility);
			PreviewImageBorder->SetVisibility(NewVisibility);
			PreviewImage->SetVisibility(NewVisibility);
		}
		else
		{
			PreviewImage->SetBrushFromTexture(ResolveProjectThemeTexture(nullptr), false);
			PreviewImageSizeBox->SetVisibility(ESlateVisibility::Collapsed);
			PreviewImageBorder->SetVisibility(ESlateVisibility::Collapsed);
			PreviewImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	RebuildPreviewIcons(AttributeIDs);
}

void UProjectCharacterBackgroundCreationWidget::RefreshEffectPanel()
{
	if (!EffectBox || !EffectTitleText || !BackgroundComponent.IsValid())
	{
		return;
	}

	EffectBox->ClearChildren();
	EffectBox->AddChildToVerticalBox(EffectTitleText);

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		EffectTitleText->SetText(LOCTEXT("StartingLevelsTitle", "Starting Levels"));
		for (const FProjectDoctrineStartingLevelModifier& Modifier : BackgroundComponent->GetSelectedBackstoryData().StartingLevels)
		{
			AddEffectRow(BuildModifierLine(Modifier.AttributeID, Modifier.StartingLevelDelta), false);
		}
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		EffectTitleText->SetText(LOCTEXT("GainModifiersTitle", "DXP Gain"));
		for (const FProjectDxpGainModifier& Modifier : BackgroundComponent->GetSelectedProfessionData().GainModifiers)
		{
			AddEffectRow(BuildGainLine(Modifier.AttributeID, Modifier.GainMultiplier), Modifier.GainMultiplier < 1.0f);
		}
	}
	else
	{
		EffectTitleText->SetText(LOCTEXT("FinalEffectsTitle", "Final Effects"));
		const FProjectCharacterBackgroundSummary Summary = BackgroundComponent->BuildSummary();
		for (const FText& Line : Summary.StartingLevelLines)
		{
			AddEffectRow(Line, false);
		}
		for (const FText& Line : Summary.GainModifierLines)
		{
			AddEffectRow(Line, Line.ToString().Contains(TEXT("x0.")));
		}
	}
}

void UProjectCharacterBackgroundCreationWidget::RefreshDescriptionPanel()
{
	if (!DescriptionText || !BackgroundComponent.IsValid())
	{
		return;
	}

	FString Text;
	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		const FProjectCharacterBackstoryData Data = BackgroundComponent->GetSelectedBackstoryData();
		const FString Advantages = ProjectCharacterBackgroundCreationPrivate::JoinTexts(Data.Advantages);
		if (!Advantages.IsEmpty())
		{
			Text = FString::Printf(TEXT("Advantages\n%s"), *Advantages);
		}
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		const FProjectCharacterProfessionData Data = BackgroundComponent->GetSelectedProfessionData();
		Text = Data.Description.ToString();
		const FString Advantages = ProjectCharacterBackgroundCreationPrivate::JoinTexts(Data.Advantages);
		const FString Disadvantages = ProjectCharacterBackgroundCreationPrivate::JoinTexts(Data.Disadvantages);
		if (!Advantages.IsEmpty())
		{
			Text += FString::Printf(TEXT("\n\nAdvantages\n%s"), *Advantages);
		}
		if (!Disadvantages.IsEmpty())
		{
			Text += FString::Printf(TEXT("\n\nTradeoffs\n%s"), *Disadvantages);
		}
	}
	else
	{
		const FProjectCharacterBackgroundSummary Summary = BackgroundComponent->BuildSummary();
		Text = FString::Printf(TEXT("Backstory: %s\nProfession: %s"), *Summary.BackstoryName.ToString(), *Summary.ProfessionName.ToString());
		if (IsChangingExistingProfile())
		{
			Text += TEXT("\n\nChanging this profile resets current Run DXP to 0 and clears run attribute levels. Meta DXP is preserved.");
		}
	}

	DescriptionText->SetText(FText::FromString(Text));

}

void UProjectCharacterBackgroundCreationWidget::RefreshFooter()
{
	const bool bCanGoBack = CurrentStep != EProjectCharacterBackgroundCreationStep::Backstory;
	const bool bHasBackstory = BackgroundComponent.IsValid() && !BackgroundComponent->GetSelectedBackstoryID().IsNone();
	const bool bHasProfession = BackgroundComponent.IsValid() && !BackgroundComponent->GetSelectedProfessionID().IsNone();
	const bool bCanConfirm = BackgroundComponent.IsValid() && BackgroundComponent->IsSelectionValid();

	if (BackButton)
	{
		BackButton->SetIsEnabled(bCanGoBack);
	}

	if (PrimaryButton)
	{
		bool bPrimaryEnabled = bCanConfirm;
		if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
		{
			bPrimaryEnabled = bHasBackstory;
		}
		else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
		{
			bPrimaryEnabled = bHasProfession;
		}
		PrimaryButton->SetIsEnabled(bPrimaryEnabled);
	}

	if (PrimaryButtonText)
	{
		if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
		{
			PrimaryButtonText->SetText(LOCTEXT("PrimaryProfession", "Profession"));
		}
		else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
		{
			PrimaryButtonText->SetText(LOCTEXT("PrimarySummary", "Summary"));
		}
		else
		{
			PrimaryButtonText->SetText(LOCTEXT("PrimaryConfirm", "Confirm"));
		}
	}

	if (FooterStatusText)
	{
		if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory && !bHasBackstory)
		{
			FooterStatusText->SetText(LOCTEXT("FooterNeedBackstory", "Choose a Backstory to continue."));
			FooterStatusText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::WarningTint());
		}
		else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession && !bHasProfession)
		{
			FooterStatusText->SetText(LOCTEXT("FooterNeedProfession", "Choose a Profession to continue."));
			FooterStatusText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::WarningTint());
		}
		else if (!bCanConfirm)
		{
			FooterStatusText->SetText(LOCTEXT("FooterNeedSelection", "Choose one Backstory and one Profession to continue."));
			FooterStatusText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::WarningTint());
		}
		else if (IsChangingExistingProfile())
		{
			FooterStatusText->SetText(LOCTEXT("FooterChangingProfile", "Profile change will reset current Run DXP and run attribute levels."));
			FooterStatusText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::WarningTint());
		}
		else
		{
			FooterStatusText->SetText(LOCTEXT("FooterControls", "Enter confirms. Escape goes back."));
			FooterStatusText->SetColorAndOpacity(ProjectCharacterBackgroundCreationPrivate::SecondaryTextTint());
		}
	}
}

void UProjectCharacterBackgroundCreationWidget::SetStep(const EProjectCharacterBackgroundCreationStep InStep)
{
	CurrentStep = InStep;
	CurrentOptionIndex = INDEX_NONE;
	RefreshDisplay();
	FocusCreationWidget();
}

void UProjectCharacterBackgroundCreationWidget::NavigateSelection(const int32 Delta)
{
	if (CurrentOptionIDs.IsEmpty() || !BackgroundComponent.IsValid())
	{
		return;
	}

	if (CurrentOptionIndex == INDEX_NONE)
	{
		CurrentOptionIndex = 0;
	}
	else
	{
		CurrentOptionIndex = (CurrentOptionIndex + Delta + CurrentOptionIDs.Num()) % CurrentOptionIDs.Num();
	}

	SelectCurrentOption();
}

void UProjectCharacterBackgroundCreationWidget::SelectCurrentOption()
{
	if (!CurrentOptionIDs.IsValidIndex(CurrentOptionIndex) || !BackgroundComponent.IsValid())
	{
		return;
	}

	const FName OptionID = CurrentOptionIDs[CurrentOptionIndex];
	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		BackgroundComponent->SetBackstory(OptionID);
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		BackgroundComponent->SetProfession(OptionID);
	}
	RefreshDisplay();
}

void UProjectCharacterBackgroundCreationWidget::ConfirmOrAdvance()
{
	if (!BackgroundComponent.IsValid())
	{
		return;
	}

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		if (BackgroundComponent->GetSelectedBackstoryID().IsNone())
		{
			RefreshFooter();
			return;
		}
		SetStep(EProjectCharacterBackgroundCreationStep::Profession);
		return;
	}

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		if (BackgroundComponent->GetSelectedProfessionID().IsNone())
		{
			RefreshFooter();
			return;
		}
		SetStep(EProjectCharacterBackgroundCreationStep::Summary);
		return;
	}

	if (BackgroundComponent->IsSelectionValid())
	{
		OnConfirmRequested.Broadcast(GetSelectedBackstoryID(), GetSelectedProfessionID(), IsChangingExistingProfile());
	}
}

void UProjectCharacterBackgroundCreationWidget::GoBack()
{
	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Summary)
	{
		SetStep(EProjectCharacterBackgroundCreationStep::Profession);
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		SetStep(EProjectCharacterBackgroundCreationStep::Backstory);
	}
}

bool UProjectCharacterBackgroundCreationWidget::HandleMenuKey(const FKey& Key)
{
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		NavigateSelection(-1);
		return true;
	}

	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		NavigateSelection(1);
		return true;
	}

	if (Key == EKeys::Enter || Key == EKeys::Virtual_Gamepad_Accept || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		ConfirmOrAdvance();
		return true;
	}

	if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		GoBack();
		return true;
	}

	return false;
}

FText UProjectCharacterBackgroundCreationWidget::BuildBackstorySubtitle(const FProjectCharacterBackstoryData& Data) const
{
	TArray<FString> Parts;
	for (const FProjectDoctrineStartingLevelModifier& Modifier : Data.StartingLevels)
	{
		Parts.Add(BuildModifierLine(Modifier.AttributeID, Modifier.StartingLevelDelta).ToString());
	}
	return FText::FromString(FString::Join(Parts, TEXT("  ")));
}

FText UProjectCharacterBackgroundCreationWidget::BuildProfessionSubtitle(const FProjectCharacterProfessionData& Data) const
{
	if (Data.GainModifiers.Num() > 4)
	{
		bool bAllSameMultiplier = true;
		const float FirstMultiplier = Data.GainModifiers[0].GainMultiplier;
		for (const FProjectDxpGainModifier& Modifier : Data.GainModifiers)
		{
			if (!FMath::IsNearlyEqual(Modifier.GainMultiplier, FirstMultiplier, 0.001f))
			{
				bAllSameMultiplier = false;
				break;
			}
		}

		if (bAllSameMultiplier)
		{
			return FText::Format(
				LOCTEXT("AllProfessionAttributesLine", "All DXP attributes x{0}"),
				FText::FromString(FString::Printf(TEXT("%.2f"), FirstMultiplier)));
		}
	}

	TArray<FString> Parts;
	for (const FProjectDxpGainModifier& Modifier : Data.GainModifiers)
	{
		Parts.Add(BuildGainLine(Modifier.AttributeID, Modifier.GainMultiplier).ToString());
	}
	return FText::FromString(FString::Join(Parts, TEXT("\n")));
}

FText UProjectCharacterBackgroundCreationWidget::BuildModifierLine(const FName AttributeID, const int32 Delta) const
{
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
	const FText AttributeName = ProjectCharacterBackground::TryResolveDoctrineAttribute(AttributeID, Attribute)
		? ProjectCharacterBackground::GetDoctrineAttributeDisplayText(Attribute)
		: FText::FromName(AttributeID);
	return FText::Format(LOCTEXT("ModifierLine", "{0} +{1}"), AttributeName, FText::AsNumber(Delta));
}

FText UProjectCharacterBackgroundCreationWidget::BuildGainLine(const FName AttributeID, const float Multiplier) const
{
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
	const FText AttributeName = ProjectCharacterBackground::TryResolveDoctrineAttribute(AttributeID, Attribute)
		? ProjectCharacterBackground::GetDoctrineAttributeDisplayText(Attribute)
		: FText::FromName(AttributeID);
	return FText::Format(
		LOCTEXT("GainLine", "{0} x{1}"),
		AttributeName,
		FText::FromString(FString::Printf(TEXT("%.2f"), Multiplier)));
}

UTexture2D* UProjectCharacterBackgroundCreationWidget::ResolveTextureForAttribute(const FName AttributeID, const bool bWatermark) const
{
	if (const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get())
	{
		for (const FProjectInnerDoctrineEntryDefinition& Definition : UISettings->InnerDoctrineEntryDefinitions)
		{
			if (Definition.AttributeName == AttributeID)
			{
				const TSoftObjectPtr<UTexture2D>& TexturePtr = bWatermark ? Definition.MenuWatermarkTexture : Definition.MenuIconTexture;
				if (UTexture2D* Texture = TexturePtr.LoadSynchronous())
				{
					return Texture;
				}
			}
		}
	}

	const TCHAR* FallbackPath = bWatermark
		? ProjectCharacterBackgroundCreationPrivate::DefaultWatermarkTexturePath
		: ProjectCharacterBackgroundCreationPrivate::DefaultIconTexturePath;
	return Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, FallbackPath));
}

UTexture2D* UProjectCharacterBackgroundCreationWidget::ResolvePreviewImageTexture()
{
	if (BackgroundComponent.IsValid())
	{
		if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
		{
			if (UTexture2D* Texture = BackgroundComponent->GetSelectedBackstoryData().PreviewImage.LoadSynchronous())
			{
				return Texture;
			}
		}
		else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
		{
			if (UTexture2D* Texture = BackgroundComponent->GetSelectedProfessionData().PreviewImage.LoadSynchronous())
			{
				return Texture;
			}
		}
		else
		{
			if (UTexture2D* Texture = BackgroundComponent->GetSelectedBackstoryData().PreviewImage.LoadSynchronous())
			{
				return Texture;
			}
			if (UTexture2D* Texture = BackgroundComponent->GetSelectedProfessionData().PreviewImage.LoadSynchronous())
			{
				return Texture;
			}
		}
	}

	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	if (Settings)
	{
		if (UTexture2D* Texture = Settings->PreviewImageTexture.LoadSynchronous())
		{
			return Texture;
		}

		if (CachedPreviewImageTexture)
		{
			return CachedPreviewImageTexture;
		}

		FString PreviewPath = Settings->PreviewImagePngPath;
		if (!PreviewPath.IsEmpty())
		{
			if (FPaths::IsRelative(PreviewPath))
			{
				PreviewPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), PreviewPath);
			}

			if (FPaths::FileExists(PreviewPath))
			{
				CachedPreviewImageTexture = FImageUtils::ImportFileAsTexture2D(PreviewPath);
				if (CachedPreviewImageTexture)
				{
					return CachedPreviewImageTexture;
				}
			}
		}
	}

	return nullptr;
}

void UProjectCharacterBackgroundCreationWidget::RebuildPreviewIcons(const TArray<FName>& AttributeIDs)
{
	if (!PreviewIconBox)
	{
		return;
	}

	const FProjectCharacterBackgroundUILayoutTuning& Layout = ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning();
	const float PreviewIconSize = Layout.AttributeIconSize * FMath::Max(0.10f, Layout.PreviewIconSizeMultiplier);

	PreviewIconBox->ClearChildren();
	for (const FName AttributeID : AttributeIDs)
	{
		USizeBox* IconSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		IconSizeBox->SetWidthOverride(PreviewIconSize);
		IconSizeBox->SetHeightOverride(PreviewIconSize);

		UImage* IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		IconImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTextureForAttribute(AttributeID, false)), false);
		IconImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			IconImage,
			EFProjectUIPalette::AccentSoft(0.95f)));
		IconSizeBox->AddChild(IconImage);

		if (UHorizontalBoxSlot* IconSlot = PreviewIconBox->AddChildToHorizontalBox(IconSizeBox))
		{
			IconSlot->SetPadding(FMargin(Layout.AttributeIconGap, 0.0f));
		}
	}
}

void UProjectCharacterBackgroundCreationWidget::AddEffectRow(const FText& Text, const bool bNegative)
{
	UProjectCharacterBackgroundEffectEntryWidget* Row = CreateWidget<UProjectCharacterBackgroundEffectEntryWidget>(
		this,
		UProjectCharacterBackgroundEffectEntryWidget::StaticClass());
	if (!Row || !EffectBox)
	{
		return;
	}

	Row->SetEffectText(Text, bNegative);
	if (UVerticalBoxSlot* RowSlot = EffectBox->AddChildToVerticalBox(Row))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ProjectCharacterBackgroundCreationPrivate::GetLayoutTuning().EffectEntryGap));
	}
}

void UProjectCharacterBackgroundCreationWidget::ApplyWidgetAdjustment(UWidget* Widget, const FProjectCharacterBackgroundWidgetAdjustment& Adjustment) const
{
	if (!Widget)
	{
		return;
	}

	Widget->SetRenderTranslation(Adjustment.Translation);
	const float UniformScale = FMath::Max(0.01f, Adjustment.UniformScale);
	Widget->SetRenderScale(FVector2D(Adjustment.Scale.X * UniformScale, Adjustment.Scale.Y * UniformScale));
	Widget->SetRenderShear(Adjustment.Shear);
	Widget->SetRenderTransformAngle(Adjustment.RotationDegrees);
	Widget->SetRenderTransformPivot(Adjustment.Pivot);
}

FSlateFontInfo UProjectCharacterBackgroundCreationWidget::MakeTitleFont(const int32 Size) const
{
	return FCoreStyle::GetDefaultFontStyle("Bold", Size);
}

FSlateFontInfo UProjectCharacterBackgroundCreationWidget::MakeBodyFont(const int32 Size) const
{
	return FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

void UProjectCharacterBackgroundCreationWidget::HandlePrimaryClicked()
{
	ConfirmOrAdvance();
}

void UProjectCharacterBackgroundCreationWidget::HandleBackClicked()
{
	GoBack();
}

void UProjectCharacterBackgroundCreationWidget::HandleOptionSelected(const FName OptionID)
{
	if (!BackgroundComponent.IsValid())
	{
		return;
	}

	if (CurrentStep == EProjectCharacterBackgroundCreationStep::Backstory)
	{
		BackgroundComponent->SetBackstory(OptionID);
	}
	else if (CurrentStep == EProjectCharacterBackgroundCreationStep::Profession)
	{
		BackgroundComponent->SetProfession(OptionID);
	}

	RefreshDisplay();
	FocusCreationWidget();
}

#undef LOCTEXT_NAMESPACE
