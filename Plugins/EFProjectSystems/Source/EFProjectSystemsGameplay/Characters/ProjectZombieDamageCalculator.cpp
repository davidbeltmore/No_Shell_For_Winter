#include "Characters/ProjectZombieDamageCalculator.h"
#include "Actors/ACFCharacter.h"
#include "Components/ACFAbilitySystemComponent.h"
#include "Game/ACFFunctionLibrary.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Characters/ProjectActionMovementGateComponent.h"
#include "Game/ACFGASDamageCalculator.h"
#include "Characters/ProjectArrowImpactContext.h"
#include "Engine/World.h"

UACFDamageCalculation* UProjectZombieDamageCalculator::GetNative()
{
    if (!Native)
    {
        const auto* Handler=GetTypedOuter<UACFDamageHandlerComponent>();
        const auto* Gate=Handler?Handler->GetOwner()->FindComponentByClass<UProjectActionMovementGateComponent>():nullptr;
        if (Gate && Gate->GetNativeDamageCalculatorClass())
            Native=NewObject<UACFDamageCalculation>(this,Gate->GetNativeDamageCalculatorClass());
    }
    return Native;
}
float UProjectZombieDamageCalculator::CalculateFinalDamage_Implementation(const FACFDamageEvent& Event,TArray<FAttributeData>& Attributes)
{
    auto* Calculator=GetNative();return Calculator?Calculator->CalculateFinalDamage(Event,Attributes):Event.FinalDamage;
}
bool UProjectZombieDamageCalculator::IsCriticalDamage_Implementation(const FACFDamageEvent& Event)
{
    auto* Calculator=GetNative();return Calculator && Calculator->IsCriticalDamage(Event);
}

FGameplayTag UProjectZombieDamageCalculator::EvaluateHitResponseAction_Implementation(const FACFDamageEvent& Event,const TArray<FOnHitActionChances>& Responses)
{
    auto* Context=Event.DamageReceiver && Event.DamageReceiver->GetWorld()?Event.DamageReceiver->GetWorld()->GetSubsystem<UProjectArrowImpactContext>():nullptr;
    const bool Arrow=Context && Context->Consume(Event);
    auto* Calculator=GetNative();
    const FGameplayTag NativeResponse=Calculator?Calculator->EvaluateHitResponseAction(Event,Responses):FGameplayTag();
    // Block/parry/heavy responses always retain the native result.
    if (NativeResponse.IsValid() && NativeResponse!=UACFFunctionLibrary::GetDefaultHitState()) return NativeResponse;
    const auto* Character=Cast<AACFCharacter>(Event.DamageReceiver);
    auto* GAS=Cast<UACFGASDamageCalculator>(Calculator);
    const float Damage=GAS?GAS->Internal_CalculateDamage(Event):0;
    if (!Character || !Character->IsAlive() || Character->GetDamageHandlerComponent()->GetIsImmortal() || !Event.DamageDealer || Damage<=0 || (!Arrow && Damage>MaximumLightHitDamage)) return NativeResponse;
    const FGameplayTag Light=FGameplayTag::RequestGameplayTag(TEXT("Actions.HitLight"));
    auto* ASC=Character->GetActionsComponent();
    return ASC && ASC->CanExecuteAbility(Light)?Light:NativeResponse;
}
