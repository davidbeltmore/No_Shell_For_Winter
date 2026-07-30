#include "InnerDoctrine/ProjectInnerDoctrineExchangeMenuWidget.h"

#include "EFProjectUIPalette.h"
#include "EFProjectUISettings.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"
#include "Styling/CoreStyle.h"
#include "UI/ProjectWidgetClassResolver.h"

#define LOCTEXT_NAMESPACE "ProjectInnerDoctrineExchangeMenuWidget"

namespace ProjectDoctrineExchangeMenuWidgetPrivate
{
	constexpr float FullscreenInset = 32.0f;
	constexpr float FrameCornerRadius = 18.0f;
	constexpr float FrameOutlineWidth = 1.2f;
	constexpr float ResourceBoxHeight = 56.0f;
	constexpr float AttributeListWidth = 560.0f;
	constexpr float RowWidth = 560.0f;
	constexpr float RowHeight = 88.0f;
	constexpr int32 RuntimeFallbackSortBase = 1000;

	const FLinearColor BackdropTint(0.0f, 0.0f, 0.0f, 0.74f);
	FLinearColor FrameFillTint()
	{
		return EFProjectUIPalette::PanelFillDeep(0.97f);
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
	FLinearColor HazeTint()
	{
		return EFProjectUIPalette::Haze(0.14f);
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
	FLinearColor AccentTint()
	{
		return EFProjectUIPalette::AccentSoft();
	}
	FLinearColor WarningTint()
	{
		return EFProjectUIPalette::Warning();
	}
	const FLinearColor ShadowTint(0.0f, 0.0f, 0.0f, 0.34f);

	const TCHAR* TitleFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Bebas.F_Chronicle_Bebas");
	const TCHAR* BodyFontPath = TEXT("/Game/_Game/Widgets/Chronicle/Assets/Fonts/F_Chronicle_Cormorant.F_Chronicle_Cormorant");
	const TCHAR* FrameTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Frame.T_InnerDoctrine_Altar_Frame");
	const TCHAR* HazeTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Haze.T_InnerDoctrine_Altar_Haze");
	const TCHAR* DividerTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Divider.T_InnerDoctrine_Altar_Divider");
	const TCHAR* StatBoxTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_StatBox.T_InnerDoctrine_Altar_StatBox");
	const TCHAR* FooterOrnamentTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Footer.T_InnerDoctrine_Altar_Footer");
	const TCHAR* ModeGlyphTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Glyph.T_InnerDoctrine_Altar_Glyph");
	const TCHAR* DefaultIconTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Icon_Default.T_InnerDoctrine_Altar_Icon_Default");
	const TCHAR* DefaultWatermarkTexturePath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/T_InnerDoctrine_Altar_Watermark_Default.T_InnerDoctrine_Altar_Watermark_Default");

	UObject* LoadObjectByPath(const TCHAR* AssetPath)
	{
		return AssetPath && AssetPath[0] != 0
			? StaticLoadObject(UObject::StaticClass(), nullptr, AssetPath)
			: nullptr;
	}

	FName BuildAttributeName(const EProjectDoctrineAttribute Attribute)
	{
		if (const UEnum* AttributeEnum = StaticEnum<EProjectDoctrineAttribute>())
		{
			const FString NameString = AttributeEnum->GetNameStringByValue(static_cast<int64>(Attribute));
			if (!NameString.IsEmpty() && NameString != TEXT("Count"))
			{
				return FName(*NameString);
			}
		}

		return NAME_None;
	}

	FString NormalizeMenuLabel(const FText& DisplayName)
	{
		FString Result = DisplayName.ToString();
		Result.TrimStartAndEndInline();
		return Result.IsEmpty() ? TEXT("Unknown") : Result;
	}

	const FProjectDoctrineMilestoneState* FindMilestoneState(
		const FProjectInnerDoctrineSnapshot& Snapshot,
		const EProjectDoctrineAttribute Attribute,
		const int32 RequiredLevel)
	{
		return Snapshot.Milestones.FindByPredicate(
			[Attribute, RequiredLevel](const FProjectDoctrineMilestoneState& State)
			{
				return State.Definition.Attribute == Attribute
					&& State.Definition.RequiredLevel == RequiredLevel;
			});
	}

	const FProjectDoctrineMilestoneDefinition* FindConfiguredMilestone(
		const EProjectDoctrineAttribute Attribute,
		const int32 RequiredLevel)
	{
		const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
		return Settings
			? Settings->MilestoneDefinitions.FindByPredicate(
				[Attribute, RequiredLevel](const FProjectDoctrineMilestoneDefinition& Definition)
				{
					return Definition.Attribute == Attribute
						&& Definition.RequiredLevel == RequiredLevel;
				})
			: nullptr;
	}

	FText BuildMilestoneLabel(const FProjectDoctrineMilestoneDefinition* Definition)
	{
		if (!Definition)
		{
			return FText::GetEmpty();
		}

		if (Definition->Description.IsEmpty())
		{
			return Definition->DisplayName;
		}

		return FText::Format(
			NSLOCTEXT("ProjectInnerDoctrineExchangeMenuWidget", "MilestoneDefinitionFormat", "{0}: {1}"),
			Definition->DisplayName,
			Definition->Description);
	}

	TArray<FName> MenuWidgetNames()
	{
		return {
			TEXT("RootCanvas"),
			TEXT("BackdropBorder"),
			TEXT("FrameOverlay"),
			TEXT("BackgroundBorder"),
			TEXT("HazeImage"),
			TEXT("FrameTextureImage"),
			TEXT("ContentBorder"),
			TEXT("ContentBox"),
			TEXT("HeaderRow"),
			TEXT("TitleText"),
			TEXT("ModeBox"),
			TEXT("ModeGlyphImage"),
			TEXT("ModeText"),
			TEXT("TopDividerImage"),
			TEXT("ResourceRow"),
			TEXT("RunDxpBorder"),
			TEXT("RunDxpLabelText"),
			TEXT("RunDxpValueScaleBox"),
			TEXT("RunDxpValueText"),
			TEXT("MetaDxpBorder"),
			TEXT("MetaDxpLabelText"),
			TEXT("MetaDxpValueScaleBox"),
			TEXT("MetaDxpValueText"),
			TEXT("BodyRow"),
			TEXT("AttributeListBorder"),
			TEXT("RowsGlobalWidget"),
			TEXT("DetailBorder"),
			TEXT("DetailOverlay"),
			TEXT("DetailWatermarkImage"),
			TEXT("DetailContentBorder"),
			TEXT("DetailContentBox"),
			TEXT("DetailNameText"),
			TEXT("DetailLevelText"),
			TEXT("DetailCostText"),
			TEXT("DetailMilestoneDividerImage"),
			TEXT("DetailMilestoneFiveText"),
			TEXT("DetailMilestoneTenText"),
			TEXT("DetailFlavorText"),
			TEXT("FooterDividerImage"),
			TEXT("FooterRow"),
			TEXT("FooterStatusText"),
			TEXT("FooterControlsText")
		};
	}

	TArray<FName> RowWidgetNames()
	{
		return {
			TEXT("RootSizeBox"),
			TEXT("DesignerRootOverlay"),
			TEXT("RootOverlay"),
			TEXT("BackgroundBorder"),
			TEXT("FrameImage"),
			TEXT("ContentBorder"),
			TEXT("ContentBox"),
			TEXT("IconSizeBox"),
			TEXT("IconBackgroundBorder"),
			TEXT("IconImage"),
			TEXT("TextColumn"),
			TEXT("NameText"),
			TEXT("MetaRow"),
			TEXT("LevelText"),
			TEXT("SeparatorText"),
			TEXT("CostText"),
			TEXT("SelectionGlyphSizeBox"),
			TEXT("SelectionGlyphImage")
		};
	}

	TArray<FName> RowsGlobalWidgetNames()
	{
		return {
			TEXT("DesignerRootOverlay"),
			TEXT("RootSizeBox"),
			TEXT("RootOverlay"),
			TEXT("RowsScrollBox"),
			TEXT("RowsLayout"),
			TEXT("WillpowerRow"),
			TEXT("OffensiveRow"),
			TEXT("DefensiveRow"),
			TEXT("FaithRow"),
			TEXT("CunningRow"),
			TEXT("CelerityRow"),
			TEXT("CharismaRow"),
			TEXT("ExtraRowsLayout")
		};
	}

	TArray<FName> DetailWidgetNames()
	{
		return {
			TEXT("DetailBorder"),
			TEXT("DetailOverlay"),
			TEXT("DetailWatermarkImage"),
			TEXT("DetailContentBorder"),
			TEXT("DetailContentBox"),
			TEXT("DetailNameText"),
			TEXT("DetailLevelText"),
			TEXT("DetailCostText"),
			TEXT("DetailMilestoneDividerImage"),
			TEXT("DetailMilestoneFiveText"),
			TEXT("DetailMilestoneTenText"),
			TEXT("DetailFlavorText")
		};
	}

	TArray<FString> AttributePreviewTexts()
	{
		return {
			TEXT("Willpower"),
			TEXT("Offensive"),
			TEXT("Defensive"),
			TEXT("Faith"),
			TEXT("Cunning"),
			TEXT("Celerity"),
			TEXT("Charisma")
		};
	}

	TArray<FString> MenuPreviewTexts()
	{
		return {
			TEXT("INNER DOCTRINE"),
			TEXT("MASTERY"),
			TEXT("Run DXP"),
			TEXT("Meta DXP"),
			TEXT("Ready to ascend."),
			TEXT("W/S navigate"),
			TEXT("E/Enter ascend"),
			TEXT("R withdraw Meta DXP"),
			TEXT("Q/Esc close")
		};
	}

	void AddManifestSpec(
		FCodeWidgetDesignerConversionManifest& Manifest,
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& TargetAssetPath,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const bool bRuntimeDefault,
		const TArray<FName>& ExpectedWidgetNames,
		const TArray<FName>& ExpectedBlueprintEvents,
		const TArray<FName>& ExpectedVisualStates,
		const bool bRequiresStableRootWrapper = false,
		TArray<FString> ExpectedPreviewTexts = TArray<FString>())
	{
		FCodeWidgetDesignerWidgetAssetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.TargetAssetPath = TargetAssetPath;
		Spec.Role = Role;
		Spec.PriorityGroup = TEXT("InnerDoctrineAltar");
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = ExpectedWidgetNames;
		Spec.ExpectedBlueprintEvents = ExpectedBlueprintEvents;
		Spec.ExpectedVisualStates = ExpectedVisualStates;
		Spec.ExpectedPreviewTexts = MoveTemp(ExpectedPreviewTexts);
		Manifest.WidgetAssets.Add(Spec);
	}
}

UProjectInnerDoctrineExchangeMenuWidget::UProjectInnerDoctrineExchangeMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	TitleFontAsset = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::TitleFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::BodyFontPath);
	FrameTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::FrameTexturePath);
	HazeTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::HazeTexturePath);
	DividerTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DividerTexturePath);
	StatBoxTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::StatBoxTexturePath);
	FooterOrnamentTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::FooterOrnamentTexturePath);
	ModeGlyphTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::ModeGlyphTexturePath);
	DefaultIconTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DefaultIconTexturePath);
	DefaultWatermarkTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DefaultWatermarkTexturePath);
	AttributeRowWidgetClass.Reset();
}

void UProjectInnerDoctrineExchangeMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshDisplay();
}

void UProjectInnerDoctrineExchangeMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshDisplay();
	FocusMenuWidget();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineExchangeMenuWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	// The altar host is frequently a converted Widget Blueprint. Its resource
	// cards and the AttributeListBorder behind the complete left-hand row list
	// retain authored brushes because InitializeVisualTree intentionally skips
	// converted WBP trees. Rebuild those brushes procedurally on every theme
	// revision so neither a cooked purple resource nor either color multiplier
	// can survive a Black, Green, Blue, or Red selection.
	auto ApplySectionBrush = [&Theme](UBorder* Border)
	{
		if (!Border)
		{
			return;
		}

		const FSlateBrush& AuthoredBrush = Border->Background;
		const float FillAlpha = FMath::Max(
			AuthoredBrush.TintColor.GetSpecifiedColor().A,
			0.01f);
		const float MultiplierAlpha = Border->GetBrushColor().A;
		const float OutlineAlpha = FMath::Max(
			AuthoredBrush.OutlineSettings.Color.GetSpecifiedColor().A,
			0.01f);
		const FVector2f BrushSize =
			AuthoredBrush.ImageSize.X > 1.0f && AuthoredBrush.ImageSize.Y > 1.0f
				? FVector2f(AuthoredBrush.ImageSize.X, AuthoredBrush.ImageSize.Y)
				: FVector2f(600.0f, 400.0f);
		const FLinearColor FillColor(
			Theme.SectionFill.R,
			Theme.SectionFill.G,
			Theme.SectionFill.B,
			FillAlpha);
		const FLinearColor OutlineColor(
			Theme.OutlineDim.R,
			Theme.OutlineDim.G,
			Theme.OutlineDim.B,
			OutlineAlpha);

		// Constructing a new rounded brush deliberately drops ResourceObject.
		// This is what removes the serialized purple background rather than
		// placing another tint over the cooked color.
		Border->SetBrush(FSlateRoundedBoxBrush(
			FillColor,
			12.0f,
			FSlateColor(OutlineColor),
			1.0f,
			BrushSize));
		Border->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, MultiplierAlpha));
	};

	ApplySectionBrush(RunDxpBorder);
	ApplySectionBrush(MetaDxpBorder);
	ApplySectionBrush(AttributeListBorder);
	ApplySectionBrush(DetailBorder);
}

void UProjectInnerDoctrineExchangeMenuWidget::NativeDestruct()
{
	RowWidgetsByName.Empty();
	CachedRowOrder.Reset();
	ResolvedEntries.Reset();
	InnerDoctrineComponent.Reset();
	Super::NativeDestruct();
}

FReply UProjectInnerDoctrineExchangeMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (HandleMenuKey(InKeyEvent.GetKey()))
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UProjectInnerDoctrineExchangeMenuWidget::SetInnerDoctrineComponent(UProjectInnerDoctrineComponent* InComponent)
{
	if (InnerDoctrineComponent.Get() == InComponent)
	{
		return;
	}

	InnerDoctrineComponent = InComponent;
	RefreshDisplay();
}

void UProjectInnerDoctrineExchangeMenuWidget::RefreshDisplay()
{
	BuildWidgetTree();
	InitializeVisualTree();

	CachedSnapshot = InnerDoctrineComponent.IsValid()
		? InnerDoctrineComponent->BuildSnapshot()
		: FProjectInnerDoctrineSnapshot();
	ResolvedEntries = BuildResolvedEntries();

	if (ResolvedEntries.IsEmpty())
	{
		SelectedIndex = INDEX_NONE;
	}
	else if (SelectedIndex == INDEX_NONE)
	{
		SelectedIndex = 0;
	}
	else
	{
		SelectedIndex = FMath::Clamp(SelectedIndex, 0, ResolvedEntries.Num() - 1);
	}

	if (DoesRowLayoutNeedRebuild(ResolvedEntries))
	{
		RebuildAttributeRows(ResolvedEntries);
	}

	RefreshVisualState();
}

void UProjectInnerDoctrineExchangeMenuWidget::FocusMenuWidget()
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

int32 UProjectInnerDoctrineExchangeMenuWidget::GetSelectedIndex() const
{
	return SelectedIndex;
}

FProjectInnerDoctrineSnapshot UProjectInnerDoctrineExchangeMenuWidget::GetCachedSnapshot() const
{
	return CachedSnapshot;
}

bool UProjectInnerDoctrineExchangeMenuWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	bVisualTreeInitialized = false;
	BuildWidgetTree();
	InitializeVisualTree();

	CachedSnapshot = FProjectInnerDoctrineSnapshot();
	CachedSnapshot.CurrentRunDxp = 2400;
	CachedSnapshot.MetaBankDxp = 3271;
	CachedSnapshot.bDoctrineMasteryMode = true;
	ResolvedEntries = BuildDesignerPreviewEntries();
	SelectedIndex = ResolvedEntries.IsEmpty() ? INDEX_NONE : 0;
	RebuildAttributeRows(ResolvedEntries);
	RefreshVisualState();

	return TargetWidgetTree->RootWidget != nullptr;
}

