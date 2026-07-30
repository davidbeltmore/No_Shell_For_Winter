#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FEFProceduralEditorCoordinator;
class UWorld;

class FEFProjectSystemsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandlePreBeginPIE(bool bIsSimulating);
	void HandleEndPIE(bool bIsSimulating);

private:
	FDelegateHandle PreBeginPIEHandle;
	FDelegateHandle EndPIEHandle;
	TMap<FString, bool> CachedPackageDirtyStates;
	TUniquePtr<FEFProceduralEditorCoordinator> ProceduralDungeonCoordinator;
};
