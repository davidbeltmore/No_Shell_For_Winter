#include "UI/ProjectActivityFeedWidget.h"

#include "EFProjectUIPalette.h"
#include "EFProjectUISettings.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "UI/ProjectActivityFeedSettings.h"
#include "UI/ProjectChronicleEmptyStateWidget.h"
#include "UI/ProjectChroniclePanelWidget.h"
#include "UI/ProjectWidgetClassResolver.h"

namespace ProjectActivityFeedWidgetPrivate
{
	constexpr float CompactWidth = 560.0f;
	constexpr float CompactHeight = 260.0f;
	constexpr float ExpandedWidth = 620.0f;
	constexpr float ExpandedHeight = 470.0f;
	constexpr int32 CompactVisibleEntries = 6;
	constexpr int32 ExpandedViewportVisibleEntries = 9;
	constexpr float PanelCornerRadius = 12.0f;
	constexpr float PanelOutlineWidth = 1.2f;
	constexpr float PanelPaddingX = 20.0f;
	constexpr float PanelPaddingY = 19.0f;
	constexpr float RuntimeFallbackSortBase = 1000.0f;

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
	FLinearColor EmptyTextTint()
	{
		return EFProjectUIPalette::SecondaryText(0.92f);
	}
	FLinearColor EmptyBoxTint()
	{
		return EFProjectUIPalette::PanelFill(0.84f);
	}
	FLinearColor EmptyBoxOutlineTint()
	{
		return EFProjectUIPalette::OutlineDim(0.28f);
	}
	FLinearColor FallbackAccentTint()
	{
		return EFProjectUIPalette::Accent();
	}
	FLinearColor FallbackBadgeFillTint()
	{
		return EFProjectUIPalette::BadgeFill(0.98f);
	}
	FLinearColor FallbackBadgeTextTint()
	{
		return EFProjectUIPalette::BadgeText();
	}

	const TCHAR* TitleFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Bebas.F_Chronicle_Bebas");
	const TCHAR* BodyFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Cormorant.F_Chronicle_Cormorant");
	const TCHAR* BodyItalicFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_CormorantItalic.F_Chronicle_CormorantItalic");
	const TCHAR* FrameTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame.T_Chronicle_Frame");
	const TCHAR* HazeTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Haze.T_Chronicle_Haze");
	const TCHAR* DividerTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Divider.T_Chronicle_Divider");
	const TCHAR* HudPillTexturePath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_HudPill.T_Chronicle_HudPill");

	struct FResolvedChronicleChannelStyle
	{
		FString DisplayLabel;
		FLinearColor AccentTint = ProjectActivityFeedWidgetPrivate::FallbackAccentTint();
		FLinearColor BadgeFillTint = ProjectActivityFeedWidgetPrivate::FallbackBadgeFillTint();
		FLinearColor BadgeTextTint = ProjectActivityFeedWidgetPrivate::FallbackBadgeTextTint();
	};

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
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FName BuildChannelName(const EProjectActivityFeedChannel Channel)
	{
		if (const UEnum* ChannelEnum = StaticEnum<EProjectActivityFeedChannel>())
		{
			return FName(*ChannelEnum->GetNameStringByValue(static_cast<int64>(Channel)));
		}

		return NAME_None;
	}

	FString HumanizeChannelName(const FName ChannelName)
	{
		FString Result = ChannelName.ToString();
		Result.ReplaceInline(TEXT("_"), TEXT(" "));
		Result.ToUpperInline();
		if (Result.IsEmpty())
		{
			Result = TEXT("LOG");
		}
		return Result;
	}

	FString BuildFallbackBadgeLabel(const FProjectActivityFeedEntry& Entry)
	{
		if (!Entry.BadgeLabelOverride.IsEmpty())
		{
			return Entry.BadgeLabelOverride;
		}

		return HumanizeChannelName(BuildChannelName(Entry.Channel));
	}

	FText BuildFallbackMessageText(const FProjectActivityFeedEntry& Entry)
	{
		if (!Entry.Message.IsEmpty())
		{
			return Entry.Message;
		}

		if (!Entry.PrimaryText.IsEmpty() && !Entry.SecondaryText.IsEmpty())
		{
			return FText::Format(FText::FromString(TEXT("{0} {1}")), Entry.PrimaryText, Entry.SecondaryText);
		}

		if (!Entry.PrimaryText.IsEmpty())
		{
			return Entry.PrimaryText;
		}

		return Entry.SecondaryText;
	}

