#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EFCalystoDungeonDirectorPolicyV6AssetFactory.generated.h"

/** Creates the one project-owned Calysto V6 Primary Data Asset. */
UCLASS()
class UEFCalystoDungeonDirectorPolicyV6AssetFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UEFCalystoDungeonDirectorPolicyV6AssetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent,
		FName InName, EObjectFlags Flags, UObject* Context,
		FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual bool ShouldShowInNewMenu() const override { return false; }
};
