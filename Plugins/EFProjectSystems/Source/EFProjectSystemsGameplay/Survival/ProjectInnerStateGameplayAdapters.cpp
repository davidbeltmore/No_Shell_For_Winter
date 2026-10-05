#include "Survival/ProjectInnerStateGameplayAdapters.h"
#include "ACFStatisticsSet.h"

UProjectInnerStateStaminaEffect::UProjectInnerStateStaminaEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = GetDefault<UACFStatisticsSet>()->StaminaRegenAttribute();
    Modifier.ModifierOp = EGameplayModOp::Multiplicitive;
    FSetByCallerFloat Magnitude;
    Magnitude.DataName = TEXT("InnerState.StaminaRecovery");
    Modifier.ModifierMagnitude = Magnitude;
    Modifiers.Add(Modifier);
}
