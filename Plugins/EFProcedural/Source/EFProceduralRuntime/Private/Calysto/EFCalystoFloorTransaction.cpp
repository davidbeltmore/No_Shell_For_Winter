#include "Calysto/EFCalystoFloorTransaction.h"
#include "Calysto/EFCalystoDirectorProbability.h"

bool FEFCalystoRollbackEvidence::IsComplete() const
{
	return bPCGCleanupComplete && Actors == 0 && Instances == 0 && Decals == 0 && Reservations == 0
		&& NavigationRegistrations == 0 && PendingCallbacks == 0 && TransientReferences == 0 && AttemptLeases == 0;
}

bool FEFCalystoCommitEvidence::IsComplete(FString& Error) const
{
	if (Entry.ContainsNaN() || ReservationHash.IsEmpty() || RealizationHash.IsEmpty())
	{ Error = TEXT("The verified entry and reservation/realization identities are required."); return false; }
	if (!bUniqueOwnedStartAndEnd || !bBlockingFloorAndCapsuleClearance || !bCompleteRelevantNavigationRoute
		|| !bMaterialsVerified || !bNativeParityVerified || !bRoomThemeContractVerified || !bGameplayPreparedWithoutCommit)
	{ Error = TEXT("Every structural, navigation, room, material and gameplay postcondition must verify."); return false; }
	if (ReservedElements.Num() != VerifiedElements.Num())
	{ Error = TEXT("The realized element count differs from the frozen reservation count."); return false; }
	for (const FGuid& Id : ReservedElements)
		if (!Id.IsValid() || !VerifiedElements.Contains(Id))
		{ Error = TEXT("A selected element failed realization or was silently substituted."); return false; }
	Error.Reset(); return true;
}

int32 FEFCalystoFloorTransaction::DeriveTopologySeed(const FEFCalystoFloorRequestIdentity& Identity, const int32 Index)
{
	FEFCalystoRandomKey Key;
	Key.RunSeed = Identity.RunSeed;
	Key.FloorNumber = Identity.FloorNumber;
	Key.RerollIndex = Identity.RerollIndex;
	Key.AttemptIndex = Index;
	Key.StyleId = Identity.SelectedStyle;
	return static_cast<int32>(FEFCalystoDirectorProbability::Hash(Key, EEFCalystoRandomDomain::Topology) % 2147483646ULL) + 1;
}

void FEFCalystoFloorTransaction::CreateAttempt(const double Now, const FGuid RequestId)
{
	FEFCalystoAttemptRecord& Record = Attempts.AddDefaulted_GetRef();
	Record.Token = {RequestId, FGuid::NewGuid()};
	Record.AttemptIndex = Attempts.Num() - 1;
	Record.TopologySeed = DeriveTopologySeed(Request, Record.AttemptIndex);
	Record.StartedSeconds = Now;
	FrozenReservationHash.Reset(); FrozenElements.Reset(); CommitEvidence = {};
	Phase = EEFCalystoFloorPhase::Preflight;
}

bool FEFCalystoFloorTransaction::Begin(const FEFCalystoFloorRequestIdentity& Identity, const FGuid& RoutingRequestId,
	const double Now, FString& Error)
{
	const double CandidateDeadline = Now + RequestBudgetSeconds;
	if (Phase != EEFCalystoFloorPhase::Idle || !FMath::IsFinite(Now) || Now < 0
		|| !FMath::IsFinite(CandidateDeadline) || CandidateDeadline <= Now
		|| !RoutingRequestId.IsValid()
		|| Identity.FloorNumber < 1 || Identity.RerollIndex < 0 || !Identity.SelectedStyle.IsValid()
		|| Identity.CompiledConfigurationHash.IsEmpty() || Identity.PreFloorGameplayHash.IsEmpty())
	{ Error = TEXT("A new transaction requires an idle owner, an external routing identity and a complete immutable floor identity."); return false; }
	Request = Identity; Deadline = CandidateDeadline; LastObservedSeconds = Now;
	CreateAttempt(Now, RoutingRequestId); Error.Reset(); return true;
}

