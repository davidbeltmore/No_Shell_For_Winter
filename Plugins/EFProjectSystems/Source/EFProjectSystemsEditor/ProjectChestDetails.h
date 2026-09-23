#pragma once

#include "IDetailCustomization.h"

/** Presents the existing component properties on placed chest actors. */
class FProjectChestDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
};
