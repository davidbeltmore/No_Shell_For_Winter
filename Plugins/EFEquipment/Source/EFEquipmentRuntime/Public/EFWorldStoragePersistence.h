#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFWorldStoragePersistence.generated.h"

class UACFStorageComponent;

/** Native serialized storage, never an independently playable inventory. */
USTRUCT()
struct EFEQUIPMENTRUNTIME_API FEFWorldStorageRecord
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FSoftClassPath ComponentClass;
    UPROPERTY(SaveGame) TArray<uint8> Bytes;
};

/** Stable world storage snapshots survive streaming and map travel in this game session. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFWorldStoragePersistence : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    bool RegisterStorage(const FString& Key, UACFStorageComponent* Storage, FString& Error);
    bool ReleaseStorage(const FString& Key, UACFStorageComponent* Storage, FString& Error);
    bool Capture(TMap<FString, FEFWorldStorageRecord>& Out, FString& Error);
    bool Restore(const TMap<FString, FEFWorldStorageRecord>& In, FString& Error);
private:
    UPROPERTY() TMap<FString, FEFWorldStorageRecord> Records;
    TMap<FString, TWeakObjectPtr<UACFStorageComponent>> Live;
    bool Write(UACFStorageComponent* Storage, FEFWorldStorageRecord& Out, FString& Error);
    bool Read(UACFStorageComponent* Storage, const FEFWorldStorageRecord& In, FString& Error);
};
