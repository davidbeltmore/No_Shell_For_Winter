#include "EFProjectSystemsUI.h"

#include "EFLevelFlowLoadingTheme.h"
#include "EFProjectUITheme.h"
#include "Modules/ModuleManager.h"

namespace EFProjectSystemsUIPrivate
{
	FEFLevelFlowLoadingTheme BuildLevelFlowLoadingTheme(
		const FProjectHUDThemeColors& ProjectTheme)
	{
		FEFLevelFlowLoadingTheme LoadingTheme;
		LoadingTheme.PanelBackground =
			EFProjectUITheme::WithAlpha(ProjectTheme.PanelFillDeep, 0.9f);
		LoadingTheme.TitleText =
			EFProjectUITheme::WithAlpha(ProjectTheme.TitleText, 1.0f);
		LoadingTheme.SecondaryText =
			EFProjectUITheme::WithAlpha(ProjectTheme.SecondaryText, 1.0f);
		LoadingTheme.ActivityIndicator =
			EFProjectUITheme::WithAlpha(ProjectTheme.AccentSoft, 1.0f);
		return LoadingTheme;
	}

	FEFLevelFlowLoadingTheme ResolveLevelFlowLoadingTheme()
	{
		return BuildLevelFlowLoadingTheme(EFProjectUITheme::GetTheme());
	}
}

void FEFProjectSystemsUIModule::StartupModule()
{
	LevelFlowThemeProviderHandle = EFLevelFlowLoadingTheme::RegisterProvider(
		&EFProjectSystemsUIPrivate::ResolveLevelFlowLoadingTheme);
	ProjectThemeChangedHandle = EFProjectUITheme::OnThemeChanged().AddRaw(
		this,
		&FEFProjectSystemsUIModule::HandleProjectThemeChanged);
}

void FEFProjectSystemsUIModule::ShutdownModule()
{
	if (ProjectThemeChangedHandle.IsValid())
	{
		EFProjectUITheme::OnThemeChanged().Remove(ProjectThemeChangedHandle);
		ProjectThemeChangedHandle.Reset();
	}

	if (LevelFlowThemeProviderHandle.IsValid())
	{
		EFLevelFlowLoadingTheme::UnregisterProvider(LevelFlowThemeProviderHandle);
		LevelFlowThemeProviderHandle.Reset();
	}
}

void FEFProjectSystemsUIModule::HandleProjectThemeChanged(
	const FProjectHUDThemeColors& Theme)
{
	// The provider resolves the authoritative profile. The event payload exists
	// to make this refresh strictly change-driven; no Slate tick or timer polls.
	(void)Theme;
	EFLevelFlowLoadingTheme::RefreshProvider(LevelFlowThemeProviderHandle);
}

IMPLEMENT_MODULE(FEFProjectSystemsUIModule, EFProjectSystemsUI)