bool UProjectInnerDoctrineExchangeMenuWidget::GatherCodeWidgetDesignerConversionManifest(
	FCodeWidgetDesignerConversionManifest& OutManifest) const
{
	using namespace ProjectDoctrineExchangeMenuWidgetPrivate;

	OutManifest = FCodeWidgetDesignerConversionManifest();
	OutManifest.SystemName = TEXT("InnerDoctrineAltar");
	OutManifest.RootPath = TEXT("/Game/_Game/Widgets");
	OutManifest.MainFolder = TEXT("Main");
	OutManifest.GlobalFolder = TEXT("Global");
	OutManifest.AssetFolders = {
		TEXT("InnerDoctrineAltar/Assets/Fonts"),
		TEXT("InnerDoctrineAltar/Assets/Textures")
	};

	OutManifest.HostWidget.WidgetClass = UProjectInnerDoctrineExchangeMenuWidget::StaticClass();
	OutManifest.HostWidget.TargetAssetPath = TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Main/WBP_ProjectInnerDoctrineExchangeMenu");
	OutManifest.HostWidget.Role = ECodeWidgetDesignerAssetRole::Host;
	OutManifest.HostWidget.PriorityGroup = TEXT("InnerDoctrineAltar");
	OutManifest.HostWidget.PriorityRank = 10000;
	OutManifest.HostWidget.ExpectedWidgetNames = MenuWidgetNames();
	OutManifest.HostWidget.ExpectedBlueprintEvents = { TEXT("OnExchangeMenuStateApplied"), TEXT("OnExchangeMenuSelectionChanged") };
	OutManifest.HostWidget.ExpectedVisualStates = { TEXT("Selected"), TEXT("Hover"), TEXT("Disabled"), TEXT("Locked"), TEXT("Affordable"), TEXT("Unaffordable") };
	OutManifest.HostWidget.ExpectedPreviewTexts = MenuPreviewTexts();

	const TArray<FName> RowEvents = { TEXT("OnExchangeAttributeRowDataApplied"), TEXT("OnExchangeAttributeRowVisualStateChanged") };
	const TArray<FName> RowStates = { TEXT("Selected"), TEXT("Hover"), TEXT("Disabled"), TEXT("Locked"), TEXT("Affordable"), TEXT("Unaffordable"), TEXT("Marker") };

	AddManifestSpec(
		OutManifest,
		UProjectInnerDoctrineExchangeMenuGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Global/WBP_ProjectInnerDoctrineExchangeMenuGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		30000,
		true,
		MenuWidgetNames(),
		{ TEXT("OnExchangeMenuStateApplied"), TEXT("OnExchangeMenuSelectionChanged") },
		{ TEXT("Selected"), TEXT("Hover"), TEXT("Disabled"), TEXT("Locked"), TEXT("Affordable"), TEXT("Unaffordable") },
		false,
		MenuPreviewTexts());
	AddManifestSpec(
		OutManifest,
		UProjectInnerDoctrineExchangeAttributeRowGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Global/WBP_ProjectInnerDoctrineExchangeAttributeRowGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		29000,
		true,
		RowWidgetNames(),
		RowEvents,
		RowStates,
		true);
	AddManifestSpec(
		OutManifest,
		UProjectInnerDoctrineExchangeRowsGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Global/WBP_ProjectInnerDoctrineExchangeRowsGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		28000,
		false,
		RowsGlobalWidgetNames(),
		{ TEXT("OnExchangeRowsApplied") },
		{ TEXT("Selected"), TEXT("Hover"), TEXT("Disabled"), TEXT("Locked"), TEXT("Affordable"), TEXT("Unaffordable") },
		true);
	AddManifestSpec(
		OutManifest,
		UProjectInnerDoctrineExchangeAttributeRowWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Main/WBP_ProjectInnerDoctrineExchangeAttributeRow"),
		ECodeWidgetDesignerAssetRole::MainBase,
		10000,
		false,
		RowWidgetNames(),
		RowEvents,
		RowStates,
		true);
	AddManifestSpec(
		OutManifest,
		UProjectInnerDoctrineExchangeDetailPanelWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Detail/WBP_ProjectInnerDoctrineExchangeDetailPanel"),
		ECodeWidgetDesignerAssetRole::Individual,
		9000,
		false,
		DetailWidgetNames(),
		{ TEXT("OnExchangeDetailPanelDataApplied") },
		{ TEXT("Selected"), TEXT("Locked"), TEXT("Unlocked"), TEXT("Watermark") },
		false,
		{ TEXT("Willpower") });

	const TArray<TPair<UClass*, FString>> IndividualRows = {
		{ UProjectInnerDoctrineExchangeWillpowerRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeWillpowerRow") },
		{ UProjectInnerDoctrineExchangeOffensiveRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeOffensiveRow") },
		{ UProjectInnerDoctrineExchangeDefensiveRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeDefensiveRow") },
		{ UProjectInnerDoctrineExchangeFaithRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeFaithRow") },
		{ UProjectInnerDoctrineExchangeCunningRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeCunningRow") },
		{ UProjectInnerDoctrineExchangeCelerityRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeCelerityRow") },
		{ UProjectInnerDoctrineExchangeCharismaRowWidget::StaticClass(), TEXT("WBP_ProjectInnerDoctrineExchangeCharismaRow") },
	};

	int32 RowPriority = 8000;
	for (const TPair<UClass*, FString>& RowSpec : IndividualRows)
	{
		AddManifestSpec(
			OutManifest,
			RowSpec.Key,
			FString::Printf(TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Rows/%s"), *RowSpec.Value),
			ECodeWidgetDesignerAssetRole::Individual,
			RowPriority--,
			false,
			RowWidgetNames(),
			RowEvents,
			RowStates,
			true,
			{ RowSpec.Value.Replace(TEXT("WBP_ProjectInnerDoctrineExchange"), TEXT("")).Replace(TEXT("Row"), TEXT("")) });
	}

	return true;
}

void UProjectInnerDoctrineExchangeMenuWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas && WidgetTree->RootWidget == RootCanvas)
	{
		return;
	}

	if (WidgetTree->RootWidget)
	{
		bUsingNativeFallbackTree = false;
		return;
	}

	bUsingNativeFallbackTree = true;

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	BackdropBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackdropBorder"));
	BackdropBorder->SetPadding(FMargin(0.0f));
	if (UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(BackdropBorder))
	{
		BackdropSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BackdropSlot->SetOffsets(FMargin(0.0f));
	}

	FrameOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FrameOverlay"));
	if (UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(FrameOverlay))
	{
		FrameSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		FrameSlot->SetOffsets(FMargin(
			ProjectDoctrineExchangeMenuWidgetPrivate::FullscreenInset,
			ProjectDoctrineExchangeMenuWidgetPrivate::FullscreenInset,
			ProjectDoctrineExchangeMenuWidgetPrivate::FullscreenInset,
			ProjectDoctrineExchangeMenuWidgetPrivate::FullscreenInset));
	}

	BackgroundBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(0.0f));
	if (UOverlaySlot* BackgroundSlot = FrameOverlay->AddChildToOverlay(BackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	HazeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HazeImage"));
	if (UOverlaySlot* HazeSlot = FrameOverlay->AddChildToOverlay(HazeImage))
	{
		HazeSlot->SetHorizontalAlignment(HAlign_Fill);
		HazeSlot->SetVerticalAlignment(VAlign_Fill);
		HazeSlot->SetPadding(FMargin(10.0f));
	}

	FrameTextureImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameTextureImage"));
	if (UOverlaySlot* FrameTextureSlot = FrameOverlay->AddChildToOverlay(FrameTextureImage))
	{
		FrameTextureSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameTextureSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentBorder"));
	ContentBorder->SetPadding(FMargin(34.0f, 26.0f, 34.0f, 24.0f));
	ContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* ContentSlot = FrameOverlay->AddChildToOverlay(ContentBorder))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	ContentBorder->SetContent(ContentBox);

	HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
	if (UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(HeaderRow))
	{
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
	}

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	if (UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(TitleText))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetHorizontalAlignment(HAlign_Left);
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(50.0f, 30.0f, 0.0f, 0.0f));
	}

	ModeBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ModeBox"));
	if (UHorizontalBoxSlot* ModeSlot = HeaderRow->AddChildToHorizontalBox(ModeBox))
	{
		ModeSlot->SetHorizontalAlignment(HAlign_Right);
		ModeSlot->SetVerticalAlignment(VAlign_Center);
		ModeSlot->SetPadding(FMargin(18.0f, 30.0f, 80.0f, 0.0f));
	}

	USizeBox* ModeGlyphSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ModeGlyphSizeBox"));
	ModeGlyphSizeBox->SetWidthOverride(12.0f);
	ModeGlyphSizeBox->SetHeightOverride(12.0f);
	if (UHorizontalBoxSlot* ModeGlyphSlot = ModeBox->AddChildToHorizontalBox(ModeGlyphSizeBox))
	{
		ModeGlyphSlot->SetHorizontalAlignment(HAlign_Center);
		ModeGlyphSlot->SetVerticalAlignment(VAlign_Center);
		ModeGlyphSlot->SetPadding(FMargin(0.0f, 1.0f, 8.0f, 0.0f));
	}

	ModeGlyphImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ModeGlyphImage"));
	ModeGlyphSizeBox->AddChild(ModeGlyphImage);

	ModeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ModeText"));
	ModeBox->AddChildToHorizontalBox(ModeText);

	USizeBox* TopDividerSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TopDividerSizeBox"));
	TopDividerSizeBox->SetHeightOverride(10.0f);
	if (UVerticalBoxSlot* DividerSlot = ContentBox->AddChildToVerticalBox(TopDividerSizeBox))
	{
		DividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
	}

	TopDividerImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TopDividerImage"));
	TopDividerSizeBox->AddChild(TopDividerImage);

	ResourceRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ResourceRow"));
	if (UVerticalBoxSlot* ResourceSlot = ContentBox->AddChildToVerticalBox(ResourceRow))
	{
		ResourceSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 22.0f));
	}

	RunDxpBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RunDxpBorder"));
	RunDxpBorder->SetPadding(FMargin(22.0f, 10.0f, 22.0f, 10.0f));
	if (UHorizontalBoxSlot* RunSlot = ResourceRow->AddChildToHorizontalBox(RunDxpBorder))
	{
		RunSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RunSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	UHorizontalBox* RunDxpContent = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RunDxpContent"));
	RunDxpBorder->SetContent(RunDxpContent);

	RunDxpLabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RunDxpLabelText"));
	if (UHorizontalBoxSlot* RunLabelSlot = RunDxpContent->AddChildToHorizontalBox(RunDxpLabelText))
	{
		RunLabelSlot->SetHorizontalAlignment(HAlign_Left);
		RunLabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	RunDxpValueScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("RunDxpValueScaleBox"));
	RunDxpValueScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UHorizontalBoxSlot* RunValueScaleSlot = RunDxpContent->AddChildToHorizontalBox(RunDxpValueScaleBox))
	{
		RunValueScaleSlot->SetHorizontalAlignment(HAlign_Left);
		RunValueScaleSlot->SetVerticalAlignment(VAlign_Center);
		RunValueScaleSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	}

	RunDxpValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RunDxpValueText"));
	RunDxpValueScaleBox->AddChild(RunDxpValueText);

	MetaDxpBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MetaDxpBorder"));
	MetaDxpBorder->SetPadding(FMargin(22.0f, 10.0f, 22.0f, 10.0f));
	if (UHorizontalBoxSlot* MetaSlot = ResourceRow->AddChildToHorizontalBox(MetaDxpBorder))
	{
		MetaSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		MetaSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
	}

	UHorizontalBox* MetaDxpContent = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MetaDxpContent"));
	MetaDxpBorder->SetContent(MetaDxpContent);

	MetaDxpLabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MetaDxpLabelText"));
	if (UHorizontalBoxSlot* MetaLabelSlot = MetaDxpContent->AddChildToHorizontalBox(MetaDxpLabelText))
	{
		MetaLabelSlot->SetHorizontalAlignment(HAlign_Left);
		MetaLabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	MetaDxpValueScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("MetaDxpValueScaleBox"));
	MetaDxpValueScaleBox->SetStretch(EStretch::ScaleToFit);
	if (UHorizontalBoxSlot* MetaValueScaleSlot = MetaDxpContent->AddChildToHorizontalBox(MetaDxpValueScaleBox))
	{
		MetaValueScaleSlot->SetHorizontalAlignment(HAlign_Left);
		MetaValueScaleSlot->SetVerticalAlignment(VAlign_Center);
		MetaValueScaleSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	}

	MetaDxpValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MetaDxpValueText"));
	MetaDxpValueScaleBox->AddChild(MetaDxpValueText);

	BodyRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BodyRow"));
	if (UVerticalBoxSlot* BodySlot = ContentBox->AddChildToVerticalBox(BodyRow))
	{
		BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BodySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	AttributeListBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AttributeListBorder"));
	AttributeListBorder->SetPadding(FMargin(0.0f));
	if (UHorizontalBoxSlot* ListSlot = BodyRow->AddChildToHorizontalBox(AttributeListBorder))
	{
		FSlateChildSize LeftSize(ESlateSizeRule::Automatic);
		ListSlot->SetSize(LeftSize);
		ListSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	}

	RowsGlobalWidget = WidgetTree->ConstructWidget<UProjectInnerDoctrineExchangeRowsGlobalWidget>(
		UProjectInnerDoctrineExchangeRowsGlobalWidget::StaticClass(),
		TEXT("RowsGlobalWidget"));
	AttributeListBorder->SetContent(RowsGlobalWidget);
	AttributesScrollBox = nullptr;
	AttributesLayout = nullptr;

	DetailBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DetailBorder"));
	DetailBorder->SetPadding(FMargin(0.0f));
	if (UHorizontalBoxSlot* DetailSlot = BodyRow->AddChildToHorizontalBox(DetailBorder))
	{
		DetailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	DetailOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DetailOverlay"));
	DetailBorder->SetContent(DetailOverlay);

	DetailWatermarkImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DetailWatermarkImage"));
	if (UOverlaySlot* WatermarkSlot = DetailOverlay->AddChildToOverlay(DetailWatermarkImage))
	{
		WatermarkSlot->SetHorizontalAlignment(HAlign_Right);
		WatermarkSlot->SetVerticalAlignment(VAlign_Center);
		WatermarkSlot->SetPadding(FMargin(0.0f, 28.0f, 42.0f, 34.0f));
	}

	DetailContentBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DetailContentBorder"));
	DetailContentBorder->SetPadding(FMargin(32.0f, 28.0f, 32.0f, 26.0f));
	DetailContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* DetailContentSlot = DetailOverlay->AddChildToOverlay(DetailContentBorder))
	{
		DetailContentSlot->SetHorizontalAlignment(HAlign_Fill);
		DetailContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	DetailContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DetailContentBox"));
	DetailContentBorder->SetContent(DetailContentBox);

	DetailNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailNameText"));
	if (UVerticalBoxSlot* DetailNameSlot = DetailContentBox->AddChildToVerticalBox(DetailNameText))
	{
		DetailNameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	DetailLevelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailLevelText"));
	if (UVerticalBoxSlot* DetailLevelSlot = DetailContentBox->AddChildToVerticalBox(DetailLevelText))
	{
		DetailLevelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	DetailCostText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailCostText"));
	if (UVerticalBoxSlot* DetailCostSlot = DetailContentBox->AddChildToVerticalBox(DetailCostText))
	{
		DetailCostSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	USizeBox* DetailDividerSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DetailDividerSizeBox"));
	DetailDividerSizeBox->SetWidthOverride(340.0f);
	DetailDividerSizeBox->SetHeightOverride(10.0f);
	if (UVerticalBoxSlot* DetailDividerSlot = DetailContentBox->AddChildToVerticalBox(DetailDividerSizeBox))
	{
		DetailDividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}

	DetailMilestoneDividerImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DetailMilestoneDividerImage"));
	DetailDividerSizeBox->AddChild(DetailMilestoneDividerImage);

	DetailMilestoneFiveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailMilestoneFiveText"));
	if (UVerticalBoxSlot* MilestoneFiveSlot = DetailContentBox->AddChildToVerticalBox(DetailMilestoneFiveText))
	{
		MilestoneFiveSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	DetailMilestoneTenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailMilestoneTenText"));
	if (UVerticalBoxSlot* MilestoneTenSlot = DetailContentBox->AddChildToVerticalBox(DetailMilestoneTenText))
	{
		MilestoneTenSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	DetailFlavorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailFlavorText"));
	DetailFlavorText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DetailFlavorSlot = DetailContentBox->AddChildToVerticalBox(DetailFlavorText))
	{
		DetailFlavorSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	USizeBox* FooterDividerSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FooterDividerSizeBox"));
	FooterDividerSizeBox->SetHeightOverride(10.0f);
	if (UVerticalBoxSlot* FooterDividerSlot = ContentBox->AddChildToVerticalBox(FooterDividerSizeBox))
	{
		FooterDividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	FooterDividerImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FooterDividerImage"));
	FooterDividerSizeBox->AddChild(FooterDividerImage);

	FooterRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FooterRow"));
	if (UVerticalBoxSlot* FooterRowSlot = ContentBox->AddChildToVerticalBox(FooterRow))
	{
		FooterRowSlot->SetPadding(FMargin(6.0f, 0.0f, 6.0f, 2.0f));
	}

	FooterStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterStatusText"));
	if (UHorizontalBoxSlot* FooterStatusSlot = FooterRow->AddChildToHorizontalBox(FooterStatusText))
	{
		FooterStatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		FooterStatusSlot->SetHorizontalAlignment(HAlign_Left);
		FooterStatusSlot->SetVerticalAlignment(VAlign_Center);
		FooterStatusSlot->SetPadding(FMargin(2.0f, 0.0f, 0.0f, 0.0f));
	}

	FooterControlsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FooterControlsText"));
	if (UHorizontalBoxSlot* FooterControlsSlot = FooterRow->AddChildToHorizontalBox(FooterControlsText))
	{
		FooterControlsSlot->SetHorizontalAlignment(HAlign_Right);
		FooterControlsSlot->SetVerticalAlignment(VAlign_Center);
		FooterControlsSlot->SetPadding(FMargin(20.0f, 0.0f, 0.0f, 0.0f));
	}
}

void UProjectInnerDoctrineExchangeMenuWidget::InitializeVisualTree()
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

	if (BackdropBorder)
	{
		const FSlateRoundedBoxBrush BackdropBrush(
			ProjectDoctrineExchangeMenuWidgetPrivate::BackdropTint,
			0.0f,
			FSlateColor(FLinearColor::Transparent),
			0.0f,
			FVector2f(1920.0f, 1080.0f));
		BackdropBorder->SetBrush(BackdropBrush);
	}

	if (BackgroundBorder)
	{
		const FSlateRoundedBoxBrush BackgroundBrush(
			ProjectDoctrineExchangeMenuWidgetPrivate::FrameFillTint(),
			ProjectDoctrineExchangeMenuWidgetPrivate::FrameCornerRadius,
			FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::FrameOutlineTint()),
			ProjectDoctrineExchangeMenuWidgetPrivate::FrameOutlineWidth,
			FVector2f(1800.0f, 1000.0f));
		BackgroundBorder->SetBrush(BackgroundBrush);
	}

	if (FrameTextureImage)
	{
		FrameTextureImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(FrameTexture, ProjectDoctrineExchangeMenuWidgetPrivate::FrameTexturePath)),
			false);
		FrameTextureImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FrameTextureImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::FrameOutlineTint().CopyWithNewOpacity(0.86f)));
	}

	if (HazeImage)
	{
		HazeImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(HazeTexture, ProjectDoctrineExchangeMenuWidgetPrivate::HazeTexturePath)),
			false);
		HazeImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			HazeImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::HazeTint()));
	}

	const FSlateRoundedBoxBrush SectionBrush(
		ProjectDoctrineExchangeMenuWidgetPrivate::SectionFillTint(),
		12.0f,
		FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SectionOutlineTint()),
		1.0f,
		FVector2f(600.0f, 400.0f));
	if (RunDxpBorder)
	{
		RunDxpBorder->SetBrush(SectionBrush);
	}

	if (MetaDxpBorder)
	{
		MetaDxpBorder->SetBrush(SectionBrush);
	}

	if (AttributeListBorder)
	{
		AttributeListBorder->SetBrush(SectionBrush);
	}

	if (DetailBorder)
	{
		DetailBorder->SetBrush(SectionBrush);
	}

	if (TitleText)
	{
		TitleText->SetText(LOCTEXT("MenuTitle", "INNER DOCTRINE"));
		TitleText->SetFont(MakeTitleFont(46, 2));
		TitleText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::TitleTint()));
		TitleText->SetShadowOffset(FVector2D(0.0f, 0.45f));
		TitleText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint);
	}

	if (ModeGlyphImage)
	{
		ModeGlyphImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(ModeGlyphTexture, ProjectDoctrineExchangeMenuWidgetPrivate::ModeGlyphTexturePath)),
			false);
		ModeGlyphImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			ModeGlyphImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint().CopyWithNewOpacity(0.96f)));
	}

	if (ModeText)
	{
		ModeText->SetFont(MakeTitleFont(20, 0));
		ModeText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::TitleTint()));
		ModeText->SetShadowOffset(FVector2D(0.0f, 0.22f));
		ModeText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.22f));
	}

	if (TopDividerImage)
	{
		TopDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectDoctrineExchangeMenuWidgetPrivate::DividerTexturePath)),
			false);
		TopDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			TopDividerImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint().CopyWithNewOpacity(0.88f)));
	}

	if (FooterDividerImage)
	{
		FooterDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(FooterOrnamentTexture, ProjectDoctrineExchangeMenuWidgetPrivate::FooterOrnamentTexturePath)),
			false);
		FooterDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			FooterDividerImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint().CopyWithNewOpacity(0.82f)));
	}

	if (RunDxpLabelText)
	{
		RunDxpLabelText->SetText(LOCTEXT("RunDxpLabel", "Run DXP"));
		RunDxpLabelText->SetFont(MakeBodyFont(25, 0));
		RunDxpLabelText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
	}

	if (RunDxpValueText)
	{
		RunDxpValueText->SetFont(MakeBodyFont(28, 0));
		RunDxpValueText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint()));
	}

	if (MetaDxpLabelText)
	{
		MetaDxpLabelText->SetText(LOCTEXT("MetaDxpLabel", "Meta DXP"));
		MetaDxpLabelText->SetFont(MakeBodyFont(25, 0));
		MetaDxpLabelText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
	}

	if (MetaDxpValueText)
	{
		MetaDxpValueText->SetFont(MakeBodyFont(28, 0));
		MetaDxpValueText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint()));
	}

	if (DetailNameText)
	{
		DetailNameText->SetFont(MakeTitleFont(50, 0));
		DetailNameText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
		DetailNameText->SetShadowOffset(FVector2D(0.0f, 0.65f));
		DetailNameText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint);
	}

	if (DetailLevelText)
	{
		DetailLevelText->SetFont(MakeBodyFont(28, 0));
		DetailLevelText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
	}

	if (DetailCostText)
	{
		DetailCostText->SetFont(MakeBodyFont(31, 0));
		DetailCostText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint()));
	}

	if (DetailMilestoneDividerImage)
	{
		DetailMilestoneDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectDoctrineExchangeMenuWidgetPrivate::DividerTexturePath)),
			false);
		DetailMilestoneDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			DetailMilestoneDividerImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint().CopyWithNewOpacity(0.62f)));
	}

	if (DetailMilestoneFiveText)
	{
		DetailMilestoneFiveText->SetFont(MakeBodyFont(23, 0));
		DetailMilestoneFiveText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
	}

	if (DetailMilestoneTenText)
	{
		DetailMilestoneTenText->SetFont(MakeBodyFont(23, 0));
		DetailMilestoneTenText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
	}

	if (DetailFlavorText)
	{
		DetailFlavorText->SetFont(MakeBodyFont(22, 0));
		DetailFlavorText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
		DetailFlavorText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		DetailFlavorText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.18f));
	}

	if (FooterStatusText)
	{
		FooterStatusText->SetFont(MakeBodyFont(18, 0));
		FooterStatusText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
	}

	if (FooterControlsText)
	{
		FooterControlsText->SetFont(MakeBodyFont(18, 0));
		FooterControlsText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::TitleTint()));
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerDoctrineExchangeMenuWidget::RefreshVisualState()
{
	if (ModeText)
	{
		ModeText->SetText(CachedSnapshot.bDoctrineMasteryMode ? LOCTEXT("MasteryMode", "MASTERY") : LOCTEXT("RunMode", "RUN"));
	}

	if (RunDxpValueText)
	{
		RunDxpValueText->SetText(FText::AsNumber(CachedSnapshot.CurrentRunDxp));
	}

	if (MetaDxpValueText)
	{
		MetaDxpValueText->SetText(FText::AsNumber(CachedSnapshot.MetaBankDxp));
	}

	RefreshAttributeRows();
	RefreshDetailPanel();
	RefreshFooterState();

	const FProjectInnerDoctrineExchangeResolvedEntry* SelectedEntry = GetSelectedEntry();
	OnExchangeMenuStateApplied(CachedSnapshot, SelectedIndex);
	OnExchangeMenuSelectionChanged(SelectedIndex, SelectedEntry ? SelectedEntry->AttributeName : NAME_None);
}

