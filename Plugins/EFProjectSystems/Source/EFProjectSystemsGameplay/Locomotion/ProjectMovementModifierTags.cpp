#include "Locomotion/ProjectMovementModifierTags.h"

namespace
{
	FGameplayTag ResolveRegisteredTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}
}

const FGameplayTag& ProjectMovementModifierTags::DoctrineCelerity()
{
	static const FGameplayTag Tag =
		ResolveRegisteredTag(TEXT("Project.Movement.Modifier.Doctrine.Celerity"));
	return Tag;
}

const FGameplayTag& ProjectMovementModifierTags::DoctrineRecoveredMomentum()
{
	static const FGameplayTag Tag =
		ResolveRegisteredTag(TEXT("Project.Movement.Modifier.Doctrine.RecoveredMomentum"));
	return Tag;
}

FGameplayTag ProjectMovementModifierTags::ForStatus(const FName StatusName)
{
	if (StatusName.IsNone())
	{
		return FGameplayTag();
	}

	const FString TagName =
		FString::Printf(TEXT("Project.Movement.Modifier.Status.%s"), *StatusName.ToString());
	return FGameplayTag::RequestGameplayTag(FName(*TagName), false);
}
