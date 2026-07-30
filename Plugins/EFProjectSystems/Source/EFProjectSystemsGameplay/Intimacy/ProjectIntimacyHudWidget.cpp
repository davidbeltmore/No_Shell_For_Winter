#include "Intimacy/ProjectIntimacyHudWidget.h"

#include "EFProjectUIPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"

namespace ProjectIntimacyHudWidgetPrivate
{
	FLinearColor PanelTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.88f);
	}
	FLinearColor BorderTint()
	{
		return EFProjectUIPalette::Outline(0.55f);
	}
	FLinearColor RoseTint()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor VioletTint()
	{
		return EFProjectUIPalette::Accent();
	}
	FLinearColor TextTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor MutedTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	FLinearColor SelectedTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor MediaFrameTint()
	{
		return EFProjectUIPalette::PanelFill(0.72f);
	}
	constexpr float PanelRightOffset = -28.0f;
	constexpr float PanelBottomOffset = -34.0f;
	constexpr float DefaultPanelWidth = 470.0f;
	constexpr float DefaultPanelHeight = 360.0f;
	constexpr float MediaPanelWidth = DefaultPanelWidth;
	constexpr float MediaPanelHeight = DefaultPanelHeight;
	constexpr float MediaFrameWidth = 442.0f;
	constexpr float MediaFrameHeight = 150.0f;

	void ApplyPanelSize(USizeBox* PanelSizeBox, const float Width, const float Height)
	{
		if (!PanelSizeBox)
		{
			return;
		}

		PanelSizeBox->SetWidthOverride(Width);
		PanelSizeBox->SetHeightOverride(Height);
		PanelSizeBox->SetMinDesiredHeight(Height);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PanelSizeBox->Slot))
		{
			CanvasSlot->SetOffsets(FMargin(PanelRightOffset, PanelBottomOffset, Width, Height));
		}
	}

	FSlateFontInfo MakeFont(const TCHAR* Weight, const int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	FString FormatSeconds(const float Seconds)
	{
		const int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(Seconds));
		const int32 Minutes = TotalSeconds / 60;
		const int32 Remainder = TotalSeconds % 60;
		return FString::Printf(TEXT("%02d:%02d"), Minutes, Remainder);
	}

	FText ModeToText(const EProjectIntimacyHudMode Mode)
	{
		switch (Mode)
		{
		case EProjectIntimacyHudMode::Talk:
			return FText::FromString(TEXT("Talk"));
		case EProjectIntimacyHudMode::Items:
			return FText::FromString(TEXT("Items"));
		case EProjectIntimacyHudMode::Please:
			return FText::FromString(TEXT("Please"));
		case EProjectIntimacyHudMode::Main:
		default:
			return FText::FromString(TEXT("Intimacy"));
		}
	}
}

UProjectIntimacyHudWidget::UProjectIntimacyHudWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UProjectIntimacyHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureDefaultWidgetTree();
	CacheNamedWidgets();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectIntimacyHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureDefaultWidgetTree();
	CacheNamedWidgets();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectIntimacyHudWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	(void)MyGeometry;
	UpdateMediaCue(InDeltaTime);
}

void UProjectIntimacyHudWidget::SetSnapshot(const FProjectIntimacySessionSnapshot& InSnapshot)
{
	CachedSnapshot = InSnapshot;
	RefreshVisuals();
	OnIntimacySnapshotApplied(CachedSnapshot);
}

const FProjectIntimacySessionSnapshot& UProjectIntimacyHudWidget::GetCachedSnapshot() const
{
	return CachedSnapshot;
}

