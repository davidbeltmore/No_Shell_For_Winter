#include "EFProjectSystemsEditor.h"

#include "EFProceduralEditorCoordinator.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ProjectChestDetails.h"
#include "UObject/Package.h"

void FEFProjectSystemsEditorModule::StartupModule()
{
    auto& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    Properties.RegisterCustomClassLayout(TEXT("BP_EquipmentDeposit_C"),
        FOnGetDetailCustomizationInstance::CreateStatic(&FProjectChestDetails::MakeInstance));
	ProceduralDungeonCoordinator = MakeUnique<FEFProceduralEditorCoordinator>();
	PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FEFProjectSystemsEditorModule::HandlePreBeginPIE);
	EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FEFProjectSystemsEditorModule::HandleEndPIE);
}

void FEFProjectSystemsEditorModule::ShutdownModule()
{
    if (auto* Properties = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
        Properties->UnregisterCustomClassLayout(TEXT("BP_EquipmentDeposit_C"));
	if (PreBeginPIEHandle.IsValid())
	{
		FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
	}

	if (EndPIEHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
	}

	CachedPackageDirtyStates.Reset();
	ProceduralDungeonCoordinator.Reset();
}

void FEFProjectSystemsEditorModule::HandlePreBeginPIE(bool bIsSimulating)
{
	if (!GEditor || !ProceduralDungeonCoordinator)
	{
		return;
	}

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (!ProceduralDungeonCoordinator->IsManagedEditorWorld(World))
		{
			return;
		}

		if (UPackage* WorldPackage = World->GetOutermost())
		{
			CachedPackageDirtyStates.Add(WorldPackage->GetName(), WorldPackage->IsDirty());
		}

		ProceduralDungeonCoordinator->PrepareEditorDungeon(World);
	}
}

void FEFProjectSystemsEditorModule::HandleEndPIE(bool bIsSimulating)
{
	if (!GEditor || !ProceduralDungeonCoordinator)
	{
		return;
	}

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		if (!ProceduralDungeonCoordinator->IsManagedEditorWorld(World))
		{
			return;
		}

		ProceduralDungeonCoordinator->CleanupEditorDungeon(World);

		if (UPackage* WorldPackage = World->GetOutermost())
		{
			if (const bool* bWasDirty = CachedPackageDirtyStates.Find(WorldPackage->GetName()))
			{
				WorldPackage->SetDirtyFlag(*bWasDirty);
				CachedPackageDirtyStates.Remove(WorldPackage->GetName());
			}
		}
	}
}

IMPLEMENT_MODULE(FEFProjectSystemsEditorModule, EFProjectSystemsEditor)
