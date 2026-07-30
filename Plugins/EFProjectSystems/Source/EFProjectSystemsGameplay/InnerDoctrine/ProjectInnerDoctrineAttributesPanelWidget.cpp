#include "InnerDoctrine/ProjectInnerDoctrineAttributesPanelWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/Texture2D.h"
#include "InnerDoctrine/ProjectInnerDoctrineAttributeCardWidget.h"
#include "Styling/CoreStyle.h"

namespace ProjectInnerDoctrineAttributesPanelWidgetPrivate
{
	constexpr float PanelWidth = 448.0f;
	constexpr float BasePanelHeight = 206.0f;
	constexpr int32 MaxCardsPerRow = 7;
	constexpr float PanelCornerRadius = 20.0f;
	constexpr float PanelOutlineWidth = 1.2f;
	constexpr float PanelPaddingLeft = 12.0f;
	constexpr float PanelPaddingTop = 10.0f;
	constexpr float PanelPaddingRight = 12.0f;
	constexpr float PanelPaddingBottom = 10.0f;
	constexpr float SummaryBoxHeight = 46.0f;
	constexpr float SummaryBoxWidth = 198.0f;
	constexpr float CardsWrapWidth = 416.0f;

	FLinearColor PanelFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.78f);
	}
	FLinearColor PanelOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.86f);
	}
	FLinearColor SummaryFillTint()
	{
		return EFProjectUIPalette::PanelFill(0.84f);
	}
	FLinearColor SummaryOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.48f);
	}
	FLinearColor HazeTint()
	{
		return EFProjectUIPalette::Haze(0.16f);
	}
	FLinearColor TitleTint()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor LabelTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	FLinearColor ValueTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.42f);

	const TCHAR* CinzelFontPath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Fonts/F_InnerState_Cinzel.F_InnerState_Cinzel");
	const TCHAR* CormorantFontPath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Fonts/F_InnerState_Cormorant.F_InnerState_Cormorant");
	const TCHAR* FrameTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Frame.T_InnerDoctrine_Frame");
	const TCHAR* HazeTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Haze.T_InnerDoctrine_Haze");
	const TCHAR* DividerTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_Divider.T_InnerDoctrine_Divider");
	const TCHAR* HudPillTexturePath = TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/T_InnerDoctrine_HudPill.T_InnerDoctrine_HudPill");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}
}

UProjectInnerDoctrineAttributesPanelWidget::UProjectInnerDoctrineAttributesPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::CinzelFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::CormorantFontPath);
	FrameTexture = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::FrameTexturePath);
	HazeTexture = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::HazeTexturePath);
	DividerTexture = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::DividerTexturePath);
	HudPillTexture = FSoftObjectPath(ProjectInnerDoctrineAttributesPanelWidgetPrivate::HudPillTexturePath);
	CurrentPanelHeight = ProjectInnerDoctrineAttributesPanelWidgetPrivate::BasePanelHeight;
}

void UProjectInnerDoctrineAttributesPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyPanelState(false, 0, 0, CurrentPanelHeight, CurrentCardCount);
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineAttributesPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyPanelState(false, 0, 0, CurrentPanelHeight, CurrentCardCount);
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineAttributesPanelWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);
	RefreshPanelBrush();
	RefreshSummaryBrushes();
}

bool UProjectInnerDoctrineAttributesPanelWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	CurrentPanelHeight = ProjectInnerDoctrineAttributesPanelWidgetPrivate::BasePanelHeight;
	CurrentCardCount = 7;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		ApplyPanelState(false, 120, 45, CurrentPanelHeight, CurrentCardCount);
	}

	return bBuiltTree;
}