TArray<FProjectInnerDoctrineExchangeResolvedEntry> UProjectInnerDoctrineExchangeMenuWidget::BuildResolvedEntries() const
{
	TMap<FName, const FProjectDoctrineAttributeState*> RuntimeDataByName;
	for (const FProjectDoctrineAttributeState& AttributeState : CachedSnapshot.Attributes)
	{
		FName AttributeName = ProjectDoctrineExchangeMenuWidgetPrivate::BuildAttributeName(AttributeState.Attribute);
		if (AttributeName.IsNone())
		{
			AttributeName = FName(*AttributeState.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}

		if (!AttributeName.IsNone())
		{
			RuntimeDataByName.Add(AttributeName, &AttributeState);
		}
	}

	TArray<FProjectInnerDoctrineExchangeResolvedEntry> BuiltEntries;
	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	static const TArray<FProjectInnerDoctrineEntryDefinition> EmptyDefinitions;
	const TArray<FProjectInnerDoctrineEntryDefinition>& Definitions = UISettings
		? UISettings->InnerDoctrineEntryDefinitions
		: EmptyDefinitions;

	for (const FProjectInnerDoctrineEntryDefinition& Definition : Definitions)
	{
		const FProjectDoctrineAttributeState* const* RuntimeState = RuntimeDataByName.Find(Definition.AttributeName);
		if (!RuntimeState || !(*RuntimeState))
		{
			continue;
		}

		FProjectInnerDoctrineExchangeResolvedEntry& Entry = BuiltEntries.AddDefaulted_GetRef();
		Entry.AttributeName = Definition.AttributeName;
		Entry.Attribute = (*RuntimeState)->Attribute;
		Entry.SortOrder = Definition.SortOrder;
		Entry.RowData.AttributeName = Definition.AttributeName;
		Entry.RowData.DisplayLabel = Definition.MenuDisplayLabel.IsEmpty()
			? ProjectDoctrineExchangeMenuWidgetPrivate::NormalizeMenuLabel((*RuntimeState)->DisplayName)
			: Definition.MenuDisplayLabel;
		Entry.RowData.IconTexture = Definition.MenuIconTexture.IsNull() ? Definition.IconTexture : Definition.MenuIconTexture;
		Entry.RowData.AccentTint = EFProjectUIPalette::AttributeForName(Definition.AttributeName);
		Entry.RowData.Level = (*RuntimeState)->Level;
		Entry.RowData.Cost = (*RuntimeState)->NextLevelCost;
		Entry.RowData.RowWidth = ProjectDoctrineExchangeMenuWidgetPrivate::RowWidth;
		Entry.RowData.RowHeight = ProjectDoctrineExchangeMenuWidgetPrivate::RowHeight;
		Entry.RowData.bAffordable = CachedSnapshot.CurrentRunDxp >= (*RuntimeState)->NextLevelCost;
		Entry.DetailName = FText::FromString(Entry.RowData.DisplayLabel);
		Entry.DetailDescription = BuildAttributeFlavor((*RuntimeState)->Attribute);
		const FProjectDoctrineMilestoneState* MilestoneFive =
			ProjectDoctrineExchangeMenuWidgetPrivate::FindMilestoneState(
				CachedSnapshot,
				(*RuntimeState)->Attribute,
				5);
		const FProjectDoctrineMilestoneState* MilestoneTen =
			ProjectDoctrineExchangeMenuWidgetPrivate::FindMilestoneState(
				CachedSnapshot,
				(*RuntimeState)->Attribute,
				10);
		Entry.MilestoneFiveLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			MilestoneFive ? &MilestoneFive->Definition : nullptr);
		Entry.MilestoneTenLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			MilestoneTen ? &MilestoneTen->Definition : nullptr);
		Entry.DetailWatermarkTexture = Definition.MenuWatermarkTexture.IsNull()
			? (Definition.MenuIconTexture.IsNull() ? Definition.IconTexture : Definition.MenuIconTexture)
			: Definition.MenuWatermarkTexture;
		Entry.bMilestoneFiveUnlocked = MilestoneFive
			? MilestoneFive->bUnlocked
			: (*RuntimeState)->bMilestone5Unlocked;
		Entry.bMilestoneTenUnlocked = MilestoneTen
			? MilestoneTen->bUnlocked
			: (*RuntimeState)->bMilestone10Unlocked;
		RuntimeDataByName.Remove(Definition.AttributeName);
	}

	int32 FallbackSortOffset = 0;
	for (const TPair<FName, const FProjectDoctrineAttributeState*>& Pair : RuntimeDataByName)
	{
		if (!Pair.Value)
		{
			continue;
		}

		FProjectInnerDoctrineExchangeResolvedEntry& Entry = BuiltEntries.AddDefaulted_GetRef();
		Entry.AttributeName = Pair.Key;
		Entry.Attribute = Pair.Value->Attribute;
		Entry.SortOrder = ProjectDoctrineExchangeMenuWidgetPrivate::RuntimeFallbackSortBase + FallbackSortOffset++;
		Entry.RowData.AttributeName = Pair.Key;
		Entry.RowData.DisplayLabel = ProjectDoctrineExchangeMenuWidgetPrivate::NormalizeMenuLabel(Pair.Value->DisplayName);
		Entry.RowData.IconTexture.Reset();
		Entry.RowData.AccentTint = EFProjectUIPalette::AttributeForName(Pair.Key);
		Entry.RowData.Level = Pair.Value->Level;
		Entry.RowData.Cost = Pair.Value->NextLevelCost;
		Entry.RowData.RowWidth = ProjectDoctrineExchangeMenuWidgetPrivate::RowWidth;
		Entry.RowData.RowHeight = ProjectDoctrineExchangeMenuWidgetPrivate::RowHeight;
		Entry.RowData.bAffordable = CachedSnapshot.CurrentRunDxp >= Pair.Value->NextLevelCost;
		Entry.DetailName = FText::FromString(Entry.RowData.DisplayLabel);
		Entry.DetailDescription = BuildAttributeFlavor(Pair.Value->Attribute);
		const FProjectDoctrineMilestoneState* MilestoneFive =
			ProjectDoctrineExchangeMenuWidgetPrivate::FindMilestoneState(
				CachedSnapshot,
				Pair.Value->Attribute,
				5);
		const FProjectDoctrineMilestoneState* MilestoneTen =
			ProjectDoctrineExchangeMenuWidgetPrivate::FindMilestoneState(
				CachedSnapshot,
				Pair.Value->Attribute,
				10);
		Entry.MilestoneFiveLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			MilestoneFive ? &MilestoneFive->Definition : nullptr);
		Entry.MilestoneTenLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			MilestoneTen ? &MilestoneTen->Definition : nullptr);
		Entry.DetailWatermarkTexture.Reset();
		Entry.bMilestoneFiveUnlocked = MilestoneFive
			? MilestoneFive->bUnlocked
			: Pair.Value->bMilestone5Unlocked;
		Entry.bMilestoneTenUnlocked = MilestoneTen
			? MilestoneTen->bUnlocked
			: Pair.Value->bMilestone10Unlocked;
	}

	BuiltEntries.Sort([](const FProjectInnerDoctrineExchangeResolvedEntry& Left, const FProjectInnerDoctrineExchangeResolvedEntry& Right)
	{
		if (Left.SortOrder != Right.SortOrder)
		{
			return Left.SortOrder < Right.SortOrder;
		}

		return Left.AttributeName.LexicalLess(Right.AttributeName);
	});

	return BuiltEntries;
}