	FResolvedChronicleChannelStyle ResolveChannelStyle(const FProjectActivityFeedEntry& Entry)
	{
		FResolvedChronicleChannelStyle Style;
		Style.DisplayLabel = BuildFallbackBadgeLabel(Entry);

		const FName ChannelName = BuildChannelName(Entry.Channel);
		Style.AccentTint = EFProjectUIPalette::ChronicleAccentForChannel(ChannelName);
		Style.BadgeFillTint = EFProjectUIPalette::ChronicleBadgeFillForChannel(ChannelName);
		Style.BadgeTextTint = EFProjectUIPalette::BadgeText();
		if (const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get())
		{
			for (const FProjectChronicleChannelDefinition& Definition : UISettings->ChronicleChannelDefinitions)
			{
				if (Definition.ChannelName == ChannelName)
				{
					Style.DisplayLabel = Entry.BadgeLabelOverride.IsEmpty() ? Definition.DisplayLabel : Entry.BadgeLabelOverride;
					break;
				}
			}
		}

		if (Entry.AccentTintOverride.A > KINDA_SMALL_NUMBER)
		{
			Style.AccentTint = Entry.AccentTintOverride;
		}

		return Style;
	}

	int32 ResolveViewportEntryCount(const bool bExpanded)
	{
		const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
		const int32 Count = Settings
			? (bExpanded ? Settings->ExpandedViewportVisibleEntries : Settings->CompactVisibleEntries)
			: (bExpanded ? ExpandedViewportVisibleEntries : CompactVisibleEntries);
		return FMath::Max(Count, 1);
	}

	float ResolveEntriesAreaHeight(const bool bExpanded, const float MinimumRowHeight, const float RowGapOverride)
	{
		const int32 VisibleRows = ResolveViewportEntryCount(bExpanded);
		const float RowHeight = FMath::Max(MinimumRowHeight, bExpanded ? 18.0f : 12.0f);
		const float Gap = FMath::Max(RowGapOverride, 0.0f);
		return RowHeight * VisibleRows + Gap * FMath::Max(VisibleRows - 1, 0);
	}

	bool AreFeedEntriesEquivalent(
		const TArray<FProjectActivityFeedEntry>& Left,
		const TArray<FProjectActivityFeedEntry>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			const FProjectActivityFeedEntry& LeftEntry = Left[Index];
			const FProjectActivityFeedEntry& RightEntry = Right[Index];
			if (LeftEntry.Channel != RightEntry.Channel
				|| LeftEntry.Sequence != RightEntry.Sequence
				|| LeftEntry.RenderStyle != RightEntry.RenderStyle
				|| LeftEntry.BadgeLabelOverride != RightEntry.BadgeLabelOverride
				|| !LeftEntry.Message.EqualTo(RightEntry.Message)
				|| !LeftEntry.PrimaryText.EqualTo(RightEntry.PrimaryText)
				|| !LeftEntry.SecondaryText.EqualTo(RightEntry.SecondaryText)
				|| LeftEntry.AccentTintOverride != RightEntry.AccentTintOverride)
			{
				return false;
			}
		}

		return true;
	}
}

UProjectActivityFeedWidget::UProjectActivityFeedWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ZOrder = 170;
	TitleFontAsset = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::TitleFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::BodyFontPath);
	BodyItalicFontAsset = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::BodyItalicFontPath);
	FrameTexture = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::FrameTexturePath);
	HazeTexture = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::HazeTexturePath);
	DividerTexture = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::DividerTexturePath);
	HudPillTexture = FSoftObjectPath(ProjectActivityFeedWidgetPrivate::HudPillTexturePath);
	CurrentPanelSize = FVector2D(ProjectActivityFeedWidgetPrivate::CompactWidth, ProjectActivityFeedWidgetPrivate::CompactHeight);
}

void UProjectActivityFeedWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyCachedState();
}

void UProjectActivityFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyHudVisibility();
	ApplyCachedState();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectActivityFeedWidget::NativeDestruct()
{
	VisibleRowWidgets.Reset();
	VisibleEmptyStateWidget = nullptr;
	NormalPanelWidget = nullptr;
	ExpandedPanelWidget = nullptr;
	ActivePanelWidget = nullptr;
	Super::NativeDestruct();
}

bool UProjectActivityFeedWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	CurrentPanelSize = FVector2D(ProjectActivityFeedWidgetPrivate::CompactWidth, ProjectActivityFeedWidgetPrivate::CompactHeight);
	bExpanded = false;
	bHudVisible = true;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	WidgetTree = TargetWidgetTree;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		UpdatePanelLayout();
	}

	return bBuiltTree;
}

