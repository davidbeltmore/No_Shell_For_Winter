#include "Defeat/ProjectKnockoutStruggleWidget.h"

#include "EFProjectUIPalette.h"
#include "Algo/Count.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "ProjectKnockoutStruggleWidget"

namespace ProjectKnockoutStruggleWidgetPrivate
{
	constexpr float TopPanelWidth = 560.f;
	constexpr float TopPanelHeight = 124.f;
	constexpr float MainPanelWidth = 1040.f;
	constexpr float MainPanelHeight = 216.f;
	constexpr float MainPanelCenterOffsetY = 168.f;
	constexpr float TopPanelGapToMainPanel = 24.f;
	constexpr float TopPanelCenterOffsetY =
		MainPanelCenterOffsetY - (MainPanelHeight * 0.5f) - (TopPanelHeight * 0.5f) - TopPanelGapToMainPanel;
	constexpr float TopPanelContentWidth = 470.f;
	constexpr float TopPanelContentHeight = 74.f;
	constexpr float TopPanelContentCenterOffsetY = TopPanelCenterOffsetY;
	constexpr float TopPanelHintPaddingTop = 2.f;
	constexpr float TopPanelProgressPaddingTop = 4.f;
	constexpr float TrackGlowWidth = 770.f;
	constexpr float TrackGlowHeight = 28.f;
	constexpr float TrackCenterOffsetX = -26.f;
	constexpr float TrackCenterOffsetY = 168.f;
	constexpr float TravelStartOffsetX = -456.f;
	constexpr float TargetCenterOffsetX = 396.f;
	constexpr float TargetChamberWidth = 176.f;
	constexpr float TargetChamberHeight = 118.f;
	constexpr float TargetRingSize = 78.f;
	constexpr float TargetPulseSize = 150.f;
	constexpr float NoteSize = 66.f;
	constexpr float FeedbackOffsetX = -338.f;
	constexpr float FeedbackOffsetY = 281.f;
	constexpr float TimingLabelsOffsetY = 281.f;
	constexpr float MissTimingOffsetX = -248.f;
	constexpr float GoodLeftTimingOffsetX = -82.f;
	constexpr float PerfectTimingOffsetX = 92.f;
	constexpr float GoodRightTimingOffsetX = 246.f;
	constexpr float PerfectThresholdRatio = 0.35f;

	const FLinearColor BackdropColor(0.0f, 0.0f, 0.0f, 0.18f);
	FLinearColor NoteColor()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor ReadyNoteColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor GoodColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor PerfectColor()
	{
		return EFProjectUIPalette::Positive();
	}
	FLinearColor MissColor()
	{
		return EFProjectUIPalette::Negative();
	}
	FLinearColor HeaderColor()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor HintColor()
	{
		return EFProjectUIPalette::SecondaryText(0.96f);
	}
	FLinearColor ProgressColor()
	{
		return EFProjectUIPalette::PrimaryText(0.98f);
	}
	FLinearColor TimingGoodColor()
	{
		return EFProjectUIPalette::SecondaryText(0.94f);
	}
	FLinearColor TimingPerfectColor()
	{
		return EFProjectUIPalette::Positive(0.96f);
	}
	FLinearColor TargetIdleColor()
	{
		return EFProjectUIPalette::Accent(0.90f);
	}
	FLinearColor TargetReadyColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor PulseColor()
	{
		return EFProjectUIPalette::AccentSoft();
	}

	const TCHAR* CinzelFontPath = TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel.FF_Cinzel");
	const TCHAR* BarlowFontPath = TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed.FF_BarlowSemiCondensed");
	const TCHAR* BarlowMediumFontPath = TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium.FF_BarlowSemiCondensedMedium");
	const TCHAR* TopPanelTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel.T_Struggle_TopPanel");
	const TCHAR* MainPanelTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel.T_Struggle_MainPanel");
	const TCHAR* ChamberTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber.T_Struggle_TargetChamber");
	const TCHAR* RingTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing.T_Struggle_TargetRing");
	const TCHAR* PulseTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse.T_Struggle_TargetPulse");
	const TCHAR* ArrowTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow.T_Struggle_Arrow");
	const TCHAR* GlowTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak.T_Struggle_GlowStreak");
	const TCHAR* NoiseTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise.T_Struggle_Noise");
	const TCHAR* VignetteTexturePath = TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette.T_Struggle_BackdropVignette");

	UCanvasPanelSlot* AddCanvasChild(
		UCanvasPanel* Canvas,
		UWidget* Child,
		const FAnchors& Anchors,
		const FVector2D& Alignment,
		const FVector2D& Position,
		const FVector2D& Size,
		const int32 ZOrder,
		const bool bAutoSize = false)
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
		Slot->SetZOrder(ZOrder);
		Slot->SetAutoSize(bAutoSize);
		if (!bAutoSize)
		{
			Slot->SetSize(Size);
		}

