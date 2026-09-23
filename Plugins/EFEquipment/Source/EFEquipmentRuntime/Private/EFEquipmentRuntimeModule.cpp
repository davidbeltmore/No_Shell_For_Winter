#include "Modules/ModuleManager.h"
void StartEFEquipmentPackagedQA();
void StopEFEquipmentPackagedQA();
class FEFEquipmentRuntimeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override { StartEFEquipmentPackagedQA(); }
    virtual void ShutdownModule() override { StopEFEquipmentPackagedQA(); }
};
IMPLEMENT_MODULE(FEFEquipmentRuntimeModule, EFEquipmentRuntime)
