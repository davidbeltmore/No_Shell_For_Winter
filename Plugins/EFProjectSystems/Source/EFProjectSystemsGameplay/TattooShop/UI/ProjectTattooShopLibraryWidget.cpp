#include "TattooShop/UI/ProjectTattooShopLibraryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EFProjectUIPalette.h"
#include "Styling/CoreStyle.h"
#include "TattooShop/UI/ProjectTattooShopCardWidget.h"
#include "TattooShop/UI/ProjectTattooShopColorPickerWidget.h"
#include "TattooShop/UI/ProjectTattooShopEditorWidget.h"
#include "UI/ProjectWidgetClassResolver.h"

namespace ProjectTattooShopLibraryWidgetPrivate
{
	FSlateFontInfo Font(const int32 Size, const bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FName Name, const FText& Text, const int32 Size, const bool bBold = false)
	{
		UTextBlock* Result = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Result->SetText(Text);
		Result->SetFont(Font(Size, bBold));
		Result->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::PrimaryText()));
		return Result;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FName Name, const FText& Label)
	{
		UButton* Result = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* LabelText = MakeText(Tree, *FString::Printf(TEXT("%sLabel"), *Name.ToString()), Label, 10, true);
		LabelText->SetJustification(ETextJustify::Center);
		Result->AddChild(LabelText);
		Result->SetBackgroundColor(EFProjectUIPalette::SectionFill(0.98f));
		return Result;
	}

	void AddPanelHeader(
		UWidgetTree* Tree,
		UVerticalBox* Parent,
		const TCHAR* Prefix,
		const FText& Title,
		const FText& Description)
	{
		if (!Tree || !Parent)
		{
			return;
		}

		UTextBlock* TitleText = MakeText(
			Tree,
			*FString::Printf(TEXT("%sAccentText"), Prefix),
			Title,
			17,
			true);
		TitleText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
		Parent->AddChildToVerticalBox(TitleText);

		UTextBlock* DescriptionText = MakeText(
			Tree,
			*FString::Printf(TEXT("%sDescriptionText"), Prefix),
			Description,
			9);
		DescriptionText->SetAutoWrapText(true);
		DescriptionText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
		if (UVerticalBoxSlot* DescriptionSlot = Parent->AddChildToVerticalBox(DescriptionText))
		{
			DescriptionSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 7.0f));
		}

		USizeBox* DividerSize = Tree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			*FString::Printf(TEXT("%sDividerSizeBox"), Prefix));
		DividerSize->SetHeightOverride(2.0f);
		UBorder* Divider = Tree->ConstructWidget<UBorder>(
			UBorder::StaticClass(),
			*FString::Printf(TEXT("%sAccentDividerBorder"), Prefix));
		Divider->SetBrushColor(EFProjectUIPalette::AccentSoft(0.82f));
		DividerSize->SetContent(Divider);
		if (UVerticalBoxSlot* DividerSlot = Parent->AddChildToVerticalBox(DividerSize))
		{
			DividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		}
	}

	void AddSectionHeading(
		UWidgetTree* Tree,
		UVerticalBox* Parent,
		const FName Name,
		const FText& Title,
		const FText& Description)
	{
		UTextBlock* TitleText = MakeText(Tree, Name, Title, 11, true);
		TitleText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::AccentSoft()));
		Parent->AddChildToVerticalBox(TitleText);

		UTextBlock* DescriptionText = MakeText(
			Tree,
			*FString::Printf(TEXT("%sDescriptionText"), *Name.ToString()),
			Description,
			8);
		DescriptionText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
		if (UVerticalBoxSlot* DescriptionSlot = Parent->AddChildToVerticalBox(DescriptionText))
		{
			DescriptionSlot->SetPadding(FMargin(0.0f, 1.0f, 0.0f, 5.0f));
		}
	}

	void AddManifestAsset(
		FCodeWidgetDesignerConversionManifest& Manifest,
		const TSubclassOf<UUserWidget> WidgetClass,
		const TCHAR* TargetPath,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 Priority,
		const bool bRuntimeDefault,
		const TArray<FName>& ExpectedNames)
	{
		FCodeWidgetDesignerWidgetAssetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.TargetAssetPath = TargetPath;
		Spec.Role = Role;
		Spec.PriorityGroup = TEXT("TattooShop");
		Spec.PriorityRank = Priority;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.ExpectedWidgetNames = ExpectedNames;
		Manifest.WidgetAssets.Add(Spec);
	}
}

