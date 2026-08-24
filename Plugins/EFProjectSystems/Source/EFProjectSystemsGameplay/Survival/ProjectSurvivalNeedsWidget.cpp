#include "Survival/ProjectSurvivalNeedsWidget.h"

#include "EFProjectUIPalette.h"
#include "EFProjectUISettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalNeedsSettings.h"
#include "Survival/ProjectSurvivalNeedsTypes.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "Styling/CoreStyle.h"

namespace ProjectInnerStateWidgetPrivate
{
	constexpr float DesignWidth = 448.0f;
	constexpr float DesignHeight = 300.0f;
	constexpr float RowsPanelWidth = 420.0f;
	constexpr float RowsPanelHeight = 224.0f;
	constexpr float RowsScrollWidth = 400.0f;
	constexpr float RowsScrollHeight = 198.0f;
	constexpr float FrameCornerRadius = 22.0f;
	constexpr float FrameOutlineWidth = 1.25f;
	constexpr int32 TotalBars = 10;
	constexpr int32 RuntimeFallbackSortBase = 1000;

	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.0f);
	const FLinearColor VignetteTint(1.0f, 1.0f, 1.0f, 0.0f);
	FLinearColor FrameFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.72f);
	}
	FLinearColor FrameOutlineTint()
	{
		return EFProjectUIPalette::Outline(0.88f);
	}
	FLinearColor HazeTint()
	{
		return EFProjectUIPalette::Haze(0.18f);
	}
	FLinearColor TitleTint()
	{
		return EFProjectUIPalette::Title();
	}
	FLinearColor HudTextTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	const FLinearColor TitleShadowTint(0.0f, 0.0f, 0.0f, 0.70f);

	const TCHAR* CinzelFontPath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Fonts/F_InnerState_Cinzel.F_InnerState_Cinzel");
	const TCHAR* CormorantFontPath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Fonts/F_InnerState_Cormorant.F_InnerState_Cormorant");
	const TCHAR* FrameTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Frame.T_InnerState_Frame");
	const TCHAR* VignetteTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Vignette.T_InnerState_Vignette");
	const TCHAR* HazeTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Haze.T_InnerState_Haze");
	const TCHAR* RowsPanelTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Panel.T_InnerState_Panel");
	const TCHAR* DividerTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_Divider.T_InnerState_Divider");
	const TCHAR* HudPillTexturePath = TEXT("/Game/_Game/Widgets/InnerState/Assets/Textures/T_InnerState_HudPill.T_InnerState_HudPill");

	struct FProjectInnerStateRuntimeData
	{
		float CurrentValue = 0.0f;
		float MaxValue = 100.0f;
		float NormalizedValue = 0.0f;
		int32 FilledBars = 0;
		int32 TotalBars = ProjectInnerStateWidgetPrivate::TotalBars;
		bool bIsSensation = false;
	};

	struct FProjectResolvedInnerStateRow
	{
		FProjectInnerStateRowDisplayData RowData;
		int32 SortOrder = 0;
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

	FString HumanizeEntryName(const FName EntryName)
	{
		FString Source = EntryName.ToString();
		Source.ReplaceInline(TEXT("_"), TEXT(" "));

		FString Result;
		Result.Reserve(Source.Len() + 4);
		for (int32 Index = 0; Index < Source.Len(); ++Index)
		{
			const TCHAR Character = Source[Index];
			const bool bIsUppercase = FChar::IsUpper(Character);
			const bool bNeedsSpacing =
				Index > 0
				&& bIsUppercase
				&& Source[Index - 1] != TEXT(' ')
				&& !FChar::IsUpper(Source[Index - 1]);

			if (bNeedsSpacing)
			{
				Result.AppendChar(TEXT(' '));
			}

			Result.AppendChar(Character);
		}

		Result.TrimStartAndEndInline();
		Result.ToUpperInline();
		return Result;
	}

	FString BuildMonogram(const FString& DisplayLabel)
	{
		for (const TCHAR Character : DisplayLabel)
		{
			if (FChar::IsAlpha(Character))
			{
				return FString::Chr(FChar::ToUpper(Character));
			}
		}

		return TEXT("?");
	}

	void ApplyRuntimeDataToRow(
		FProjectInnerStateRowDisplayData& InOutRowData,
		const FProjectInnerStateRuntimeData* RuntimeData)
	{
		const float NormalizedValue = RuntimeData ? FMath::Clamp(RuntimeData->NormalizedValue, 0.0f, 1.0f) : 0.0f;
		const int32 RuntimeTotalBars = RuntimeData ? FMath::Max(1, RuntimeData->TotalBars) : ProjectInnerStateWidgetPrivate::TotalBars;
		const int32 FilledBars = RuntimeData
			? (RuntimeTotalBars == ProjectInnerStateWidgetPrivate::TotalBars
				? FMath::Clamp(RuntimeData->FilledBars, 0, ProjectInnerStateWidgetPrivate::TotalBars)
				: FMath::Clamp(FMath::RoundToInt(NormalizedValue * ProjectInnerStateWidgetPrivate::TotalBars), 0, ProjectInnerStateWidgetPrivate::TotalBars))
			: 0;

		InOutRowData.CurrentValue = RuntimeData ? RuntimeData->CurrentValue : 0.0f;
		InOutRowData.MaxValue = RuntimeData ? RuntimeData->MaxValue : 100.0f;
		InOutRowData.NormalizedValue = NormalizedValue;
		InOutRowData.TotalBars = ProjectInnerStateWidgetPrivate::TotalBars;
		InOutRowData.FilledBars = FilledBars;
		InOutRowData.PercentValue = FMath::Clamp(FMath::RoundToInt(NormalizedValue * 100.0f), 0, 100);
	}
}

