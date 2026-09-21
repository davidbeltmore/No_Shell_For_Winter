// Included inside EFClothingFitCompilerPrivate, after the render-LOD helpers.
// All geometry below is transient. Render topology and protected assets stay intact.
static void ExcludeFitBoneBranches(USkeletalMesh* Body, const TArray<FName>& Branches, FSurfaceRenderLOD& LOD)
{
	if (Branches.IsEmpty()) { return; }
	const FReferenceSkeleton& Ref = Body->GetRefSkeleton();
	TSet<int32> Bones;
	for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
	{
		for (int32 Parent = Bone; Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		{
			if (Branches.Contains(Ref.GetBoneName(Parent))) { Bones.Add(Bone); break; }
		}
	}
	TArray<int32> Remove;
	for (const int32 Triangle : LOD.Mesh.TriangleIndicesItr())
	{
		const FIndex3i Vertices = LOD.Mesh.GetTriangle(Triangle);
		bool bExcluded = false;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			float Weight = 0.0f;
			for (const auto& Influence : LOD.SkinWeights[Vertices[Corner]])
			{
				if (Bones.Contains(Influence.Key)) { Weight += Influence.Value; }
			}
			bExcluded |= Weight > 0.1f;
		}
		if (bExcluded) { Remove.Add(Triangle); }
	}
	for (const int32 Triangle : Remove)
	{
		LOD.Mesh.RemoveTriangle(Triangle, false);
		++LOD.ExcludedTriangleCount;
	}
}

/** Reconstruct the reference shape in target render-index space using shared body UVs.
 * The runtime samples those exact target indices after animation, transporting the
 * reference garment offset in both directions (including a smaller chest).
 * UV triangles, rather than import indices or vertex counts, support different LODs.
 */
