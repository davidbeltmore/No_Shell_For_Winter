#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFCalystoV6EditorValidationLibrary.generated.h"

/**
 * Narrow editor-only bridge for inspecting the typed Asset Registry payload
 * that UE 5.8 stores separately from ordinary string tags.
 */
UCLASS()
class EFPROCEDURALEDITOR_API UEFCalystoV6EditorValidationLibrary final
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Validation")
	static TArray<FString> GetSerializedAssetBundleNames(FName PackageName);

	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Validation")
	static TArray<FString> GetSerializedAssetBundleAssetPaths(
		FName PackageName, FName BundleName);
};
