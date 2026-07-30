#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectNullParentBlueprintRepairLibrary.generated.h"

class UBlueprint;

/**
 * Editor-only, fail-closed repair helpers for migrated Blueprints whose parent
 * was serialized as null. None of these functions saves an asset package.
 */
UCLASS()
class EFPROJECTSYSTEMSEDITOR_API UProjectNullParentBlueprintRepairLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Migration|Blueprint Repair")
	static bool RepairInnerDoctrineAltarNullParent(UBlueprint* Blueprint);

	UFUNCTION(BlueprintCallable, Category = "Migration|Blueprint Repair")
	static bool RepairLockedWorldItemNullParent(UBlueprint* Blueprint);

	UFUNCTION(BlueprintPure, Category = "Migration|Blueprint Repair")
	static bool ValidateInnerDoctrineAltarRepair(UBlueprint* Blueprint);

	UFUNCTION(BlueprintPure, Category = "Migration|Blueprint Repair")
	static bool ValidateLockedWorldItemRepair(UBlueprint* Blueprint);

	/** Stable event-node/link descriptions suitable for before/after evidence. */
	UFUNCTION(BlueprintPure, Category = "Migration|Blueprint Repair")
	static TArray<FString> DescribeOverrideEventLinks(UBlueprint* Blueprint);
};
