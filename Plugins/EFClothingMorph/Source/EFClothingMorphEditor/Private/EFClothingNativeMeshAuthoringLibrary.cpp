#include "EFClothingNativeMeshAuthoringLibrary.h"

#include "AssetCompilingManager.h"
#include "Animation/MorphTarget.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingSkeletonFingerprint.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "SkeletalMeshEditorSubsystem.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingNativeMeshAuthoring, Log, All);

namespace
{
	TSharedRef<FJsonObject> EstimateWitnessWork(const FSkeletalMeshLODRenderData& LOD)
	{
		auto Result = MakeShared<FJsonObject>();
		TArray<uint32> Indices;
		LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
		const int32 Vertices = LOD.GetNumVertices();
		if (Vertices <= 0 || Indices.IsEmpty() || Indices.Num() % 3 != 0
			|| Indices.ContainsByPredicate([Vertices](uint32 Index) { return Index >= uint32(Vertices); }))
		{
			Result->SetStringField(TEXT("error"), TEXT("Invalid render topology in transient probe."));
			return Result;
		}
		Result->SetNumberField(TEXT("vertices"), Vertices);
		Result->SetNumberField(TEXT("triangles"), Indices.Num() / 3);
		TArray<TSharedPtr<FJsonValue>> SpacingRows;
		for (const double Spacing : {0.5, 0.75, 1.0})
		{
			TArray<int64> Incidence;
			Incidence.Init(0, Vertices);
			int64 Witnesses = 0;
			int32 OverSubdivisionLimit = 0;
			for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
			{
				const FVector3d A(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I]));
				const FVector3d B(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I + 1]));
				const FVector3d C(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I + 2]));
				const double Edge = FMath::Max3((B - A).Length(), (C - B).Length(), (A - C).Length());
				if (A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN() || !FMath::IsFinite(Edge)
					|| Edge / Spacing > 4096.0)
				{
					Result->SetStringField(TEXT("error"), TEXT("Invalid or unsupported edge length in transient probe."));
					return Result;
				}
				const int64 N = FMath::Max<int64>(1, FMath::CeilToInt64(Edge / Spacing));
				OverSubdivisionLimit += N > 32 ? 1 : 0;
				// The compiler's barycentric grid excludes corners; a vertex only
				// references samples with a positive weight for that vertex.
				Witnesses += N == 1 ? 1 : (N + 1) * (N + 2) / 2 - 3;
				const int64 References = N == 1 ? 1 : N * (N + 1) / 2 - 1;
				for (int32 Corner = 0; Corner < 3; ++Corner) Incidence[Indices[I + Corner]] += References;
			}
			int64 Maximum = 0, Total = 0;
			for (const int64 Count : Incidence) { Maximum = FMath::Max(Maximum, Count); Total += Count; }
			auto Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("spacing_cm"), Spacing);
			Row->SetNumberField(TEXT("witness_count_upper_bound"), Witnesses);
			Row->SetNumberField(TEXT("maximum_incidence_upper_bound"), Maximum);
			Row->SetNumberField(TEXT("mean_incidence_upper_bound"), Vertices ? double(Total) / Vertices : 0);
			Row->SetNumberField(TEXT("triangles_over_subdivision_limit"), OverSubdivisionLimit);
			Row->SetBoolField(TEXT("fits_vertex_times_16_plane_capacity"), Witnesses <= int64(Vertices) * 16);
			SpacingRows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetArrayField(TEXT("spacing_estimates"), SpacingRows);
		return Result;
	}
}

namespace EFClothingNativeMeshAuthoringPrivate
{
	struct FResolvedGarment
	{
		FEFClothingGarmentRow* Row = nullptr;
		USkeletalMesh* Mesh = nullptr;
	};

