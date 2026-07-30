#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ProjectCharacterBackgroundSaveGame.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	bool bHasConfirmedProfile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName SelectedBackstoryID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName SelectedProfessionID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	int32 ProfileRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FString SavedAtUtc;
};
