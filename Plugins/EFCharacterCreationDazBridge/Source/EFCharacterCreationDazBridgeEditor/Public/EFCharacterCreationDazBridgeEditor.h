#pragma once

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FEFCharacterCreationDazBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandleAuditDazDQCorrectivesCommand();
	void HandleRepairDazDQCorrectivesCommand();
	void HandleSyncDazDualQuatMorphCommand();

private:
	IConsoleObject* AuditDazDQCorrectivesCommand = nullptr;
	IConsoleObject* RepairDazDQCorrectivesCommand = nullptr;
	IConsoleObject* SyncDazDualQuatMorphCommand = nullptr;
};