static bool BuildUnisexReferenceShape(
	USkeletalMesh* Target, USkeletalMesh* Reference, FSurfaceRenderLOD& TargetLOD, FString& OutError)
{
	FSkinnedAssetCompilingManager::Get().FinishCompilation({Reference});
	FSurfaceRenderLOD ReferenceLOD;
	if (!BuildSurfaceRenderLOD(Reference, 0, {}, NAME_None, ReferenceLOD, OutError)) { return false; }
	const auto& RefBuffer = Reference->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer;
	const auto& TargetBuffer = Target->GetResourceForRendering()->LODRenderData[TargetLOD.LODIndex].StaticVertexBuffers.StaticMeshVertexBuffer;
	if (RefBuffer.GetNumTexCoords() == 0 || TargetBuffer.GetNumTexCoords() == 0)
	{
		OutError = TEXT("Unisex reference transport requires a shared body UV atlas."); return false;
	}
	constexpr double Grid = 64.0;
	TMap<FIntPoint, TArray<int32>> Cells;
	auto Cell = [](const FVector2f& UV) { return FIntPoint(FMath::FloorToInt(UV.X * Grid), FMath::FloorToInt(UV.Y * Grid)); };
	for (int32 Triangle : ReferenceLOD.Mesh.TriangleIndicesItr())
	{
		const FIndex3i V = ReferenceLOD.Mesh.GetTriangle(Triangle);
		const FVector2f A = RefBuffer.GetVertexUV(V.A, 0), B = RefBuffer.GetVertexUV(V.B, 0), C = RefBuffer.GetVertexUV(V.C, 0);
		const FIntPoint Min = Cell(FVector2f(FMath::Min3(A.X,B.X,C.X), FMath::Min3(A.Y,B.Y,C.Y)));
		const FIntPoint Max = Cell(FVector2f(FMath::Max3(A.X,B.X,C.X), FMath::Max3(A.Y,B.Y,C.Y)));
		if (Max.X - Min.X > 64 || Max.Y - Min.Y > 64) { continue; }
		for (int32 X = Min.X; X <= Max.X; ++X)
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y) { Cells.FindOrAdd(FIntPoint(X,Y)).Add(Triangle); }
	}
	TBitArray<> Mapped(false, TargetLOD.Positions.Num());
	// Mesh-local bone indices can differ even when the skeleton is shared.
	// Compare semantic influences in the reference mesh's index space only.
	TArray<int32> TargetToReferenceBone;
	const FReferenceSkeleton& TargetSkeleton = Target->GetRefSkeleton();
	const FReferenceSkeleton& ReferenceSkeleton = Reference->GetRefSkeleton();
	for (int32 Bone = 0; Bone < TargetSkeleton.GetNum(); ++Bone)
	{
		TargetToReferenceBone.Add(ReferenceSkeleton.FindBoneIndex(TargetSkeleton.GetBoneName(Bone)));
	}
	for (int32 Vertex = 0; Vertex < TargetLOD.Positions.Num(); ++Vertex)
	{
		const FVector2f UV = TargetBuffer.GetVertexUV(Vertex, 0);
		const TArray<int32>* Candidates = Cells.Find(Cell(UV));
		if (!Candidates) { continue; }
		TMap<int32, float> ReferenceSpaceWeights;
		for (const auto& Influence : TargetLOD.SkinWeights[Vertex])
		{
			if (TargetToReferenceBone.IsValidIndex(Influence.Key)
				&& TargetToReferenceBone[Influence.Key] != INDEX_NONE)
			{
				ReferenceSpaceWeights.FindOrAdd(TargetToReferenceBone[Influence.Key]) += Influence.Value;
			}
		}
		double BestScore = TNumericLimits<double>::Max();
		FVector3d BestPosition = FVector3d::Zero(), BestNormal = FVector3d::Zero();
		for (const int32 Triangle : *Candidates)
		{
			const FIndex3i V = ReferenceLOD.Mesh.GetTriangle(Triangle);
			const FVector2f A = RefBuffer.GetVertexUV(V.A,0), B = RefBuffer.GetVertexUV(V.B,0), C = RefBuffer.GetVertexUV(V.C,0);
			const FVector2f AB = B-A, AC = C-A, AP = UV-A;
			const double Det = double(AB.X)*AC.Y - double(AB.Y)*AC.X;
			if (FMath::Abs(Det) < 1.e-12) { continue; }
			const double W1 = (double(AP.X)*AC.Y-double(AP.Y)*AC.X)/Det;
			const double W2 = (double(AB.X)*AP.Y-double(AB.Y)*AP.X)/Det;
			const double W0 = 1.0-W1-W2;
			if (FMath::Min3(W0,W1,W2) < -0.002) { continue; }
			const FVector3d P = ReferenceLOD.Positions[V.A]*W0 + ReferenceLOD.Positions[V.B]*W1 + ReferenceLOD.Positions[V.C]*W2;
			const double Distance = (P-TargetLOD.Positions[Vertex]).SquaredLength();
			if (Distance > FMath::Square(double(EFClothingMorphV4::MaximumAutomaticBodyShapeTravelCm))) { continue; }
			const double Similarity = ComputeBoneWeightSimilarity(ReferenceSpaceWeights, ReferenceLOD, FIntVector(V.A,V.B,V.C), FVector3d(W0,W1,W2));
			const double Score = Distance + (1.0-Similarity)*400.0;
			if (Score < BestScore)
			{
				BestScore = Score; BestPosition = P;
				BestNormal = ReferenceLOD.Normals[V.A]*W0 + ReferenceLOD.Normals[V.B]*W1 + ReferenceLOD.Normals[V.C]*W2;
			}
		}
		if (BestScore < TNumericLimits<double>::Max())
		{
			Mapped[Vertex] = true;
			TargetLOD.Positions[Vertex] = BestPosition;
			TargetLOD.Mesh.SetVertex(Vertex, BestPosition);
			BestNormal.Normalize(); TargetLOD.Normals[Vertex] = BestNormal;
		}
	}
	TArray<int32> Remove;
	const int32 OriginalTriangles = TargetLOD.Mesh.TriangleCount();
	for (const int32 Triangle : TargetLOD.Mesh.TriangleIndicesItr())
	{
		const FIndex3i V = TargetLOD.Mesh.GetTriangle(Triangle);
		if (!Mapped[V.A] || !Mapped[V.B] || !Mapped[V.C]
			|| (TargetLOD.Positions[V.B]-TargetLOD.Positions[V.A]).Cross(TargetLOD.Positions[V.C]-TargetLOD.Positions[V.A]).SquaredLength() < 1.e-12)
		{ Remove.Add(Triangle); }
	}
	for (const int32 Triangle : Remove) { TargetLOD.Mesh.RemoveTriangle(Triangle, false); }
	UE_LOG(LogEFClothingFitCompiler, Display, TEXT("Unisex reference %s -> %s LOD %d: %d/%d UV triangles mapped."),
		*Reference->GetName(), *Target->GetName(), TargetLOD.LODIndex, TargetLOD.Mesh.TriangleCount(), OriginalTriangles);
	if (TargetLOD.Mesh.TriangleCount() < OriginalTriangles * 0.85)
	{
		OutError = TEXT("Unisex body UV correspondence covers less than 85% of the active surface; provide a compatible body atlas.");
		return false;
	}
	return true;
}
