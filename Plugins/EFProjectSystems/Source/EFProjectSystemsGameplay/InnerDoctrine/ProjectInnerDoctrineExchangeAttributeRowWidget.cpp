#include "InnerDoctrine/ProjectInnerDoctrineExchangeAttributeRowWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
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
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

namespace ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate
{
	constexpr float DefaultRowWidth = 560.0f;
	constexpr float DefaultRowHeight = 88.0f;
	constexpr float RowCornerRadius = 12.0f;
	constexpr float RowOutlineWidth = 1.2f;
	constexpr float IconSize = 84.0f;
	constexpr float RowsGlobalWidth = 560.0f;
	constexpr float RowsGlobalHeight = 690.0f;

	FLinearColor DefaultAccentTint()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor UnselectedFillTint()
	{
		return EFProjectUIPalette::PanelFill(0.92f);
	}
	FLinearColor SelectedFillTint()
	{
		return EFProjectUIPalette::SectionFill(0.96f);
	}
	FLinearColor UnselectedOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.52f);
	}
	FLinearColor SelectedOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.96f);
	}
	FLinearColor IconPlateFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.95f);
	}
	FLinearColor NameTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor MetaTint()
	{
		return EFProjectUIPalette::SecondaryText(0.96f);
	}
	FLinearColor WarningTint()
	{
		return EFProjectUIPalette::Warning();
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.32f);

	const TCHAR* TitleFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Bebas.F_Chronicle_Bebas");
	const TCHAR* BodyFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Cormorant.F_Chronicle_Cormorant");
	const TCHAR* RowFrameTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_RowFrame.T_InnerDoctrine_Altar_RowFrame");
	const TCHAR* DefaultIconTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Default.T_InnerDoctrine_Altar_Icon_Default");
	const TCHAR* SelectionGlyphTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Glyph.T_InnerDoctrine_Altar_Glyph");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FProjectInnerDoctrineExchangeAttributeRowDisplayData MakePreviewData(
		const TCHAR* AttributeName,
		const TCHAR* DisplayLabel,
		const int32 Level,
		const int32 Cost,
		const FLinearColor& AccentTint,
		const TCHAR* IconPath,
		const bool bSelected = false)
	{
		FProjectInnerDoctrineExchangeAttributeRowDisplayData Data;
		Data.AttributeName = FName(AttributeName);
		Data.DisplayLabel = DisplayLabel;
		Data.IconTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(IconPath));
		Data.AccentTint = AccentTint;
		Data.Level = Level;
		Data.Cost = Cost;
		Data.RowWidth = DefaultRowWidth;
		Data.RowHeight = DefaultRowHeight;
		Data.bSelected = bSelected;
		Data.bAffordable = true;
		return Data;
	}

	const TCHAR* IconPathForAttribute(const TCHAR* AttributeName)
	{
		if (FCString::Strcmp(AttributeName, TEXT("Willpower")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Willpower.T_InnerDoctrine_Altar_Icon_Willpower");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Offensive")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Offensive.T_InnerDoctrine_Altar_Icon_Offensive");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Defensive")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Defensive.T_InnerDoctrine_Altar_Icon_Defensive");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Faith")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Faith.T_InnerDoctrine_Altar_Icon_Faith");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Cunning")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Cunning.T_InnerDoctrine_Altar_Icon_Cunning");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Celerity")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Celerity.T_InnerDoctrine_Altar_Icon_Celerity");
		}
		if (FCString::Strcmp(AttributeName, TEXT("Charisma")) == 0)
		{
			return TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Charisma.T_InnerDoctrine_Altar_Icon_Charisma");
		}
		return DefaultIconTexturePath;
	}

	UClass* NativeRowClassForAttribute(const FName AttributeName)
	{
		if (AttributeName == FName(TEXT("Willpower")))
		{
			return UProjectInnerDoctrineExchangeWillpowerRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Offensive")))
		{
			return UProjectInnerDoctrineExchangeOffensiveRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Defensive")))
		{
			return UProjectInnerDoctrineExchangeDefensiveRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Faith")))
		{
			return UProjectInnerDoctrineExchangeFaithRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Cunning")))
		{
			return UProjectInnerDoctrineExchangeCunningRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Celerity")))
		{
			return UProjectInnerDoctrineExchangeCelerityRowWidget::StaticClass();
		}
		if (AttributeName == FName(TEXT("Charisma")))
		{
			return UProjectInnerDoctrineExchangeCharismaRowWidget::StaticClass();
		}
		return UProjectInnerDoctrineExchangeAttributeRowWidget::StaticClass();
	}
}

UProjectInnerDoctrineExchangeAttributeRowWidget::UProjectInnerDoctrineExchangeAttributeRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::TitleFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::BodyFontPath);
	RowFrameTexture = FSoftObjectPath(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowFrameTexturePath);
	DefaultIconTexture = FSoftObjectPath(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultIconTexturePath);
	SelectionGlyphTexture = FSoftObjectPath(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::SelectionGlyphTexturePath);
	CurrentDisplayData = MakeDesignerPreviewData();
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	// Converted row Blueprints can retain authored purple brushes and textures;
	// their nested WidgetTree is not guaranteed to be traversed by the host's
	// generic pass. Resolve every row-owned image through the native pack here.
	if (FrameImage)
	{
		const float Alpha = FrameImage->GetColorAndOpacity().A;
		FrameImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(
				ResolveTexture(RowFrameTexture, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowFrameTexturePath)),
			false);
		FrameImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
	}
	if (SelectionGlyphImage)
	{
		const float Alpha = SelectionGlyphImage->GetColorAndOpacity().A;
		SelectionGlyphImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(
				ResolveTexture(SelectionGlyphTexture, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::SelectionGlyphTexturePath)),
			false);
		SelectionGlyphImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
	}
	if (IconImage)
	{
		const float Alpha = IconImage->GetColorAndOpacity().A;
		const TSoftObjectPtr<UTexture2D>& SourceIcon =
			CurrentDisplayData.IconTexture.IsNull() ? DefaultIconTexture : CurrentDisplayData.IconTexture;
		IconImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(
				ResolveTexture(SourceIcon, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultIconTexturePath)),
			false);
		IconImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
	}

	const float RowWidth = FMath::Max(CurrentDisplayData.RowWidth, 320.0f);
	const float RowHeight = FMath::Max(CurrentDisplayData.RowHeight, 70.0f);
	const bool bSelected = CurrentDisplayData.bSelected;
	if (BackgroundBorder)
	{
		BackgroundBorder->SetBrush(FSlateRoundedBoxBrush(
			bSelected ? Theme.SectionFill : Theme.PanelFill,
			ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowCornerRadius,
			FSlateColor(bSelected ? Theme.Outline : Theme.OutlineDim),
			ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowOutlineWidth,
			FVector2f(RowWidth, RowHeight)));
		BackgroundBorder->SetBrushColor(FLinearColor::White);
	}
	if (IconBackgroundBorder)
	{
		IconBackgroundBorder->SetBrush(FSlateRoundedBoxBrush(
			Theme.PanelFillDeep,
			10.0f,
			FSlateColor(Theme.OutlineDim),
			1.0f,
			FVector2f(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize,
				ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize)));
		IconBackgroundBorder->SetBrushColor(FLinearColor::White);
	}

	// Refresh text/selection state after the brush replacement so a row that is
	// currently selected keeps its visibility and cost affordance unchanged.
	RefreshVisuals();
}