bool FEFCalystoFloorTransaction::AcceptClock(const double Now, FString& Error)
{
	if (!FMath::IsFinite(Now) || Now < 0.0 || Now < LastObservedSeconds)
	{
		Error = TEXT("Transaction time must be finite, nonnegative and monotonically nondecreasing.");
		return false;
	}
	LastObservedSeconds = Now;
	Error.Reset();
	return true;
}

bool FEFCalystoFloorTransaction::Owns(const FEFCalystoAttemptToken& Token) const
{
	return !Attempts.IsEmpty() && Token == Attempts.Last().Token;
}

bool FEFCalystoFloorTransaction::Transition(const FEFCalystoAttemptToken& Token, const EEFCalystoFloorPhase Expected,
	const EEFCalystoFloorPhase Next, const double Now, FString& Error)
{
	if (!Owns(Token)) { Error = TEXT("Stale or foreign attempt callback."); return false; }
	if (!AcceptClock(Now, Error)) return false;
	if (ObserveDeadline(Now)) { Error = TEXT("The shared floor request deadline expired."); return false; }
	if (Phase != Expected) { Error = TEXT("The operation is not valid in the current transaction phase."); return false; }
	Phase = Next; Error.Reset(); return true;
}

bool FEFCalystoFloorTransaction::FinishPreflight(const FEFCalystoAttemptToken& Token, double Now, FString& Error)
{ return Transition(Token, EEFCalystoFloorPhase::Preflight, EEFCalystoFloorPhase::NativeGeneration, Now, Error); }

bool FEFCalystoFloorTransaction::ArmRootGeneration(const FEFCalystoAttemptToken& Token, double Now, FString& Error)
{
	if (!Transition(Token, EEFCalystoFloorPhase::NativeGeneration, EEFCalystoFloorPhase::NativeGeneration, Now, Error)) return false;
	if (Attempts.Last().RootGenerationRequests != 0) { Error = TEXT("Root GenerateLocal is already consumed for this attempt."); return false; }
	++Attempts.Last().RootGenerationRequests; return true;
}

bool FEFCalystoFloorTransaction::NativeGenerationFinished(const FEFCalystoAttemptToken& Token, double Now, FString& Error)
{
	if (!Owns(Token) || Attempts.Last().RootGenerationRequests != 1)
	{ Error = TEXT("Native completion requires exactly one owned root generation request."); return false; }
	return Transition(Token, EEFCalystoFloorPhase::NativeGeneration, EEFCalystoFloorPhase::StructuralVerification, Now, Error);
}

bool FEFCalystoFloorTransaction::StructuralVerificationFinished(const FEFCalystoAttemptToken& Token, double Now, FString& Error)
{ return Transition(Token, EEFCalystoFloorPhase::StructuralVerification, EEFCalystoFloorPhase::NavigationAndReservations, Now, Error); }

bool FEFCalystoFloorTransaction::FreezeReservations(const FEFCalystoAttemptToken& Token, const FString& Hash,
	const TSet<FGuid>& Elements, double Now, FString& Error)
{
	if (Hash.IsEmpty()) { Error = TEXT("A canonical reservation identity is required."); return false; }
	for (const FGuid& Id : Elements) if (!Id.IsValid()) { Error = TEXT("Invalid reserved element identity."); return false; }
	if (!Transition(Token, EEFCalystoFloorPhase::NavigationAndReservations, EEFCalystoFloorPhase::RealizationVerification, Now, Error)) return false;
	FrozenReservationHash = Hash; FrozenElements = Elements; return true;
}

