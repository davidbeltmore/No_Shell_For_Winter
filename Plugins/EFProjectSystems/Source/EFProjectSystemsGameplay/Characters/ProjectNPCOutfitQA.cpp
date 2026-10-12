#include "Characters/ProjectNPCOutfitComponent.h"
#include "Characters/ProjectNPCOutfitSubsystem.h"
#include "Characters/ProjectNPCRoster.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFClothingMorphV3RuntimeComponent.h"
#include "Components/ACFEquipmentComponent.h"
#include "Components/ACFArmorSlotComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Items/ACFArmor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EngineGlobals.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "RenderingThread.h"
#include "DynamicRHI.h"
#include "CoreGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/GarbageCollection.h"

#if !UE_BUILD_SHIPPING
namespace
{
struct FOutfitQA : TSharedFromThis<FOutfitQA>
{
    TWeakObjectPtr<UWorld> World;
    TArray<TWeakObjectPtr<ACharacter>> Pawns;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AActor> PreviousView;
    TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Frames, CPUs, GPUs, Checks, GameTimes, RenderTimes, SurfaceTimes, ReconcileTimes;
    FString Directory, Body, Archetype=TEXT("SkillGuy"), Loading=TEXT("warm"), Name=TEXT("run");
    int32 Count=1, Seed=731, Stage=0;
    bool bDressed=true, bQuit=false, bFunctional=false, bCollectGarbage=false;
    bool bScreenshotProcessed=false, bMeasurementStarted=false;
    FDelegateHandle ScreenshotProcessedHandle;
    double Started=FPlatformTime::Seconds(), ReadyStart=0, CaptureStart=0, Due=0, Peak=0, ReadyTime=0;
    float CaptureSeconds=30, Timeout=120;
    TArray<TArray<FGuid>> BeforeIds;
    FGuid RemovedGuid;
    TArray<FString> Identities;
    bool RecordCheck(const FString& Text, bool Passed)
    {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Text); Row->SetStringField(TEXT("status"),Passed?TEXT("PASS"):TEXT("FAIL"));
        Checks.Add(MakeShared<FJsonValueObject>(Row)); return Passed;
    }
    void DestroyPawns()
    {
        for (auto P:Pawns) if (P.IsValid()) P->Destroy();
        Pawns.Reset();
    }
    bool Finish(bool Passed, const FString& Reason)
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
        // Observe map-owned initializations separately from the prepared fixture.
        // A fresh process can still have resident dependencies; report that fact
        // instead of calling every packaged request a cold resource load.
        TArray<TSharedPtr<FJsonValue>> InitialWorldOutfits;
        if (auto* W = World.Get()) for (TActorIterator<APawn> It(W); It; ++It)
        {
            APawn* Pawn = *It;
            if (Pawn->Tags.ContainsByPredicate([](FName Tag) { return Tag.ToString().StartsWith(TEXT("EF.AppearanceId.OutfitQA.")); })) continue;
            auto* Outfit = Pawn->FindComponentByClass<UProjectNPCOutfitComponent>();
            if (!Outfit || Outfit->EngineSecondsAtStart < 0) continue;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("actor"),Pawn->GetPathName());
            Row->SetStringField(TEXT("class"),Pawn->GetClass()->GetPathName());
            Row->SetStringField(TEXT("outfit"),Outfit->SelectedId.ToString());
            Row->SetStringField(TEXT("state"),UEnum::GetValueAsString(Outfit->State));
            Row->SetBoolField(TEXT("restored"),Outfit->bRestoredAtStart);
            Row->SetBoolField(TEXT("definition_resident_at_request"),Outfit->bDefinitionResidentAtRequest);
            Row->SetNumberField(TEXT("item_classes_requested"),Outfit->ItemClassesRequested);
            Row->SetNumberField(TEXT("item_classes_resident_at_request"),Outfit->ItemClassesResidentAtRequest);
            Row->SetNumberField(TEXT("definition_load_seconds"),Outfit->DefinitionLoadSeconds);
            Row->SetNumberField(TEXT("item_classes_load_seconds"),Outfit->ItemClassesLoadSeconds);
            Row->SetNumberField(TEXT("engine_seconds_at_start"),Outfit->EngineSecondsAtStart);
            Row->SetNumberField(TEXT("engine_seconds_at_ready"),Outfit->EngineSecondsAtReady);
            Row->SetNumberField(TEXT("initialization_seconds"),Outfit->ReadySeconds);
            InitialWorldOutfits.Add(MakeShared<FJsonValueObject>(Row));
        }
        Report->SetArrayField(TEXT("initial_world_outfits"),InitialWorldOutfits);
        Report->SetStringField(TEXT("initial_loading_scope"),TEXT("Map-owned initializations, separate from the QA group. Engine-relative timestamps include startup/queue dependencies; async durations include scheduling. Residency observations are not OS disk-cache measurements."));
        DestroyPawns();
        if (auto* W=World.Get())
        {
            if (auto* PC=UGameplayStatics::GetPlayerController(W,0); PC && PreviousView.IsValid()) PC->SetViewTarget(PreviousView.Get());
            if (auto* Saved=AProjectNPCAppearanceState::Ensure(W)) for (const auto& Id:Identities) Saved->Records.Remove(Id);
        }
        if (Camera.IsValid()) Camera->Destroy();
        Report->SetStringField(TEXT("status"),Passed?TEXT("PASS"):TEXT("FAIL")); Report->SetStringField(TEXT("reason"),Reason);
        Report->SetNumberField(TEXT("elapsed_seconds"),FPlatformTime::Seconds()-Started);
        Report->SetArrayField(TEXT("checks"),Checks);
        FString Json; auto Writer=TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Report.ToSharedRef(),Writer);
        IFileManager::Get().MakeDirectory(*Directory,true); FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("runtime.json")));
        UE_LOG(LogTemp,Display,TEXT("NPCOutfitsQA %s %s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Reason,*Directory);
        if (bQuit) FPlatformMisc::RequestExit(false);
        return false;
    }
    bool Tick(float Delta)
    {
        const double Now=FPlatformTime::Seconds();
        auto* W=World.Get(); auto* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr; APawn* Player=PC?PC->GetPawn():nullptr;
        if (!Player || Now-Started>Timeout) return Finish(false,TEXT("Missing player or internal timeout"));
        if (Now<Due) return true;
        Peak=FMath::Max(Peak,static_cast<double>(Delta)*1000);
        if (Stage==0)
        {
            auto* Roster=LoadObject<UProjectNPCRoster>(nullptr,TEXT("/Game/_Game/Characters/Data/DA_NPCRoster.DA_NPCRoster"));
            const auto* Row=Roster?Roster->Archetypes.FindByPredicate([this](const auto& Entry){return Entry.Id==FName(*Archetype);}):nullptr;
            if (!Row) return Finish(false,TEXT("Archetype missing"));
            auto Class=(Body.StartsWith(TEXT("Female"))?Row->Female:Row->Male).LoadSynchronous();
            if (!Class) return Finish(false,TEXT("NPC class missing"));
            const FVector Origin=Player->GetActorLocation()+FVector(1200,0,25);
            ReadyStart=FPlatformTime::Seconds();
            for (int32 I=0; I<Count; ++I)
            {
                const FVector Location=Origin+FVector((I/5)*230,(I%5)*180,0);
                auto* P=W->SpawnActorDeferred<ACharacter>(Class,FTransform(FRotator(0,180,0),Location),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                if (!P) return Finish(false,TEXT("Spawn failed"));
                const FString Id=FString::Printf(TEXT("EF.AppearanceId.OutfitQA.%d.%d"),Seed,I); Identities.Add(Id); P->Tags.Add(FName(*Id));
                // The fixture requests its final body before admitting clothing,
                // matching production initialization instead of fitting twice.
                auto* Outfit=NewObject<UProjectNPCOutfitComponent>(P,TEXT("ProjectNPCOutfit")); Outfit->bEnabled=false;
                P->AddInstanceComponent(Outfit); Outfit->RegisterComponent();
                UGameplayStatics::FinishSpawningActor(P,P->GetActorTransform());
                Pawns.Add(P); P->SetActorEnableCollision(false); P->GetCharacterMovement()->DisableMovement();
                P->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                if (auto* AI=Cast<AAIController>(P->GetController()); AI && AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Controlled outfit measurement"));
            }
            Report->SetNumberField(TEXT("spawn_cpu_ms"),(FPlatformTime::Seconds()-ReadyStart)*1000);
            PreviousView=PC->GetViewTarget(); Camera=W->SpawnActor<ACameraActor>();
            const FVector Focus=Origin+FVector((Count>5?(Count/5-1)*115:0),FMath::Min(Count-1,4)*90,-15);
            const FVector Eye=Focus+FVector(-FMath::Max(900,Count*32),0,350);
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(65); PC->SetViewTarget(Camera.Get());
            Report->SetStringField(TEXT("camera"),Eye.ToString()+TEXT(" -> ")+Focus.ToString());
            Stage=1; return true;
        }
        if (Stage==1)
        {
            for (auto Weak:Pawns)
            {
                auto* P=Weak.Get(); auto* Custom=P?P->FindComponentByClass<UEFCharacterCustomizationComponent>():nullptr;
                if (!Custom || Custom->GetBodyPresetId().IsNone()) return true;
                auto* Saved=AProjectNPCAppearanceState::Ensure(W);
                if (!Saved || !Saved->Records.Contains(AProjectNPCAppearanceState::IdentityFor(P))) return true;
            }
            if (!Body.IsEmpty()) for (auto Weak:Pawns)
                if (!Weak->FindComponentByClass<UEFCharacterCustomizationComponent>()->SelectBodyPreset(FName(*Body),true)) return Finish(false,TEXT("Body selection rejected"));
            Report->SetNumberField(TEXT("body_initialization_seconds"),FPlatformTime::Seconds()-ReadyStart);
            if (bDressed) for (auto Weak:Pawns)
            {
                Weak->FindComponentByClass<UProjectNPCOutfitComponent>()->bEnabled=true;
                W->GetSubsystem<UProjectNPCOutfitSubsystem>()->AppearanceReady(Weak.Get());
            }
            Stage=2; return true;
        }
        if (Stage==2)
        {
            for (auto Weak:Pawns)
            {
                auto* P=Weak.Get(); if (!P) return Finish(false,TEXT("Fixture destroyed"));
                if (auto* Damage=P->FindComponentByClass<UACFDamageHandlerComponent>(); Damage && !Damage->GetIsAlive()) return Finish(false,TEXT("Fixture died"));
                auto* Native=P->FindComponentByClass<UACFEquipmentComponent>();
                if (bDressed)
                {
                    auto* Outfit=P->FindComponentByClass<UProjectNPCOutfitComponent>();
                    if (Outfit->State==EProjectNPCOutfitState::Failed) return Finish(false,Outfit->Failure);
                    if (Outfit->State!=EProjectNPCOutfitState::Ready) return true;
                    int32 Rags=0;
                    for (const auto& Item:Native->GetInventory()) if (Item.ItemClass->GetPathName().StartsWith(TEXT("/Game/_Game/Clothes/Rags01/")))
                    { if (Item.Count!=1 || !Item.bIsEquipped) return Finish(false,TEXT("Rag quantity/equip mismatch")); ++Rags; }
                    if (Rags!=3) return Finish(false,TEXT("Expected three FullRag entries"));
                    auto* Clothing=P->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>();
                    TInlineComponentArray<UACFArmorSlotComponent*> Slots(P); int32 Ready=0;
                    for (auto* Slot:Slots) if (Slot->GetSkeletalMeshAsset() && Slot->IsVisible() && !Slot->bHiddenInGame
                        && Clothing->GetGarmentRuntimeState(Slot)==EEFClothingMorphV3RuntimeState::Ready) ++Ready;
                    if (Ready<3) return true;
                    if (P->FindComponentByClass<UEFEquipmentCoordinatorComponent>()->HasNativeDeathAdapter()) return Finish(false,TEXT("NPC acquired player death adapter"));
                }
            }
            BeforeIds.Reset();
            for (auto Weak:Pawns)
            {
                TArray<FGuid> Ids;
                for (const auto& Item:Weak->FindComponentByClass<UACFEquipmentComponent>()->GetInventory()) Ids.Add(Item.GetItemGuid());
                BeforeIds.Add(Ids);
            }
            ReadyTime=Now-ReadyStart;
            TArray<TSharedPtr<FJsonValue>> InitializationRows;
            for (auto P : Pawns) if (P.IsValid()) if (const auto* Outfit=P->FindComponentByClass<UProjectNPCOutfitComponent>())
            {
                auto Row=MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("actor"),P->GetPathName());
                Row->SetNumberField(TEXT("spawn_to_admission_seconds"),Outfit->EngineSecondsAtStart-(ReadyStart-GStartTime));
                Row->SetNumberField(TEXT("initialization_seconds"),Outfit->ReadySeconds);
                Row->SetNumberField(TEXT("definition_load_seconds"),Outfit->DefinitionLoadSeconds);
                Row->SetNumberField(TEXT("item_classes_load_seconds"),Outfit->ItemClassesLoadSeconds);
                Row->SetBoolField(TEXT("definition_resident_at_request"),Outfit->bDefinitionResidentAtRequest);
                Row->SetNumberField(TEXT("item_classes_requested"),Outfit->ItemClassesRequested);
                Row->SetNumberField(TEXT("item_classes_resident_at_request"),Outfit->ItemClassesResidentAtRequest);
                InitializationRows.Add(MakeShared<FJsonValueObject>(Row));
            }
            Report->SetArrayField(TEXT("fixture_initializations"),InitializationRows);
            Report->SetStringField(TEXT("admission_timing_scope"),TEXT("Since group spawn started; includes body appearance resolution and queue wait. Initialization includes streaming, scheduling, equipment and render submission."));
            if (auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("Project.NPCOutfits.MaxActiveInitializations")))
                Report->SetNumberField(TEXT("max_active_initializations"),FMath::Clamp(Limit->GetInt(),1,16));
            Report->SetBoolField(TEXT("threaded_rendering"), GIsThreadedRendering);
            Report->SetBoolField(TEXT("threaded_rendering_requested"), GUseThreadedRendering);
            Report->SetBoolField(TEXT("rhi_separate_thread"), IsRunningRHIInSeparateThread());
            Report->SetNumberField(TEXT("spawn_peak_ms"),Peak);
            IFileManager::Get().MakeDirectory(*Directory,true);
            if (FScreenshotRequest::IsScreenshotRequested()) return Finish(false,TEXT("Another screenshot request is active"));
            ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddSPLambda(
                this, [this]() { bScreenshotProcessed=true; });
            FScreenshotRequest::RequestScreenshot(Directory/TEXT("outfits.png"),false,false);
            Stage=3; return true;
        }
        if (Stage==3)
        {
            if (!bScreenshotProcessed) return true;
            if (!bMeasurementStarted)
            {
                // The request may stall rendering/readback. Start on the next
                // tick after its completion, keeping readiness timing separate.
                FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
                ScreenshotProcessedHandle.Reset();
                CaptureStart=Now;
                bMeasurementStarted=true;
                Report->SetNumberField(TEXT("ready_to_capture_seconds"),Now-ReadyStart-ReadyTime);
                Report->SetBoolField(TEXT("screenshot_processed_before_capture"),true);
                return true;
            }
            double SurfaceMs=0, ReconcileMs=0;
            for (auto P:Pawns) if (P.IsValid()) if (auto* Clothing=P->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
            { SurfaceMs+=Clothing->GetLastSurfacePassMilliseconds(); ReconcileMs+=Clothing->GetLastReconcileMilliseconds(); }
            GameTimes.Add(MakeShared<FJsonValueNumber>(FPlatformTime::ToMilliseconds(GGameThreadTime)));
            RenderTimes.Add(MakeShared<FJsonValueNumber>(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
            SurfaceTimes.Add(MakeShared<FJsonValueNumber>(SurfaceMs)); ReconcileTimes.Add(MakeShared<FJsonValueNumber>(ReconcileMs));
            Frames.Add(MakeShared<FJsonValueNumber>(Delta*1000));
            CPUs.Add(MakeShared<FJsonValueNumber>(FMath::Max(FPlatformTime::ToMilliseconds(GGameThreadTime),FPlatformTime::ToMilliseconds(GRenderThreadTime))));
            GPUs.Add(MakeShared<FJsonValueNumber>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
            if (Now-CaptureStart<CaptureSeconds) return true;
            Report->SetNumberField(TEXT("seconds"),Now-CaptureStart); Report->SetNumberField(TEXT("outfit_ready_seconds"),ReadyTime);
            Report->SetArrayField(TEXT("frame_ms"),Frames); Report->SetArrayField(TEXT("cpu_ms"),CPUs); Report->SetArrayField(TEXT("gpu_ms"),GPUs);
            Report->SetArrayField(TEXT("game_ms"),GameTimes); Report->SetArrayField(TEXT("render_ms"),RenderTimes);
            Report->SetArrayField(TEXT("surface_cpu_ms"),SurfaceTimes); Report->SetArrayField(TEXT("reconcile_cpu_ms"),ReconcileTimes);
            Report->SetNumberField(TEXT("physical_used_bytes"),static_cast<double>(FPlatformMemory::GetStats().UsedPhysical));
            if (bFunctional && bDressed)
            {
                auto* P=Pawns[0].Get(); auto* Native=P->FindComponentByClass<UACFEquipmentComponent>();
                auto* Coordinator=P->FindComponentByClass<UEFEquipmentCoordinatorComponent>();
                for (const auto& Item:Native->GetInventory()) if (Item.ItemClass->GetPathName().Contains(TEXT("RagShirt")))
                { RemovedGuid=Item.GetItemGuid(); if (!Coordinator->RequestUnequip(RemovedGuid).bSuccess) return Finish(false,TEXT("Unequip rejected")); break; }
                if (!RemovedGuid.IsValid()) return Finish(false,TEXT("Shirt missing"));
                Stage=4; Due=Now+1; return true;
            }
            DestroyPawns(); Stage=6; Due=Now+1; return true;
        }
        if (Stage==4)
        {
            // Same initialization input must preserve the legitimately changed inventory.
            W->GetSubsystem<UProjectNPCOutfitSubsystem>()->AppearanceReady(Pawns[0].Get());
            Stage=5; Due=Now+1; return true;
        }
        if (Stage==5)
        {
            auto* Native=Pawns[0]->FindComponentByClass<UACFEquipmentComponent>(); FInventoryItem Shirt;
            if (!RecordCheck(TEXT("Repeated initialization preserves removed shirt"),Native->GetItemByGuid(RemovedGuid,Shirt) && !Shirt.bIsEquipped)) return Finish(false,TEXT("Removed shirt returned"));
            if (!RecordCheck(TEXT("Inventory count unchanged"),Native->GetInventory().Num()==BeforeIds[0].Num())) return Finish(false,TEXT("Duplicate inventory"));
            DestroyPawns(); Stage=6; Due=Now+1; return true;
        }
        if (Stage==6)
        {
            if (bCollectGarbage)
            {
                CollectGarbage(RF_NoFlags, true);
                Report->SetBoolField(TEXT("garbage_collection_completed"),true);
            }
            const int32 Pending=W->GetSubsystem<UProjectNPCOutfitSubsystem>()->GetPendingCount(); Report->SetNumberField(TEXT("pending_after_destroy"),Pending);
            return Finish(Pending==0,Pending?TEXT("Outstanding initialization"):TEXT("Bounded outfit fixture completed; visual review separate"));
        }
        return true;
    }
};
TWeakPtr<FOutfitQA> ActiveOutfitQA;
FAutoConsoleCommandWithWorldAndArgs OutfitQACommand(TEXT("Project.NPCOutfits.QA"),
    TEXT("count=1 body=Male.01 archetype=SkillGuy dressed=1 seed=731 capture=30 timeout=120 name=run functional=0 loading=warm [gc] [quit]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
    {
        if (!W || !W->IsGameWorld() || ActiveOutfitQA.IsValid()) return;
        auto QA=MakeShared<FOutfitQA>(); QA->World=W; const FString Options=FString::Join(Args,TEXT(" "));
        int32 Dressed=1, Functional=0;
        FParse::Value(*Options,TEXT("count="),QA->Count); FParse::Value(*Options,TEXT("seed="),QA->Seed);
        FParse::Value(*Options,TEXT("body="),QA->Body); FParse::Value(*Options,TEXT("archetype="),QA->Archetype);
        FParse::Value(*Options,TEXT("dressed="),Dressed); FParse::Value(*Options,TEXT("functional="),Functional);
        FParse::Value(*Options,TEXT("capture="),QA->CaptureSeconds); FParse::Value(*Options,TEXT("timeout="),QA->Timeout);
        FParse::Value(*Options,TEXT("name="),QA->Name); FParse::Value(*Options,TEXT("loading="),QA->Loading);
        QA->Count=FMath::Clamp(QA->Count,1,50); QA->CaptureSeconds=FMath::Clamp(QA->CaptureSeconds,1.f,45.f); QA->Timeout=FMath::Clamp(QA->Timeout,10.f,180.f);
        QA->bDressed=Dressed!=0; QA->bFunctional=Functional!=0; QA->bQuit=Args.Contains(TEXT("quit"));
        QA->bCollectGarbage=Args.Contains(TEXT("gc"));
        for (TCHAR Ch:QA->Name) if (!FChar::IsAlnum(Ch) && Ch!=TEXT('_') && Ch!=TEXT('-')) return;
        QA->Directory=FPaths::ProjectSavedDir()/TEXT("NPCOutfits/Native")/QA->Name;
        QA->Report->SetNumberField(TEXT("count"),QA->Count); QA->Report->SetNumberField(TEXT("seed"),QA->Seed);
        QA->Report->SetNumberField(TEXT("fixture_version"),3);
        QA->Report->SetStringField(TEXT("body"),QA->Body); QA->Report->SetStringField(TEXT("archetype"),QA->Archetype);
        QA->Report->SetBoolField(TEXT("dressed"),QA->bDressed); QA->Report->SetStringField(TEXT("loading"),QA->Loading);
        FVector2D Size; if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
        QA->Report->SetStringField(TEXT("resolution"),Size.ToString()); QA->Report->SetStringField(TEXT("quality"),TEXT("Current session settings; pair must use identical launch/cvars"));
        ActiveOutfitQA=QA;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([QA](float Delta){return QA->Tick(Delta);}));
    }));
}
#endif
