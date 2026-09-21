#pragma once

#include "CoreMinimal.h"

class UClass;

/**
 * Immutable CDO-derived collision facts used before content Chance/Amount/Weight.
 * The placement pipeline deliberately uses a conservative root-component box: it
 * prevents a selected actor from discovering a larger spawn footprint only after
 * the transaction has committed its random decision.
 */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentCollisionContract
{
	FSoftObjectPath ActorClassPath;
	FString RootComponentClassPath;
	/** Exact CDO root-relative transform composed by UE before DontSpawnIfColliding. */
	FTransform RootRelativeTransform = FTransform::Identity;
	FName CollisionProfile;
	ECollisionChannel ObjectType = ECC_Pawn;
	uint8 CollisionEnabled = 0;
	uint8 PawnResponse = 0;
	/** Valid only when UE's CDO preflight has query participants. It is their conservative union in actor-local coordinates. */
	FBox LocalRootBounds = FBox(ForceInit);
	bool bHasQueryCollision = false;
	/** Conservative union of every finite nonzero CDO primitive bound in actor-local coordinates.
	 * It reserves the complete generated component envelope, not merely collision. */
	FBox LocalPrimitiveBounds = FBox(ForceInit);
	bool bHasPrimitiveBounds = false;
	/** Canonical, order-independent signature of UE's query participants and responses. */
	FString QueryParticipantsHash;
	FString Hash;

	bool IsValid() const
	{
		return ActorClassPath.IsValid() && !Hash.IsEmpty()
			&& (!bHasQueryCollision || (LocalRootBounds.IsValid && !LocalRootBounds.Min.ContainsNaN() && !LocalRootBounds.Max.ContainsNaN()));
	}
};

/** Builds a bounded, read-only CDO descriptor. It never loads a class and never spawns an actor. */
struct EFPROCEDURALRUNTIME_API FEFCalystoContentCollisionContracts final
{
	static bool Build(UClass* ActorClass, FEFCalystoContentCollisionContract& OutContract, FString& OutError);
};
