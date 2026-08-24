#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "EFCalystoSpecialEventProbe.generated.h"

/**
 * Development acceptance actor used to prove the V4 Special Event hard cap.
 *
 * The authored V4 policy intentionally keeps Special Events empty. The
 * unattended acceptance fixture references this native class only from a
 * transient policy clone. It has no visual, collision, navigation, network,
 * damage, tick, or gameplay behavior.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class EFPROCEDURALPCGRUNTIME_API AEFCalystoSpecialEventProbe final : public AActor
{
	GENERATED_BODY()

public:
	AEFCalystoSpecialEventProbe();

	static const FName SpecialEventProbeTag;
};
