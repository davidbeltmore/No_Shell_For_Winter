#include "Characters/ProjectNPCOutfitComponent.h"
#include "Characters/ProjectNPCAppearanceState.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentPersistence.h"
#include "EFClothingRuntimeComponent.h"
#include "EFClothingEquipmentBridgeComponent.h"
#include "EFClothingMorphV3RuntimeComponent.h"
#include "Components/ACFEquipmentComponent.h"
#include "Components/ACFArmorSlotComponent.h"
#include "Items/ACFArmor.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "Engine/GameInstance.h"

UProjectNPCOutfitComponent::UProjectNPCOutfitComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UProjectNPCOutfitComponent::Fail(const FString& Reason)
{
    Failure = Reason; State = EProjectNPCOutfitState::Failed;
    if (Load) Load->CancelHandle(); Load.Reset();
    UE_LOG(LogTemp, Error, TEXT("NPCOutfits FAIL %s: %s"), *GetNameSafe(GetOwner()), *Reason);
}

void UProjectNPCOutfitComponent::Start(const TArray<FProjectNPCOutfitChoice>& Choices)
{
    auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->HasAuthority() || Pawn->IsPlayerControlled()) { Fail(TEXT("Ineligible owner")); return; }
    auto* Saved = AProjectNPCAppearanceState::Ensure(GetWorld());
    if (Saved && ObservedRestoreSerial == Saved->RestoreSerial && State == EProjectNPCOutfitState::Ready) return;
    ObservedRestoreSerial = Saved ? Saved->RestoreSerial : -1;
    if (Load) Load->CancelHandle(); Load.Reset();
    Started = FPlatformTime::Seconds(); Failure.Reset(); State = EProjectNPCOutfitState::Pending;
    Identity = AProjectNPCAppearanceState::IdentityFor(Pawn);
    Equipment = Pawn->FindComponentByClass<UACFEquipmentComponent>();
    if (!Equipment) { Fail(TEXT("Native equipment missing")); return; }
    const auto* Record = Saved ? Saved->Records.Find(Identity) : nullptr;
    if (!Record) { Fail(TEXT("Appearance must resolve before outfit")); return; }
    bRestoring = Record->OutfitVersion != 0;
    if (bRestoring)
    {
        if (Record->OutfitVersion != 1 || Record->OutfitInventory.IsEmpty()) { Fail(TEXT("Unsupported or incomplete saved outfit; no items granted")); return; }
        SelectedId = Record->OutfitId;
        return; // Native initialization and save callbacks finish before Advance.
    }
    double Total = 0;
    for (const auto& Choice : Choices)
    {
        if (!FMath::IsFinite(Choice.Weight) || Choice.Weight < 0 || Choice.Outfit.IsNull()) { Fail(TEXT("Invalid outfit choice")); return; }
        Total += Choice.Weight;
    }
    if (Total <= 0 || !FMath::IsFinite(Total)) { Fail(TEXT("No positive outfit weights")); return; }
    FRandomStream Random(Record->Seed ^ 0x4F555446);
    double Roll = Random.FRand() * Total;
    FSoftObjectPath Selected;
    for (const auto& Choice : Choices) if (Choice.Weight > 0)
    {
        Selected = Choice.Outfit.ToSoftObjectPath(); Roll -= Choice.Weight;
        if (Roll < 0) break;
    }
    State = EProjectNPCOutfitState::Loading;
    Load = UAssetManager::GetStreamableManager().RequestAsyncLoad(Selected, FStreamableDelegate::CreateWeakLambda(this, [this, Selected]
    {
        Definition = Cast<UProjectNPCOutfit>(Selected.ResolveObject());
        FString Error;
        if (!Definition || !Definition->Validate(Error)) { Fail(TEXT("Invalid outfit definition: ") + Error); return; }
        SelectedId = Definition->Id;
        TArray<FSoftObjectPath> Paths;
        for (const auto& Row : Definition->Items) Paths.AddUnique(Row.Item.ToSoftObjectPath());
        Load = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateWeakLambda(this, [this]
        { if (State == EProjectNPCOutfitState::Loading) State = EProjectNPCOutfitState::Pending; }));
    }));
}

