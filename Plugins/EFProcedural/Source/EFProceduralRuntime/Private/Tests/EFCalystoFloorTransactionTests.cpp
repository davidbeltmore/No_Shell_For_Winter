#include "Calysto/EFCalystoFloorTransaction.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FEFCalystoFloorTransaction>);
static_assert(!std::is_copy_assignable_v<FEFCalystoFloorTransaction>);
static_assert(!std::is_move_constructible_v<FEFCalystoFloorTransaction>);
static_assert(!std::is_move_assignable_v<FEFCalystoFloorTransaction>);

namespace EFCalystoTransactionTests
{
static FEFCalystoFloorRequestIdentity Identity()
{
	FEFCalystoFloorRequestIdentity Result;
	Result.RunSeed = 2959332854660340481ULL;
	Result.SelectedStyle = FGuid(1, 2, 3, 4);
	Result.CompiledConfigurationHash = TEXT("immutable-config");
	Result.PreFloorGameplayHash = TEXT("pre-floor-companions-inventory-outcomes");
	return Result;
}
static FGuid RoutingRequest(const uint32 Value = 1)
{ return FGuid(0x524F5554, 0x494E4749, 0, Value); }
static FEFCalystoRollbackEvidence Released()
{ FEFCalystoRollbackEvidence E; E.bPCGCleanupComplete = true; return E; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTransactionRecoveryTest,
	"NoShellForWinter.CalystoDungeon.Director.Transaction.SharedDeadlineAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTransactionRecoveryTest::RunTest(const FString&)
{
	FEFCalystoFloorTransaction T;
	FString Error;
	const auto Original = EFCalystoTransactionTests::Identity();
	TestTrue(TEXT("Begin complete request"), T.Begin(Original, EFCalystoTransactionTests::RoutingRequest(), 100, Error));
	const auto First = T.GetToken();
	TestEqual(TEXT("Transaction retains the externally captured routing identity"), First.Request,
		EFCalystoTransactionTests::RoutingRequest());
	const auto FirstSeed = T.GetAttempts()[0].TopologySeed;
	TestTrue(TEXT("Preflight"), T.FinishPreflight(First, 101, Error));
	TestTrue(TEXT("Arm root once"), T.ArmRootGeneration(First, 101, Error));
	TestFalse(TEXT("Duplicate root rejected"), T.ArmRootGeneration(First, 101, Error));
	TestTrue(TEXT("Spatial rejection"), T.Reject(First, EEFCalystoAttemptFailure::Spatial, TEXT("Entry"), TEXT("No blocking entry floor."), 102));
	TestFalse(TEXT("Cannot overlap cleanup"), T.BeginRetry(102, Error));
	auto Cleanup = EFCalystoTransactionTests::Released(); Cleanup.NavigationRegistrations = 1;
	TestFalse(TEXT("Nav registration prevents retry"), T.CompleteRollback(First, Cleanup, 103, Error));
	Cleanup.NavigationRegistrations = 0;
	TestTrue(TEXT("Cleanup complete"), T.CompleteRollback(First, Cleanup, 104, Error));
	TestTrue(TEXT("Retry begins"), T.BeginRetry(105, Error));
	TestEqual(TEXT("Deadline not reset"), T.GetDeadline(), 130.0);
	TestTrue(TEXT("Reseed"), T.GetAttempts().Last().TopologySeed != FirstSeed);
	TestEqual(TEXT("Style frozen"), T.GetIdentity().SelectedStyle, Original.SelectedStyle);
	TestEqual(TEXT("Floor unchanged"), T.GetIdentity().FloorNumber, Original.FloorNumber);
	TestEqual(TEXT("Gameplay snapshot unchanged"), T.GetIdentity().PreFloorGameplayHash, Original.PreFloorGameplayHash);
	TestFalse(TEXT("Stale completion cannot advance retry"), T.NativeGenerationFinished(First, 105, Error));
	TestTrue(TEXT("Deadline rolls back current attempt"), T.ObserveDeadline(130));
	TestTrue(TEXT("Cleanup allowed after deadline for ownership release"), T.CompleteRollback(T.GetToken(), Cleanup, 131, Error));
	TestEqual(TEXT("Exhaustion remains Failed; no HUB action exists in transaction"), T.GetPhase(), EEFCalystoFloorPhase::Failed);
	TestFalse(TEXT("No committed outcomes"), T.HasCommittedGameplay());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTransactionLimitsTest,
	"NoShellForWinter.CalystoDungeon.Director.Transaction.AttemptLimitsAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTransactionLimitsTest::RunTest(const FString&)
{
	FString Error;
	FEFCalystoFloorTransaction T;
	TestTrue(TEXT("Begin"), T.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(), 0, Error));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(TEXT("Reject attempt"), T.Reject(T.GetToken(), EEFCalystoAttemptFailure::Spatial, TEXT("NoRoute"), TEXT("No complete route."), Index * 2 + 1));
		TestTrue(TEXT("Rollback attempt"), T.CompleteRollback(T.GetToken(), EFCalystoTransactionTests::Released(), Index * 2 + 2, Error));
		TestEqual(TEXT("Bounded retry"), T.BeginRetry(Index * 2 + 2, Error), Index < 3);
	}
	TestEqual(TEXT("Exactly four total attempts"), T.GetAttempts().Num(), 4);
	for (const auto Failure : {EEFCalystoAttemptFailure::Resource, EEFCalystoAttemptFailure::Configuration})
	{
		FEFCalystoFloorTransaction Resource;
		Resource.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(10 + int32(Failure)), 0, Error);
		Resource.Reject(Resource.GetToken(), Failure, TEXT("Preflight"), TEXT("Unavailable contract."), 1);
		Resource.CompleteRollback(Resource.GetToken(), EFCalystoTransactionTests::Released(), 2, Error);
		TestFalse(TEXT("Seed cannot fix resource/config"), Resource.BeginRetry(2, Error));
	}
	FEFCalystoFloorTransaction Cancelled;
	Cancelled.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(20), 0, Error);
	TestTrue(TEXT("Cancel owned request"), Cancelled.Cancel(1));
	TestTrue(TEXT("Cancelled resources released"), Cancelled.CompleteRollback(Cancelled.GetToken(), EFCalystoTransactionTests::Released(), 2, Error));
	TestEqual(TEXT("Cancelled state"), Cancelled.GetPhase(), EEFCalystoFloorPhase::Cancelled);
	TestFalse(TEXT("Cancellation never starts retry"), Cancelled.BeginRetry(3, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTransactionCommitTest,
	"NoShellForWinter.CalystoDungeon.Director.Transaction.StrictCommitAndStableSeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTransactionCommitTest::RunTest(const FString&)
{
	FString Error;
	FEFCalystoFloorTransaction T, Replay;
	const auto Identity = EFCalystoTransactionTests::Identity();
	T.Begin(Identity, EFCalystoTransactionTests::RoutingRequest(30), 0, Error);
	Replay.Begin(Identity, EFCalystoTransactionTests::RoutingRequest(31), 800, Error);
	TestTrue(TEXT("Routing identity differs"), !(T.GetToken() == Replay.GetToken()));
	TestEqual(TEXT("Routing/time do not affect seed"), T.GetAttempts()[0].TopologySeed, Replay.GetAttempts()[0].TopologySeed);
	const auto Token = T.GetToken();
	TestFalse(TEXT("Release cannot bypass validation"), T.CommitAndReleasePlayer(Token, 1, Error));
	T.FinishPreflight(Token, 1, Error); T.ArmRootGeneration(Token, 1, Error);
	T.NativeGenerationFinished(Token, 2, Error); T.StructuralVerificationFinished(Token, 3, Error);
	const FGuid Selected(10, 20, 30, 40);
	TSet<FGuid> Reserved; Reserved.Add(Selected);
	TestTrue(TEXT("Freeze reservations"), T.FreezeReservations(Token, TEXT("reservation"), Reserved, 4, Error));
	FEFCalystoCommitEvidence E;
	E.ReservationHash = TEXT("reservation"); E.RealizationHash = TEXT("realization"); E.ReservedElements = Reserved;
	E.bUniqueOwnedStartAndEnd = E.bBlockingFloorAndCapsuleClearance = E.bCompleteRelevantNavigationRoute = true;
	E.bMaterialsVerified = E.bNativeParityVerified = E.bRoomThemeContractVerified = E.bGameplayPreparedWithoutCommit = true;
	E.VerifiedElements = Reserved;
	TestTrue(TEXT("Every reserved element verified"), T.VerifyRealization(Token, E, 5, Error));
	TestNotNull(TEXT("Single entry published after verification"), T.GetValidatedEntry());
	TestTrue(TEXT("Commit and release state advance in one owned operation"), T.CommitAndReleasePlayer(Token, 6, Error));
	TestFalse(TEXT("No duplicate gameplay commit"), T.CommitAndReleasePlayer(Token, 6, Error));
	TestEqual(TEXT("Ready only after complete evidence"), T.GetPhase(), EEFCalystoFloorPhase::Ready);
	TestTrue(TEXT("Accepted record"), T.GetAttempts()[0].bAccepted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTransactionRealizationRejectionTest,
	"NoShellForWinter.CalystoDungeon.Director.Transaction.RealizationFailureRejectsWholeAttempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTransactionRealizationRejectionTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FEFCalystoFloorTransaction T, Foreign;
		FString Error;
		T.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(40 + Case * 2), 0, Error);
		Foreign.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(41 + Case * 2), 0, Error);
		const FEFCalystoAttemptToken Token = T.GetToken();
		T.FinishPreflight(Token, 1, Error); T.ArmRootGeneration(Token, 1, Error);
		T.NativeGenerationFinished(Token, 2, Error); T.StructuralVerificationFinished(Token, 3, Error);
		const FGuid Selected(10, 20, 30, 40);
		TSet<FGuid> Reserved; Reserved.Add(Selected);
		T.FreezeReservations(Token, TEXT("reservation"), Reserved, 4, Error);
		FEFCalystoCommitEvidence E;
		E.ReservationHash = TEXT("reservation"); E.RealizationHash = TEXT("realization");
		E.ReservedElements = Reserved;
		E.bUniqueOwnedStartAndEnd = E.bBlockingFloorAndCapsuleClearance = E.bCompleteRelevantNavigationRoute = true;
		E.bMaterialsVerified = E.bNativeParityVerified = E.bRoomThemeContractVerified = E.bGameplayPreparedWithoutCommit = true;
		if (Case == 1) E.VerifiedElements.Add(FGuid(9, 9, 9, 9));
		if (Case == 2) { E.VerifiedElements = Reserved; E.ReservationHash = TEXT("foreign-reservation"); }
		TestFalse(TEXT("A foreign callback cannot reject the current reserved attempt"), T.VerifyRealization(Foreign.GetToken(), E, 1000, Error));
		TestEqual(TEXT("Foreign callback leaves current phase unchanged"), T.GetPhase(), EEFCalystoFloorPhase::RealizationVerification);
		TestTrue(TEXT("Foreign callback creates no failure"), T.GetAttempts().Last().FailureCode.IsNone());
		TestFalse(TEXT("Settled failed realization rejects"), T.VerifyRealization(Token, E, 5, Error));
		TestEqual(TEXT("Whole attempt enters rollback immediately"), T.GetPhase(), EEFCalystoFloorPhase::RollingBack);
		TestFalse(TEXT("Failed realization never commits outcomes"), T.HasCommittedGameplay());
		E.ReservationHash = TEXT("reservation"); E.VerifiedElements = Reserved;
		TestFalse(TEXT("Corrected evidence cannot resurrect the rejected attempt"), T.VerifyRealization(Token, E, 6, Error));
		TestFalse(TEXT("Release is rejected while cleanup is pending"), T.CommitAndReleasePlayer(Token, 6, Error));
		TestTrue(TEXT("Every failed realization must release ownership"), T.CompleteRollback(Token, EFCalystoTransactionTests::Released(), 7, Error));
		TestFalse(TEXT("Old attempt was never accepted"), T.GetAttempts()[0].bAccepted);
		if (Case < 2)
		{
			TestTrue(TEXT("Spatial realization failure may start a cleaned new attempt"), T.BeginRetry(8, Error));
			TestFalse(TEXT("Retry has a distinct routing identity"), T.GetToken() == Token);
			TestEqual(TEXT("New attempt returns to preflight, without reusing realization"), T.GetPhase(), EEFCalystoFloorPhase::Preflight);
		}
		else TestFalse(TEXT("Reservation contract mismatch is not repaired by another seed"), T.BeginRetry(8, Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoTransactionClockOwnershipTest,
	"NoShellForWinter.CalystoDungeon.Director.Transaction.MonotonicClockAndUniqueOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoTransactionClockOwnershipTest::RunTest(const FString&)
{
	TestFalse(TEXT("A live transaction cannot be copied into a second owner"), std::is_copy_constructible_v<FEFCalystoFloorTransaction>);
	TestFalse(TEXT("Assignment cannot duplicate live tokens or accepted state"), std::is_copy_assignable_v<FEFCalystoFloorTransaction>);
	FString Error;
	const double InvalidTimes[] = {-1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
	for (const double Now : InvalidTimes)
	{
		FEFCalystoFloorTransaction Unstarted;
		TestFalse(TEXT("Invalid initial time is rejected"), Unstarted.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(), Now, Error));
		TestEqual(TEXT("Invalid begin preserves idle owner"), Unstarted.GetPhase(), EEFCalystoFloorPhase::Idle);
	}
	FEFCalystoFloorTransaction Overflow;
	TestFalse(TEXT("Clock too large to represent a future deadline is rejected"),
		Overflow.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(), std::numeric_limits<double>::max(), Error));
	FEFCalystoFloorTransaction T;
	T.Begin(EFCalystoTransactionTests::Identity(), EFCalystoTransactionTests::RoutingRequest(), 100, Error);
	const auto Token = T.GetToken();
	for (const double Now : InvalidTimes)
	{
		TestFalse(TEXT("Invalid time cannot advance preflight"), T.FinishPreflight(Token, Now, Error));
		TestFalse(TEXT("Invalid time cannot reject or cancel"), T.Reject(Token, EEFCalystoAttemptFailure::Spatial, TEXT("InvalidClock"), TEXT("Invalid clock"), Now));
		TestFalse(TEXT("Invalid cancellation clock cannot change ownership"), T.Cancel(Now));
		TestFalse(TEXT("Invalid observation cannot invent deadline expiry"), T.ObserveDeadline(Now));
		TestEqual(TEXT("Invalid times leave preflight intact"), T.GetPhase(), EEFCalystoFloorPhase::Preflight);
	}
	TestFalse(TEXT("Time cannot move backwards before the request start"), T.FinishPreflight(Token, 99, Error));
	TestFalse(TEXT("An ordinary observation before the deadline stays pending"), T.ObserveDeadline(110));
	TestFalse(TEXT("Callbacks older than the last observation are rejected"), T.FinishPreflight(Token, 109, Error));
	TestTrue(TEXT("Equal-time callbacks are allowed on one game-thread turn"), T.FinishPreflight(Token, 110, Error));
	TestTrue(TEXT("Valid spatial failure starts rollback"), T.Reject(Token, EEFCalystoAttemptFailure::Spatial, TEXT("Spatial"), TEXT("Spatial failure"), 111));
	TestFalse(TEXT("Rollback cannot move time backwards"), T.CompleteRollback(Token, EFCalystoTransactionTests::Released(), 110, Error));
	TestTrue(TEXT("Rollback observes actual later completion"), T.CompleteRollback(Token, EFCalystoTransactionTests::Released(), 112, Error));
	for (const double Now : InvalidTimes)
		TestFalse(TEXT("Invalid retry clock cannot create another attempt"), T.BeginRetry(Now, Error));
	TestFalse(TEXT("Retry cannot precede cleanup"), T.BeginRetry(111, Error));
	TestEqual(TEXT("Rejected clocks create no attempts"), T.GetAttempts().Num(), 1);
	TestTrue(TEXT("Valid retry uses the same request deadline"), T.BeginRetry(113, Error));
	TestEqual(TEXT("Clock checks never renew the 30 second budget"), T.GetDeadline(), 130.0);
	return true;
}
#endif
