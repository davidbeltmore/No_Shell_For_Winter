#pragma once
#include "CoreMinimal.h"
#include "ANSNavWidget.h"
#include "GameplayTagContainer.h"
#include "Blueprint/DragDropOperation.h"
#include "CommonActivatableWidget.h"
#include "EFEquipmentNativeWidgets.generated.h"

class UEFEquipmentCoordinatorComponent;
class UPanelWidget;
class UACFEquipmentComponent;
class UButton;

UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentSatchelWindowWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeConstruct() override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> CloseButton;
    UFUNCTION() void CloseWindow();
};

UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentDragOperation : public UDragDropOperation
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UACFEquipmentComponent> Source;
    UPROPERTY(Transient) FGuid ItemGuid;
    UPROPERTY(Transient) int64 Revision = -1;
};

/** Retains the ACF item button Designer and selection dispatchers. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentInventoryIconWidget : public UANSNavWidget
{
    GENERATED_BODY()
protected:
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& Operation) override;
private:
    UPROPERTY(Transient) TObjectPtr<UACFEquipmentComponent> DragSource;
    FGuid DragGuid;
    int64 DragRevision = -1;
};

UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentNativeIconWidget : public UANSNavWidget
{
    GENERATED_BODY()
protected:
    virtual bool NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation) override;
};

/** Behavior for the original ACF equipment WBP; its Designer tree owns all visuals. */
UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentNativeLayoutWidget : public UANSNavWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Equipment") TArray<FGameplayTag> OuterwearSlots;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Equipment") TArray<FGameplayTag> AdditionalSlots;
    UPROPERTY(EditDefaultsOnly, Category="Equipment") TSubclassOf<UEFEquipmentSatchelWindowWidget> SatchelWindowClass;
protected:
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentCoordinatorComponent> Coordinator;
    UFUNCTION() void RefreshAdditionalSlots();
    UFUNCTION() void OpenSatchel();
    bool InitializeList(UPanelWidget* List, const TArray<FGameplayTag>& Slots);
};
