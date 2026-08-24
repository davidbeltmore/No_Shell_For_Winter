#include "Calysto/EFCalystoCategoryProfileV4Customization.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV4.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

TSharedRef<IPropertyTypeCustomization>
FEFCalystoCategoryProfileV4Customization::MakeInstance()
{
	return MakeShared<FEFCalystoCategoryProfileV4Customization>();
}

void FEFCalystoCategoryProfileV4Customization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	(void)CustomizationUtils;
	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		StructPropertyHandle->CreatePropertyValueWidget()
	];
}

void FEFCalystoCategoryProfileV4Customization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	(void)CustomizationUtils;
	const bool bThemeOwnsEnabled = IsNestedUnderThemes(StructPropertyHandle);
	uint32 ChildCount = 0;
	StructPropertyHandle->GetNumChildren(ChildCount);
	for (uint32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		const TSharedPtr<IPropertyHandle> Child =
			StructPropertyHandle->GetChildHandle(ChildIndex);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}
		if (!bThemeOwnsEnabled
			&& Child->GetProperty()->GetFName()
				== GET_MEMBER_NAME_CHECKED(FEFCalystoCategoryProfileV4, bEnabled))
		{
			continue;
		}
		ChildBuilder.AddProperty(Child.ToSharedRef());
	}
}

bool FEFCalystoCategoryProfileV4Customization::IsNestedUnderThemes(
	TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	TSharedPtr<IPropertyHandle> Current = StructPropertyHandle;
	for (int32 Depth = 0; Current.IsValid() && Depth < 16; ++Depth)
	{
		if (const FProperty* Property = Current->GetProperty())
		{
			if (Property->GetFName()
				== GET_MEMBER_NAME_CHECKED(UEFCalystoDungeonDirectorPolicyV4, Themes))
			{
				return true;
			}
			if (Property->GetFName()
				== GET_MEMBER_NAME_CHECKED(UEFCalystoDungeonDirectorPolicyV4, Styles))
			{
				return false;
			}
		}
		Current = Current->GetParentHandle();
	}

	// If ancestry cannot be proven, hide the gate. Exposing another activation
	// authority would be less safe than requiring the canonical Theme surface.
	return false;
}