UProjectTattooShopLibraryWidget::UProjectTattooShopLibraryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CurrentStatusText = FText::FromString(TEXT("Select a design to add or an applied tattoo to edit."));
}

void UProjectTattooShopLibraryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RebuildAllCards();
	RefreshActionState();
	RefreshPresentationMode();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectTattooShopLibraryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTreeIfNeeded();
	BindControls();
	RebuildAllCards();
	RefreshActionState();
	RefreshPresentationMode();
	ReapplyProjectThemeAfterNativeConstruct();
}

bool UProjectTattooShopLibraryWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	return BuildDefaultWidgetTree(TargetWidgetTree);
}

bool UProjectTattooShopLibraryWidget::GatherCodeWidgetDesignerConversionManifest(
	FCodeWidgetDesignerConversionManifest& OutManifest) const
{
	using namespace ProjectTattooShopLibraryWidgetPrivate;

	OutManifest = FCodeWidgetDesignerConversionManifest();
	OutManifest.SystemName = TEXT("TattooShop");
	OutManifest.RootPath = TEXT("/Game/_Game/Widgets/TattooShop");
	OutManifest.MainFolder = TEXT("Main");
	OutManifest.GlobalFolder = TEXT("Global");
	OutManifest.GenerationMode = ECodeWidgetDesignerGenerationMode::PreserveManual;

	OutManifest.HostWidget.WidgetClass = UProjectTattooShopLibraryWidget::StaticClass();
	OutManifest.HostWidget.TargetAssetPath = TEXT("/Game/_Game/Widgets/TattooShop/Main/WBP_ProjectTattooLibrary");
	OutManifest.HostWidget.Role = ECodeWidgetDesignerAssetRole::Host;
	OutManifest.HostWidget.PriorityGroup = TEXT("TattooShop");
	OutManifest.HostWidget.PriorityRank = 20000;
	OutManifest.HostWidget.bRuntimeDefault = true;
	OutManifest.HostWidget.ExpectedWidgetNames =
	{
		TEXT("RootSizeBox"), TEXT("BackgroundBorder"), TEXT("ManagementToolbar"), TEXT("CatalogSection"), TEXT("ManagementSection"),
		TEXT("ManagementAccentText"), TEXT("CatalogAccentText"),
		TEXT("UploadButton"), TEXT("DeleteButton"),
		TEXT("AddButton"), TEXT("EditButton"), TEXT("RemoveButton"),
		TEXT("LibraryGrid"), TEXT("ManualGrid"), TEXT("AutomaticGrid"), TEXT("StatusText"),
		TEXT("ManualAccentText"), TEXT("AutomaticAccentText")
	};

	AddManifestAsset(
		OutManifest,
		UProjectTattooShopEditorWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/TattooShop/Main/WBP_ProjectTattooEditor"),
		ECodeWidgetDesignerAssetRole::MainBase,
		19000,
		true,
		{ TEXT("BackgroundBorder"), TEXT("TitleText"), TEXT("EditorScrollBox"), TEXT("PlacementComboBox"),
			TEXT("EditorAccentText"), TEXT("LocationAccentText"), TEXT("TransformAccentText"), TEXT("AppearanceAccentText"),
			TEXT("OffsetXSlider"), TEXT("OffsetYSlider"), TEXT("SizeSlider"), TEXT("RotationSlider"),
			TEXT("ProjectionSlider"), TEXT("OpacitySlider"), TEXT("EnabledCheckBox"), TEXT("TintEnabledCheckBox"), TEXT("ColorPicker"),
			TEXT("AcceptButton"), TEXT("CancelButton") });
	AddManifestAsset(
		OutManifest,
		UProjectTattooShopCardWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/TattooShop/Global/WBP_ProjectTattooCard"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		18000,
		true,
		{ TEXT("RootSizeBox"), TEXT("SelectionBorder"), TEXT("SelectedFrameBorder"), TEXT("CardButton"), TEXT("ThumbnailImage"), TEXT("NameText"), TEXT("SubtitleText") });
	AddManifestAsset(
		OutManifest,
		UProjectTattooShopColorPickerWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/TattooShop/Global/WBP_ProjectTattooColorPicker"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		17000,
		true,
		{ TEXT("RootBox"), TEXT("ColorPreviewBorder"), TEXT("RedSlider"), TEXT("GreenSlider"), TEXT("BlueSlider") });

	return true;
}

