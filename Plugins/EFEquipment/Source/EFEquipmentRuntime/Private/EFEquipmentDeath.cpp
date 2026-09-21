#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentDeathRouting.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "UObject/UnrealType.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "TimerManager.h"

namespace EFEquipmentDeathRouting
{
    FMulticastDelegateProperty* Property(UObject* Source, FName Event)
    {
        auto* P = Source ? FindFProperty<FMulticastDelegateProperty>(Source->GetClass(), Event) : nullptr;
        return P && P->SignatureFunction && P->SignatureFunction->NumParms == 0 ? P : nullptr;
    }
    bool Contains(UObject* Source, FName Event, UObject* Receiver, FName Function)
    {
        auto* P = Property(Source, Event); if (!P) return false;
        const auto* Delegate = P->GetMulticastDelegate(P->ContainerPtrToValuePtr<void>(Source));
        return Delegate && Delegate->Contains(Receiver, Function);
    }
    bool Replace(UObject* Source, FName Event, UObject* Original, FName OriginalFunction, UObject* Replacement, FName ReplacementFunction)
    {
        auto* P = Property(Source, Event);
        auto* Function = IsValid(Replacement) ? Replacement->FindFunction(ReplacementFunction) : nullptr;
        if (!P || !Function || Function->NumParms != 0) return false;
        void* Value = P->ContainerPtrToValuePtr<void>(Source);
        const auto* Delegate = P->GetMulticastDelegate(Value);
        if (!Delegate || !Delegate->Contains(Original, OriginalFunction)) return false;
        FMulticastScriptDelegate Copy = *Delegate;
        TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Save(Writer, false); Save << Copy;
        if (Save.IsError() || Bytes.Num() > 65536) return false;
        TArray<FScriptDelegate> Bindings; FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive Load(Reader, false); Load << Bindings;
        if (Load.IsError() || Reader.Tell() != Bytes.Num() || Bindings.Num() > 128) return false;
        FMulticastScriptDelegate Result; bool Replaced = false;
        for (const auto& Binding : Bindings)
        {
            // Reinitialization may have appended the original callback again.
            if (Binding.GetUObject() == Replacement && Binding.GetFunctionName() == ReplacementFunction) continue;
            if (Binding.GetUObject() == Original && Binding.GetFunctionName() == OriginalFunction)
            {
                if (Replaced) return false;
                FScriptDelegate Routed; Routed.BindUFunction(Replacement, ReplacementFunction); Result.Add(Routed); Replaced = true;
            }
            else Result.Add(Binding);
        }
        if (Replaced) P->SetMulticastDelegate(Value, MoveTemp(Result));
        return Replaced;
    }
}

void UEFEquipmentCoordinatorComponent::InstallOwnerDeathAdapter()
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsManagedPlayer(GetOwner())) return;
    auto* Native = GetOwner()->FindFunction(TEXT("HandleCharacterDeath"));
    if (!Native || Native->NumParms != 0 || !Native->HasAnyFunctionFlags(FUNC_Native)) return;
    if (!DeathSource.IsValid())
        for (auto* Component : GetOwner()->GetComponents())
            for (UClass* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
                if (Class->GetPathName() == TEXT("/Script/AscentCombatFramework.ACFDamageHandlerComponent")) { DeathSource = Component; break; }
    if (auto* Source = DeathSource.Get())
        EFEquipmentDeathRouting::Replace(Source, TEXT("OnOwnerDeath"), GetOwner(), TEXT("HandleCharacterDeath"), this, TEXT("HandleNativeOwnerDeath"));
    // Bind after native InitializeCharacter, including later native reinitializations.
    if (auto* Property = EFEquipmentDeathRouting::Property(GetOwner(), TEXT("OnCharacterFullyInitialized")))
    {
        void* Value = Property->ContainerPtrToValuePtr<void>(GetOwner());
        FMulticastScriptDelegate Copy = *Property->GetMulticastDelegate(Value);
        FScriptDelegate Callback; Callback.BindUFunction(this, TEXT("InstallOwnerDeathAdapter")); Copy.AddUnique(Callback);
        Property->SetMulticastDelegate(Value, MoveTemp(Copy));
    }
}

bool UEFEquipmentCoordinatorComponent::HasNativeDeathAdapter() const
{ return EFEquipmentDeathRouting::Contains(DeathSource.Get(), TEXT("OnOwnerDeath"), const_cast<UEFEquipmentCoordinatorComponent*>(this), TEXT("HandleNativeOwnerDeath")); }

