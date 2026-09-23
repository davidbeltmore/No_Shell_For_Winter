#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectLockpickingEditorLibrary.generated.h"

class UBlueprint;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickingEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Storage|Editor") static FString AddProceduralChestAuthoring();
    UFUNCTION(BlueprintCallable, Category="Storage|Editor") static bool ShowProceduralChestDetails(UBlueprint* Blueprint);
    UFUNCTION(BlueprintCallable, Category="Storage|Editor") static FString BuildChestFamily();
    UFUNCTION(BlueprintCallable, Category="Storage|Editor") static FString RemapChestCatalogs();
	UFUNCTION(BlueprintCallable, Category = "Lockpicking|Editor")
	static bool ConfigureBlueprintForComponentLockpicking(UBlueprint* Blueprint);
};
