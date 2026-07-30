#pragma once

#include "CoreMinimal.h"
#include "EFProjectUIPalette.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "EFProjectUISettings.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSCORE_API FProjectInnerStateEntryDefinition
{
	GENERATED_BODY()

	FProjectInnerStateEntryDefinition()
		: EntryName(NAME_None)
		, DisplayLabel()
		, Monogram()
		, bIsSensation(false)
		, AccentTint(EFProjectUIPalette::Accent())
		, SortOrder(0)
	{
	}

	FProjectInnerStateEntryDefinition(
		const FName InEntryName,
		const FString& InDisplayLabel,
		const FString& InMonogram,
		const bool bInIsSensation,
		const FLinearColor& InAccentTint,
		const int32 InSortOrder)
		: EntryName(InEntryName)
		, DisplayLabel(InDisplayLabel)
		, Monogram(InMonogram)
		, bIsSensation(bInIsSensation)
		, AccentTint(InAccentTint)
		, SortOrder(InSortOrder)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FName EntryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FString Monogram;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	bool bIsSensation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner State")
	int32 SortOrder;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSCORE_API FProjectInnerDoctrineEntryDefinition
{
	GENERATED_BODY()

	FProjectInnerDoctrineEntryDefinition()
		: AttributeName(NAME_None)
		, DisplayLabel()
		, ShortLabel()
		, IconTexture(nullptr)
		, MenuDisplayLabel()
		, MenuIconTexture(nullptr)
		, MenuWatermarkTexture(nullptr)
		, AccentTint(EFProjectUIPalette::Accent())
		, SortOrder(0)
	{
	}

	FProjectInnerDoctrineEntryDefinition(
		const FName InAttributeName,
		const FString& InDisplayLabel,
		const FString& InShortLabel,
		const TSoftObjectPtr<UTexture2D>& InIconTexture,
		const FString& InMenuDisplayLabel,
		const TSoftObjectPtr<UTexture2D>& InMenuIconTexture,
		const TSoftObjectPtr<UTexture2D>& InMenuWatermarkTexture,
		const FLinearColor& InAccentTint,
		const int32 InSortOrder)
		: AttributeName(InAttributeName)
		, DisplayLabel(InDisplayLabel)
		, ShortLabel(InShortLabel)
		, IconTexture(InIconTexture)
		, MenuDisplayLabel(InMenuDisplayLabel)
		, MenuIconTexture(InMenuIconTexture)
		, MenuWatermarkTexture(InMenuWatermarkTexture)
		, AccentTint(InAccentTint)
		, SortOrder(InSortOrder)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FString ShortLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TSoftObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FString MenuDisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TSoftObjectPtr<UTexture2D> MenuIconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TSoftObjectPtr<UTexture2D> MenuWatermarkTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 SortOrder;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSCORE_API FProjectChronicleChannelDefinition
{
	GENERATED_BODY()

	FProjectChronicleChannelDefinition()
		: ChannelName(NAME_None)
		, DisplayLabel()
		, AccentTint(EFProjectUIPalette::Accent())
		, BadgeFillTint(EFProjectUIPalette::BadgeFill(0.96f))
		, BadgeTextTint(EFProjectUIPalette::BadgeText())
	{
	}

	FProjectChronicleChannelDefinition(
		const FName InChannelName,
		const FString& InDisplayLabel,
		const FLinearColor& InAccentTint,
		const FLinearColor& InBadgeFillTint,
		const FLinearColor& InBadgeTextTint)
		: ChannelName(InChannelName)
		, DisplayLabel(InDisplayLabel)
		, AccentTint(InAccentTint)
		, BadgeFillTint(InBadgeFillTint)
		, BadgeTextTint(InBadgeTextTint)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FName ChannelName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FString DisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor AccentTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor BadgeFillTint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle")
	FLinearColor BadgeTextTint;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Project UI"))
class EFPROJECTSYSTEMSCORE_API UEFProjectUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFProjectUISettings();

	static const UEFProjectUISettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme")
	FProjectHUDThemeColors HUDTheme;

	/** Enables the event-driven project theme system. No periodic restyling occurs. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme")
	bool bEnableDynamicRuntimeTheme = true;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Profiles")
	FProjectHUDThemeColors RedTheme;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Profiles")
	FProjectHUDThemeColors BlueTheme;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Profiles")
	FProjectHUDThemeColors PurpleTheme;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Profiles")
	FProjectHUDThemeColors GreenTheme;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Profiles")
	FProjectHUDThemeColors BlackTheme;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Selection")
	EEFProjectHUDThemePreset DefaultThemePreset = EEFProjectHUDThemePreset::Black;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Selection")
	EEFProjectHUDThemePreset MaleThemePreset = EEFProjectHUDThemePreset::Black;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Selection")
	EEFProjectHUDThemePreset FemaleThemePreset = EEFProjectHUDThemePreset::Black;

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Selection")
	EEFProjectHUDThemePreset UnknownIdentityThemePreset = EEFProjectHUDThemePreset::Black;

	/** Cooked root containing Red/Blue/Purple/Green/Black native chrome packs. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Textures")
	FString NativeThemeTextureRoot = TEXT("/Game/_Game/Textures/UI/Themes");

	UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Theme|Scope")
	TArray<FString> DynamicThemeWidgetClassPathPrefixes;

	const FProjectHUDThemeColors& GetThemeProfile(EEFProjectHUDThemePreset Preset) const;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath NeedsWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath DayCycleWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets|Inner State")
	TArray<FProjectInnerStateEntryDefinition> InnerStateEntryDefinitions;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath StatusWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath InnerDoctrineWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath InnerDoctrineExchangeMenuWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	TArray<FDirectoryPath> WidgetDiscoveryRootPaths;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath LockpickingWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath LockpickingPromptWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets|Inner Doctrine")
	TArray<FProjectInnerDoctrineEntryDefinition> InnerDoctrineEntryDefinitions;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets")
	FSoftClassPath ActivityFeedWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets|Gameplay Debug")
	FSoftClassPath GameplayDebugMenuWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets|Gameplay Debug")
	FSoftClassPath GameplayDebugMenuOptionRowWidgetClass;

	UPROPERTY(EditAnywhere, Config, Category = "Widgets|Chronicle")
	TArray<FProjectChronicleChannelDefinition> ChronicleChannelDefinitions;
};
