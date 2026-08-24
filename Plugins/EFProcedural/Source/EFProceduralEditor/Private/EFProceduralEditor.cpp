#include "EFProceduralEditor.h"

#include "Calysto/EFCalystoCategoryProfileV4Customization.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Calysto/EFCalystoTierMixV4Customization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void FEFProceduralEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomPropertyTypeLayout(
		FEFCalystoTierMixV4::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FEFCalystoTierMixV4Customization::MakeInstance));
	PropertyEditor.RegisterCustomPropertyTypeLayout(
		FEFCalystoCategoryProfileV4::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FEFCalystoCategoryProfileV4Customization::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();
}

void FEFProceduralEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* PropertyEditor =
		FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomPropertyTypeLayout(
			FEFCalystoTierMixV4::StaticStruct()->GetFName());
		PropertyEditor->UnregisterCustomPropertyTypeLayout(
			FEFCalystoCategoryProfileV4::StaticStruct()->GetFName());
		PropertyEditor->NotifyCustomizationModuleChanged();
	}
}

IMPLEMENT_MODULE(FEFProceduralEditorModule, EFProceduralEditor)
