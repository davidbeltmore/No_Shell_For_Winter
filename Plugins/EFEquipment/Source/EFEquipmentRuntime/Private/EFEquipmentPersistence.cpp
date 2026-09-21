#include "EFEquipmentPersistence.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "EFEquipmentSaveArchive.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace EFEquipmentPersistencePrivate
{
    constexpr int32 MaximumInventoryBytes = 64 * 1024 * 1024;
    struct FScratch
    {
        AActor* Owner = nullptr;
        UACFEquipmentComponent* Inventory = nullptr;
        explicit FScratch(UWorld* World, UClass* Class = UACFEquipmentComponent::StaticClass())
        {
            if (!World || !IsInGameThread() || !Class || !Class->IsChildOf(UACFEquipmentComponent::StaticClass())) return;
            FActorSpawnParameters Parameters; Parameters.ObjectFlags |= RF_Transient;
            Owner = World->SpawnActor<AActor>(Parameters);
            if (!Owner) return;
            Inventory = NewObject<UACFEquipmentComponent>(Owner, Class);
            Owner->AddInstanceComponent(Inventory);
            EFEquipmentACF::Inventory(Inventory)->Init(Owner); Inventory->SetIsInitialized(true);
        }
        ~FScratch() { if (Owner) Owner->Destroy(); }
    };

    bool ResolveIntent(UACFEquipmentComponent* Inventory, UEFEquipmentSlotRegistry* Registry, FEFEquipmentSavedPlayer& State, FString& Error)
    {
        if (!Registry || !Registry->Validate(Error)) return false;
        TArray<FInventoryItem> Entries = Inventory->GetInventory();
        for (auto& Entry : Entries)
            for (const auto& Rule : Registry->MigrationRules)
                if (Rule.ItemClass.ToSoftObjectPath() == FSoftObjectPath(Entry.ItemClass.Get()) && Entry.EquipmentSlot == Rule.OldSlot)
                { Entry.EquipmentSlot = Rule.NewSlot; break; }
        auto Slots = [](const FInventoryItem& Entry)
        {
            TArray<FGameplayTag> Result{Entry.EquipmentSlot};
            if (const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Rules && Rules->Profile)
                for (auto Tag : Rules->Profile->AdditionalReservations) Result.AddUnique(Tag);
            return Result;
        };
        Entries.Sort([&](const auto& A, const auto& B)
        {
            const bool AReserves = Slots(A).Num() > 1, BReserves = Slots(B).Num() > 1;
            if (AReserves != BReserves) return AReserves;
            const auto* ASlot = Registry->Find(A.EquipmentSlot); const auto* BSlot = Registry->Find(B.EquipmentSlot);
            const int32 AOrder = ASlot ? ASlot->Order : MAX_int32, BOrder = BSlot ? BSlot->Order : MAX_int32;
            return AOrder != BOrder ? AOrder < BOrder : A.GetItemGuid().ToString() < B.GetItemGuid().ToString();
        });
        TArray<FGameplayTag> Occupied; State.EquippedSlots.Empty(); State.DisplacedItems.Empty();
        for (const auto& Entry : Entries)
        {
            if (!Entry.bIsEquipped) continue;
            const auto Required = Slots(Entry);
            if (!Entry.EquipmentSlot.IsValid() || !Entry.Item->GetPossibleItemSlots().Contains(Entry.EquipmentSlot)
                || FEFEquipmentPolicy::SlotsConflict(Occupied, Required))
            { State.DisplacedItems.Add(Entry.GetItemGuid()); continue; }
            State.EquippedSlots.Add(Entry.GetItemGuid(), Entry.EquipmentSlot); Occupied.Append(Required);
        }
        // Native OnComponentLoaded must not apply effects before coordinator conflict
        // resolution. ACF stores objects; the versioned metadata stores equip intent.
        for (const auto& Entry : Entries) EFEquipmentACF::Inventory(Inventory)->MarkItemAsEquipped(Entry.GetItemGuid(), false, FGameplayTag());
        return true;
    }

    bool SameIdentity(UACFInventoryComponent* A, UACFInventoryComponent* B, FString& Error)
    {
        const auto Left = A->GetInventory(), Right = B->GetInventory();
        if (Left.Num() != Right.Num()) { Error = TEXT("Inventory entry count changed during serialization."); return false; }
        for (const auto& Entry : Left)
        {
            FInventoryItem Restored;
            if (!B->GetItemByGuid(Entry.GetItemGuid(), Restored) || Restored.Count != Entry.Count || Restored.ItemClass != Entry.ItemClass
                || Restored.DropChancePercentage != Entry.DropChancePercentage || Restored.Item->Fragments.Num() != Entry.Item->Fragments.Num())
            { Error = TEXT("Inventory identity, quantity or fragment composition changed during serialization."); return false; }
            for (int32 Index = 0; Index < Entry.Item->Fragments.Num(); ++Index)
            {
                auto* Before = Entry.Item->Fragments[Index]; auto* After = Restored.Item->Fragments[Index];
                if (!Before || !After || Before->GetClass() != After->GetClass())
                { Error = TEXT("A saved item fragment type changed."); return false; }
                // Tagged SaveGame properties are compared without object instance names.
                for (TFieldIterator<FProperty> It(Before->GetClass()); It; ++It)
                    if (It->HasAnyPropertyFlags(CPF_SaveGame) && !It->Identical_InContainer(Before, After))
                    { Error = TEXT("An item fragment did not preserve its saved state."); return false; }
            }
            for (TFieldIterator<FProperty> It(Entry.Item->GetClass()); It; ++It)
                if (It->HasAnyPropertyFlags(CPF_SaveGame) && !It->Identical_InContainer(Entry.Item, Restored.Item))
                { Error = TEXT("An item did not preserve its saved state."); return false; }
        }
        return true;
    }
}

