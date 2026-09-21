#include "EFEquipmentSaveLibrary.h"
#include "EFEquipmentSaveSubsystem.h"
#include "ANSUIPlayerSubsystem.h"
#include "Kismet/GameplayStatics.h"

namespace EFEquipmentSaveEntry
{
    UEFEquipmentSaveSubsystem* Resolve(UALSLoadAndSaveSubsystem* Target)
    { return Target && Target->GetGameInstance() ? Target->GetGameInstance()->GetSubsystem<UEFEquipmentSaveSubsystem>() : nullptr; }
}

void UEFEquipmentSaveLibrary::SaveGameWorld(UALSLoadAndSaveSubsystem* Target, const FString& slotName, const FOnSaveFinished& saveCallback,
    bool bSaveLocalPlayer, bool bSaveScreenshot, const FString& slotDescription)
{
    auto* Save = EFEquipmentSaveEntry::Resolve(Target);
    const bool bSuccess = Save && Save->Save(slotName, true, bSaveLocalPlayer, bSaveScreenshot, slotDescription);
    saveCallback.ExecuteIfBound(bSuccess);
}

void UEFEquipmentSaveLibrary::SaveGameWorldInCurrentSlot(UALSLoadAndSaveSubsystem* Target, const FOnSaveFinished& saveCallback,
    bool bSaveLocalPlayer, bool bSaveScreenshot, const FString& slotDescription)
{ SaveGameWorld(Target, GetCurrentSaveName(Target), saveCallback, bSaveLocalPlayer, bSaveScreenshot, slotDescription); }

bool UEFEquipmentSaveLibrary::SaveLocalPlayer(UALSLoadAndSaveSubsystem* Target, const FString& slotName)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save && Save->Save(slotName, false, true, false, TEXT("")); }

bool UEFEquipmentSaveLibrary::LoadLocalPlayer(UALSLoadAndSaveSubsystem* Target, const FString& slotName, bool bReloadTransform)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save && Save->LoadLocal(slotName, bReloadTransform); }

void UEFEquipmentSaveLibrary::LoadGameWorld(UALSLoadAndSaveSubsystem* Target, const FString& slotName, const FOnLoadFinished& loadCallback, bool reloadPlayer)
{ if (auto* Save = EFEquipmentSaveEntry::Resolve(Target)) Save->LoadWorld(slotName, loadCallback, reloadPlayer); else loadCallback.ExecuteIfBound(false); }

bool UEFEquipmentSaveLibrary::TravelLocalPlayer(UALSLoadAndSaveSubsystem* Target)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save && Save->CaptureTravel(); }

bool UEFEquipmentSaveLibrary::LoadTraveledPlayer(UALSLoadAndSaveSubsystem* Target)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save && Save->RestoreTravel(); }

FString UEFEquipmentSaveLibrary::GetCurrentSaveName(UALSLoadAndSaveSubsystem* Target)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save ? Save->GetActiveSlot() : FString(); }

FText UEFEquipmentSaveLibrary::GetLastSaveError(UALSLoadAndSaveSubsystem* Target)
{ auto* Save = EFEquipmentSaveEntry::Resolve(Target); return Save ? Save->GetLastError() : FText::FromString(TEXT("The equipment save system is unavailable.")); }

bool UEFEquipmentSaveLibrary::ReportSaveResult(UObject* WorldContextObject, bool bSuccess)
{
    if (!bSuccess) if (auto* Game = UGameplayStatics::GetGameInstance(WorldContextObject))
    {
        auto* Save = Game->GetSubsystem<UEFEquipmentSaveSubsystem>();
        FText Error = Save ? Save->GetLastError() : FText::GetEmpty();
        if (Error.IsEmpty()) Error = FText::FromString(TEXT("The save operation failed. The original save was retained."));
        if (auto* UI = Game->GetSubsystem<UANSUIPlayerSubsystem>()) UI->BroadcastNotification(Error, 10.f);
        UE_LOG(LogTemp, Warning, TEXT("EFEquipmentSave: %s"), *Error.ToString());
    }
    return bSuccess;
}
