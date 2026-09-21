#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EFCalystoMaterialEditorLibrary.generated.h"

class UMaterialExpression;

/** Narrow read-only bridge for validating protected material graph inputs. */
UCLASS()
class EFPROCEDURALEDITOR_API UEFCalystoMaterialEditorLibrary final
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns the exact source expression path connected to InputIndex, or empty when disconnected. */
	UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V6|Materials")
	static FString GetMaterialExpressionInputSourcePath(
		UMaterialExpression* Expression,
		int32 InputIndex);
};
