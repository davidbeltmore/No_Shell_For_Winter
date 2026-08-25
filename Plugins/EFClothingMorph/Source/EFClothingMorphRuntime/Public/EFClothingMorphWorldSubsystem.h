#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EFClothingMorphWorldSubsystem.generated.h"

class APawn;

UCLASS()
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleActorSpawned(AActor* Actor);
	void AttachToPawn(APawn* Pawn);

	FDelegateHandle ActorSpawnedHandle;
};
