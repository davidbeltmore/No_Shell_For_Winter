#pragma once
#include "Actions/ACFSustainedAction.h"
#include "ProjectSimulatedSustainedAction.generated.h"

/** Keeps the existing ACF release payload in simulation seconds, including pause. */
UCLASS(Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSimulatedSustainedAction : public UACFSustainedAction
{
    GENERATED_BODY()
protected:
    virtual void Internal_OnActivated(UACFAbilitySystemComponent* ActionManager, UAnimMontage* Montage) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
    virtual void BeginDestroy() override;
private:
    void SynchronizeClock(UWorld* World, ELevelTick TickType, float DeltaSeconds);
    void RemoveClockDelegates();
    double ActivationGameSeconds = 0.;
    FDelegateHandle BeforeActorsHandle;
    FDelegateHandle AfterWorldHandle;
};
