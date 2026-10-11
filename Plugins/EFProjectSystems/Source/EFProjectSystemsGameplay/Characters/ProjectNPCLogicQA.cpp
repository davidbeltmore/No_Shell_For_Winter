#include "Characters/ProjectNPCRoster.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "ACFAIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameplayTasksComponent.h"
#include "Tasks/AITask_MoveTo.h"
#include "UObject/UObjectIterator.h"
#include "ADSDialoguePartecipantComponent.h"
#include "Graph/ADSDialogue.h"
#include "Graph/ADSDialogueResponseNode.h"
#include "ADSDialogueWidgetInterface.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "CommonActivatableWidget.h"
#include "ANSUIPlayerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Components/ACFInteractionComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Interfaces/ACFInteractableInterface.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING
namespace
{
// Independent of the pose fixture: never stop brains, force targets or issue MoveTo.
struct FNPCLogicQA : TSharedFromThis<FNPCLogicQA>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACharacter> NPC;
    TWeakObjectPtr<APawn> Player;
    FTransform OldTransform;
    FRotator OldControl;
    bool bOldImmortal=false,bQuit=false,bNative=false,bAIOnly=false,bServicesOnly=false,bRangedApproach=false;
    TWeakObjectPtr<UADSDialogue> ServiceGraph;
    TWeakObjectPtr<UCommonActivatableWidget> OpenedService;
    void CloseService()
    {
        if (!OpenedService.IsValid()) return;
        auto* GI=World.IsValid()?World->GetGameInstance():nullptr;
        auto* UI=GI?GI->GetSubsystem<UANSUIPlayerSubsystem>():nullptr;
        if (UI) UI->RemoveInGameWidget(OpenedService.Get(),true,true);
        OpenedService.Reset();
    }
    bool IsExpectedMenu(UCommonActivatableWidget* Widget) const
    {
        const FString Expected=Services[Index/2]==TEXT("LevelingGuy")?TEXT("ACF_GASLevelUp_WB_C"):
            Services[Index/2]==TEXT("Vendor")?TEXT("ACF_BuySell_WB_C"):TEXT("ACF_SkillTreesNavigation_WBP_C");
        return Widget && Widget->IsActivated() && Widget->IsVisible() && Widget->GetClass()->GetName()==Expected;
    }
    int32 Stage=0,Index=0;
    double Start=FPlatformTime::Seconds(),Due=0,SampleStart=0;
    FVector EnemyStart;
    FVector Arena=FVector(-400,-3200,96);
    FString Events,Directory=FPaths::ProjectSavedDir()/TEXT("NPCLogic/Runtime");
    TArray<FName> Services={TEXT("LevelingGuy"),TEXT("Vendor"),TEXT("SkillGuy"),TEXT("Guy")};
    TArray<FName> Enemies={TEXT("MeleeEnemy"),TEXT("GunEnemy")};
    void Record(const FString& S)
    {
        Events+=S+TEXT("\n");UE_LOG(LogTemp,Display,TEXT("[NPCLogic] %s"),*S);
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("progress.txt")));
    }
    void ClearNPC()
    {
        if (auto* P=NPC.Get())
        {
            if (auto* D=P->FindComponentByClass<UADSDialoguePartecipantComponent>())
                for (auto* Dialogue:D->GetAllDialogues()) if (Dialogue && Dialogue->IsDialogueStarted()) Dialogue->EndDialogue();
            if (auto* State=AProjectNPCAppearanceState::Ensure(World.Get())) State->Records.Remove(AProjectNPCAppearanceState::IdentityFor(P));
            if (P->GetController()) P->GetController()->Destroy();
            P->Destroy();NPC.Reset();
        }
    }
    UACFInteractionComponent* Interaction() const
    {
        auto* P=Player.Get(); if (!P) return nullptr;
        auto* I=P->FindComponentByClass<UACFInteractionComponent>();
        return I?I:(P->GetController()?P->GetController()->FindComponentByClass<UACFInteractionComponent>():nullptr);
    }
    void Key(EInputEvent Event)
    {
        if (!World.IsValid()) return;
        if (auto* PC=UGameplayStatics::GetPlayerController(World.Get(),0))
            PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::E,Event,FPlatformTime::Cycles64()));
    }
    bool Finish(bool Pass,const FString& Reason)
    {
        Key(IE_Released);
        CloseService();
        ClearNPC();
        if (auto* P=Player.Get())
        {
            if (auto* I=Interaction()) I->EndInteraction();
            P->SetActorTransform(OldTransform,false,nullptr,ETeleportType::TeleportPhysics);
            if (auto* PC=Cast<APlayerController>(P->GetController())) PC->SetControlRotation(OldControl);
            if (auto* D=P->FindComponentByClass<UACFDamageHandlerComponent>()) D->SetIsImmortal(bOldImmortal);
        }
        Record(FString::Printf(TEXT("status=%s reason=%s elapsed_seconds=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),*Reason,FPlatformTime::Seconds()-Start));
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("runtime.txt")));
        if (bQuit) FPlatformMisc::RequestExit(false);
        return false;
    }
    ACharacter* Spawn(FName Id,bool Female,const FVector& At)
    {
        auto* R=LoadObject<UProjectNPCRoster>(nullptr,TEXT("/Game/_Game/Characters/Data/DA_NPCRoster.DA_NPCRoster"));
        if (!R) return nullptr;
        const auto* Row=R->Archetypes.FindByPredicate([&](const auto& V){return V.Id==Id;});if (!Row) return nullptr;
        UClass* C=(Female?Row->Female:Row->Male).LoadSynchronous();
        if (bNative)
        {
            FString Name=C?C->GetName():TEXT("");Name.RemoveFromEnd(TEXT("_C"));Name.RemoveFromEnd(Female?TEXT("Female"):TEXT("Male"));
            const FString Folder=Stage<5?TEXT("/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/"):TEXT("/Game/FullSample/Blueprints/Characters/Enemies/");
            C=LoadClass<ACharacter>(nullptr,*(Folder+Name+TEXT(".")+Name+TEXT("_C")));
        }
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* P=C?World->SpawnActor<ACharacter>(C,At,FRotator(0,180,0),Params):nullptr;
        NPC=P;
        if (P)
        {
            // Match ACFGroupAIComponent's spawn contract. No target/state/MoveTo is forced.
            if (!P->GetController()) P->SpawnDefaultController();
            Record(TEXT("spawn=")+P->GetClass()->GetPathName()+TEXT(" controller=")+GetNameSafe(P->GetController()));
        }
        return P;
    }
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if (Now-Start>150) return Finish(false,TEXT("150-second watchdog"));
        auto* W=World.Get();auto* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr;
        if (!W) return Finish(false,TEXT("Test world closed"));
        if (!PC || !PC->GetPawn()) return true;
        if (Now<Due) return true;
        if (Stage==0)
        {
            Player=PC->GetPawn();OldTransform=Player->GetActorTransform();OldControl=PC->GetControlRotation();
            if (auto* D=Player->FindComponentByClass<UACFDamageHandlerComponent>()) {bOldImmortal=D->GetIsImmortal();D->SetIsImmortal(true);}
            Stage=bAIOnly?5:1;Record(TEXT("fixture=normal_AI real_E_input authored_HUB_nav_volume ACF_spawner_controller_contract"));
        }
        if (Stage==1)
        {
            if (Index>=(bNative?1:Services.Num()*2)) {Stage=5;Index=0;Due=Now+1;return true;}
            Player->SetActorLocationAndRotation(OldTransform.GetLocation(),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            PC->SetControlRotation(FRotator::ZeroRotator);
            if (!Spawn(Services[Index/2],Index%2!=0,Player->GetActorLocation()+FVector(145,0,0))) return Finish(false,TEXT("Service spawn failed"));
            Stage=2;Due=Now+2;return true;
        }
        if (Stage==2)
        {
            auto* P=NPC.Get();auto* I=Interaction();
            if (!P || !I || !P->GetClass()->ImplementsInterface(UACFInteractableInterface::StaticClass())) return Finish(false,TEXT("Missing interaction contract"));
            const FString Name=IACFInteractableInterface::Execute_GetInteractableName(P).ToString();
            const bool Can=IACFInteractableInterface::Execute_CanBeInteracted(P,Player.Get());
            I->RefreshInteractions();
            Record(FString::Printf(TEXT("service=%s label=%s can_interact=%d detected=%s"),*P->GetClass()->GetName(),*Name,Can,*GetNameSafe(I->GetCurrentBestInteractableActor())));
            if (Name.IsEmpty() || Name==TEXT("Default Name") || !Can || I->GetCurrentBestInteractableActor()!=P) return Finish(false,TEXT("Service name/detection failed"));
            Key(IE_Pressed);Stage=3;Due=Now+.2;return true;
        }
        if (Stage==3) {Key(IE_Released);Stage=4;Due=Now+2;return true;}
        if (Stage==4)
        {
            auto* P=NPC.Get();auto* D=P?P->FindComponentByClass<UADSDialoguePartecipantComponent>():nullptr;
            const bool Interacted=Interaction() && Interaction()->GetCurrentInteractingActor()==P;
            bool Dialogue=false,VisibleUI=false;
            ServiceGraph.Reset();
            if (D) for (auto* Graph:D->GetAllDialogues()) if (Graph && Graph->IsDialogueStarted()) {Dialogue=true;ServiceGraph=Graph;}
            TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Widgets,UUserWidget::StaticClass(),false);
            for (auto* Widget:Widgets) if (Widget->GetClass()->ImplementsInterface(UADSDialogueWidgetInterface::StaticClass()))
            {
                auto* Activatable=Cast<UCommonActivatableWidget>(Widget);
                const bool Visible=Widget->IsVisible() && (Activatable?Activatable->IsActivated():Widget->IsInViewport());
                VisibleUI|=Visible;
                if (Visible) Record(TEXT("dialogue_ui=")+Widget->GetClass()->GetPathName());
            }
            Record(FString::Printf(TEXT("E=%s interaction=%d graph_started=%d visible_ui=%d participant_flag=%d dialogue_count=%d"),*GetNameSafe(P),Interacted,Dialogue,VisibleUI,D && D->IsInDialogue(),D?D->GetAllDialogues().Num():-1));
            if (!Interacted || (Services[Index/2]!=TEXT("Guy") && !(Dialogue && VisibleUI))) return Finish(false,TEXT("E did not start expected dialogue graph/UI"));
            FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Service_%02d.png"),Index),true,false);
            Stage=7;Due=Now+1;return true;
        }
        if (Stage==7)
        {
            if (Services[Index/2]!=TEXT("Guy"))
            {
                if (!ServiceGraph.IsValid()) return Finish(false,TEXT("Service graph lost before response"));
                UADSDialogueResponseNode* Choice=nullptr;
                for (auto* Answer:ServiceGraph->GetAllButtonAnswersForCurrentNode())
                {
                    const FString Text=Answer->GetDialogueText().ToString();
                    if ((Services[Index/2]==TEXT("LevelingGuy") && Text.Contains(TEXT("level up"))) ||
                        (Services[Index/2]==TEXT("Vendor") && Text.Contains(TEXT("goods"))) ||
                        (Services[Index/2]==TEXT("SkillGuy") && Text==TEXT("Sure!"))) Choice=Answer;
                }
                if (!Choice) return Finish(false,TEXT("Native service response missing"));
                Record(TEXT("response=")+Choice->GetDialogueText().ToString());Choice->ResponseSelected();Stage=9;Due=Now+1;return true;
            }
            if (auto* I=Interaction()) I->EndInteraction();ClearNPC();++Index;Stage=1;Due=Now+1;return true;
        }
        if (Stage==9)
        {
            TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Widgets,UUserWidget::StaticClass(),false);
            bool Menu=false;
            for (auto* Widget:Widgets) if (auto* A=Cast<UCommonActivatableWidget>(Widget);IsExpectedMenu(A))
            {Menu=true;OpenedService=A;Record(TEXT("service_menu=")+A->GetClass()->GetPathName());}
            if (!Menu) return Finish(false,TEXT("Service response did not activate its menu"));
            FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Menu_%02d.png"),Index),true,false);
            Stage=10;Due=Now+1;return true;
        }
        if (Stage==10)
        {
            CloseService();
            if (auto* I=Interaction()) I->EndInteraction();ClearNPC();++Index;Stage=1;Due=Now+1;return true;
        }
        if (Stage==8) {ClearNPC();++Index;Stage=5;Due=Now+1;return true;}
        if (Stage==5)
        {
            if (bServicesOnly) return Finish(true,TEXT("Requested service/quest interaction sample"));
            if (Index>=(bNative?1:Enemies.Num()*2)) return Finish(true,bNative?TEXT("Native baseline: requested sample"):bAIOnly?TEXT("2 melee pursuit and 2 ranged spacing samples"):TEXT("6 service menus, 2 quest-NPC interaction contracts, 2 melee pursuit and 2 ranged spacing samples"));
            // Clear corridor inside the authored volume, away from the HUB companion outside nav.
            Player->SetActorLocationAndRotation(Arena,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator::ZeroRotator);
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);FNavLocation Point;
            const FVector Desired=Player->GetActorLocation()+FVector(1200,0,0);
            if (!Nav || !Nav->ProjectPointToNavigation(Desired,Point,FVector(150,150,300))) return Finish(false,TEXT("No navigation on authored HUB volume"));
            auto* P=Spawn(Enemies[Index/2],Index%2!=0,Point.Location+FVector(0,0,96));
            if (!P) return Finish(false,TEXT("Enemy spawn failed"));
            EnemyStart=P->GetActorLocation();SampleStart=Now;bRangedApproach=false;Stage=6;Due=Now+1;return true;
        }
        if (Stage==6)
        {
            auto* P=NPC.Get();auto* AI=P?Cast<AACFAIController>(P->GetController()):nullptr;
            if (!P || !AI) return Finish(false,TEXT("Enemy controller missing"));
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
            auto* Path=Nav?Nav->FindPathToActorSynchronously(W,P->GetActorLocation(),Player.Get(),100,P):nullptr;
            const double Moved=FVector::Dist2D(EnemyStart,P->GetActorLocation());
            const double Advance=FVector::Dist2D(EnemyStart,Player->GetActorLocation())-FVector::Dist2D(P->GetActorLocation(),Player->GetActorLocation());
            const bool Brain=AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning();
            const bool Target=AI->GetTargetActorBK()==Player.Get();
            const bool Route=Path && Path->IsValid() && !Path->IsPartial();
            const bool Ranged=Enemies[Index/2]==TEXT("GunEnemy");
            // DA_ACFRangedCB attacks above 100cm and retreats below 350cm.
            // Test the source role's spacing response, without forcing AI state/target.
            if (Ranged && !bRangedApproach && Target && AI->GetCombatStateBK()==EAICombatState::ERangedCombat && Now-SampleStart>3)
            {
                Record(TEXT("ranged_attack_state=observed stimulus=player_approaches_200cm"));
                Player->SetActorLocation(P->GetActorLocation()-FVector(200,0,0),false,nullptr,ETeleportType::TeleportPhysics);
                EnemyStart=P->GetActorLocation();SampleStart=Now;bRangedApproach=true;return true;
            }
            const bool Movement=Ranged?(bRangedApproach && Moved>150 && Advance<-150):Advance>200;
            if ((Brain && Target && Route && Movement) || Now-SampleStart>12)
            {
                Record(FString::Printf(TEXT("enemy=%s brain=%d target_player=%d path=%d moved_cm=%.1f advance_cm=%.1f state=%s seconds=%.2f"),*P->GetClass()->GetName(),Brain,Target,Route,Moved,Advance,*AI->GetAIState().ToString(),Now-SampleStart));
                Record(TEXT("target=")+GetNameSafe(AI->GetTargetActorBK()));
                if (auto* BT=Cast<UBehaviorTreeComponent>(AI->GetBrainComponent())) Record(BT->GetDebugInfoString()+TEXT(" tasks=")+BT->DescribeActiveTasks());
                if (auto* BB=AI->GetBlackboardComponent()) Record(BB->GetDebugInfoString(EBlackboardDescription::KeyWithValue));
                if (auto* PF=AI->GetPathFollowingComponent()) Record(FString::Printf(TEXT("path_following=%d"),int32(PF->GetStatus())));
                Record(FString::Printf(TEXT("postpone_paths=%d"),AI->ShouldPostponePathUpdates()));
                for (TObjectIterator<UAITask_MoveTo> It;It;++It) if (It->GetAIController()==AI)
                    Record(FString::Printf(TEXT("move_task=%s state=%d component=%s debug=%s"),*It->GetName(),int32(It->GetState()),*GetPathNameSafe(It->GetGameplayTasksComponent()),*It->GetDebugString()));
                TArray<UGameplayTasksComponent*> TaskComponents;P->GetComponents(TaskComponents);
                for (auto* TC:TaskComponents)
                {
                    Record(TEXT("claimed_resources=")+TC->GetCurrentlyUsedResources().GetDebugDescription());
                    for(auto It=TC->GetKnownTaskIterator();It;++It) if (auto* Task=It->Get())
                        Record(FString::Printf(TEXT("known_task=%s state=%d required=%s claimed=%s"),*Task->GetName(),int32(Task->GetState()),*Task->GetRequiredResources().GetDebugDescription(),*Task->GetClaimedResources().GetDebugDescription()));
                }
                if (!(Brain && Target && Route && Movement)) return Finish(false,TEXT("Enemy failed native pursuit/spacing response"));
                FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Enemy_%02d.png"),Index),false,false);
                Stage=8;Due=Now+1;
            }
            return true;
        }
        return true;
    }
};
TWeakPtr<FNPCLogicQA> ActiveLogic;
FAutoConsoleCommandWithWorldAndArgs LogicCommand(TEXT("Project.NPCLogic.QA"),TEXT("Bounded 150s real interaction and pursuit QA; optional quit/native, arena_x and arena_y."),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
{
    if (!World || ActiveLogic.IsValid()) return;
    auto Test=MakeShared<FNPCLogicQA>();Test->World=World;Test->bQuit=Args.Contains(TEXT("quit"));Test->bNative=Args.Contains(TEXT("native"));ActiveLogic=Test;
    const FString Options=FString::Join(Args,TEXT(" "));
    FParse::Value(*Options,TEXT("arena_x="),Test->Arena.X);
    FParse::Value(*Options,TEXT("arena_y="),Test->Arena.Y);
    if (!FMath::IsFinite(Test->Arena.X) || !FMath::IsFinite(Test->Arena.Y)) return;
    Test->bAIOnly=Args.Contains(TEXT("ai"));Test->bServicesOnly=Args.Contains(TEXT("services"));
    if (Args.Contains(TEXT("guy"))) {Test->Services={TEXT("Guy")};Test->bServicesOnly=true;}
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Dt){return Test->Tick(Dt);}));
}));
}
#endif
