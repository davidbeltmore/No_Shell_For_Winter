#pragma once
#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "EFEquipmentModalWidget.generated.h"

/** Shared keyboard/back behavior for project dialogs in ACF's native UI stack. */
UCLASS(Abstract, Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentModalWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeConstruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
};
