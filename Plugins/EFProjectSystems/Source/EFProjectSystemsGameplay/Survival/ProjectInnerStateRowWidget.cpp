#include "Survival/ProjectInnerStateRowWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace ProjectInnerStateRowWidgetPrivate
{
	constexpr float RowWidth = 400.0f;
	constexpr float RowHeight = 30.0f;
	constexpr float MedallionSize = 24.0f;
	constexpr float LabelWidth = 78.0f;
	constexpr float ValueWidth = 38.0f;
	constexpr float GlyphWidth = 16.0f;
	constexpr float PipWidth = 16.0f;
	constexpr float PipHeight = 13.0f;
	constexpr float TotalPips = 10.0f;
	constexpr float RowsGlobalWidth = 400.0f;
	constexpr float RowsGlobalHeight = 190.0f;

	FLinearColor EmptyPipTint()
	{
		return EFProjectUIPalette::OutlineDim(0.42f);
	}
	FLinearColor FrameDimTint()
	{
		return EFProjectUIPalette::OutlineDim(0.32f);
	}
	const FLinearColor MonogramShadow(0.0f, 0.0f, 0.0f, 0.74f);
	const FLinearColor LabelShadow(0.0f, 0.0f, 0.0f, 0.55f);

	FLinearColor ResolveLiveAccentTint(const FProjectInnerStateRowDisplayData& DisplayData)
	{
		const float Alpha = FMath::IsFinite(DisplayData.AccentTint.A)
			? FMath::Clamp(DisplayData.AccentTint.A, 0.0f, 1.0f)
			: 1.0f;
		return EFProjectUIPalette::InnerStateForName(DisplayData.EntryName, Alpha);
	}

	const TCHAR* CinzelFontPath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Fonts/F_InnerState_Cinzel.F_InnerState_Cinzel");
	const TCHAR* CormorantFontPath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Fonts/F_InnerState_Cormorant.F_InnerState_Cormorant");
	const TCHAR* RowFrameTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_RowFrame.T_InnerState_RowFrame");
	const TCHAR* MedallionTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Medallion.T_InnerState_Medallion");
	const TCHAR* PipFilledTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_PipFilled.T_InnerState_PipFilled");
	const TCHAR* PipEmptyTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_PipEmpty.T_InnerState_PipEmpty");
	const TCHAR* SparkTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Spark.T_InnerState_Spark");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FProjectInnerStateRowDisplayData MakePreviewData(
		const FName EntryName,
		const FString& DisplayLabel,
		const FString& Monogram,
		const int32 PercentValue,
		const int32 FilledBars,
		const bool bIsSensation,
		const FLinearColor& AccentTint)
	{
		FProjectInnerStateRowDisplayData Data;
		Data.EntryName = EntryName;
		Data.DisplayLabel = DisplayLabel;
		Data.Monogram = Monogram;
		Data.AccentTint = AccentTint;
		Data.CurrentValue = PercentValue;
		Data.MaxValue = 100.0f;
		Data.NormalizedValue = FMath::Clamp(static_cast<float>(PercentValue) / 100.0f, 0.0f, 1.0f);
		Data.FilledBars = FMath::Clamp(FilledBars, 0, static_cast<int32>(TotalPips));
		Data.TotalBars = static_cast<int32>(TotalPips);
		Data.PercentValue = FMath::Clamp(PercentValue, 0, 100);
		Data.bIsSensation = bIsSensation;
		return Data;
	}

	void AddVerticalRow(UVerticalBox* RowsBox, UProjectInnerStateRowWidget* RowWidget)
	{
		if (!RowsBox || !RowWidget)
		{
			return;
		}

		if (UVerticalBoxSlot* RowSlot = RowsBox->AddChildToVerticalBox(RowWidget))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetVerticalAlignment(VAlign_Top);
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		}
	}
}

UProjectInnerStateRowWidget::UProjectInnerStateRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::CinzelFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::CormorantFontPath);
	RowFrameTexture = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::RowFrameTexturePath);
	MedallionTexture = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::MedallionTexturePath);
	PipFilledTexture = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::PipFilledTexturePath);
	PipEmptyTexture = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::PipEmptyTexturePath);
	SparkTexture = FSoftObjectPath(ProjectInnerStateRowWidgetPrivate::SparkTexturePath);
}

void UProjectInnerStateRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshPips();
}

void UProjectInnerStateRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyDisplayData(CurrentDisplayData);
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectInnerStateRowWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	CurrentDisplayData = MakeDesignerPreviewData();
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		ApplyDisplayData(CurrentDisplayData);
	}

	return bBuiltTree;
}

void UProjectInnerStateRowWidget::ApplyDisplayData(const FProjectInnerStateRowDisplayData& InDisplayData)
{
	CurrentDisplayData = InDisplayData;
	// Display data may be cached by a global rows widget or supplied by a
	// Blueprint. Treat its RGB as authored metadata and resolve the entry's
	// live semantic color on every refresh, preserving authored opacity.
	CurrentDisplayData.AccentTint =
		ProjectInnerStateRowWidgetPrivate::ResolveLiveAccentTint(CurrentDisplayData);
	BuildWidgetTree();
	InitializeVisualTree();

	const FLinearColor AccentTint = CurrentDisplayData.AccentTint.GetClamped(0.0f, 1.0f);
	const FLinearColor AccentGlow = AccentTint.CopyWithNewOpacity(0.88f);

	if (bUsingNativeFallbackTree && RowFrameImage)
	{
		const FLinearColor FrameTint = FLinearColor(
			FMath::Max(ProjectInnerStateRowWidgetPrivate::FrameDimTint().R, AccentTint.R),
			FMath::Max(ProjectInnerStateRowWidgetPrivate::FrameDimTint().G, AccentTint.G * 0.7f),
			FMath::Max(ProjectInnerStateRowWidgetPrivate::FrameDimTint().B, AccentTint.B),
			0.44f);
		RowFrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			RowFrameImage,
			FrameTint));
	}

	if (bUsingNativeFallbackTree && MedallionImage)
	{
		MedallionImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			MedallionImage,
			AccentGlow));
	}

	if (MonogramText)
	{
		MonogramText->SetText(FText::FromString(CurrentDisplayData.Monogram));
		if (bUsingNativeFallbackTree)
		{
			MonogramText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
		}
	}

	if (LabelText)
	{
		LabelText->SetText(FText::FromString(CurrentDisplayData.DisplayLabel));
		if (bUsingNativeFallbackTree)
		{
			LabelText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
		}
	}

	if (ValueText)
	{
		ValueText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), CurrentDisplayData.PercentValue)));
		if (bUsingNativeFallbackTree)
		{
			ValueText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
		}
	}

	if (bUsingNativeFallbackTree && GlyphImage)
	{
		GlyphImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			GlyphImage,
			AccentTint.CopyWithNewOpacity(0.78f)));
	}

	RefreshPips();
	OnInnerStateRowDataApplied(CurrentDisplayData);
}

void UProjectInnerStateRowWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (CurrentDisplayData.EntryName.IsNone())
	{
		return;
	}

	CurrentDisplayData.AccentTint =
		ProjectInnerStateRowWidgetPrivate::ResolveLiveAccentTint(CurrentDisplayData);
	ApplyDisplayData(CurrentDisplayData);
}

void UProjectInnerStateRowWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = bUsingNativeFallbackTree && RootSizeBox != nullptr;
		RebuildPipImageCache();
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerStateRowWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	UOverlay* DesignerRootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DesignerRootOverlay"));
	TargetWidgetTree->RootWidget = DesignerRootOverlay;

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::RowWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerStateRowWidgetPrivate::RowHeight);
	if (UOverlaySlot* RootSlot = DesignerRootOverlay->AddChildToOverlay(RootSizeBox))
	{
		RootSlot->SetHorizontalAlignment(HAlign_Center);
		RootSlot->SetVerticalAlignment(VAlign_Center);
	}

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->AddChild(RootOverlay);

	RowFrameImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("RowFrameImage"));
	if (UOverlaySlot* FrameSlot = RootOverlay->AddChildToOverlay(RowFrameImage))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(0.0f));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = RootOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
		ContentSlot->SetPadding(FMargin(7.0f, 3.0f, 7.0f, 3.0f));
	}

	ContentBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	MedallionSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MedallionSizeBox"));
	MedallionSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::MedallionSize);
	MedallionSizeBox->SetHeightOverride(ProjectInnerStateRowWidgetPrivate::MedallionSize);
	if (UHorizontalBoxSlot* MedallionSlot = ContentBox->AddChildToHorizontalBox(MedallionSizeBox))
	{
		MedallionSlot->SetHorizontalAlignment(HAlign_Left);
		MedallionSlot->SetVerticalAlignment(VAlign_Center);
		MedallionSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	MedallionOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MedallionOverlay"));
	MedallionSizeBox->AddChild(MedallionOverlay);

	MedallionImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MedallionImage"));
	if (UOverlaySlot* MedallionImageSlot = MedallionOverlay->AddChildToOverlay(MedallionImage))
	{
		MedallionImageSlot->SetHorizontalAlignment(HAlign_Fill);
		MedallionImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	MonogramText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MonogramText"));
	if (UOverlaySlot* MonogramSlot = MedallionOverlay->AddChildToOverlay(MonogramText))
	{
		MonogramSlot->SetHorizontalAlignment(HAlign_Center);
		MonogramSlot->SetVerticalAlignment(VAlign_Center);
	}

	LabelSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LabelSizeBox"));
	LabelSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::LabelWidth);
	if (UHorizontalBoxSlot* LabelSlot = ContentBox->AddChildToHorizontalBox(LabelSizeBox))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Left);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	LabelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LabelText"));
	LabelSizeBox->AddChild(LabelText);

	PipsBox = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PipsBox"));
	if (UHorizontalBoxSlot* PipsSlot = ContentBox->AddChildToHorizontalBox(PipsBox))
	{
		PipsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PipsSlot->SetHorizontalAlignment(HAlign_Fill);
		PipsSlot->SetVerticalAlignment(VAlign_Center);
		PipsSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	PipImages.Reset();
	FilledPipImages.Reset();
	EmptyPipImages.Reset();
	for (int32 PipIndex = 0; PipIndex < static_cast<int32>(ProjectInnerStateRowWidgetPrivate::TotalPips); ++PipIndex)
	{
		USizeBox* PipSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			*FString::Printf(TEXT("PipSizeBox_%d"), PipIndex));
		PipSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::PipWidth);
		PipSizeBox->SetHeightOverride(ProjectInnerStateRowWidgetPrivate::PipHeight);
		if (UHorizontalBoxSlot* PipSlot = PipsBox->AddChildToHorizontalBox(PipSizeBox))
		{
			PipSlot->SetHorizontalAlignment(HAlign_Left);
			PipSlot->SetVerticalAlignment(VAlign_Center);
			PipSlot->SetPadding(FMargin(PipIndex == 0 ? 0.0f : 3.0f, 0.0f, 0.0f, 0.0f));
		}

		UOverlay* PipOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(),
			*FString::Printf(TEXT("PipOverlay_%d"), PipIndex));
		PipSizeBox->AddChild(PipOverlay);

		UImage* EmptyPipImage = TargetWidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(),
			*FString::Printf(TEXT("PipEmptyImage_%d"), PipIndex));
		if (UOverlaySlot* EmptyPipSlot = PipOverlay->AddChildToOverlay(EmptyPipImage))
		{
			EmptyPipSlot->SetHorizontalAlignment(HAlign_Fill);
			EmptyPipSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UImage* FilledPipImage = TargetWidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(),
			*FString::Printf(TEXT("PipFilledImage_%d"), PipIndex));
		if (UOverlaySlot* FilledPipSlot = PipOverlay->AddChildToOverlay(FilledPipImage))
		{
			FilledPipSlot->SetHorizontalAlignment(HAlign_Fill);
			FilledPipSlot->SetVerticalAlignment(VAlign_Fill);
		}

		PipImages.Add(FilledPipImage);
		FilledPipImages.Add(FilledPipImage);
		EmptyPipImages.Add(EmptyPipImage);
	}

	ValueSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ValueSizeBox"));
	ValueSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::ValueWidth);
	if (UHorizontalBoxSlot* ValueSlot = ContentBox->AddChildToHorizontalBox(ValueSizeBox))
	{
		ValueSlot->SetHorizontalAlignment(HAlign_Right);
		ValueSlot->SetVerticalAlignment(VAlign_Center);
		ValueSlot->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
	}

	ValueText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ValueText"));
	ValueSizeBox->AddChild(ValueText);

	GlyphSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GlyphSizeBox"));
	GlyphSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::GlyphWidth);
	GlyphSizeBox->SetHeightOverride(ProjectInnerStateRowWidgetPrivate::GlyphWidth);
	if (UHorizontalBoxSlot* GlyphSlot = ContentBox->AddChildToHorizontalBox(GlyphSizeBox))
	{
		GlyphSlot->SetHorizontalAlignment(HAlign_Center);
		GlyphSlot->SetVerticalAlignment(VAlign_Center);
	}

	GlyphImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("GlyphImage"));
	GlyphSizeBox->AddChild(GlyphImage);
	return true;
}

void UProjectInnerStateRowWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	RebuildPipImageCache();
	if (!bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = true;
		return;
	}

	if (RowFrameImage)
	{
		RowFrameImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(RowFrameTexture, ProjectInnerStateRowWidgetPrivate::RowFrameTexturePath)),
			false);
		RowFrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			RowFrameImage,
			ProjectInnerStateRowWidgetPrivate::FrameDimTint()));
	}

	if (MedallionImage)
	{
		MedallionImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(MedallionTexture, ProjectInnerStateRowWidgetPrivate::MedallionTexturePath)),
			false);
	}

	if (MonogramText)
	{
		MonogramText->SetFont(MakeTitleFont(13));
		MonogramText->SetJustification(ETextJustify::Center);
		MonogramText->SetShadowOffset(FVector2D(0.0f, 2.0f));
		MonogramText->SetShadowColorAndOpacity(ProjectInnerStateRowWidgetPrivate::MonogramShadow);
	}

	if (LabelText)
	{
		LabelText->SetFont(MakeBodyFont(13, 20));
		LabelText->SetAutoWrapText(false);
		LabelText->SetJustification(ETextJustify::Left);
		LabelText->SetShadowOffset(FVector2D(0.0f, 2.0f));
		LabelText->SetShadowColorAndOpacity(ProjectInnerStateRowWidgetPrivate::LabelShadow);
	}

	if (ValueText)
	{
		ValueText->SetFont(MakeBodyFont(13, 0));
		ValueText->SetJustification(ETextJustify::Right);
		ValueText->SetShadowOffset(FVector2D(0.0f, 2.0f));
		ValueText->SetShadowColorAndOpacity(ProjectInnerStateRowWidgetPrivate::LabelShadow);
	}

	if (GlyphImage)
	{
		GlyphImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(SparkTexture, ProjectInnerStateRowWidgetPrivate::SparkTexturePath)),
			false);
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerStateRowWidget::RefreshPips()
{
	const UTexture2D* FilledTextureAsset = ResolveTexture(PipFilledTexture, ProjectInnerStateRowWidgetPrivate::PipFilledTexturePath);
	const UTexture2D* EmptyTextureAsset = ResolveTexture(PipEmptyTexture, ProjectInnerStateRowWidgetPrivate::PipEmptyTexturePath);
	const FLinearColor AccentTint = CurrentDisplayData.AccentTint.GetClamped(0.0f, 1.0f);
	const int32 TotalBars = FMath::Max(1, CurrentDisplayData.TotalBars);
	const int32 FilledBars = FMath::Clamp(CurrentDisplayData.FilledBars, 0, TotalBars);

	RebuildPipImageCache();

	const int32 PairCount = FMath::Max(FilledPipImages.Num(), EmptyPipImages.Num());
	for (int32 PipIndex = 0; PipIndex < PairCount; ++PipIndex)
	{
		const bool bFilled = PipIndex < FilledBars;

		if (FilledPipImages.IsValidIndex(PipIndex))
		{
			if (UImage* FilledPipImage = FilledPipImages[PipIndex])
			{
				FilledPipImage->SetVisibility(bFilled ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
				if (bUsingNativeFallbackTree)
				{
					FilledPipImage->SetBrushFromTexture(ResolveProjectThemeTexture(const_cast<UTexture2D*>(FilledTextureAsset)), false);
					FilledPipImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
						FilledPipImage,
						AccentTint));
				}
			}
		}

		if (EmptyPipImages.IsValidIndex(PipIndex))
		{
			if (UImage* EmptyPipImage = EmptyPipImages[PipIndex])
			{
				EmptyPipImage->SetVisibility(bFilled ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible);
				if (bUsingNativeFallbackTree)
				{
					EmptyPipImage->SetBrushFromTexture(ResolveProjectThemeTexture(const_cast<UTexture2D*>(EmptyTextureAsset)), false);
					EmptyPipImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
						EmptyPipImage,
						ProjectInnerStateRowWidgetPrivate::EmptyPipTint()));
				}
			}
		}
	}

	if (PairCount > 0)
	{
		return;
	}

	for (int32 PipIndex = 0; PipIndex < PipImages.Num(); ++PipIndex)
	{
		UImage* PipImage = PipImages[PipIndex];
		if (!PipImage)
		{
			continue;
		}

		const bool bFilled = PipIndex < FilledBars;
		if (bUsingNativeFallbackTree)
		{
			PipImage->SetBrushFromTexture(ResolveProjectThemeTexture(bFilled ? const_cast<UTexture2D*>(FilledTextureAsset) : const_cast<UTexture2D*>(EmptyTextureAsset)), false);
			PipImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				PipImage,
				bFilled ? AccentTint : ProjectInnerStateRowWidgetPrivate::EmptyPipTint()));
		}
		else
		{
			PipImage->SetVisibility(bFilled ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
		}
	}
}

