#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "EFEquipmentTypes.h"
#include "EFEquipmentWidgets.generated.h"

class UButton;
class UTextBlock;
class UGridPanel;
class UPanelWidget;
class USpinBox;
class UEFEquipmentCoordinatorComponent;
class UEFEquipmentPanelWidget;

/** All visible layout/style lives in the Widget Blueprint. Native code binds data only. */
UCLASS(Abstract, Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentSlotWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Equipment") void SetSlotView(UEFEquipmentPanelWidget* Panel, const FEFEquipmentSlotView& View);
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FEFEquipmentSlotView SlotView;
protected:
    virtual void NativeConstruct() override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SlotLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> OccupantLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> SelectButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> UnequipButton;
    UFUNCTION() void SelectOwner();
    UFUNCTION() void UnequipOwner();
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentPanelWidget> OwnerPanel;
};

UCLASS(Abstract, Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentTransferRowWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(UEFEquipmentPanelWidget* Panel, FGuid Guid, const FText& Name, int32 Count, bool bExtract);
protected:
    virtual void NativeConstruct() override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ItemLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ActionLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> TransferButton;
    UFUNCTION() void Transfer();
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentPanelWidget> OwnerPanel;
    FGuid ItemGuid;
    int32 AvailableCount = 0;
    bool bExtractItem = false;
};

UCLASS(Abstract, Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentPanelWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Equipment") void RefreshEquipment();
    UFUNCTION(BlueprintCallable, Category="Equipment") void SelectItem(FGuid Guid);
    UFUNCTION(BlueprintCallable, Category="Equipment") void UnequipItem(FGuid Guid);
    UFUNCTION(BlueprintPure, Category="Equipment") FGuid GetSelectedItem() const { return SelectedItem; }
    UFUNCTION(BlueprintImplementableEvent, Category="Equipment") void OnItemSelected(FGuid Guid);
    void TransferItem(FGuid Guid, int32 AvailableCount, bool bExtract);
    UPROPERTY(EditDefaultsOnly, Category="Equipment") TSubclassOf<UEFEquipmentSlotWidget> SlotWidgetClass;
    UPROPERTY(EditDefaultsOnly, Category="Equipment") TMap<FGameplayTag, TSubclassOf<UEFEquipmentSlotWidget>> SlotWidgetOverrides;
    UPROPERTY(EditDefaultsOnly, Category="Equipment") TSubclassOf<UEFEquipmentTransferRowWidget> TransferRowClass;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UGridPanel> SlotGrid;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> WeightLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MessageLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ContainerLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UComboBoxString> ContainerPicker;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> ContainerRows;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> InventoryRows;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USpinBox> TransferQuantity;
    UFUNCTION() void SelectContainer(FString Selected, ESelectInfo::Type Type);
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentCoordinatorComponent> Coordinator;
    UPROPERTY(Transient) TMap<FGameplayTag, TObjectPtr<UEFEquipmentSlotWidget>> SlotWidgets;
    TMap<FString, FGuid> ContainerOptions;
    FGuid SelectedItem;
    FGuid SelectedContainer;
    bool bRefreshing = false;
    FTimerHandle BindTimer;
    void BindPlayer();
};
