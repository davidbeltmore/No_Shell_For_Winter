#pragma once

#include "IPropertyTypeCustomization.h"

/** Native property handles retain transactions/reset/search; only the internal reference gets a name picker. */
class FEFCalystoTraitBindingDetails final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Property, FDetailWidgetRow& Header,
		IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Property, IDetailChildrenBuilder& Children,
		IPropertyTypeCustomizationUtils& Utils) override;
};
