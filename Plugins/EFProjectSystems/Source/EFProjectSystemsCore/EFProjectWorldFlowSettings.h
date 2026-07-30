#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFProjectWorldFlowSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Project World Flow"))
class EFPROJECTSYSTEMSCORE_API UEFProjectWorldFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFProjectWorldFlowSettings();

	static const UEFProjectWorldFlowSettings* Get();

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Maps")
	FName ManagedDungeonMapName;

	UPROPERTY(EditAnywhere, Config, Category = "Maps")
	FSoftObjectPath DungeonGenerationMap;

	UPROPERTY(EditAnywhere, Config, Category = "Maps")
	FSoftObjectPath DoorToLevelBlueprint;
};
