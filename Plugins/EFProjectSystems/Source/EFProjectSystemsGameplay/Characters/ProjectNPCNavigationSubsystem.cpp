#include "Characters/ProjectNPCNavigationSubsystem.h"
#include "ACFAIController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

bool UProjectNPCNavigationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type==EWorldType::Game || Type==EWorldType::PIE;
}
void UProjectNPCNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (auto* GI=GetWorld()->GetGameInstance()) GI->GetOnPawnControllerChanged().AddDynamic(this,&ThisClass::ControllerChanged);
}
void UProjectNPCNavigationSubsystem::Deinitialize()
{
    if (auto* GI=GetWorld()->GetGameInstance()) GI->GetOnPawnControllerChanged().RemoveDynamic(this,&ThisClass::ControllerChanged);
    GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
    Super::Deinitialize();
}
void UProjectNPCNavigationSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    for (TActorIterator<APawn> It(&World);It;++It) ControllerChanged(*It,It->GetController());
}
void UProjectNPCNavigationSubsystem::ControllerChanged(APawn* Pawn,AController* Controller)
{
    if (!Pawn || Pawn->GetWorld()!=GetWorld() || !Pawn->HasAuthority() || !Cast<AACFAIController>(Controller)) return;
    bool Owned=false;
    for (auto* C=Pawn->GetClass();C;C=C->GetSuperClass())
        if (C->GetPathName().StartsWith(TEXT("/Game/_Game/Characters/"))) {Owned=true;break;}
    if (Owned) Configure(Pawn,0);
}
void UProjectNPCNavigationSubsystem::Configure(TWeakObjectPtr<APawn> Pawn,int32 Attempt)
{
    if (!Pawn.IsValid()) return;
    auto* AI=Cast<AACFAIController>(Pawn->GetController());
    auto* Crowd=AI?Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent()):nullptr;
    if (!Crowd || !Crowd->IsCrowdSimulationEnabled()) return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    auto* Manager=UCrowdManager::GetCurrent(GetWorld());
    const auto* Selected=Nav?Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(),Pawn->GetNavAgentLocation()):nullptr;
    if (!Selected || !Manager || !Manager->GetNavData())
    {
        // Bounded startup retry; no permanent pawn scan or movement/target overrides.
        if (Attempt<10)
        {
            FTimerHandle Handle;
            GetWorld()->GetTimerManager().SetTimer(Handle,FTimerDelegate::CreateUObject(this,&ThisClass::Configure,Pawn,Attempt+1),.2f,false);
        }
        return;
    }
    if (Selected!=Manager->GetNavData())
    {
        // Detour only owns one nav data. Its SetMoveSegment aborts other agents'
        // paths synchronously, before AITask_MoveTo binds its completion delegate.
        // Preserve the original controller/BT; use engine base path following.
        Crowd->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
        UE_LOG(LogTemp,Display,TEXT("[NPCNavigation] pawn=%s selected=%s crowd=%s fallback=base_path_following"),
            *Pawn->GetName(),*GetNameSafe(Selected),*GetNameSafe(Manager->GetNavData()));
    }
}
