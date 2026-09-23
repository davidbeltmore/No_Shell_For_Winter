#include "EFClothingSkeletonFingerprint.h"

#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/SecureHash.h"
#include "EFClothingFitProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
namespace
{
	// Explicit diagnostic only. At most 32 bindings and 32 unique meshes; never
	// save assets or print the skeleton payload to the game log.
	FAutoConsoleCommand IdentityAuditCommand(TEXT("EF.Clothing.IdentityAudit"),
		TEXT("Write bounded stored/calculated skeleton identities to Saved/ClothingIdentityAudit.txt."),
		FConsoleCommandDelegate::CreateLambda([]()
	{
		auto* Registry = LoadObject<UEFClothingFitRegistry>(nullptr,
			TEXT("/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry"));
		if (!Registry || Registry->NativeSourceBindings.Num() > 32) return;
		FString Report; TSet<const USkeletalMesh*> Meshes; TSet<const USkeleton*> Skeletons;
		for (const auto& Binding : Registry->NativeSourceBindings)
		{
			if (!Binding) continue;
			auto* Source = Binding->SourceGarment.LoadSynchronous();
			auto* Body = Binding->BodySurface.LoadSynchronous();
			if (!Source || !Body) continue;
			Report += FString::Printf(TEXT("Binding\t%s\tSource=%s/%s\tBody=%s/%s\tShared=%s/%s\n"),
				*Binding->GetPathName(), *Binding->SourceSkeletonFingerprint, *EFClothingSkeleton::BuildFingerprint(Source),
				*Binding->BodySkeletonFingerprint, *EFClothingSkeleton::BuildFingerprint(Body),
				*Binding->SharedSkeletonFingerprint, *EFClothingSkeleton::BuildSharedSkeletonFingerprint(Source->GetSkeleton()));
			Meshes.Add(Source); Meshes.Add(Body);
		}
		if (Meshes.Num() > 32) return;
		auto AppendBones = [&Report](const FString& Path, const FReferenceSkeleton& Ref, const USkeleton* Shared)
		{
			Report += FString::Printf(TEXT("Count\t%s\t%d\tRaw=%d\n"), *Path, Ref.GetNum(), Ref.GetRawBoneNum());
			for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
			{
				FString Bone = FString::Printf(TEXT("Bone\t%s\t%d\t%s\t%d\tRT=%d"), *Path, Index,
					*Ref.GetBoneName(Index).ToString(), Ref.GetParentIndex(Index),
					Shared ? static_cast<int32>(Shared->GetBoneTranslationRetargetingMode(Index)) : -1);
				AppendTransform(Bone, Ref.GetRefBonePose()[Index]); Report += Bone + TEXT("\n");
			}
		};
		for (const USkeletalMesh* Mesh : Meshes)
		{
			Report += FString::Printf(TEXT("Mesh\t%s\tSkeleton=%s\n"), *Mesh->GetPathName(), *GetPathNameSafe(Mesh->GetSkeleton()));
			AppendBones(Mesh->GetPathName(), Mesh->GetRefSkeleton(), nullptr); Skeletons.Add(Mesh->GetSkeleton());
		}
		for (const USkeleton* Skeleton : Skeletons)
		{
			if (!Skeleton) continue;
			Report += FString::Printf(TEXT("Shared\t%s\tGuid=%s\tVirtualBoneGuid=%s\tUseCompatibleRetarget=%d\n"),
				*Skeleton->GetPathName(), *Skeleton->GetGuid().ToString(EGuidFormats::Digits),
				*Skeleton->GetVirtualBoneGuid().ToString(EGuidFormats::Digits), Skeleton->GetUseRetargetModesFromCompatibleSkeleton());
			AppendBones(Skeleton->GetPathName(), Skeleton->GetReferenceSkeleton(), Skeleton);
			for (const auto& Bone : Skeleton->GetVirtualBones())
				Report += FString::Printf(TEXT("VB\t%s\t%s>%s:%s\n"), *Skeleton->GetPathName(), *Bone.SourceBoneName.ToString(), *Bone.TargetBoneName.ToString(), *Bone.VirtualBoneName.ToString());
			for (const auto& Compatible : Skeleton->GetCompatibleSkeletons())
				Report += FString::Printf(TEXT("CS\t%s\t%s\n"), *Skeleton->GetPathName(), *Compatible.ToSoftObjectPath().ToString());
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("ClothingIdentityAudit.txt");
		if (FFileHelper::SaveStringToFile(Report, *Path))
			UE_LOG(LogTemp, Display, TEXT("Clothing identity audit: %d bindings, %d meshes, %d shared skeletons. %s"), Registry->NativeSourceBindings.Num(), Meshes.Num(), Skeletons.Num(), *Path);
	}));
}
#endif

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
			*RefSkeleton.GetBoneName(BoneIndex).ToString().ToLower(),
			RefSkeleton.GetParentIndex(BoneIndex));
		AppendTransform(Canonical, RefSkeleton.GetRefBonePose()[BoneIndex]);
	}

	return FMD5::HashAnsiString(*Canonical);
}

