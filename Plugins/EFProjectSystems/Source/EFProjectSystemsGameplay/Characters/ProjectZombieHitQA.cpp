#include "Characters/ProjectActionMovementGateComponent.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "EFCharacterCustomizationComponent.h"
#include "ACFAIController.h"
#include "ACFGASStatisticsComponent.h"
#include "Actions/ACFHitAction.h"
#include "Components/ACFAbilitySystemComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Game/ACFDamageType.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FZombieHitQA : TSharedFromThis<FZombieHitQA>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACharacter> NPC,Player;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AActor> OldView;
    FTransform OldTransform;
    FRotator OldControl;
    bool bOldImmortal=false,bCaptured=false,bQuit=false,bSawReaction=false,bSawBlendTail=false,bLightDone=false;
    int32 Stage=0,Index=0,Round=0,Samples=0,Completed=0;
    double Start=FPlatformTime::Seconds(),Due=0,WindowStart=0,Travel=0,MaxSpeed=0,PreSpeed=0;
    FVector Last,Origin;
    FQuat LegStart,TorsoStart;
    double LegDelta=0,TorsoDelta=0;
    FString Events,Directory=FPaths::ProjectSavedDir()/TEXT("ZombieHit/Runtime");
    void Record(const FString& Text)
    {
        Events+=Text+TEXT("\n");UE_LOG(LogTemp,Display,TEXT("[ZombieHit] %s"),*Text);
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("progress.txt")));
    }
    void ClearNPC()
    {
        if (auto* P=NPC.Get())
        {
            if (auto* State=AProjectNPCAppearanceState::Ensure(World.Get())) State->Records.Remove(AProjectNPCAppearanceState::IdentityFor(P));
            if (P->GetController()) P->GetController()->Destroy();P->Destroy();
        }
        NPC.Reset();
    }
    bool Finish(bool Pass,const FString& Reason)
    {
        Record(FString::Printf(TEXT("status=%s cases=%d seconds=%.3f reason=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Completed,FPlatformTime::Seconds()-Start,*Reason));
        ClearNPC();
        if (bCaptured && Player.IsValid())
        {
            Player->SetActorTransform(OldTransform,false,nullptr,ETeleportType::TeleportPhysics);
            if (auto* D=Player->FindComponentByClass<UACFDamageHandlerComponent>()) D->SetIsImmortal(bOldImmortal);
            if (auto* PC=Cast<APlayerController>(Player->GetController())) {PC->SetControlRotation(OldControl);PC->SetViewTarget(OldView.IsValid()?OldView.Get():Player.Get());}
        }
        if (Camera.IsValid()) Camera->Destroy();
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("runtime.txt")));
        if (bQuit && World.IsValid()) if (auto* PC=UGameplayStatics::GetPlayerController(World.Get(),0)) PC->ConsoleCommand(TEXT("quit"));
        return false;
    }
    void Screenshot(const TCHAR* Mode)
    {
        if (NPC.IsValid() && Camera.IsValid())
        {
            const FVector Focus=NPC->GetActorLocation()+FVector(0,0,20),Eye=Focus+FVector(-340,-340,115);
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
            FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("%s_%d_%d.png"),Mode,Index,Round),false,false);
        }
    }
    bool Hit(float Damage)
    {
        auto* P=NPC.Get();auto* D=P?P->FindComponentByClass<UACFDamageHandlerComponent>():nullptr;
        if (!D || !D->GetIsAlive()) return false;
        FHitResult Contact(P,P->GetMesh(),P->GetMesh()->GetSocketLocation(TEXT("spine_03")),FVector(-1,0,0));
        Contact.BoneName=TEXT("spine_03");
        // Engine hitscan point-damage path: no synthetic action or manual montage.
        const float Applied=UGameplayStatics::ApplyPointDamage(P,Damage,FVector(-1,0,0),Contact,Player->GetController(),Player.Get(),UACFDamageType::StaticClass());
        Record(FString::Printf(TEXT("point_impact requested=%.2f applied=%.2f response=%s"),Damage,Applied,*D->GetLastDamageInfo().HitResponseAction.ToString()));
        return Applied>0;
    }
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if (Now-Start>90) return Finish(false,TEXT("90-second watchdog"));
        auto* W=World.Get();auto* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr;
        if (!W) return Finish(false,TEXT("world lost"));
        if (!PC || !PC->GetPawn() || W->GetTimeSeconds()<2 || Now<Due) return true;
        if (Stage==0)
        {
            Player=Cast<ACharacter>(PC->GetPawn());if (!Player.IsValid()) return Finish(false,TEXT("no character player"));
            OldTransform=Player->GetActorTransform();OldControl=PC->GetControlRotation();OldView=PC->GetViewTarget();bCaptured=true;
            if (auto* D=Player->FindComponentByClass<UACFDamageHandlerComponent>()) {bOldImmortal=D->GetIsImmortal();D->SetIsImmortal(true);}
            Camera=W->SpawnActor<ACameraActor>();PC->SetViewTarget(Camera.Get());
            Record(TEXT("fixture=engine_hitscan_point_damage native_AI no_forced_target_or_MoveTo deadline_seconds=90"));Stage=1;
        }
        if (Stage==1)
        {
            if (Index==2) return Finish(true,TEXT("2 DAZ zombies: planted impacts, repeat, cancellation, death and natural pursuit recovery"));
            Player->SetActorLocation(FVector(-400,-3200,96),false,nullptr,ETeleportType::TeleportPhysics);
            const FString Name=Index?TEXT("ACFZombieBPFemale"):TEXT("ACFZombieBPMale");
            const FString Path=FString::Printf(TEXT("/Game/_Game/Characters/%s/%s.%s_C"),Index?TEXT("Female"):TEXT("Male"),*Name,*Name);
            UClass* C=LoadClass<ACharacter>(nullptr,*Path);FNavLocation NavPoint;
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
            if (!C || !Nav || !Nav->ProjectPointToNavigation(FVector(800,-3200,0),NavPoint,FVector(150,150,300))) return Finish(false,TEXT("class or HUB navigation missing"));
            FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            NPC=W->SpawnActor<ACharacter>(C,NavPoint.Location+FVector(0,0,96),FRotator(0,180,0),Params);
            if (!NPC.IsValid()) return Finish(false,TEXT("spawn failed"));
            if (!NPC->GetController()) NPC->SpawnDefaultController();
            NPC->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Screenshot(TEXT("ready"));
            Round=0;bLightDone=false;Stage=2;Due=Now+3;return true;
        }
        auto* P=NPC.Get();auto* AI=P?Cast<AACFAIController>(P->GetController()):nullptr;
        auto* Gate=P?P->FindComponentByClass<UProjectActionMovementGateComponent>():nullptr;
        auto* ASC=P?P->FindComponentByClass<UACFAbilitySystemComponent>():nullptr;
        if (!P || !AI || !Gate || !ASC) return Finish(false,TEXT("native controller, action manager or gate missing"));
        if (Stage==2)
        {
            if (!P->FindComponentByClass<UEFCharacterCustomizationComponent>()) return Finish(false,TEXT("DAZ appearance missing"));
            // Keep each pursuit phase inside the measured HUB nav corridor.
            // Relative retreat accumulated outside NavMesh after earlier reactions.
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
            FNavLocation Target;
            const FVector Candidate=P->GetActorLocation().X<200 ? FVector(800,-3200,0) : FVector(-400,-3200,0);
            if (!Nav || !Nav->ProjectPointToNavigation(Candidate,Target,FVector(100,100,300)) || FVector::Dist2D(Candidate,Target.Location)>20)
                return Finish(false,TEXT("pursuit fixture target outside navigation"));
            Player->SetActorLocation(Target.Location+FVector(0,0,96),false,nullptr,ETeleportType::TeleportPhysics);
            Origin=P->GetActorLocation();WindowStart=Now;Stage=3;return true;
        }
        if (Stage==3)
        {
            if (Now-WindowStart>6) return Finish(false,TEXT("no natural pre-impact pursuit"));
            const double Advance=FVector::Dist2D(Origin,Player->GetActorLocation())-FVector::Dist2D(P->GetActorLocation(),Player->GetActorLocation());
            if (AI->GetTargetActorBK()!=Player.Get() || P->GetVelocity().Size2D()<100 || Advance<25) return true;
            PreSpeed=P->GetVelocity().Size2D();
            if (auto* Stats=P->FindComponentByClass<UACFGASStatisticsComponent>()) Stats->RefillStat(FGameplayTag::RequestGameplayTag(TEXT("RPG.Statistics.Health")));
            if (!Hit(bLightDone?60:10)) return Finish(false,TEXT("point damage not applied"));
            Last=P->GetActorLocation();Travel=0;MaxSpeed=0;Samples=0;bSawReaction=false;bSawBlendTail=false;WindowStart=Now;
            if (!bLightDone)
            {
                LegStart=P->GetMesh()->GetSocketTransform(TEXT("thigh_l"),RTS_Component).GetRotation();
                TorsoStart=P->GetMesh()->GetSocketTransform(TEXT("spine_03"),RTS_Component).GetRotation();LegDelta=0;TorsoDelta=0;Stage=8;
            }
            else Stage=4;
            return true;
        }
        if (Stage==4)
        {
            const bool Action=Cast<UACFHitAction>(ASC->GetCurrentAction())!=nullptr;
            const bool Holding=Gate->IsHoldingNavigation();
            if (Action || Holding)
            {
                bSawReaction|=Action; bSawBlendTail|=Holding && !Action;
                Travel+=FVector::Dist2D(Last,P->GetActorLocation());Last=P->GetActorLocation();MaxSpeed=FMath::Max(MaxSpeed,double(P->GetVelocity().Size2D()));++Samples;
                if (Samples==12) Screenshot(TEXT("reaction"));
                if (Round==1 && Now-WindowStart>.4 && Action)
                {
                    // Exercise native interrupted-montage lifecycle, not only the happy path.
                    P->GetMesh()->GetAnimInstance()->Montage_Stop(.15f,ASC->GetCurrentAction()->GetAnimMontage());
                    Record(TEXT("interruption=montage_stop blend_seconds=0.15"));
                }
                if (Now-WindowStart>6) return Finish(false,TEXT("reaction or navigation gate stuck"));
                return true;
            }
            Record(FString::Printf(TEXT("case=%d round=%d pre_speed_cm_s=%.2f hit_samples=%d held_seconds=%.3f slide_cm=%.3f max_speed_cm_s=%.3f blend_tail=%d"),Index,Round,PreSpeed,Samples,Now-WindowStart,Travel,MaxSpeed,bSawBlendTail));
            if (!bSawReaction || Samples<5 || Travel>3 || MaxSpeed>5) return Finish(false,TEXT("reaction absent or unwanted sliding"));
            Origin=P->GetActorLocation();WindowStart=Now;Stage=5;return true;
        }
        if (Stage==5)
        {
            const double Advance=FVector::Dist2D(Origin,Player->GetActorLocation())-FVector::Dist2D(P->GetActorLocation(),Player->GetActorLocation());
            if (Now-WindowStart>6) return Finish(false,TEXT("pursuit did not recover"));
            if (Advance<100 || P->GetVelocity().Size2D()<100 || AI->GetTargetActorBK()!=Player.Get()) return true;
            Record(FString::Printf(TEXT("recovery case=%d round=%d advance_cm=%.2f seconds=%.3f"),Index,Round,Advance,Now-WindowStart));
            if (Round==0) {++Round;Stage=2;return true;}
            if (auto* Stats=P->FindComponentByClass<UACFGASStatisticsComponent>()) Stats->RefillStat(FGameplayTag::RequestGameplayTag(TEXT("RPG.Statistics.Health")));
            if (!Hit(60) || !Gate->IsHoldingNavigation()) return Finish(false,TEXT("death-interruption reaction not active"));
            if (!Hit(100000)) return Finish(false,TEXT("death damage not applied"));
            Origin=P->GetActorLocation();WindowStart=Now;Stage=6;Due=Now+.6;return true;
        }
        if (Stage==6)
        {
            auto* D=P->FindComponentByClass<UACFDamageHandlerComponent>();
            const double DeathTravel=FVector::Dist2D(Origin,P->GetActorLocation());
            // Native ACF may retain a Moving request after disabling the dead
            // pawn's movement. Certify actual translation, not a stale status.
            Record(FString::Printf(TEXT("death case=%d alive=%d speed_cm_s=%.3f displacement_cm=%.3f path_status=%d"),Index,D?D->GetIsAlive():true,P->GetVelocity().Size2D(),DeathTravel,int32(AI->GetPathFollowingComponent()->GetStatus())));
            if (!D || D->GetIsAlive() || P->GetVelocity().Size2D()>5 || DeathTravel>3) return Finish(false,TEXT("death resumed translation"));
            Screenshot(TEXT("death"));
            ++Completed;Stage=7;Due=Now+.3;return true;
        }
        if (Stage==7) {ClearNPC();++Index;Stage=1;Due=Now+.2;}
        if (Stage==8)
        {
            const bool Light=ASC->GetCurrentActionTag()==FGameplayTag::RequestGameplayTag(TEXT("Actions.HitLight"));
            if (Light)
            {
                if (Gate->IsHoldingNavigation()) return Finish(false,TEXT("light impact paused locomotion"));
                bSawReaction=true;++Samples;
                Travel+=FVector::Dist2D(Last,P->GetActorLocation());Last=P->GetActorLocation();
                LegDelta=FMath::Max(LegDelta,double(LegStart.AngularDistance(P->GetMesh()->GetSocketTransform(TEXT("thigh_l"),RTS_Component).GetRotation())));
                TorsoDelta=FMath::Max(TorsoDelta,double(TorsoStart.AngularDistance(P->GetMesh()->GetSocketTransform(TEXT("spine_03"),RTS_Component).GetRotation())));
                if (Samples==10) Screenshot(TEXT("light_torso"));
                if (Now-WindowStart>3) return Finish(false,TEXT("light reaction stuck"));
                return true;
            }
            Record(FString::Printf(TEXT("light case=%d samples=%d movement_cm=%.3f leg_motion_rad=%.4f torso_motion_rad=%.4f seconds=%.3f"),Index,Samples,Travel,LegDelta,TorsoDelta,Now-WindowStart));
            if (!bSawReaction || Samples<4 || Travel<30 || LegDelta<.05 || TorsoDelta<.02) return Finish(false,TEXT("light torso reaction or leg locomotion missing"));
            bLightDone=true;Stage=2;return true;
        }
        return true;
    }
};
TWeakPtr<FZombieHitQA> ActiveZombieHit;
FAutoConsoleCommandWithWorldAndArgs ZombieHitCommand(TEXT("Project.ZombieHit.QA"),TEXT("90-second planted hit and pursuit recovery QA; optional quit."),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
{
    if (!World || ActiveZombieHit.IsValid()) return;
    auto Test=MakeShared<FZombieHitQA>();Test->World=World;Test->bQuit=Args.Contains(TEXT("quit"));ActiveZombieHit=Test;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Dt){return Test->Tick(Dt);}));
}));
}
#endif