TArray<FProjectInnerDoctrineExchangeResolvedEntry> UProjectInnerDoctrineExchangeMenuWidget::BuildDesignerPreviewEntries() const
{
	struct FPreviewAttribute
	{
		FName AttributeName = NAME_None;
		EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
		int32 Level = 0;
		int32 Cost = 80;
		bool bMilestoneFiveUnlocked = false;
		bool bMilestoneTenUnlocked = false;
	};

	const TArray<FPreviewAttribute> PreviewAttributes = {
		{ FName(TEXT("Willpower")), EProjectDoctrineAttribute::Willpower, 1, 204, false, false },
		{ FName(TEXT("Offensive")), EProjectDoctrineAttribute::Offensive, 0, 80, false, false },
		{ FName(TEXT("Defensive")), EProjectDoctrineAttribute::Defensive, 0, 80, false, false },
		{ FName(TEXT("Faith")), EProjectDoctrineAttribute::Faith, 0, 80, false, false },
		{ FName(TEXT("Cunning")), EProjectDoctrineAttribute::Cunning, 0, 80, false, false },
		{ FName(TEXT("Celerity")), EProjectDoctrineAttribute::Celerity, 0, 80, false, false },
		{ FName(TEXT("Charisma")), EProjectDoctrineAttribute::Charisma, 0, 80, false, false },
	};

	const UEFProjectUISettings* UISettings = UEFProjectUISettings::Get();
	const auto FindDefinition = [UISettings](const FName AttributeName) -> const FProjectInnerDoctrineEntryDefinition*
	{
		return UISettings
			? UISettings->InnerDoctrineEntryDefinitions.FindByPredicate(
				[AttributeName](const FProjectInnerDoctrineEntryDefinition& Definition)
				{
					return Definition.AttributeName == AttributeName;
				})
			: nullptr;
	};

	TArray<FProjectInnerDoctrineExchangeResolvedEntry> BuiltEntries;
	BuiltEntries.Reserve(PreviewAttributes.Num());

	for (int32 Index = 0; Index < PreviewAttributes.Num(); ++Index)
	{
		const FPreviewAttribute& PreviewAttribute = PreviewAttributes[Index];
		const FProjectInnerDoctrineEntryDefinition* Definition = FindDefinition(PreviewAttribute.AttributeName);

		FString PreviewDisplayLabel = Definition ? Definition->MenuDisplayLabel : FString();
		if (PreviewDisplayLabel.IsEmpty() && Definition)
		{
			PreviewDisplayLabel = Definition->DisplayLabel;
		}
		if (PreviewDisplayLabel.IsEmpty())
		{
			PreviewDisplayLabel = PreviewAttribute.AttributeName.ToString();
		}

		FProjectInnerDoctrineExchangeResolvedEntry& Entry = BuiltEntries.AddDefaulted_GetRef();
		Entry.AttributeName = PreviewAttribute.AttributeName;
		Entry.Attribute = PreviewAttribute.Attribute;
		Entry.SortOrder = Definition ? Definition->SortOrder : ((Index + 1) * 10);
		Entry.RowData.AttributeName = PreviewAttribute.AttributeName;
		Entry.RowData.DisplayLabel = PreviewDisplayLabel;
		Entry.RowData.IconTexture = Definition
			? (Definition->MenuIconTexture.IsNull() ? Definition->IconTexture : Definition->MenuIconTexture)
			: TSoftObjectPtr<UTexture2D>();
		Entry.RowData.AccentTint = EFProjectUIPalette::AttributeForName(PreviewAttribute.AttributeName);
		Entry.RowData.Level = PreviewAttribute.Level;
		Entry.RowData.Cost = PreviewAttribute.Cost;
		Entry.RowData.RowWidth = ProjectDoctrineExchangeMenuWidgetPrivate::RowWidth;
		Entry.RowData.RowHeight = ProjectDoctrineExchangeMenuWidgetPrivate::RowHeight;
		Entry.RowData.bAffordable = CachedSnapshot.CurrentRunDxp >= PreviewAttribute.Cost;
		Entry.DetailName = FText::FromString(PreviewDisplayLabel);
		Entry.DetailDescription = BuildAttributeFlavor(PreviewAttribute.Attribute);
		Entry.MilestoneFiveLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			ProjectDoctrineExchangeMenuWidgetPrivate::FindConfiguredMilestone(
				PreviewAttribute.Attribute,
				5));
		Entry.MilestoneTenLabel = ProjectDoctrineExchangeMenuWidgetPrivate::BuildMilestoneLabel(
			ProjectDoctrineExchangeMenuWidgetPrivate::FindConfiguredMilestone(
				PreviewAttribute.Attribute,
				10));
		Entry.DetailWatermarkTexture = Definition
			? (Definition->MenuWatermarkTexture.IsNull()
				? (Definition->MenuIconTexture.IsNull() ? Definition->IconTexture : Definition->MenuIconTexture)
				: Definition->MenuWatermarkTexture)
			: TSoftObjectPtr<UTexture2D>();
		Entry.bMilestoneFiveUnlocked = PreviewAttribute.bMilestoneFiveUnlocked;
		Entry.bMilestoneTenUnlocked = PreviewAttribute.bMilestoneTenUnlocked;
	}

	BuiltEntries.Sort([](const FProjectInnerDoctrineExchangeResolvedEntry& Left, const FProjectInnerDoctrineExchangeResolvedEntry& Right)
	{
		if (Left.SortOrder != Right.SortOrder)
		{
			return Left.SortOrder < Right.SortOrder;
		}
		return Left.AttributeName.LexicalLess(Right.AttributeName);
	});

	return BuiltEntries;
}

