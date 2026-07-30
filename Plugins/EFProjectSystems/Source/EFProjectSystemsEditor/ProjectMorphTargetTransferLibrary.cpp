#include "ProjectMorphTargetTransferLibrary.h"

#include "Animation/MorphTarget.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace ProjectMorphTargetTransfer
{
	static FString ToJson(const TSharedRef<FJsonObject>& Root)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}

	static UMorphTarget* FindMorphTarget(USkeletalMesh* Mesh, const FName MorphName)
	{
		if (!Mesh || MorphName.IsNone())
		{
			return nullptr;
		}

		const TArray<TObjectPtr<UMorphTarget>>& MorphTargets = Mesh->GetMorphTargets();
		for (UMorphTarget* MorphTarget : MorphTargets)
		{
			if (MorphTarget && MorphTarget->GetFName() == MorphName)
			{
				return MorphTarget;
			}
		}

		return nullptr;
	}

	static TArray<FName> ResolveMorphNames(USkeletalMesh* SourceMesh, const TArray<FName>& RequestedMorphNames)
	{
		TArray<FName> Result;
		if (!SourceMesh)
		{
			return Result;
		}

		if (!RequestedMorphNames.IsEmpty())
		{
			for (const FName MorphName : RequestedMorphNames)
			{
				if (!MorphName.IsNone())
				{
					Result.AddUnique(MorphName);
				}
			}
			return Result;
		}

		for (UMorphTarget* MorphTarget : SourceMesh->GetMorphTargets())
		{
			if (MorphTarget)
			{
				Result.AddUnique(MorphTarget->GetFName());
			}
		}
		return Result;
	}

	static int32 GetLODCount(const USkeletalMesh* Mesh)
	{
		const FSkeletalMeshModel* ImportedModel = Mesh ? Mesh->GetImportedModel() : nullptr;
		return ImportedModel ? ImportedModel->LODModels.Num() : 0;
	}

	static int32 GetLODVertexCount(const USkeletalMesh* Mesh, const int32 LODIndex)
	{
		const FSkeletalMeshModel* ImportedModel = Mesh ? Mesh->GetImportedModel() : nullptr;
		if (!ImportedModel || !ImportedModel->LODModels.IsValidIndex(LODIndex))
		{
			return 0;
		}

		return static_cast<int32>(ImportedModel->LODModels[LODIndex].NumVertices);
	}

	static void AddStringArray(TSharedRef<FJsonObject> Root, const TCHAR* FieldName, const TArray<FName>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> ArrayValues;
		for (const FName Value : Values)
		{
			ArrayValues.Add(MakeShared<FJsonValueString>(Value.ToString()));
		}
		Root->SetArrayField(FieldName, ArrayValues);
	}

	static void AddStringArray(TSharedRef<FJsonObject> Root, const TCHAR* FieldName, const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> ArrayValues;
		for (const FString& Value : Values)
		{
			ArrayValues.Add(MakeShared<FJsonValueString>(Value));
		}
		Root->SetArrayField(FieldName, ArrayValues);
	}

	static TArray<FString> NameTokensToLowerStrings(const TArray<FName>& Tokens)
	{
		TArray<FString> Result;
		for (const FName Token : Tokens)
		{
			FString TokenString = Token.ToString().TrimStartAndEnd().ToLower();
			if (!TokenString.IsEmpty())
			{
				Result.AddUnique(TokenString);
			}
		}
		return Result;
	}

	static const FSkeletalMeshLODRenderData* GetLODRenderData(const USkeletalMesh* Mesh, const int32 LODIndex)
	{
		const FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetResourceForRendering() : nullptr;
		if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
		{
			return nullptr;
		}
		return &RenderData->LODRenderData[LODIndex];
	}

	static bool GetRefBoneForInfluence(
		const USkeletalMesh* Mesh,
		const FSkeletalMeshLODRenderData& LODRenderData,
		const uint32 VertexIndex,
		const uint32 InfluenceIndex,
		FName& OutBoneName,
		uint16& OutWeight)
	{
		OutBoneName = NAME_None;
		OutWeight = 0;
		if (!Mesh || VertexIndex >= LODRenderData.GetNumVertices())
		{
			return false;
		}

		const FSkinWeightVertexBuffer* SkinWeights = LODRenderData.GetSkinWeightVertexBuffer();
		if (!SkinWeights)
		{
			return false;
		}

		OutWeight = SkinWeights->GetBoneWeight(VertexIndex, InfluenceIndex);
		if (OutWeight == 0)
		{
			return false;
		}

		int32 SectionIndex = INDEX_NONE;
		int32 SectionVertexIndex = INDEX_NONE;
		LODRenderData.GetSectionFromVertexIndex(static_cast<int32>(VertexIndex), SectionIndex, SectionVertexIndex);
		if (!LODRenderData.RenderSections.IsValidIndex(SectionIndex))
		{
			return false;
		}

		const FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];
		const uint32 LocalBoneIndex = SkinWeights->GetBoneIndex(VertexIndex, InfluenceIndex);
		const int32 RefBoneIndex = Section.BoneMap.IsValidIndex(static_cast<int32>(LocalBoneIndex))
			? static_cast<int32>(Section.BoneMap[LocalBoneIndex])
			: static_cast<int32>(LocalBoneIndex);

		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		if (RefBoneIndex < 0 || RefBoneIndex >= RefSkeleton.GetNum())
		{
			return false;
		}

		OutBoneName = RefSkeleton.GetBoneName(RefBoneIndex);
		return !OutBoneName.IsNone();
	}

	static bool VertexMatchesBoneTokens(
		const USkeletalMesh* Mesh,
		const int32 LODIndex,
		const uint32 VertexIndex,
		const TArray<FString>& RequiredTokens,
		const float MinBoneWeight)
	{
		if (RequiredTokens.IsEmpty())
		{
			return true;
		}

		const FSkeletalMeshLODRenderData* LODRenderData = GetLODRenderData(Mesh, LODIndex);
		if (!LODRenderData)
		{
			return false;
		}

		const FSkinWeightVertexBuffer* SkinWeights = LODRenderData->GetSkinWeightVertexBuffer();
		const uint32 MaxInfluences = SkinWeights ? SkinWeights->GetMaxBoneInfluences() : 0;
		const float ClampedMinBoneWeight = FMath::Clamp(MinBoneWeight, 0.0f, 65535.0f);

		FName DominantBoneName;
		uint16 DominantBoneWeight = 0;
		for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
		{
			FName BoneName;
			uint16 BoneWeight = 0;
			if (!GetRefBoneForInfluence(Mesh, *LODRenderData, VertexIndex, InfluenceIndex, BoneName, BoneWeight))
			{
				continue;
			}

			if (BoneWeight > DominantBoneWeight)
			{
				DominantBoneWeight = BoneWeight;
				DominantBoneName = BoneName;
			}
		}

		if (DominantBoneName.IsNone() || static_cast<float>(DominantBoneWeight) < ClampedMinBoneWeight)
		{
			return false;
		}

		const FString BoneNameLower = DominantBoneName.ToString().ToLower();
		for (const FString& Token : RequiredTokens)
		{
			if (BoneNameLower.Contains(Token))
			{
				return true;
			}
		}
		return false;
	}

	static TSharedRef<FJsonObject> VectorBoundsObject(const FVector3f& MinValue, const FVector3f& MaxValue)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("min_x"), MinValue.X);
		Root->SetNumberField(TEXT("min_y"), MinValue.Y);
		Root->SetNumberField(TEXT("min_z"), MinValue.Z);
		Root->SetNumberField(TEXT("max_x"), MaxValue.X);
		Root->SetNumberField(TEXT("max_y"), MaxValue.Y);
		Root->SetNumberField(TEXT("max_z"), MaxValue.Z);
		return Root;
	}

	static FIntVector QuantizePosition(const FVector3f& Position, const float CellSize)
	{
		return FIntVector(
			FMath::RoundToInt(Position.X / CellSize),
			FMath::RoundToInt(Position.Y / CellSize),
			FMath::RoundToInt(Position.Z / CellSize));
	}

	static void BuildPositionLookup(
		const FSkeletalMeshLODRenderData& LODRenderData,
		const float CellSize,
		TMap<FIntVector, TArray<uint32>>& OutLookup)
	{
		OutLookup.Reset();
		const uint32 VertexCount = LODRenderData.GetNumVertices();
		for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FVector3f Position = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
			OutLookup.FindOrAdd(QuantizePosition(Position, CellSize)).Add(VertexIndex);
		}
	}

	static bool FindNearestVertexByPosition(
		const FSkeletalMeshLODRenderData& LODRenderData,
		const TMap<FIntVector, TArray<uint32>>& Lookup,
		const FVector3f& SourcePosition,
		const float CellSize,
		const float MaxDistance,
		uint32& OutVertexIndex,
		float& OutDistance)
	{
		const FIntVector BaseKey = QuantizePosition(SourcePosition, CellSize);
		float BestDistanceSq = FMath::Square(MaxDistance);
		bool bFound = false;
		uint32 BestVertexIndex = 0;

		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					const FIntVector Key(BaseKey.X + X, BaseKey.Y + Y, BaseKey.Z + Z);
					const TArray<uint32>* CandidateVertices = Lookup.Find(Key);
					if (!CandidateVertices)
					{
						continue;
					}

					for (const uint32 CandidateVertexIndex : *CandidateVertices)
					{
						if (CandidateVertexIndex >= LODRenderData.GetNumVertices())
						{
							continue;
						}

						const FVector3f CandidatePosition = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(CandidateVertexIndex);
						const float DistanceSq = FVector3f::DistSquared(SourcePosition, CandidatePosition);
						if (DistanceSq <= BestDistanceSq)
						{
							BestDistanceSq = DistanceSq;
							BestVertexIndex = CandidateVertexIndex;
							bFound = true;
						}
					}
				}
			}
		}

		if (!bFound)
		{
			return false;
		}

		OutVertexIndex = BestVertexIndex;
		OutDistance = FMath::Sqrt(BestDistanceSq);
		return true;
	}

	static bool AuditMorph(
		USkeletalMesh* SourceMesh,
		USkeletalMesh* TargetMesh,
		const FName MorphName,
		TSharedRef<FJsonObject> OutMorphObject,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		OutMorphObject->SetStringField(TEXT("name"), MorphName.ToString());

		UMorphTarget* SourceMorph = FindMorphTarget(SourceMesh, MorphName);
		UMorphTarget* ExistingTargetMorph = FindMorphTarget(TargetMesh, MorphName);
		OutMorphObject->SetBoolField(TEXT("exists_in_source"), SourceMorph != nullptr);
		OutMorphObject->SetBoolField(TEXT("exists_in_target"), ExistingTargetMorph != nullptr);
		if (!SourceMorph)
		{
			OutFailureReason = TEXT("missing_source_morph");
			OutMorphObject->SetStringField(TEXT("failure"), OutFailureReason);
			return false;
		}

		const FSkeletalMeshModel* SourceImportedModel = SourceMesh ? SourceMesh->GetImportedModel() : nullptr;
		const FSkeletalMeshModel* TargetImportedModel = TargetMesh ? TargetMesh->GetImportedModel() : nullptr;
		const int32 SourceLODCount = SourceImportedModel ? SourceImportedModel->LODModels.Num() : 0;
		const int32 TargetLODCount = TargetImportedModel ? TargetImportedModel->LODModels.Num() : 0;
		OutMorphObject->SetNumberField(TEXT("source_lod_count"), SourceLODCount);
		OutMorphObject->SetNumberField(TEXT("target_lod_count"), TargetLODCount);
		if (!SourceImportedModel || !TargetImportedModel || TargetLODCount < SourceLODCount)
		{
			OutFailureReason = TEXT("lod_count_mismatch");
			OutMorphObject->SetStringField(TEXT("failure"), OutFailureReason);
			return false;
		}

		bool bHasAnyDelta = false;
		TArray<TSharedPtr<FJsonValue>> LODObjects;
		for (int32 LODIndex = 0; LODIndex < SourceLODCount; ++LODIndex)
		{
			const int32 SourceVertexCount = GetLODVertexCount(SourceMesh, LODIndex);
			const int32 TargetVertexCount = GetLODVertexCount(TargetMesh, LODIndex);
			const TConstArrayView<FMorphTargetDelta> SourceDeltas = SourceMorph->GetMorphTargetDeltas(LODIndex);

			const TSharedRef<FJsonObject> LODObject = MakeShared<FJsonObject>();
			LODObject->SetNumberField(TEXT("lod"), LODIndex);
			LODObject->SetNumberField(TEXT("source_vertices"), SourceVertexCount);
			LODObject->SetNumberField(TEXT("target_vertices"), TargetVertexCount);
			LODObject->SetNumberField(TEXT("delta_count"), SourceDeltas.Num());
			LODObject->SetNumberField(TEXT("num_base_mesh_vertices"), SourceMorph->GetMorphLODModels().IsValidIndex(LODIndex) ? SourceMorph->GetMorphLODModels()[LODIndex].NumBaseMeshVerts : 0);
			LODObject->SetBoolField(TEXT("vertex_count_match"), SourceVertexCount == TargetVertexCount);
			LODObjects.Add(MakeShared<FJsonValueObject>(LODObject));

			if (SourceDeltas.Num() > 0)
			{
				bHasAnyDelta = true;
			}

			if (SourceVertexCount != TargetVertexCount)
			{
				OutFailureReason = FString::Printf(TEXT("vertex_count_mismatch_lod_%d"), LODIndex);
			}
		}

		OutMorphObject->SetArrayField(TEXT("lods"), LODObjects);
		if (!OutFailureReason.IsEmpty())
		{
			OutMorphObject->SetStringField(TEXT("failure"), OutFailureReason);
			return false;
		}

		if (!bHasAnyDelta)
		{
			OutFailureReason = TEXT("source_morph_has_no_deltas");
			OutMorphObject->SetStringField(TEXT("failure"), OutFailureReason);
			return false;
		}

		OutMorphObject->SetBoolField(TEXT("compatible"), true);
		return true;
	}

	static bool RegisterCopiedMorph(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, UMorphTarget* SourceMorph)
	{
		if (!SourceMesh || !TargetMesh || !SourceMorph)
		{
			return false;
		}

		const FSkeletalMeshModel* SourceImportedModel = SourceMesh->GetImportedModel();
		FSkeletalMeshModel* TargetImportedModel = TargetMesh->GetImportedModel();
		if (!SourceImportedModel || !TargetImportedModel)
		{
			return false;
		}

		UMorphTarget* CopiedMorph = NewObject<UMorphTarget>(TargetMesh, SourceMorph->GetFName(), RF_Public | RF_Transactional);
		if (!CopiedMorph)
		{
			return false;
		}

		CopiedMorph->BaseSkelMesh = TargetMesh;
		const int32 SourceLODCount = SourceImportedModel->LODModels.Num();
		for (int32 LODIndex = 0; LODIndex < SourceLODCount; ++LODIndex)
		{
			if (!TargetImportedModel->LODModels.IsValidIndex(LODIndex))
			{
				return false;
			}

			const TConstArrayView<FMorphTargetDelta> SourceDeltasView = SourceMorph->GetMorphTargetDeltas(LODIndex);
			if (SourceDeltasView.IsEmpty())
			{
				continue;
			}

			TArray<FMorphTargetDelta> CopiedDeltas;
			CopiedDeltas.Reserve(SourceDeltasView.Num());
			for (const FMorphTargetDelta& SourceDelta : SourceDeltasView)
			{
				CopiedDeltas.Add(SourceDelta);
			}

			const bool bGeneratedByEngine = SourceMorph->IsGeneratedByEngine(LODIndex);
			CopiedMorph->PopulateDeltas(
				CopiedDeltas,
				LODIndex,
				TargetImportedModel->LODModels[LODIndex].Sections,
				false,
				bGeneratedByEngine);
			CopiedMorph->SetGeneratedByEngine(LODIndex, bGeneratedByEngine);
			if (SourceMorph->IsCustomImported(LODIndex))
			{
				CopiedMorph->SetCustomImportedSourceFilename(LODIndex, SourceMorph->GetCustomImportedSourceFilename(LODIndex));
			}
		}

		CopiedMorph->RemoveEmptyMorphTargets();
		if (!CopiedMorph->HasValidData())
		{
			CopiedMorph->MarkAsGarbage();
			return false;
		}

		return TargetMesh->RegisterMorphTarget(CopiedMorph, false);
	}

	static bool RegisterFilteredMorph(
		USkeletalMesh* SourceMesh,
		USkeletalMesh* TargetMesh,
		UMorphTarget* SourceMorph,
		const FName TargetMorphName,
		const TArray<FString>& RequiredBoneTokens,
		const float MinBoneWeight,
		int32& OutCopiedDeltaCount,
		int32& OutSkippedDeltaCount,
		int32& OutUnmappedDeltaCount,
		float& OutMaxMappingDistance,
		float& OutAverageMappingDistance)
	{
		OutCopiedDeltaCount = 0;
		OutSkippedDeltaCount = 0;
		OutUnmappedDeltaCount = 0;
		OutMaxMappingDistance = 0.0f;
		OutAverageMappingDistance = 0.0f;
		if (!SourceMesh || !TargetMesh || !SourceMorph || TargetMorphName.IsNone())
		{
			return false;
		}

		const FSkeletalMeshModel* SourceImportedModel = SourceMesh->GetImportedModel();
		FSkeletalMeshModel* TargetImportedModel = TargetMesh->GetImportedModel();
		if (!SourceImportedModel || !TargetImportedModel)
		{
			return false;
		}

		UMorphTarget* CopiedMorph = NewObject<UMorphTarget>(TargetMesh, TargetMorphName, RF_Public | RF_Transactional);
		if (!CopiedMorph)
		{
			return false;
		}

		CopiedMorph->BaseSkelMesh = TargetMesh;
		const int32 SourceLODCount = SourceImportedModel->LODModels.Num();
		double MappingDistanceSum = 0.0;
		for (int32 LODIndex = 0; LODIndex < SourceLODCount; ++LODIndex)
		{
			if (!TargetImportedModel->LODModels.IsValidIndex(LODIndex))
			{
				return false;
			}

			const FSkeletalMeshLODRenderData* SourceLODRenderData = GetLODRenderData(SourceMesh, LODIndex);
			const FSkeletalMeshLODRenderData* TargetLODRenderData = GetLODRenderData(TargetMesh, LODIndex);
			if (!SourceLODRenderData || !TargetLODRenderData)
			{
				return false;
			}

			constexpr float PositionCellSize = 0.05f;
			constexpr float MaxPositionMatchDistance = 0.25f;
			TMap<FIntVector, TArray<uint32>> TargetPositionLookup;
			BuildPositionLookup(*TargetLODRenderData, PositionCellSize, TargetPositionLookup);

			const TConstArrayView<FMorphTargetDelta> SourceDeltasView = SourceMorph->GetMorphTargetDeltas(LODIndex);
			if (SourceDeltasView.IsEmpty())
			{
				continue;
			}

			TArray<FMorphTargetDelta> CopiedDeltas;
			CopiedDeltas.Reserve(SourceDeltasView.Num());
			for (const FMorphTargetDelta& SourceDelta : SourceDeltasView)
			{
				if (VertexMatchesBoneTokens(SourceMesh, LODIndex, SourceDelta.SourceIdx, RequiredBoneTokens, MinBoneWeight))
				{
					if (SourceDelta.SourceIdx >= SourceLODRenderData->GetNumVertices())
					{
						++OutUnmappedDeltaCount;
						continue;
					}

					const FVector3f SourcePosition = SourceLODRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(SourceDelta.SourceIdx);
					uint32 TargetVertexIndex = 0;
					float MappingDistance = 0.0f;
					if (!FindNearestVertexByPosition(*TargetLODRenderData, TargetPositionLookup, SourcePosition, PositionCellSize, MaxPositionMatchDistance, TargetVertexIndex, MappingDistance))
					{
						++OutUnmappedDeltaCount;
						continue;
					}

					FMorphTargetDelta CopiedDelta = SourceDelta;
					CopiedDelta.SourceIdx = TargetVertexIndex;
					CopiedDeltas.Add(CopiedDelta);
					++OutCopiedDeltaCount;
					OutMaxMappingDistance = FMath::Max(OutMaxMappingDistance, MappingDistance);
					MappingDistanceSum += static_cast<double>(MappingDistance);
				}
				else
				{
					++OutSkippedDeltaCount;
				}
			}

			if (CopiedDeltas.IsEmpty())
			{
				continue;
			}

			const bool bGeneratedByEngine = SourceMorph->IsGeneratedByEngine(LODIndex);
			CopiedMorph->PopulateDeltas(
				CopiedDeltas,
				LODIndex,
				TargetImportedModel->LODModels[LODIndex].Sections,
				false,
				bGeneratedByEngine);
			CopiedMorph->SetGeneratedByEngine(LODIndex, bGeneratedByEngine);
			if (SourceMorph->IsCustomImported(LODIndex))
			{
				CopiedMorph->SetCustomImportedSourceFilename(LODIndex, SourceMorph->GetCustomImportedSourceFilename(LODIndex));
			}
		}

		CopiedMorph->RemoveEmptyMorphTargets();
		if (!CopiedMorph->HasValidData())
		{
			CopiedMorph->MarkAsGarbage();
			return false;
		}

		OutAverageMappingDistance = OutCopiedDeltaCount > 0
			? static_cast<float>(MappingDistanceSum / static_cast<double>(OutCopiedDeltaCount))
			: 0.0f;
		return TargetMesh->RegisterMorphTarget(CopiedMorph, false);
	}
}

