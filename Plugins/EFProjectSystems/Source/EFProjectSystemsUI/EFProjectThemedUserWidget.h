#pragma once

#include "Blueprint/UserWidget.h"
#include "EFProjectUITheme.h"
#include "EFProjectThemedUserWidget.generated.h"

class UTexture2D;
class UImage;

/**
 * Registration base for project-owned runtime widgets.
 *
 * It does not rebuild a WidgetTree and therefore preserves every Blueprint
 * hierarchy, animation, binding, slot and layout decision made by the current
 * UI. Its only responsibility is joining the event-driven theme lifecycle.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSUI_API UEFProjectThemedUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void NotifyProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision);

	UFUNCTION(BlueprintPure, Category = "EF|UI Theme")
	int32 GetLastAppliedThemeRevision() const { return LastAppliedThemeRevision; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Native counterpart to OnProjectThemeApplied for widgets whose visual
	 * surface is built directly in Slate rather than represented in WidgetTree.
	 * The default implementation is intentionally empty.
	 */
	virtual void NativeOnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision);

	/**
	 * Call at the logical end of an overriding NativeConstruct after its tree,
	 * bindings and procedural visual refresh have finished. This reapplies the
	 * current theme only; it never changes the selected preset.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF|UI Theme", meta = (BlueprintProtected = "true"))
	void ReapplyProjectThemeAfterNativeConstruct();

	/**
	 * Resolves a texture through the active native theme pack. If that pack has
	 * no matching asset, the original texture is returned unchanged.
	 */
	UFUNCTION(BlueprintPure, Category = "EF|UI Theme", meta = (BlueprintProtected = "true"))
	UTexture2D* ResolveProjectThemeTexture(UTexture2D* SourceTexture) const;

	/**
	 * Native theme textures already contain their final RGB palette. Preserve
	 * the caller's opacity while preventing later refreshes or animations from
	 * multiplying those pixels by a second color tint. Original/fallback
	 * textures retain the requested tint unchanged.
	 */
	UFUNCTION(BlueprintPure, Category = "EF|UI Theme", meta = (BlueprintProtected = "true"))
	FLinearColor ResolveProjectThemeImageTint(
		const UImage* Image,
		const FLinearColor& RequestedTint) const;

	/**
	 * Optional event for a widget with custom procedural paint logic. Generic
	 * brushes and native textures have already been updated when this fires.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EF|UI Theme")
	void OnProjectThemeApplied(
		EEFProjectHUDThemePreset Preset,
		const FProjectHUDThemeColors& Theme,
		int32 Revision);

private:
	int32 LastAppliedThemeRevision = INDEX_NONE;
};
