#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFProceduralSettings.generated.h"

class AActor;
class AController;
class UEFProceduralProjectPreset;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Procedural"))
class EFPROCEDURALRUNTIME_API UEFProceduralSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFProceduralSettings();

	static const UEFProceduralSettings* Get();

	virtual FName GetCategoryName() const override;

	const UEFProceduralProjectPreset* LoadProjectPreset() const;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	TArray<FString> ManagedMapNames;

	UPROPERTY(EditAnywhere, Config, Category = "General")
	TSoftObjectPtr<UEFProceduralProjectPreset> ProjectPreset;

	UPROPERTY(EditAnywhere, Config, Category = "Bootstrap")
	int32 MaxDungeonBootstrapAttempts = 40;

	UPROPERTY(EditAnywhere, Config, Category = "Bootstrap")
	float DungeonBootstrapRetrySeconds = 0.1f;

	UPROPERTY(EditAnywhere, Config, Category = "Spawn")
	int32 MaxControllerFixAttempts = 80;

	UPROPERTY(EditAnywhere, Config, Category = "Spawn")
	float ControllerFixRetrySeconds = 0.1f;

	UPROPERTY(EditAnywhere, Config, Category = "Spawn")
	int32 MaxSpawnSanitizationAttempts = 12;

	UPROPERTY(EditAnywhere, Config, Category = "Spawn")
	float SpawnSanitizationRetrySeconds = 0.15f;

	UPROPERTY(EditAnywhere, Config, Category = "PCG")
	int32 DefaultPCGSeed = 42;

	UPROPERTY(EditAnywhere, Config, Category = "PCG")
	TArray<FName> DungeonRandomizeFunctionNames;

	UPROPERTY(EditAnywhere, Config, Category = "PCG")
	TArray<FName> DungeonRefreshFunctionNames;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	float NavigationBoundsXYPadding = 1200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	float NavigationBoundsZPadding = 800.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	float MinimumNavigationBoundsExtentXY = 2500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	float MinimumNavigationBoundsExtentZ = 1200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	float MinimumNavigationInvokerRadius = 3500.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Navigation")
	FVector RuntimeNavMeshVolumeScale = FVector(200.0f, 200.0f, 2.0f);

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	FName GeneratedActorTag = TEXT("PCG Generated Actor");

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	FName RuntimeNavMeshVolumeName = TEXT("EFRuntimeNavMeshBounds");

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	TSoftClassPtr<AActor> DungeonActorClass;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	TSoftClassPtr<AActor> StartPointActorClass;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	TSoftClassPtr<AController> MeleeAIControllerClass;

	UPROPERTY(EditAnywhere, Config, Category = "Runtime")
	TSoftClassPtr<AController> RangedAIControllerClass;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> EnemyClassPathHints;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> EnemyClassNameHints;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> MeleeEnemyClassPathHints;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> MeleeEnemyClassNameHints;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> RangedEnemyClassPathHints;

	UPROPERTY(EditAnywhere, Config, Category = "Detection")
	TArray<FString> RangedEnemyClassNameHints;

	TArray<FString> GetManagedMapNamesResolved() const;
	TSoftClassPtr<AActor> GetDungeonActorClassResolved() const;
	TSoftClassPtr<AActor> GetStartPointActorClassResolved() const;
	TSoftClassPtr<AController> GetMeleeAIControllerClassResolved() const;
	TSoftClassPtr<AController> GetRangedAIControllerClassResolved() const;
	TArray<FString> GetEnemyClassPathHintsResolved() const;
	TArray<FString> GetEnemyClassNameHintsResolved() const;
	TArray<FString> GetMeleeEnemyClassPathHintsResolved() const;
	TArray<FString> GetMeleeEnemyClassNameHintsResolved() const;
	TArray<FString> GetRangedEnemyClassPathHintsResolved() const;
	TArray<FString> GetRangedEnemyClassNameHintsResolved() const;
};