bool UProjectInnerDoctrineExchangeMenuWidget::DoesRowLayoutNeedRebuild(const TArray<FProjectInnerDoctrineExchangeResolvedEntry>& InEntries) const
{
	if (CachedRowOrder.Num() != InEntries.Num())
	{
		return true;
	}

	for (int32 Index = 0; Index < InEntries.Num(); ++Index)
	{
		if (!CachedRowOrder.IsValidIndex(Index) || CachedRowOrder[Index] != InEntries[Index].AttributeName)
		{
			return true;
		}
	}

	return false;
}

void UProjectInnerDoctrineExchangeMenuWidget::RebuildAttributeRows(const TArray<FProjectInnerDoctrineExchangeResolvedEntry>& InEntries)
{
	RowWidgetsByName.Empty();
	CachedRowOrder.Reset();

	const TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> RowClass = ResolveRowWidgetClass();
	UClass* EffectiveRowClass = RowClass ? RowClass.Get() : UProjectInnerDoctrineExchangeAttributeRowWidget::StaticClass();

	if (RowsGlobalWidget)
	{
		TArray<FProjectInnerDoctrineExchangeAttributeRowDisplayData> RowDataArray;
		RowDataArray.Reserve(InEntries.Num());
		for (int32 Index = 0; Index < InEntries.Num(); ++Index)
		{
			FProjectInnerDoctrineExchangeAttributeRowDisplayData RowData = InEntries[Index].RowData;
			RowData.bSelected = (Index == SelectedIndex);
			RowData.bAffordable = CachedSnapshot.CurrentRunDxp >= RowData.Cost;
			RowDataArray.Add(RowData);
			CachedRowOrder.Add(InEntries[Index].AttributeName);
		}

		RowsGlobalWidget->ApplyRows(RowDataArray, RowClass);
		for (const FProjectInnerDoctrineExchangeResolvedEntry& Entry : InEntries)
		{
			if (UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget = RowsGlobalWidget->FindRowWidgetByAttribute(Entry.AttributeName))
			{
				RowWidgetsByName.Add(Entry.AttributeName, RowWidget);
			}
		}
		return;
	}

	if (!AttributesLayout)
	{
		return;
	}

	AttributesLayout->ClearChildren();

	for (int32 Index = 0; Index < InEntries.Num(); ++Index)
	{
		const FProjectInnerDoctrineExchangeResolvedEntry& Entry = InEntries[Index];
		UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget = nullptr;
		if (APlayerController* OwningPlayer = GetOwningPlayer())
		{
			RowWidget = CreateWidget<UProjectInnerDoctrineExchangeAttributeRowWidget>(
				OwningPlayer,
				EffectiveRowClass);
		}

		if (!RowWidget && GetWorld())
		{
			RowWidget = CreateWidget<UProjectInnerDoctrineExchangeAttributeRowWidget>(
				GetWorld(),
				EffectiveRowClass);
		}

		if (!RowWidget)
		{
			continue;
		}

		FProjectInnerDoctrineExchangeAttributeRowDisplayData RowData = Entry.RowData;
		RowData.bSelected = (Index == SelectedIndex);
		RowWidget->ApplyDisplayData(RowData);

		if (UVerticalBoxSlot* RowSlot = AttributesLayout->AddChildToVerticalBox(RowWidget))
		{
			if (Index > 0)
			{
				RowSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			}
		}

		RowWidgetsByName.Add(Entry.AttributeName, RowWidget);
		CachedRowOrder.Add(Entry.AttributeName);
	}
}

void UProjectInnerDoctrineExchangeMenuWidget::RefreshAttributeRows()
{
	for (int32 Index = 0; Index < ResolvedEntries.Num(); ++Index)
	{
		FProjectInnerDoctrineExchangeResolvedEntry& Entry = ResolvedEntries[Index];
		Entry.RowData.AccentTint =
			EFProjectUIPalette::AttributeForName(Entry.AttributeName);
		Entry.RowData.bSelected = (Index == SelectedIndex);
		Entry.RowData.bAffordable = CachedSnapshot.CurrentRunDxp >= Entry.RowData.Cost;

		if (TObjectPtr<UProjectInnerDoctrineExchangeAttributeRowWidget>* ExistingWidget = RowWidgetsByName.Find(Entry.AttributeName))
		{
			if (UProjectInnerDoctrineExchangeAttributeRowWidget* RowWidget = ExistingWidget->Get())
			{
				RowWidget->ApplyDisplayData(Entry.RowData);
				if (RowsGlobalWidget && Entry.RowData.bSelected)
				{
					RowsGlobalWidget->ScrollRowWidgetIntoView(RowWidget);
				}
				else if (AttributesScrollBox && Entry.RowData.bSelected)
				{
					AttributesScrollBox->ScrollWidgetIntoView(RowWidget, true, EDescendantScrollDestination::Center, 24.0f);
				}
			}
		}
	}
}

