#pragma once

#include "CoreMinimal.h"
#include "EFProjectUITheme.generated.h"

/**
 * Complete authored HUD profiles. Auto is the neutral Black selection policy
 * owned by the runtime subsystem; character identity never changes HUD color.
 */
UENUM(BlueprintType)
enum class EEFProjectHUDThemePreset : uint8
{
	Auto UMETA(DisplayName = "Auto (Neutral Black)"),
	Red UMETA(DisplayName = "Red"),
	Blue UMETA(DisplayName = "Blue"),
	Purple UMETA(DisplayName = "Purple"),
	Green UMETA(DisplayName = "Green"),
	Black UMETA(DisplayName = "Black")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSCORE_API FProjectHUDThemeColors
{
	GENERATED_BODY()

	FProjectHUDThemeColors();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor PanelFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor PanelFillDeep;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor SectionFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor Outline;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor OutlineDim;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels") FLinearColor Haze;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text") FLinearColor TitleText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text") FLinearColor PrimaryText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text") FLinearColor SecondaryText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text") FLinearColor MutedText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents") FLinearColor Accent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents") FLinearColor AccentSoft;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents") FLinearColor AccentMuted;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents") FLinearColor Warning;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badges") FLinearColor BadgeFill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badges") FLinearColor BadgeText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "States") FLinearColor Positive;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "States") FLinearColor Negative;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnProjectUIThemeChanged, const FProjectHUDThemeColors&);

namespace EFProjectUITheme
{
	EFPROJECTSYSTEMSCORE_API const FProjectHUDThemeColors& GetTheme();
	EFPROJECTSYSTEMSCORE_API FProjectHUDThemeColors BuildPresetTheme(EEFProjectHUDThemePreset Preset);
	EFPROJECTSYSTEMSCORE_API FString GetPresetName(EEFProjectHUDThemePreset Preset);
	EFPROJECTSYSTEMSCORE_API bool TryParsePreset(const FString& Value, EEFProjectHUDThemePreset& OutPreset);
	EFPROJECTSYSTEMSCORE_API bool AreEquivalent(
		const FProjectHUDThemeColors& Left,
		const FProjectHUDThemeColors& Right,
		float Tolerance = KINDA_SMALL_NUMBER);

	/** Legacy compatibility helper. Runtime HUD selection uses BuildPresetTheme. */
	EFPROJECTSYSTEMSCORE_API FProjectHUDThemeColors BuildThemeFromAccent(const FLinearColor& AccentColor);
	EFPROJECTSYSTEMSCORE_API void SetRuntimeTheme(const FProjectHUDThemeColors& Theme);
	EFPROJECTSYSTEMSCORE_API void ClearRuntimeTheme();
	EFPROJECTSYSTEMSCORE_API bool HasRuntimeTheme();
	EFPROJECTSYSTEMSCORE_API FOnProjectUIThemeChanged& OnThemeChanged();
	EFPROJECTSYSTEMSCORE_API FLinearColor WithAlpha(const FLinearColor& Color, float Alpha);
	EFPROJECTSYSTEMSCORE_API FLinearColor PanelFill(float Alpha = 0.96f);
	EFPROJECTSYSTEMSCORE_API FLinearColor PanelFillDeep(float Alpha = 0.98f);
	EFPROJECTSYSTEMSCORE_API FLinearColor SectionFill(float Alpha = 0.95f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Outline(float Alpha = 0.86f);
	EFPROJECTSYSTEMSCORE_API FLinearColor OutlineDim(float Alpha = 0.42f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Haze(float Alpha = 0.16f);
	EFPROJECTSYSTEMSCORE_API FLinearColor TitleText(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor PrimaryText(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor SecondaryText(float Alpha = 0.96f);
	EFPROJECTSYSTEMSCORE_API FLinearColor MutedText(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Accent(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor AccentSoft(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor AccentMuted(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Warning(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor BadgeFill(float Alpha = 0.98f);
	EFPROJECTSYSTEMSCORE_API FLinearColor BadgeText(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Positive(float Alpha = 1.0f);
	EFPROJECTSYSTEMSCORE_API FLinearColor Negative(float Alpha = 1.0f);
}
