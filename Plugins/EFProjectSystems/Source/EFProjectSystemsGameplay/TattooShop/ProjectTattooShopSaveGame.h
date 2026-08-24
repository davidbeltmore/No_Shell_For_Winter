#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TattooShop/ProjectTattooShopStateSubsystem.h"
#include "ProjectTattooShopSaveGame.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 Version = UProjectTattooShopStateSubsystem::CurrentSaveVersion;

	UPROPERTY(SaveGame)
	TArray<FProjectTattooRecord> Records;
};
