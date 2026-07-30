#pragma once

#include "CoreMinimal.h"
#include "Defeat/ProjectDefeatTypes.h"

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPostDefeatEligibilityContext
{
	EProjectDefeatReason DefeatReason = EProjectDefeatReason::None;
	EProjectKnockoutReason KnockoutReason = EProjectKnockoutReason::None;
	bool bPlayerCompletedStruggleMinigame = false;
	bool bMatureDefeatAllowed = false;
	bool bPresentationAvailable = false;
	bool bAuthority = false;
	bool bTechnicalFailure = false;
	bool bCancelledBeforeResolution = false;
};

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatFlowLogic
{
public:
	static constexpr float MatureSoloVignetteChance = 0.10f;

	static bool ShouldKeepCombatSessionActive(
		float CurrentWorldTimeSeconds,
		float LastCombatEventTimeSeconds,
		float CombatWindowSeconds,
		int32 NearbyQualifiedEnemyCount);

	static bool IsCombatPressureActive(
		bool bQualifiedEnemyHit,
		bool bHasActiveCombatSession,
		int32 NearbyQualifiedEnemyCount);

	static bool ShouldEnterRepeatKnockoutDefeat(
		bool bAlreadyDowned,
		int32 ActiveCombatSessionId,
		int32 LastKnockoutCombatSessionId);

	static bool IsMaturePresentationEligible(const FProjectPostDefeatEligibilityContext& Context);

	static EProjectPostDefeatPresentation ResolvePostDefeatPresentation(
		FProjectDefeatTransferPayload& InOutPayload,
		const FProjectPostDefeatEligibilityContext& Context,
		float AuthorityRoll);

	static bool IsStoredMaturePresentationValid(const FProjectDefeatTransferPayload& Payload);

	static void EnforceMinimumTotalStruggleNotes(
		TArray<FProjectStruggleRound>& InOutRounds,
		int32 MinimumTotalNotes,
		int32 MaxNotesPerRound,
		float MaxSpacingSeconds);
};
