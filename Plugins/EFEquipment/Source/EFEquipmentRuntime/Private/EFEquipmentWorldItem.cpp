#include "EFEquipmentWorldItem.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "Components/ACFStorageComponent.h"
#include "GameFramework/Pawn.h"

void AEFEquipmentWorldItem::OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType)
{
    if (!Pawn || !StorageComponent) return;
    auto* Coordinator = UEFEquipmentCoordinatorComponent::IsManagedPlayer(Pawn) ? UEFEquipmentCoordinatorComponent::FindOrCreate(Pawn) : nullptr;
    if (!Coordinator) { Super::OnInteractedByPawn_Implementation(Pawn, InteractionType); return; }
    for (const auto& Item : StorageComponent->GetInventory())
        Coordinator->TakeFromInventory(StorageComponent, Item.GetItemGuid(), Item.Count, true);
    auto* Destination = Coordinator->GetNativeEquipment();
    if (Destination) Destination->GatherCurrency(StorageComponent->GetCurrentCurrencyAmount(), StorageComponent);
    if (bDestroyOnGather && StorageComponent->GetInventory().IsEmpty() && StorageComponent->GetCurrentCurrencyAmount() == 0) Destroy();
}
