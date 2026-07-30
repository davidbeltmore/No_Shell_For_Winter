#pragma once

#include "Modules/ModuleInterface.h"

class FEFProceduralEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