	static bool ResolveGarment(
		UEFClothingMorphDirectorPolicy* Director,
		const FName GarmentId,
		FResolvedGarment& OutGarment,
		FString& OutReport)
	{
		OutGarment = {};
		OutReport.Reset();
		if (!IsValid(Director))
		{
			OutReport = TEXT("The EF Clothing Morph Director is invalid.");
			return false;
		}
		if (GarmentId.IsNone())
		{
			OutReport = TEXT("Select a garment entry with a valid Garment ID.");
			return false;
		}

		for (FEFClothingGarmentRow& Candidate : Director->Garments)
		{
			if (Candidate.GarmentId == GarmentId)
			{
				if (OutGarment.Row != nullptr)
				{
					OutReport = FString::Printf(
						TEXT("Garment ID '%s' is duplicated in the Director."),
						*GarmentId.ToString());
					return false;
				}
				OutGarment.Row = &Candidate;
			}
		}
		if (OutGarment.Row == nullptr)
		{
			OutReport = FString::Printf(
				TEXT("Garment ID '%s' was not found in the Director."),
				*GarmentId.ToString());
			return false;
		}

		OutGarment.Mesh = OutGarment.Row->SourceGarment.LoadSynchronous();
		USkeletalMesh* RowBody = OutGarment.Row->BodySurface.LoadSynchronous();
		if (!IsValid(OutGarment.Mesh) || !IsValid(RowBody))
		{
			OutReport = TEXT("The garment's Editable Garment Mesh or Reference Body could not be loaded.");
			return false;
		}
		if (OutGarment.Mesh == RowBody)
		{
			OutReport = TEXT("The editable garment and Reference Body must be different assets.");
			return false;
		}

		// Body surfaces are read-only inputs even if another row accidentally points
		// at one of them as its garment. This rule is data-driven, not name-driven.
		for (const FEFClothingGarmentRow& Candidate : Director->Garments)
		{
			if (Candidate.BodySurface.ToSoftObjectPath()
				== FSoftObjectPath(OutGarment.Mesh))
			{
				OutReport = FString::Printf(
					TEXT("%s is configured as a Reference Body and cannot be modified by a garment authoring action."),
					*OutGarment.Mesh->GetPathName());
				return false;
			}
		}

		const FString PackageName = OutGarment.Mesh->GetOutermost()->GetName();
		if (!PackageName.StartsWith(TEXT("/Game/")))
		{
			OutReport = FString::Printf(
				TEXT("Native garment authoring is restricted to project Content assets. Refused: %s"),
				*PackageName);
			return false;
		}
		if (!IsValid(OutGarment.Mesh->GetSkeleton())
			|| !OutGarment.Mesh->GetImportedModel()
			|| OutGarment.Mesh->GetImportedModel()->LODModels.IsEmpty())
		{
			OutReport = TEXT("The editable garment has no valid Skeleton or imported LOD0 source data.");
			return false;
		}
		return true;
	}

	static void CollectMorphNames(const USkeletalMesh* Mesh, TArray<FName>& OutNames)
	{
		OutNames.Reset();
		if (!IsValid(Mesh))
		{
			return;
		}
		for (const UMorphTarget* MorphTarget : Mesh->GetMorphTargets())
		{
			if (IsValid(MorphTarget))
			{
				OutNames.Add(MorphTarget->GetFName());
			}
		}
		OutNames.Sort(FNameLexicalLess());
	}

