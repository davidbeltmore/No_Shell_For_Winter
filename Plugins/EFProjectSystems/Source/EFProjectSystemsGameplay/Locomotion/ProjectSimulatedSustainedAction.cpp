#include "Locomotion/ProjectSimulatedSustainedAction.h"
#include "Engine/World.h"

void UProjectSimulatedSustainedAction::Internal_OnActivated(UACFAbilitySystemComponent* ActionManager, UAnimMontage* Montage)
{
    Super::Internal_OnActivated(ActionManager, Montage);
    RemoveClockDelegates();
    if (const UWorld* World=GetWorld())
    {
        ActivationGameSeconds=World->GetTimeSeconds();
        // Before input can release the action; world end also runs during pause.
        BeforeActorsHandle=FWorldDelegates::OnWorldPreActorTick.AddUObject(this,&ThisClass::SynchronizeClock);
        AfterWorldHandle=FWorldDelegates::OnWorldTickEnd.AddUObject(this,&ThisClass::SynchronizeClock);
    }
}

void UProjectSimulatedSustainedAction::SynchronizeClock(UWorld* World, ELevelTick, float)
{
    if (World && World==GetWorld() && IsActive())
        startTime=static_cast<float>(World->GetRealTimeSeconds())-
            static_cast<float>(FMath::Max(0.,World->GetTimeSeconds()-ActivationGameSeconds));
}

void UProjectSimulatedSustainedAction::RemoveClockDelegates()
{
    FWorldDelegates::OnWorldPreActorTick.Remove(BeforeActorsHandle);
    FWorldDelegates::OnWorldTickEnd.Remove(AfterWorldHandle);
    BeforeActorsHandle.Reset(); AfterWorldHandle.Reset();
}

void UProjectSimulatedSustainedAction::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    RemoveClockDelegates();
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}

void UProjectSimulatedSustainedAction::BeginDestroy()
{
    RemoveClockDelegates();
    Super::BeginDestroy();
}
