#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LevelReadinessProvider.generated.h"

class UWorld;

UINTERFACE(MinimalAPI, BlueprintType)
class ULevelReadinessProvider : public UInterface
{
	GENERATED_BODY()
};

class EFPROCEDURALRUNTIME_API ILevelReadinessProvider
{
	GENERATED_BODY()

public:
	virtual bool IsLevelRuntimeReady(UWorld* World) = 0;
};
