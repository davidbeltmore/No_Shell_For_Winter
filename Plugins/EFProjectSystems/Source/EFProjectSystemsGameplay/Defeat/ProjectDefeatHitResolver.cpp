#include "Defeat/ProjectDefeatHitResolver.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"

namespace
{
	struct FProjectPendingResolvedActor
	{
		const AActor* Actor = nullptr;
		FString Route;
	};

	void EnqueueResolvedActor(
		TArray<FProjectPendingResolvedActor>& PendingActors,
		TSet<const AActor*>& QueuedActors,
		const AActor* Actor,
		const FString& Route)
	{
		if (!Actor || QueuedActors.Contains(Actor))
		{
			return;
		}

		QueuedActors.Add(Actor);
		PendingActors.Add({ Actor, Route });
	}
}

bool FProjectDefeatHitResolver::DoesActorMatchClassHintsRecursive(const AActor* Actor, const TArray<FString>& Hints)
{
	if (!Actor || Hints.IsEmpty())
	{
		return false;
	}

	FProjectIncomingHitContext HitContext;
	HitContext.SourceActor = const_cast<AActor*>(Actor);

	FProjectDefeatHitResolution Resolution;
	return ResolveQualifiedEnemyActor(HitContext, Hints, Resolution);
}

bool FProjectDefeatHitResolver::ResolveQualifiedEnemyActor(
	const FProjectIncomingHitContext& HitContext,
	const TArray<FString>& Hints,
	FProjectDefeatHitResolution& OutResolution)
{
	OutResolution = FProjectDefeatHitResolution();
	OutResolution.SourceSummary = FString::Printf(
		TEXT("Source=%s | Causer=%s | Instigator=%s | Owner=%s | AttachParent=%s"),
		*DescribeActor(HitContext.SourceActor.Get()),
		*DescribeActor(HitContext.DamageCauser.Get()),
		*DescribeActor(HitContext.InstigatorActor.Get()),
		*DescribeActor(HitContext.OwnerActor.Get()),
		*DescribeActor(HitContext.AttachParentActor.Get()));

	if (Hints.IsEmpty())
	{
		OutResolution.ResolutionReason = FString::Printf(TEXT("No class hints configured. %s"), *OutResolution.SourceSummary);
		return false;
	}

	TArray<FProjectPendingResolvedActor> PendingActors;
	TSet<const AActor*> QueuedActors;
	TSet<const AActor*> VisitedActors;

	EnqueueResolvedActor(PendingActors, QueuedActors, HitContext.SourceActor.Get(), TEXT("SourceActor"));
	EnqueueResolvedActor(PendingActors, QueuedActors, HitContext.DamageCauser.Get(), TEXT("DamageCauser"));
	EnqueueResolvedActor(PendingActors, QueuedActors, HitContext.InstigatorActor.Get(), TEXT("InstigatorActor"));
	EnqueueResolvedActor(PendingActors, QueuedActors, HitContext.OwnerActor.Get(), TEXT("OwnerActor"));
	EnqueueResolvedActor(PendingActors, QueuedActors, HitContext.AttachParentActor.Get(), TEXT("AttachParentActor"));

	while (PendingActors.Num() > 0)
	{
		const FProjectPendingResolvedActor Candidate = PendingActors[0];
		PendingActors.RemoveAt(0, 1, EAllowShrinking::No);

		if (!Candidate.Actor || VisitedActors.Contains(Candidate.Actor))
		{
			continue;
		}

		VisitedActors.Add(Candidate.Actor);

		FString MatchedHint;
		if (DoesClassLineageContainAnyHint(Candidate.Actor->GetClass(), Hints, &MatchedHint))
		{
			OutResolution.bQualified = true;
			OutResolution.ResolvedActor = const_cast<AActor*>(Candidate.Actor);
			OutResolution.MatchedHint = MatchedHint;
			OutResolution.ResolutionReason = FString::Printf(
				TEXT("Matched hint '%s' via %s -> %s"),
				*MatchedHint,
				*Candidate.Route,
				*DescribeActor(Candidate.Actor));
			return true;
		}

		if (const AController* Controller = Cast<AController>(Candidate.Actor))
		{
			EnqueueResolvedActor(PendingActors, QueuedActors, Controller->GetPawn(), Candidate.Route + TEXT(" -> ControllerPawn"));
		}

		EnqueueResolvedActor(PendingActors, QueuedActors, Candidate.Actor->GetOwner(), Candidate.Route + TEXT(" -> Owner"));
		EnqueueResolvedActor(PendingActors, QueuedActors, Candidate.Actor->GetInstigator(), Candidate.Route + TEXT(" -> Instigator"));
		EnqueueResolvedActor(PendingActors, QueuedActors, Candidate.Actor->GetAttachParentActor(), Candidate.Route + TEXT(" -> AttachParent"));
	}

	OutResolution.ResolutionReason = FString::Printf(TEXT("No qualified enemy match found. %s"), *OutResolution.SourceSummary);
	return false;
}

FString FProjectDefeatHitResolver::DescribeActor(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("None");
	}

	const UClass* ActorClass = Actor->GetClass();
	return FString::Printf(
		TEXT("%s (%s)"),
		*Actor->GetName(),
		ActorClass ? *ActorClass->GetName() : TEXT("NoClass"));
}

bool FProjectDefeatHitResolver::DoesClassLineageContainAnyHint(
	const UClass* ActorClass,
	const TArray<FString>& Hints,
	FString* OutMatchedHint)
{
	if (!ActorClass)
	{
		return false;
	}

	for (const UClass* CurrentClass = ActorClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		const FString ClassName = CurrentClass->GetName();
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && ClassName.Contains(Hint, ESearchCase::IgnoreCase))
			{
				if (OutMatchedHint)
				{
					*OutMatchedHint = Hint;
				}

				return true;
			}
		}
	}

	return false;
}
