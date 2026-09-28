#include "Progression/ProjectAttributeSynergies.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Training/ProjectPracticeComponent.h"

float FProjectAttributeSynergies::PhysicalEffortIntensity(const AActor* Owner)
{
    const auto* Training = Owner ? Owner->FindComponentByClass<UProjectPracticeComponent>() : nullptr;
    if (!Training || !Training->IsTrainingActive()) return 0.f;
    const auto* Definition = GetDefault<UProjectPracticeSettings>()->Find(Training->GetActiveTrainingId());
    return Definition && FMath::IsFinite(Definition->PhysicalEffortIntensity)
        ? FMath::Clamp(Definition->PhysicalEffortIntensity, 0.f, 1.f) : 0.f;
}

float FProjectAttributeSynergies::EffectiveRank(const AActor* Owner, FName Attribute)
{
    const auto* Settings = GetDefault<UProjectPracticeSettings>();
    if (!Owner || !Settings->bEnabled) return 0.f;
    const auto* ASC = Owner->FindComponentByClass<UAbilitySystemComponent>();
    const auto Field = UProjectPracticeProgressionComponent::ResolveAttribute(Attribute);
    if (!ASC || !Field.IsValid() || !ASC->HasAttributeSetForAttribute(Field)) return 0.f;
    const float Value = ASC->GetNumericAttribute(Field);
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, static_cast<float>(Settings->MaximumAttributeRank)) : 0.f;
}

float FProjectAttributeSynergies::BoundedReduction(float Rank, float MaximumRank, float MaximumReduction)
{
    if (!FMath::IsFinite(Rank) || !FMath::IsFinite(MaximumRank) || !FMath::IsFinite(MaximumReduction)) return 1.f;
    return 1.f - FMath::Clamp(MaximumReduction, 0.f, 0.9f) * FMath::Clamp(Rank / FMath::Max(1.f, MaximumRank), 0.f, 1.f);
}

float FProjectAttributeSynergies::PainGainMultiplier(const AActor* Owner)
{
    const auto* S = GetDefault<UProjectPracticeSettings>();
    return BoundedReduction(EffectiveRank(Owner, TEXT("Constitution")), S->MaximumAttributeRank, S->ConstitutionMaximumPainReduction);
}

float FProjectAttributeSynergies::SweatGainMultiplier(const AActor* Owner)
{
    const auto* S = GetDefault<UProjectPracticeSettings>();
    return BoundedReduction(EffectiveRank(Owner, TEXT("Endurance")), S->MaximumAttributeRank, S->EnduranceMaximumSweatReduction);
}

int32 FProjectAttributeSynergies::IntelligenceLockpickBonus(const AActor* Owner)
{
    const auto* S = GetDefault<UProjectPracticeSettings>();
    if (!FMath::IsFinite(S->IntelligenceLockpickContribution)) return 0;
    return FMath::FloorToInt(FMath::Clamp(EffectiveRank(Owner, TEXT("Intelligence")) * S->IntelligenceLockpickContribution,
        0.f, static_cast<float>(FMath::Max(0, S->IntelligenceMaximumLockpickBonus))));
}
