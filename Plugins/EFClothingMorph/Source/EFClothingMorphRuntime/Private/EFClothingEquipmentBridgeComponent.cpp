#include "EFClothingEquipmentBridgeComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphSettings.h"
#include "EFClothingMorphV3RuntimeComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace EFClothingEquipment
{
    constexpr double ReadinessWindowSeconds = 5.0;
    constexpr double SafetyScanSeconds = 2.0;

    // Reflection preserves EFClothingMorph's optional ACF integration. Validate
    // every property type before reading a precompiled plugin's public contract.
    FStructProperty* TagProperty(UStruct* Type, const FName Name)
    {
        FStructProperty* Property = FindFProperty<FStructProperty>(Type, Name);
        return Property && Property->Struct == FGameplayTag::StaticStruct() ? Property : nullptr;
    }

    bool ArmorSlot(USkeletalMeshComponent* Component, FName& OutSlot)
    {
        for (UClass* Type = Component->GetClass(); Type; Type = Type->GetSuperClass())
        {
            if (Type->GetFName() == TEXT("ACFArmorSlotComponent"))
            {
                if (FStructProperty* Property = TagProperty(Type, TEXT("ArmorSlot")))
                {
                    OutSlot = Property->ContainerPtrToValuePtr<FGameplayTag>(Component)->GetTagName();
                    return !OutSlot.IsNone();
                }
            }
        }
        return false;
    }

    bool ArmorMesh(UObject* Item, AActor* Owner, FSoftObjectPath& OutPath)
    {
        UFunction* Function = Item ? Item->FindFunction(TEXT("GetArmorMesh")) : nullptr;
        if (!Function) return false;
        FObjectPropertyBase* OwnerProperty = FindFProperty<FObjectPropertyBase>(Function, TEXT("actorOwner"));
        FSoftObjectProperty* ReturnProperty = FindFProperty<FSoftObjectProperty>(Function, TEXT("ReturnValue"));
        if (!OwnerProperty || !ReturnProperty || !ReturnProperty->HasAnyPropertyFlags(CPF_ReturnParm)) return false;
        FStructOnScope Parameters(Function);
        OwnerProperty->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), Owner);
        Item->ProcessEvent(Function, Parameters.GetStructMemory());
        OutPath = ReturnProperty->GetPropertyValue_InContainer(Parameters.GetStructMemory()).ToSoftObjectPath();
        return true;
    }
}

UEFClothingEquipmentBridgeComponent::UEFClothingEquipmentBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UEFClothingEquipmentBridgeComponent::BeginPlay()
{
    Super::BeginPlay();
    BindEquipment();
    RenderDirtyHandle = UActorComponent::MarkRenderStateDirtyEvent.AddUObject(
        this, &UEFClothingEquipmentBridgeComponent::HandleRenderStateDirty);
    RefreshEquipment();
}

void UEFClothingEquipmentBridgeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    UActorComponent::MarkRenderStateDirtyEvent.Remove(RenderDirtyHandle);
    UnbindEquipment();
    MeshStates.Reset();
    HiddenByBridge.Reset();
    Super::EndPlay(Reason);
}

bool UEFClothingEquipmentBridgeComponent::BindEquipment()
{
    if (Equipment.IsValid()) return true;
    if (!GetOwner()) return false;
    TInlineComponentArray<UActorComponent*> Components(GetOwner());
    for (UActorComponent* Component : Components)
    {
        FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(
            Component->GetClass(), TEXT("OnEquippedArmorChanged"));
        if (!Property) continue;
        UFunction* Signature = Property->SignatureFunction;
        FStructProperty* Tag = Signature ? EFClothingEquipment::TagProperty(Signature, TEXT("ArmorSlot")) : nullptr;
        if (!Tag || Signature->NumParms != 1) continue;
        void* Address = Property->ContainerPtrToValuePtr<void>(Component);
        FMulticastScriptDelegate Delegate;
        if (const FMulticastScriptDelegate* Current = Property->GetMulticastDelegate(Address)) Delegate = *Current;
        FScriptDelegate Handler;
        Handler.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UEFClothingEquipmentBridgeComponent, HandleArmorChanged));
        Delegate.AddUnique(Handler);
        Property->SetMulticastDelegate(Address, MoveTemp(Delegate));
        Equipment = Component;
        bEquipmentDirty = true;
        return true;
    }
    return false;
}

