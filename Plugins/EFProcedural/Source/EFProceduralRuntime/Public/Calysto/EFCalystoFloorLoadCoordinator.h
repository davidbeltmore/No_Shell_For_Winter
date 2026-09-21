#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"

/** Ordered runtime load phases retained for the lifetime of one Calysto floor. */
enum class EEFCalystoLoadPhase : uint8
{
	SessionCore,
	FloorVisual,
	PostTopologyContent,
	OptionalDecals
};

/** Observable terminal state for a retained asynchronous load phase. */
enum class EEFCalystoLoadPhaseState : uint8
{
	NotStarted,
	Loading,
	Ready,
	Failed
};

/**
 * Deduplicates and retains asynchronous asset loads used by Dungeon Director V6.
 *
 * The coordinator owns no UObject and never ticks. Callers retain the coordinator,
 * provide completion delegates owned by their subsystem, and explicitly reset floor
 * or session leases at lifecycle boundaries.
 */
class EFPROCEDURALRUNTIME_API FEFCalystoFloorLoadCoordinator final
{
public:
	FEFCalystoFloorLoadCoordinator() = default;
	~FEFCalystoFloorLoadCoordinator();

	FEFCalystoFloorLoadCoordinator(const FEFCalystoFloorLoadCoordinator&) = delete;
	FEFCalystoFloorLoadCoordinator& operator=(const FEFCalystoFloorLoadCoordinator&) = delete;

	/** Starts or replaces one phase. Paths are canonicalized and loaded exactly once. */
	bool BeginPhase(
		EEFCalystoLoadPhase Phase,
		TConstArrayView<FSoftObjectPath> AssetPaths,
		FStreamableDelegate Completion,
		FString& OutError);

	/** Verifies that every asset retained by the phase is currently resolved. */
	bool IsPhaseReady(EEFCalystoLoadPhase Phase, FString& OutError) const;

	/** Distinguishes an in-flight phase from a completed load with unresolved assets. */
	EEFCalystoLoadPhaseState GetPhaseState(
		EEFCalystoLoadPhase Phase,
		FString& OutError) const;

	/** Returns the canonical path closure retained by one phase. */
	TArray<FSoftObjectPath> GetPhasePaths(EEFCalystoLoadPhase Phase) const;

	/** Distinguishes an intentionally empty phase lease from a phase not yet started. */
	bool HasPhase(EEFCalystoLoadPhase Phase) const;

	/** Cancels floor-specific work while retaining the immutable session core. */
	void ResetFloor();

	/** Cancels and releases every phase. */
	void ResetAll();

private:
	struct FPhaseLease
	{
		TArray<FSoftObjectPath> Paths;
		TSharedPtr<FStreamableHandle> Handle;
	};

	void ResetPhase(EEFCalystoLoadPhase Phase);

	TMap<EEFCalystoLoadPhase, FPhaseLease> PhaseLeases;
};
