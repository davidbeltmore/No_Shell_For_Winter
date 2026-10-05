#include "Survival/ProjectInnerStateQA.h"
#include "Survival/ProjectInnerStateComponent.h"
#include "Survival/ProjectInnerStateConsumable.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectACFPoisonStatus.h"
#include "Progression/ProjectProgressionState.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "Components/ACFInventoryComponent.h"
#include "Components/ACFStatusEffectManagerComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

namespace
{
    double P95(TArray<double> Values)
    { Values.Sort(); return Values.IsEmpty()?0:Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*.95)-1,0,Values.Num()-1)]; }
    double Median(TArray<double> Values) { Values.Sort(); return Values.IsEmpty()?0:Values[Values.Num()/2]; }
    void SerializeComponent(UObject* Object,TArray<uint8>& Bytes,bool bLoad)
    {
        if (bLoad) { FMemoryReader Memory(Bytes); FObjectAndNameAsStringProxyArchive Ar(Memory,true); Ar.ArIsSaveGame=true; Object->Serialize(Ar); }
        else { FMemoryWriter Memory(Bytes); FObjectAndNameAsStringProxyArchive Ar(Memory,false); Ar.ArIsSaveGame=true; Object->Serialize(Ar); }
    }
}
AProjectInnerStateQA::AProjectInnerStateQA() { PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; }
void AProjectInnerStateQA::BeginPlay()
{
    Super::BeginPlay(); Started=StageStarted=FPlatformTime::Seconds();
    OutputDirectory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("InnerStateQA"),FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    FString Explicit; if (FParse::Value(FCommandLine::Get(),TEXT("InnerStateQAOutput="),Explicit)) OutputDirectory=Explicit;
    IFileManager::Get().MakeDirectory(*OutputDirectory,true);
    UE_LOG(LogTemp,Display,TEXT("[InnerStateQA] START map=%s output=%s budget=110s"),*GetWorld()->GetMapName(),*OutputDirectory);
}
void AProjectInnerStateQA::Check(const FString& Name,bool bResult)
{
    auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),Name); Item->SetBoolField(TEXT("passed"),bResult);
    Checks.Add(MakeShared<FJsonValueObject>(Item)); bPassed &= bResult;
    UE_LOG(LogTemp,Display,TEXT("[InnerStateQA] %s %s"),bResult?TEXT("PASS"):TEXT("FAIL"),*Name);
}
void AProjectInnerStateQA::Next(int32 NextStage)
{ Stage=NextStage; StageStarted=FPlatformTime::Seconds(); SimulationStarted=GetWorld()->GetTimeSeconds(); }
double AProjectInnerStateQA::Health() const { float V=0,M=0; if (Resources) Resources->TryReadOwnerResource(TEXT("Health"),V,M); return V; }
double AProjectInnerStateQA::MaximumHealth() const { float V=0,M=0; if (Resources) Resources->TryReadOwnerResource(TEXT("Health"),V,M); return M; }
void AProjectInnerStateQA::SetHealth(double Fraction) { Resources->ApplyOwnerResourceDelta(TEXT("Health"),static_cast<float>(MaximumHealth()*Fraction-Health())); }
void AProjectInnerStateQA::Neutral()
{
    Inner->ClearBreathingSource(TEXT("GlobalQA")); Inner->ClearComposureContext(TEXT("GlobalQA")); Inner->ClearContaminationSource(TEXT("GlobalQA"));
    Inner->SetSleeping(false);
    Inner->State=FProjectInnerStatePersistentState(); Inner->OnStateLoaded();
    for (const FName Name:{FName(TEXT("Hunger")),FName(TEXT("Thirst")),FName(TEXT("Sleep"))}) Needs->SetNeedCurrentValue(Name,Needs->GetNeedMaxValue(Name));
    for (const FName Name:{FName(TEXT("Pain")),FName(TEXT("Madness")),FName(TEXT("Curse")),FName(TEXT("Alcohol"))}) Needs->SetSensationCurrentValue(Name,0);
    SetHealth(1);
}
bool AProjectInnerStateQA::UseItem(const FString& Name)
{
    const FString Path=FString::Printf(TEXT("/Game/_Game/InnerState/Items/%s.%s_C"),*Name,*Name);
    UClass* Class=LoadClass<UProjectInnerStateConsumable>(nullptr,*Path);
    if (!Inventory || !Class) return false;
    Inventory->AddItemToInventoryByClass(Class,1,false);
    for (const auto& Item:Inventory->GetInventory())
        if (Item.ItemClass==Class)
        { if (!Inventory->CanUseConsumable(Item,Player)) return false; Inventory->UseConsumableOnTarget(Item,Player); return true; }
    return false;
}
void AProjectInnerStateQA::Screenshot(const FString& Name)
{ FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputDirectory,Name+TEXT(".png")),true,false); }
void AProjectInnerStateQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); if (bDone) return;
    const double Wall=FPlatformTime::Seconds();
    if (Wall-Started>=110) { Finish(TEXT("110 second watchdog")); return; }
    if (Stage==-1)
    {
        Player=UGameplayStatics::GetPlayerPawn(this,0);
        if (!Player) return;
        Inner=Player->FindComponentByClass<UProjectInnerStateComponent>(); Needs=Player->FindComponentByClass<UProjectSurvivalNeedsComponent>();
        Resources=Player->FindComponentByClass<UProjectRealtimeSnapshotComponent>(); Inventory=Player->FindComponentByClass<UACFInventoryComponent>();
        if (!Inner || !Needs || !Resources || !Inventory || MaximumHealth()<=0 || Wall-Started<3) return;
        Check(TEXT("real ACF player and survival stack outside dungeon"), !GetWorld()->GetMapName().Contains(TEXT("Dungeon")));
        if (UGameplayStatics::IsGamePaused(this)) { Finish(TEXT("Resume gameplay before running the fixture")); return; }
        Neutral();
        if (auto* Hud=GetWorld()->GetSubsystem<UProjectSurvivalNeedsSubsystem>()) Hud->SetNeedsHudVisible(true);
        InitialHours=Inner->State.CalendarHours; InitialHunger=Needs->GetNeedCurrentValue(TEXT("Hunger")); Next(0); return;
    }
    if (!IsValid(Player) || !IsValid(Inner)) { Finish(TEXT("Player was replaced before fixture completed")); return; }
    const double Age=GetWorld()->GetTimeSeconds()-SimulationStarted;
    if(Stage==1 && Age>=4 && !bStressShot) { Screenshot(TEXT("simultaneous")); bStressShot=true; }
    if (Stage==0 || Stage==1)
    {
        (Stage==0?BaselineFrames:LoadedFrames).Add(DeltaSeconds*1000);
        if (Inner->UpdateSerial!=LastSerial)
        { (Stage==0?BaselineCpu:LoadedCpu).Add(Inner->LastUpdateMicroseconds); LastSerial=Inner->UpdateSerial; }
    }
    if (Stage==0 && Age>=5)
    {
        Check(TEXT("legacy hunger follows calendar (4/hour)"),FMath::Abs(InitialHunger-Needs->GetNeedCurrentValue(TEXT("Hunger"))-(Inner->State.CalendarHours-InitialHours)*4)<.15);
        Check(TEXT("hygiene and bathroom advance at calendar rate"),Inner->State.Hygiene<100 && Inner->State.BathroomHours>0);
        Inner->SetDebugValue(TEXT("Stress"),75); Inner->SetDebugValue(TEXT("Stimulation"),-50); Inner->State.DepressedHours=48;
        Inner->SetDebugValue(TEXT("Hygiene"),10); Inner->SetDebugValue(TEXT("Bathroom"),99.999);
        Inner->SetDebugValue(TEXT("Sensation"),-35); Inner->ContractSickness(50);
        Inner->AddPoison(5,EProjectPoisonSpeed::Slow,TEXT("QA.Visual"),FGuid::NewGuid());
        Inner->SetComposureContext(TEXT("GlobalQA"),-100);
        Needs->SetSensationCurrentValue(TEXT("Madness"),65); Needs->SetSensationCurrentValue(TEXT("Curse"),25);
        if (auto* Status=Player->FindComponentByClass<UProjectSurvivalStatusComponent>()) Status->SetForcedStatusActive(TEXT("Cursed"),true);
        for(int32 I=0;I<200;++I)
        { FProjectInnerStateInfluence E; E.Source=FName(*FString::Printf(TEXT("QA.%d"),I)); E.Remaining=1; E.WellbeingPerHour=(I%2?-.01:.01); Inner->SetInfluence(E); }
        Next(1);
    }
    else if (Stage==1 && Age>=5)
    {
        Check(TEXT("200 simultaneous influences retained"),Inner->State.Influences.Num()==200);
        Check(TEXT("bathroom deadline causes hygiene accident"),Inner->State.BathroomHours<1 && Inner->State.Hygiene==0);
        Check(TEXT("depression and fear are live"),Inner->State.bDepressed && Inner->State.Composure<-60);
        Check(TEXT("bipolar sensation preserves legacy nonnegative Pain"),Inner->GetValue(TEXT("Sensation"))<0 && Needs->GetSensationCurrentValue(TEXT("Pain"))==0);
        Check(TEXT("stimulation effects remain finite and bounded"),FMath::IsFinite(Inner->State.Stimulation)&&FMath::Abs(Inner->GetWellbeingRatePerHour())<=1.5);
        Check(TEXT("loaded component p95 below 1 ms"),LoadedCpu.Num()>=10 && P95(LoadedCpu)<1000);
        Neutral(); Inner->SetDebugValue(TEXT("Hygiene"),20);
        Check(TEXT("ACF washcloth use"),UseItem(TEXT("BP_Washcloth")) && Inner->State.Hygiene==45);
        Inner->Wash(); Check(TEXT("full wash restores hygiene"),Inner->State.Hygiene==100);
        Inner->SetDebugValue(TEXT("Bathroom"),50); Check(TEXT("field bathroom action"),Inner->RequestCareAction(TEXT("Bathroom")) && Inner->State.Hygiene==95);
        Check(TEXT("ACF comfort tea"),UseItem(TEXT("BP_ComfortTea")));
        UseItem(TEXT("BP_ComfortTea")); Check(TEXT("repeat tea refreshes one family"),Inner->State.Influences.Num()==1);
        Inner->SetDebugValue(TEXT("Sensation"),50); Check(TEXT("ACF analgesic"),UseItem(TEXT("BP_Analgesic")) && Inner->GetValue(TEXT("Sensation"))<50);
        Neutral();
        Check(TEXT("ACF spoiled food adds debt"),UseItem(TEXT("BP_RottenRation")) && FMath::IsNearlyEqual(Inner->GetValue(TEXT("Poison")),25.0));
        InitialHealth=Health(); Next(2);
    }
    else if(Stage==2)
    {
        if (!bEarlyChecked && Age>=1)
        { Check(TEXT("fast poison respects 3 second incubation"),FMath::IsNearlyEqual(Inner->GetValue(TEXT("Poison")),25.0) && Health()>=InitialHealth-.01); bEarlyChecked=true; }
        if(Age>=5)
        {
            Check(TEXT("fast poison drains at one reference percent per second"),FMath::Abs(Inner->GetValue(TEXT("Poison"))-(25-(Age-3)))<.25 && Health()<InitialHealth);
            Inner->AddPoison(100,EProjectPoisonSpeed::Fast,TEXT("QA"),FGuid::NewGuid()); Inner->AddPoison(100,EProjectPoisonSpeed::Fast,TEXT("QA"),FGuid::NewGuid());
            Check(TEXT("re-exposure is additive beyond 200"),Inner->GetValue(TEXT("Poison"))>200 && Inner->State.Poison.HasActiveDose());
            SetHealth(.5); const double Debt=Inner->State.Poison.GetPendingDamage();
            Check(TEXT("ACF health potion heals 25 percent without changing debt"),UseItem(TEXT("BP_HealthPotion")) && FMath::Abs(Health()/MaximumHealth()-.75)<.001 && Inner->State.Poison.GetPendingDamage()==Debt);
            Check(TEXT("ACF antidote cancels remaining debt"),UseItem(TEXT("BP_Antidote")) && Inner->GetValue(TEXT("Poison"))==0);
            Neutral(); Inner->AddPoison(4,EProjectPoisonSpeed::Normal,TEXT("Normal"),FGuid::NewGuid()); Inner->AddPoison(2,EProjectPoisonSpeed::Slow,TEXT("Slow"),FGuid::NewGuid());
            // Disk round-trip of the same SaveGame fields used by ALS/travel.
            TArray<uint8> Bytes; SerializeComponent(Inner,Bytes,false);
            const FString Path=FPaths::Combine(OutputDirectory,TEXT("inner_state_roundtrip.bin"));
            const bool Saved=FFileHelper::SaveArrayToFile(Bytes,*Path); Inner->UseAntidote(); Bytes.Reset();
            const bool Loaded=FFileHelper::LoadFileToArray(Bytes,*Path); if(Loaded) { SerializeComponent(Inner,Bytes,true); Inner->OnStateLoaded(); }
            Check(TEXT("disk save/load preserves poison latency and debt"),Saved && Loaded && Inner->GetValue(TEXT("Poison"))==6 && !Inner->State.Poison.HasActiveDose());
            auto* Progression=GetWorld()->GetSubsystem<UProjectProgressionWorldSubsystem>();
            if (Progression && Progression->State)
            {
                Progression->State->Capture(Player); Inner->UseAntidote();
                Check(TEXT("progression travel snapshot restores actual player needs"),Progression->State->Restore(Player) && Inner->GetValue(TEXT("Poison"))==6);
            }
            else Check(TEXT("progression travel snapshot available"),false);
            Next(3);
        }
    }
    else if(Stage==3)
    {
        if(!bNormalChecked && Age>=8) { Check(TEXT("normal and slow incubation remain independent"),Inner->GetValue(TEXT("Poison"))==6); bNormalChecked=true; }
        if(Age>=31)
        {
            Check(TEXT("normal finishes and slow starts after 30 seconds"),Inner->GetValue(TEXT("Poison"))>1.5 && Inner->GetValue(TEXT("Poison"))<2);
            Inner->UseAntidote();
            if(auto* Acf=Player->FindComponentByClass<UACFStatusEffectManagerComponent>())
            {
                UClass* Poison=LoadClass<UProjectACFPoisonStatus>(nullptr,TEXT("/Game/FullSample/Blueprints/StatusEffects/ACF_PoisonStatus_BP.ACF_PoisonStatus_BP_C"));
                Check(TEXT("existing ACF poison uses project adapter"),Poison!=nullptr);
                if(Poison) { Acf->CreateAndApplyStatusEffect(Poison,Player); Acf->CreateAndApplyStatusEffect(Poison,Player); }
                Check(TEXT("ACF poison retrigger adds two exposures"),FMath::IsNearlyEqual(Inner->GetValue(TEXT("Poison")),50.0));
                Inner->UseAntidote();
            }
            else Check(TEXT("ACF poison manager available"),false);
            Neutral(); Inner->ContractSickness(100); InitialHealth=Health(); Next(4);
        }
    }
    else if(Stage==4 && Age>=3)
    {
        Check(TEXT("critical sickness damages health and awareness"),Health()<InitialHealth && Inner->State.Awareness<100);
        Check(TEXT("medicine starts 24 hour treatment"),UseItem(TEXT("BP_Medicine")) && Inner->State.TreatmentHours==24);
        Inner->State.Sickness=80.001; Next(40);
    }
    else if(Stage==40 && Age>=1.2)
    {
        Check(TEXT("medicine lowers sickness and clears critical latch at 80"),Inner->State.Sickness<80 && !Inner->State.bCriticalSickness);
        Neutral(); SetHealth(.25); Inner->SetDebugValue(TEXT("Sensation"),40); Inner->NotifyAwarenessImpact(100); InitialHealth=Health(); Next(5);
    }
    else if(Stage==5)
    {
        if(Age>=1 && !bKnockoutChecked)
        {
            const auto* Defeat=Player->FindComponentByClass<UProjectDefeatFlowComponent>();
            Check(TEXT("awareness zero enters real defeat bridge"),Inner->State.bUnconscious && Defeat && Defeat->GetCurrentPhase()!=EProjectDefeatPhase::None);
            bKnockoutChecked=true;
        }
        if(Age>=1 && !Inner->State.bUnconscious && Age<5) { Check(TEXT("awareness zero enters unconsciousness"),false); Next(6); }
        else if(Age>=14)
        {
            Check(TEXT("physiological knockout recovers after minimum duration"),!Inner->State.bUnconscious && Inner->State.Awareness>=30);
            Check(TEXT("knockout did not grant free health or clear pain"),Health()<=InitialHealth+.01 && Inner->GetValue(TEXT("Sensation"))>0);
            Neutral(); Needs->SetNeedCurrentValue(TEXT("Sleep"),Needs->GetNeedMaxValue(TEXT("Sleep"))*.5f);
            Check(TEXT("Y care rest enters runtime animation"),Inner->RequestCareAction(TEXT("Rest")) && Inner->IsSleeping());
            InitialHunger=Needs->GetNeedCurrentValue(TEXT("Sleep")); Next(6);
        }
    }
    else if(Stage==6 && Age>=2)
    {
        Check(TEXT("rest restores sleep through gameplay"),Needs->GetNeedCurrentValue(TEXT("Sleep"))>InitialHunger);
        if(auto* Actions=GetWorld()->GetSubsystem<UProjectEmoteSubsystem>()) Actions->CancelRuntimeAction();
        Inner->SetSleeping(false); Neutral(); Inner->State.Air=1; Inner->SetBreathingSource(TEXT("GlobalQA"),0); InitialHealth=Health(); Next(7);
    }
    else if(Stage==7 && Age>=4)
    {
        Check(TEXT("air zero immediately drains health and awareness"),Inner->State.Air==0 && Inner->State.Awareness<85 && Health()<InitialHealth);
        Inner->ClearBreathingSource(TEXT("GlobalQA")); Next(8);
    }
    else if(Stage==8 && Age>=6)
    {
        Check(TEXT("breathing recovers after source removal"),Inner->State.Air>=99 && Inner->State.Awareness>80);
        // Fatal scenario is last; run only in a disposable PIE/package session.
        Inner->State.Air=0; Inner->State.Awareness=.1; Inner->SetBreathingSource(TEXT("GlobalQA"),0); Next(9);
    }
    else if(Stage==9 && Age>=.3)
    {
        const auto* Handler=Player->FindComponentByClass<UACFDamageHandlerComponent>();
        Check(TEXT("zero air plus zero awareness invokes actual ACF death"),Inner->State.bFatalAsphyxia && Health()<=0 && Handler && !Handler->GetIsAlive());
        Finish();
    }
}
void AProjectInnerStateQA::Finish(const FString& Error)
{
    if(bDone) return; bDone=true;
    if(!Error.IsEmpty()) Check(Error,false);
    auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),bPassed); Report->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Started);
    Report->SetStringField(TEXT("map"),GetWorld()->GetMapName()); Report->SetStringField(TEXT("pawn"),GetPathNameSafe(Player)); Report->SetArrayField(TEXT("checks"),Checks);
    Report->SetNumberField(TEXT("baseline_component_p95_us"),P95(BaselineCpu)); Report->SetNumberField(TEXT("loaded_200_component_p95_us"),P95(LoadedCpu));
    Report->SetNumberField(TEXT("baseline_frame_p95_ms"),P95(BaselineFrames)); Report->SetNumberField(TEXT("loaded_frame_p95_ms"),P95(LoadedFrames));
    Report->SetNumberField(TEXT("baseline_component_samples"),BaselineCpu.Num()); Report->SetNumberField(TEXT("loaded_component_samples"),LoadedCpu.Num());
    Report->SetNumberField(TEXT("baseline_component_median_us"),Median(BaselineCpu)); Report->SetNumberField(TEXT("loaded_component_median_us"),Median(LoadedCpu));
    Report->SetNumberField(TEXT("baseline_frame_median_ms"),Median(BaselineFrames)); Report->SetNumberField(TEXT("loaded_frame_median_ms"),Median(LoadedFrames));
    Report->SetNumberField(TEXT("influence_sources"),200); Report->SetNumberField(TEXT("per_influence_timers"),0);
    FString Json; const auto Writer=TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Report,Writer);
    FFileHelper::SaveStringToFile(Json,*FPaths::Combine(OutputDirectory,TEXT("report.json")));
    UE_LOG(LogTemp,Display,TEXT("[InnerStateQA] FINISH passed=%d seconds=%.2f report=%s"),bPassed,FPlatformTime::Seconds()-Started,*OutputDirectory);
    if(FParse::Param(FCommandLine::Get(),TEXT("InnerStateQAQuit")))
        if(auto* PC=GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
    SetActorTickEnabled(false);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld InnerQACommand(TEXT("Project.InnerState.QA"),TEXT("Disposable non-dungeon gameplay QA; <=110s, fatal scenario last. Do not save this session."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if(!World || !World->IsGameWorld() || World->GetMapName().Contains(TEXT("Dungeon")) || World->GetMapName().Contains(TEXT("Calysto"))) return;
        World->SpawnActor<AProjectInnerStateQA>();
    }));