void UProjectTattooShopLibraryWidget::GatherCodeWidgetDesignerChildWidgetClasses(
	TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const
{
	OutWidgetClasses =
	{
		UProjectTattooShopEditorWidget::StaticClass(),
		UProjectTattooShopCardWidget::StaticClass(),
		UProjectTattooShopColorPickerWidget::StaticClass()
	};
}

void UProjectTattooShopLibraryWidget::GatherCodeWidgetDesignerChildWidgetSpecs(
	TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	OutWidgetSpecs.Reset();

	auto AddSpec = [&OutWidgetSpecs](
		const TSubclassOf<UUserWidget> WidgetClass,
		const TCHAR* AssetName,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank)
	{
		FCodeWidgetDesignerChildWidgetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.RelativeFolder = Role == ECodeWidgetDesignerAssetRole::MainBase ? TEXT("Main") : TEXT("Global");
		Spec.AssetNameOverride = AssetName;
		Spec.Role = Role;
		Spec.PriorityGroup = TEXT("TattooShop");
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = true;
		OutWidgetSpecs.Add(Spec);
	};

	AddSpec(UProjectTattooShopEditorWidget::StaticClass(), TEXT("WBP_ProjectTattooEditor"), ECodeWidgetDesignerAssetRole::MainBase, 19000);
	AddSpec(UProjectTattooShopCardWidget::StaticClass(), TEXT("WBP_ProjectTattooCard"), ECodeWidgetDesignerAssetRole::GlobalTemplate, 18000);
	AddSpec(UProjectTattooShopColorPickerWidget::StaticClass(), TEXT("WBP_ProjectTattooColorPicker"), ECodeWidgetDesignerAssetRole::GlobalTemplate, 17000);
}

void UProjectTattooShopLibraryWidget::SetLibraryEntries(const TArray<FProjectTattooShopCardData>& InEntries)
{
	LibraryEntries = InEntries;
	for (FProjectTattooShopCardData& Entry : LibraryEntries)
	{
		Entry.Kind = EProjectTattooShopCardKind::Catalog;
	}
	RebuildCardPanel(LibraryGrid, LibraryEntries);
}

void UProjectTattooShopLibraryWidget::SetManualTattoos(const TArray<FProjectTattooShopCardData>& InEntries)
{
	ManualEntries = InEntries;
	for (FProjectTattooShopCardData& Entry : ManualEntries)
	{
		Entry.Kind = EProjectTattooShopCardKind::Manual;
		Entry.bReadOnly = false;
	}
	RebuildCardPanel(ManualGrid, ManualEntries);
}

void UProjectTattooShopLibraryWidget::SetAutomaticTattoos(const TArray<FProjectTattooShopCardData>& InEntries)
{
	AutomaticEntries = InEntries;
	for (FProjectTattooShopCardData& Entry : AutomaticEntries)
	{
		Entry.Kind = EProjectTattooShopCardKind::Automatic;
		Entry.bReadOnly = true;
	}
	RebuildCardPanel(AutomaticGrid, AutomaticEntries);
}

void UProjectTattooShopLibraryWidget::SetSelectedEntry(const FProjectTattooShopCardData& InSelection)
{
	SelectedEntry = InSelection;
	bHasSelection = true;
	RebuildAllCards();
	RefreshActionState();
	OnSelectionChanged.Broadcast(SelectedEntry);
}

void UProjectTattooShopLibraryWidget::ClearSelection()
{
	bHasSelection = false;
	SelectedEntry = FProjectTattooShopCardData();
	RebuildAllCards();
	RefreshActionState();
}

void UProjectTattooShopLibraryWidget::SetStatusText(const FText& InStatusText)
{
	CurrentStatusText = InStatusText;
	if (StatusText)
	{
		StatusText->SetText(CurrentStatusText);
	}
}

void UProjectTattooShopLibraryWidget::SetPresentationMode(
	const EProjectTattooShopLibraryPresentationMode InPresentationMode)
{
	PresentationMode = InPresentationMode;
	RefreshPresentationMode();
}

void UProjectTattooShopLibraryWidget::BuildWidgetTreeIfNeeded()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectTattooShopLibraryWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	using namespace ProjectTattooShopLibraryWidgetPrivate;
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(320.0f);
	RootSizeBox->SetHeightOverride(680.0f);
	TargetWidgetTree->RootWidget = RootSizeBox;

	BackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetPadding(FMargin(12.0f));
	RootSizeBox->SetContent(BackgroundBorder);

	UVerticalBox* RootBox = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootBox"));
	BackgroundBorder->SetContent(RootBox);

	ManagementToolbar = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ManagementToolbar"));
	RootBox->AddChildToVerticalBox(ManagementToolbar);
	AddPanelHeader(
		TargetWidgetTree,
		ManagementToolbar,
		TEXT("Management"),
		FText::FromString(TEXT("TATTOOS")),
		FText::FromString(TEXT("Manage the layers placed on your character.")));

	UHorizontalBox* Header = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SourceActionBox"));
	if (UVerticalBoxSlot* SourceActionsSlot = ManagementToolbar->AddChildToVerticalBox(Header))
	{
		SourceActionsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}
	UploadButton = MakeButton(TargetWidgetTree, TEXT("UploadButton"), FText::FromString(TEXT("UPLOAD PNG")));
	UploadButton->SetToolTipText(FText::FromString(TEXT("Import a PNG into the tattoo library.")));
	DeleteButton = MakeButton(TargetWidgetTree, TEXT("DeleteButton"), FText::FromString(TEXT("DELETE SOURCE")));
	DeleteButton->SetToolTipText(FText::FromString(TEXT("Delete the selected uploaded PNG when it is not in use.")));
	if (UHorizontalBoxSlot* HeaderUploadSlot = Header->AddChildToHorizontalBox(UploadButton)) HeaderUploadSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (UHorizontalBoxSlot* HeaderDeleteSlot = Header->AddChildToHorizontalBox(DeleteButton)) { HeaderDeleteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); HeaderDeleteSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f)); }

	UHorizontalBox* Actions = TargetWidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LayerActionBox"));
	if (UVerticalBoxSlot* ActionsSlot = ManagementToolbar->AddChildToVerticalBox(Actions)) ActionsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	AddButton = MakeButton(TargetWidgetTree, TEXT("AddButton"), FText::FromString(TEXT("ADD")));
	EditButton = MakeButton(TargetWidgetTree, TEXT("EditButton"), FText::FromString(TEXT("EDIT")));
	RemoveButton = MakeButton(TargetWidgetTree, TEXT("RemoveButton"), FText::FromString(TEXT("REMOVE")));
	AddButton->SetToolTipText(FText::FromString(TEXT("Add the selected library design as a new layer.")));
	EditButton->SetToolTipText(FText::FromString(TEXT("Edit the selected manual tattoo layer.")));
	RemoveButton->SetToolTipText(FText::FromString(TEXT("Remove the selected manual tattoo layer.")));
	if (UHorizontalBoxSlot* AddButtonSlot = Actions->AddChildToHorizontalBox(AddButton)) AddButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (UHorizontalBoxSlot* EditButtonSlot = Actions->AddChildToHorizontalBox(EditButton)) { EditButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); EditButtonSlot->SetPadding(FMargin(6.0f, 0.0f)); }
	if (UHorizontalBoxSlot* RemoveButtonSlot = Actions->AddChildToHorizontalBox(RemoveButton)) RemoveButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	StatusText = MakeText(TargetWidgetTree, TEXT("StatusText"), CurrentStatusText, 10);
	StatusText->SetAutoWrapText(true);
	StatusText->SetColorAndOpacity(FSlateColor(EFProjectUIPalette::SecondaryText()));
	if (UVerticalBoxSlot* StatusSlot = RootBox->AddChildToVerticalBox(StatusText)) StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 9.0f));

	UScrollBox* Scroll = TargetWidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LibraryScrollBox"));
	if (UVerticalBoxSlot* ScrollSlot = RootBox->AddChildToVerticalBox(Scroll)) ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* Sections = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SectionsBox"));
	Scroll->AddChild(Sections);

	CatalogSection = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CatalogSection"));
	Sections->AddChildToVerticalBox(CatalogSection);
	AddPanelHeader(
		TargetWidgetTree,
		CatalogSection,
		TEXT("Catalog"),
		FText::FromString(TEXT("DESIGN LIBRARY")),
		FText::FromString(TEXT("Choose a design, then use ADD to place it.")));
	LibraryGrid = TargetWidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("LibraryGrid"));
	LibraryGrid->SetMinDesiredSlotWidth(132.0f);
	LibraryGrid->SetMinDesiredSlotHeight(116.0f);
	if (UVerticalBoxSlot* LibraryGridSlot = CatalogSection->AddChildToVerticalBox(LibraryGrid)) LibraryGridSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 12.0f));

	ManagementSection = TargetWidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ManagementSection"));
	Sections->AddChildToVerticalBox(ManagementSection);
	AddSectionHeading(
		TargetWidgetTree,
		ManagementSection,
		TEXT("ManualAccentText"),
		FText::FromString(TEXT("MANUAL LAYERS")),
		FText::FromString(TEXT("Select a layer to edit or remove it.")));
	ManualGrid = TargetWidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("ManualGrid"));
	ManualGrid->SetMinDesiredSlotWidth(136.0f);
	ManualGrid->SetMinDesiredSlotHeight(116.0f);
	if (UVerticalBoxSlot* ManualGridSlot = ManagementSection->AddChildToVerticalBox(ManualGrid)) ManualGridSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 12.0f));

	AddSectionHeading(
		TargetWidgetTree,
		ManagementSection,
		TEXT("AutomaticAccentText"),
		FText::FromString(TEXT("AUTOMATIC")),
		FText::FromString(TEXT("READ ONLY - unlocked by story progression")));
	AutomaticGrid = TargetWidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("AutomaticGrid"));
	AutomaticGrid->SetMinDesiredSlotWidth(136.0f);
	AutomaticGrid->SetMinDesiredSlotHeight(116.0f);
	ManagementSection->AddChildToVerticalBox(AutomaticGrid);

	return true;
}

