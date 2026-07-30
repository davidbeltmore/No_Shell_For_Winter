#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectCalystoDataRepairLibrary.generated.h"

class UUserDefinedStruct;

/** Exact, editor-only repair helpers for migrated Calysto data contracts. */
UCLASS()
class EFPROJECTSYSTEMSEDITOR_API UProjectCalystoDataRepairLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Repairs the migrated ST_SmartScatter.Data member after its old Calysto
	 * object class/default paths resolve as an invalid property in UE 5.8.
	 * The caller remains responsible for saving the struct and its dependent BP.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Migration|Calysto")
	static bool RepairSmartScatterDataMember(
		UUserDefinedStruct* SmartScatterStruct,
		UClass* ScatterDataClass,
		UObject* ScatterDefaultObject,
		UEnum* ScatterModeEnum,
		FString& OutDetails);
};