static FAutoConsoleCommandWithWorldAndArgs InnerSetCommand(TEXT("Project.InnerState.Set"),TEXT("Set <channel> <value> for tuning in disposable gameplay."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    { if(Args.Num()!=2 || !World) return; if(auto* P=UGameplayStatics::GetPlayerPawn(World,0)) if(auto* I=P->FindComponentByClass<UProjectInnerStateComponent>()) I->SetDebugValue(FName(*Args[0]),FCString::Atod(*Args[1])); }));
static FAutoConsoleCommandWithWorld InnerReportCommand(TEXT("Project.InnerState.Report"),TEXT("Read current Inner State including exact outstanding poison damage."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    { if(!World) return; if(auto* P=UGameplayStatics::GetPlayerPawn(World,0)) if(auto* I=P->FindComponentByClass<UProjectInnerStateComponent>())
        UE_LOG(LogTemp,Display,TEXT("[InnerState] Stress=%.3f Stimulation=%.3f Hygiene=%.3f Bathroom=%.3f Sickness=%.3f Awareness=%.3f Air=%.3f Composure=%.3f Sensation=%.3f Poison=%.6f debtHP=%.6f influences=%d cpu_us=%.2f"),I->State.Stress,I->State.Stimulation,I->State.Hygiene,I->GetValue(TEXT("Bathroom")),I->State.Sickness,I->State.Awareness,I->State.Air,I->State.Composure,I->GetValue(TEXT("Sensation")),I->GetValue(TEXT("Poison")),I->State.Poison.GetPendingDamage(),I->State.Influences.Num(),I->LastUpdateMicroseconds); }));
#endif
