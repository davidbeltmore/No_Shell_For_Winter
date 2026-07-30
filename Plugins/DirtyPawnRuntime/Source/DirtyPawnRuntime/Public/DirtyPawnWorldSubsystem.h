#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "DirtyPawnWorldSubsystem.generated.h"

class UDirtyPawnComponent;
class USkeletalMeshComponent;

UCLASS()
class DIRTYPAWNRUNTIME_API UDirtyPawnWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void HandleActorSpawned(AActor* Actor);
	void AttachOrPrewarmActor(AActor* Actor);
	bool ShouldConsiderActor(AActor* Actor) const;
	bool HasDazLikeMesh(AActor* Actor) const;
	bool IsUsableDirtyPawnComponent(const UDirtyPawnComponent* Component) const;

	FDelegateHandle ActorSpawnedHandle;
};