TArray<FName> UProjectMorphTargetTransferLibrary::ListMorphTargets(USkeletalMesh* Mesh)
{
	TArray<FName> Result;
	if (!Mesh)
	{
		return Result;
	}

	for (const TPair<FName, int32>& MorphTargetPair : Mesh->GetMorphTargetIndexMap())
	{
		if (!MorphTargetPair.Key.IsNone())
		{
			Result.AddUnique(MorphTargetPair.Key);
		}
	}

	for (UMorphTarget* MorphTarget : Mesh->GetMorphTargets())
	{
		if (MorphTarget)
		{
			Result.AddUnique(MorphTarget->GetFName());
		}
	}

	Result.Sort([](const FName& A, const FName& B)
	{
		return A.ToString() < B.ToString();
	});
	return Result;
}

FString UProjectMorphTargetTransferLibrary::AuditMorphTargetTransfer(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, const TArray<FName>& MorphNames)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("source_mesh"), SourceMesh ? SourceMesh->GetPathName() : FString());
	Root->SetStringField(TEXT("target_mesh"), TargetMesh ? TargetMesh->GetPathName() : FString());
	Root->SetBoolField(TEXT("source_valid"), SourceMesh != nullptr);
	Root->SetBoolField(TEXT("target_valid"), TargetMesh != nullptr);
	Root->SetNumberField(TEXT("source_lod_count"), ProjectMorphTargetTransfer::GetLODCount(SourceMesh));
	Root->SetNumberField(TEXT("target_lod_count"), ProjectMorphTargetTransfer::GetLODCount(TargetMesh));

	const TArray<FName> Names = ProjectMorphTargetTransfer::ResolveMorphNames(SourceMesh, MorphNames);
	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("requested_morphs"), Names);

	bool bAllCompatible = SourceMesh && TargetMesh && !Names.IsEmpty();
	TArray<TSharedPtr<FJsonValue>> MorphObjects;
	for (const FName MorphName : Names)
	{
		const TSharedRef<FJsonObject> MorphObject = MakeShared<FJsonObject>();
		FString FailureReason;
		const bool bCompatible = ProjectMorphTargetTransfer::AuditMorph(SourceMesh, TargetMesh, MorphName, MorphObject, FailureReason);
		MorphObject->SetBoolField(TEXT("compatible"), bCompatible);
		bAllCompatible &= bCompatible;
		MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
	}

	Root->SetArrayField(TEXT("morphs"), MorphObjects);
	Root->SetBoolField(TEXT("compatible"), bAllCompatible);
	return ProjectMorphTargetTransfer::ToJson(Root);
}

