#pragma once

#include "CoreMinimal.h"
#include "EFProjectUITheme.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFProjectDynamicThemeSubsystem.generated.h"

class UTexture2D;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FEFProjectDynamicThemeChanged,
	EEFProjectHUDThemePreset,
	Preset,
	FProjectHUDThemeColors,
	Theme,
	int32,
	Revision);

/**
 * Event-driven owner of the project HUD appearance.
 *
 * Widgets register once when constructed. A complete semantic profile and its
 * matching native texture pack are then applied atomically only when the
 * resolved preset changes. There is deliberately no periodic brush mutation.
 */
UCLASS()
class EFPROJECTSYSTEMSUI_API UEFProjectDynamicThemeSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void SetThemePreset(EEFProjectHUDThemePreset Preset);

	/** Restores Auto mode, which always resolves to neutral Black. */
	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void ResetToConfiguredTheme();

	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void RegisterThemedWidget(UUserWidget* UserWidget);

	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void UnregisterThemedWidget(UUserWidget* UserWidget);

	/**
	 * Applies the already-resolved theme to one widget without changing the
	 * selected preset or advancing the theme revision.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void ReapplyCurrentThemeToWidget(UUserWidget* UserWidget);

	/**
	 * Resolves a source texture through the active native theme pack. Missing
	 * variants deliberately fall back to the supplied source texture.
	 */
	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	UTexture2D* ResolveCurrentThemeTexture(UTexture2D* SourceTexture) const;

	/**
	 * Explicit compatibility pass for project-owned Blueprint-only widgets.
	 * This is called on real theme changes, never from a timer.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme")
	void RefreshAllThemedWidgets();

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	FProjectHUDThemeColors GetActiveTheme() const;

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	EEFProjectHUDThemePreset GetSelectionPreset() const { return SelectionPreset; }

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	EEFProjectHUDThemePreset GetResolvedPreset() const { return ResolvedPreset; }

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	int32 GetThemeRevision() const { return ThemeRevision; }

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	bool IsManualOverrideActive() const
	{
		return SelectionPreset != EEFProjectHUDThemePreset::Auto;
	}

	UPROPERTY(BlueprintAssignable, Category = "EF|UI Theme")
	FEFProjectDynamicThemeChanged OnDynamicThemeChanged;

	static bool IsWidgetClassPathInScope(
		const FString& ClassPath,
		const TArray<FString>& Prefixes);

	static FString BuildThemedTextureObjectPath(
		const FString& SourceObjectPath,
		const FString& ThemeRoot,
		EEFProjectHUDThemePreset Preset);

private:
	void ApplyResolvedPreset(EEFProjectHUDThemePreset Preset);
	EEFProjectHUDThemePreset ResolveAutomaticPreset() const;
	const FProjectHUDThemeColors& ResolveThemeProfile(
		EEFProjectHUDThemePreset Preset) const;
	void HandleCharacterCreationWidgetReady(UUserWidget* UserWidget);
	void HandleCoreThemeChanged(const FProjectHUDThemeColors& Theme);
	void ApplyThemeToWidget(
		UUserWidget* UserWidget,
		const FProjectHUDThemeColors& Theme,
		EEFProjectHUDThemePreset Preset) const;
	bool ReplaceBrushTexture(
		struct FSlateBrush& Brush,
		EEFProjectHUDThemePreset Preset) const;
	UTexture2D* ResolveThemedTexture(
		const UTexture2D* SourceTexture,
		EEFProjectHUDThemePreset Preset) const;
	void PreloadNativeTexturePacks();
	void CompactRegisteredWidgets();

	FDelegateHandle CoreThemeChangedHandle;
	FDelegateHandle CharacterCreationWidgetReadyHandle;

	EEFProjectHUDThemePreset SelectionPreset = EEFProjectHUDThemePreset::Auto;
	EEFProjectHUDThemePreset ResolvedPreset = EEFProjectHUDThemePreset::Black;
	int32 ThemeRevision = 0;

	TSet<TWeakObjectPtr<UUserWidget>> RegisteredWidgets;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> LoadedThemeTextures;
};