UProjectSurvivalNeedsWidget::UProjectSurvivalNeedsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, NeedsComponent(nullptr)
	, bHudVisible(false)
{
	ZOrder = 150;
	TitleFontAsset = FSoftObjectPath(ProjectInnerStateWidgetPrivate::CinzelFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectInnerStateWidgetPrivate::CormorantFontPath);
	FrameTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::FrameTexturePath);
	VignetteTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::VignetteTexturePath);
	HazeTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::HazeTexturePath);
	RowsPanelTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::RowsPanelTexturePath);
	DividerTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::DividerTexturePath);
	HudPillTexture = FSoftObjectPath(ProjectInnerStateWidgetPrivate::HudPillTexturePath);
}

void UProjectSurvivalNeedsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshDisplay();
}

void UProjectSurvivalNeedsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyHudVisibility();
	RefreshDisplay();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectSurvivalNeedsWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	// This border is intentionally transparent for the comma HUD. Keep that
	// interaction contract explicit on every theme revision.
	if (BackdropBorder)
	{
		BackdropBorder->SetBrushColor(
			ProjectInnerStateWidgetPrivate::BackdropTint);
	}

	// The native fallback owns a few procedural rounded brushes outside the
	// WidgetTree asset data. Rebuild only those paint properties; no hierarchy,
	// slot, binding, or animation is touched.
	if (bUsingNativeFallbackTree)
	{
		bVisualTreeInitialized = false;
		InitializeVisualTree();
	}
}

void UProjectSurvivalNeedsWidget::NativeDestruct()
{
	RowWidgetsByName.Empty();
	CachedRowOrder.Reset();
	RowsGlobalWidget = nullptr;
	NeedsComponent = nullptr;
	Super::NativeDestruct();
}

bool UProjectSurvivalNeedsWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;

	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshDisplay();
	}

	return bBuiltTree;
}