FString UProjectMorphTargetTransferLibrary::CopyMorphTargetsToMesh(
	USkeletalMesh* SourceMesh,
	USkeletalMesh* TargetMesh,
	const TArray<FName>& MorphNames,
	const bool bOverwriteExisting,
	const bool bDryRun)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("source_mesh"), SourceMesh ? SourceMesh->GetPathName() : FString());
	Root->SetStringField(TEXT("target_mesh"), TargetMesh ? TargetMesh->GetPathName() : FString());
	Root->SetBoolField(TEXT("dry_run"), bDryRun);
	Root->SetBoolField(TEXT("overwrite_existing"), bOverwriteExisting);

	const TArray<FName> Names = ProjectMorphTargetTransfer::ResolveMorphNames(SourceMesh, MorphNames);
	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("requested_morphs"), Names);

	bool bSuccess = SourceMesh && TargetMesh && !Names.IsEmpty();
	TArray<FName> CopiedNames;
	TArray<TSharedPtr<FJsonValue>> MorphObjects;

	for (const FName MorphName : Names)
	{
		const TSharedRef<FJsonObject> MorphObject = MakeShared<FJsonObject>();
		FString FailureReason;
		const bool bCompatible = ProjectMorphTargetTransfer::AuditMorph(SourceMesh, TargetMesh, MorphName, MorphObject, FailureReason);
		MorphObject->SetBoolField(TEXT("compatible"), bCompatible);
		if (!bCompatible)
		{
			bSuccess = false;
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		const bool bAlreadyExists = ProjectMorphTargetTransfer::FindMorphTarget(TargetMesh, MorphName) != nullptr;
		MorphObject->SetBoolField(TEXT("already_exists"), bAlreadyExists);
		if (bAlreadyExists && !bOverwriteExisting)
		{
			MorphObject->SetStringField(TEXT("status"), TEXT("skipped_existing"));
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		if (bDryRun)
		{
			MorphObject->SetStringField(TEXT("status"), TEXT("dry_run_ready"));
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		UMorphTarget* SourceMorph = ProjectMorphTargetTransfer::FindMorphTarget(SourceMesh, MorphName);
		if (bAlreadyExists)
		{
			TargetMesh->RemoveMorphTargets(MakeArrayView(&MorphName, 1));
		}

		TargetMesh->Modify();
		const bool bRegistered = ProjectMorphTargetTransfer::RegisterCopiedMorph(SourceMesh, TargetMesh, SourceMorph);
		MorphObject->SetBoolField(TEXT("registered"), bRegistered);
		MorphObject->SetStringField(TEXT("status"), bRegistered ? TEXT("copied") : TEXT("copy_failed"));
		if (bRegistered)
		{
			CopiedNames.Add(MorphName);
		}
		else
		{
			bSuccess = false;
		}

		MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
	}

	if (!bDryRun && !CopiedNames.IsEmpty())
	{
		TargetMesh->InitMorphTargets(true);
		TargetMesh->InvalidateDeriveDataCacheGUID();
		TargetMesh->MarkPackageDirty();
		if (UPackage* Package = TargetMesh->GetOutermost())
		{
			Package->MarkPackageDirty();
		}
	}

	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("copied_morphs"), CopiedNames);
	Root->SetNumberField(TEXT("copied_count"), CopiedNames.Num());
	Root->SetArrayField(TEXT("morphs"), MorphObjects);
	Root->SetBoolField(TEXT("success"), bSuccess);
	return ProjectMorphTargetTransfer::ToJson(Root);
}

FString UProjectMorphTargetTransferLibrary::CompareMorphTargetDeltas(USkeletalMesh* Mesh, const FName MorphA, const FName MorphB)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mesh"), Mesh ? Mesh->GetPathName() : FString());
	Root->SetStringField(TEXT("morph_a"), MorphA.ToString());
	Root->SetStringField(TEXT("morph_b"), MorphB.ToString());

	UMorphTarget* MorphTargetA = ProjectMorphTargetTransfer::FindMorphTarget(Mesh, MorphA);
	UMorphTarget* MorphTargetB = ProjectMorphTargetTransfer::FindMorphTarget(Mesh, MorphB);
	Root->SetBoolField(TEXT("morph_a_exists"), MorphTargetA != nullptr);
	Root->SetBoolField(TEXT("morph_b_exists"), MorphTargetB != nullptr);

	if (!MorphTargetA || !MorphTargetB)
	{
		Root->SetBoolField(TEXT("success"), false);
		return ProjectMorphTargetTransfer::ToJson(Root);
	}

	TSet<uint32> VerticesA;
	TSet<uint32> VerticesB;
	const int32 LODCount = FMath::Max(MorphTargetA->GetMorphLODModels().Num(), MorphTargetB->GetMorphLODModels().Num());
	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		for (const FMorphTargetDelta& Delta : MorphTargetA->GetMorphTargetDeltas(LODIndex))
		{
			VerticesA.Add(Delta.SourceIdx);
		}
		for (const FMorphTargetDelta& Delta : MorphTargetB->GetMorphTargetDeltas(LODIndex))
		{
			VerticesB.Add(Delta.SourceIdx);
		}
	}

	int32 SharedCount = 0;
	for (const uint32 VertexIndex : VerticesA)
	{
		if (VerticesB.Contains(VertexIndex))
		{
			++SharedCount;
		}
	}

	Root->SetNumberField(TEXT("morph_a_vertex_count"), VerticesA.Num());
	Root->SetNumberField(TEXT("morph_b_vertex_count"), VerticesB.Num());
	Root->SetNumberField(TEXT("shared_vertex_count"), SharedCount);
	Root->SetNumberField(TEXT("a_only_vertex_count"), VerticesA.Num() - SharedCount);
	Root->SetNumberField(TEXT("b_only_vertex_count"), VerticesB.Num() - SharedCount);
	Root->SetBoolField(TEXT("success"), true);
	return ProjectMorphTargetTransfer::ToJson(Root);
}

