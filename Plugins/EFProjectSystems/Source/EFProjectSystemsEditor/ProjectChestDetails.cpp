#include "ProjectChestDetails.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "GameFramework/Actor.h"
#include "Storage/ProjectProceduralChestComponent.h"
#include "Widgets/Input/SButton.h"

TSharedRef<IDetailCustomization> FProjectChestDetails::MakeInstance()
{
    return MakeShared<FProjectChestDetails>();
}

void FProjectChestDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Selection;
    Builder.GetObjectsBeingCustomized(Selection);
    TArray<UObject*> Components;
    TArray<TWeakObjectPtr<UProjectProceduralChestComponent>> WeakComponents;
    for (const auto& Object : Selection)
    {
        auto* Actor = Cast<AActor>(Object.Get());
        // Blueprint defaults retain their existing component editor. This view
        // edits real placed instances, including multi-selection and subclasses.
        if (!Actor || Actor->IsTemplate()) return;
        auto* Component = Actor->FindComponentByClass<UProjectProceduralChestComponent>();
        if (!Component) return;
        Components.Add(Component);
        WeakComponents.Add(Component);
    }
    if (Components.IsEmpty()) return;

    auto& Category = Builder.EditCategory(TEXT("Procedural Chest"),
        NSLOCTEXT("ProjectChest", "Category", "Procedural Chest"), ECategoryPriority::Important);
    Category.InitiallyCollapsed(false);
    Category.SetSortOrder(1);
    // Native external property handles preserve transactions, reset-to-default,
    // edit conditions, multi-edit and component instance serialization.
    for (const FName Name : {GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, bProceduralMode),
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, ChestRarity),
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, Profile),
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, bOverrideContent),
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, ContentOverride),
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, Seed)})
    {
        Category.AddExternalObjectProperty(Components, Name);
    }
    Category.AddExternalObjectProperty(Components,
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, StableIdentity), EPropertyLocation::Advanced);
    Category.AddCustomRow(NSLOCTEXT("ProjectChest", "ValidateSearch", "Validate Configuration"))
        .ValueContent()
        [SNew(SButton)
            .Text(NSLOCTEXT("ProjectChest", "Validate", "Validate Configuration"))
            .OnClicked_Lambda([WeakComponents]()
            {
                for (const auto& Component : WeakComponents)
                    if (Component.IsValid()) Component->ValidateConfiguration();
                return FReply::Handled();
            })];
    Category.AddExternalObjectProperty(Components,
        GET_MEMBER_NAME_CHECKED(UProjectProceduralChestComponent, ValidationResult));
}
