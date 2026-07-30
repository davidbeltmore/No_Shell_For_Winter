#pragma once

#include "CoreMinimal.h"
#include "Combat/ProjectCombatTypes.h"

class AActor;

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatHitResolution
{
	bool bQualified = false;
	TWeakObjectPtr<AActor> ResolvedActor;
	FString MatchedHint;
	FString ResolutionReason;
	FString SourceSummary;
};

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatHitResolver
{
public:
	static bool DoesActorMatchClassHintsRecursive(const AActor* Actor, const TArray<FString>& Hints);
	static bool ResolveQualifiedEnemyActor(const FProjectIncomingHitContext& HitContext, const TArray<FString>& Hints, FProjectDefeatHitResolution& OutResolution);
	static FString DescribeActor(const AActor* Actor);

private:
	static bool DoesClassLineageContainAnyHint(const UClass* ActorClass, const TArray<FString>& Hints, FString* OutMatchedHint = nullptr);
};
