#include "InnerDoctrine/ProjectInnerDoctrineTravelStateSubsystem.h"

#include "Survival/ProjectCurseTypes.h"
#include "Progression/ProjectProgressionState.h"
#include "Engine/GameInstance.h"

void UProjectInnerDoctrineTravelStateSubsystem::StoreCurseState(
	const float CurrentCurse,
	const bool bCursedEpisodeActive,
	const float CursedEpisodeRemainingSeconds)
{
	CurseState.bValid = true;
	CurseState.Curse = FMath::Clamp(CurrentCurse, 0.f, ProjectCurse::Maximum);
	CurseState.CursedEpisodeRemainingSeconds = FMath::Max(0.f, CursedEpisodeRemainingSeconds);
	CurseState.bCursedEpisodeActive =
		bCursedEpisodeActive && CurseState.CursedEpisodeRemainingSeconds > KINDA_SMALL_NUMBER;
}

bool UProjectInnerDoctrineTravelStateSubsystem::TryGetCurseState(
	FProjectCurseTravelState& OutState) const
{
	if (!CurseState.bValid)
	{
		return false;
	}

	OutState = CurseState;
	return true;
}

void UProjectInnerDoctrineTravelStateSubsystem::ResetCurseState()
{
	CurseState = FProjectCurseTravelState();
}

uint64 UProjectInnerDoctrineTravelStateSubsystem::GetLifecycleGeneration() const
{
	return LifecycleGeneration;
}

void UProjectInnerDoctrineTravelStateSubsystem::NotifyNewGameStarted()
{
	if (auto* Progression = GetGameInstance()->GetSubsystem<UProjectProgressionTravelState>()) Progression->ResetForNewCharacter();
	ResetCurseState();
	++LifecycleGeneration;
}