void UEFEquipmentCoordinatorComponent::RemoveOwnerDeathAdapter()
{
    EFEquipmentDeathRouting::Replace(DeathSource.Get(), TEXT("OnOwnerDeath"), this, TEXT("HandleNativeOwnerDeath"), GetOwner(), TEXT("HandleCharacterDeath"));
    if (auto* Property = EFEquipmentDeathRouting::Property(GetOwner(), TEXT("OnCharacterFullyInitialized")))
    {
        void* Value = Property->ContainerPtrToValuePtr<void>(GetOwner()); FMulticastScriptDelegate Copy = *Property->GetMulticastDelegate(Value);
        Copy.Remove(this, TEXT("InstallOwnerDeathAdapter")); Property->SetMulticastDelegate(Value, MoveTemp(Copy));
    }
    DeathSource.Reset();
}

void UEFEquipmentCoordinatorComponent::HandleNativeOwnerDeath()
{
    const TWeakObjectPtr<AActor> Owner = GetOwner();
    ProcessOwnerDeath([Owner]() { if (auto* Actor = Owner.Get()) if (auto* Function = Actor->FindFunction(TEXT("HandleCharacterDeath"))) Actor->ProcessEvent(Function, nullptr); });
}

void UEFEquipmentCoordinatorComponent::ProcessOwnerDeath(TFunction<void()> NativeDeath)
{
    if (bDeathInProgress || bDeathDeferred) return;
    if (bOperation)
    {
        bDeathDeferred = true;
        GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Callback = MoveTemp(NativeDeath)]() mutable
        { bDeathDeferred = false; ProcessOwnerDeath(MoveTemp(Callback)); }));
        return;
    }
    TGuardValue<bool> Death(bDeathInProgress, true);
    if (!InitializeEquipment()) { NativeDeath(); return; }
    const auto* DropPolicy = FindFProperty<FBoolProperty>(Equipment->GetClass(), TEXT("bDropItemsOnDeath"));
    const bool DropEnabled = DropPolicy && DropPolicy->GetPropertyValue_InContainer(Equipment);
    TMap<FGuid, float> RetainedChances;
    const auto Before = Equipment->GetInventory();
    for (int32 Index = Before.Num() - 1; Index >= 0; --Index)
    {
        const auto& Entry = Before[Index]; auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
        if (!Rules || !Rules->Profile || !Rules->Profile->bContainer) continue;
        const bool Droppable = Entry.ItemClass && Entry.ItemClass->GetDefaultObject<UACFItem>()->GetItemInfo().bDroppable;
        // Match the native container item's opt-in policy. Contained items never
        // receive separate death rolls or become additional loose loot.
        const float Roll = DropEnabled && Droppable ? FMath::RandRange(0.f, 100.f) : 100.f;
        if (DropEnabled && Droppable && Entry.DropChancePercentage > Roll)
        {
            const auto Result = RequestDrop(Entry.GetItemGuid(), Entry.Count);
            UE_LOG(LogTemp, Display, TEXT("EFEquipment death bag=%s chance=%.3f roll=%.3f dropped=%d message=%s"),
                *Entry.GetItemGuid().ToString(), Entry.DropChancePercentage, Roll, Result.bSuccess, *Result.Message.ToString());
        }
        else UE_LOG(LogTemp, Display, TEXT("EFEquipment death bag=%s retained policy=%d droppable=%d chance=%.3f roll=%.3f"),
            *Entry.GetItemGuid().ToString(), DropEnabled, Droppable, Entry.DropChancePercentage, Roll);
        FInventoryItem Remaining;
        if (Equipment->GetItemByGuid(Entry.GetItemGuid(), Remaining))
        {
            RetainedChances.Add(Entry.GetItemGuid(), Entry.DropChancePercentage);
            Remaining.DropChancePercentage = 0; EFEquipmentACF::Inventory(Equipment)->ChangeEntry(Remaining);
        }
    }
    {
        TGuardValue<bool> Operation(bOperation, true);
        NativeDeath(); // Animation, movement, currency, ordinary loot and OnDeath remain native.
        for (const auto& Pair : RetainedChances)
        {
            FInventoryItem Remaining;
            if (Equipment->GetItemByGuid(Pair.Key, Remaining))
            { Remaining.DropChancePercentage = Pair.Value; EFEquipmentACF::Inventory(Equipment)->ChangeEntry(Remaining); }
        }
        // ACF destroys visual actors/effects but retains equipment entries for
        // kept items. Retire managed entries so derived reservations stay truthful.
        const auto CleanupEquipment = Equipment->GetCurrentEquipment().GetEquippedItems();
        for (const auto& Equipped : CleanupEquipment)
            if (Equipped.Item && (Equipped.Item->IsA<UEFEquipmentArmor>() || Equipped.Item->IsA<UEFEquipmentAccessory>())) Equipment->UnequipItemByGuid(Equipped.ItemGuid);
    }
    RefreshState();
}
