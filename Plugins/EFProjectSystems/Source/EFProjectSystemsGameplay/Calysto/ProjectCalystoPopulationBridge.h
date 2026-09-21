#pragma once

#include "CoreMinimal.h"

#include "Calysto/EFCalystoPopulationBridgeV6.h"
#include "Companions/ProjectRunCompanionTypes.h"

/** Definitive project gameplay bridge for Calysto Enemy, NPC, and Chest decisions. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoPopulationBridge final
	: public IEFCalystoPopulationBridgeV6
{
public:
	static bool BuildRandomNPCDefinition(
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& Decision,
		FProjectCompanionDefinition& OutDefinition,
		FString& OutError);

	virtual bool HandlesCategory(FName CategoryId) const override;
	virtual bool ProvidesCompanionRosterReadiness() const override { return true; }
	virtual bool ValidateCompanionRosterReady(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		const FString& ExpectedSnapshotHash,
		FString& OutError) const override;
	virtual bool GatherAdditionalPreloadPaths(
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FSoftObjectPath>& OutAssetPaths,
		FString& OutError) const override;

	virtual bool PrepareDeferredActor(
		UWorld* World,
		AActor* DeferredActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision,
		TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
		FString& OutError) override;

	virtual bool FinalizeSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision,
		TConstArrayView<FEFCalystoPopulationDecisionV6> ChestContents,
		TArray<FString>& OutVerifiedChestContentDecisionIds,
		FString& OutError) override;

	virtual void RollbackSpawnedActor(
		UWorld* World,
		AActor* SpawnedActor,
		const FEFCalystoPopulationPlanV6& Plan,
		const FEFCalystoPopulationDecisionV6& ActorDecision) override;

	virtual bool CommitPopulationPlan(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan,
		FString& OutError) override;

	virtual void RollbackPopulationPlan(
		UWorld* World,
		const FEFCalystoPopulationPlanV6& Plan) override;

private:
	struct FPendingNPC
	{
		FProjectCompanionDefinition Definition;
		bool bStatisticsRepairApplied = false;
		bool bFinalized = false;
	};

	void PruneTransientState() const;

	mutable TMap<TWeakObjectPtr<AActor>, FPendingNPC> PendingNPCs;
	mutable TMap<TWeakObjectPtr<UWorld>, FString> CommittedPopulationByWorld;
};
