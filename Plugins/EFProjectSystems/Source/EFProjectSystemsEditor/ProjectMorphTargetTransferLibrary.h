#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectMorphTargetTransferLibrary.generated.h"

class USkeletalMesh;

UCLASS()
class EFPROJECTSYSTEMSEDITOR_API UProjectMorphTargetTransferLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static TArray<FName> ListMorphTargets(USkeletalMesh* Mesh);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString AuditMorphTargetTransfer(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, const TArray<FName>& MorphNames);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString CopyMorphTargetsToMesh(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, const TArray<FName>& MorphNames, bool bOverwriteExisting = false, bool bDryRun = true);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString CompareMorphTargetDeltas(USkeletalMesh* Mesh, FName MorphA, FName MorphB);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString AnalyzeMorphTargetDeltas(USkeletalMesh* Mesh, const TArray<FName>& MorphNames);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString CopyMorphTargetToMeshFilteredByBone(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, FName SourceMorphName, FName TargetMorphName, const TArray<FName>& RequiredBoneNameTokens, float MinBoneWeight = 1.0f, bool bOverwriteExisting = false);

	UFUNCTION(BlueprintCallable, Category = "Project|Morph Transfer")
	static FString RemoveMorphTargetsFromMesh(USkeletalMesh* TargetMesh, const TArray<FName>& MorphNames);
};
