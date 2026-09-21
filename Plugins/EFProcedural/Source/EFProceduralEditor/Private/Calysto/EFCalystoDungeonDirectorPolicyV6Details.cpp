#include "Calysto/EFCalystoDungeonDirectorPolicyV6Details.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#endif

#define LOCTEXT_NAMESPACE "EFCalystoDungeonDirectorPolicyV6Details"

namespace EFCalystoPolicyV6DetailsPrivate
{
struct FPolicySnapshot
{
	bool bSinglePolicy = false;
	bool bValid = false;
	FString ObjectPath;
	FString ValidationMessage;
	FString GameplayHash;
	FString AuthoringHash;
	FString MaterialHash;
	FString DecalHash;
	FString StyleSummary;
	FString ThemeSummary;
	FString ProbabilitySummary;
};

FString SoftAssetName(const FSoftObjectPath& Path)
{
	return Path.IsNull() ? TEXT("None") : Path.GetAssetName();
}

FString MaterialSetSummary(const FEFCalystoSurfaceMaterialSetV6& Materials)
{
	return FString::Printf(
		TEXT("Floor: %s  |  Wall: %s  |  Roof: %s"),
		*SoftAssetName(Materials.FloorMaterial.ToSoftObjectPath()),
		*SoftAssetName(Materials.WallMaterial.ToSoftObjectPath()),
		*SoftAssetName(Materials.RoofMaterial.ToSoftObjectPath()));
}

const TCHAR* MaterialModeLabel(const EEFCalystoMaterialResolutionModeV6 Mode)
{
	return Mode == EEFCalystoMaterialResolutionModeV6::Override
		? TEXT("Override")
		: TEXT("Inherit Style");
}

FString ThemeMaterialSummary(const FEFCalystoRoomThemeProfileV6& Theme)
{
	const FEFCalystoThemeMaterialPolicyV6& Policy = Theme.RoomMaterials;
	return FString::Printf(
		TEXT("Floor: %s%s  |  Wall: %s%s  |  Roof: %s%s"),
		MaterialModeLabel(Policy.FloorMode),
		Policy.FloorMode == EEFCalystoMaterialResolutionModeV6::Override
			? *FString::Printf(TEXT(" (%s)"), *SoftAssetName(Policy.Overrides.FloorMaterial.ToSoftObjectPath()))
			: TEXT(""),
		MaterialModeLabel(Policy.WallMode),
		Policy.WallMode == EEFCalystoMaterialResolutionModeV6::Override
			? *FString::Printf(TEXT(" (%s)"), *SoftAssetName(Policy.Overrides.WallMaterial.ToSoftObjectPath()))
			: TEXT(""),
		MaterialModeLabel(Policy.RoofMode),
		Policy.RoofMode == EEFCalystoMaterialResolutionModeV6::Override
			? *FString::Printf(TEXT(" (%s)"), *SoftAssetName(Policy.Overrides.RoofMaterial.ToSoftObjectPath()))
			: TEXT(""));
}

FPolicySnapshot BuildSnapshot(const UEFCalystoDungeonDirectorPolicyV6Asset* Policy)
{
	FPolicySnapshot Snapshot;
	if (!Policy)
	{
		Snapshot.ValidationMessage = TEXT("Select exactly one V6 policy to inspect deterministic diagnostics.");
		return Snapshot;
	}

	Snapshot.bSinglePolicy = true;
	Snapshot.ObjectPath = Policy->GetPathName();
	FString ValidationError;
	Snapshot.bValid = Policy->Validate(ValidationError);
	Snapshot.ValidationMessage = Snapshot.bValid
		? TEXT("PASS — the policy is internally valid and can produce deterministic identities.")
		: FString::Printf(TEXT("FAIL — %s"), ValidationError.IsEmpty() ? TEXT("validation returned no diagnostic") : *ValidationError);

	if (Snapshot.bValid)
	{
		// These are pure canonical calculations. They inspect soft paths as text and
		// deliberately never resolve or load any referenced object.
		Snapshot.GameplayHash = Policy->GetGameplayHash();
		Snapshot.AuthoringHash = Policy->GetAuthoringHash();
		Snapshot.MaterialHash = Policy->GetMaterialHash();
		Snapshot.DecalHash = Policy->GetDecalHash();
	}
	else
	{
		Snapshot.GameplayHash = TEXT("Unavailable while validation fails");
		Snapshot.AuthoringHash = TEXT("Unavailable while validation fails");
		Snapshot.MaterialHash = TEXT("Unavailable while validation fails");
		Snapshot.DecalHash = TEXT("Unavailable while validation fails");
	}

	TArray<FString> StyleLines;
	StyleLines.Reserve(Policy->Styles.Num());
	for (const FEFCalystoStyleProfileV6& Style : Policy->Styles)
	{
		StyleLines.Add(FString::Printf(
			TEXT("%s%s — %.1f%% floor, %.1f%% Theme presence\n    %s"),
			*Style.StyleId.ToString(),
			Style.bEnabled ? TEXT("") : TEXT(" [Disabled]"),
			Style.NormalizedSelectionProbability * 100.0f,
			Style.RoomThemeChance * 100.0f,
			*MaterialSetSummary(Style.DungeonMaterials)));
	}
	Snapshot.StyleSummary = StyleLines.IsEmpty()
		? TEXT("No Dungeon Styles are authored.")
		: FString::Join(StyleLines, TEXT("\n"));

	TArray<FString> ThemeLines;
	ThemeLines.Reserve(Policy->RoomThemes.Num());
	for (const FEFCalystoRoomThemeProfileV6& Theme : Policy->RoomThemes)
	{
		ThemeLines.Add(FString::Printf(
			TEXT("%s%s — %.1f%% conditional, %.3f%% average eligible room\n    %s"),
			*Theme.ThemeId.ToString(),
			Theme.bEnabled ? TEXT("") : TEXT(" [Disabled]"),
			Theme.ConditionalSelectionProbability * 100.0f,
			Theme.OverallEligibleRoomProbability * 100.0f,
			*ThemeMaterialSummary(Theme)));
	}
	Snapshot.ThemeSummary = ThemeLines.IsEmpty()
		? TEXT("No Room Themes are authored. Eligible rooms will always resolve to internal NoTheme.")
		: FString::Join(ThemeLines, TEXT("\n"));

	TArray<FString> ProbabilityLines;
	for (const FEFCalystoStyleProbabilityPreviewV6& StylePreview : Policy->ProbabilityPreview)
	{
		TArray<FString> EffectiveThemeParts;
		for (const FEFCalystoThemeProbabilityPreviewV6& ThemePreview : StylePreview.Themes)
		{
			EffectiveThemeParts.Add(FString::Printf(
				TEXT("%s %.3f%%"),
				*ThemePreview.ThemeId.ToString(),
				ThemePreview.EligibleRoomProbability * 100.0f));
		}
		ProbabilityLines.Add(FString::Printf(
			TEXT("%s: NoTheme %.1f%%  |  %s"),
			*StylePreview.StyleId.ToString(),
			StylePreview.NoThemeProbability * 100.0f,
			*FString::Join(EffectiveThemeParts, TEXT("  |  "))));
	}
	Snapshot.ProbabilitySummary = ProbabilityLines.IsEmpty()
		? TEXT("Probability preview is unavailable until the policy has valid enabled weights.")
		: FString::Join(ProbabilityLines, TEXT("\n"));

	return Snapshot;
}

void AddReadOnlyRow(
	IDetailCategoryBuilder& Category,
	const FText& SearchText,
	const FText& Label,
	const FString& Value,
	const FText& Tooltip)
{
	Category.AddCustomRow(SearchText)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(Label)
		.ToolTipText(Tooltip)
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	]
	.ValueContent()
	.MinDesiredWidth(520.0f)
	.MaxDesiredWidth(900.0f)
	[
		SNew(SEditableTextBox)
		.Text(FText::FromString(Value))
		.IsReadOnly(true)
		.SelectAllTextWhenFocused(true)
		.ToolTipText(Tooltip)
	];
}

