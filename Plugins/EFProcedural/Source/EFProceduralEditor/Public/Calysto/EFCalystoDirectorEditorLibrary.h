#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFCalystoDirectorEditorLibrary.generated.h"

/** Editor-only inspection. No loading of soft references and no package modification. */
UCLASS()
class EFPROCEDURALEDITOR_API UEFCalystoDirectorEditorLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Complete text export, including defaults inside nested arrays. Unsupported assets return an empty string. */
	UFUNCTION(BlueprintCallable, Category = "Calysto|Inspector")
	static FString ExportAuthoredField(UObject* Asset, FName Field);
};