void UProjectSurvivalNeedsWidget::GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	const auto AddSpec = [&OutWidgetSpecs](
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& RelativeFolder,
		const FString& AssetNameOverride,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const bool bRuntimeDefault = false,
		const bool bRequiresStableRootWrapper = false,
		TArray<FName> ExpectedWidgetNames = TArray<FName>())
	{
		FCodeWidgetDesignerChildWidgetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.RelativeFolder = RelativeFolder;
		Spec.AssetNameOverride = AssetNameOverride;
		Spec.Role = Role;
		Spec.PriorityGroup = FName(TEXT("InnerState"));
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = MoveTemp(ExpectedWidgetNames);
		OutWidgetSpecs.Add(Spec);
	};

	const TArray<FName> RowWidgetNames = {
		TEXT("DesignerRootOverlay"),
		TEXT("RootSizeBox"),
		TEXT("RootOverlay"),
		TEXT("RowFrameImage"),
		TEXT("MedallionImage"),
		TEXT("MonogramText"),
		TEXT("LabelText"),
		TEXT("PipsBox"),
		TEXT("ValueText"),
		TEXT("GlyphImage")
	};

	AddSpec(
		UProjectInnerStateGlobalWidget::StaticClass(),
		TEXT("Globals"),
		TEXT("WBP_ProjectInnerStateGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		9000,
		true,
		false,
		{
			TEXT("RootCanvas"),
			TEXT("FrameCanvas"),
			TEXT("TitleText"),
			TEXT("HudPillOverlay"),
			TEXT("HudText"),
			TEXT("RowsGlobalWidget")
		});
	AddSpec(
		UProjectInnerStateRowGlobalWidget::StaticClass(),
		TEXT("Globals"),
		TEXT("WBP_ProjectInnerStateRowGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		10000,
		true,
		true,
		RowWidgetNames);
	AddSpec(
		UProjectInnerStateRowsGlobalWidget::StaticClass(),
		TEXT("Globals"),
		TEXT("WBP_ProjectInnerStateRowsGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		8000,
		false,
		false,
		{ TEXT("RowsBox"), TEXT("HungerRow"), TEXT("ThirstRow"), TEXT("SleepRow"), TEXT("MadnessRow"), TEXT("CurseRow"), TEXT("PainRow") });
	AddSpec(
		UProjectInnerStateRowWidget::StaticClass(),
		TEXT("Main"),
		TEXT("WBP_ProjectInnerStateRow"),
		ECodeWidgetDesignerAssetRole::MainBase,
		1000,
		false,
		true,
		RowWidgetNames);
	AddSpec(UProjectInnerStateHungerRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStateHungerRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
	AddSpec(UProjectInnerStateThirstRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStateThirstRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
	AddSpec(UProjectInnerStateSleepRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStateSleepRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
	AddSpec(UProjectInnerStateMadnessRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStateMadnessRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
	AddSpec(UProjectInnerStateCurseRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStateCurseRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
	AddSpec(UProjectInnerStatePainRowWidget::StaticClass(), TEXT("Rows"), TEXT("WBP_ProjectInnerStatePainRow"), ECodeWidgetDesignerAssetRole::Individual, 500, false, true, RowWidgetNames);
}

void UProjectInnerStateGlobalWidget::GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	OutWidgetSpecs.Reset();
}

void UProjectSurvivalNeedsWidget::SetNeedsComponent(UProjectSurvivalNeedsComponent* InNeedsComponent)
{
	if (NeedsComponent == InNeedsComponent)
	{
		return;
	}

	NeedsComponent = InNeedsComponent;
	RefreshDisplay();
}

void UProjectSurvivalNeedsWidget::RefreshDisplay()
{
	BuildWidgetTree();
	InitializeVisualTree();
	SyncRowsGlobalWidget();

	if (!RowsScrollBox && !RowsGlobalWidget)
	{
		return;
	}

	const TArray<FProjectInnerStateRowDisplayData> ResolvedRows = BuildResolvedRowData();
	if (DoesRowLayoutNeedRebuild(ResolvedRows))
	{
		RebuildRows(ResolvedRows);
	}

	for (const FProjectInnerStateRowDisplayData& RowData : ResolvedRows)
	{
		if (TObjectPtr<UProjectInnerStateRowWidget>* ExistingRow = RowWidgetsByName.Find(RowData.EntryName))
		{
			if (UProjectInnerStateRowWidget* RowWidget = ExistingRow->Get())
			{
				RowWidget->ApplyDisplayData(RowData);
			}
		}
	}
}

void UProjectSurvivalNeedsWidget::SetHudVisible(const bool bVisible)
{
	if (bHudVisible == bVisible)
	{
		return;
	}

	bHudVisible = bVisible;
	ApplyHudVisibility();

	if (bHudVisible)
	{
		RefreshDisplay();
	}
}

void UProjectSurvivalNeedsWidget::ToggleHudVisible()
{
	SetHudVisible(!bHudVisible);
}

bool UProjectSurvivalNeedsWidget::IsHudVisible() const
{
	return bHudVisible;
}

void UProjectSurvivalNeedsWidget::ApplyHudVisibility()
{
	SetVisibility(bHudVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UProjectSurvivalNeedsWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas || WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = bUsingNativeFallbackTree && RootCanvas != nullptr && WidgetTree->RootWidget == RootCanvas;
		SyncRowsGlobalWidget();
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectSurvivalNeedsWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	BackdropBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackdropBorder"));
	BackdropBorder->SetPadding(FMargin(0.0f));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		RootCanvas,
		BackdropBorder,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		0);

	VignetteImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("VignetteImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		RootCanvas,
		VignetteImage,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		1);

	FrameScaleBox = TargetWidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("FrameScaleBox"));
	FrameScaleBox->SetStretch(EStretch::ScaleToFit);
	FrameScaleBox->SetStretchDirection(EStretchDirection::Both);
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		RootCanvas,
		FrameScaleBox,
		FAnchors(0.0f, 1.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(10.0f, -14.0f),
		FVector2D(ProjectInnerStateWidgetPrivate::DesignWidth, ProjectInnerStateWidgetPrivate::DesignHeight),
		2);

	FrameSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FrameSizeBox"));
	FrameSizeBox->SetWidthOverride(ProjectInnerStateWidgetPrivate::DesignWidth);
	FrameSizeBox->SetHeightOverride(ProjectInnerStateWidgetPrivate::DesignHeight);
	FrameScaleBox->AddChild(FrameSizeBox);

	FrameCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("FrameCanvas"));
	FrameSizeBox->AddChild(FrameCanvas);

	FrameImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		FrameImage,
		FAnchors(0.0f, 0.0f, 1.0f, 1.0f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		0);

	HazeImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HazeImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		HazeImage,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 15.0f),
		FVector2D(396.0f, 260.0f),
		1);

	RowsPanelImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("RowsPanelImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		RowsPanelImage,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 66.0f),
		FVector2D(ProjectInnerStateWidgetPrivate::RowsPanelWidth, ProjectInnerStateWidgetPrivate::RowsPanelHeight),
		3);

	TitleText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		TitleText,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 20.0f),
		FVector2D::ZeroVector,
		5,
		true);

	TopDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TopDividerImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		TopDividerImage,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 52.0f),
		FVector2D(270.0f, 14.0f),
		5);

	HudPillOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudPillOverlay"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		HudPillOverlay,
		FAnchors(1.0f, 0.0f),
		FVector2D(1.0f, 0.0f),
		FVector2D(-22.0f, 18.0f),
		FVector2D(72.0f, 28.0f),
		6);

	HudPillImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HudPillImage"));
	if (UOverlaySlot* HudPillImageSlot = HudPillOverlay->AddChildToOverlay(HudPillImage))
	{
		HudPillImageSlot->SetHorizontalAlignment(HAlign_Fill);
		HudPillImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	HudText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HudText"));
	if (UOverlaySlot* HudTextSlot = HudPillOverlay->AddChildToOverlay(HudText))
	{
		HudTextSlot->SetHorizontalAlignment(HAlign_Center);
		HudTextSlot->SetVerticalAlignment(VAlign_Center);
	}

	RowsGlobalWidget = TargetWidgetTree->ConstructWidget<UProjectInnerStateRowsGlobalWidget>(
		UProjectInnerStateRowsGlobalWidget::StaticClass(),
		TEXT("RowsGlobalWidget"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		RowsGlobalWidget,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 82.0f),
		FVector2D(ProjectInnerStateWidgetPrivate::RowsScrollWidth, ProjectInnerStateWidgetPrivate::RowsScrollHeight),
		4);

	RowsScrollBox = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RowsScrollBox"));
	RowsScrollBox->SetOrientation(EOrientation::Orient_Vertical);
	RowsScrollBox->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
	RowsScrollBox->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	RowsScrollBox->SetScrollbarThickness(FVector2D(0.0f, 0.0f));
	RowsScrollBox->SetVisibility(ESlateVisibility::Collapsed);
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		RowsScrollBox,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 82.0f),
		FVector2D(ProjectInnerStateWidgetPrivate::RowsScrollWidth, ProjectInnerStateWidgetPrivate::RowsScrollHeight),
		4);

	BottomDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BottomDividerImage"));
	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		BottomDividerImage,
		FAnchors(0.5f, 1.0f),
		FVector2D(0.5f, 1.0f),
		FVector2D(0.0f, -13.0f),
		FVector2D(128.0f, 18.0f),
		5);

	return true;
}

void UProjectSurvivalNeedsWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	SyncRowsGlobalWidget();
	if (!bUsingNativeFallbackTree)
	{
		if (BackdropBorder)
		{
			BackdropBorder->SetBrushColor(ProjectInnerStateWidgetPrivate::BackdropTint);
		}
		if (FrameImage)
		{
			FrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				FrameImage,
				ProjectInnerStateWidgetPrivate::FrameOutlineTint()));
		}
		if (HazeImage)
		{
			HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				HazeImage,
				ProjectInnerStateWidgetPrivate::HazeTint()));
		}
		if (RowsPanelImage)
		{
			RowsPanelImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				RowsPanelImage,
				EFProjectUIPalette::PanelFillDeep(0.88f)));
		}
		if (TitleText)
		{
			TitleText->SetText(FText::FromString(TEXT("INNER STATE")));
			TitleText->SetColorAndOpacity(FSlateColor(ProjectInnerStateWidgetPrivate::TitleTint()));
		}
		if (TopDividerImage) TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(TopDividerImage, ProjectInnerStateWidgetPrivate::TitleTint().CopyWithNewOpacity(0.92f)));
		if (BottomDividerImage) BottomDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(BottomDividerImage, ProjectInnerStateWidgetPrivate::TitleTint().CopyWithNewOpacity(0.82f)));
		if (HudPillImage) HudPillImage->SetColorAndOpacity(ResolveProjectThemeImageTint(HudPillImage, EFProjectUIPalette::PanelFillDeep(0.82f)));
		if (HudText)
		{
			HudText->SetText(FText::FromString(TEXT("HUD")));
			HudText->SetColorAndOpacity(FSlateColor(ProjectInnerStateWidgetPrivate::HudTextTint()));
		}
		bVisualTreeInitialized = true;
		return;
	}

	if (BackdropBorder)
	{
		BackdropBorder->SetBrushColor(ProjectInnerStateWidgetPrivate::BackdropTint);
	}

	if (VignetteImage)
	{
		VignetteImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(VignetteTexture, ProjectInnerStateWidgetPrivate::VignetteTexturePath)),
			false);
		VignetteImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			VignetteImage,
			ProjectInnerStateWidgetPrivate::VignetteTint));
	}

	if (FrameImage)
	{
		const FSlateRoundedBoxBrush FrameBrush(
			ProjectInnerStateWidgetPrivate::FrameFillTint(),
			ProjectInnerStateWidgetPrivate::FrameCornerRadius,
			FSlateColor(ProjectInnerStateWidgetPrivate::FrameOutlineTint()),
			ProjectInnerStateWidgetPrivate::FrameOutlineWidth,
			FVector2f(
				ProjectInnerStateWidgetPrivate::DesignWidth,
				ProjectInnerStateWidgetPrivate::DesignHeight));
		FrameImage->SetBrush(FrameBrush);
		FrameImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FrameImage,
			FLinearColor::White));
	}

	if (HazeImage)
	{
		HazeImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HazeTexture, ProjectInnerStateWidgetPrivate::HazeTexturePath)),
			false);
		HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HazeImage,
			ProjectInnerStateWidgetPrivate::HazeTint()));
	}

	if (RowsPanelImage)
	{
		RowsPanelImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(RowsPanelTexture, ProjectInnerStateWidgetPrivate::RowsPanelTexturePath)),
			false);
	}

	if (TitleText)
	{
		TitleText->SetText(FText::FromString(TEXT("INNER STATE")));
		TitleText->SetFont(MakeTitleFont(22));
		TitleText->SetJustification(ETextJustify::Center);
		TitleText->SetColorAndOpacity(FSlateColor(ProjectInnerStateWidgetPrivate::TitleTint()));
		TitleText->SetShadowOffset(FVector2D(0.0f, 2.0f));
		TitleText->SetShadowColorAndOpacity(ProjectInnerStateWidgetPrivate::TitleShadowTint);
	}

	if (TopDividerImage)
	{
		TopDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectInnerStateWidgetPrivate::DividerTexturePath)),
			false);
		TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TopDividerImage,
			ProjectInnerStateWidgetPrivate::TitleTint().CopyWithNewOpacity(0.92f)));
	}

	if (BottomDividerImage)
	{
		BottomDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectInnerStateWidgetPrivate::DividerTexturePath)),
			false);
		BottomDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			BottomDividerImage,
			ProjectInnerStateWidgetPrivate::TitleTint().CopyWithNewOpacity(0.82f)));
	}

	if (HudPillImage)
	{
		HudPillImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HudPillTexture, ProjectInnerStateWidgetPrivate::HudPillTexturePath)),
			false);
	}

	if (HudText)
	{
		HudText->SetText(FText::FromString(TEXT("HUD")));
		HudText->SetFont(MakeBodyFont(13, 60));
		HudText->SetJustification(ETextJustify::Center);
		HudText->SetColorAndOpacity(FSlateColor(ProjectInnerStateWidgetPrivate::HudTextTint()));
	}

	bVisualTreeInitialized = true;
}

