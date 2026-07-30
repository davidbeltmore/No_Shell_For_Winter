#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ProjectInnerDoctrineSaveGame.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 MetaBankDxp = 0;

	UPROPERTY()
	bool bDoctrineMasteryMode = false;
};
