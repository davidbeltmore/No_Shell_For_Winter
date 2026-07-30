#include "EFProjectUITheme.h"

#include "EFProjectUISettings.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/UObjectBase.h"

namespace EFProjectUIThemePrivate
{
	FProjectHUDThemeColors RuntimeTheme;
	bool bHasRuntimeTheme = false;
	FOnProjectUIThemeChanged ThemeChanged;

	FLinearColor Color(
		const float Red,
		const float Green,
		const float Blue,
		const float Alpha = 1.0f)
	{
		return FLinearColor(Red, Green, Blue, Alpha);
	}

	FLinearColor ScaleRgb(const FLinearColor& Color, const float Scale, const float Alpha = 1.0f)
	{
		return FLinearColor(
			FMath::Clamp(Color.R * Scale, 0.0f, 1.0f),
			FMath::Clamp(Color.G * Scale, 0.0f, 1.0f),
			FMath::Clamp(Color.B * Scale, 0.0f, 1.0f),
			Alpha);
	}
}

FProjectHUDThemeColors::FProjectHUDThemeColors()
	: PanelFill(0.010f, 0.011f, 0.014f, 1.0f)
	, PanelFillDeep(0.002f, 0.002f, 0.003f, 1.0f)
	, SectionFill(0.022f, 0.024f, 0.030f, 1.0f)
	, Outline(0.36f, 0.38f, 0.43f, 1.0f)
	, OutlineDim(0.14f, 0.15f, 0.18f, 1.0f)
	, Haze(0.30f, 0.32f, 0.38f, 1.0f)
	, TitleText(1.0f, 1.0f, 1.0f, 1.0f)
	, PrimaryText(0.92f, 0.93f, 0.96f, 1.0f)
	, SecondaryText(0.69f, 0.71f, 0.76f, 1.0f)
	, MutedText(0.46f, 0.48f, 0.53f, 1.0f)
	, Accent(0.50f, 0.52f, 0.58f, 1.0f)
	, AccentSoft(0.82f, 0.84f, 0.90f, 1.0f)
	, AccentMuted(0.20f, 0.21f, 0.25f, 1.0f)
	, Warning(0.96f, 0.22f, 0.08f, 1.0f)
	, BadgeFill(0.25f, 0.26f, 0.30f, 1.0f)
	, BadgeText(0.98f, 0.98f, 0.98f, 1.0f)
	, Positive(0.18f, 0.78f, 0.34f, 1.0f)
	, Negative(0.96f, 0.18f, 0.12f, 1.0f)
{
}

namespace EFProjectUITheme
{
	const FProjectHUDThemeColors& GetTheme()
	{
		static const FProjectHUDThemeColors FallbackTheme;
		if (EFProjectUIThemePrivate::bHasRuntimeTheme)
		{
			return EFProjectUIThemePrivate::RuntimeTheme;
		}

		// Several widget translation units cache palette colors in namespace-level
		// FLinearColor values. In monolithic packaged builds those initializers run
		// before CoreUObject, so GetDefault() is not legal yet.
		if (!UObjectInitialized())
		{
			return FallbackTheme;
		}

		static bool bResolvingTheme = false;
		if (bResolvingTheme)
		{
			return FallbackTheme;
		}

		TGuardValue<bool> ResolvingGuard(bResolvingTheme, true);
		if (const UEFProjectUISettings* Settings = UEFProjectUISettings::Get())
		{
			return Settings->HUDTheme;
		}
		return FallbackTheme;
	}