bool FEFCalystoFloorTransaction::VerifyRealization(const FEFCalystoAttemptToken& Token,
	const FEFCalystoCommitEvidence& Evidence, double Now, FString& Error)
{
	if (!Owns(Token) || Phase != EEFCalystoFloorPhase::RealizationVerification)
	{ Error = TEXT("Realization verification requires the current reserved attempt."); return false; }
	if (!AcceptClock(Now, Error)) return false;
	if (ObserveDeadline(Now)) { Error = TEXT("The shared floor request deadline expired."); return false; }
	const auto RejectEvidence = [&](EEFCalystoAttemptFailure Failure, FName Code)
	{
		Reject(Token, Failure, Code, Error, Now);
		return false;
	};
	if (Evidence.ReservationHash != FrozenReservationHash || Evidence.ReservedElements.Num() != FrozenElements.Num())
	{ Error = TEXT("Realization evidence is not bound to this attempt's frozen reservation."); return RejectEvidence(EEFCalystoAttemptFailure::Configuration, TEXT("RealizationReservationMismatch")); }
	for (const FGuid& Id : FrozenElements) if (!Evidence.ReservedElements.Contains(Id))
	{ Error = TEXT("Realization changed a frozen element identity."); return RejectEvidence(EEFCalystoAttemptFailure::Configuration, TEXT("RealizationIdentityChanged")); }
	if (!Evidence.IsComplete(Error)) return RejectEvidence(EEFCalystoAttemptFailure::Spatial, TEXT("RealizationPostconditionFailed"));
	if (!Transition(Token, EEFCalystoFloorPhase::RealizationVerification, EEFCalystoFloorPhase::Commit, Now, Error)) return false;
	CommitEvidence = Evidence; return true;
}

bool FEFCalystoFloorTransaction::CommitAndReleasePlayer(const FEFCalystoAttemptToken& Token, double Now, FString& Error)
{
	if (bGameplayCommitted) { Error = TEXT("Gameplay is already committed for this request."); return false; }
	if (!Transition(Token, EEFCalystoFloorPhase::Commit, EEFCalystoFloorPhase::PlayerRelease, Now, Error)) return false;
	bGameplayCommitted = true;
	Phase = EEFCalystoFloorPhase::Ready;
	Attempts.Last().bAccepted = true; Attempts.Last().FinishedSeconds = Now; return true;
}

bool FEFCalystoFloorTransaction::FailAcceptedActivation(const FEFCalystoAttemptToken& Token, const FString& Message, double Now)
{
	if (!Owns(Token) || !bGameplayCommitted || Phase != EEFCalystoFloorPhase::Ready) return false;
	FString ClockError; if (!AcceptClock(Now, ClockError)) return false;
	Phase = EEFCalystoFloorPhase::Failed; FailureMessage = Message;
	Attempts.Last().Failure = EEFCalystoAttemptFailure::Configuration;
	Attempts.Last().FailureCode = TEXT("AcceptedActivationInvariant");
	return true;
}

bool FEFCalystoFloorTransaction::CompleteAcceptedRelease(const FEFCalystoAttemptToken& Token,
	const FEFCalystoRollbackEvidence& Evidence)
{
	if (!Owns(Token) || !bGameplayCommitted || !Evidence.IsComplete()
		|| (Phase != EEFCalystoFloorPhase::Ready && Phase != EEFCalystoFloorPhase::Failed)) return false;
	Attempts.Last().bCleanupVerified = true;
	return true;
}

bool FEFCalystoFloorTransaction::Reject(const FEFCalystoAttemptToken& Token, const EEFCalystoAttemptFailure Failure,
	const FName Code, const FString& Message, double Now)
{
	if (!Owns(Token) || Phase == EEFCalystoFloorPhase::Ready || Phase == EEFCalystoFloorPhase::Cancelled
		|| Phase == EEFCalystoFloorPhase::Failed || Phase == EEFCalystoFloorPhase::RetryAvailable
		|| Phase == EEFCalystoFloorPhase::RollingBack || bGameplayCommitted) return false;
	FString ClockError;
	if (!AcceptClock(Now, ClockError)) return false;
	Attempts.Last().Failure = Failure; Attempts.Last().FailureCode = Code;
	FailureMessage = Message; Phase = EEFCalystoFloorPhase::RollingBack; return true;
}

