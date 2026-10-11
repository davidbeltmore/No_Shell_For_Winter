#include "EFCharacterCustomizationComponent.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "Components/ACFEquipmentComponent.h"
#include "Components/ACFCharacterMovementComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "ItemActors/ACFRangedWeaponActor.h"
#include "Components/ACFShootingComponent.h"
#include "Animation/ACFAnimInstance.h"
#include "Animation/ACFBaseOverlayLayer.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace
{
// Pose/shooting fixture, not AI certification. Uses actual player and native
// equipment calls; restores appearance, slot GUIDs, viewpoint and transform.
struct FGunPoseQA : TSharedFromThis<FGunPoseQA>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACharacter> Player, Subject;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AACFRangedWeaponActor> StoredGun;
    TWeakObjectPtr<AActor> OldView;
    FCharacterCustomizationState OldAppearance;
    FTransform OldTransform, HoldHand;
    FRotator OldControl;
    TArray<FInventoryItem> OldInventory;
    TArray<FEquippedItem> OldEquipment;
    FGameplayTag OldWeaponSlot;
    bool bOldImmortal=false,bQuit=false,bCaptured=false,bBow=false;
    int32 Index=0,Stage=0,Completed=0;
    double Start=FPlatformTime::Seconds(),Due=0,NeckBaseline=0,NeckElevationBaseline=0,AmmoBefore=0;
    FString Events,Directory=FPaths::ProjectSavedDir()/TEXT("GunPose/Runtime");
    double MotionStart=0,LastMotionSample=0,MaxCarryNeckAngle=0,MaxAimNeckAngle=0,MotionDistance=0;
    int32 MotionSamples=0,MotionShot=-1;
    FVector MotionPrevious;
    FTransform MotionOrigin;
    bool MotionTick(double Now)
    {
        auto* P=Subject.Get();auto* M=P?P->GetMesh():nullptr;
        auto* Move=P?Cast<UACFCharacterMovementComponent>(P->GetCharacterMovement()):nullptr;
        if (!M || !Move || !P->GetController()) return Finish(false,TEXT("Bow movement subject missing"));
        const double T=Now-MotionStart;
        if (T>=8)
        {
            Move->StopMovementImmediately();Move->SetIsAiming(true);
            P->SetActorTransform(MotionOrigin,false,nullptr,ETeleportType::TeleportPhysics);AimPitch(0);
            Record(FString::Printf(TEXT("bow_motion samples=%d distance_cm=%.2f carry_head_chest_max_deg=%.2f aim_head_chest_max_deg=%.2f"),MotionSamples,MotionDistance,MaxCarryNeckAngle,MaxAimNeckAngle));
            if (MotionSamples<50 || MotionDistance<400 || MaxCarryNeckAngle>32 || MaxAimNeckAngle>45)
                return Finish(false,TEXT("Bow movement/neck coupling contract failed"));
            return Fire(Now);
        }
        const bool Aiming=T>=3 && T<6;
        Move->SetIsAiming(Aiming);
        // Travel back toward the captured player origin. The static fixture is
        // +2200cm along X; moving farther +X walked off the finite HUB floor.
        const FRotator Direction(Aiming?30*FMath::Sin(T):0,180+45*FMath::Sin(T*1.2),0);
        P->GetController()->SetControlRotation(Direction);
        P->AddMovementInput(FRotator(0,Direction.Yaw,0).Vector(),.65f);
        const FVector At=P->GetActorLocation(),Focus=At+FVector(0,0,50),Eye=Focus+FVector(340,170,30);
        Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
        MotionDistance+=FVector::Distance(At,MotionPrevious);MotionPrevious=At;
        if (Now-LastMotionSample>=.05)
        {
            LastMotionSample=Now;++MotionSamples;
            const double Angle=FMath::RadiansToDegrees(M->GetSocketQuaternion(TEXT("head")).AngularDistance(M->GetSocketQuaternion(TEXT("spine_05"))));
            // Allow the native 0.3s stance blend to settle before classifying carry.
            if (T>1 && T<3 || T>6.5) MaxCarryNeckAngle=FMath::Max(MaxCarryNeckAngle,Angle);
            if (T>3.5 && T<6) MaxAimNeckAngle=FMath::Max(MaxAimNeckAngle,Angle);
            const double Elevation=M->GetSocketLocation(TEXT("head")).Z-.5*(M->GetSocketLocation(TEXT("clavicle_l")).Z+M->GetSocketLocation(TEXT("clavicle_r")).Z);
            if (T>.75 && !Move->IsMovingOnGround())
                return Finish(false,TEXT("Bow motion fixture left walkable floor"));
            if (!FMath::IsFinite(Angle) || Neck(P)>NeckBaseline*1.35 || Elevation<NeckElevationBaseline*.7)
            {
                Record(FString::Printf(TEXT("motion_failure t=%.3f angle=%.3f neck=%.3f baseline=%.3f elevation=%.3f baseline=%.3f"),T,Angle,Neck(P),NeckBaseline,Elevation,NeckElevationBaseline));
                return Finish(false,TEXT("Bow moving pose proportion failed"));
            }
        }
        const int32 Frame=FMath::FloorToInt(T);
        if (Frame!=MotionShot) {MotionShot=Frame;Shot(*FString::Printf(TEXT("motion_%02d"),Frame));}
        return true;
    }
    int32 WeaponCount() const { return bBow?1:2; }
    int32 CaseCount() const { return 4*WeaponCount(); }
    bool IsPlayer() const { return Index>=2*WeaponCount(); }
    bool Female() const { return (Index/WeaponCount())%2!=0; }
    bool Rifle() const { return !bBow && Index%2!=0; }
    const TCHAR* WeaponName() const { return bBow?TEXT("bow"):Rifle()?TEXT("rifle"):TEXT("pistol"); }
    void AimPitch(float Pitch)
    {
        if (Subject.IsValid() && Subject->GetController()) Subject->GetController()->SetControlRotation(FRotator(Pitch,0,0));
    }
    double Ammo(AACFRangedWeaponActor* Gun) const { return bBow?Gun->GetShootingComponent()->GetTotalAmmoCount():Gun->GetCurrentAmmo(); }
    void Record(const FString& S)
    {
        Events+=S+TEXT("\n"); UE_LOG(LogTemp,Display,TEXT("[GunPose] %s"),*S);
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("progress.txt")));
    }
    UACFEquipmentComponent* Equipment(ACharacter* P) const { return P?P->FindComponentByClass<UACFEquipmentComponent>():nullptr; }
    void ClearNPC()
    {
        if (auto* P=Subject.Get(); P && P!=Player.Get())
        {
            if (auto* State=AProjectNPCAppearanceState::Ensure(World.Get())) State->Records.Remove(AProjectNPCAppearanceState::IdentityFor(P));
            if (P->GetController()) P->GetController()->Destroy();
            P->Destroy();
        }
        Subject.Reset();
    }
    double Neck(ACharacter* P) const
    {
        auto* M=P->GetMesh();
        return FVector::Distance(M->GetSocketLocation(TEXT("head")),
            .5*(M->GetSocketLocation(TEXT("clavicle_l"))+M->GetSocketLocation(TEXT("clavicle_r"))));
    }
    bool Pose(const TCHAR* Mode)
    {
        auto* P=Subject.Get();auto* M=P?P->GetMesh():nullptr;auto* A=M?Cast<UACFAnimInstance>(M->GetAnimInstance()):nullptr;
        if (!A) return false;
        auto* C=P->FindComponentByClass<UEFCharacterCustomizationComponent>();
        const FGameplayTag Tag=FGameplayTag::RequestGameplayTag(bBow?TEXT("Moveset.Bow"):Rifle()?TEXT("Moveset.Rifle"):TEXT("Moveset.Pistol"));
        FOverlayLayer Row; const bool Has=A->GetAnimationOverlay(Tag,Row);
        const FString Expected=bBow?TEXT("ABP_DAZBowOverlay_C"):Rifle()?TEXT("ABP_DAZRifleOverlay_C"):TEXT("ABP_DAZPistolOverlay_C");
        const double Length=Neck(P),Ratio=Length/FMath::Max(NeckBaseline,1.0);
        const double Elevation=M->GetSocketLocation(TEXT("head")).Z-.5*(M->GetSocketLocation(TEXT("clavicle_l")).Z+M->GetSocketLocation(TEXT("clavicle_r")).Z);
        Record(FString::Printf(TEXT("head_elevation_cm=%.3f baseline=%.3f"),Elevation,NeckElevationBaseline));
        const FVector Scale=M->GetSocketTransform(TEXT("head"),RTS_Component).GetScale3D();
        const auto* Movement=Cast<UACFCharacterMovementComponent>(P->GetCharacterMovement());
        Record(FString::Printf(TEXT("case=%d role=%s body=%s weapon=%s mode=%s neck_cm=%.3f baseline_cm=%.3f ratio=%.3f head_scale=%s overlay=%s"),
            Index,IsPlayer()?TEXT("player"):TEXT("npc"),C?*C->GetBodyPresetId().ToString():TEXT("missing"),WeaponName(),Mode,Length,NeckBaseline,Ratio,*Scale.ToString(),Has?*GetNameSafe(Row.Overlay.Get()):TEXT("missing")));
        return Has && Row.Overlay && Row.Overlay->GetName()==Expected && A->GetCurrentOverlay()==Tag &&
            Movement && Movement->GetACFAnimInstance()==A && FMath::IsFinite(Length) && Ratio<1.35 && Elevation>NeckElevationBaseline*(bBow?.70:.55) &&
            Scale.GetMin()>.6 && Scale.GetMax()<1.5 &&
            M->GetSocketLocation(TEXT("head")).Z-M->GetSocketLocation(TEXT("foot_l")).Z>75;
    }
    void Shot(const TCHAR* Mode)
    {
        FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Case_%02d_%s.png"),Index,Mode),false,false);
    }
    bool Fire(double Now)
    {
        auto* Gun=Cast<AACFRangedWeaponActor>(Equipment(Subject.Get())->GetCurrentMainWeapon());
        // A restored player may still have firearm ammunition in its ammo slot.
        // Use the native reload/equip path to select arrows for this bow fixture.
        if (bBow && Gun && Gun->GetShootingComponent()) Gun->GetShootingComponent()->Reload(true);
        if (!Gun || !Gun->CanShoot()) return Finish(false,TEXT("Native shooting unavailable"));
        AmmoBefore=Ammo(Gun);Gun->ShootAtDirection(FRotator(30,0,0));Stage=5;Due=Now+.5;return true;
    }
    bool Finish(bool Pass,const FString& Reason)
    {
        ClearNPC();
        if (auto* P=Player.Get(); P && bCaptured)
        {
            auto* E=Equipment(P);E->SheathCurrentWeapon();
            // Remove only fixture-added identities, never clear the player's inventory.
            for (const auto& Item:E->GetInventory())
                if (!OldInventory.ContainsByPredicate([&](const auto& Old){return Old.GetItemGuid()==Item.GetItemGuid();}))
                { E->UnequipItemByGuid(Item.GetItemGuid());E->RemoveItem(Item,Item.Count); }
            for (const auto& Slot:E->GetCurrentEquipment().GetEquippedItems())
                if (!OldEquipment.ContainsByPredicate([&](const auto& Old){return Old.ItemGuid==Slot.ItemGuid && Old.ItemSlot==Slot.ItemSlot;})) E->UnequipItemByGuid(Slot.ItemGuid);
            for (const auto& Old:OldEquipment)
            {
                FEquippedItem Existing;
                if (E->GetEquippedItem(Old.ItemGuid,Existing) && Existing.ItemSlot==Old.ItemSlot) continue;
                for (const auto& Item:E->GetInventory()) if (Item.GetItemGuid()==Old.ItemGuid) { E->EquipItemFromInventoryInSlot(Item,Old.ItemSlot);break; }
            }
            if (OldWeaponSlot.IsValid()) E->UseEquippedItemBySlot(OldWeaponSlot);
            auto* C=P->FindComponentByClass<UEFCharacterCustomizationComponent>();if (C) C->ApplyState(OldAppearance);
            if (auto* M=Cast<UACFCharacterMovementComponent>(P->GetCharacterMovement())) M->SetIsAiming(false);
            P->GetCharacterMovement()->StopMovementImmediately();
            P->SetActorTransform(OldTransform,false,nullptr,ETeleportType::TeleportPhysics);
            if (auto* D=P->FindComponentByClass<UACFDamageHandlerComponent>()) D->SetIsImmortal(bOldImmortal);
            if (auto* PC=UGameplayStatics::GetPlayerController(World.Get(),0)) {PC->SetControlRotation(OldControl);if (OldView.IsValid()) PC->SetViewTarget(OldView.Get());}
            // Native shots consume inventory ammunition. Refund only the original
            // identities/counts changed by this fixture, retaining current slot flags.
            for (const auto& Old:OldInventory)
            {
                FInventoryItem Current;
                if (E->GetInventoryListConst().GetItem(Old.GetItemGuid(),Current) && Current.Count!=Old.Count)
                {
                    Record(FString::Printf(TEXT("refund=%s old_count=%d current_count=%d"),*GetNameSafe(Old.ItemClass.Get()),Old.Count,Current.Count));
                    Current.Count=Old.Count;
                    auto* ListProperty=FindFProperty<FStructProperty>(E->GetClass(),TEXT("InventoryList"));
                    if (ListProperty && ListProperty->Struct==FACFInventoryList::StaticStruct())
                        ListProperty->ContainerPtrToValuePtr<FACFInventoryList>(E)->ChangeEntry(Current);
                }
            }
            E->RefreshTotalWeight();
            const auto Inventory=E->GetInventory();
            for (const auto& Old:OldInventory) if (!Inventory.ContainsByPredicate([&](const auto& Item){return Item.GetItemGuid()==Old.GetItemGuid() && Item.Count==Old.Count;})) {Pass=false;Record(TEXT("restoration=FAIL inventory identity/count"));}
            if (Inventory.Num()!=OldInventory.Num() || !C || C->GetBodyPresetId()!=OldAppearance.BodyPresetId) {Pass=false;Record(TEXT("restoration=FAIL appearance/count"));}
        }
        if (Camera.IsValid()) Camera->Destroy();
        Record(FString::Printf(TEXT("status=%s completed=%d/%d reason=%s elapsed_seconds=%.3f"),Pass?TEXT("PASS"):TEXT("FAIL"),Completed,CaseCount(),*Reason,FPlatformTime::Seconds()-Start));
        FFileHelper::SaveStringToFile(Events,*(Directory/TEXT("runtime.txt")));
        if (bQuit) FPlatformMisc::RequestExit(false);return false;
    }
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();if (Now-Start>110) return Finish(false,TEXT("110-second watchdog"));
        auto* W=World.Get();auto* PC=W?UGameplayStatics::GetPlayerController(W,0):nullptr;
        auto* Hero=PC?Cast<ACharacter>(PC->GetPawn()):nullptr;if (!Hero) return true;
        if (Stage==30) return MotionTick(Now);
        if (Now<Due) return true;
        if (Stage==0 && Now-Start<5) return true;
        if (Stage==0)
        {
            Player=Hero;auto* C=Hero->FindComponentByClass<UEFCharacterCustomizationComponent>();auto* E=Equipment(Hero);
            if (!C || !E) return Finish(false,TEXT("Player appearance/equipment missing"));
            OldAppearance=C->CaptureCurrentState();OldTransform=Hero->GetActorTransform();OldControl=PC->GetControlRotation();OldView=PC->GetViewTarget();
            OldInventory=E->GetInventory();OldEquipment=E->GetCurrentEquipment().GetEquippedItems();
            for (const auto& Slot:OldEquipment) if (Slot.ItemActor==E->GetCurrentMainWeapon()) OldWeaponSlot=Slot.ItemSlot;
            if (auto* D=Hero->FindComponentByClass<UACFDamageHandlerComponent>()) {bOldImmortal=D->GetIsImmortal();D->SetIsImmortal(true);}
            bCaptured=true;Camera=W->SpawnActor<ACameraActor>();Stage=1;Due=Now+1;return true;
        }
        if (Stage==1)
        {
            if (Index==CaseCount()) return Finish(true,bBow?TEXT("bow x 2 genders x actual player/NPC; directional aim, native shooting and restoration"):TEXT("2 weapons x 2 genders x actual player/NPC; native shooting and restoration"));
            ClearNPC();const FVector At=OldTransform.GetLocation()+FVector(2200,0,20);
            if (IsPlayer()) Subject=Hero;
            else
            {
                const FString Name=Female()?TEXT("ACFGunEnemyBPFemale"):TEXT("ACFGunEnemyBPMale");const FString Path=TEXT("/Game/_Game/Characters/")+FString(Female()?TEXT("Female/"):TEXT("Male/"))+Name+TEXT(".")+Name+TEXT("_C");
                auto* Class=LoadClass<ACharacter>(nullptr,*Path);FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Subject=Class?W->SpawnActor<ACharacter>(Class,At,FRotator::ZeroRotator,Params):nullptr;
                if (!Subject.IsValid()) return Finish(false,TEXT("NPC spawn missing"));
                if (bBow && !Subject->GetController()) Subject->SpawnDefaultController();
                if (bBow && !Subject->GetController()) return Finish(false,TEXT("Bow fixture requires native NPC controller"));
                Subject->SetActorEnableCollision(false);Subject->GetCharacterMovement()->DisableMovement();
                if (auto* AI=Cast<AAIController>(Subject->GetController());AI && AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Gun pose fixture"));
                if (bBow && Subject->GetController()) Subject->GetController()->SetActorTickEnabled(false);
            }
            auto* P=Subject.Get();P->SetActorLocationAndRotation(At,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            P->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            const FVector Focus=At+FVector(0,0,50),Eye=Focus+FVector(340,170,30);
            Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());PC->SetViewTarget(Camera.Get());PC->SetControlRotation(FRotator::ZeroRotator);
            Stage=10;Due=FPlatformTime::Seconds()+3;return true;
        }
        if (Stage==10)
        {
            auto* P=Subject.Get();auto* C=P?P->FindComponentByClass<UEFCharacterCustomizationComponent>():nullptr;
            if (!C || !C->SelectBodyPreset(Female()?FName(TEXT("Female.01")):FName(IsPlayer()?TEXT("Male.02"):TEXT("Male.01")),false)) return Finish(false,TEXT("Preset unavailable after initialization"));
            Equipment(P)->SheathCurrentWeapon();Cast<UACFCharacterMovementComponent>(P->GetCharacterMovement())->SetIsAiming(false);
            Stage=2;Due=Now+1;return true;
        }
        if (Stage==2)
        {
            auto* P=Subject.Get();if (!P) return Finish(false,TEXT("Subject lost"));NeckBaseline=Neck(P);
            NeckElevationBaseline=P->GetMesh()->GetSocketLocation(TEXT("head")).Z-.5*(P->GetMesh()->GetSocketLocation(TEXT("clavicle_l")).Z+P->GetMesh()->GetSocketLocation(TEXT("clavicle_r")).Z);
            auto* E=Equipment(P);const FString Name=bBow?TEXT("ACFBowBP"):Rifle()?TEXT("ACFAutomaticRifleBP"):TEXT("ACFPistolBP");
            if (bBow)
            {
                auto* Arrow=LoadClass<UACFItem>(nullptr,TEXT("/Game/FullSample/Blueprints/Items/Ammo/ACF_Arrow_BP.ACF_Arrow_BP_C"));
                if (!Arrow) return Finish(false,TEXT("Native arrow definition missing"));
                if (!E->GetInventory().ContainsByPredicate([&](const auto& I){return I.ItemClass==Arrow && I.Count>0;})) E->AddItemToInventoryByClass(Arrow,10,false);
            }
            auto* Class=LoadClass<UACFItem>(nullptr,*(TEXT("/Game/FullSample/Blueprints/Items/Weapons/")+Name+TEXT(".")+Name+TEXT("_C")));
            if (!Class) return Finish(false,TEXT("Weapon definition missing"));
            auto Items=E->GetInventory();
            if (!Items.ContainsByPredicate([&](const auto& I){return I.ItemClass==Class;})) E->AddItemToInventoryByClass(Class,1,false);
            Items=E->GetInventory();const auto* Item=Items.FindByPredicate([&](const auto& I){return I.ItemClass==Class;});
            if (!Item) return Finish(false,TEXT("Weapon add failed"));
            E->EquipItemFromInventory(*Item);FEquippedItem Slot;
            if (!E->GetEquippedItem(Item->GetItemGuid(),Slot)) return Finish(false,TEXT("Weapon equip failed"));
            E->UseEquippedItemBySlot(Slot.ItemSlot);Stage=3;Due=Now+1;return true;
        }
        if (Stage==3)
        {
            if (!Pose(TEXT("hold"))) return Finish(false,TEXT("Held weapon pose/cache contract failed"));
            Shot(TEXT("hold"));HoldHand=Subject->GetMesh()->GetSocketTransform(TEXT("hand_r"),RTS_Component);
            Cast<UACFCharacterMovementComponent>(Subject->GetCharacterMovement())->SetIsAiming(true);Stage=4;Due=Now+1;return true;
        }
        if (Stage==4)
        {
            if (!Pose(TEXT("aim"))) return Finish(false,TEXT("Aimed weapon pose/cache contract failed"));
            const auto Hand=Subject->GetMesh()->GetSocketTransform(TEXT("hand_r"),RTS_Component);
            const double Delta=FVector::Distance(Hand.GetLocation(),HoldHand.GetLocation()),Angle=Hand.GetRotation().AngularDistance(HoldHand.GetRotation());
            Record(FString::Printf(TEXT("aim_transition_cm=%.3f radians=%.3f"),Delta,Angle));
            if (!bBow && Delta<1 && Angle<.02) return Finish(false,TEXT("Aim pose did not change"));
            Shot(TEXT("aim"));
            if (bBow) {HoldHand=Hand;AimPitch(-60);Stage=20;Due=Now+1;return true;}
            return Fire(Now);
        }
        if (Stage==20 || Stage==21)
        {
            if (!Pose(Stage==20?TEXT("aim_down"):TEXT("aim_up"))) return Finish(false,TEXT("Directional bow pose failed"));
            const auto Hand=Subject->GetMesh()->GetSocketTransform(TEXT("hand_r"),RTS_Component);
            const double Delta=FVector::Distance(Hand.GetLocation(),HoldHand.GetLocation()),Angle=Hand.GetRotation().AngularDistance(HoldHand.GetRotation());
            Record(FString::Printf(TEXT("directional_aim pitch=%d hand_delta_cm=%.3f radians=%.3f"),Stage==20?-60:60,Delta,Angle));
            // Idle breathing can move a hand >1cm without aiming the weapon.
            // Require a substantial directional rotation, not incidental motion.
            if (Angle<.15) return Finish(false,TEXT("Directional bow aim frozen or insufficient"));
            Shot(Stage==20?TEXT("aim_down"):TEXT("aim_up"));
            if (Stage==20) {AimPitch(60);Stage=21;Due=Now+1;return true;}
            AimPitch(0);
            if (IsPlayer())
            {
                MotionStart=Now;LastMotionSample=Now;MotionSamples=0;MotionShot=-1;
                MaxCarryNeckAngle=MaxAimNeckAngle=MotionDistance=0;
                MotionOrigin=Subject->GetActorTransform();MotionPrevious=MotionOrigin.GetLocation();
                Subject->SetActorRotation(FRotator(0,180,0));
                Subject->GetController()->SetControlRotation(FRotator(0,180,0));
                Stage=30;return true;
            }
            return Fire(Now);
        }
        if (Stage==5)
        {
            auto* Gun=Cast<AACFRangedWeaponActor>(Equipment(Subject.Get())->GetCurrentMainWeapon());
            if (!Gun || Ammo(Gun)>=AmmoBefore || !Pose(TEXT("after_shoot"))) return Finish(false,TEXT("Native shot/ammo or post-shot pose failed"));
            Record(FString::Printf(TEXT("native_shot=PASS ammo_before=%.1f ammo_after=%.1f"),AmmoBefore,Ammo(Gun)));
            Cast<UACFCharacterMovementComponent>(Subject->GetCharacterMovement())->SetIsAiming(false);
            StoredGun=Gun;Equipment(Subject.Get())->SheathCurrentWeapon();Stage=6;Due=Now+.5;return true;
        }
        if (Stage==6 && bBow)
        {
            ++Index;++Completed;Stage=1;Due=Now+.3;return true;
        }
        if (Stage==6)
        {
            auto* P=Subject.Get();auto* Gun=StoredGun.Get();auto* Mesh=P?P->GetMesh():nullptr;
            if (!Gun || !Mesh) return Finish(false,TEXT("Stowed weapon missing"));
            const FName Name=Gun->GetOnBodySocketName();
            const bool NativeSocket=Mesh->DoesSocketExist(Name);
            auto* Reference=LoadObject<USkeletalMesh>(nullptr,TEXT("/AscentCombatFramework/Animation/SkeletalMeshes/SKM_Manny.SKM_Manny"));
            const auto* Ref=Reference?Reference->FindSocket(Name):nullptr;
            if (!NativeSocket && !Ref) return Finish(false,TEXT("Stow reference unavailable"));
            const FName Attach=NativeSocket?Name:Ref->BoneName;
            const FTransform Expected=NativeSocket?Mesh->GetSocketTransform(Name):FTransform(Ref->RelativeRotation,Ref->RelativeLocation,Ref->RelativeScale)*Mesh->GetSocketTransform(Attach);
            const double Error=FVector::Distance(Gun->GetActorLocation(),Expected.GetLocation());
            Record(FString::Printf(TEXT("stow=%s native_socket=%s attach=%s error_cm=%.3f"),*Name.ToString(),NativeSocket?TEXT("true"):TEXT("false"),*Gun->GetRootComponent()->GetAttachSocketName().ToString(),Error));
            Shot(TEXT("stow"));
            // The actual player equips native weapons on its configured compatibility
            // mesh. Require owned skeletal parent and alignment to the visible pose.
            const auto* Parent=Cast<USkeletalMeshComponent>(Gun->GetRootComponent()->GetAttachParent());
            Record(FString::Printf(TEXT("stow_parent=%s owner=%s"),*GetNameSafe(Parent),Parent?*GetNameSafe(Parent->GetOwner()):TEXT("missing")));
            if (!Parent || Parent->GetOwner()!=P || Gun->GetRootComponent()->GetAttachSocketName()!=Attach || Error>2) return Finish(false,TEXT("Stowed weapon attachment contract failed"));
            ++Index;++Completed;Stage=1;Due=Now+.3;return true;
        }
        return true;
    }
};
TWeakPtr<FGunPoseQA> ActiveGunPose;
FAutoConsoleCommandWithWorldAndArgs GunPoseCommand(TEXT("Project.GunPose.QA"),TEXT("Bounded 110s actual player/NPC gun pose smoke; optional quit."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
    {
        if (!W || ActiveGunPose.IsValid()) return;
        auto Test=MakeShared<FGunPoseQA>();Test->World=W;Test->bQuit=Args.Contains(TEXT("quit"));Test->bBow=Args.Contains(TEXT("bow"));ActiveGunPose=Test;
        if (Test->bBow) Test->Directory=FPaths::ProjectSavedDir()/TEXT("BowPose/Runtime");
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Dt){return Test->Tick(Dt);}));
    }));
}
#endif
