#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "GameFramework/Pawn.h"
#include "Items/ACFEquippableItem.h"
#include "TimerManager.h"
#include "ACFItemSystemFunctionLibrary.h"
#include "Items/ACFWorldItem.h"
#include "GameFramework/Character.h"
#include "ALSLoadAndSaveComponent.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "Engine/GameInstance.h"

UEFEquipmentCoordinatorComponent::UEFEquipmentCoordinatorComponent()
{ PrimaryComponentTick.bCanEverTick = false; }

bool UEFEquipmentCoordinatorComponent::IsManagedPlayer(const AActor* Actor)
{
    const APawn* Pawn = Cast<APawn>(Actor);
    if (!Pawn || !GetDefault<UEFEquipmentSettings>()->bEnabled) return false;
    if (Pawn->IsPlayerControlled()) return true;
    for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
        if (Class->GetPathName() == TEXT("/Game/FullSample/Player.Player_C")) return true;
    return false;
}

UEFEquipmentCoordinatorComponent* UEFEquipmentCoordinatorComponent::FindOrCreate(AActor* Actor)
{
    if (!IsValid(Actor)) return nullptr;
    if (auto* Existing = Actor->FindComponentByClass<UEFEquipmentCoordinatorComponent>()) return Existing;
    if (!IsManagedPlayer(Actor) || !Actor->HasAuthority()) return nullptr;
    auto* Created = NewObject<UEFEquipmentCoordinatorComponent>(Actor, TEXT("EFEquipmentCoordinator"));
    Actor->AddInstanceComponent(Created); Created->RegisterComponent(); Created->InitializeEquipment();
    return Created;
}

void UEFEquipmentCoordinatorComponent::BeginPlay()
{
    Super::BeginPlay(); InitializeEquipment();
    GetWorld()->GetTimerManager().SetTimer(DiscoveryTimer, this, &UEFEquipmentCoordinatorComponent::PollEquipment, 0.5f, true);
}

void UEFEquipmentCoordinatorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    FString Error; FlushContainers(Error);
    if (auto* Save = GetOwner()->FindComponentByClass<UALSLoadAndSaveComponent>()) Save->OnActorLoaded.RemoveDynamic(this, &UEFEquipmentCoordinatorComponent::HandleActorLoaded);
    if (Equipment)
    {
        Equipment->OnInventoryChanged.RemoveDynamic(this, &UEFEquipmentCoordinatorComponent::HandleInventoryChanged);
        Equipment->OnEquipmentChanged.RemoveDynamic(this, &UEFEquipmentCoordinatorComponent::HandleEquipmentChanged);
        if (BaseCarryCapacity >= 0) EFEquipmentACF::SetCapacity(Equipment, BaseCarryCapacity);
    }
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DiscoveryTimer);
    Super::EndPlay(Reason);
}

