#include "EFEquipmentSaveFiles.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
bool EFEquipmentSaveFiles::FailBeforePublication = false;
#endif

namespace EFEquipmentSaveFilesPrivate
{
    bool SlotPath(const FString& Slot, FString& Path, FString& Error)
    {
        if (Slot.IsEmpty() || Slot.Len() > 128 || Slot != FPaths::GetCleanFilename(Slot)
            || Slot != FPaths::MakeValidFileName(Slot) || Slot.Contains(TEXT("..")) || Slot.EndsWith(TEXT(".")))
        { Error = TEXT("The save slot name is invalid."); return false; }
        const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames"));
        Path = FPaths::ConvertRelativePathToFull(Root / (Slot + TEXT(".sav")));
        FPaths::NormalizeFilename(Path);
        if (!FPaths::IsUnderDirectory(Path, Root)) { Error = TEXT("The save slot is outside the project save directory."); return false; }
        return true;
    }

    bool SamePhysicalBytes(const FString& Path, const TArray<uint8>& Expected)
    { TArray<uint8> Bytes; return FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes == Expected; }

    bool Install(const FString& Destination, const FString& Staged, bool bReplace)
    {
#if PLATFORM_WINDOWS
        // IFileManager::Move(Replace=true) deletes the destination first. Use the
        // Windows replacement primitive for this project's local Windows backend.
        return bReplace ? !!::ReplaceFileW(*Destination, *Staged, nullptr, 0, nullptr, nullptr)
            : !!::MoveFileW(*Staged, *Destination);
#else
        return !bReplace && FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Destination, *Staged);
#endif
    }
}

bool EFEquipmentSaveFiles::Backup(const FString& Slot, FEFEquipmentSlotBackup& Out, FString& Error)
{
    using namespace EFEquipmentSaveFilesPrivate;
    Error.Reset(); Out = {};
    FString Path;
    if (!IsInGameThread() || !SlotPath(Slot, Path, Error)) return false;
    Out.SlotName = Slot; Out.bExisted = UGameplayStatics::DoesSaveGameExist(Slot, 0);
    if (!Out.bExisted)
    {
        if (IFileManager::Get().FileExists(*Path)) { Error = TEXT("The active save backend disagrees with the local slot path."); return false; }
        return true;
    }
    if (!UGameplayStatics::LoadDataFromSlot(Out.OriginalBytes, Slot, 0) || !SamePhysicalBytes(Path, Out.OriginalBytes))
    { Error = TEXT("The original slot could not be read and verified for a local backup."); return false; }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("SaveGames/EFEquipmentBackups") / Slot;
    IFileManager::Get().MakeDirectory(*Directory, true);
    Out.BackupPath = FPaths::ConvertRelativePathToFull(Directory / (FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".sav")));
    if (IFileManager::Get().FileExists(*Out.BackupPath) || !FFileHelper::SaveArrayToFile(Out.OriginalBytes, *Out.BackupPath)
        || !SamePhysicalBytes(Out.BackupPath, Out.OriginalBytes))
    { Error = TEXT("The original save backup could not be verified. Conversion was not started."); return false; }
    FSHAHash Hash; FSHA1::HashBuffer(Out.OriginalBytes.GetData(), Out.OriginalBytes.Num(), Hash.Hash);
    const FString Receipt = FString::Printf(TEXT("Equipment backup version 1\nSlot: %s\nBytes: %d\nSHA1: %s\nRestore this file with the earlier game version.\n"),
        *Slot, Out.OriginalBytes.Num(), *Hash.ToString());
    if (!FFileHelper::SaveStringToFile(Receipt, *(Out.BackupPath + TEXT(".txt"))))
    { Error = TEXT("The save backup receipt could not be written. Conversion was not started."); return false; }
    return true;
}

