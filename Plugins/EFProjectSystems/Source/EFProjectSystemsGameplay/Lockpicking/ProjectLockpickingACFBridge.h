#pragma once

#include "CoreMinimal.h"

class UProjectLockpickableComponent;

namespace ProjectLockpickingACFBridge
{
	EFPROJECTSYSTEMSGAMEPLAY_API bool TryConsumeACFInteractionProcessEvent(
		AActor* Owner,
		UProjectLockpickableComponent* LockpickableComponent,
		UFunction* Function,
		void* Parms,
		bool bLogGate);
}
