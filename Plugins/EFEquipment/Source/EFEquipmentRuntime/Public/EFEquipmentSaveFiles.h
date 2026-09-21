#pragma once
#include "CoreMinimal.h"

class USaveGame;

/** Exact pre-operation bytes and an immutable backup, prepared before conversion. */
struct EFEQUIPMENTRUNTIME_API FEFEquipmentSlotBackup
{
    FString SlotName;
    FString BackupPath;
    TArray<uint8> OriginalBytes;
    bool bExisted = false;
};

namespace EFEquipmentSaveFiles
{
    EFEQUIPMENTRUNTIME_API bool Backup(const FString& Slot, FEFEquipmentSlotBackup& Out, FString& Error);
    /** Caller validates domain integrity; this verifies native serialization and physical publication. */
    EFEQUIPMENTRUNTIME_API bool Publish(USaveGame* Candidate, const FEFEquipmentSlotBackup& Before,
        TFunction<bool(USaveGame*, FString&)> Validate, FString& Error);
    EFEQUIPMENTRUNTIME_API bool Recover(const FEFEquipmentSlotBackup& Before, FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
    extern EFEQUIPMENTRUNTIME_API bool FailBeforePublication;
#endif
}
