#include "Defeat/ProjectDefeatFlowLogic.h"

bool FProjectDefeatFlowLogic::ShouldKeepCombatSessionActive(
	const float CurrentWorldTimeSeconds,
	const float LastCombatEventTimeSeconds,
	const float CombatWindowSeconds,
	const int32 NearbyQualifiedEnemyCount)
{
	return NearbyQualifiedEnemyCount > 0
		|| (CurrentWorldTimeSeconds - LastCombatEventTimeSeconds) <= CombatWindowSeconds;
}

bool FProjectDefeatFlowLogic::IsCombatPressureActive(
	const bool bQualifiedEnemyHit,
	const bool bHasActiveCombatSession,
	const int32 NearbyQualifiedEnemyCount)
{
	return bQualifiedEnemyHit || bHasActiveCombatSession || NearbyQualifiedEnemyCount > 0;
}

bool FProjectDefeatFlowLogic::ShouldEnterRepeatKnockoutDefeat(
	const bool bAlreadyDowned,
	const int32 ActiveCombatSessionId,
	const int32 LastKnockoutCombatSessionId)
{
	return bAlreadyDowned
		|| (ActiveCombatSessionId != 0 && LastKnockoutCombatSessionId == ActiveCombatSessionId);
}

bool FProjectDefeatFlowLogic::IsMaturePresentationEligible(const FProjectPostDefeatEligibilityContext& Context)
{
	return Context.bAuthority
		&& Context.DefeatReason == EProjectDefeatReason::LostStruggle
		&& Context.KnockoutReason != EProjectKnockoutReason::Surrender
		&& Context.KnockoutReason != EProjectKnockoutReason::TacticalRetreat
		&& Context.bPlayerCompletedStruggleMinigame
		&& Context.bMatureDefeatAllowed
		&& Context.bPresentationAvailable
		&& !Context.bTechnicalFailure
		&& !Context.bCancelledBeforeResolution;
}

EProjectPostDefeatPresentation FProjectDefeatFlowLogic::ResolvePostDefeatPresentation(
	FProjectDefeatTransferPayload& InOutPayload,
	const FProjectPostDefeatEligibilityContext& Context,
	const float AuthorityRoll)
{
	if (InOutPayload.bPostDefeatPresentationResolved)
	{
		return InOutPayload.PostDefeatPresentation;
	}

	InOutPayload.DefeatReason = Context.DefeatReason;
	InOutPayload.KnockoutReason = Context.KnockoutReason;
	InOutPayload.bPlayerCompletedStruggleMinigame = Context.bPlayerCompletedStruggleMinigame;
	InOutPayload.bTechnicalFailure = Context.bTechnicalFailure;
	InOutPayload.bCancelledBeforeResolution = Context.bCancelledBeforeResolution;
	InOutPayload.bPostDefeatPresentationEligible = IsMaturePresentationEligible(Context);
	const bool bAuthorityRollValid = FMath::IsFinite(AuthorityRoll)
		&& AuthorityRoll >= 0.0f
		&& AuthorityRoll <= 1.0f;
	InOutPayload.PostDefeatPresentationRoll =
		InOutPayload.bPostDefeatPresentationEligible && bAuthorityRollValid
		? AuthorityRoll
		: -1.0f;
	InOutPayload.PostDefeatPresentation =
		InOutPayload.bPostDefeatPresentationEligible
		&& bAuthorityRollValid
		&& InOutPayload.PostDefeatPresentationRoll < MatureSoloVignetteChance
			? EProjectPostDefeatPresentation::MatureSoloVignette
			: EProjectPostDefeatPresentation::None;
	InOutPayload.bPostDefeatPresentationResolved = true;
	return InOutPayload.PostDefeatPresentation;
}

bool FProjectDefeatFlowLogic::IsStoredMaturePresentationValid(const FProjectDefeatTransferPayload& Payload)
{
	return Payload.bPostDefeatPresentationResolved
		&& Payload.bPostDefeatPresentationEligible
		&& Payload.PostDefeatPresentation == EProjectPostDefeatPresentation::MatureSoloVignette
		&& Payload.DefeatReason == EProjectDefeatReason::LostStruggle
		&& Payload.KnockoutReason != EProjectKnockoutReason::Surrender
		&& Payload.KnockoutReason != EProjectKnockoutReason::TacticalRetreat
		&& Payload.bPlayerCompletedStruggleMinigame
		&& !Payload.bTechnicalFailure
		&& !Payload.bCancelledBeforeResolution
		&& Payload.PostDefeatPresentationRoll >= 0.0f
		&& Payload.PostDefeatPresentationRoll < MatureSoloVignetteChance;
}

void FProjectDefeatFlowLogic::EnforceMinimumTotalStruggleNotes(
	TArray<FProjectStruggleRound>& InOutRounds,
	const int32 MinimumTotalNotes,
	const int32 MaxNotesPerRound,
	const float MaxSpacingSeconds)
{
	if (InOutRounds.Num() <= 0)
	{
		return;
	}

	int32 CurrentTotalNotes = 0;
	for (const FProjectStruggleRound& Round : InOutRounds)
	{
		CurrentTotalNotes += Round.NoteCount;
	}

	int32 RemainingNotesToAdd = FMath::Max(0, MinimumTotalNotes - CurrentTotalNotes);
	while (RemainingNotesToAdd > 0)
	{
		bool bAddedNoteThisPass = false;

		for (FProjectStruggleRound& Round : InOutRounds)
		{
			if (RemainingNotesToAdd <= 0)
			{
				break;
			}

			if (Round.NoteCount >= MaxNotesPerRound)
			{
				continue;
			}

			++Round.NoteCount;
			Round.DurationSeconds = Round.TravelTimeSeconds + (Round.NoteCount * MaxSpacingSeconds);
			--RemainingNotesToAdd;
			bAddedNoteThisPass = true;
		}

		if (!bAddedNoteThisPass)
		{
			break;
		}
	}
}
