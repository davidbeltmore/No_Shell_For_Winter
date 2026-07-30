#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FEFProjectSystemsUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandleProjectThemeChanged(const struct FProjectHUDThemeColors& Theme);

	FDelegateHandle LevelFlowThemeProviderHandle;
	FDelegateHandle ProjectThemeChangedHandle;
};
