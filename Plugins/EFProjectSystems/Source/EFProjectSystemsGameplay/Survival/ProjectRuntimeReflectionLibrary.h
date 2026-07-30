#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectRuntimeReflectionLibrary.generated.h"

class UProjectEmoteSubsystem;
class UProjectEmoteComponent;
class UProjectActivityFeedSubsystem;
class UProjectGameplayDebugSubsystem;
class UProjectEnemyTargetInfoComponent;
class UProjectIntimacySubsystem;
class UProjectLocomotionOverrideComponent;
class UProjectInnerDoctrineComponent;
class UProjectInnerDoctrineWidget;
class AActor;
class UProjectInnerDoctrineMenuSubsystem;
class UProjectTargetingFixComponent;
class UEFProceduralRuntimeSubsystem;
class UEFCharacterCreationSubsystem;
class UProjectTattooShopInputSubsystem;
class UProjectDefaultTattooSkinnedDecalSubsystem;
class UProjectSurvivalNeedsSubsystem;
class AController;
#if WITH_EDITOR
class UProjectCharacterBackgroundSubsystem;
#endif

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRuntimeReflectionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeNoArgFunction(UObject* Target, FName FunctionName);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeInt32Function(UObject* Target, FName FunctionName, int32 Int32Value);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeObjectArgFunction(UObject* Target, FName FunctionName, UObject* ObjectValue);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeBoolReturnFunction(UObject* Target, FName FunctionName, bool& OutReturnValue);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeInt32ReturnFunction(UObject* Target, FName FunctionName, int32& OutReturnValue);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeObjectReturnFunction(UObject* Target, FName FunctionName, TSubclassOf<UObject> ExpectedClass, UObject*& OutReturnValue);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection")
	static bool InvokeObjectArgObjectReturnFunction(
		UObject* Target,
		FName FunctionName,
		UObject* ObjectValue,
		TSubclassOf<UObject> ExpectedClass,
		UObject*& OutReturnValue);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection")
	static TArray<FName> GetAvailableFunctionNames(UObject* Target, FString NameContains = "");

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectEmoteSubsystem* GetProjectEmoteSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectGameplayDebugSubsystem* GetProjectGameplayDebugSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectActivityFeedSubsystem* GetProjectActivityFeedSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectIntimacySubsystem* GetProjectIntimacySubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectInnerDoctrineMenuSubsystem* GetProjectInnerDoctrineMenuSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectSurvivalNeedsSubsystem* GetProjectSurvivalNeedsSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UEFCharacterCreationSubsystem* GetEFCharacterCreationSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectTattooShopInputSubsystem* GetProjectTattooShopInputSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectDefaultTattooSkinnedDecalSubsystem* GetProjectDefaultTattooSkinnedDecalSubsystem(UObject* WorldContextObject);

#if WITH_EDITOR
	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectCharacterBackgroundSubsystem* GetProjectCharacterBackgroundSubsystem(UObject* WorldContextObject);
#endif

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UEFProceduralRuntimeSubsystem* GetProceduralRuntimeSubsystem(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectTargetingFixComponent* GetProjectTargetingFixComponent(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectLocomotionOverrideComponent* GetProjectLocomotionOverrideComponent(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectInnerDoctrineComponent* GetProjectInnerDoctrineComponent(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectInnerDoctrineWidget* GetProjectInnerDoctrineWidget(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static UProjectEmoteComponent* GetProjectEmoteComponent(UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection")
	static UProjectEnemyTargetInfoComponent* GetProjectEnemyTargetInfoComponent(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection|ACF")
	static AActor* GetACFAIControllerTarget(AController* Controller);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection|ACF")
	static AActor* GetACFAIControllerBlackboardTarget(AController* Controller);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection|ACF")
	static UObject* GetACFAIControllerGroup(AController* Controller);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection|ACF")
	static bool IsACFAIControllerThreateningActor(AController* Controller, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection|ACF")
	static bool SetACFAIControllerTarget(AController* Controller, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection|ACF")
	static bool ClearACFAIControllerAwarenessOfActor(AController* Controller, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnActorByClassPath(UObject* WorldContextObject, const FString& ActorClassPath, FVector Location, FRotator Rotation);

	UFUNCTION(BlueprintPure, Category = "Project|Reflection", meta = (WorldContext = "WorldContextObject"))
	static bool IsProceduralDungeonRuntimeReady(UObject* WorldContextObject);
};