bool UProjectInnerDoctrineExchangeAttributeRowWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	CurrentDisplayData = MakeDesignerPreviewData();
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshVisuals();
	}
	return bBuiltTree;
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::ApplyDisplayData(const FProjectInnerDoctrineExchangeAttributeRowDisplayData& InDisplayData)
{
	CurrentDisplayData = InDisplayData;
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox && WidgetTree->RootWidget == RootSizeBox)
	{
		return;
	}

	if (WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerDoctrineExchangeAttributeRowWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultRowWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultRowHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	DesignerRootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DesignerRootOverlay"));
	RootSizeBox->AddChild(DesignerRootOverlay);

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	if (UOverlaySlot* RootOverlaySlot = DesignerRootOverlay->AddChildToOverlay(RootOverlay))
	{
		RootOverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		RootOverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(0.0f));
	BackgroundBorder->SetBrushColor(FLinearColor::White);
	if (UOverlaySlot* BackgroundSlot = RootOverlay->AddChildToOverlay(BackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	FrameImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameImage"));
	if (UOverlaySlot* FrameSlot = RootOverlay->AddChildToOverlay(FrameImage))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(10.0f, 10.0f, 12.0f, 10.0f));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	IconSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconSizeBox"));
	IconSizeBox->SetWidthOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize);
	IconSizeBox->SetHeightOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize);
	if (UHorizontalBoxSlot* IconSlot = ContentBox->AddChildToHorizontalBox(IconSizeBox))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Left);
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}

	IconBackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("IconBackgroundBorder"));
	IconBackgroundBorder->SetPadding(FMargin(16.0f));
	IconBackgroundBorder->SetBrushColor(FLinearColor::White);
	IconSizeBox->AddChild(IconBackgroundBorder);

	IconImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
	IconBackgroundBorder->SetContent(IconImage);

	TextColumn = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TextColumn"));
	if (UHorizontalBoxSlot* TextSlot = ContentBox->AddChildToHorizontalBox(TextColumn))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetHorizontalAlignment(HAlign_Fill);
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(16.0f, 0.0f, 10.0f, 0.0f));
	}

	NameText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameText"));
	if (UVerticalBoxSlot* NameSlot = TextColumn->AddChildToVerticalBox(NameText))
	{
		NameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f));
	}

	MetaRow = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MetaRow"));
	TextColumn->AddChildToVerticalBox(MetaRow);

	LevelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelText"));
	if (UHorizontalBoxSlot* LevelSlot = MetaRow->AddChildToHorizontalBox(LevelText))
	{
		LevelSlot->SetHorizontalAlignment(HAlign_Left);
		LevelSlot->SetVerticalAlignment(VAlign_Center);
	}

	SeparatorText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SeparatorText"));
	if (UHorizontalBoxSlot* SeparatorSlot = MetaRow->AddChildToHorizontalBox(SeparatorText))
	{
		SeparatorSlot->SetPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f));
		SeparatorSlot->SetHorizontalAlignment(HAlign_Left);
		SeparatorSlot->SetVerticalAlignment(VAlign_Center);
	}

	CostText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CostText"));
	MetaRow->AddChildToHorizontalBox(CostText);

	SelectionGlyphSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SelectionGlyphSizeBox"));
	SelectionGlyphSizeBox->SetWidthOverride(18.0f);
	SelectionGlyphSizeBox->SetHeightOverride(18.0f);
	if (UHorizontalBoxSlot* SelectionSlot = ContentBox->AddChildToHorizontalBox(SelectionGlyphSizeBox))
	{
		SelectionSlot->SetHorizontalAlignment(HAlign_Center);
		SelectionSlot->SetVerticalAlignment(VAlign_Center);
	}

	SelectionGlyphImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SelectionGlyphImage"));
	SelectionGlyphSizeBox->AddChild(SelectionGlyphImage);

	return true;
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::InitializeVisualTree()
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

	if (FrameImage)
	{
		FrameImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(RowFrameTexture, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowFrameTexturePath)),
			false);
	}

	if (SelectionGlyphImage)
	{
		SelectionGlyphImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(SelectionGlyphTexture, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::SelectionGlyphTexturePath)),
			false);
	}

	if (NameText)
	{
		NameText->SetFont(MakeTitleFont(24, 0));
		NameText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::NameTint()));
		NameText->SetShadowOffset(FVector2D(0.0f, 0.30f));
		NameText->SetShadowColorAndOpacity(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::ShadowTint);
	}

	if (LevelText)
	{
		LevelText->SetFont(MakeBodyFont(15, 0));
		LevelText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MetaTint()));
	}

	if (SeparatorText)
	{
		SeparatorText->SetFont(MakeBodyFont(15, 0));
		SeparatorText->SetText(FText::FromString(TEXT("|")));
		SeparatorText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MetaTint().CopyWithNewOpacity(0.74f)));
	}

	if (CostText)
	{
		CostText->SetFont(MakeBodyFont(15, 0));
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerDoctrineExchangeAttributeRowWidget::RefreshVisuals()
{
	const float RowWidth = FMath::Max(CurrentDisplayData.RowWidth, 320.0f);
	const float RowHeight = FMath::Max(CurrentDisplayData.RowHeight, 70.0f);
	const FLinearColor AccentTint = CurrentDisplayData.AccentTint.A > KINDA_SMALL_NUMBER
		? CurrentDisplayData.AccentTint.GetClamped(0.0f, 1.0f)
		: ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultAccentTint();
	const bool bSelected = CurrentDisplayData.bSelected;

	if (bUsingNativeFallbackTree)
	{
		if (RootSizeBox)
		{
			RootSizeBox->SetWidthOverride(RowWidth);
			RootSizeBox->SetHeightOverride(RowHeight);
		}

		if (BackgroundBorder)
		{
			const FLinearColor FillTint = bSelected
				? ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::SelectedFillTint()
				: ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::UnselectedFillTint();
			const FLinearColor OutlineTint = bSelected
				? AccentTint.CopyWithNewOpacity(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::SelectedOutlineTint().A)
				: ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::UnselectedOutlineTint();
			const FSlateRoundedBoxBrush BackgroundBrush(
				FillTint,
				ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowCornerRadius,
				FSlateColor(OutlineTint),
				ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowOutlineWidth,
				FVector2f(RowWidth, RowHeight));
			BackgroundBorder->SetBrush(BackgroundBrush);
		}

		if (FrameImage)
		{
			FrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				FrameImage,
				AccentTint.CopyWithNewOpacity(bSelected ? 0.82f : 0.40f)));
		}

		if (IconSizeBox)
		{
			IconSizeBox->SetWidthOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize);
			IconSizeBox->SetHeightOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize);
		}

		if (IconBackgroundBorder)
		{
			const FSlateRoundedBoxBrush IconBrush(
				ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPlateFillTint(),
				10.0f,
				FSlateColor(AccentTint.CopyWithNewOpacity(bSelected ? 0.74f : 0.32f)),
				1.0f,
				FVector2f(
					ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize,
					ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconSize));
			IconBackgroundBorder->SetBrush(IconBrush);
		}
	}

	if (IconImage)
	{
		IconImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(
				CurrentDisplayData.IconTexture.IsNull() ? DefaultIconTexture : CurrentDisplayData.IconTexture,
				ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultIconTexturePath)),
			false);
		if (bUsingNativeFallbackTree)
		{
			IconImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				IconImage,
				AccentTint.CopyWithNewOpacity(bSelected ? 0.96f : 0.72f)));
		}
	}

	if (NameText)
	{
		NameText->SetText(FText::FromString(CurrentDisplayData.DisplayLabel));
		if (bUsingNativeFallbackTree)
		{
			NameText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::NameTint()));
		}
	}

	if (LevelText)
	{
		LevelText->SetText(FText::Format(
			FText::FromString(TEXT("Lv {0}")),
			FText::AsNumber(CurrentDisplayData.Level)));
		if (bUsingNativeFallbackTree)
		{
			LevelText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MetaTint()));
		}
	}

	if (SeparatorText && bUsingNativeFallbackTree)
	{
		SeparatorText->SetColorAndOpacity(FSlateColor(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MetaTint().CopyWithNewOpacity(0.72f)));
	}

	if (CostText)
	{
		CostText->SetText(FText::Format(
			FText::FromString(TEXT("Cost {0}")),
			FText::AsNumber(CurrentDisplayData.Cost)));
		if (bUsingNativeFallbackTree)
		{
			CostText->SetColorAndOpacity(FSlateColor(
				(CurrentDisplayData.bAffordable || bSelected)
					? AccentTint.CopyWithNewOpacity(0.98f)
					: ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::WarningTint()));
		}
	}

	if (SelectionGlyphImage)
	{
		if (bUsingNativeFallbackTree)
		{
			SelectionGlyphImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				SelectionGlyphImage,
				AccentTint.CopyWithNewOpacity(bSelected ? 0.98f : 0.22f)));
		}
		SelectionGlyphImage->SetVisibility(bSelected ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	OnExchangeAttributeRowDataApplied(CurrentDisplayData);
	OnExchangeAttributeRowVisualStateChanged(CurrentDisplayData.AttributeName, CurrentDisplayData.bSelected, CurrentDisplayData.bAffordable);
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeAttributeRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(
		TEXT("Willpower"),
		TEXT("Willpower"),
		1,
		204,
		EFProjectUIPalette::AttributeWillpower(),
		ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Willpower")),
		true);
}

