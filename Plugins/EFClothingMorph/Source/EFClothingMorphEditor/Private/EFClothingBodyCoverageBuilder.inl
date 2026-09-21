// Project-owned adaptation. The source Daz graph is loaded read-only.
FEFClothingSurfaceDeformerBuildResult UEFClothingSurfaceDeformerBuilderLibrary::BuildBodyCoverageDeformer()
{
	FEFClothingSurfaceDeformerBuildResult Result;
	const FString PackageName = TEXT("/EFClothingMorph/Deformers/DG_EFBodyCoverageDQS");
	const FString ObjectPath = PackageName + TEXT(".DG_EFBodyCoverageDQS");
	Result.DeformerAsset = FSoftObjectPath(ObjectPath);
	UOptimusDeformer* Graph = LoadObject<UOptimusDeformer>(nullptr, *ObjectPath);
	if (!Graph)
	{
		UOptimusDeformer* Source = LoadObject<UOptimusDeformer>(nullptr,
			TEXT("/DazToUnreal/Deformers/DTU_DQS_Deformer.DTU_DQS_Deformer"));
		if (!Source) { Result.Report = TEXT("FAIL: Daz DQS source graph is unavailable."); return Result; }
		Graph = DuplicateObject<UOptimusDeformer>(Source, CreatePackage(*PackageName), TEXT("DG_EFBodyCoverageDQS"));
		Graph->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Graph);
	}
	int32 Kernels = 0;
	for (UOptimusNode* Node : Graph->GetUpdateGraph()->GetAllNodes())
	{
		IOptimusShaderTextProvider* Provider = Cast<IOptimusShaderTextProvider>(Node);
		if (!Provider) { continue; }
		if (!Provider->GetShaderText().Contains(TEXT("DualQuat"))) { continue; }
		Provider->SetShaderText(TEXT(R"HLSL(KERNEL
{
    if (Index >= ReadNumThreads().x) return;
    float3 Position = ReadPosition(Index) + ReadDeltaPosition(Index);
    float4 TangentX = ReadTangentX(Index);
    float4 TangentZ = ReadTangentZ(Index);
    float3 Normal = TangentZ.xyz + ReadDeltaNormal(Index);
    float2x4 DQuat = 0;
    float3 LinearPosition = 0;
    float3 LinearTangent = 0;
    float3 LinearNormal = 0;
    bool HasHiddenInfluence = false;
    for (int Bone = 0; Bone < ReadNumBones(Index); ++Bone)
    {
        float Weight = ReadBoneWeight(Index, Bone);
        if (Weight <= 0) continue;
        float3x4 Matrix = ReadBoneMatrix(Index, Bone);
        float ScaleSquared = dot(Matrix[0].xyz, Matrix[0].xyz)
            + dot(Matrix[1].xyz, Matrix[1].xyz) + dot(Matrix[2].xyz, Matrix[2].xyz);
        bool Hidden = ScaleSquared < 1.e-8f;
        HasHiddenInfluence = HasHiddenInfluence || Hidden;
        LinearPosition += Weight * mul(Matrix, float4(Position, 1));
        LinearTangent += Weight * mul(Matrix, float4(TangentX.xyz, 0));
        LinearNormal += Weight * mul(Matrix, float4(Normal, 0));
        // A collapsed matrix is not a rigid transform and cannot become a dual quaternion.
        if (!Hidden)
        {
            float2x4 BoneQuat = DualQuatFromMatrix(transpose(float4x4(Matrix, float4(0,0,0,1))));
            if (dot(BoneQuat[0], DQuat[0]) < 0) BoneQuat = -BoneQuat;
            DQuat += Weight * BoneQuat;
        }
    }
    float3 OutPosition, OutTangent, OutNormal;
    if (HasHiddenInfluence)
    {
        OutPosition = LinearPosition;
        OutTangent = dot(LinearTangent,LinearTangent) > 1.e-10f ? normalize(LinearTangent) : float3(1,0,0);
        OutNormal = dot(LinearNormal,LinearNormal) > 1.e-10f ? normalize(LinearNormal) : float3(0,0,1);
    }
    else
    {
        DQuat = DualQuatNormalize(DQuat);
        OutPosition = DualQuatTransformVector(DQuat, float4(Position,1));
        OutTangent = normalize(DualQuatRotateVector(DQuat, TangentX.xyz));
        OutNormal = normalize(DualQuatRotateVector(DQuat, Normal));
    }
    WriteOutPosition(Index, OutPosition);
    WriteOutTangentX(Index, float4(OutTangent, TangentX.w));
    WriteOutTangentZ(Index, float4(OutNormal, TangentZ.w));
})HLSL"));
		++Kernels;
	}
	if (Kernels != 1) { Result.Report = TEXT("FAIL: expected one DQS kernel; no package saved."); return Result; }
	if (!EFClothingSurfaceDeformerBuilder::CompileDeformer(Graph, Result.Report)) { return Result; }
	Graph->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	Result.bSuccess = UPackage::SavePackage(Graph->GetOutermost(), Graph, *Filename, Args);
	Result.bRebuilt = Result.bSuccess;
	return Result;
}