bool UEFEquipmentCoordinatorComponent::InitializeEquipment(UEFEquipmentSlotRegistry* RegistryOverride)
{
    if (bInitialized) return true;
    Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UACFEquipmentComponent>() : nullptr;
    Registry = RegistryOverride ? RegistryOverride : GetDefault<UEFEquipmentSettings>()->SlotRegistry.LoadSynchronous();
    FString Error;
    if (!Equipment || !Registry || !Registry->Validate(Error) || !EFEquipmentACF::Inventory(Equipment)) return false;
    if (BaseCarryCapacity < 0 && !EFEquipmentACF::Capacity(Equipment, BaseCarryCapacity)) return false;
    TArray<FGameplayTag> Available = Equipment->GetAvailableEquipmentSlot();
    // Remove the legacy Armor parent. All remaining slots are preserved unless
    // they are ancestors/descendants of an authored slot, which is invalid in ACF.
    Available.Remove(FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Armor"), false));
    for (const auto& Slot : Registry->Slots) Available.AddUnique(Slot.Slot);
    for (int32 I = 0; I < Available.Num(); ++I)
        for (int32 J = I + 1; J < Available.Num(); ++J)
            if (Available[I].MatchesTag(Available[J]) || Available[J].MatchesTag(Available[I]))
            { LastMessage = FText::FromString(TEXT("Available equipment slots contain an ancestor conflict.")); return false; }
    Equipment->SetAvailableEquipmentSlot(Available);
    Equipment->OnInventoryChanged.AddUniqueDynamic(this, &UEFEquipmentCoordinatorComponent::HandleInventoryChanged);
    Equipment->OnEquipmentChanged.AddUniqueDynamic(this, &UEFEquipmentCoordinatorComponent::HandleEquipmentChanged);
    if (auto* Save = GetOwner()->FindComponentByClass<UALSLoadAndSaveComponent>()) Save->OnActorLoaded.AddUniqueDynamic(this, &UEFEquipmentCoordinatorComponent::HandleActorLoaded);
    bInitialized = true; RefreshState(); return true;
}

void UEFEquipmentCoordinatorComponent::PollEquipment()
{
    if (!InitializeEquipment() || bOperation) return;
    if (!bInitialItemsHandled && Equipment->GetIsInitialized())
    {
        auto* Save = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>() : nullptr;
        if (Save && Save->GetSystemState() == ELoadingState::ELoading) return;
        bInitialItemsHandled = true;
        if (Save && Save->GetCurrentSaveGame()) HandleActorLoaded();
        else
        {
            // ACF's native initial equip was vetoed. Replay only explicit StartingItems
            // auto-equip intent; later ordinary items are routed by their entry points.
            const auto Starting = Equipment->GetStartingItems();
            for (const auto& Initial : Starting)
            {
                if (!Initial.bAutoEquip) continue;
                for (const auto& Item : Equipment->GetInventory())
                    if (Item.ItemClass == Initial.ItemClass && Item.Item && (Item.Item->IsA<UEFEquipmentArmor>() || Item.Item->IsA<UEFEquipmentAccessory>()))
                    { RequestEquip(Item.GetItemGuid(), FGameplayTag()); break; }
            }
        }
    }
    RefreshState();
}

void UEFEquipmentCoordinatorComponent::HandleActorLoaded()
{
    if (bOperation || !InitializeEquipment()) return;
    bInitialItemsHandled = true;
    const auto Intent = Equipment->GetInventory();
    double LoadedRootLimit = 0;
    if (!EFEquipmentACF::Capacity(Equipment, LoadedRootLimit)) { LastMessage = FText::FromString(TEXT("The saved carry capacity is invalid.")); return; }
    // Drop stale transient container components. New item fragments own the saved bytes.
    for (const auto& Pair : Containers) if (IsValid(Pair.Value)) Pair.Value->DestroyComponent();
    Containers.Empty();
    double SavedContentWeight = 0;
    for (const auto& Item : Intent)
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
        if (!Rules || !Rules->Profile || !Rules->Profile->bContainer) continue;
        FString Error; auto* Container = ResolveContainer(Item.GetItemGuid(), Error);
        if (!Container) { LastMessage = FText::FromString(Error); return; }
        SavedContentWeight += FEFEquipmentPolicy::EffectiveContentWeight(Container->GetCurrentInventoryTotalWeight(), Rules->Profile->WeightReduction, Item.bIsEquipped);
    }
    BaseCarryCapacity = LoadedRootLimit + SavedContentWeight;
    RestoreEquippedIntent(Intent);
}

bool UEFEquipmentCoordinatorComponent::NativeEquipGate(UACFItem* Item, UACFEquipmentComponent* Native)
{
    if (!Native || !Item) return false;
    if (!IsManagedPlayer(Native->GetOwner()) && !Native->GetOwner()->FindComponentByClass<UEFEquipmentCoordinatorComponent>()) return true;
    auto* Coordinator = FindOrCreate(Native->GetOwner());
    return Coordinator && Coordinator->IsAuthorized(Item);
}

bool UEFEquipmentCoordinatorComponent::IsAuthorized(const UACFItem* Item) const
{ return bInitialized && AuthorizedItem == Item && (bOperation || bCheckingRequirements); }