void AddGuide(
	IDetailCategoryBuilder& Category,
	const FText& SearchText,
	const FText& Text)
{
	Category.AddCustomRow(SearchText)
	.WholeRowContent()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.055f, 0.075f, 0.105f, 1.0f))
		.Padding(FMargin(10.0f, 8.0f))
		[
			SNew(STextBlock)
			.Text(Text)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
	];
}

void BindRefresh(
	const TSharedPtr<IPropertyHandle>& Property,
	const TWeakPtr<IPropertyUtilities>& WeakUtilities)
{
	if (!Property.IsValid())
	{
		return;
	}

	const FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda([WeakUtilities]()
	{
		if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
		{
			// Rebuild only after an authored edit. No Slate attribute recomputes
			// hashes or validation during paint/tick.
			Utilities->RequestForceRefresh();
		}
	});
	Property->SetOnPropertyValueChanged(RefreshDelegate);
	Property->SetOnChildPropertyValueChanged(RefreshDelegate);
}
}

TSharedRef<IDetailCustomization> FEFCalystoDungeonDirectorPolicyV6Details::MakeInstance()
{
	return MakeShared<FEFCalystoDungeonDirectorPolicyV6Details>();
}

void FEFCalystoDungeonDirectorPolicyV6Details::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace EFCalystoPolicyV6DetailsPrivate;

	IDetailCategoryBuilder& Identity = DetailBuilder.EditCategory(
		TEXT("01 Identity"), LOCTEXT("IdentityCategory", "01 Identity"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Styles = DetailBuilder.EditCategory(
		TEXT("02 Dungeon Styles"), LOCTEXT("StylesCategory", "02 Dungeon Styles"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Themes = DetailBuilder.EditCategory(
		TEXT("03 Room Themes"), LOCTEXT("ThemesCategory", "03 Room Themes"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Safety = DetailBuilder.EditCategory(
		TEXT("04 Performance & Safety"), LOCTEXT("SafetyCategory", "04 Performance & Safety"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Probability = DetailBuilder.EditCategory(
		TEXT("05 Probability Preview"), LOCTEXT("ProbabilityCategory", "05 Probability Preview"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Cook = DetailBuilder.EditCategory(
		TEXT("06 Cook Validation"), LOCTEXT("CookCategory", "06 Cook Validation"), ECategoryPriority::Important);

	Identity.SetSortOrder(0);
	Styles.SetSortOrder(1);
	Themes.SetSortOrder(2);
	Safety.SetSortOrder(3);
	Probability.SetSortOrder(4);
	Cook.SetSortOrder(5);
	Identity.InitiallyCollapsed(false);
	Styles.InitiallyCollapsed(false);
	Themes.InitiallyCollapsed(false);
	Safety.InitiallyCollapsed(true);
	Probability.InitiallyCollapsed(false);
	Cook.InitiallyCollapsed(true);

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	const UEFCalystoDungeonDirectorPolicyV6Asset* Policy = nullptr;
	if (CustomizedObjects.Num() == 1)
	{
		Policy = Cast<UEFCalystoDungeonDirectorPolicyV6Asset>(CustomizedObjects[0].Get());
	}
	const FPolicySnapshot Snapshot = BuildSnapshot(Policy);

	Identity.AddCustomRow(LOCTEXT("AuthoritySearch", "V6 Authority Single Primary Data Asset Director"))
	.WholeRowContent()
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.025f, 0.14f, 0.24f, 1.0f))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AuthorityTitle", "CALYSTO DUNGEON DIRECTOR V6 - DEFINITIVE AUTHORITY"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ColorAndOpacity(FLinearColor(0.40f, 0.82f, 1.0f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 5.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"AuthorityBody",
					"This single Primary Data Asset owns Styles, per-room Themes, inline Architecture, materials, probability, placement-aware catalogs, decals, performance limits, and cook closure. It does not reference authored Calysto Room Data Assets. One Style is selected per floor; every eligible room independently resolves NoTheme or one authored Theme."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Snapshot.ObjectPath.IsEmpty() ? TEXT("Multiple selection - diagnostics unavailable") : Snapshot.ObjectPath))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
	];

	AddReadOnlyRow(
		Identity,
		LOCTEXT("ValidationSearch", "Validation Status PASS FAIL"),
		LOCTEXT("ValidationLabel", "Validation Status"),
		Snapshot.ValidationMessage,
		LOCTEXT("ValidationTooltip", "Pure V6 structural validation. It never loads a soft reference."));
	AddReadOnlyRow(Identity, LOCTEXT("GameplayHashSearch", "Gameplay Hash"), LOCTEXT("GameplayHashLabel", "Gameplay Hash"), Snapshot.GameplayHash,
		LOCTEXT("GameplayHashTooltip", "Deterministic identity of runtime gameplay intent."));
	AddReadOnlyRow(Identity, LOCTEXT("AuthoringHashSearch", "Authoring Hash"), LOCTEXT("AuthoringHashLabel", "Authoring Hash"), Snapshot.AuthoringHash,
		LOCTEXT("AuthoringHashTooltip", "Deterministic identity including authoring-only presentation data."));
	AddReadOnlyRow(Identity, LOCTEXT("MaterialHashSearch", "Material Hash"), LOCTEXT("MaterialHashLabel", "Material Hash"), Snapshot.MaterialHash,
		LOCTEXT("MaterialHashTooltip", "Deterministic identity of Style and Theme surface-material authority."));
	AddReadOnlyRow(Identity, LOCTEXT("DecalHashSearch", "Decal Hash"), LOCTEXT("DecalHashLabel", "Decal Hash"), Snapshot.DecalHash,
		LOCTEXT("DecalHashTooltip", "Deterministic identity of Style and Theme decal policy."));

	AddGuide(
		Styles,
		LOCTEXT("StyleGuideSearch", "Dungeon Style floor material probability guide"),
		LOCTEXT(
			"StyleGuide",
			"Select or expand a Style index below. Selection Weight controls which single Style owns the floor. Room Theme Chance is a separate per-room presence probability. Architecture contains the native Floor, Wall, Doorway, Light, Roof, Ramp, Start, and End definitions inline. Dungeon Materials are the general Floor, Wall, and Roof baseline and always override transient Calysto defaults."));
	Styles.AddCustomRow(LOCTEXT("StyleOverviewSearch", "Style profile material probability overview"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Snapshot.StyleSummary))
		.AutoWrapText(true)
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	];

	AddGuide(
		Themes,
		LOCTEXT("ThemeGuideSearch", "Room Theme override inherit material probability guide"),
		LOCTEXT(
			"ThemeGuide",
			"Select or expand a Theme index below. Theme weights are normalized only after a room passes its Style's Theme-presence roll. Architecture owns Wall Bottom, Wall Middle, Wall Top, Floor, Corners, and Roof directly inside this index. Set each material slot independently to Inherit Style or Override. An Override with no material is invalid; NoTheme is internal and must never be authored."));
	Themes.AddCustomRow(LOCTEXT("ThemeOverviewSearch", "Theme profile material probability overview"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Snapshot.ThemeSummary))
		.AutoWrapText(true)
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	];

	AddGuide(
		Safety,
		LOCTEXT("SafetyGuideSearch", "Performance safety preload pool hard limits"),
		LOCTEXT(
			"SafetyGuide",
			"These are floor-wide safety ceilings. Catalog overlays cannot multiply the Style's global budgets. V6 uses one retained asynchronous preload coordinator, one PCG request, and one shared decal pool capped at 24 components."));

	AddGuide(
		Probability,
		LOCTEXT("ProbabilityGuideSearch", "Probability NoTheme effective conditional independent"),
		LOCTEXT(
			"ProbabilityGuide",
			"Read this as two independent rolls per eligible room: first Theme presence, then Theme type. Changing Theme weights changes only the second roll and cannot change which rooms passed presence. Start, End, critical, and progression rooms always resolve to NoTheme. Main-path and door-clearance protection applies independently to decal placement."));
	Probability.AddCustomRow(LOCTEXT("ProbabilityOverviewSearch", "Effective probability overview"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Snapshot.ProbabilitySummary))
		.AutoWrapText(true)
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	];

	AddGuide(
		Cook,
		LOCTEXT("CookGuideSearch", "Cook bundle validation closure RealisticBlood"),
		LOCTEXT(
			"CookGuide",
			"Cook Bundle References are derived and read-only. The CalystoFloorV6 closure must contain the definitive policy/runtime assets and only the selected RealisticBlood decal textures; never Demo, Niagara, sample Blueprints, Manny, or Quinn content."));

	const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();
	BindRefresh(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UEFCalystoDungeonDirectorPolicyV6Asset, Styles)), WeakUtilities);
	BindRefresh(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UEFCalystoDungeonDirectorPolicyV6Asset, RoomThemes)), WeakUtilities);
	BindRefresh(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UEFCalystoDungeonDirectorPolicyV6Asset, PerformanceAndSafety)), WeakUtilities);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCalystoV6PolicyDetailsSnapshotTest,
	"NoShellForWinter.CalystoDungeon.V6.Editor.PolicyDetailsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoV6PolicyDetailsSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy =
		NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient V6 policy"), Policy);
	if (!Policy)
	{
		return false;
	}
	Policy->InitializeV6Defaults();

	const EFCalystoPolicyV6DetailsPrivate::FPolicySnapshot First =
		EFCalystoPolicyV6DetailsPrivate::BuildSnapshot(Policy);
	const EFCalystoPolicyV6DetailsPrivate::FPolicySnapshot Second =
		EFCalystoPolicyV6DetailsPrivate::BuildSnapshot(Policy);
	TestTrue(TEXT("Default snapshot validates"), First.bValid);
	TestEqual(TEXT("Gameplay diagnostic is stable"), First.GameplayHash, Second.GameplayHash);
	TestEqual(TEXT("Authoring diagnostic is stable"), First.AuthoringHash, Second.AuthoringHash);
	TestEqual(TEXT("Material diagnostic is stable"), First.MaterialHash, Second.MaterialHash);
	TestEqual(TEXT("Decal diagnostic is stable"), First.DecalHash, Second.DecalHash);
	TestEqual(TEXT("Gameplay diagnostic is SHA-256"), First.GameplayHash.Len(), 64);
	TestTrue(TEXT("Style overview exposes general material"), First.StyleSummary.Contains(TEXT("MI_GreyTiles")));
	TestTrue(TEXT("Theme overview exposes Forge material"), First.ThemeSummary.Contains(TEXT("MI_Template_BaseOrange")));
	TestTrue(TEXT("Theme overview exposes Shrine material"), First.ThemeSummary.Contains(TEXT("MI_Display_Blue")));
	TestTrue(TEXT("Probability overview exposes NoTheme"), First.ProbabilitySummary.Contains(TEXT("NoTheme")));
	return true;
}
#endif

#undef LOCTEXT_NAMESPACE
