#include "EFBodyOnboardingLibrary.h"
#include "Animation/Skeleton.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/PackageName.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
	FName ParentName(const FReferenceSkeleton& Ref, int32 Index)
	{
		const int32 Parent = Ref.GetParentIndex(Index);
		return Parent == INDEX_NONE ? NAME_None : Ref.GetBoneName(Parent);
	}
}
FString UEFBodyOnboardingLibrary::InspectBody(USkeletalMesh* Mesh, USkeleton* ProposedSkeleton)
{
	const auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mesh"), GetPathNameSafe(Mesh));
	if (Mesh)
	{
		Root->SetStringField(TEXT("assignedSkeleton"), GetPathNameSafe(Mesh->GetSkeleton()));
		Root->SetBoolField(TEXT("assignedCompatible"), Mesh->GetSkeleton() && Mesh->GetSkeleton()->IsCompatibleMesh(Mesh));
		Root->SetBoolField(TEXT("proposedCompatible"), ProposedSkeleton && ProposedSkeleton->IsCompatibleMesh(Mesh));
		Root->SetNumberField(TEXT("morphCount"), Mesh->GetMorphTargets().Num());
		Root->SetNumberField(TEXT("lodCount"), Mesh->GetLODNum());
		TArray<TSharedPtr<FJsonValue>> Geometry;
		if (const auto* Render = Mesh->GetResourceForRendering())
		{
			for (const auto& LOD : Render->LODRenderData)
			{
				const auto& Positions=LOD.StaticVertexBuffers.PositionVertexBuffer;
				const auto& Tangents=LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
				int32 InvalidPositions=0, ZeroNormals=0, ZeroTangents=0;
				for (uint32 I=0; I<Positions.GetNumVertices(); ++I)
				{
					if (Positions.VertexPosition(I).ContainsNaN()) ++InvalidPositions;
					if (FVector3f(Tangents.VertexTangentZ(I)).SizeSquared()<1.e-8f) ++ZeroNormals;
					if (FVector3f(Tangents.VertexTangentX(I)).SizeSquared()<1.e-8f) ++ZeroTangents;
				}
				const auto Item=MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("vertices"),Positions.GetNumVertices());
				Item->SetNumberField(TEXT("invalidPositions"),InvalidPositions);
				Item->SetNumberField(TEXT("zeroNormals"),ZeroNormals);
				Item->SetNumberField(TEXT("zeroTangents"),ZeroTangents);
				Geometry.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
		Root->SetArrayField(TEXT("renderGeometry"),Geometry);
		TSet<FName> Palette;
		if (const auto* Render = Mesh->GetResourceForRendering())
			for (const auto& LOD : Render->LODRenderData)
				for (const auto& Section : LOD.RenderSections)
					for (int32 Bone : Section.BoneMap)
						if (Bone >= 0 && Bone < Mesh->GetRefSkeleton().GetNum()) Palette.Add(Mesh->GetRefSkeleton().GetBoneName(Bone));
		TArray<TSharedPtr<FJsonValue>> SkinBones;
		for (FName Bone : Palette) SkinBones.Add(MakeShared<FJsonValueString>(Bone.ToString()));
		Root->SetArrayField(TEXT("skinPaletteBones"), SkinBones);
		TArray<TSharedPtr<FJsonValue>> Bones, Differences;
		const auto& Ref = Mesh->GetRefSkeleton();
		for (int32 I = 0; I < Ref.GetRawBoneNum(); ++I)
		{
			const auto Bone = MakeShared<FJsonObject>();
			Bone->SetStringField(TEXT("name"), Ref.GetBoneName(I).ToString());
			Bone->SetStringField(TEXT("parent"), ParentName(Ref, I).ToString());
			Bone->SetStringField(TEXT("pose"), Ref.GetRefBonePose()[I].ToString());
			Bones.Add(MakeShared<FJsonValueObject>(Bone));
			if (ProposedSkeleton)
			{
				const auto& Other = ProposedSkeleton->GetReferenceSkeleton();
				const int32 J = Other.FindBoneIndex(Ref.GetBoneName(I));
				if (J != INDEX_NONE) Bone->SetStringField(TEXT("skeletonPose"), Other.GetRefBonePose()[J].ToString());
				if (J != INDEX_NONE && ParentName(Ref, I) != ParentName(Other, J))
				{
					const auto Diff = MakeShared<FJsonObject>();
					Diff->SetStringField(TEXT("bone"), Ref.GetBoneName(I).ToString());
					Diff->SetStringField(TEXT("meshParent"), ParentName(Ref,I).ToString());
					Diff->SetStringField(TEXT("skeletonParent"), ParentName(Other,J).ToString());
					Differences.Add(MakeShared<FJsonValueObject>(Diff));
				}
			}
		}
		Root->SetArrayField(TEXT("bones"), Bones);
		Root->SetArrayField(TEXT("parentDifferences"), Differences);
	}
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

bool UEFBodyOnboardingLibrary::BindLocalSkeleton(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody, FString& Error)
{
	if (!Mesh || !LocalSkeleton || !ReferenceBody || !ReferenceBody->GetSkeleton())
	{ Error = TEXT("Missing mesh, local skeleton or reference body"); return false; }
	const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	if (!Folder.StartsWith(TEXT("/Game/DazToUnreal/")) || FPackageName::GetLongPackagePath(LocalSkeleton->GetOutermost()->GetName()) != Folder ||
		LocalSkeleton == ReferenceBody->GetSkeleton() || Mesh == ReferenceBody ||
		Folder==TEXT("/Game/DazToUnreal/Male") || Folder==TEXT("/Game/DazToUnreal/Female") || Folder==TEXT("/Game/DazToUnreal/Multiple"))
	{ Error = TEXT("Only a candidate's own imported skeleton may be modified"); return false; }
	if (!LocalSkeleton->IsCompatibleMesh(Mesh))
	{ Error = TEXT("Local skeleton is incompatible with candidate mesh; inspect before conversion"); return false; }
	const auto& Ref = Mesh->GetRefSkeleton();
	const auto& Established = ReferenceBody->GetRefSkeleton();
	const TArray<FName> CoreBones = {TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"),
		TEXT("head"), TEXT("neck_01"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("hand_l"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r")};
	for (const FName Bone : CoreBones)
	{
		const int32 I = Ref.FindBoneIndex(Bone), J = Established.FindBoneIndex(Bone);
		if (I == INDEX_NONE || J == INDEX_NONE || ParentName(Ref,I) != ParentName(Established,J))
		{ Error = FString::Printf(TEXT("Core hierarchy mismatch: %s"), *Bone.ToString()); return false; }
	}
	USkeleton* AnimationSkeleton = ReferenceBody->GetSkeleton();
	LocalSkeleton->Modify(); Mesh->Modify();
	// This converted mesh was authored to consume UEFN rotations directly. A new
	// skeleton with the mesh's DAZ bind pose introduces an extra cross-skeleton
	// rotation remap. Keep canonical animation reference frames on the common
	// hierarchy; the mesh retains its own proportions, bind pose and skin weights.
	{
		FReferenceSkeletonModifier Modifier(LocalSkeleton);
		const auto& Local = LocalSkeleton->GetReferenceSkeleton();
		const auto& Source = AnimationSkeleton->GetReferenceSkeleton();
		TSet<FName> Aligned;
		for (int32 I = 0; I < Local.GetRawBoneNum(); ++I)
		{
			const FName Name = Local.GetBoneName(I), Parent = ParentName(Local, I);
			const int32 J = Source.FindRawBoneIndex(Name);
			if (J != INDEX_NONE && Parent == ParentName(Source, J) && (Parent.IsNone() || Aligned.Contains(Parent)))
			{
				Modifier.UpdateRefPoseTransform(I, Source.GetRefBonePose()[J]);
				Aligned.Add(Name);
			}
		}
	}
	LocalSkeleton->PostEditChange();
	LocalSkeleton->AddCompatibleSkeleton(AnimationSkeleton);
	for (const auto& Group : AnimationSkeleton->GetSlotGroups())
		for (FName Slot : Group.SlotNames) LocalSkeleton->SetSlotGroupName(Slot, Group.GroupName);
	for (USkeletalMeshSocket* Socket : AnimationSkeleton->Sockets)
		if (Socket && Ref.FindBoneIndex(Socket->BoneName) != INDEX_NONE && !LocalSkeleton->FindSocket(Socket->SocketName))
			LocalSkeleton->Sockets.Add(DuplicateObject<USkeletalMeshSocket>(Socket, LocalSkeleton));
	for (const auto& Bone : AnimationSkeleton->GetVirtualBones())
		if (LocalSkeleton->GetReferenceSkeleton().FindBoneIndex(Bone.SourceBoneName) != INDEX_NONE &&
			LocalSkeleton->GetReferenceSkeleton().FindBoneIndex(Bone.TargetBoneName) != INDEX_NONE &&
			!LocalSkeleton->GetVirtualBones().ContainsByPredicate([&Bone](const auto& V) { return V.VirtualBoneName == Bone.VirtualBoneName; }))
			LocalSkeleton->AddNewNamedVirtualBone(Bone.SourceBoneName, Bone.TargetBoneName, Bone.VirtualBoneName);
	for (int32 I=0; I<LocalSkeleton->GetReferenceSkeleton().GetRawBoneNum(); ++I)
	{
		int32 J = AnimationSkeleton->GetReferenceSkeleton().FindBoneIndex(LocalSkeleton->GetReferenceSkeleton().GetBoneName(I));
		if (J != INDEX_NONE) LocalSkeleton->SetBoneTranslationRetargetingMode(I, AnimationSkeleton->GetBoneTranslationRetargetingMode(J), false);
	}
	Mesh->SetSkeleton(LocalSkeleton);
	Mesh->PostEditChange();
	LocalSkeleton->MarkPackageDirty(); Mesh->MarkPackageDirty();
	Error.Reset();
	return LocalSkeleton->IsCompatibleMesh(Mesh);
}
