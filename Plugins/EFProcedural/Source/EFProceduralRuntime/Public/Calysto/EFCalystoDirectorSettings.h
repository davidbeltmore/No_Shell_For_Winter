#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFCalystoDirectorSettings.generated.h"

class UEFCalystoDungeonDirectorAsset;
class UWorld;

/** Integration paths only. All authored generation rules belong to the master Director asset. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Calysto Integration"))
class EFPROCEDURALRUNTIME_API UEFCalystoDirectorSettings final : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UEFCalystoDirectorSettings();
	static bool IsEnabled();
	UPROPERTY(Config) bool bEnabled = false;
	UPROPERTY(EditAnywhere, Config, Category = "Integration") TSoftObjectPtr<UEFCalystoDungeonDirectorAsset> Director;
	UPROPERTY(EditAnywhere, Config, Category = "Integration") TSoftObjectPtr<UWorld> DungeonMap;
	UPROPERTY(EditAnywhere, Config, Category = "Integration") TSoftObjectPtr<UWorld> HubMap;
};
