#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DungeonCurseEnemyInterface.generated.h"

UINTERFACE(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UDungeonCurseEnemyInterface : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IDungeonCurseEnemyInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Enemy")
	void ApplyEnemyLevelOverride(int32 DesiredLevel);
	virtual void ApplyEnemyLevelOverride_Implementation(int32 DesiredLevel) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Enemy")
	void SetEnemyMaxLevelForFloor(bool bEnabled);
	virtual void SetEnemyMaxLevelForFloor_Implementation(bool bEnabled) {}
};
