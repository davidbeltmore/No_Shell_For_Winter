#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DungeonCurseListenerInterface.generated.h"

UINTERFACE(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UDungeonCurseListenerInterface : public UInterface
{
	GENERATED_BODY()
};

class EFPROJECTSYSTEMSGAMEPLAY_API IDungeonCurseListenerInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Listener")
	void OnRoomCurseEntered(FName CurseID, const FText& DisplayName);
	virtual void OnRoomCurseEntered_Implementation(FName CurseID, const FText& DisplayName) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Listener")
	void OnRoomCurseExited(FName CurseID);
	virtual void OnRoomCurseExited_Implementation(FName CurseID) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon Curse|Listener")
	void OnRoomCurseDiscovered(FName CurseID);
	virtual void OnRoomCurseDiscovered_Implementation(FName CurseID) {}
};