void UEFClothingEquipmentBridgeComponent::UnbindEquipment()
{
    if (UActorComponent* Component = Equipment.Get())
    {
        if (FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(Component->GetClass(), TEXT("OnEquippedArmorChanged")))
        {
            void* Address = Property->ContainerPtrToValuePtr<void>(Component);
            FMulticastScriptDelegate Delegate;
            if (const FMulticastScriptDelegate* Current = Property->GetMulticastDelegate(Address)) Delegate = *Current;
            Delegate.Remove(this, GET_FUNCTION_NAME_CHECKED(UEFClothingEquipmentBridgeComponent, HandleArmorChanged));
            Property->SetMulticastDelegate(Address, MoveTemp(Delegate));
        }
    }
    Equipment.Reset();
}

void UEFClothingEquipmentBridgeComponent::Wake()
{
    ObserveUntil = FPlatformTime::Seconds() + EFClothingEquipment::ReadinessWindowSeconds;
    SetComponentTickIntervalAndCooldown(0.0f);
}

void UEFClothingEquipmentBridgeComponent::RefreshEquipment()
{
    bEquipmentDirty = true;
    Wake();
}

void UEFClothingEquipmentBridgeComponent::HandleArmorChanged(FGameplayTag ArmorSlot)
{
    (void)ArmorSlot;
    ++EquipmentEvents;
    RefreshEquipment(); // Defer until the current equipment mutation has completed.
}

void UEFClothingEquipmentBridgeComponent::HandleRenderStateDirty(UActorComponent& Component)
{
    if (!IsInGameThread() || bApplyingVisibility || Component.GetOwner() != GetOwner()) return;
    FName Slot;
    if (USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(&Component); Mesh && EFClothingEquipment::ArmorSlot(Mesh, Slot))
    {
        Wake(); // Late async completions also wake the observer after its window.
    }
}

bool UEFClothingEquipmentBridgeComponent::ReadDesiredSlots()
{
    UActorComponent* Component = Equipment.Get();
    if (!Component) return false;
    FStructProperty* EquipmentProperty = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("Equipment"));
    if (!EquipmentProperty) return false;
    FArrayProperty* Items = FindFProperty<FArrayProperty>(EquipmentProperty->Struct, TEXT("EquippedItems"));
    FStructProperty* ItemStruct = Items ? CastField<FStructProperty>(Items->Inner) : nullptr;
    if (!ItemStruct) return false;
    FStructProperty* SlotProperty = EFClothingEquipment::TagProperty(ItemStruct->Struct, TEXT("ItemSlot"));
    FObjectPropertyBase* ItemProperty = FindFProperty<FObjectPropertyBase>(ItemStruct->Struct, TEXT("Item"));
    if (!SlotProperty || !ItemProperty) return false;
    const void* EquipmentAddress = EquipmentProperty->ContainerPtrToValuePtr<void>(Component);
    FScriptArrayHelper Array(Items, Items->ContainerPtrToValuePtr<void>(EquipmentAddress));
    TMap<FName, FSoftObjectPath> NextSlots;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        const void* Row = Array.GetRawPtr(Index);
        const FName Slot = SlotProperty->ContainerPtrToValuePtr<FGameplayTag>(Row)->GetTagName();
        UObject* Item = ItemProperty->GetObjectPropertyValue_InContainer(Row);
        FSoftObjectPath Mesh;
        if (EFClothingEquipment::ArmorMesh(Item, GetOwner(), Mesh)) NextSlots.Add(Slot, Mesh);
        else if (!Slot.IsNone()) NextSlots.Add(Slot, FSoftObjectPath()); // Unknown item: do not infer absence.
    }
    DesiredSlots = MoveTemp(NextSlots);
    return true;
}

