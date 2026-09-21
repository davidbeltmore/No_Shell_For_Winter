#include "EFProceduralEditor.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6Details.h"
#include "Calysto/EFCalystoDirectorToolset.h"
#include "Calysto/EFCalystoTraitBindingDetails.h"
#include "Calysto/EFCalystoDirectorTypes.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

namespace
{
	FDelegateHandle CalystoToolsetRegistration;
	void RegisterCalystoToolset()
	{
		if (!IsRunningCommandlet()) UToolsetRegistry::RegisterToolsetClass(UEFCalystoDirectorToolset::StaticClass());
	}
}

void FEFProceduralEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomClassLayout(
		UEFCalystoDungeonDirectorPolicyV6Asset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FEFCalystoDungeonDirectorPolicyV6Details::MakeInstance));
	PropertyEditor.RegisterCustomPropertyTypeLayout(FEFCalystoTraitBinding::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FEFCalystoTraitBindingDetails::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();
	if (!IsRunningCommandlet())
	{
		if (GEditor && UToolsetRegistry::IsAvailable()) RegisterCalystoToolset();
		else CalystoToolsetRegistration = FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterCalystoToolset);
	}
}

void FEFProceduralEditorModule::ShutdownModule()
{
	FCoreDelegates::GetOnPostEngineInit().Remove(CalystoToolsetRegistration);
	UEFCalystoDirectorToolset::CancelPendingShutdown();
	UToolsetRegistry::UnregisterToolsetClass(UEFCalystoDirectorToolset::StaticClass());
	if (FPropertyEditorModule* PropertyEditor =
		FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomClassLayout(
			UEFCalystoDungeonDirectorPolicyV6Asset::StaticClass()->GetFName());
		PropertyEditor->UnregisterCustomPropertyTypeLayout(FEFCalystoTraitBinding::StaticStruct()->GetFName());
		PropertyEditor->NotifyCustomizationModuleChanged();
	}
}

IMPLEMENT_MODULE(FEFProceduralEditorModule, EFProceduralEditor)
