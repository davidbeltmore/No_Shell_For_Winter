#include "EFEquipmentAuthoringLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"

UWidget* UEFEquipmentAuthoringLibrary::FindInventoryWidget(UUserWidget* Root, FName Name)
{
    return Root && Root->WidgetTree ? Root->WidgetTree->FindWidget(Name) : nullptr;
}

bool UEFEquipmentAuthoringLibrary::SendInventoryPointer(UWidget* Target, FName Action, float WheelDelta, FVector2D LocalOffset)
{
    if (!Target || !Target->GetWorld() || Target->GetWorld()->WorldType != EWorldType::PIE || !FSlateApplication::IsInitialized()) return false;
    const auto Widget = Target->GetCachedWidget(); if (!Widget) return false;
    FWidgetPath Path; auto& Slate = FSlateApplication::Get();
    if (!Slate.GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path)) return false;
    // GeneratePathToWidget builds layout-only paths. Routed pointer events also
    // require one optional virtual pointer position per widget (UE 5.8).
    TArray<FWidgetAndPointer> PointerWidgets;
    for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        PointerWidgets.Emplace(Path.Widgets[Index], TOptional<FVirtualPointerPosition>());
    Path = FWidgetPath(MakeArrayView(PointerWidgets));
    const auto Geometry = Path.Widgets.Last().Geometry;
    const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f + LocalOffset);
    static FVector2D Previous;
    TSet<FKey> Buttons;
    if (Action == TEXT("Down") || Action == TEXT("Drag")) Buttons.Add(EKeys::LeftMouseButton);
    const FPointerEvent Event(0, Position, Previous, Buttons,
        Action == TEXT("Down") || Action == TEXT("Up") ? EKeys::LeftMouseButton : FKey(), WheelDelta, FModifierKeysState());
    Previous = Position;
    if (Action == TEXT("Wheel")) return Slate.RouteMouseWheelOrGestureEvent(Path, Event).IsEventHandled();
    if (Action == TEXT("Down"))
    {
        // A real click first enters the widget. SButton requires hover on release;
        // routing only down/up can press it without ever producing OnClicked.
        const FPointerEvent Hover(0, Position, Previous, TSet<FKey>(), FKey(), 0, FModifierKeysState());
        Slate.RoutePointerMoveEvent(Path, Hover, false);
        return Slate.RoutePointerDownEvent(Path, Event).IsEventHandled();
    }
    if (Action == TEXT("Drag")) return Slate.RoutePointerMoveEvent(Path, Event, false);
    if (Action == TEXT("Up")) return Slate.RoutePointerUpEvent(Path, Event).IsEventHandled();
    return false;
}