void UProjectInnerStateRowWidget::RebuildPipImageCache()
{
	if (!WidgetTree)
	{
		return;
	}

	FilledPipImages.Reset();
	EmptyPipImages.Reset();
	PipImages.Reset();

	for (int32 PipIndex = 0; PipIndex < static_cast<int32>(ProjectInnerStateRowWidgetPrivate::TotalPips); ++PipIndex)
	{
		if (UImage* FilledPipImage = Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("PipFilledImage_%d"), PipIndex)))))
		{
			FilledPipImages.Add(FilledPipImage);
			PipImages.Add(FilledPipImage);
		}

		if (UImage* EmptyPipImage = Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("PipEmptyImage_%d"), PipIndex)))))
		{
			EmptyPipImages.Add(EmptyPipImage);
		}

		if (!FilledPipImages.IsValidIndex(PipIndex))
		{
			if (UImage* LegacyPipImage = Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("PipImage_%d"), PipIndex)))))
			{
				PipImages.Add(LegacyPipImage);
			}
		}
	}
}

FProjectInnerStateRowDisplayData UProjectInnerStateRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("InnerState"),
		TEXT("INNER STATE"),
		TEXT("I"),
		67,
		7,
		false,
		EFProjectUIPalette::Accent());
}

FProjectInnerStateRowDisplayData UProjectInnerStateRowGlobalWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Global"),
		TEXT("GLOBAL"),
		TEXT("G"),
		72,
		7,
		false,
		EFProjectUIPalette::AccentSoft());
}

FProjectInnerStateRowDisplayData UProjectInnerStateHungerRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Hunger"),
		TEXT("HUNGER"),
		TEXT("H"),
		86,
		9,
		false,
		EFProjectUIPalette::InnerStateHunger());
}

FProjectInnerStateRowDisplayData UProjectInnerStateThirstRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Thirst"),
		TEXT("THIRST"),
		TEXT("T"),
		76,
		8,
		false,
		EFProjectUIPalette::InnerStateThirst());
}

FProjectInnerStateRowDisplayData UProjectInnerStateSleepRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Sleep"),
		TEXT("SLEEP"),
		TEXT("S"),
		68,
		7,
		false,
		EFProjectUIPalette::InnerStateSleep());
}

FProjectInnerStateRowDisplayData UProjectInnerStateMadnessRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Madness"),
		TEXT("MADNESS"),
		TEXT("M"),
		18,
		2,
		true,
		EFProjectUIPalette::InnerStateMadness());
}

FProjectInnerStateRowDisplayData UProjectInnerStateCurseRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Curse"),
		TEXT("CURSE"),
		TEXT("C"),
		42,
		4,
		true,
		EFProjectUIPalette::InnerStateCurse());
}

FProjectInnerStateRowDisplayData UProjectInnerStatePainRowWidget::MakeDesignerPreviewData() const
{
	return ProjectInnerStateRowWidgetPrivate::MakePreviewData(
		TEXT("Pain"),
		TEXT("PAIN"),
		TEXT("P"),
		24,
		2,
		true,
		EFProjectUIPalette::InnerStatePain());
}

UProjectInnerStateRowsGlobalWidget::UProjectInnerStateRowsGlobalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectInnerStateRowsGlobalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UProjectInnerStateRowsGlobalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectInnerStateRowsGlobalWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

void UProjectInnerStateRowsGlobalWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = bUsingNativeFallbackTree && RootSizeBox != nullptr;
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectInnerStateRowsGlobalWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectInnerStateRowWidgetPrivate::RowsGlobalWidth);
	RootSizeBox->SetHeightOverride(ProjectInnerStateRowWidgetPrivate::RowsGlobalHeight);
	TargetWidgetTree->RootWidget = RootSizeBox;

	RowsBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowsBox"));
	RootSizeBox->AddChild(RowsBox);

	const auto AddPreviewRow = [this, TargetWidgetTree](
		UClass* RowClass,
		const TCHAR* WidgetName,
		const FProjectInnerStateRowDisplayData& PreviewData) -> UProjectInnerStateRowWidget*
	{
		if (!RowsBox || !RowClass || !WidgetName)
		{
			return nullptr;
		}

		UProjectInnerStateRowWidget* RowWidget =
			TargetWidgetTree->ConstructWidget<UProjectInnerStateRowWidget>(RowClass, FName(WidgetName));
		if (!RowWidget)
		{
			return nullptr;
		}

		RowWidget->ApplyDisplayData(PreviewData);
		ProjectInnerStateRowWidgetPrivate::AddVerticalRow(RowsBox, RowWidget);
		return RowWidget;
	};

	HungerRow = AddPreviewRow(
		UProjectInnerStateHungerRowWidget::StaticClass(),
		TEXT("HungerRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Hunger"),
			TEXT("HUNGER"),
			TEXT("H"),
			86,
			9,
			false,
			EFProjectUIPalette::InnerStateHunger()));

	ThirstRow = AddPreviewRow(
		UProjectInnerStateThirstRowWidget::StaticClass(),
		TEXT("ThirstRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Thirst"),
			TEXT("THIRST"),
			TEXT("T"),
			76,
			8,
			false,
			EFProjectUIPalette::InnerStateThirst()));

	SleepRow = AddPreviewRow(
		UProjectInnerStateSleepRowWidget::StaticClass(),
		TEXT("SleepRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Sleep"),
			TEXT("SLEEP"),
			TEXT("S"),
			68,
			7,
			false,
			EFProjectUIPalette::InnerStateSleep()));

	MadnessRow = AddPreviewRow(
		UProjectInnerStateMadnessRowWidget::StaticClass(),
		TEXT("MadnessRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Madness"),
			TEXT("MADNESS"),
			TEXT("M"),
			18,
			2,
			true,
			EFProjectUIPalette::InnerStateMadness()));

	CurseRow = AddPreviewRow(
		UProjectInnerStateCurseRowWidget::StaticClass(),
		TEXT("CurseRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Curse"),
			TEXT("CURSE"),
			TEXT("C"),
			42,
			4,
			true,
			EFProjectUIPalette::InnerStateCurse()));

	PainRow = AddPreviewRow(
		UProjectInnerStatePainRowWidget::StaticClass(),
		TEXT("PainRow"),
		ProjectInnerStateRowWidgetPrivate::MakePreviewData(
			TEXT("Pain"),
			TEXT("PAIN"),
			TEXT("P"),
			24,
			2,
			true,
			EFProjectUIPalette::InnerStatePain()));

	ExtraRowsBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExtraRowsBox"));
	ExtraRowsBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ExtraRowsSlot = RowsBox->AddChildToVerticalBox(ExtraRowsBox))
	{
		ExtraRowsSlot->SetHorizontalAlignment(HAlign_Fill);
		ExtraRowsSlot->SetVerticalAlignment(VAlign_Top);
	}

	return true;
}

int32 UProjectInnerStateRowsGlobalWidget::ApplyRows(
	const TArray<FProjectInnerStateRowDisplayData>& InRowData,
	TSubclassOf<UProjectInnerStateRowWidget> FallbackRowWidgetClass)
{
	BuildWidgetTree();

	RuntimeRowsByName.Empty();
	TSet<FName> VisibleFixedRowNames;
	VisibleRowCount = 0;
	ClearFallbackRows();

	for (const FProjectInnerStateRowDisplayData& RowData : InRowData)
	{
		UProjectInnerStateRowWidget* RowWidget = GetFixedRowForEntry(RowData.EntryName);
		if (RowWidget)
		{
			VisibleFixedRowNames.Add(RowData.EntryName);
		}
		else
		{
			RowWidget = GetOrCreateFallbackRow(RowData.EntryName, FallbackRowWidgetClass);
		}

		if (!RowWidget)
		{
			continue;
		}

		RowWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		RowWidget->ApplyDisplayData(RowData);
		RuntimeRowsByName.Add(RowData.EntryName, RowWidget);
		++VisibleRowCount;
	}

	HideUnusedFixedRows(VisibleFixedRowNames);
	if (ExtraRowsBox)
	{
		ExtraRowsBox->SetVisibility(FallbackRowsByName.IsEmpty()
			? ESlateVisibility::Collapsed
			: ESlateVisibility::SelfHitTestInvisible);
	}

	OnInnerStateRowsGlobalApplied(VisibleRowCount);
	return VisibleRowCount;
}

