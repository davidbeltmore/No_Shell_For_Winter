#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class USkeleton;

namespace EFClothingSkeleton
{
	EFCLOTHINGMORPHRUNTIME_API FString BuildFingerprint(const USkeletalMesh* Mesh);

	/** Cook-stable hierarchy, GUID, virtual-bone, compatibility and translation-retarget signature. */
	EFCLOTHINGMORPHRUNTIME_API FString BuildSharedSkeletonFingerprint(const USkeleton* Skeleton);

	/** Editor compile guard: cook-stable core plus editor-only retarget-source reference poses. */
	EFCLOTHINGMORPHRUNTIME_API FString BuildSharedSkeletonEditorFingerprint(const USkeleton* Skeleton);

	/**
	 * Editor/PIE content signature used to invalidate generated fits after a mesh reimport.
	 * In cooked builds the immutable cook is protected by the compiler/cook gates instead.
	 */
	EFCLOTHINGMORPHRUNTIME_API FString BuildContentFingerprint(const USkeletalMesh* Mesh);

	/** Same USkeleton object plus identical bone count, order, names and parents. */
	EFCLOTHINGMORPHRUNTIME_API bool AreBoneHierarchiesCompatible(
		const USkeletalMesh* A,
		const USkeletalMesh* B,
		FString* OutFailureReason = nullptr);

	/** Every bone and parent relation in Subset must exist in Superset; Superset may add branches. */
	EFCLOTHINGMORPHRUNTIME_API bool IsBoneHierarchySubsetCompatible(
		const USkeletalMesh* Subset,
		const USkeletalMesh* Superset,
		FString* OutFailureReason = nullptr);

	/** Compatible common hierarchy; each mesh may own independent deform-bone branches. */
	EFCLOTHINGMORPHRUNTIME_API bool AreSharedBoneHierarchiesCompatible(
		const USkeletalMesh* A,
		const USkeletalMesh* B,
		FString* OutFailureReason = nullptr);

	EFCLOTHINGMORPHRUNTIME_API bool AreReferenceSkeletonsStrictlyCompatible(
		const USkeletalMesh* A,
		const USkeletalMesh* B,
		FString* OutFailureReason = nullptr);
}
