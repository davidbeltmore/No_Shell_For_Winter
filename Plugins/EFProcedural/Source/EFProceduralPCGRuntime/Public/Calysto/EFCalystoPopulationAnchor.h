#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "EFCalystoPopulationAnchor.generated.h"

/**
 * Lightweight candidate emitted by Calysto's existing spawner pass.
 *
 * Its transform-only scene root has no mesh, collision, navigation influence,
 * tick, or gameplay behaviour. The project-owned PCG runtime consumes and
 * destroys every instance after navigation is ready.
 */
UCLASS(NotPlaceable, Transient)
class EFPROCEDURALPCGRUNTIME_API AEFCalystoPopulationAnchor final : public AActor
{
	GENERATED_BODY()

public:
	AEFCalystoPopulationAnchor();

	static const FName PopulationAnchorTag;
};
