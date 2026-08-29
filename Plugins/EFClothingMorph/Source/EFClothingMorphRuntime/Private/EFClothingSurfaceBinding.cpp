#include "EFClothingSurfaceBinding.h"

const FEFClothingSurfaceLODPairBinding* UEFClothingSurfaceBinding::FindLODPair(
	const int32 GarmentLODIndex,
	const int32 BodyLODIndex) const
{
	return LODPairBindings.FindByPredicate(
		[GarmentLODIndex, BodyLODIndex](const FEFClothingSurfaceLODPairBinding& Pair)
		{
			return Pair.GarmentTopology.LODIndex == GarmentLODIndex
				&& Pair.BodyTopology.LODIndex == BodyLODIndex;
		});
}
