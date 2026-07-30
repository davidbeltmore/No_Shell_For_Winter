#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectSurvivalConsumableEditorLibrary.generated.h"

class UBlueprint;
class UACFItem;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSurvivalConsumableEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Survival|Editor")
	static bool ConfigureBlueprintAsSurvivalConsumable(UBlueprint* Blueprint);

	UFUNCTION(BlueprintCallable, Category = "Survival|Editor")
	static bool ConfigureBlueprintAsWorldItemPickup(UBlueprint* Blueprint, TSubclassOf<UACFItem> ItemClass, int32 ItemCount = 1, float MeshScale = 1.f);
};