FString EFClothingSkeleton::BuildSharedSkeletonFingerprint(const USkeleton* Skeleton)
{
	if (!IsValid(Skeleton))
	{
		return FString();
	}

	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	FString Canonical = FString::Printf(
		TEXT("Path=%s|Guid=%s|VirtualBoneGuid=%s|UseCompatibleRetarget=%d|Bones=%d"),
		*Skeleton->GetPathName().ToLower(),
		*Skeleton->GetGuid().ToString(EGuidFormats::Digits),
		*Skeleton->GetVirtualBoneGuid().ToString(EGuidFormats::Digits),
		Skeleton->GetUseRetargetModesFromCompatibleSkeleton() ? 1 : 0,
		Reference.GetNum());
	Canonical.Reserve(Reference.GetNum() * 160);
	for (int32 BoneIndex = 0; BoneIndex < Reference.GetNum(); ++BoneIndex)
	{
		Canonical += FString::Printf(
			TEXT("|B%d:%s:%d:RT%d"),
			BoneIndex,
			*Reference.GetBoneName(BoneIndex).ToString().ToLower(),
			Reference.GetParentIndex(BoneIndex),
			static_cast<int32>(Skeleton->GetBoneTranslationRetargetingMode(BoneIndex)));
		AppendTransform(Canonical, Reference.GetRefBonePose()[BoneIndex]);
	}

	for (const FVirtualBone& VirtualBone : Skeleton->GetVirtualBones())
	{
		Canonical += FString::Printf(
			TEXT("|VB=%s>%s:%s"),
			*VirtualBone.SourceBoneName.ToString().ToLower(),
			*VirtualBone.TargetBoneName.ToString().ToLower(),
			*VirtualBone.VirtualBoneName.ToString().ToLower());
	}

	TArray<FString> CompatibleSkeletonPaths;
	for (const TSoftObjectPtr<USkeleton>& CompatibleSkeleton : Skeleton->GetCompatibleSkeletons())
	{
		CompatibleSkeletonPaths.Add(CompatibleSkeleton.ToSoftObjectPath().ToString().ToLower());
	}
	CompatibleSkeletonPaths.Sort();
	for (const FString& CompatibleSkeletonPath : CompatibleSkeletonPaths)
	{
		Canonical += FString::Printf(TEXT("|CS=%s"), *CompatibleSkeletonPath);
	}

	return FMD5::HashAnsiString(*Canonical);
}

FString EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(const USkeleton* Skeleton)
{
	if (!IsValid(Skeleton))
	{
		return FString();
	}

	FString Canonical = TEXT("Core=") + BuildSharedSkeletonFingerprint(Skeleton);
#if WITH_EDITORONLY_DATA
	TArray<FName> RetargetSources;
	Skeleton->GetRetargetSources(RetargetSources);
	RetargetSources.Sort(FNameLexicalLess());
	for (FName RetargetSource : RetargetSources)
	{
		Canonical += FString::Printf(TEXT("|RS=%s"), *RetargetSource.ToString());
		for (const FTransform& PoseTransform : Skeleton->GetRefLocalPoses(RetargetSource))
		{
			AppendTransform(Canonical, PoseTransform);
		}
	}
#endif
	return FMD5::HashAnsiString(*Canonical);
}

FString EFClothingSkeleton::BuildContentFingerprint(const USkeletalMesh* Mesh)
{
	if (!IsValid(Mesh))
	{
		return FString();
	}

	FString Canonical = FString::Printf(
		TEXT("Path=%s|Skeleton=%s|LODs=%d|Morphs=%d|SkinProfiles=%d"),
		*Mesh->GetPathName(),
		*BuildFingerprint(Mesh),
		Mesh->GetLODNum(),
		Mesh->GetMorphTargets().Num(),
		Mesh->GetSkinWeightProfiles().Num());

#if WITH_EDITOR
	// The skeletal-mesh DDC key includes imported-model IDs, LOD build GUIDs,
	// build settings, morph render data and deformer-affecting mesh state.
	Canonical += TEXT("|DDC=");
	Canonical += const_cast<USkeletalMesh*>(Mesh)->GetDerivedDataKey();
#endif

	for (const TObjectPtr<UMorphTarget>& MorphTarget : Mesh->GetMorphTargets())
	{
		if (IsValid(MorphTarget))
		{
			Canonical += FString::Printf(
				TEXT("|M=%s:%d"),
				*MorphTarget->GetName(),
				MorphTarget->HasDataForLOD(0) ? MorphTarget->GetNumDeltasForLOD(0) : 0);
		}
	}

	return FMD5::HashAnsiString(*Canonical);
}