bool FEFCalystoFloorTransaction::CompleteRollback(const FEFCalystoAttemptToken& Token,
	const FEFCalystoRollbackEvidence& Evidence, double Now, FString& Error)
{
	if (!Owns(Token) || Phase != EEFCalystoFloorPhase::RollingBack)
	{ Error = TEXT("Rollback completion is not owned by the current rejected attempt."); return false; }
	if (!AcceptClock(Now, Error)) return false;
	if (!Evidence.IsComplete()) { Error = TEXT("Attempt cleanup is unfinished; another generation cannot start."); return false; }
	FEFCalystoAttemptRecord& Record = Attempts.Last();
	Record.bCleanupVerified = true; Record.FinishedSeconds = Now;
	FrozenElements.Reset(); FrozenReservationHash.Reset(); CommitEvidence = {};
	if (Record.Failure == EEFCalystoAttemptFailure::Cancelled) Phase = EEFCalystoFloorPhase::Cancelled;
	else if (Record.Failure == EEFCalystoAttemptFailure::Spatial && Now < Deadline && Attempts.Num() < MaximumAttempts)
		Phase = EEFCalystoFloorPhase::RetryAvailable;
	else Phase = EEFCalystoFloorPhase::Failed;
	Error.Reset(); return true;
}

bool FEFCalystoFloorTransaction::BeginRetry(double Now, FString& Error)
{
	if (Phase != EEFCalystoFloorPhase::RetryAvailable)
	{ Error = TEXT("Recovery requires completed cleanup and remaining shared request time/attempts."); return false; }
	if (!AcceptClock(Now, Error)) return false;
	if (ObserveDeadline(Now) || Attempts.Num() >= MaximumAttempts)
	{ Error = TEXT("Recovery requires completed cleanup and remaining shared request time/attempts."); return false; }
	const FGuid RequestId = GetToken().Request; CreateAttempt(Now, RequestId); Error.Reset(); return true;
}

bool FEFCalystoFloorTransaction::Cancel(double Now)
{
	if (Phase == EEFCalystoFloorPhase::Idle || Phase == EEFCalystoFloorPhase::Ready
		|| Phase == EEFCalystoFloorPhase::Failed || Phase == EEFCalystoFloorPhase::Cancelled) return false;
	FString ClockError;
	if (!AcceptClock(Now, ClockError)) return false;
	if (Phase == EEFCalystoFloorPhase::RetryAvailable) { Phase = EEFCalystoFloorPhase::Cancelled; return true; }
	if (Phase == EEFCalystoFloorPhase::RollingBack) { Attempts.Last().Failure = EEFCalystoAttemptFailure::Cancelled; return true; }
	return Reject(GetToken(), EEFCalystoAttemptFailure::Cancelled, TEXT("Cancelled"), TEXT("The floor request was cancelled."), Now);
}

bool FEFCalystoFloorTransaction::ObserveDeadline(double Now)
{
	if (Phase == EEFCalystoFloorPhase::Idle || Phase == EEFCalystoFloorPhase::Ready || Phase == EEFCalystoFloorPhase::Failed
		|| Phase == EEFCalystoFloorPhase::Cancelled) return false;
	FString ClockError;
	if (!AcceptClock(Now, ClockError) || Now < Deadline) return false;
	if (Phase == EEFCalystoFloorPhase::RetryAvailable) { Phase = EEFCalystoFloorPhase::Failed; return true; }
	if (Phase != EEFCalystoFloorPhase::RollingBack)
		Reject(GetToken(), EEFCalystoAttemptFailure::Deadline, TEXT("RequestDeadline"), TEXT("The dungeon could not be prepared within 30 seconds."), Now);
	return true;
}

const FTransform* FEFCalystoFloorTransaction::GetValidatedEntry() const
{
	return (Phase == EEFCalystoFloorPhase::Commit || Phase == EEFCalystoFloorPhase::PlayerRelease || Phase == EEFCalystoFloorPhase::Ready)
		? &CommitEvidence.Entry : nullptr;
}
