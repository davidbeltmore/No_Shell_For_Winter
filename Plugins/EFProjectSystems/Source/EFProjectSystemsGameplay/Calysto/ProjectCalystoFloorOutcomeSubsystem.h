#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectCalystoFloorOutcomeSubsystem.generated.h"

class AActor;
class UEFCalystoDungeonSubsystem;
class UProjectCombatAttributeComponent;

/**
 * Project-owned telemetry bridge for Dungeon Director V4.
 *
 * It samples only stable, bounded gameplay signals immediately before every
 * production Advance request. Missing signals remain neutral (0.5); this
 * subsystem never blocks travel and never owns Director state.
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
	void HandleBeforeFloorAdvance(
		int64 CompletedFloor,
		const FEFCalystoResolvedFloorIntentV4& CompletedIntent);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest);
	void HandleFloorTravelFailed();

	UFUNCTION()
	void HandleTrackedPlayerDeath(AActor* SourceActor);

	void BindTrackedPlayerDeath();
	void UnbindTrackedPlayerDeath();
	AActor* ResolveLocalPlayerPawn() const;
	FEFCalystoFloorOutcomeV4 BuildOutcome(
		const FEFCalystoResolvedFloorIntentV4& CompletedIntent) const;
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
	TWeakObjectPtr<UProjectCombatAttributeComponent> TrackedPlayerCombat;
	int64 TrackedRunSeed = 0;
	int64 TrackedFloorNumber = 0;
	double FloorReadySeconds = -1.0;
	int32 FloorDeaths = 0;
	int32 FloorFailures = 0;
};
