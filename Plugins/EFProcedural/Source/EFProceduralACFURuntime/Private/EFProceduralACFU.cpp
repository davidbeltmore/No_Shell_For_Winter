#include "EFProceduralACFU.h"

#include "EFProceduralSettings.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace EFProceduralACFUPrivate
{
	static bool MatchesAnyHint(const FString& Source, const TArray<FString>& Hints)
	{
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && Source.Contains(Hint, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static bool MatchesClassHints(const UClass* ActorClass, const TArray<FString>& PathHints, const TArray<FString>& NameHints)
	{
		if (!IsValid(ActorClass))
		{
			return false;
		}

		const FString ClassPath = ActorClass->GetPathName();
		return MatchesAnyHint(ClassPath, PathHints)
			|| MatchesAnyHint(ActorClass->GetName(), NameHints);
	}
}

bool FEFProceduralACFU::LooksLikeSupportedEnemyPawn(const APawn* Pawn, const UEFProceduralSettings* Settings)
{
	if (!IsValid(Pawn) || !Settings)
	{
		return false;
	}

	return EFProceduralACFUPrivate::MatchesClassHints(
		Pawn->GetClass(),
		Settings->GetEnemyClassPathHintsResolved(),
		Settings->GetEnemyClassNameHintsResolved());
}

void FEFProceduralACFU::ApplyFallbackAIControllerClass(APawn* Pawn, const UEFProceduralSettings* Settings)
{
	if (!IsValid(Pawn) || !Settings || Pawn->AIControllerClass)
	{
		return;
	}

	const UClass* PawnClass = Pawn->GetClass();
	if (EFProceduralACFUPrivate::MatchesClassHints(
		PawnClass,
		Settings->GetRangedEnemyClassPathHintsResolved(),
		Settings->GetRangedEnemyClassNameHintsResolved()))
	{
		if (UClass* RangedControllerClass = Settings->GetRangedAIControllerClassResolved().LoadSynchronous())
		{
			Pawn->AIControllerClass = RangedControllerClass;
		}
		return;
	}

	if (EFProceduralACFUPrivate::MatchesClassHints(
		PawnClass,
		Settings->GetMeleeEnemyClassPathHintsResolved(),
		Settings->GetMeleeEnemyClassNameHintsResolved()))
	{
		if (UClass* MeleeControllerClass = Settings->GetMeleeAIControllerClassResolved().LoadSynchronous())
		{
			Pawn->AIControllerClass = MeleeControllerClass;
		}
	}
}
