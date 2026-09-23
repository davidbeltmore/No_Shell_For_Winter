#pragma once
#include "CoreMinimal.h"
#include "ANSNavPageWidget.h"
#include "Components/ComboBoxString.h"
#include "EFEquipmentStoragePage.generated.h"

class UACFVendorComponent;
class UEFEquipmentCoordinatorComponent;
class UComboBoxString;
class UTextBlock;

UENUM(BlueprintType)
enum class EEFStorageContext : uint8 { Deposit, Satchel, Merchant };

/** Native ACF Designer trees provide the visuals; this page identifies the operation. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentStoragePage : public UANSNavPageWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Storage") EEFStorageContext StorageContext = EEFStorageContext::Deposit;
    UFUNCTION(BlueprintPure, Category="Storage") UACFVendorComponent* GetBagInventory();
    static UEFEquipmentStoragePage* FindContext(const UObject* Widget);
    FGuid GetBagGuid() const { return BagGuid; }
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> BagSelector;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BagWeight;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StorageMessage;
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentCoordinatorComponent> Coordinator;
    TArray<FGuid> Bags;
    FGuid BagGuid;
    bool bRefreshing = false;
    UFUNCTION() void RefreshStorage();
    UFUNCTION() void SelectBag(FString Selected, ESelectInfo::Type Selection);
};
