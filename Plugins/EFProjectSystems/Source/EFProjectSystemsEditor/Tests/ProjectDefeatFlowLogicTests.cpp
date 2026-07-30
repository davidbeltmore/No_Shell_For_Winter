#include "Defeat/ProjectDefeatFlowLogic.h"
#include "Defeat/ProjectDefeatFlowComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatFlowCombatSessionPersistsTest,
	"NoShellForWinter.Defeat.Flow.CombatSessionPersistsWithNearbyEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatFlowCombatSessionPersistsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const bool bKeepActive = FProjectDefeatFlowLogic::ShouldKeepCombatSessionActive(
		20.0f,
		10.0f,
		5.0f,
		1);

	TestTrue(TEXT("A combat session should stay active while a qualified enemy remains nearby."), bKeepActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatFlowCombatSessionEndsTest,
	"NoShellForWinter.Defeat.Flow.CombatSessionEndsWithoutNearbyEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatFlowCombatSessionEndsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const bool bKeepActive = FProjectDefeatFlowLogic::ShouldKeepCombatSessionActive(
		20.0f,
		10.0f,
		5.0f,
		0);

	TestFalse(TEXT("A combat session should end once the grace window expires and no enemies remain nearby."), bKeepActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatFlowRepeatKnockoutTest,
	"NoShellForWinter.Defeat.Flow.RepeatKnockoutDirectDefeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatFlowRepeatKnockoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(
		TEXT("Being downed again while already downed should force defeated."),
		FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(true, 0, 0));

	TestTrue(
		TEXT("A second lethal hit inside the same combat session should force defeated."),
		FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(false, 3, 3));

	TestFalse(
		TEXT("A new combat session should allow another knockout before defeated."),
		FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(false, 4, 3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatFlowMinNotesTest,
	"NoShellForWinter.Defeat.Flow.MinimumStruggleNotes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatFlowMinNotesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FProjectStruggleRound> Rounds;
	FProjectStruggleRound& FirstRound = Rounds.AddDefaulted_GetRef();
	FirstRound.NoteCount = 4;
	FirstRound.TravelTimeSeconds = 1.5f;
	FirstRound.DurationSeconds = 1.5f + (FirstRound.NoteCount * 0.5f);

	FProjectStruggleRound& SecondRound = Rounds.AddDefaulted_GetRef();
	SecondRound.NoteCount = 5;
	SecondRound.TravelTimeSeconds = 1.5f;
	SecondRound.DurationSeconds = 1.5f + (SecondRound.NoteCount * 0.5f);

	FProjectDefeatFlowLogic::EnforceMinimumTotalStruggleNotes(Rounds, 20, 12, 0.5f);

	int32 TotalNotes = 0;
	for (const FProjectStruggleRound& Round : Rounds)
	{
		TotalNotes += Round.NoteCount;
		TestTrue(TEXT("No struggle round should exceed the configured per-round cap."), Round.NoteCount <= 12);
		TestTrue(TEXT("Round duration should still include note travel spacing."), Round.DurationSeconds >= Round.TravelTimeSeconds);
	}

	TestEqual(TEXT("The helper should grow the full struggle queue to the configured minimum note count."), TotalNotes, 20);
	return true;
}

namespace ProjectDefeatOutcomeTests
{
	FProjectPostDefeatEligibilityContext MakeEligibleContext()
	{
		FProjectPostDefeatEligibilityContext Context;
		Context.DefeatReason = EProjectDefeatReason::LostStruggle;
		Context.KnockoutReason = EProjectKnockoutReason::PainMaxed;
		Context.bPlayerCompletedStruggleMinigame = true;
		Context.bMatureDefeatAllowed = true;
		Context.bPresentationAvailable = true;
		Context.bAuthority = true;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatOutcomeRollBoundariesTest,
	"NoShellForWinter.Defeat.Outcome.RollBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatOutcomeRollBoundariesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FProjectPostDefeatEligibilityContext Context = ProjectDefeatOutcomeTests::MakeEligibleContext();

	auto Resolve = [&Context](const float Roll)
	{
		FProjectDefeatTransferPayload Payload;
		return FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(Payload, Context, Roll);
	};

	TestEqual(TEXT("A roll of zero selects the optional vignette."), Resolve(0.0f), EProjectPostDefeatPresentation::MatureSoloVignette);
	TestEqual(TEXT("A roll immediately below ten percent selects the optional vignette."), Resolve(0.099f), EProjectPostDefeatPresentation::MatureSoloVignette);
	TestEqual(TEXT("The ten-percent boundary is direct respawn."), Resolve(0.10f), EProjectPostDefeatPresentation::None);
	TestEqual(TEXT("A roll above the threshold is direct respawn."), Resolve(0.90f), EProjectPostDefeatPresentation::None);
	TestEqual(TEXT("An invalid negative roll fails closed."), Resolve(-0.01f), EProjectPostDefeatPresentation::None);
	TestEqual(TEXT("An invalid roll above one fails closed."), Resolve(1.01f), EProjectPostDefeatPresentation::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatOutcomeIdempotencyTest,
	"NoShellForWinter.Defeat.Outcome.PayloadResolutionIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatOutcomeIdempotencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FProjectDefeatTransferPayload Payload;
	Payload.TransferId = FGuid::NewGuid();
	const FGuid OriginalTransferId = Payload.TransferId;

	FProjectPostDefeatEligibilityContext Context = ProjectDefeatOutcomeTests::MakeEligibleContext();
	const EProjectPostDefeatPresentation FirstResult =
		FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(Payload, Context, 0.05f);

	Context.bMatureDefeatAllowed = false;
	const EProjectPostDefeatPresentation SecondResult =
		FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(Payload, Context, 0.95f);

	TestTrue(TEXT("The payload should record that the outcome was resolved."), Payload.bPostDefeatPresentationResolved);
	TestEqual(TEXT("The first eligible roll should select the vignette."), FirstResult, EProjectPostDefeatPresentation::MatureSoloVignette);
	TestEqual(TEXT("A later resolution attempt must return the stored result."), SecondResult, FirstResult);
	TestEqual(TEXT("A later resolution attempt must not replace the stored roll."), Payload.PostDefeatPresentationRoll, 0.05f);
	TestEqual(TEXT("Resolution must not replace the defeat transfer identity."), Payload.TransferId, OriginalTransferId);
	TestTrue(
		TEXT("The stored mature selection should validate before travel."),
		FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(Payload));

	FProjectDefeatTransferPayload TamperedPayload = Payload;
	TamperedPayload.bPlayerCompletedStruggleMinigame = false;
	TestFalse(
		TEXT("A transferred payload cannot promote an uncompleted minigame into mature presentation."),
		FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(TamperedPayload));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatOutcomeEligibilityTest,
	"NoShellForWinter.Defeat.Outcome.EligibilityIsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatOutcomeEligibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FProjectPostDefeatEligibilityContext Eligible = ProjectDefeatOutcomeTests::MakeEligibleContext();
	TestTrue(TEXT("A real completed minigame loss with policy approval is eligible."), FProjectDefeatFlowLogic::IsMaturePresentationEligible(Eligible));

	auto TestRejected = [this, &Eligible](const TCHAR* Message, TFunctionRef<void(FProjectPostDefeatEligibilityContext&)> Mutate)
	{
		FProjectPostDefeatEligibilityContext Context = Eligible;
		Mutate(Context);
		TestFalse(Message, FProjectDefeatFlowLogic::IsMaturePresentationEligible(Context));
	};

	TestRejected(TEXT("Repeat knockout is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.DefeatReason = EProjectDefeatReason::RepeatKnockout; });
	TestRejected(TEXT("A missing struggle widget is a technical direct respawn."), [](FProjectPostDefeatEligibilityContext& Context) { Context.DefeatReason = EProjectDefeatReason::StruggleUnavailable; Context.bTechnicalFailure = true; });
	TestRejected(TEXT("An uncompleted minigame is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.bPlayerCompletedStruggleMinigame = false; });
	TestRejected(TEXT("A cancelled attempt is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.bCancelledBeforeResolution = true; });
	TestRejected(TEXT("Tactical Retreat is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.KnockoutReason = EProjectKnockoutReason::TacticalRetreat; });
	TestRejected(TEXT("The Tactical Retreat defeat reason is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.DefeatReason = EProjectDefeatReason::TacticalRetreat; });
	TestRejected(TEXT("Legacy surrender is also fail-closed."), [](FProjectPostDefeatEligibilityContext& Context) { Context.KnockoutReason = EProjectKnockoutReason::Surrender; });
	TestRejected(TEXT("A presentation suppressed by runtime policy is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.bMatureDefeatAllowed = false; });
	TestRejected(TEXT("An unavailable presentation is never eligible."), [](FProjectPostDefeatEligibilityContext& Context) { Context.bPresentationAvailable = false; });
	TestRejected(TEXT("A non-authoritative caller cannot select mature presentation."), [](FProjectPostDefeatEligibilityContext& Context) { Context.bAuthority = false; });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatTacticalRetreatDirectTest,
	"NoShellForWinter.Defeat.Flow.TacticalRetreatIsDirectAndNeutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatTacticalRetreatDirectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UProjectDefeatFlowComponent* Component = NewObject<UProjectDefeatFlowComponent>();
	TestNotNull(TEXT("The defeat flow component is constructible."), Component);
	if (!Component)
	{
		return false;
	}

	TestTrue(
		TEXT("Tactical Retreat enters defeat directly without knockout or struggle."),
		Component->RequestTacticalRetreat(TEXT("Automation.TacticalRetreat")));
	TestEqual(
		TEXT("Tactical Retreat records its own knockout reason."),
		Component->GetCurrentKnockoutReason(),
		EProjectKnockoutReason::TacticalRetreat);
	TestEqual(
		TEXT("Tactical Retreat records its own direct-defeat reason."),
		Component->GetCurrentDefeatReason(),
		EProjectDefeatReason::TacticalRetreat);
	TestEqual(
		TEXT("Tactical Retreat never selects a mature presentation."),
		Component->GetCurrentPostDefeatPresentation(),
		EProjectPostDefeatPresentation::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDefeatOutcomeSeededRateTest,
	"NoShellForWinter.Defeat.Outcome.SeededTenPercentRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDefeatOutcomeSeededRateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FProjectPostDefeatEligibilityContext Context = ProjectDefeatOutcomeTests::MakeEligibleContext();
	FRandomStream RandomStream(0x51A7C0DE);
	constexpr int32 SampleCount = 10000;
	int32 VignetteCount = 0;

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		FProjectDefeatTransferPayload Payload;
		const EProjectPostDefeatPresentation Result =
			FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(Payload, Context, RandomStream.FRand());
		VignetteCount += Result == EProjectPostDefeatPresentation::MatureSoloVignette ? 1 : 0;
	}

	const float ObservedRate = static_cast<float>(VignetteCount) / static_cast<float>(SampleCount);
	TestTrue(
		TEXT("A deterministic sample should remain close to the configured ten-percent outcome."),
		ObservedRate >= 0.085f && ObservedRate <= 0.115f);
	return true;
}

#endif
