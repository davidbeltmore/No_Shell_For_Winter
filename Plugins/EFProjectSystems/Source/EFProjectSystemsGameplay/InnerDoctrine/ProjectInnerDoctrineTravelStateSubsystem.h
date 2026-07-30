#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectInnerDoctrineTravelStateSubsystem.generated.h"

USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCurseTravelState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	bool bValid = false;

	UPROPERTY(Transient)
	float Curse = 0.f;

	UPROPERTY(Transient)
	bool bCursedEpisodeActive = false;

	UPROPERTY(Transient)
	float CursedEpisodeRemainingSeconds = 0.f;
};

/**
 * Holds player Curse state only for the lifetime of the current game instance.
 * This preserves Curse across map travel without turning it into persistent
 * save-game progression.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineTravelStateSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void StoreCurseState(
		float CurrentCurse,
		bool bCursedEpisodeActive,
		float CursedEpisodeRemainingSeconds);

	bool TryGetCurseState(FProjectCurseTravelState& OutState) const;
	void ResetCurseState();
	uint64 GetLifecycleGeneration() const;

	/**
	 * Canonical new-game bridge. The authoritative new-game flow must invoke
	 * this before opening its first gameplay map.
	 *
	 * Integration status: PENDING caller; no project-owned authoritative
	 * new-game event exists yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Lifecycle")
	void NotifyNewGameStarted();

private:
	UPROPERTY(Transient)
	FProjectCurseTravelState CurseState;

	uint64 LifecycleGeneration = 0;
};
