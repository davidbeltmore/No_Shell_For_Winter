#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFClothingNativeMeshAuthoringLibrary.generated.h"

class UEFClothingMorphDirectorPolicy;

/**
 * Explicit editor actions for the exact Skeletal Mesh referenced by a V3
 * Director garment entry. These functions never run during equip, PIE tick or
 * cook and never create a replacement garment asset.
 */
UCLASS()
class EFCLOTHINGMORPHEDITOR_API UEFClothingNativeMeshAuthoringLibrary final
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Opens the authoritative garment in Unreal's native Skeletal Mesh Editor. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Native Authoring")
	static bool OpenEditableMesh(
		UEFClothingMorphDirectorPolicy* Director,
		FName GarmentId,
		FString& OutReport);

	/**
	 * Applies the row's Native UE Offset settings to LOD0 of the exact editable
	 * source mesh. The operation is transactional and intentionally does not save.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Native Authoring")
	static bool ApplyNativeOffsetToEditableMesh(
		UEFClothingMorphDirectorPolicy* Director,
		FName GarmentId,
		FString& OutReport);

	/**
	 * Explicit topology-changing action. It is refused when morph targets or
	 * Chaos clothing data are present; those assets must be thickened manually in
	 * Unreal so their dependent data can be reviewed and repaired deliberately.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V3|Native Authoring")
	static bool CreateShellOnEditableMesh(
		UEFClothingMorphDirectorPolicy* Director,
		FName GarmentId,
		FString& OutReport);
};