	FProjectHUDThemeColors BuildPresetTheme(const EEFProjectHUDThemePreset Preset)
	{
		using namespace EFProjectUIThemePrivate;

		FProjectHUDThemeColors Theme;
		Theme.Warning = Color(0.96f, 0.22f, 0.08f);
		Theme.Positive = Color(0.18f, 0.78f, 0.34f);
		Theme.Negative = Color(0.96f, 0.18f, 0.12f);
		Theme.BadgeText = Color(0.98f, 0.98f, 0.98f);

		switch (Preset)
		{
		case EEFProjectHUDThemePreset::Blue:
			Theme.PanelFillDeep = Color(0.003f, 0.007f, 0.022f);
			Theme.PanelFill = Color(0.006f, 0.018f, 0.052f);
			Theme.SectionFill = Color(0.010f, 0.032f, 0.082f);
			Theme.Outline = Color(0.055f, 0.30f, 0.92f);
			Theme.OutlineDim = Color(0.025f, 0.12f, 0.34f);
			Theme.Haze = Color(0.04f, 0.26f, 0.88f);
			Theme.TitleText = Color(0.86f, 0.94f, 1.0f);
			Theme.PrimaryText = Color(0.94f, 0.97f, 1.0f);
			Theme.SecondaryText = Color(0.66f, 0.78f, 0.94f);
			Theme.MutedText = Color(0.43f, 0.54f, 0.70f);
			Theme.Accent = Color(0.035f, 0.24f, 0.88f);
			Theme.AccentSoft = Color(0.18f, 0.52f, 1.0f);
			Theme.AccentMuted = Color(0.025f, 0.11f, 0.34f);
			Theme.BadgeFill = Color(0.025f, 0.16f, 0.58f);
			break;

		case EEFProjectHUDThemePreset::Purple:
			Theme.PanelFillDeep = Color(0.010f, 0.003f, 0.020f);
			Theme.PanelFill = Color(0.026f, 0.008f, 0.052f);
			Theme.SectionFill = Color(0.044f, 0.014f, 0.082f);
			Theme.Outline = Color(0.47f, 0.095f, 0.84f);
			Theme.OutlineDim = Color(0.19f, 0.040f, 0.34f);
			Theme.Haze = Color(0.52f, 0.08f, 0.86f);
			Theme.TitleText = Color(0.96f, 0.88f, 1.0f);
			Theme.PrimaryText = Color(0.98f, 0.95f, 1.0f);
			Theme.SecondaryText = Color(0.80f, 0.68f, 0.92f);
			Theme.MutedText = Color(0.58f, 0.48f, 0.68f);
			Theme.Accent = Color(0.50f, 0.075f, 0.84f);
			Theme.AccentSoft = Color(0.75f, 0.30f, 1.0f);
			Theme.AccentMuted = Color(0.22f, 0.045f, 0.36f);
			Theme.BadgeFill = Color(0.35f, 0.055f, 0.58f);
			break;

		case EEFProjectHUDThemePreset::Green:
			Theme.PanelFillDeep = Color(0.002f, 0.012f, 0.006f);
			Theme.PanelFill = Color(0.004f, 0.030f, 0.014f);
			Theme.SectionFill = Color(0.006f, 0.052f, 0.024f);
			Theme.Outline = Color(0.035f, 0.60f, 0.22f);
			Theme.OutlineDim = Color(0.014f, 0.24f, 0.090f);
			Theme.Haze = Color(0.025f, 0.56f, 0.18f);
			Theme.TitleText = Color(0.86f, 1.0f, 0.91f);
			Theme.PrimaryText = Color(0.94f, 1.0f, 0.96f);
			Theme.SecondaryText = Color(0.65f, 0.88f, 0.72f);
			Theme.MutedText = Color(0.42f, 0.63f, 0.49f);
			Theme.Accent = Color(0.025f, 0.58f, 0.18f);
			Theme.AccentSoft = Color(0.12f, 0.88f, 0.38f);
			Theme.AccentMuted = Color(0.012f, 0.25f, 0.080f);
			Theme.BadgeFill = Color(0.018f, 0.40f, 0.13f);
			break;

		case EEFProjectHUDThemePreset::Auto:
		case EEFProjectHUDThemePreset::Black:
		default:
			Theme.PanelFillDeep = Color(0.002f, 0.002f, 0.003f);
			Theme.PanelFill = Color(0.010f, 0.011f, 0.014f);
			Theme.SectionFill = Color(0.022f, 0.024f, 0.030f);
			Theme.Outline = Color(0.36f, 0.38f, 0.43f);
			Theme.OutlineDim = Color(0.14f, 0.15f, 0.18f);
			Theme.Haze = Color(0.30f, 0.32f, 0.38f);
			Theme.TitleText = Color(1.0f, 1.0f, 1.0f);
			Theme.PrimaryText = Color(0.92f, 0.93f, 0.96f);
			Theme.SecondaryText = Color(0.69f, 0.71f, 0.76f);
			Theme.MutedText = Color(0.46f, 0.48f, 0.53f);
			Theme.Accent = Color(0.50f, 0.52f, 0.58f);
			Theme.AccentSoft = Color(0.82f, 0.84f, 0.90f);
			Theme.AccentMuted = Color(0.20f, 0.21f, 0.25f);
			Theme.BadgeFill = Color(0.25f, 0.26f, 0.30f);
			break;

		case EEFProjectHUDThemePreset::Red:
			Theme.PanelFillDeep = Color(0.012f, 0.002f, 0.003f);
			Theme.PanelFill = Color(0.036f, 0.004f, 0.006f);
			Theme.SectionFill = Color(0.064f, 0.007f, 0.010f);
			Theme.Outline = Color(0.62f, 0.022f, 0.028f);
			Theme.OutlineDim = Color(0.25f, 0.008f, 0.012f);
			Theme.Haze = Color(0.66f, 0.025f, 0.030f);
			Theme.TitleText = Color(1.0f, 0.88f, 0.88f);
			Theme.PrimaryText = Color(1.0f, 0.95f, 0.95f);
			Theme.SecondaryText = Color(0.90f, 0.67f, 0.67f);
			Theme.MutedText = Color(0.68f, 0.43f, 0.44f);
			Theme.Accent = Color(0.70f, 0.018f, 0.024f);
			Theme.AccentSoft = Color(1.0f, 0.12f, 0.12f);
			Theme.AccentMuted = Color(0.31f, 0.008f, 0.014f);
			Theme.BadgeFill = Color(0.50f, 0.014f, 0.022f);
			break;
		}

		return Theme;
	}