void UProjectActivityFeedWidget::GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	const auto AddSpec = [&OutWidgetSpecs](
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& RelativeFolder,
		const FString& AssetNameOverride,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const bool bRuntimeDefault = false,
		TArray<FName> ExpectedWidgetNames = TArray<FName>())
	{
		FCodeWidgetDesignerChildWidgetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.RelativeFolder = RelativeFolder;
		Spec.AssetNameOverride = AssetNameOverride;
		Spec.Role = Role;
		Spec.PriorityGroup = FName(TEXT("Chronicle"));
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.ExpectedWidgetNames = MoveTemp(ExpectedWidgetNames);
		OutWidgetSpecs.Add(Spec);
	};

	AddSpec(UProjectChronicleNormalGlobalWidget::StaticClass(), TEXT("Globals"), TEXT("WBP_ProjectChronicleNormalGlobal"), ECodeWidgetDesignerAssetRole::GlobalPanel, 9000, true, { TEXT("EntriesBox") });
	AddSpec(UProjectChronicleExpandedGlobalWidget::StaticClass(), TEXT("Globals"), TEXT("WBP_ProjectChronicleExpandedGlobal"), ECodeWidgetDesignerAssetRole::GlobalPanel, 9000, true, { TEXT("EntriesBox") });
	AddSpec(UProjectActivityFeedEntryRowWidget::StaticClass(), TEXT("Main"), TEXT("WBP_ProjectChronicleEntryRow"), ECodeWidgetDesignerAssetRole::GlobalTemplate, 7000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleEmptyStateWidget::StaticClass(), TEXT("Main"), TEXT("WBP_ProjectChronicleEmptyState"), ECodeWidgetDesignerAssetRole::GlobalTemplate, 7000, false, { TEXT("MessageText") });

	AddSpec(UProjectChronicleNormalStandardRowWidget::StaticClass(), TEXT("Normal"), TEXT("WBP_ProjectChronicleNormalStandardRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleNormalGainRowWidget::StaticClass(), TEXT("Normal"), TEXT("WBP_ProjectChronicleNormalGainRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleNormalDialogueQuoteRowWidget::StaticClass(), TEXT("Normal"), TEXT("WBP_ProjectChronicleNormalDialogueQuoteRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleNormalEmptyStateWidget::StaticClass(), TEXT("Normal"), TEXT("WBP_ProjectChronicleNormalEmptyState"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("MessageText") });

	AddSpec(UProjectChronicleExpandedStandardRowWidget::StaticClass(), TEXT("Expanded"), TEXT("WBP_ProjectChronicleExpandedStandardRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleExpandedGainRowWidget::StaticClass(), TEXT("Expanded"), TEXT("WBP_ProjectChronicleExpandedGainRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleExpandedDialogueQuoteRowWidget::StaticClass(), TEXT("Expanded"), TEXT("WBP_ProjectChronicleExpandedDialogueQuoteRow"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("RootSizeBox"), TEXT("BadgeText"), TEXT("MessageText") });
	AddSpec(UProjectChronicleExpandedEmptyStateWidget::StaticClass(), TEXT("Expanded"), TEXT("WBP_ProjectChronicleExpandedEmptyState"), ECodeWidgetDesignerAssetRole::Individual, 5000, false, { TEXT("MessageText") });
}

void UProjectActivityFeedWidget::SetFeedEntries(const TArray<FProjectActivityFeedEntry>& InEntries)
{
	if (ProjectActivityFeedWidgetPrivate::AreFeedEntriesEquivalent(CachedEntries, InEntries))
	{
		return;
	}

	CachedEntries = InEntries;
	ApplyCachedState();
}

void UProjectActivityFeedWidget::SetExpanded(const bool bInExpanded)
{
	if (bExpanded == bInExpanded)
	{
		return;
	}

	bExpanded = bInExpanded;
	ApplyCachedState();
	OnChronicleExpandedChanged(bExpanded);
}

void UProjectActivityFeedWidget::SetHudVisible(const bool bVisible)
{
	if (bHudVisible == bVisible)
	{
		return;
	}

	bHudVisible = bVisible;
	ApplyHudVisibility();

	if (bHudVisible)
	{
		ApplyCachedState();
	}
}

void UProjectActivityFeedWidget::SetChronicleLayoutPolicy(const FProjectChronicleLayoutPolicy& InLayoutPolicy)
{
	CurrentLayoutPolicy = InLayoutPolicy;
	CurrentLayoutPolicy.MinimumRowHeight = FMath::Max(CurrentLayoutPolicy.MinimumRowHeight, 12.0f);
	CurrentLayoutPolicy.MaximumTextWidth = FMath::Max(CurrentLayoutPolicy.MaximumTextWidth, 64.0f);
	CurrentLayoutPolicy.RowGap = FMath::Max(CurrentLayoutPolicy.RowGap, 0.0f);
	CurrentLayoutPolicy.InlinePrimaryWidthRatio = FMath::Clamp(CurrentLayoutPolicy.InlinePrimaryWidthRatio, 0.1f, 0.9f);
	CurrentLayoutPolicy.LineHeightPercentage = FMath::Clamp(CurrentLayoutPolicy.LineHeightPercentage, 1.0f, 2.0f);
}

bool UProjectActivityFeedWidget::IsExpanded() const
{
	return bExpanded;
}

bool UProjectActivityFeedWidget::IsHudVisible() const
{
	return bHudVisible;
}

void UProjectActivityFeedWidget::ScrollHistoryByEntries(const int32 Direction)
{
	if (!bExpanded || !bHudVisible || !EntriesScrollBox || Direction == 0 || VisibleRowWidgets.IsEmpty())
	{
		return;
	}

	if (!VisibleRowWidgets.IsValidIndex(ScrollTargetRowIndex))
	{
		ScrollTargetRowIndex = VisibleRowWidgets.Num() - 1;
	}

	ScrollTargetRowIndex = FMath::Clamp(
		ScrollTargetRowIndex + (Direction > 0 ? 1 : -1),
		0,
		VisibleRowWidgets.Num() - 1);
	EntriesScrollBox->ScrollWidgetIntoView(
		VisibleRowWidgets[ScrollTargetRowIndex].Get(),
		true,
		EDescendantScrollDestination::IntoView,
		CurrentLayoutPolicy.RowGap);
}

void UProjectActivityFeedWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		if (!FrameCanvasSlot && FrameSizeBox)
		{
			FrameCanvasSlot = Cast<UCanvasPanelSlot>(FrameSizeBox->Slot);
		}
		EnsurePanelHost();
		EnsurePanelWidgets();
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
	EnsurePanelHost();
	EnsurePanelWidgets();
}

bool UProjectActivityFeedWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	FrameSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FrameSizeBox"));
	FrameSizeBox->SetWidthOverride(CurrentPanelSize.X);
	FrameSizeBox->SetHeightOverride(CurrentPanelSize.Y);
	FrameCanvasSlot = ProjectActivityFeedWidgetPrivate::AddCanvasChild(
		RootCanvas,
		FrameSizeBox,
		FAnchors(1.0f, 0.0f),
		FVector2D(1.0f, 0.0f),
		FVector2D(-16.0f, 240.0f),
		CurrentPanelSize,
		ZOrder);

	FrameOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FrameOverlay"));
	FrameSizeBox->AddChild(FrameOverlay);

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
		HazeSlot->SetPadding(FMargin(6.0f, 6.0f, 6.0f, 6.0f));
	}

	FrameTextureImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameTextureImage"));
	if (UOverlaySlot* FrameTextureSlot = FrameOverlay->AddChildToOverlay(FrameTextureImage))
	{
		FrameTextureSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameTextureSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(
		ProjectActivityFeedWidgetPrivate::PanelPaddingX,
		ProjectActivityFeedWidgetPrivate::PanelPaddingY,
		ProjectActivityFeedWidgetPrivate::PanelPaddingX,
		ProjectActivityFeedWidgetPrivate::PanelPaddingY));
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
	EntriesScrollSizeBox->SetHeightOverride(ProjectActivityFeedWidgetPrivate::ResolveEntriesAreaHeight(
		false,
		CurrentLayoutPolicy.MinimumRowHeight,
		CurrentLayoutPolicy.RowGap));
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

void UProjectActivityFeedWidget::InitializeVisualTree()
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
			ProjectActivityFeedWidgetPrivate::PanelFillTint(),
			ProjectActivityFeedWidgetPrivate::PanelCornerRadius,
			FSlateColor(ProjectActivityFeedWidgetPrivate::PanelOutlineTint()),
			ProjectActivityFeedWidgetPrivate::PanelOutlineWidth,
			FVector2f(CurrentPanelSize.X, CurrentPanelSize.Y));
		BackgroundBorder->SetBrush(BackgroundBrush);
	}

	if (FrameTextureImage)
	{
		FrameTextureImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(FrameTexture, ProjectActivityFeedWidgetPrivate::FrameTexturePath)),
			false);
		FrameTextureImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FrameTextureImage,
			ProjectActivityFeedWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.84f)));
	}

	if (HazeImage)
	{
		HazeImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HazeTexture, ProjectActivityFeedWidgetPrivate::HazeTexturePath)),
			false);
		HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HazeImage,
			ProjectActivityFeedWidgetPrivate::HazeTint()));
	}

	if (TitleText)
	{
		TitleText->SetFont(MakeTitleFont(28, 0));
		TitleText->SetColorAndOpacity(FSlateColor(ProjectActivityFeedWidgetPrivate::TitleTint()));
		TitleText->SetShadowOffset(FVector2D(0.0f, 0.45f));
		TitleText->SetShadowColorAndOpacity(ProjectActivityFeedWidgetPrivate::ShadowTint);
	}

	if (HudPillImage)
	{
		HudPillImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HudPillTexture, ProjectActivityFeedWidgetPrivate::HudPillTexturePath)),
			false);
		HudPillImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HudPillImage,
			ProjectActivityFeedWidgetPrivate::PanelOutlineTint().CopyWithNewOpacity(0.92f)));
	}

	if (HintText)
	{
		HintText->SetFont(MakeTitleFont(11, 0));
		HintText->SetColorAndOpacity(FSlateColor(ProjectActivityFeedWidgetPrivate::HintTint()));
		HintText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		HintText->SetShadowColorAndOpacity(ProjectActivityFeedWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.18f));
	}

	if (TopDividerImage)
	{
		TopDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectActivityFeedWidgetPrivate::DividerTexturePath)),
			false);
		TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TopDividerImage,
			ProjectActivityFeedWidgetPrivate::TitleTint().CopyWithNewOpacity(0.92f)));
	}

	if (FooterDividerImage)
	{
		FooterDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectActivityFeedWidgetPrivate::DividerTexturePath)),
			false);
		FooterDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FooterDividerImage,
			ProjectActivityFeedWidgetPrivate::TitleTint().CopyWithNewOpacity(0.86f)));
	}

	bVisualTreeInitialized = true;
}

