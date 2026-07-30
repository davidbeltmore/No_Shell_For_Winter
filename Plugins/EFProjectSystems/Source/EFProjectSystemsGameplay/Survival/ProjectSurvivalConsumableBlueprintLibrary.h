#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectSurvivalNeedsTypes.h"
#include "ProjectSurvivalConsumableBlueprintLibrary.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalConsumableBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Survival|Consumables")
	static bool ApplySurvivalConsumableProfile(APawn* Consumer, UObject* SourceAsset, const FProjectSurvivalConsumableProfile& Profile);

	UFUNCTION(BlueprintCallable, Category = "Survival|Consumables")
	static bool ApplySurvivalConsumableFromSource(APawn* Consumer, UObject* SourceAsset);
};