void UProjectIntimacyHudWidget::PlayMediaCue(const FProjectIntimacyMediaCueRow& Cue)
{
	if (!Cue.bEnabled || Cue.MediaType == EProjectIntimacyMediaType::None)
	{
		return;
	}

	UTexture2D* Texture = ResolveMediaTexture(Cue);
	if (!Texture)
	{
		return;
	}

	ActiveMediaCue = Cue;
	ActiveMediaTexture = Texture;
	ActiveMediaElapsedSeconds = 0.0f;
	bMediaCueActive = true;

	ProjectIntimacyHudWidgetPrivate::ApplyPanelSize(
		PanelSizeBox,
		ProjectIntimacyHudWidgetPrivate::MediaPanelWidth,
		ProjectIntimacyHudWidgetPrivate::MediaPanelHeight);

	if (MediaImage)
	{
		const FVector2D MediaSize = Cue.SourceMediaSize.X > 0.0f && Cue.SourceMediaSize.Y > 0.0f
			? Cue.SourceMediaSize
			: FVector2D(static_cast<float>(Texture->GetSizeX()), static_cast<float>(Texture->GetSizeY()));
		MediaImage->SetBrushFromTexture(ResolveProjectThemeTexture(Texture), true);
		MediaImage->SetDesiredSizeOverride(MediaSize);
		MediaImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			MediaImage,
			FLinearColor::White));
	}
	if (MediaFrameSizeBox)
	{
		MediaFrameSizeBox->SetWidthOverride(ProjectIntimacyHudWidgetPrivate::MediaFrameWidth);
		MediaFrameSizeBox->SetHeightOverride(ProjectIntimacyHudWidgetPrivate::MediaFrameHeight);
		MediaFrameSizeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (MediaFrameBorder)
	{
		MediaFrameBorder->SetRenderOpacity(0.0f);
	}
}

void UProjectIntimacyHudWidget::EnsureDefaultWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	PanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSizeBox"));
	ProjectIntimacyHudWidgetPrivate::ApplyPanelSize(
		PanelSizeBox,
		ProjectIntimacyHudWidgetPrivate::DefaultPanelWidth,
		ProjectIntimacyHudWidgetPrivate::DefaultPanelHeight);

	if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(PanelSizeBox))
	{
		CanvasSlot->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
		CanvasSlot->SetAlignment(FVector2D(1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(
			ProjectIntimacyHudWidgetPrivate::PanelRightOffset,
			ProjectIntimacyHudWidgetPrivate::PanelBottomOffset,
			ProjectIntimacyHudWidgetPrivate::DefaultPanelWidth,
			ProjectIntimacyHudWidgetPrivate::DefaultPanelHeight));
	}

	PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
	PanelBorder->SetBrushColor(ProjectIntimacyHudWidgetPrivate::PanelTint());
	PanelBorder->SetPadding(FMargin(14.0f, 12.0f));
	PanelSizeBox->SetContent(PanelBorder);

	UVerticalBox* RootBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	PanelBorder->SetContent(RootBox);

	HeaderText = MakeTextBlock(TEXT("HeaderText"), 18, ProjectIntimacyHudWidgetPrivate::RoseTint(), true);
	RootBox->AddChildToVerticalBox(HeaderText);

	PartnerText = MakeTextBlock(TEXT("PartnerText"), 15, ProjectIntimacyHudWidgetPrivate::TextTint(), false);
	if (UVerticalBoxSlot* PartnerSlot = RootBox->AddChildToVerticalBox(PartnerText))
	{
		PartnerSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	SessionProgressText = MakeTextBlock(TEXT("SessionProgressText"), 13, ProjectIntimacyHudWidgetPrivate::MutedTint(), false);
	if (UVerticalBoxSlot* ProgressTextSlot = RootBox->AddChildToVerticalBox(SessionProgressText))
	{
		ProgressTextSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 2.0f));
	}

	SessionProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("SessionProgressBar"));
	SessionProgressBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::RoseTint());
	if (UVerticalBoxSlot* ProgressBarSlot = RootBox->AddChildToVerticalBox(SessionProgressBar))
	{
		ProgressBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	MetaText = MakeTextBlock(TEXT("MetaText"), 12, ProjectIntimacyHudWidgetPrivate::MutedTint(), false);
	RootBox->AddChildToVerticalBox(MetaText);

	PleaseText = MakeTextBlock(TEXT("PleaseText"), 12, ProjectIntimacyHudWidgetPrivate::VioletTint(), false);
	if (UVerticalBoxSlot* PleaseTextSlot = RootBox->AddChildToVerticalBox(PleaseText))
	{
		PleaseTextSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 2.0f));
	}

	PleaseBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("PleaseBar"));
	PleaseBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::VioletTint());
	if (UVerticalBoxSlot* PleaseBarSlot = RootBox->AddChildToVerticalBox(PleaseBar))
	{
		PleaseBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	OptionsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OptionsBox"));
	if (UVerticalBoxSlot* OptionsSlot = RootBox->AddChildToVerticalBox(OptionsBox))
	{
		OptionsSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}

	MediaFrameSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MediaFrameSizeBox"));
	MediaFrameSizeBox->SetWidthOverride(ProjectIntimacyHudWidgetPrivate::MediaFrameWidth);
	MediaFrameSizeBox->SetHeightOverride(ProjectIntimacyHudWidgetPrivate::MediaFrameHeight);
	MediaFrameSizeBox->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* MediaSlot = RootBox->AddChildToVerticalBox(MediaFrameSizeBox))
	{
		MediaSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}

	MediaFrameBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MediaFrameBorder"));
	MediaFrameBorder->SetBrushColor(ProjectIntimacyHudWidgetPrivate::MediaFrameTint());
	MediaFrameBorder->SetPadding(FMargin(4.0f));
	MediaFrameSizeBox->SetContent(MediaFrameBorder);

	MediaScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("MediaScaleBox"));
	MediaScaleBox->SetStretch(EStretch::Fill);
	MediaFrameBorder->SetContent(MediaScaleBox);

	MediaImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MediaImage"));
	MediaImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
		MediaImage,
		FLinearColor::White));
	MediaScaleBox->SetContent(MediaImage);

	StatusText = MakeTextBlock(TEXT("StatusText"), 12, ProjectIntimacyHudWidgetPrivate::MutedTint(), false);
	if (UVerticalBoxSlot* StatusSlot = RootBox->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}
	StatusText->SetVisibility(ESlateVisibility::Collapsed);
}

