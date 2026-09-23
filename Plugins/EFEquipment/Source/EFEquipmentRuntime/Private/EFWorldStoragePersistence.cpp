#include "EFWorldStoragePersistence.h"
#include "Components/ACFStorageComponent.h"
#include "EFEquipmentPersistence.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

bool UEFWorldStoragePersistence::Write(UACFStorageComponent* Storage, FEFWorldStorageRecord& Out, FString& Error)
{
    if (!EFEquipmentPersistence::ValidateContents(Storage, Error)) return false;
    auto* SaveEvent = Storage->FindFunction(TEXT("OnComponentSaved"));
    if (!SaveEvent) { Error = TEXT("Native storage save callback is unavailable."); return false; }
    Storage->ProcessEvent(SaveEvent, nullptr);
    FEFWorldStorageRecord Candidate; Candidate.ComponentClass = FSoftClassPath(Storage->GetClass());
    FMemoryWriter Writer(Candidate.Bytes); FEFEquipmentSaveArchive Ar(Writer, false, true);
    Storage->Serialize(Ar);
    if (Ar.IsError() || Candidate.Bytes.Num() > 16 * 1024 * 1024)
    { Error = TEXT("World storage snapshot exceeds its 16 MiB bound."); return false; }
    Out = MoveTemp(Candidate); return true;
}
bool UEFWorldStoragePersistence::Read(UACFStorageComponent* Storage, const FEFWorldStorageRecord& In, FString& Error)
{
    if (In.ComponentClass != FSoftClassPath(Storage->GetClass()) || In.Bytes.IsEmpty() || In.Bytes.Num() > 16 * 1024 * 1024)
    { Error = TEXT("World storage snapshot class or size is incompatible."); return false; }
    EFEquipmentACF::Inventory(Storage)->Init(Storage->GetOwner());
    FMemoryReader Reader(In.Bytes); FEFEquipmentSaveArchive Ar(Reader, true, true); Storage->Serialize(Ar);
    if (Ar.IsError() || Reader.Tell() != In.Bytes.Num()) { Error = TEXT("World storage snapshot is truncated."); return false; }
    auto* LoadEvent = Storage->FindFunction(TEXT("OnComponentLoaded"));
    if (!LoadEvent) { Error = TEXT("Native storage load callback is unavailable."); return false; }
    Storage->ProcessEvent(LoadEvent, nullptr); Storage->RefreshTotalWeight();
    return EFEquipmentPersistence::ValidateContents(Storage, Error);
}
bool UEFWorldStoragePersistence::RegisterStorage(const FString& Key, UACFStorageComponent* Storage, FString& Error)
{
    if (Key.IsEmpty() || !Storage) { Error = TEXT("World storage requires a stable identity."); return false; }
    if (const auto* Existing = Live.Find(Key); Existing && Existing->IsValid() && Existing->Get() != Storage)
    { Error = TEXT("Duplicate world storage identity. Opening is disabled."); return false; }
    if (const auto* Record = Records.Find(Key); Record && !Read(Storage, *Record, Error)) return false;
    Live.Add(Key, Storage); return true;
}
bool UEFWorldStoragePersistence::ReleaseStorage(const FString& Key, UACFStorageComponent* Storage, FString& Error)
{
    const auto* Found = Live.Find(Key);
    if (!Found || Found->Get() != Storage) return true;
    FEFWorldStorageRecord Record;
    const bool Saved = Write(Storage, Record, Error);
    if (Saved) Records.Add(Key, MoveTemp(Record));
    Live.Remove(Key); return Saved;
}
bool UEFWorldStoragePersistence::Capture(TMap<FString, FEFWorldStorageRecord>& Out, FString& Error)
{
    auto Candidate = Records;
    for (const auto& Pair : Live)
        if (Pair.Value.IsValid() && !Write(Pair.Value.Get(), Candidate.FindOrAdd(Pair.Key), Error)) return false;
    int64 Total = 0; for (const auto& Pair : Candidate) Total += Pair.Value.Bytes.Num();
    if (Total > 64 * 1024 * 1024) { Error = TEXT("World storage snapshots exceed the 64 MiB session limit."); return false; }
    Records = Candidate; Out = MoveTemp(Candidate); return true;
}
bool UEFWorldStoragePersistence::Restore(const TMap<FString, FEFWorldStorageRecord>& In, FString& Error)
{
    int64 Total = 0;
    for (const auto& Pair : In)
    {
        Total += Pair.Value.Bytes.Num();
        if (Pair.Key.IsEmpty() || Pair.Value.Bytes.IsEmpty() || Pair.Value.Bytes.Num() > 16 * 1024 * 1024)
        { Error = TEXT("Invalid world storage snapshot."); return false; }
    }
    if (Total > 64 * 1024 * 1024) { Error = TEXT("World storage snapshots exceed the session limit."); return false; }
    for (const auto& Pair : Live)
        if (const auto* Record = In.Find(Pair.Key); Record && Pair.Value.IsValid() && !Read(Pair.Value.Get(), *Record, Error)) return false;
    Records = In; return true;
}
