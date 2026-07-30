#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Interfaces/LevelReadinessProvider.h"
#include "Interfaces/PlayerStartResolver.h"
#include "Interfaces/SpawnPostProcessor.h"

#include "EFProceduralRuntimeSubsystem.generated.h"

class AActor;
class UObject;
class UWorld;

UCLASS()
class EFPROCEDURALRUNTIME_API UEFProceduralRuntimeSubsystem
	: public UGameInstanceSubsystem
	, public ILevelReadinessProvider
	, public IPlayerStartResolver
	, public ISpawnPostProcessor
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterProvider(UObject* Provider);
	void UnregisterProvider(UObject* Provider);

	bool IsDungeonRuntimeReady(UWorld* World);

	virtual bool IsLevelRuntimeReady(UWorld* World) override;
	virtual bool ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const override;
	virtual bool ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const override;
	virtual void PostProcessSpawnedActor(AActor* SpawnedActor) override;

private:
	static void CompactProviders(TArray<TWeakObjectPtr<UObject>>& Providers);

	TArray<TWeakObjectPtr<UObject>> LevelReadinessProviders;
	TArray<TWeakObjectPtr<UObject>> PlayerStartResolvers;
	TArray<TWeakObjectPtr<UObject>> SpawnPostProcessors;
};