void UProjectInnerDoctrineAttributesPanelWidget::ApplyPanelState(
	const bool bDoctrineMasteryMode,
	const int32 CurrentRunDxp,
	const int32 MetaBankDxp,
	const float PanelHeight,
	const int32 CardCount)
{
	CurrentPanelHeight = FMath::Max(PanelHeight, 1.0f);
	CurrentCardCount = FMath::Max(CardCount, 0);

	BuildWidgetTree();
	InitializeVisualTree();

	if (RootSizeBox && bUsingNativeFallbackTree)
	{
		RootSizeBox->SetWidthOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelWidth);
		RootSizeBox->SetHeightOverride(CurrentPanelHeight);
	}

	if (ModeText)
	{
		ModeText->SetText(FText::FromString(bDoctrineMasteryMode ? TEXT("MASTERY") : TEXT("RUN")));
	}

	if (RunSummaryValueText)
	{
		RunSummaryValueText->SetText(FText::AsNumber(CurrentRunDxp));
	}

	if (MetaSummaryValueText)
	{
		MetaSummaryValueText->SetText(FText::AsNumber(MetaBankDxp));
	}

	if (AttributesWrapBox && bUsingNativeFallbackTree)
	{
		AttributesWrapBox->SetWrapSize(ProjectInnerDoctrineAttributesPanelWidgetPrivate::CardsWrapWidth);
	}

	if (FrameTextureImage && bUsingNativeFallbackTree)
	{
		const int32 SafeCardCount = FMath::Max(1, CurrentCardCount);
		const int32 RowCount = FMath::Max(1, FMath::DivideAndRoundUp(SafeCardCount, ProjectInnerDoctrineAttributesPanelWidgetPrivate::MaxCardsPerRow));
		FrameTextureImage->SetVisibility(RowCount == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	RefreshPanelBrush();
	RefreshSummaryBrushes();
	OnAttributesPanelStateApplied(bDoctrineMasteryMode, CurrentRunDxp, MetaBankDxp, CurrentPanelHeight, CurrentCardCount);
}

void UProjectInnerDoctrineAttributesPanelWidget::BuildWidgetTree()
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

bool UProjectInnerDoctrineAttributesPanelWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelWidth);
	RootSizeBox->SetHeightOverride(CurrentPanelHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	FrameOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FrameOverlay"));
	RootSizeBox->AddChild(FrameOverlay);

	BackgroundImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BackgroundImage"));
	if (UOverlaySlot* BackgroundSlot = FrameOverlay->AddChildToOverlay(BackgroundImage))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	HazeImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HazeImage"));
	if (UOverlaySlot* HazeSlot = FrameOverlay->AddChildToOverlay(HazeImage))
	{
		HazeSlot->SetHorizontalAlignment(HAlign_Fill);
		HazeSlot->SetVerticalAlignment(VAlign_Fill);
		HazeSlot->SetPadding(FMargin(6.0f, 4.0f, 6.0f, 4.0f));
	}

	FrameTextureImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameTextureImage"));
	if (UOverlaySlot* FrameTextureSlot = FrameOverlay->AddChildToOverlay(FrameTextureImage))
	{
		FrameTextureSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameTextureSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelPaddingLeft,
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelPaddingTop,
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelPaddingRight,
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelPaddingBottom));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = FrameOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	HeaderOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HeaderOverlay"));
	if (UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(HeaderOverlay))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	}

	TitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	if (UOverlaySlot* TitleSlot = HeaderOverlay->AddChildToOverlay(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(34.0f, 8.0f, 34.0f, 0.0f));
	}

	USizeBox* HudPillSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HudPillSizeBox"));
	HudPillSizeBox->SetWidthOverride(72.0f);
	HudPillSizeBox->SetHeightOverride(24.0f);
	if (UOverlaySlot* PillSizeSlot = HeaderOverlay->AddChildToOverlay(HudPillSizeBox))
	{
		PillSizeSlot->SetHorizontalAlignment(HAlign_Right);
		PillSizeSlot->SetVerticalAlignment(VAlign_Center);
		PillSizeSlot->SetPadding(FMargin(0.0f, 6.0f, 6.0f, 0.0f));
	}

	HudPillOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudPillOverlay"));
	HudPillSizeBox->AddChild(HudPillOverlay);

	HudPillImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HudPillImage"));
	if (UOverlaySlot* PillImageSlot = HudPillOverlay->AddChildToOverlay(HudPillImage))
	{
		PillImageSlot->SetHorizontalAlignment(HAlign_Fill);
		PillImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ModeText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ModeText"));
	if (UOverlaySlot* ModeTextSlot = HudPillOverlay->AddChildToOverlay(ModeText))
	{
		ModeTextSlot->SetHorizontalAlignment(HAlign_Center);
		ModeTextSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* TopDividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TopDividerSizeBox"));
	TopDividerSizeBox->SetWidthOverride(238.0f);
	TopDividerSizeBox->SetHeightOverride(12.0f);
	if (UVerticalBoxSlot* DividerSlot = ContentBox->AddChildToVerticalBox(TopDividerSizeBox))
	{
		DividerSlot->SetHorizontalAlignment(HAlign_Center);
		DividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	}

	TopDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TopDividerImage"));
	TopDividerSizeBox->AddChild(TopDividerImage);

	SummaryRow = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SummaryRow"));
	if (UVerticalBoxSlot* SummarySlot = ContentBox->AddChildToVerticalBox(SummaryRow))
	{
		SummarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 9.0f));
	}

	USizeBox* RunSummarySizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RunSummarySizeBox"));
	RunSummarySizeBox->SetHeightOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxHeight);
	RunSummarySizeBox->SetWidthOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxWidth);
	if (UHorizontalBoxSlot* RunSummarySlot = SummaryRow->AddChildToHorizontalBox(RunSummarySizeBox))
	{
		RunSummarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RunSummarySlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	RunSummaryBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RunSummaryBorder"));
	RunSummaryBorder->SetPadding(FMargin(10.0f, 6.0f, 10.0f, 4.0f));
	RunSummaryBorder->SetBrushColor(FLinearColor::White);
	RunSummarySizeBox->AddChild(RunSummaryBorder);

	UVerticalBox* RunSummaryBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RunSummaryBox"));
	RunSummaryBorder->SetContent(RunSummaryBox);

	RunSummaryLabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RunSummaryLabelText"));
	if (UVerticalBoxSlot* RunLabelSlot = RunSummaryBox->AddChildToVerticalBox(RunSummaryLabelText))
	{
		RunLabelSlot->SetHorizontalAlignment(HAlign_Center);
		RunLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f));
	}

	RunSummaryValueScaleBox = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("RunSummaryValueScaleBox"));
	RunSummaryValueScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UVerticalBoxSlot* RunValueScaleSlot = RunSummaryBox->AddChildToVerticalBox(RunSummaryValueScaleBox))
	{
		RunValueScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		RunValueScaleSlot->SetVerticalAlignment(VAlign_Center);
		RunValueScaleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	RunSummaryValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RunSummaryValueText"));
	RunSummaryValueScaleBox->AddChild(RunSummaryValueText);

	USizeBox* MetaSummarySizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MetaSummarySizeBox"));
	MetaSummarySizeBox->SetHeightOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxHeight);
	MetaSummarySizeBox->SetWidthOverride(ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxWidth);
	if (UHorizontalBoxSlot* MetaSummarySlot = SummaryRow->AddChildToHorizontalBox(MetaSummarySizeBox))
	{
		MetaSummarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	MetaSummaryBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MetaSummaryBorder"));
	MetaSummaryBorder->SetPadding(FMargin(10.0f, 6.0f, 10.0f, 4.0f));
	MetaSummaryBorder->SetBrushColor(FLinearColor::White);
	MetaSummarySizeBox->AddChild(MetaSummaryBorder);

	UVerticalBox* MetaSummaryBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MetaSummaryBox"));
	MetaSummaryBorder->SetContent(MetaSummaryBox);

	MetaSummaryLabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MetaSummaryLabelText"));
	if (UVerticalBoxSlot* MetaLabelSlot = MetaSummaryBox->AddChildToVerticalBox(MetaSummaryLabelText))
	{
		MetaLabelSlot->SetHorizontalAlignment(HAlign_Center);
		MetaLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f));
	}

	MetaSummaryValueScaleBox = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("MetaSummaryValueScaleBox"));
	MetaSummaryValueScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UVerticalBoxSlot* MetaValueScaleSlot = MetaSummaryBox->AddChildToVerticalBox(MetaSummaryValueScaleBox))
	{
		MetaValueScaleSlot->SetHorizontalAlignment(HAlign_Fill);
		MetaValueScaleSlot->SetVerticalAlignment(VAlign_Center);
		MetaValueScaleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	MetaSummaryValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MetaSummaryValueText"));
	MetaSummaryValueScaleBox->AddChild(MetaSummaryValueText);

	AttributeCardsGlobal = TargetWidgetTree->ConstructWidget<UProjectInnerDoctrineAttributeCardsGlobalWidget>(
		UProjectInnerDoctrineAttributeCardsGlobalWidget::StaticClass(),
		TEXT("AttributeCardsGlobal"));
	if (UVerticalBoxSlot* CardsGlobalSlot = ContentBox->AddChildToVerticalBox(AttributeCardsGlobal))
	{
		CardsGlobalSlot->SetHorizontalAlignment(HAlign_Center);
		CardsGlobalSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		CardsGlobalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	AttributesWrapBox = TargetWidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("AttributesWrapBox"));
	AttributesWrapBox->SetOrientation(EOrientation::Orient_Horizontal);
	AttributesWrapBox->SetHorizontalAlignment(HAlign_Center);
	AttributesWrapBox->SetInnerSlotPadding(FVector2D(2.0f, 4.0f));
	AttributesWrapBox->SetExplicitWrapSize(true);
	AttributesWrapBox->SetWrapSize(ProjectInnerDoctrineAttributesPanelWidgetPrivate::CardsWrapWidth);
	AttributesWrapBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* CardsSlot = ContentBox->AddChildToVerticalBox(AttributesWrapBox))
	{
		CardsSlot->SetHorizontalAlignment(HAlign_Center);
		CardsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	}

	USizeBox* BottomDividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BottomDividerSizeBox"));
	BottomDividerSizeBox->SetWidthOverride(116.0f);
	BottomDividerSizeBox->SetHeightOverride(10.0f);
	if (UVerticalBoxSlot* BottomDividerSlot = ContentBox->AddChildToVerticalBox(BottomDividerSizeBox))
	{
		BottomDividerSlot->SetHorizontalAlignment(HAlign_Center);
	}

	BottomDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BottomDividerImage"));
	BottomDividerSizeBox->AddChild(BottomDividerImage);

	return true;
}