void UProjectNPCOutfitComponent::Apply()
{
    auto* Pawn = Cast<APawn>(GetOwner());
    Coordinator = UEFEquipmentCoordinatorComponent::CreateForNPC(Pawn);
    if (!Coordinator || !Coordinator->InitializeEquipment()) { Fail(TEXT("Shared equipment rules could not initialize")); return; }
    // Explicit attachment opts in this eligible owner without enabling all non-player pawns.
    if (!Pawn->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
    {
        auto* Clothing = NewObject<UEFClothingRuntimeComponent>(Pawn, TEXT("EFClothingRuntime"));
        Pawn->AddInstanceComponent(Clothing); Clothing->RegisterComponent();
    }
    if (!Pawn->FindComponentByClass<UEFClothingEquipmentBridgeComponent>())
    {
        auto* Bridge = NewObject<UEFClothingEquipmentBridgeComponent>(Pawn, TEXT("EFClothingEquipmentBridge"));
        Pawn->AddInstanceComponent(Bridge); Bridge->RegisterComponent();
    }
    auto* Saved = AProjectNPCAppearanceState::Ensure(GetWorld());
    auto* Record = Saved ? Saved->Records.Find(Identity) : nullptr;
    if (!Record) { Fail(TEXT("Appearance record disappeared")); return; }
    TGuardValue<bool> Restoring(bRestoring, true);
    FString Error;
    if (Record->OutfitVersion)
    {
        const TArray<uint8> Bytes = Record->OutfitInventory;
        for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems()) Equipment->UnequipItemByGuid(Current.ItemGuid);
        if (!EFEquipmentPersistence::DeserializeInventory(Equipment, Bytes, Error)) { Fail(Error); return; }
        const auto Intent = Equipment->GetInventory();
        const auto Result = Coordinator->RestoreEquippedIntent(Intent);
        if (!Result.bSuccess || !Result.DisplacedItems.IsEmpty()) { Fail(TEXT("Saved equipment could not be restored without displacement")); return; }
    }
    else
    {
        // Capture the native starting inventory before granting. Failure rolls back through the same native serializer.
        TArray<uint8> Before;
        if (!Definition || !EFEquipmentPersistence::SerializeInventory(Equipment, Before, Error)) { Fail(TEXT("Cannot capture initial inventory: ") + Error); return; }
        for (const auto& Row : Definition->Items)
        {
            UClass* Class = Row.Item.Get();
            if (!Class || !Class->IsChildOf(UACFArmor::StaticClass())) { Error = TEXT("Outfit item is not a loaded armor class"); break; }
            FInventoryItem Item;
            for (const auto& Existing : Equipment->GetInventory()) if (Existing.ItemClass == Class && Existing.Count > 0) { Item = Existing; break; }
            if (!Item.Item)
            {
                Equipment->AddItemToInventoryByClass(Class, 1, false);
                for (const auto& Existing : Equipment->GetInventory()) if (Existing.ItemClass == Class && Existing.Count > 0) { Item = Existing; break; }
            }
            if (!Item.Item) { Error = TEXT("Native inventory rejected outfit item"); break; }
            const auto Result = Coordinator->RequestEquip(Item.GetItemGuid(), Row.Destination);
            if (!Result.bSuccess) { Error = Result.Message.ToString(); break; }
        }
        if (!Error.IsEmpty())
        {
            const FString Cause = Error;
            for (const auto& Current : Equipment->GetCurrentEquipment().GetEquippedItems()) Equipment->UnequipItemByGuid(Current.ItemGuid);
            FString Recovery;
            const bool Recovered = EFEquipmentPersistence::DeserializeInventory(Equipment, Before, Recovery);
            if (Recovered) Coordinator->RestoreEquippedIntent(Equipment->GetInventory());
            Fail(Cause + (Recovered ? TEXT("; initial inventory restored") : TEXT("; recovery failed: ") + Recovery)); return;
        }
        Record->OutfitId = SelectedId;
        Record->OutfitVersion = 1;
    }
    State = EProjectNPCOutfitState::Equipped;
    Coordinator->OnStateChanged.AddUniqueDynamic(this, &UProjectNPCOutfitComponent::InventoryChanged);
}

