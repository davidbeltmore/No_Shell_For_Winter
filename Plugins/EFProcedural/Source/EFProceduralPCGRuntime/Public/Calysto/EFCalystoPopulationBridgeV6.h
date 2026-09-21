#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

#include "Calysto/EFCalystoPopulationPlannerV6.h"

class AActor;
class UWorld;

/**
 * Gameplay-owned extension point for V6 population categories that need
 * project systems during deferred spawning. The PCG runtime owns placement;
 * implementations may only apply values already frozen in the population
 * plan and may never load or reroll content.
 */
class EFPROCEDURALPCGRUNTIME_API IEFCalystoPopulationBridgeV6 : public IModularFeature
{
public:
	virtual ~IEFCalystoPopulationBridgeV6() = default;

	static FName GetModularFeatureName();

	/** Categories with a mandatory gameplay bridge in the definitive runtime. */
	static bool CategoryRequiresBridge(FName CategoryId);

	/** Resolves exactly one implementation for a mandatory category. */
	static IEFCalystoPopulationBridgeV6* ResolveUniqueBridge(
		FName CategoryId,
		FString& OutError);

	/**
	 * Collects project-owned dependencies for every bridge used by this plan.
	 * The caller must merge these paths with Plan.PreloadClassPaths and load the
	 * resulting closure asynchronously before calling the materializer.
	 */
	static bool GatherRegisteredAdditionalPreloadPaths(
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError);

	/**
	 * Final post-commit roster gate. Exactly one registered bridge must opt in,
	 * including valid floors which happened to select no bridged actor category.
	 */
	static bool ValidateRegisteredCompanionRosterReady(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		const FString& ExpectedSnapshotHash,
		FString& OutError);

	virtual bool HandlesCategory(FName CategoryId) const = 0;

	virtual bool ProvidesCompanionRosterReadiness() const
	{
		return false;
	}

	virtual bool ValidateCompanionRosterReady(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		const FString& ExpectedSnapshotHash,
		FString& OutError) const
	{
		OutError = TEXT("This Calysto V6 population bridge does not provide companion-roster readiness.");
		return false;
	}

	virtual bool GatherAdditionalPreloadPaths(
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError) const
	{
		OutAssetPaths.Reset();
		OutError.Reset();
		return true;
	}

	/** Configure an actor while SpawnActorDeferred is still active. */
	virtual bool PrepareDeferredActor(
		UWorld* World,
		AActor* DeferredActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision,
		TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
		FString& OutError) = 0;

	/**
	 * Finalize gameplay registration after BeginPlay. Chest implementations
	 * return the exact verified child Decision IDs in canonical order.
	 */
	virtual bool FinalizeSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision,
		TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
		TArray<FString>& OutVerifiedChestContentDecisionIds,
		FString& OutError) = 0;

	/** Undo every per-actor gameplay registration made by this bridge. */
	virtual void RollbackSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision) = 0;

	/**
	 * Apply floor-global gameplay state after every actor has been verified.
	 * The materializer calls this exactly once per distinct bridge and plan.
	 */
	virtual bool CommitPopulationPlan(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		FString& OutError)
	{
		OutError.Reset();
		return true;
	}

	/** Transactional inverse of CommitPopulationPlan. */
	virtual void RollbackPopulationPlan(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan)
	{
	}
};
