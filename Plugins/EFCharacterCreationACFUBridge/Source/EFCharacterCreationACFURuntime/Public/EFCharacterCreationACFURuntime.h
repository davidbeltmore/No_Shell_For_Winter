#pragma once

#include "Modules/ModuleManager.h"

class FEFCharacterCreationACFURuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle SetPawnCanMoveHandle;
	FDelegateHandle CancelPawnAbilitiesHandle;
};
