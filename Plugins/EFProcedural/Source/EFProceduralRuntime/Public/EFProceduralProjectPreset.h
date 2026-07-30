#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "EFProceduralProjectPreset.generated.h"

class AActor;
class AController;

UCLASS(BlueprintType)
class EFPROCEDURALRUNTIME_API UEFProceduralProjectPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maps")
	TArray<FString> ManagedMapNames;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSoftClassPtr<AActor> DungeonActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSoftClassPtr<AActor> StartPointActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSoftClassPtr<AController> MeleeAIControllerClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Runtime")
	TSoftClassPtr<AController> RangedAIControllerClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> EnemyClassPathHints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> EnemyClassNameHints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> MeleeEnemyClassPathHints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> MeleeEnemyClassNameHints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> RangedEnemyClassPathHints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	TArray<FString> RangedEnemyClassNameHints;
};
