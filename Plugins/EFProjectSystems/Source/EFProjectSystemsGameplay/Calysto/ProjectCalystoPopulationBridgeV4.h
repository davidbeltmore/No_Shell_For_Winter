#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoPopulationBridgeV4.h"
#include "Companions/ProjectRunCompanionTypes.h"

/** The single project gameplay implementation for V4 enemy, NPC and chest materialization. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoPopulationBridgeV4 final
	: public IEFCalystoPopulationBridgeV4
{
public:
	/**
	 * Canonical conversion used both while materializing a new floor NPC and
	 * while validating a same-floor recruited companion for revival. Keeping
	 * the stable-ID derivation here prevents the two paths from drifting.
	 */
	static bool BuildRandomNPCDefinitionFromIntent(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		FProjectCompanionDefinition& OutDefinition,
		FString& OutError);

	virtual bool HandlesCategory(EEFCalystoContentCategoryV4 Category) const override;
	virtual bool GatherAdditionalPreloadPaths(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError) const override;

	virtual bool PrepareDeferredActor(
		UWorld* World,
		AActor* DeferredActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
		FString& OutError) override;

	virtual bool FinalizeSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		TConstArrayView<FEFCalystoChestContentDirectiveV4> ChestContent,
		TArray<FEFCalystoChestContentDirectiveV4>& OutVerifiedChestContents,
		FString& OutError) override;

	virtual void RollbackSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive) override;

	virtual bool ProvidesCompanionRosterReadiness() const override { return true; }
	virtual bool IsCompanionRosterReady(
		UWorld* World,
		const FString& ExpectedCompanionSnapshotHash,
		FString& OutError) const override;

private:
	struct FPendingNPC
	{
		FProjectCompanionDefinition Definition;
		bool bRosterProjection = false;
		bool bStatisticsRepairApplied = false;
		bool bFinalized = false;
	};

	static FGuid StableGuidFromInstanceId(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		FName StableInstanceId);
	static bool BuildRandomNPCDefinition(
		UWorld* World,
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		FProjectCompanionDefinition& OutDefinition,
		FString& OutError);
	void PruneInvalidPendingNPCs() const;

	mutable TMap<TWeakObjectPtr<AActor>, FPendingNPC> PendingNPCs;
};
