#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectBlueprintMigrationLibrary.generated.h"

class UBlueprint;

/** Editor-only Blueprint migration helpers exposed to Unreal Python. */
UCLASS()
class EFPROJECTSYSTEMSEDITOR_API UProjectBlueprintMigrationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Restores the legacy TattooShop character contract after its parent was
	 * intentionally excluded from migration. The target parent and Blueprint
	 * interface are supplied by the caller so this editor module stays free of
	 * hard runtime asset dependencies. The operation snapshots the serialized
	 * Blueprint contract, compiles without saving, and only returns true when
	 * variables/defaults, graphs, interfaces, templates, and original SCS nodes
	 * survived intact.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Migration|TattooShop")
	static bool RepairTattooShopCharacterBlueprint(
		UBlueprint* Blueprint,
		UClass* TargetParentClass,
		UClass* CharacterCustomizationInterface);

	/** Equivalent to the Blueprint editor's Refresh All Nodes command. */
	UFUNCTION(BlueprintCallable, Category = "Project|Migration")
	static bool RefreshAllBlueprintNodes(UBlueprint* Blueprint);

	/** Reconstructs every K2 node without the structural refresh batching. */
	UFUNCTION(BlueprintCallable, Category = "Project|Migration")
	static bool ReconstructAllBlueprintNodes(UBlueprint* Blueprint);

	/** Refreshes and compiles in memory, deliberately performing no package save. */
	UFUNCTION(BlueprintCallable, Category = "Project|Migration")
	static bool RefreshAllBlueprintNodesAndCompileWithoutSaving(UBlueprint* Blueprint);
};
