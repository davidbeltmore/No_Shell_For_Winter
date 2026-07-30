#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectAnimationDiagnosticsLibrary.generated.h"

class UAnimSequenceBase;
class UAnimSequence;
class UAnimationAsset;
class USkeletalMeshComponent;
class USkeleton;

UCLASS()
class EFPROJECTSYSTEMSEDITOR_API UProjectAnimationDiagnosticsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Project|Animation Migration")
	static bool AssignMissingAnimationSkeleton(UAnimationAsset* AnimationAsset, USkeleton* Skeleton);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Migration")
	static FString BuildMaleWalkRetargetAssets();

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString SnapshotObjectProperties(UObject* Target, const FString& NameContainsCsv);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString SnapshotSkeletalMeshComponent(USkeletalMeshComponent* MeshComponent, const FString& BoneNameContainsCsv, int32 MaxBones);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString SnapshotAnimSequenceAsset(UAnimSequenceBase* AnimationAsset);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString SnapshotAnimBlueprintForMesh(USkeletalMeshComponent* MeshComponent, const FString& NodeNameContainsCsv);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString CompareMeshPoseToCurrentMontage(USkeletalMeshComponent* MeshComponent, const FString& BoneNameContainsCsv, int32 MaxBones);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Diagnostics")
	static FString CompareMeshPoseToAnimationAtTime(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* AnimationAsset, float TimeSeconds, const FString& BoneNameContainsCsv, int32 MaxBones);

	UFUNCTION(BlueprintCallable, Category = "Project|Animation Migration")
	static FString RedistributeAdjacentBoneRotation(
		UAnimSequence* AnimationAsset,
		FName LowerBoneName,
		FName UpperBoneName,
		float LowerBoneReferenceBlend);
};