void UProjectSurvivalNeedsWidget::EnsureRowsGlobalWidget()
{
	if (RowsGlobalWidget || !FrameCanvas)
	{
		return;
	}

	const TSubclassOf<UProjectInnerStateRowsGlobalWidget> RowsGlobalClass =
		ProjectWidgetClassResolver::DiscoverWidgetClass<UProjectInnerStateRowsGlobalWidget>(TEXT("ProjectInnerStateRowsGlobal"));

	RowsGlobalWidget = CreateWidget<UProjectInnerStateRowsGlobalWidget>(
		this,
		RowsGlobalClass ? RowsGlobalClass.Get() : UProjectInnerStateRowsGlobalWidget::StaticClass());
	if (!RowsGlobalWidget)
	{
		return;
	}

	ProjectInnerStateWidgetPrivate::AddCanvasChild(
		FrameCanvas,
		RowsGlobalWidget,
		FAnchors(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f),
		FVector2D(0.0f, 82.0f),
		FVector2D(ProjectInnerStateWidgetPrivate::RowsScrollWidth, ProjectInnerStateWidgetPrivate::RowsScrollHeight),
		4);
}

void UProjectSurvivalNeedsWidget::SyncRowsGlobalWidget()
{
	if (!RowsGlobalWidget && WidgetTree)
	{
		RowsGlobalWidget = Cast<UProjectInnerStateRowsGlobalWidget>(WidgetTree->FindWidget(FName(TEXT("RowsGlobalWidget"))));
	}

	if (!RowsGlobalWidget)
	{
		EnsureRowsGlobalWidget();
	}
}

