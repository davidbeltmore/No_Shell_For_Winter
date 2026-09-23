#include "Storage/ProjectChestInventoryComponent.h"
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
    if (bProceduralMode || bDirectorOwnsContents) Items.Reset();
    Super::BeginPlay(); Items = Manual;
    if (auto* Lock = GetOwner()->FindComponentByClass<UProjectLockpickableComponent>())
    {
        if (Profile) Lock->Difficulty = Profile->LockDifficulty;
        if (bSavedLockState) Lock->SetLocked(bSavedLocked);
        Lock->bReplayOwnerACFInteractionOnSuccess = false;
        Lock->OnUnlockedInteractionRequested.AddUniqueDynamic(this, &ThisClass::ContinueUnlockedInteraction);
    }
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
    if (bGenerated || !bProceduralMode || bDirectorOwnsContents) return true;
    if (bGenerating || !GetOwner()->HasAuthority() || !Profile)
    { Error = TEXT("The chest generation profile is unavailable."); return false; }
    TGuardValue<bool> Guard(bGenerating, true);
    auto Rules = Profile->ContentRules;
    if (Rules.IsEmpty()) { Error = TEXT("Add at least one chest content rule."); return false; }
    for (auto& Rule : Rules)
    {
        if (!Rule.Rarity.IsValid()) Rule.Rarity = Profile->ChestRarity;
        if (!Rule.Category.IsValid() || !Rule.Rarity.IsValid() || Rule.MinItemCount < 0 || Rule.MaxItemCount < Rule.MinItemCount)
        { Error = TEXT("Chest content rules require category, rarity and valid quantity bounds."); return false; }
    }
    auto* PC = GetWorld()->GetFirstPlayerController();
    auto* Manager = PC ? PC->FindComponentByClass<UACFItemsManagerComponent>() : nullptr;
    TArray<FBaseItem> Generated;
    // ACF reports a zero count as generation failure. Resolve optional counts
    // first, then ask its catalog for the selected positive quantities.
    for (auto& Rule : Rules) Rule.MinItemCount = Rule.MaxItemCount = FMath::RandRange(Rule.MinItemCount, Rule.MaxItemCount);
    Rules.RemoveAll([](const FACFItemGenerationRule& Rule) { return Rule.MaxItemCount == 0; });
    if (!Rules.IsEmpty() && (!Manager || !Manager->GenerateItemsFromRules(Rules, Generated)))
    { Error = TEXT("The ACF catalog cannot satisfy every chest content rule."); return false; }
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
        GetInventoryList().Empty();
        for (const auto& Item : Before) GetInventoryList().AddEntry(Item);
        Error = TEXT("Chest generation could not preserve every generated quantity."); return false;
    }
    bGenerated = true;
    return true;
}
bool UProjectChestInventoryComponent::ConfigureResolvedLoot(TConstArrayView<FProjectCalystoResolvedChestEntry> Entries, FString& Error)
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