	static bool DynamicMeshPreservesSkinning(const UDynamicMesh* DynamicMesh)
	{
		bool bHasBones = false;
		bool bHasSkinWeights = false;
		if (!IsValid(DynamicMesh))
		{
			return false;
		}
		DynamicMesh->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
		{
			const UE::Geometry::FDynamicMeshAttributeSet* Attributes =
				Mesh.HasAttributes() ? Mesh.Attributes() : nullptr;
			bHasBones = Attributes && Attributes->HasBones();
			bHasSkinWeights = Attributes
				&& !Attributes->GetSkinWeightsAttributes().IsEmpty();
		});
		return bHasBones && bHasSkinWeights;
	}

	static bool UndoUnsafeWrite(const FString& Reason, FString& OutReport)
	{
		const bool bUndone = GEditor && GEditor->UndoTransaction();
		OutReport = FString::Printf(
			TEXT("%s The native edit was %s."),
			*Reason,
			bUndone ? TEXT("rolled back with Undo") : TEXT("not safely undoable; close the asset without saving"));
		return false;
	}

	static bool ApplyGeometryOperation(
		FResolvedGarment& Garment,
		const bool bCreateShell,
		FString& OutReport)
	{
		check(Garment.Row && IsValid(Garment.Mesh));
		USkeletalMesh* Mesh = Garment.Mesh;
		FEFClothingGarmentRow& Row = *Garment.Row;

		const float DistanceCm = bCreateShell
			? Row.ShellThicknessCm
			: Row.NativeUEOffset.DistanceCm;
		if (!FMath::IsFinite(DistanceCm) || FMath::IsNearlyZero(DistanceCm, 1.0e-5f))
		{
			OutReport = bCreateShell
				? TEXT("Surface Inflate must be greater than zero before creating a real native shell.")
				: TEXT("Native UE Offset Distance is zero; the editable mesh was not changed.");
			return false;
		}
		if (bCreateShell && DistanceCm < 0.0f)
		{
			OutReport = TEXT("Create Shell requires a positive thickness.");
			return false;
		}
		if (bCreateShell && !Mesh->GetMorphTargets().IsEmpty())
		{
			OutReport = TEXT("Create Shell was refused because this garment has morph targets. Use Unreal's native editor manually so the topology-dependent morph data can be reviewed.");
			return false;
		}
		if (bCreateShell && !Mesh->GetMeshClothingAssets().IsEmpty())
		{
			OutReport = TEXT("Create Shell was refused because this garment has Chaos clothing data. Use Unreal's native editor manually and rebuild the cloth data after changing topology.");
			return false;
		}

		USkeleton* const OriginalSkeleton = Mesh->GetSkeleton();
		const FString OriginalMeshSkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(Mesh);
		const FString OriginalSharedSkeletonFingerprint =
			EFClothingSkeleton::BuildSharedSkeletonFingerprint(OriginalSkeleton);
		const uint32 OriginalVertexCount = Mesh->GetImportedModel()->LODModels[0].NumVertices;
		const int32 OriginalIndexCount = Mesh->GetImportedModel()->LODModels[0].IndexBuffer.Num();
		const int32 OriginalSkinWeightProfileCount = Mesh->GetNumSkinWeightProfiles();
		TArray<FName> OriginalMorphNames;
		CollectMorphNames(Mesh, OriginalMorphNames);

		UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
		FGeometryScriptCopyMeshFromAssetOptions ReadOptions;
		ReadOptions.bApplyBuildSettings = false;
		ReadOptions.bRequestTangents = true;
		ReadOptions.bIgnoreRemoveDegenerates = true;
		ReadOptions.bUseBuildScale = true;
		FGeometryScriptMeshReadLOD ReadLOD;
		ReadLOD.LODType = EGeometryScriptLODType::SourceModel;
		ReadLOD.LODIndex = 0;
		EGeometryScriptOutcomePins ReadOutcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromSkeletalMesh(
			Mesh,
			DynamicMesh,
			ReadOptions,
			ReadLOD,
			ReadOutcome);
		if (ReadOutcome != EGeometryScriptOutcomePins::Success
			|| !DynamicMeshPreservesSkinning(DynamicMesh))
		{
			OutReport = TEXT("Unreal could not copy LOD0 with its bone hierarchy and skin weights intact. No change was made.");
			return false;
		}

		FGeometryScriptMeshOffsetOptions OffsetOptions;
		OffsetOptions.OffsetDistance = DistanceCm;
		OffsetOptions.SolveSteps = FMath::Clamp(Row.NativeUEOffset.Steps, 1, 100);
		OffsetOptions.bFixedBoundary = !Row.NativeUEOffset.bOffsetBoundaries;
		OffsetOptions.SmoothAlpha = FMath::Clamp(Row.NativeUEOffset.SmoothingPerStep, 0.0f, 0.9f);
		OffsetOptions.bReprojectDuringSmoothing = Row.NativeUEOffset.bReprojectAfterSmoothing;
		if (bCreateShell)
		{
			UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshShell(
				DynamicMesh,
				OffsetOptions);
		}
		else
		{
			UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshOffset(
				DynamicMesh,
				OffsetOptions);
		}
		if (!DynamicMeshPreservesSkinning(DynamicMesh))
		{
			OutReport = TEXT("The native geometry operation did not preserve bone/skin-weight attributes. No source asset was written.");
			return false;
		}

		FGeometryScriptCopyMeshToAssetOptions WriteOptions;
		WriteOptions.BoneHierarchyMismatchHandling =
			EGeometryScriptBoneHierarchyMismatchHandling::DoNothing;
		WriteOptions.bUseOriginalVertexOrder = !bCreateShell;
		WriteOptions.bUseBuildScale = true;
		WriteOptions.bReplaceMaterials = false;
		WriteOptions.bCleanAssignedMaterials = false;
		WriteOptions.bEnableRecomputeNormals = false;
		WriteOptions.bEnableRecomputeTangents = false;
		WriteOptions.bEmitTransaction = true;
		WriteOptions.bDeferMeshPostEditChange = false;
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.bWriteHiResSource = false;
		WriteLOD.LODIndex = 0;
		EGeometryScriptOutcomePins WriteOutcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToSkeletalMesh(
			DynamicMesh,
			Mesh,
			WriteOptions,
			WriteLOD,
			WriteOutcome);
		if (WriteOutcome != EGeometryScriptOutcomePins::Success)
		{
			return UndoUnsafeWrite(
				TEXT("Unreal failed while committing the native Skeletal Mesh edit."),
				OutReport);
		}

		FAssetCompilingManager::Get().FinishAllCompilation();
		const FSkeletalMeshModel* UpdatedModel = Mesh->GetImportedModel();
		TArray<FName> UpdatedMorphNames;
		CollectMorphNames(Mesh, UpdatedMorphNames);
		const bool bSkeletonSafe = Mesh->GetSkeleton() == OriginalSkeleton
			&& EFClothingSkeleton::BuildFingerprint(Mesh) == OriginalMeshSkeletonFingerprint
			&& EFClothingSkeleton::BuildSharedSkeletonFingerprint(OriginalSkeleton)
				== OriginalSharedSkeletonFingerprint;
		const bool bDependenciesSafe = Mesh->GetNumSkinWeightProfiles()
				== OriginalSkinWeightProfileCount
			&& UpdatedMorphNames == OriginalMorphNames;
		const bool bTopologySafe = bCreateShell
			? UpdatedModel && !UpdatedModel->LODModels.IsEmpty()
				&& UpdatedModel->LODModels[0].NumVertices > OriginalVertexCount
				&& UpdatedModel->LODModels[0].IndexBuffer.Num() > OriginalIndexCount
			: UpdatedModel && !UpdatedModel->LODModels.IsEmpty()
				&& UpdatedModel->LODModels[0].NumVertices == OriginalVertexCount
				&& UpdatedModel->LODModels[0].IndexBuffer.Num() == OriginalIndexCount;
		if (!bSkeletonSafe || !bDependenciesSafe || !bTopologySafe)
		{
			return UndoUnsafeWrite(
				TEXT("The post-edit integrity check detected an unexpected skeleton, dependency or topology change."),
				OutReport);
		}

		Mesh->MarkPackageDirty();
		OutReport = FString::Printf(
			TEXT("Applied %s %.3f cm to %s LOD0. The exact source mesh remains active, the edit is undoable, and the asset was not auto-saved. Refresh the V3 binding before Play."),
			bCreateShell ? TEXT("a native shell of") : TEXT("a native iterative offset of"),
			DistanceCm,
			*Mesh->GetPathName());
		UE_LOG(LogEFClothingNativeMeshAuthoring, Display, TEXT("%s"), *OutReport);
		return true;
	}
}