FString UProjectMorphTargetTransferLibrary::AnalyzeMorphTargetDeltas(USkeletalMesh* Mesh, const TArray<FName>& MorphNames)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mesh"), Mesh ? Mesh->GetPathName() : FString());
	Root->SetBoolField(TEXT("mesh_valid"), Mesh != nullptr);

	const TArray<FName> Names = ProjectMorphTargetTransfer::ResolveMorphNames(Mesh, MorphNames);
	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("requested_morphs"), Names);

	TArray<TSharedPtr<FJsonValue>> MorphObjects;
	for (const FName MorphName : Names)
	{
		const TSharedRef<FJsonObject> MorphObject = MakeShared<FJsonObject>();
		MorphObject->SetStringField(TEXT("name"), MorphName.ToString());

		UMorphTarget* MorphTarget = ProjectMorphTargetTransfer::FindMorphTarget(Mesh, MorphName);
		MorphObject->SetBoolField(TEXT("exists"), MorphTarget != nullptr);
		if (!Mesh || !MorphTarget)
		{
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		int32 TotalDeltas = 0;
		double DeltaMagnitudeSum = 0.0;
		float DeltaMagnitudeMax = 0.0f;
		bool bHasPositionBounds = false;
		const float MaxFloat = TNumericLimits<float>::Max();
		FVector3f MinPosition(MaxFloat, MaxFloat, MaxFloat);
		FVector3f MaxPosition(-MaxFloat, -MaxFloat, -MaxFloat);
		FVector3f MinDelta(MaxFloat, MaxFloat, MaxFloat);
		FVector3f MaxDelta(-MaxFloat, -MaxFloat, -MaxFloat);

		TMap<FString, double> BoneWeightSums;
		TMap<FString, int32> BoneInfluenceCounts;
		TMap<FString, int32> DominantBoneCounts;

		const int32 LODCount = MorphTarget->GetMorphLODModels().Num();
		TArray<TSharedPtr<FJsonValue>> LODObjects;
		for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		{
			const TConstArrayView<FMorphTargetDelta> Deltas = MorphTarget->GetMorphTargetDeltas(LODIndex);
			const FSkeletalMeshLODRenderData* LODRenderData = ProjectMorphTargetTransfer::GetLODRenderData(Mesh, LODIndex);

			int32 LODDeltaCount = 0;
			for (const FMorphTargetDelta& Delta : Deltas)
			{
				++LODDeltaCount;
				++TotalDeltas;

				const float DeltaMagnitude = Delta.PositionDelta.Size();
				DeltaMagnitudeSum += DeltaMagnitude;
				DeltaMagnitudeMax = FMath::Max(DeltaMagnitudeMax, DeltaMagnitude);
				MinDelta.X = FMath::Min(MinDelta.X, Delta.PositionDelta.X);
				MinDelta.Y = FMath::Min(MinDelta.Y, Delta.PositionDelta.Y);
				MinDelta.Z = FMath::Min(MinDelta.Z, Delta.PositionDelta.Z);
				MaxDelta.X = FMath::Max(MaxDelta.X, Delta.PositionDelta.X);
				MaxDelta.Y = FMath::Max(MaxDelta.Y, Delta.PositionDelta.Y);
				MaxDelta.Z = FMath::Max(MaxDelta.Z, Delta.PositionDelta.Z);

				if (LODRenderData && Delta.SourceIdx < LODRenderData->GetNumVertices())
				{
					const FVector3f Position = LODRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Delta.SourceIdx);
					MinPosition.X = FMath::Min(MinPosition.X, Position.X);
					MinPosition.Y = FMath::Min(MinPosition.Y, Position.Y);
					MinPosition.Z = FMath::Min(MinPosition.Z, Position.Z);
					MaxPosition.X = FMath::Max(MaxPosition.X, Position.X);
					MaxPosition.Y = FMath::Max(MaxPosition.Y, Position.Y);
					MaxPosition.Z = FMath::Max(MaxPosition.Z, Position.Z);
					bHasPositionBounds = true;

					const FSkinWeightVertexBuffer* SkinWeights = LODRenderData->GetSkinWeightVertexBuffer();
					const uint32 MaxInfluences = SkinWeights ? SkinWeights->GetMaxBoneInfluences() : 0;
					uint16 DominantWeight = 0;
					FString DominantBoneName;
					for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
					{
						FName BoneName;
						uint16 BoneWeight = 0;
						if (!ProjectMorphTargetTransfer::GetRefBoneForInfluence(Mesh, *LODRenderData, Delta.SourceIdx, InfluenceIndex, BoneName, BoneWeight))
						{
							continue;
						}

						const FString BoneNameString = BoneName.ToString();
						BoneWeightSums.FindOrAdd(BoneNameString) += static_cast<double>(BoneWeight);
						BoneInfluenceCounts.FindOrAdd(BoneNameString) += 1;
						if (BoneWeight > DominantWeight)
						{
							DominantWeight = BoneWeight;
							DominantBoneName = BoneNameString;
						}
					}

					if (!DominantBoneName.IsEmpty())
					{
						DominantBoneCounts.FindOrAdd(DominantBoneName) += 1;
					}
				}
			}

			const TSharedRef<FJsonObject> LODObject = MakeShared<FJsonObject>();
			LODObject->SetNumberField(TEXT("lod"), LODIndex);
			LODObject->SetNumberField(TEXT("delta_count"), LODDeltaCount);
			LODObjects.Add(MakeShared<FJsonValueObject>(LODObject));
		}

		TArray<FString> BoneNames;
		BoneWeightSums.GetKeys(BoneNames);
		BoneNames.Sort([&BoneWeightSums](const FString& A, const FString& B)
		{
			return BoneWeightSums.FindRef(A) > BoneWeightSums.FindRef(B);
		});

		TArray<TSharedPtr<FJsonValue>> BoneObjects;
		const int32 MaxBoneRows = FMath::Min(BoneNames.Num(), 24);
		for (int32 Index = 0; Index < MaxBoneRows; ++Index)
		{
			const FString& BoneName = BoneNames[Index];
			const TSharedRef<FJsonObject> BoneObject = MakeShared<FJsonObject>();
			BoneObject->SetStringField(TEXT("bone"), BoneName);
			BoneObject->SetNumberField(TEXT("weight_sum"), BoneWeightSums.FindRef(BoneName));
			BoneObject->SetNumberField(TEXT("influence_count"), BoneInfluenceCounts.FindRef(BoneName));
			BoneObject->SetNumberField(TEXT("dominant_count"), DominantBoneCounts.FindRef(BoneName));
			BoneObjects.Add(MakeShared<FJsonValueObject>(BoneObject));
		}

		MorphObject->SetNumberField(TEXT("total_delta_count"), TotalDeltas);
		MorphObject->SetNumberField(TEXT("delta_magnitude_avg"), TotalDeltas > 0 ? DeltaMagnitudeSum / static_cast<double>(TotalDeltas) : 0.0);
		MorphObject->SetNumberField(TEXT("delta_magnitude_max"), DeltaMagnitudeMax);
		if (bHasPositionBounds)
		{
			MorphObject->SetObjectField(TEXT("base_position_bounds"), ProjectMorphTargetTransfer::VectorBoundsObject(MinPosition, MaxPosition));
		}
		if (TotalDeltas > 0)
		{
			MorphObject->SetObjectField(TEXT("position_delta_bounds"), ProjectMorphTargetTransfer::VectorBoundsObject(MinDelta, MaxDelta));
		}
		MorphObject->SetArrayField(TEXT("lods"), LODObjects);
		MorphObject->SetArrayField(TEXT("top_bones"), BoneObjects);
		MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
	}

	Root->SetArrayField(TEXT("morphs"), MorphObjects);
	return ProjectMorphTargetTransfer::ToJson(Root);
}

