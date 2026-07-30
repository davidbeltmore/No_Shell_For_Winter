#include "EFLevelFlowLoadingTheme.h"

namespace EFLevelFlowLoadingThemePrivate
{
	FEFLevelFlowLoadingThemeProvider ThemeProvider;
	FDelegateHandle ThemeProviderHandle;
	FOnEFLevelFlowLoadingThemeChanged ThemeChanged;

	void BroadcastResolvedTheme()
	{
		const FEFLevelFlowLoadingTheme ResolvedTheme =
			ThemeProvider ? ThemeProvider() : FEFLevelFlowLoadingTheme();
		ThemeChanged.Broadcast(ResolvedTheme);
	}
}

namespace EFLevelFlowLoadingTheme
{
	FDelegateHandle RegisterProvider(FEFLevelFlowLoadingThemeProvider Provider)
	{
		if (!ensureMsgf(IsInGameThread(), TEXT("Loading theme providers must be registered on the game thread.")))
		{
			return FDelegateHandle();
		}

		if (!Provider)
		{
			return FDelegateHandle();
		}

		EFLevelFlowLoadingThemePrivate::ThemeProvider = MoveTemp(Provider);
		EFLevelFlowLoadingThemePrivate::ThemeProviderHandle =
			FDelegateHandle(FDelegateHandle::GenerateNewHandle);
		EFLevelFlowLoadingThemePrivate::BroadcastResolvedTheme();
		return EFLevelFlowLoadingThemePrivate::ThemeProviderHandle;
	}

	void UnregisterProvider(const FDelegateHandle ProviderHandle)
	{
		if (!ensureMsgf(IsInGameThread(), TEXT("Loading theme providers must be unregistered on the game thread.")))
		{
			return;
		}

		if (!ProviderHandle.IsValid()
			|| ProviderHandle != EFLevelFlowLoadingThemePrivate::ThemeProviderHandle)
		{
			return;
		}

		EFLevelFlowLoadingThemePrivate::ThemeProvider = nullptr;
		EFLevelFlowLoadingThemePrivate::ThemeProviderHandle.Reset();
		EFLevelFlowLoadingThemePrivate::BroadcastResolvedTheme();
	}

	void RefreshProvider(const FDelegateHandle ProviderHandle)
	{
		if (!ensureMsgf(IsInGameThread(), TEXT("Loading themes must be refreshed on the game thread.")))
		{
			return;
		}

		if (ProviderHandle.IsValid()
			&& ProviderHandle == EFLevelFlowLoadingThemePrivate::ThemeProviderHandle)
		{
			EFLevelFlowLoadingThemePrivate::BroadcastResolvedTheme();
		}
	}

	FEFLevelFlowLoadingTheme ResolveTheme()
	{
		return EFLevelFlowLoadingThemePrivate::ThemeProvider
			? EFLevelFlowLoadingThemePrivate::ThemeProvider()
			: FEFLevelFlowLoadingTheme();
	}

	FOnEFLevelFlowLoadingThemeChanged& OnThemeChanged()
	{
		return EFLevelFlowLoadingThemePrivate::ThemeChanged;
	}
}
