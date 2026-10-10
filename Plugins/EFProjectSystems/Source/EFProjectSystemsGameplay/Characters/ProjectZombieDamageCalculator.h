#pragma once
#include "Game/ACFDamageCalculation.h"
#include "ProjectZombieDamageCalculator.generated.h"

/** Adds a non-staggering flinch; retains native damage, defense and strong reactions. */
UCLASS(Blueprintable,Config=Game)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectZombieDamageCalculator : public UACFDamageCalculation
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditDefaultsOnly, Category="Animation|Hit Calibration",meta=(ClampMin="0"))
    float MaximumLightHitDamage=25.f;
    virtual FGameplayTag EvaluateHitResponseAction_Implementation(const FACFDamageEvent& Event,const TArray<FOnHitActionChances>& Responses) override;
    virtual float CalculateFinalDamage_Implementation(const FACFDamageEvent& Event,TArray<FAttributeData>& Attributes) override;
    virtual bool IsCriticalDamage_Implementation(const FACFDamageEvent& Event) override;
private:
    UACFDamageCalculation* GetNative();
    UPROPERTY(Transient) TObjectPtr<UACFDamageCalculation> Native;
};
