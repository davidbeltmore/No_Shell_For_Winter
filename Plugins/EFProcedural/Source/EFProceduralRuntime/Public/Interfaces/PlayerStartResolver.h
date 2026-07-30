#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlayerStartResolver.generated.h"

class UWorld;

UINTERFACE(MinimalAPI, BlueprintType)
class UPlayerStartResolver : public UInterface
{
	GENERATED_BODY()
};

class EFPROCEDURALRUNTIME_API IPlayerStartResolver
{
	GENERATED_BODY()

public:
	virtual bool ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const = 0;
};
