#include "EFProjectUIPalette.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFProjectDynamicThemeSubsystem.h"
#include "EFProjectUISettings.h"
#include "EFProjectUITheme.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectUIThemeResolutionTest,
	"NoShellForWinter.ProjectSystems.UI.ThemeResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectUIThemeResolutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UEFProjectUISettings* Settings = GetDefault<UEFProjectUISettings>();
	TestNotNull(TEXT("EF Project UI settings resolve"), Settings);
	if (!Settings)
	{
		return false;
	}

	const FLinearColor Accent = EFProjectUITheme::Accent(0.37f);
	TestEqual(TEXT("Theme accent reads configured red channel"), Accent.R, Settings->HUDTheme.Accent.R);
	TestEqual(TEXT("Theme accent reads configured green channel"), Accent.G, Settings->HUDTheme.Accent.G);
	TestEqual(TEXT("Theme accent reads configured blue channel"), Accent.B, Settings->HUDTheme.Accent.B);
	TestEqual(TEXT("Theme helper replaces alpha"), Accent.A, 0.37f);

	const FLinearColor Source(0.1f, 0.2f, 0.3f, 0.4f);
	const FLinearColor WithAlpha = EFProjectUITheme::WithAlpha(Source, 0.8f);
	TestEqual(TEXT("WithAlpha preserves RGB"), FVector(WithAlpha.R, WithAlpha.G, WithAlpha.B), FVector(Source.R, Source.G, Source.B));
	TestEqual(TEXT("WithAlpha replaces alpha"), WithAlpha.A, 0.8f);
	TestEqual(TEXT("Palette semantic accent resolves through settings"), EFProjectUIPalette::Accent(), Settings->HUDTheme.Accent);
	TestEqual(TEXT("Positive state resolves through settings"), EFProjectUIPalette::Positive(), Settings->HUDTheme.Positive);
	TestEqual(TEXT("Negative state resolves through settings"), EFProjectUIPalette::Negative(), Settings->HUDTheme.Negative);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectUIDynamicThemePaletteTest,
	"NoShellForWinter.ProjectSystems.UI.DynamicThemePalette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectUIDynamicThemePaletteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FProjectHUDThemeColors Red =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Red);
	const FProjectHUDThemeColors Blue =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Blue);
	const FProjectHUDThemeColors Purple =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Purple);
	const FProjectHUDThemeColors Green =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Green);
	const FProjectHUDThemeColors Black =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Black);
	const FProjectHUDThemeColors Auto =
		EFProjectUITheme::BuildPresetTheme(EEFProjectHUDThemePreset::Auto);

	TestTrue(TEXT("Red is a complete red-dominant profile"), Red.Accent.R > Red.Accent.G && Red.Accent.R > Red.Accent.B);
	TestTrue(TEXT("Blue is a complete blue-dominant profile"), Blue.Accent.B > Blue.Accent.R && Blue.Accent.B > Blue.Accent.G);
	TestTrue(TEXT("Purple retains both red and blue energy"), Purple.Accent.B > Purple.Accent.R && Purple.Accent.R > Purple.Accent.G);
	TestTrue(TEXT("Green is a complete green-dominant profile"), Green.Accent.G > Green.Accent.R && Green.Accent.G > Green.Accent.B);
	TestTrue(TEXT("Black uses neutral silver chrome"), FMath::IsNearlyEqual(Black.Accent.R, Black.Accent.G, 0.03f) && FMath::IsNearlyEqual(Black.Accent.G, Black.Accent.B, 0.08f));
	TestTrue(TEXT("Every panel remains darker than its accent"), Red.PanelFill.R < Red.Accent.R && Blue.PanelFill.B < Blue.Accent.B && Purple.PanelFill.B < Purple.Accent.B && Green.PanelFill.G < Green.Accent.G);
	TestFalse(TEXT("Red and Purple are authored as distinct complete profiles"), EFProjectUITheme::AreEquivalent(Red, Purple));
	TestFalse(TEXT("Blue and Black are authored as distinct complete profiles"), EFProjectUITheme::AreEquivalent(Blue, Black));
	TestTrue(TEXT("Auto is exactly the neutral Black profile"), EFProjectUITheme::AreEquivalent(Auto, Black));

	const UEFProjectUISettings* Settings = GetDefault<UEFProjectUISettings>();
	TestNotNull(TEXT("Theme settings exist"), Settings);
	if (Settings)
	{
		TestTrue(TEXT("Configured HUD fallback is neutral Black"), EFProjectUITheme::AreEquivalent(Settings->HUDTheme, Black));
		TestEqual(TEXT("Default selection maps to Black"), Settings->DefaultThemePreset, EEFProjectHUDThemePreset::Black);
		TestEqual(TEXT("Male compatibility setting is neutral Black"), Settings->MaleThemePreset, EEFProjectHUDThemePreset::Black);
		TestEqual(TEXT("Female compatibility setting is neutral Black"), Settings->FemaleThemePreset, EEFProjectHUDThemePreset::Black);
		TestEqual(TEXT("Unknown compatibility setting is neutral Black"), Settings->UnknownIdentityThemePreset, EEFProjectHUDThemePreset::Black);
	}

	UGameInstance* TestGameInstance =
		NewObject<UGameInstance>(GetTransientPackage());
	UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
		NewObject<UEFProjectDynamicThemeSubsystem>(TestGameInstance);
	TestNotNull(TEXT("Transient theme subsystem exists"), ThemeSubsystem);
	if (ThemeSubsystem)
	{
		ThemeSubsystem->SetThemePreset(EEFProjectHUDThemePreset::Blue);
		TestEqual(TEXT("Manual Blue remains selectable"), ThemeSubsystem->GetResolvedPreset(), EEFProjectHUDThemePreset::Blue);
		ThemeSubsystem->SetThemePreset(EEFProjectHUDThemePreset::Auto);
		TestEqual(TEXT("Auto selection is retained"), ThemeSubsystem->GetSelectionPreset(), EEFProjectHUDThemePreset::Auto);
		TestEqual(TEXT("Auto resolves to neutral Black"), ThemeSubsystem->GetResolvedPreset(), EEFProjectHUDThemePreset::Black);
	}
	EFProjectUITheme::ClearRuntimeTheme();

	EEFProjectHUDThemePreset ParsedPreset = EEFProjectHUDThemePreset::Auto;
	TestTrue(TEXT("Purple command parses"), EFProjectUITheme::TryParsePreset(TEXT("Purple"), ParsedPreset));
	TestEqual(TEXT("Purple command resolves exactly"), ParsedPreset, EEFProjectHUDThemePreset::Purple);
	TestFalse(TEXT("Unsupported amber command is rejected"), EFProjectUITheme::TryParsePreset(TEXT("Amber"), ParsedPreset));

	TestTrue(
		TEXT("Native project widgets are in dynamic-theme scope"),
		UEFProjectDynamicThemeSubsystem::IsWidgetClassPathInScope(
			TEXT("/Script/EFProjectSystemsGameplay.ProjectChroniclePanelWidget"),
			{ TEXT("/Script/EFProjectSystems"), TEXT("/Game/_Game/") }));
	TestTrue(
		TEXT("Project Widget Blueprints are in dynamic-theme scope"),
		UEFProjectDynamicThemeSubsystem::IsWidgetClassPathInScope(
			TEXT("/Game/_Game/Widgets/Chronicle/WBP_ChroniclePanel.WBP_ChroniclePanel_C"),
			{ TEXT("/Script/EFProjectSystems"), TEXT("/Game/_Game/") }));

	const FString NativeBluePath =
		UEFProjectDynamicThemeSubsystem::BuildThemedTextureObjectPath(
			TEXT("/Game/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame.T_Chronicle_Frame"),
			TEXT("/Game/_Game/Textures/UI/Themes"),
			EEFProjectHUDThemePreset::Blue);
	TestEqual(
		TEXT("Native texture path preserves exact original hierarchy"),
		NativeBluePath,
		FString(TEXT("/Game/_Game/Textures/UI/Themes/Blue/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame.T_Chronicle_Frame")));

	const FString PurpleFromBluePath =
		UEFProjectDynamicThemeSubsystem::BuildThemedTextureObjectPath(
			NativeBluePath,
			TEXT("/Game/_Game/Textures/UI/Themes"),
			EEFProjectHUDThemePreset::Purple);
	TestEqual(
		TEXT("Switching packs strips the previous theme segment"),
		PurpleFromBluePath,
		FString(TEXT("/Game/_Game/Textures/UI/Themes/Purple/_Game/Widgets/Chronicle/Assets/Textures/T_Chronicle_Frame.T_Chronicle_Frame")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCharacterIdentityTagExclusivityTest,
	"NoShellForWinter.ProjectSystems.Identity.GenderTagExclusivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCharacterIdentityTagExclusivityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	APawn* Pawn = NewObject<APawn>(GetTransientPackage());
	UAbilitySystemComponent* AbilitySystem = NewObject<UAbilitySystemComponent>(Pawn, TEXT("IdentityTestAbilitySystem"));
	Pawn->AddInstanceComponent(AbilitySystem);

	const FGameplayTag Male = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false);
	const FGameplayTag Female = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Female"), false);
	const FGameplayTag Unknown = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Unknown"), false);
	TestTrue(TEXT("Gender tags are registered"), Male.IsValid() && Female.IsValid() && Unknown.IsValid());

	EFCharacterCreationGameplayHooks::OnIdentityChanged().Broadcast(Pawn, TEXT("Alia"), ECharacterCreationGender::Male, Male);
	TestEqual(TEXT("Male tag is present exactly once"), AbilitySystem->GetTagCount(Male), 1);
	TestEqual(TEXT("Female sibling is absent"), AbilitySystem->GetTagCount(Female), 0);
	TestEqual(TEXT("Unknown sibling is absent"), AbilitySystem->GetTagCount(Unknown), 0);

	EFCharacterCreationGameplayHooks::OnIdentityChanged().Broadcast(Pawn, TEXT("Alia"), ECharacterCreationGender::Female, Female);
	TestEqual(TEXT("Male sibling is removed"), AbilitySystem->GetTagCount(Male), 0);
	TestEqual(TEXT("Female tag is present exactly once"), AbilitySystem->GetTagCount(Female), 1);
	TestEqual(TEXT("Unknown sibling remains absent"), AbilitySystem->GetTagCount(Unknown), 0);

	EFCharacterCreationGameplayHooks::OnIdentityChanged().Broadcast(Pawn, TEXT("Player"), ECharacterCreationGender::NotApplicable, Unknown);
	TestEqual(TEXT("Male sibling remains absent"), AbilitySystem->GetTagCount(Male), 0);
	TestEqual(TEXT("Female sibling is removed"), AbilitySystem->GetTagCount(Female), 0);
	TestEqual(TEXT("Unknown tag is present exactly once"), AbilitySystem->GetTagCount(Unknown), 1);
	return true;
}

#endif