FString UProjectMorphTargetTransferLibrary::CopyMorphTargetToMeshFilteredByBone(
	USkeletalMesh* SourceMesh,
	USkeletalMesh* TargetMesh,
	const FName SourceMorphName,
	const FName TargetMorphName,
	const TArray<FName>& RequiredBoneNameTokens,
	const float MinBoneWeight,
	const bool bOverwriteExisting)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("source_mesh"), SourceMesh ? SourceMesh->GetPathName() : FString());
	Root->SetStringField(TEXT("target_mesh"), TargetMesh ? TargetMesh->GetPathName() : FString());
	Root->SetStringField(TEXT("source_morph"), SourceMorphName.ToString());
	Root->SetStringField(TEXT("target_morph"), TargetMorphName.ToString());
	Root->SetNumberField(TEXT("min_bone_weight"), MinBoneWeight);
	Root->SetBoolField(TEXT("overwrite_existing"), bOverwriteExisting);

	const TArray<FString> RequiredTokens = ProjectMorphTargetTransfer::NameTokensToLowerStrings(RequiredBoneNameTokens);
	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("required_bone_tokens"), RequiredTokens);

	UMorphTarget* SourceMorph = ProjectMorphTargetTransfer::FindMorphTarget(SourceMesh, SourceMorphName);
	Root->SetBoolField(TEXT("source_morph_exists"), SourceMorph != nullptr);
	const bool bAlreadyExists = ProjectMorphTargetTransfer::FindMorphTarget(TargetMesh, TargetMorphName) != nullptr;
	Root->SetBoolField(TEXT("target_morph_exists_before"), bAlreadyExists);

	TSharedRef<FJsonObject> AuditObject = MakeShared<FJsonObject>();
	FString FailureReason;
	const bool bCompatible = ProjectMorphTargetTransfer::AuditMorph(SourceMesh, TargetMesh, SourceMorphName, AuditObject, FailureReason);
	Root->SetObjectField(TEXT("audit"), AuditObject);
	if (!SourceMesh || !TargetMesh || !SourceMorph || TargetMorphName.IsNone() || !bCompatible)
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("status"), TEXT("audit_failed"));
		return ProjectMorphTargetTransfer::ToJson(Root);
	}

	if (bAlreadyExists && !bOverwriteExisting)
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("status"), TEXT("target_exists"));
		return ProjectMorphTargetTransfer::ToJson(Root);
	}

	if (bAlreadyExists)
	{
		TargetMesh->RemoveMorphTargets(MakeArrayView(&TargetMorphName, 1));
	}

	int32 CopiedDeltaCount = 0;
	int32 SkippedDeltaCount = 0;
	int32 UnmappedDeltaCount = 0;
	float MaxMappingDistance = 0.0f;
	float AverageMappingDistance = 0.0f;
	TargetMesh->Modify();
	const bool bRegistered = ProjectMorphTargetTransfer::RegisterFilteredMorph(
		SourceMesh,
		TargetMesh,
		SourceMorph,
		TargetMorphName,
		RequiredTokens,
		MinBoneWeight,
		CopiedDeltaCount,
		SkippedDeltaCount,
		UnmappedDeltaCount,
		MaxMappingDistance,
		AverageMappingDistance);

	Root->SetBoolField(TEXT("registered"), bRegistered);
	Root->SetNumberField(TEXT("copied_delta_count"), CopiedDeltaCount);
	Root->SetNumberField(TEXT("skipped_delta_count"), SkippedDeltaCount);
	Root->SetNumberField(TEXT("unmapped_delta_count"), UnmappedDeltaCount);
	Root->SetNumberField(TEXT("max_mapping_distance"), MaxMappingDistance);
	Root->SetNumberField(TEXT("average_mapping_distance"), AverageMappingDistance);
	if (bRegistered)
	{
		TargetMesh->InitMorphTargets(true);
		TargetMesh->InvalidateDeriveDataCacheGUID();
		TargetMesh->MarkPackageDirty();
		if (UPackage* Package = TargetMesh->GetOutermost())
		{
			Package->MarkPackageDirty();
		}
	}

	Root->SetBoolField(TEXT("success"), bRegistered);
	Root->SetStringField(TEXT("status"), bRegistered ? TEXT("copied_filtered") : TEXT("copy_failed"));
	return ProjectMorphTargetTransfer::ToJson(Root);
}