TArray<FGameplayTag> UEFEquipmentCoordinatorComponent::Occupancy(const UACFItem* Item, FGameplayTag Primary) const
{
    TArray<FGameplayTag> Result; Result.Add(Primary);
    const auto* Fragment = UEFEquipmentRulesFragment::Find(Item);
    if (Fragment && Fragment->Profile)
        for (FGameplayTag Tag : Fragment->Profile->AdditionalReservations) Result.AddUnique(Tag);
    return Result;
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::PreviewEquip(FGuid Guid, FGameplayTag Destination)
{
    if (bOperation || bCheckingRequirements) return FEFEquipmentResult::Failure(TEXT("Another equipment operation is in progress."));
    if (!InitializeEquipment()) return FEFEquipmentResult::Failure(TEXT("Equipment is not ready."));
    FInventoryItem Item;
    if (!Equipment->GetItemByGuid(Guid, Item) || !Item.Item) return FEFEquipmentResult::Failure(TEXT("The item is no longer in your inventory."));
    auto* Equippable = Cast<UACFEquippableItem>(Item.Item);
    if (!Equippable || !Equipment->CanBeEquipped(Item.ItemClass)) return FEFEquipmentResult::Failure(TEXT("This item cannot be equipped."));
    const auto Possible = Item.Item->GetPossibleItemSlots();
    if (!Destination.IsValid())
    {
        for (FGameplayTag Candidate : Possible)
        {
            if (!Registry->Find(Candidate)) continue;
            if (!Destination.IsValid()) Destination = Candidate;
            bool bFree = true;
            for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems())
                if (Current.ItemGuid != Guid && FEFEquipmentPolicy::SlotsConflict(Occupancy(Item.Item, Candidate), Occupancy(Current.Item, Current.ItemSlot))) { bFree = false; break; }
            if (bFree) { Destination = Candidate; break; }
        }
    }
    if (!Possible.Contains(Destination) || !Registry->Find(Destination)) return FEFEquipmentResult::Failure(TEXT("This item does not fit that equipment slot."));
    const auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
    FString Error;
    if (Rules && (!Rules->Profile || !Rules->Profile->Validate(Error))) return FEFEquipmentResult::Failure(Error.IsEmpty() ? TEXT("Equipment rule profile is missing.") : Error);
    for (FGameplayTag Tag : Occupancy(Item.Item, Destination))
        if (!Registry->Find(Tag)) return FEFEquipmentResult::Failure(TEXT("The item reserves an unavailable slot."));
    TGuardValue<bool> Checking(bCheckingRequirements, true);
    TGuardValue<TObjectPtr<UACFItem>> Authorized(AuthorizedItem, Item.Item);
    if (!Equippable->CanBeEquipped(Equipment)) return FEFEquipmentResult::Failure(TEXT("The native equipment requirements are not met."));
    FEFEquipmentResult Result = FEFEquipmentResult::Success(); Result.SelectedSlot = Destination; Result.Revision = Revision;
    for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems())
        if (Current.ItemGuid != Guid && FEFEquipmentPolicy::SlotsConflict(Occupancy(Item.Item, Destination), Occupancy(Current.Item, Current.ItemSlot))) Result.DisplacedItems.AddUnique(Current.ItemGuid);
    return Result;
}

