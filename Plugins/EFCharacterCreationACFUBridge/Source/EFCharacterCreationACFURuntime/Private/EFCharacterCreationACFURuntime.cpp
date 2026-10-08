#include "EFCharacterCreationACFURuntime.h"

#include "EFCharacterCreationACFUBridge.h"
#include "EFCharacterCreationGameplayHooks.h"
#include "EFCharacterCreationAppearanceHooks.h"
#include "Modules/ModuleManager.h"

void FEFCharacterCreationACFURuntimeModule::StartupModule()
{
	SetPawnCanMoveHandle = EFCharacterCreationGameplayHooks::OnSetPawnCanMove().AddStatic(&FEFCharacterCreationACFUBridge::SetPawnCanMove);
	CancelPawnAbilitiesHandle = EFCharacterCreationGameplayHooks::OnCancelPawnAbilities().AddStatic(&FEFCharacterCreationACFUBridge::CancelPawnAbilities);
	AppearanceChangedHandle = EFCharacterCreationGameplayHooks::GetOnAppearanceMeshStateChanged().AddStatic(&FEFCharacterCreationACFUBridge::RestoreAnimationAfterBodyChange);
}

void FEFCharacterCreationACFURuntimeModule::ShutdownModule()
{
	EFCharacterCreationGameplayHooks::GetOnAppearanceMeshStateChanged().Remove(AppearanceChangedHandle);
	if (SetPawnCanMoveHandle.IsValid())
	{
		EFCharacterCreationGameplayHooks::OnSetPawnCanMove().Remove(SetPawnCanMoveHandle);
		SetPawnCanMoveHandle.Reset();
	}

	if (CancelPawnAbilitiesHandle.IsValid())
	{
		EFCharacterCreationGameplayHooks::OnCancelPawnAbilities().Remove(CancelPawnAbilitiesHandle);
		CancelPawnAbilitiesHandle.Reset();
	}
}

IMPLEMENT_MODULE(FEFCharacterCreationACFURuntimeModule, EFCharacterCreationACFURuntime)