bool EFEquipmentPersistence::SerializeInventory(UACFEquipmentComponent* Inventory, TArray<uint8>& Bytes, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !ValidateContents(Inventory, Error)) return false;
    TArray<uint8> Candidate; FMemoryWriter Writer(Candidate);
    FEFEquipmentSaveArchive Ar(Writer, false);
    Inventory->Serialize(Ar);
    if (Ar.IsError() || Candidate.Num() > EFEquipmentPersistencePrivate::MaximumInventoryBytes)
    { Error = TEXT("The native inventory could not be serialized within its size limit."); return false; }
    Bytes = MoveTemp(Candidate); return true;
}

bool EFEquipmentPersistence::DeserializeInventory(UACFEquipmentComponent* Inventory, const TArray<uint8>& Bytes, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !Inventory || Bytes.IsEmpty() || Bytes.Num() > EFEquipmentPersistencePrivate::MaximumInventoryBytes)
    { Error = TEXT("The native inventory record is missing or too large."); return false; }
    EFEquipmentACF::Inventory(Inventory)->Init(Inventory->GetOwner());
    FMemoryReader Reader(Bytes); FEFEquipmentSaveArchive Ar(Reader, true);
    Inventory->Serialize(Ar); Inventory->RefreshTotalWeight();
    if (Ar.IsError() || Reader.Tell() != Bytes.Num()) { Error = TEXT("The native inventory record is truncated or incompatible."); return false; }
    return ValidateContents(Inventory, Error);
}

bool EFEquipmentPersistence::ValidateContents(UACFInventoryComponent* Inventory, FString& Error)
{
    if (!EFEquipmentACF::ValidateInventory(Inventory, Error)) return false;
    TSet<FGuid> Ids;
    for (const auto& Entry : Inventory->GetInventory()) Ids.Add(Entry.GetItemGuid());
    for (const auto& Entry : Inventory->GetInventory())
    {
        auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
        if (!Rules) continue;
        if (!Rules->Profile || !Rules->Profile->Validate(Error) || !Rules->FlushContainer(Error)) return false;
        if (!Rules->Profile->bContainer) continue;
        if (Entry.Count != 1) { Error = TEXT("A satchel must be a unique item."); return false; }
        TStrongObjectPtr<UEFContainerInventoryComponent> Contents(NewObject<UEFContainerInventoryComponent>(Inventory->GetOwner()));
        Contents->InitializeContainer(Entry.GetItemGuid(), Rules->Profile->ContentCapacity);
        const bool bRead = Contents->ReadPayload(Rules->ContainerPayload, Error);
        if (bRead) for (const auto& Child : Contents->GetInventory())
        {
            if (Ids.Contains(Child.GetItemGuid())) { Error = TEXT("An item GUID belongs to more than one inventory."); Contents->DestroyComponent(); return false; }
            Ids.Add(Child.GetItemGuid());
        }
        Contents->DestroyComponent(); if (!bRead) return false;
    }
    double Capacity = 0;
    if (!EFEquipmentACF::Capacity(Inventory, Capacity)) { Error = TEXT("Native carry capacity is invalid."); return false; }
    return true; // A negative projected limit and legitimate overburden are valid.
}