void UProjectInnerDoctrineExchangeMenuWidget::RefreshDetailPanel()
{
	const FProjectInnerDoctrineExchangeDetailDisplayData DetailData = BuildDetailDisplayData(GetSelectedEntry());

	if (DetailNameText)
	{
		DetailNameText->SetText(DetailData.DetailName);
	}

	if (DetailLevelText)
	{
		DetailLevelText->SetText(DetailData.LevelText);
	}

	if (DetailCostText)
	{
		DetailCostText->SetText(DetailData.CostText);
		if (bUsingNativeFallbackTree)
		{
			DetailCostText->SetColorAndOpacity(FSlateColor(DetailData.AccentTint.CopyWithNewOpacity(0.98f)));
		}
	}

	if (DetailMilestoneFiveText)
	{
		DetailMilestoneFiveText->SetText(DetailData.MilestoneFiveText);
	}

	if (DetailMilestoneTenText)
	{
		DetailMilestoneTenText->SetText(DetailData.MilestoneTenText);
	}

	if (DetailFlavorText)
	{
		DetailFlavorText->SetText(DetailData.FlavorText);
	}

	if (DetailWatermarkImage)
	{
		if (DetailData.bHasSelection)
		{
			const TSoftObjectPtr<UTexture2D> WatermarkTexture = DetailData.WatermarkTexture.IsNull()
				? (DefaultWatermarkTexture.IsNull() ? DefaultIconTexture : DefaultWatermarkTexture)
				: DetailData.WatermarkTexture;
			DetailWatermarkImage->SetBrushFromTexture(
				ResolveProjectThemeTexture(ResolveTexture(WatermarkTexture, ProjectDoctrineExchangeMenuWidgetPrivate::DefaultWatermarkTexturePath)),
				false);
			if (bUsingNativeFallbackTree)
			{
				DetailWatermarkImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
					DetailWatermarkImage,
					DetailData.AccentTint.CopyWithNewOpacity(0.08f)));
				DetailWatermarkImage->SetDesiredSizeOverride(FVector2D(340.0f, 340.0f));
			}
			DetailWatermarkImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			DetailWatermarkImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UProjectInnerDoctrineExchangeMenuWidget::RefreshFooterState()
{
	if (!FooterControlsText)
	{
		return;
	}

	FooterControlsText->SetText(LOCTEXT(
		"FooterControls",
		"W/S navigate   |   E/Enter ascend   |   R withdraw Meta DXP   |   Q/Esc close"));

	if (!FooterStatusText)
	{
		return;
	}

	const FProjectInnerDoctrineExchangeResolvedEntry* SelectedEntry = GetSelectedEntry();
	if (!SelectedEntry)
	{
		FooterStatusText->SetText(LOCTEXT("FooterUnavailable", "Approach the altar again once the player runtime has initialized."));
		if (bUsingNativeFallbackTree)
		{
			FooterStatusText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
		}
		return;
	}

	if (SelectedEntry->RowData.Cost <= 0)
	{
		FooterStatusText->SetText(LOCTEXT("FooterMaxed", "Maximum level reached."));
		if (bUsingNativeFallbackTree)
		{
			FooterStatusText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
		}
		return;
	}

	const int32 MissingDxp = FMath::Max(SelectedEntry->RowData.Cost - CachedSnapshot.CurrentRunDxp, 0);
	if (MissingDxp > 0)
	{
		FooterStatusText->SetText(FText::Format(LOCTEXT("FooterMissingDxp", "Need {0} more DXP."), FText::AsNumber(MissingDxp)));
		if (bUsingNativeFallbackTree)
		{
			FooterStatusText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::WarningTint()));
		}
		return;
	}

	FooterStatusText->SetText(LOCTEXT("FooterReady", "Ready to ascend."));
	if (bUsingNativeFallbackTree)
	{
		FooterStatusText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint()));
	}
}

void UProjectInnerDoctrineExchangeMenuWidget::NavigateSelectionByDirection(const int32 Direction)
{
	if (Direction == 0 || ResolvedEntries.IsEmpty())
	{
		return;
	}

	if (SelectedIndex == INDEX_NONE)
	{
		SelectedIndex = 0;
	}
	else
	{
		SelectedIndex = (SelectedIndex + Direction + ResolvedEntries.Num()) % ResolvedEntries.Num();
	}

	RefreshVisualState();
}

void UProjectInnerDoctrineExchangeMenuWidget::ConfirmSelection()
{
	const FProjectInnerDoctrineExchangeResolvedEntry* SelectedEntry = GetSelectedEntry();
	if (!SelectedEntry)
	{
		return;
	}

	OnPurchaseRequested.Broadcast(SelectedEntry->Attribute);
}

bool UProjectInnerDoctrineExchangeMenuWidget::HandleMenuKey(const FKey& Key)
{
	if (Key == EKeys::Up || Key == EKeys::W)
	{
		NavigateSelectionByDirection(-1);
		return true;
	}

	if (Key == EKeys::Down || Key == EKeys::S)
	{
		NavigateSelectionByDirection(1);
		return true;
	}

	if (Key == EKeys::Enter || Key == EKeys::E)
	{
		ConfirmSelection();
		return true;
	}

	if (Key == EKeys::R)
	{
		OnWithdrawRequested.Broadcast();
		return true;
	}

	if (Key == EKeys::Q || Key == EKeys::Escape)
	{
		OnCloseRequested.Broadcast();
		return true;
	}

	return false;
}

