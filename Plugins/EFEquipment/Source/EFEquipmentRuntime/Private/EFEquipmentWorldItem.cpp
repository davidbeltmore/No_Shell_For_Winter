#include "EFEquipmentWorldItem.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "Components/ACFStorageComponent.h"
#include "GameFramework/Pawn.h"
#include "EFEquipmentPrototypeActor.h"
#include "Components/StaticMeshComponent.h"
#include "ALSLoadAndSaveComponent.h"
#include "UObject/UnrealType.h"

void AEFEquipmentWorldItem::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    // ALS world restoration explicitly deserializes this actor and its storage.
    // Per-actor BeginPlay auto-reload uses only the actor name; a newly dropped
    // bag/currency pickup can reuse a collected actor's name and replay old loot.
    // Disable that second loader before component BeginPlay, for this subclass only.
    auto* AutoReload = FindFProperty<FBoolProperty>(UALSLoadAndSaveComponent::StaticClass(), TEXT("bAutoReload"));
    if (!ensureAlwaysMsgf(AutoReload, TEXT("The native ALS auto-reload contract changed."))) return;
    TArray<UALSLoadAndSaveComponent*> SaveComponents; GetComponents(SaveComponents);
    for (auto* Component : SaveComponents) AutoReload->SetPropertyValue_InContainer(Component, false);
}

TArray<UActorComponent*> AEFEquipmentWorldItem::GetComponentsToSave_Implementation() const
{
    auto Result = Super::GetComponentsToSave_Implementation();
    if (StorageComponent) Result.AddUnique(StorageComponent);
    return Result;
}

void AEFEquipmentWorldItem::BeginPlay()
{
    Super::BeginPlay();
    const auto Items = GetItems();
    if (ObjectMesh && Items.Num() == 1 && Items[0].Item)
        if (auto* Class = Items[0].Item->GetItemActorClass().Get(); Class && Class->IsChildOf(AEFEquipmentPrototypeActor::StaticClass()))
            ObjectMesh->SetRelativeScale3D(Class->GetDefaultObject<AEFEquipmentPrototypeActor>()->MeshScale);
}

void AEFEquipmentWorldItem::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
    bPickupCompleted = false;
    if (!Pawn || !StorageComponent) return;
    auto* Coordinator = Pawn->FindComponentByClass<UEFEquipmentCoordinatorComponent>();
    if (!Coordinator && UEFEquipmentCoordinatorComponent::IsManagedPlayer(Pawn)) Coordinator = UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn);
    if (!Coordinator)
    {
        Super::OnInteractedByPawn_Implementation(Pawn, InteractionType);
        bPickupCompleted = IsActorBeingDestroyed() || StorageComponent->GetInventory().IsEmpty();
        return;
    }
    for (const auto& Item : StorageComponent->GetInventory())
        Coordinator->TakeFromInventory(StorageComponent, Item.GetItemGuid(), Item.Count, true);
    auto* Destination = Coordinator->GetNativeEquipment();
    if (Destination) Destination->GatherCurrency(StorageComponent->GetCurrentCurrencyAmount(), StorageComponent);
    bPickupCompleted = StorageComponent->GetInventory().IsEmpty() && StorageComponent->GetCurrentCurrencyAmount() == 0;
    if (bDestroyOnGather && bPickupCompleted) Destroy();
}
