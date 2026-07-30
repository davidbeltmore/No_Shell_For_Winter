#include "EFProjectUISettings.h"
#include "EFProjectUIPalette.h"

UEFProjectUISettings::UEFProjectUISettings()
{
	RedTheme = EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Red);
	BlueTheme = EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Blue);
	PurpleTheme = EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Purple);
	GreenTheme = EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Green);
	BlackTheme = EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Black);
	HUDTheme = BlackTheme;

	DynamicThemeWidgetClassPathPrefixes = {
		TEXT("/Script/EFProjectSystems"),
		TEXT("/Script/EFCharacterCreation"),
		TEXT("/Script/EFLevelFlow"),
		TEXT("/Script/EFProcedural"),
		TEXT("/Script/NoShellForWinter"),
		TEXT("/Game/_Game/"),
		TEXT("/Game/UI/")
	};

	const auto CompactIconPath = [](const TCHAR* AssetName) -> TSoftObjectPtr<UTexture2D>
	{
		return TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/_Game/Widgets/Attributes/Assets/Textures/%s.%s"),
			AssetName,
			AssetName)));
	};

	const auto MenuIconPath = [](const TCHAR* AssetName) -> TSoftObjectPtr<UTexture2D>
	{
		return TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/_Game/Widgets/InnerDoctrineAltar/Assets/Textures/%s.%s"),
			AssetName,
			AssetName)));
	};

	NeedsWidgetClass = FSoftClassPath();
	DayCycleWidgetClass = FSoftClassPath();
	InnerStateEntryDefinitions = {
		FProjectInnerStateEntryDefinition(TEXT("Hunger"), TEXT("HUNGER"), TEXT("H"), false, EFProjectUIPalette::InnerStateHunger(), 10),
		FProjectInnerStateEntryDefinition(TEXT("Thirst"), TEXT("THIRST"), TEXT("T"), false, EFProjectUIPalette::InnerStateThirst(), 20),
		FProjectInnerStateEntryDefinition(TEXT("Sleep"), TEXT("SLEEP"), TEXT("S"), false, EFProjectUIPalette::InnerStateSleep(), 30),
		FProjectInnerStateEntryDefinition(TEXT("Madness"), TEXT("MADNESS"), TEXT("M"), true, EFProjectUIPalette::InnerStateMadness(), 40),
		FProjectInnerStateEntryDefinition(TEXT("Curse"), TEXT("CURSE"), TEXT("C"), true, EFProjectUIPalette::InnerStateCurse(), 50),
		FProjectInnerStateEntryDefinition(TEXT("Pain"), TEXT("PAIN"), TEXT("P"), true, EFProjectUIPalette::InnerStatePain(), 60),
	};
	StatusWidgetClass = FSoftClassPath();
	InnerDoctrineWidgetClass = FSoftClassPath();
	InnerDoctrineExchangeMenuWidgetClass = FSoftClassPath();

	FDirectoryPath WidgetRootPath;
	WidgetRootPath.Path = TEXT("/Game/_Game/Widgets");
	WidgetDiscoveryRootPaths = { WidgetRootPath };

	InnerDoctrineEntryDefinitions = {
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Willpower"),
			TEXT("WILLPOWER"),
			TEXT("WIL"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Willpower")),
			TEXT("Willpower"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Willpower")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Willpower")),
			EFProjectUIPalette::AttributeWillpower(),
			10),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Offensive"),
			TEXT("OFFENSIVE"),
			TEXT("OFF"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Offensive")),
			TEXT("Offensive"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Offensive")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Offensive")),
			EFProjectUIPalette::AttributeOffensive(),
			20),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Defensive"),
			TEXT("DEFENSIVE"),
			TEXT("DEF"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Defensive")),
			TEXT("Defensive"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Defensive")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Defensive")),
			EFProjectUIPalette::AttributeDefensive(),
			30),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Faith"),
			TEXT("FAITH"),
			TEXT("FAI"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Faith")),
			TEXT("Faith"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Faith")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Faith")),
			EFProjectUIPalette::AttributeFaith(),
			40),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Cunning"),
			TEXT("CUNNING"),
			TEXT("CUN"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Cunning")),
			TEXT("Cunning"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Cunning")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Cunning")),
			EFProjectUIPalette::AttributeCunning(),
			50),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Celerity"),
			TEXT("CELERITY"),
			TEXT("CEL"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Celerity")),
			TEXT("Celerity"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Celerity")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Celerity")),
			EFProjectUIPalette::AttributeCelerity(),
			60),
		FProjectInnerDoctrineEntryDefinition(
			TEXT("Charisma"),
			TEXT("CHARISMA"),
			TEXT("CHA"),
			CompactIconPath(TEXT("T_InnerDoctrine_Icon_Charisma")),
			TEXT("Charisma"),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Icon_Charisma")),
			MenuIconPath(TEXT("T_InnerDoctrine_Altar_Watermark_Charisma")),
			EFProjectUIPalette::AttributeCharisma(),
			70),
	};
	ActivityFeedWidgetClass = FSoftClassPath();
	GameplayDebugMenuWidgetClass = FSoftClassPath();
	GameplayDebugMenuOptionRowWidgetClass = FSoftClassPath();
	ChronicleChannelDefinitions = {
		FProjectChronicleChannelDefinition(
			TEXT("System"),
			TEXT("LOG"),
			EFProjectUIPalette::SecondaryText(),
			EFProjectUIPalette::BadgeFill(0.84f),
			EFProjectUIPalette::BadgeText()),
		FProjectChronicleChannelDefinition(
			TEXT("Loot"),
			TEXT("LOOT"),
			EFProjectUIPalette::AccentSoft(),
			EFProjectUIPalette::BadgeFill(0.92f),
			EFProjectUIPalette::BadgeText()),
		FProjectChronicleChannelDefinition(
			TEXT("Experience"),
			TEXT("DXP"),
			EFProjectUIPalette::Accent(),
			EFProjectUIPalette::BadgeFill(),
			EFProjectUIPalette::BadgeText()),
		FProjectChronicleChannelDefinition(
			TEXT("Combat"),
			TEXT("COMBAT"),
			EFProjectUIPalette::Warning(),
			EFProjectUIPalette::AccentMuted(0.98f),
			EFProjectUIPalette::BadgeText()),
		FProjectChronicleChannelDefinition(
			TEXT("Status"),
			TEXT("STATUS"),
			EFProjectUIPalette::Title(),
			EFProjectUIPalette::AccentMuted(0.92f),
			EFProjectUIPalette::BadgeText()),
		FProjectChronicleChannelDefinition(
			TEXT("Dialogue"),
			TEXT("ENEMY"),
			EFProjectUIPalette::PrimaryText(),
			EFProjectUIPalette::OutlineDim(0.98f),
			EFProjectUIPalette::BadgeText()),
	};
}

const UEFProjectUISettings* UEFProjectUISettings::Get()
{
	return GetDefault<UEFProjectUISettings>();
}

FName UEFProjectUISettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}

const FProjectHUDThemeColors& UEFProjectUISettings::GetThemeProfile(
	const EEFProjectHUDThemePreset Preset) const
{
	switch (Preset)
	{
	case EEFProjectHUDThemePreset::Red:
		return RedTheme;
	case EEFProjectHUDThemePreset::Blue:
		return BlueTheme;
	case EEFProjectHUDThemePreset::Green:
		return GreenTheme;
	case EEFProjectHUDThemePreset::Black:
		return BlackTheme;
	case EEFProjectHUDThemePreset::Purple:
		return PurpleTheme;
	case EEFProjectHUDThemePreset::Auto:
	default:
		return BlackTheme;
	}
}