		return Slot;
	}

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		if (!AssetPath || AssetPath[0] == 0)
		{
			return nullptr;
		}

		static TMap<FName, TWeakObjectPtr<UObject>> ResolvedAssets;
		const FName AssetKey(AssetPath);
		if (const TWeakObjectPtr<UObject>* CachedAsset = ResolvedAssets.Find(AssetKey))
		{
			if (CachedAsset->IsValid())
			{
				return CachedAsset->Get();
			}
		}

		UObject* LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath);
		if (LoadedAsset)
		{
			ResolvedAssets.Add(AssetKey, LoadedAsset);
		}
		return LoadedAsset;
	}

	FSlateFontInfo MakeFont(const TCHAR* AssetPath, const TCHAR* FallbackWeight, const int32 Size)
	{
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(FallbackWeight, Size);
		FontInfo.Size = Size;
		if (UObject* FontObject = LoadObjectByPath(AssetPath))
		{
			FontInfo.FontObject = FontObject;
			FontInfo.TypefaceFontName = NAME_None;
		}

		return FontInfo;
	}

	FString BuildEnemyDisplayName(const FName EnemyClassName)
	{
		FString DisplayName = EnemyClassName.ToString();
		DisplayName.RemoveFromEnd(TEXT("_C"));
		DisplayName.ReplaceInline(TEXT("BP_"), TEXT(""));
		return DisplayName;
	}
}

UProjectKnockoutStruggleWidget::UProjectKnockoutStruggleWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	TitleFontAsset = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::CinzelFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::BarlowFontPath);
	BodyMediumFontAsset = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::BarlowMediumFontPath);
	TopPanelTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::TopPanelTexturePath);
	MainPanelTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::MainPanelTexturePath);
	TargetChamberTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::ChamberTexturePath);
	TargetRingTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::RingTexturePath);
	TargetPulseTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::PulseTexturePath);
	ArrowTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::ArrowTexturePath);
	GlowTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::GlowTexturePath);
	NoiseTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::NoiseTexturePath);
	VignetteTexture = FSoftObjectPath(ProjectKnockoutStruggleWidgetPrivate::VignetteTexturePath);
}

void UProjectKnockoutStruggleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshHeader();
	RefreshLaneVisuals();
}

void UProjectKnockoutStruggleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshHeader();
	RefreshLaneVisuals();
	FocusWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectKnockoutStruggleWidget::NativeDestruct()
{
	ClearRoundState();
	Super::NativeDestruct();
}

void UProjectKnockoutStruggleWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	(void)MyGeometry;
	(void)InDeltaTime;

	if (!bRoundActive || bRoundCompleted || (!NotesLayer && !RootCanvas))
	{
		return;
	}

	const float ElapsedSeconds = GetElapsedRoundSeconds();
	const float TravelTimeSeconds = FMath::Max(ActiveRound.TravelTimeSeconds, 0.05f);

	int32 ResolvedNotes = 0;
	for (int32 NoteIndex = 0; NoteIndex < Notes.Num(); ++NoteIndex)
	{
		FProjectStruggleNoteRuntime& Note = Notes[NoteIndex];
		if (Note.bResolved)
		{
			++ResolvedNotes;
			if (Note.NoteImage)
			{
				Note.NoteImage->SetVisibility(ESlateVisibility::Collapsed);
			}
			continue;
		}

		if (ElapsedSeconds > (Note.HitTimeSeconds + ActiveRound.HitWindowSeconds))
		{
			if (RegisterMissedNote(NoteIndex))
			{
				return;
			}

			++ResolvedNotes;
			continue;
		}

		if (!Note.NoteImage)
		{
			continue;
		}

		const float TimeUntilHitSeconds = Note.HitTimeSeconds - ElapsedSeconds;
		const float Normalized = 1.f - FMath::Clamp(TimeUntilHitSeconds / TravelTimeSeconds, 0.f, 1.f);
		const float NotePosX = FMath::Lerp(
			ProjectKnockoutStruggleWidgetPrivate::TravelStartOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TargetCenterOffsetX,
			Normalized);
		const float DistanceToHitSeconds = FMath::Abs(TimeUntilHitSeconds);
		const bool bReady = DistanceToHitSeconds <= ActiveRound.HitWindowSeconds;
		const float Scale = FMath::Lerp(0.78f, bReady ? 1.08f : 0.98f, Normalized);
		const float Opacity = bReady ? 1.0f : FMath::Lerp(0.55f, 0.92f, Normalized);
		const FLinearColor Tint = bReady
			? ProjectKnockoutStruggleWidgetPrivate::ReadyNoteColor()
			: ProjectKnockoutStruggleWidgetPrivate::NoteColor();

		ApplyNoteVisual(Note, Tint, Scale, Opacity);

		if (UCanvasPanelSlot* NoteSlot = Cast<UCanvasPanelSlot>(Note.NoteImage->Slot))
		{
			NoteSlot->SetPosition(FVector2D(NotePosX, ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY));
		}
	}

	RefreshLaneVisuals();
	UpdateProgressText();

	if (ResolvedNotes >= Notes.Num() && Notes.Num() > 0)
	{
		CompleteRound(MissCount < MaxMissCount);
	}
}

FReply UProjectKnockoutStruggleWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	(void)InGeometry;

	if (!bRoundActive || bRoundCompleted)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const int32 LaneIndex = ResolveLaneFromKey(InKeyEvent.GetKey());
	if (LaneIndex == INDEX_NONE)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	int32 NoteIndex = INDEX_NONE;
	float HitDeltaSeconds = TNumericLimits<float>::Max();
	if (!TryResolveHitNote(LaneIndex, NoteIndex, HitDeltaSeconds))
	{
		SetFeedbackState(LOCTEXT("WrongFeedback", "MISS"), ProjectKnockoutStruggleWidgetPrivate::MissColor());
		RegisterWrongInputMiss();
		return FReply::Handled();
	}

	if (!Notes.IsValidIndex(NoteIndex))
	{
		return FReply::Handled();
	}

	const bool bPerfectHit = IsPerfectHit(HitDeltaSeconds);
	FProjectStruggleNoteRuntime& Note = Notes[NoteIndex];
	Note.bResolved = true;
	if (Note.NoteImage)
	{
		ApplyNoteVisual(
			Note,
			bPerfectHit ? ProjectKnockoutStruggleWidgetPrivate::PerfectColor() : ProjectKnockoutStruggleWidgetPrivate::GoodColor(),
			1.12f,
			1.0f);
	}

	SetFeedbackState(
		bPerfectHit ? LOCTEXT("PerfectFeedback", "PERFECT") : LOCTEXT("GoodFeedback", "GOOD"),
		bPerfectHit ? ProjectKnockoutStruggleWidgetPrivate::PerfectColor() : ProjectKnockoutStruggleWidgetPrivate::GoodColor());
	RefreshLaneVisuals();
	UpdateProgressText();

	const bool bAllResolved = Notes.FindByPredicate([](const FProjectStruggleNoteRuntime& Candidate)
	{
		return !Candidate.bResolved;
	}) == nullptr;
	if (bAllResolved)
	{
		CompleteRound(MissCount < MaxMissCount);
	}

	return FReply::Handled();
}

void UProjectKnockoutStruggleWidget::StartRound(const FProjectStruggleRound& InRound)
{
	BuildWidgetTree();
	InitializeVisualTree();
	ClearRoundState();

	ActiveRound = InRound;
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	MaxMissCount = FMath::Max(
		1,
		InRound.MaxMissCount > 0
			? InRound.MaxMissCount
			: (Settings ? Settings->MaxStruggleMisses : 5));
	MissCount = 0;
	bRoundActive = true;
	bRoundCompleted = false;
	RoundStartTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	LastFeedbackRoundSeconds = -1.0;
	ActiveFeedbackText = FText::GetEmpty();
	ActiveFeedbackColorRole = EFeedbackColorRole::Header;
	ActiveFeedbackColor = ProjectKnockoutStruggleWidgetPrivate::HeaderColor();

	GenerateChart(InRound);
	RefreshHeader();
	RefreshLaneVisuals();
	FocusWidget();
}

void UProjectKnockoutStruggleWidget::AbortRound(const bool bTreatAsFailure)
{
	if (!bRoundActive || bRoundCompleted)
	{
		ClearRoundState();
		return;
	}

	CompleteRound(!bTreatAsFailure, false);
}

bool UProjectKnockoutStruggleWidget::IsRoundActive() const
{
	return bRoundActive && !bRoundCompleted;
}

int32 UProjectKnockoutStruggleWidget::GetMissCount() const
{
	return MissCount;
}

int32 UProjectKnockoutStruggleWidget::GetMaxMissCount() const
{
	return MaxMissCount;
}

void UProjectKnockoutStruggleWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("KnockoutStruggleRoot"));
	WidgetTree->RootWidget = RootCanvas;

	BackdropBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		BackdropBorder,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		0);
	BackdropBorder->SetPadding(FMargin(0.f));

	VignetteImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("VignetteImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		VignetteImage,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		1);

	NoiseImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("NoiseImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		NoiseImage,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		2);

	TopPanelImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TopPanelImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TopPanelImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.f, ProjectKnockoutStruggleWidgetPrivate::TopPanelCenterOffsetY),
		FVector2D(ProjectKnockoutStruggleWidgetPrivate::TopPanelWidth, ProjectKnockoutStruggleWidgetPrivate::TopPanelHeight),
		3);

	MainPanelImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MainPanelImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		MainPanelImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.f, ProjectKnockoutStruggleWidgetPrivate::MainPanelCenterOffsetY),
		FVector2D(ProjectKnockoutStruggleWidgetPrivate::MainPanelWidth, ProjectKnockoutStruggleWidgetPrivate::MainPanelHeight),
		4);

	TopPanelContentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TopPanelContentBox"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TopPanelContentBox,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(0.f, ProjectKnockoutStruggleWidgetPrivate::TopPanelContentCenterOffsetY),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TopPanelContentWidth,
			ProjectKnockoutStruggleWidgetPrivate::TopPanelContentHeight),
		9);

	TopPanelContentBox->SetWidthOverride(ProjectKnockoutStruggleWidgetPrivate::TopPanelContentWidth);
	TopPanelContentBox->SetHeightOverride(ProjectKnockoutStruggleWidgetPrivate::TopPanelContentHeight);
	TopPanelContentBox->SetClipping(EWidgetClipping::ClipToBounds);

	TopPanelTextBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TopPanelTextBox"));
	TopPanelTextBox->SetClipping(EWidgetClipping::ClipToBounds);
	TopPanelContentBox->AddChild(TopPanelTextBox);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	if (UVerticalBoxSlot* TitleSlot = TopPanelTextBox->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f));
	}

	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintText"));
	HintText->SetText(LOCTEXT("StruggleHint", "Press the matching arrow key when the indicator reaches the target."));
	HintText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* HintSlot = TopPanelTextBox->AddChildToVerticalBox(HintText))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetPadding(FMargin(0.f, ProjectKnockoutStruggleWidgetPrivate::TopPanelHintPaddingTop, 0.f, 0.f));
	}

	ProgressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProgressText"));
	if (UVerticalBoxSlot* ProgressSlot = TopPanelTextBox->AddChildToVerticalBox(ProgressText))
	{
		ProgressSlot->SetHorizontalAlignment(HAlign_Center);
		ProgressSlot->SetPadding(FMargin(0.f, ProjectKnockoutStruggleWidgetPrivate::TopPanelProgressPaddingTop, 0.f, 0.f));
	}

	TrackGlowImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TrackGlowImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TrackGlowImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY),
		FVector2D(ProjectKnockoutStruggleWidgetPrivate::TrackGlowWidth, ProjectKnockoutStruggleWidgetPrivate::TrackGlowHeight),
		5);

	TargetChamberImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TargetChamberImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TargetChamberImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetCenterOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetChamberWidth,
			ProjectKnockoutStruggleWidgetPrivate::TargetChamberHeight),
		6);

	TargetPulseImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TargetPulseImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TargetPulseImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetCenterOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetPulseSize,
			ProjectKnockoutStruggleWidgetPrivate::TargetPulseSize),
		7);

	TargetRingImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TargetRingImage"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		TargetRingImage,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetCenterOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::TargetRingSize,
			ProjectKnockoutStruggleWidgetPrivate::TargetRingSize),
		8);

	FeedbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FeedbackText"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		FeedbackText,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.f, 0.5f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::FeedbackOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::FeedbackOffsetY),
		FVector2D::ZeroVector,
		9,
		true);

	MissTimingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MissTimingText"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		MissTimingText,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::MissTimingOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TimingLabelsOffsetY),
		FVector2D::ZeroVector,
		9,
		true);

	GoodLeftTimingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GoodLeftTimingText"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		GoodLeftTimingText,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::GoodLeftTimingOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TimingLabelsOffsetY),
		FVector2D::ZeroVector,
		9,
		true);

	PerfectTimingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PerfectTimingText"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		PerfectTimingText,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::PerfectTimingOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TimingLabelsOffsetY),
		FVector2D::ZeroVector,
		9,
		true);

	GoodRightTimingText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GoodRightTimingText"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		GoodRightTimingText,
		FAnchors(0.5f, 0.5f),
		FVector2D(0.5f, 0.f),
		FVector2D(
			ProjectKnockoutStruggleWidgetPrivate::GoodRightTimingOffsetX,
			ProjectKnockoutStruggleWidgetPrivate::TimingLabelsOffsetY),
		FVector2D::ZeroVector,
		9,
		true);

	NotesLayer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("NotesLayer"));
	ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
		RootCanvas,
		NotesLayer,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		10);
}

void UProjectKnockoutStruggleWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (BackdropBorder)
	{
		BackdropBorder->SetBrushColor(ProjectKnockoutStruggleWidgetPrivate::BackdropColor);
	}

	if (VignetteImage)
	{
		VignetteImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(VignetteTexture, ProjectKnockoutStruggleWidgetPrivate::VignetteTexturePath)), false);
		VignetteImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			VignetteImage,
			FLinearColor(1.f, 1.f, 1.f, 0.96f)));
	}

	if (NoiseImage)
	{
		NoiseImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(NoiseTexture, ProjectKnockoutStruggleWidgetPrivate::NoiseTexturePath)), false);
		NoiseImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			NoiseImage,
			FLinearColor(1.f, 1.f, 1.f, 0.08f)));
	}

	if (TopPanelImage)
	{
		TopPanelImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(TopPanelTexture, ProjectKnockoutStruggleWidgetPrivate::TopPanelTexturePath)), false);
	}

	if (MainPanelImage)
	{
		MainPanelImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(MainPanelTexture, ProjectKnockoutStruggleWidgetPrivate::MainPanelTexturePath)), false);
	}

	if (TrackGlowImage)
	{
		TrackGlowImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(GlowTexture, ProjectKnockoutStruggleWidgetPrivate::GlowTexturePath)), false);
		TrackGlowImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TrackGlowImage,
			ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor().CopyWithNewOpacity(0.42f)));
	}

	if (TargetChamberImage)
	{
		TargetChamberImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(TargetChamberTexture, ProjectKnockoutStruggleWidgetPrivate::ChamberTexturePath)), false);
		TargetChamberImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetChamberImage,
			ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor()));
	}

	if (TargetPulseImage)
	{
		UTexture2D* PulseTexture = ResolveTexture(TargetPulseTexture, ProjectKnockoutStruggleWidgetPrivate::PulseTexturePath);
		if (!PulseTexture)
		{
			PulseTexture = ResolveTexture(TargetRingTexture, ProjectKnockoutStruggleWidgetPrivate::RingTexturePath);
		}

		TargetPulseImage->SetBrushFromTexture(ResolveProjectThemeTexture(PulseTexture), false);
		TargetPulseImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetPulseImage,
			ProjectKnockoutStruggleWidgetPrivate::PulseColor().CopyWithNewOpacity(0.10f)));
		TargetPulseImage->SetRenderOpacity(0.10f);
		TargetPulseImage->SetRenderScale(FVector2D(1.f, 1.f));
	}

	if (TargetRingImage)
	{
		TargetRingImage->SetBrushFromTexture(ResolveProjectThemeTexture(ResolveTexture(TargetRingTexture, ProjectKnockoutStruggleWidgetPrivate::RingTexturePath)), false);
		TargetRingImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetRingImage,
			ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor()));
	}

	if (TitleText)
	{
		TitleText->SetFont(MakeTitleFont(22));
		TitleText->SetJustification(ETextJustify::Center);
		TitleText->SetAutoWrapText(false);
		TitleText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
		TitleText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::HeaderColor()));
		TitleText->SetShadowOffset(FVector2D(0.f, 1.f));
		TitleText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	}

	if (HintText)
	{
		HintText->SetVisibility(ESlateVisibility::Collapsed);
		HintText->SetFont(MakeBodyFont(9, false));
		HintText->SetJustification(ETextJustify::Center);
		HintText->SetAutoWrapText(false);
		HintText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
		HintText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::HintColor().CopyWithNewOpacity(0.82f)));
		HintText->SetShadowOffset(FVector2D(0.f, 1.f));
		HintText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	}

	if (ProgressText)
	{
		ProgressText->SetFont(MakeBodyFont(18, true));
		ProgressText->SetJustification(ETextJustify::Center);
		ProgressText->SetAutoWrapText(false);
		ProgressText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		ProgressText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::ProgressColor()));
		ProgressText->SetShadowOffset(FVector2D(0.f, 1.f));
		ProgressText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	}

	if (FeedbackText)
	{
		FeedbackText->SetFont(MakeBodyFont(18, true));
		FeedbackText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor()));
		FeedbackText->SetShadowOffset(FVector2D(0.f, 1.f));
		FeedbackText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		FeedbackText->SetText(FText::GetEmpty());
	}

	if (MissTimingText)
	{
		MissTimingText->SetText(LOCTEXT("MissTimingLabel", "MISS"));
		MissTimingText->SetFont(MakeBodyFont(11, true));
		MissTimingText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::MissColor()));
	}

	if (GoodLeftTimingText)
	{
		GoodLeftTimingText->SetText(LOCTEXT("GoodLeftTimingLabel", "GOOD"));
		GoodLeftTimingText->SetFont(MakeBodyFont(11, true));
		GoodLeftTimingText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::TimingGoodColor()));
	}

	if (PerfectTimingText)
	{
		PerfectTimingText->SetText(LOCTEXT("PerfectTimingLabel", "PERFECT"));
		PerfectTimingText->SetFont(MakeBodyFont(11, true));
		PerfectTimingText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::TimingPerfectColor()));
	}

	if (GoodRightTimingText)
	{
		GoodRightTimingText->SetText(LOCTEXT("GoodRightTimingLabel", "GOOD"));
		GoodRightTimingText->SetFont(MakeBodyFont(11, true));
		GoodRightTimingText->SetColorAndOpacity(FSlateColor(ProjectKnockoutStruggleWidgetPrivate::TimingGoodColor()));
	}

	bVisualTreeInitialized = true;
}

void UProjectKnockoutStruggleWidget::ClearRoundState()
{
	for (FProjectStruggleNoteRuntime& Note : Notes)
	{
		if (Note.NoteImage)
		{
			Note.NoteImage->RemoveFromParent();
			Note.NoteImage = nullptr;
		}
	}

	Notes.Reset();
	ActiveRound = FProjectStruggleRound();
	RoundStartTimeSeconds = 0.0;
	LastFeedbackRoundSeconds = -1.0;
	ActiveFeedbackText = FText::GetEmpty();
	ActiveFeedbackColorRole = EFeedbackColorRole::Header;
	ActiveFeedbackColor = ProjectKnockoutStruggleWidgetPrivate::HeaderColor();
	MissCount = 0;
	bRoundActive = false;
	bRoundCompleted = false;
	RefreshHeader();
	RefreshLaneVisuals();
}

void UProjectKnockoutStruggleWidget::FocusWidget()
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

