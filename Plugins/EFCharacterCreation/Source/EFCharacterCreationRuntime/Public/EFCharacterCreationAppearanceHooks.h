#pragma once

#include "CoreMinimal.h"

class AActor;

namespace EFCharacterCreationGameplayHooks
{
	/**
	 * Project-owned, dependency-neutral signal that the visible skeletal-mesh set
	 * of a customized actor may have changed. Consumers rediscover the components
	 * they care about; Character Creation does not depend on those consumers.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAppearanceMeshStateChanged, AActor* /* Actor */);

	EFCHARACTERCREATIONRUNTIME_API FOnAppearanceMeshStateChanged& GetOnAppearanceMeshStateChanged();
	/** Release component-local consumers before replacing body meshes in place. */
	EFCHARACTERCREATIONRUNTIME_API FOnAppearanceMeshStateChanged& GetOnBodyMeshesWillChange();
	EFCHARACTERCREATIONRUNTIME_API void NotifyAppearanceMeshStateChanged(AActor* Actor);
}
