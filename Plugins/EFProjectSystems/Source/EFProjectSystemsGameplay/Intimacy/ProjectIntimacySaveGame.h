#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "ProjectIntimacySaveGame.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectIntimacySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	TMap<FString, FProjectIntimacyPartnerProfile> PartnerProfiles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Rewards")
	TSet<FName> UnlockedAutomaticTattooIds;
};