	FString GetPresetName(const EEFProjectHUDThemePreset Preset)
	{
		switch (Preset)
		{
		case EEFProjectHUDThemePreset::Auto: return TEXT("Auto");
		case EEFProjectHUDThemePreset::Red: return TEXT("Red");
		case EEFProjectHUDThemePreset::Blue: return TEXT("Blue");
		case EEFProjectHUDThemePreset::Purple: return TEXT("Purple");
		case EEFProjectHUDThemePreset::Green: return TEXT("Green");
		case EEFProjectHUDThemePreset::Black: return TEXT("Black");
		default: return TEXT("Black");
		}
	}

	bool TryParsePreset(const FString& Value, EEFProjectHUDThemePreset& OutPreset)
	{
		const FString Normalized = Value.TrimStartAndEnd();
		if (Normalized.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Auto;
			return true;
		}
		if (Normalized.Equals(TEXT("Red"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Crimson"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Red;
			return true;
		}
		if (Normalized.Equals(TEXT("Blue"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Cobalt"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Blue;
			return true;
		}
		if (Normalized.Equals(TEXT("Purple"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Violet"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Purple;
			return true;
		}
		if (Normalized.Equals(TEXT("Green"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Emerald"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Green;
			return true;
		}
		if (Normalized.Equals(TEXT("Black"), ESearchCase::IgnoreCase)
			|| Normalized.Equals(TEXT("Obsidian"), ESearchCase::IgnoreCase))
		{
			OutPreset = EEFProjectHUDThemePreset::Black;
			return true;
		}
		return false;
	}

	bool AreEquivalent(
		const FProjectHUDThemeColors& Left,
		const FProjectHUDThemeColors& Right,
		const float Tolerance)
	{
		return Left.PanelFill.Equals(Right.PanelFill, Tolerance)
			&& Left.PanelFillDeep.Equals(Right.PanelFillDeep, Tolerance)
			&& Left.SectionFill.Equals(Right.SectionFill, Tolerance)
			&& Left.Outline.Equals(Right.Outline, Tolerance)
			&& Left.OutlineDim.Equals(Right.OutlineDim, Tolerance)
			&& Left.Haze.Equals(Right.Haze, Tolerance)
			&& Left.TitleText.Equals(Right.TitleText, Tolerance)
			&& Left.PrimaryText.Equals(Right.PrimaryText, Tolerance)
			&& Left.SecondaryText.Equals(Right.SecondaryText, Tolerance)
			&& Left.MutedText.Equals(Right.MutedText, Tolerance)
			&& Left.Accent.Equals(Right.Accent, Tolerance)
			&& Left.AccentSoft.Equals(Right.AccentSoft, Tolerance)
			&& Left.AccentMuted.Equals(Right.AccentMuted, Tolerance)
			&& Left.Warning.Equals(Right.Warning, Tolerance)
			&& Left.BadgeFill.Equals(Right.BadgeFill, Tolerance)
			&& Left.BadgeText.Equals(Right.BadgeText, Tolerance)
			&& Left.Positive.Equals(Right.Positive, Tolerance)
			&& Left.Negative.Equals(Right.Negative, Tolerance);
	}

	FProjectHUDThemeColors BuildThemeFromAccent(const FLinearColor& AccentColor)
	{
		const FLinearColor Accent(
			FMath::Clamp(AccentColor.R, 0.0f, 1.0f),
			FMath::Clamp(AccentColor.G, 0.0f, 1.0f),
			FMath::Clamp(AccentColor.B, 0.0f, 1.0f),
			1.0f);

		FProjectHUDThemeColors Theme;
		Theme.PanelFill = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.062f);
		Theme.PanelFillDeep = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.024f);
		Theme.SectionFill = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.095f);
		Theme.Outline = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.57f);
		Theme.OutlineDim = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.286f);
		Theme.Haze = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.667f);
		Theme.TitleText = FLinearColor::White;
		Theme.PrimaryText = FLinearColor::White;
		Theme.SecondaryText = FLinearColor::White;
		Theme.MutedText = FLinearColor(0.72f, 0.72f, 0.72f, 1.0f);
		Theme.Accent = Accent;
		Theme.AccentSoft = EFProjectUIThemePrivate::ScaleRgb(Accent, 1.19f);
		Theme.AccentMuted = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.524f);
		Theme.Warning = FLinearColor(0.96f, 0.24f, 0.16f, 1.0f);
		Theme.BadgeFill = EFProjectUIThemePrivate::ScaleRgb(Accent, 0.905f);
		Theme.BadgeText = FLinearColor::White;
		Theme.Positive = FLinearColor(0.76f, 0.82f, 0.74f, 1.0f);
		Theme.Negative = Theme.Warning;
		return Theme;
	}

	void SetRuntimeTheme(const FProjectHUDThemeColors& Theme)
	{
		if (EFProjectUIThemePrivate::bHasRuntimeTheme
			&& AreEquivalent(EFProjectUIThemePrivate::RuntimeTheme, Theme))
		{
			return;
		}

		EFProjectUIThemePrivate::RuntimeTheme = Theme;
		EFProjectUIThemePrivate::bHasRuntimeTheme = true;
		EFProjectUIThemePrivate::ThemeChanged.Broadcast(EFProjectUIThemePrivate::RuntimeTheme);
	}

	void ClearRuntimeTheme()
	{
		EFProjectUIThemePrivate::bHasRuntimeTheme = false;
		EFProjectUIThemePrivate::ThemeChanged.Broadcast(GetTheme());
	}

	bool HasRuntimeTheme()
	{
		return EFProjectUIThemePrivate::bHasRuntimeTheme;
	}

	FOnProjectUIThemeChanged& OnThemeChanged()
	{
		return EFProjectUIThemePrivate::ThemeChanged;
	}

	FLinearColor WithAlpha(const FLinearColor& Color, const float Alpha) { return FLinearColor(Color.R, Color.G, Color.B, Alpha); }
	FLinearColor PanelFill(const float Alpha) { return WithAlpha(GetTheme().PanelFill, Alpha); }
	FLinearColor PanelFillDeep(const float Alpha) { return WithAlpha(GetTheme().PanelFillDeep, Alpha); }
	FLinearColor SectionFill(const float Alpha) { return WithAlpha(GetTheme().SectionFill, Alpha); }
	FLinearColor Outline(const float Alpha) { return WithAlpha(GetTheme().Outline, Alpha); }
	FLinearColor OutlineDim(const float Alpha) { return WithAlpha(GetTheme().OutlineDim, Alpha); }
	FLinearColor Haze(const float Alpha) { return WithAlpha(GetTheme().Haze, Alpha); }
	FLinearColor TitleText(const float Alpha) { return WithAlpha(GetTheme().TitleText, Alpha); }
	FLinearColor PrimaryText(const float Alpha) { return WithAlpha(GetTheme().PrimaryText, Alpha); }
	FLinearColor SecondaryText(const float Alpha) { return WithAlpha(GetTheme().SecondaryText, Alpha); }
	FLinearColor MutedText(const float Alpha) { return WithAlpha(GetTheme().MutedText, Alpha); }
	FLinearColor Accent(const float Alpha) { return WithAlpha(GetTheme().Accent, Alpha); }
	FLinearColor AccentSoft(const float Alpha) { return WithAlpha(GetTheme().AccentSoft, Alpha); }
	FLinearColor AccentMuted(const float Alpha) { return WithAlpha(GetTheme().AccentMuted, Alpha); }
	FLinearColor Warning(const float Alpha) { return WithAlpha(GetTheme().Warning, Alpha); }
	FLinearColor BadgeFill(const float Alpha) { return WithAlpha(GetTheme().BadgeFill, Alpha); }
	FLinearColor BadgeText(const float Alpha) { return WithAlpha(GetTheme().BadgeText, Alpha); }
	FLinearColor Positive(const float Alpha) { return WithAlpha(GetTheme().Positive, Alpha); }
	FLinearColor Negative(const float Alpha) { return WithAlpha(GetTheme().Negative, Alpha); }
}
