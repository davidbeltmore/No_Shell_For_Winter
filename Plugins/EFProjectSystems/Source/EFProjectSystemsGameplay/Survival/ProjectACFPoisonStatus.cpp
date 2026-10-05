#include "Survival/ProjectACFPoisonStatus.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "GameFramework/Character.h"

UProjectACFPoisonStatus::UProjectACFPoisonStatus(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) { bCanBeRetriggered = true; }
void UProjectACFPoisonStatus::OnStatusEffectStarts_Implementation(ACharacter* Character)
{
    // Do not invoke the DOT parent: it emits damage before incubation.
    if (auto* Inner = UProjectInnerStateComponent::Ensure(Character))
    {
        Inner->OnInnerStateChanged.AddUniqueDynamic(this, &ThisClass::HandleInnerStateChanged);
        AddExposure();
    }
}
void UProjectACFPoisonStatus::OnStatusRetriggered_Implementation() { AddExposure(); }
void UProjectACFPoisonStatus::AddExposure()
{
    if (auto* Inner = UProjectInnerStateComponent::Ensure(GetCharacterOwner()))
        Inner->AddPoison(DosePoints, PoisonSpeed, Toxin, FGuid::NewGuid());
}
void UProjectACFPoisonStatus::HandleInnerStateChanged()
{
    if (const auto* Inner = GetCharacterOwner() ? GetCharacterOwner()->FindComponentByClass<UProjectInnerStateComponent>() : nullptr)
        if (Inner->GetValue(TEXT("Poison")) <= 0) EndEffect();
}
void UProjectACFPoisonStatus::OnStatusEffectEnds_Implementation()
{
    if (auto* Inner = GetCharacterOwner() ? GetCharacterOwner()->FindComponentByClass<UProjectInnerStateComponent>() : nullptr)
        Inner->OnInnerStateChanged.RemoveDynamic(this, &ThisClass::HandleInnerStateChanged);
    Super::OnStatusEffectEnds_Implementation();
}