bool UEFEquipmentCoordinatorComponent::VerifyEquipped(FGuid Guid, FGameplayTag Slot) const
{
    FInventoryItem Entry;
    if (!Equipment->GetItemByGuid(Guid, Entry) || !Entry.bIsEquipped || Entry.EquipmentSlot != Slot) return false;
    for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems())
        if (Current.ItemGuid == Guid && Current.ItemSlot == Slot && Current.Item == Entry.Item) return true;
    return false;
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::RequestEquip(FGuid Guid, FGameplayTag Destination, int64 ExpectedRevision)
{
    if (bOperation || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Another equipment operation is in progress.")));
    if (ExpectedRevision >= 0 && ExpectedRevision != Revision) return Publish(FEFEquipmentResult::Failure(TEXT("Your inventory changed. Please try again.")));
    FEFEquipmentResult Result = PreviewEquip(Guid, Destination);
    if (!Result.bSuccess) return Publish(Result);
    FInventoryItem Item; Equipment->GetItemByGuid(Guid, Item);
    const auto Before = Equipment->GetCurrentEquipment().GetEquippedItems();
    {
        TGuardValue<bool> Operation(bOperation, true);
        for (FGuid Conflict : Result.DisplacedItems) Equipment->UnequipItemByGuid(Conflict);
        TGuardValue<TObjectPtr<UACFItem>> Authorized(AuthorizedItem, Item.Item);
        bool bSkipNative = false, bForceFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
        bSkipNative = TestFailurePoint == 1; bForceFailure = TestFailurePoint != 0; TestFailurePoint = 0;
#endif
        if (!bSkipNative) Equipment->EquipItemFromInventoryInSlot(Item, Result.SelectedSlot);
        if (bForceFailure || !VerifyEquipped(Guid, Result.SelectedSlot))
        {
            // Native ACF may apply modifiers/fragments before actor spawning fails.
            Equipment->UnequipItemByGuid(Guid);
            // Project parents balance effects even when native spawning failed before
            // publishing an equipment entry. Repeated cleanup is a no-op.
            if (Item.Item->IsA<UEFEquipmentArmor>() || Item.Item->IsA<UEFEquipmentAccessory>())
                CastChecked<UACFEquippableItem>(Item.Item)->Internal_OnUnEquipped();
            bool bRestored = true;
            for (const auto& Previous : Before)
            {
                if (VerifyEquipped(Previous.ItemGuid, Previous.ItemSlot)) continue;
                FInventoryItem Restore;
                if (!Equipment->GetItemByGuid(Previous.ItemGuid, Restore)) { bRestored = false; continue; }
                AuthorizedItem = Restore.Item;
                Equipment->EquipItemFromInventoryInSlot(Restore, Previous.ItemSlot);
                bRestored &= VerifyEquipped(Previous.ItemGuid, Previous.ItemSlot);
            }
            Result = FEFEquipmentResult::Failure(bRestored ? TEXT("Equip failed. Your previous equipment was restored.") : TEXT("Equip failed. Some equipment could not be restored; your items remain in inventory."));
        }
    }
    RefreshState(); return Publish(Result);
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::RequestUnequip(FGuid Guid)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    { TGuardValue<bool> Operation(bOperation, true); Equipment->UnequipItemByGuid(Guid); }
    for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems())
        if (Current.ItemGuid == Guid) return Publish(FEFEquipmentResult::Failure(TEXT("The item could not be unequipped.")));
    RefreshState(); return Publish(FEFEquipmentResult::Success());
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::Publish(FEFEquipmentResult Result)
{ LastMessage = Result.Message; Result.Revision = Revision; OnStateChanged.Broadcast(); return Result; }

FEFEquipmentResult UEFEquipmentCoordinatorComponent::RequestDrop(FGuid Guid, int32 Count)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    FInventoryItem Entry;
    if (!Equipment->GetItemByGuid(Guid, Entry) || !Entry.Item || Count <= 0 || Count > Entry.Count)
        return Publish(FEFEquipmentResult::Failure(TEXT("Choose an available item and a valid quantity.")));
    if (!Entry.Item->GetItemInfo().bDroppable) return Publish(FEFEquipmentResult::Failure(TEXT("This item cannot be dropped.")));
    FString Error; auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
    if (Rules && Rules->Profile && Rules->Profile->bContainer)
    {
        auto* Container = ResolveContainer(Guid, Error);
        if (!Container) return Publish(FEFEquipmentResult::Failure(Error));
        for (const auto& Child : Container->GetInventory())
            if (!Child.Item->GetItemInfo().bDroppable)
                return Publish(FEFEquipmentResult::Failure(FString::Printf(TEXT("Cannot drop the satchel: %s cannot be dropped."), *Child.Item->GetItemName().ToString())));
        if (!Rules->FlushContainer(Error)) return Publish(FEFEquipmentResult::Failure(Error));
    }
    if (Count < Entry.Count && !Entry.Item->Fragments.IsEmpty()) return Publish(FEFEquipmentResult::Failure(TEXT("Drop this stateful stack as a whole.")));
    const FGameplayTag PreviousSlot = Entry.EquipmentSlot;
    if (Entry.bIsEquipped && !RequestUnequip(Guid).bSuccess) return Publish(FEFEquipmentResult::Failure(TEXT("The item could not be unequipped.")));
    bool bDropped = false;
    {
        TGuardValue<bool> Operation(bOperation, true);
        Equipment->GetItemByGuid(Guid, Entry);
        FInventoryItem Drop = Entry; Drop.Count = Count;
        if (Count < Entry.Count) { Drop.ResetGuid(); Drop.Item = DuplicateObject<UACFItem>(Entry.Item, GetOwner()); }
        FVector Location = GetOwner()->GetActorLocation();
        if (const auto* Character = Cast<ACharacter>(GetOwner())) Location.Z -= Character->GetSimpleCollisionHalfHeight();
        if (AACFWorldItem* WorldItem = UACFItemSystemFunctionLibrary::SpawnWorldItemWithInstance(this, Drop, Location))
        {
            bDropped = WorldItem->GetItems().ContainsByPredicate([&](const auto& Item) { return Item.GetItemGuid() == Drop.GetItemGuid() && Item.Count == Count; });
            if (bDropped)
            {
                Equipment->RemoveItem(Entry, Count);
                if (Rules && IsValid(Rules->RuntimeInventory)) { Rules->RuntimeInventory->DestroyComponent(); Rules->RuntimeInventory = nullptr; }
            }
            else WorldItem->Destroy();
        }
    }
    if (!bDropped && PreviousSlot.IsValid()) RequestEquip(Guid, PreviousSlot);
    RefreshState();
    return Publish(bDropped ? FEFEquipmentResult::Success() : FEFEquipmentResult::Failure(TEXT("Drop failed. The item remains in your inventory.")));
}