TArray<FProjectInnerStateRowDisplayData> UProjectSurvivalNeedsWidget::BuildResolvedRowData() const
{
	TMap<FName, ProjectInnerStateWidgetPrivate::FProjectInnerStateRuntimeData> RuntimeDataByName;

	if (NeedsComponent)
	{
		for (const FProjectSurvivalNeedSnapshot& Snapshot : NeedsComponent->BuildNeedSnapshots())
		{
			ProjectInnerStateWidgetPrivate::FProjectInnerStateRuntimeData& RuntimeData = RuntimeDataByName.FindOrAdd(Snapshot.NeedName);
			RuntimeData.CurrentValue = Snapshot.CurrentValue;
			RuntimeData.MaxValue = Snapshot.MaxValue;
			RuntimeData.NormalizedValue = Snapshot.NormalizedValue;
			RuntimeData.FilledBars = Snapshot.FilledBars;
			RuntimeData.TotalBars = Snapshot.TotalBars;
			RuntimeData.bIsSensation = false;
		}

		for (const FProjectSurvivalSensationSnapshot& Snapshot : NeedsComponent->BuildSensationSnapshots())
		{
			if (IsEntryHiddenFromHud(Snapshot.SensationName))
			{
				continue;
			}

			ProjectInnerStateWidgetPrivate::FProjectInnerStateRuntimeData& RuntimeData = RuntimeDataByName.FindOrAdd(Snapshot.SensationName);
			RuntimeData.CurrentValue = Snapshot.CurrentValue;
			RuntimeData.MaxValue = Snapshot.MaxValue;
			RuntimeData.NormalizedValue = Snapshot.NormalizedValue;
			RuntimeData.FilledBars = Snapshot.FilledBars;
			RuntimeData.TotalBars = Snapshot.TotalBars;
			RuntimeData.bIsSensation = true;
		}
	}

	TArray<ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow> ResolvedRows;
	TSet<FName> AddedEntryNames;
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	static const TArray<FProjectInnerStateEntryDefinition> EmptyDefinitions;
	const TArray<FProjectInnerStateEntryDefinition>& Definitions = UISettings
		? UISettings->InnerStateEntryDefinitions
		: EmptyDefinitions;

	for (const FProjectInnerStateEntryDefinition& Definition : Definitions)
	{
		if (Definition.EntryName.IsNone() || IsEntryHiddenFromHud(Definition.EntryName))
		{
			continue;
		}

		ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow ResolvedRow;
		ResolvedRow.SortOrder = Definition.SortOrder;
		ResolvedRow.RowData.EntryName = Definition.EntryName;
		ResolvedRow.RowData.DisplayLabel = Definition.DisplayLabel.IsEmpty()
			? ProjectInnerStateWidgetPrivate::HumanizeEntryName(Definition.EntryName)
			: Definition.DisplayLabel;
		ResolvedRow.RowData.Monogram = Definition.Monogram.IsEmpty()
			? ProjectInnerStateWidgetPrivate::BuildMonogram(ResolvedRow.RowData.DisplayLabel)
			: Definition.Monogram;
		ResolvedRow.RowData.AccentTint = EFProjectUIPalette::InnerStateForName(Definition.EntryName);
		ResolvedRow.RowData.bIsSensation = Definition.bIsSensation;
		ProjectInnerStateWidgetPrivate::ApplyRuntimeDataToRow(
			ResolvedRow.RowData,
			RuntimeDataByName.Find(Definition.EntryName));

		ResolvedRows.Add(ResolvedRow);
		AddedEntryNames.Add(Definition.EntryName);
	}

	int32 FallbackSortOffset = 0;
	for (const TPair<FName, ProjectInnerStateWidgetPrivate::FProjectInnerStateRuntimeData>& Pair : RuntimeDataByName)
	{
		if (AddedEntryNames.Contains(Pair.Key))
		{
			continue;
		}

		ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow ResolvedRow;
		ResolvedRow.SortOrder = ProjectInnerStateWidgetPrivate::RuntimeFallbackSortBase + FallbackSortOffset++;
		ResolvedRow.RowData.EntryName = Pair.Key;
		ResolvedRow.RowData.DisplayLabel = ProjectInnerStateWidgetPrivate::HumanizeEntryName(Pair.Key);
		ResolvedRow.RowData.Monogram = ProjectInnerStateWidgetPrivate::BuildMonogram(ResolvedRow.RowData.DisplayLabel);
		ResolvedRow.RowData.AccentTint = EFProjectUIPalette::Accent();
		ResolvedRow.RowData.bIsSensation = Pair.Value.bIsSensation;
		ProjectInnerStateWidgetPrivate::ApplyRuntimeDataToRow(ResolvedRow.RowData, &Pair.Value);
		ResolvedRows.Add(ResolvedRow);
	}

	ResolvedRows.Sort([](
		const ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow& Left,
		const ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow& Right)
	{
		if (Left.SortOrder != Right.SortOrder)
		{
			return Left.SortOrder < Right.SortOrder;
		}

		return Left.RowData.DisplayLabel < Right.RowData.DisplayLabel;
	});

	TArray<FProjectInnerStateRowDisplayData> FinalRows;
	FinalRows.Reserve(ResolvedRows.Num());
	for (const ProjectInnerStateWidgetPrivate::FProjectResolvedInnerStateRow& ResolvedRow : ResolvedRows)
	{
		FinalRows.Add(ResolvedRow.RowData);
	}

	return FinalRows;
}