void UProjectKnockoutStruggleWidget::GenerateChart(const FProjectStruggleRound& InRound)
{
	UCanvasPanel* NoteCanvas = NotesLayer ? NotesLayer : RootCanvas;
	if (!WidgetTree || !NoteCanvas)
	{
		return;
	}

	UTexture2D* ArrowTextureAsset = ResolveTexture(ArrowTexture, ProjectKnockoutStruggleWidgetPrivate::ArrowTexturePath);
	FRandomStream RandomStream(InRound.ChartSeed == 0 ? 31337 : InRound.ChartSeed);
	float NextHitTimeSeconds = FMath::Max(0.75f, InRound.TravelTimeSeconds * 0.95f);
	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();

	for (int32 NoteIndex = 0; NoteIndex < InRound.NoteCount; ++NoteIndex)
	{
		FProjectStruggleNoteRuntime Note;
		Note.Lane = RandomStream.RandRange(0, 3);
		Note.HitTimeSeconds = NextHitTimeSeconds;
		Note.bResolved = false;
		Note.NoteImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("Note_%d"), NoteIndex));
		Note.NoteImage->SetBrushFromTexture(ResolveProjectThemeTexture(ArrowTextureAsset), false);
		Note.NoteImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		ApplyNoteVisual(Note, ProjectKnockoutStruggleWidgetPrivate::NoteColor(), 0.78f, 0.55f);

		if (UCanvasPanelSlot* NoteSlot = ProjectKnockoutStruggleWidgetPrivate::AddCanvasChild(
			NoteCanvas,
			Note.NoteImage,
			FAnchors(0.5f, 0.5f),
			FVector2D(0.5f, 0.5f),
			FVector2D(
				ProjectKnockoutStruggleWidgetPrivate::TravelStartOffsetX,
				ProjectKnockoutStruggleWidgetPrivate::TrackCenterOffsetY),
			FVector2D(
				ProjectKnockoutStruggleWidgetPrivate::NoteSize,
				ProjectKnockoutStruggleWidgetPrivate::NoteSize),
			10))
		{
			NoteSlot->SetAutoSize(false);
		}

		Notes.Add(Note);

		const float SpacingMinSeconds = InRound.NoteSpacingMinSeconds > KINDA_SMALL_NUMBER
			? InRound.NoteSpacingMinSeconds
			: (Settings ? Settings->StruggleNoteSpacingMinSeconds : 0.32f);
		const float SpacingMaxSeconds = InRound.NoteSpacingMaxSeconds > KINDA_SMALL_NUMBER
			? FMath::Max(InRound.NoteSpacingMaxSeconds, SpacingMinSeconds)
			: (Settings ? Settings->StruggleNoteSpacingMaxSeconds : 0.56f);
		const float SpacingSeconds = RandomStream.FRandRange(SpacingMinSeconds, FMath::Max(SpacingMinSeconds, SpacingMaxSeconds));
		NextHitTimeSeconds += SpacingSeconds;
	}

	ActiveRound.DurationSeconds = NextHitTimeSeconds + InRound.HitWindowSeconds;
}

void UProjectKnockoutStruggleWidget::RefreshHeader()
{
	if (TitleText)
	{
		if (bRoundActive)
		{
			TitleText->SetText(FText::Format(
				LOCTEXT("RoundTitleFormat", "STRUGGLE  |  Round {0}"),
				FText::AsNumber(ActiveRound.RoundIndex + 1)));
		}
		else
		{
			TitleText->SetText(LOCTEXT("IdleTitle", "STRUGGLE"));
		}
	}

	UpdateProgressText();
}

void UProjectKnockoutStruggleWidget::UpdateProgressText()
{
	if (!ProgressText)
	{
		return;
	}

	if (!bRoundActive)
	{
		ProgressText->SetText(FText::GetEmpty());
		return;
	}

	const int32 ResolvedNotes = Algo::CountIf(Notes, [](const FProjectStruggleNoteRuntime& Candidate)
	{
		return Candidate.bResolved;
	});

	ProgressText->SetText(FText::Format(
		LOCTEXT("ProgressWithEnemyFormat", "Enemy {0}  |  Notes {1}/{2}  |  Fails {3}/{4}"),
		FText::FromString(ProjectKnockoutStruggleWidgetPrivate::BuildEnemyDisplayName(ActiveRound.EnemyClassName)),
		FText::AsNumber(ResolvedNotes),
		FText::AsNumber(Notes.Num()),
		FText::AsNumber(MissCount),
		FText::AsNumber(MaxMissCount)));
}

void UProjectKnockoutStruggleWidget::RefreshLaneVisuals()
{
	ActiveFeedbackColor = ResolveActiveFeedbackColor();
	float NearestHitDeltaSeconds = TNumericLimits<float>::Max();
	for (const FProjectStruggleNoteRuntime& Note : Notes)
	{
		if (Note.bResolved)
		{
			continue;
		}

		NearestHitDeltaSeconds = FMath::Min(NearestHitDeltaSeconds, FMath::Abs(Note.HitTimeSeconds - GetElapsedRoundSeconds()));
	}

	const bool bTargetReady = bRoundActive && NearestHitDeltaSeconds <= ActiveRound.HitWindowSeconds;
	const float ReadyAlpha = bTargetReady
		? 1.f - FMath::Clamp(NearestHitDeltaSeconds / FMath::Max(ActiveRound.HitWindowSeconds, 0.001f), 0.f, 1.f)
		: 0.f;

	if (TargetChamberImage)
	{
		TargetChamberImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetChamberImage,
			bTargetReady
				? ProjectKnockoutStruggleWidgetPrivate::TargetReadyColor()
				: ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor()));
	}

	if (TargetRingImage)
	{
		TargetRingImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetRingImage,
			bTargetReady
				? ProjectKnockoutStruggleWidgetPrivate::TargetReadyColor()
				: ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor()));
		TargetRingImage->SetRenderScale(FVector2D(1.f + (ReadyAlpha * 0.08f), 1.f + (ReadyAlpha * 0.08f)));
	}

	if (TargetPulseImage)
	{
		TargetPulseImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TargetPulseImage,
			ProjectKnockoutStruggleWidgetPrivate::PulseColor().CopyWithNewOpacity(0.22f + (ReadyAlpha * 0.62f))));
		TargetPulseImage->SetRenderOpacity(0.18f + (ReadyAlpha * 0.68f));
		TargetPulseImage->SetRenderScale(FVector2D(1.f + (ReadyAlpha * 0.18f), 1.f + (ReadyAlpha * 0.18f)));
	}

	if (TrackGlowImage)
	{
		TrackGlowImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TrackGlowImage,
			(bTargetReady ? ProjectKnockoutStruggleWidgetPrivate::TargetReadyColor() : ProjectKnockoutStruggleWidgetPrivate::TargetIdleColor())
				.CopyWithNewOpacity(0.32f + (ReadyAlpha * 0.50f))));
	}

	if (!FeedbackText)
	{
		return;
	}

	if (!bRoundActive || LastFeedbackRoundSeconds < 0.0 || ActiveFeedbackText.IsEmpty())
	{
		FeedbackText->SetText(FText::GetEmpty());
		return;
	}

	const float FeedbackAgeSeconds = FMath::Max(0.f, GetElapsedRoundSeconds() - static_cast<float>(LastFeedbackRoundSeconds));
	const float FeedbackAlpha = 1.f - FMath::Clamp(FeedbackAgeSeconds / 0.42f, 0.f, 1.f);
	if (FeedbackAlpha <= KINDA_SMALL_NUMBER)
	{
		FeedbackText->SetText(FText::GetEmpty());
		return;
	}

	FeedbackText->SetText(ActiveFeedbackText);
	FeedbackText->SetColorAndOpacity(FSlateColor(ActiveFeedbackColor.CopyWithNewOpacity(FeedbackAlpha)));
}