TArray<FEFEquipmentSlotView> UEFEquipmentCoordinatorComponent::GetSlotViews() const
{
    TArray<FEFEquipmentSlotView> Result;
    if (!Registry || !Equipment) return Result;
    for (const auto& Definition : Registry->Slots)
    {
        FEFEquipmentSlotView View; View.Definition = Definition;
        for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems())
            if (Occupancy(Current.Item, Current.ItemSlot).Contains(Definition.Slot))
            { View.OwnerGuid = Current.ItemGuid; View.bReserved = Current.ItemSlot != Definition.Slot; View.ItemName = Current.Item ? Current.Item->GetItemName() : FText(); break; }
        Result.Add(View);
    }
    Result.Sort([](const auto& A, const auto& B) { return A.Definition.Order < B.Definition.Order; }); return Result;
}

UEFContainerInventoryComponent* UEFEquipmentCoordinatorComponent::ResolveContainer(FGuid Guid, FString& Error)
{
    FInventoryItem Item;
    if (!Equipment || !Equipment->GetItemByGuid(Guid, Item) || !Item.Item) { Error = TEXT("The satchel is no longer in your inventory."); return nullptr; }
    auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
    if (!Rules || !Rules->Profile || !Rules->Profile->bContainer || !Rules->Profile->Validate(Error))
    { if (Error.IsEmpty()) Error = TEXT("This item is not a valid container."); return nullptr; }
    if (auto* Existing = Containers.Find(Guid)) return Existing->Get();
    if (!IsValid(Rules->RuntimeInventory)) Rules->RuntimeInventory = nullptr;
    if (Rules->RuntimeInventory && Rules->RuntimeInventory->GetOwner() != GetOwner())
    {
        if (!Rules->FlushContainer(Error)) return nullptr;
        Rules->RuntimeInventory->DestroyComponent(); Rules->RuntimeInventory = nullptr;
    }
    auto* Container = Rules->RuntimeInventory.Get();
    if (!Container)
    {
        Container = NewObject<UEFContainerInventoryComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UEFContainerInventoryComponent::StaticClass(), TEXT("SatchelInventory")));
        Container->InitializeContainer(Guid, Rules->Profile->ContentCapacity);
        Container->RegisterComponent();
        if (!Container->ReadPayload(Rules->ContainerPayload, Error)) { Container->DestroyComponent(); return nullptr; }
        Rules->RuntimeInventory = Container;
    }
    Containers.Add(Guid, Container); return Container;
}

