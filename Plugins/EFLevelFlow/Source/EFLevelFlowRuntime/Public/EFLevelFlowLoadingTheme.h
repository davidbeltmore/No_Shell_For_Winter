#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * Semantic colors used by EFLevelFlow's native Slate loading fallback.
 *
 * The defaults intentionally preserve the fallback's original appearance so
 * EFLevelFlow remains self-contained when no project theme provider is
 * registered.
 */
struct EFLEVELFLOWRUNTIME_API FEFLevelFlowLoadingTheme
{
	FLinearColor PanelBackground = FLinearColor(0.01f, 0.01f, 0.01f, 0.9f);
	FLinearColor TitleText = FLinearColor::White;
	FLinearColor SecondaryText = FLinearColor(0.82f, 0.82f, 0.82f, 1.0f);
	FLinearColor ActivityIndicator = FLinearColor::White;
};

using FEFLevelFlowLoadingThemeProvider = TFunction<FEFLevelFlowLoadingTheme()>;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnEFLevelFlowLoadingThemeChanged,
	const FEFLevelFlowLoadingTheme&);

/**
 * Dependency-inversion seam for project-owned HUD palettes.
 *
 * EFLevelFlow owns only this neutral contract. A higher-level UI module may
 * register a provider and explicitly refresh it when its event-driven theme
 * source changes, without introducing an EFLevelFlow -> project dependency.
 */
namespace EFLevelFlowLoadingTheme
{
	EFLEVELFLOWRUNTIME_API FDelegateHandle RegisterProvider(
		FEFLevelFlowLoadingThemeProvider Provider);

	EFLEVELFLOWRUNTIME_API void UnregisterProvider(FDelegateHandle ProviderHandle);

	/** Re-resolves and broadcasts the registered provider without polling. */
	EFLEVELFLOWRUNTIME_API void RefreshProvider(FDelegateHandle ProviderHandle);

	/** Resolves the registered provider, or the original neutral defaults. */
	EFLEVELFLOWRUNTIME_API FEFLevelFlowLoadingTheme ResolveTheme();

	EFLEVELFLOWRUNTIME_API FOnEFLevelFlowLoadingThemeChanged& OnThemeChanged();
}
