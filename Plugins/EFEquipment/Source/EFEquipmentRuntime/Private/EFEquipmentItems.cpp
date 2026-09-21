#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "ItemActors/ACFItemActor.h"

UEFEquipmentRulesFragment* UEFEquipmentRulesFragment::Find(const UACFItem* Item)
{
    if (Item) for (UACFItemFragment* Fragment : Item->Fragments)
        if (auto* Rules = Cast<UEFEquipmentRulesFragment>(Fragment)) return Rules;
    return nullptr;
}

bool UEFEquipmentRulesFragment::FlushContainer(FString& Error)
{
    if (ContainerSchema != 1) { Error = TEXT("This container uses an unsupported saved version. Its data was preserved."); return false; }
    return !IsValid(RuntimeInventory) || RuntimeInventory->WritePayload(ContainerPayload, Error);
}

void UEFEquipmentRulesFragment::Serialize(FArchive& Ar)
{
    if (Ar.IsSaveGame() && Ar.IsSaving())
    {
        FString Error;
        if (!FlushContainer(Error)) { Ar.SetError(); return; }
    }
    Super::Serialize(Ar);
    if (Ar.IsSaveGame() && Ar.IsLoading()) RuntimeInventory = nullptr;
}

bool UEFEquipmentArmor::CanBeEquipped_Implementation(UACFEquipmentComponent* Equipment)
{ return Super::CanBeEquipped_Implementation(Equipment) && UEFEquipmentCoordinatorComponent::NativeEquipGate(this, Equipment); }

bool UEFEquipmentAccessory::CanBeEquipped_Implementation(UACFEquipmentComponent* Equipment)
{ return Super::CanBeEquipped_Implementation(Equipment) && UEFEquipmentCoordinatorComponent::NativeEquipGate(this, Equipment); }

TSubclassOf<AACFItemActor> UEFEquipmentArmor::GetItemActorClass_Implementation() const
{ return PrototypeActorClass ? PrototypeActorClass : Super::GetItemActorClass_Implementation(); }
TSubclassOf<AACFItemActor> UEFEquipmentAccessory::GetItemActorClass_Implementation() const
{ return PrototypeActorClass ? PrototypeActorClass : Super::GetItemActorClass_Implementation(); }

void UEFEquipmentArmor::Internal_OnEquipped(APawn* Owner)
{
    if (!Owner || bEffectsApplied) return;
    bEffectsApplied = true; Super::Internal_OnEquipped(Owner);
}
void UEFEquipmentArmor::Internal_OnUnEquipped()
{
    if (!bEffectsApplied) return;
    bEffectsApplied = false; Super::Internal_OnUnEquipped();
}
void UEFEquipmentAccessory::Internal_OnEquipped(APawn* Owner)
{
    if (!Owner || bEffectsApplied) return;
    bEffectsApplied = true; Super::Internal_OnEquipped(Owner);
}
void UEFEquipmentAccessory::Internal_OnUnEquipped()
{
    if (!bEffectsApplied) return;
    bEffectsApplied = false; Super::Internal_OnUnEquipped();
}