bool UEFClothingNativeMeshAuthoringLibrary::OpenEditableMesh(
	UEFClothingMorphDirectorPolicy* Director,
	const FName GarmentId,
	FString& OutReport)
{
	using namespace EFClothingNativeMeshAuthoringPrivate;
	FResolvedGarment Garment;
	if (!ResolveGarment(Director, GarmentId, Garment, OutReport))
	{
		return false;
	}
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor
		? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
		: nullptr;
	if (!IsValid(AssetEditorSubsystem)
		|| !AssetEditorSubsystem->OpenEditorForAsset(Garment.Mesh))
	{
		OutReport = TEXT("Unreal could not open the editable garment in the Skeletal Mesh Editor.");
		return false;
	}
	OutReport = FString::Printf(TEXT("Opened %s in the native Skeletal Mesh Editor."), *Garment.Mesh->GetPathName());
	return true;
}

bool UEFClothingNativeMeshAuthoringLibrary::ApplyNativeOffsetToEditableMesh(
	UEFClothingMorphDirectorPolicy* Director,
	const FName GarmentId,
	FString& OutReport)
{
	using namespace EFClothingNativeMeshAuthoringPrivate;
	FResolvedGarment Garment;
	return ResolveGarment(Director, GarmentId, Garment, OutReport)
		&& ApplyGeometryOperation(Garment, false, OutReport);
}

bool UEFClothingNativeMeshAuthoringLibrary::CreateShellOnEditableMesh(
	UEFClothingMorphDirectorPolicy* Director,
	const FName GarmentId,
	FString& OutReport)
{
	using namespace EFClothingNativeMeshAuthoringPrivate;
	FResolvedGarment Garment;
	return ResolveGarment(Director, GarmentId, Garment, OutReport)
		&& ApplyGeometryOperation(Garment, true, OutReport);
}

