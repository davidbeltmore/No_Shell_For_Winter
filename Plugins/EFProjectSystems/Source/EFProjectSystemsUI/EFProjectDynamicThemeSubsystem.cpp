#include "EFProjectDynamicThemeSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "UI/EFCharacterCreationRootWidget.h"
#include "EFProjectThemedUserWidget.h"
#include "EFProjectUISettings.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFProjectDynamicTheme, Log, All);

namespace EFProjectDynamicThemePrivate
{
	bool ContainsAny(const FString& Source, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (Source.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	FLinearColor WithAlpha(const FLinearColor& Color, const float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Alpha);
	}

	FLinearColor LightenRgb(const FLinearColor& Color, const float Amount)
	{
		return FLinearColor(
			FMath::Lerp(Color.R, 1.0f, Amount),
			FMath::Lerp(Color.G, 1.0f, Amount),
			FMath::Lerp(Color.B, 1.0f, Amount),
			Color.A);
	}

	FLinearColor DarkenRgb(const FLinearColor& Color, const float Amount)
	{
		return FLinearColor(
			FMath::Lerp(Color.R, 0.0f, Amount),
			FMath::Lerp(Color.G, 0.0f, Amount),
			FMath::Lerp(Color.B, 0.0f, Amount),
			Color.A);
	}

	FLinearColor DeepRedAction(const FLinearColor& Negative, const float Intensity)
	{
		// Destructive actions should read as restrained burgundy in dark HUDs,
		// not as a bright warning-orange. Keep the active theme's negative hue
		// while suppressing green/blue and controlling luminance explicitly.
		return FLinearColor(
			FMath::Clamp(Negative.R * Intensity, 0.0f, 1.0f),
			FMath::Clamp(Negative.G * Intensity * 0.40f, 0.0f, 1.0f),
			FMath::Clamp(Negative.B * Intensity * 0.62f, 0.0f, 1.0f),
			Negative.A);
	}

	bool IsPositiveActionName(const FString& Name)
	{
		return ContainsAny(Name, {
			TEXT("AcceptButton"),
			TEXT("AcceptLabel"),
			TEXT("ConfirmButton"),
			TEXT("ConfirmLabel"),
			TEXT("PositiveButton"),
			TEXT("PositiveLabel") });
	}

	bool IsDestructiveActionName(const FString& Name)
	{
		return ContainsAny(Name, {
			TEXT("DeleteButton"),
			TEXT("DeleteLabel"),
			TEXT("RemoveButton"),
			TEXT("RemoveLabel"),
			TEXT("DangerButton"),
			TEXT("DangerLabel") });
	}

	bool IsPrimaryActionName(const FString& Name)
	{
		return ContainsAny(Name, {
			TEXT("AddButton"),
			TEXT("EditButton"),
			TEXT("UploadButton"),
			TEXT("PrimaryButton") });
	}

	bool IsActionTextName(const FString& Name)
	{
		// Action labels are resolved before the generic "Label" semantic so
		// their foreground stays legible over saturated button fills. Limit
		// Add/Edit to conventional label names to avoid recoloring descriptive
		// editor text merely because its owner contains "Editor".
		return IsPositiveActionName(Name)
			|| IsDestructiveActionName(Name)
			|| ContainsAny(Name, {
				TEXT("UploadLabel"),
				TEXT("PrimaryLabel"),
				TEXT("AddLabel"),
				TEXT("AddText"),
				TEXT("EditLabel"),
				TEXT("EditText") });
	}

	bool IsWhiteColorMultiplier(const FLinearColor& Color)
	{
		return FMath::IsNearlyEqual(Color.R, 1.0f, 0.03f)
			&& FMath::IsNearlyEqual(Color.G, 1.0f, 0.03f)
			&& FMath::IsNearlyEqual(Color.B, 1.0f, 0.03f);
	}

	void SetBrushTintPreservingAlpha(
		FSlateBrush& Brush,
		const FLinearColor& Color)
	{
		const float ExistingAlpha =
			Brush.TintColor.GetSpecifiedColor().A;
		Brush.TintColor = FSlateColor(WithAlpha(Color, ExistingAlpha));
	}

	void SetRoundedOutlinePreservingAlpha(
		FSlateBrush& Brush,
		const FLinearColor& Color)
	{
		if (Brush.DrawAs != ESlateBrushDrawType::RoundedBox)
		{
			return;
		}

		const float ExistingAlpha =
			Brush.OutlineSettings.Color.GetSpecifiedColor().A;
		Brush.OutlineSettings.Color =
			FSlateColor(WithAlpha(Color, ExistingAlpha));
	}

	bool IsOptedOut(const UWidget* Widget)
	{
		return Widget
			&& Widget->GetName().Contains(TEXT("NoTheme"), ESearchCase::IgnoreCase);
	}

	bool IsClassOrParentInModule(
		const UClass* Class,
		const TCHAR* ModuleClassPathPrefix)
	{
		for (const UClass* Candidate = Class;
			Candidate;
			Candidate = Candidate->GetSuperClass())
		{
			if (Candidate->GetPathName().StartsWith(
				ModuleClassPathPrefix,
				ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	FLinearColor ResolveSemanticFill(
		const FString& Name,
		const FProjectHUDThemeColors& Theme,
		bool& bOutApply)
	{
		bOutApply = true;
		// A backdrop owns its interaction mode as well as its color. HUD widgets
		// such as Inner State deliberately author a fully transparent backdrop,
		// while modal menus author their own dim strength. Replacing both with a
		// global 0.78 black fill darkens gameplay whenever the comma HUD opens.
		if (ContainsAny(Name, { TEXT("Backdrop"), TEXT("Blackout") }))
		{
			bOutApply = false;
			return FLinearColor::White;
		}
		if (ContainsAny(Name, { TEXT("Dimmer") }))
		{
			return FLinearColor::Black;
		}
		if (ContainsAny(Name, { TEXT("Selected"), TEXT("Active"), TEXT("Focused") }))
		{
			return Theme.AccentMuted;
		}
		if (ContainsAny(Name, {
			TEXT("MenuPanelBorder"),
			TEXT("SelectionFrame"),
			TEXT("OutlineBorder") }))
		{
			return Theme.Outline;
		}
		if (ContainsAny(Name, { TEXT("Footer") }))
		{
			return Theme.PanelFill;
		}
		if (ContainsAny(Name, { TEXT("Summary") }))
		{
			return Theme.SectionFill;
		}
		if (ContainsAny(Name, {
			TEXT("Inner"),
			TEXT("Row"),
			TEXT("Card"),
			TEXT("Detail"),
			TEXT("Resource"),
			TEXT("RunDxp"),
			TEXT("MetaDxp"),
			TEXT("Content"),
			TEXT("Badge"),
			TEXT("Pill"),
			TEXT("Tab"),
			TEXT("IconBorder"),
			TEXT("OptionBorder") }))
		{
			return Theme.SectionFill;
		}
		if (ContainsAny(Name, {
			TEXT("Background"),
			TEXT("Panel"),
			TEXT("Outer"),
			TEXT("Frame"),
			TEXT("Outline"),
			TEXT("RootBorder"),
			TEXT("Shell") }))
		{
			return Theme.PanelFillDeep;
		}
		if (ContainsAny(Name, { TEXT("Section") }))
		{
			return Theme.SectionFill;
		}

		bOutApply = false;
		return FLinearColor::White;
	}

	FLinearColor ResolveSemanticOutline(
		const FString& Name,
		const FProjectHUDThemeColors& Theme)
	{
		if (ContainsAny(Name, {
			TEXT("Selected"),
			TEXT("Active"),
			TEXT("Focused"),
			TEXT("Frame"),
			TEXT("Outer"),
			TEXT("Outline") }))
		{
			return Theme.Outline;
		}
		return Theme.OutlineDim;
	}

	FLinearColor ResolveSemanticText(
		const FString& Name,
		const FProjectHUDThemeColors& Theme)
	{
		if (ContainsAny(Name, { TEXT("Warning"), TEXT("Error"), TEXT("Failure") }))
		{
			return Theme.Warning;
		}
		if (IsActionTextName(Name))
		{
			return Theme.TitleText;
		}
		if (ContainsAny(Name, {
			TEXT("Cost"),
			TEXT("Value"),
			TEXT("Accent"),
			TEXT("Selected"),
			TEXT("Enemy") }))
		{
			return Theme.AccentSoft;
		}
		if (ContainsAny(Name, {
			TEXT("Hint"),
			TEXT("Footer"),
			TEXT("Description"),
			TEXT("Flavor"),
			TEXT("Meta"),
			TEXT("Subtitle"),
			TEXT("Label"),
			TEXT("Level") }))
		{
			return Theme.SecondaryText;
		}
		if (ContainsAny(Name, { TEXT("Title"), TEXT("Header"), TEXT("Heading") }))
		{
			return Theme.TitleText;
		}
		return Theme.PrimaryText;
	}

	void TintButtonBrushes(
		FButtonStyle& Style,
		const FLinearColor& Normal,
		const FLinearColor& Hovered,
		const FLinearColor& Pressed,
		const FLinearColor& Disabled)
	{
		SetBrushTintPreservingAlpha(Style.Normal, Normal);
		SetBrushTintPreservingAlpha(Style.Hovered, Hovered);
		SetBrushTintPreservingAlpha(Style.Pressed, Pressed);
		SetBrushTintPreservingAlpha(Style.Disabled, Disabled);
		SetRoundedOutlinePreservingAlpha(Style.Normal, Hovered);
		SetRoundedOutlinePreservingAlpha(Style.Hovered, Pressed);
		SetRoundedOutlinePreservingAlpha(Style.Pressed, Pressed);
		SetRoundedOutlinePreservingAlpha(Style.Disabled, Disabled);
	}

	UEFProjectDynamicThemeSubsystem* ResolveSubsystem(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance
			? GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>()
			: nullptr;
	}

	void SetPresetFromConsole(const TArray<FString>& Arguments, UWorld* World)
	{
		UEFProjectDynamicThemeSubsystem* Subsystem = ResolveSubsystem(World);
		if (!Subsystem)
		{
			return;
		}

		EEFProjectHUDThemePreset Preset = EEFProjectHUDThemePreset::Auto;
		const FString Requested = Arguments.IsEmpty() ? TEXT("Auto") : Arguments[0];
		if (!EFProjectUITheme::TryParsePreset(Requested, Preset))
		{
			UE_LOG(
				LogEFProjectDynamicTheme,
				Warning,
				TEXT("Unknown HUD theme '%s'. Use Auto, Red, Blue, Purple, Green, or Black."),
				*Requested);
			return;
		}

		Subsystem->SetThemePreset(Preset);
		UE_LOG(
			LogEFProjectDynamicTheme,
			Log,
			TEXT("HUD theme selection=%s resolved=%s revision=%d."),
			*EFProjectUITheme::GetPresetName(Subsystem->GetSelectionPreset()),
			*EFProjectUITheme::GetPresetName(Subsystem->GetResolvedPreset()),
			Subsystem->GetThemeRevision());
	}

	static FAutoConsoleCommandWithWorldAndArgs SetThemePresetCommand(
		TEXT("EF.UI.ThemePreset"),
		TEXT("Selects Auto, Red, Blue, Purple, Green, or Black native HUD theme."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetPresetFromConsole));
}

void UEFProjectDynamicThemeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CoreThemeChangedHandle = EFProjectUITheme::OnThemeChanged().AddUObject(
		this,
		&ThisClass::HandleCoreThemeChanged);
	CharacterCreationWidgetReadyHandle =
		EFCharacterCreationGameplayHooks::OnWidgetReady().AddUObject(
			this,
			&ThisClass::HandleCharacterCreationWidgetReady);

	PreloadNativeTexturePacks();
	ResetToConfiguredTheme();
}

void UEFProjectDynamicThemeSubsystem::Deinitialize()
{
	if (CharacterCreationWidgetReadyHandle.IsValid())
	{
		EFCharacterCreationGameplayHooks::OnWidgetReady().Remove(
			CharacterCreationWidgetReadyHandle);
		CharacterCreationWidgetReadyHandle.Reset();
	}
	if (CoreThemeChangedHandle.IsValid())
	{
		EFProjectUITheme::OnThemeChanged().Remove(CoreThemeChangedHandle);
		CoreThemeChangedHandle.Reset();
	}

	RegisteredWidgets.Reset();
	LoadedThemeTextures.Reset();
	EFProjectUITheme::ClearRuntimeTheme();
	Super::Deinitialize();
}

void UEFProjectDynamicThemeSubsystem::SetThemePreset(
	const EEFProjectHUDThemePreset Preset)
{
	SelectionPreset = Preset;
	ApplyResolvedPreset(
		Preset == EEFProjectHUDThemePreset::Auto
			? ResolveAutomaticPreset()
			: Preset);
}

void UEFProjectDynamicThemeSubsystem::ResetToConfiguredTheme()
{
	SetThemePreset(EEFProjectHUDThemePreset::Auto);
}

void UEFProjectDynamicThemeSubsystem::RegisterThemedWidget(UUserWidget* UserWidget)
{
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	if (!IsValid(UserWidget)
		|| UserWidget->IsTemplate()
		|| !Settings
		|| !Settings->bEnableDynamicRuntimeTheme)
	{
		return;
	}

	RegisteredWidgets.Add(UserWidget);
	ReapplyCurrentThemeToWidget(UserWidget);
}

void UEFProjectDynamicThemeSubsystem::UnregisterThemedWidget(UUserWidget* UserWidget)
{
	if (UserWidget)
	{
		RegisteredWidgets.Remove(UserWidget);
	}
}

void UEFProjectDynamicThemeSubsystem::ReapplyCurrentThemeToWidget(
	UUserWidget* UserWidget)
{
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	if (!IsValid(UserWidget)
		|| UserWidget->IsTemplate()
		|| !Settings
		|| !Settings->bEnableDynamicRuntimeTheme)
	{
		return;
	}

	ApplyThemeToWidget(UserWidget, EFProjectUITheme::GetTheme(), ResolvedPreset);
}

UTexture2D* UEFProjectDynamicThemeSubsystem::ResolveCurrentThemeTexture(
	UTexture2D* SourceTexture) const
{
	if (!SourceTexture)
	{
		return nullptr;
	}

	if (UTexture2D* Replacement =
		ResolveThemedTexture(SourceTexture, ResolvedPreset))
	{
		return Replacement;
	}

	return SourceTexture;
}

void UEFProjectDynamicThemeSubsystem::RefreshAllThemedWidgets()
{
	UWorld* World = GetWorld();
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	if (!World || !Settings || !Settings->bEnableDynamicRuntimeTheme)
	{
		return;
	}

	CompactRegisteredWidgets();
	const FProjectHUDThemeColors& Theme = EFProjectUITheme::GetTheme();
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UserWidget = *It;
		if (!IsValid(UserWidget)
			|| UserWidget->IsTemplate()
			|| UserWidget->GetWorld() != World
			|| !IsWidgetClassPathInScope(
				UserWidget->GetClass()->GetPathName(),
				Settings->DynamicThemeWidgetClassPathPrefixes))
		{
			continue;
		}

		RegisteredWidgets.Add(UserWidget);
		ApplyThemeToWidget(UserWidget, Theme, ResolvedPreset);
	}
}

FProjectHUDThemeColors UEFProjectDynamicThemeSubsystem::GetActiveTheme() const
{
	return EFProjectUITheme::GetTheme();
}

bool UEFProjectDynamicThemeSubsystem::IsWidgetClassPathInScope(
	const FString& ClassPath,
	const TArray<FString>& Prefixes)
{
	for (const FString& Prefix : Prefixes)
	{
		if (!Prefix.IsEmpty()
			&& ClassPath.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString UEFProjectDynamicThemeSubsystem::BuildThemedTextureObjectPath(
	const FString& SourceObjectPath,
	const FString& ThemeRoot,
	const EEFProjectHUDThemePreset Preset)
{
	if (SourceObjectPath.IsEmpty()
		|| ThemeRoot.IsEmpty()
		|| Preset == EEFProjectHUDThemePreset::Auto)
	{
		return FString();
	}

	FString PackagePath;
	FString AssetName;
	if (!SourceObjectPath.Split(TEXT("."), &PackagePath, &AssetName))
	{
		PackagePath = SourceObjectPath;
		AssetName = FPackageName::GetLongPackageAssetName(SourceObjectPath);
	}
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return FString();
	}

	FString OriginalRelativePath;
	const FString NormalizedRoot = ThemeRoot.EndsWith(TEXT("/"))
		? ThemeRoot.LeftChop(1)
		: ThemeRoot;
	const FString ThemePrefix = NormalizedRoot + TEXT("/");
	if (PackagePath.StartsWith(ThemePrefix, ESearchCase::IgnoreCase))
	{
		const FString ExistingThemeRelative = PackagePath.RightChop(ThemePrefix.Len());
		int32 FirstSlash = INDEX_NONE;
		if (!ExistingThemeRelative.FindChar(TEXT('/'), FirstSlash))
		{
			return FString();
		}
		OriginalRelativePath = ExistingThemeRelative.RightChop(FirstSlash + 1);
	}
	else if (PackagePath.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
	{
		OriginalRelativePath = PackagePath.RightChop(6);
	}
	else
	{
		return FString();
	}

	return FString::Printf(
		TEXT("%s/%s/%s.%s"),
		*NormalizedRoot,
		*EFProjectUITheme::GetPresetName(Preset),
		*OriginalRelativePath,
		*AssetName);
}

void UEFProjectDynamicThemeSubsystem::ApplyResolvedPreset(
	const EEFProjectHUDThemePreset Preset)
{
	const EEFProjectHUDThemePreset SafePreset =
		Preset == EEFProjectHUDThemePreset::Auto
			? EEFProjectHUDThemePreset::Black
			: Preset;
	const FProjectHUDThemeColors& Theme = ResolveThemeProfile(SafePreset);

	if (ResolvedPreset == SafePreset
		&& EFProjectUITheme::HasRuntimeTheme()
		&& EFProjectUITheme::AreEquivalent(EFProjectUITheme::GetTheme(), Theme))
	{
		return;
	}

	ResolvedPreset = SafePreset;
	++ThemeRevision;
	EFProjectUITheme::SetRuntimeTheme(Theme);

	// SetRuntimeTheme drives HandleCoreThemeChanged synchronously. Should a
	// future core implementation suppress an equivalent broadcast, keep the
	// explicit state observable without introducing any timer.
	if (!EFProjectUITheme::AreEquivalent(EFProjectUITheme::GetTheme(), Theme))
	{
		RefreshAllThemedWidgets();
		OnDynamicThemeChanged.Broadcast(ResolvedPreset, Theme, ThemeRevision);
	}
}

EEFProjectHUDThemePreset UEFProjectDynamicThemeSubsystem::ResolveAutomaticPreset() const
{
	return EEFProjectHUDThemePreset::Black;
}

const FProjectHUDThemeColors& UEFProjectDynamicThemeSubsystem::ResolveThemeProfile(
	const EEFProjectHUDThemePreset Preset) const
{
	if (const UEFProjectUISettings* Settings = UEFProjectUISettings::Get())
	{
		return Settings->GetThemeProfile(Preset);
	}

	static FProjectHUDThemeColors Fallback;
	Fallback = EFProjectUITheme::BuildPresetTheme(Preset);
	return Fallback;
}

void UEFProjectDynamicThemeSubsystem::HandleCoreThemeChanged(
	const FProjectHUDThemeColors& Theme)
{
	RefreshAllThemedWidgets();
	OnDynamicThemeChanged.Broadcast(ResolvedPreset, Theme, ThemeRevision);
}

void UEFProjectDynamicThemeSubsystem::HandleCharacterCreationWidgetReady(
	UUserWidget* UserWidget)
{
	RegisterThemedWidget(UserWidget);
}

void UEFProjectDynamicThemeSubsystem::ApplyThemeToWidget(
	UUserWidget* UserWidget,
	const FProjectHUDThemeColors& Theme,
	const EEFProjectHUDThemePreset Preset) const
{
	if (!UserWidget || !UserWidget->WidgetTree)
	{
		return;
	}

	const bool bIsCharacterCreationWidget =
		EFProjectDynamicThemePrivate::IsClassOrParentInModule(
			UserWidget->GetClass(),
			TEXT("/Script/EFCharacterCreation"));
	const UEFCharacterCreationRootWidget* CharacterCreationRoot =
		Cast<UEFCharacterCreationRootWidget>(UserWidget);

	UserWidget->WidgetTree->ForEachWidget(
		[this, &Theme, Preset, bIsCharacterCreationWidget, CharacterCreationRoot](
			UWidget* Widget)
		{
			using namespace EFProjectDynamicThemePrivate;
			if (!Widget || IsOptedOut(Widget))
			{
				return;
			}

			const FString Name = Widget->GetName();
			if (UImage* Image = Cast<UImage>(Widget))
			{
				FSlateBrush Brush = Image->GetBrush();
				const float ExistingImageAlpha = Image->GetColorAndOpacity().A;
				if (ReplaceBrushTexture(Brush, Preset))
				{
					Image->SetBrush(Brush);
					Image->SetColorAndOpacity(
						WithAlpha(FLinearColor::White, ExistingImageAlpha));
				}
				else if (ContainsAny(Name, { TEXT("Glow"), TEXT("Haze") }))
				{
					SetBrushTintPreservingAlpha(Brush, FLinearColor::White);
					Image->SetBrush(Brush);
					Image->SetColorAndOpacity(
						WithAlpha(Theme.Haze, ExistingImageAlpha));
				}
				else if (ContainsAny(Name, {
					TEXT("Divider"),
					TEXT("Ornament"),
					TEXT("Glyph"),
					TEXT("Watermark"),
					TEXT("Selection"),
					TEXT("ThemeIcon") }))
				{
					SetBrushTintPreservingAlpha(Brush, FLinearColor::White);
					Image->SetBrush(Brush);
					Image->SetColorAndOpacity(
						WithAlpha(Theme.AccentSoft, ExistingImageAlpha));
				}
				else
				{
					bool bApplyFill = false;
					const FLinearColor Fill =
						ResolveSemanticFill(Name, Theme, bApplyFill);
					if (bApplyFill)
					{
						if (Brush.DrawAs == ESlateBrushDrawType::RoundedBox)
						{
							SetBrushTintPreservingAlpha(Brush, Fill);
							SetRoundedOutlinePreservingAlpha(
								Brush,
								ResolveSemanticOutline(Name, Theme));
							Image->SetBrush(Brush);
							Image->SetColorAndOpacity(
								WithAlpha(
									FLinearColor::White,
									ExistingImageAlpha));
						}
						else
						{
							SetBrushTintPreservingAlpha(
								Brush,
								FLinearColor::White);
							Image->SetBrush(Brush);
							Image->SetColorAndOpacity(
								WithAlpha(Fill, ExistingImageAlpha));
						}
					}
				}
				return;
			}

			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				FSlateBrush Brush = Border->Background;
				const FLinearColor ExistingBorderColor =
					Border->GetBrushColor();
				if (ReplaceBrushTexture(Brush, Preset))
				{
					Border->SetBrush(Brush);
					Border->SetBrushColor(
						WithAlpha(
							FLinearColor::White,
							ExistingBorderColor.A));
				}
				else
				{
					bool bApplyFill = false;
					const FLinearColor Fill = ResolveSemanticFill(Name, Theme, bApplyFill);
					if (bApplyFill)
					{
						SetRoundedOutlinePreservingAlpha(
							Brush,
							ResolveSemanticOutline(Name, Theme));

						// UBorder can store its visible RGB either in
						// Background.TintColor (notably RoundedBox brushes) or
						// in BrushColor. Detect the authored white multiplier
						// instead of multiplying two theme colors together.
						const bool bBrushOwnsFill =
							Brush.DrawAs == ESlateBrushDrawType::RoundedBox
							&& IsWhiteColorMultiplier(ExistingBorderColor);
						SetBrushTintPreservingAlpha(
							Brush,
							bBrushOwnsFill ? Fill : FLinearColor::White);
						Border->SetBrush(Brush);
						Border->SetBrushColor(
							WithAlpha(
								bBrushOwnsFill
									? FLinearColor::White
									: Fill,
								ExistingBorderColor.A));
					}
					else if (bIsCharacterCreationWidget && Border->GetContent())
					{
						if (ExistingBorderColor.A > 0.05f)
						{
							Border->SetBrushColor(
								WithAlpha(
									Theme.SectionFill,
									ExistingBorderColor.A));
						}
					}
				}
				return;
			}

			if (UButton* Button = Cast<UButton>(Widget))
			{
				FButtonStyle Style = Button->GetStyle();
				bool bIsActiveCharacterCreationTab = false;
				const bool bIsCharacterCreationTab =
					CharacterCreationRoot
					&& CharacterCreationRoot->TryGetThemeTabState(
						Button,
						bIsActiveCharacterCreationTab);
				const bool bHasNativeBrush =
					ReplaceBrushTexture(Style.Normal, Preset)
					| ReplaceBrushTexture(Style.Hovered, Preset)
					| ReplaceBrushTexture(Style.Pressed, Preset)
					| ReplaceBrushTexture(Style.Disabled, Preset);
				if (!bHasNativeBrush)
				{
					// Character-creation tabs keep their established active/inactive
					// semantics even if a tab happens to use an action-like name.
					if (bIsCharacterCreationTab)
					{
						if (bIsActiveCharacterCreationTab)
						{
							TintButtonBrushes(
								Style,
								WithAlpha(Theme.AccentMuted, 0.98f),
								WithAlpha(Theme.Accent, 0.99f),
								WithAlpha(Theme.AccentSoft, 0.99f),
								WithAlpha(Theme.SectionFill, 0.62f));
						}
						else
						{
							TintButtonBrushes(
								Style,
								WithAlpha(Theme.SectionFill, 0.96f),
								WithAlpha(Theme.AccentMuted, 0.98f),
								WithAlpha(Theme.Accent, 0.98f),
								WithAlpha(Theme.SectionFill, 0.62f));
						}
					}
					else if (IsPositiveActionName(Name))
					{
						TintButtonBrushes(
							Style,
							WithAlpha(Theme.Positive, 0.98f),
							WithAlpha(LightenRgb(Theme.Positive, 0.16f), 0.99f),
							WithAlpha(DarkenRgb(Theme.Positive, 0.18f), 0.99f),
							WithAlpha(Theme.SectionFill, 0.62f));
					}
					else if (IsDestructiveActionName(Name))
					{
						TintButtonBrushes(
							Style,
							WithAlpha(DeepRedAction(Theme.Negative, 0.48f), 0.96f),
							WithAlpha(DeepRedAction(Theme.Negative, 0.62f), 0.99f),
							WithAlpha(DeepRedAction(Theme.Negative, 0.36f), 0.99f),
							WithAlpha(Theme.SectionFill, 0.62f));
					}
					else if (IsPrimaryActionName(Name))
					{
						TintButtonBrushes(
							Style,
							WithAlpha(Theme.AccentMuted, 0.96f),
							WithAlpha(Theme.Accent, 0.99f),
							WithAlpha(Theme.AccentSoft, 0.99f),
							WithAlpha(Theme.SectionFill, 0.62f));
					}
					else
					{
						TintButtonBrushes(
							Style,
							WithAlpha(Theme.SectionFill, 0.96f),
							WithAlpha(Theme.AccentMuted, 0.98f),
							WithAlpha(Theme.Accent, 0.98f),
							WithAlpha(Theme.SectionFill, 0.62f));
					}
				}
				Button->SetStyle(Style);
				Button->SetBackgroundColor(
					WithAlpha(
						FLinearColor::White,
						Button->GetBackgroundColor().A));
				return;
			}

			if (UProgressBar* ProgressBar = Cast<UProgressBar>(Widget))
			{
				FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
				ReplaceBrushTexture(Style.BackgroundImage, Preset);
				ReplaceBrushTexture(Style.FillImage, Preset);
				ReplaceBrushTexture(Style.MarqueeImage, Preset);
				ProgressBar->SetWidgetStyle(Style);
				ProgressBar->SetFillColorAndOpacity(Theme.Accent);
				return;
			}

			if (USlider* Slider = Cast<USlider>(Widget))
			{
				FSliderStyle Style = Slider->GetWidgetStyle();
				ReplaceBrushTexture(Style.NormalBarImage, Preset);
				ReplaceBrushTexture(Style.HoveredBarImage, Preset);
				ReplaceBrushTexture(Style.DisabledBarImage, Preset);
				ReplaceBrushTexture(Style.NormalThumbImage, Preset);
				ReplaceBrushTexture(Style.HoveredThumbImage, Preset);
				ReplaceBrushTexture(Style.DisabledThumbImage, Preset);
				Slider->SetWidgetStyle(Style);
				Slider->SetSliderBarColor(Theme.OutlineDim);
				Slider->SetSliderHandleColor(Theme.AccentSoft);
				return;
			}

			if (UCheckBox* CheckBox = Cast<UCheckBox>(Widget))
			{
				FCheckBoxStyle Style = CheckBox->GetWidgetStyle();
				ReplaceBrushTexture(Style.UncheckedImage, Preset);
				ReplaceBrushTexture(Style.UncheckedHoveredImage, Preset);
				ReplaceBrushTexture(Style.UncheckedPressedImage, Preset);
				ReplaceBrushTexture(Style.CheckedImage, Preset);
				ReplaceBrushTexture(Style.CheckedHoveredImage, Preset);
				ReplaceBrushTexture(Style.CheckedPressedImage, Preset);
				CheckBox->SetWidgetStyle(Style);
				return;
			}

			if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				const float ExistingTextAlpha =
					TextBlock->GetColorAndOpacity().GetSpecifiedColor().A;
				bool bIsActiveCharacterCreationTab = false;
				if (CharacterCreationRoot
					&& CharacterCreationRoot->TryGetThemeTabState(
						TextBlock,
						bIsActiveCharacterCreationTab))
				{
					TextBlock->SetColorAndOpacity(
						FSlateColor(
							WithAlpha(
								bIsActiveCharacterCreationTab
									? Theme.TitleText
									: Theme.SecondaryText,
								ExistingTextAlpha)));
				}
				else
				{
					TextBlock->SetColorAndOpacity(
						FSlateColor(
							WithAlpha(
								ResolveSemanticText(Name, Theme),
								ExistingTextAlpha)));
				}
			}
		});

	if (UEFProjectThemedUserWidget* ThemedWidget =
		Cast<UEFProjectThemedUserWidget>(UserWidget))
	{
		ThemedWidget->NotifyProjectThemeApplied(Preset, Theme, ThemeRevision);
	}
}

bool UEFProjectDynamicThemeSubsystem::ReplaceBrushTexture(
	FSlateBrush& Brush,
	const EEFProjectHUDThemePreset Preset) const
{
	const UTexture2D* SourceTexture = Cast<UTexture2D>(Brush.GetResourceObject());
	UTexture2D* Replacement = ResolveThemedTexture(SourceTexture, Preset);
	if (!Replacement)
	{
		return false;
	}

	const float ExistingTintAlpha = Brush.TintColor.GetSpecifiedColor().A;
	if (Replacement != SourceTexture)
	{
		Brush.SetResourceObject(Replacement);
	}
	Brush.TintColor = FSlateColor(
		EFProjectDynamicThemePrivate::WithAlpha(
			FLinearColor::White,
			ExistingTintAlpha));
	return true;
}

UTexture2D* UEFProjectDynamicThemeSubsystem::ResolveThemedTexture(
	const UTexture2D* SourceTexture,
	const EEFProjectHUDThemePreset Preset) const
{
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	if (!SourceTexture
		|| !Settings
		|| Preset == EEFProjectHUDThemePreset::Auto)
	{
		return nullptr;
	}

	const FString ObjectPath = BuildThemedTextureObjectPath(
		SourceTexture->GetPathName(),
		Settings->NativeThemeTextureRoot,
		Preset);
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}

	if (const TObjectPtr<UTexture2D>* Loaded = LoadedThemeTextures.Find(ObjectPath))
	{
		return Loaded->Get();
	}

	// This is normally already resident because all five packs are preloaded.
	// It also makes editor iteration work immediately after importing a pack.
	if (UTexture2D* ThemedTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath))
	{
		return ThemedTexture;
	}

	// When the source brush already points into a theme pack and the requested
	// sibling is absent, never retain the previous theme. Reverse the isolated
	// pack path back to its authored /Game source as the deterministic fallback.
	const FString SourceObjectPath = SourceTexture->GetPathName();
	FString SourcePackagePath;
	FString SourceAssetName;
	if (!SourceObjectPath.Split(
		TEXT("."),
		&SourcePackagePath,
		&SourceAssetName))
	{
		return nullptr;
	}

	const FString NormalizedRoot =
		Settings->NativeThemeTextureRoot.EndsWith(TEXT("/"))
			? Settings->NativeThemeTextureRoot.LeftChop(1)
			: Settings->NativeThemeTextureRoot;
	const FString ThemePrefix = NormalizedRoot + TEXT("/");
	if (!SourcePackagePath.StartsWith(
		ThemePrefix,
		ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	const FString ExistingThemeRelative =
		SourcePackagePath.RightChop(ThemePrefix.Len());
	int32 ThemeSeparatorIndex = INDEX_NONE;
	if (!ExistingThemeRelative.FindChar(
		TEXT('/'),
		ThemeSeparatorIndex))
	{
		return nullptr;
	}

	const FString OriginalRelativePath =
		ExistingThemeRelative.RightChop(ThemeSeparatorIndex + 1);
	const FString OriginalObjectPath = FString::Printf(
		TEXT("/Game/%s.%s"),
		*OriginalRelativePath,
		*SourceAssetName);
	return LoadObject<UTexture2D>(nullptr, *OriginalObjectPath);
}

void UEFProjectDynamicThemeSubsystem::PreloadNativeTexturePacks()
{
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	if (!Settings || Settings->NativeThemeTextureRoot.IsEmpty())
	{
		return;
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	static constexpr EEFProjectHUDThemePreset Presets[] = {
		EEFProjectHUDThemePreset::Red,
		EEFProjectHUDThemePreset::Blue,
		EEFProjectHUDThemePreset::Purple,
		EEFProjectHUDThemePreset::Green,
		EEFProjectHUDThemePreset::Black
	};

	for (const EEFProjectHUDThemePreset Preset : Presets)
	{
		const FString PackPath = FString::Printf(
			TEXT("%s/%s"),
			*Settings->NativeThemeTextureRoot,
			*EFProjectUITheme::GetPresetName(Preset));
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*PackPath), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
			{
				LoadedThemeTextures.Add(Texture->GetPathName(), Texture);
			}
		}
	}

	UE_LOG(
		LogEFProjectDynamicTheme,
		Log,
		TEXT("Preloaded %d native HUD theme textures from %s."),
		LoadedThemeTextures.Num(),
		*Settings->NativeThemeTextureRoot);
}

void UEFProjectDynamicThemeSubsystem::CompactRegisteredWidgets()
{
	for (auto It = RegisteredWidgets.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
