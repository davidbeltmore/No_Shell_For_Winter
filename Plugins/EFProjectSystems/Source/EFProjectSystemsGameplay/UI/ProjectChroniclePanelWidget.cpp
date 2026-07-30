#include "UI/ProjectChroniclePanelWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace ProjectChroniclePanelWidgetPrivate
{
	constexpr float CompactWidth = 560.0f;
	constexpr float CompactHeight = 260.0f;
	constexpr float ExpandedWidth = 620.0f;
	constexpr float ExpandedHeight = 470.0f;
	constexpr float CompactEntriesHeight = 148.0f;
	constexpr float ExpandedEntriesHeight = 288.0f;
	constexpr float PanelCornerRadius = 12.0f;
	constexpr float PanelOutlineWidth = 1.2f;
	constexpr float PanelPaddingX = 20.0f;
	constexpr float PanelPaddingY = 19.0f;

	FLinearColor PanelFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.82f);
	}
	FLinearColor PanelOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.88f);
	}
	FLinearColor HazeTint()
	{
		return EFProjectUIPalette::Haze(0.12f);
	}
	FLinearColor TitleTint()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor HintTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.26f);

	const TCHAR* TitleFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Bebas.F_Chronicle_Bebas");
	const TCHAR* FrameTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame.T_Chronicle_Frame");
	const TCHAR* HazeTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Haze.T_Chronicle_Haze");
	const TCHAR* DividerTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Divider.T_Chronicle_Divider");
	const TCHAR* HudPillTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_HudPill.T_Chronicle_HudPill");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}
}

UProjectChroniclePanelWidget::UProjectChroniclePanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectChroniclePanelWidgetPrivate::TitleFontPath);
	FrameTexture = FSoftObjectPath(ProjectChroniclePanelWidgetPrivate::FrameTexturePath);
	HazeTexture = FSoftObjectPath(ProjectChroniclePanelWidgetPrivate::HazeTexturePath);
	DividerTexture = FSoftObjectPath(ProjectChroniclePanelWidgetPrivate::DividerTexturePath);
	HudPillTexture = FSoftObjectPath(ProjectChroniclePanelWidgetPrivate::HudPillTexturePath);
	CurrentPanelSize = GetDesignerPreviewPanelSize();
	CurrentEntriesHeight = IsExpandedDesignerPreview()
		? ProjectChroniclePanelWidgetPrivate::ExpandedEntriesHeight
		: ProjectChroniclePanelWidgetPrivate::CompactEntriesHeight;
	bExpanded = IsExpandedDesignerPreview();
}

void UProjectChroniclePanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshPanelVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectChroniclePanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshPanelVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectChroniclePanelWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	CurrentPanelSize = GetDesignerPreviewPanelSize();
	CurrentEntriesHeight = IsExpandedDesignerPreview()
		? ProjectChroniclePanelWidgetPrivate::ExpandedEntriesHeight
		: ProjectChroniclePanelWidgetPrivate::CompactEntriesHeight;
	bExpanded = IsExpandedDesignerPreview();
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshPanelVisuals();
	}

	return bBuiltTree;
}

void UProjectChroniclePanelWidget::ApplyPanelState(
	const bool bInExpanded,
	const FVector2D& InPanelSize,
	const float InEntriesHeight)
{
	bExpanded = bInExpanded;
	CurrentPanelSize = InPanelSize;
	CurrentEntriesHeight = FMath::Max(InEntriesHeight, 1.0f);

	BuildWidgetTree();
	InitializeVisualTree();
	RefreshPanelVisuals();
	OnChroniclePanelStateApplied(bExpanded, CurrentPanelSize, CurrentEntriesHeight);
}

bool UProjectChroniclePanelWidget::IsExpandedDesignerPreview() const
{
	return false;
}

FVector2D UProjectChroniclePanelWidget::GetDesignerPreviewPanelSize() const
{
	return FVector2D(ProjectChroniclePanelWidgetPrivate::CompactWidth, ProjectChroniclePanelWidgetPrivate::CompactHeight);
}

void UProjectChroniclePanelWidget::BuildWidgetTree()
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

