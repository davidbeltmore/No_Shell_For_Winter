#include "EFContainerInventoryComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentItems.h"
#include "EFEquipmentData.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/Crc.h"

UEFContainerInventoryComponent::UEFContainerInventoryComponent()
{ PrimaryComponentTick.bCanEverTick = false; }

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
    TArray<uint8> Candidate; FMemoryWriter Writer(Candidate);
    FObjectAndNameAsStringProxyArchive Ar(Writer, false); Ar.ArIsSaveGame = true; Ar.ArNoDelta = true;
    Serialize(Ar);
    if (Ar.IsError() || Candidate.Num() > 16 * 1024 * 1024) { Error = TEXT("Container serialization failed or exceeded 16 MiB."); return false; }
    TArray<uint8> Envelope; FMemoryWriter Header(Envelope);
    int32 Version = 1, Length = Candidate.Num(); uint32 Checksum = FCrc::MemCrc32(Candidate.GetData(), Candidate.Num());
    Header << Version << Length << Checksum; Header.Serialize(Candidate.GetData(), Candidate.Num());
    Bytes = MoveTemp(Envelope); return true;
}

bool UEFContainerInventoryComponent::ReadPayload(const TArray<uint8>& Bytes, FString& Error)
{
    if (Bytes.IsEmpty()) return true;
    if (Bytes.Num() > 16 * 1024 * 1024) { Error = TEXT("Container payload exceeds 16 MiB."); return false; }
    if (Bytes.Num() < 12) { Error = TEXT("Container envelope is truncated."); return false; }
    FMemoryReader Header(Bytes); int32 Version = 0, Length = 0; uint32 Checksum = 0;
    Header << Version << Length << Checksum;
    if (Version != 1 || Length != Bytes.Num() - Header.Tell() || Length < 0
        || FCrc::MemCrc32(Bytes.GetData() + Header.Tell(), Length) != Checksum)
    { Error = TEXT("Container envelope version, length or checksum is invalid."); return false; }
    TArray<uint8> Payload; Payload.Append(Bytes.GetData() + Header.Tell(), Length);
    FMemoryReader Reader(Payload); FObjectAndNameAsStringProxyArchive Ar(Reader, true); Ar.ArIsSaveGame = true; Ar.ArNoDelta = true;
    GetInventoryList().Init(GetOwner()); Serialize(Ar);
    if (Ar.IsError() || Reader.Tell() != Payload.Num() || !EFEquipmentACF::ValidateInventory(this, Error))
    { if (Error.IsEmpty()) Error = TEXT("Container payload is invalid."); return false; }
    for (const auto& Entry : GetInventory())
    {
        const auto* Rules = UEFEquipmentRulesFragment::Find(Entry.Item);
        if (Entry.bIsEquipped || (Rules && Rules->Profile && Rules->Profile->bContainer))
        { Error = TEXT("Containers cannot contain equipped items or other containers."); return false; }
        RegisterFragmentsForItem(Entry.Item);
    }
    MaxInventoryWeight = static_cast<float>(ContentCapacity);
    ReserveEntryCapacity(1); RefreshTotalWeight();
    // Previously valid over-capacity saves retain their contents; additions remain blocked.
    return true;
}
