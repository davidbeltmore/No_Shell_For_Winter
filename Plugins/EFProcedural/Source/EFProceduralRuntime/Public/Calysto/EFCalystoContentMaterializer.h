#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentGameplayBridge.h"
#include "UObject/GCObject.h"

struct FStreamableHandle;

enum class EEFCalystoContentRealizationState : uint8
{ Idle, Loading, Spawning, Verifying, Prepared, GameplayPublished, Committed, Releasing, Released };

struct EFPROCEDURALRUNTIME_API FEFCalystoContentRealizationObservation
{
	FEFCalystoAttemptToken Token;
	EEFCalystoContentRealizationState State = EEFCalystoContentRealizationState::Idle;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Configuration;
	FName FailureCode;
	FString Message;
	FString ManifestHash;
	FString RealizationHash;
	TSet<FGuid> VerifiedElements;
	int32 OwnedActors = 0;
	int32 RetainedLeases = 0;
};

/** One game-thread owner per floor request, with at most four sequential attempt tokens.
 * No root PCG generation, position search, random roll,
 * asset fallback, V6 dependency or gameplay commit occurs during observation. */
class EFPROCEDURALRUNTIME_API FEFCalystoContentMaterializer final : public FGCObject
{
public:
	FEFCalystoContentMaterializer() = default;
	virtual ~FEFCalystoContentMaterializer() override;
	FEFCalystoContentMaterializer(const FEFCalystoContentMaterializer&) = delete;
	FEFCalystoContentMaterializer& operator=(const FEFCalystoContentMaterializer&) = delete;
	/** An identical active token+sealed request is idempotent; another token requires verified release. */
	bool Begin(const FEFCalystoContentGameplayContext& Request,
		TSharedRef<IEFCalystoContentGameplayBridge> GameplayBridge, double Now, FString& Error);
	/** At most Operations actor/item operations per call (1..32); loading uses one retained async handle.
	 * Caller schedules bounded observations; this class creates no timers or completion callbacks. */
	FEFCalystoContentRealizationObservation Observe(double Now, int32 Operations = 4);
	/** Reverify exact actor/item/material bindings before atomic gameplay publication. */
	bool Commit(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	/** Only the coordinator's accepted exact token may enter this boundary. The caller must handle
	 * InvariantFailure without releasing player protection or rolling back accepted persistent state.
	 * AcceptedExitRequested means a callback already requested token-scoped accepted cleanup. */
	EEFCalystoActivationResult ActivateCommitted(const FEFCalystoAttemptToken& Token, FString& Error);
	void Release(const FEFCalystoAttemptToken& Token);
	FEFCalystoRollbackEvidence GetReleaseEvidence(const FEFCalystoAttemptToken& Token) const;
	const FEFCalystoContentRealizationObservation& GetObservation() const { return Observation; }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FEFCalystoContentMaterializer"); }
private:
	struct FOwnedActor
	{
		FGuid ReservationId;
		TObjectPtr<AActor> Actor = nullptr;
		bool bHiddenOnRelease = false;
		bool bCollisionOnRelease = false;
		bool bTickOnRelease = false;
		bool bQuarantinedForRelease = false;
	};
	FEFCalystoContentGameplayContext Context;
	TSharedPtr<IEFCalystoContentGameplayBridge> Bridge;
	TSharedPtr<FStreamableHandle> LoadHandle;
	TArray<FSoftObjectPath> Dependencies;
	TArray<FOwnedActor> Owned;
	TArray<int32> ActorIndices;
	/** World actors finalize before their exact child inventory reservations are observed. */
	TArray<int32> VerificationIndices;
	TArray<FGuid> ExistingContainerIds;
	TMap<FGuid, TArray<FEFCalystoReservedContent>> Contents;
	TMap<FGuid, TArray<FEFCalystoContentMaterialExpectation>> MaterialsByActor;
	TMap<FGuid, FString> NativeStateHashes;
	FEFCalystoGameplayCommitReceipt CommitReceipt;
	FEFCalystoContentRealizationObservation Observation;
	FEFCalystoGameplayReleaseEvidence GameplayRelease;
	FString RequestSeal;
	FGuid OwningRequest;
	TSet<FGuid> AttemptHistory;
	double LastNow = -1;
	int32 SpawnIndex = 0;
	int32 ContainerIndex = 0;
	int32 VerifyIndex = 0;
	bool bActivated = false;
	bool bCoordinatorAccepted = false;
	bool bAcceptanceNotified = false;
	bool bActivationFailed = false;
	bool bBridgeReleaseBegun = false;
	bool bOperationActive = false;
	EEFCalystoContentReleaseIntent ReleaseIntent = EEFCalystoContentReleaseIntent::RejectedAttempt;
	bool AcceptClock(double Now, FString& Error);
	bool Fail(EEFCalystoAttemptFailure Failure, FName Code, const FString& Message);
	bool SpawnOne(const FEFCalystoReservedContent& Reservation, FString& Error);
	EEFCalystoGameplayObservation VerifyOne(const FEFCalystoReservedContent& Reservation, FString& Error);
	AActor* FindContainer(FGuid Id) const;
	void StepRelease(int32 Operations);
	void UpdateCounts();
	static FString Seal(const FEFCalystoContentGameplayContext& Request);
};
