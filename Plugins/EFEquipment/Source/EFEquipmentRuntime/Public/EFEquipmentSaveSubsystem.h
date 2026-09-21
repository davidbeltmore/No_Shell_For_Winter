#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ALSLoadAndSaveSubsystem.h"
#include "EFEquipmentSaveSubsystem.generated.h"

class UEFEquipmentSaveGame;

/** Project composition around ALS. Native records remain the storage format. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentSaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    bool Save(const FString& Slot, bool bWorld, bool bLocalPlayer, bool bScreenshot, const FString& Description);
    bool LoadLocal(const FString& Slot, bool bTransform);
    void LoadWorld(const FString& Slot, const FOnLoadFinished& Callback, bool bPlayer);
    bool CaptureTravel();
    bool RestoreTravel();
    FString GetActiveSlot() const;
    FText GetLastError() const { return FText::FromString(LastError); }
    UFUNCTION(BlueprintPure, Category="Equipment|Save") FText GetLastMigrationSummary() const { return LastMigrationSummary; }
    bool IsBusy() const { return bBusy; }
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentSaveGame> TravelingPlayer;
    UPROPERTY(Transient) FOnLoadFinished PendingLoadCallback;
    FString ActiveSlot;
    FString PendingSlot;
    FString LastError;
    FText LastMigrationSummary;
    bool bConvertedOnLoad = false;
    bool bBusy = false;
    bool bReloadPlayer = true;
    UALSLoadAndSaveSubsystem* Native() const;
    UEFEquipmentSaveGame* PrepareLoad(const FString& Slot);
    bool RestoreLocalState(UEFEquipmentSaveGame* Loaded, bool bTransform);
    void PublishMigrationSummary(UEFEquipmentSaveGame* Loaded);
    UFUNCTION() void CompleteNativeLoad(bool bSuccess);
};
