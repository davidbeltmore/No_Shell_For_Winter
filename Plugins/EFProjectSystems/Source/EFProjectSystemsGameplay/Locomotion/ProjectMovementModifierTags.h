#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace ProjectMovementModifierTags
{
	EFPROJECTSYSTEMSGAMEPLAY_API const FGameplayTag& DoctrineCelerity();
	EFPROJECTSYSTEMSGAMEPLAY_API const FGameplayTag& DoctrineRecoveredMomentum();

	/**
	 * Resolves the registered modifier identity for a survival status.
	 * Unknown status names deliberately return an invalid tag so callers cannot
	 * silently introduce unregistered movement writers.
	 */
	EFPROJECTSYSTEMSGAMEPLAY_API FGameplayTag ForStatus(FName StatusName);
}
