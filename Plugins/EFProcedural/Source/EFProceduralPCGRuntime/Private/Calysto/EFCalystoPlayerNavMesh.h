#pragma once

#include "NavMesh/RecastNavMesh.h"
#include "EFCalystoPlayerNavMesh.generated.h"

/** Native Recast with sufficient raster precision for the actual player capsule. */
UCLASS(NotBlueprintable, Config=Engine)
class AEFCalystoPlayerNavMesh final : public ARecastNavMesh
{
	GENERATED_BODY()

public:
	AEFCalystoPlayerNavMesh(const FObjectInitializer& ObjectInitializer);
};