bool UProjectSurvivalNeedsWidget::IsEntryHiddenFromHud(const FName EntryName)
{
	const UProjectSurvivalNeedsSettings* NeedsSettings = UProjectSurvivalNeedsSettings::Get();
	return NeedsSettings && NeedsSettings->HiddenHudEntryNames.Contains(EntryName);
}

bool UProjectSurvivalNeedsWidget::DoesRowLayoutNeedRebuild(const TArray<FProjectInnerStateRowDisplayData>& InRowData) const
{
	if (CachedRowOrder.Num() != InRowData.Num())
	{
		return true;
	}

	for (int32 Index = 0; Index < InRowData.Num(); ++Index)
	{
		if (CachedRowOrder[Index] != InRowData[Index].EntryName)
		{
			return true;
		}
	}

	return false;
}

void UProjectSurvivalNeedsWidget::RebuildRows(const TArray<FProjectInnerStateRowDisplayData>& InRowData)
{
	SyncRowsGlobalWidget();
	if (RowsGlobalWidget)
	{
		RowWidgetsByName.Empty();
		CachedRowOrder.Reset();

		const int32 VisibleRowCount = RowsGlobalWidget->ApplyRows(InRowData, ResolveRowWidgetClass());
		for (const FProjectInnerStateRowDisplayData& RowData : InRowData)
		{
			if (UProjectInnerStateRowWidget* RowWidget = RowsGlobalWidget->FindRowWidgetByEntry(RowData.EntryName))
			{
				RowWidgetsByName.Add(RowData.EntryName, RowWidget);
			}
			CachedRowOrder.Add(RowData.EntryName);
		}

		if (RowsScrollBox)
		{
			RowsScrollBox->ClearChildren();
			RowsScrollBox->SetVisibility(ESlateVisibility::Collapsed);
		}
		(void)VisibleRowCount;
		return;
	}

	if (!RowsScrollBox)
	{
		return;
	}

	RowsScrollBox->ClearChildren();
	RowsScrollBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RowWidgetsByName.Empty();
	CachedRowOrder.Reset();

	const TSubclassOf<UProjectInnerStateRowWidget> ResolvedRowWidgetClass = ResolveRowWidgetClass();
	for (const FProjectInnerStateRowDisplayData& RowData : InRowData)
	{
		if (!ResolvedRowWidgetClass)
		{
			continue;
		}

		UProjectInnerStateRowWidget* RowWidget = nullptr;
		if (APlayerController* OwningPlayer = GetOwningPlayer())
		{
			RowWidget = CreateWidget<UProjectInnerStateRowWidget>(
				OwningPlayer,
				ResolvedRowWidgetClass);
		}

		if (!RowWidget && GetWorld())
		{
			RowWidget = CreateWidget<UProjectInnerStateRowWidget>(
				GetWorld(),
				ResolvedRowWidgetClass);
		}

		if (!RowWidget)
		{
			continue;
		}

		RowWidget->ApplyDisplayData(RowData);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(RowsScrollBox->AddChild(RowWidget)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetVerticalAlignment(VAlign_Top);
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		}

		RowWidgetsByName.Add(RowData.EntryName, RowWidget);
		CachedRowOrder.Add(RowData.EntryName);
	}
}

TSubclassOf<UProjectInnerStateRowWidget> UProjectSurvivalNeedsWidget::ResolveRowWidgetClass() const
{
	if (!RowWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = RowWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	if (UClass* GlobalRowClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		UProjectInnerStateRowGlobalWidget::StaticClass(),
		TEXT("ProjectInnerStateRowGlobal")))
	{
		return GlobalRowClass;
	}

	return UProjectInnerStateRowWidget::StaticClass();
}

UTexture2D* UProjectSurvivalNeedsWidget::ResolveTexture(
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

	return Cast<UTexture2D>(ProjectInnerStateWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectSurvivalNeedsWidget::ResolveStyleAsset(
	const TSoftObjectPtr<UObject>& AssetPtr,
	const TCHAR* FallbackPath) const
{
	if (!AssetPtr.IsNull())
	{
		if (UObject* LoadedAsset = AssetPtr.LoadSynchronous())
		{
			return LoadedAsset;
		}
	}

	return ProjectInnerStateWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectSurvivalNeedsWidget::MakeTitleFont(const int32 Size) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = 110;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectInnerStateWidgetPrivate::CinzelFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectSurvivalNeedsWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectInnerStateWidgetPrivate::CormorantFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}