void UProjectKnockoutStruggleWidget::CompleteRound(
	const bool bSuccess,
	const bool bPlayerCompletedMinigame)
{
	if (bRoundCompleted)
	{
		return;
	}

	bRoundCompleted = true;
	bRoundActive = false;
	RefreshLaneVisuals();
	OnRoundCompleted.Broadcast(bSuccess, bPlayerCompletedMinigame, ActiveRound);
}

void UProjectKnockoutStruggleWidget::SetFeedbackState(const FText& FeedbackLabel, const FLinearColor& FeedbackColor)
{
	ActiveFeedbackText = FeedbackLabel;
	ActiveFeedbackColorRole = ClassifyFeedbackColor(FeedbackColor);
	ActiveFeedbackColor = ResolveActiveFeedbackColor();
	LastFeedbackRoundSeconds = GetElapsedRoundSeconds();
	RefreshLaneVisuals();
}

UProjectKnockoutStruggleWidget::EFeedbackColorRole
UProjectKnockoutStruggleWidget::ClassifyFeedbackColor(
	const FLinearColor& FeedbackColor) const
{
	if (FeedbackColor.Equals(
		ProjectKnockoutStruggleWidgetPrivate::PerfectColor(),
		0.001f))
	{
		return EFeedbackColorRole::Positive;
	}
	if (FeedbackColor.Equals(
		ProjectKnockoutStruggleWidgetPrivate::MissColor(),
		0.001f))
	{
		return EFeedbackColorRole::Negative;
	}
	if (FeedbackColor.Equals(
		ProjectKnockoutStruggleWidgetPrivate::GoodColor(),
		0.001f))
	{
		return EFeedbackColorRole::Good;
	}
	return EFeedbackColorRole::Header;
}

FLinearColor UProjectKnockoutStruggleWidget::ResolveActiveFeedbackColor() const
{
	switch (ActiveFeedbackColorRole)
	{
	case EFeedbackColorRole::Good:
		return ProjectKnockoutStruggleWidgetPrivate::GoodColor();
	case EFeedbackColorRole::Positive:
		return ProjectKnockoutStruggleWidgetPrivate::PerfectColor();
	case EFeedbackColorRole::Negative:
		return ProjectKnockoutStruggleWidgetPrivate::MissColor();
	default:
		return ProjectKnockoutStruggleWidgetPrivate::HeaderColor();
	}
}

void UProjectKnockoutStruggleWidget::ApplyNoteVisual(
	FProjectStruggleNoteRuntime& Note,
	const FLinearColor& Tint,
	const float Scale,
	const float Opacity) const
{
	if (!Note.NoteImage)
	{
		return;
	}

	FWidgetTransform NoteRenderTransform = Note.NoteImage->GetRenderTransform();
	NoteRenderTransform.Translation = FVector2D::ZeroVector;
	NoteRenderTransform.Scale = FVector2D(Scale, Scale);
	NoteRenderTransform.Shear = FVector2D::ZeroVector;
	NoteRenderTransform.Angle = ResolveLaneAngleDegrees(Note.Lane);
	Note.NoteImage->SetRenderTransform(NoteRenderTransform);
	Note.NoteImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Note.NoteImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
		Note.NoteImage,
		Tint.CopyWithNewOpacity(Opacity)));
	Note.NoteImage->SetVisibility(ESlateVisibility::HitTestInvisible);
}

