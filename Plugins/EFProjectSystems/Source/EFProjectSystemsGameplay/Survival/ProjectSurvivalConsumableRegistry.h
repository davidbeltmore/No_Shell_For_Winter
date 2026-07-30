#pragma once

#include "Engine/DataAsset.h"
#include "ProjectSurvivalNeedsTypes.h"
#include "ProjectSurvivalConsumableRegistry.generated.h"

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalConsumableRegistryEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	FName RegistryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	FProjectSurvivalConsumableProfile Profile;
};

UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalConsumableRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Survival|Consumables")
	bool FindProfileByRegistryId(FName RegistryId, FProjectSurvivalConsumableProfile& OutProfile) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Consumable")
	TArray<FProjectSurvivalConsumableRegistryEntry> Entries;
};
