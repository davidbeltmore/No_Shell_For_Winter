#include "EFCharacterCreationACFUBridge.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"

void FEFCharacterCreationACFUBridge::SetPawnCanMove(APawn* Pawn, bool bCanMove)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	for (UActorComponent* ActorComponent : Pawn->GetComponents())
	{
		if (!IsValid(ActorComponent))
		{
			continue;
		}

		if (ActorComponent->GetClass()->GetName().Contains(TEXT("ACFCharacterMovementComponent")))
		{
			if (UFunction* SetCanMoveFunction = ActorComponent->FindFunction(TEXT("SetCanMove")))
			{
				struct FSetCanMoveParams
				{
					bool bCanMove = true;
				};

				FSetCanMoveParams Params;
				Params.bCanMove = bCanMove;
				ActorComponent->ProcessEvent(SetCanMoveFunction, &Params);
			}
		}
	}
}

void FEFCharacterCreationACFUBridge::CancelPawnAbilities(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	if (IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = AbilitySystemInterface->GetAbilitySystemComponent())
		{
			AbilitySystemComponent->CancelAllAbilities();
		}
	}
}