void UProjectInnerDoctrineAttributesPanelWidget::InitializeVisualTree()
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

	RefreshPanelBrush();
	RefreshSummaryBrushes();

	if (FrameTextureImage)
	{
		if (UTexture2D* ResolvedFrameTexture = ResolveTexture(FrameTexture, ProjectInnerDoctrineAttributesPanelWidgetPrivate::FrameTexturePath))
		{
			FrameTextureImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolvedFrameTexture), false);
		}
		FrameTextureImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FrameTextureImage,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.84f)));
	}

	if (HazeImage)
	{
		if (UTexture2D* ResolvedHazeTexture = ResolveTexture(HazeTexture, ProjectInnerDoctrineAttributesPanelWidgetPrivate::HazeTexturePath))
		{
			HazeImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolvedHazeTexture), false);
		}
		HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HazeImage,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::HazeTint()));
	}

	if (TitleText)
	{
		TitleText->SetText(FText::FromString(TEXT("INNER DOCTRINE")));
		TitleText->SetFont(MakeTitleFont(15, 0));
		TitleText->SetJustification(ETextJustify::Center);
		TitleText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::TitleTint()));
		TitleText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		TitleText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.34f));
	}

	if (HudPillImage)
	{
		if (UTexture2D* ResolvedHudPillTexture = ResolveTexture(HudPillTexture, ProjectInnerDoctrineAttributesPanelWidgetPrivate::HudPillTexturePath))
		{
			HudPillImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolvedHudPillTexture), false);
		}
		HudPillImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HudPillImage,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.92f)));
	}

	if (ModeText)
	{
		ModeText->SetFont(MakeTitleFont(10, 0));
		ModeText->SetJustification(ETextJustify::Center);
		ModeText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ValueTint()));
		ModeText->SetShadowOffset(FVector2D(0.0f, 0.25f));
		ModeText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.26f));
	}

	if (TopDividerImage)
	{
		if (UTexture2D* ResolvedDividerTexture = ResolveTexture(DividerTexture, ProjectInnerDoctrineAttributesPanelWidgetPrivate::DividerTexturePath))
		{
			TopDividerImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolvedDividerTexture), false);
		}
		TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TopDividerImage,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::TitleTint().CopyWithNewOpacity(0.92f)));
	}

	if (BottomDividerImage)
	{
		if (UTexture2D* ResolvedDividerTexture = ResolveTexture(DividerTexture, ProjectInnerDoctrineAttributesPanelWidgetPrivate::DividerTexturePath))
		{
			BottomDividerImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolvedDividerTexture), false);
		}
		BottomDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			BottomDividerImage,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::TitleTint().CopyWithNewOpacity(0.74f)));
	}

	if (RunSummaryLabelText)
	{
		RunSummaryLabelText->SetText(FText::FromString(TEXT("DXP")));
		RunSummaryLabelText->SetFont(MakeTitleFont(10, 0));
		RunSummaryLabelText->SetJustification(ETextJustify::Center);
		RunSummaryLabelText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::LabelTint()));
		RunSummaryLabelText->SetShadowOffset(FVector2D(0.0f, 0.25f));
		RunSummaryLabelText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.24f));
	}

	if (MetaSummaryLabelText)
	{
		MetaSummaryLabelText->SetText(FText::FromString(TEXT("META")));
		MetaSummaryLabelText->SetFont(MakeTitleFont(10, 0));
		MetaSummaryLabelText->SetJustification(ETextJustify::Center);
		MetaSummaryLabelText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::LabelTint()));
		MetaSummaryLabelText->SetShadowOffset(FVector2D(0.0f, 0.25f));
		MetaSummaryLabelText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.24f));
	}

	if (RunSummaryValueText)
	{
		RunSummaryValueText->SetFont(MakeBodyFont(17, 0));
		RunSummaryValueText->SetJustification(ETextJustify::Center);
		RunSummaryValueText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ValueTint()));
		RunSummaryValueText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		RunSummaryValueText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.30f));
	}

	if (MetaSummaryValueText)
	{
		MetaSummaryValueText->SetFont(MakeBodyFont(17, 0));
		MetaSummaryValueText->SetJustification(ETextJustify::Center);
		MetaSummaryValueText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ValueTint()));
		MetaSummaryValueText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		MetaSummaryValueText->SetShadowColorAndOpacity(ProjectInnerDoctrineAttributesPanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.30f));
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerDoctrineAttributesPanelWidget::RefreshPanelBrush()
{
	if (!BackgroundImage || !bUsingNativeFallbackTree)
	{
		return;
	}

	const FSlateRoundedBoxBrush PanelBrush(
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelFillTint(),
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelCornerRadius,
		FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelOutlineTint()),
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelOutlineWidth,
		FVector2f(ProjectInnerDoctrineAttributesPanelWidgetPrivate::PanelWidth, CurrentPanelHeight));
	BackgroundImage->SetBrush(PanelBrush);
	BackgroundImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
		BackgroundImage,
		FLinearColor::White));
}

