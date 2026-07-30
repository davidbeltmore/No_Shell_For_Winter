#pragma once

#include "CoreMinimal.h"
#include "EFCharacterCreationTypes.h"
#include "GameplayTagContainer.h"

class APawn;
class UUserWidget;

DECLARE_MULTICAST_DELEGATE_TwoParams(FEFCharacterCreationSetPawnCanMoveHook, APawn* /* Pawn */, bool /* bCanMove */);
DECLARE_MULTICAST_DELEGATE_OneParam(FEFCharacterCreationCancelPawnAbilitiesHook, APawn* /* Pawn */);
DECLARE_MULTICAST_DELEGATE_FourParams(FEFCharacterCreationIdentityChangedHook, APawn* /* Pawn */, const FString& /* CharacterName */, ECharacterCreationGender /* Gender */, FGameplayTag /* GenderTag */);
DECLARE_MULTICAST_DELEGATE_OneParam(FEFCharacterCreationWidgetReadyHook, UUserWidget* /* Widget */);

namespace EFCharacterCreationGameplayHooks
{
	EFCHARACTERCREATIONRUNTIME_API FEFCharacterCreationSetPawnCanMoveHook& OnSetPawnCanMove();
	EFCHARACTERCREATIONRUNTIME_API FEFCharacterCreationCancelPawnAbilitiesHook& OnCancelPawnAbilities();
	EFCHARACTERCREATIONRUNTIME_API FEFCharacterCreationIdentityChangedHook& OnIdentityChanged();
	EFCHARACTERCREATIONRUNTIME_API FEFCharacterCreationWidgetReadyHook& OnWidgetReady();
}
