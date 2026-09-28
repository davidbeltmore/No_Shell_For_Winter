#include "Progression/ProjectQuestStartComponent.h"
#include "AQSQuestManagerComponent.h"
#include "AQSQuestObjective.h"
#include "AQSQuestTargetComponent.h"
#include "Graph/AQSQuest.h"
#include "Graph/AQSStartQuestNode.h"
#include "Graph/AQSObjectiveNode.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

UProjectQuestStartComponent::UProjectQuestStartComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickInterval=1.f;
}

bool UProjectQuestStartComponent::AreInitialTargetsReady(const UAQSQuest* Quest, const UWorld* World)
{
    if (!Quest || !World) return false;
    // AQS loads its own data layer as part of StartQuest. Do not block that path.
    if (Quest->GetLayerToLoad()) return true;
    TArray<UAGSGraphNode*> Pending;
    for (auto* Node:Quest->GetAllNodes()) if (Cast<UAQSStartQuestNode>(Node)) Pending.Append(Node->ChildrenNodes);
    TSet<UAGSGraphNode*> Seen;
    while (!Pending.IsEmpty())
    {
        auto* Node=Pending.Pop(EAllowShrinking::No);
        if (!Node || Seen.Contains(Node)) continue;
        Seen.Add(Node);
        const auto* ObjectiveNode=Cast<UAQSObjectiveNode>(Node);
        if (!ObjectiveNode) { Pending.Append(Node->ChildrenNodes); continue; }
        const auto* Objective=ObjectiveNode->GetQuestObjective();
        if (!Objective || Objective->GetTargetRefType()!=ETargetReferenceType::ESoftRef) continue;
        for (const auto& Target:Objective->GetObjectiveTargetsSoftRef())
        {
            // Never load another world to satisfy a target in the current world.
            AActor* Actor=Target.Get();
#if WITH_EDITOR
            if ((!Actor || Actor->GetWorld()!=World) && World->WorldType==EWorldType::PIE)
            {
                FSoftObjectPath Path=Target.ToSoftObjectPath();
                Path.FixupForPIE(World->GetPackage()->GetPIEInstanceID());
                Actor=Cast<AActor>(Path.ResolveObject());
            }
#endif
            if (!IsValid(Actor) || Actor->GetWorld()!=World || !Actor->FindComponentByClass<UAQSQuestTargetComponent>()) return false;
        }
        // Later objectives may intentionally target a different level.
    }
    return true;
}

bool UProjectQuestStartComponent::StartQuestWhenReady(UAQSQuestManagerComponent* Manager, const FGameplayTag& QuestTag)
{
    if (!IsValid(Manager) || !Manager->GetOwner() || !QuestTag.IsValid()) return false;
    if (Manager->IsQuestInProgressByTag(QuestTag) || Manager->IsQuestCompletedByTag(QuestTag) || Manager->IsQuestFailedByTag(QuestTag)) return true;
    auto* Quest=Manager->GetQuestFromDB(QuestTag);
    if (!Quest) return false;
    if (AreInitialTargetsReady(Quest,Manager->GetWorld())) return Manager->StartQuest(QuestTag);
    auto* Deferred=Manager->GetOwner()->FindComponentByClass<UProjectQuestStartComponent>();
    if (!Deferred)
    {
        Deferred=NewObject<UProjectQuestStartComponent>(Manager->GetOwner());
        Manager->GetOwner()->AddInstanceComponent(Deferred); Deferred->RegisterComponent();
    }
    if (Deferred->PendingQuest && Deferred->PendingTag!=QuestTag) return false;
    Deferred->PendingManager=Manager; Deferred->PendingQuest=Quest; Deferred->PendingTag=QuestTag;
    Deferred->SetComponentTickEnabled(true);
    return true;
}

void UProjectQuestStartComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (!IsValid(PendingManager) || !IsValid(PendingQuest)) { SetComponentTickEnabled(false); return; }
    if (PendingManager->IsQuestInProgressByTag(PendingTag) || PendingManager->IsQuestCompletedByTag(PendingTag) || PendingManager->IsQuestFailedByTag(PendingTag))
    { SetComponentTickEnabled(false); PendingQuest=nullptr; return; }
    if (AreInitialTargetsReady(PendingQuest,GetWorld()))
    {
        SetComponentTickEnabled(false);
        PendingManager->StartQuest(PendingTag);
        PendingQuest=nullptr;
    }
}
