#pragma once

#include "CoreMinimal.h"

class UEFCalystoDungeonDirectorAsset;

#if !UE_BUILD_SHIPPING
/** Temporary Stage 2 fixture, selected only by the explicit CalystoDirectorNativeParity test mode.
 * It is never an authored asset, a failed-load fallback, or evidence of full Director acceptance.
 * Remove this fixture and its explicit test mode before final release acceptance. */
namespace EFCalystoDirectorTestFixture
{
	EFPROCEDURALRUNTIME_API FGuid StyleId();
	EFPROCEDURALRUNTIME_API UEFCalystoDungeonDirectorAsset* MakeTransientFixture(bool bEnableNativeWallLights = false,
		bool bEnableReservedArchitecture = false);
}
#endif