bool UProjectChroniclePanelWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(CurrentPanelSize.X);
	RootSizeBox->SetHeightOverride(CurrentPanelSize.Y);
	TargetWidgetTree->RootWidget = RootSizeBox;

	FrameOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FrameOverlay"));
	RootSizeBox->AddChild(FrameOverlay);

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(0.0f));
	BackgroundBorder->SetBrushColor(FLinearColor::White);
	if (UOverlaySlot* BackgroundSlot = FrameOverlay->AddChildToOverlay(BackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	HazeImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HazeImage"));
	if (UOverlaySlot* HazeSlot = FrameOverlay->AddChildToOverlay(HazeImage))
	{
		HazeSlot->SetHorizontalAlignment(HAlign_Fill);
		HazeSlot->SetVerticalAlignment(VAlign_Fill);
		HazeSlot->SetPadding(FMargin(6.0f));
	}

	FrameTextureImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameTextureImage"));
	if (UOverlaySlot* FrameTextureSlot = FrameOverlay->AddChildToOverlay(FrameTextureImage))
	{
		FrameTextureSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameTextureSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(
		ProjectChroniclePanelWidgetPrivate::PanelPaddingX,
		ProjectChroniclePanelWidgetPrivate::PanelPaddingY,
		ProjectChroniclePanelWidgetPrivate::PanelPaddingX,
		ProjectChroniclePanelWidgetPrivate::PanelPaddingY));
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
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	TitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	if (UOverlaySlot* TitleSlot = HeaderOverlay->AddChildToOverlay(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Left);
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(8.0f, 0.0f, 140.0f, 0.0f));
	}

	USizeBox* HudPillSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HudPillSizeBox"));
	HudPillSizeBox->SetWidthOverride(98.0f);
	HudPillSizeBox->SetHeightOverride(24.0f);
	if (UOverlaySlot* PillSlot = HeaderOverlay->AddChildToOverlay(HudPillSizeBox))
	{
		PillSlot->SetHorizontalAlignment(HAlign_Right);
		PillSlot->SetVerticalAlignment(VAlign_Center);
	}

	HudPillOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudPillOverlay"));
	HudPillSizeBox->AddChild(HudPillOverlay);

	HudPillImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HudPillImage"));
	if (UOverlaySlot* PillImageSlot = HudPillOverlay->AddChildToOverlay(HudPillImage))
	{
		PillImageSlot->SetHorizontalAlignment(HAlign_Fill);
		PillImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	HintText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintText"));
	if (UOverlaySlot* HintSlot = HudPillOverlay->AddChildToOverlay(HintText))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetVerticalAlignment(VAlign_Center);
		HintSlot->SetPadding(FMargin(7.0f, 0.0f, 7.0f, 0.0f));
	}

	USizeBox* TopDividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TopDividerSizeBox"));
	TopDividerSizeBox->SetWidthOverride(278.0f);
	TopDividerSizeBox->SetHeightOverride(8.0f);
	if (UVerticalBoxSlot* DividerSlot = ContentBox->AddChildToVerticalBox(TopDividerSizeBox))
	{
		DividerSlot->SetHorizontalAlignment(HAlign_Left);
		DividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}

	TopDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TopDividerImage"));
	TopDividerSizeBox->AddChild(TopDividerImage);

	EntriesScrollSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("EntriesScrollSizeBox"));
	EntriesScrollSizeBox->SetWidthOverride(CurrentPanelSize.X - 40.0f);
	EntriesScrollSizeBox->SetHeightOverride(CurrentEntriesHeight);
	if (UVerticalBoxSlot* EntriesSlot = ContentBox->AddChildToVerticalBox(EntriesScrollSizeBox))
	{
		EntriesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	EntriesScrollBox = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("EntriesScrollBox"));
	EntriesScrollBox->SetAnimateWheelScrolling(false);
	EntriesScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
	EntriesScrollSizeBox->AddChild(EntriesScrollBox);

	EntriesBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EntriesBox"));
	EntriesScrollBox->AddChild(EntriesBox);

	USizeBox* FooterDividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FooterDividerSizeBox"));
	FooterDividerSizeBox->SetWidthOverride(278.0f);
	FooterDividerSizeBox->SetHeightOverride(8.0f);
	if (UVerticalBoxSlot* FooterSlot = ContentBox->AddChildToVerticalBox(FooterDividerSizeBox))
	{
		FooterSlot->SetHorizontalAlignment(HAlign_Center);
		FooterSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	FooterDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FooterDividerImage"));
	FooterDividerSizeBox->AddChild(FooterDividerImage);

	return true;
}

void UProjectChroniclePanelWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (TitleText)
	{
		TitleText->SetText(FText::FromString(TEXT("CHRONICLE")));
	}

	if (!bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = true;
		return;
	}

	if (BackgroundBorder)
	{
		const FSlateRoundedBoxBrush BackgroundBrush(
			ProjectChroniclePanelWidgetPrivate::PanelFillTint(),
			ProjectChroniclePanelWidgetPrivate::PanelCornerRadius,
			FSlateColor(ProjectChroniclePanelWidgetPrivate::PanelOutlineTint()),
			ProjectChroniclePanelWidgetPrivate::PanelOutlineWidth,
			FVector2f(CurrentPanelSize.X, CurrentPanelSize.Y));
		BackgroundBorder->SetBrush(BackgroundBrush);
	}

	if (FrameTextureImage)
	{
		FrameTextureImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(FrameTexture, ProjectChroniclePanelWidgetPrivate::FrameTexturePath)),
			false);
		FrameTextureImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FrameTextureImage,
			ProjectChroniclePanelWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.84f)));
	}

	if (HazeImage)
	{
		HazeImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HazeTexture, ProjectChroniclePanelWidgetPrivate::HazeTexturePath)),
			false);
		HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HazeImage,
			ProjectChroniclePanelWidgetPrivate::HazeTint()));
	}

	if (TitleText)
	{
		TitleText->SetFont(MakeTitleFont(28));
		TitleText->SetColorAndOpacity(FSlateColor(ProjectChroniclePanelWidgetPrivate::TitleTint()));
		TitleText->SetShadowOffset(FVector2D(0.0f, 0.45f));
		TitleText->SetShadowColorAndOpacity(ProjectChroniclePanelWidgetPrivate::ShadowTint);
	}

	if (HudPillImage)
	{
		HudPillImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HudPillTexture, ProjectChroniclePanelWidgetPrivate::HudPillTexturePath)),
			false);
		HudPillImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HudPillImage,
			ProjectChroniclePanelWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.92f)));
	}

	if (HintText)
	{
		HintText->SetFont(MakeTitleFont(11));
		HintText->SetColorAndOpacity(FSlateColor(ProjectChroniclePanelWidgetPrivate::HintTint()));
		HintText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		HintText->SetShadowColorAndOpacity(ProjectChroniclePanelWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.18f));
	}

	if (TopDividerImage)
	{
		TopDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectChroniclePanelWidgetPrivate::DividerTexturePath)),
			false);
		TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TopDividerImage,
			ProjectChroniclePanelWidgetPrivate::TitleTint().CopyWithNewOpacity(0.92f)));
	}

	if (FooterDividerImage)
	{
		FooterDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectChroniclePanelWidgetPrivate::DividerTexturePath)),
			false);
		FooterDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FooterDividerImage,
			ProjectChroniclePanelWidgetPrivate::TitleTint().CopyWithNewOpacity(0.86f)));
	}

	bVisualTreeInitialized = true;
}

void UProjectChroniclePanelWidget::RefreshPanelVisuals()
{
	if (RootSizeBox)
	{
		RootSizeBox->SetWidthOverride(CurrentPanelSize.X);
		RootSizeBox->SetHeightOverride(CurrentPanelSize.Y);
	}

	if (EntriesScrollSizeBox)
	{
		EntriesScrollSizeBox->SetWidthOverride(FMath::Max(CurrentPanelSize.X - 40.0f, 1.0f));
		EntriesScrollSizeBox->SetHeightOverride(CurrentEntriesHeight);
	}

	if (HintText)
	{
		HintText->SetText(FText::FromString(bExpanded ? TEXT("J  COLLAPSE") : TEXT("J  EXPAND")));
	}

	if (BackgroundBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush BackgroundBrush(
			ProjectChroniclePanelWidgetPrivate::PanelFillTint(),
			ProjectChroniclePanelWidgetPrivate::PanelCornerRadius,
			FSlateColor(ProjectChroniclePanelWidgetPrivate::PanelOutlineTint()),
			ProjectChroniclePanelWidgetPrivate::PanelOutlineWidth,
			FVector2f(CurrentPanelSize.X, CurrentPanelSize.Y));
		BackgroundBorder->SetBrush(BackgroundBrush);
	}
}

UTexture2D* UProjectChroniclePanelWidget::ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectChroniclePanelWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectChroniclePanelWidget::ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedObject = AssetPtr.LoadSynchronous())
		{
			return LoadedObject;
		}
	}

	return ProjectChroniclePanelWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectChroniclePanelWidget::MakeTitleFont(const int32 Size) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = 0;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectChroniclePanelWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

bool UProjectChronicleNormalGlobalWidget::IsExpandedDesignerPreview() const
{
	return false;
}

bool UProjectChronicleExpandedGlobalWidget::IsExpandedDesignerPreview() const
{
	return true;
}

FVector2D UProjectChronicleExpandedGlobalWidget::GetDesignerPreviewPanelSize() const
{
	return FVector2D(ProjectChroniclePanelWidgetPrivate::ExpandedWidth, ProjectChroniclePanelWidgetPrivate::ExpandedHeight);
}
