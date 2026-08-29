#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

#include "Calysto/EFCalystoDungeonTypesV4.h"

class AActor;
class UWorld;

/**
 * Project-owned extension point used by the PCG materializer for categories
 * that require gameplay-module knowledge before BeginPlay (currently enemies,
 * NPCs and chest storage). Implementations live outside EFProcedural, so this contract
 * keeps the module dependency one-way and never reaches into ACF or Calysto.
 *
 * Exactly one implementation must claim a required category. Zero or multiple
 * claimants are a fail-closed schema error. Implementations must not reroll any
 * value from the frozen directive.
 */
class EFPROCEDURALRUNTIME_API IEFCalystoPopulationBridgeV4 : public IModularFeature
{
public:
	IEFCalystoPopulationBridgeV4();
	virtual ~IEFCalystoPopulationBridgeV4();

	static FName GetModularFeatureName();

	/**
	 * Ask every distinct registered bridge for project-owned assets required by
	 * this frozen floor. The result is deduplicated and canonically sorted.
	 * Enemy, NPC and Chest directives must each have exactly one owning bridge before
	 * any asynchronous load or world travel can proceed.
	 */
	static bool GatherRegisteredAdditionalPreloadPaths(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError);

	/** Returns true only for categories this implementation fully owns. */
	virtual bool HandlesCategory(EEFCalystoContentCategoryV4 Category) const = 0;

	/**
	 * Contribute project-owned soft references that must be loaded before any
	 * deferred actor is prepared. Implementations may inspect only the frozen
	 * intent and must never synchronously load, reroll or mutate it.
	 */
	virtual bool GatherAdditionalPreloadPaths(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError) const
	{
		OutError.Reset();
		return true;
	}

	/**
	 * Configure a deferred actor instance before FinishSpawning/BeginPlay.
	 * ChestContent contains only directives whose ContainerInstanceId matches
	 * Directive.StableInstanceId.
	 */
	virtual bool PrepareDeferredActor(
		UWorld* World,
		AActor* DeferredActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
		FString& OutError) = 0;

	/**
	 * Register and validate the finished actor. For chests, return the exact
	 * per-attempt payload actually present in storage; every stable attempt ID,
	 * catalog ID, class and tier must match the frozen input.
	 */
	virtual bool FinalizeSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
		TArray<FEFCalystoChestContentDirectiveV4>& OutVerifiedChestContents,
		FString& OutError) = 0;

	/**
	 * Undo gameplay registrations made by FinalizeSpawnedActor. PCG calls this
	 * for every already-realized bridged actor if any later slot fails.
	 */
	virtual void RollbackSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive) = 0;

	/** True only for the bridge that owns the persistent companion roster. */
	virtual bool ProvidesCompanionRosterReadiness() const
	{
		return false;
	}

	/**
	 * Validate that the live roster projection matches the snapshot frozen into
	 * the V4 floor intent. Called strictly after population realization.
	 */
	virtual bool IsCompanionRosterReady(
		UWorld* World,
		const FString& ExpectedCompanionSnapshotHash,
		FString& OutError) const
	{
		OutError = TEXT("This population bridge does not provide companion-roster readiness.");
		return false;
	}
};