void UProjectActivityFeedWidget::ApplyCachedState()
{
	BuildWidgetTree();
	InitializeVisualTree();
	UpdatePanelLayout();
	RebuildVisibleRows();
}

void UProjectActivityFeedWidget::ApplyHudVisibility()
{
	SetVisibility(bHudVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UProjectActivityFeedWidget::EnsurePanelHost()
{
	if (!RootCanvas || !WidgetTree)
	{
		return;
	}

	if (!PanelHost)
	{
		PanelHost = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelHost"));
		if (PanelHost)
		{
			if (UCanvasPanelSlot* PanelHostSlot = RootCanvas->AddChildToCanvas(PanelHost))
			{
				if (FrameCanvasSlot)
				{
					PanelHostSlot->SetAnchors(FrameCanvasSlot->GetAnchors());
					PanelHostSlot->SetAlignment(FrameCanvasSlot->GetAlignment());
					PanelHostSlot->SetPosition(FrameCanvasSlot->GetPosition());
					PanelHostSlot->SetZOrder(FrameCanvasSlot->GetZOrder());
					PanelHostSlot->SetSize(CurrentPanelSize);
				}
				else
				{
					const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
					const FVector2D HudMargin = Settings ? Settings->HudMargin : FVector2D(16.0f, 240.0f);
					PanelHostSlot->SetAnchors(FAnchors(1.0f, 0.0f));
					PanelHostSlot->SetAlignment(FVector2D(1.0f, 0.0f));
					PanelHostSlot->SetPosition(FVector2D(-HudMargin.X, HudMargin.Y));
					PanelHostSlot->SetZOrder(ZOrder);
					PanelHostSlot->SetSize(CurrentPanelSize);
				}
			}
		}
	}

	if (FrameSizeBox && PanelHost)
	{
		FrameSizeBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UProjectActivityFeedWidget::EnsurePanelWidgets()
{
	if (!PanelHost)
	{
		return;
	}

	const auto AddPanelToHost = [this](UProjectChroniclePanelWidget* PanelWidget)
	{
		if (!PanelWidget || PanelWidget->GetParent())
		{
			return;
		}

		if (UOverlaySlot* PanelSlot = PanelHost->AddChildToOverlay(PanelWidget))
		{
			PanelSlot->SetHorizontalAlignment(HAlign_Fill);
			PanelSlot->SetVerticalAlignment(VAlign_Fill);
		}
	};

	if (!NormalPanelWidget)
	{
		const TSubclassOf<UProjectChroniclePanelWidget> NormalPanelClass = ResolvePanelWidgetClass(false);
		NormalPanelWidget = CreateWidget<UProjectChroniclePanelWidget>(
			this,
			NormalPanelClass ? NormalPanelClass.Get() : UProjectChronicleNormalGlobalWidget::StaticClass());
		AddPanelToHost(NormalPanelWidget);
	}

	if (!ExpandedPanelWidget)
	{
		const TSubclassOf<UProjectChroniclePanelWidget> ExpandedPanelClass = ResolvePanelWidgetClass(true);
		ExpandedPanelWidget = CreateWidget<UProjectChroniclePanelWidget>(
			this,
			ExpandedPanelClass ? ExpandedPanelClass.Get() : UProjectChronicleExpandedGlobalWidget::StaticClass());
		AddPanelToHost(ExpandedPanelWidget);
	}
}

void UProjectActivityFeedWidget::SyncActivePanelWidget()
{
	ActivePanelWidget = bExpanded ? ExpandedPanelWidget : NormalPanelWidget;

	if (NormalPanelWidget)
	{
		NormalPanelWidget->SetVisibility(!bExpanded ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (ExpandedPanelWidget)
	{
		ExpandedPanelWidget->SetVisibility(bExpanded ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (ActivePanelWidget)
	{
		EntriesBox = ActivePanelWidget->GetEntriesBox();
		EntriesScrollBox = ActivePanelWidget->GetEntriesScrollBox();
	}
}

void UProjectActivityFeedWidget::UpdatePanelLayout()
{
	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const FVector2D PanelSize = Settings
		? (bExpanded ? Settings->ExpandedPanelSize : Settings->CompactPanelSize)
		: FVector2D(
			bExpanded ? ProjectActivityFeedWidgetPrivate::ExpandedWidth : ProjectActivityFeedWidgetPrivate::CompactWidth,
			bExpanded ? ProjectActivityFeedWidgetPrivate::ExpandedHeight : ProjectActivityFeedWidgetPrivate::CompactHeight);
	const FVector2D HudMargin = Settings ? Settings->HudMargin : FVector2D(16.0f, 240.0f);
	const float EntriesHeight = ProjectActivityFeedWidgetPrivate::ResolveEntriesAreaHeight(
		bExpanded,
		CurrentLayoutPolicy.MinimumRowHeight,
		CurrentLayoutPolicy.RowGap);

	CurrentPanelSize = PanelSize;

	EnsurePanelHost();
	EnsurePanelWidgets();
	SyncActivePanelWidget();

	if (PanelHost)
	{
		if (UCanvasPanelSlot* PanelHostSlot = Cast<UCanvasPanelSlot>(PanelHost->Slot))
		{
			PanelHostSlot->SetSize(PanelSize);
		}
	}

	if (ActivePanelWidget)
	{
		ActivePanelWidget->ApplyPanelState(bExpanded, PanelSize, EntriesHeight);
		EntriesBox = ActivePanelWidget->GetEntriesBox();
		EntriesScrollBox = ActivePanelWidget->GetEntriesScrollBox();
		return;
	}

	if (FrameSizeBox)
	{
		FrameSizeBox->SetWidthOverride(PanelSize.X);
		FrameSizeBox->SetHeightOverride(PanelSize.Y);
	}

	if (FrameCanvasSlot)
	{
		FrameCanvasSlot->SetSize(PanelSize);
		if (bUsingNativeFallbackTree)
		{
			FrameCanvasSlot->SetPosition(FVector2D(-HudMargin.X, HudMargin.Y));
		}
	}

	if (EntriesScrollSizeBox)
	{
		EntriesScrollSizeBox->SetWidthOverride(PanelSize.X - 40.0f);
		EntriesScrollSizeBox->SetHeightOverride(EntriesHeight);
	}

	if (BackgroundBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush BackgroundBrush(
			ProjectActivityFeedWidgetPrivate::PanelFillTint(),
			ProjectActivityFeedWidgetPrivate::PanelCornerRadius,
			FSlateColor(ProjectActivityFeedWidgetPrivate::PanelOutlineTint()),
			ProjectActivityFeedWidgetPrivate::PanelOutlineWidth,
			FVector2f(PanelSize.X, PanelSize.Y));
		BackgroundBorder->SetBrush(BackgroundBrush);
	}

	if (HintText)
	{
		HintText->SetText(FText::FromString(bExpanded ? TEXT("J  COLLAPSE") : TEXT("J  EXPAND")));
	}
}

void UProjectActivityFeedWidget::RebuildVisibleRows()
{
	SyncActivePanelWidget();

	if (!EntriesBox || !EntriesScrollBox)
	{
		return;
	}

	EntriesBox->ClearChildren();
	VisibleRowWidgets.Reset();
	VisibleEmptyStateWidget = nullptr;

	const TArray<FProjectActivityFeedRowDisplayData> ResolvedRows = BuildResolvedRowData();
	if (ResolvedRows.IsEmpty())
	{
		const TSubclassOf<UProjectChronicleEmptyStateWidget> EmptyStateClass = ResolveEmptyStateWidgetClass();
		UProjectChronicleEmptyStateWidget* EmptyStateWidget = CreateWidget<UProjectChronicleEmptyStateWidget>(
			this,
			EmptyStateClass ? EmptyStateClass.Get() : UProjectChronicleEmptyStateWidget::StaticClass());
		if (EmptyStateWidget)
		{
			const FText EmptyMessage = FText::FromString(TEXT("No events yet. Your journey will be recorded here."));
			EmptyStateWidget->ApplyEmptyState(bExpanded, EmptyMessage, CurrentPanelSize.X - 40.0f);
			VisibleEmptyStateWidget = EmptyStateWidget;

			if (UVerticalBoxSlot* EmptySlot = EntriesBox->AddChildToVerticalBox(EmptyStateWidget))
			{
				EmptySlot->SetHorizontalAlignment(HAlign_Fill);
			}
		}
	}
	else
	{
		for (int32 RowIndex = 0; RowIndex < ResolvedRows.Num(); ++RowIndex)
		{
			const FProjectActivityFeedRowDisplayData& RowData = ResolvedRows[RowIndex];
			const TSubclassOf<UProjectActivityFeedEntryRowWidget> RowClass = ResolveRowWidgetClassForData(RowData);
			UClass* EffectiveRowClass = RowClass ? RowClass.Get() : UProjectActivityFeedEntryRowWidget::StaticClass();
			UProjectActivityFeedEntryRowWidget* RowWidget = CreateWidget<UProjectActivityFeedEntryRowWidget>(this, EffectiveRowClass);
			if (!RowWidget)
			{
				continue;
			}

			RowWidget->ApplyDisplayData(RowData);
			VisibleRowWidgets.Add(RowWidget);

			if (UVerticalBoxSlot* RowSlot = EntriesBox->AddChildToVerticalBox(RowWidget))
			{
				RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				RowSlot->SetHorizontalAlignment(HAlign_Fill);
				if (RowIndex > 0)
				{
					RowSlot->SetPadding(FMargin(0.0f, CurrentLayoutPolicy.RowGap, 0.0f, 0.0f));
				}
			}
		}
	}

	EntriesBox->InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();
	ScrollTargetRowIndex = VisibleRowWidgets.IsEmpty() ? INDEX_NONE : VisibleRowWidgets.Num() - 1;
	EntriesScrollBox->ScrollToEnd();
	OnChronicleRowsRebuilt(bExpanded, VisibleRowWidgets.Num());
}

TArray<FProjectActivityFeedRowDisplayData> UProjectActivityFeedWidget::BuildResolvedRowData() const
{
	TArray<FProjectActivityFeedRowDisplayData> ResolvedRows;

	const UProjectActivityFeedSettings* Settings = UProjectActivityFeedSettings::Get();
	const int32 VisibleEntryLimit = Settings
		? FMath::Max(bExpanded ? Settings->ExpandedVisibleEntries : Settings->CompactVisibleEntries, 1)
		: (bExpanded ? 4 : 2);
	const int32 StartIndex = FMath::Max(CachedEntries.Num() - VisibleEntryLimit, 0);
	const float RowWidth = CurrentPanelSize.X - 40.0f;
	const float RowHeight = CurrentLayoutPolicy.MinimumRowHeight;
	const float TextWrapWidth = FMath::Min(CurrentLayoutPolicy.MaximumTextWidth, RowWidth);
	const int32 BodyFontSize = Settings
		? FMath::Max(bExpanded ? Settings->ExpandedBodyFontSize : Settings->CompactBodyFontSize, 6)
		: (bExpanded ? 14 : 12);
	const int32 BadgeFontSize = Settings
		? FMath::Max(bExpanded ? Settings->ExpandedBadgeFontSize : Settings->CompactBadgeFontSize, 6)
		: (bExpanded ? 12 : 10);
	const int32 PrimaryFontSize = Settings
		? FMath::Max(bExpanded ? Settings->ExpandedPrimaryFontSize : Settings->CompactPrimaryFontSize, 6)
		: (bExpanded ? 18 : 14);

	ResolvedRows.Reserve(CachedEntries.Num() - StartIndex);
	for (int32 EntryIndex = StartIndex; EntryIndex < CachedEntries.Num(); ++EntryIndex)
	{
		const FProjectActivityFeedEntry& Entry = CachedEntries[EntryIndex];
		const ProjectActivityFeedWidgetPrivate::FResolvedChronicleChannelStyle Style = ProjectActivityFeedWidgetPrivate::ResolveChannelStyle(Entry);

		FProjectActivityFeedRowDisplayData& RowData = ResolvedRows.AddDefaulted_GetRef();
		RowData.Channel = Entry.Channel;
		RowData.RenderStyle = Entry.RenderStyle == EProjectActivityFeedRenderStyle::Auto
			? EProjectActivityFeedRenderStyle::Standard
			: Entry.RenderStyle;
		RowData.BadgeLabel = Style.DisplayLabel;
		RowData.AccentTint = Style.AccentTint;
		RowData.BadgeFillTint = Style.BadgeFillTint;
		RowData.BadgeTextTint = Style.BadgeTextTint;
		RowData.PrimaryText = Entry.PrimaryText;
		RowData.SecondaryText = Entry.SecondaryText;
		RowData.Message = ProjectActivityFeedWidgetPrivate::BuildFallbackMessageText(Entry);
		RowData.RowWidth = RowWidth;
		RowData.RowHeight = RowHeight;
		RowData.TextWrapWidth = TextWrapWidth;
		RowData.InlinePrimaryWidthRatio = CurrentLayoutPolicy.InlinePrimaryWidthRatio;
		RowData.LineHeightPercentage = CurrentLayoutPolicy.LineHeightPercentage;
		RowData.BodyFontSize = BodyFontSize;
		RowData.BadgeFontSize = BadgeFontSize;
		RowData.PrimaryFontSize = PrimaryFontSize;
		RowData.bExpanded = bExpanded;

		if (RowData.RenderStyle == EProjectActivityFeedRenderStyle::Standard
			&& !RowData.PrimaryText.IsEmpty()
			&& !RowData.SecondaryText.IsEmpty())
		{
			RowData.RenderStyle = EProjectActivityFeedRenderStyle::Auto;
		}
	}

	return ResolvedRows;
}

TSubclassOf<UProjectChroniclePanelWidget> UProjectActivityFeedWidget::ResolvePanelWidgetClass(const bool bForExpanded) const
{
	UClass* NativePanelClass = bForExpanded
		? UProjectChronicleExpandedGlobalWidget::StaticClass()
		: UProjectChronicleNormalGlobalWidget::StaticClass();

	if (UClass* DiscoveredClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		NativePanelClass,
		bForExpanded ? TEXT("ProjectChronicleExpandedGlobal") : TEXT("ProjectChronicleNormalGlobal")))
	{
		return DiscoveredClass;
	}

	return NativePanelClass;
}

TSubclassOf<UProjectActivityFeedEntryRowWidget> UProjectActivityFeedWidget::ResolveRowWidgetClass() const
{
	if (!RowWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = RowWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	return ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectActivityFeedEntryRowWidget>(TEXT("ProjectChronicleEntryRow"));
}

TSubclassOf<UProjectActivityFeedEntryRowWidget> UProjectActivityFeedWidget::ResolveRowWidgetClassForData(
	const FProjectActivityFeedRowDisplayData& RowData) const
{
	if (!RowWidgetClass.IsNull())
	{
		return ResolveRowWidgetClass();
	}

	UClass* NativeRowClass = nullptr;
	if (bExpanded)
	{
		if (RowData.RenderStyle == EProjectActivityFeedRenderStyle::Gain)
		{
			NativeRowClass = UProjectChronicleExpandedGainRowWidget::StaticClass();
		}
		else if (RowData.RenderStyle == EProjectActivityFeedRenderStyle::DialogueQuote)
		{
			NativeRowClass = UProjectChronicleExpandedDialogueQuoteRowWidget::StaticClass();
		}
		else
		{
			NativeRowClass = UProjectChronicleExpandedStandardRowWidget::StaticClass();
		}
	}
	else
	{
		if (RowData.RenderStyle == EProjectActivityFeedRenderStyle::Gain)
		{
			NativeRowClass = UProjectChronicleNormalGainRowWidget::StaticClass();
		}
		else if (RowData.RenderStyle == EProjectActivityFeedRenderStyle::DialogueQuote)
		{
			NativeRowClass = UProjectChronicleNormalDialogueQuoteRowWidget::StaticClass();
		}
		else
		{
			NativeRowClass = UProjectChronicleNormalStandardRowWidget::StaticClass();
		}
	}

	if (UClass* DiscoveredClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		NativeRowClass,
		TEXT("ProjectChronicleModeRow")))
	{
		return DiscoveredClass;
	}

	return ResolveRowWidgetClass();
}

TSubclassOf<UProjectChronicleEmptyStateWidget> UProjectActivityFeedWidget::ResolveEmptyStateWidgetClass() const
{
	if (!EmptyStateWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = EmptyStateWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	UClass* NativeEmptyClass = bExpanded
		? UProjectChronicleExpandedEmptyStateWidget::StaticClass()
		: UProjectChronicleNormalEmptyStateWidget::StaticClass();

	if (UClass* DiscoveredClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		NativeEmptyClass,
		TEXT("ProjectChronicleEmptyState")))
	{
		return DiscoveredClass;
	}

	return ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectChronicleEmptyStateWidget>(TEXT("ProjectChronicleEmptyState"));
}

UTexture2D* UProjectActivityFeedWidget::ResolveTexture(const TSoftObjectPtr<UTexture2D>& AssetPtr, const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UTexture2D* LoadedTexture = AssetPtr.LoadSynchronous())
		{
			return LoadedTexture;
		}
	}

	return Cast<UTexture2D>(ProjectActivityFeedWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectActivityFeedWidget::ResolveStyleAsset(const TSoftObjectPtr<UObject>& AssetPtr, const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedObject = AssetPtr.LoadSynchronous())
		{
			return LoadedObject;
		}
	}

	return ProjectActivityFeedWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectActivityFeedWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectActivityFeedWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectActivityFeedWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectActivityFeedWidgetPrivate::BodyFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}
