#include "EFBodyOnboardingLibrary.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "SkeletonModifier.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace
{
// Same operations as the installed converter; all mutations stay on the modifier
// until its result passes preflight. The generated recipe is versioned with source.
void SetBoneOrientation(USkeletonModifier* M, FName Bone, FQuat Rotation)
{
	FTransform T = M->GetBoneTransform(Bone); T.SetRotation(Rotation);
	M->SetBoneTransform(Bone, T, false);
}
void AdditiveBoneOrientation(USkeletonModifier* M, FName Bone, FQuat Rotation)
{
	FTransform T = M->GetBoneTransform(Bone); T.ConcatenateRotation(Rotation);
	M->SetBoneTransform(Bone, T, false);
}
void AlignBone(USkeletonModifier* M, FName Parent, FName Child, FVector Axis)
{
	const FVector P = M->GetBoneTransform(Child, false).GetLocation();
	const double Yaw = FMath::IsNearlyEqual(FMath::Abs(Axis.Z), 1.) ? FMath::RadiansToDegrees(FMath::Atan(P.Y/P.X))*Axis.Z : 0.;
	const double Pitch = FMath::IsNearlyEqual(FMath::Abs(Axis.Y), 1.) ? FMath::RadiansToDegrees(FMath::Atan(P.Z/P.X))*Axis.Y : 0.;
	AdditiveBoneOrientation(M, Parent, FQuat(FRotator(Pitch*Axis.Y, Yaw*Axis.Z, 0.)));
}
#include "EFGenesis9ConversionRecipe.inl"

FString MorphDigest(USkeletalMesh* Mesh)
{
	FMD5 Hash;
	TArray<UMorphTarget*> Morphs;
	for (const auto& Morph : Mesh->GetMorphTargets()) Morphs.Add(Morph.Get());
	Morphs.Sort([](const UMorphTarget& A, const UMorphTarget& B) { return A.GetName()<B.GetName(); });
	for (const auto* Morph : Morphs)
	{
		const FString Name = Morph->GetName();
		Hash.Update(reinterpret_cast<const uint8*>(*Name), Name.Len()*sizeof(TCHAR));
		for (const auto& LOD : Morph->GetMorphLODModels())
		{
			Hash.Update(reinterpret_cast<const uint8*>(&LOD.NumBaseMeshVerts), sizeof(LOD.NumBaseMeshVerts));
			for (const auto& V : LOD.Vertices)
			{
				Hash.Update(reinterpret_cast<const uint8*>(&V.SourceIdx),sizeof(V.SourceIdx));
				Hash.Update(reinterpret_cast<const uint8*>(&V.PositionDelta),sizeof(V.PositionDelta));
				Hash.Update(reinterpret_cast<const uint8*>(&V.TangentZDelta),sizeof(V.TangentZDelta));
			}
		}
	}
	uint8 Digest[16]; Hash.Final(Digest); return BytesToHex(Digest,16);
}
}

FString UEFBodyOnboardingLibrary::ConvertGenesis9BodyReport(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody)
{
	FString Error;
	return ConvertGenesis9Body(Mesh,LocalSkeleton,ReferenceBody,Error) ? TEXT("PASS: local skeleton, morph deltas, LODs, materials and local physics") : TEXT("ERROR: ")+Error;
}