bool EFEquipmentSaveFiles::Recover(const FEFEquipmentSlotBackup& Before, FString& Error)
{
    using namespace EFEquipmentSaveFilesPrivate;
    FString Path; if (!SlotPath(Before.SlotName, Path, Error)) return false;
    if (!Before.bExisted)
    {
        if (UGameplayStatics::DoesSaveGameExist(Before.SlotName, 0) && !UGameplayStatics::DeleteGameInSlot(Before.SlotName, 0))
        { Error = TEXT("The failed new save could not be removed."); return false; }
        return true;
    }
    if (!SamePhysicalBytes(Before.BackupPath, Before.OriginalBytes)) { Error = TEXT("The original backup is unavailable for recovery."); return false; }
    if (SamePhysicalBytes(Path, Before.OriginalBytes)) return true;
    const FString Recovery = Path + TEXT(".recover_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveArrayToFile(Before.OriginalBytes, *Recovery) || !SamePhysicalBytes(Recovery, Before.OriginalBytes)
        || !Install(Path, Recovery, IFileManager::Get().FileExists(*Path)) || !SamePhysicalBytes(Path, Before.OriginalBytes))
    { Error = TEXT("Save recovery needs the retained original backup: ") + Before.BackupPath; return false; }
    return true;
}

bool EFEquipmentSaveFiles::Publish(USaveGame* Candidate, const FEFEquipmentSlotBackup& Before,
    TFunction<bool(USaveGame*, FString&)> Validate, FString& Error)
{
    using namespace EFEquipmentSaveFilesPrivate;
    Error.Reset(); FString Path;
    if (!IsInGameThread() || !Candidate || !SlotPath(Before.SlotName, Path, Error) || !Validate || !Validate(Candidate, Error)) return false;
#if !PLATFORM_WINDOWS
    Error = TEXT("Verified equipment save replacement is currently supported by the Windows local save backend only."); return false;
#endif
    TArray<uint8> Bytes;
    if (!UGameplayStatics::SaveGameToMemory(Candidate, Bytes)) { Error = TEXT("Save serialization failed."); return false; }
    TStrongObjectPtr<USaveGame> Reopened(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!Reopened || Reopened->GetClass() != Candidate->GetClass() || !Validate(Reopened.Get(), Error))
    { if (Error.IsEmpty()) Error = TEXT("Serialized save integrity validation failed."); return false; }
    const FString StagedSlot = TEXT("__EFEquipmentPending_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString StagedPath; if (!SlotPath(StagedSlot, StagedPath, Error)) return false;
    struct FRemoveStage { FString Slot; ~FRemoveStage() { if (UGameplayStatics::DoesSaveGameExist(Slot, 0)) UGameplayStatics::DeleteGameInSlot(Slot, 0); } } Cleanup{StagedSlot};
    if (!UGameplayStatics::SaveDataToSlot(Bytes, StagedSlot, 0) || !SamePhysicalBytes(StagedPath, Bytes))
    { Error = TEXT("The staged save could not be verified through the active local backend."); return false; }
    TArray<uint8> Check; const bool bExistsNow = UGameplayStatics::DoesSaveGameExist(Before.SlotName, 0);
    if (bExistsNow != Before.bExisted || (bExistsNow && (!UGameplayStatics::LoadDataFromSlot(Check, Before.SlotName, 0)
        || Check != Before.OriginalBytes || !SamePhysicalBytes(Before.BackupPath, Before.OriginalBytes))))
    { Error = TEXT("The original save changed after its backup. The new save was not published."); return false; }
#if WITH_DEV_AUTOMATION_TESTS
    if (FailBeforePublication) { FailBeforePublication = false; Error = TEXT("Injected failure before save publication."); return false; }
#endif
    if (!Install(Path, StagedPath, Before.bExisted) || !SamePhysicalBytes(Path, Bytes))
    {
        FString RecoveryError;
        Error = Recover(Before, RecoveryError) ? TEXT("Save publication failed. The original slot was retained.") : RecoveryError;
        return false;
    }
    return true;
}