double UEFEquipmentCoordinatorComponent::GetEffectiveContainerWeight() const
{
    double Weight = 0;
    if (Equipment) for (const auto& Pair : Containers)
    {
        FInventoryItem Bag;
        if (!IsValid(Pair.Value) || !Equipment->GetItemByGuid(Pair.Key, Bag)) continue;
        const auto* Rules = UEFEquipmentRulesFragment::Find(Bag.Item);
        if (Rules && Rules->Profile) Weight += FEFEquipmentPolicy::EffectiveContentWeight(Pair.Value->GetCurrentInventoryTotalWeight(), Rules->Profile->WeightReduction,
            VerifyEquipped(Bag.GetItemGuid(), FGameplayTag::RequestGameplayTag(TEXT("ItemSlot.Satchel"))));
    }
    return Weight;
}

FEFEquipmentWeightSummary UEFEquipmentCoordinatorComponent::GetWeightSummary() const
{
    FEFEquipmentWeightSummary Result; Result.BaseCapacity = BaseCarryCapacity;
    if (!Equipment) return Result;
    Result.OriginalWeight = Equipment->GetCurrentInventoryTotalWeight();
    Result.EffectiveWeight = Result.OriginalWeight + GetEffectiveContainerWeight();
    for (const auto& Pair : Containers)
    { FInventoryItem Bag; if (IsValid(Pair.Value) && Equipment->GetItemByGuid(Pair.Key, Bag)) Result.OriginalWeight += Pair.Value->GetCurrentInventoryTotalWeight(); }
    Result.bOverburdened = Result.EffectiveWeight > Result.BaseCapacity + 0.000001; return Result;
}

bool UEFEquipmentCoordinatorComponent::SetBaseCarryCapacity(double Capacity)
{
    if (!FMath::IsFinite(Capacity) || Capacity < 0 || !GetOwner()->HasAuthority() || bOperation) return false;
    BaseCarryCapacity = Capacity; RefreshState(); return true;
}

bool UEFEquipmentCoordinatorComponent::FlushContainers(FString& Error)
{
    if (!Equipment) return true;
    for (const auto& Item : Equipment->GetInventory())
        if (auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item)) if (!Rules->FlushContainer(Error)) return false;
    return true;
}

void UEFEquipmentCoordinatorComponent::RefreshState()
{
    if (bOperation || bRefreshing || !bInitialized) return;
    TGuardValue<bool> Refreshing(bRefreshing, true);
    TSet<FGuid> Live;
    for (const auto& Item : Equipment->GetInventory())
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
        if (Rules && Rules->Profile && Rules->Profile->bContainer)
        { Live.Add(Item.GetItemGuid()); FString Error; if (!ResolveContainer(Item.GetItemGuid(), Error)) LastMessage = FText::FromString(Error); }
    }
    for (auto It = Containers.CreateIterator(); It; ++It)
        if (!Live.Contains(It.Key())) It.RemoveCurrent(); // Item fragment retains the component until its next owner resolves it.
    Equipment->RefreshTotalWeight();
    EFEquipmentACF::SetCapacity(Equipment, BaseCarryCapacity - GetEffectiveContainerWeight());
    uint32 Fingerprint = GetTypeHash(BaseCarryCapacity);
    auto HashInventory = [&Fingerprint](UACFInventoryComponent* Inventory)
    {
        for (const auto& Entry : Inventory->GetInventory())
        {
            Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.GetItemGuid()));
            Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.Count));
            Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.bIsEquipped));
            Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.EquipmentSlot));
            Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.Item));
        }
    };
    HashInventory(Equipment);
    for (const auto& Pair : Containers) HashInventory(Pair.Value);
    for (const auto& Entry : Equipment->GetCurrentEquipment().GetEquippedItems())
    { Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.ItemGuid)); Fingerprint = HashCombine(Fingerprint, GetTypeHash(Entry.ItemSlot)); }
    if (Fingerprint != StateFingerprint)
    { StateFingerprint = Fingerprint; ++Revision; OnStateChanged.Broadcast(); }
}

void UEFEquipmentCoordinatorComponent::HandleInventoryChanged() { RefreshState(); }
void UEFEquipmentCoordinatorComponent::HandleEquipmentChanged(const FEquipment& Current) { RefreshState(); }