UProjectInnerStateRowWidget* UProjectInnerStateRowsGlobalWidget::FindRowWidgetByEntry(const FName EntryName) const
{
	if (const TObjectPtr<UProjectInnerStateRowWidget>* FoundWidget = RuntimeRowsByName.Find(EntryName))
	{
		return FoundWidget->Get();
	}

	return GetFixedRowForEntry(EntryName);
}

UProjectInnerStateRowWidget* UProjectInnerStateRowsGlobalWidget::GetFixedRowForEntry(const FName EntryName) const
{
	if (EntryName == FName(TEXT("Hunger")))
	{
		return HungerRow.Get();
	}
	if (EntryName == FName(TEXT("Thirst")))
	{
		return ThirstRow.Get();
	}
	if (EntryName == FName(TEXT("Sleep")))
	{
		return SleepRow.Get();
	}
	if (EntryName == FName(TEXT("Madness")))
	{
		return MadnessRow.Get();
	}
	if (EntryName == FName(TEXT("Curse")))
	{
		return CurseRow.Get();
	}
	if (EntryName == FName(TEXT("Pain")))
	{
		return PainRow.Get();
	}

	return nullptr;
}

UProjectInnerStateRowWidget* UProjectInnerStateRowsGlobalWidget::GetOrCreateFallbackRow(
	const FName EntryName,
	TSubclassOf<UProjectInnerStateRowWidget> FallbackRowWidgetClass)
{
	if (!ExtraRowsBox || !FallbackRowWidgetClass)
	{
		return nullptr;
	}

	if (TObjectPtr<UProjectInnerStateRowWidget>* ExistingRow = FallbackRowsByName.Find(EntryName))
	{
		return ExistingRow->Get();
	}

	UProjectInnerStateRowWidget* FallbackRow = CreateWidget<UProjectInnerStateRowWidget>(
		this,
		FallbackRowWidgetClass.Get(),
		*FString::Printf(TEXT("FallbackInnerStateRow_%s"), *EntryName.ToString()));
	if (!FallbackRow)
	{
		return nullptr;
	}

	ProjectInnerStateRowWidgetPrivate::AddVerticalRow(ExtraRowsBox, FallbackRow);
	FallbackRowsByName.Add(EntryName, FallbackRow);
	return FallbackRow;
}

void UProjectInnerStateRowsGlobalWidget::HideUnusedFixedRows(const TSet<FName>& VisibleFixedRowNames)
{
	const auto HideIfUnused = [&VisibleFixedRowNames](const FName EntryName, UProjectInnerStateRowWidget* RowWidget)
	{
		if (RowWidget && !VisibleFixedRowNames.Contains(EntryName))
		{
			RowWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	HideIfUnused(FName(TEXT("Hunger")), HungerRow.Get());
	HideIfUnused(FName(TEXT("Thirst")), ThirstRow.Get());
	HideIfUnused(FName(TEXT("Sleep")), SleepRow.Get());
	HideIfUnused(FName(TEXT("Madness")), MadnessRow.Get());
	HideIfUnused(FName(TEXT("Curse")), CurseRow.Get());
	HideIfUnused(FName(TEXT("Pain")), PainRow.Get());
}

void UProjectInnerStateRowsGlobalWidget::ClearFallbackRows()
{
	if (ExtraRowsBox)
	{
		ExtraRowsBox->ClearChildren();
	}
	FallbackRowsByName.Empty();
}

UTexture2D* UProjectInnerStateRowWidget::ResolveTexture(
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

	return Cast<UTexture2D>(ProjectInnerStateRowWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerStateRowWidget::ResolveStyleAsset(
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

	return ProjectInnerStateRowWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerStateRowWidget::MakeTitleFont(const int32 Size) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = 60;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectInnerStateRowWidgetPrivate::CinzelFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerStateRowWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectInnerStateRowWidgetPrivate::CormorantFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}
