#pragma once
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ProjectQuestStartComponent.generated.h"

class UAQSQuest;
class UAQSQuestManagerComponent;

/** Defers automatic quest start until its initial soft actor targets exist in this world. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectQuestStartComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectQuestStartComponent();
    /** True means started or queued. Repeated requests never restart a quest. */
    UFUNCTION(BlueprintCallable, Category="Project|Quests")
    static bool StartQuestWhenReady(UAQSQuestManagerComponent* Manager, const FGameplayTag& QuestTag);
    static bool AreInitialTargetsReady(const UAQSQuest* Quest, const UWorld* World);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    UPROPERTY(Transient) TObjectPtr<UAQSQuestManagerComponent> PendingManager;
    UPROPERTY(Transient) TObjectPtr<UAQSQuest> PendingQuest;
    FGameplayTag PendingTag;
};