void UProjectNPCOutfitComponent::Advance()
{
    if (State == EProjectNPCOutfitState::Failed || State == EProjectNPCOutfitState::Ready) return;
    if (FPlatformTime::Seconds() - Started > 20) { Fail(TEXT("Initialization/presentation exceeded 20 seconds")); return; }
    if (State == EProjectNPCOutfitState::Pending && Equipment && Equipment->GetIsInitialized())
    {
        Apply(); bRestoring = false;
        if (State == EProjectNPCOutfitState::Equipped) CaptureInventory();
    }
    if (State != EProjectNPCOutfitState::Equipped) return;
    const auto* Clothing = GetOwner()->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>();
    if (!Clothing) { Fail(TEXT("Clothing runtime missing")); return; }
    for (const auto& Item : Equipment->GetCurrentEquipment().GetEquippedItems())
    {
        const auto* Armor = Cast<UACFArmor>(Item.Item);
        if (!Armor) continue;
        const auto Expected = Armor->GetArmorMesh(GetOwner());
        bool Found = false;
        TInlineComponentArray<UACFArmorSlotComponent*> Slots(GetOwner());
        for (const auto* Slot : Slots)
            if (Slot->GetSkeletalMeshAsset() && FSoftObjectPath(Slot->GetSkeletalMeshAsset()) == Expected.ToSoftObjectPath()
                && Slot->IsVisible() && !Slot->bHiddenInGame && Clothing->GetGarmentRuntimeState(Slot) == EEFClothingMorphV3RuntimeState::Ready) { Found = true; break; }
        if (!Found) return;
    }
    State = EProjectNPCOutfitState::Ready; ReadySeconds = FPlatformTime::Seconds() - Started;
    Load.Reset();
}

void UProjectNPCOutfitComponent::InventoryChanged()
{
    if (!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) return;
    if (bRestoring || State == EProjectNPCOutfitState::Pending || State == EProjectNPCOutfitState::Loading) return;
    if (auto* Save = GetWorld()->GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>(); Save && Save->GetSystemState() != ELoadingState::EIdle) return;
    // Native changes are already published by the coordinator. Snapshot now:
    // destruction or a save in this same frame must not resurrect a removed item.
    CaptureInventory();
}

void UProjectNPCOutfitComponent::CaptureInventory()
{
    if (bRestoring || !Equipment || Identity.IsEmpty() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) return;
    if (auto* Save = GetWorld()->GetGameInstance()->GetSubsystem<UALSLoadAndSaveSubsystem>(); Save && Save->GetSystemState() != ELoadingState::EIdle) return;
    auto* Saved = AProjectNPCAppearanceState::Ensure(GetWorld());
    auto* Record = Saved ? Saved->Records.Find(Identity) : nullptr;
    if (!Record || Record->OutfitVersion != 1) return;
    FString Error;
    if (!EFEquipmentPersistence::SerializeInventory(Equipment, Record->OutfitInventory, Error)) { Fail(TEXT("Inventory snapshot failed: ") + Error); return; }
}

void UProjectNPCOutfitComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (Load) Load->CancelHandle(); Load.Reset();
    if (Coordinator) Coordinator->OnStateChanged.RemoveDynamic(this, &UProjectNPCOutfitComponent::InventoryChanged);
    // Do not recapture after native ACF teardown has destroyed equipment.
    Super::EndPlay(Reason);
}