TArray<FInventoryItem> UEFEquipmentCoordinatorComponent::GetContainerItems(FGuid Guid)
{ FString Error; auto* Container = ResolveContainer(Guid, Error); return Container ? Container->GetInventory() : TArray<FInventoryItem>(); }

FEFEquipmentResult UEFEquipmentCoordinatorComponent::TakeFromInventory(UACFInventoryComponent* Source, FGuid Guid, int32 Count, bool bAutoEquip)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    if (!Source || Source == Equipment || Source->IsA<UEFContainerInventoryComponent>()) return Publish(FEFEquipmentResult::Failure(TEXT("Extract satchel items before using them.")));
    FInventoryItem Item;
    if (!Source->GetItemByGuid(Guid, Item) || !Item.Item || Count <= 0 || Count > Item.Count) return Publish(FEFEquipmentResult::Failure(TEXT("The source item or quantity changed.")));
    double AdditionalWeight = static_cast<double>(Item.Item->GetItemInfo().ItemWeight) * Count;
    FString Error; auto* Rules = UEFEquipmentRulesFragment::Find(Item.Item);
    if (Rules && Rules->Profile && Rules->Profile->bContainer)
    {
        if (Count != 1 || Item.Count != 1) return Publish(FEFEquipmentResult::Failure(TEXT("Satchels must be unique inventory entries.")));
        if (!Rules->FlushContainer(Error)) return Publish(FEFEquipmentResult::Failure(Error));
        auto* Inspection = NewObject<UEFContainerInventoryComponent>(GetOwner());
        Inspection->InitializeContainer(Guid, Rules->Profile->ContentCapacity);
        if (!Inspection->ReadPayload(Rules->ContainerPayload, Error)) return Publish(FEFEquipmentResult::Failure(Error));
        AdditionalWeight += Inspection->GetCurrentInventoryTotalWeight();
    }
    if (AdditionalWeight > 0 && GetWeightSummary().EffectiveWeight + AdditionalWeight > BaseCarryCapacity + 0.000001)
        return Publish(FEFEquipmentResult::Failure(TEXT("This item and its contents would exceed your carry capacity.")));
    bool bTransferred;
    { TGuardValue<bool> Operation(bOperation, true); bTransferred = EFEquipmentACF::Transfer(Source, Equipment, Guid, Count, true, Error); }
    RefreshState();
    if (!bTransferred) return Publish(FEFEquipmentResult::Failure(Error));
    if (bAutoEquip && Count == Item.Count)
    {
        auto Preview = PreviewEquip(Guid, FGameplayTag());
        if (Preview.bSuccess && Preview.DisplacedItems.IsEmpty()) RequestEquip(Guid, Preview.SelectedSlot);
    }
    return Publish(FEFEquipmentResult::Success());
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::MoveToContainer(FGuid Guid, FGuid ContainerGuid, int32 Count)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    FString Error; auto* Container = ResolveContainer(ContainerGuid, Error); FInventoryItem Entry;
    if (!Container || !Equipment->GetItemByGuid(Guid, Entry)) return Publish(FEFEquipmentResult::Failure(Error.IsEmpty() ? TEXT("The item is unavailable.") : Error));
    const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
    if (Rules && Rules->Profile && Rules->Profile->bContainer) return Publish(FEFEquipmentResult::Failure(TEXT("Satchels cannot be placed inside other satchels.")));
    if (Count <= 0 || Count > Entry.Count) return Publish(FEFEquipmentResult::Failure(TEXT("Choose a valid quantity.")));
    if (Count < Entry.Count && !Entry.Item->Fragments.IsEmpty()) return Publish(FEFEquipmentResult::Failure(TEXT("Move this stateful stack as a whole.")));
    const double AddedWeight = static_cast<double>(Entry.Item->GetItemInfo().ItemWeight) * Count;
    if (Container->GetCurrentInventoryTotalWeight() + AddedWeight > Container->GetContentCapacity() + 0.000001)
        return Publish(FEFEquipmentResult::Failure(TEXT("The satchel cannot carry this weight.")));
    if (Entry.bIsEquipped)
    { auto Result = RequestUnequip(Guid); if (!Result.bSuccess) return Result; }
    bool bSuccess = false;
    {
        TGuardValue<bool> Operation(bOperation, true); Container->ReserveEntryCapacity(1);
        bSuccess = EFEquipmentACF::Transfer(Equipment, Container, Guid, Count, false, Error);
        if (bSuccess) bSuccess = FlushContainers(Error);
    }
    RefreshState(); return Publish(bSuccess ? FEFEquipmentResult::Success() : FEFEquipmentResult::Failure(Error));
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::MoveFromContainer(FGuid ContainerGuid, FGuid Guid, int32 Count)
{
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is busy or unavailable.")));
    FString Error; auto* Container = ResolveContainer(ContainerGuid, Error);
    if (!Container) return Publish(FEFEquipmentResult::Failure(Error));
    bool bSuccess = false;
    {
        TGuardValue<bool> Operation(bOperation, true);
        bSuccess = EFEquipmentACF::Transfer(Container, Equipment, Guid, Count, true, Error);
        if (bSuccess) bSuccess = FlushContainers(Error);
    }
    RefreshState(); return Publish(bSuccess ? FEFEquipmentResult::Success() : FEFEquipmentResult::Failure(Error));
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::RestoreEquippedIntent(const TArray<FInventoryItem>& Intent)
{
    if (bOperation || !InitializeEquipment()) return Publish(FEFEquipmentResult::Failure(TEXT("Equipment is not ready for restoration.")));
    TArray<FInventoryItem> Ordered = Intent;
    for (auto& Item : Ordered)
        for (const auto& Rule : Registry->MigrationRules)
            if (Rule.ItemClass.ToSoftObjectPath() == FSoftObjectPath(Item.ItemClass.Get()) && Rule.OldSlot == Item.EquipmentSlot)
            { Item.EquipmentSlot = Rule.NewSlot; break; }
    {
        TGuardValue<bool> Operation(bOperation, true);
        for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems()) Equipment->UnequipItemByGuid(Current.ItemGuid);
        for (const auto& Item : Equipment->GetInventory())
            EFEquipmentACF::Inventory(Equipment)->MarkItemAsEquipped(Item.GetItemGuid(), false, FGameplayTag());
    }
    Ordered.Sort([this](const auto& A, const auto& B)
    {
        const int32 ReservationsA = Occupancy(A.Item, A.EquipmentSlot).Num(), ReservationsB = Occupancy(B.Item, B.EquipmentSlot).Num();
        if (ReservationsA != ReservationsB) return ReservationsA > ReservationsB;
        const auto* SlotA = Registry->Find(A.EquipmentSlot); const auto* SlotB = Registry->Find(B.EquipmentSlot);
        const int32 OrderA = SlotA ? SlotA->Order : MAX_int32, OrderB = SlotB ? SlotB->Order : MAX_int32;
        if (OrderA != OrderB) return OrderA < OrderB;
        return A.GetItemGuid().ToString() < B.GetItemGuid().ToString();
    });
    FEFEquipmentResult Summary = FEFEquipmentResult::Success();
    for (auto& Item : Ordered)
    {
        if (!Item.bIsEquipped) continue;
        if (Item.Item && !Item.Item->IsA<UEFEquipmentArmor>() && !Item.Item->IsA<UEFEquipmentAccessory>())
        {
            Equipment->EquipItemFromInventoryInSlot(Item, Item.EquipmentSlot);
            if (!VerifyEquipped(Item.GetItemGuid(), Item.EquipmentSlot)) Summary.DisplacedItems.Add(Item.GetItemGuid());
            continue;
        }
        auto Preview = PreviewEquip(Item.GetItemGuid(), Item.EquipmentSlot);
        if (!Preview.bSuccess || !Preview.DisplacedItems.IsEmpty()) { Summary.DisplacedItems.Add(Item.GetItemGuid()); continue; }
        auto Result = RequestEquip(Item.GetItemGuid(), Item.EquipmentSlot);
        if (!Result.bSuccess) Summary.DisplacedItems.Add(Item.GetItemGuid());
    }
    EquipmentSchema = 1;
    if (!Summary.DisplacedItems.IsEmpty()) Summary.Message = FText::FromString(TEXT("Your equipment was updated. Incompatible items remain in your inventory."));
    return Publish(Summary);
}