void UProjectInnerDoctrineAttributesPanelWidget::RefreshSummaryBrushes()
{
	if (!bUsingNativeFallbackTree)
	{
		return;
	}

	const FSlateRoundedBoxBrush SummaryBrush(
		ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryFillTint(),
		11.0f,
		FSlateColor(ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryOutlineTint()),
		1.0f,
		FVector2f(
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxWidth,
			ProjectInnerDoctrineAttributesPanelWidgetPrivate::SummaryBoxHeight));

	if (RunSummaryBorder)
	{
		RunSummaryBorder->SetBrush(SummaryBrush);
	}

	if (MetaSummaryBorder)
	{
		MetaSummaryBorder->SetBrush(SummaryBrush);
	}
}

UTexture2D* UProjectInnerDoctrineAttributesPanelWidget::ResolveTexture(
	const TSoftObjectPtr<UTexture2D>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectInnerDoctrineAttributesPanelWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerDoctrineAttributesPanelWidget::ResolveStyleAsset(
	const TSoftObjectPtr<UObject>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedObject = AssetPtr.LoadSynchronous())
		{
			return LoadedObject;
		}
	}

	return ProjectInnerDoctrineAttributesPanelWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerDoctrineAttributesPanelWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectInnerDoctrineAttributesPanelWidgetPrivate::CinzelFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerDoctrineAttributesPanelWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectInnerDoctrineAttributesPanelWidgetPrivate::CormorantFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}
