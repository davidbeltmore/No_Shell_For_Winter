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
	FLinearColor RushTint()
	{
		return EFProjectUIPalette::Positive();
	}
	FLinearColor MediaFrameTint()
	{
		return EFProjectUIPalette::PanelFill(0.72f);
	}
	constexpr float PanelRightOffset = -28.0f;
	constexpr float PanelBottomOffset = -34.0f;
	constexpr float DefaultPanelWidth = 470.0f;
	constexpr float DefaultPanelHeight = 420.0f;
	constexpr float MediaPanelWidth = DefaultPanelWidth;
	constexpr float MediaPanelHeight = 580.0f;
	constexpr float MediaFrameWidth = 442.0f;
	constexpr float MediaFrameHeight = 150.0f;
	constexpr int32 MaxSourceMediaTextureCacheEntries = 16;

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

void UProjectIntimacyHudWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);
	ApplyThemeColors();
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
		ProgressBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}

	PartnerClimaxText = MakeTextBlock(TEXT("PartnerClimaxText"), 13, ProjectIntimacyHudWidgetPrivate::MutedTint(), false);
	if (UVerticalBoxSlot* PartnerClimaxTextSlot = RootBox->AddChildToVerticalBox(PartnerClimaxText))
	{
		PartnerClimaxTextSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 2.0f));
	}

	PartnerClimaxBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("PartnerClimaxBar"));
	PartnerClimaxBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::VioletTint());
	if (UVerticalBoxSlot* PartnerClimaxBarSlot = RootBox->AddChildToVerticalBox(PartnerClimaxBar))
	{
		PartnerClimaxBarSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	MetaText = MakeTextBlock(TEXT("MetaText"), 12, ProjectIntimacyHudWidgetPrivate::MutedTint(), false);
	RootBox->AddChildToVerticalBox(MetaText);

	OrgasmRushText = MakeTextBlock(TEXT("OrgasmRushText"), 12, ProjectIntimacyHudWidgetPrivate::RushTint(), true);
	if (UVerticalBoxSlot* OrgasmRushSlot = RootBox->AddChildToVerticalBox(OrgasmRushText))
	{
		OrgasmRushSlot->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));
	}
	OrgasmRushText->SetVisibility(ESlateVisibility::Collapsed);

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
	PartnerClimaxText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PartnerClimaxText")));
	PartnerClimaxBar = Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("PartnerClimaxBar")));
	MetaText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MetaText")));
	OrgasmRushText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("OrgasmRushText")));
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
			TEXT("Player Climax %.0f%%  |  +%.1f%%/s"),
			CachedSnapshot.PlayerClimax,
			CachedSnapshot.PlayerClimaxPerSecond)));
	}

	if (SessionProgressBar)
	{
		SessionProgressBar->SetPercent(FMath::Clamp(CachedSnapshot.PlayerClimax / 100.0f, 0.0f, 1.0f));
	}

	if (PartnerClimaxText)
	{
		PartnerClimaxText->SetText(FText::FromString(FString::Printf(
			TEXT("Partner Climax %.0f%%  |  +%.1f%%/s"),
			CachedSnapshot.PartnerClimax,
			CachedSnapshot.PartnerClimaxPerSecond)));
	}

	if (PartnerClimaxBar)
	{
		PartnerClimaxBar->SetPercent(FMath::Clamp(CachedSnapshot.PartnerClimax / 100.0f, 0.0f, 1.0f));
	}

	if (MetaText)
	{
		MetaText->SetVisibility(ESlateVisibility::HitTestInvisible);
		MetaText->SetText(FText::FromString(FString::Printf(
			TEXT("Orgasms  You %d  |  Partner %d     Curse -%.1f%%/s"),
			CachedSnapshot.PlayerOrgasmCount,
			CachedSnapshot.PartnerOrgasmCount,
			FMath::Max(0.0f, CachedSnapshot.CurseReductionPercentPerSecond))));
	}

	if (OrgasmRushText)
	{
		TArray<FString> RushParticipants;
		if (CachedSnapshot.bPlayerOrgasmRush)
		{
			RushParticipants.Add(FString::Printf(
				TEXT("You %.1fs"),
				FMath::Max(0.0f, CachedSnapshot.PlayerOrgasmRushRemaining)));
		}
		if (CachedSnapshot.bPartnerOrgasmRush)
		{
			RushParticipants.Add(FString::Printf(
				TEXT("Partner %.1fs"),
				FMath::Max(0.0f, CachedSnapshot.PartnerOrgasmRushRemaining)));
		}

		const bool bRushVisible = RushParticipants.Num() > 0;
		OrgasmRushText->SetVisibility(bRushVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		OrgasmRushText->SetText(bRushVisible
			? FText::FromString(FString::Printf(TEXT("ORGASM RUSH  |  %s"), *FString::Join(RushParticipants, TEXT("  |  "))))
			: FText::GetEmpty());
	}

	if (PleaseText)
	{
		PleaseText->SetVisibility(CachedSnapshot.bPleaseActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		PleaseText->SetText(FText::FromString(FString::Printf(
			TEXT("Please (%s) %d/%d  |  Hits %d  |  Bonus Climax %.0f"),
			CachedSnapshot.PleaseClimaxTarget == EProjectIntimacyClimaxTarget::Player
				? TEXT("Player")
				: TEXT("Partner"),
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
		FString FooterText;
		if (!CachedSnapshot.StatusText.IsEmpty())
		{
			FooterText = CachedSnapshot.StatusText.ToString();
		}
		if (!CachedSnapshot.HintText.IsEmpty())
		{
			if (!FooterText.IsEmpty())
			{
				FooterText += TEXT("  |  ");
			}
			FooterText += CachedSnapshot.HintText.ToString();
		}
		StatusText->SetVisibility(FooterText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		StatusText->SetText(FText::FromString(FooterText));
	}

	RefreshOptionRowsIfNeeded();
}

void UProjectIntimacyHudWidget::ApplyThemeColors()
{
	const auto SetTextColor = [](UTextBlock* TextBlock, const FLinearColor& Color)
	{
		if (TextBlock)
		{
			TextBlock->SetColorAndOpacity(FSlateColor(Color));
		}
	};

	if (PanelBorder)
	{
		PanelBorder->SetBrushColor(ProjectIntimacyHudWidgetPrivate::PanelTint());
	}
	if (MediaFrameBorder)
	{
		MediaFrameBorder->SetBrushColor(ProjectIntimacyHudWidgetPrivate::MediaFrameTint());
	}

	SetTextColor(HeaderText, ProjectIntimacyHudWidgetPrivate::RoseTint());
	SetTextColor(PartnerText, ProjectIntimacyHudWidgetPrivate::TextTint());
	SetTextColor(SessionProgressText, ProjectIntimacyHudWidgetPrivate::MutedTint());
	SetTextColor(PartnerClimaxText, ProjectIntimacyHudWidgetPrivate::MutedTint());
	SetTextColor(MetaText, ProjectIntimacyHudWidgetPrivate::MutedTint());
	SetTextColor(OrgasmRushText, ProjectIntimacyHudWidgetPrivate::RushTint());
	SetTextColor(PleaseText, ProjectIntimacyHudWidgetPrivate::VioletTint());
	SetTextColor(StatusText, ProjectIntimacyHudWidgetPrivate::MutedTint());

	if (SessionProgressBar)
	{
		SessionProgressBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::RoseTint());
	}
	if (PartnerClimaxBar)
	{
		PartnerClimaxBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::VioletTint());
	}
	if (PleaseBar)
	{
		PleaseBar->SetFillColorAndOpacity(ProjectIntimacyHudWidgetPrivate::VioletTint());
	}

	for (int32 Index = 0; Index < OptionTextBlocks.Num(); ++Index)
	{
		SetTextColor(
			OptionTextBlocks[Index],
			Index == CachedSnapshot.SelectedOptionIndex
				? ProjectIntimacyHudWidgetPrivate::SelectedTint()
				: ProjectIntimacyHudWidgetPrivate::TextTint());
	}

	if (MediaImage && ActiveMediaTexture)
	{
		MediaImage->SetBrushFromTexture(ResolveProjectThemeTexture(ActiveMediaTexture), true);
		MediaImage->SetColorAndOpacity(ResolveProjectThemeImageTint(MediaImage, FLinearColor::White));
	}
}

void UProjectIntimacyHudWidget::RefreshOptionRowsIfNeeded()
{
	const uint32 CurrentSignature = BuildOptionRowsSignature();
	if (!bOptionRowsInitialized || CurrentSignature != LastOptionRowsSignature)
	{
		RebuildOptionRows();
	}
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

	LastOptionRowsSignature = BuildOptionRowsSignature();
	bOptionRowsInitialized = true;
}

uint32 UProjectIntimacyHudWidget::BuildOptionRowsSignature() const
{
	uint32 Signature = GetTypeHash(static_cast<uint8>(CachedSnapshot.HudMode));
	Signature = HashCombine(Signature, GetTypeHash(CachedSnapshot.SelectedOptionIndex));
	Signature = HashCombine(Signature, GetTypeHash(CachedSnapshot.Options.Num()));

	for (const FProjectIntimacyHudOption& Option : CachedSnapshot.Options)
	{
		Signature = HashCombine(Signature, GetTypeHash(Option.OptionId));
		Signature = HashCombine(Signature, GetTypeHash(Option.Label.ToString()));
	}

	return Signature;
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

	if (ResolvedPath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath.RightChop(6));
	}
	else if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath);
	}
	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	FPaths::NormalizeFilename(ResolvedPath);
	FPaths::CollapseRelativeDirectories(ResolvedPath);

	FString CacheKey = ResolvedPath;
#if PLATFORM_WINDOWS
	CacheKey.ToLowerInline();
#endif

	if (TObjectPtr<UTexture2D>* CachedTexture = SourceMediaTextureCache.Find(CacheKey))
	{
		if (IsValid(CachedTexture->Get()))
		{
			SourceMediaTextureCacheOrder.Remove(CacheKey);
			SourceMediaTextureCacheOrder.Add(CacheKey);
			return CachedTexture->Get();
		}

		SourceMediaTextureCache.Remove(CacheKey);
		SourceMediaTextureCacheOrder.Remove(CacheKey);
	}

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

	while (SourceMediaTextureCache.Num() >= ProjectIntimacyHudWidgetPrivate::MaxSourceMediaTextureCacheEntries)
	{
		bool bEvictedEntry = false;
		for (int32 Index = 0; Index < SourceMediaTextureCacheOrder.Num(); ++Index)
		{
			const FString CandidateKey = SourceMediaTextureCacheOrder[Index];
			const TObjectPtr<UTexture2D>* CandidateTexture = SourceMediaTextureCache.Find(CandidateKey);
			if (CandidateTexture && CandidateTexture->Get() == ActiveMediaTexture.Get())
			{
				continue;
			}

			SourceMediaTextureCache.Remove(CandidateKey);
			SourceMediaTextureCacheOrder.RemoveAt(Index);
			bEvictedEntry = true;
			break;
		}

		if (!bEvictedEntry)
		{
			break;
		}
	}

	SourceMediaTextureCache.Add(CacheKey, Texture);
	SourceMediaTextureCacheOrder.Add(CacheKey);
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