bool EFClothingSkeleton::AreBoneHierarchiesCompatible(
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
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}

bool EFClothingSkeleton::IsBoneHierarchySubsetCompatible(
	const USkeletalMesh* Subset,
	const USkeletalMesh* Superset,
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

	if (!IsValid(Subset) || !IsValid(Superset))
	{
		return Fail(TEXT("One or both skeletal meshes are invalid."));
	}
	if (Subset->GetSkeleton() != Superset->GetSkeleton())
	{
		return Fail(TEXT("The meshes do not reference the same USkeleton object."));
	}

	const FReferenceSkeleton& SubsetRef = Subset->GetRefSkeleton();
	const FReferenceSkeleton& SupersetRef = Superset->GetRefSkeleton();
	for (int32 SubsetIndex = 0; SubsetIndex < SubsetRef.GetNum(); ++SubsetIndex)
	{
		const FName BoneName = SubsetRef.GetBoneName(SubsetIndex);
		const int32 SupersetIndex = SupersetRef.FindBoneIndex(BoneName);
		if (SupersetIndex == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("Required bone %s is absent from the compatibility superset."), *BoneName.ToString()));
		}

		const int32 SubsetParentIndex = SubsetRef.GetParentIndex(SubsetIndex);
		const int32 SupersetParentIndex = SupersetRef.GetParentIndex(SupersetIndex);
		const FName SubsetParentName = SubsetParentIndex == INDEX_NONE
			? NAME_None
			: SubsetRef.GetBoneName(SubsetParentIndex);
		const FName SupersetParentName = SupersetParentIndex == INDEX_NONE
			? NAME_None
			: SupersetRef.GetBoneName(SupersetParentIndex);
		if (SubsetParentName != SupersetParentName)
		{
			return Fail(FString::Printf(
				TEXT("Parent relation differs for %s (%s vs %s)."),
				*BoneName.ToString(),
				*SubsetParentName.ToString(),
				*SupersetParentName.ToString()));
		}
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}

bool EFClothingSkeleton::AreSharedBoneHierarchiesCompatible(
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
	auto FindNearestSharedParent = [](const FReferenceSkeleton& From, int32 BoneIndex, const FReferenceSkeleton& Other) -> FName
	{
		for (int32 ParentIndex = From.GetParentIndex(BoneIndex);
			ParentIndex != INDEX_NONE;
			ParentIndex = From.GetParentIndex(ParentIndex))
		{
			const FName ParentName = From.GetBoneName(ParentIndex);
			if (Other.FindBoneIndex(ParentName) != INDEX_NONE)
			{
				return ParentName;
			}
		}
		return NAME_None;
	};

	int32 SharedBoneCount = 0;
	for (int32 BoneIndexA = 0; BoneIndexA < RefA.GetNum(); ++BoneIndexA)
	{
		const FName BoneName = RefA.GetBoneName(BoneIndexA);
		const int32 BoneIndexB = RefB.FindBoneIndex(BoneName);
		if (BoneIndexB == INDEX_NONE)
		{
			continue;
		}
		++SharedBoneCount;

		const FName SharedParentA = FindNearestSharedParent(RefA, BoneIndexA, RefB);
		const FName SharedParentB = FindNearestSharedParent(RefB, BoneIndexB, RefA);
		if (SharedParentA != SharedParentB)
		{
			return Fail(FString::Printf(
				TEXT("Shared hierarchy differs for %s (%s vs %s)."),
				*BoneName.ToString(),
				*SharedParentA.ToString(),
				*SharedParentB.ToString()));
		}
	}

	const int32 MinimumRequiredSharedBones = FMath::Max(32, FMath::Min(RefA.GetNum(), RefB.GetNum()) / 2);
	if (SharedBoneCount < MinimumRequiredSharedBones)
	{
		return Fail(FString::Printf(
			TEXT("Only %d bones are shared; at least %d are required."),
			SharedBoneCount,
			MinimumRequiredSharedBones));
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}

bool EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(
	const USkeletalMesh* A,
	const USkeletalMesh* B,
	FString* OutFailureReason)
{
	if (!AreBoneHierarchiesCompatible(A, B, OutFailureReason))
	{
		return false;
	}

	const FReferenceSkeleton& RefA = A->GetRefSkeleton();
	const FReferenceSkeleton& RefB = B->GetRefSkeleton();
	for (int32 BoneIndex = 0; BoneIndex < RefA.GetNum(); ++BoneIndex)
	{
		if (!RefA.GetRefBonePose()[BoneIndex].Equals(RefB.GetRefBonePose()[BoneIndex], 1.e-4f))
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Reference transform differs at index %d (%s)."),
					BoneIndex,
					*RefA.GetBoneName(BoneIndex).ToString());
			}
			return false;
		}
	}

	if (OutFailureReason)
	{
		OutFailureReason->Reset();
	}
	return true;
}
