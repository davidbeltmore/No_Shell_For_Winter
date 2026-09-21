#pragma once

#include "IDetailCustomization.h"

/**
 * Focused authoring surface for the definitive Calysto Dungeon Director V6.
 *
 * The customization adds guidance and diagnostics only. It never loads assets,
 * mutates the policy, or creates an alternate authoring document.
 */
class FEFCalystoDungeonDirectorPolicyV6Details final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

