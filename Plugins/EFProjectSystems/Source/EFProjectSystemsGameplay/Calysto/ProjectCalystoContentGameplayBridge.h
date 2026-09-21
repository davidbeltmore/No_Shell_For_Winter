#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentGameplayBridge.h"
#include "Calysto/ProjectCalystoDormantController.h"
#include "Companions/ProjectRunCompanionTypes.h"

class AActor;
class APawn;
class AACFCharacter;
class UWorld;
class UProjectSocialSubsystem;
class FProjectCalystoGameplaySnapshot;

/** Direct V7 implementation of the neutral content materialization contract.
 * It consumes typed roles and stable identities only; no V6 plan, category
 * label, display name, or entry-name parsing participates in this bridge. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoContentGameplayBridge final
	: public IEFCalystoContentGameplayBridge
{
public:
	explicit FProjectCalystoContentGameplayBridge(TSharedRef<FProjectCalystoGameplaySnapshot> InSnapshot);

	virtual bool Preflight(const FEFCalystoContentGameplayContext& Context,
		TArray<FSoftObjectPath>& AdditionalDependencies, FString& Error) const override;
	virtual bool PrepareDeferredActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor,
		TConstArrayView<FEFCalystoReservedContent> ContainerContents, FString& Error) override;
	virtual bool StageExistingContainer(const FEFCalystoContentGameplayContext& Context, FGuid ContainerId,
		AActor* Container, TConstArrayView<FEFCalystoReservedContent> Contents, FString& Error) override;
	virtual EEFCalystoGameplayObservation FinalizeSpawnedActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor, FString& Error) override;
	virtual FEFCalystoGameplayElementEvidence ObserveActor(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor) const override;
	virtual FEFCalystoGameplayElementEvidence ObserveInventoryItem(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Container) const override;
	virtual EEFCalystoGameplayObservation PrepareCommit(const FEFCalystoContentGameplayContext& Context,
		FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) override;
	virtual bool CommitPrepared(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) override;
	virtual EEFCalystoActivationResult ConfirmAccepted(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoGameplayCommitReceipt& Receipt, FString& Error) override;
	virtual void BeginRelease(const FEFCalystoContentGameplayContext& Context,
		EEFCalystoContentReleaseIntent Intent) override;
	virtual FEFCalystoGameplayReleaseEvidence ObserveRelease(const FEFCalystoContentGameplayContext& Context,
		EEFCalystoContentReleaseIntent Intent) const override;

private:
	struct FActorStage
	{
		TWeakObjectPtr<AActor> Actor;
		FEFCalystoReservedContent Reservation;
		FProjectCompanionDefinition Companion;
		TSharedPtr<FProjectCalystoDormantController> DormantController;
		TArray<FGuid> ContentReservations;
		bool bEnemy = false;
		bool bSupportNpc = false;
		bool bContainer = false;
		bool bFinalized = false;
		bool bControllerTransferred = false;
	};
	TSharedRef<FProjectCalystoGameplaySnapshot> Snapshot;
	mutable TMap<FGuid, FActorStage> Stages;
	mutable FEFCalystoAttemptToken OwnedToken;
	mutable TWeakObjectPtr<UWorld> OwnedWorld;
	mutable FString OwnedManifestHash;
	mutable bool bPrepared = false;
	mutable bool bPublished = false;
	mutable bool bAccepted = false;
	mutable bool bReleaseBegun = false;
	mutable bool bSocialReleaseSucceeded = false;

	bool Owns(const FEFCalystoContentGameplayContext& Context, FString& Error) const;
	bool Bind(const FEFCalystoContentGameplayContext& Context, FString& Error) const;
	bool BuildCompanionDefinition(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, FProjectCompanionDefinition& OutDefinition, FString& Error) const;
	bool VerifyStage(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, AActor* Actor, FString& Error) const;
	FEFCalystoGameplayElementEvidence Evidence(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation, EEFCalystoGameplayObservation State,
		const FString& NativeStateHash, const FString& Message = FString()) const;
	static FString HashRows(TArray<FString> Rows);
	static FGuid StableCompanionId(const FEFCalystoContentGameplayContext& Context,
		const FEFCalystoReservedContent& Reservation);
	static UProjectSocialSubsystem* ResolveSocial(const UWorld* World);
};