void UProjectTattooShopLibraryWidget::BindControls()
{
	if (UploadButton) UploadButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleUploadClicked);
	if (DeleteButton) DeleteButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleDeleteClicked);
	if (AddButton) AddButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleAddClicked);
	if (EditButton) EditButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleEditClicked);
	if (RemoveButton) RemoveButton->OnClicked.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleRemoveClicked);
}

void UProjectTattooShopLibraryWidget::RefreshActionState()
{
	const bool bCatalog = bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Catalog;
	const bool bManual = bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Manual && SelectedEntry.TattooId.IsValid();
	const bool bAutomatic = bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Automatic;
	if (AddButton) AddButton->SetIsEnabled(bCatalog);
	if (EditButton) EditButton->SetIsEnabled(bManual);
	if (RemoveButton) RemoveButton->SetIsEnabled(bManual);
	if (DeleteButton) DeleteButton->SetIsEnabled(bCatalog && SelectedEntry.RuntimeTexture != nullptr);
	if (StatusText) StatusText->SetText(bAutomatic ? FText::FromString(TEXT("Automatic tattoos are controlled by story progression and are read only.")) : CurrentStatusText);

	if (BackgroundBorder && bUsingNativeFallbackTree)
	{
		const FSlateRoundedBoxBrush Brush(
			EFProjectUIPalette::PanelFill(0.94f), 2.0f,
			FSlateColor(EFProjectUIPalette::OutlineDim(0.66f)), 1.0f,
			FVector2f(PresentationMode == EProjectTattooShopLibraryPresentationMode::Management
				? ManagementPanelSize
				: CatalogPanelSize));
		BackgroundBorder->SetBrush(Brush);
	}
}

