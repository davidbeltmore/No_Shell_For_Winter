#pragma once

#include "CoreMinimal.h"

/** Monotonic phases owned by one floor request. Rejection never commits gameplay. */
enum class EEFCalystoFloorPhase : uint8
{
	Idle, Preflight, NativeGeneration, StructuralVerification, NavigationAndReservations,
	RealizationVerification, Commit, PlayerRelease, Ready, RollingBack, RetryAvailable,
	Failed, Cancelled
};

enum class EEFCalystoAttemptFailure : uint8 { Spatial, Configuration, Resource, Cancelled, Deadline };

/** Acceptance is irreversible. Activation failure requires accepted cleanup, never spatial recovery. */
enum class EEFCalystoActivationResult : uint8 { Activated, AcceptedExitRequested, InvariantFailure };

/** A delayed final navigation/material observation keeps ownership and the player protected. */
enum class EEFCalystoPlayerReleaseResult : uint8 { Pending, Released, Rejected };

/** Routing identity is deliberately separate from reproducible random identity. */
struct FEFCalystoAttemptToken
{
	FGuid Request;
	FGuid Attempt;
	bool operator==(const FEFCalystoAttemptToken& Other) const
	{ return Request == Other.Request && Attempt == Other.Attempt; }
};

struct FEFCalystoFloorRequestIdentity
{
	int64 RunSeed = 0;
	int64 FloorNumber = 1;
	int32 RerollIndex = 0;
	FGuid SelectedStyle;
	FString CompiledConfigurationHash;
	FString PreFloorGameplayHash;
};

struct FEFCalystoAttemptRecord
{
	FEFCalystoAttemptToken Token;
	int32 AttemptIndex = 0;
	int32 TopologySeed = 0;
	int32 RootGenerationRequests = 0;
	double StartedSeconds = 0;
	double FinishedSeconds = 0;
	FName FailureCode;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Spatial;
	bool bAccepted = false;
	bool bCleanupVerified = false;
};

/** Actual owners fill this after asynchronous teardown; zero means no retained ownership. */
struct EFPROCEDURALRUNTIME_API FEFCalystoRollbackEvidence
{
	int32 Actors = 0;
	int32 Instances = 0;
	int32 Decals = 0;
	int32 Reservations = 0;
	int32 NavigationRegistrations = 0;
	int32 PendingCallbacks = 0;
	int32 TransientReferences = 0;
	int32 AttemptLeases = 0;
	bool bPCGCleanupComplete = false;
	bool IsComplete() const;
};

/** Evidence is supplied by the native adapter and strict materializer, never a debug menu. */
struct EFPROCEDURALRUNTIME_API FEFCalystoCommitEvidence
{
	FTransform Entry = FTransform::Identity;
	FString ReservationHash;
	FString RealizationHash;
	TSet<FGuid> ReservedElements;
	TSet<FGuid> VerifiedElements;
	bool bUniqueOwnedStartAndEnd = false;
	bool bBlockingFloorAndCapsuleClearance = false;
	bool bCompleteRelevantNavigationRoute = false;
	bool bMaterialsVerified = false;
	bool bNativeParityVerified = false;
	bool bRoomThemeContractVerified = false;
	bool bGameplayPreparedWithoutCommit = false;
	bool IsComplete(FString& OutError) const;
};

/** Pure transaction state. World/asset/companion owners compose this and supply real evidence. */
class EFPROCEDURALRUNTIME_API FEFCalystoFloorTransaction final
{
public:
	static constexpr double RequestBudgetSeconds = 30.0;
	static constexpr int32 MaximumAttempts = 4;
	FEFCalystoFloorTransaction() = default;
	FEFCalystoFloorTransaction(const FEFCalystoFloorTransaction&) = delete;
	FEFCalystoFloorTransaction& operator=(const FEFCalystoFloorTransaction&) = delete;
	FEFCalystoFloorTransaction(FEFCalystoFloorTransaction&&) = delete;
	FEFCalystoFloorTransaction& operator=(FEFCalystoFloorTransaction&&) = delete;
	/** The request identifier is created by the travel owner before any pre-floor
	 * gameplay snapshot is captured. It routes callbacks only and never enters a
	 * random domain. */
	bool Begin(const FEFCalystoFloorRequestIdentity& Identity, const FGuid& RoutingRequestId,
		double Now, FString& Error);
	bool FinishPreflight(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	bool ArmRootGeneration(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	bool NativeGenerationFinished(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	bool StructuralVerificationFinished(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	bool FreezeReservations(const FEFCalystoAttemptToken& Token, const FString& Hash,
		const TSet<FGuid>& Elements, double Now, FString& Error);
	bool VerifyRealization(const FEFCalystoAttemptToken& Token, const FEFCalystoCommitEvidence& Evidence,
		double Now, FString& Error);
	// Finalize in one game-thread operation; never leave committed gameplay behind a delayed release callback.
	bool CommitAndReleasePlayer(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	bool FailAcceptedActivation(const FEFCalystoAttemptToken& Token, const FString& Message, double Now);
	bool CompleteAcceptedRelease(const FEFCalystoAttemptToken& Token, const FEFCalystoRollbackEvidence& Evidence);
	bool Reject(const FEFCalystoAttemptToken& Token, EEFCalystoAttemptFailure Failure,
		FName Code, const FString& Message, double Now);
	bool CompleteRollback(const FEFCalystoAttemptToken& Token, const FEFCalystoRollbackEvidence& Evidence,
		double Now, FString& Error);
	bool BeginRetry(double Now, FString& Error);
	bool Cancel(double Now);
	bool ObserveDeadline(double Now);
	bool Owns(const FEFCalystoAttemptToken& Token) const;
	EEFCalystoFloorPhase GetPhase() const { return Phase; }
	const FEFCalystoFloorRequestIdentity& GetIdentity() const { return Request; }
	const TArray<FEFCalystoAttemptRecord>& GetAttempts() const { return Attempts; }
	FEFCalystoAttemptToken GetToken() const { return Attempts.IsEmpty() ? FEFCalystoAttemptToken() : Attempts.Last().Token; }
	double GetDeadline() const { return Deadline; }
	const FTransform* GetValidatedEntry() const;
	/** Evidence exists only after strict verification; inspection never advances the transaction. */
	const FEFCalystoCommitEvidence* GetVerifiedCommitEvidence() const
	{ return GetValidatedEntry() ? &CommitEvidence : nullptr; }
	const FString& GetFailureMessage() const { return FailureMessage; }
	bool HasCommittedGameplay() const { return bGameplayCommitted; }
	static int32 DeriveTopologySeed(const FEFCalystoFloorRequestIdentity& Identity, int32 AttemptIndex);

private:
	FEFCalystoFloorRequestIdentity Request;
	TArray<FEFCalystoAttemptRecord> Attempts;
	EEFCalystoFloorPhase Phase = EEFCalystoFloorPhase::Idle;
	FEFCalystoCommitEvidence CommitEvidence;
	FString FrozenReservationHash;
	TSet<FGuid> FrozenElements;
	FString FailureMessage;
	double Deadline = 0;
	double LastObservedSeconds = -1.0;
	bool bGameplayCommitted = false;
	bool AcceptClock(double Now, FString& Error);
	bool Transition(const FEFCalystoAttemptToken& Token, EEFCalystoFloorPhase Expected,
		EEFCalystoFloorPhase Next, double Now, FString& Error);
	void CreateAttempt(double Now, FGuid RequestId);
};
