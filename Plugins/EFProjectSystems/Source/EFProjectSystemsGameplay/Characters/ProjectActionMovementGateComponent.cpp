#include "Characters/ProjectActionMovementGateComponent.h"
#include "Actions/ACFActionAbility.h"
#include "Actions/ACFHitAction.h"
#include "AIController.h"
#include "Components/ACFAbilitySystemComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/ProjectZombieDamageCalculator.h"
#include "Game/ACFGASDamageCalculator.h"
#include "UObject/UnrealType.h"

UProjectActionMovementGateComponent::UProjectActionMovementGateComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    PlantedActionTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Actions.Hit")));
    RequiredActionClass=UACFHitAction::StaticClass();
}
void UProjectActionMovementGateComponent::EnsureZombiePolicy(APawn* Pawn)
{
    if (!Pawn || !Pawn->HasAuthority() || !Pawn->ActorHasTag(TEXT("EF.NPC.Zombie")) ||
        Pawn->FindComponentByClass<UProjectActionMovementGateComponent>()) return;
    auto* Gate=NewObject<UProjectActionMovementGateComponent>(Pawn);
    Pawn->AddInstanceComponent(Gate);
    Gate->RegisterComponent();
    if (auto* Damage=Pawn->FindComponentByClass<UACFDamageHandlerComponent>())
    {
        // Preserve the actual configured BP class, including its GAS attribute mappings.
        const auto* Property=FindFProperty<FClassProperty>(Damage->GetClass(),TEXT("DamageCalculatorClass"));
        UClass* NativeClass=Property?Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Damage)):nullptr;
        if (NativeClass && NativeClass->IsChildOf(UACFGASDamageCalculator::StaticClass()))
        {
            Gate->NativeDamageCalculatorClass=NativeClass;
            Damage->SetDamageCalculatorClass(UProjectZombieDamageCalculator::StaticClass());
        }
    }
}
void UProjectActionMovementGateComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;
    Actions=GetOwner()->FindComponentByClass<UACFAbilitySystemComponent>();
    if (Actions.IsValid())
    {
        Actions->OnAbilityStartedEvent.AddDynamic(this,&ThisClass::ActionStarted);
        Actions->OnAbilityFinishedEvent.AddDynamic(this,&ThisClass::ActionFinished);
    }
}
void UProjectActionMovementGateComponent::ActionStarted(FGameplayTag Tag)
{
    const auto* Action=Actions.IsValid()?Actions->GetCurrentAction():nullptr;
    if (!Action || !PlantedActionTags.HasTagExact(Tag) ||
        (RequiredActionClass && !Action->IsA(RequiredActionClass))) return;
    auto* Character=Cast<ACharacter>(GetOwner());
    auto* AI=Character?Cast<AAIController>(Character->GetController()):nullptr;
    if (!Character || !AI) return;
    // Pause, don't abort: retain AITask_MoveTo's request and completion delegate.
    auto* Path=AI->GetPathFollowingComponent();
    if (Path && Path->GetStatus()==EPathFollowingStatus::Moving)
    {
        PausedRequest=Path->GetCurrentRequestId();
        PausedPath=Path;
        Path->PauseMove(PausedRequest,EPathFollowingVelocityMode::Reset);
    }
    HeldActionTag=Tag;
    bWaitingForVisualEnd=false;
    if (ReactionAnimation.IsValid()) ReactionAnimation->OnMontageEnded.RemoveDynamic(this,&ThisClass::MontageEnded);
    ReactionAnimation=Character->GetMesh()->GetAnimInstance();
    ReactionMontage=Action->GetAnimMontage();
    if (ReactionAnimation.IsValid()) ReactionAnimation->OnMontageEnded.AddDynamic(this,&ThisClass::MontageEnded);
    Character->GetCharacterMovement()->StopMovementImmediately();
    Character->GetCharacterMovement()->StopActiveMovement();
    // Acceleration-based path following can already have queued pawn input for
    // this frame. Stopping velocity alone leaves that input to reaccelerate it.
    Character->ConsumeMovementInputVector();
    // Root motion and movement mode remain native; only AI translation is held.
}
void UProjectActionMovementGateComponent::ActionFinished(FGameplayTag Tag)
{
    if (Tag!=HeldActionTag) return;
    // ACF ends the ability at blend-out START. Keep the planted visual tail still.
    bWaitingForVisualEnd=ReactionAnimation.IsValid() && ReactionMontage.IsValid() &&
        ReactionAnimation->Montage_IsActive(ReactionMontage.Get());
    if (!bWaitingForVisualEnd) Release();
}
void UProjectActionMovementGateComponent::MontageEnded(UAnimMontage* Montage,bool bInterrupted)
{
    if (bWaitingForVisualEnd && Montage==ReactionMontage.Get()) Release();
}
void UProjectActionMovementGateComponent::Release()
{
    auto* Character=Cast<ACharacter>(GetOwner());
    auto* AI=Character?Cast<AAIController>(Character->GetController()):nullptr;
    const auto* Damage=Character?Character->FindComponentByClass<UACFDamageHandlerComponent>():nullptr;
    // Resume only our live request. Death and a replacement controller/path win.
    if (AI && (!Damage || Damage->GetIsAlive()) && PausedPath.IsValid() &&
        AI->GetPathFollowingComponent()==PausedPath.Get() &&
        PausedPath->GetCurrentRequestId()==PausedRequest &&
        PausedPath->GetStatus()==EPathFollowingStatus::Paused)
        PausedPath->ResumeMove(PausedRequest);
    PausedPath.Reset();PausedRequest=FAIRequestID::InvalidRequest;HeldActionTag=FGameplayTag();
    bWaitingForVisualEnd=false;ReactionMontage.Reset();
}
void UProjectActionMovementGateComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Actions.IsValid())
    {
        Actions->OnAbilityStartedEvent.RemoveDynamic(this,&ThisClass::ActionStarted);
        Actions->OnAbilityFinishedEvent.RemoveDynamic(this,&ThisClass::ActionFinished);
    }
    // Never resume navigation while the pawn/world is being destroyed.
    if (ReactionAnimation.IsValid()) ReactionAnimation->OnMontageEnded.RemoveDynamic(this,&ThisClass::MontageEnded);
    PausedPath.Reset();Actions.Reset();HeldActionTag=FGameplayTag();
    Super::EndPlay(Reason);
}
