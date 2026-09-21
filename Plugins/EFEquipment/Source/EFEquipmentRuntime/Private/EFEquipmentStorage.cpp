#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentFunctionLibrary.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "EFContainerInventoryComponent.h"
#include "GameFramework/Pawn.h"

FEFEquipmentResult UEFEquipmentCoordinatorComponent::GiveToInventory(UACFInventoryComponent* Destination, FGuid Guid, int32 Count)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    if (!IsValid(Destination) || Destination == Equipment || Destination->GetWorld() != GetWorld() || Destination->IsA<UEFContainerInventoryComponent>())
        return Publish(FEFEquipmentResult::Failure(TEXT("Use the satchel contents panel for internal transfers.")));
    FInventoryItem Entry; FString Error;
    if (!Equipment->GetItemByGuid(Guid, Entry) || !Entry.Item || Count <= 0 || Count > Entry.Count)
        return Publish(FEFEquipmentResult::Failure(TEXT("The selected item or quantity changed.")));
    if (const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
    {
        auto* Contents = ResolveContainer(Guid, Error);
        if (!Contents) return Publish(FEFEquipmentResult::Failure(Error));
        if (Cast<APawn>(Destination->GetOwner()) && !Contents->GetInventory().IsEmpty())
            return Publish(FEFEquipmentResult::Failure(TEXT("Filled satchels cannot be transferred to companions in this version. Extract their contents first.")));
        if (!FlushContainers(Error)) return Publish(FEFEquipmentResult::Failure(Error));
    }
    const FGameplayTag PreviousSlot = Entry.EquipmentSlot;
    if (Entry.bIsEquipped && !RequestUnequip(Guid).bSuccess) return Publish(FEFEquipmentResult::Failure(TEXT("The item could not be unequipped.")));
    bool Success; FEFEquipmentResult Receipt;
    { TGuardValue<bool> Operation(bOperation, true); Success = EFEquipmentACF::Transfer(Equipment, Destination, Guid, Count, false, Error, &Receipt); }
    if (!Success && PreviousSlot.IsValid()) RequestEquip(Guid, PreviousSlot);
    RefreshState(); return Publish(Success ? Receipt : FEFEquipmentResult::Failure(Error));
}

void UEFEquipmentFunctionLibrary::MoveItemsFromInventory(UACFInventoryComponent* Target, const TArray<FInventoryItem>& inItems, UACFInventoryComponent* sourceInventory)
{
    if (!IsValid(Target) || !IsValid(sourceInventory)) return;
    auto* Receiving = UEFEquipmentCoordinatorComponent::FindOrCreate(Target->GetOwner());
    auto* Giving = UEFEquipmentCoordinatorComponent::FindOrCreate(sourceInventory->GetOwner());
    if (Receiving && Receiving->GetNativeEquipment() != Target) Receiving = nullptr;
    if (Giving && Giving->GetNativeEquipment() != sourceInventory) Giving = nullptr;
    if (!Receiving && !Giving) { Target->MoveItemsFromInventory(inItems, sourceInventory); return; }
    // Native widgets supply GUID-bearing entries, including selected quantities.
    // Stop at a rejected row; every completed row has a verified transfer receipt.
    for (const auto& Entry : inItems)
    {
        const auto Result = Receiving ? Receiving->TakeFromInventory(sourceInventory, Entry.GetItemGuid(), Entry.Count, false)
            : Giving->GiveToInventory(Target, Entry.GetItemGuid(), Entry.Count);
        if (!Result.bSuccess) break;
    }
}
