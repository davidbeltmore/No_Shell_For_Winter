#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/ACFEquipmentComponent.h"
#include "EFEquipmentTypes.h"
#include "EFEquipmentCoordinatorComponent.generated.h"

class UEFEquipmentSlotRegistry;
class UEFEquipmentRuleProfile;
class UEFEquipmentRulesFragment;
class UEFContainerInventoryComponent;
class UACFVendorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEFEquipmentChanged);

UCLASS(BlueprintType, ClassGroup=(Equipment), meta=(BlueprintSpawnableComponent))
class EFEQUIPMENTRUNTIME_API UEFEquipmentCoordinatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UEFEquipmentCoordinatorComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="Equipment") bool InitializeEquipment(UEFEquipmentSlotRegistry* RegistryOverride = nullptr);
    UFUNCTION(BlueprintCallable, Category="Equipment") FEFEquipmentResult PreviewEquip(FGuid ItemGuid, FGameplayTag Destination);
    UFUNCTION(BlueprintCallable, Category="Equipment") FEFEquipmentResult RequestEquip(FGuid ItemGuid, FGameplayTag Destination, int64 ExpectedRevision = -1);
    UFUNCTION(BlueprintCallable, Category="Equipment") FEFEquipmentResult RequestUnequip(FGuid ItemGuid);
    UFUNCTION(BlueprintCallable, Category="Equipment") FEFEquipmentResult RequestDrop(FGuid ItemGuid, int32 Count = 1);
    UFUNCTION(BlueprintPure, Category="Equipment") TArray<FEFEquipmentSlotView> GetSlotViews() const;
    UFUNCTION(BlueprintPure, Category="Equipment") FEFEquipmentWeightSummary GetWeightSummary() const;
    UFUNCTION(BlueprintPure, Category="Equipment") UACFEquipmentComponent* GetNativeEquipment() const { return Equipment; }
    UFUNCTION(BlueprintPure, Category="Equipment") int64 GetRevision() const { return Revision; }
    UFUNCTION(BlueprintPure, Category="Equipment") FText GetLastMessage() const { return LastMessage; }
    UFUNCTION(BlueprintCallable, Category="Equipment") bool SetBaseCarryCapacity(double Capacity);
    UFUNCTION(BlueprintCallable, Category="Equipment|Containers") TArray<FInventoryItem> GetContainerItems(FGuid ContainerGuid);
    UFUNCTION(BlueprintCallable, Category="Equipment|Containers") FEFEquipmentResult MoveToContainer(FGuid ItemGuid, FGuid ContainerGuid, int32 Count);
    UFUNCTION(BlueprintCallable, Category="Equipment|Containers") FEFEquipmentResult MoveFromContainer(FGuid ContainerGuid, FGuid ItemGuid, int32 Count);
    UFUNCTION(BlueprintCallable, Category="Equipment") FEFEquipmentResult TakeFromInventory(UACFInventoryComponent* Source, FGuid ItemGuid, int32 Count, bool bAutoEquip = true);
    UFUNCTION(BlueprintCallable, Category="Equipment|Trade") FEFEquipmentSaleQuote PreviewSale(UACFVendorComponent* Vendor, FGuid ItemGuid, int32 Count = 1);
    UFUNCTION(BlueprintCallable, Category="Equipment|Trade") FEFEquipmentResult ConfirmSale(FGuid QuoteId);
    UFUNCTION(BlueprintCallable, Category="Equipment|Trade") void CancelSale();
    UFUNCTION(BlueprintCallable, Category="Equipment|Migration") FEFEquipmentResult RestoreEquippedIntent(const TArray<FInventoryItem>& Intent);
    UFUNCTION(BlueprintCallable, Category="Equipment") void RefreshState();
    /** Integration hook after native ACF inventory bytes have been restored. */
    UFUNCTION(BlueprintCallable, Category="Equipment|Migration") void HandleActorLoaded();
    UPROPERTY(BlueprintAssignable, Category="Equipment") FEFEquipmentChanged OnStateChanged;
    UPROPERTY(SaveGame) double BaseCarryCapacity = -1;
    UPROPERTY(SaveGame) int32 EquipmentSchema = 0;

    /** Called by common native item parents before ACF applies any new effects. */
    static bool NativeEquipGate(UACFItem* Item, UACFEquipmentComponent* Native);
    static bool IsManagedPlayer(const AActor* Actor);
    static UEFEquipmentCoordinatorComponent* FindOrCreate(AActor* Actor);
    bool IsAuthorized(const UACFItem* Item) const;
    UEFContainerInventoryComponent* ResolveContainer(FGuid Guid, FString& Error);
    bool FlushContainers(FString& Error);
    double GetEffectiveContainerWeight() const;
    bool IsBusy() const { return bOperation; }
#if WITH_DEV_AUTOMATION_TESTS
    // One-shot fault injection: 1 after displacement, 2 after native effects, 3 after sale settlement.
    int32 TestFailurePoint = 0;
#endif

private:
    UPROPERTY(Transient) TObjectPtr<UACFEquipmentComponent> Equipment;
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentSlotRegistry> Registry;
    UPROPERTY(Transient) TMap<FGuid, TObjectPtr<UEFContainerInventoryComponent>> Containers;
    UPROPERTY(Transient) TObjectPtr<UACFItem> AuthorizedItem;
    UPROPERTY(Transient) FText LastMessage;
    UPROPERTY(Transient) FEFEquipmentSaleQuote PendingSale;
    UPROPERTY(Transient) TWeakObjectPtr<UACFVendorComponent> SaleVendor;
    FString SaleSignature;
    int64 Revision = 0;
    uint32 StateFingerprint = 0;
    bool bOperation = false;
    bool bRefreshing = false;
    bool bCheckingRequirements = false;
    bool bInitialized = false;
    bool bInitialItemsHandled = false;
    FTimerHandle DiscoveryTimer;
    UFUNCTION() void HandleInventoryChanged();
    UFUNCTION() void HandleEquipmentChanged(const FEquipment& Current);
    void PollEquipment();
    TArray<FGameplayTag> Occupancy(const UACFItem* Item, FGameplayTag Primary) const;
    bool VerifyEquipped(FGuid Guid, FGameplayTag Slot) const;
    FEFEquipmentResult Publish(FEFEquipmentResult Result);
    bool BuildSale(UACFVendorComponent* Vendor, FGuid Guid, int32 Count, FEFEquipmentSaleQuote& Quote, FString& Signature, FString& Error);
};