FString UProjectMorphTargetTransferLibrary::RemoveMorphTargetsFromMesh(USkeletalMesh* TargetMesh, const TArray<FName>& MorphNames)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("target_mesh"), TargetMesh ? TargetMesh->GetPathName() : FString());
	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("requested_morphs"), MorphNames);

	bool bSuccess = TargetMesh && !MorphNames.IsEmpty();
	TArray<FName> RemovedNames;
	TArray<TSharedPtr<FJsonValue>> MorphObjects;

	if (TargetMesh)
	{
		TargetMesh->Modify();
	}

	for (const FName MorphName : MorphNames)
	{
		const TSharedRef<FJsonObject> MorphObject = MakeShared<FJsonObject>();
		MorphObject->SetStringField(TEXT("name"), MorphName.ToString());

		if (!TargetMesh || MorphName.IsNone())
		{
			MorphObject->SetStringField(TEXT("status"), TEXT("invalid_request"));
			bSuccess = false;
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		const bool bExistsBefore = ProjectMorphTargetTransfer::FindMorphTarget(TargetMesh, MorphName) != nullptr;
		MorphObject->SetBoolField(TEXT("exists_before"), bExistsBefore);
		if (!bExistsBefore)
		{
			MorphObject->SetStringField(TEXT("status"), TEXT("missing"));
			MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
			continue;
		}

		TargetMesh->RemoveMorphTargets(MakeArrayView(&MorphName, 1));

		const bool bExistsAfter = ProjectMorphTargetTransfer::FindMorphTarget(TargetMesh, MorphName) != nullptr;
		MorphObject->SetBoolField(TEXT("exists_after"), bExistsAfter);
		MorphObject->SetStringField(TEXT("status"), bExistsAfter ? TEXT("remove_failed") : TEXT("removed"));
		if (bExistsAfter)
		{
			bSuccess = false;
		}
		else
		{
			RemovedNames.Add(MorphName);
		}
		MorphObjects.Add(MakeShared<FJsonValueObject>(MorphObject));
	}

	if (TargetMesh && !RemovedNames.IsEmpty())
	{
		TargetMesh->InitMorphTargets(true);
		TargetMesh->InvalidateDeriveDataCacheGUID();
		TargetMesh->MarkPackageDirty();
		if (UPackage* Package = TargetMesh->GetOutermost())
		{
			Package->MarkPackageDirty();
		}
	}

	ProjectMorphTargetTransfer::AddStringArray(Root, TEXT("removed_morphs"), RemovedNames);
	Root->SetNumberField(TEXT("removed_count"), RemovedNames.Num());
	Root->SetArrayField(TEXT("morphs"), MorphObjects);
	Root->SetBoolField(TEXT("success"), bSuccess);
	return ProjectMorphTargetTransfer::ToJson(Root);
}
