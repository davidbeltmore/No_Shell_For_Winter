#include "EFContainerInventoryComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "EFEquipmentSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/Crc.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace EFContainerEnvelope { constexpr int32 MaximumBytes = 16 * 1024 * 1024; constexpr int32 HeaderBytes = 12; }


UEFContainerInventoryComponent::UEFContainerInventoryComponent()
{ PrimaryComponentTick.bCanEverTick = false; PriceMultiplierOnBuy = 0; PriceMultiplierOnSell = 0; bUseVendorCurrencyComponent = false; }

void UEFContainerInventoryComponent::InitializeContainer(FGuid Guid, double Capacity)
{
    ContainerGuid = Guid; ContentCapacity = Capacity;
    MaxInventoryWeight = static_cast<float>(Capacity);
    GetInventoryList().Init(GetOwner()); SetIsInitialized(true);
}

void UEFContainerInventoryComponent::ReserveEntryCapacity(int32 AdditionalEntries)
{ MaxInventorySlots = FMath::Max(MaxInventorySlots, GetInventory().Num() + FMath::Max(AdditionalEntries, 0)); }

bool UEFContainerInventoryComponent::WritePayload(TArray<uint8>& Bytes, FString& Error)
{
    if (!EFEquipmentACF::ValidateInventory(this, Error)) return false;
    for (const auto& Entry : GetInventory())
        if (const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item); Entry.bIsEquipped || (Rules && Rules->Profile && Rules->Profile->bContainer))
        { Error = TEXT("Containers cannot contain equipped items or other containers."); return false; }
    TArray<uint8> Candidate; FMemoryWriter Writer(Candidate);
    FEFEquipmentSaveArchive Ar(Writer, false, true);
    Serialize(Ar);
    if (Ar.IsError() || Candidate.Num() > EFContainerEnvelope::MaximumBytes - EFContainerEnvelope::HeaderBytes)
    { Error = TEXT("Container serialization failed or exceeded the 16 MiB envelope limit."); return false; }
    TArray<uint8> Envelope; FMemoryWriter Header(Envelope);
    int32 Version = 2, Length = Candidate.Num(); uint32 Checksum = FCrc::MemCrc32(Candidate.GetData(), Candidate.Num());
    Header << Version << Length << Checksum; Header.Serialize(Candidate.GetData(), Candidate.Num());
    Bytes = MoveTemp(Envelope); return true;
}

bool UEFContainerInventoryComponent::ReadPayload(const TArray<uint8>& Bytes, FString& Error)
{
    if (Bytes.IsEmpty()) return true;
    if (Bytes.Num() > EFContainerEnvelope::MaximumBytes) { Error = TEXT("Container payload exceeds 16 MiB."); return false; }
    if (Bytes.Num() < EFContainerEnvelope::HeaderBytes) { Error = TEXT("Container envelope is truncated."); return false; }
    FMemoryReader Header(Bytes); int32 Version = 0, Length = 0; uint32 Checksum = 0;
    Header << Version << Length << Checksum;
    if ((Version != 1 && Version != 2) || Length != Bytes.Num() - Header.Tell() || Length < 0
        || FCrc::MemCrc32(Bytes.GetData() + Header.Tell(), Length) != Checksum)
    { Error = TEXT("Container envelope version, length or checksum is invalid."); return false; }
    TArray<uint8> Payload; Payload.Append(Bytes.GetData() + Header.Tell(), Length);
    // Native deserialization empties the destination before validating its entries.
    // Read into isolated storage so a valid-checksum but invalid payload cannot
    // discard an already populated live container.
    TStrongObjectPtr<UEFContainerInventoryComponent> Candidate(NewObject<UEFContainerInventoryComponent>(GetOwner()));
    Candidate->InitializeContainer(ContainerGuid, ContentCapacity);
    FMemoryReader Reader(Payload); FEFEquipmentSaveArchive Ar(Reader, true, true);
    Candidate->Serialize(Ar);
    if (Ar.IsError() || Reader.Tell() != Payload.Num() || !EFEquipmentACF::ValidateInventory(Candidate.Get(), Error))
    { if (Error.IsEmpty()) Error = TEXT("Container payload is invalid."); Candidate->DestroyComponent(); return false; }
    const auto Entries = Candidate->GetInventory();
    for (const auto& Entry : Entries)
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
        if (Entry.bIsEquipped || (Rules && Rules->Profile && Rules->Profile->bContainer))
        { Error = TEXT("Containers cannot contain equipped items or other containers."); Candidate->DestroyComponent(); return false; }
    }
    for (const auto& Previous : GetInventory()) UnregisterFragmentsForItem(Previous.Item);
    GetInventoryList().Empty();
    GetInventoryList().Init(GetOwner());
    for (const auto& Entry : Entries) { GetInventoryList().AddEntry(Entry); RegisterFragmentsForItem(Entry.Item); }
    Candidate->GetInventoryList().Empty(); Candidate->DestroyComponent();
    MaxInventoryWeight = static_cast<float>(ContentCapacity);
    ReserveEntryCapacity(1); RefreshTotalWeight();
    // Previously valid over-capacity saves retain their contents; additions remain blocked.
    return true;
}
