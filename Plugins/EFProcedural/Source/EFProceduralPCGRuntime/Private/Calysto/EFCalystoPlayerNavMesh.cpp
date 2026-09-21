#include "Calysto/EFCalystoPlayerNavMesh.h"

AEFCalystoPlayerNavMesh::AEFCalystoPlayerNavMesh(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The 100 cm player capsule physically clears native doorways that the
	// 19 cm raster can erase after voxelization and radius erosion. Configure
	// before generator initialization; changing a live mesh leaves cached cs stale.
	SetCellSize(ENavigationDataResolution::Low, 20.0f);
	SetCellSize(ENavigationDataResolution::Default, 10.0f);
	SetCellSize(ENavigationDataResolution::High, 10.0f);
}