TSubclassOf<UProjectInnerDoctrineExchangeAttributeRowWidget> UProjectInnerDoctrineExchangeMenuWidget::ResolveRowWidgetClass() const
{
	if (!AttributeRowWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = AttributeRowWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	if (UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClassWithPriority(
		FSoftClassPath(),
		UProjectInnerDoctrineExchangeAttributeRowGlobalWidget::StaticClass(),
		TEXT("ProjectInnerDoctrineAltarExchangeAttributeRow"),
		TEXT("InnerDoctrineAltar")))
	{
		if (ResolvedClass->IsChildOf(UProjectInnerDoctrineExchangeAttributeRowWidget::StaticClass()))
		{
			return ResolvedClass;
		}
	}

	return UProjectInnerDoctrineExchangeAttributeRowGlobalWidget::StaticClass();
}

UTexture2D* UProjectInnerDoctrineExchangeMenuWidget::ResolveTexture(
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

	return Cast<UTexture2D>(ProjectDoctrineExchangeMenuWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerDoctrineExchangeMenuWidget::ResolveStyleAsset(
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

	return ProjectDoctrineExchangeMenuWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerDoctrineExchangeMenuWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectDoctrineExchangeMenuWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerDoctrineExchangeMenuWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectDoctrineExchangeMenuWidgetPrivate::BodyFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FText UProjectInnerDoctrineExchangeMenuWidget::BuildAttributeFlavor(const EProjectDoctrineAttribute Attribute) const
{
	switch (Attribute)
	{
	case EProjectDoctrineAttribute::Willpower:
		return LOCTEXT("WillpowerFlavor", "Expands survival limits and protects the mind from Fear and Dizzy.");
	case EProjectDoctrineAttribute::Offensive:
		return LOCTEXT("OffensiveFlavor", "Improves non-magical damage.");
	case EProjectDoctrineAttribute::Defensive:
		return LOCTEXT("DefensiveFlavor", "Reduces post-armour damage.");
	case EProjectDoctrineAttribute::Faith:
		return LOCTEXT("FaithFlavor", "Improves magic and calms Madness.");
	case EProjectDoctrineAttribute::Cunning:
		return LOCTEXT("CunningFlavor", "Improves lockpicking and struggle control and helps retain equipment after defeat.");
	case EProjectDoctrineAttribute::Celerity:
		return FText::GetEmpty();
	case EProjectDoctrineAttribute::Charisma:
		return LOCTEXT("CharismaFlavor", "Improves social progression and unlocks optional adult interactions at level 10.");
	case EProjectDoctrineAttribute::Count:
	default:
		break;
	}

	return FText::GetEmpty();
}

FText UProjectInnerDoctrineExchangeMenuWidget::BuildMilestoneStatusText(const FText& LabelText, const bool bUnlocked) const
{
	if (LabelText.IsEmpty())
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		LOCTEXT("MilestoneStatusFormat", "{0} {1}"),
		LabelText,
		bUnlocked ? LOCTEXT("MilestoneUnlocked", "unlocked") : LOCTEXT("MilestoneLocked", "locked"));
}

FProjectInnerDoctrineExchangeDetailDisplayData UProjectInnerDoctrineExchangeMenuWidget::BuildDetailDisplayData(
	const FProjectInnerDoctrineExchangeResolvedEntry* SelectedEntry) const
{
	FProjectInnerDoctrineExchangeDetailDisplayData DetailData;
	if (!SelectedEntry)
	{
		DetailData.DetailName = LOCTEXT("UnavailableTitle", "Inner Doctrine unavailable");
		DetailData.LevelText = LOCTEXT("UnavailableLevel", "No player DXP component was found for this interaction.");
		DetailData.CostText = FText::GetEmpty();
		DetailData.MilestoneFiveText = FText::GetEmpty();
		DetailData.MilestoneTenText = FText::GetEmpty();
		DetailData.FlavorText = LOCTEXT("UnavailableFlavor", "Approach the altar again once the player runtime has initialized.");
		DetailData.bHasSelection = false;
		return DetailData;
	}

	DetailData.AttributeName = SelectedEntry->AttributeName;
	DetailData.DetailName = SelectedEntry->DetailName;
	DetailData.LevelText = FText::Format(
		LOCTEXT("SelectedLevelText", "Current Level {0}"),
		FText::AsNumber(SelectedEntry->RowData.Level));
	DetailData.CostText = SelectedEntry->RowData.Cost > 0
		? FText::Format(LOCTEXT("SelectedCostText", "Next ascent costs {0} DXP"), FText::AsNumber(SelectedEntry->RowData.Cost))
		: LOCTEXT("AlreadyMaxedText", "Already maxed");
	DetailData.MilestoneFiveText = BuildMilestoneStatusText(SelectedEntry->MilestoneFiveLabel, SelectedEntry->bMilestoneFiveUnlocked);
	DetailData.MilestoneTenText = BuildMilestoneStatusText(SelectedEntry->MilestoneTenLabel, SelectedEntry->bMilestoneTenUnlocked);
	DetailData.FlavorText = SelectedEntry->DetailDescription;
	DetailData.WatermarkTexture = SelectedEntry->DetailWatermarkTexture;
	DetailData.AccentTint =
		EFProjectUIPalette::AttributeForName(SelectedEntry->AttributeName);
	DetailData.bHasSelection = true;
	return DetailData;
}

const FProjectInnerDoctrineExchangeResolvedEntry* UProjectInnerDoctrineExchangeMenuWidget::GetSelectedEntry() const
{
	return ResolvedEntries.IsValidIndex(SelectedIndex) ? &ResolvedEntries[SelectedIndex] : nullptr;
}

UProjectInnerDoctrineExchangeDetailPanelWidget::UProjectInnerDoctrineExchangeDetailPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TitleFontAsset = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::TitleFontPath);
	BodyFontAsset = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::BodyFontPath);
	DividerTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DividerTexturePath);
	DefaultWatermarkTexture = FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DefaultWatermarkTexturePath);

	CurrentDisplayData.AttributeName = FName(TEXT("Willpower"));
	CurrentDisplayData.DetailName = LOCTEXT("DetailPreviewName", "Willpower");
	CurrentDisplayData.LevelText = LOCTEXT("DetailPreviewLevel", "Current Level 1");
	CurrentDisplayData.CostText = LOCTEXT("DetailPreviewCost", "Next ascent costs 204 DXP");
	CurrentDisplayData.MilestoneFiveText = LOCTEXT("DetailPreviewMilestoneFive", "Second Breath locked");
	CurrentDisplayData.MilestoneTenText = LOCTEXT("DetailPreviewMilestoneTen", "Unbroken Mind locked");
	CurrentDisplayData.FlavorText = LOCTEXT("DetailPreviewFlavor", "Endurance for long expeditions: expands survival limits, grants a second breath between floors, and steadies the mind against fear and confusion.");
	CurrentDisplayData.WatermarkTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(ProjectDoctrineExchangeMenuWidgetPrivate::DefaultWatermarkTexturePath));
	CurrentDisplayData.AccentTint = ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint();
	CurrentDisplayData.bHasSelection = true;
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectInnerDoctrineExchangeDetailPanelWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	bVisualTreeInitialized = false;
	const bool bBuiltTree = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuiltTree)
	{
		InitializeVisualTree();
		RefreshVisuals();
	}
	return bBuiltTree;
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::ApplyDetailData(
	const FProjectInnerDoctrineExchangeDetailDisplayData& InDisplayData)
{
	CurrentDisplayData = InDisplayData;
	BuildWidgetTree();
	InitializeVisualTree();
	RefreshVisuals();
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (DetailBorder && WidgetTree->RootWidget == DetailBorder)
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

bool UProjectInnerDoctrineExchangeDetailPanelWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	DetailBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DetailBorder"));
	DetailBorder->SetPadding(FMargin(0.0f));
	TargetWidgetTree->RootWidget = DetailBorder;

	DetailOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DetailOverlay"));
	DetailBorder->SetContent(DetailOverlay);

	DetailWatermarkImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DetailWatermarkImage"));
	if (UOverlaySlot* WatermarkSlot = DetailOverlay->AddChildToOverlay(DetailWatermarkImage))
	{
		WatermarkSlot->SetHorizontalAlignment(HAlign_Right);
		WatermarkSlot->SetVerticalAlignment(VAlign_Center);
		WatermarkSlot->SetPadding(FMargin(0.0f, 28.0f, 42.0f, 34.0f));
	}

	DetailContentBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DetailContentBorder"));
	DetailContentBorder->SetPadding(FMargin(32.0f, 28.0f, 32.0f, 26.0f));
	DetailContentBorder->SetBrushColor(FLinearColor::Transparent);
	if (UOverlaySlot* DetailContentSlot = DetailOverlay->AddChildToOverlay(DetailContentBorder))
	{
		DetailContentSlot->SetHorizontalAlignment(HAlign_Fill);
		DetailContentSlot->SetVerticalAlignment(VAlign_Fill);
	}

	DetailContentBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DetailContentBox"));
	DetailContentBorder->SetContent(DetailContentBox);

	DetailNameText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailNameText"));
	if (UVerticalBoxSlot* DetailNameSlot = DetailContentBox->AddChildToVerticalBox(DetailNameText))
	{
		DetailNameSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	DetailLevelText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailLevelText"));
	if (UVerticalBoxSlot* DetailLevelSlot = DetailContentBox->AddChildToVerticalBox(DetailLevelText))
	{
		DetailLevelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	DetailCostText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailCostText"));
	if (UVerticalBoxSlot* DetailCostSlot = DetailContentBox->AddChildToVerticalBox(DetailCostText))
	{
		DetailCostSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	USizeBox* DetailDividerSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DetailDividerSizeBox"));
	DetailDividerSizeBox->SetWidthOverride(340.0f);
	DetailDividerSizeBox->SetHeightOverride(10.0f);
	if (UVerticalBoxSlot* DetailDividerSlot = DetailContentBox->AddChildToVerticalBox(DetailDividerSizeBox))
	{
		DetailDividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}

	DetailMilestoneDividerImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DetailMilestoneDividerImage"));
	DetailDividerSizeBox->AddChild(DetailMilestoneDividerImage);

	DetailMilestoneFiveText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailMilestoneFiveText"));
	if (UVerticalBoxSlot* MilestoneFiveSlot = DetailContentBox->AddChildToVerticalBox(DetailMilestoneFiveText))
	{
		MilestoneFiveSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	DetailMilestoneTenText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailMilestoneTenText"));
	if (UVerticalBoxSlot* MilestoneTenSlot = DetailContentBox->AddChildToVerticalBox(DetailMilestoneTenText))
	{
		MilestoneTenSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	DetailFlavorText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailFlavorText"));
	DetailFlavorText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* DetailFlavorSlot = DetailContentBox->AddChildToVerticalBox(DetailFlavorText))
	{
		DetailFlavorSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	return true;
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::InitializeVisualTree()
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

	if (DetailBorder)
	{
		const FSlateRoundedBoxBrush SectionBrush(
			ProjectDoctrineExchangeMenuWidgetPrivate::SectionFillTint(),
			12.0f,
			FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SectionOutlineTint()),
			1.0f,
			FVector2f(600.0f, 400.0f));
		DetailBorder->SetBrush(SectionBrush);
	}

	if (DetailNameText)
	{
		DetailNameText->SetFont(MakeTitleFont(50, 0));
		DetailNameText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
		DetailNameText->SetShadowOffset(FVector2D(0.0f, 0.65f));
		DetailNameText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint);
	}

	if (DetailLevelText)
	{
		DetailLevelText->SetFont(MakeBodyFont(28, 0));
		DetailLevelText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::PrimaryTextTint()));
	}

	if (DetailCostText)
	{
		DetailCostText->SetFont(MakeBodyFont(31, 0));
		DetailCostText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint()));
	}

	if (DetailMilestoneDividerImage)
	{
		DetailMilestoneDividerImage->SetBrushFromTexture(
			ResolveProjectThemeTexture(ResolveTexture(DividerTexture, ProjectDoctrineExchangeMenuWidgetPrivate::DividerTexturePath)),
			false);
		DetailMilestoneDividerImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
			DetailMilestoneDividerImage,
			ProjectDoctrineExchangeMenuWidgetPrivate::AccentTint().CopyWithNewOpacity(0.62f)));
	}

	if (DetailMilestoneFiveText)
	{
		DetailMilestoneFiveText->SetFont(MakeBodyFont(23, 0));
		DetailMilestoneFiveText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
	}

	if (DetailMilestoneTenText)
	{
		DetailMilestoneTenText->SetFont(MakeBodyFont(23, 0));
		DetailMilestoneTenText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
	}

	if (DetailFlavorText)
	{
		DetailFlavorText->SetFont(MakeBodyFont(22, 0));
		DetailFlavorText->SetColorAndOpacity(FSlateColor(ProjectDoctrineExchangeMenuWidgetPrivate::SecondaryTextTint()));
		DetailFlavorText->SetShadowOffset(FVector2D(0.0f, 0.35f));
		DetailFlavorText->SetShadowColorAndOpacity(ProjectDoctrineExchangeMenuWidgetPrivate::ShadowTint.CopyWithNewOpacity(0.18f));
	}

	bVisualTreeInitialized = true;
}

void UProjectInnerDoctrineExchangeDetailPanelWidget::RefreshVisuals()
{
	if (DetailNameText)
	{
		DetailNameText->SetText(CurrentDisplayData.DetailName);
	}

	if (DetailLevelText)
	{
		DetailLevelText->SetText(CurrentDisplayData.LevelText);
	}

	if (DetailCostText)
	{
		DetailCostText->SetText(CurrentDisplayData.CostText);
		if (bUsingNativeFallbackTree)
		{
			DetailCostText->SetColorAndOpacity(FSlateColor(CurrentDisplayData.AccentTint.CopyWithNewOpacity(0.98f)));
		}
	}

	if (DetailMilestoneFiveText)
	{
		DetailMilestoneFiveText->SetText(CurrentDisplayData.MilestoneFiveText);
	}

	if (DetailMilestoneTenText)
	{
		DetailMilestoneTenText->SetText(CurrentDisplayData.MilestoneTenText);
	}

	if (DetailFlavorText)
	{
		DetailFlavorText->SetText(CurrentDisplayData.FlavorText);
	}

	if (DetailWatermarkImage)
	{
		if (CurrentDisplayData.bHasSelection)
		{
			const TSoftObjectPtr<UTexture2D> WatermarkTexture = CurrentDisplayData.WatermarkTexture.IsNull()
				? DefaultWatermarkTexture
				: CurrentDisplayData.WatermarkTexture;
			DetailWatermarkImage->SetBrushFromTexture(
				ResolveProjectThemeTexture(ResolveTexture(WatermarkTexture, ProjectDoctrineExchangeMenuWidgetPrivate::DefaultWatermarkTexturePath)),
				false);
			if (bUsingNativeFallbackTree)
			{
				DetailWatermarkImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
					DetailWatermarkImage,
					CurrentDisplayData.AccentTint.CopyWithNewOpacity(0.08f)));
				DetailWatermarkImage->SetDesiredSizeOverride(FVector2D(340.0f, 340.0f));
			}
			DetailWatermarkImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			DetailWatermarkImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	OnExchangeDetailPanelDataApplied(CurrentDisplayData);
}

UTexture2D* UProjectInnerDoctrineExchangeDetailPanelWidget::ResolveTexture(
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

	return Cast<UTexture2D>(ProjectDoctrineExchangeMenuWidgetPrivate::LoadObjectByPath(FallbackPath));
}

UObject* UProjectInnerDoctrineExchangeDetailPanelWidget::ResolveStyleAsset(
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

	return ProjectDoctrineExchangeMenuWidgetPrivate::LoadObjectByPath(FallbackPath);
}

FSlateFontInfo UProjectInnerDoctrineExchangeDetailPanelWidget::MakeTitleFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(TitleFontAsset, ProjectDoctrineExchangeMenuWidgetPrivate::TitleFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

FSlateFontInfo UProjectInnerDoctrineExchangeDetailPanelWidget::MakeBodyFont(const int32 Size, const int32 LetterSpacing) const
{
	FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	FontInfo.Size = Size;
	FontInfo.LetterSpacing = LetterSpacing;
	if (UObject* FontObject = ResolveStyleAsset(BodyFontAsset, ProjectDoctrineExchangeMenuWidgetPrivate::BodyFontPath))
	{
		FontInfo.FontObject = FontObject;
		FontInfo.TypefaceFontName = NAME_None;
	}

	return FontInfo;
}

#undef LOCTEXT_NAMESPACE
