#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Game/ACFDamageType.h"
#include "ProjectInnerStateGameplayAdapters.generated.h"

/** Project-owned GAS modifier; never edits or replaces ACF's regeneration. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateStaminaEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UProjectInnerStateStaminaEffect();
};

/** Assign to blunt/concussive weapon damage profiles. Armor-reduced damage drives shock. */
UCLASS(Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectConcussiveDamageType : public UACFDamageType
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inner State", meta=(ClampMin="0")) float AwarenessPerHealthPercent = 2.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Inner State", meta=(ClampMin="1")) float HeadMultiplier = 1.5f;
};
