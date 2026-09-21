#include "EFCharacterCreationAppearanceHooks.h"

#include "GameFramework/Actor.h"

namespace EFCharacterCreationGameplayHooks
{
	FOnAppearanceMeshStateChanged& GetOnBodyMeshesWillChange()
	{
		static FOnAppearanceMeshStateChanged Hook;
		return Hook;
	}

	FOnAppearanceMeshStateChanged& GetOnAppearanceMeshStateChanged()
	{
		static FOnAppearanceMeshStateChanged Hook;
		return Hook;
	}

	void NotifyAppearanceMeshStateChanged(AActor* Actor)
	{
		if (IsValid(Actor))
		{
			GetOnAppearanceMeshStateChanged().Broadcast(Actor);
		}
	}
}
