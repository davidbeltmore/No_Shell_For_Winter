#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectCalystoPatrolSubsystem.generated.h"

class APawn;
class UProjectCalystoPatrolSettings;

struct FProjectCalystoPatrolRuntimeState
{
	double NextAttemptSeconds = 0.0;
	double LastPatrolProgressSeconds = 0.0;
	double LastMoveRequestSeconds = 0.0;
	FVector LastPatrolProgressLocation = FVector::ZeroVector;
	FVector LastMoveDestination = FVector::ZeroVector;
	bool bHasPatrolProgressObservation = false;
	bool bHasLastMoveDestination = false;
	bool bNavigationFailureLogged = false;
	bool bDestinationFailureLogged = false;
};

/**
 * Enables the public ACF random-patrol contract for transient Calysto V6 population enemies.
 * It deliberately leaves ACFU and the shared enemy Blueprint CDOs untouched.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCalystoPatrolSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	bool IsDungeonWorld(const UProjectCalystoPatrolSettings& Settings) const;
	bool ShouldManagePawn(const APawn* Pawn) const;
	void RefreshPatrols(const UProjectCalystoPatrolSettings& Settings);
	void TryStartOrResumePatrol(
		APawn* Pawn,
		FProjectCalystoPatrolRuntimeState& RuntimeState,
		const UProjectCalystoPatrolSettings& Settings);
	void PruneRuntimeState();

	float ScanAccumulatorSeconds = 0.0f;
	bool bInvalidStateTagsLogged = false;
	TMap<TWeakObjectPtr<APawn>, FProjectCalystoPatrolRuntimeState> RuntimeStates;
};
