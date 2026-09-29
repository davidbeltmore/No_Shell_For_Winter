#include "ProjectCompatibilityInspectionLibrary.h"
#include "Graph/AQSQuest.h"
#include "Graph/AQSObjectiveNode.h"
#include "AQSQuestObjective.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Progression/ProjectAttributeSynergies.h"
#include "ACFAbilitySet.h"
#include "ACFGameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "Locomotion/ProjectSimulatedSustainedAction.h"
#include "AGSAction.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

bool UProjectCompatibilityInspectionLibrary::IsNavigationFixtureReady(AActor* Context, AActor* NavigationData, FVector Location)
{
    if (!IsValid(Context) || !Context->GetWorld() || Context->GetWorld()->WorldType != EWorldType::PIE) return false;
    const auto* Data = Cast<ANavigationData>(NavigationData);
    auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context->GetWorld());
    FNavLocation Projected;
    return Data && Data->GetWorld() == Context->GetWorld() && Navigation &&
        Navigation->ProjectPointToNavigation(Location, Projected, FVector(100,100,200), Data);
}

bool UProjectCompatibilityInspectionLibrary::RemapTransientQuestTargets(UAQSQuest* Quest,AActor* Guy,AActor* Vendor,AActor* Spline)
{
    const auto* Controller=Quest ? Quest->GetTypedOuter<APlayerController>() : nullptr;
    if (!Controller || !Controller->GetWorld() || Controller->GetWorld()->WorldType!=EWorldType::PIE ||
        !IsValid(Guy) || !IsValid(Vendor) || Guy->GetWorld()!=Controller->GetWorld() || Vendor->GetWorld()!=Controller->GetWorld() ||
        (Spline && (!IsValid(Spline) || Spline->GetWorld()!=Controller->GetWorld()))) return false;
    // Validate every field before changing the transient duplicate.
    for (auto* Node:Quest->GetAllNodes()) if (auto* ObjectiveNode=Cast<UAQSObjectiveNode>(Node))
    {
        auto* Objective=ObjectiveNode->GetQuestObjective();
        if (!Objective) return false;
        auto* References=FindFProperty<FArrayProperty>(Objective->GetClass(),TEXT("ReferencedActors"));
        if (!References || !CastField<FSoftObjectProperty>(References->Inner)) return false;
        if (Spline)
            if (auto* Path=FindFProperty<FObjectPropertyBase>(Objective->GetClass(),TEXT("TargetSpline")); Path && !Spline->IsA(Path->PropertyClass)) return false;
    }
    for (auto* Node:Quest->GetAllNodes()) if (auto* ObjectiveNode=Cast<UAQSObjectiveNode>(Node))
    {
        auto* Objective=ObjectiveNode->GetQuestObjective();
        if (!Objective) return false;
        if (Spline)
        {
            if (auto* Path=FindFProperty<FObjectPropertyBase>(Objective->GetClass(),TEXT("TargetSpline")))
            {
                if (!Spline->IsA(Path->PropertyClass)) return false;
                Path->SetObjectPropertyValue_InContainer(Objective,Spline);
            }
        }
        auto* Property=FindFProperty<FArrayProperty>(Objective->GetClass(),TEXT("ReferencedActors"));
        if (!Property || !CastField<FSoftObjectProperty>(Property->Inner)) return false;
        auto* References=Property->ContainerPtrToValuePtr<TArray<TSoftObjectPtr<AActor>>>(Objective);
        for (auto& Reference:*References)
            Reference=Reference.ToSoftObjectPath().ToString().Contains(TEXT("ACF_Vendor")) ? Vendor : Guy;
    }
    for (auto* RawNode:Quest->GetAllNodes()) if (auto* Node=Cast<UAQSBaseNode>(RawNode))
    {
        TArray<UAGSAction*> Actions;
        for (auto* Action:Node->GetActivationActions()) Actions.Add(Action);
        for (auto* Action:Node->GetOnCompletionActions()) Actions.Add(Action);
        for (auto* Action:Actions) if (Action)
            if (auto* Speaker=FindFProperty<FSoftObjectProperty>(Action->GetClass(),TEXT("DialogueSpeaker")); Speaker && Guy->IsA(Speaker->PropertyClass))
                Speaker->SetObjectPropertyValue_InContainer(Action,Guy);
    }
    return true;
}

float UProjectCompatibilityInspectionLibrary::GetPhysicalActivityIntensity(AActor* Actor)
{
    return FProjectAttributeSynergies::PhysicalEffortIntensity(Actor);
}

bool UProjectCompatibilityInspectionLibrary::RepairPlayerLadderDownMontage()
{
    auto* Set=LoadObject<UACFAbilitySet>(nullptr,TEXT("/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_Player.AS_ACF_Player"));
    auto* Montage=LoadObject<UAnimMontage>(nullptr,TEXT("/Game/FullSample/Animations/LadderSystem/Animations/ACF_Climb-Down_Loop.ACF_Climb-Down_Loop"));
    if (!Set || !Montage) return false;
    for (auto& Config:Set->ActionAbilities)
    {
        if (Config.TriggeringTag.GetTagName()!=TEXT("Actions.LadderClimb.Loop")) continue;
        auto* Action=Config.Action.Get();
        if (!Action || Action->GetOutermost()!=Set->GetOutermost() ||
            Action->GetClass()->GetPathName()!=TEXT("/AscentCombatFramework/Blueprints/Abilities/ACF_ClimbLadderLoop_BP.ACF_ClimbLadderLoop_BP_C")) return false;
        auto* Down=FindFProperty<FObjectPropertyBase>(Action->GetClass(),TEXT("Down"));
        if (!Down || !Montage->IsA(Down->PropertyClass)) return false;
        UObject* Existing=Down->GetObjectPropertyValue_InContainer(Action);
        if (Existing) return Existing==Montage;
        Action->Modify();
        Down->SetObjectPropertyValue_InContainer(Action,Montage);
        Set->MarkPackageDirty();
        return true;
    }
    return false;
}

bool UProjectCompatibilityInspectionLibrary::InstallPlayerSimulatedBowCharge()
{
    auto* Set=LoadObject<UACFAbilitySet>(nullptr,TEXT("/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_NewMMBowActions.AS_ACF_NewMMBowActions"));
    auto* Class=LoadClass<UProjectSimulatedSustainedAction>(nullptr,TEXT("/Game/_Game/Blueprints/Compatibility/BP_ProjectChargedShoot.BP_ProjectChargedShoot_C"));
    if (!Set || !Class) return false;
    for (auto& Config:Set->ActionAbilities)
    {
        if (Config.TriggeringTag.GetTagName()!=TEXT("Actions.Shoot")) continue;
        auto* Old=Config.Action.Get();
        if (!Old || Old->GetOutermost()!=Set->GetOutermost()) return false;
        if (Old->GetClass()==Class) return true;
        if (Old->GetClass()->GetPathName()!=TEXT("/AscentCombatFramework/Blueprints/Actions/ACF_ChargedShootAction_BP.ACF_ChargedShootAction_BP_C")) return false;
        Set->Modify();
        auto* Replacement=NewObject<UProjectSimulatedSustainedAction>(Set,Class,NAME_None,RF_Transactional);
        UEngine::CopyPropertiesForUnrelatedObjects(Old,Replacement);
        Config.Action=Replacement;
        Set->MarkPackageDirty();
        return true;
    }
    return false;
}
