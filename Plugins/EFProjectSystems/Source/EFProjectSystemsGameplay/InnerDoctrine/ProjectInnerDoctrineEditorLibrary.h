#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectInnerDoctrineEditorLibrary.generated.h"

class UBlueprint;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Editor")
	static bool ConfigureBlueprintAsDxpAltar(UBlueprint* Blueprint);
};
