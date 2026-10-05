#pragma once
#include "CoreMinimal.h"
#include "StatusEffects/ACFDamageOverTimeStatusEffect.h"
#include "Survival/ProjectInnerStateMath.h"
#include "ProjectACFPoisonStatus.generated.h"

/** Keeps ACF's public status/tag/cue contract while Inner State owns all damage.
 * The DOT parent is retained for existing BP property compatibility only. Its
 * timer is deliberately never started. Every native retrigger is a new dose. */
UCLASS(Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectACFPoisonStatus : public UACFDamageOverTimeStatusEffect
{
    GENERATED_BODY()
public:
    UProjectACFPoisonStatus(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") double DosePoints = 25.0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") EProjectPoisonSpeed PoisonSpeed = EProjectPoisonSpeed::Normal;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") FName Toxin = TEXT("ACF.Poison");
protected:
    virtual void OnStatusEffectStarts_Implementation(ACharacter* Character) override;
    virtual void OnStatusRetriggered_Implementation() override;
    virtual void OnStatusEffectEnds_Implementation() override;
private:
    void AddExposure();
    UFUNCTION() void HandleInnerStateChanged();
};
