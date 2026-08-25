#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

namespace EFClothingSkeleton
{
	EFCLOTHINGMORPHRUNTIME_API FString BuildFingerprint(const USkeletalMesh* Mesh);

	EFCLOTHINGMORPHRUNTIME_API bool AreReferenceSkeletonsStrictlyCompatible(
		const USkeletalMesh* A,
		const USkeletalMesh* B,
		FString* OutFailureReason = nullptr);
}
