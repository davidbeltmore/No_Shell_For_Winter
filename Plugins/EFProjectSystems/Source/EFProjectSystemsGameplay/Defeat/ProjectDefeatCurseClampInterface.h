#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ProjectDefeatCurseClampInterface.generated.h"

UINTERFACE(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefeatCurseClampReceiver : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IProjectDefeatCurseClampReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Defeat|Curse")
	void ClampCurseAfterDefeat(float MaximumCurse);

	virtual void ClampCurseAfterDefeat_Implementation(float MaximumCurse)
	{
	}
};