void UProjectIntimacyHudWidget::CacheNamedWidgets()
{
	if (!WidgetTree)
	{
		return;
	}

	RootCanvas = Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("RootCanvas")));
	PanelSizeBox = Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PanelSizeBox")));
	PanelBorder = Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelBorder")));
	HeaderText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HeaderText")));
	PartnerText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PartnerText")));
	SessionProgressText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("SessionProgressText")));
	SessionProgressBar = Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("SessionProgressBar")));
	MetaText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MetaText")));
	OptionsBox = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("OptionsBox")));
	MediaFrameSizeBox = Cast<USizeBox>(WidgetTree->FindWidget(TEXT("MediaFrameSizeBox")));
	MediaFrameBorder = Cast<UBorder>(WidgetTree->FindWidget(TEXT("MediaFrameBorder")));
	MediaScaleBox = Cast<UScaleBox>(WidgetTree->FindWidget(TEXT("MediaScaleBox")));
	MediaImage = Cast<UImage>(WidgetTree->FindWidget(TEXT("MediaImage")));
	StatusText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("StatusText")));
	PleaseText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PleaseText")));
	PleaseBar = Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("PleaseBar")));
}

void UProjectIntimacyHudWidget::RefreshVisuals()
{
	SetVisibility(CachedSnapshot.bActive && CachedSnapshot.bHudVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!CachedSnapshot.bActive)
	{
		StopMediaCue();
	}

	if (PanelBorder)
	{
		PanelBorder->SetBrushColor(ProjectIntimacyHudWidgetPrivate::PanelTint());
	}

	if (HeaderText)
	{
		HeaderText->SetText(ProjectIntimacyHudWidgetPrivate::ModeToText(CachedSnapshot.HudMode));
	}

	if (PartnerText)
	{
		PartnerText->SetText(CachedSnapshot.PartnerDisplayName);
	}

	if (SessionProgressText)
	{
		SessionProgressText->SetText(FText::FromString(FString::Printf(
			TEXT("Session Progress %.0f%%  |  Peak %.0f%%  |  Rate %.1f/s"),
			CachedSnapshot.SessionProgress,
			CachedSnapshot.SessionPeak,
			CachedSnapshot.SessionProgressPerSecond)));
	}

	if (SessionProgressBar)
	{
		SessionProgressBar->SetPercent(FMath::Clamp(CachedSnapshot.SessionProgress / 100.0f, 0.0f, 1.0f));
	}

	if (MetaText)
	{
		MetaText->SetVisibility(ESlateVisibility::Collapsed);
		MetaText->SetText(FText::GetEmpty());
	}

	if (PleaseText)
	{
		PleaseText->SetVisibility(CachedSnapshot.bPleaseActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		PleaseText->SetText(FText::FromString(FString::Printf(
			TEXT("Please %d/%d  |  Hits %d  |  Bonus progress %.0f"),
			FMath::Clamp(CachedSnapshot.PleaseAttemptIndex + 1, 1, FMath::Max(1, CachedSnapshot.PleaseAttemptCount)),
			CachedSnapshot.PleaseAttemptCount,
			CachedSnapshot.PleaseSuccessCount,
			CachedSnapshot.PleasePreviewProgress)));
	}

	if (PleaseBar)
	{
		PleaseBar->SetVisibility(CachedSnapshot.bPleaseActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		PleaseBar->SetPercent(FMath::Clamp(CachedSnapshot.PleaseCursorValue, 0.0f, 1.0f));
	}

	if (StatusText)
	{
		StatusText->SetVisibility(ESlateVisibility::Collapsed);
		StatusText->SetText(FText::GetEmpty());
	}

	RebuildOptionRows();
}

void UProjectIntimacyHudWidget::RebuildOptionRows()
{
	if (!OptionsBox || !WidgetTree)
	{
		return;
	}

	OptionsBox->ClearChildren();
	OptionTextBlocks.Reset();

	for (int32 Index = 0; Index < CachedSnapshot.Options.Num(); ++Index)
	{
		const FProjectIntimacyHudOption& Option = CachedSnapshot.Options[Index];
		UTextBlock* OptionText = MakeTextBlock(
			*FString::Printf(TEXT("OptionText_%d"), Index),
			13,
			Index == CachedSnapshot.SelectedOptionIndex
				? ProjectIntimacyHudWidgetPrivate::SelectedTint()
				: ProjectIntimacyHudWidgetPrivate::TextTint(),
			Index == CachedSnapshot.SelectedOptionIndex);
		if (!OptionText)
		{
			continue;
		}

		const FString Prefix = Index == CachedSnapshot.SelectedOptionIndex ? TEXT("> ") : TEXT("  ");
		OptionText->SetText(FText::FromString(Prefix + Option.Label.ToString()));
		if (UVerticalBoxSlot* OptionSlot = OptionsBox->AddChildToVerticalBox(OptionText))
		{
			OptionSlot->SetPadding(FMargin(0.0f, 1.0f));
		}
		OptionTextBlocks.Add(OptionText);
	}
}

void UProjectIntimacyHudWidget::StopMediaCue()
{
	bMediaCueActive = false;
	ActiveMediaElapsedSeconds = 0.0f;
	ActiveMediaCue = FProjectIntimacyMediaCueRow();
	ActiveMediaTexture = nullptr;

	ProjectIntimacyHudWidgetPrivate::ApplyPanelSize(
		PanelSizeBox,
		ProjectIntimacyHudWidgetPrivate::DefaultPanelWidth,
		ProjectIntimacyHudWidgetPrivate::DefaultPanelHeight);

	if (MediaFrameSizeBox)
	{
		MediaFrameSizeBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (MediaFrameBorder)
	{
		MediaFrameBorder->SetRenderOpacity(0.0f);
	}
}

void UProjectIntimacyHudWidget::UpdateMediaCue(const float InDeltaTime)
{
	if (!bMediaCueActive)
	{
		return;
	}

	const float FadeIn = FMath::Max(0.0f, ActiveMediaCue.FadeInSeconds);
	const float Hold = FMath::Max(0.0f, ActiveMediaCue.HoldSeconds);
	const float FadeOut = FMath::Max(0.0f, ActiveMediaCue.FadeOutSeconds);
	const float TotalDuration = FadeIn + Hold + FadeOut;
	ActiveMediaElapsedSeconds += FMath::Max(0.0f, InDeltaTime);

	if (TotalDuration <= 0.0f || ActiveMediaElapsedSeconds >= TotalDuration)
	{
		StopMediaCue();
		return;
	}

	float Alpha = 1.0f;
	if (FadeIn > 0.0f && ActiveMediaElapsedSeconds < FadeIn)
	{
		Alpha = ActiveMediaElapsedSeconds / FadeIn;
	}
	else if (FadeOut > 0.0f && ActiveMediaElapsedSeconds > FadeIn + Hold)
	{
		Alpha = 1.0f - ((ActiveMediaElapsedSeconds - FadeIn - Hold) / FadeOut);
	}

	if (MediaFrameSizeBox)
	{
		MediaFrameSizeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (MediaFrameBorder)
	{
		MediaFrameBorder->SetRenderOpacity(FMath::Clamp(Alpha, 0.0f, 1.0f));
	}
}

UTexture2D* UProjectIntimacyHudWidget::ResolveMediaTexture(const FProjectIntimacyMediaCueRow& Cue)
{
	if (Cue.MediaType != EProjectIntimacyMediaType::Image)
	{
		return nullptr;
	}

	if (UTexture2D* TextureAsset = Cast<UTexture2D>(Cue.TextureAsset.TryLoad()))
	{
		return TextureAsset;
	}

	return LoadSourceImageTexture(Cue.SourceImagePath);
}

UTexture2D* UProjectIntimacyHudWidget::LoadSourceImageTexture(const FString& SourceImagePath)
{
	FString ResolvedPath = SourceImagePath;
	if (ResolvedPath.IsEmpty())
	{
		return nullptr;
	}

	if (ResolvedPath.StartsWith(TEXT("/Game/")))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath.RightChop(6));
	}
	else if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath);
	}
	FPaths::NormalizeFilename(ResolvedPath);

	TArray<uint8> CompressedData;
	if (!FFileHelper::LoadFileToArray(CompressedData, *ResolvedPath))
	{
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(ResolvedPath).ToLower();
	const EImageFormat ImageFormat = (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
		? EImageFormat::JPEG
		: EImageFormat::PNG;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
	{
		return nullptr;
	}

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData) || RawData.Num() <= 0)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
	{
		return nullptr;
	}

	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->LODGroup = TEXTUREGROUP_UI;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!TextureData)
	{
		Mip.BulkData.Unlock();
		return nullptr;
	}

	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	LoadedMediaTextures.Add(Texture);
	return Texture;
}

UTextBlock* UProjectIntimacyHudWidget::MakeTextBlock(
	const FName WidgetName,
	const int32 FontSize,
	const FLinearColor& Color,
	const bool bBold) const
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
	if (!TextBlock)
	{
		return nullptr;
	}

	TextBlock->SetFont(ProjectIntimacyHudWidgetPrivate::MakeFont(bBold ? TEXT("Bold") : TEXT("Regular"), FontSize));
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
	TextBlock->SetAutoWrapText(true);
	return TextBlock;
}