TArray<FALSComponentData>* EFEquipmentPersistence::ComponentRecords(FALSActorData& Actor, FString& Error)
{
    static auto* Property = FindFProperty<FArrayProperty>(FALSActorData::StaticStruct(), TEXT("ComponentRecords"));
    const auto* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    if (!Inner || Inner->Struct != FALSComponentData::StaticStruct())
    { Error = TEXT("The native ALS component-record contract changed."); return nullptr; }
    return Property->ContainerPtrToValuePtr<TArray<FALSComponentData>>(&Actor);
}

bool EFEquipmentPersistence::ValidateActorInventories(AActor* Actor, FALSActorData& Record, FString& Error)
{
    using namespace EFEquipmentPersistencePrivate;
    auto* Records = ComponentRecords(Record, Error);
    if (!Actor || !Records) return false;
    TArray<UACFInventoryComponent*> Inventories; Actor->GetComponents(Inventories);
    for (auto* Inventory : Inventories)
    {
        // An inventory omitted by a native actor's save contract cannot retain a
        // filled satchel. Reject that save instead of publishing silent item loss.
        auto* Part = Records->FindByPredicate([&](const auto& Entry) { return Entry.alsName == Inventory->GetFName() && Entry.Class == Inventory->GetClass(); });
        if (!Part)
        {
            if (Inventory->GetInventory().ContainsByPredicate([](const auto& Entry) {
                const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); return Rules && Rules->Profile && Rules->Profile->bContainer;
            })) { Error = TEXT("A world satchel's native inventory is missing from its actor's save contract."); return false; }
            continue;
        }
        if (Part->Data.IsEmpty() || Part->Data.Num() > MaximumInventoryBytes)
        { Error = TEXT("A world inventory record is missing or too large."); return false; }
        // Normalize only the candidate's native inventory record. ALS still loads
        // this tagged ACF format, without resolving obsolete cached world pointers.
        TArray<uint8> PortableBytes; FMemoryWriter Writer(PortableBytes); FEFEquipmentSaveArchive SaveAr(Writer, false);
        Inventory->Serialize(SaveAr);
        if (SaveAr.IsError() || PortableBytes.Num() > MaximumInventoryBytes)
        { Error = TEXT("A world inventory could not be serialized within its size limit."); return false; }
        Part->Data = MoveTemp(PortableBytes);
        FScratch Scratch(Actor->GetWorld());
        if (!Scratch.Owner) { Error = TEXT("Could not validate a world inventory record."); return false; }
        auto* Verify = NewObject<UACFInventoryComponent>(Scratch.Owner, Inventory->GetClass());
        EFEquipmentACF::Inventory(Verify)->Init(Scratch.Owner);
        FMemoryReader Reader(Part->Data); FEFEquipmentSaveArchive Ar(Reader, true);
        Verify->Serialize(Ar); Verify->RefreshTotalWeight();
        if (Ar.IsError() || Reader.Tell() != Part->Data.Num() || !ValidateContents(Verify, Error) || !SameIdentity(Inventory, Verify, Error))
        { if (Error.IsEmpty()) Error = TEXT("A world inventory failed its serialization round trip."); return false; }
    }
    return true;
}

