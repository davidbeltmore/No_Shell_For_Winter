#include "EFCharacterCreationGameplayHooks.h"

namespace EFCharacterCreationGameplayHooks
{
	FEFCharacterCreationSetPawnCanMoveHook& OnSetPawnCanMove()
	{
		static FEFCharacterCreationSetPawnCanMoveHook Hook;
		return Hook;
	}

	FEFCharacterCreationCancelPawnAbilitiesHook& OnCancelPawnAbilities()
	{
		static FEFCharacterCreationCancelPawnAbilitiesHook Hook;
		return Hook;
	}

	FEFCharacterCreationIdentityChangedHook& OnIdentityChanged()
	{
		static FEFCharacterCreationIdentityChangedHook Hook;
		return Hook;
	}

	FEFCharacterCreationWidgetReadyHook& OnWidgetReady()
	{
		static FEFCharacterCreationWidgetReadyHook Hook;
		return Hook;
	}
}
