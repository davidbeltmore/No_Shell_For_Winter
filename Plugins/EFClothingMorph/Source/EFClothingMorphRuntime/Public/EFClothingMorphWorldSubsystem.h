#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EFClothingMorphWorldSubsystem.generated.h"

class APawn;
class AController;

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
	void ObservePawn(APawn* Pawn);
	void AttachToPawn(APawn* Pawn);
	void ScanForEligiblePawns();

	UFUNCTION()
	void HandlePawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	FDelegateHandle ActorSpawnedHandle;
	FTimerHandle EligiblePawnScanTimer;
	TSet<TWeakObjectPtr<APawn>> ControllerObservedPawns;
};