UTexture2D* UProjectInnerDoctrineExchangeAttributeRowWidget::ResolveTexture(
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

	return Cast<UTexture2D>(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerDoctrineExchangeAttributeRowWidget::ResolveStyleAsset(
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

	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerDoctrineExchangeAttributeRowWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerDoctrineExchangeAttributeRowWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::BodyFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeAttributeRowGlobalWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(
		TEXT("Global"),
		TEXT("Attribute"),
		2,
		350,
		EFProjectUIPalette::AccentSoft(),
		ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::DefaultIconTexturePath,
		true);
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeWillpowerRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Willpower"), TEXT("Willpower"), 1, 204, EFProjectUIPalette::AttributeWillpower(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Willpower")), true);
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeOffensiveRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Offensive"), TEXT("Offensive"), 0, 80, EFProjectUIPalette::AttributeOffensive(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Offensive")));
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeDefensiveRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Defensive"), TEXT("Defensive"), 0, 80, EFProjectUIPalette::AttributeDefensive(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Defensive")));
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeFaithRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Faith"), TEXT("Faith"), 0, 80, EFProjectUIPalette::AttributeFaith(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Faith")));
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeCunningRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Cunning"), TEXT("Cunning"), 0, 80, EFProjectUIPalette::AttributeCunning(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Cunning")));
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeCelerityRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Celerity"), TEXT("Celerity"), 0, 80, EFProjectUIPalette::AttributeCelerity(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Celerity")));
}

