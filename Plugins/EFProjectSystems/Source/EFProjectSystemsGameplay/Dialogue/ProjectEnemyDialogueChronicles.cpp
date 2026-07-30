#include "Dialogue/ProjectEnemyDialogueChronicles.h"

#include "GameFramework/Actor.h"

namespace ProjectEnemyDialogueChronicles
{
	namespace
	{
		struct FProjectEnemyDialogueBuckets
		{
			TArray<FString> Solo;
			TArray<FString> Group;
		};

		// Keep all editable sight-bark text in this file so narrative updates only need one CPP.
		const FProjectEnemyDialogueBuckets GMeleeDialogue = {
			{
				TEXT("You're mine now."),
				TEXT("Come here, you fucking whore."),
				TEXT("I'll break you."),
				TEXT("This bitch belongs to me."),
				TEXT("I'm gonna fuck you senseless."),
				TEXT("You're my fresh meat."),
				TEXT("I'll tear you apart."),
				TEXT("This cunt is mine."),
				TEXT("I'm gonna ruin you."),
				TEXT("I'll make you scream.")
			},
			{
				TEXT("Let's fuck this bitch."),
				TEXT("Don't let her get away."),
				TEXT("Fresh meat for us."),
				TEXT("She's all ours now."),
				TEXT("Hold her down, boys."),
				TEXT("Let's tear her apart."),
				TEXT("Pin the whore."),
				TEXT("We're gonna destroy her."),
				TEXT("Let's fuck and ruin her."),
				TEXT("Surround the whore.")
			}
		};

		const FProjectEnemyDialogueBuckets GRangedDialogue = {
			{
				TEXT("You're not escaping me."),
				TEXT("This body is mine to destroy."),
				TEXT("I'll fuck your brains out."),
				TEXT("You're my plaything now."),
				TEXT("I'll rip you open."),
				TEXT("I’ll choke you while I fuck."),
				TEXT("Perfect little whore for me."),
				TEXT("I'll make you bleed."),
				TEXT("You're dead after I use you."),
				TEXT("I'm gonna wreck you.")
			},
			{
				TEXT("We all take turns."),
				TEXT("No escaping our cocks."),
				TEXT("She's gonna get wrecked."),
				TEXT("Let's make her suffer."),
				TEXT("Fresh fuck for the pack."),
				TEXT("Don't stop till she breaks."),
				TEXT("Hell’s new cumdump."),
				TEXT("Let's beat and fuck her."),
				TEXT("She's gonna choke on it."),
				TEXT("Grab her, spread her.")
			}
		};

		const FProjectEnemyDialogueBuckets GMageDialogue = {
			{
				TEXT("Your holes are mine."),
				TEXT("Kneel and take it, whore."),
				TEXT("I'll fuck the hope out of you."),
				TEXT("This sinner belongs to my cock."),
				TEXT("You're going to beg for more."),
				TEXT("I'll break you with every thrust."),
				TEXT("Your screams will echo in hell."),
				TEXT("Time to claim every inch of you."),
				TEXT("You're my personal fucktoy."),
				TEXT("I'll make you bleed while I fuck.")
			},
			{
				TEXT("Let's fuck her to death."),
				TEXT("Don't kill her. She must live for us."),
				TEXT("Perfect hole for all of us."),
				TEXT("Destroy this bitch."),
				TEXT("Time to get violent with her."),
				TEXT("Let’s make her take all our cocks."),
				TEXT("She’s ours."),
				TEXT("Surround her and ruin her."),
				TEXT("This whore won’t last long."),
				TEXT("She’s ours to destroy.")
			}
		};

		const FProjectEnemyDialogueBuckets GFallbackDialogue = {
			{
				TEXT("There you are, bitch."),
				TEXT("You’re mine now."),
				TEXT("Come here you fucking slut."),
				TEXT("I found fresh meat."),
				TEXT("This cunt is all mine."),
				TEXT("You’re not leaving until I’m done."),
				TEXT("I’ll fuck you until you break."),
				TEXT("Look at you… my new toy."),
				TEXT("You’re already wet for me."),
				TEXT("I’ll make you scream my name.")
			},
			{
				TEXT("Don’t let the slut escape."),
				TEXT("She’s all ours now."),
				TEXT("Let’s tear this bitch apart."),
				TEXT("Grab her and spread her."),
				TEXT("We all take turns on her."),
				TEXT("Let’s beat and fuck her."),
				TEXT("She’s gonna choke on our cocks."),
				TEXT("Fresh whore for the pack."),
				TEXT("This bitch won’t last long."),
				TEXT("She’s ours to destroy.")
			}
		};

		const FProjectEnemyDialogueBuckets& ResolveDialogueBuckets(const AActor* EnemyActor)
		{
			const FString ClassName = EnemyActor ? EnemyActor->GetClass()->GetName() : FString();
			if (ClassName.Contains(TEXT("Mage"), ESearchCase::IgnoreCase))
			{
				return GMageDialogue;
			}

			if (ClassName.Contains(TEXT("Ranged"), ESearchCase::IgnoreCase))
			{
				return GRangedDialogue;
			}

			if (ClassName.Contains(TEXT("Melee"), ESearchCase::IgnoreCase))
			{
				return GMeleeDialogue;
			}

			return GFallbackDialogue;
		}

		FString PickRandomLine(const TArray<FString>& Pool, const TCHAR* FallbackLine)
		{
			if (Pool.Num() <= 0)
			{
				return FString(FallbackLine);
			}

			return Pool[FMath::RandHelper(Pool.Num())];
		}
	}

	FString PickSightBark(const AActor* EnemyActor, const bool bGroupBark)
	{
		const FProjectEnemyDialogueBuckets& Buckets = ResolveDialogueBuckets(EnemyActor);
		const TArray<FString>& Pool = bGroupBark ? Buckets.Group : Buckets.Solo;
		return PickRandomLine(Pool, TEXT("There you are."));
	}
}