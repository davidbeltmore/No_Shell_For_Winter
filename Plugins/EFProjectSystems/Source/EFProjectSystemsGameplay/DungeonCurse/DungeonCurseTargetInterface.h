#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DungeonCurseTargetInterface.generated.h"

UINTERFACE(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UDungeonCurseTargetInterface : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IDungeonCurseTargetInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Target")
	void ApplyInnerStateFlatModifier(FName StateName, float Value);
	virtual void ApplyInnerStateFlatModifier_Implementation(FName StateName, float Value) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Target")
	void ApplyInnerStateDrainMultiplier(FName StateName, float Multiplier, bool bEnabled);
	virtual void ApplyInnerStateDrainMultiplier_Implementation(FName StateName, float Multiplier, bool bEnabled) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Target")
	void ApplyMovementCurse(FName CurseName, bool bEnabled);
	virtual void ApplyMovementCurse_Implementation(FName CurseName, bool bEnabled) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Target")
	void OnRoomCurseApplied(FName CurseID);
	virtual void OnRoomCurseApplied_Implementation(FName CurseID) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Target")
	void OnRoomCurseRemoved(FName CurseID);
	virtual void OnRoomCurseRemoved_Implementation(FName CurseID) {}
};