bool UEFBodyOnboardingLibrary::ConvertGenesis9Body(USkeletalMesh* Mesh, USkeleton* LocalSkeleton, USkeletalMesh* ReferenceBody, FString& Error)
{
	Error.Reset();
	if (!Mesh || !LocalSkeleton || !ReferenceBody || !ReferenceBody->GetSkeleton() || !GEditor)
	{ Error=TEXT("Missing candidate, local skeleton, established reference or editor"); return false; }
	const FString Folder=FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
	if (!Folder.StartsWith(TEXT("/Game/DazToUnreal/")) || Mesh->GetSkeleton()!=LocalSkeleton ||
		LocalSkeleton==ReferenceBody->GetSkeleton() || FPackageName::GetLongPackagePath(LocalSkeleton->GetOutermost()->GetName())!=Folder ||
		Folder==TEXT("/Game/DazToUnreal/Male") || Folder==TEXT("/Game/DazToUnreal/Female") || Folder==TEXT("/Game/DazToUnreal/Multiple"))
	{ Error=TEXT("Conversion requires an unshared candidate and its imported local skeleton; protected bodies refused"); return false; }
	UPhysicsAsset* Physics=Mesh->GetPhysicsAsset();
	if (!Physics || FPackageName::GetLongPackagePath(Physics->GetOutermost()->GetName())!=Folder)
	{ Error=TEXT("Candidate must have its own imported physics asset in the same folder"); return false; }
	TArray<FName> Referencers;
	auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.GetReferencers(LocalSkeleton->GetOutermost()->GetFName(),Referencers);
	for (FName Package : Referencers)
		if (Package!=Mesh->GetOutermost()->GetFName() && Package!=LocalSkeleton->GetOutermost()->GetFName())
		{ Error=TEXT("Imported skeleton has another referencer: ")+Package.ToString(); return false; }
	Referencers.Reset(); Registry.GetReferencers(Physics->GetOutermost()->GetFName(),Referencers);
	for (FName Package : Referencers)
		if (Package!=Mesh->GetOutermost()->GetFName() && Package!=Physics->GetOutermost()->GetFName())
		{ Error=TEXT("Imported physics asset has another referencer: ")+Package.ToString(); return false; }
	const auto& Raw=Mesh->GetRefSkeleton();
	if (Raw.FindRawBoneIndex(TEXT("spine_01"))!=INDEX_NONE)
	{ Error=TEXT("Already converted: inspect/bind the local skeleton; never convert twice"); return false; }
	for (FName Bone : {FName("root"),FName("pelvis"),FName("hip"),FName("spine1"),FName("spine4"),FName("neck1"),FName("neck2"),FName("l_thigh"),FName("r_thigh"),FName("l_hand"),FName("r_hand")})
		if (Raw.FindRawBoneIndex(Bone)==INDEX_NONE)
		{ Error=TEXT("Unsupported input hierarchy: missing G9 bone ")+Bone.ToString(); return false; }
	USkeletonModifier* Modifier=NewObject<USkeletonModifier>();
	if (!Modifier->SetSkeletalMesh(Mesh) || !ApplyGenesis9Recipe(Modifier))
	{ Error=TEXT("G9 recipe rejected input before any asset mutation"); return false; }
	const FReferenceSkeleton& Converted=Modifier->GetReferenceSkeleton();
	if (Converted.GetRawBoneNum()!=Raw.GetRawBoneNum())
	{ Error=TEXT("Recipe changed bone count"); return false; }
	for (const FTransform& Transform : Converted.GetRawRefBonePose())
		if (!Transform.IsValid()) { Error=TEXT("Non-finite or invalid converted transform"); return false; }
	// Build a fresh tree in isolation. Assign it before commit so SkeletonModifier
	// takes its compatible path instead of opening a New/Merge modal or touching UEFN.
	USkeletalMesh* TreeCarrier=NewObject<USkeletalMesh>();
	USkeleton* NewTree=NewObject<USkeleton>();
	TreeCarrier->SetRefSkeleton(Converted); TreeCarrier->SetSkeleton(NewTree);
	if (!NewTree->MergeAllBonesToBoneTree(TreeCarrier,false))
	{ Error=TEXT("Could not create isolated converted skeleton"); return false; }
	const FString BeforeMorphs=MorphDigest(Mesh);
	const int32 BeforeLODs=Mesh->GetLODNum();
	const auto BeforeMaterials=Mesh->GetMaterials();
	bool bSuccess=false;
	{
		FScopedTransaction Transaction(NSLOCTEXT("EFBodyOnboarding","ConvertG9","Convert imported G9 body with its own skeleton"));
		Mesh->Modify(); LocalSkeleton->Modify(); Physics->Modify();
		Mesh->SetSkeleton(NewTree);
		bSuccess=Modifier->CommitSkeletonToSkeletalMesh();
		if (bSuccess) bSuccess=LocalSkeleton->RecreateBoneTree(Mesh);
		Mesh->SetSkeleton(LocalSkeleton);
		if (bSuccess) bSuccess=BindLocalSkeleton(Mesh,LocalSkeleton,ReferenceBody,Error);
		if (bSuccess && (BeforeMorphs!=MorphDigest(Mesh) || BeforeLODs!=Mesh->GetLODNum() || BeforeMaterials!=Mesh->GetMaterials()))
		{ bSuccess=false; Error=TEXT("Conversion changed morph deltas, LOD count or material slots"); }
		if (bSuccess)
		{
			FText PhysicsError;
			bSuccess=FPhysicsAssetUtils::CreateFromSkeletalMesh(Physics,Mesh,FPhysAssetCreateParams(),PhysicsError);
			if (!bSuccess) Error=TEXT("Candidate physics rebuild failed: ")+PhysicsError.ToString();
		}
	}
	if (!bSuccess)
	{
		const bool bUndone=GEditor->UndoTransaction();
		Error=FString::Printf(TEXT("Conversion failed; transaction rollback=%s. Do not save. %s"),bUndone?TEXT("true"):TEXT("false"),*Error);
		return false;
	}
	Mesh->MarkPackageDirty(); LocalSkeleton->MarkPackageDirty(); Physics->MarkPackageDirty();
	return true;
}
