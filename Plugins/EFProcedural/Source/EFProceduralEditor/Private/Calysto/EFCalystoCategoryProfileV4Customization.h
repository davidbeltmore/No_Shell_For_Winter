#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailChildrenBuilder;
class IPropertyHandle;
class IPropertyTypeCustomizationUtils;
class FDetailWidgetRow;

/**
 * Keeps V4 category activation Theme-owned on the authoring surface.
 *
 * FEFCalystoCategoryProfileV4 is shared by Style and Theme arrays for schema
 * compatibility. The runtime ignores the legacy Style bEnabled value, and this
 * customization hides that duplicate field unless the category is nested below
 * UEFCalystoDungeonDirectorPolicyV4::Themes.
 */
class FEFCalystoCategoryProfileV4Customization final
	: public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	static bool IsNestedUnderThemes(
		TSharedRef<IPropertyHandle> StructPropertyHandle);
};
