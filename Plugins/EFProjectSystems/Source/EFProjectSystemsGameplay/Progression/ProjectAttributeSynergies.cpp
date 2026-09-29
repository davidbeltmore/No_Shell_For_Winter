#include "Progression/ProjectAttributeSynergies.h"
#include "Progression/ProjectPracticeSettings.h"
#include "Progression/ProjectPracticeProgressionComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Training/ProjectPracticeComponent.h"
#include "Components/ACFAbilitySystemComponent.h"
#include "Components/ACFLadderComponent.h"
#include "Actions/ACFAttackAction.h"
#include "Actions/ACFDirectionalDodgeAction.h"
#include "Actions/ACFShootAction.h"
#include "Interfaces/ACFEntityInterface.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

float FProjectAttributeSynergies::PhysicalEffortIntensity(const AActor* Owner)
{
    if (!IsValid(Owner) || (Owner->Implements<UACFEntityInterface>() && !IACFEntityInterface::Execute_IsEntityAlive(Owner))) return 0.f;
    const auto* Settings = GetDefault<UProjectPracticeSettings>();
    float Intensity = 0.f;
    const auto Include = [&Intensity](float Value)
    {
        if (FMath::IsFinite(Value)) Intensity = FMath::Max(Intensity, FMath::Clamp(Value, 0.f, 1.f));
    };
    if (const auto* Training = Owner->FindComponentByClass<UProjectPracticeComponent>(); Training && Training->IsTrainingActive())
        if (const auto* Definition = Settings->Find(Training->GetActiveTrainingId())) Include(Definition->PhysicalEffortIntensity);

    // Read the executing GAS action, never a pressed key, buffered action or montage.
    // No cached effort source survives cancellation, possession, death or load.
    if (const auto* Actions = Owner->FindComponentByClass<UACFAbilitySystemComponent>(); Actions && Actions->IsPerformingAction())
    {
        const auto* Action = Actions->GetCurrentAction();
        if (IsValid(Action) && Action->IsActive())
        {
            if (Action->IsA<UACFAttackAction>()) Include(Settings->AttackEffortIntensity);
            else if (Action->IsA<UACFDirectionalDodgeAction>()) Include(Settings->DodgeEffortIntensity);
            else if (Action->IsA<UACFShootAction>()) Include(Settings->ShootEffortIntensity);
            else if (Actions->GetCurrentActionTag().MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Actions.LadderClimb"), false)))
            {
                // Sample ladder abilities warp the actor directly: CharacterMovement
                // velocity remains zero while climbing. Count only an executing ladder
                // ability in custom movement, not idle attachment or held movement input.
                const auto* Character = Cast<ACharacter>(Owner);
                if (Character && Character->GetCharacterMovement()->MovementMode == MOVE_Custom)
                    Include(Settings->ClimbEffortIntensity);
            }
            // The authored bow charge/release use sustained Blueprint actions,
            // not UACFShootAction. Both were audited in AS_ACF_NewMMBowActions.
            else if (Actions->GetCurrentMovesetActionsTag().GetTagName() == TEXT("Moveset.Bow") &&
                (Actions->GetCurrentActionTag().GetTagName() == TEXT("Actions.Shoot") ||
                 Actions->GetCurrentActionTag().GetTagName() == TEXT("Actions.Release")))
                Include(Settings->ShootEffortIntensity);
        }
    }
    if (const auto* Character = Cast<ACharacter>(Owner))
    {
        const auto* Movement = Character->GetCharacterMovement();
        if (const auto* Ladder = Owner->FindComponentByClass<UACFLadderComponent>();
            Ladder && Ladder->IsLadderClimbing() && Ladder->CanClimb() && Movement &&
            Movement->MovementMode == MOVE_Custom && FMath::Abs(Owner->GetVelocity().Z) > 1.f)
            Include(Settings->ClimbEffortIntensity);
        if (const auto* Locomotion = Owner->FindComponentByClass<UProjectLocomotionOverrideComponent>();
            Locomotion && Locomotion->IsCrawlModeActive() && Movement && Movement->IsMovingOnGround() && Owner->GetVelocity().SizeSquared2D() > 1.f)
            Include(Settings->CrawlEffortIntensity);
    }
    // Concurrent physical activities use the strongest source, not summed sweat.
    return Intensity;
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