bool UEFClothingEquipmentBridgeComponent::IsCatalogGarment(const USkeletalMesh* Mesh) const
{
    const UEFClothingMorphDirectorPolicy* Director = GetDefault<UEFClothingMorphSettings>()->Director.Get();
    if (!Mesh || !Director) return false;
    const FSoftObjectPath Path(Mesh);
    return Director->Garments.ContainsByPredicate([&](const FEFClothingGarmentRow& Row) { return Row.SourceGarment.ToSoftObjectPath() == Path; });
}

void UEFClothingEquipmentBridgeComponent::ObserveMeshes()
{
    ++Scans;
    if (!GetOwner()) return;
    TInlineComponentArray<USkeletalMeshComponent*> Components(GetOwner());
    TMap<TWeakObjectPtr<USkeletalMeshComponent>, FMeshState> NextStates;
    bool bChanged = false;
    const UEFCharacterCustomizationComponent* Customization = GetOwner()->FindComponentByClass<UEFCharacterCustomizationComponent>();
    for (USkeletalMeshComponent* Component : Components)
    {
        FName Slot;
        if (!EFClothingEquipment::ArmorSlot(Component, Slot)) continue;
        USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
        if (bAuthoritativeSlots && IsCatalogGarment(Mesh))
        {
            const FSoftObjectPath* Desired = DesiredSlots.Find(Slot);
            const bool bObsolete = !Desired || (!Desired->IsNull() && *Desired != FSoftObjectPath(Mesh));
            TGuardValue<bool> VisibilityGuard(bApplyingVisibility, true);
            if (bObsolete && Component->IsVisible())
            {
                Component->SetVisibility(false);
                HiddenByBridge.Add(Component);
                ++VisibilityCorrections;
            }
            else if (!bObsolete && HiddenByBridge.Contains(Component))
            {
                if (!Customization || Customization->GetShowClothes()) Component->SetVisibility(true);
                HiddenByBridge.Remove(Component);
            }
        }
        const FMeshState State{Mesh, Component->IsVisible()};
        const FMeshState* Previous = MeshStates.Find(Component);
        bChanged |= !Previous || !(*Previous == State);
        NextStates.Add(Component, State);
    }
    bChanged |= NextStates.Num() != MeshStates.Num();
    MeshStates = MoveTemp(NextStates);
    for (auto It = HiddenByBridge.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
    if (bChanged)
    {
        if (UEFClothingMorphV3RuntimeComponent* Runtime = GetOwner()->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
        {
            Runtime->NotifyEquipmentChanged();
            ++Notifications;
        }
    }
}

void UEFClothingEquipmentBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    const double Now = FPlatformTime::Seconds();
    const bool bSafetyDue = Now >= NextSafetyScan;
    if (bSafetyDue)
    {
        BindEquipment();
        bEquipmentDirty = true;
        NextSafetyScan = Now + EFClothingEquipment::SafetyScanSeconds;
    }
    if (bEquipmentDirty)
    {
        bAuthoritativeSlots = ReadDesiredSlots();
        bEquipmentDirty = false;
    }
    if (Now <= ObserveUntil || bSafetyDue) ObserveMeshes();
    if (Now > ObserveUntil) SetComponentTickInterval(0.25f);
}

FString UEFClothingEquipmentBridgeComponent::GetDebugSummary() const
{
    return FString::Printf(TEXT("equipmentBridge bound=%d authoritative=%d events=%u scans=%u notifications=%u staleVisibilityCorrections=%u"),
        Equipment.IsValid(), bAuthoritativeSlots, EquipmentEvents, Scans, Notifications, VisibilityCorrections);
}
