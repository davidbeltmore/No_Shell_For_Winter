#pragma once
#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "EFEquipmentTypes.h"
#include "EFEquipmentSaleWidget.generated.h"

class UEFEquipmentCoordinatorComponent;
class UButton;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentSaleWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UEFEquipmentSaleWidget();
    void SetQuote(UEFEquipmentCoordinatorComponent* Owner, const FEFEquipmentSaleQuote& Quote);
protected:
    virtual void NativeConstruct() override;
    virtual void NativeOnDeactivated() override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BreakdownLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CancelButton;
    UFUNCTION() void Confirm();
    UFUNCTION() void Cancel();
private:
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentCoordinatorComponent> Coordinator;
    FGuid QuoteId;
};