UEFEquipmentSaveGame* EFEquipmentPersistence::ConvertSlot(UWorld* World, UALSSaveGame* Original, UEFEquipmentSlotRegistry* Registry, FString& Error)
{
    using namespace EFEquipmentPersistencePrivate;
    Error.Reset();
    if (!Original || !IsInGameThread()) { Error = TEXT("A native save slot is required on the game thread."); return nullptr; }
    if (auto* Existing = Cast<UEFEquipmentSaveGame>(Original))
    {
        if (Existing->EquipmentVersion != 1 || Existing->EquipmentPlayer.Version != 1)
        { Error = TEXT("This save uses an unsupported equipment version. The original was preserved."); return nullptr; }
        return ValidateSlot(World, Existing, Error) ? DuplicateObject<UEFEquipmentSaveGame>(Existing, GetTransientPackage()) : nullptr;
    }
    if (Original->GetClass() != UALSSaveGame::StaticClass())
    { Error = TEXT("This custom save class needs an explicit equipment migration adapter."); return nullptr; }
    auto* Candidate = NewObject<UEFEquipmentSaveGame>();
    for (TFieldIterator<FProperty> It(UALSSaveGame::StaticClass()); It; ++It)
        if (It->HasAnyPropertyFlags(CPF_SaveGame)) It->CopyCompleteValue_InContainer(Candidate, Original);
    FALSPlayerData Player; Candidate->GetLocalPlayer(Player);
    auto* Records = ComponentRecords(Player.Pawn, Error); if (!Records) return nullptr;
    FALSComponentData* Record = nullptr;
    for (auto& Component : *Records) if (Component.Class && Component.Class->IsChildOf(UACFEquipmentComponent::StaticClass()))
    { if (Record) { Error = TEXT("The player has multiple native equipment records."); return nullptr; } Record = &Component; }
    if (!Record)
    { Error = TEXT("The save has no local-player equipment record to migrate."); return nullptr; }
    FScratch Scratch(World, Record->Class), Verify(World, Record->Class);
    if (!DeserializeInventory(Scratch.Inventory, Record->Data, Error)) return nullptr;
    auto& State = Candidate->EquipmentPlayer; State.InventoryComponent = Record->alsName;
    if (!EFEquipmentACF::Capacity(Scratch.Inventory, State.BaseCarryCapacity) || State.BaseCarryCapacity < 0)
    { Error = TEXT("The legacy carry capacity is invalid."); return nullptr; }
    // Legacy product saves predate satchels. Experimental unversioned container
    // saves are ambiguous about capacity projection and require a separate adapter.
    for (const auto& Entry : Scratch.Inventory->GetInventory())
        if (auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
        { Error = TEXT("An unversioned save already contains satchels; its capacity requires an explicit migration adapter."); return nullptr; }
    if (!ResolveIntent(Scratch.Inventory, Registry, State, Error) || !SerializeInventory(Scratch.Inventory, Record->Data, Error)
        || !DeserializeInventory(Verify.Inventory, Record->Data, Error) || !SameIdentity(Scratch.Inventory, Verify.Inventory, Error)) return nullptr;
    Candidate->StoreLocalPlayer(Player); return ValidateSlot(World, Candidate, Error) ? Candidate : nullptr;
}

bool EFEquipmentPersistence::ValidateSlot(UWorld* World, UEFEquipmentSaveGame* Candidate, FString& Error)
{
    using namespace EFEquipmentPersistencePrivate;
    if (!Candidate || Candidate->EquipmentVersion != 1 || Candidate->EquipmentPlayer.Version != 1
        || !FMath::IsFinite(Candidate->EquipmentPlayer.BaseCarryCapacity) || Candidate->EquipmentPlayer.BaseCarryCapacity < 0)
    { Error = TEXT("The equipment save version or base capacity is invalid."); return false; }
    FALSPlayerData Player; Candidate->GetLocalPlayer(Player);
    auto* Records = ComponentRecords(Player.Pawn, Error); if (!Records) return false;
    const auto* Record = Records->FindByPredicate([&](const auto& Part) { return Part.alsName == Candidate->EquipmentPlayer.InventoryComponent; });
    if (!Record || !Record->Class || !Record->Class->IsChildOf(UACFEquipmentComponent::StaticClass()))
    { Error = TEXT("The saved native equipment record is missing."); return false; }
    FScratch Scratch(World, Record->Class);
    if (!DeserializeInventory(Scratch.Inventory, Record->Data, Error)) return false;
    TArray<FGameplayTag> Occupied;
    for (const auto& Entry : Scratch.Inventory->GetInventory())
        if (Entry.bIsEquipped) { Error = TEXT("Native effects must wait for equipment restoration."); return false; }
    for (const auto& Pair : Candidate->EquipmentPlayer.EquippedSlots)
    {
        FInventoryItem Entry;
        if (!Scratch.Inventory->GetItemByGuid(Pair.Key, Entry) || !Entry.Item->GetPossibleItemSlots().Contains(Pair.Value))
        { Error = TEXT("Saved equipment intent has an invalid item or slot."); return false; }
        TArray<FGameplayTag> Required{Pair.Value};
        if (auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Rules && Rules->Profile) Required.Append(Rules->Profile->AdditionalReservations);
        if (FEFEquipmentPolicy::SlotsConflict(Occupied, Required)) { Error = TEXT("Saved equipment intent contains unresolved conflicts."); return false; }
        Occupied.Append(Required);
    }
    return true;
}

bool EFEquipmentPersistence::CapturePlayer(UEFEquipmentSaveGame* Candidate, UEFEquipmentCoordinatorComponent* Coordinator, FALSPlayerData& Player, FString& Error)
{
    using namespace EFEquipmentPersistencePrivate;
    if (!Candidate || !Coordinator || !Coordinator->InitializeEquipment() || !Coordinator->FlushContainers(Error)) return false;
    auto* Native = Coordinator->GetNativeEquipment();
    auto* Records = ComponentRecords(Player.Pawn, Error); if (!Records) return false;
    auto* Record = Records->FindByPredicate([&](const auto& Part) { return Part.alsName == Native->GetFName() && Part.Class == Native->GetClass(); });
    if (!Record) { Error = TEXT("The player's native inventory is absent from its ALS component contract."); return false; }
    FScratch Scratch(Coordinator->GetWorld(), Native->GetClass()), Verify(Coordinator->GetWorld(), Native->GetClass());
    TArray<uint8> Bytes;
    if (!SerializeInventory(Native, Bytes, Error) || !DeserializeInventory(Scratch.Inventory, Bytes, Error) || !SameIdentity(Native, Scratch.Inventory, Error)) return false;
    auto& State = Candidate->EquipmentPlayer; State.Version = 1; State.BaseCarryCapacity = Coordinator->GetWeightSummary().BaseCapacity;
    State.EquipmentOrder.Reset();
    for (const auto& Entry : Native->GetCurrentEquipment().GetEquippedItems()) State.EquipmentOrder.Add(Entry.ItemGuid);
    State.InventoryComponent = Native->GetFName();
    if (!FMath::IsFinite(State.BaseCarryCapacity) || State.BaseCarryCapacity < 0)
    { Error = TEXT("Base carry capacity is invalid."); return false; }
    auto* Registry = GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    if (!ResolveIntent(Scratch.Inventory, Registry, State, Error) || !SerializeInventory(Scratch.Inventory, Record->Data, Error)
        || !DeserializeInventory(Verify.Inventory, Record->Data, Error) || !SameIdentity(Scratch.Inventory, Verify.Inventory, Error)) return false;
    Candidate->EquipmentVersion = 1; Candidate->StoreLocalPlayer(Player); return true;
}

bool EFEquipmentPersistence::RestorePlayer(UEFEquipmentCoordinatorComponent* Coordinator, const FEFEquipmentSavedPlayer& Saved, FString& Error)
{
    if (!Coordinator || Saved.Version != 1 || !Coordinator->InitializeEquipment() || !FMath::IsFinite(Saved.BaseCarryCapacity)
        || Saved.BaseCarryCapacity < 0 || Coordinator->GetNativeEquipment()->GetFName() != Saved.InventoryComponent)
    { Error = TEXT("Saved equipment metadata is incompatible with this player."); return false; }
    auto* Native = Coordinator->GetNativeEquipment();
    if (!ValidateContents(Native, Error)) return false;
    Coordinator->ResetDerivedContainers();
    auto Intent = Native->GetInventory();
    for (auto& Entry : Intent)
    {
        const auto* Slot = Saved.EquippedSlots.Find(Entry.GetItemGuid());
        Entry.bIsEquipped = Slot != nullptr; Entry.EquipmentSlot = Slot ? *Slot : FGameplayTag();
    }
    for (const auto& Pair : Saved.EquippedSlots)
        if (!Intent.ContainsByPredicate([&](const auto& Entry) { return Entry.GetItemGuid() == Pair.Key; }))
        { Error = TEXT("Saved equipped item identity is missing from the native inventory."); return false; }
    if (!Saved.EquipmentOrder.IsEmpty()) Intent.StableSort([&](const auto& A, const auto& B)
    {
        const int32 AIndex = Saved.EquipmentOrder.IndexOfByKey(A.GetItemGuid()), BIndex = Saved.EquipmentOrder.IndexOfByKey(B.GetItemGuid());
        return (AIndex == INDEX_NONE ? MAX_int32 : AIndex) < (BIndex == INDEX_NONE ? MAX_int32 : BIndex);
    });
    Coordinator->SetBaseCarryCapacity(Saved.BaseCarryCapacity);
    const auto Result = Coordinator->RestoreEquippedIntent(Intent);
    if (!Result.bSuccess || !Result.DisplacedItems.IsEmpty()) { Error = TEXT("Saved equipment could not restore its selected items. ") + Result.Message.ToString(); return false; }
    Coordinator->RefreshState(); return true;
}

FText EFEquipmentPersistence::DescribeMigration(UACFEquipmentComponent* Inventory, const FEFEquipmentSavedPlayer& Saved, bool bConverted)
{
    if (Saved.DisplacedItems.IsEmpty()) return bConverted
        ? FText::FromString(TEXT("Equipment save upgraded. All items were preserved.")) : FText::GetEmpty();
    TArray<FString> Names;
    for (const FGuid Guid : Saved.DisplacedItems)
    {
        FInventoryItem Entry;
        Names.Add(Inventory && Inventory->GetItemByGuid(Guid, Entry) && Entry.Item
            ? Entry.Item->GetItemName().ToString() : TEXT("Item ") + Guid.ToString(EGuidFormats::Digits).Left(8));
    }
    return FText::FromString(TEXT("Equipment save upgraded. Unequipped incompatible items: ") + FString::Join(Names, TEXT(", "))
        + TEXT(". They remain in your inventory."));
}