FProjectInnerDoctrineExchangeAttributeRowDisplayData UProjectInnerDoctrineExchangeCharismaRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Charisma"), TEXT("Charisma"), 0, 80, EFProjectUIPalette::AttributeCharisma(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Charisma")));
}

UProjectInnerDoctrineExchangeRowsGlobalWidget::UProjectInnerDoctrineExchangeRowsGlobalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (!RowsScrollBox)
	{
		return;
	}

	// The altar's converted RowsScrollBox carried a cooked purple scrollbar
	// track. Rebuild only that Slate style as neutral theme-owned brushes;
	// scroll behavior, thickness and layout remain untouched.
	FScrollBarStyle Style = RowsScrollBox->GetWidgetBarStyle();
	auto SetSolidBrush = [](FSlateBrush& Brush, const FLinearColor& Color)
	{
		const float Alpha = FMath::Max(Brush.TintColor.GetSpecifiedColor().A, 0.01f);
		Brush.SetResourceObject(nullptr);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(FLinearColor(Color.R, Color.G, Color.B, Alpha));
	};

	SetSolidBrush(Style.VerticalBackgroundImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.VerticalTopSlotImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.VerticalBottomSlotImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.HorizontalBackgroundImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.HorizontalTopSlotImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.HorizontalBottomSlotImage, Theme.PanelFillDeep);
	SetSolidBrush(Style.NormalThumbImage, Theme.OutlineDim);
	SetSolidBrush(Style.HoveredThumbImage, Theme.Outline);
	SetSolidBrush(Style.DraggedThumbImage, Theme.AccentSoft);
	RowsScrollBox->SetWidgetBarStyle(Style);

	// Also clear authored scroll-edge shadow resources. Converted altar WBP
	// instances can retain a purple shadow texture even when the scrollbar
	// brushes themselves have been replaced.
	FScrollBoxStyle ScrollStyle = RowsScrollBox->GetWidgetStyle();
	ScrollStyle.TopShadowBrush = FSlateNoResource();
	ScrollStyle.BottomShadowBrush = FSlateNoResource();
	RowsScrollBox->SetWidgetStyle(ScrollStyle);

	// The converted altar RowsGlobal WBP also owns a full-size background Border
	// behind the scroll box. Its authored brush was the last persistent source
	// of the dark purple band visible between rows. Rebuild every border in this
	// widget tree from the active theme; row widgets have their own trees and
	// therefore keep their selected/attribute-specific styling callbacks.
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([&Theme](UWidget* Widget)
		{
			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				Border->SetBrush(FSlateRoundedBoxBrush(
					Theme.PanelFillDeep,
					6.0f,
					FSlateColor(Theme.OutlineDim),
					1.0f,
					FVector2f(560.0f, 690.0f)));
				Border->SetBrushColor(FLinearColor::White);
			}
		});
	}
}

