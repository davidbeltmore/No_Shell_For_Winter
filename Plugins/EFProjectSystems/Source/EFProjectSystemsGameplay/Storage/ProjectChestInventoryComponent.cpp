#include "Storage/ProjectChestInventoryComponent.h"
#include "Storage/ProjectProceduralChestComponent.h"
#include "Misc/Crc.h"
#include "EFWorldStoragePersistence.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "ACFItemsManagerComponent.h"
#include "ACFItemSystemFunctionLibrary.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Interfaces/ACFInteractableInterface.h"
#include "GameFramework/Pawn.h"
#include "Components/ACFInteractionComponent.h"
#include "TimerManager.h"

UProjectChestInventoryComponent::UProjectChestInventoryComponent()
{
    PriceMultiplierOnBuy = 0; PriceMultiplierOnSell = 0; bUseVendorCurrencyComponent = false;
}
void UProjectChestInventoryComponent::BeginPlay()
{
    // The native component seeds manual Items in BeginPlay. Resolved/procedural
    // content has a different, exclusive owner and must not get a second seed.
    const auto Manual = Items;
    const auto* Procedural = GetOwner()->FindComponentByClass<UProjectProceduralChestComponent>();
    if (bProceduralMode || (Procedural && Procedural->bProceduralMode) || bDirectorOwnsContents) Items.Reset();
    Super::BeginPlay(); Items = Manual;
    if (Procedural && Procedural->StableIdentity.IsValid() && GetOwner()->HasAuthority() && GetWorld()->GetGameInstance() && !bDirectorOwnsContents)
    {
        PersistenceKey = UWorld::RemovePIEPrefix(GetOwner()->GetLevel()->GetOutermost()->GetName()) + TEXT(":") + Procedural->StableIdentity.ToString(EGuidFormats::Digits);
        GetWorld()->GetGameInstance()->GetSubsystem<UEFWorldStoragePersistence>()->RegisterStorage(PersistenceKey, this, PersistenceError);
    }
    if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>())
    {
        if (Profile) Lock->Difficulty = Profile->LockDifficulty;
        if (bSavedLockState) Lock->SetLocked(bSavedLocked);
        Lock->bReplayOwnerACFInteractionOnSuccess = false;
        Lock->OnUnlockedInteractionRequested.AddUniqueDynamic(this, &ThisClass::ContinueUnlockedInteraction);
    }
    // Component BeginPlay order is not a lock-state contract. Reapply restored
    // state after every sibling (including Lockpickable) has initialized.
    if (bSavedLockState)
    {
        FTimerDelegate RestoreLock;
        RestoreLock.BindWeakLambda(this, [this]()
        {
            if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>()) Lock->SetLocked(bSavedLocked);
        });
        GetWorld()->GetTimerManager().SetTimerForNextTick(RestoreLock);
    }
}
void UProjectChestInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!PersistenceKey.IsEmpty() && GetWorld() && GetWorld()->GetGameInstance())
    {
        FString Error;
        if (!GetWorld()->GetGameInstance()->GetSubsystem<UEFWorldStoragePersistence>()->ReleaseStorage(PersistenceKey, this, Error))
            UE_LOG(LogTemp, Error, TEXT("Chest persistence failed: %s"), *Error);
    }
    Super::EndPlay(Reason);
}
void UProjectChestInventoryComponent::ContinueUnlockedInteraction(APawn* Pawn, const FString& InteractionType)
{
    if (!Pawn || !Pawn->IsLocallyControlled() || bUnlockContinuationPending) return;
    auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>();
    if (!Lock || Lock->IsLocked()) return;
    // Finish the lockpick request first. Only then start the native opening
    // action, whose animation notify opens the deposit. Never replay its local
    // storage event inside a still-running Confirm callback.
    bUnlockContinuationPending = true;
    FTimerDelegate Continue;
    Continue.BindWeakLambda(this, [this, WeakPawn = TWeakObjectPtr<APawn>(Pawn), InteractionType]()
    {
        bUnlockContinuationPending = false;
        auto* CurrentLock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>();
        if (!WeakPawn.IsValid() || !CurrentLock || CurrentLock->IsLocked()) return;
        if (auto* Interaction = WeakPawn->FindComponentByClass<UACFInteractionComponent>())
        {
            Interaction->SetCurrentBestInteractable(GetOwner());
            Interaction->Interact(InteractionType);
        }
    });
    GetWorld()->GetTimerManager().SetTimerForNextTick(Continue);
}
void UProjectChestInventoryComponent::OnComponentSaved_Implementation()
{
    Super::OnComponentSaved_Implementation();
    if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>())
    { bSavedLocked = Lock->IsLocked(); bSavedLockState = true; }
}
void UProjectChestInventoryComponent::OnComponentLoaded_Implementation()
{
    Super::OnComponentLoaded_Implementation();
    if (bSavedLockState)
        if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>()) Lock->SetLocked(bSavedLocked);
}
bool UProjectChestInventoryComponent::PrepareInteraction(AActor* Owner)
{
    auto* Storage = Owner ? Owner->FindComponentByClass<UProjectChestInventoryComponent>() : nullptr;
    FString Error;
    if (!Storage || !Storage->EnsureContents(Error))
    { UE_LOG(LogTemp, Warning, TEXT("Chest interaction rejected: %s"), *Error); return false; }
    return true;
}
bool UProjectChestInventoryComponent::EnsureContents(FString& Error)
{
    Error.Reset();
    // Runtime adapters may assign their stable context after spawning. Bind it
    // before the first opening, never after generated items have been published.
    const auto* Procedural = GetOwner()->FindComponentByClass<UProjectProceduralChestComponent>();
    if (PersistenceKey.IsEmpty() && PersistenceError.IsEmpty() && !bDirectorOwnsContents && Procedural
        && Procedural->StableIdentity.IsValid() && GetOwner()->HasAuthority() && GetWorld()->GetGameInstance())
    {
        PersistenceKey = UWorld::RemovePIEPrefix(GetOwner()->GetLevel()->GetOutermost()->GetName()) + TEXT(":") + Procedural->StableIdentity.ToString(EGuidFormats::Digits);
        GetWorld()->GetGameInstance()->GetSubsystem<UEFWorldStoragePersistence>()->RegisterStorage(PersistenceKey, this, PersistenceError);
    }
    if (!PersistenceError.IsEmpty()) { Error = PersistenceError; return false; }
    if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>(); Lock && Lock->IsLocked())
    { Error = TEXT("The chest is locked."); return false; }
    const bool NewMode = Procedural && Procedural->bProceduralMode;
    if (bGenerated || bDirectorOwnsContents || (!NewMode && !bProceduralMode)) return true;
    if (bGenerating || !GetOwner()->HasAuthority())
    { Error = TEXT("Chest generation requires an idle authoritative inventory."); return false; }
    if (!GetInventory().IsEmpty())
    { Error = TEXT("Procedural generation refuses to replace or append to existing manual contents."); return false; }
    TGuardValue<bool> Guard(bGenerating, true);
    TArray<FBaseItem> Generated;
    if (NewMode)
    {
        if (!Procedural->PreviewContents(Generated, Error)) return false;
    }
    else
    {
        // Compatibility for existing profiles. New authoring uses separate
        // selection/stack/rarity controls; old rules retain their unit counts.
        if (!Profile || !Procedural || !Procedural->StableIdentity.IsValid())
        { Error = TEXT("Legacy generation requires a profile and stable Procedural Chest identity."); return false; }
        auto* PC = GetWorld()->GetFirstPlayerController();
        auto* Manager = PC ? PC->FindComponentByClass<UACFItemsManagerComponent>() : nullptr;
        if (!Manager || Profile->ContentRules.IsEmpty())
        { Error = TEXT("Legacy chest catalog or rules are unavailable."); return false; }
        int32 RuleIndex = 0;
        for (const auto& Rule : Profile->ContentRules)
        {
            if (Rule.MinItemCount < 0 || Rule.MaxItemCount < Rule.MinItemCount || Rule.MaxItemCount > 255)
            { Error = TEXT("Legacy rule has invalid quantity bounds."); return false; }
            FRandomStream Random(FCrc::StrCrc32(*Procedural->StableIdentity.ToString(), Procedural->Seed + RuleIndex++));
            const int32 Units = Random.RandRange(Rule.MinItemCount, Rule.MaxItemCount);
            FProjectChestContentSettings Settings; Settings.Catalog = Manager->GetItemsDB();
            Settings.Categories.AddTag(Rule.Category);
            FProjectChestRarityChance Chance; Chance.Rarity = Rule.Rarity.IsValid() ? Rule.Rarity : Profile->ChestRarity;
            Settings.ItemRarities.Add(Chance); Settings.FixedSelections = Units ? 1 : 0;
            Settings.MinUnitsPerStack = Settings.MaxUnitsPerStack = FMath::Max(1, Units);
            TArray<FBaseItem> Part;
            if (!FProjectChestSelector::Generate(Settings, Procedural->StableIdentity, Procedural->Seed + RuleIndex, Part, Error)) return false;
            Generated.Append(Part);
        }
    }
    // Generate fully before publishing: the native generator can return partial
    // results with false; those results never enter this inventory.
    const auto Before = GetInventory();
    TMap<UClass*, int32> Expected, Actual;
    for (const auto& Item : Before) Expected.FindOrAdd(Item.ItemClass) += Item.Count;
    for (const auto& Item : Generated)
    {
        if (!Item.ItemClass || Item.Count < 0) { Error = TEXT("ACF generated an invalid chest item."); return false; }
        Expected.FindOrAdd(Item.ItemClass) += Item.Count;
    }
    AddItems(Generated);
    for (const auto& Item : GetInventory()) Actual.FindOrAdd(Item.ItemClass) += Item.Count;
    // Zero-count rules are legitimate optional entries, not stored objects.
    for (auto It = Expected.CreateIterator(); It; ++It) if (It.Value() == 0) It.RemoveCurrent();
    if (!Expected.OrderIndependentCompareEqual(Actual))
    {
        // Native removal tears down item/fragment registration and weight too.
        for (const auto& Item : GetInventory()) RemoveItem(Item, Item.Count);
        RefreshTotalWeight();
        if (!GetInventory().IsEmpty()) PersistenceError = TEXT("Chest publication rollback was incomplete. Opening is disabled; retained items require recovery.");
        Error = TEXT("Chest generation could not preserve every generated quantity."); return false;
    }
    bGenerated = true;
    return true;
}
bool UProjectChestInventoryComponent::ConfigureResolvedLoot(TConstArrayView<FProjectChestResolvedEntry> Entries, FString& Error)
{
    Error.Reset();
    if (GetOwner()->HasActorBegunPlay() || bGenerated || bDirectorOwnsContents || !GetOwner()->HasAuthority() || !GetInventory().IsEmpty())
    { Error = TEXT("Resolved chest loot requires an empty authoritative deferred actor."); return false; }
    TSet<FName> Ids;
    for (const auto& Entry : Entries)
    {
        FItemDescriptor Descriptor;
        if (Entry.StableAttemptId.IsNone() || Entry.ContentCatalogId.IsNone() || Ids.Contains(Entry.StableAttemptId)
            || Entry.Quantity != 1 || !UACFItemSystemFunctionLibrary::GetItemData(Entry.ItemClass, Descriptor))
        { Error = TEXT("Resolved chest loot contains an invalid or duplicate entry."); return false; }
        Ids.Add(Entry.StableAttemptId);
    }
    FrozenContents.Reset(); FrozenContents.Append(Entries.GetData(), Entries.Num());
    bDirectorOwnsContents = true;
    for (const auto& Entry : Entries) AddItem(FBaseItem(Entry.ItemClass, Entry.Quantity));
    if (!VerifyResolvedLoot(Error)) return false;
    bGenerated = true;
    return true;
}
bool UProjectChestInventoryComponent::VerifyResolvedLoot(FString& Error) const
{
    TMap<UClass*, int32> Expected, Actual;
    for (const auto& Entry : FrozenContents) Expected.FindOrAdd(Entry.ItemClass) += Entry.Quantity;
    for (const auto& Item : GetInventory()) Actual.FindOrAdd(Item.ItemClass) += Item.Count;
    if (!Expected.OrderIndependentCompareEqual(Actual))
    { Error = TEXT("Chest inventory does not match its resolved content manifest."); return false; }
    return true;
}
bool UProjectChestInventoryComponent::FinalizeAndVerifyResolvedLoot(TArray<FName>& Verified, FString& Error)
{
    Verified.Reset();
    if (!bDirectorOwnsContents || !bGenerated || bDirectorVerified || !GetOwner()->HasActorBegunPlay() || !VerifyResolvedLoot(Error)) return false;
    for (const auto& Entry : FrozenContents) Verified.Add(Entry.ContentCatalogId);
    bDirectorVerified = true;
    return true;
}
