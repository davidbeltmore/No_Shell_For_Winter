#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ProjectEnemyLevelContextProvider.generated.h"

UINTERFACE(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEnemyLevelContextProvider : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IProjectEnemyLevelContextProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Project|EnemyLevel")
	bool TryResolveEnemyWorldTier(const UObject* WorldContextObject, int32& OutWorldTier) const;

	virtual bool TryResolveEnemyWorldTier_Implementation(const UObject* WorldContextObject, int32& OutWorldTier) const
	{
		return false;
	}
};