bool UProjectInnerDoctrineExchangeRowsGlobalWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		TArray<FProjectInnerDoctrineExchangeAttributeRowDisplayData> PreviewRows;
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Willpower"), TEXT("Willpower"), 1, 204, EFProjectUIPalette::AttributeWillpower(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Willpower")), true));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Offensive"), TEXT("Offensive"), 0, 80, EFProjectUIPalette::AttributeOffensive(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Offensive"))));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Defensive"), TEXT("Defensive"), 0, 80, EFProjectUIPalette::AttributeDefensive(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Defensive"))));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Faith"), TEXT("Faith"), 0, 80, EFProjectUIPalette::AttributeFaith(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Faith"))));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Cunning"), TEXT("Cunning"), 0, 80, EFProjectUIPalette::AttributeCunning(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Cunning"))));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Celerity"), TEXT("Celerity"), 0, 80, EFProjectUIPalette::AttributeCelerity(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Celerity"))));
		PreviewRows.Add(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::MakePreviewData(TEXT("Charisma"), TEXT("Charisma"), 0, 80, EFProjectUIPalette::AttributeCharisma(), ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::IconPathForAttribute(TEXT("Charisma"))));
		ApplyRows(PreviewRows, UProjectInnerDoctrineExchangeAttributeRowWidget::StaticClass());
	}
	return bBuiltTree;
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox && WidgetTree->RootWidget == RootSizeBox)
	{
		return;
	}

	if (WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerDoctrineExchangeRowsGlobalWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowsGlobalWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerDoctrineExchangeAttributeRowWidgetPrivate::RowsGlobalHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	DesignerRootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DesignerRootOverlay"));
	RootSizeBox->AddChild(DesignerRootOverlay);

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	if (UOverlaySlot* RootOverlaySlot = DesignerRootOverlay->AddChildToOverlay(RootOverlay))
	{
		RootOverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		RootOverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}

	RowsScrollBox = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RowsScrollBox"));
	RowsScrollBox->SetAnimateWheelScrolling(false);
	RowsScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	if (UOverlaySlot* ScrollSlot = RootOverlay->AddChildToOverlay(RowsScrollBox))
	{
		ScrollSlot->SetHorizontalAlignment(HAlign_Fill);
		ScrollSlot->SetVerticalAlignment(VAlign_Fill);
	}

	RowsLayout = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowsLayout"));
	RowsScrollBox->AddChild(RowsLayout);

	const TArray<TPair<FName, UClass*>> FixedRows = {
		{ FName(TEXT("Willpower")), UProjectInnerDoctrineExchangeWillpowerRowWidget::StaticClass() },
		{ FName(TEXT("Offensive")), UProjectInnerDoctrineExchangeOffensiveRowWidget::StaticClass() },
		{ FName(TEXT("Defensive")), UProjectInnerDoctrineExchangeDefensiveRowWidget::StaticClass() },
		{ FName(TEXT("Faith")), UProjectInnerDoctrineExchangeFaithRowWidget::StaticClass() },
		{ FName(TEXT("Cunning")), UProjectInnerDoctrineExchangeCunningRowWidget::StaticClass() },
		{ FName(TEXT("Celerity")), UProjectInnerDoctrineExchangeCelerityRowWidget::StaticClass() },
		{ FName(TEXT("Charisma")), UProjectInnerDoctrineExchangeCharismaRowWidget::StaticClass() },
	};

	for (int32 Index = 0; Index < FixedRows.Num(); ++Index)
	{
		const TPair<FName, UClass*>& FixedRow = FixedRows[Index];
		UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget =
			TargetWidgetTree->ConstructWidget<UProjectInnerDoctrineExchangeAttributeRowWidget>(
				FixedRow.Value,
				*FString::Printf(TEXT("%sRow"), *FixedRow.Key.ToString()));
		if (!RowWidget)
		{
			continue;
		}

		if (UVerticalBoxSlot* RowSlot = RowsLayout->AddChildToVerticalBox(RowWidget))
		{
			RowSlot->SetPadding(FMargin(0.0f, Index == 0 ? 0.0f : 10.0f, 0.0f, 0.0f));
		}

		if (FixedRow.Key == FName(TEXT("Willpower")))
		{
			WillpowerRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Offensive")))
		{
			OffensiveRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Defensive")))
		{
			DefensiveRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Faith")))
		{
			FaithRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Cunning")))
		{
			CunningRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Celerity")))
		{
			CelerityRow = RowWidget;
		}
		else if (FixedRow.Key == FName(TEXT("Charisma")))
		{
			CharismaRow = RowWidget;
		}
	}

	ExtraRowsLayout = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExtraRowsLayout"));
	if (UVerticalBoxSlot* ExtraSlot = RowsLayout->AddChildToVerticalBox(ExtraRowsLayout))
	{
		ExtraSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	}

	return true;
}

int32 UProjectInnerDoctrineExchangeRowsGlobalWidget::ApplyRows(
	const TArray<FProjectInnerDoctrineExchangeAttributeRowDisplayData>& InRowData,
	TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> FallbackRowWidgetClass)
{
	BuildWidgetTree();
	RuntimeRowsByName.Empty();
	ClearFallbackRows();

	TSet<FName> VisibleFixedRowNames;
	int32 SelectedIndex = INDEX_NONE;
	VisibleRowCount = 0;

	for (int32 Index = 0; Index < InRowData.Num(); ++Index)
	{
		const FProjectInnerDoctrineExchangeAttributeRowDisplayData& RowData = InRowData[Index];
		UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget = GetFixedRowForAttribute(RowData.AttributeName);
		if (RowWidget)
		{
			VisibleFixedRowNames.Add(RowData.AttributeName);
		}
		else
		{
			RowWidget = GetOrCreateFallbackRow(RowData.AttributeName, FallbackRowWidgetClass);
		}

		if (!RowWidget)
		{
			continue;
		}

		RowWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		RowWidget->ApplyDisplayData(RowData);
		RuntimeRowsByName.Add(RowData.AttributeName, RowWidget);
		++VisibleRowCount;

		if (RowData.bSelected)
		{
			SelectedIndex = Index;
		}
	}

	HideUnusedFixedRows(VisibleFixedRowNames);
	OnExchangeRowsApplied(VisibleRowCount, SelectedIndex);
	return VisibleRowCount;
}

UProjectInnerDoctrineExchangeAttributeRowWidget* UProjectInnerDoctrineExchangeRowsGlobalWidget::FindRowWidgetByAttribute(
	const FName AttributeName) const
{
	if (const TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>* FoundWidget = RuntimeRowsByName.Find(AttributeName))
	{
		return FoundWidget->Get();
	}
	return GetFixedRowForAttribute(AttributeName);
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::ScrollRowWidgetIntoView(UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget)
{
	if (RowsScrollBox && RowWidget)
	{
		RowsScrollBox->ScrollWidgetIntoView(RowWidget, true, EDescendantScrollDestination::Center, 24.0f);
	}
}

UProjectInnerDoctrineExchangeAttributeRowWidget* UProjectInnerDoctrineExchangeRowsGlobalWidget::GetFixedRowForAttribute(
	const FName AttributeName) const
{
	if (AttributeName == FName(TEXT("Willpower")))
	{
		return WillpowerRow.Get();
	}
	if (AttributeName == FName(TEXT("Offensive")))
	{
		return OffensiveRow.Get();
	}
	if (AttributeName == FName(TEXT("Defensive")))
	{
		return DefensiveRow.Get();
	}
	if (AttributeName == FName(TEXT("Faith")))
	{
		return FaithRow.Get();
	}
	if (AttributeName == FName(TEXT("Cunning")))
	{
		return CunningRow.Get();
	}
	if (AttributeName == FName(TEXT("Celerity")))
	{
		return CelerityRow.Get();
	}
	if (AttributeName == FName(TEXT("Charisma")))
	{
		return CharismaRow.Get();
	}
	return nullptr;
}

UProjectInnerDoctrineExchangeAttributeRowWidget* UProjectInnerDoctrineExchangeRowsGlobalWidget::GetOrCreateFallbackRow(
	const FName AttributeName,
	TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> FallbackRowWidgetClass)
{
	if (!ExtraRowsLayout || !FallbackRowWidgetClass)
	{
		return nullptr;
	}

	if (TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>* ExistingRow = FallbackRowsByName.Find(AttributeName))
	{
		return ExistingRow->Get();
	}

	UProjectInnerDoctrineExchangeAttributeRowWidget* FallbackRow =
		CreateWidget<UProjectInnerDoctrineExchangeAttributeRowWidget>(this, FallbackRowWidgetClass.Get());
	if (!FallbackRow)
	{
		return nullptr;
	}

	if (UVerticalBoxSlot* RowSlot = ExtraRowsLayout->AddChildToVerticalBox(FallbackRow))
	{
		RowSlot->SetPadding(FMargin(0.0f, FallbackRowsByName.IsEmpty() ? 0.0f : 10.0f, 0.0f, 0.0f));
	}

	FallbackRowsByName.Add(AttributeName, FallbackRow);
	return FallbackRow;
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::HideUnusedFixedRows(const TSet<FName>& VisibleFixedRowNames)
{
	const auto HideIfUnused = [&VisibleFixedRowNames](const FName AttributeName, UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget)
	{
		if (RowWidget && !VisibleFixedRowNames.Contains(AttributeName))
		{
			RowWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	HideIfUnused(FName(TEXT("Willpower")), WillpowerRow.Get());
	HideIfUnused(FName(TEXT("Offensive")), OffensiveRow.Get());
	HideIfUnused(FName(TEXT("Defensive")), DefensiveRow.Get());
	HideIfUnused(FName(TEXT("Faith")), FaithRow.Get());
	HideIfUnused(FName(TEXT("Cunning")), CunningRow.Get());
	HideIfUnused(FName(TEXT("Celerity")), CelerityRow.Get());
	HideIfUnused(FName(TEXT("Charisma")), CharismaRow.Get());
}

void UProjectInnerDoctrineExchangeRowsGlobalWidget::ClearFallbackRows()
{
	if (ExtraRowsLayout)
	{
		ExtraRowsLayout->ClearChildren();
	}
	FallbackRowsByName.Empty();
}
