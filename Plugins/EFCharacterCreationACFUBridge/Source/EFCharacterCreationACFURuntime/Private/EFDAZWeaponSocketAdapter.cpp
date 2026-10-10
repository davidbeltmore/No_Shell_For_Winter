#include "EFDAZWeaponSocketAdapter.h"
#include "EFDAZWeaponPoseSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "ItemActors/ACFWeaponActor.h"
#include "TimerManager.h"

void UEFDAZWeaponSocketAdapter::EnsureAndRefresh(AActor* Actor)
{
    if (!Actor) return;
    auto* Adapter = Actor->FindComponentByClass<UEFDAZWeaponSocketAdapter>();
    if (!Adapter)
    {
        Adapter = NewObject<UEFDAZWeaponSocketAdapter>(Actor, NAME_None, RF_Transient);
        Actor->AddInstanceComponent(Adapter);
        Adapter->RegisterComponent();
        Adapter->Equipment = Actor->FindComponentByClass<UACFEquipmentComponent>();
        if (Adapter->Equipment) Adapter->Equipment->OnEquipmentChanged.AddUniqueDynamic(Adapter, &UEFDAZWeaponSocketAdapter::EquipmentChanged);
    }
    Adapter->EquipmentChanged(FEquipment());
}

void UEFDAZWeaponSocketAdapter::EquipmentChanged(const FEquipment&)
{
    // Native broadcasts can occur before the sheath operation finishes attaching.
    if (bRefreshQueued || !GetWorld()) return;
    bRefreshQueued = true;
    GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]
    {
        bRefreshQueued = false;
        Refresh();
    }));
}

void UEFDAZWeaponSocketAdapter::Refresh()
{
    auto* Character = Cast<ACharacter>(GetOwner());
    auto* Mesh = Character ? Character->GetMesh() : nullptr;
    auto* Body = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    const auto* Settings = GetDefault<UEFDAZWeaponPoseSettings>();
    if (!Equipment || !Body || !Settings->MeshRoots.ContainsByPredicate([Body](const FString& Root)
        { return !Root.IsEmpty() && Body->GetPathName().StartsWith(Root); })) return;
    for (const auto& Slot : Equipment->GetCurrentEquipment().GetEquippedItems())
    {
        auto* Weapon = Cast<AACFWeaponActor>(Slot.ItemActor);
        if (!Weapon || Weapon == Equipment->GetCurrentMainWeapon() || Weapon == Equipment->GetCurrentOffhandWeapon()) continue;
        const FName SocketName = Weapon->GetOnBodySocketName();
        if (!Settings->MissingBodySockets.Contains(SocketName) || Mesh->DoesSocketExist(SocketName)) continue;
        auto* Reference = Settings->SocketReferenceMesh.LoadSynchronous();
        const auto* Socket = Reference ? Reference->FindSocket(SocketName) : nullptr;
        auto* Root = Weapon->GetRootComponent();
        if (!Socket || !Root || Mesh->GetBoneIndex(Socket->BoneName) == INDEX_NONE) continue;
        const FTransform Local(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
        if (Root->GetAttachParent() == Mesh && Root->GetAttachSocketName() == Socket->BoneName && Root->GetRelativeTransform().Equals(Local)) continue;
        Root->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket->BoneName);
        Root->SetRelativeTransform(Local);
        UE_LOG(LogTemp, Display, TEXT("[DAZWeaponSocket] actor=%s weapon=%s missing=%s reference_bone=%s"),
            *Character->GetName(), *Weapon->GetName(), *SocketName.ToString(), *Socket->BoneName.ToString());
    }
}

void UEFDAZWeaponSocketAdapter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Equipment) Equipment->OnEquipmentChanged.RemoveDynamic(this, &UEFDAZWeaponSocketAdapter::EquipmentChanged);
    Super::EndPlay(Reason);
}
