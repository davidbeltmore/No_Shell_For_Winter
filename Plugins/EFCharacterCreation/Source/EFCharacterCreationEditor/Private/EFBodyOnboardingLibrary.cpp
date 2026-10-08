#include "EFBodyOnboardingLibrary.h"
#include "Animation/Skeleton.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/PackageName.h"

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
		LocalSkeleton == ReferenceBody->GetSkeleton() || Mesh == ReferenceBody)
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
