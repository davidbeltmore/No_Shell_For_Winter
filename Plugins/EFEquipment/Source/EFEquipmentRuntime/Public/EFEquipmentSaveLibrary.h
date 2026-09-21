#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "EFEquipmentSaveLibrary.generated.h"

/** Blueprint-compatible save entry points; Target retains the native subsystem pin. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentSaveLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static void SaveGameWorld(UALSLoadAndSaveSubsystem* Target, const FString& slotName,
        const FOnSaveFinished& saveCallback, bool bSaveLocalPlayer = true, bool bSaveScreenshot = true, const FString& slotDescription = TEXT(""));
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static void SaveGameWorldInCurrentSlot(UALSLoadAndSaveSubsystem* Target,
        const FOnSaveFinished& saveCallback, bool bSaveLocalPlayer = true, bool bSaveScreenshot = true, const FString& slotDescription = TEXT(""));
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static bool SaveLocalPlayer(UALSLoadAndSaveSubsystem* Target, const FString& slotName);
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static bool LoadLocalPlayer(UALSLoadAndSaveSubsystem* Target, const FString& slotName, bool bReloadTransform);
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static void LoadGameWorld(UALSLoadAndSaveSubsystem* Target, const FString& slotName, const FOnLoadFinished& loadCallback, bool reloadPlayer = true);
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static bool TravelLocalPlayer(UALSLoadAndSaveSubsystem* Target);
    UFUNCTION(BlueprintCallable, Category="Equipment|Save") static bool LoadTraveledPlayer(UALSLoadAndSaveSubsystem* Target);
    UFUNCTION(BlueprintPure, Category="Equipment|Save") static FString GetCurrentSaveName(UALSLoadAndSaveSubsystem* Target);
    UFUNCTION(BlueprintPure, Category="Equipment|Save") static FText GetLastSaveError(UALSLoadAndSaveSubsystem* Target);
    UFUNCTION(BlueprintCallable, Category="Equipment|Save", meta=(WorldContext="WorldContextObject"))
    static bool ReportSaveResult(UObject* WorldContextObject, bool bSuccess);
};
