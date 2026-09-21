#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EFCalystoPCGInternalClosureEditorLibrary.generated.h"

/** Narrow Python/Editor bridge for the create-once Calysto V6 PCG closure. */
UCLASS()
class EFPROCEDURALEDITOR_API UEFCalystoPCGInternalClosureEditorLibrary final
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Patches two fresh vendor duplicates after strict preflight validation.
	 * Returns an empty string on success. It never saves a package.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon|V6|Internal")
	static FString PrepareNewInternalCookedClosure();

	/**
	 * Loads and validates the two persisted internal graphs read-only.
	 * Returns an empty string on success.
	 */
	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Internal")
	static FString ValidateInternalCookedClosure();
};
