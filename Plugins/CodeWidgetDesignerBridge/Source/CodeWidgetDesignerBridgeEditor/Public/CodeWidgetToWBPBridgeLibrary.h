#pragma once

#include "CodeWidgetDesignerTreeProvider.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CodeWidgetToWBPBridgeLibrary.generated.h"

class UUserWidget;
class UWidget;
class UWidgetBlueprint;

UCLASS()
class CODEWIDGETDESIGNERBRIDGEEDITOR_API UCodeWidgetToWBPBridgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Code Widget Designer Bridge")
	static UWidgetBlueprint* CreateOrUpdateWidgetBlueprintFromCode(
		TSubclassOf<UUserWidget> NativeWidgetClass,
		const FString& TargetAssetPath,
		bool bReplaceExistingDesignerRoot,
		bool bSaveAsset,
		FString& OutReport);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Code Widget Designer Bridge")
	static UWidgetBlueprint* CreateOrUpdateWidgetBlueprintFromCodeWithNamedWidgetClassOverrides(
		TSubclassOf<UUserWidget> NativeWidgetClass,
		const FString& TargetAssetPath,
		bool bReplaceExistingDesignerRoot,
		bool bSaveAsset,
		const TArray<FName>& WidgetNames,
		const TArray<TSubclassOf<UWidget>>& WidgetClasses,
		FString& OutReport);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Code Widget Designer Bridge")
	static bool CreateOrUpdateWidgetBlueprintsFromManifest(
		TSubclassOf<UUserWidget> NativeWidgetClass,
		const FString& TargetAssetPath,
		ECodeWidgetDesignerGenerationMode GenerationMode,
		bool bSaveAssets,
		FString& OutReport);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Code Widget Designer Bridge")
	static bool ValidateWidgetBlueprintConversion(
		TSubclassOf<UUserWidget> NativeWidgetClass,
		const FString& TargetAssetPath,
		FCodeWidgetDesignerValidationReport& OutValidationReport,
		FString& OutReport);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Code Widget Designer Bridge")
	static bool PreflightWidgetBlueprintConversion(
		TSubclassOf<UUserWidget> NativeWidgetClass,
		const FString& TargetAssetPath,
		ECodeWidgetDesignerGenerationMode GenerationMode,
		bool bStrict,
		FCodeWidgetDesignerValidationReport& OutValidationReport,
		FString& OutReport);
};
