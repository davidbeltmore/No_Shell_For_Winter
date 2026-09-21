#pragma once

#include "CoreMinimal.h"
#include "EFClothingMorphV3RuntimeComponent.h"
#include "EFClothingRuntimeComponent.generated.h"

/**
 * Version-neutral V5 entry point for the proven EF Clothing Morph runtime.
 *
 * The implementation intentionally remains in the serialized V3-named base
 * class so existing Blueprint components and saves keep their exact class
 * contract. New integrations should depend on this canonical type.
 */
UCLASS(ClassGroup = (EF), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "EF Clothing Runtime"))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingRuntimeComponent final
	: public UEFClothingMorphV3RuntimeComponent
{
	GENERATED_BODY()
};