UTexture2D* UProjectKnockoutStruggleWidget::ResolveTexture(
	const TSoftObjectPtr<UTexture2D>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.Get())
		{
			return LoadedTexture;
		}

		if (UTexture2D* ResolvedTexture = Cast<UTexture2D>(AssetPtr.ToSoftObjectPath().ResolveObject()))
		{
			return ResolvedTexture;
		}

		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectKnockoutStruggleWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectKnockoutStruggleWidget::ResolveStyleAsset(
	const TSoftObjectPtr<UObject>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedAsset = AssetPtr.Get())
		{
			return LoadedAsset;
		}

		if (UObject* ResolvedAsset = AssetPtr.ToSoftObjectPath().ResolveObject())
		{
			return ResolvedAsset;
		}

		if (UObject* LoadedAsset = AssetPtr.LoadSynchronous())
		{
			return LoadedAsset;
		}
	}

	return ProjectKnockoutStruggleWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectKnockoutStruggleWidget::MakeTitleFont(const int32 Size) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectKnockoutStruggleWidgetPrivate::CinzelFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}
	FontInfo.LetterSpacing = 160;
	return FontInfo;
}

FSlateFontInfo UProjectKnockoutStruggleWidget::MakeBodyFont(const int32 Size, const bool bUseMediumWeight) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(bUseMediumWeight ? TEXT("Bold") : TEXT("Regular"), Size);
	FontInfo.Size = Size;
	if (UObject* FontObject = ResolveStyleAsset(
		bUseMediumWeight ? BodyMediumFontAsset : BodyFontAsset,
		bUseMediumWeight
			? ProjectKnockoutStruggleWidgetPrivate::BarlowMediumFontPath
			: ProjectKnockoutStruggleWidgetPrivate::BarlowFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}
	return FontInfo;
}

float UProjectKnockoutStruggleWidget::ResolveLaneAngleDegrees(const int32 LaneIndex) const
{
	switch (LaneIndex)
	{
	case 0:
		return -90.f;
	case 1:
		return 0.f;
	case 2:
		return 90.f;
	case 3:
	default:
		return 180.f;
	}
}

bool UProjectKnockoutStruggleWidget::IsPerfectHit(const float DeltaSeconds) const
{
	return DeltaSeconds <= (FMath::Max(ActiveRound.HitWindowSeconds, 0.001f) * ProjectKnockoutStruggleWidgetPrivate::PerfectThresholdRatio);
}

bool UProjectKnockoutStruggleWidget::RegisterWrongInputMiss()
{
	const bool bHasRemainingNotes = Notes.FindByPredicate([](const FProjectStruggleNoteRuntime& Candidate)
	{
		return !Candidate.bResolved;
	}) != nullptr;
	if (!bHasRemainingNotes)
	{
		return false;
	}

	MissCount = FMath::Min(MissCount + 1, MaxMissCount);
	UpdateProgressText();
	RefreshLaneVisuals();

	if (MissCount >= MaxMissCount)
	{
		CompleteRound(false);
		return true;
	}

	return false;
}

bool UProjectKnockoutStruggleWidget::RegisterMissedNote(const int32 NoteIndex)
{
	if (!Notes.IsValidIndex(NoteIndex))
	{
		return false;
	}

	FProjectStruggleNoteRuntime& Note = Notes[NoteIndex];
	if (Note.bResolved)
	{
		return false;
	}

	Note.bResolved = true;
	if (Note.NoteImage)
	{
		Note.NoteImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			Note.NoteImage,
			ProjectKnockoutStruggleWidgetPrivate::MissColor()));
		Note.NoteImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	MissCount = FMath::Min(MissCount + 1, MaxMissCount);
	SetFeedbackState(LOCTEXT("MissFeedback", "MISS"), ProjectKnockoutStruggleWidgetPrivate::MissColor());
	UpdateProgressText();

	if (MissCount >= MaxMissCount)
	{
		CompleteRound(false);
		return true;
	}

	return false;
}

int32 UProjectKnockoutStruggleWidget::ResolveLaneFromKey(const FKey& Key) const
{
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		return 0;
	}

	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		return 1;
	}

	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		return 2;
	}

	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		return 3;
	}

	return INDEX_NONE;
}

bool UProjectKnockoutStruggleWidget::TryResolveHitNote(
	const int32 LaneIndex,
	int32& OutNoteIndex,
	float& OutDeltaSeconds) const
{
	OutNoteIndex = INDEX_NONE;
	OutDeltaSeconds = TNumericLimits<float>::Max();

	const float ElapsedSeconds = GetElapsedRoundSeconds();
	for (int32 NoteIndex = 0; NoteIndex < Notes.Num(); ++NoteIndex)
	{
		const FProjectStruggleNoteRuntime& Note = Notes[NoteIndex];
		if (Note.bResolved || Note.Lane != LaneIndex)
		{
			continue;
		}

		const float DeltaSeconds = FMath::Abs(Note.HitTimeSeconds - ElapsedSeconds);
		if (DeltaSeconds <= ActiveRound.HitWindowSeconds && DeltaSeconds < OutDeltaSeconds)
		{
			OutDeltaSeconds = DeltaSeconds;
			OutNoteIndex = NoteIndex;
		}
	}

	return OutNoteIndex != INDEX_NONE;
}

float UProjectKnockoutStruggleWidget::GetElapsedRoundSeconds() const
{
	return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - RoundStartTimeSeconds) : 0.f;
}

#undef LOCTEXT_NAMESPACE