bool UEFClothingNativeMeshAuthoringLibrary::InspectReducedGarmentLOD(
	UEFClothingMorphDirectorPolicy* Director, FName GarmentId, float TriangleRatio, FString& OutReport)
{
	using namespace EFClothingNativeMeshAuthoringPrivate;
	FResolvedGarment Garment;
	if (!ResolveGarment(Director, GarmentId, Garment, OutReport)) return false;
	if (!GEditor || GEditor->PlayWorld || !FMath::IsFinite(TriangleRatio)
		|| TriangleRatio < 0.1f || TriangleRatio >= 1.0f)
	{
		OutReport = TEXT("LOD probe requires an idle editor and a triangle ratio in [0.1, 1).");
		return false;
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	auto* Source = Garment.Mesh;
	auto* Description = Source->GetMeshDescription(0);
	if (!Description || Source->GetNumSourceModels() != 1)
	{
		OutReport = TEXT("LOD probe expects a single imported LOD; existing authored LODs are not replaced.");
		return false;
	}
	const double Started = FPlatformTime::Seconds();
	const bool bSourceDirty = Source->GetOutermost()->IsDirty();
	FBufferArchive Before;
	Description->Serialize(Before);
	TStrongObjectPtr<USkeletalMesh> Probe(DuplicateObject<USkeletalMesh>(Source, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), USkeletalMesh::StaticClass(), TEXT("EFClothingLODProbe"))));
	if (!Probe.IsValid()) { OutReport = TEXT("Could not duplicate the transient probe mesh."); return false; }
	Probe->ClearFlags(RF_Public | RF_Standalone);
	Probe->SetFlags(RF_Transient);
	const FSkeletalMeshLODInfo BaseInfo = *Probe->GetLODInfo(0);
	static_cast<FSkeletalMeshLODInfo&>(Probe->AddSourceModel(true)) = BaseInfo;
	auto& Info = *Probe->GetLODInfo(1);
	Info.ReductionSettings.BaseLOD = 0;
	Info.ReductionSettings.NumOfTrianglesPercentage = TriangleRatio;
	Info.ReductionSettings.TerminationCriterion = SMTC_NumOfTriangles;
	Info.ReductionSettings.bLockEdges = true;
	Info.ReductionSettings.bRemapMorphTargets = true;
	Info.ReductionSettings.bEnforceBoneBoundaries = true;
	Info.bAllowMeshDeformer = true;
	const bool bGenerated = USkeletalMeshEditorSubsystem::RegenerateLOD(Probe.Get(), 2, false, false);
	FAssetCompilingManager::Get().FinishAllCompilation();
	FBufferArchive After;
	if (auto* Current = Source->GetMeshDescription(0)) Current->Serialize(After);
	if (!bGenerated || Before != After || Source->GetNumSourceModels() != 1
		|| Source->GetOutermost()->IsDirty() != bSourceDirty)
	{
		OutReport = TEXT("Transient LOD generation failed or a source invariant changed; no asset was saved.");
		return false;
	}
	const auto* RenderData = Probe->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.Num() != 2)
	{
		OutReport = TEXT("Transient probe did not produce two render LODs.");
		return false;
	}
	auto Base = EstimateWitnessWork(RenderData->LODRenderData[0]);
	auto Reduced = EstimateWitnessWork(RenderData->LODRenderData[1]);
	auto Result = MakeShared<FJsonObject>();
	const bool bValid = !Base->HasField(TEXT("error")) && !Reduced->HasField(TEXT("error"));
	Result->SetStringField(TEXT("status"), bValid ? TEXT("PASS") : TEXT("FAIL"));
	Result->SetStringField(TEXT("source"), Source->GetPathName());
	Result->SetStringField(TEXT("garment"), GarmentId.ToString());
	Result->SetStringField(TEXT("scope"), TEXT("Transient geometry only. Witness estimates precede body/PreserveUpstream exclusions; no binding certification or runtime capacity claim. Alternative spacings are estimates, not a compiler policy change."));
	Result->SetNumberField(TEXT("triangle_ratio"), TriangleRatio);
	Result->SetObjectField(TEXT("lod0"), Base);
	Result->SetObjectField(TEXT("lod1"), Reduced);
	Result->SetBoolField(TEXT("source_description_unchanged"), true);
	Result->SetBoolField(TEXT("saved_assets"), false);
	Result->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Started);
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&OutReport));
	return bValid;
}
