#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "ProjectActionMovementGateComponent.generated.h"

class UACFAbilitySystemComponent;
class UACFActionAbility;
class UAnimInstance;
class UAnimMontage;
class UACFDamageCalculation;

/** Opt-in planted-action policy. Leaves native ability, montage and BT ownership intact. */
UCLASS(ClassGroup=(Project), meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectActionMovementGateComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectActionMovementGateComponent();
    static void EnsureZombiePolicy(APawn* Pawn);
    UPROPERTY(EditAnywhere, Category="Animation|Movement")
    FGameplayTagContainer PlantedActionTags;
    UPROPERTY(EditAnywhere, Category="Animation|Movement")
    TSubclassOf<UACFActionAbility> RequiredActionClass;
    UFUNCTION(BlueprintPure, Category="Animation|Movement")
    bool IsHoldingNavigation() const { return HeldActionTag.IsValid(); }
    TSubclassOf<UACFDamageCalculation> GetNativeDamageCalculatorClass() const { return NativeDamageCalculatorClass; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void ActionStarted(FGameplayTag Tag);
    UFUNCTION() void ActionFinished(FGameplayTag Tag);
    UFUNCTION() void MontageEnded(UAnimMontage* Montage, bool bInterrupted);
    void Release();
    TWeakObjectPtr<UACFAbilitySystemComponent> Actions;
    TWeakObjectPtr<UPathFollowingComponent> PausedPath;
    FAIRequestID PausedRequest=FAIRequestID::InvalidRequest;
    FGameplayTag HeldActionTag;
    TWeakObjectPtr<UAnimInstance> ReactionAnimation;
    TWeakObjectPtr<UAnimMontage> ReactionMontage;
    bool bWaitingForVisualEnd=false;
    UPROPERTY(Transient) TSubclassOf<UACFDamageCalculation> NativeDamageCalculatorClass;
};
