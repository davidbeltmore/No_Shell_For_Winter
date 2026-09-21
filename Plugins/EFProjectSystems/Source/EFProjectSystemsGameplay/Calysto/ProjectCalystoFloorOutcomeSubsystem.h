#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectCalystoFloorOutcomeSubsystem.generated.h"

class AActor;
class UEFCalystoDirectorSubsystem;
class UEFCalystoDungeonSubsystem;
class UProjectCombatAttributeComponent;
struct FEFCalystoDirectorSnapshot;

/**
 * Project-owned floor telemetry bound to the active Director authority.
 *
 * It samples only stable, bounded gameplay signals immediately before every
 * legacy production Advance request. The unversioned Director context records
 * observations without submitting adaptive outcomes until its staged gameplay
 * bridge is available. This subsystem never owns Director state.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCalystoFloorOutcomeSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

#if WITH_DEV_AUTOMATION_TESTS
	static float AutomationCombatScore(int32 InitialEnemyCount, int32 AliveEnemyCount);
	static float AutomationPaceScore(double ElapsedSeconds, const FIntVector& DungeonSize, int32 InitialEnemyCount);
#endif

private:
	friend class FProjectCalystoGameplaySnapshot;
	friend class FProjectCalystoGameplaySnapshotTest;
	/** Same-world snapshot ownership, excluded from all canonical gameplay state. */
	FGuid GameplaySnapshotRequest;
	void HandleDirectorRequestPreparing(int64 FloorNumber);
	void HandleDirectorContextReady(const FEFCalystoDirectorSnapshot& Snapshot);
	void HandleDirectorContextFailed(const FEFCalystoDirectorSnapshot& Snapshot);
	void HandleBeforeFloorAdvance(
		int64 CompletedFloor,
		const FEFCalystoResolvedFloorIntentV6& CompletedIntent);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoRealizedFloorManifestV6& Manifest);
	void HandleFloorTravelFailed();

	UFUNCTION()
	void HandleTrackedPlayerDeath(AActor* SourceActor);

	void BindTrackedPlayerDeath();
	void UnbindTrackedPlayerDeath();
	AActor* ResolveLocalPlayerPawn() const;
	FEFCalystoFloorOutcomeV6 BuildOutcome(
		const FEFCalystoResolvedFloorIntentV6& CompletedIntent) const;
	int32 CountAliveDungeonEnemies() const;
	float ResolveSurvivalScore() const;
	float ResolveResourceScore() const;

	static float SanitizeUnit(float Value, float Fallback = 0.5f);
	static float ComputeCombatScore(int32 InitialEnemyCount, int32 AliveEnemyCount);
	static float ComputePaceScore(
		double ElapsedSeconds,
		const FIntVector& DungeonSize,
		int32 InitialEnemyCount);

	TWeakObjectPtr<UEFCalystoDungeonSubsystem> DungeonSubsystem;
	TWeakObjectPtr<UEFCalystoDirectorSubsystem> BoundDirector;
	TWeakObjectPtr<UProjectCombatAttributeComponent> TrackedPlayerCombat;
	int64 TrackedRunSeed = 0;
	int64 TrackedRunEpoch = 0;
	int64 TrackedFloorNumber = 0;
	double FloorReadySeconds = -1.0;
	int32 FloorDeaths = 0;
	int32 FloorFailures = 0;
};
