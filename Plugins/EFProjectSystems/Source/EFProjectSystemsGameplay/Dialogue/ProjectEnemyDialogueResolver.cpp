#include "Dialogue/ProjectEnemyDialogueResolver.h"

#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "Dialogue/ProjectEnemyDialogueChronicles.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace ProjectEnemyDialogueResolver
{
	namespace
	{
		struct FNeutralDialogueBuckets
		{
			TArray<FString> Solo;
			TArray<FString> Group;
		};

		const FNeutralDialogueBuckets GMeleeDialogue = {
			{
				TEXT("You won't get past me."),
				TEXT("Stand and fight."),
				TEXT("I've been waiting for this."),
				TEXT("Your path ends here."),
				TEXT("Come closer and face me."),
				TEXT("You're in my reach."),
				TEXT("No more running."),
				TEXT("I'll break your guard."),
			},
			{
				TEXT("Cut off the escape."),
				TEXT("Keep the pressure on."),
				TEXT("Close the formation."),
				TEXT("Don't let the target through."),
				TEXT("Surround the intruder."),
				TEXT("Drive the target back."),
				TEXT("Hold the line."),
				TEXT("Move in together."),
			}
		};

		const FNeutralDialogueBuckets GRangedDialogue = {
			{
				TEXT("I've got a clear shot."),
				TEXT("You can't outrun this."),
				TEXT("Stay in my sights."),
				TEXT("One clean hit is all I need."),
				TEXT("Keep moving. It won't help."),
				TEXT("I've marked the target."),
				TEXT("There's nowhere to hide."),
				TEXT("I won't miss twice."),
			},
			{
				TEXT("Cover every exit."),
				TEXT("Keep the target exposed."),
				TEXT("Crossfire, now."),
				TEXT("Watch the flanks."),
				TEXT("Force the target into the open."),
				TEXT("Maintain firing distance."),
				TEXT("Pin the intruder down."),
				TEXT("Take the shot when it opens."),
			}
		};

		const FNeutralDialogueBuckets GMageDialogue = {
			{
				TEXT("The curse has found you."),
				TEXT("Your resolve will be tested."),
				TEXT("The veil closes around you."),
				TEXT("You cannot resist forever."),
				TEXT("Power answers my call."),
				TEXT("Your defenses are fading."),
				TEXT("The relic knows your name."),
				TEXT("Step into the circle."),
			},
			{
				TEXT("Bind the intruder."),
				TEXT("Complete the warding circle."),
				TEXT("Do not break concentration."),
				TEXT("Channel the curse together."),
				TEXT("Seal every path."),
				TEXT("The target is weakening."),
				TEXT("Hold the ritual line."),
				TEXT("Now, focus the spell."),
			}
		};

		const FNeutralDialogueBuckets GFallbackDialogue = {
			{
				TEXT("There you are."),
				TEXT("You chose the wrong path."),
				TEXT("This area is restricted."),
				TEXT("Your luck just ran out."),
				TEXT("Turn back while you can."),
				TEXT("I see you."),
				TEXT("You're not leaving easily."),
				TEXT("Let's finish this."),
			},
			{
				TEXT("Don't let the intruder escape."),
				TEXT("Move to intercept."),
				TEXT("Stay together."),
				TEXT("Block the corridor."),
				TEXT("We have the advantage."),
				TEXT("Keep the target contained."),
				TEXT("Advance on my signal."),
				TEXT("End this quickly."),
			}
		};

		const FNeutralDialogueBuckets& ResolveNeutralBuckets(
			const EProjectEnemyDialogueArchetype Archetype)
		{
			switch (Archetype)
			{
			case EProjectEnemyDialogueArchetype::Melee:
				return GMeleeDialogue;
			case EProjectEnemyDialogueArchetype::Ranged:
				return GRangedDialogue;
			case EProjectEnemyDialogueArchetype::Mage:
				return GMageDialogue;
			default:
				return GFallbackDialogue;
			}
		}

		FString PickNeutralLine(const TArray<FString>& Pool)
		{
			return Pool.IsEmpty()
				? FString(TEXT("There you are."))
				: Pool[FMath::RandHelper(Pool.Num())];
		}

		bool ResolveStreamerSafeForced(const AActor* EnemyActor)
		{
			const UWorld* World = EnemyActor ? EnemyActor->GetWorld() : nullptr;
			const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			const UProjectContentPolicySubsystem* Policy = GameInstance
				? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
				: nullptr;
			// A missing runtime policy fails closed to the neutral pool.
			return !Policy || Policy->IsStreamerSafeForced();
		}
	}

	EProjectEnemyDialogueArchetype ResolveArchetype(const AActor* EnemyActor)
	{
		const FString ClassName = EnemyActor ? EnemyActor->GetClass()->GetName() : FString();
		if (ClassName.Contains(TEXT("Mage"), ESearchCase::IgnoreCase))
		{
			return EProjectEnemyDialogueArchetype::Mage;
		}
		if (ClassName.Contains(TEXT("Ranged"), ESearchCase::IgnoreCase))
		{
			return EProjectEnemyDialogueArchetype::Ranged;
		}
		if (ClassName.Contains(TEXT("Melee"), ESearchCase::IgnoreCase))
		{
			return EProjectEnemyDialogueArchetype::Melee;
		}
		return EProjectEnemyDialogueArchetype::Fallback;
	}

	bool ShouldUseOriginalBark(
		const bool bStreamerSafeForced,
		const float OriginalBarkRoll)
	{
		return !bStreamerSafeForced
			&& FMath::IsFinite(OriginalBarkRoll)
			&& OriginalBarkRoll >= 0.0f
			&& OriginalBarkRoll <= 1.0f
			&& OriginalBarkRoll < 0.10f;
	}

	FString PickSightBarkForRoll(
		const AActor* EnemyActor,
		const bool bGroupBark,
		const bool bStreamerSafeForced,
		const float OriginalBarkRoll)
	{
		if (ShouldUseOriginalBark(bStreamerSafeForced, OriginalBarkRoll))
		{
			return ProjectEnemyDialogueChronicles::PickSightBark(EnemyActor, bGroupBark);
		}

		const FNeutralDialogueBuckets& Buckets = ResolveNeutralBuckets(ResolveArchetype(EnemyActor));
		return PickNeutralLine(bGroupBark ? Buckets.Group : Buckets.Solo);
	}

	FString PickSightBark(const AActor* EnemyActor, const bool bGroupBark)
	{
		return PickSightBarkForRoll(
			EnemyActor,
			bGroupBark,
			ResolveStreamerSafeForced(EnemyActor),
			FMath::FRand());
	}
}