void UProjectTattooShopLibraryWidget::RefreshPresentationMode()
{
	const bool bManagement = PresentationMode == EProjectTattooShopLibraryPresentationMode::Management;
	if (RootSizeBox)
	{
		const FVector2D DesiredSize = bManagement ? ManagementPanelSize : CatalogPanelSize;
		RootSizeBox->SetWidthOverride(DesiredSize.X);
		RootSizeBox->SetHeightOverride(DesiredSize.Y);
	}
	if (ManagementToolbar)
	{
		ManagementToolbar->SetVisibility(bManagement ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ManagementSection)
	{
		ManagementSection->SetVisibility(bManagement ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (CatalogSection)
	{
		CatalogSection->SetVisibility(!bManagement ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (StatusText)
	{
		StatusText->SetVisibility(bManagement ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	RebuildAllCards();
}

void UProjectTattooShopLibraryWidget::RebuildAllCards()
{
	RebuildCardPanel(LibraryGrid, LibraryEntries);
	RebuildCardPanel(ManualGrid, ManualEntries);
	RebuildCardPanel(AutomaticGrid, AutomaticEntries);
	OnLibraryRebuilt(LibraryEntries.Num(), ManualEntries.Num(), AutomaticEntries.Num());
}

void UProjectTattooShopLibraryWidget::RebuildCardPanel(
	UPanelWidget* Panel,
	const TArray<FProjectTattooShopCardData>& Entries)
{
	UUniformGridPanel* Grid = Cast<UUniformGridPanel>(Panel);
	if (!Grid)
	{
		return;
	}

	Grid->ClearChildren();
	const TSubclassOf<UProjectTattooShopCardWidget> EffectiveClass = ResolveCardWidgetClass();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		FProjectTattooShopCardData CardData = Entries[Index];
		CardData.bSelected = bHasSelection
			&& CardData.EntryId == SelectedEntry.EntryId
			&& CardData.TattooId == SelectedEntry.TattooId
			&& CardData.Kind == SelectedEntry.Kind;

		UProjectTattooShopCardWidget* Card = CreateWidget<UProjectTattooShopCardWidget>(
			this,
			EffectiveClass ? EffectiveClass.Get() : UProjectTattooShopCardWidget::StaticClass());
		if (!Card)
		{
			continue;
		}
		Card->ApplyCardData(CardData);
		Card->OnSelected.AddUniqueDynamic(this, &UProjectTattooShopLibraryWidget::HandleCardSelected);
		const int32 EffectiveColumnCount = PresentationMode == EProjectTattooShopLibraryPresentationMode::Management
			? 2
			: FMath::Clamp(CardColumnCount, 1, 2);
		if (UUniformGridSlot* CardSlot = Grid->AddChildToUniformGrid(Card, Index / EffectiveColumnCount, Index % EffectiveColumnCount))
		{
			CardSlot->SetHorizontalAlignment(HAlign_Left);
			CardSlot->SetVerticalAlignment(VAlign_Top);
		}
	}
}

TSubclassOf<UProjectTattooShopCardWidget> UProjectTattooShopLibraryWidget::ResolveCardWidgetClass() const
{
	if (!CardWidgetClass.IsNull())
	{
		if (UClass* Loaded = CardWidgetClass.LoadSynchronous())
		{
			return Loaded;
		}
	}

	if (UClass* Discovered = ProjectWidgetClassResolver::ResolveWidgetClassWithPriority(
		FSoftClassPath(), UProjectTattooShopCardWidget::StaticClass(), TEXT("ProjectTattooShopCard"), TEXT("TattooShop")))
	{
		return Discovered;
	}

	return UProjectTattooShopCardWidget::StaticClass();
}

void UProjectTattooShopLibraryWidget::HandleCardSelected(FProjectTattooShopCardData CardData)
{
	SetSelectedEntry(CardData);
}
void UProjectTattooShopLibraryWidget::HandleUploadClicked() { OnUploadRequested.Broadcast(); }
void UProjectTattooShopLibraryWidget::HandleDeleteClicked()
{
	if (bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Catalog) OnDeleteSourceRequested.Broadcast(SelectedEntry);
}
void UProjectTattooShopLibraryWidget::HandleAddClicked()
{
	if (bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Catalog) OnAddRequested.Broadcast(SelectedEntry);
}
void UProjectTattooShopLibraryWidget::HandleEditClicked()
{
	if (bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Manual) OnEditRequested.Broadcast(SelectedEntry.TattooId);
}
void UProjectTattooShopLibraryWidget::HandleRemoveClicked()
{
	if (bHasSelection && SelectedEntry.Kind == EProjectTattooShopCardKind::Manual) OnRemoveRequested.Broadcast(SelectedEntry.TattooId);
}
