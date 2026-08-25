#include "EFClothingSkeletonFingerprint.h"

#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/SecureHash.h"

namespace
{
	void AppendTransform(FString& Text, const FTransform& Transform)
	{
		FQuat Rotation = Transform.GetRotation().GetNormalized();
		if (Rotation.W < 0.0)
		{
			Rotation.X *= -1.0;
			Rotation.Y *= -1.0;
			Rotation.Z *= -1.0;
			Rotation.W *= -1.0;
		}

		const FVector Translation = Transform.GetTranslation();
		const FVector Scale = Transform.GetScale3D();
		Text += FString::Printf(
			TEXT("|T%.9g,%.9g,%.9g|R%.9g,%.9g,%.9g,%.9g|S%.9g,%.9g,%.9g"),
			Translation.X, Translation.Y, Translation.Z,
			Rotation.X, Rotation.Y, Rotation.Z, Rotation.W,
			Scale.X, Scale.Y, Scale.Z);
	}
}

FString EFClothingSkeleton::BuildFingerprint(const USkeletalMesh* Mesh)
{
	if (!IsValid(Mesh))
	{
		return FString();
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	FString Canonical;
	Canonical.Reserve(RefSkeleton.GetNum() * 160);
	Canonical += FString::Printf(TEXT("Bones=%d"), RefSkeleton.GetNum());

	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		Canonical += FString::Printf(
			TEXT("|%d:%s:%d"),
			BoneIndex,
			*RefSkeleton.GetBoneName(BoneIndex).ToString(),
			RefSkeleton.GetParentIndex(BoneIndex));
		AppendTransform(Canonical, RefSkeleton.GetRefBonePose()[BoneIndex]);
	}

	return FMD5::HashAnsiString(*Canonical);
}

bool EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(
	const USkeletalMesh* A,
	const USkeletalMesh* B,
	FString* OutFailureReason)
{
	auto Fail = [OutFailureReason](const FString& Reason)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = Reason;
		}
		return false;
	};

	if (!IsValid(A) || !IsValid(B))
	{
		return Fail(TEXT("One or both skeletal meshes are invalid."));
	}

	if (A->GetSkeleton() != B->GetSkeleton())
	{
		return Fail(TEXT("The meshes do not reference the same USkeleton object."));
	}

	const FReferenceSkeleton& RefA = A->GetRefSkeleton();
	const FReferenceSkeleton& RefB = B->GetRefSkeleton();
	if (RefA.GetNum() != RefB.GetNum())
	{
		return Fail(FString::Printf(TEXT("Reference bone count differs: %d vs %d."), RefA.GetNum(), RefB.GetNum()));
	}

	for (int32 BoneIndex = 0; BoneIndex < RefA.GetNum(); ++BoneIndex)
	{
		if (RefA.GetBoneName(BoneIndex) != RefB.GetBoneName(BoneIndex))
		{
			return Fail(FString::Printf(TEXT("Bone name differs at index %d."), BoneIndex));
		}
		if (RefA.GetParentIndex(BoneIndex) != RefB.GetParentIndex(BoneIndex))
		{
			return Fail(FString::Printf(TEXT("Bone parent differs at index %d (%s)."), BoneIndex, *RefA.GetBoneName(BoneIndex).ToString()));
		}
		if (!RefA.GetRefBonePose()[BoneIndex].Equals(RefB.GetRefBonePose()[BoneIndex], 1.e-4f))
		{
			return Fail(FString::Printf(TEXT("Reference transform differs at index %d (%s)."), BoneIndex, *RefA.GetBoneName(BoneIndex).ToString()));
		}
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}
