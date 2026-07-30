#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpawnPostProcessor.generated.h"

class AActor;

UINTERFACE(MinimalAPI, BlueprintType)
class USpawnPostProcessor : public UInterface
{
	GENERATED_BODY()
};

class EFPROCEDURALRUNTIME_API ISpawnPostProcessor
{
	GENERATED_BODY()

public:
	virtual bool ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const = 0;
	virtual void PostProcessSpawnedActor(AActor* SpawnedActor) = 0;
};
