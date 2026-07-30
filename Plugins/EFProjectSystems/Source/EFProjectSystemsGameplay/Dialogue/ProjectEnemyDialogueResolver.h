#pragma once

#include "CoreMinimal.h"

class AActor;

enum class EProjectEnemyDialogueArchetype : uint8
{
	Melee,
	Ranged,
	Mage,
	Fallback
};

/**
 * Exterior presentation resolver. Chronicles owns its original text byte for
 * byte; neutral pools and the probability gate live here.
 */
namespace ProjectEnemyDialogueResolver
{
	EFPROJECTSYSTEMSGAMEPLAY_API FString PickSightBark(
		const AActor* EnemyActor,
		bool bGroupBark);

	EFPROJECTSYSTEMSGAMEPLAY_API FString PickSightBarkForRoll(
		const AActor* EnemyActor,
		bool bGroupBark,
		bool bStreamerSafeForced,
		float OriginalBarkRoll);

	EFPROJECTSYSTEMSGAMEPLAY_API bool ShouldUseOriginalBark(
		bool bStreamerSafeForced,
		float OriginalBarkRoll);

	EFPROJECTSYSTEMSGAMEPLAY_API EProjectEnemyDialogueArchetype ResolveArchetype(
		const AActor* EnemyActor);
}
