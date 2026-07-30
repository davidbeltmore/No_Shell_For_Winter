#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "EFCharacterCreationTypes.h"
#include "EFCharacterCustomizationSaveGame.generated.h"

UCLASS()
class EFCHARACTERCREATIONRUNTIME_API UEFCharacterCustomizationSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Presets")
	TArray<FCharacterPresetData> Presets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bHasLastConfirmedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FCharacterCustomizationState LastConfirmedState;

	int32 FindPresetIndexByName(const FString& PresetName) const;
	void AddOrUpdatePreset(const FCharacterPresetData& PresetData);
	bool TryGetPreset(const FString& PresetName, FCharacterPresetData& OutPresetData) const;
	TArray<FString> GetPresetNames() const;
};
