#include "EFEquipmentModalWidget.h"
#include "InputCoreTypes.h"

void UEFEquipmentModalWidget::NativeConstruct()
{
    // ACF owns the navigation stack; this project has no CommonUI default back
    // action data. Route back keys here instead of registering an empty action.
    bIsBackHandler = false; SetIsFocusable(true); Super::NativeConstruct();
}

FReply UEFEquipmentModalWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::BackSpace || Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
    { DeactivateWidget(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, Event);
}
