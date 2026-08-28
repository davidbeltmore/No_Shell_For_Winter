#include "EFClothingFitCompilerLibrary.h"

#include "GameplayTagsManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Animation/MorphTarget.h"
#include "Algo/Unique.h"
#include "Async/ParallelFor.h"
#include "BoneWeights.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshIndexMappings.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "DynamicMeshEditor.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "EFClothingSurfaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "MeshBoundaryLoops.h"
#include "MeshQueries.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Operations/SelectiveTessellate.h"
#include "Operations/JoinMeshLoops.h"
#include "Rendering/SkinWeightProfile.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "SkinnedAssetCompiler.h"
#include "Spatial/MeshAABBTree3.h"
#include "Templates/Greater.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingFitCompiler, Log, All);

namespace EFClothingFitCompilerPrivate
{
	using namespace UE::Geometry;

	static constexpr int32 CompilerVersion = EFClothingMorphV26::CompilerVersion;
	static constexpr double CompilerClearanceReserveCm = 0.10;
	static constexpr int32 CertifiedClearanceTierCount = 9;
	static constexpr double CertifiedClearanceTierMin = 1.0;
	static constexpr double CertifiedClearanceTierMax = 2.0;
	// The generated runtime topology itself must obey the same 0.5 cm spacing as
	// the witness lattice. A looser chord bound left a small set of crotch/pelvis
	// faces connecting oppositely oriented skin patches even though every corner
	// was vertex-safe. Red-green tessellation keeps this conforming and the hard
	// derived-vertex cap below remains the fail-closed guard for unsuitable input.
	static constexpr double SurfaceRuntimeMaximumEdgeLengthCm = 0.50;
	static constexpr int32 SurfaceRuntimeMaximumDerivedVertexCount = 65535;
	static constexpr int32 ThicknessShellAlgorithmVersion = 4;
	// Explicitly excluded DAZ anatomy can leave a very small inherited overlap in
	// an otherwise closed shell. Keep the exception bounded, measurable and tied
	// to catalog-authored surface + bone exclusions; ordinary garments remain at 0.
	static constexpr double MaximumExcludedAnatomyShellIntersectionFraction = 0.01;
	static const FName FitWeightProfileName(TEXT("EF_AutoFit"));
	static const FName ClearanceMorphName(TEXT("EF_AutoFit_Clearance"));

	struct FSurfaceCorrespondence
	{
		int32 BodyTriangle = INDEX_NONE;
		FVector3d Barycentric = FVector3d::Zero();
		FVector3d SurfaceNormal = FVector3d::UnitZ();
		FVector3d ClosestPoint = FVector3d::Zero();
		double SignedGap = 0.0;
	};

	struct FMorphCandidate
	{
		FName Name = NAME_None;
		double MaxDelta = 0.0;
		bool bTransferred = false;
	};

	struct FCompiledMorphKey
	{
		double BodyValue = 0.0;
		FName GarmentMorph = NAME_None;
		bool bIdentity = false;
		bool bStepFromPrevious = false;
		double StepSwitchBodyValue = 0.0;
		double MinimumClearanceMultiplier = CertifiedClearanceTierMin;
			TArray<FVector3d> MorphDeltas;
	};

	/** In-memory pairing that keeps both geometric shell layers deformation-identical. */
	struct FThicknessShellCompileData
	{
		bool bEnabled = false;
		int32 PreShellVertexCount = 0;
		int32 PreShellTriangleCount = 0;
		int32 FinalShellVertexCount = 0;
		int32 FinalShellTriangleCount = 0;
		int32 BoundaryLoopCount = 0;
		int32 WallTriangleCount = 0;
		int32 OpenBoundaryCountAfter = 0;
		int32 DegenerateTriangleCount = 0;
		int32 DetectedNonAdjacentIntersectionCount = 0;
		int32 BaselineSourceIntersectionPairCount = 0;
		int32 ToleratedInheritedSourceIntersectionCount = 0;
		double BaselineInheritanceRadiusCm = 0.0;
		int32 ToleratedLocalRepairIntersectionCount = 0;
		double LocalRepairThicknessCeilingCm = 0.0;
		int32 ToleratedExcludedRegionIntersectionCount = 0;
		int32 ExcludedRegionAffectedSourceTriangleCount = 0;
		double ExcludedRegionCertificationRadiusCm = 0.0;
		double ExcludedRegionMaximumWitnessDistanceCm = 0.0;
		bool bSelfIntersects = false;
		double RequestedThicknessCm = 0.0;
		double MinimumMeasuredThicknessCm = 0.0;
		double AverageMeasuredThicknessCm = 0.0;
		double MaximumMeasuredThicknessCm = 0.0;
		TArray<int32> SourceVertexIDByOrdinal;
		TArray<int32> SourceOrdinalByVertex;
		TArray<uint8> LayerByVertex;
		TArray<TArray<int32>> OuterVerticesBySourceOrdinal;
		TArray<TArray<int32>> InnerVerticesBySourceOrdinal;
	};

	static double MinimumCertifiedThicknessPointCm(const double RequestedThicknessCm)
	{
		// Prevent numerical layer collapse without forcing a visibly different shape
		// through concave/excluded anatomy. The requested thickness remains the normal
		// result; this micro-floor is used only by local collision contraction.
		return FMath::Max(1.e-5, RequestedThicknessCm * 0.001);
	}

	static FString SanitizeAssetName(const FString& Name)
	{
		FString Sanitized = Name;
		for (TCHAR& Character : Sanitized)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				Character = TEXT('_');
			}
		}
		return Sanitized;
	}

	static bool IsAllowedOutputRoot(const FString& Root)
	{
		static const FString AllowedRoot(TEXT("/EFClothingMorph/_Internal/Compiled/V26"));
		return (Root == AllowedRoot || Root.StartsWith(AllowedRoot + TEXT("/"), ESearchCase::CaseSensitive))
			&& !Root.Contains(TEXT(".."));
	}

	static FString BuildSourceKey(const USkeletalMesh* SourceGarment)
	{
		return FString::Printf(TEXT("%08X"), FCrc::StrCrc32(*SourceGarment->GetPathName()));
	}

	static FString BuildArtifactKey(
		const USkeletalMesh* SourceGarment,
		const USkeletalMesh* BodySurface,
		const USkeletalMesh* CompatibilityReference,
		const FEFClothingFitCompileOptions& Options,
		const FEFClothingGarmentRow& CatalogRow,
		const TArray<FName>& ExcludedBodySurfaceMaterialSlots,
		const TArray<FName>& ExcludedBodyBoneBranches,
		const TArray<FString>& ExcludedBodyMorphPrefixes)
	{
		TArray<FString> PairKeys;
		for (const FEFClothingMorphPairCompileRequest& Request : Options.MorphPairRequests)
		{
			FName First = Request.FirstBodyMorph;
			FName Second = Request.SecondBodyMorph;
			if (Second.LexicalLess(First))
			{
				Swap(First, Second);
			}
			PairKeys.Add(First.ToString() + TEXT("+") + Second.ToString());
		}
		PairKeys.Sort();
		const FString PairSignature = FString::Join(PairKeys, TEXT(","));
		TArray<FString> ExcludedSurfaceSlots;
		ExcludedSurfaceSlots.Reserve(ExcludedBodySurfaceMaterialSlots.Num());
		for (const FName SlotName : ExcludedBodySurfaceMaterialSlots)
		{
			ExcludedSurfaceSlots.Add(SlotName.ToString());
		}
		ExcludedSurfaceSlots.Sort();
		const FString ExcludedSurfaceSignature = FString::Join(ExcludedSurfaceSlots, TEXT(","));
		TArray<FString> ExcludedBoneRoots;
		ExcludedBoneRoots.Reserve(ExcludedBodyBoneBranches.Num());
		for (const FName BoneName : ExcludedBodyBoneBranches)
		{
			ExcludedBoneRoots.Add(BoneName.ToString());
		}
		ExcludedBoneRoots.Sort();
		const FString ExcludedBoneSignature = FString::Join(ExcludedBoneRoots, TEXT(","));
		TArray<FString> ExcludedMorphPrefixes = ExcludedBodyMorphPrefixes;
		ExcludedMorphPrefixes.Sort();
		const FString ExcludedMorphSignature = FString::Join(ExcludedMorphPrefixes, TEXT(","));
		TArray<FGameplayTag> CoverageTags;
		CatalogRow.CoverageTags.GetGameplayTagArray(CoverageTags);
		CoverageTags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		});
		TArray<FString> CoverageTagStrings;
		CoverageTagStrings.Reserve(CoverageTags.Num());
		for (const FGameplayTag& CoverageTag : CoverageTags)
		{
			CoverageTagStrings.Add(CoverageTag.ToString());
		}
		const FString CoverageSignature = FString::Join(CoverageTagStrings, TEXT(","));
		const FString Canonical = FString::Printf(
			TEXT("V=%d|S=%s:%s|B=%s:%s|C=%s:%s|Backend=%d|FitPolicy=%d|CatalogFingerprint=%s|Coverage=%s|Fabric=%.6f|MaxCorrection=%.6f|FailClosedLOD=%d|ExcludedSurface=%s|ExcludedBones=%s|ExcludedMorphs=%s|Clearance=%.6f|Push=%.6f|Smooth=%d|Influences=%d|CompileMorphs=%d|Transfer=%d|MaxMorphs=%d|MinMorph=%.6f|MorphSamples=%d|MorphRepair=%.6f|Pairs=%s|PairGrid=%d|PairProbes=%d|PairEpsilon=%.9f|Deformer=%d"),
			CompilerVersion,
			*SourceGarment->GetPathName(), *EFClothingSkeleton::BuildContentFingerprint(SourceGarment),
			*BodySurface->GetPathName(), *EFClothingSkeleton::BuildContentFingerprint(BodySurface),
			*CompatibilityReference->GetPathName(), *EFClothingSkeleton::BuildContentFingerprint(CompatibilityReference),
			static_cast<int32>(CatalogRow.Backend),
			static_cast<int32>(CatalogRow.FitPolicy),
			*CatalogRow.BuildCompileFingerprint(),
			*CoverageSignature,
			CatalogRow.FabricClearanceCm,
			CatalogRow.MaximumCorrectionCm,
			CatalogRow.bFailClosedOnMissingLOD ? 1 : 0,
			*ExcludedSurfaceSignature,
			*ExcludedBoneSignature,
			*ExcludedMorphSignature,
			Options.MinimumClearanceCm,
			Options.MaximumPushCm,
			Options.SmoothingIterations,
			Options.MaximumInfluences,
			Options.bCompileBodyMorphBindings ? 1 : 0,
			Options.bTransferMissingBodyMorphs ? 1 : 0,
			Options.MaximumTransferredMorphs,
			Options.MinimumTransferredMorphDeltaCm,
			Options.MorphClearanceSampleCount,
			Options.MaximumMorphRepairCm,
			*PairSignature,
			Options.MorphPairGridResolution,
			Options.MorphPairProbeCountPerAxis,
			Options.MorphActivationEpsilon,
			Options.bCopyBodyDeformerToDerived ? 1 : 0);
		// AdditionalClearanceCm is authoring/runtime tuning and is deliberately
		// absent from this canonical key. Offset-only edits must not invalidate
		// compiled geometry, weights or surface bindings.
		return FMD5::HashAnsiString(*Canonical).Left(12);
	}

	static void CanonicalizeMaterialSlotNames(TArray<FName>& SlotNames)
	{
		SlotNames.Remove(NAME_None);
		SlotNames.Sort(FNameLexicalLess());
		SlotNames.SetNum(Algo::Unique(SlotNames));
	}

	static void CanonicalizeBoneBranchNames(TArray<FName>& BoneNames)
	{
		BoneNames.Remove(NAME_None);
		BoneNames.Sort(FNameLexicalLess());
		BoneNames.SetNum(Algo::Unique(BoneNames));
	}

	static void CanonicalizeMorphPrefixes(TArray<FString>& Prefixes)
	{
		Prefixes.RemoveAll([](const FString& Prefix) { return Prefix.IsEmpty(); });
		Prefixes.Sort();
		Prefixes.SetNum(Algo::Unique(Prefixes));
	}

	static bool ResolveCatalogSurfacePolicy(
		const USkeletalMesh* SourceGarment,
		const USkeletalMesh* BodySurface,
		TArray<FName>& OutExcludedBodySurfaceMaterialSlots,
		TArray<FName>& OutExcludedBodyBoneBranches,
		TArray<FString>& OutExcludedBodyMorphPrefixes,
		FEFClothingGarmentRow& OutCatalogRow,
		FName& OutCatalogRowName,
		FString& OutError)
	{
		OutExcludedBodySurfaceMaterialSlots.Reset();
		OutExcludedBodyBoneBranches.Reset();
		OutExcludedBodyMorphPrefixes.Reset();
		OutCatalogRow = FEFClothingGarmentRow();
		OutCatalogRowName = NAME_None;
		const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
		if (!Settings || Settings->DirectorPolicy.IsNull())
		{
			OutError = TEXT("EF Clothing Morph V26 requires a configured Clothing Director; implicit garment compilation is disabled.");
			return false;
		}

		UEFClothingMorphDirectorPolicy* Director = Settings->DirectorPolicy.LoadSynchronous();
		if (!IsValid(Director))
		{
			OutError = FString::Printf(
				TEXT("Configured Clothing Director could not be loaded: %s"),
				*Settings->DirectorPolicy.ToSoftObjectPath().ToString());
			return false;
		}
		FString DirectorValidationError;
		if (!Director->ValidatePolicy(DirectorValidationError))
		{
			OutError = FString::Printf(
				TEXT("Configured Clothing Director %s is invalid: %s"),
				*Director->GetPathName(),
				*DirectorValidationError);
			return false;
		}

		const FSoftObjectPath SourcePath(SourceGarment);
		const FSoftObjectPath BodyPath(BodySurface);
		const FEFClothingGarmentRow* MatchedRow = nullptr;
		TSet<FName> UniqueGarmentIds;
		TSet<FString> UniqueSourceBodyKeys;
		for (const FEFClothingGarmentRow& Row : Director->Garments)
		{
			if (Row.IsDisabledEmptyPlaceholder())
			{
				continue;
			}
			if (Row.GarmentId.IsNone())
			{
				OutError = TEXT("Configured Clothing Director contains a garment with an empty GarmentId.");
				return false;
			}
			if (UniqueGarmentIds.Contains(Row.GarmentId))
			{
				OutError = FString::Printf(
					TEXT("Configured Clothing Director contains duplicate GarmentId %s."),
					*Row.GarmentId.ToString());
				return false;
			}
			UniqueGarmentIds.Add(Row.GarmentId);
			if (!Row.bEnabled)
			{
				continue;
			}

			const FSoftObjectPath RowSourcePath = Row.SourceGarment.ToSoftObjectPath();
			const FSoftObjectPath RowBodyPath = Row.BodySurface.ToSoftObjectPath();
			if (RowSourcePath.IsNull() || RowBodyPath.IsNull())
			{
				continue;
			}
			const FString SourceBodyKey = RowSourcePath.ToString() + TEXT("|") + RowBodyPath.ToString();
			if (UniqueSourceBodyKeys.Contains(SourceBodyKey))
			{
				OutError = FString::Printf(
					TEXT("Configured Clothing Director contains a duplicate source/body pair at GarmentId %s."),
					*Row.GarmentId.ToString());
				return false;
			}
			UniqueSourceBodyKeys.Add(SourceBodyKey);

			if (RowSourcePath != SourcePath || RowBodyPath != BodyPath)
			{
				continue;
			}
			if (MatchedRow)
			{
				OutError = FString::Printf(
					TEXT("Configured Clothing Director contains duplicate source/body garments %s and %s."),
					*OutCatalogRowName.ToString(),
					*Row.GarmentId.ToString());
				return false;
			}
			MatchedRow = &Row;
			OutCatalogRowName = Row.GarmentId;
		}

		if (!MatchedRow)
		{
			OutError = FString::Printf(
				TEXT("Configured Clothing Director has no garment for source %s and body %s."),
				*SourcePath.ToString(),
				*BodyPath.ToString());
			return false;
		}
		if (!MatchedRow->bEnabled
			|| MatchedRow->Backend == EEFClothingSurfaceBackend::Disabled)
		{
			OutError = FString::Printf(
				TEXT("Clothing Director garment %s is disabled or requests an unavailable backend (%d)."),
				*OutCatalogRowName.ToString(),
				static_cast<int32>(MatchedRow->Backend));
			return false;
		}
		if (!FMath::IsFinite(MatchedRow->MinimumClearanceMultiplier)
			|| MatchedRow->MinimumClearanceMultiplier < static_cast<float>(CertifiedClearanceTierMin)
			|| MatchedRow->MinimumClearanceMultiplier > static_cast<float>(CertifiedClearanceTierMax))
		{
			OutError = FString::Printf(
				TEXT("Clothing Director garment %s has MinimumClearanceMultiplier %.9g outside the certified [%.3f, %.3f] range."),
				*OutCatalogRowName.ToString(),
				MatchedRow->MinimumClearanceMultiplier,
				CertifiedClearanceTierMin,
				CertifiedClearanceTierMax);
			return false;
		}
		if (!FMath::IsFinite(MatchedRow->FabricClearanceCm)
			|| (!EFClothingMorphV26::IsAutomaticCentimeterValue(MatchedRow->FabricClearanceCm)
				&& MatchedRow->FabricClearanceCm < 0.0f)
			|| !FMath::IsFinite(MatchedRow->MaximumCorrectionCm)
			|| (!EFClothingMorphV26::IsAutomaticCentimeterValue(MatchedRow->MaximumCorrectionCm)
				&& MatchedRow->MaximumCorrectionCm <= 0.0f))
		{
			OutError = FString::Printf(
				TEXT("Clothing Director garment %s has an invalid centimeter clearance policy."),
				*OutCatalogRowName.ToString());
			return false;
		}
		if (MatchedRow->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU
			&& !MatchedRow->bFailClosedOnMissingLOD)
		{
			OutError = FString::Printf(
				TEXT("Clothing Director garment %s requests SurfaceWrapGPU without fail-closed LOD policy."),
				*OutCatalogRowName.ToString());
			return false;
		}
		if (MatchedRow->bCreateThicknessShell
			&& (MatchedRow->ShellOffsetSteps < 1
				|| MatchedRow->ShellOffsetSteps > 100
				|| !FMath::IsFinite(MatchedRow->ShellSmoothingPerStep)
				|| MatchedRow->ShellSmoothingPerStep < 0.0f
				|| MatchedRow->ShellSmoothingPerStep > 1.0f
				|| !MatchedRow->bShellOffsetBoundaries))
		{
			OutError = FString::Printf(
				TEXT("Clothing Director garment %s has an invalid thickness-shell policy."),
				*OutCatalogRowName.ToString());
			return false;
		}

		OutExcludedBodySurfaceMaterialSlots = MatchedRow->ExcludedBodySurfaceMaterialSlots;
		OutExcludedBodyBoneBranches = MatchedRow->ExcludedBodyBoneBranches;
		OutExcludedBodyMorphPrefixes = MatchedRow->ExcludedBodyMorphPrefixes;
		OutCatalogRow = *MatchedRow;
		CanonicalizeMaterialSlotNames(OutExcludedBodySurfaceMaterialSlots);
		CanonicalizeBoneBranchNames(OutExcludedBodyBoneBranches);
		CanonicalizeMorphPrefixes(OutExcludedBodyMorphPrefixes);
		TArray<FName> CanonicalHiddenBodyMaterialSlots = MatchedRow->HiddenBodyMaterialSlots;
		CanonicalizeMaterialSlotNames(CanonicalHiddenBodyMaterialSlots);
		for (const FName ExcludedSurfaceSlot : OutExcludedBodySurfaceMaterialSlots)
		{
			if (!CanonicalHiddenBodyMaterialSlots.Contains(ExcludedSurfaceSlot))
			{
				OutError = FString::Printf(
					TEXT("Clothing Director garment %s excludes body surface slot %s from geometry fitting but does not hide that slot at runtime."),
					*OutCatalogRowName.ToString(),
					*ExcludedSurfaceSlot.ToString());
				return false;
			}
		}
		return true;
	}

	static bool ExcludeBodySurfaceMaterialSlots(
		const USkeletalMesh* BodySurface,
		UDynamicMesh* BodyDynamicMesh,
		const TArray<FName>& ExcludedBodySurfaceMaterialSlots,
		int32& OutExcludedTriangleCount,
		FDynamicMesh3* OutExcludedBodySurface,
		FString& OutError)
	{
		OutExcludedTriangleCount = 0;
		if (OutExcludedBodySurface)
		{
			OutExcludedBodySurface->Clear();
		}
		if (ExcludedBodySurfaceMaterialSlots.IsEmpty())
		{
			return true;
		}
		if (!IsValid(BodySurface) || !IsValid(BodyDynamicMesh))
		{
			OutError = TEXT("Cannot filter an invalid body surface or dynamic mesh.");
			return false;
		}

		TSet<int32> ExcludedMaterialIndices;
		const TArray<FSkeletalMaterial>& Materials = BodySurface->GetMaterials();
		for (const FName RequestedSlot : ExcludedBodySurfaceMaterialSlots)
		{
			int32 MatchIndex = INDEX_NONE;
			for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
			{
				const FSkeletalMaterial& Material = Materials[MaterialIndex];
				if (Material.MaterialSlotName != RequestedSlot
					&& Material.ImportedMaterialSlotName != RequestedSlot)
				{
					continue;
				}
				if (MatchIndex != INDEX_NONE)
				{
					OutError = FString::Printf(
						TEXT("Body surface material slot %s is ambiguous."),
						*RequestedSlot.ToString());
					return false;
				}
				MatchIndex = MaterialIndex;
			}
			if (MatchIndex == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("Body surface %s has no material slot %s requested by the garment catalog."),
					*BodySurface->GetPathName(),
					*RequestedSlot.ToString());
				return false;
			}
			ExcludedMaterialIndices.Add(MatchIndex);
		}

		FDynamicMesh3& BodyMesh = BodyDynamicMesh->GetMeshRef();
		if (!BodyMesh.HasAttributes()
			|| !BodyMesh.Attributes()->HasMaterialID()
			|| !BodyMesh.Attributes()->GetMaterialID())
		{
			OutError = TEXT("Body LOD0 dynamic mesh has no per-triangle material IDs.");
			return false;
		}
		const FDynamicMeshMaterialAttribute* MaterialIDs = BodyMesh.Attributes()->GetMaterialID();
		TArray<int32> TrianglesToRemove;
		TMap<int32, int32> RemovedPerMaterial;
		for (const int32 TriangleID : BodyMesh.TriangleIndicesItr())
		{
			const int32 MaterialIndex = MaterialIDs->GetValue(TriangleID);
			if (ExcludedMaterialIndices.Contains(MaterialIndex))
			{
				TrianglesToRemove.Add(TriangleID);
				++RemovedPerMaterial.FindOrAdd(MaterialIndex);
			}
		}
		for (const int32 MaterialIndex : ExcludedMaterialIndices)
		{
			if (RemovedPerMaterial.FindRef(MaterialIndex) <= 0)
			{
				OutError = FString::Printf(
					TEXT("Excluded body material index %d has no source triangles in LOD0."),
					MaterialIndex);
				return false;
			}
		}
		if (TrianglesToRemove.Num() >= BodyMesh.TriangleCount())
		{
			OutError = TEXT("Surface exclusion would remove every body LOD0 triangle.");
			return false;
		}
		if (OutExcludedBodySurface)
		{
			TMap<int32, int32> ExcludedVertexMap;
			for (const int32 TriangleID : TrianglesToRemove)
			{
				if (!BodyMesh.IsTriangle(TriangleID))
				{
					OutError = TEXT("Excluded body surface contains an invalid source triangle.");
					return false;
				}
				const FIndex3i SourceTriangle = BodyMesh.GetTriangle(TriangleID);
				const int32 SourceVertices[3] = {
					SourceTriangle.A,
					SourceTriangle.B,
					SourceTriangle.C};
				int32 ExcludedVertices[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					const int32 SourceVertexID = SourceVertices[CornerIndex];
					if (const int32* ExistingVertexID = ExcludedVertexMap.Find(SourceVertexID))
					{
						ExcludedVertices[CornerIndex] = *ExistingVertexID;
					}
					else
					{
						const int32 NewVertexID = OutExcludedBodySurface->AppendVertex(
							BodyMesh.GetVertex(SourceVertexID));
						ExcludedVertexMap.Add(SourceVertexID, NewVertexID);
						ExcludedVertices[CornerIndex] = NewVertexID;
					}
				}
				if (OutExcludedBodySurface->AppendTriangle(FIndex3i(
					ExcludedVertices[0],
					ExcludedVertices[1],
					ExcludedVertices[2])) < 0)
				{
					OutError = FString::Printf(
						TEXT("Failed to copy excluded body LOD0 triangle %d into the transient certification surface."),
						TriangleID);
					return false;
				}
			}
			if (OutExcludedBodySurface->TriangleCount() != TrianglesToRemove.Num())
			{
				OutError = TEXT("Transient excluded-body certification surface lost triangles.");
				return false;
			}
		}
		for (const int32 TriangleID : TrianglesToRemove)
		{
			if (BodyMesh.RemoveTriangle(TriangleID, true, false) != EMeshResult::Ok)
			{
				OutError = FString::Printf(
					TEXT("Failed to remove excluded body LOD0 triangle %d."),
					TriangleID);
				return false;
			}
		}
		OutExcludedTriangleCount = TrianglesToRemove.Num();
		return true;
	}

	static bool CopySourceLOD(USkeletalMesh* Asset, UDynamicMesh* OutMesh, FString& OutError)
	{
		if (!IsValid(Asset) || !IsValid(OutMesh))
		{
			OutError = TEXT("Invalid skeletal mesh or dynamic mesh output.");
			return false;
		}

		FGeometryScriptCopyMeshFromAssetOptions CopyOptions;
		CopyOptions.bApplyBuildSettings = false;
		CopyOptions.bRequestTangents = true;
		CopyOptions.bIgnoreRemoveDegenerates = true;
		CopyOptions.bUseBuildScale = true;

		FGeometryScriptMeshReadLOD ReadLOD;
		ReadLOD.LODType = EGeometryScriptLODType::SourceModel;
		ReadLOD.LODIndex = 0;

		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromSkeletalMesh(
			Asset,
			OutMesh,
			CopyOptions,
			ReadLOD,
			Outcome,
			nullptr);

		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = FString::Printf(TEXT("Failed to read SourceModel LOD0 from %s."), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	/**
	 * A vertex-only runtime constraint cannot prevent a long garment face from
	 * cutting across a curved animated body between its three endpoints. Surface
	 * Wrap therefore compiles the generated mesh to a conservative chord bound
	 * while retaining a 0.5cm face-witness lattice. Splitting happens only on the fresh
	 * generated topology; DynamicMesh propagates UV/normal overlays and linearly
	 * interpolates every skin-weight profile on each new vertex.
	 */
	static bool DensifySurfaceGarmentTopology(
		UDynamicMesh* GarmentDynamicMesh,
		int32& OutOriginalVertexCount,
		int32& OutFinalVertexCount,
		int32& OutSplitCount,
		FString& OutError)
	{
		OutOriginalVertexCount = 0;
		OutFinalVertexCount = 0;
		OutSplitCount = 0;
		if (!IsValid(GarmentDynamicMesh))
		{
			OutError = TEXT("Surface densification received no generated garment mesh.");
			return false;
		}

		FDynamicMesh3& Mesh = GarmentDynamicMesh->GetMeshRef();
		OutOriginalVertexCount = Mesh.VertexCount();
		OutFinalVertexCount = OutOriginalVertexCount;
		if (Mesh.TriangleCount() <= 0 || Mesh.EdgeCount() <= 0)
		{
			OutError = TEXT("Surface densification requires non-empty garment topology.");
			return false;
		}

		// Sequential SplitEdge refinement can starve on thin triangles: each split
		// introduces a new diagonal, and a LIFO/FIFO worklist may keep refining that
		// local chain without ever reaching the original long edges. Red-green
		// tessellation subdivides every selected source triangle simultaneously and
		// adds conforming green neighbors. With level L every selected source chord is
		// split into 2^L segments, so the final maximum edge has a deterministic bound.
		const FDynamicMesh3 SourceMesh(Mesh);
		const FNonManifoldMappingSupport SourceMapping(SourceMesh);
		TArray<int32> SourceCanonicalImportVertexIDs;
		SourceCanonicalImportVertexIDs.Init(INDEX_NONE, SourceMesh.MaxVertexID());
		TArray<int32> SourceVertexToResultVertex;
		SourceVertexToResultVertex.Init(INDEX_NONE, SourceMesh.MaxVertexID());
		int32 NextCanonicalImportVertexID = 0;
		int32 NextOriginalResultVertexID = 0;
		for (const int32 VertexID : SourceMesh.VertexIndicesItr())
		{
			const int32 CanonicalID = SourceMapping.GetOriginalNonManifoldVertexID(VertexID);
			if (CanonicalID < 0)
			{
				OutError = FString::Printf(
					TEXT("SurfaceWrap source vertex %d has no canonical import identity."),
					VertexID);
				return false;
			}
			SourceCanonicalImportVertexIDs[VertexID] = CanonicalID;
			SourceVertexToResultVertex[VertexID] = NextOriginalResultVertexID++;
			NextCanonicalImportVertexID = FMath::Max(NextCanonicalImportVertexID, CanonicalID + 1);
		}

		double InitialMaximumEdgeLengthCm = 0.0;
		int32 InitialEdgesAboveOneCm = 0;
		int32 InitialEdgesAboveTwoCm = 0;
		int32 InitialEdgesAboveFiveCm = 0;
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			const FIndex2i EdgeVertices = Mesh.GetEdgeV(EdgeID);
			const double EdgeLengthCm =
				(Mesh.GetVertex(EdgeVertices.A) - Mesh.GetVertex(EdgeVertices.B)).Length();
			InitialMaximumEdgeLengthCm = FMath::Max(InitialMaximumEdgeLengthCm, EdgeLengthCm);
			InitialEdgesAboveOneCm += EdgeLengthCm > 1.0 ? 1 : 0;
			InitialEdgesAboveTwoCm += EdgeLengthCm > 2.0 ? 1 : 0;
			InitialEdgesAboveFiveCm += EdgeLengthCm > 5.0 ? 1 : 0;
		}
		if (InitialMaximumEdgeLengthCm <= SurfaceRuntimeMaximumEdgeLengthCm + 1.e-6)
		{
			return true;
		}

		TArray<int32> TriangleTessellationLevels;
		TriangleTessellationLevels.Init(0, SourceMesh.MaxTriangleID());
		int32 SelectedTriangleCount = 0;
		int32 MaximumTessellationLevel = 0;
		for (const int32 TriangleID : SourceMesh.TriangleIndicesItr())
		{
			const FIndex3i TriangleEdges = SourceMesh.GetTriEdges(TriangleID);
			double TriangleMaximumEdgeLengthCm = 0.0;
			for (int32 EdgeCorner = 0; EdgeCorner < 3; ++EdgeCorner)
			{
				const FIndex2i EdgeVertices = SourceMesh.GetEdgeV(TriangleEdges[EdgeCorner]);
				TriangleMaximumEdgeLengthCm = FMath::Max(
					TriangleMaximumEdgeLengthCm,
					(SourceMesh.GetVertex(EdgeVertices.A) - SourceMesh.GetVertex(EdgeVertices.B)).Length());
			}
			int32 TessellationLevel = 0;
			while (TriangleMaximumEdgeLengthCm
				/ static_cast<double>(1 << TessellationLevel)
				> SurfaceRuntimeMaximumEdgeLengthCm + 1.e-6)
			{
				++TessellationLevel;
				if (TessellationLevel > 8)
				{
					OutError = FString::Printf(
						TEXT("SurfaceWrap triangle %d requires unsupported tessellation level %d (max edge %.6fcm)."),
						TriangleID,
						TessellationLevel,
						TriangleMaximumEdgeLengthCm);
					return false;
				}
			}
			TriangleTessellationLevels[TriangleID] = TessellationLevel;
			SelectedTriangleCount += TessellationLevel > 0 ? 1 : 0;
			MaximumTessellationLevel = FMath::Max(MaximumTessellationLevel, TessellationLevel);
		}

		TUniquePtr<FTessellationPattern> Pattern =
			FSelectiveTessellate::CreateRedGreenTessellationPattern(
				&SourceMesh,
				TriangleTessellationLevels);
		if (!Pattern)
		{
			OutError = TEXT("Could not create the deterministic red-green SurfaceWrap tessellation pattern.");
			return false;
		}
		FDynamicMesh3 TessellatedMesh;
		FSelectiveTessellate Tessellator(&SourceMesh, &TessellatedMesh);
		Tessellator.bUseParallel = false;
		Tessellator.SetPattern(Pattern.Get());
		if (Tessellator.Validate() != EOperationValidationResult::Ok || !Tessellator.Compute())
		{
			OutError = TEXT("Deterministic red-green SurfaceWrap tessellation failed validation or execution.");
			return false;
		}
		if (SourceMesh.HasAttributes()
			&& SourceMesh.Attributes()->HasBones()
			&& TessellatedMesh.HasAttributes())
		{
			TessellatedMesh.Attributes()->CopyBoneAttributes(*SourceMesh.Attributes());
		}
		OutFinalVertexCount = TessellatedMesh.VertexCount();
		OutSplitCount = OutFinalVertexCount - OutOriginalVertexCount;
		if (OutFinalVertexCount > SurfaceRuntimeMaximumDerivedVertexCount)
		{
			OutError = FString::Printf(
				TEXT("SurfaceWrap red-green tessellation exceeded the certified %d-vertex per-garment budget (source=%d result=%d selectedTriangles=%d maxLevel=%d initialMaxEdge=%.4fcm edges>1=%d edges>2=%d edges>5=%d)."),
				SurfaceRuntimeMaximumDerivedVertexCount,
				OutOriginalVertexCount,
				OutFinalVertexCount,
				SelectedTriangleCount,
				MaximumTessellationLevel,
				InitialMaximumEdgeLengthCm,
				InitialEdgesAboveOneCm,
				InitialEdgesAboveTwoCm,
				InitialEdgesAboveFiveCm);
			return false;
		}

		TArray<int32> ResultCanonicalImportVertexIDs;
		ResultCanonicalImportVertexIDs.Init(INDEX_NONE, TessellatedMesh.MaxVertexID());
		for (const int32 SourceVertexID : SourceMesh.VertexIndicesItr())
		{
			const int32 ResultVertexID = SourceVertexToResultVertex[SourceVertexID];
			if (!TessellatedMesh.IsVertex(ResultVertexID))
			{
				OutError = FString::Printf(
					TEXT("SurfaceWrap tessellation lost source vertex %d (expected result vertex %d)."),
					SourceVertexID,
					ResultVertexID);
				return false;
			}
			ResultCanonicalImportVertexIDs[ResultVertexID] =
				SourceCanonicalImportVertexIDs[SourceVertexID];
		}
		for (const int32 ResultVertexID : TessellatedMesh.VertexIndicesItr())
		{
			if (ResultCanonicalImportVertexIDs[ResultVertexID] == INDEX_NONE)
			{
				ResultCanonicalImportVertexIDs[ResultVertexID] = NextCanonicalImportVertexID++;
			}
		}
		FNonManifoldMappingSupport::RemoveAllNonManifoldMappingData(TessellatedMesh);
		if (!FNonManifoldMappingSupport::AttachNonManifoldVertexMappingData(
			ResultCanonicalImportVertexIDs,
			TessellatedMesh))
		{
			OutError = TEXT("Could not reattach canonical import mapping after SurfaceWrap densification.");
			return false;
		}

		double MaximumFinalEdgeLengthCm = 0.0;
		for (const int32 EdgeID : TessellatedMesh.EdgeIndicesItr())
		{
			const FIndex2i EdgeVertices = TessellatedMesh.GetEdgeV(EdgeID);
			MaximumFinalEdgeLengthCm = FMath::Max(
				MaximumFinalEdgeLengthCm,
				(TessellatedMesh.GetVertex(EdgeVertices.A) - TessellatedMesh.GetVertex(EdgeVertices.B)).Length());
		}
		if (MaximumFinalEdgeLengthCm > SurfaceRuntimeMaximumEdgeLengthCm + 1.e-5)
		{
			OutError = FString::Printf(
				TEXT("SurfaceWrap densification left a %.6fcm edge above its %.6fcm certification limit."),
				MaximumFinalEdgeLengthCm,
				SurfaceRuntimeMaximumEdgeLengthCm);
			return false;
		}
		Mesh = MoveTemp(TessellatedMesh);
		return true;
	}

	static bool RebuildThicknessShellPairing(
		const FDynamicMesh3& Mesh,
		FThicknessShellCompileData& InOutShell,
		FString& OutError)
	{
		if (!InOutShell.bEnabled || InOutShell.PreShellVertexCount <= 0)
		{
			OutError = TEXT("Thickness-shell pairing was requested without a compiled shell.");
			return false;
		}
		const int32 ExpectedImportVertexCount = InOutShell.PreShellVertexCount * 2;
		InOutShell.SourceOrdinalByVertex.Init(INDEX_NONE, Mesh.MaxVertexID());
		InOutShell.LayerByVertex.Init(MAX_uint8, Mesh.MaxVertexID());
		InOutShell.OuterVerticesBySourceOrdinal.SetNum(InOutShell.PreShellVertexCount);
		InOutShell.InnerVerticesBySourceOrdinal.SetNum(InOutShell.PreShellVertexCount);
		for (TArray<int32>& Vertices : InOutShell.OuterVerticesBySourceOrdinal)
		{
			Vertices.Reset();
		}
		for (TArray<int32>& Vertices : InOutShell.InnerVerticesBySourceOrdinal)
		{
			Vertices.Reset();
		}

		const FNonManifoldMappingSupport Mapping(Mesh);
		TBitArray<> SeenImportVertices(false, ExpectedImportVertexCount);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			const int32 ImportVertexID = Mapping.GetOriginalNonManifoldVertexID(VertexID);
			if (ImportVertexID < 0 || ImportVertexID >= ExpectedImportVertexCount)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell dynamic vertex %d maps to invalid import vertex %d (expected 0..%d)."),
					VertexID,
					ImportVertexID,
					ExpectedImportVertexCount - 1);
				return false;
			}
			const bool bOuter = ImportVertexID < InOutShell.PreShellVertexCount;
			const int32 SourceOrdinal = bOuter
				? ImportVertexID
				: ImportVertexID - InOutShell.PreShellVertexCount;
			InOutShell.SourceOrdinalByVertex[VertexID] = SourceOrdinal;
			InOutShell.LayerByVertex[VertexID] = bOuter ? 0 : 1;
			(bOuter
				? InOutShell.OuterVerticesBySourceOrdinal[SourceOrdinal]
				: InOutShell.InnerVerticesBySourceOrdinal[SourceOrdinal]).Add(VertexID);
			SeenImportVertices[ImportVertexID] = true;
		}
		for (int32 ImportVertexID = 0; ImportVertexID < ExpectedImportVertexCount; ++ImportVertexID)
		{
			if (!SeenImportVertices[ImportVertexID])
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell import vertex %d disappeared after skeletal-mesh publication."),
					ImportVertexID);
				return false;
			}
		}

		double ThicknessSum = 0.0;
		InOutShell.MinimumMeasuredThicknessCm = TNumericLimits<double>::Max();
		InOutShell.MaximumMeasuredThicknessCm = 0.0;
		for (int32 SourceOrdinal = 0; SourceOrdinal < InOutShell.PreShellVertexCount; ++SourceOrdinal)
		{
			const TArray<int32>& OuterVertices = InOutShell.OuterVerticesBySourceOrdinal[SourceOrdinal];
			const TArray<int32>& InnerVertices = InOutShell.InnerVerticesBySourceOrdinal[SourceOrdinal];
			if (OuterVertices.IsEmpty() || InnerVertices.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell source ordinal %d has an incomplete outer/inner pair."),
					SourceOrdinal);
				return false;
			}
			FVector3d OuterPosition = FVector3d::Zero();
			for (const int32 VertexID : OuterVertices)
			{
				OuterPosition += Mesh.GetVertex(VertexID);
			}
			OuterPosition /= static_cast<double>(OuterVertices.Num());
			FVector3d InnerPosition = FVector3d::Zero();
			for (const int32 VertexID : InnerVertices)
			{
				InnerPosition += Mesh.GetVertex(VertexID);
			}
			InnerPosition /= static_cast<double>(InnerVertices.Num());
			const double MeasuredThicknessCm = (OuterPosition - InnerPosition).Length();
			if (!FMath::IsFinite(MeasuredThicknessCm))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell pair %d has non-finite measured thickness."),
					SourceOrdinal);
				return false;
			}
			ThicknessSum += MeasuredThicknessCm;
			InOutShell.MinimumMeasuredThicknessCm = FMath::Min(
				InOutShell.MinimumMeasuredThicknessCm,
				MeasuredThicknessCm);
			InOutShell.MaximumMeasuredThicknessCm = FMath::Max(
				InOutShell.MaximumMeasuredThicknessCm,
				MeasuredThicknessCm);
		}
		InOutShell.AverageMeasuredThicknessCm = ThicknessSum
			/ static_cast<double>(InOutShell.PreShellVertexCount);
		const double MinimumUsefulThickness =
			MinimumCertifiedThicknessPointCm(InOutShell.RequestedThicknessCm);
		if (InOutShell.MinimumMeasuredThicknessCm + 1.e-6 < MinimumUsefulThickness
			|| InOutShell.AverageMeasuredThicknessCm < InOutShell.RequestedThicknessCm * 0.90
			|| InOutShell.AverageMeasuredThicknessCm > InOutShell.RequestedThicknessCm * 1.50
			|| InOutShell.MaximumMeasuredThicknessCm > InOutShell.RequestedThicknessCm * 2.50)
		{
			OutError = FString::Printf(
				TEXT("Thickness-shell measurement is outside the certified iterative-offset envelope (requested %.8fcm, measured %.8f/%.8f/%.8fcm)."),
				InOutShell.RequestedThicknessCm,
				InOutShell.MinimumMeasuredThicknessCm,
				InOutShell.AverageMeasuredThicknessCm,
				InOutShell.MaximumMeasuredThicknessCm);
			return false;
		}
		return true;
	}

	static bool MeasureUnsafeThicknessShellSelfIntersections(
		const FDynamicMesh3& Mesh,
		const int32 PreShellImportVertexCount,
		int32& OutUnsafeIntersectionCount,
		int32& OutOuterLayerIntersectionCount,
		int32& OutInnerLayerIntersectionCount,
		int32& OutCrossLayerOrWallIntersectionCount,
		FString& OutError)
	{
		OutUnsafeIntersectionCount = 0;
		OutOuterLayerIntersectionCount = 0;
		OutInnerLayerIntersectionCount = 0;
		OutCrossLayerOrWallIntersectionCount = 0;
		if (PreShellImportVertexCount <= 0 || !Mesh.HasAttributes())
		{
			OutError = TEXT("Thickness-shell self-intersection audit has no paired import topology.");
			return false;
		}
		const int32 ExpectedImportVertexCount = PreShellImportVertexCount * 2;
		const FNonManifoldMappingSupport Mapping(Mesh);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			const int32 ImportVertexID = Mapping.GetOriginalNonManifoldVertexID(VertexID);
			if (ImportVertexID < 0 || ImportVertexID >= ExpectedImportVertexCount)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell self-intersection audit found invalid import vertex %d."),
					ImportVertexID);
				return false;
			}
		}

		auto TriangleImportIDs = [&](const int32 TriangleID)
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			return FIndex3i(
				Mapping.GetOriginalNonManifoldVertexID(Triangle.A),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.B),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.C));
		};
		auto TriangleLayer = [&](const FIndex3i& ImportIDs)
		{
			const int32 OuterCount =
				(ImportIDs.A < PreShellImportVertexCount ? 1 : 0)
				+ (ImportIDs.B < PreShellImportVertexCount ? 1 : 0)
				+ (ImportIDs.C < PreShellImportVertexCount ? 1 : 0);
			return OuterCount == 3 ? 0 : (OuterCount == 0 ? 1 : 2);
		};
		auto SharesImportVertex = [](const FIndex3i& A, const FIndex3i& B)
		{
			const int32 AValues[3] = {A.A, A.B, A.C};
			const int32 BValues[3] = {B.A, B.B, B.C};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if (AValue == BValue)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SharesSourceOrdinal = [PreShellImportVertexCount](
			const FIndex3i& A,
			const FIndex3i& B)
		{
			const int32 AValues[3] = {
				A.A % PreShellImportVertexCount,
				A.B % PreShellImportVertexCount,
				A.C % PreShellImportVertexCount};
			const int32 BValues[3] = {
				B.A % PreShellImportVertexCount,
				B.B % PreShellImportVertexCount,
				B.C % PreShellImportVertexCount};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if (AValue == BValue)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SharesCoincidentMeshVertex = [&Mesh](
			const int32 TriangleA,
			const int32 TriangleB)
		{
			const FIndex3i VerticesA = Mesh.GetTriangle(TriangleA);
			const FIndex3i VerticesB = Mesh.GetTriangle(TriangleB);
			const int32 AValues[3] = {VerticesA.A, VerticesA.B, VerticesA.C};
			const int32 BValues[3] = {VerticesB.A, VerticesB.B, VerticesB.C};
			constexpr double CoincidentVertexToleranceSquared = 1.e-12;
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if ((Mesh.GetVertex(AValue) - Mesh.GetVertex(BValue)).SquaredLength()
						<= CoincidentVertexToleranceSquared)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SourceTriangleKey = [PreShellImportVertexCount](const FIndex3i& Imports)
		{
			const int32 A = Imports.A % PreShellImportVertexCount;
			const int32 B = Imports.B % PreShellImportVertexCount;
			const int32 C = Imports.C % PreShellImportVertexCount;
			const int32 Minimum = FMath::Min3(A, B, C);
			const int32 Maximum = FMath::Max3(A, B, C);
			const int32 Middle = A + B + C - Minimum - Maximum;
			return FString::Printf(TEXT("%d,%d,%d"), Minimum, Middle, Maximum);
		};
		auto SourceIntersectionKey = [&SourceTriangleKey](
			const FIndex3i& ImportsA,
			const FIndex3i& ImportsB)
		{
			const FString KeyA = SourceTriangleKey(ImportsA);
			const FString KeyB = SourceTriangleKey(ImportsB);
			return KeyA < KeyB
				? KeyA + TEXT("|") + KeyB
				: KeyB + TEXT("|") + KeyA;
		};

		const FDynamicMeshAABBTree3 ShellSpatial(&Mesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			ShellSpatial.FindAllSelfIntersections(true);
		TArray<FIntPoint> IntersectionTrianglePairs;
		IntersectionTrianglePairs.Reserve(
			Intersections.Points.Num()
			+ Intersections.Segments.Num()
			+ Intersections.Polygons.Num());
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			IntersectionTrianglePairs.Emplace(
				Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			IntersectionTrianglePairs.Emplace(
				Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			IntersectionTrianglePairs.Emplace(
				Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}

		TSet<FString> InheritedSourceIntersectionPairs;
		for (const FIntPoint& Pair : IntersectionTrianglePairs)
		{
			if (!Mesh.IsTriangle(Pair.X) || !Mesh.IsTriangle(Pair.Y))
			{
				continue;
			}
			const FIndex3i ImportsA = TriangleImportIDs(Pair.X);
			const FIndex3i ImportsB = TriangleImportIDs(Pair.Y);
			const int32 LayerA = TriangleLayer(ImportsA);
			const int32 LayerB = TriangleLayer(ImportsB);
			if (LayerA == 1 && LayerB == 1
				&& !SharesImportVertex(ImportsA, ImportsB))
			{
				InheritedSourceIntersectionPairs.Add(
					SourceIntersectionKey(ImportsA, ImportsB));
			}
		}

		for (const FIntPoint& Pair : IntersectionTrianglePairs)
		{
			if (!Mesh.IsTriangle(Pair.X) || !Mesh.IsTriangle(Pair.Y))
			{
				continue;
			}
			const FIndex3i ImportsA = TriangleImportIDs(Pair.X);
			const FIndex3i ImportsB = TriangleImportIDs(Pair.Y);
			const int32 LayerA = TriangleLayer(ImportsA);
			const int32 LayerB = TriangleLayer(ImportsB);
			// Attribute seams retain one exact import ID. Stitched wall triangles
			// use the paired outer/inner IDs, so canonical source-ordinal adjacency
			// is also expected only when at least one triangle is a wall.
			const bool bSameSurfaceLayer = LayerA == LayerB && LayerA != 2;
			const bool bTouchesWallLayer = LayerA == 2 || LayerB == 2;
			if (SharesImportVertex(ImportsA, ImportsB)
				|| ((bSameSurfaceLayer || bTouchesWallLayer)
					&& (SharesSourceOrdinal(ImportsA, ImportsB)
						|| SharesCoincidentMeshVertex(
							Pair.X,
							Pair.Y))))
			{
				continue;
			}
			if (LayerA == 0 && LayerB == 0)
			{
				++OutOuterLayerIntersectionCount;
			}
			else if (LayerA == 1 && LayerB == 1)
			{
				++OutInnerLayerIntersectionCount;
				continue;
			}
			else
			{
				++OutCrossLayerOrWallIntersectionCount;
			}
			if (!InheritedSourceIntersectionPairs.Contains(
				SourceIntersectionKey(ImportsA, ImportsB)))
			{
				++OutUnsafeIntersectionCount;
			}
		}
		return true;
	}

	static bool ClassifyThicknessShellIntersections(
		const FEFClothingGarmentRow& CatalogRow,
		const int32 DetectedIntersectionCount,
		const int32 FinalTriangleCount,
		int32& OutToleratedExcludedAnatomyIntersectionCount,
		FString& OutError)
	{
		OutToleratedExcludedAnatomyIntersectionCount = 0;
		if (DetectedIntersectionCount < 0 || FinalTriangleCount <= 0)
		{
			OutError = TEXT("Thickness-shell intersection policy received invalid geometry metrics.");
			return false;
		}
		if (DetectedIntersectionCount == 0)
		{
			return true;
		}

		// This is intentionally a catalog contract rather than a garment-name rule.
		// A row must identify both the excluded rendered surface and the excluded
		// skeletal branch before a tiny residual can be kept inside that declared
		// anatomy domain. Rows without both declarations remain strictly zero-hit.
		const bool bHasExplicitAnatomyExclusion =
			!CatalogRow.ExcludedBodySurfaceMaterialSlots.IsEmpty()
			&& !CatalogRow.ExcludedBodyBoneBranches.IsEmpty();
		const int32 MaximumToleratedIntersectionCount = FMath::FloorToInt(
			static_cast<double>(FinalTriangleCount)
			* MaximumExcludedAnatomyShellIntersectionFraction);
		if (!bHasExplicitAnatomyExclusion
			|| DetectedIntersectionCount > MaximumToleratedIntersectionCount)
		{
			OutError = FString::Printf(
				TEXT("Thickness shell has %d non-adjacent intersections; the explicit-anatomy allowance is %d (surfaceExclusions=%d, boneExclusions=%d, limit=%.2f%% of %d triangles)."),
				DetectedIntersectionCount,
				MaximumToleratedIntersectionCount,
				CatalogRow.ExcludedBodySurfaceMaterialSlots.Num(),
				CatalogRow.ExcludedBodyBoneBranches.Num(),
				MaximumExcludedAnatomyShellIntersectionFraction * 100.0,
				FinalTriangleCount);
			return false;
		}

		OutToleratedExcludedAnatomyIntersectionCount = DetectedIntersectionCount;
		return true;
	}

	static bool ExtractThicknessShellInnerLayer(
		const FDynamicMesh3& ShellMesh,
		const int32 PreShellImportVertexCount,
		FDynamicMesh3& OutInnerLayer,
		FString& OutError)
	{
		OutInnerLayer.Clear();
		if (PreShellImportVertexCount <= 0 || !ShellMesh.HasAttributes())
		{
			OutError = TEXT("Cannot extract a thickness-shell inner layer without paired import topology.");
			return false;
		}
		const FNonManifoldMappingSupport Mapping(ShellMesh);
		TMap<int32, int32> InnerVertexMap;
		TArray<int32> NormalizedImportVertexIDs;
		for (const int32 TriangleID : ShellMesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = ShellMesh.GetTriangle(TriangleID);
			const int32 Vertices[3] = {Triangle.A, Triangle.B, Triangle.C};
			int32 ImportIDs[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
			bool bInnerTriangle = true;
			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				ImportIDs[CornerIndex] = Mapping.GetOriginalNonManifoldVertexID(
					Vertices[CornerIndex]);
				bInnerTriangle &= ImportIDs[CornerIndex] >= PreShellImportVertexCount
					&& ImportIDs[CornerIndex] < PreShellImportVertexCount * 2;
			}
			if (!bInnerTriangle)
			{
				continue;
			}
			int32 InnerVertices[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 ShellVertexID = Vertices[CornerIndex];
				if (const int32* Existing = InnerVertexMap.Find(ShellVertexID))
				{
					InnerVertices[CornerIndex] = *Existing;
				}
				else
				{
					const int32 NewVertexID = OutInnerLayer.AppendVertex(
						ShellMesh.GetVertex(ShellVertexID));
					InnerVertexMap.Add(ShellVertexID, NewVertexID);
					if (NormalizedImportVertexIDs.Num() <= NewVertexID)
					{
						NormalizedImportVertexIDs.SetNum(NewVertexID + 1);
					}
					NormalizedImportVertexIDs[NewVertexID] =
						ImportIDs[CornerIndex] - PreShellImportVertexCount;
					InnerVertices[CornerIndex] = NewVertexID;
				}
			}
			if (OutInnerLayer.AppendTriangle(FIndex3i(
				InnerVertices[0], InnerVertices[1], InnerVertices[2])) < 0)
			{
				OutError = TEXT("Thickness-shell inner-layer extraction produced invalid topology.");
				return false;
			}
		}
		if (OutInnerLayer.TriangleCount() <= 0)
		{
			OutError = TEXT("Thickness-shell inner-layer extraction found no pure inner triangles.");
			return false;
		}
		OutInnerLayer.EnableAttributes();
		if (NormalizedImportVertexIDs.Num() != OutInnerLayer.MaxVertexID()
			|| !FNonManifoldMappingSupport::AttachNonManifoldVertexMappingData(
				NormalizedImportVertexIDs,
				OutInnerLayer))
		{
			OutError = TEXT("Thickness-shell inner-layer extraction could not preserve normalized source import identities.");
			return false;
		}
		return true;
	}

	struct FSourceIntersectionWitnessEvidence
	{
		FIndex3i SourceTriangleA = FIndex3i::Invalid();
		FIndex3i SourceTriangleB = FIndex3i::Invalid();
		TArray<FVector3d> WitnessPoints;
	};

	static bool CollectSelfIntersectionWitnessEvidence(
		const FDynamicMesh3& Mesh,
		const int32 SourceImportVertexCount,
		TMap<FString, FSourceIntersectionWitnessEvidence>& OutEvidenceBySourcePair,
		int32& OutUniquePairCount,
		FString& OutError)
	{
		OutEvidenceBySourcePair.Reset();
		OutUniquePairCount = 0;
		if (Mesh.TriangleCount() <= 0
			|| SourceImportVertexCount <= 0
			|| !Mesh.HasAttributes())
		{
			OutError = TEXT("Self-intersection baseline has no attributed source topology.");
			return false;
		}
		const FNonManifoldMappingSupport Mapping(Mesh);
		auto TriangleImportIDs = [&](const int32 TriangleID)
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			return FIndex3i(
				Mapping.GetOriginalNonManifoldVertexID(Triangle.A),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.B),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.C));
		};
		auto SourceTriangleKey = [SourceImportVertexCount](const FIndex3i& Imports)
		{
			const int32 A = Imports.A % SourceImportVertexCount;
			const int32 B = Imports.B % SourceImportVertexCount;
			const int32 C = Imports.C % SourceImportVertexCount;
			const int32 Minimum = FMath::Min3(A, B, C);
			const int32 Maximum = FMath::Max3(A, B, C);
			const int32 Middle = A + B + C - Minimum - Maximum;
			return FString::Printf(TEXT("%d,%d,%d"), Minimum, Middle, Maximum);
		};
		auto SourceIntersectionKey = [&SourceTriangleKey](
			const FIndex3i& ImportsA,
			const FIndex3i& ImportsB)
		{
			const FString KeyA = SourceTriangleKey(ImportsA);
			const FString KeyB = SourceTriangleKey(ImportsB);
			return KeyA < KeyB
				? KeyA + TEXT("|") + KeyB
				: KeyB + TEXT("|") + KeyA;
		};
		auto SharesSourceOrdinal = [SourceImportVertexCount](
			const FIndex3i& A,
			const FIndex3i& B)
		{
			const int32 AValues[3] = {
				A.A % SourceImportVertexCount,
				A.B % SourceImportVertexCount,
				A.C % SourceImportVertexCount};
			const int32 BValues[3] = {
				B.A % SourceImportVertexCount,
				B.B % SourceImportVertexCount,
				B.C % SourceImportVertexCount};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if (AValue == BValue)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SharesCoincidentMeshVertex = [&Mesh](
			const int32 TriangleA,
			const int32 TriangleB)
		{
			const FIndex3i VerticesA = Mesh.GetTriangle(TriangleA);
			const FIndex3i VerticesB = Mesh.GetTriangle(TriangleB);
			const int32 AValues[3] = {VerticesA.A, VerticesA.B, VerticesA.C};
			const int32 BValues[3] = {VerticesB.A, VerticesB.B, VerticesB.C};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if ((Mesh.GetVertex(AValue) - Mesh.GetVertex(BValue)).SquaredLength()
						<= 1.e-12)
					{
						return true;
					}
				}
			}
			return false;
		};
		struct FBaselinePairEvidence
		{
			FIntPoint TrianglePair = FIntPoint(INDEX_NONE, INDEX_NONE);
			TArray<FVector3d> WitnessPoints;
		};
		TMap<uint64, FBaselinePairEvidence> RawPairEvidence;
		const FDynamicMeshAABBTree3 Spatial(&Mesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			Spatial.FindAllSelfIntersections(true);
		auto AppendWitness = [&](const int32 TriangleA, const int32 TriangleB, const FVector3d& Point)
		{
			if (!FMath::IsFinite(Point.X)
				|| !FMath::IsFinite(Point.Y)
				|| !FMath::IsFinite(Point.Z))
			{
				return false;
			}
			const uint32 Minimum = static_cast<uint32>(FMath::Min(TriangleA, TriangleB));
			const uint32 Maximum = static_cast<uint32>(FMath::Max(TriangleA, TriangleB));
			const uint64 PairKey = (static_cast<uint64>(Minimum) << 32) | Maximum;
			FBaselinePairEvidence& Evidence = RawPairEvidence.FindOrAdd(PairKey);
			Evidence.TrianglePair = FIntPoint(
				static_cast<int32>(Minimum),
				static_cast<int32>(Maximum));
			Evidence.WitnessPoints.Add(Point);
			return true;
		};
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			if (!AppendWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Intersection.Point))
			{
				OutError = TEXT("Self-intersection baseline contains a non-finite point.");
				return false;
			}
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			if (!AppendWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Intersection.Point[0])
				|| !AppendWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Intersection.Point[1])
				|| !AppendWitness(
					Intersection.TriangleID[0],
					Intersection.TriangleID[1],
					(Intersection.Point[0] + Intersection.Point[1]) * 0.5))
			{
				OutError = TEXT("Self-intersection baseline contains a non-finite segment.");
				return false;
			}
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			if (Intersection.Quantity <= 0 || Intersection.Quantity > UE_ARRAY_COUNT(Intersection.Point))
			{
				OutError = TEXT("Self-intersection baseline contains an invalid polygon.");
				return false;
			}
			FVector3d Centroid = FVector3d::Zero();
			for (int32 PointIndex = 0; PointIndex < Intersection.Quantity; ++PointIndex)
			{
				Centroid += Intersection.Point[PointIndex];
				if (!AppendWitness(
					Intersection.TriangleID[0],
					Intersection.TriangleID[1],
					Intersection.Point[PointIndex]))
				{
					OutError = TEXT("Self-intersection baseline contains a non-finite polygon point.");
					return false;
				}
			}
			if (!AppendWitness(
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				Centroid / static_cast<double>(Intersection.Quantity)))
			{
				OutError = TEXT("Self-intersection baseline contains a non-finite polygon centroid.");
				return false;
			}
		}
		for (const TPair<uint64, FBaselinePairEvidence>& Pair : RawPairEvidence)
		{
			const FBaselinePairEvidence& Evidence = Pair.Value;
			if (!Mesh.IsTriangle(Evidence.TrianglePair.X)
				|| !Mesh.IsTriangle(Evidence.TrianglePair.Y)
				|| Evidence.WitnessPoints.IsEmpty())
			{
				OutError = TEXT("Self-intersection baseline retained invalid pair evidence.");
				return false;
			}
			const FIndex3i ImportsA = TriangleImportIDs(Evidence.TrianglePair.X);
			const FIndex3i ImportsB = TriangleImportIDs(Evidence.TrianglePair.Y);
			const int32 ImportValues[6] = {
				ImportsA.A, ImportsA.B, ImportsA.C,
				ImportsB.A, ImportsB.B, ImportsB.C};
			for (const int32 ImportValue : ImportValues)
			{
				if (ImportValue < 0 || ImportValue >= SourceImportVertexCount)
				{
					OutError = TEXT("Self-intersection baseline contains an invalid normalized source import identity.");
					return false;
				}
			}
			if (SharesSourceOrdinal(ImportsA, ImportsB)
				|| SharesCoincidentMeshVertex(
					Evidence.TrianglePair.X,
					Evidence.TrianglePair.Y))
			{
				continue;
			}
			const FIndex3i NormalizedA(
				ImportsA.A % SourceImportVertexCount,
				ImportsA.B % SourceImportVertexCount,
				ImportsA.C % SourceImportVertexCount);
			const FIndex3i NormalizedB(
				ImportsB.A % SourceImportVertexCount,
				ImportsB.B % SourceImportVertexCount,
				ImportsB.C % SourceImportVertexCount);
			const FString KeyA = SourceTriangleKey(NormalizedA);
			const FString KeyB = SourceTriangleKey(NormalizedB);
			FSourceIntersectionWitnessEvidence& StoredEvidence =
				OutEvidenceBySourcePair.FindOrAdd(
					SourceIntersectionKey(NormalizedA, NormalizedB));
			StoredEvidence.SourceTriangleA = KeyA < KeyB ? NormalizedA : NormalizedB;
			StoredEvidence.SourceTriangleB = KeyA < KeyB ? NormalizedB : NormalizedA;
			StoredEvidence.WitnessPoints.Append(Evidence.WitnessPoints);
		}
		OutUniquePairCount = OutEvidenceBySourcePair.Num();
		if (OutUniquePairCount == 0 && !OutEvidenceBySourcePair.IsEmpty())
		{
			OutError = TEXT("Self-intersection baseline witness/pair evidence is inconsistent.");
			return false;
		}
		return true;
	}

	struct FThicknessShellIntersectionAudit
	{
		int32 DetectedPairCount = 0;
		int32 ToleratedInheritedSourcePairCount = 0;
		int32 ToleratedLocalRepairPairCount = 0;
		int32 ToleratedExcludedRegionPairCount = 0;
		int32 AffectedSourceTriangleCount = 0;
		int32 OuterLayerPairCount = 0;
		int32 InnerLayerPairCount = 0;
		int32 CrossLayerOrWallPairCount = 0;
		int32 UnrepairableInnerOnlyPairCount = 0;
		TSet<int32> UnsafeOuterSourceOrdinals;
		TSet<FString> UnsafeSourcePairKeys;
		double MaximumExcludedRegionWitnessDistanceCm = 0.0;
	};

	static bool MeasureThicknessShellIntersectionsV3(
		const FDynamicMesh3& Mesh,
		const int32 PreShellImportVertexCount,
		const TMap<FString, FSourceIntersectionWitnessEvidence>& BaselineEvidenceBySourcePair,
		const double BaselineInheritanceRadiusCm,
		const FDynamicMesh3* ExcludedBodySurface,
		const double ExcludedRegionCertificationRadiusCm,
		const double LocalRepairThicknessCeilingCm,
		FThicknessShellIntersectionAudit& OutAudit,
		FString& OutError)
	{
		OutAudit = FThicknessShellIntersectionAudit();
		if (PreShellImportVertexCount <= 0 || !Mesh.HasAttributes())
		{
			OutError = TEXT("Thickness-shell V3 self-intersection audit has no paired import topology.");
			return false;
		}
		const int32 ExpectedImportVertexCount = PreShellImportVertexCount * 2;
		const FNonManifoldMappingSupport Mapping(Mesh);
		TArray<FVector3d> MeanPositionByImportID;
		MeanPositionByImportID.Init(FVector3d::Zero(), ExpectedImportVertexCount);
		TArray<int32> VertexCountByImportID;
		VertexCountByImportID.Init(0, ExpectedImportVertexCount);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			const int32 ImportVertexID = Mapping.GetOriginalNonManifoldVertexID(VertexID);
			if (ImportVertexID < 0 || ImportVertexID >= ExpectedImportVertexCount)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell V3 audit found invalid import vertex %d."),
					ImportVertexID);
				return false;
			}
			MeanPositionByImportID[ImportVertexID] += Mesh.GetVertex(VertexID);
			++VertexCountByImportID[ImportVertexID];
		}
		for (int32 ImportVertexID = 0;
			ImportVertexID < ExpectedImportVertexCount;
			++ImportVertexID)
		{
			if (VertexCountByImportID[ImportVertexID] <= 0)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell V3 audit is missing import vertex %d."),
					ImportVertexID);
				return false;
			}
			MeanPositionByImportID[ImportVertexID] /=
				static_cast<double>(VertexCountByImportID[ImportVertexID]);
		}

		auto TriangleImportIDs = [&](const int32 TriangleID)
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			return FIndex3i(
				Mapping.GetOriginalNonManifoldVertexID(Triangle.A),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.B),
				Mapping.GetOriginalNonManifoldVertexID(Triangle.C));
		};
		auto TriangleLayer = [&](const FIndex3i& ImportIDs)
		{
			const int32 OuterCount =
				(ImportIDs.A < PreShellImportVertexCount ? 1 : 0)
				+ (ImportIDs.B < PreShellImportVertexCount ? 1 : 0)
				+ (ImportIDs.C < PreShellImportVertexCount ? 1 : 0);
			return OuterCount == 3 ? 0 : (OuterCount == 0 ? 1 : 2);
		};
		auto SharesImportVertex = [](const FIndex3i& A, const FIndex3i& B)
		{
			const int32 AValues[3] = {A.A, A.B, A.C};
			const int32 BValues[3] = {B.A, B.B, B.C};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if (AValue == BValue)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SharesSourceOrdinal = [PreShellImportVertexCount](
			const FIndex3i& A,
			const FIndex3i& B)
		{
			const int32 AValues[3] = {
				A.A % PreShellImportVertexCount,
				A.B % PreShellImportVertexCount,
				A.C % PreShellImportVertexCount};
			const int32 BValues[3] = {
				B.A % PreShellImportVertexCount,
				B.B % PreShellImportVertexCount,
				B.C % PreShellImportVertexCount};
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if (AValue == BValue)
					{
						return true;
					}
				}
			}
			return false;
		};
		auto SourceTriangleKey = [PreShellImportVertexCount](const FIndex3i& Imports)
		{
			const int32 A = Imports.A % PreShellImportVertexCount;
			const int32 B = Imports.B % PreShellImportVertexCount;
			const int32 C = Imports.C % PreShellImportVertexCount;
			const int32 Minimum = FMath::Min3(A, B, C);
			const int32 Maximum = FMath::Max3(A, B, C);
			const int32 Middle = A + B + C - Minimum - Maximum;
			return FString::Printf(TEXT("%d,%d,%d"), Minimum, Middle, Maximum);
		};
		auto SourceIntersectionKey = [&SourceTriangleKey](
			const FIndex3i& ImportsA,
			const FIndex3i& ImportsB)
		{
			const FString KeyA = SourceTriangleKey(ImportsA);
			const FString KeyB = SourceTriangleKey(ImportsB);
			return KeyA < KeyB
				? KeyA + TEXT("|") + KeyB
				: KeyB + TEXT("|") + KeyA;
		};

		struct FIntersectionPairEvidence
		{
			uint64 Key = 0;
			FIntPoint TrianglePair = FIntPoint(INDEX_NONE, INDEX_NONE);
			TArray<FVector3d> WitnessPoints;
		};
		TArray<FIntersectionPairEvidence> UniquePairs;
		TMap<uint64, int32> PairIndexByKey;
		auto AppendPairWitness = [&](const int32 TriangleA, const int32 TriangleB, const FVector3d& Point)
		{
			if (!FMath::IsFinite(Point.X)
				|| !FMath::IsFinite(Point.Y)
				|| !FMath::IsFinite(Point.Z))
			{
				return false;
			}
			const uint32 Minimum = static_cast<uint32>(FMath::Min(TriangleA, TriangleB));
			const uint32 Maximum = static_cast<uint32>(FMath::Max(TriangleA, TriangleB));
			const uint64 Key = (static_cast<uint64>(Minimum) << 32) | Maximum;
			int32* ExistingPairIndex = PairIndexByKey.Find(Key);
			if (!ExistingPairIndex)
			{
				const int32 NewPairIndex = UniquePairs.AddDefaulted();
				FIntersectionPairEvidence& Evidence = UniquePairs[NewPairIndex];
				Evidence.Key = Key;
				Evidence.TrianglePair = FIntPoint(
					static_cast<int32>(Minimum),
					static_cast<int32>(Maximum));
				PairIndexByKey.Add(Key, NewPairIndex);
				ExistingPairIndex = PairIndexByKey.Find(Key);
			}
			UniquePairs[*ExistingPairIndex].WitnessPoints.Add(Point);
			return true;
		};

		const FDynamicMeshAABBTree3 ShellSpatial(&Mesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			ShellSpatial.FindAllSelfIntersections(true);
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			if (!AppendPairWitness(
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				Intersection.Point))
			{
				OutError = TEXT("Thickness-shell V3 audit found a non-finite point witness.");
				return false;
			}
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			const FVector3d Midpoint = (Intersection.Point[0] + Intersection.Point[1]) * 0.5;
			if (!AppendPairWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Intersection.Point[0])
				|| !AppendPairWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Intersection.Point[1])
				|| !AppendPairWitness(Intersection.TriangleID[0], Intersection.TriangleID[1], Midpoint))
			{
				OutError = TEXT("Thickness-shell V3 audit found a non-finite segment witness.");
				return false;
			}
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			if (Intersection.Quantity <= 0 || Intersection.Quantity > UE_ARRAY_COUNT(Intersection.Point))
			{
				OutError = TEXT("Thickness-shell V3 audit found an invalid polygon witness.");
				return false;
			}
			FVector3d Centroid = FVector3d::Zero();
			for (int32 PointIndex = 0; PointIndex < Intersection.Quantity; ++PointIndex)
			{
				Centroid += Intersection.Point[PointIndex];
				if (!AppendPairWitness(
					Intersection.TriangleID[0],
					Intersection.TriangleID[1],
					Intersection.Point[PointIndex]))
				{
					OutError = TEXT("Thickness-shell V3 audit found a non-finite polygon witness.");
					return false;
				}
			}
			Centroid /= static_cast<double>(Intersection.Quantity);
			if (!AppendPairWitness(
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				Centroid))
			{
				OutError = TEXT("Thickness-shell V3 audit found a non-finite polygon centroid.");
				return false;
			}
		}
		UniquePairs.Sort([](const FIntersectionPairEvidence& A, const FIntersectionPairEvidence& B)
		{
			return A.Key < B.Key;
		});

		TUniquePtr<FDynamicMeshAABBTree3> ExcludedBodySpatial;
		if (ExcludedBodySurface
			&& ExcludedBodySurface->TriangleCount() > 0
			&& FMath::IsFinite(ExcludedRegionCertificationRadiusCm)
			&& ExcludedRegionCertificationRadiusCm > 0.0)
		{
			ExcludedBodySpatial = MakeUnique<FDynamicMeshAABBTree3>(ExcludedBodySurface, true);
		}
		const double CertificationRadiusSquared = FMath::Square(
			FMath::Max(0.0, ExcludedRegionCertificationRadiusCm));
		const double BaselineInheritanceRadiusSquared = FMath::Square(
			FMath::Max(0.0, BaselineInheritanceRadiusCm));
		auto SharesCoincidentMeshVertexV3 = [&Mesh](
			const int32 TriangleA,
			const int32 TriangleB)
		{
			const FIndex3i VerticesA = Mesh.GetTriangle(TriangleA);
			const FIndex3i VerticesB = Mesh.GetTriangle(TriangleB);
			const int32 AValues[3] = {VerticesA.A, VerticesA.B, VerticesA.C};
			const int32 BValues[3] = {VerticesB.A, VerticesB.B, VerticesB.C};
			constexpr double CoincidentVertexToleranceSquared = 1.e-12;
			for (const int32 AValue : AValues)
			{
				for (const int32 BValue : BValues)
				{
					if ((Mesh.GetVertex(AValue) - Mesh.GetVertex(BValue)).SquaredLength()
						<= CoincidentVertexToleranceSquared)
					{
						return true;
					}
				}
			}
			return false;
		};
		TSet<FString> AffectedSourceTriangles;
		for (const FIntersectionPairEvidence& Evidence : UniquePairs)
		{
			if (!Mesh.IsTriangle(Evidence.TrianglePair.X)
				|| !Mesh.IsTriangle(Evidence.TrianglePair.Y)
				|| Evidence.WitnessPoints.IsEmpty())
			{
				OutError = TEXT("Thickness-shell V3 audit retained invalid pair evidence.");
				return false;
			}
			const FIndex3i ImportsA = TriangleImportIDs(Evidence.TrianglePair.X);
			const FIndex3i ImportsB = TriangleImportIDs(Evidence.TrianglePair.Y);
			const int32 LayerA = TriangleLayer(ImportsA);
			const int32 LayerB = TriangleLayer(ImportsB);
			const bool bSameSurfaceLayer = LayerA == LayerB && LayerA != 2;
			const bool bTouchesWallLayer = LayerA == 2 || LayerB == 2;
			if (SharesImportVertex(ImportsA, ImportsB)
				|| ((bSameSurfaceLayer || bTouchesWallLayer)
					&& (SharesSourceOrdinal(ImportsA, ImportsB)
						|| SharesCoincidentMeshVertexV3(
							Evidence.TrianglePair.X,
							Evidence.TrianglePair.Y))))
			{
				continue;
			}

			++OutAudit.DetectedPairCount;
			if (LayerA == 0 && LayerB == 0)
			{
				++OutAudit.OuterLayerPairCount;
			}
			else if (LayerA == 1 && LayerB == 1)
			{
				++OutAudit.InnerLayerPairCount;
			}
			else
			{
				++OutAudit.CrossLayerOrWallPairCount;
			}

			const FIndex3i NormalizedA(
				ImportsA.A % PreShellImportVertexCount,
				ImportsA.B % PreShellImportVertexCount,
				ImportsA.C % PreShellImportVertexCount);
			const FIndex3i NormalizedB(
				ImportsB.A % PreShellImportVertexCount,
				ImportsB.B % PreShellImportVertexCount,
				ImportsB.C % PreShellImportVertexCount);
			auto WitnessesMatchBaseline = [
				&Evidence,
				BaselineInheritanceRadiusSquared](
				const FSourceIntersectionWitnessEvidence& BaselineEvidence)
			{
				if (BaselineEvidence.WitnessPoints.IsEmpty())
				{
					return false;
				}
				for (const FVector3d& WitnessPoint : Evidence.WitnessPoints)
				{
					double MinimumBaselineDistanceSquared = TNumericLimits<double>::Max();
					for (const FVector3d& BaselinePoint : BaselineEvidence.WitnessPoints)
					{
						MinimumBaselineDistanceSquared = FMath::Min(
							MinimumBaselineDistanceSquared,
							(WitnessPoint - BaselinePoint).SquaredLength());
					}
					if (!FMath::IsFinite(MinimumBaselineDistanceSquared)
						|| MinimumBaselineDistanceSquared
							> BaselineInheritanceRadiusSquared + 1.e-8)
					{
						return false;
					}
				}
				return true;
			};
			bool bAllWitnessesInheritedFromSource = false;
			if (FMath::IsFinite(BaselineInheritanceRadiusCm)
				&& BaselineInheritanceRadiusCm > 0.0)
			{
				const FString ExactSourcePairKey =
					SourceIntersectionKey(NormalizedA, NormalizedB);
				if (const FSourceIntersectionWitnessEvidence* ExactBaseline =
					BaselineEvidenceBySourcePair.Find(ExactSourcePairKey))
				{
					bAllWitnessesInheritedFromSource =
						WitnessesMatchBaseline(*ExactBaseline);
				}
				if (!bAllWitnessesInheritedFromSource)
				{
					for (const TPair<FString, FSourceIntersectionWitnessEvidence>& Pair :
						BaselineEvidenceBySourcePair)
					{
						const FSourceIntersectionWitnessEvidence& Baseline = Pair.Value;
						const bool bSameTopologicalNeighborhood =
							(SharesSourceOrdinal(NormalizedA, Baseline.SourceTriangleA)
								&& SharesSourceOrdinal(NormalizedB, Baseline.SourceTriangleB))
							|| (SharesSourceOrdinal(NormalizedA, Baseline.SourceTriangleB)
								&& SharesSourceOrdinal(NormalizedB, Baseline.SourceTriangleA));
						if (bSameTopologicalNeighborhood
							&& WitnessesMatchBaseline(Baseline))
						{
							bAllWitnessesInheritedFromSource = true;
							break;
						}
					}
				}
			}
			if (bAllWitnessesInheritedFromSource)
			{
				++OutAudit.ToleratedInheritedSourcePairCount;
				continue;
			}

			bool bAllWitnessesInsideExcludedRegion = ExcludedBodySpatial.IsValid();
			double PairMaximumExcludedRegionDistanceCm = 0.0;
			for (const FVector3d& WitnessPoint : Evidence.WitnessPoints)
			{
				double DistanceSquared = TNumericLimits<double>::Max();
				const int32 ExcludedTriangleID = ExcludedBodySpatial
					? ExcludedBodySpatial->FindNearestTriangle(WitnessPoint, DistanceSquared)
					: IndexConstants::InvalidID;
				if (ExcludedTriangleID == IndexConstants::InvalidID
					|| !FMath::IsFinite(DistanceSquared))
				{
					bAllWitnessesInsideExcludedRegion = false;
					continue;
				}
				const double DistanceCm = FMath::Sqrt(FMath::Max(0.0, DistanceSquared));
				PairMaximumExcludedRegionDistanceCm = FMath::Max(
					PairMaximumExcludedRegionDistanceCm,
					DistanceCm);
				bAllWitnessesInsideExcludedRegion &= DistanceSquared
					<= CertificationRadiusSquared + 1.e-8;
			}
			if (bAllWitnessesInsideExcludedRegion)
			{
				++OutAudit.ToleratedExcludedRegionPairCount;
				AffectedSourceTriangles.Add(SourceTriangleKey(ImportsA));
				AffectedSourceTriangles.Add(SourceTriangleKey(ImportsB));
				OutAudit.MaximumExcludedRegionWitnessDistanceCm = FMath::Max(
					OutAudit.MaximumExcludedRegionWitnessDistanceCm,
					PairMaximumExcludedRegionDistanceCm);
				continue;
			}
			const int32 ImportValues[6] = {
				ImportsA.A, ImportsA.B, ImportsA.C,
				ImportsB.A, ImportsB.B, ImportsB.C};
			bool bHasRepairableOuterVertex = false;
			bool bAllOuterVerticesLocallyContracted =
				FMath::IsFinite(LocalRepairThicknessCeilingCm)
				&& LocalRepairThicknessCeilingCm > 0.0;
			for (const int32 ImportValue : ImportValues)
			{
				if (ImportValue >= 0
					&& ImportValue < PreShellImportVertexCount)
				{
					bHasRepairableOuterVertex = true;
					const double LocalThicknessCm =
						(MeanPositionByImportID[ImportValue]
							- MeanPositionByImportID[
								PreShellImportVertexCount + ImportValue]).Length();
					bAllOuterVerticesLocallyContracted &=
						FMath::IsFinite(LocalThicknessCm)
						&& LocalThicknessCm
							<= LocalRepairThicknessCeilingCm + 1.e-6;
				}
			}
			if (bHasRepairableOuterVertex
				&& bAllOuterVerticesLocallyContracted)
			{
				++OutAudit.ToleratedLocalRepairPairCount;
				continue;
			}
			OutAudit.UnsafeSourcePairKeys.Add(
				SourceIntersectionKey(ImportsA, ImportsB));
			for (const int32 ImportValue : ImportValues)
			{
				if (ImportValue >= 0
					&& ImportValue < PreShellImportVertexCount)
				{
					OutAudit.UnsafeOuterSourceOrdinals.Add(ImportValue);
				}
			}
			if (!bHasRepairableOuterVertex)
			{
				++OutAudit.UnrepairableInnerOnlyPairCount;
			}
		}
		OutAudit.AffectedSourceTriangleCount = AffectedSourceTriangles.Num();
		return true;
	}

	static bool ClassifyThicknessShellIntersectionsV3(
		const FEFClothingGarmentRow& CatalogRow,
		const int32 ExcludedBodyTriangleCount,
		const int32 BaselineSourceIntersectionPairCount,
		const double BaselineInheritanceRadiusCm,
		const int32 PreShellTriangleCount,
		const int32 FinalShellTriangleCount,
		const double CertificationRadiusCm,
		const FThicknessShellIntersectionAudit& Audit,
		FString& OutError)
	{
		if (Audit.DetectedPairCount < 0
			|| Audit.ToleratedInheritedSourcePairCount < 0
			|| Audit.ToleratedLocalRepairPairCount < 0
			|| Audit.ToleratedExcludedRegionPairCount < 0
			|| Audit.ToleratedInheritedSourcePairCount
				+ Audit.ToleratedLocalRepairPairCount
				+ Audit.ToleratedExcludedRegionPairCount > Audit.DetectedPairCount
			|| Audit.AffectedSourceTriangleCount < 0
			|| BaselineSourceIntersectionPairCount < 0
			|| !FMath::IsFinite(BaselineInheritanceRadiusCm)
			|| BaselineInheritanceRadiusCm <= 0.0
			|| PreShellTriangleCount <= 0
			|| FinalShellTriangleCount <= 0
			|| !FMath::IsFinite(CertificationRadiusCm)
			|| CertificationRadiusCm <= 0.0
			|| !FMath::IsFinite(Audit.MaximumExcludedRegionWitnessDistanceCm))
		{
			OutError = TEXT("Thickness-shell V3 intersection evidence is invalid.");
			return false;
		}
		if (Audit.DetectedPairCount == 0)
		{
			return Audit.ToleratedInheritedSourcePairCount == 0
				&& Audit.ToleratedLocalRepairPairCount == 0
				&& Audit.ToleratedExcludedRegionPairCount == 0
				&& Audit.AffectedSourceTriangleCount == 0;
		}

		const int32 MaximumInheritedPairCount = FMath::Max(
			64,
			BaselineSourceIntersectionPairCount * 8 + 64);
		const int32 ResidualPairCount = Audit.DetectedPairCount
			- Audit.ToleratedInheritedSourcePairCount
			- Audit.ToleratedLocalRepairPairCount;
		const bool bHasExplicitAnatomyExclusion =
			!CatalogRow.ExcludedBodySurfaceMaterialSlots.IsEmpty()
			&& !CatalogRow.ExcludedBodyBoneBranches.IsEmpty()
			&& ExcludedBodyTriangleCount > 0;
		const int32 MaximumPairCount = FMath::Min(
			512,
			FMath::CeilToInt(
				static_cast<double>(FinalShellTriangleCount)
				* MaximumExcludedAnatomyShellIntersectionFraction));
		const int32 MaximumAffectedSourceTriangleCount = FMath::CeilToInt(
			static_cast<double>(PreShellTriangleCount)
			* MaximumExcludedAnatomyShellIntersectionFraction);
		const bool bInheritedPolicyPass =
			Audit.ToleratedInheritedSourcePairCount <= MaximumInheritedPairCount
			&& (BaselineSourceIntersectionPairCount > 0
				|| Audit.ToleratedInheritedSourcePairCount == 0);
		const int32 MaximumLocallyRepairedPairCount = FMath::Min(
			512,
			FMath::CeilToInt(
				static_cast<double>(FinalShellTriangleCount)
				* MaximumExcludedAnatomyShellIntersectionFraction));
		const bool bLocalRepairPolicyPass =
			Audit.ToleratedLocalRepairPairCount <= MaximumLocallyRepairedPairCount
			&& Audit.UnrepairableInnerOnlyPairCount == 0;
		const bool bResidualPolicyPass = ResidualPairCount == 0
			? Audit.ToleratedExcludedRegionPairCount == 0
				&& Audit.AffectedSourceTriangleCount == 0
			: bHasExplicitAnatomyExclusion
				&& ResidualPairCount == Audit.ToleratedExcludedRegionPairCount
				&& ResidualPairCount <= MaximumPairCount
				&& Audit.AffectedSourceTriangleCount <= MaximumAffectedSourceTriangleCount
				&& Audit.MaximumExcludedRegionWitnessDistanceCm
					<= CertificationRadiusCm + 1.e-4;
		const bool bPass = bInheritedPolicyPass
			&& bLocalRepairPolicyPass
			&& bResidualPolicyPass;
		if (!bPass)
		{
			OutError = FString::Printf(
				TEXT("Thickness-shell V3 intersection policy failed: detected/inherited/localRepair/excluded=%d/%d/%d/%d (inheritedMax %d, localRepairMax %d, residualMax %d), baselinePairs/radius=%d/%.6fcm, affectedExcludedSourceTriangles=%d (max %d), maxExcludedWitnessDistance=%.6fcm (radius %.6fcm), innerOnly=%d, excludedSlots/bones/bodyTriangles=%d/%d/%d."),
				Audit.DetectedPairCount,
				Audit.ToleratedInheritedSourcePairCount,
				Audit.ToleratedLocalRepairPairCount,
				Audit.ToleratedExcludedRegionPairCount,
				MaximumInheritedPairCount,
				MaximumLocallyRepairedPairCount,
				MaximumPairCount,
				BaselineSourceIntersectionPairCount,
				BaselineInheritanceRadiusCm,
				Audit.AffectedSourceTriangleCount,
				MaximumAffectedSourceTriangleCount,
				Audit.MaximumExcludedRegionWitnessDistanceCm,
				CertificationRadiusCm,
				Audit.UnrepairableInnerOnlyPairCount,
				CatalogRow.ExcludedBodySurfaceMaterialSlots.Num(),
				CatalogRow.ExcludedBodyBoneBranches.Num(),
				ExcludedBodyTriangleCount);
		}
		return bPass;
	}

	static bool BuildGeneratedThicknessShell(
		UDynamicMesh* GarmentDynamicMesh,
		const FEFClothingGarmentRow& CatalogRow,
		const TArray<FSurfaceCorrespondence>& SurfaceCorrespondence,
		const FDynamicMesh3* ExcludedBodySurface,
		const int32 ExcludedBodyTriangleCount,
		const double ExcludedRegionCertificationRadiusCm,
		FThicknessShellCompileData& OutShell,
		FString& OutError)
	{
		OutShell = FThicknessShellCompileData();
		if (!CatalogRow.bCreateThicknessShell)
		{
			return true;
		}
		if (!IsValid(GarmentDynamicMesh)
			|| CatalogRow.ShellOffsetSteps < 1
			|| CatalogRow.ShellOffsetSteps > 100
			|| !FMath::IsFinite(CatalogRow.ShellSmoothingPerStep)
			|| CatalogRow.ShellSmoothingPerStep < 0.0f
			|| CatalogRow.ShellSmoothingPerStep > 1.0f
			|| !CatalogRow.bShellOffsetBoundaries)
		{
			OutError = TEXT("Thickness-shell settings are invalid or disable the required boundary offset.");
			return false;
		}

		FDynamicMesh3& Mesh = GarmentDynamicMesh->GetMeshRef();
		if (!Mesh.HasAttributes() || Mesh.VertexCount() <= 0 || Mesh.TriangleCount() <= 0)
		{
			OutError = TEXT("Thickness shell requires a non-empty attributed garment mesh.");
			return false;
		}
		FMeshBoundaryLoops BoundaryLoops(&Mesh, true);
		if (BoundaryLoops.bAborted
			|| BoundaryLoops.bSawOpenSpans
			|| BoundaryLoops.bFellBackToSpansOnFailure
			|| !BoundaryLoops.Spans.IsEmpty()
			|| BoundaryLoops.Loops.IsEmpty())
		{
			OutError = TEXT("Thickness shell requires simple closed garment boundary loops; closed/bowtie/open-span input is rejected.");
			return false;
		}

		OutShell.bEnabled = true;
		// Visible thickness is a per-garment runtime control. Build one stable,
		// conservative reference shell so editing the Director slider never changes
		// topology, content fingerprints or publication identity.
		OutShell.RequestedThicknessCm =
			EFClothingMorphV26::CompiledThicknessReferenceCm;
		OutShell.ExcludedRegionCertificationRadiusCm =
			ExcludedRegionCertificationRadiusCm;
		OutShell.PreShellVertexCount = Mesh.VertexCount();
		OutShell.PreShellTriangleCount = Mesh.TriangleCount();
		OutShell.BoundaryLoopCount = BoundaryLoops.Loops.Num();
		OutShell.SourceVertexIDByOrdinal.Reserve(OutShell.PreShellVertexCount);
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			OutShell.SourceVertexIDByOrdinal.Add(VertexID);
		}
		OutShell.SourceVertexIDByOrdinal.Sort();

		TArray<TArray<int32>> BoundaryVertexLoops;
		TArray<TArray<int32>> BoundaryMaterialIDs;
		BoundaryVertexLoops.Reserve(BoundaryLoops.Loops.Num());
		BoundaryMaterialIDs.Reserve(BoundaryLoops.Loops.Num());
		const FDynamicMeshMaterialAttribute* InitialMaterialIDs = Mesh.Attributes()->HasMaterialID()
			? Mesh.Attributes()->GetMaterialID()
			: nullptr;
		for (const FEdgeLoop& BoundaryLoop : BoundaryLoops.Loops)
		{
			BoundaryVertexLoops.Add(BoundaryLoop.Vertices);
			TArray<int32>& LoopMaterials = BoundaryMaterialIDs.AddDefaulted_GetRef();
			LoopMaterials.SetNum(BoundaryLoop.Vertices.Num());
			for (int32 LoopIndex = 0; LoopIndex < BoundaryLoop.Vertices.Num(); ++LoopIndex)
			{
				const int32 NextLoopIndex = (LoopIndex + 1) % BoundaryLoop.Vertices.Num();
				const int32 EdgeID = Mesh.FindEdge(
					BoundaryLoop.Vertices[LoopIndex],
					BoundaryLoop.Vertices[NextLoopIndex]);
				if (EdgeID == FDynamicMesh3::InvalidID || !Mesh.IsBoundaryEdge(EdgeID))
				{
					OutError = TEXT("Thickness-shell boundary loop contains an invalid source edge.");
					return false;
				}
				const int32 AdjacentTriangleID = Mesh.GetEdgeT(EdgeID).A;
				LoopMaterials[LoopIndex] = InitialMaterialIDs && Mesh.IsTriangle(AdjacentTriangleID)
					? InitialMaterialIDs->GetValue(AdjacentTriangleID)
					: 0;
			}
		}

		const FDynamicMesh3 InnerMesh(Mesh);
		TMap<FString, FSourceIntersectionWitnessEvidence> BaselineIntersectionWitnessPointsBySourcePair;
		if (!CollectSelfIntersectionWitnessEvidence(
			InnerMesh,
			OutShell.PreShellVertexCount,
			BaselineIntersectionWitnessPointsBySourcePair,
			OutShell.BaselineSourceIntersectionPairCount,
			OutError))
		{
			return false;
		}
		OutShell.BaselineInheritanceRadiusCm =
			SurfaceRuntimeMaximumEdgeLengthCm * 0.5
			+ OutShell.RequestedThicknessCm
			+ 0.01;
		FMeshNormals GarmentNormals(&Mesh);
		GarmentNormals.ComputeVertexNormals();
		double OrientationAlignmentSum = 0.0;
		double OrientationAlignmentMagnitude = 0.0;
		for (const int32 SourceVertexID : OutShell.SourceVertexIDByOrdinal)
		{
			if (!SurfaceCorrespondence.IsValidIndex(SourceVertexID))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell source vertex %d has no body-surface correspondence."),
					SourceVertexID);
				return false;
			}
			FVector3d GarmentNormal = GarmentNormals[SourceVertexID];
			FVector3d BodyOutwardNormal = SurfaceCorrespondence[SourceVertexID].SurfaceNormal;
			if (!GarmentNormal.Normalize() || !BodyOutwardNormal.Normalize())
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell source vertex %d has a degenerate orientation frame."),
					SourceVertexID);
				return false;
			}
			const double Alignment = GarmentNormal.Dot(BodyOutwardNormal);
			OrientationAlignmentSum += Alignment;
			OrientationAlignmentMagnitude += FMath::Abs(Alignment);
		}
		if (!FMath::IsFinite(OrientationAlignmentSum)
			|| OrientationAlignmentMagnitude <= 1.e-6)
		{
			OutError = TEXT("Thickness-shell could not resolve an exterior direction from the body surface.");
			return false;
		}
		// Modeling Offset follows the garment winding. Select its sign from the
		// already-certified body correspondence so reversed source winding still
		// grows away from skin. A per-vertex signed check below rejects mixed or
		// locally folded winding instead of ever publishing inward thickness.
		const double SignedOffsetDistanceCm = OrientationAlignmentSum >= 0.0
			? OutShell.RequestedThicknessCm
			: -OutShell.RequestedThicknessCm;
		FGeometryScriptMeshOffsetOptions OffsetOptions;
		OffsetOptions.OffsetDistance = SignedOffsetDistanceCm;
		OffsetOptions.bFixedBoundary = false;
		OffsetOptions.SolveSteps = CatalogRow.ShellOffsetSteps;
		OffsetOptions.SmoothAlpha = CatalogRow.ShellSmoothingPerStep;
		OffsetOptions.bReprojectDuringSmoothing = CatalogRow.bShellReprojectSmooth;
		OffsetOptions.BoundaryAlpha = 0.2f;
		UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshOffset(
			GarmentDynamicMesh,
			OffsetOptions,
			nullptr);

		FDynamicMesh3& OffsetMesh = GarmentDynamicMesh->GetMeshRef();
		if (OffsetMesh.VertexCount() != OutShell.PreShellVertexCount
			|| OffsetMesh.TriangleCount() != OutShell.PreShellTriangleCount)
		{
			OutError = TEXT("Iterative offset unexpectedly changed the source-layer topology.");
			return false;
		}
		for (const int32 SourceVertexID : OutShell.SourceVertexIDByOrdinal)
		{
			if (!OffsetMesh.IsVertex(SourceVertexID))
			{
				OutError = FString::Printf(
					TEXT("Iterative offset lost source vertex %d."),
					SourceVertexID);
				return false;
			}
		}
		double SignedThicknessSum = 0.0;
		double MinimumSignedThicknessCm = TNumericLimits<double>::Max();
		int32 AmbiguousSignedThicknessVertexCount = 0;
		const double MinimumUsefulSignedThicknessCm = FMath::Max(
			1.e-4,
			OutShell.RequestedThicknessCm * 0.10);
		for (const int32 SourceVertexID : OutShell.SourceVertexIDByOrdinal)
		{
			FVector3d BodyOutwardNormal = SurfaceCorrespondence[SourceVertexID].SurfaceNormal;
			if (!BodyOutwardNormal.Normalize())
			{
				OutError = TEXT("Thickness-shell lost a valid body-surface normal after offset.");
				return false;
			}
			const double SignedThicknessCm =
				(OffsetMesh.GetVertex(SourceVertexID) - InnerMesh.GetVertex(SourceVertexID))
				.Dot(BodyOutwardNormal);
			if (!FMath::IsFinite(SignedThicknessCm))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell source vertex %d has non-finite signed thickness."),
					SourceVertexID);
				return false;
			}
			MinimumSignedThicknessCm = FMath::Min(
				MinimumSignedThicknessCm,
				SignedThicknessCm);
			AmbiguousSignedThicknessVertexCount +=
				SignedThicknessCm < MinimumUsefulSignedThicknessCm ? 1 : 0;
			SignedThicknessSum += SignedThicknessCm;
		}
		const double AverageSignedThicknessCm = SignedThicknessSum
			/ static_cast<double>(OutShell.SourceVertexIDByOrdinal.Num());
		const double AmbiguousSignedThicknessFraction =
			static_cast<double>(AmbiguousSignedThicknessVertexCount)
			/ static_cast<double>(OutShell.SourceVertexIDByOrdinal.Num());
		// Concave anatomy (notably the explicitly excluded DAZ genital domain) can
		// expose the opposite nearest-body branch for a small local patch. Defer
		// those vertices to the exclusion-aware render binding below, but reject a
		// globally inward or broadly ambiguous shell here.
		if (AverageSignedThicknessCm < OutShell.RequestedThicknessCm * 0.50
			|| AmbiguousSignedThicknessFraction > 0.10)
		{
			OutError = FString::Printf(
				TEXT("Thickness shell does not remain predominantly exterior to the body (signed min/avg %.6f/%.6fcm, ambiguous %d/%d, requested %.6fcm)."),
				MinimumSignedThicknessCm,
				AverageSignedThicknessCm,
				AmbiguousSignedThicknessVertexCount,
				OutShell.SourceVertexIDByOrdinal.Num(),
				OutShell.RequestedThicknessCm);
			return false;
		}

		TArray<FVector3d> DesiredOuterPositionBySourceOrdinal;
		DesiredOuterPositionBySourceOrdinal.SetNum(
			OutShell.SourceVertexIDByOrdinal.Num());
		for (int32 SourceOrdinal = 0;
			SourceOrdinal < OutShell.SourceVertexIDByOrdinal.Num();
			++SourceOrdinal)
		{
			const int32 SourceVertexID =
				OutShell.SourceVertexIDByOrdinal[SourceOrdinal];
			DesiredOuterPositionBySourceOrdinal[SourceOrdinal] =
				OffsetMesh.GetVertex(SourceVertexID);
		}

		FDynamicMesh3 ReversedInnerMesh(InnerMesh);
		ReversedInnerMesh.ReverseOrientation();
		FDynamicMeshEditor MeshEditor(&OffsetMesh);
		FMeshIndexMappings InnerMappings;
		MeshEditor.AppendMesh(&ReversedInnerMesh, InnerMappings);
		for (int32 BoundaryLoopIndex = 0; BoundaryLoopIndex < BoundaryVertexLoops.Num(); ++BoundaryLoopIndex)
		{
			const TArray<int32>& OuterLoop = BoundaryVertexLoops[BoundaryLoopIndex];
			TArray<int32> InnerLoop;
			InnerLoop.SetNum(OuterLoop.Num());
			for (int32 LoopIndex = 0; LoopIndex < OuterLoop.Num(); ++LoopIndex)
			{
				InnerLoop[LoopIndex] = InnerMappings.GetNewVertex(OuterLoop[LoopIndex]);
				if (!OffsetMesh.IsVertex(InnerLoop[LoopIndex]))
				{
					OutError = TEXT("Thickness shell could not resolve an appended inner boundary vertex.");
					return false;
				}
			}
			FJoinMeshLoops Join(&OffsetMesh, OuterLoop, InnerLoop);
			if (Join.Validate() != EOperationValidationResult::Ok
				|| !Join.Apply()
				|| Join.JoinQuads.Num() != OuterLoop.Num())
			{
				OutError = FString::Printf(
					TEXT("Thickness shell could not stitch boundary loop %d."),
					BoundaryLoopIndex);
				return false;
			}
			FDynamicMeshMaterialAttribute* MaterialIDs = OffsetMesh.Attributes()->HasMaterialID()
				? OffsetMesh.Attributes()->GetMaterialID()
				: nullptr;
			if (MaterialIDs)
			{
				for (int32 QuadIndex = 0; QuadIndex < Join.JoinQuads.Num(); ++QuadIndex)
				{
					const int32 MaterialID = BoundaryMaterialIDs[BoundaryLoopIndex][QuadIndex];
					const FIndex2i WallTriangles = Join.JoinQuads[QuadIndex];
					if (OffsetMesh.IsTriangle(WallTriangles.A))
					{
						MaterialIDs->SetValue(WallTriangles.A, MaterialID);
					}
					if (OffsetMesh.IsTriangle(WallTriangles.B))
					{
						MaterialIDs->SetValue(WallTriangles.B, MaterialID);
					}
				}
			}
			OutShell.WallTriangleCount += Join.JoinTriangles.Num();
		}

		const int32 ExpectedVertexCount = OutShell.PreShellVertexCount * 2;
		const int32 ExpectedTriangleCount = OutShell.PreShellTriangleCount * 2
			+ OutShell.WallTriangleCount;
		if (OffsetMesh.VertexCount() != ExpectedVertexCount
			|| OffsetMesh.TriangleCount() != ExpectedTriangleCount
			|| OffsetMesh.VertexCount() > SurfaceRuntimeMaximumDerivedVertexCount)
		{
			OutError = FString::Printf(
				TEXT("Thickness shell violates its topology/budget contract (vertices %d/%d, triangles %d/%d, cap %d)."),
				OffsetMesh.VertexCount(),
				ExpectedVertexCount,
				OffsetMesh.TriangleCount(),
				ExpectedTriangleCount,
				SurfaceRuntimeMaximumDerivedVertexCount);
			return false;
		}

		TArray<int32> UniqueImportVertexIDs;
		UniqueImportVertexIDs.Init(INDEX_NONE, OffsetMesh.MaxVertexID());
		for (int32 SourceOrdinal = 0; SourceOrdinal < OutShell.SourceVertexIDByOrdinal.Num(); ++SourceOrdinal)
		{
			const int32 OuterVertexID = OutShell.SourceVertexIDByOrdinal[SourceOrdinal];
			const int32 InnerVertexID = InnerMappings.GetNewVertex(OuterVertexID);
			if (!OffsetMesh.IsVertex(OuterVertexID) || !OffsetMesh.IsVertex(InnerVertexID))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell pair %d is invalid before publication."),
					SourceOrdinal);
				return false;
			}
			UniqueImportVertexIDs[OuterVertexID] = SourceOrdinal;
			UniqueImportVertexIDs[InnerVertexID] = OutShell.PreShellVertexCount + SourceOrdinal;
		}
		for (const int32 VertexID : OffsetMesh.VertexIndicesItr())
		{
			if (!UniqueImportVertexIDs.IsValidIndex(VertexID)
				|| UniqueImportVertexIDs[VertexID] == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("Thickness shell produced untracked vertex %d."),
					VertexID);
				return false;
			}
		}
		FNonManifoldMappingSupport::RemoveAllNonManifoldMappingData(OffsetMesh);
		if (!FNonManifoldMappingSupport::AttachNonManifoldVertexMappingData(
			UniqueImportVertexIDs,
			OffsetMesh))
		{
			OutError = TEXT("Could not publish unique outer/inner thickness-shell import identities.");
			return false;
		}

		FMeshNormals::QuickRecomputeOverlayNormals(OffsetMesh);
		FMeshBoundaryLoops ClosedShellBoundaries(&OffsetMesh, true);
		OutShell.OpenBoundaryCountAfter = ClosedShellBoundaries.Loops.Num()
			+ ClosedShellBoundaries.Spans.Num();
		OutShell.DegenerateTriangleCount = 0;
		for (const int32 TriangleID : OffsetMesh.TriangleIndicesItr())
		{
			OutShell.DegenerateTriangleCount += OffsetMesh.GetTriArea(TriangleID) <= 1.e-10 ? 1 : 0;
		}
		FThicknessShellIntersectionAudit IntersectionAudit;
		const double LocalRepairThicknessCeilingCm = FMath::Max(
			1.e-4,
			OutShell.RequestedThicknessCm * 0.15);
		auto MeasureCurrentIntersections = [
			&OffsetMesh,
			&OutShell,
			&BaselineIntersectionWitnessPointsBySourcePair,
			ExcludedBodySurface,
			ExcludedRegionCertificationRadiusCm,
			LocalRepairThicknessCeilingCm,
			&OutError](FThicknessShellIntersectionAudit& OutAudit)
		{
			return MeasureThicknessShellIntersectionsV3(
				OffsetMesh,
				OutShell.PreShellVertexCount,
				BaselineIntersectionWitnessPointsBySourcePair,
				OutShell.BaselineInheritanceRadiusCm,
				ExcludedBodySurface,
				ExcludedRegionCertificationRadiusCm,
				LocalRepairThicknessCeilingCm,
				OutAudit,
				OutError);
		};
		if (!MeasureCurrentIntersections(IntersectionAudit))
		{
			return false;
		}

		// UE's native Create Shell deliberately preserves topology and therefore
		// cannot boolean-away a new local self contact. Repair only the generated
		// outer layer by contracting it along its exact inner->outer pairing. The
		// inner fitted surface, source asset, weights, morphs and skeleton never move.
		auto UnsafePairCount = [](const FThicknessShellIntersectionAudit& Audit)
		{
			return Audit.DetectedPairCount
				- Audit.ToleratedInheritedSourcePairCount
				- Audit.ToleratedLocalRepairPairCount
				- Audit.ToleratedExcludedRegionPairCount;
		};
		constexpr int32 MaximumLocalRepairIterations = 8;
		// MeshDescription stores positions at render precision. Keep a tiny margin
		// above the certification floor so publication/readback cannot quantize a
		// locally repaired pair back below it.
		const double MinimumRepairThicknessCm = FMath::Max(
			1.e-4,
			MinimumCertifiedThicknessPointCm(OutShell.RequestedThicknessCm) * 4.0);
		TArray<double> MinimumThicknessScaleBySourceOrdinal;
		MinimumThicknessScaleBySourceOrdinal.SetNum(OutShell.PreShellVertexCount);
		for (int32 SourceOrdinal = 0;
			SourceOrdinal < OutShell.PreShellVertexCount;
			++SourceOrdinal)
		{
			const int32 OuterVertexID =
				OutShell.SourceVertexIDByOrdinal[SourceOrdinal];
			const int32 InnerVertexID = InnerMappings.GetNewVertex(OuterVertexID);
			if (!OffsetMesh.IsVertex(InnerVertexID))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell pair %d cannot establish its repair floor."),
					SourceOrdinal);
				return false;
			}
			const FVector3d DesiredOffset =
				DesiredOuterPositionBySourceOrdinal[SourceOrdinal]
					- OffsetMesh.GetVertex(InnerVertexID);
			const double DesiredThicknessCm = DesiredOffset.Length();
			if (!FMath::IsFinite(DesiredThicknessCm)
				|| DesiredThicknessCm <= 1.e-8)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell pair %d has invalid repair reach %.8fcm."),
					SourceOrdinal,
					DesiredThicknessCm);
				return false;
			}
			MinimumThicknessScaleBySourceOrdinal[SourceOrdinal] = FMath::Clamp(
				MinimumRepairThicknessCm / DesiredThicknessCm,
				0.002,
				1.0);
		}
		TArray<double> CurrentThicknessScale;
		CurrentThicknessScale.Init(1.0, OutShell.PreShellVertexCount);
		TArray<double> BestThicknessScale = CurrentThicknessScale;
		int32 AcceptedRepairIterationCount = 0;
		UE_LOG(
			LogEFClothingFitCompiler,
			Display,
			TEXT("Thickness-shell local collision repair input: unsafePairs=%d regions=%d outerSeeds=%d innerOnly=%d."),
			UnsafePairCount(IntersectionAudit),
			IntersectionAudit.UnsafeSourcePairKeys.Num(),
			IntersectionAudit.UnsafeOuterSourceOrdinals.Num(),
			IntersectionAudit.UnrepairableInnerOnlyPairCount);
		auto ApplyThicknessScale = [
			&OffsetMesh,
			&OutShell,
			&InnerMappings,
			&DesiredOuterPositionBySourceOrdinal](const TArray<double>& Scale)
		{
			if (Scale.Num() != OutShell.PreShellVertexCount)
			{
				return false;
			}
			for (int32 SourceOrdinal = 0;
				SourceOrdinal < OutShell.PreShellVertexCount;
				++SourceOrdinal)
			{
				const int32 OuterVertexID =
					OutShell.SourceVertexIDByOrdinal[SourceOrdinal];
				const int32 InnerVertexID = InnerMappings.GetNewVertex(OuterVertexID);
				if (!OffsetMesh.IsVertex(OuterVertexID)
					|| !OffsetMesh.IsVertex(InnerVertexID)
					|| !DesiredOuterPositionBySourceOrdinal.IsValidIndex(SourceOrdinal)
					|| !FMath::IsFinite(Scale[SourceOrdinal]))
				{
					return false;
				}
				const FVector3d InnerPosition = OffsetMesh.GetVertex(InnerVertexID);
				OffsetMesh.SetVertex(
					OuterVertexID,
					InnerPosition
						+ (DesiredOuterPositionBySourceOrdinal[SourceOrdinal]
							- InnerPosition)
							* Scale[SourceOrdinal]);
			}
			return true;
		};
		auto IsUnsafeRegionSubset = [](
			const TSet<FString>& Candidate,
			const TSet<FString>& Current)
		{
			for (const FString& CandidateKey : Candidate)
			{
				if (!Current.Contains(CandidateKey))
				{
					return false;
				}
			}
			return true;
		};
		for (int32 RepairIteration = 0;
			RepairIteration < MaximumLocalRepairIterations
				&& UnsafePairCount(IntersectionAudit) > 0;
			++RepairIteration)
		{
			if (IntersectionAudit.UnrepairableInnerOnlyPairCount > 0
				|| IntersectionAudit.UnsafeOuterSourceOrdinals.IsEmpty())
			{
				break;
			}
			TArray<double> RepairWeight;
			RepairWeight.Init(0.0, OutShell.PreShellVertexCount);
			TSet<int32> Frontier = IntersectionAudit.UnsafeOuterSourceOrdinals;
			for (const int32 SourceOrdinal : Frontier)
			{
				if (RepairWeight.IsValidIndex(SourceOrdinal))
				{
					RepairWeight[SourceOrdinal] = 1.0;
				}
			}
			const FNonManifoldMappingSupport ShellMapping(OffsetMesh);
			for (int32 RingIndex = 1; RingIndex <= 2; ++RingIndex)
			{
				TSet<int32> NextFrontier;
				const double RingWeight = RingIndex == 1 ? 0.50 : 0.25;
				for (const int32 SourceOrdinal : Frontier)
				{
					if (!OutShell.SourceVertexIDByOrdinal.IsValidIndex(SourceOrdinal))
					{
						continue;
					}
					const int32 OuterVertexID =
						OutShell.SourceVertexIDByOrdinal[SourceOrdinal];
					for (const int32 NeighborVertexID : OffsetMesh.VtxVerticesItr(OuterVertexID))
					{
						const int32 NeighborImportID =
							ShellMapping.GetOriginalNonManifoldVertexID(NeighborVertexID);
						if (NeighborImportID < 0
							|| NeighborImportID >= OutShell.PreShellVertexCount
							|| !RepairWeight.IsValidIndex(NeighborImportID))
						{
							continue;
						}
						RepairWeight[NeighborImportID] = FMath::Max(
							RepairWeight[NeighborImportID],
							RingWeight);
						NextFrontier.Add(NeighborImportID);
					}
				}
				Frontier = MoveTemp(NextFrontier);
			}

			const int32 CurrentUnsafePairCount = UnsafePairCount(IntersectionAudit);
			const int32 CurrentUnsafeRegionCount =
				IntersectionAudit.UnsafeSourcePairKeys.Num();
			bool bFoundImprovement = false;
			FThicknessShellIntersectionAudit BestAudit = IntersectionAudit;
			const double CandidateAmplitudes[] = {0.25, 0.50, 0.75, 1.0};
			for (const double Amplitude : CandidateAmplitudes)
			{
				TArray<double> CandidateScale = CurrentThicknessScale;
				double CandidateScaleSum = 0.0;
				for (int32 SourceOrdinal = 0;
					SourceOrdinal < CandidateScale.Num();
					++SourceOrdinal)
				{
					const double MinimumThicknessScale =
						MinimumThicknessScaleBySourceOrdinal[SourceOrdinal];
					CandidateScale[SourceOrdinal] = FMath::Clamp(
						CurrentThicknessScale[SourceOrdinal]
							- Amplitude * RepairWeight[SourceOrdinal]
								* (CurrentThicknessScale[SourceOrdinal]
									- MinimumThicknessScale),
						MinimumThicknessScale,
						1.0);
					CandidateScaleSum += CandidateScale[SourceOrdinal];
				}
				const double CandidateAverageScale = CandidateScaleSum
					/ static_cast<double>(CandidateScale.Num());
				if (CandidateAverageScale < 0.90
					|| !ApplyThicknessScale(CandidateScale))
				{
					continue;
				}
				FThicknessShellIntersectionAudit CandidateAudit;
				if (!MeasureCurrentIntersections(CandidateAudit))
				{
					return false;
				}
				int32 CandidateDegenerateTriangleCount = 0;
				for (const int32 TriangleID : OffsetMesh.TriangleIndicesItr())
				{
					CandidateDegenerateTriangleCount +=
						OffsetMesh.GetTriArea(TriangleID) <= 1.e-10 ? 1 : 0;
				}
				const int32 CandidateUnsafePairCount =
					UnsafePairCount(CandidateAudit);
				const bool bStrictlyImproves =
					CandidateUnsafePairCount < CurrentUnsafePairCount
					|| (CandidateUnsafePairCount == CurrentUnsafePairCount
						&& CandidateAudit.UnsafeSourcePairKeys.Num()
							< CurrentUnsafeRegionCount);
				const bool bBeatsBest = !bFoundImprovement
					|| CandidateUnsafePairCount < UnsafePairCount(BestAudit)
					|| (CandidateUnsafePairCount == UnsafePairCount(BestAudit)
						&& CandidateAudit.DetectedPairCount < BestAudit.DetectedPairCount);
				UE_LOG(
					LogEFClothingFitCompiler,
					Display,
					TEXT("Thickness-shell repair candidate iteration=%d amplitude=%.2f unsafe=%d regions=%d newRegion=%d degenerate=%d innerOnly=%d avgScale=%.4f."),
					RepairIteration,
					Amplitude,
					CandidateUnsafePairCount,
					CandidateAudit.UnsafeSourcePairKeys.Num(),
					IsUnsafeRegionSubset(
						CandidateAudit.UnsafeSourcePairKeys,
						IntersectionAudit.UnsafeSourcePairKeys) ? 0 : 1,
					CandidateDegenerateTriangleCount,
					CandidateAudit.UnrepairableInnerOnlyPairCount,
					CandidateAverageScale);
				if (CandidateDegenerateTriangleCount == 0
					&& CandidateAudit.UnrepairableInnerOnlyPairCount == 0
					&& CandidateAudit.UnsafeSourcePairKeys.Num()
						<= CurrentUnsafeRegionCount
					&& bStrictlyImproves
					&& bBeatsBest)
				{
					bFoundImprovement = true;
					BestThicknessScale = MoveTemp(CandidateScale);
					BestAudit = MoveTemp(CandidateAudit);
				}
			}
			if (!bFoundImprovement)
			{
				ApplyThicknessScale(CurrentThicknessScale);
				break;
			}
			CurrentThicknessScale = BestThicknessScale;
			IntersectionAudit = MoveTemp(BestAudit);
			++AcceptedRepairIterationCount;
			if (!ApplyThicknessScale(CurrentThicknessScale))
			{
				OutError = TEXT("Thickness-shell local collision repair lost its outer/inner pairing.");
				return false;
			}
		}
		FMeshNormals::QuickRecomputeOverlayNormals(OffsetMesh);
		OutShell.DegenerateTriangleCount = 0;
		for (const int32 TriangleID : OffsetMesh.TriangleIndicesItr())
		{
			OutShell.DegenerateTriangleCount +=
				OffsetMesh.GetTriArea(TriangleID) <= 1.e-10 ? 1 : 0;
		}
		if (!MeasureCurrentIntersections(IntersectionAudit))
		{
			return false;
		}
		if (AcceptedRepairIterationCount > 0)
		{
			int32 AdjustedVertexCount = 0;
			double MinimumAppliedScale = 1.0;
			double AverageAppliedScale = 0.0;
			for (const double Scale : CurrentThicknessScale)
			{
				AdjustedVertexCount += Scale < 1.0 - 1.e-6 ? 1 : 0;
				MinimumAppliedScale = FMath::Min(MinimumAppliedScale, Scale);
				AverageAppliedScale += Scale;
			}
			AverageAppliedScale /= static_cast<double>(CurrentThicknessScale.Num());
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("Thickness-shell local collision repair accepted %d iterations: adjusted=%d/%d scale min/avg=%.4f/%.4f residualPairs=%d."),
				AcceptedRepairIterationCount,
				AdjustedVertexCount,
				CurrentThicknessScale.Num(),
				MinimumAppliedScale,
				AverageAppliedScale,
				UnsafePairCount(IntersectionAudit));
		}
		OutShell.DetectedNonAdjacentIntersectionCount =
			IntersectionAudit.DetectedPairCount;
		OutShell.ToleratedInheritedSourceIntersectionCount =
			IntersectionAudit.ToleratedInheritedSourcePairCount;
		OutShell.ToleratedLocalRepairIntersectionCount =
			IntersectionAudit.ToleratedLocalRepairPairCount;
		OutShell.LocalRepairThicknessCeilingCm =
			LocalRepairThicknessCeilingCm;
		OutShell.ToleratedExcludedRegionIntersectionCount =
			IntersectionAudit.ToleratedExcludedRegionPairCount;
		OutShell.ExcludedRegionAffectedSourceTriangleCount =
			IntersectionAudit.AffectedSourceTriangleCount;
		OutShell.ExcludedRegionMaximumWitnessDistanceCm =
			IntersectionAudit.MaximumExcludedRegionWitnessDistanceCm;
		OutShell.bSelfIntersects = IntersectionAudit.DetectedPairCount > 0;
		FString IntersectionPolicyError;
		const bool bIntersectionPolicyPass = ClassifyThicknessShellIntersectionsV3(
			CatalogRow,
			ExcludedBodyTriangleCount,
			OutShell.BaselineSourceIntersectionPairCount,
			OutShell.BaselineInheritanceRadiusCm,
			OutShell.PreShellTriangleCount,
			ExpectedTriangleCount,
			ExcludedRegionCertificationRadiusCm,
			IntersectionAudit,
			IntersectionPolicyError);
		if (ClosedShellBoundaries.bAborted
			|| OutShell.OpenBoundaryCountAfter != 0
			|| OutShell.DegenerateTriangleCount != 0
			|| !bIntersectionPolicyPass)
		{
			OutError = FString::Printf(
				TEXT("Thickness shell failed closed-surface certification (open=%d, degenerate=%d, detected/inherited/excluded=%d/%d/%d, baselinePairs/radius=%d/%.6fcm, affectedResidualSource=%d, maxExcludedWitnessDistance/radius=%.6f/%.6fcm, outer/inner/cross=%d/%d/%d, policy=%s)."),
				OutShell.OpenBoundaryCountAfter,
				OutShell.DegenerateTriangleCount,
				IntersectionAudit.DetectedPairCount,
				IntersectionAudit.ToleratedInheritedSourcePairCount,
				IntersectionAudit.ToleratedExcludedRegionPairCount,
				OutShell.BaselineSourceIntersectionPairCount,
				OutShell.BaselineInheritanceRadiusCm,
				IntersectionAudit.AffectedSourceTriangleCount,
				IntersectionAudit.MaximumExcludedRegionWitnessDistanceCm,
				ExcludedRegionCertificationRadiusCm,
				IntersectionAudit.OuterLayerPairCount,
				IntersectionAudit.InnerLayerPairCount,
				IntersectionAudit.CrossLayerOrWallPairCount,
				IntersectionPolicyError.IsEmpty() ? TEXT("none") : *IntersectionPolicyError);
			return false;
		}
		OutShell.FinalShellVertexCount = ExpectedVertexCount;
		OutShell.FinalShellTriangleCount = ExpectedTriangleCount;
		return RebuildThicknessShellPairing(OffsetMesh, OutShell, OutError);
	}

	static bool WriteGeneratedSurfaceTopology(
		UDynamicMesh* GarmentDynamicMesh,
		USkeletalMesh* Derived,
		FString& OutError,
		bool bRecomputeTangents = false)
	{
		if (!IsValid(GarmentDynamicMesh) || !IsValid(Derived))
		{
			OutError = TEXT("Cannot publish invalid generated SurfaceWrap topology.");
			return false;
		}

		FGeometryScriptCopyMeshToAssetOptions WriteOptions;
		WriteOptions.bEnableRecomputeNormals = false;
		WriteOptions.bEnableRecomputeTangents = bRecomputeTangents;
		WriteOptions.bEnableRemoveDegenerates = false;
		WriteOptions.BoneHierarchyMismatchHandling =
			EGeometryScriptBoneHierarchyMismatchHandling::RemapGeometryToReferenceSkeleton;
		WriteOptions.bUseOriginalVertexOrder = true;
		WriteOptions.bUseBuildScale = true;
		WriteOptions.bReplaceMaterials = false;
		WriteOptions.bEmitTransaction = false;
		WriteOptions.bDeferMeshPostEditChange = false;

		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = 0;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToSkeletalMesh(
			GarmentDynamicMesh,
			Derived,
			WriteOptions,
			WriteLOD,
			Outcome,
			nullptr);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = FString::Printf(
				TEXT("Failed to commit densified SurfaceWrap LOD0 to %s."),
				*Derived->GetPathName());
			return false;
		}
		FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});
		return true;
	}

	static bool RebindGeneratedMeshToBody(
		USkeletalMesh* Derived,
		const USkeletalMesh* BodySurface,
		FString& OutError)
	{
		if (!IsValid(Derived) || !IsValid(BodySurface))
		{
			OutError = TEXT("Generated/body mesh is invalid during bind-pose synchronization.");
			return false;
		}
		FString HierarchyError;
		if (!EFClothingSkeleton::AreBoneHierarchiesCompatible(
			Derived, BodySurface, &HierarchyError))
		{
			OutError = FString::Printf(
				TEXT("Generated/body hierarchy differs before rebind: %s"),
				*HierarchyError);
			return false;
		}

		if (Derived->GetNumSourceModels() < 1)
		{
			OutError = TEXT("Generated garment has no source LOD0 for body rebind.");
			return false;
		}
		// UObject::Modify returns whether it captured a transaction snapshot, not
		// whether the MeshDescription is writable. Commandlets intentionally have
		// no undo transaction, so validate the editable description itself below.
		Derived->ModifyMeshDescription(0, true);
		FMeshDescription* MeshDescription = Derived->GetMeshDescription(0);
		if (!MeshDescription)
		{
			OutError = TEXT("Generated garment has no editable LOD0 MeshDescription for body rebind.");
			return false;
		}
		FSkeletalMeshAttributes MeshAttributes(*MeshDescription);
		MeshAttributes.Register(true);
		const FReferenceSkeleton& BodyReference = BodySurface->GetRefSkeleton();
		if (!MeshAttributes.HasBones()
			|| MeshAttributes.GetNumBones() != BodyReference.GetRawBoneNum())
		{
			OutError = FString::Printf(
				TEXT("Generated LOD0 bone table count differs from body (%d vs %d)."),
				MeshAttributes.GetNumBones(),
				BodyReference.GetRawBoneNum());
			return false;
		}

		FSkeletalMeshAttributes::FBoneNameAttributesRef BoneNames = MeshAttributes.GetBoneNames();
		FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParents =
			MeshAttributes.GetBoneParentIndices();
		FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses = MeshAttributes.GetBonePoses();
		for (int32 BoneIndex = 0; BoneIndex < BodyReference.GetRawBoneNum(); ++BoneIndex)
		{
			const FBoneID BoneID(BoneIndex);
			if (!MeshAttributes.Bones().IsValid(BoneID)
				|| BoneNames.Get(BoneID) != BodyReference.GetBoneName(BoneIndex)
				|| BoneParents.Get(BoneID) != BodyReference.GetParentIndex(BoneIndex))
			{
				OutError = FString::Printf(
					TEXT("Generated LOD0 hierarchy differs from body at raw bone index %d."),
					BoneIndex);
				return false;
			}
			BonePoses.Set(BoneID, BodyReference.GetRefBonePose()[BoneIndex]);
		}

		USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
		CommitParams.bUpdateMorphTargets = false;
		CommitParams.bUpdateSkinWeightProfiles = false;
		CommitParams.bUpdateVertexAttributes = false;
		CommitParams.bUpdateVertexColors = false;
		CommitParams.bMarkPackageDirty = true;
		CommitParams.bForceUpdate = true;
		if (!Derived->CommitMeshDescription(0, CommitParams))
		{
			OutError = TEXT("Failed to commit the generated LOD0 body bind pose.");
			return false;
		}
		FMeshDescription* CommittedDescription = Derived->GetMeshDescription(0);
		if (!CommittedDescription)
		{
			OutError = TEXT("Generated LOD0 MeshDescription disappeared after body rebind commit.");
			return false;
		}
		FSkeletalMeshAttributes CommittedAttributes(*CommittedDescription);
		const FSkeletalMeshAttributes::FBoneNameAttributesConstRef CommittedNames =
			CommittedAttributes.GetBoneNames();
		const FSkeletalMeshAttributes::FBoneParentIndexAttributesConstRef CommittedParents =
			CommittedAttributes.GetBoneParentIndices();
		const FSkeletalMeshAttributes::FBonePoseAttributesConstRef CommittedPoses =
			CommittedAttributes.GetBonePoses();
		for (int32 BoneIndex = 0; BoneIndex < BodyReference.GetRawBoneNum(); ++BoneIndex)
		{
			const FBoneID BoneID(BoneIndex);
			if (!CommittedAttributes.Bones().IsValid(BoneID)
				|| CommittedNames.Get(BoneID) != BodyReference.GetBoneName(BoneIndex)
				|| CommittedParents.Get(BoneID) != BodyReference.GetParentIndex(BoneIndex)
				|| !CommittedPoses.Get(BoneID).Equals(
					BodyReference.GetRefBonePose()[BoneIndex], 1.e-4f))
			{
				OutError = FString::Printf(
					TEXT("Generated LOD0 body bind readback failed at raw bone index %d."),
					BoneIndex);
				return false;
			}
		}

		Derived->SetRefSkeleton(BodyReference);
		Derived->GetRefBasesInvMatrix().Reset();
		Derived->CalculateInvRefMatrices();
		Derived->MarkPackageDirty();
		FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});
		if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(
			Derived, BodySurface, &HierarchyError))
		{
			OutError = FString::Printf(
				TEXT("Generated garment does not exactly match the body bind pose after commit: %s"),
				*HierarchyError);
			return false;
		}
		return true;
	}

	static bool ValidateGeneratedBodyBindArtifacts(
		const USkeletalMesh* Derived,
		const USkeletalMesh* BodySurface,
		FString& OutError)
	{
		if (!IsValid(Derived) || !IsValid(BodySurface))
		{
			OutError = TEXT("Generated/body mesh is invalid during final bind validation.");
			return false;
		}

		FString BindError;
		if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(
			Derived, BodySurface, &BindError))
		{
			OutError = FString::Printf(
				TEXT("Generated garment reference pose differs from Female: %s"),
				*BindError);
			return false;
		}

		const FReferenceSkeleton& BodyReference = BodySurface->GetRefSkeleton();
		const FMeshDescription* Description = Derived->GetMeshDescription(0);
		if (!Description)
		{
			OutError = TEXT("Generated garment has no LOD0 MeshDescription for final bind validation.");
			return false;
		}
		const FSkeletalMeshConstAttributes Attributes(*Description);
		if (!Attributes.HasBones() || Attributes.GetNumBones() != BodyReference.GetRawBoneNum())
		{
			OutError = TEXT("Generated LOD0 bone table does not match Female during final bind validation.");
			return false;
		}
		const auto BoneNames = Attributes.GetBoneNames();
		const auto BoneParents = Attributes.GetBoneParentIndices();
		const auto BonePoses = Attributes.GetBonePoses();
		for (int32 BoneIndex = 0; BoneIndex < BodyReference.GetRawBoneNum(); ++BoneIndex)
		{
			const FBoneID BoneID(BoneIndex);
			if (!Attributes.Bones().IsValid(BoneID)
				|| BoneNames.Get(BoneID) != BodyReference.GetBoneName(BoneIndex)
				|| BoneParents.Get(BoneID) != BodyReference.GetParentIndex(BoneIndex)
				|| !BonePoses.Get(BoneID).Equals(BodyReference.GetRefBonePose()[BoneIndex], 1.e-4f))
			{
				OutError = FString::Printf(
					TEXT("Generated LOD0 bind table differs from Female at raw bone index %d (%s)."),
					BoneIndex,
					*BodyReference.GetBoneName(BoneIndex).ToString());
				return false;
			}
		}

		const TArray<FMatrix44f>& DerivedInverseBind = Derived->GetRefBasesInvMatrix();
		const TArray<FMatrix44f>& BodyInverseBind = BodySurface->GetRefBasesInvMatrix();
		if (DerivedInverseBind.Num() != BodyInverseBind.Num()
			|| DerivedInverseBind.Num() != BodyReference.GetRawBoneNum())
		{
			OutError = FString::Printf(
				TEXT("Generated/Female inverse-bind counts differ (%d/%d, expected %d)."),
				DerivedInverseBind.Num(),
				BodyInverseBind.Num(),
				BodyReference.GetRawBoneNum());
			return false;
		}
		for (int32 BoneIndex = 0; BoneIndex < DerivedInverseBind.Num(); ++BoneIndex)
		{
			if (!DerivedInverseBind[BoneIndex].Equals(BodyInverseBind[BoneIndex], 1.e-4f))
			{
				OutError = FString::Printf(
					TEXT("Generated inverse bind differs from Female at raw bone index %d (%s)."),
					BoneIndex,
					*BodyReference.GetBoneName(BoneIndex).ToString());
				return false;
			}
		}
		return true;
	}

	static bool ValidateGeneratedBodyDeformerParity(
		const USkeletalMesh* Derived,
		const USkeletalMesh* BodySurface,
		FString& OutError)
	{
		if (!IsValid(Derived) || !IsValid(BodySurface))
		{
			OutError = TEXT("Generated/body mesh is invalid during deformer validation.");
			return false;
		}
		if (Derived->GetDefaultMeshDeformer() != BodySurface->GetDefaultMeshDeformer()
			|| Derived->GetTargetMeshDeformers() != BodySurface->GetTargetMeshDeformers())
		{
			OutError = TEXT("Generated garment does not preserve Female's default/target mesh-deformer contract.");
			return false;
		}
		const FSkeletalMeshLODInfo* DerivedLODInfo = Derived->GetLODInfo(0);
		const FSkeletalMeshLODInfo* BodyLODInfo = BodySurface->GetLODInfo(0);
		if (!DerivedLODInfo
			|| !BodyLODInfo
			|| DerivedLODInfo->bAllowMeshDeformer != BodyLODInfo->bAllowMeshDeformer
			|| DerivedLODInfo->bBuildHalfEdgeBuffers != BodyLODInfo->bBuildHalfEdgeBuffers
			|| Derived->HasHalfEdgeBuffer(0) != BodySurface->HasHalfEdgeBuffer(0))
		{
			OutError = TEXT("Generated garment does not preserve Female's LOD0 deformer/half-edge build contract.");
			return false;
		}
		return true;
	}

	static USkeletalMesh* FindOrDuplicateDerived(
		USkeletalMesh* SourceGarment,
		const FString& OutputRoot,
		const FString& ArtifactKey,
		FString& OutError)
	{
		const FString AssetName = FString::Printf(
			TEXT("SK_%s_%s_EFV2_%s"),
			*SanitizeAssetName(SourceGarment->GetName()),
			*BuildSourceKey(SourceGarment),
			*ArtifactKey);
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *OutputRoot, *AssetName, *AssetName);
		if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *ObjectPath))
		{
			OutError = FString::Printf(
				TEXT("Fresh publication key collided with existing generated mesh %s."),
				*Existing->GetPathName());
			return nullptr;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		USkeletalMesh* Derived = Cast<USkeletalMesh>(AssetTools.DuplicateAsset(AssetName, OutputRoot, SourceGarment));
		if (!Derived)
		{
			OutError = FString::Printf(TEXT("Could not duplicate %s into %s."), *SourceGarment->GetPathName(), *OutputRoot);
		}
		return Derived;
	}

	template<typename T>
	static T* FindOrCreateDataAsset(const FString& PackagePath, const FString& AssetName)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
		if (T* Existing = LoadObject<T>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		const FString PackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	static bool SaveAsset(UObject* Asset, FString& OutError)
	{
		if (!IsValid(Asset) || !Asset->GetOutermost())
		{
			OutError = TEXT("Cannot save an invalid generated asset.");
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Failed to save %s."), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	static FVector3d InterpolateNormal(
		const FDynamicMesh3& BodyMesh,
		const FMeshNormals& BodyNormals,
		int32 TriangleID,
		const FVector3d& Barycentric)
	{
		const FIndex3i Triangle = BodyMesh.GetTriangle(TriangleID);
		FVector3d Normal = BodyNormals[Triangle.A] * Barycentric.X
			+ BodyNormals[Triangle.B] * Barycentric.Y
			+ BodyNormals[Triangle.C] * Barycentric.Z;
		if (!Normal.Normalize())
		{
			Normal = BodyMesh.GetTriNormal(TriangleID);
		}
		return Normal;
	}

	static bool BuildCorrespondenceAndClearance(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		double DesiredClearance,
		double MaximumPush,
		int32 SmoothingIterations,
		TArray<FSurfaceCorrespondence>& OutCorrespondence,
		TArray<FVector3d>& OutDeltas,
		int32& OutPenetratingBefore,
		int32& OutPenetratingAfter,
		double& OutMinimumBefore,
		double& OutMinimumAfter,
		FString& OutError)
	{
		OutError.Reset();
		FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		FMeshNormals GarmentNormals(&GarmentMesh);
		GarmentNormals.ComputeVertexNormals();
		const FVector3d BodyCenter = BodyMesh.GetBounds().Center();
		const double MaximumCorrespondenceDistance = FMath::Clamp(
			MaximumPush + DesiredClearance + 1.0,
			2.0,
			8.0);
		constexpr double MinimumNormalAlignment = 0.35;
		constexpr double MinimumRadialAlignment = 0.0;
		constexpr double NearContactFallbackRadialAlignment = 0.5;
		constexpr double NearContactFallbackDistanceCm = 0.75;
		constexpr double BarycentricTolerance = 1.e-4;

		auto IsFiniteVector = [](const FVector3d& Vector) -> bool
		{
			return FMath::IsFinite(Vector.X)
				&& FMath::IsFinite(Vector.Y)
				&& FMath::IsFinite(Vector.Z);
		};
		auto BuildGarmentFrame = [&](
			int32 VertexID,
			const FVector3d& Position,
			FVector3d& OutOutwardNormal,
			FVector3d& OutRadial,
			bool& bOutHasRadial) -> bool
		{
			OutOutwardNormal = GarmentNormals[VertexID];
			if (!IsFiniteVector(OutOutwardNormal) || !OutOutwardNormal.Normalize())
			{
				return false;
			}
			OutRadial = Position - BodyCenter;
			bOutHasRadial = IsFiniteVector(OutRadial) && OutRadial.Normalize();
			if (bOutHasRadial && OutOutwardNormal.Dot(OutRadial) < 0.0)
			{
				OutOutwardNormal = -OutOutwardNormal;
			}
			return true;
		};
		auto FindValidatedSurface = [&](
			const FVector3d& QueryPoint,
			const FVector3d& GarmentOutwardNormal,
			const FVector3d& GarmentRadial,
			bool bHasGarmentRadial,
			bool bAllowNearContactNormalFallback,
			int32& OutTriangleID,
			FVector3d& OutClosestPoint,
			FVector3d& OutBarycentric,
			FVector3d& OutSurfaceNormal) -> bool
		{
			if (!IsFiniteVector(QueryPoint))
			{
				return false;
			}
			IMeshSpatial::FQueryOptions QueryOptions(MaximumCorrespondenceDistance);
			QueryOptions.TriangleFilterF = [&](int32 CandidateTriangleID)
			{
				if (!BodyMesh.IsTriangle(CandidateTriangleID))
				{
					return false;
				}
				const FDistPoint3Triangle3d CandidateQuery =
					TMeshQueries<FDynamicMesh3>::TriangleDistance(
						BodyMesh, CandidateTriangleID, QueryPoint);
				FVector3d CandidateNormal = InterpolateNormal(
					BodyMesh,
					BodyNormals,
					CandidateTriangleID,
					CandidateQuery.TriangleBaryCoords);
				if (!IsFiniteVector(CandidateNormal)
					|| !CandidateNormal.Normalize()
					|| CandidateNormal.Dot(GarmentOutwardNormal) < MinimumNormalAlignment)
				{
					return false;
				}
				if (bHasGarmentRadial)
				{
					FVector3d CandidateRadial = CandidateQuery.ClosestTrianglePoint - BodyCenter;
					if (IsFiniteVector(CandidateRadial)
						&& CandidateRadial.Normalize()
						&& CandidateRadial.Dot(GarmentRadial) < MinimumRadialAlignment)
					{
						return false;
					}
				}
				return true;
			};

			double DistanceSquared = TNumericLimits<double>::Max();
			OutTriangleID = BodySpatial.FindNearestTriangle(QueryPoint, DistanceSquared, QueryOptions);
			bool bUsedNearContactNormalFallback = false;
			if (OutTriangleID == IndexConstants::InvalidID && bAllowNearContactNormalFallback)
			{
				double FallbackDistanceSquared = TNumericLimits<double>::Max();
				const int32 FallbackTriangleID = BodySpatial.FindNearestTriangle(
					QueryPoint, FallbackDistanceSquared);
				if (FallbackTriangleID != IndexConstants::InvalidID
					&& FMath::IsFinite(FallbackDistanceSquared)
					&& FallbackDistanceSquared <= FMath::Square(NearContactFallbackDistanceCm) + 1.e-6)
				{
					const FDistPoint3Triangle3d FallbackQuery =
						TMeshQueries<FDynamicMesh3>::TriangleDistance(
							BodyMesh, FallbackTriangleID, QueryPoint);
					FVector3d FallbackRadial = FallbackQuery.ClosestTrianglePoint - BodyCenter;
					const bool bFallbackRadialValid = IsFiniteVector(FallbackRadial)
						&& FallbackRadial.Normalize();
					if (!bHasGarmentRadial
						|| (bFallbackRadialValid
							&& FallbackRadial.Dot(GarmentRadial) >= NearContactFallbackRadialAlignment))
					{
						OutTriangleID = FallbackTriangleID;
						DistanceSquared = FallbackDistanceSquared;
						bUsedNearContactNormalFallback = true;
					}
				}
			}
			if (OutTriangleID == IndexConstants::InvalidID
				|| !FMath::IsFinite(DistanceSquared)
				|| DistanceSquared > FMath::Square(MaximumCorrespondenceDistance) + 1.e-6)
			{
				return false;
			}

			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh, OutTriangleID, QueryPoint);
			OutClosestPoint = Query.ClosestTrianglePoint;
			OutBarycentric = Query.TriangleBaryCoords;
			const double BarycentricSum = OutBarycentric.X + OutBarycentric.Y + OutBarycentric.Z;
			if (!IsFiniteVector(OutClosestPoint)
				|| !IsFiniteVector(OutBarycentric)
				|| OutBarycentric.X < -BarycentricTolerance
				|| OutBarycentric.Y < -BarycentricTolerance
				|| OutBarycentric.Z < -BarycentricTolerance
				|| OutBarycentric.X > 1.0 + BarycentricTolerance
				|| OutBarycentric.Y > 1.0 + BarycentricTolerance
				|| OutBarycentric.Z > 1.0 + BarycentricTolerance
				|| !FMath::IsNearlyEqual(BarycentricSum, 1.0, BarycentricTolerance))
			{
				return false;
			}
			OutSurfaceNormal = InterpolateNormal(BodyMesh, BodyNormals, OutTriangleID, OutBarycentric);
			return IsFiniteVector(OutSurfaceNormal)
				&& OutSurfaceNormal.Normalize()
				&& (bUsedNearContactNormalFallback
					|| OutSurfaceNormal.Dot(GarmentOutwardNormal) >= MinimumNormalAlignment);
		};
		auto DescribeUnrestrictedNearest = [&](
			const FVector3d& QueryPoint,
			const FVector3d& AnchorNormal,
			const FVector3d& AnchorRadial,
			bool bHasAnchorRadial) -> FString
		{
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(QueryPoint, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID || !FMath::IsFinite(DistanceSquared))
			{
				return TEXT("unrestricted nearest triangle is invalid");
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh, TriangleID, QueryPoint);
			FVector3d SurfaceNormal = InterpolateNormal(
				BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			const bool bHasSurfaceNormal = IsFiniteVector(SurfaceNormal) && SurfaceNormal.Normalize();
			FVector3d SurfaceRadial = Query.ClosestTrianglePoint - BodyCenter;
			const bool bHasSurfaceRadial = IsFiniteVector(SurfaceRadial) && SurfaceRadial.Normalize();
			return FString::Printf(
				TEXT("nearestTriangle=%d distance=%.6fcm normalDot=%.6f radialDot=%.6f maxDistance=%.6fcm"),
				TriangleID,
				FMath::Sqrt(FMath::Max(0.0, DistanceSquared)),
				bHasSurfaceNormal ? SurfaceNormal.Dot(AnchorNormal) : -2.0,
				bHasAnchorRadial && bHasSurfaceRadial ? SurfaceRadial.Dot(AnchorRadial) : 2.0,
				MaximumCorrespondenceDistance);
		};

		OutCorrespondence.SetNum(GarmentMesh.MaxVertexID());
		OutDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		OutPenetratingBefore = 0;
		OutMinimumBefore = TNumericLimits<double>::Max();

		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d Position = GarmentMesh.GetVertex(VertexID);
			FVector3d GarmentOutwardNormal;
			FVector3d GarmentRadial;
			bool bHasGarmentRadial = false;
			if (!BuildGarmentFrame(
				VertexID, Position, GarmentOutwardNormal, GarmentRadial, bHasGarmentRadial))
			{
				OutError = FString::Printf(TEXT("Garment vertex %d has a non-finite or degenerate normal."), VertexID);
				return false;
			}
			int32 TriangleID = INDEX_NONE;
			FVector3d ClosestPoint;
			FVector3d Barycentric;
			FVector3d Normal;
			if (!FindValidatedSurface(
				Position,
				GarmentOutwardNormal,
				GarmentRadial,
				bHasGarmentRadial,
				true,
				TriangleID,
				ClosestPoint,
				Barycentric,
				Normal))
			{
				OutError = FString::Printf(
					TEXT("Initial surface correspondence rejected garment vertex %d: %s."),
					VertexID,
					*DescribeUnrestrictedNearest(
						Position, GarmentOutwardNormal, GarmentRadial, bHasGarmentRadial));
				return false;
			}
			const double SignedGap = (Position - ClosestPoint).Dot(Normal);
			if (!FMath::IsFinite(SignedGap))
			{
				OutError = FString::Printf(TEXT("Initial signed gap is non-finite at garment vertex %d."), VertexID);
				return false;
			}

			FSurfaceCorrespondence& Correspondence = OutCorrespondence[VertexID];
			Correspondence.BodyTriangle = TriangleID;
			Correspondence.Barycentric = Barycentric;
			Correspondence.SurfaceNormal = Normal;
			Correspondence.ClosestPoint = ClosestPoint;
			Correspondence.SignedGap = SignedGap;

			OutMinimumBefore = FMath::Min(OutMinimumBefore, SignedGap);
			if (SignedGap < 0.0)
			{
				++OutPenetratingBefore;
			}

			const double Push = FMath::Clamp(DesiredClearance - SignedGap, 0.0, MaximumPush);
			OutDeltas[VertexID] = Normal * Push;
		}

		for (int32 Iteration = 0; Iteration < SmoothingIterations; ++Iteration)
		{
			TArray<FVector3d> Smoothed = OutDeltas;
			for (int32 VertexID : GarmentMesh.VertexIndicesItr())
			{
				FVector3d NeighborAverage = FVector3d::Zero();
				int32 NeighborCount = 0;
				for (int32 NeighborID : GarmentMesh.VtxVerticesItr(VertexID))
				{
					NeighborAverage += OutDeltas[NeighborID];
					++NeighborCount;
				}
				if (NeighborCount > 0)
				{
					NeighborAverage /= static_cast<double>(NeighborCount);
					Smoothed[VertexID] = OutDeltas[VertexID] * 0.65 + NeighborAverage * 0.35;
					if (Smoothed[VertexID].Length() > MaximumPush)
					{
						Smoothed[VertexID].Normalize();
						Smoothed[VertexID] *= MaximumPush;
					}
				}
			}
			OutDeltas = MoveTemp(Smoothed);
		}

		// Re-project iteratively after smoothing. A single correction is insufficient
		// around the groin/axilla where moving a vertex changes its nearest triangle.
		// The final gate enforces the requested clearance, not merely non-penetration.
		OutPenetratingAfter = 0;
		OutMinimumAfter = TNumericLimits<double>::Max();
		constexpr int32 MaximumProjectionIterations = 16;
		constexpr double ClearanceToleranceCm = 0.001;
		constexpr double ProjectionSafetyMarginCm = 0.005;
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d BasePosition = GarmentMesh.GetVertex(VertexID);
			FVector3d GarmentOutwardNormal;
			FVector3d GarmentRadial;
			bool bHasGarmentRadial = false;
			if (!BuildGarmentFrame(
				VertexID, BasePosition, GarmentOutwardNormal, GarmentRadial, bHasGarmentRadial))
			{
				OutError = FString::Printf(TEXT("Projection frame is invalid at garment vertex %d."), VertexID);
				return false;
			}
			if (!OutCorrespondence.IsValidIndex(VertexID)
				|| !IsFiniteVector(OutCorrespondence[VertexID].SurfaceNormal))
			{
				OutError = FString::Printf(TEXT("Stored surface correspondence is invalid at garment vertex %d."), VertexID);
				return false;
			}
			const FVector3d ProjectionAnchorNormal = OutCorrespondence[VertexID].SurfaceNormal;
			FVector3d FittedPosition = BasePosition + OutDeltas[VertexID];
			double SignedGap = -TNumericLimits<double>::Max();
			for (int32 ProjectionIteration = 0; ProjectionIteration < MaximumProjectionIterations; ++ProjectionIteration)
			{
				int32 TriangleID = INDEX_NONE;
				FVector3d ClosestPoint;
				FVector3d Barycentric;
				FVector3d Normal;
				if (!FindValidatedSurface(
					FittedPosition,
					ProjectionAnchorNormal,
					GarmentRadial,
					bHasGarmentRadial,
					false,
					TriangleID,
					ClosestPoint,
					Barycentric,
					Normal))
				{
					OutError = FString::Printf(
						TEXT("Projection surface correspondence rejected garment vertex %d at iteration %d: %s."),
						VertexID,
						ProjectionIteration,
						*DescribeUnrestrictedNearest(
							FittedPosition, ProjectionAnchorNormal, GarmentRadial, bHasGarmentRadial));
					return false;
				}
				SignedGap = (FittedPosition - ClosestPoint).Dot(Normal);
				if (!FMath::IsFinite(SignedGap))
				{
					OutError = FString::Printf(
						TEXT("Projection signed gap is non-finite at garment vertex %d iteration %d."),
						VertexID,
						ProjectionIteration);
					return false;
				}
				if (SignedGap >= DesiredClearance + ProjectionSafetyMarginCm)
				{
					break;
				}

				const double RemainingCapacity = FMath::Max(0.0, MaximumPush - OutDeltas[VertexID].Length());
				if (RemainingCapacity <= 1.e-9)
				{
					break;
				}

				// Slight over-relaxation avoids asymptotic under-clearance after the
				// nearest surface changes, while MaximumPush remains a hard bound.
				const double Correction = FMath::Min(
					(DesiredClearance + ProjectionSafetyMarginCm - SignedGap + ClearanceToleranceCm) * 1.05,
					RemainingCapacity);
				OutDeltas[VertexID] += Normal * Correction;
				FittedPosition = BasePosition + OutDeltas[VertexID];
			}

			// Measure once more at the final position so the report and gate use
			// the exact geometry written into the clearance morph.
			int32 FinalTriangleID = INDEX_NONE;
			FVector3d FinalClosestPoint;
			FVector3d FinalBarycentric;
			FVector3d FinalNormal;
			if (!FindValidatedSurface(
				FittedPosition,
				ProjectionAnchorNormal,
				GarmentRadial,
				bHasGarmentRadial,
				false,
				FinalTriangleID,
				FinalClosestPoint,
				FinalBarycentric,
				FinalNormal))
			{
				OutError = FString::Printf(
					TEXT("Final surface correspondence rejected garment vertex %d: %s."),
					VertexID,
					*DescribeUnrestrictedNearest(
						FittedPosition, ProjectionAnchorNormal, GarmentRadial, bHasGarmentRadial));
				return false;
			}
			SignedGap = (FittedPosition - FinalClosestPoint).Dot(FinalNormal);
			if (!FMath::IsFinite(SignedGap))
			{
				OutError = FString::Printf(TEXT("Final signed gap is non-finite at garment vertex %d."), VertexID);
				return false;
			}

			// V26 runtime bindings must be anchored to the exact final auto-sculpted
			// rest position, not the pre-clearance nearest triangle captured above.
			FSurfaceCorrespondence& FinalCorrespondence = OutCorrespondence[VertexID];
			FinalCorrespondence.BodyTriangle = FinalTriangleID;
			FinalCorrespondence.Barycentric = FinalBarycentric;
			FinalCorrespondence.SurfaceNormal = FinalNormal;
			FinalCorrespondence.ClosestPoint = FinalClosestPoint;
			FinalCorrespondence.SignedGap = SignedGap;

			OutMinimumAfter = FMath::Min(OutMinimumAfter, SignedGap);
			if (SignedGap < 0.0)
			{
				++OutPenetratingAfter;
			}
		}

		return OutPenetratingAfter == 0
			&& OutMinimumAfter >= DesiredClearance - ClearanceToleranceCm;
	}

	/** Re-anchor every import vertex after the last serialized auto-sculpt repair. */
	static bool RebuildFinalSurfaceCorrespondence(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		const TArray<FVector3d>& FinalClearanceDeltas,
		double RequiredClearanceCm,
		TArray<FSurfaceCorrespondence>& OutCorrespondence,
		FString& OutError)
	{
		OutCorrespondence.SetNum(GarmentMesh.MaxVertexID());
		if (FinalClearanceDeltas.Num() < GarmentMesh.MaxVertexID())
		{
			OutError = TEXT("Final surface correspondence has an incomplete clearance field.");
			return false;
		}
		FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d FinalPosition = GarmentMesh.GetVertex(VertexID)
				+ FinalClearanceDeltas[VertexID];
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 BodyTriangleID = BodySpatial.FindNearestTriangle(
				FinalPosition,
				DistanceSquared);
			if (BodyTriangleID == IndexConstants::InvalidID
				|| !BodyMesh.IsTriangle(BodyTriangleID)
				|| !FMath::IsFinite(DistanceSquared))
			{
				OutError = FString::Printf(
					TEXT("Final surface correspondence found no body triangle for garment vertex %d."),
					VertexID);
				return false;
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh,
				BodyTriangleID,
				FinalPosition);
			FVector3d Normal = InterpolateNormal(
				BodyMesh,
				BodyNormals,
				BodyTriangleID,
				Query.TriangleBaryCoords);
			const double BarycentricSum = Query.TriangleBaryCoords.X
				+ Query.TriangleBaryCoords.Y + Query.TriangleBaryCoords.Z;
			if (Query.ClosestTrianglePoint.ContainsNaN()
				|| Query.TriangleBaryCoords.ContainsNaN()
				|| !Normal.Normalize()
				|| !FMath::IsNearlyEqual(BarycentricSum, 1.0, 1.e-4))
			{
				OutError = FString::Printf(
					TEXT("Final surface correspondence produced invalid geometry at garment vertex %d."),
					VertexID);
				return false;
			}
			const double SignedGap = (FinalPosition - Query.ClosestTrianglePoint).Dot(Normal);
			if (!FMath::IsFinite(SignedGap)
				|| SignedGap < RequiredClearanceCm - 0.02)
			{
				OutError = FString::Printf(
					TEXT("Final surface correspondence vertex %d violates clearance (%.4f/%.4fcm)."),
					VertexID,
					SignedGap,
					RequiredClearanceCm);
				return false;
			}
			FSurfaceCorrespondence& Correspondence = OutCorrespondence[VertexID];
			Correspondence.BodyTriangle = BodyTriangleID;
			Correspondence.Barycentric = Query.TriangleBaryCoords;
			Correspondence.SurfaceNormal = Normal;
			Correspondence.ClosestPoint = Query.ClosestTrianglePoint;
			Correspondence.SignedGap = SignedGap;
		}
		return true;
	}

	static bool TransferWeights(
		const USkeletalMesh* SourceGarment,
		const USkeletalMesh* BodySurface,
		const USkeletalMesh* CompatibilityReference,
		const FDynamicMesh3& BodyMesh,
		const TArray<FSurfaceCorrespondence>& Correspondence,
		const TArray<FName>& ExcludedBodyBoneBranches,
		UDynamicMesh* GarmentDynamicMesh,
		int32 MaximumInfluences,
		FString& OutMethod,
		TArray<FName>& OutRequiredWeightedBones,
		int32& OutRemappedWeightedBoneCount,
		int32& OutReconciledSplitVertexCount,
		FString& OutError)
	{
		if (!IsValid(SourceGarment) || !IsValid(BodySurface) || !IsValid(CompatibilityReference))
		{
			OutError = TEXT("Weight transfer requires valid source/body/compatibility skeletal meshes.");
			return false;
		}
		const FReferenceSkeleton& BodyReference = BodySurface->GetRefSkeleton();
		const FReferenceSkeleton& CompatibilityPoseReference = CompatibilityReference->GetRefSkeleton();
		TSet<int32> ExcludedBodyBoneIndices;
		for (const FName ExcludedRootName : ExcludedBodyBoneBranches)
		{
			const int32 ExcludedRootIndex = BodyReference.FindBoneIndex(ExcludedRootName);
			if (ExcludedRootIndex <= 0)
			{
				OutError = FString::Printf(
					TEXT("Excluded body bone branch root %s is missing or resolves to the skeleton root."),
					*ExcludedRootName.ToString());
				return false;
			}
			for (int32 BodyBoneIndex = 0; BodyBoneIndex < BodyReference.GetRawBoneNum(); ++BodyBoneIndex)
			{
				for (int32 AncestorIndex = BodyBoneIndex;
					AncestorIndex != INDEX_NONE;
					AncestorIndex = BodyReference.GetParentIndex(AncestorIndex))
				{
					if (AncestorIndex == ExcludedRootIndex)
					{
						ExcludedBodyBoneIndices.Add(BodyBoneIndex);
						break;
					}
				}
			}
		}
		TArray<int32> CompatibleBodyBoneIndex;
		CompatibleBodyBoneIndex.Init(INDEX_NONE, BodyReference.GetRawBoneNum());
		for (int32 BodyBoneIndex = 0; BodyBoneIndex < BodyReference.GetRawBoneNum(); ++BodyBoneIndex)
		{
			int32 CandidateBodyIndex = BodyBoneIndex;
			while (CandidateBodyIndex != INDEX_NONE)
			{
				if (ExcludedBodyBoneIndices.Contains(CandidateBodyIndex))
				{
					CandidateBodyIndex = BodyReference.GetParentIndex(CandidateBodyIndex);
					continue;
				}
				const FName CandidateName = BodyReference.GetBoneName(CandidateBodyIndex);
				const int32 CompatibilityIndex = CompatibilityPoseReference.FindBoneIndex(CandidateName);
				if (CompatibilityIndex != INDEX_NONE)
				{
					const int32 BodyParentIndex = BodyReference.GetParentIndex(CandidateBodyIndex);
					const int32 CompatibilityParentIndex = CompatibilityPoseReference.GetParentIndex(CompatibilityIndex);
					const FName BodyParentName = BodyParentIndex == INDEX_NONE
						? NAME_None
						: BodyReference.GetBoneName(BodyParentIndex);
					const FName CompatibilityParentName = CompatibilityParentIndex == INDEX_NONE
						? NAME_None
						: CompatibilityPoseReference.GetBoneName(CompatibilityParentIndex);
					if (BodyParentName == CompatibilityParentName)
					{
						CompatibleBodyBoneIndex[BodyBoneIndex] = CandidateBodyIndex;
						break;
					}
				}
				CandidateBodyIndex = BodyReference.GetParentIndex(CandidateBodyIndex);
			}
			if (CompatibleBodyBoneIndex[BodyBoneIndex] == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("Female bone %s has no hierarchy-safe ancestor in the effective Multiple pose driver."),
					*BodyReference.GetBoneName(BodyBoneIndex).ToString());
				return false;
			}
		}
		const FDynamicMeshVertexSkinWeightsAttribute* BodyWeights = BodyMesh.Attributes()
			? BodyMesh.Attributes()->GetSkinWeightsAttribute(
				FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName)
			: nullptr;
		const FDynamicMeshBoneNameAttribute* BodyBoneNamesAttribute = BodyMesh.Attributes()
			? BodyMesh.Attributes()->GetBoneNames()
			: nullptr;
		if (!IsValid(GarmentDynamicMesh)
			|| Correspondence.Num() < GarmentDynamicMesh->GetMeshRef().MaxVertexID()
			|| !BodyWeights
			|| !BodyBoneNamesAttribute)
		{
			OutError = TEXT("Barycentric weight transfer lacks body weights, bone names or surface correspondence.");
			return false;
		}
		const TArray<FName>& BodyDynamicBoneNames = BodyBoneNamesAttribute->GetAttribValues();
		OutMethod = ExcludedBodyBoneIndices.IsEmpty()
			? TEXT("BarycentricSurfaceExactFemaleBind")
			: FString::Printf(
				TEXT("BarycentricSurfaceExactFemaleBindCatalogBranchRemap(excludedBones=%d)"),
				ExcludedBodyBoneIndices.Num());
		OutReconciledSplitVertexCount = 0;
		int32 ReboundGarmentBoneCount = 0;
		TSet<FName> RequiredBoneNames;
		TSet<FName> RemappedBodyBoneNames;
		bool bWeightsValid = true;
		FString WeightFailureReason;
		const int32 MaximumInfluenceCount = FMath::Clamp(MaximumInfluences, 1, 12);
		GarmentDynamicMesh->EditMesh([&](FDynamicMesh3& Mesh)
		{
			auto RejectWeights = [&](const FString& Reason)
			{
				if (WeightFailureReason.IsEmpty())
				{
					WeightFailureReason = Reason;
				}
				bWeightsValid = false;
			};
			if (Mesh.Attributes()
				&& !Mesh.Attributes()->GetSkinWeightsAttribute(FitWeightProfileName))
			{
				Mesh.Attributes()->AttachSkinWeightsAttribute(
					FitWeightProfileName,
					new FDynamicMeshVertexSkinWeightsAttribute(&Mesh));
			}
			FDynamicMeshVertexSkinWeightsAttribute* Weights = Mesh.Attributes()
				? Mesh.Attributes()->GetSkinWeightsAttribute(FitWeightProfileName)
				: nullptr;
			FDynamicMeshBoneNameAttribute* BoneNamesAttribute = Mesh.Attributes()
				? Mesh.Attributes()->GetBoneNames()
				: nullptr;
			FDynamicMeshBoneParentIndexAttribute* BoneParentsAttribute = Mesh.Attributes()
				? Mesh.Attributes()->GetBoneParentIndices()
				: nullptr;
			FDynamicMeshBonePoseAttribute* BonePosesAttribute = Mesh.Attributes()
				? Mesh.Attributes()->GetBonePoses()
				: nullptr;
			FDynamicMeshBoneColorAttribute* BoneColorsAttribute = Mesh.Attributes()
				? Mesh.Attributes()->GetBoneColors()
				: nullptr;
			if (!Weights
				|| !BoneNamesAttribute
				|| !BoneParentsAttribute
				|| !BonePosesAttribute
				|| !BoneColorsAttribute)
			{
				RejectWeights(TEXT("Garment dynamic mesh lacks EF_AutoFit weights or complete bone attributes."));
				return;
			}

			const int32 InitialGarmentBoneCount = BoneNamesAttribute->Num();
			if (BoneParentsAttribute->Num() != InitialGarmentBoneCount
				|| BonePosesAttribute->Num() != InitialGarmentBoneCount
				|| BoneColorsAttribute->Num() != InitialGarmentBoneCount)
			{
				RejectWeights(TEXT("Garment dynamic bone attributes do not have matching lengths."));
				return;
			}
			const FDynamicMeshBoneParentIndexAttribute* BodyBoneParentsAttribute =
				BodyMesh.Attributes() ? BodyMesh.Attributes()->GetBoneParentIndices() : nullptr;
			const FDynamicMeshBonePoseAttribute* BodyBonePosesAttribute =
				BodyMesh.Attributes() ? BodyMesh.Attributes()->GetBonePoses() : nullptr;
			const FDynamicMeshBoneColorAttribute* BodyBoneColorsAttribute =
				BodyMesh.Attributes() ? BodyMesh.Attributes()->GetBoneColors() : nullptr;
			const int32 BodyBoneCount = BodyDynamicBoneNames.Num();
			ReboundGarmentBoneCount = BodyBoneCount;
			if (!BodyBoneParentsAttribute
				|| !BodyBonePosesAttribute
				|| BodyBoneCount != BodyReference.GetRawBoneNum()
				|| BodyBoneParentsAttribute->Num() != BodyBoneCount
				|| BodyBonePosesAttribute->Num() != BodyBoneCount
				|| (BodyBoneColorsAttribute
					&& !BodyBoneColorsAttribute->IsEmpty()
					&& BodyBoneColorsAttribute->Num() != BodyBoneCount))
			{
				RejectWeights(TEXT("Body dynamic bone table is incomplete or does not match Female's reference skeleton."));
				return;
			}
			if (InitialGarmentBoneCount != BodyBoneCount)
			{
				RejectWeights(FString::Printf(
					TEXT("Garment/body dynamic bone counts differ before exact transfer (%d/%d); expansion is forbidden."),
					InitialGarmentBoneCount,
					BodyBoneCount));
				return;
			}
			for (int32 BoneIndex = 0; BoneIndex < BodyBoneCount; ++BoneIndex)
			{
				if (BodyDynamicBoneNames[BoneIndex] != BodyReference.GetBoneName(BoneIndex)
					|| BodyBoneParentsAttribute->GetValue(BoneIndex) != BodyReference.GetParentIndex(BoneIndex)
					|| !BodyBonePosesAttribute->GetValue(BoneIndex).Equals(
						BodyReference.GetRefBonePose()[BoneIndex], 1.e-4f))
				{
					RejectWeights(FString::Printf(
						TEXT("Body dynamic bind table differs from Female at bone index %d (%s)."),
						BoneIndex,
						*BodyReference.GetBoneName(BoneIndex).ToString()));
					return;
				}
				if (BoneNamesAttribute->GetValue(BoneIndex) != BodyReference.GetBoneName(BoneIndex)
					|| BoneParentsAttribute->GetValue(BoneIndex) != BodyReference.GetParentIndex(BoneIndex))
				{
					RejectWeights(FString::Printf(
						TEXT("Garment dynamic hierarchy is not index-identical to Female at bone %d (%s)."),
						BoneIndex,
						*BodyReference.GetBoneName(BoneIndex).ToString()));
					return;
				}
			}

			// Copy Female's table index-for-index. GeometryScript skin profiles store
			// numeric bone indices; name remapping after interpolation is not exact.
			Mesh.Attributes()->EnableBones(BodyBoneCount);
			BoneNamesAttribute = Mesh.Attributes()->GetBoneNames();
			BoneParentsAttribute = Mesh.Attributes()->GetBoneParentIndices();
			BonePosesAttribute = Mesh.Attributes()->GetBonePoses();
			BoneColorsAttribute = Mesh.Attributes()->GetBoneColors();
			for (int32 BoneIndex = 0; BoneIndex < BodyBoneCount; ++BoneIndex)
			{
				BoneNamesAttribute->SetValue(BoneIndex, BodyDynamicBoneNames[BoneIndex]);
				BoneParentsAttribute->SetValue(BoneIndex, BodyBoneParentsAttribute->GetValue(BoneIndex));
				BonePosesAttribute->SetValue(BoneIndex, BodyBonePosesAttribute->GetValue(BoneIndex));
				BoneColorsAttribute->SetValue(
					BoneIndex,
					BodyBoneColorsAttribute && !BodyBoneColorsAttribute->IsEmpty()
						? BodyBoneColorsAttribute->GetValue(BoneIndex)
						: FVector4f::One());
			}
			if (!Mesh.Attributes()->CheckBoneValidity(EValidityCheckFailMode::ReturnOnly))
			{
				RejectWeights(TEXT("Female bind table failed garment dynamic-mesh validity checks."));
				return;
			}
			const TArray<FName>& DynamicBoneNames = BoneNamesAttribute->GetAttribValues();

			UE::AnimationCore::FBoneWeightsSettings WeightSettings;
			WeightSettings
				.SetMaxWeightCount(MaximumInfluenceCount)
				.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

			// Interpolate Female's exact body-surface influences, but collapse a
			// DAZ-only influence to the nearest hierarchy-safe ancestor that the
			// effective Multiple pose driver can actually animate. The derived mesh
			// still keeps Female's complete bind table index-for-index; only its
			// generated EF_AutoFit profile is remapped, so no protected skeleton or
			// source asset is ever changed.
			for (int32 VertexID : Mesh.VertexIndicesItr())
			{
				if (!Correspondence.IsValidIndex(VertexID))
				{
					RejectWeights(FString::Printf(TEXT("Garment vertex %d has no surface correspondence."), VertexID));
					break;
				}
				const FSurfaceCorrespondence& Surface = Correspondence[VertexID];
				if (!BodyMesh.IsTriangle(Surface.BodyTriangle))
				{
					RejectWeights(FString::Printf(
						TEXT("Garment vertex %d references invalid body triangle %d."),
						VertexID,
						Surface.BodyTriangle));
					break;
				}
				const FIndex3i BodyTriangle = BodyMesh.GetTriangle(Surface.BodyTriangle);
				const int32 BodyVertexIDs[3] = {BodyTriangle.A, BodyTriangle.B, BodyTriangle.C};
				const double RawBarycentricWeights[3] = {
					Surface.Barycentric.X,
					Surface.Barycentric.Y,
					Surface.Barycentric.Z};
				double BarycentricWeights[3] = {};
				for (int32 BarycentricIndex = 0; BarycentricIndex < 3; ++BarycentricIndex)
				{
					const double RawWeight = RawBarycentricWeights[BarycentricIndex];
					if (!FMath::IsFinite(RawWeight) || RawWeight < -1.e-5 || RawWeight > 1.0 + 1.e-5)
					{
						RejectWeights(FString::Printf(
							TEXT("Garment vertex %d has invalid barycentric[%d]=%.9f."),
							VertexID,
							BarycentricIndex,
							RawWeight));
						break;
					}
					BarycentricWeights[BarycentricIndex] = FMath::Clamp(RawWeight, 0.0, 1.0);
				}
				if (!bWeightsValid)
				{
					break;
				}
				const double BarycentricSum = BarycentricWeights[0]
					+ BarycentricWeights[1] + BarycentricWeights[2];
				if (!FMath::IsFinite(BarycentricSum)
					|| BarycentricSum <= UE_DOUBLE_SMALL_NUMBER
					|| !FMath::IsNearlyEqual(BarycentricSum, 1.0, 1.e-3))
				{
					RejectWeights(FString::Printf(
						TEXT("Garment vertex %d barycentric sum %.9f is invalid."),
						VertexID,
						BarycentricSum));
					break;
				}

				TMap<int32, double> AccumulatedWeights;
				double VertexTotalMass = 0.0;
				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					const double CornerWeight = BarycentricWeights[CornerIndex] / BarycentricSum;
					UE::AnimationCore::FBoneWeights BodyVertexWeights;
					BodyWeights->GetValue(BodyVertexIDs[CornerIndex], BodyVertexWeights);
					if (BodyVertexWeights.Num() == 0)
					{
						RejectWeights(FString::Printf(
							TEXT("Body vertex %d (garment %d corner %d) is unweighted."),
							BodyVertexIDs[CornerIndex],
							VertexID,
							CornerIndex));
						break;
					}
					for (const UE::AnimationCore::FBoneWeight& BodyInfluence : BodyVertexWeights)
					{
						const int32 BodyDynamicBoneIndex = static_cast<int32>(BodyInfluence.GetBoneIndex());
						if (!BodyDynamicBoneNames.IsValidIndex(BodyDynamicBoneIndex))
						{
							RejectWeights(FString::Printf(
								TEXT("Body influence index %d is invalid at garment vertex %d."),
								BodyDynamicBoneIndex,
								VertexID));
							break;
						}
						const FName OriginalBodyBoneName = BodyDynamicBoneNames[BodyDynamicBoneIndex];
						const int32 SafeGarmentBoneIndex = CompatibleBodyBoneIndex[BodyDynamicBoneIndex];
						if (!DynamicBoneNames.IsValidIndex(SafeGarmentBoneIndex)
							|| DynamicBoneNames[SafeGarmentBoneIndex]
								!= BodyReference.GetBoneName(SafeGarmentBoneIndex))
						{
							RejectWeights(FString::Printf(
								TEXT("Body bone %s has invalid compatibility-map index %d at garment vertex %d."),
								*OriginalBodyBoneName.ToString(),
								SafeGarmentBoneIndex,
								VertexID));
							break;
						}
						if (SafeGarmentBoneIndex != BodyDynamicBoneIndex)
						{
							RemappedBodyBoneNames.Add(OriginalBodyBoneName);
						}
						const double Contribution = CornerWeight * static_cast<double>(BodyInfluence.GetWeight());
						if (!FMath::IsFinite(Contribution) || Contribution < 0.0)
						{
							RejectWeights(FString::Printf(
								TEXT("Non-finite/negative weight contribution at garment vertex %d bone %s."),
								VertexID,
								*OriginalBodyBoneName.ToString()));
							break;
						}
						VertexTotalMass += Contribution;
						AccumulatedWeights.FindOrAdd(SafeGarmentBoneIndex) += Contribution;
					}
					if (!bWeightsValid)
					{
						break;
					}
				}
				if (!bWeightsValid
					|| AccumulatedWeights.IsEmpty()
					|| !FMath::IsFinite(VertexTotalMass)
					|| VertexTotalMass <= UE_DOUBLE_SMALL_NUMBER)
				{
					RejectWeights(FString::Printf(
						TEXT("Garment vertex %d failed exact Female weight mass gate (total=%.9f)."),
						VertexID,
						VertexTotalMass));
					break;
				}

				TArray<double> SortedMasses;
				AccumulatedWeights.GenerateValueArray(SortedMasses);
				SortedMasses.Sort(TGreater<double>());
				double TotalMass = 0.0;
				double RetainedMass = 0.0;
				for (int32 WeightIndex = 0; WeightIndex < SortedMasses.Num(); ++WeightIndex)
				{
					TotalMass += SortedMasses[WeightIndex];
					if (WeightIndex < MaximumInfluenceCount)
					{
						RetainedMass += SortedMasses[WeightIndex];
					}
				}
				if (!FMath::IsFinite(TotalMass) || TotalMass <= UE_DOUBLE_SMALL_NUMBER
					|| RetainedMass + 1.e-6 < TotalMass * 0.95)
				{
					RejectWeights(FString::Printf(
						TEXT("Garment vertex %d would retain only %.6f/%.6f influence mass at max %d influences."),
						VertexID,
						RetainedMass,
						TotalMass,
						MaximumInfluenceCount));
					break;
				}

				TArray<int32> SortedBoneIndices;
				AccumulatedWeights.GetKeys(SortedBoneIndices);
				SortedBoneIndices.Sort();
				TArray<UE::AnimationCore::FBoneWeight> InterpolatedInfluences;
				InterpolatedInfluences.Reserve(SortedBoneIndices.Num());
				for (int32 BoneIndex : SortedBoneIndices)
				{
					InterpolatedInfluences.Emplace(
						static_cast<FBoneIndexType>(BoneIndex),
						static_cast<float>(AccumulatedWeights.FindChecked(BoneIndex)));
				}
				const UE::AnimationCore::FBoneWeights InterpolatedWeights =
					UE::AnimationCore::FBoneWeights::Create(InterpolatedInfluences, WeightSettings);
				if (InterpolatedWeights.Num() == 0 || InterpolatedWeights.Num() > MaximumInfluenceCount)
				{
					RejectWeights(FString::Printf(
						TEXT("Garment vertex %d produced %d normalized influences (limit %d)."),
						VertexID,
						InterpolatedWeights.Num(),
						MaximumInfluenceCount));
					break;
				}
				Weights->SetValue(VertexID, InterpolatedWeights);
			}
			if (!bWeightsValid)
			{
				return;
			}

			// GeometryScript serializes dynamic split vertices back through their
			// original MeshDescription vertex ID. Reconcile every split group first,
			// otherwise traversal order makes the last seam vertex silently win.
			const FNonManifoldMappingSupport GarmentMapping(Mesh);
			TMap<int32, TArray<int32>> SplitGroups;
			for (int32 VertexID : Mesh.VertexIndicesItr())
			{
				SplitGroups.FindOrAdd(GarmentMapping.GetOriginalNonManifoldVertexID(VertexID)).Add(VertexID);
			}
			for (const TPair<int32, TArray<int32>>& GroupPair : SplitGroups)
			{
				const TArray<int32>& SplitVertices = GroupPair.Value;
				if (SplitVertices.Num() <= 1)
				{
					continue;
				}
				TMap<int32, double> AccumulatedWeights;
				for (int32 SplitVertexID : SplitVertices)
				{
					UE::AnimationCore::FBoneWeights SplitWeights;
					Weights->GetValue(SplitVertexID, SplitWeights);
					for (const UE::AnimationCore::FBoneWeight& Influence : SplitWeights)
					{
						AccumulatedWeights.FindOrAdd(static_cast<int32>(Influence.GetBoneIndex()))
							+= static_cast<double>(Influence.GetWeight());
					}
				}
				TArray<int32> SortedBoneIndices;
				AccumulatedWeights.GetKeys(SortedBoneIndices);
				SortedBoneIndices.Sort();
				TArray<UE::AnimationCore::FBoneWeight> AverageInfluences;
				AverageInfluences.Reserve(SortedBoneIndices.Num());
				for (int32 BoneIndex : SortedBoneIndices)
				{
					AverageInfluences.Emplace(
						static_cast<FBoneIndexType>(BoneIndex),
						static_cast<float>(AccumulatedWeights.FindChecked(BoneIndex) / SplitVertices.Num()));
				}
				TArray<double> SortedAverageMasses;
				for (const UE::AnimationCore::FBoneWeight& Influence : AverageInfluences)
				{
					SortedAverageMasses.Add(static_cast<double>(Influence.GetWeight()));
				}
				SortedAverageMasses.Sort(TGreater<double>());
				double AverageTotalMass = 0.0;
				double AverageRetainedMass = 0.0;
				for (int32 WeightIndex = 0; WeightIndex < SortedAverageMasses.Num(); ++WeightIndex)
				{
					AverageTotalMass += SortedAverageMasses[WeightIndex];
					if (WeightIndex < MaximumInfluenceCount)
					{
						AverageRetainedMass += SortedAverageMasses[WeightIndex];
					}
				}
				if (!FMath::IsFinite(AverageTotalMass)
					|| AverageTotalMass <= UE_DOUBLE_SMALL_NUMBER
					|| AverageRetainedMass + 1.e-6 < AverageTotalMass * 0.95)
				{
					RejectWeights(FString::Printf(
						TEXT("Split group %d would retain only %.6f/%.6f influence mass."),
						GroupPair.Key,
						AverageRetainedMass,
						AverageTotalMass));
					break;
				}
				const UE::AnimationCore::FBoneWeights ReconciledWeights =
					UE::AnimationCore::FBoneWeights::Create(AverageInfluences, WeightSettings);
				if (ReconciledWeights.Num() == 0 || ReconciledWeights.Num() > MaximumInfluenceCount)
				{
					RejectWeights(FString::Printf(
						TEXT("Split group %d produced %d normalized influences (limit %d)."),
						GroupPair.Key,
						ReconciledWeights.Num(),
						MaximumInfluenceCount));
					break;
				}
				for (int32 SplitVertexID : SplitVertices)
				{
					Weights->SetValue(SplitVertexID, ReconciledWeights);
				}
				OutReconciledSplitVertexCount += SplitVertices.Num();
			}

			RequiredBoneNames.Reset();
			if (bWeightsValid)
			{
				for (int32 VertexID : Mesh.VertexIndicesItr())
				{
					UE::AnimationCore::FBoneWeights FinalWeights;
					Weights->GetValue(VertexID, FinalWeights);
					if (FinalWeights.Num() == 0 || FinalWeights.Num() > MaximumInfluenceCount)
					{
						RejectWeights(FString::Printf(
							TEXT("Final garment vertex %d has %d influences (limit %d)."),
							VertexID,
							FinalWeights.Num(),
							MaximumInfluenceCount));
						break;
					}
					for (const UE::AnimationCore::FBoneWeight& Influence : FinalWeights)
					{
						const int32 BoneIndex = static_cast<int32>(Influence.GetBoneIndex());
						if (!DynamicBoneNames.IsValidIndex(BoneIndex))
						{
							RejectWeights(FString::Printf(
								TEXT("Final garment vertex %d references invalid bone index %d."),
								VertexID,
								BoneIndex));
							break;
						}
						RequiredBoneNames.Add(DynamicBoneNames[BoneIndex]);
					}
					if (!bWeightsValid)
					{
						break;
					}
				}
			}
		}, EDynamicMeshChangeType::AttributeEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

		if (!bWeightsValid)
		{
			OutError = WeightFailureReason.IsEmpty()
				? TEXT("Transferred weights failed an unspecified fail-closed validation gate.")
				: WeightFailureReason;
		}
		OutRequiredWeightedBones = RequiredBoneNames.Array();
		OutRequiredWeightedBones.Sort(FNameLexicalLess());
		OutRemappedWeightedBoneCount = RemappedBodyBoneNames.Num();
		OutMethod = ExcludedBodyBoneIndices.IsEmpty()
			? FString::Printf(
				TEXT("BarycentricSurfaceFemaleToMultipleAncestorRemap(bones=%d,remapped=%d)"),
				ReboundGarmentBoneCount,
				OutRemappedWeightedBoneCount)
			: FString::Printf(
				TEXT("BarycentricSurfaceFemaleToMultipleAncestorRemap(bones=%d,remapped=%d,excludedBranches=%d,excludedBones=%d)"),
				ReboundGarmentBoneCount,
				OutRemappedWeightedBoneCount,
				ExcludedBodyBoneBranches.Num(),
				ExcludedBodyBoneIndices.Num());
		if (bWeightsValid && OutRequiredWeightedBones.IsEmpty())
		{
			OutError = TEXT("Transferred weights produced no required weighted bones.");
			return false;
		}
		return bWeightsValid;
	}

	static bool WriteSkinProfile(
		UDynamicMesh* GarmentDynamicMesh,
		USkeletalMesh* Derived,
		int32 MaximumInfluences,
		int32& OutCertifiedVertexCount,
		FString& OutError)
	{
		OutCertifiedVertexCount = 0;
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = 0;
		FGeometryScriptCopySkinWeightProfileToAssetOptions Options;
		Options.bOverwriteExistingProfile = true;
		Options.bEmitTransaction = false;
		Options.bDeferMeshPostEditChange = true;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopySkinWeightProfileToSkeletalMesh(
			GarmentDynamicMesh,
			Derived,
			FitWeightProfileName,
			FitWeightProfileName,
			Options,
			WriteLOD,
			Outcome,
			nullptr);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = TEXT("CopySkinWeightProfileToSkeletalMesh failed.");
			return false;
		}

		for (FSkinWeightProfileInfo& ProfileInfo : Derived->GetSkinWeightProfiles())
		{
			if (ProfileInfo.Name == FitWeightProfileName)
			{
				// Runtime activates this profile explicitly. Marking it default can
				// compete with an inherited profile under LoadByDefaultMode CVars.
				ProfileInfo.DefaultProfile = false;
				ProfileInfo.DefaultProfileFromLODIndex = 0;
			}
		}

		const FDynamicMesh3& DynamicMesh = GarmentDynamicMesh->GetMeshRef();
		const FDynamicMeshVertexSkinWeightsAttribute* RequestedWeights = DynamicMesh.Attributes()
			? DynamicMesh.Attributes()->GetSkinWeightsAttribute(FitWeightProfileName)
			: nullptr;
		const FMeshDescription* StoredDescription = Derived->GetMeshDescription(0);
		if (!RequestedWeights || !StoredDescription)
		{
			OutError = TEXT("Generated skin profile cannot be read back from LOD0.");
			return false;
		}
		const FSkeletalMeshAttributesShared StoredAttributes(*StoredDescription);
		const FSkinWeightsVertexAttributesConstRef StoredWeights =
			StoredAttributes.GetVertexSkinWeights(FitWeightProfileName);
		if (!StoredWeights.IsValid())
		{
			OutError = TEXT("EF_AutoFit is absent from the committed MeshDescription.");
			return false;
		}

		const FNonManifoldMappingSupport Mapping(DynamicMesh);
		TSet<int32> CertifiedOriginalVertices;
		for (int32 VertexID : DynamicMesh.VertexIndicesItr())
		{
			const int32 OriginalVertexID = Mapping.GetOriginalNonManifoldVertexID(VertexID);
			if (!StoredDescription->Vertices().IsValid(FVertexID(OriginalVertexID)))
			{
				OutError = FString::Printf(
					TEXT("EF_AutoFit maps dynamic vertex %d to invalid stored vertex %d."),
					VertexID,
					OriginalVertexID);
				return false;
			}
			UE::AnimationCore::FBoneWeights RequestedVertexWeights;
			RequestedWeights->GetValue(VertexID, RequestedVertexWeights);
			const FVertexBoneWeightsConst StoredVertexWeights = StoredWeights.Get(FVertexID(OriginalVertexID));
			if (RequestedVertexWeights.Num() == 0
				|| RequestedVertexWeights.Num() > MaximumInfluences
				|| StoredVertexWeights.Num() != RequestedVertexWeights.Num())
			{
				OutError = FString::Printf(
					TEXT("EF_AutoFit readback count mismatch at dynamic/stored vertex %d/%d."),
					VertexID,
					OriginalVertexID);
				return false;
			}
			for (int32 InfluenceIndex = 0; InfluenceIndex < RequestedVertexWeights.Num(); ++InfluenceIndex)
			{
				const UE::AnimationCore::FBoneWeight RequestedInfluence = RequestedVertexWeights[InfluenceIndex];
				const UE::AnimationCore::FBoneWeight StoredInfluence = StoredVertexWeights[InfluenceIndex];
				if (RequestedInfluence != StoredInfluence)
				{
					OutError = FString::Printf(
						TEXT("EF_AutoFit readback differs at vertex %d influence %d."),
						OriginalVertexID,
						InfluenceIndex);
					return false;
				}
			}
			CertifiedOriginalVertices.Add(OriginalVertexID);
		}
		OutCertifiedVertexCount = CertifiedOriginalVertices.Num();
		if (OutCertifiedVertexCount != StoredDescription->Vertices().Num())
		{
			OutError = FString::Printf(
				TEXT("EF_AutoFit certified %d of %d stored vertices."),
				OutCertifiedVertexCount,
				StoredDescription->Vertices().Num());
			return false;
		}
		return true;
	}

	static bool WriteMorph(UDynamicMesh* MorphMesh, USkeletalMesh* Derived, FName MorphName, FString& OutError)
	{
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = 0;
		FGeometryScriptCopyMorphTargetToAssetOptions Options;
		Options.bOverwriteExistingTarget = true;
		Options.bEmitTransaction = false;
		Options.bDeferMeshPostEditChange = true;
		Options.bCopyNormals = false;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMorphTargetToSkeletalMesh(
			MorphMesh,
			Derived,
			MorphName,
			Options,
			WriteLOD,
			Outcome,
			nullptr);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = FString::Printf(TEXT("Failed to write morph target %s."), *MorphName.ToString());
			return false;
		}
		return true;
	}

	static bool ReadStoredMorphDeltas(
		const USkeletalMesh* Mesh,
		FName MorphName,
		const FDynamicMesh3& DynamicTopology,
		const TArray<FVector3d>* RequestedDeltas,
		TArray<FVector3d>& OutDeltas,
		int32& OutAlteredDeltaCount,
		FString& OutError)
	{
		const FMeshDescription* Description = IsValid(Mesh) ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Description)
		{
			OutError = FString::Printf(TEXT("Stored morph %s has no LOD0 MeshDescription."), *MorphName.ToString());
			return false;
		}

		const FSkeletalMeshAttributesShared Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> StoredDeltas =
			Attributes.GetVertexMorphPositionDelta(MorphName);
		if (!StoredDeltas.IsValid())
		{
			OutError = FString::Printf(TEXT("Stored morph %s has no position-delta attribute."), *MorphName.ToString());
			return false;
		}

		const FNonManifoldMappingSupport Mapping(DynamicTopology);
		const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(0);
		const double ComparisonToleranceCm = FMath::Max(
			1.e-6,
			LODInfo ? static_cast<double>(LODInfo->BuildSettings.MorphThresholdPosition) * 0.01 : 1.e-6);
		OutDeltas.Init(FVector3d::Zero(), DynamicTopology.MaxVertexID());
		for (int32 VertexID : DynamicTopology.VertexIndicesItr())
		{
			const int32 OriginalVertexID = Mapping.GetOriginalNonManifoldVertexID(VertexID);
			if (!Description->Vertices().IsValid(FVertexID(OriginalVertexID)))
			{
				OutError = FString::Printf(
					TEXT("Stored morph %s maps dynamic vertex %d to invalid source vertex %d."),
					*MorphName.ToString(), VertexID, OriginalVertexID);
				return false;
			}
			const FVector3f Stored = StoredDeltas.Get(FVertexID(OriginalVertexID));
			if (!FMath::IsFinite(Stored.X) || !FMath::IsFinite(Stored.Y) || !FMath::IsFinite(Stored.Z))
			{
				OutError = FString::Printf(
					TEXT("Stored morph %s contains a non-finite delta at vertex %d."),
					*MorphName.ToString(), VertexID);
				return false;
			}
			OutDeltas[VertexID] = FVector3d(Stored);
			if (RequestedDeltas
				&& RequestedDeltas->IsValidIndex(VertexID)
				&& (OutDeltas[VertexID] - (*RequestedDeltas)[VertexID]).SquaredLength()
					> FMath::Square(ComparisonToleranceCm))
			{
				++OutAlteredDeltaCount;
			}
		}
		return true;
	}

	static void RemoveGeneratedMorph(USkeletalMesh* Derived, FName MorphName)
	{
		if (!IsValid(Derived) || MorphName.IsNone())
		{
			return;
		}
		if (FMeshDescription* Description = Derived->GetMeshDescription(0))
		{
			FSkeletalMeshAttributes Attributes(*Description);
			constexpr bool bKeepExistingAttributes = true;
			Attributes.Register(bKeepExistingAttributes);
			Attributes.UnregisterMorphTargetAttribute(MorphName);
		}
		if (UMorphTarget* ExistingTarget = Derived->FindMorphTarget(MorphName))
		{
			Derived->UnregisterMorphTarget(ExistingTarget, false);
			// UnregisterMorphTarget(..., false) intentionally skips the render-data
			// rebuild, but it also leaves MorphTargetIndexMap pointing at the old
			// array indices.  The envelope fallback can discard several provisional
			// targets in one pass, so repair the lookup map before the next removal.
			// Preserve unrelated pre-existing empty targets while rebuilding the map.
			Derived->InitMorphTargets(true);
		}
	}

	static bool RemoveDirectMorphsCommitted(
		USkeletalMesh* Derived,
		TConstArrayView<FName> MorphNames,
		FString& OutError)
	{
		if (!IsValid(Derived) || MorphNames.IsEmpty())
		{
			return IsValid(Derived);
		}
		FMeshDescription* Description = Derived->GetMeshDescription(0);
		if (!Description)
		{
			OutError = TEXT("Derived garment has no LOD0 MeshDescription for direct-morph isolation.");
			return false;
		}

		FSkeletalMeshAttributes Attributes(*Description);
		constexpr bool bKeepExistingAttributes = true;
		Attributes.Register(bKeepExistingAttributes);
		const TArray<FName> ExistingAttributeNames = Attributes.GetMorphTargetNames();
		TArray<TObjectPtr<UMorphTarget>> RemovedObjects;
		bool bNeedsCommit = false;
		for (FName MorphName : MorphNames)
		{
			if (UMorphTarget* Target = Derived->FindMorphTarget(MorphName))
			{
				RemovedObjects.Add(Target);
			}
			bNeedsCommit |= ExistingAttributeNames.Contains(MorphName);
		}

		if (bNeedsCommit)
		{
			Derived->ModifyMeshDescription(0);
			for (FName MorphName : MorphNames)
			{
				if (ExistingAttributeNames.Contains(MorphName))
				{
					Attributes.UnregisterMorphTargetAttribute(MorphName);
				}
			}
			USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
			CommitParams.bUpdateMorphTargets = true;
			CommitParams.bUpdateSkinWeightProfiles = false;
			CommitParams.bUpdateVertexAttributes = false;
			CommitParams.bUpdateVertexColors = false;
			if (!Derived->CommitMeshDescription(0, CommitParams))
			{
				OutError = TEXT("Could not commit direct body-morph removal to the derived garment.");
				return false;
			}
		}

		for (UMorphTarget* RemovedObject : RemovedObjects)
		{
			if (!IsValid(RemovedObject))
			{
				continue;
			}
			if (Derived->FindMorphTarget(RemovedObject->GetFName()) == RemovedObject)
			{
				Derived->UnregisterMorphTarget(RemovedObject, false);
			}
			RemovedObject->RemoveFromRoot();
			RemovedObject->ClearFlags(RF_Standalone);
			RemovedObject->Rename(
				nullptr,
				GetTransientPackage(),
				REN_DoNotDirty | REN_DontCreateRedirectors);
			RemovedObject->MarkAsGarbage();
		}
		for (int32 LODIndex = 0; LODIndex < Derived->GetLODNum(); ++LODIndex)
		{
			if (FSkeletalMeshLODInfo* LODInfo = Derived->GetLODInfo(LODIndex))
			{
				for (FName MorphName : MorphNames)
				{
					LODInfo->ImportedMorphTargetSourceFilename.Remove(MorphName.ToString());
				}
			}
		}
		Derived->InitMorphTargets(true);
		return true;
	}

	static FVector3d GetTransferredBodyMorphDelta(
		const FDynamicMesh3& BodyMesh,
		const FNonManifoldMappingSupport& BodyMapping,
		const TVertexAttributesConstRef<FVector3f>& BodyMorphDeltas,
		const FSurfaceCorrespondence& Correspondence)
	{
		if (Correspondence.BodyTriangle == INDEX_NONE)
		{
			return FVector3d::Zero();
		}

		const FIndex3i Triangle = BodyMesh.GetTriangle(Correspondence.BodyTriangle);
		auto DeltaAt = [&](int32 DynamicVertexID)
		{
			const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(DynamicVertexID);
			return FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID)));
		};

		return DeltaAt(Triangle.A) * Correspondence.Barycentric.X
			+ DeltaAt(Triangle.B) * Correspondence.Barycentric.Y
			+ DeltaAt(Triangle.C) * Correspondence.Barycentric.Z;
	}

	static bool MeasureVertexClearancePrepared(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMeshAABBTree3& BodySpatial,
		const FMeshNormals& BodyNormals,
		const FDynamicMesh3& GarmentMesh,
		double& OutMinimumGap,
		int32& OutPenetratingVertices)
	{
		OutMinimumGap = TNumericLimits<double>::Max();
		OutPenetratingVertices = 0;
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d Position = GarmentMesh.GetVertex(VertexID);
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(Position, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID)
			{
				return false;
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh, TriangleID, Position);
			const FVector3d Normal = InterpolateNormal(
				BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			const double SignedGap = (Position - Query.ClosestTrianglePoint).Dot(Normal);
			OutMinimumGap = FMath::Min(OutMinimumGap, SignedGap);
			if (SignedGap < 0.0)
			{
				++OutPenetratingVertices;
			}
		}
		return true;
	}

	static bool MeasureVertexClearance(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		double& OutMinimumGap,
		int32& OutPenetratingVertices)
	{
		const FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		return MeasureVertexClearancePrepared(
			BodyMesh,
			BodySpatial,
			BodyNormals,
			GarmentMesh,
			OutMinimumGap,
			OutPenetratingVertices);
	}

	static bool MeshesIntersectPrepared(const FDynamicMeshAABBTree3& ATree, const FDynamicMesh3& B)
	{
		// The raw-mesh overload walks every ID up to MaxTriangleID and assumes a
		// compact triangle set.  Generated skeletal meshes can contain sparse IDs,
		// so always use a second validated tree.
		const FDynamicMeshAABBTree3 BTree(&B, true);
		return ATree.TestIntersection(BTree);
	}

	static void CollectIntersectingGarmentVerticesPrepared(
		const FDynamicMeshAABBTree3& BodyTree,
		const FDynamicMesh3& GarmentMesh,
		TSet<int32>& OutVertexIDs)
	{
		OutVertexIDs.Reset();
		const FDynamicMeshAABBTree3 GarmentTree(&GarmentMesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			BodyTree.FindAllIntersections(GarmentTree);
		auto AddGarmentTriangle = [&GarmentMesh, &OutVertexIDs](int32 TriangleID)
		{
			if (!GarmentMesh.IsTriangle(TriangleID))
			{
				return;
			}
			const FIndex3i Triangle = GarmentMesh.GetTriangle(TriangleID);
			OutVertexIDs.Add(Triangle.A);
			OutVertexIDs.Add(Triangle.B);
			OutVertexIDs.Add(Triangle.C);
		};
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			AddGarmentTriangle(Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			AddGarmentTriangle(Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			AddGarmentTriangle(Intersection.TriangleID[1]);
		}

		// Include one ring so the correction remains smooth enough for skinning
		// instead of creating a sharp spike at the intersecting triangle.
		const TArray<int32> SeedVertices = OutVertexIDs.Array();
		for (int32 SeedVertexID : SeedVertices)
		{
			for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(SeedVertexID))
			{
				OutVertexIDs.Add(NeighborVertexID);
			}
		}
	}

	struct FRawIntersectionContacts
	{
		TSet<int32> GarmentTriangleIDs;
		TMap<int32, FVector3d> GarmentTriangleNormalSums;
		int32 PrimitiveCount = 0;
		double NormalizedMeasure = 0.0;
	};

	struct FRawIntersectionComponent
	{
		TArray<int32> CoreTriangleIDs;
		TArray<int32> CoreVertexIDs;
		TMap<int32, int32> SupportDepthByVertex;
		FVector3d Direction = FVector3d::Zero();
		double NormalCoherence = 0.0;
		int32 MinimumTriangleID = IndexConstants::InvalidID;
	};

	struct FIntersectionTopologyScore
	{
		int32 IntersectingTierCount = 0;
		int32 ContactTriangleTierPairCount = 0;
		int32 ContactTriangleCount = 0;
		int32 PrimitiveCount = 0;
		double NormalizedMeasure = 0.0;
		double MinimumGap = TNumericLimits<double>::Max();
		double MaximumTopologyDisplacement = 0.0;
		double TopologySquaredEnergy = 0.0;
		double MaximumRepairMagnitude = 0.0;
		double RepairSquaredSum = 0.0;
		double MinimumTriangleNormalDot = 1.0;
		double MaximumAreaScaleDeviation = 0.0;
		double MaximumEdgeScaleDeviation = 0.0;
	};

	static bool CollectRawIntersectingGarmentContactsPrepared(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMeshAABBTree3& BodyTree,
		const FDynamicMesh3& GarmentMesh,
		FRawIntersectionContacts& OutContacts)
	{
		OutContacts = FRawIntersectionContacts();
		const FDynamicMeshAABBTree3 GarmentTree(&GarmentMesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			BodyTree.FindAllIntersections(GarmentTree);
		auto AddContact = [
			&BodyMesh,
			&GarmentMesh,
			&OutContacts](int32 BodyTriangleID, int32 GarmentTriangleID, double NormalizedMeasure)
		{
			if (!BodyMesh.IsTriangle(BodyTriangleID)
				|| !GarmentMesh.IsTriangle(GarmentTriangleID))
			{
				return;
			}
			FVector3d ContactNormal = BodyMesh.GetTriNormal(BodyTriangleID);
			if (!ContactNormal.Normalize())
			{
				return;
			}
			const double Weight = FMath::Max(NormalizedMeasure, 1.e-6);
			OutContacts.GarmentTriangleIDs.Add(GarmentTriangleID);
			OutContacts.GarmentTriangleNormalSums.FindOrAdd(GarmentTriangleID)
				+= ContactNormal * Weight;
			++OutContacts.PrimitiveCount;
			OutContacts.NormalizedMeasure += Weight;
		};
		auto LocalLength = [&GarmentMesh](int32 GarmentTriangleID) -> double
		{
			return FMath::Sqrt(FMath::Max(GarmentMesh.GetTriArea(GarmentTriangleID), 1.e-8));
		};
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			AddContact(Intersection.TriangleID[0], Intersection.TriangleID[1], 1.0);
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			const int32 GarmentTriangleID = Intersection.TriangleID[1];
			const double Scale = GarmentMesh.IsTriangle(GarmentTriangleID)
				? LocalLength(GarmentTriangleID)
				: 1.0;
			AddContact(
				Intersection.TriangleID[0],
				GarmentTriangleID,
				(Intersection.Point[1] - Intersection.Point[0]).Length() / Scale);
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			double PolygonArea = 0.0;
			for (int32 PointIndex = 1; PointIndex + 1 < Intersection.Quantity; ++PointIndex)
			{
				PolygonArea += 0.5 * (Intersection.Point[PointIndex] - Intersection.Point[0])
					.Cross(Intersection.Point[PointIndex + 1] - Intersection.Point[0]).Length();
			}
			const int32 GarmentTriangleID = Intersection.TriangleID[1];
			const double TriangleArea = GarmentMesh.IsTriangle(GarmentTriangleID)
				? FMath::Max(GarmentMesh.GetTriArea(GarmentTriangleID), 1.e-8)
				: 1.0;
			AddContact(
				Intersection.TriangleID[0],
				GarmentTriangleID,
				PolygonArea / TriangleArea);
		}
		return OutContacts.PrimitiveCount > 0
			&& !OutContacts.GarmentTriangleIDs.IsEmpty();
	}

	static bool BuildRawIntersectionComponents(
		const FDynamicMesh3& GarmentMesh,
		const FRawIntersectionContacts& Contacts,
		TArray<FRawIntersectionComponent>& OutComponents)
	{
		OutComponents.Reset();
		if (Contacts.GarmentTriangleIDs.IsEmpty())
		{
			return false;
		}

		TMap<int32, FVector3d> TriangleDirections;
		TArray<int32> SortedContactTriangles = Contacts.GarmentTriangleIDs.Array();
		SortedContactTriangles.Sort();
		for (int32 TriangleID : SortedContactTriangles)
		{
			FVector3d Direction = Contacts.GarmentTriangleNormalSums.FindRef(TriangleID);
			if (!GarmentMesh.IsTriangle(TriangleID) || !Direction.Normalize())
			{
				return false;
			}
			TriangleDirections.Add(TriangleID, Direction);
		}

		// Segment raw contact triangles before any support-ring expansion. The
		// normal compatibility gate prevents a continuous crotch/axilla strip from
		// cancelling two anatomically opposed escape directions into one vector.
		constexpr double MinimumAdjacentNormalDot = 0.50;
		constexpr double MinimumComponentCoherence = 0.20;
		constexpr int32 SupportRingCount = 12;
		TSet<int32> UnvisitedTriangles = Contacts.GarmentTriangleIDs;
		for (int32 SeedTriangleID : SortedContactTriangles)
		{
			if (!UnvisitedTriangles.Contains(SeedTriangleID))
			{
				continue;
			}

			FRawIntersectionComponent Component;
			Component.MinimumTriangleID = SeedTriangleID;
			TArray<int32> PendingTriangles;
			PendingTriangles.Add(SeedTriangleID);
			UnvisitedTriangles.Remove(SeedTriangleID);
			for (int32 PendingIndex = 0; PendingIndex < PendingTriangles.Num(); ++PendingIndex)
			{
				const int32 TriangleID = PendingTriangles[PendingIndex];
				Component.CoreTriangleIDs.Add(TriangleID);
				const FVector3d CurrentDirection = TriangleDirections.FindChecked(TriangleID);
				const FIndex3i TriangleNeighbors = GarmentMesh.GetTriNeighbourTris(TriangleID);
				TArray<int32, TInlineAllocator<3>> SortedNeighbors;
				for (int32 NeighborIndex = 0; NeighborIndex < 3; ++NeighborIndex)
				{
					const int32 NeighborTriangleID = TriangleNeighbors[NeighborIndex];
					if (NeighborTriangleID != IndexConstants::InvalidID
						&& UnvisitedTriangles.Contains(NeighborTriangleID))
					{
						SortedNeighbors.Add(NeighborTriangleID);
					}
				}
				SortedNeighbors.Sort();
				for (int32 NeighborTriangleID : SortedNeighbors)
				{
					if (CurrentDirection.Dot(TriangleDirections.FindChecked(NeighborTriangleID))
						< MinimumAdjacentNormalDot)
					{
						continue;
					}
					if (UnvisitedTriangles.Remove(NeighborTriangleID) > 0)
					{
						PendingTriangles.Add(NeighborTriangleID);
					}
				}
			}

			Component.CoreTriangleIDs.Sort();
			TSet<int32> CoreVertices;
			FVector3d WeightedDirection = FVector3d::Zero();
			double TotalDirectionWeight = 0.0;
			for (int32 TriangleID : Component.CoreTriangleIDs)
			{
				const FIndex3i Triangle = GarmentMesh.GetTriangle(TriangleID);
				CoreVertices.Add(Triangle.A);
				CoreVertices.Add(Triangle.B);
				CoreVertices.Add(Triangle.C);
				const double DirectionWeight = FMath::Max(GarmentMesh.GetTriArea(TriangleID), 1.e-8);
				WeightedDirection += TriangleDirections.FindChecked(TriangleID) * DirectionWeight;
				TotalDirectionWeight += DirectionWeight;
			}
			const double DirectionLength = WeightedDirection.Length();
			Component.NormalCoherence = TotalDirectionWeight > 0.0
				? DirectionLength / TotalDirectionWeight
				: 0.0;
			if (Component.NormalCoherence < MinimumComponentCoherence
				|| !WeightedDirection.Normalize())
			{
				return false;
			}
			Component.Direction = WeightedDirection;
			Component.CoreVertexIDs = CoreVertices.Array();
			Component.CoreVertexIDs.Sort();

			TArray<int32> Frontier = Component.CoreVertexIDs;
			for (int32 VertexID : Component.CoreVertexIDs)
			{
				Component.SupportDepthByVertex.Add(VertexID, 0);
			}
			for (int32 SupportDepth = 1; SupportDepth <= SupportRingCount; ++SupportDepth)
			{
				TSet<int32> NextFrontierSet;
				for (int32 VertexID : Frontier)
				{
					TArray<int32> SortedVertexNeighbors;
					for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(VertexID))
					{
						SortedVertexNeighbors.Add(NeighborVertexID);
					}
					SortedVertexNeighbors.Sort();
					for (int32 NeighborVertexID : SortedVertexNeighbors)
					{
						if (!Component.SupportDepthByVertex.Contains(NeighborVertexID))
						{
							NextFrontierSet.Add(NeighborVertexID);
						}
					}
				}
				Frontier = NextFrontierSet.Array();
				Frontier.Sort();
				for (int32 VertexID : Frontier)
				{
					Component.SupportDepthByVertex.Add(VertexID, SupportDepth);
				}
			}
			OutComponents.Add(MoveTemp(Component));
		}

		OutComponents.Sort([](const FRawIntersectionComponent& A, const FRawIntersectionComponent& B)
		{
			return A.MinimumTriangleID < B.MinimumTriangleID;
		});
		return !OutComponents.IsEmpty();
	}

	static void CollectIntersectingGarmentContactNormalsPrepared(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMeshAABBTree3& BodyTree,
		const FDynamicMesh3& GarmentMesh,
		TMap<int32, FVector3d>& OutVertexNormals)
	{
		OutVertexNormals.Reset();
		const FDynamicMeshAABBTree3 GarmentTree(&GarmentMesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			BodyTree.FindAllIntersections(GarmentTree);
		auto AddContact = [
			&BodyMesh,
			&GarmentMesh,
			&OutVertexNormals](int32 BodyTriangleID, int32 GarmentTriangleID)
		{
			if (!BodyMesh.IsTriangle(BodyTriangleID)
				|| !GarmentMesh.IsTriangle(GarmentTriangleID))
			{
				return;
			}
			FVector3d ContactNormal = BodyMesh.GetTriNormal(BodyTriangleID);
			if (!ContactNormal.Normalize())
			{
				return;
			}
			const FIndex3i GarmentTriangle = GarmentMesh.GetTriangle(GarmentTriangleID);
			OutVertexNormals.FindOrAdd(GarmentTriangle.A) += ContactNormal;
			OutVertexNormals.FindOrAdd(GarmentTriangle.B) += ContactNormal;
			OutVertexNormals.FindOrAdd(GarmentTriangle.C) += ContactNormal;
		};
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			AddContact(Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			AddContact(Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			AddContact(Intersection.TriangleID[0], Intersection.TriangleID[1]);
		}

		const TMap<int32, FVector3d> SeedNormals = OutVertexNormals;
		for (const TPair<int32, FVector3d>& Seed : SeedNormals)
		{
			for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(Seed.Key))
			{
				OutVertexNormals.FindOrAdd(NeighborVertexID) += Seed.Value * 0.5;
			}
		}
	}

	static void ReconcileNonManifoldVectorField(
		const FDynamicMesh3& Mesh,
		TArray<FVector3d>& InOutValues,
		const FThicknessShellCompileData* Shell = nullptr)
	{
		if (InOutValues.Num() < Mesh.MaxVertexID())
		{
			return;
		}
		const FNonManifoldMappingSupport Mapping(Mesh);
		TMap<int32, TArray<int32>> SplitGroups;
		for (int32 VertexID : Mesh.VertexIndicesItr())
		{
			SplitGroups.FindOrAdd(Mapping.GetOriginalNonManifoldVertexID(VertexID)).Add(VertexID);
		}
		for (const TPair<int32, TArray<int32>>& SplitGroup : SplitGroups)
		{
			if (SplitGroup.Value.Num() <= 1)
			{
				continue;
			}
			FVector3d Average = FVector3d::Zero();
			for (int32 SplitVertexID : SplitGroup.Value)
			{
				Average += InOutValues[SplitVertexID];
			}
			Average /= static_cast<double>(SplitGroup.Value.Num());
			for (int32 SplitVertexID : SplitGroup.Value)
			{
				InOutValues[SplitVertexID] = Average;
			}
		}
		if (!Shell || !Shell->bEnabled)
		{
			return;
		}
		for (int32 SourceOrdinal = 0; SourceOrdinal < Shell->PreShellVertexCount; ++SourceOrdinal)
		{
			if (!Shell->OuterVerticesBySourceOrdinal.IsValidIndex(SourceOrdinal)
				|| !Shell->InnerVerticesBySourceOrdinal.IsValidIndex(SourceOrdinal))
			{
				continue;
			}
			const TArray<int32>& OuterVertices = Shell->OuterVerticesBySourceOrdinal[SourceOrdinal];
			const TArray<int32>& InnerVertices = Shell->InnerVerticesBySourceOrdinal[SourceOrdinal];
			FVector3d PairValue = FVector3d::Zero();
			double PairValueSquaredLength = -1.0;
			for (const int32 VertexID : OuterVertices)
			{
				if (InOutValues.IsValidIndex(VertexID))
				{
					const double CandidateSquaredLength = InOutValues[VertexID].SquaredLength();
					if (CandidateSquaredLength > PairValueSquaredLength)
					{
						PairValue = InOutValues[VertexID];
						PairValueSquaredLength = CandidateSquaredLength;
					}
				}
			}
			for (const int32 VertexID : InnerVertices)
			{
				if (InOutValues.IsValidIndex(VertexID))
				{
					const double CandidateSquaredLength = InOutValues[VertexID].SquaredLength();
					if (CandidateSquaredLength > PairValueSquaredLength)
					{
						PairValue = InOutValues[VertexID];
						PairValueSquaredLength = CandidateSquaredLength;
					}
				}
			}
			if (PairValueSquaredLength < 0.0)
			{
				continue;
			}
			for (const int32 VertexID : OuterVertices)
			{
				if (InOutValues.IsValidIndex(VertexID))
				{
					InOutValues[VertexID] = PairValue;
				}
			}
			for (const int32 VertexID : InnerVertices)
			{
				if (InOutValues.IsValidIndex(VertexID))
				{
					InOutValues[VertexID] = PairValue;
				}
			}
		}
	}

	static bool ReconcileThicknessShellCorrespondence(
		const FThicknessShellCompileData& Shell,
		TArray<FSurfaceCorrespondence>& InOutCorrespondence,
		FString& OutError)
	{
		if (!Shell.bEnabled)
		{
			return true;
		}
		for (int32 SourceOrdinal = 0; SourceOrdinal < Shell.PreShellVertexCount; ++SourceOrdinal)
		{
			if (!Shell.OuterVerticesBySourceOrdinal.IsValidIndex(SourceOrdinal)
				|| !Shell.InnerVerticesBySourceOrdinal.IsValidIndex(SourceOrdinal)
				|| Shell.OuterVerticesBySourceOrdinal[SourceOrdinal].IsEmpty()
				|| Shell.InnerVerticesBySourceOrdinal[SourceOrdinal].IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell correspondence pair %d is incomplete."),
					SourceOrdinal);
				return false;
			}
			const int32 AuthoritativeInnerVertexID =
				Shell.InnerVerticesBySourceOrdinal[SourceOrdinal][0];
			if (!InOutCorrespondence.IsValidIndex(AuthoritativeInnerVertexID))
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell inner correspondence %d is invalid."),
					AuthoritativeInnerVertexID);
				return false;
			}
			const FSurfaceCorrespondence Authoritative =
				InOutCorrespondence[AuthoritativeInnerVertexID];
			for (const int32 VertexID : Shell.OuterVerticesBySourceOrdinal[SourceOrdinal])
			{
				if (!InOutCorrespondence.IsValidIndex(VertexID))
				{
					OutError = TEXT("Thickness-shell outer correspondence array is incomplete.");
					return false;
				}
				InOutCorrespondence[VertexID] = Authoritative;
			}
			for (const int32 VertexID : Shell.InnerVerticesBySourceOrdinal[SourceOrdinal])
			{
				if (!InOutCorrespondence.IsValidIndex(VertexID))
				{
					OutError = TEXT("Thickness-shell inner correspondence array is incomplete.");
					return false;
				}
				InOutCorrespondence[VertexID] = Authoritative;
			}
		}
		return true;
	}

	static bool BuildDirectionalClearanceCorrections(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		double DesiredClearance,
		double MaximumRepair,
		TArray<FVector3d>& OutCorrections,
		double& OutMinimumGap)
	{
		FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		const FVector3d BodyCenter = BodyMesh.GetBounds().Center();
		OutCorrections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		OutMinimumGap = TNumericLimits<double>::Max();

		auto MeasurePoint = [&](const FVector3d& Position, FVector3d* OutNormal, FVector3d* OutClosestPoint) -> double
		{
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(Position, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID)
			{
				return -TNumericLimits<double>::Max();
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh, TriangleID, Position);
			const FVector3d Normal = InterpolateNormal(
				BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			if (OutNormal)
			{
				*OutNormal = Normal;
			}
			if (OutClosestPoint)
			{
				*OutClosestPoint = Query.ClosestTrianglePoint;
			}
			return (Position - Query.ClosestTrianglePoint).Dot(Normal);
		};

		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d BasePosition = GarmentMesh.GetVertex(VertexID);
			FVector3d Position = BasePosition;
			FVector3d Correction = FVector3d::Zero();
			double Gap = MeasurePoint(Position, nullptr, nullptr);
			for (int32 Iteration = 0; Gap < DesiredClearance - 0.001 && Iteration < 96; ++Iteration)
			{
				const double Remaining = MaximumRepair - Correction.Length();
				if (Remaining <= 1.e-6)
				{
					break;
				}
				FVector3d NearestNormal;
				FVector3d ClosestPoint;
				Gap = MeasurePoint(Position, &NearestNormal, &ClosestPoint);
				FVector3d FromSurface = Position - ClosestPoint;
				FromSurface.Normalize();
				FVector3d Radial = Position - BodyCenter;
				Radial.Normalize();
				TArray<FVector3d, TInlineAllocator<32>> Directions;
				Directions.Add(NearestNormal);
				Directions.Add(FromSurface);
				Directions.Add(Radial);
				Directions.Add(-NearestNormal);
				for (int32 X = -1; X <= 1; ++X)
				{
					for (int32 Y = -1; Y <= 1; ++Y)
					{
						for (int32 Z = -1; Z <= 1; ++Z)
						{
							if (X != 0 || Y != 0 || Z != 0)
							{
								Directions.Add(FVector3d(X, Y, Z));
							}
						}
					}
				}

				const double MinimumStep = FMath::Min(
					Remaining,
					FMath::Clamp(DesiredClearance - Gap + 0.02, 0.05, 0.50));
				double BestGap = Gap;
				FVector3d BestPosition = Position;
				const double StepCandidates[] = {
					MinimumStep,
					FMath::Min(Remaining, FMath::Max(MinimumStep * 2.0, 0.25)),
					FMath::Min(Remaining, 0.50),
					FMath::Min(Remaining, 1.00)};
				for (double Step : StepCandidates)
				{
					if (Step <= 1.e-6)
					{
						continue;
					}
					for (FVector3d Direction : Directions)
					{
						if (!Direction.Normalize())
						{
							continue;
						}
						const FVector3d CandidatePosition = Position + Direction * Step;
						if ((CandidatePosition - BasePosition).Length() > MaximumRepair + 1.e-6)
						{
							continue;
						}
						const double CandidateGap = MeasurePoint(CandidatePosition, nullptr, nullptr);
						if (CandidateGap > BestGap + 1.e-6)
						{
							BestGap = CandidateGap;
							BestPosition = CandidatePosition;
						}
					}
				}
				if (BestPosition.Equals(Position, 1.e-9))
				{
					break;
				}
				Position = BestPosition;
				Correction = Position - BasePosition;
				Gap = BestGap;
			}
			Gap = MeasurePoint(Position, nullptr, nullptr);
			if (Gap < DesiredClearance - 0.001)
			{
				OutMinimumGap = FMath::Min(OutMinimumGap, Gap);
				return false;
			}
			OutCorrections[VertexID] = Correction;
			OutMinimumGap = FMath::Min(OutMinimumGap, Gap);
		}
		return true;
	}

	static bool BuildMultiTierShapeClearanceCorrections(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		const TArray<FVector3d>& ClearanceDeltas,
		const TArray<FVector3d>& ShapeMorphDeltas,
		const TArray<FVector3d>& ShapeClearanceDirections,
		double DesiredClearance,
		double MaximumRepair,
		bool bScaleCorrectionWithClearanceTier,
		bool bAllowBodyFrameConeDirections,
		bool& bOutUsedBodyFrameConeDirections,
		TArray<FVector3d>& OutCorrections,
		double& OutMinimumGap)
	{
		bOutUsedBodyFrameConeDirections = false;
		if (ClearanceDeltas.Num() < GarmentMesh.MaxVertexID()
			|| ShapeMorphDeltas.Num() < GarmentMesh.MaxVertexID()
			|| ShapeClearanceDirections.Num() < GarmentMesh.MaxVertexID())
		{
			return false;
		}

		const FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		const FVector3d BodyCenter = BodyMesh.GetBounds().Center();
		OutCorrections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		OutMinimumGap = TNumericLimits<double>::Max();
		constexpr double MarchStepCm = 0.10;
		constexpr int32 RefinementIterations = 12;
		constexpr int32 SupportSmoothingIterations = 4;
		constexpr double SupportFalloff = 0.80;

		auto MeasurePoint = [&BodyMesh, &BodySpatial, &BodyNormals](
			const FVector3d& Position,
			FVector3d* OutNormal,
			FVector3d* OutClosestPoint) -> double
		{
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(Position, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID)
			{
				return -TNumericLimits<double>::Max();
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh,
				TriangleID,
				Position);
			const FVector3d Normal = InterpolateNormal(
				BodyMesh,
				BodyNormals,
				TriangleID,
				Query.TriangleBaryCoords);
			if (OutNormal)
			{
				*OutNormal = Normal;
			}
			if (OutClosestPoint)
			{
				*OutClosestPoint = Query.ClosestTrianglePoint;
			}
			return (Position - Query.ClosestTrianglePoint).Dot(Normal);
		};

		TArray<FVector3d> CoherentDirections;
		TArray<double> RequiredDistances;
		TArray<double> StableDistances;
		CoherentDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		RequiredDistances.Init(0.0, GarmentMesh.MaxVertexID());
		StableDistances.Init(0.0, GarmentMesh.MaxVertexID());

		auto ComputeCorrectionRayLimit = [
			&ClearanceDeltas,
			MaximumRepair,
			bScaleCorrectionWithClearanceTier](
				int32 VertexID,
				const FVector3d& UnitDirection) -> double
		{
			if (!bScaleCorrectionWithClearanceTier)
			{
				return MaximumRepair;
			}

			// MaximumRepair bounds the final base clearance delta. Rotating an
			// existing delta inside that sphere can require a correction up to 2M.
			const FVector3d& CurrentDelta = ClearanceDeltas[VertexID];
			if (CurrentDelta.SquaredLength()
				> MaximumRepair * MaximumRepair + 1.e-6)
			{
				return 0.0;
			}
			const double Projection = CurrentDelta.Dot(UnitDirection);
			const double Radicand = Projection * Projection
				+ MaximumRepair * MaximumRepair
				- CurrentDelta.SquaredLength();
			if (Radicand < -1.e-6)
			{
				return 0.0;
			}
			return FMath::Max(
				0.0,
				-Projection + FMath::Sqrt(FMath::Max(0.0, Radicand)));
		};

		auto MeasureAcrossTiers = [
			&GarmentMesh,
			&ClearanceDeltas,
			&ShapeMorphDeltas,
			&MeasurePoint,
			bScaleCorrectionWithClearanceTier](
			int32 VertexID,
			const FVector3d& Correction) -> double
		{
			double MinimumGap = TNumericLimits<double>::Max();
			for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
			{
				const double Tier = FMath::Lerp(
					CertifiedClearanceTierMin,
					CertifiedClearanceTierMax,
					static_cast<double>(TierIndex)
						/ static_cast<double>(CertifiedClearanceTierCount - 1));
				const FVector3d TierPosition = GarmentMesh.GetVertex(VertexID)
					+ ClearanceDeltas[VertexID] * Tier
					+ ShapeMorphDeltas[VertexID]
					+ Correction * (bScaleCorrectionWithClearanceTier ? Tier : 1.0);
				MinimumGap = FMath::Min(MinimumGap, MeasurePoint(TierPosition, nullptr, nullptr));
			}
			return MinimumGap;
		};

		// Solve only a scalar displacement along the body-correspondence direction.
		// The old per-vertex free-direction search could choose unrelated axes on
		// adjacent vertices and fold otherwise valid garment triangles.
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			FVector3d SurfaceDirection = ShapeClearanceDirections[VertexID];
			if (!SurfaceDirection.Normalize())
			{
				SurfaceDirection = ClearanceDeltas[VertexID];
			}
			if (!SurfaceDirection.Normalize())
			{
				double WorstGap = TNumericLimits<double>::Max();
				for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
				{
					const double Tier = FMath::Lerp(
						CertifiedClearanceTierMin,
						CertifiedClearanceTierMax,
						static_cast<double>(TierIndex)
							/ static_cast<double>(CertifiedClearanceTierCount - 1));
					const FVector3d TierPosition = GarmentMesh.GetVertex(VertexID)
						+ ClearanceDeltas[VertexID] * Tier
						+ ShapeMorphDeltas[VertexID];
					FVector3d CandidateNormal;
					const double Gap = MeasurePoint(TierPosition, &CandidateNormal, nullptr);
					if (Gap < WorstGap)
					{
						WorstGap = Gap;
						SurfaceDirection = CandidateNormal;
					}
				}
			}
			if (!SurfaceDirection.Normalize())
			{
				return false;
			}

			FVector3d RadialDirection = GarmentMesh.GetVertex(VertexID)
				+ ShapeMorphDeltas[VertexID]
				+ ClearanceDeltas[VertexID] * CertifiedClearanceTierMin
				- BodyCenter;
			if (!RadialDirection.Normalize())
			{
				RadialDirection = SurfaceDirection;
			}
			// A pure correspondence normal can point from one inner limb directly
			// toward the opposing limb. Blend continuously toward the global radial
			// field only where those two coherent fields disagree; this preserves
			// surface-normal motion elsewhere without making per-vertex axis choices.
			const double DirectionAgreement = SurfaceDirection.Dot(RadialDirection);
			const double RadialWeight = FMath::Clamp((0.35 - DirectionAgreement) * 0.75, 0.0, 1.0);
			FVector3d Direction = SurfaceDirection * (1.0 - RadialWeight)
				+ RadialDirection * RadialWeight;
			if (!Direction.Normalize())
			{
				Direction = RadialDirection;
			}
			CoherentDirections[VertexID] = Direction;

			double VertexMinimumGap = MeasureAcrossTiers(VertexID, FVector3d::Zero());
			if (VertexMinimumGap >= DesiredClearance - 0.001)
			{
				continue;
			}

			FVector3d CurrentWorstNormal = SurfaceDirection;
			FVector3d CurrentWorstFromSurface = SurfaceDirection;
			double CurrentWorstGap = TNumericLimits<double>::Max();
			for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
			{
				const double Tier = FMath::Lerp(
					CertifiedClearanceTierMin,
					CertifiedClearanceTierMax,
					static_cast<double>(TierIndex)
						/ static_cast<double>(CertifiedClearanceTierCount - 1));
				const FVector3d TierPosition = GarmentMesh.GetVertex(VertexID)
					+ ClearanceDeltas[VertexID] * Tier
					+ ShapeMorphDeltas[VertexID];
				FVector3d CurrentNormal;
				FVector3d CurrentClosestPoint;
				const double CurrentGap = MeasurePoint(
					TierPosition,
					&CurrentNormal,
					&CurrentClosestPoint);
				if (CurrentGap < CurrentWorstGap)
				{
					CurrentWorstGap = CurrentGap;
					CurrentWorstNormal = CurrentNormal;
					CurrentWorstFromSurface = TierPosition - CurrentClosestPoint;
				}
			}

			double BestGap = VertexMinimumGap;
			double BestRequiredDistance = TNumericLimits<double>::Max();
			FVector3d BestCorrectionDirection = Direction;
			bool bBestDirectionUsesBodyFrameCone = false;
			auto TryCoherentDirection = [
				&MeasureAcrossTiers,
				&BestGap,
				&BestRequiredDistance,
				&BestCorrectionDirection,
				&bBestDirectionUsesBodyFrameCone,
				&ClearanceDeltas,
				&ComputeCorrectionRayLimit,
				VertexID,
				DesiredClearance,
				MaximumRepair,
				bScaleCorrectionWithClearanceTier](FVector3d CandidateDirection, bool bBodyFrameCone) -> bool
			{
				if (!CandidateDirection.Normalize())
				{
					return false;
				}
				// Keep the normal anatomical fields inside the established stable
				// repair budget. Only the explicit body-frame escape cone may consume
				// the extended emergency budget; otherwise a distant first-choice
				// normal can pre-empt a better alternate direction and fold topology.
				const double RayLimit = ComputeCorrectionRayLimit(VertexID, CandidateDirection);
				const double DirectionMaximumRepair = bBodyFrameCone
					? RayLimit
					: FMath::Min(RayLimit, 10.0);
				if (DirectionMaximumRepair <= 1.e-9)
				{
					return false;
				}
				const int32 DirectionMarchSteps = FMath::Max(
					1,
					FMath::CeilToInt(DirectionMaximumRepair / MarchStepCm));
				double PreviousDistance = 0.0;
				for (int32 StepIndex = 1; StepIndex <= DirectionMarchSteps; ++StepIndex)
				{
					const double Distance = FMath::Min(
						DirectionMaximumRepair,
						StepIndex * MarchStepCm);
					if (bScaleCorrectionWithClearanceTier
						&& (ClearanceDeltas[VertexID] + CandidateDirection * Distance).Length()
							> MaximumRepair + 1.e-6)
					{
						continue;
					}
					const double CandidateGap = MeasureAcrossTiers(
						VertexID,
						CandidateDirection * Distance);
					BestGap = FMath::Max(BestGap, CandidateGap);
					if (CandidateGap >= DesiredClearance - 0.001)
					{
						double LowDistance = PreviousDistance;
						double HighDistance = Distance;
						for (int32 Refinement = 0; Refinement < RefinementIterations; ++Refinement)
						{
							const double MidDistance = (LowDistance + HighDistance) * 0.5;
							if (MeasureAcrossTiers(
								VertexID,
								CandidateDirection * MidDistance)
								>= DesiredClearance - 0.001)
							{
								HighDistance = MidDistance;
							}
							else
							{
								LowDistance = MidDistance;
							}
						}
						if (HighDistance < BestRequiredDistance)
						{
							BestRequiredDistance = HighDistance;
							BestCorrectionDirection = CandidateDirection;
							bBestDirectionUsesBodyFrameCone = bBodyFrameCone;
						}
						return true;
					}
					PreviousDistance = Distance;
				}
				return false;
			};

			bool bFoundCorrection = false;
			// Evaluate every anatomical field. Boolean short-circuiting here used to
			// preserve the first valid direction and miss a smaller coherent repair.
			bFoundCorrection |= TryCoherentDirection(Direction, false);
			bFoundCorrection |= TryCoherentDirection(RadialDirection, false);
			bFoundCorrection |= TryCoherentDirection(CurrentWorstNormal, false);
			bFoundCorrection |= TryCoherentDirection(CurrentWorstFromSurface, false);
			bFoundCorrection |= TryCoherentDirection(SurfaceDirection, false);
			bFoundCorrection |= TryCoherentDirection(ClearanceDeltas[VertexID], false);
			if (!bFoundCorrection && bAllowBodyFrameConeDirections)
			{
				FVector3d NormalDirection = CurrentWorstNormal;
				NormalDirection.Normalize();
				const FVector3d WorldUp(0.0, 0.0, 1.0);
				const FVector3d WorldForward(1.0, 0.0, 0.0);
				FVector3d AxialTangent = WorldUp
					- NormalDirection * NormalDirection.Dot(WorldUp);
				if (!AxialTangent.Normalize())
				{
					AxialTangent = WorldForward
						- NormalDirection * NormalDirection.Dot(WorldForward);
					AxialTangent.Normalize();
				}
				FVector3d CircumferentialTangent = NormalDirection.Cross(AxialTangent);
				CircumferentialTangent.Normalize();
				// These are continuous body-frame cone fields, not unrelated world-axis
				// guesses. They allow one shared correction to escape a concavity where
				// different offset tiers see different nearest body triangles.
				for (double TangentScale : {0.5, 1.0, 2.0})
				{
					for (FVector3d ConeDirection : {
						NormalDirection + AxialTangent * TangentScale,
						NormalDirection - AxialTangent * TangentScale,
						NormalDirection + CircumferentialTangent * TangentScale,
						NormalDirection - CircumferentialTangent * TangentScale})
					{
						bFoundCorrection |= TryCoherentDirection(ConeDirection, true);
					}
				}
			}

			if (!bFoundCorrection)
			{
				UE_LOG(
					LogEFClothingFitCompiler,
					Warning,
					TEXT("Coherent multi-tier solve failed at vertex %d: initial=%.4fcm best=%.4fcm agreement=%.4f."),
					VertexID,
					VertexMinimumGap,
					BestGap,
					DirectionAgreement);
				OutMinimumGap = FMath::Min(OutMinimumGap, BestGap);
				return false;
			}
			CoherentDirections[VertexID] = BestCorrectionDirection;
			RequiredDistances[VertexID] = BestRequiredDistance;
			StableDistances[VertexID] = BestRequiredDistance;
			bOutUsedBodyFrameConeDirections |= bBestDirectionUsesBodyFrameCone;
			if (bBestDirectionUsesBodyFrameCone)
			{
				const FVector3d TierOnePosition = GarmentMesh.GetVertex(VertexID)
					+ ClearanceDeltas[VertexID] * CertifiedClearanceTierMin
					+ ShapeMorphDeltas[VertexID];
				UE_LOG(
					LogEFClothingFitCompiler,
					Display,
					TEXT("V24 cone solve vertex=%d initialGap=%.4fcm distance=%.4fcm direction=(%.4f,%.4f,%.4f) tier1=(%.4f,%.4f,%.4f)."),
					VertexID,
					VertexMinimumGap,
					BestRequiredDistance,
					BestCorrectionDirection.X,
					BestCorrectionDirection.Y,
					BestCorrectionDirection.Z,
					TierOnePosition.X,
					TierOnePosition.Y,
					TierOnePosition.Z);
			}
		}

		// Broaden every required displacement over neighboring rings with a
		// deterministic falloff. This leaves the exact minimum untouched while
		// preventing a one-vertex spike from inverting a skinned triangle.
		for (int32 Iteration = 0; Iteration < SupportSmoothingIterations; ++Iteration)
		{
			TArray<double> SmoothedDistances = StableDistances;
			for (int32 VertexID : GarmentMesh.VertexIndicesItr())
			{
				double NeighborMaximum = 0.0;
				for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(VertexID))
				{
					NeighborMaximum = FMath::Max(NeighborMaximum, StableDistances[NeighborVertexID]);
				}
				const double DirectionRayLimit = ComputeCorrectionRayLimit(
					VertexID,
					CoherentDirections[VertexID]);
				const double ProposedDistance = FMath::Min(
					DirectionRayLimit,
					FMath::Max(
						RequiredDistances[VertexID],
						FMath::Max(StableDistances[VertexID], NeighborMaximum * SupportFalloff)));
				// Around close opposing surfaces (notably the inner thighs), moving
				// farther along an otherwise outward correspondence normal is not
				// guaranteed to be monotonic. Broaden support only when the proposed
				// value itself preserves every certified tier.
				const FVector3d ProposedCorrection =
					CoherentDirections[VertexID] * ProposedDistance;
				const bool bWithinRepairBudget = !bScaleCorrectionWithClearanceTier
					|| (ClearanceDeltas[VertexID] + ProposedCorrection).Length()
						<= MaximumRepair + 1.e-6;
				if (bWithinRepairBudget
					&& (ProposedDistance <= StableDistances[VertexID] + 1.e-9
						|| MeasureAcrossTiers(VertexID, ProposedCorrection)
							>= DesiredClearance - 0.001))
				{
					SmoothedDistances[VertexID] = ProposedDistance;
				}
			}
			StableDistances = MoveTemp(SmoothedDistances);
		}

		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			OutCorrections[VertexID] = CoherentDirections[VertexID] * StableDistances[VertexID];
		}

		if (bOutUsedBodyFrameConeDirections)
		{
			// A cone is an emergency escape from a concavity, so its direction can
			// differ sharply from neighbors that were already point-safe. Broaden
			// the correction VECTOR (not the anatomical morph) with clearance-aware
			// backtracking before triangles are constructed by the caller.
			constexpr int32 ConeVectorSmoothingIterations = 8;
			constexpr int32 ConeVectorBacktrackIterations = 12;
			constexpr double MaximumConeNeighborBlend = 0.65;
			for (int32 Iteration = 0; Iteration < ConeVectorSmoothingIterations; ++Iteration)
			{
				const TArray<FVector3d> BeforeSmoothing = OutCorrections;
				for (int32 VertexID : GarmentMesh.VertexIndicesItr())
				{
					FVector3d NeighborAverage = FVector3d::Zero();
					int32 NeighborCount = 0;
					for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(VertexID))
					{
						NeighborAverage += BeforeSmoothing[NeighborVertexID];
						++NeighborCount;
					}
					if (NeighborCount == 0)
					{
						continue;
					}
					NeighborAverage /= static_cast<double>(NeighborCount);
					const FVector3d CurrentCorrection = BeforeSmoothing[VertexID];
					FVector3d AcceptedCorrection = CurrentCorrection;
					double LowBlend = 0.0;
					double HighBlend = MaximumConeNeighborBlend;
					for (int32 Refinement = 0; Refinement < ConeVectorBacktrackIterations; ++Refinement)
					{
						const double Blend = (LowBlend + HighBlend) * 0.5;
						const FVector3d CandidateCorrection = CurrentCorrection * (1.0 - Blend)
							+ NeighborAverage * Blend;
						const bool bCandidateWithinBudget = bScaleCorrectionWithClearanceTier
							? (ClearanceDeltas[VertexID] + CandidateCorrection).Length()
								<= MaximumRepair + 1.e-6
							: CandidateCorrection.Length() <= MaximumRepair + 1.e-6;
						const bool bCandidateSafe = bCandidateWithinBudget
							&& MeasureAcrossTiers(VertexID, CandidateCorrection)
								>= DesiredClearance - 0.001;
						if (bCandidateSafe)
						{
							LowBlend = Blend;
							AcceptedCorrection = CandidateCorrection;
						}
						else
						{
							HighBlend = Blend;
						}
					}
					OutCorrections[VertexID] = AcceptedCorrection;
				}
			}
		}

		// Re-measure the coherent field before returning it. The caller reconciles
		// the complete requested morph field across non-manifold splits, writes it,
		// reads it back, and invokes this solve again if that committed seam average
		// consumed any clearance reserve.
		OutMinimumGap = TNumericLimits<double>::Max();
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			if (bScaleCorrectionWithClearanceTier
				&& (ClearanceDeltas[VertexID] + OutCorrections[VertexID]).Length()
					> MaximumRepair + 1.e-6)
			{
				return false;
			}
			const double FinalGap = MeasureAcrossTiers(VertexID, OutCorrections[VertexID]);
			OutMinimumGap = FMath::Min(OutMinimumGap, FinalGap);
			if (FinalGap < DesiredClearance - 0.001)
			{
				return false;
			}
		}
		return true;
	}

	static bool BuildAnchoredClearanceCorrections(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMesh3& GarmentMesh,
		const TArray<FVector3d>& RadialDirections,
		const TArray<FVector3d>& ClearanceDirections,
		const TArray<FSurfaceCorrespondence>& RestCorrespondence,
		double DesiredClearance,
		double MaximumRepair,
		TArray<FVector3d>& OutCorrections,
		double& OutMinimumGap)
	{
		if (RadialDirections.Num() < GarmentMesh.MaxVertexID()
			|| ClearanceDirections.Num() < GarmentMesh.MaxVertexID()
			|| RestCorrespondence.Num() < GarmentMesh.MaxVertexID())
		{
			return false;
		}

		FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();
		OutCorrections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		OutMinimumGap = TNumericLimits<double>::Max();

		auto MeasurePoint = [&](const FVector3d& Position) -> double
		{
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(Position, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID)
			{
				return -TNumericLimits<double>::Max();
			}
			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
				BodyMesh, TriangleID, Position);
			const FVector3d Normal = InterpolateNormal(
				BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			return (Position - Query.ClosestTrianglePoint).Dot(Normal);
		};

		constexpr double MarchStepCm = 0.10;
		constexpr int32 RefinementIterations = 10;
		const int32 MarchSteps = FMath::Max(1, FMath::CeilToInt(MaximumRepair / MarchStepCm));
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d BasePosition = GarmentMesh.GetVertex(VertexID);
			double Gap = MeasurePoint(BasePosition);
			if (Gap >= DesiredClearance - 0.001)
			{
				OutMinimumGap = FMath::Min(OutMinimumGap, Gap);
				continue;
			}

			TArray<FVector3d, TInlineAllocator<3>> Directions;
			Directions.Add(RadialDirections[VertexID]);
			Directions.Add(ClearanceDirections[VertexID]);
			Directions.Add(RestCorrespondence[VertexID].SurfaceNormal);
			bool bFoundCorrection = false;
			for (FVector3d Direction : Directions)
			{
				if (!Direction.Normalize())
				{
					continue;
				}
				double PreviousDistance = 0.0;
				for (int32 StepIndex = 1; StepIndex <= MarchSteps; ++StepIndex)
				{
					const double Distance = FMath::Min(MaximumRepair, StepIndex * MarchStepCm);
					const double CandidateGap = MeasurePoint(BasePosition + Direction * Distance);
					if (CandidateGap >= DesiredClearance - 0.001)
					{
						double LowDistance = PreviousDistance;
						double HighDistance = Distance;
						for (int32 Refinement = 0; Refinement < RefinementIterations; ++Refinement)
						{
							const double MidDistance = (LowDistance + HighDistance) * 0.5;
							if (MeasurePoint(BasePosition + Direction * MidDistance) >= DesiredClearance - 0.001)
							{
								HighDistance = MidDistance;
							}
							else
							{
								LowDistance = MidDistance;
							}
						}
						OutCorrections[VertexID] = Direction * HighDistance;
						Gap = MeasurePoint(BasePosition + OutCorrections[VertexID]);
						bFoundCorrection = true;
						break;
					}
					PreviousDistance = Distance;
				}
				if (bFoundCorrection)
				{
					break;
				}
			}
			if (!bFoundCorrection || Gap < DesiredClearance - 0.001)
			{
				OutMinimumGap = FMath::Min(OutMinimumGap, Gap);
				return false;
			}
			OutMinimumGap = FMath::Min(OutMinimumGap, Gap);
		}
		return true;
	}

	static TArray<FName> BakeMorphs(
		USkeletalMesh* BodySurface,
		USkeletalMesh* SourceGarment,
		USkeletalMesh* Derived,
		const UDynamicMesh* BodyDynamicMesh,
		const UDynamicMesh* GarmentDynamicMesh,
		const TArray<FSurfaceCorrespondence>& Correspondence,
		const TArray<FVector3d>& ClearanceDeltas,
		const FThicknessShellCompileData* ThicknessShell,
		const TArray<FString>& ExcludedBodyMorphPrefixes,
		const FEFClothingFitCompileOptions& Options,
		TArray<FEFClothingMorphBinding>& OutBindings,
		TArray<FEFClothingMorphPairCertificate>& OutPairCertificates,
		TArray<FName>& OutMonitoredBodyMorphNames,
		int32& OutValidatedMorphCount,
		int32& OutRepairedMorphCount,
		int32& OutPairBodyProbeCount,
		int32& OutPairOffsetEvaluationCount,
		double& OutMinimumSampledMorphGap,
		double& OutMinimumSampledPairGap,
		double& OutMinimumCertifiedOffsetGap,
		double& OutMaximumMorphDisplacement,
		int32& OutPostThresholdAlteredDeltaCount,
		FString& OutError)
	{
		TArray<FName> TransferredNames;
		OutPairCertificates.Reset();
		OutMonitoredBodyMorphNames.Reset();
		OutValidatedMorphCount = 0;
		OutRepairedMorphCount = 0;
		OutPairBodyProbeCount = 0;
		OutPairOffsetEvaluationCount = 0;
		OutMinimumSampledMorphGap = TNumericLimits<double>::Max();
		OutMinimumSampledPairGap = TNumericLimits<double>::Max();
		OutMinimumCertifiedOffsetGap = TNumericLimits<double>::Max();
		OutMaximumMorphDisplacement = 0.0;
		OutPostThresholdAlteredDeltaCount = 0;
		const FMeshDescription* BodyDescription = BodySurface->GetMeshDescription(0);
		const FMeshDescription* GarmentDescription = SourceGarment->GetMeshDescription(0);
		if (!BodyDescription || !GarmentDescription)
		{
			OutError = TEXT("Body or garment MeshDescription disappeared during morph baking.");
			return TransferredNames;
		}

		const FSkeletalMeshAttributesShared BodyAttributes(*BodyDescription);
		const FDynamicMesh3& BodyMesh = BodyDynamicMesh->GetMeshRef();
		const FDynamicMesh3& GarmentMesh = GarmentDynamicMesh->GetMeshRef();
		const FNonManifoldMappingSupport BodyMapping(BodyMesh);
		if (ClearanceDeltas.Num() < GarmentMesh.MaxVertexID())
		{
			OutError = TEXT("Clearance delta topology does not match the garment morph topology.");
			return TransferredNames;
		}

		TArray<FName> BodyMorphNames;
		TSet<FName> ExistingGarmentMorphNames;
		for (const TObjectPtr<UMorphTarget>& MorphTarget : BodySurface->GetMorphTargets())
		{
			if (IsValid(MorphTarget) && MorphTarget->HasDataForLOD(0) && MorphTarget->GetNumDeltasForLOD(0) > 0)
			{
				BodyMorphNames.AddUnique(MorphTarget->GetFName());
			}
		}
		for (const TObjectPtr<UMorphTarget>& MorphTarget : SourceGarment->GetMorphTargets())
		{
			if (IsValid(MorphTarget) && MorphTarget->HasDataForLOD(0) && MorphTarget->GetNumDeltasForLOD(0) > 0)
			{
				ExistingGarmentMorphNames.Add(MorphTarget->GetFName());
			}
		}
		BodyMorphNames.Sort(FNameLexicalLess());

		TArray<FMorphCandidate> Candidates;
		TArray<FMorphCandidate> TransferCandidates;
		TSet<FName> MonitoredBodyMorphNameSet;
		for (FName MorphName : BodyMorphNames)
		{
			if (MorphName == ClearanceMorphName)
			{
				continue;
			}
			const FString MorphNameString = MorphName.ToString();
			const bool bExcludedMorphNamespace = ExcludedBodyMorphPrefixes.ContainsByPredicate(
				[&MorphNameString](const FString& Prefix)
				{
					return MorphNameString.StartsWith(Prefix, ESearchCase::CaseSensitive);
				});
			if (bExcludedMorphNamespace)
			{
				const TVertexAttributesConstRef<FVector3f> ExcludedMorphDeltas =
					BodyAttributes.GetVertexMorphPositionDelta(MorphName);
				if (ExcludedMorphDeltas.IsValid())
				{
					constexpr double RetainedSurfaceLeakToleranceCm = 1.0e-4;
					double MaximumRetainedDeltaCm = 0.0;
					int32 MaximumRetainedOriginalVertexID = INDEX_NONE;
			for (const int32 BodyVertexID : BodyMesh.VertexIndicesItr())
			{
				// Surface-policy filtering can leave isolated vertices behind in the
				// dynamic mesh. They no longer participate in the retained collision
				// surface, so excluded morph deltas on them are not a runtime leak.
				if (BodyMesh.GetVtxTriangleCount(BodyVertexID) <= 0)
				{
					continue;
				}

				const int32 OriginalVertexID =
					BodyMapping.GetOriginalNonManifoldVertexID(BodyVertexID);
						if (!BodyDescription->Vertices().IsValid(FVertexID(OriginalVertexID)))
						{
							OutError = FString::Printf(
								TEXT("Excluded morph %s maps retained body vertex %d to invalid source vertex %d."),
								*MorphName.ToString(),
								BodyVertexID,
								OriginalVertexID);
							return TransferredNames;
						}
						const double DeltaLengthCm = FVector3d(
							ExcludedMorphDeltas.Get(FVertexID(OriginalVertexID))).Length();
						if (!FMath::IsFinite(DeltaLengthCm))
						{
							OutError = FString::Printf(
								TEXT("Excluded morph %s has a non-finite retained-surface delta at source vertex %d."),
								*MorphName.ToString(),
								OriginalVertexID);
							return TransferredNames;
						}
						if (DeltaLengthCm > MaximumRetainedDeltaCm)
						{
							MaximumRetainedDeltaCm = DeltaLengthCm;
							MaximumRetainedOriginalVertexID = OriginalVertexID;
						}
					}
					if (MaximumRetainedDeltaCm > RetainedSurfaceLeakToleranceCm)
					{
						// A DAZ graft corrective can legitimately move both the excluded
						// accessory and retained base skin. Exclusion means that this morph
						// must not be transferred to or monitored on the garment; it does
						// not imply a zero delta on the visible body. SurfaceWrapGPU reads
						// the final animated body geometry, so that retained-skin motion is
						// still followed at runtime without duplicating the graft morph.
						UE_LOG(
							LogEFClothingFitCompiler,
							Verbose,
							TEXT("Excluded body morph %s also deforms retained body-surface vertex %d by %.6fcm; retained skin remains authoritative for SurfaceWrapGPU."),
							*MorphName.ToString(),
							MaximumRetainedOriginalVertexID,
							MaximumRetainedDeltaCm);
					}
				}
				continue;
			}
			const TVertexAttributesConstRef<FVector3f> BodyMorphDeltas = BodyAttributes.GetVertexMorphPositionDelta(MorphName);
			if (!BodyMorphDeltas.IsValid())
			{
				continue;
			}

			double MaxDelta = 0.0;
			for (int32 VertexID : GarmentMesh.VertexIndicesItr())
			{
				MaxDelta = FMath::Max(MaxDelta, GetTransferredBodyMorphDelta(
					BodyMesh, BodyMapping, BodyMorphDeltas, Correspondence[VertexID]).Length());
			}
			if (MaxDelta > 1.e-6)
			{
				MonitoredBodyMorphNameSet.Add(MorphName);
			}
			if (!Options.bCompileBodyMorphBindings)
			{
				continue;
			}

			if (ExistingGarmentMorphNames.Contains(MorphName))
			{
				// Existing garment morphs are deliberately rebuilt from the body
				// correspondence too. Their mere presence does not prove that their
				// skinning and authored deltas preserve clearance on this body.
				Candidates.Add({MorphName, MaxDelta, false});
				continue;
			}

			if (!Options.bTransferMissingBodyMorphs || Options.MaximumTransferredMorphs <= 0)
			{
				continue;
			}
			if (MaxDelta >= Options.MinimumTransferredMorphDeltaCm)
			{
				TransferCandidates.Add({MorphName, MaxDelta, true});
			}
		}

		TransferCandidates.Sort([](const FMorphCandidate& A, const FMorphCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.MaxDelta, B.MaxDelta))
			{
				return A.MaxDelta > B.MaxDelta;
			}
			return A.Name.LexicalLess(B.Name);
		});
		if (TransferCandidates.Num() > Options.MaximumTransferredMorphs)
		{
			OutError = FString::Printf(
				TEXT("Morph coverage is incomplete: %d relevant body targets exceed MaximumTransferredMorphs=%d."),
				TransferCandidates.Num(), Options.MaximumTransferredMorphs);
			return TransferredNames;
		}
		Candidates.Append(TransferCandidates);
		for (const FMorphCandidate& Candidate : Candidates)
		{
			MonitoredBodyMorphNameSet.Add(Candidate.Name);
		}
		OutMonitoredBodyMorphNames = MonitoredBodyMorphNameSet.Array();
		OutMonitoredBodyMorphNames.Sort(FNameLexicalLess());
		// Certify the largest geometric changes first so a difficult shape fails
		// fast without weakening full-catalog coverage or determinism.
		Candidates.Sort([](const FMorphCandidate& A, const FMorphCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.MaxDelta, B.MaxDelta))
			{
				return A.MaxDelta > B.MaxDelta;
			}
			return A.Name.LexicalLess(B.Name);
		});

		const int32 SampleCount = FMath::Clamp(Options.MorphClearanceSampleCount, 2, 8);
		const double DesiredClearance = FMath::Max(static_cast<double>(Options.MinimumClearanceCm), 0.02);
		// Linear morph interpolation and changing nearest triangles can slightly
		// erode a just-equal solve at other samples. Certify against the requested
		// value but repair toward a small deterministic reserve.
		const double MorphSolveClearance = DesiredClearance + CompilerClearanceReserveCm;
		const double MorphFallbackClearance = DesiredClearance + CompilerClearanceReserveCm * 0.5;
		const double MaximumRepair = FMath::Max(static_cast<double>(Options.MaximumMorphRepairCm), DesiredClearance);
		const double StandardShapeRepair = FMath::Min(MaximumRepair, 10.0);
		constexpr double ClearanceToleranceCm = 0.001;
		TSet<FName> GeneratedSampleMorphNames = ExistingGarmentMorphNames;
		GeneratedSampleMorphNames.Add(ClearanceMorphName);
		for (FName BodyMorphName : BodyMorphNames)
		{
			GeneratedSampleMorphNames.Add(BodyMorphName);
		}
		const FVector3d RestBodyCenter = BodyMesh.GetBounds().Center();
		TArray<FVector3d> AnchoredRadialDirections;
		TArray<FVector3d> AnchoredClearanceDirections;
		AnchoredRadialDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		AnchoredClearanceDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d BaseFittedPosition = GarmentMesh.GetVertex(VertexID) + ClearanceDeltas[VertexID];
			AnchoredRadialDirections[VertexID] = BaseFittedPosition - RestBodyCenter;
			AnchoredClearanceDirections[VertexID] = ClearanceDeltas[VertexID].IsNearlyZero(1.e-8)
				? Correspondence[VertexID].SurfaceNormal
				: ClearanceDeltas[VertexID];
		}

		auto MeasureRuntimeShapeAcrossOffsetTiers = [
			&GarmentMesh,
			&ClearanceDeltas,
			DesiredClearance,
			ClearanceToleranceCm](
			const FDynamicMesh3& ShapeBody,
			const TArray<FVector3d>& ShapeMorphDeltas,
			double& OutMinimumGap,
			bool& bOutIntersects,
			double& OutMinimumClearanceMultiplier) -> bool
		{
			if (ShapeMorphDeltas.Num() < GarmentMesh.MaxVertexID())
			{
				return false;
			}
			const FDynamicMeshAABBTree3 BodySpatial(&ShapeBody, true);
			FMeshNormals BodyNormals(&ShapeBody);
			BodyNormals.ComputeVertexNormals();
			TArray<double> TierGaps;
			TArray<uint8> TierMeasured;
			TArray<uint8> TierIntersections;
			TierGaps.Init(0.0, CertifiedClearanceTierCount);
			TierMeasured.Init(0, CertifiedClearanceTierCount);
			TierIntersections.Init(0, CertifiedClearanceTierCount);
			ParallelFor(CertifiedClearanceTierCount, [&](int32 TierIndex)
			{
				const double Tier = FMath::Lerp(
					CertifiedClearanceTierMin,
					CertifiedClearanceTierMax,
					static_cast<double>(TierIndex) / static_cast<double>(CertifiedClearanceTierCount - 1));
				FDynamicMesh3 TierGarment(GarmentMesh);
				for (int32 VertexID : TierGarment.VertexIndicesItr())
				{
					TierGarment.SetVertex(
						VertexID,
						TierGarment.GetVertex(VertexID)
							+ ClearanceDeltas[VertexID] * Tier
							+ ShapeMorphDeltas[VertexID]);
				}
				double TierGap = 0.0;
				int32 TierPenetratingVertices = 0;
				TierMeasured[TierIndex] = MeasureVertexClearancePrepared(
					ShapeBody,
					BodySpatial,
					BodyNormals,
					TierGarment,
					TierGap,
					TierPenetratingVertices) ? 1 : 0;
				TierGaps[TierIndex] = TierGap;
				TierIntersections[TierIndex] = MeshesIntersectPrepared(BodySpatial, TierGarment) ? 1 : 0;
			});
			OutMinimumGap = TNumericLimits<double>::Max();
			bOutIntersects = false;
			OutMinimumClearanceMultiplier = CertifiedClearanceTierMax;
			for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
			{
				if (TierMeasured[TierIndex] == 0)
				{
					return false;
				}
			}

			// A public offset request is rounded upward at runtime. Certify the
			// lowest contiguous suffix whose every tier is clear; lower unsafe tiers
			// become an automatic per-shape floor rather than forcing destructive
			// sculpting of an otherwise valid garment.
			int32 MinimumPassingTierIndex = INDEX_NONE;
			bool bPassingSuffix = true;
			for (int32 TierIndex = CertifiedClearanceTierCount - 1; TierIndex >= 0; --TierIndex)
			{
				const bool bTierPass = TierGaps[TierIndex]
						>= DesiredClearance - ClearanceToleranceCm
					&& TierIntersections[TierIndex] == 0;
				bPassingSuffix &= bTierPass;
				if (bPassingSuffix)
				{
					MinimumPassingTierIndex = TierIndex;
				}
			}
			if (MinimumPassingTierIndex == INDEX_NONE)
			{
				for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
				{
					OutMinimumGap = FMath::Min(OutMinimumGap, TierGaps[TierIndex]);
					bOutIntersects |= TierIntersections[TierIndex] != 0;
				}
				return true;
			}

			OutMinimumClearanceMultiplier = FMath::Lerp(
				CertifiedClearanceTierMin,
				CertifiedClearanceTierMax,
				static_cast<double>(MinimumPassingTierIndex)
					/ static_cast<double>(CertifiedClearanceTierCount - 1));
			for (int32 TierIndex = MinimumPassingTierIndex;
				TierIndex < CertifiedClearanceTierCount;
				++TierIndex)
			{
				OutMinimumGap = FMath::Min(OutMinimumGap, TierGaps[TierIndex]);
			}
			return true;
		};

		auto WriteAndCertifyStoredShapeMorph = [
			Derived,
			ThicknessShell,
			&GarmentMesh,
			&ClearanceDeltas,
			&MeasureRuntimeShapeAcrossOffsetTiers,
			&OutPostThresholdAlteredDeltaCount,
			&OutError,
			MaximumRepair,
			MorphSolveClearance,
			DesiredClearance,
			ClearanceToleranceCm](
			const FDynamicMesh3& ShapeBody,
			const TArray<FVector3d>& ShapeClearanceDirections,
			FName StoredMorphName,
			const FString& Context,
			TArray<FVector3d>& InOutShapeMorphDeltas,
			bool& bOutRepaired,
			double& OutMinimumClearanceMultiplier) -> bool
		{
			constexpr int32 MaximumStoredMorphCookPasses = 8;
			// Triangle crossings can persist even with >0.5cm vertex gaps on coarse
			// topology. Use a material patch nudge, still bounded by the per-vertex
			// MaximumMorphRepairCm contract, rather than many sub-threshold taps.
			constexpr double IntersectionNudgeCm = 0.25;
			if (StoredMorphName.IsNone()
				|| ShapeClearanceDirections.Num() < GarmentMesh.MaxVertexID()
				|| InOutShapeMorphDeltas.Num() < GarmentMesh.MaxVertexID())
			{
				OutError = FString::Printf(TEXT("Stored morph certification received invalid input for %s."), *Context);
				return false;
			}

			TArray<FVector3d> RequestedDeltas = InOutShapeMorphDeltas;
			ReconcileNonManifoldVectorField(GarmentMesh, RequestedDeltas, ThicknessShell);
			const TArray<FVector3d> InitialRequestedDeltas = RequestedDeltas;
			const FDynamicMeshAABBTree3 ShapeBodySpatial(&ShapeBody, true);
			FMeshNormals ShapeBodyNormals(&ShapeBody);
			ShapeBodyNormals.ComputeVertexNormals();
			TSet<uint32> FailedStoredMorphHashes;
			OutMinimumClearanceMultiplier = CertifiedClearanceTierMax;
			int32 PreviousIntersectingTierCount = TNumericLimits<int32>::Max();
			int32 PreviousFailedTierCount = TNumericLimits<int32>::Max();
			double PreviousMinimumGap = -TNumericLimits<double>::Max();

			for (int32 CookPass = 0; CookPass < MaximumStoredMorphCookPasses; ++CookPass)
			{
				UDynamicMesh* MorphMesh = NewObject<UDynamicMesh>(GetTransientPackage());
				MorphMesh->SetMesh(GarmentMesh);
				MorphMesh->EditMesh([&](FDynamicMesh3& EditMesh)
				{
					for (int32 VertexID : EditMesh.VertexIndicesItr())
					{
						EditMesh.SetVertex(
							VertexID,
							EditMesh.GetVertex(VertexID) + RequestedDeltas[VertexID]);
					}
				}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions, false);

				FString MorphError;
				if (!WriteMorph(MorphMesh, Derived, StoredMorphName, MorphError))
				{
					OutError = MorphError;
					return false;
				}

				TArray<FVector3d> StoredDeltas;
				int32 CookPassAlteredDeltaCount = 0;
				if (!ReadStoredMorphDeltas(
					Derived,
					StoredMorphName,
					GarmentMesh,
					&RequestedDeltas,
					StoredDeltas,
					CookPassAlteredDeltaCount,
					OutError))
				{
					return false;
				}

				double CookPassMinimumGap = TNumericLimits<double>::Max();
				int32 FailedTierCount = 0;
				int32 IntersectingTierCount = 0;
				int32 FailingTierIndex = INDEX_NONE;
				double FailingTierGap = TNumericLimits<double>::Max();
				bool bFailingTierIntersects = false;
				TArray<double, TInlineAllocator<CertifiedClearanceTierCount>> TierGaps;
				TArray<uint8, TInlineAllocator<CertifiedClearanceTierCount>> TierPasses;
				TierGaps.Init(0.0, CertifiedClearanceTierCount);
				TierPasses.Init(0, CertifiedClearanceTierCount);
				for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
				{
					const double Tier = FMath::Lerp(
						CertifiedClearanceTierMin,
						CertifiedClearanceTierMax,
						static_cast<double>(TierIndex) / static_cast<double>(CertifiedClearanceTierCount - 1));
					FDynamicMesh3 TierGarment(GarmentMesh);
					for (int32 VertexID : TierGarment.VertexIndicesItr())
					{
						TierGarment.SetVertex(
							VertexID,
							TierGarment.GetVertex(VertexID)
								+ ClearanceDeltas[VertexID] * Tier
								+ StoredDeltas[VertexID]);
					}
					double TierGap = 0.0;
					int32 TierPenetratingVertices = 0;
					const bool bMeasured = MeasureVertexClearancePrepared(
						ShapeBody,
						ShapeBodySpatial,
						ShapeBodyNormals,
						TierGarment,
						TierGap,
						TierPenetratingVertices);
					const bool bIntersects = MeshesIntersectPrepared(ShapeBodySpatial, TierGarment);
					const bool bTierFailed = !bMeasured
						|| TierGap < DesiredClearance - ClearanceToleranceCm
						|| bIntersects;
					TierGaps[TierIndex] = TierGap;
					TierPasses[TierIndex] = bTierFailed ? 0 : 1;
					FailedTierCount += bTierFailed ? 1 : 0;
					IntersectingTierCount += bIntersects ? 1 : 0;
					if (bTierFailed
						&& (FailingTierIndex == INDEX_NONE
							|| (bIntersects && !bFailingTierIntersects)
							|| (bIntersects == bFailingTierIntersects && TierGap < FailingTierGap)))
					{
						FailingTierIndex = TierIndex;
						FailingTierGap = TierGap;
						bFailingTierIntersects = bIntersects;
					}
				}

				int32 MinimumPassingTierIndex = INDEX_NONE;
				bool bPassingSuffix = true;
				for (int32 TierIndex = CertifiedClearanceTierCount - 1; TierIndex >= 0; --TierIndex)
				{
					bPassingSuffix &= TierPasses[TierIndex] != 0;
					if (bPassingSuffix)
					{
						MinimumPassingTierIndex = TierIndex;
					}
				}
				if (MinimumPassingTierIndex != INDEX_NONE)
				{
					CookPassMinimumGap = TNumericLimits<double>::Max();
					for (int32 TierIndex = MinimumPassingTierIndex;
						TierIndex < CertifiedClearanceTierCount;
						++TierIndex)
					{
						CookPassMinimumGap = FMath::Min(CookPassMinimumGap, TierGaps[TierIndex]);
					}
					OutMinimumClearanceMultiplier = FMath::Lerp(
						CertifiedClearanceTierMin,
						CertifiedClearanceTierMax,
						static_cast<double>(MinimumPassingTierIndex)
							/ static_cast<double>(CertifiedClearanceTierCount - 1));
					InOutShapeMorphDeltas = MoveTemp(StoredDeltas);
					OutPostThresholdAlteredDeltaCount += CookPassAlteredDeltaCount;
					UE_LOG(
						LogEFClothingFitCompiler,
						Display,
						TEXT("V24 stored morph %s certified after %d cook pass(es): minimum tier %.3f, gap %.4fcm."),
						*Context,
						CookPass + 1,
						OutMinimumClearanceMultiplier,
						CookPassMinimumGap);
					return true;
				}
				CookPassMinimumGap = TNumericLimits<double>::Max();
				for (double TierGap : TierGaps)
				{
					CookPassMinimumGap = FMath::Min(CookPassMinimumGap, TierGap);
				}

				uint32 StoredMorphHash = 0;
				for (int32 VertexID : GarmentMesh.VertexIndicesItr())
				{
					StoredMorphHash = HashCombineFast(StoredMorphHash, GetTypeHash(StoredDeltas[VertexID]));
				}
				if (FailedStoredMorphHashes.Contains(StoredMorphHash))
				{
					OutError = FString::Printf(TEXT("Stored morph auto-sculpt for %s detected a cycle."), *Context);
					return false;
				}
				FailedStoredMorphHashes.Add(StoredMorphHash);
				if (CookPass > 0)
				{
					const bool bImproved = IntersectingTierCount < PreviousIntersectingTierCount
						|| (IntersectingTierCount == PreviousIntersectingTierCount
							&& FailedTierCount < PreviousFailedTierCount)
						|| (IntersectingTierCount == PreviousIntersectingTierCount
							&& FailedTierCount == PreviousFailedTierCount
							&& CookPassMinimumGap > PreviousMinimumGap + 1.e-6);
					if (!bImproved)
					{
						OutError = FString::Printf(
							TEXT("Stored morph auto-sculpt for %s did not improve on cook pass %d (intersections %d, failed tiers %d, gap %.4fcm)."),
							*Context,
							CookPass + 1,
							IntersectingTierCount,
							FailedTierCount,
							CookPassMinimumGap);
						return false;
					}
				}
				PreviousIntersectingTierCount = IntersectingTierCount;
				PreviousFailedTierCount = FailedTierCount;
				PreviousMinimumGap = CookPassMinimumGap;

				const double FailingTier = FMath::Lerp(
					CertifiedClearanceTierMin,
					CertifiedClearanceTierMax,
					static_cast<double>(FailingTierIndex) / static_cast<double>(CertifiedClearanceTierCount - 1));
				if (CookPass + 1 >= MaximumStoredMorphCookPasses)
				{
					OutError = FString::Printf(
						TEXT("Stored morph %s failed certified tier %.3f after %d cook passes: gap %.4fcm, triangles=%s."),
						*Context,
						FailingTier,
						MaximumStoredMorphCookPasses,
						FailingTierGap,
						bFailingTierIntersects ? TEXT("INTERSECT") : TEXT("CLEAR"));
					return false;
				}

				TArray<FVector3d> Corrections;
				double RepairGap = TNumericLimits<double>::Max();
				double CoherentRepairTargetClearance = MorphSolveClearance;
				const double ReserveMaximumRepair = FMath::Min(MaximumRepair, 10.0);
				bool bUsedBodyFrameConeDirections = false;
				bool bBuiltCoherentRepair = BuildMultiTierShapeClearanceCorrections(
					ShapeBody,
					GarmentMesh,
					ClearanceDeltas,
					StoredDeltas,
					ShapeClearanceDirections,
					MorphSolveClearance,
					ReserveMaximumRepair,
					false,
					false,
					bUsedBodyFrameConeDirections,
					Corrections,
					RepairGap);
				if (!bBuiltCoherentRepair)
				{
					// The reserve is intentionally stronger than the public certificate.
					// In narrow anatomical corridors it can be impossible to increase a
					// point farther without approaching an opposing body surface. Retry at
					// the exact certified clearance; triangle crossings remain forbidden.
					CoherentRepairTargetClearance = DesiredClearance;
					bBuiltCoherentRepair = BuildMultiTierShapeClearanceCorrections(
						ShapeBody,
						GarmentMesh,
						ClearanceDeltas,
						StoredDeltas,
						ShapeClearanceDirections,
						DesiredClearance,
						MaximumRepair,
						false,
						true,
						bUsedBodyFrameConeDirections,
						Corrections,
						RepairGap);
				}
				if (!bBuiltCoherentRepair)
				{
					OutError = FString::Printf(
						TEXT("Stored morph auto-sculpt could not repair %s at tier %.3f within %.4fcm (gap %.4fcm)."),
						*Context,
						FailingTier,
						MaximumRepair,
						RepairGap);
					return false;
				}

				RequestedDeltas = MoveTemp(StoredDeltas);
				bool bAppliedCorrection = false;
				for (int32 VertexID : GarmentMesh.VertexIndicesItr())
				{
					const FVector3d CandidateDelta = RequestedDeltas[VertexID] + Corrections[VertexID];
					if ((CandidateDelta - InitialRequestedDeltas[VertexID]).Length() > MaximumRepair + 1.e-6)
					{
						OutError = FString::Printf(
							TEXT("Stored morph auto-sculpt for %s exhausted MaximumMorphRepairCm %.4f at vertex %d."),
							*Context,
							MaximumRepair,
							VertexID);
						return false;
					}
					bAppliedCorrection |= !Corrections[VertexID].IsNearlyZero(1.e-8);
					RequestedDeltas[VertexID] = CandidateDelta;
				}
				ReconcileNonManifoldVectorField(GarmentMesh, RequestedDeltas, ThicknessShell);

				// The coherent surface correction may already have established a
				// certified upper suffix of runtime clearance tiers. Persist that
				// correction on the next cook pass before attempting triangle-level
				// topology repair. Lower unsafe tiers will be excluded by the
				// per-shape automatic clearance floor stored in the certificate.
				double RepairedSuffixMinimumGap = TNumericLimits<double>::Max();
				double RepairedSuffixMinimumMultiplier = CertifiedClearanceTierMax;
				bool bRepairedSuffixIntersects = false;
				if (MeasureRuntimeShapeAcrossOffsetTiers(
					ShapeBody,
					RequestedDeltas,
					RepairedSuffixMinimumGap,
					bRepairedSuffixIntersects,
					RepairedSuffixMinimumMultiplier)
					&& RepairedSuffixMinimumGap >= DesiredClearance - ClearanceToleranceCm
					&& !bRepairedSuffixIntersects)
				{
					bOutRepaired |= bAppliedCorrection;
					UE_LOG(
						LogEFClothingFitCompiler,
						Display,
						TEXT("V25 stored morph %s coherent repair established certified suffix %.3f (gap %.4fcm); deferring to cook/readback."),
						*Context,
						RepairedSuffixMinimumMultiplier,
						RepairedSuffixMinimumGap);
					continue;
				}

				// Diagnostic reserve probes determine whether a coarse body/garment
				// triangle crossing can be solved by a larger geometric clearance
				// tier without weakening topology-quality gates. These values are not
				// accepted by the current public certificate unless its range is
				// explicitly expanded in a later compiler schema.
				for (int32 ExtraTierIndex = 1; ExtraTierIndex <= 8; ++ExtraTierIndex)
				{
					const double ExtraTier = CertifiedClearanceTierMax
						+ static_cast<double>(ExtraTierIndex) * 0.125;
					FDynamicMesh3 ExtraTierGarment(GarmentMesh);
					for (int32 VertexID : ExtraTierGarment.VertexIndicesItr())
					{
						ExtraTierGarment.SetVertex(
							VertexID,
							ExtraTierGarment.GetVertex(VertexID)
								+ ClearanceDeltas[VertexID] * ExtraTier
								+ RequestedDeltas[VertexID]);
					}
					double ExtraTierGap = 0.0;
					int32 ExtraTierPenetratingVertices = 0;
					const bool bExtraTierMeasured = MeasureVertexClearancePrepared(
						ShapeBody,
						ShapeBodySpatial,
						ShapeBodyNormals,
						ExtraTierGarment,
						ExtraTierGap,
						ExtraTierPenetratingVertices);
					const bool bExtraTierIntersects = MeshesIntersectPrepared(
						ShapeBodySpatial,
						ExtraTierGarment);
					UE_LOG(
						LogEFClothingFitCompiler,
						Display,
						TEXT("V25 extended clearance probe %s tier %.3f: measured=%s gap=%.4fcm intersects=%s."),
						*Context,
						ExtraTier,
						bExtraTierMeasured ? TEXT("true") : TEXT("false"),
						ExtraTierGap,
						bExtraTierIntersects ? TEXT("true") : TEXT("false"));
				}

				const TArray<FVector3d> TopologyBaselineDeltas = RequestedDeltas;
				const double MaximumTopologyExtraRepair = FMath::Min(MaximumRepair, 4.0);
				constexpr double MaximumTopologyRmsRepair = 1.50;
				// When no certified suffix exists, repair the maximum runtime tier
				// first. Clearing that tier is sufficient to create a safe one-tier
				// suffix; the runtime will then enforce it as this shape's automatic
				// floor. Requiring topology progress at lower tiers simultaneously can
				// reject a valid high-tier repair because those tiers will never render.
				constexpr int32 MinimumTopologyTierIndex = CertifiedClearanceTierCount - 1;
				constexpr double TopologyReferenceTier = CertifiedClearanceTierMax;

				// Vertex distance alone cannot detect a triangle/edge crossing. Evaluate
				// each repair as a transaction across the suffix being established and
				// commit it only when the raw intersection topology strictly improves.
				auto EvaluateIntersectionCandidate = [
					&GarmentMesh,
					&ClearanceDeltas,
					&InitialRequestedDeltas,
					&TopologyBaselineDeltas,
					&ShapeBody,
					&ShapeBodySpatial,
					&ShapeBodyNormals,
					MinimumTopologyTierIndex](
						const TArray<FVector3d>& CandidateDeltas,
						FIntersectionTopologyScore& OutScore,
						FRawIntersectionContacts& OutContacts,
						FString& OutEvaluationError) -> bool
				{
					OutScore = FIntersectionTopologyScore();
					OutContacts = FRawIntersectionContacts();
					for (int32 VertexID : GarmentMesh.VertexIndicesItr())
					{
						const double TopologyDisplacement = (
							CandidateDeltas[VertexID]
							- TopologyBaselineDeltas[VertexID]).Length();
						OutScore.MaximumTopologyDisplacement = FMath::Max(
							OutScore.MaximumTopologyDisplacement,
							TopologyDisplacement);
						OutScore.TopologySquaredEnergy +=
							TopologyDisplacement * TopologyDisplacement;
						const double RepairMagnitude = (
							CandidateDeltas[VertexID]
							- InitialRequestedDeltas[VertexID]).Length();
						OutScore.MaximumRepairMagnitude = FMath::Max(
							OutScore.MaximumRepairMagnitude,
							RepairMagnitude);
						OutScore.RepairSquaredSum += RepairMagnitude * RepairMagnitude;
					}
					for (int32 TierIndex = MinimumTopologyTierIndex;
						TierIndex < CertifiedClearanceTierCount;
						++TierIndex)
					{
						const double Tier = FMath::Lerp(
							CertifiedClearanceTierMin,
							CertifiedClearanceTierMax,
							static_cast<double>(TierIndex)
								/ static_cast<double>(CertifiedClearanceTierCount - 1));
						FDynamicMesh3 CandidateTierGarment(GarmentMesh);
						for (int32 VertexID : CandidateTierGarment.VertexIndicesItr())
						{
							CandidateTierGarment.SetVertex(
								VertexID,
								CandidateTierGarment.GetVertex(VertexID)
									+ ClearanceDeltas[VertexID] * Tier
									+ CandidateDeltas[VertexID]);
						}

						// Reject topology fixes that clear the body by folding, collapsing or
						// excessively stretching the garment relative to the post-clearance
						// baseline at the same offset tier.
							constexpr double MinimumAreaRatio = 0.50;
							constexpr double MaximumAreaRatio = 2.0;
							constexpr double MinimumEdgeRatio = 0.50;
							constexpr double MaximumEdgeRatio = 2.0;
							constexpr double MinimumNormalDotReserve = 0.50;
						for (int32 TriangleID : GarmentMesh.TriangleIndicesItr())
						{
							const FIndex3i Triangle = GarmentMesh.GetTriangle(TriangleID);
							const FVector3d BaselineA = GarmentMesh.GetVertex(Triangle.A)
								+ ClearanceDeltas[Triangle.A] * Tier
								+ TopologyBaselineDeltas[Triangle.A];
							const FVector3d BaselineB = GarmentMesh.GetVertex(Triangle.B)
								+ ClearanceDeltas[Triangle.B] * Tier
								+ TopologyBaselineDeltas[Triangle.B];
							const FVector3d BaselineC = GarmentMesh.GetVertex(Triangle.C)
								+ ClearanceDeltas[Triangle.C] * Tier
								+ TopologyBaselineDeltas[Triangle.C];
							const FVector3d CandidateA = CandidateTierGarment.GetVertex(Triangle.A);
							const FVector3d CandidateB = CandidateTierGarment.GetVertex(Triangle.B);
							const FVector3d CandidateC = CandidateTierGarment.GetVertex(Triangle.C);
							FVector3d BaselineNormal = (BaselineB - BaselineA).Cross(BaselineC - BaselineA);
							FVector3d CandidateNormal = (CandidateB - CandidateA).Cross(CandidateC - CandidateA);
							const double BaselineAreaTwice = BaselineNormal.Length();
							const double CandidateAreaTwice = CandidateNormal.Length();
							if (!FMath::IsFinite(BaselineAreaTwice)
								|| !FMath::IsFinite(CandidateAreaTwice)
								|| BaselineAreaTwice <= 1.e-8
								|| CandidateAreaTwice <= 1.e-8)
							{
								OutEvaluationError = TEXT("Topology candidate contains a degenerate triangle.");
								return false;
							}
							const double AreaRatio = CandidateAreaTwice / BaselineAreaTwice;
							BaselineNormal /= BaselineAreaTwice;
							CandidateNormal /= CandidateAreaTwice;
							const double NormalDot = BaselineNormal.Dot(CandidateNormal);
							OutScore.MinimumTriangleNormalDot = FMath::Min(
								OutScore.MinimumTriangleNormalDot,
								NormalDot);
							OutScore.MaximumAreaScaleDeviation = FMath::Max(
								OutScore.MaximumAreaScaleDeviation,
								FMath::Abs(FMath::Loge(AreaRatio)));
							if (AreaRatio < MinimumAreaRatio
								|| AreaRatio > MaximumAreaRatio
								|| !FMath::IsFinite(NormalDot)
								|| NormalDot < MinimumNormalDotReserve)
							{
								OutEvaluationError = TEXT("Topology candidate inverted or excessively changed a triangle area.");
								return false;
							}
							const double BaselineEdges[] = {
								(BaselineB - BaselineA).Length(),
								(BaselineC - BaselineB).Length(),
								(BaselineA - BaselineC).Length()};
							const double CandidateEdges[] = {
								(CandidateB - CandidateA).Length(),
								(CandidateC - CandidateB).Length(),
								(CandidateA - CandidateC).Length()};
							for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
							{
								if (BaselineEdges[EdgeIndex] <= 1.e-8)
								{
									OutEvaluationError = TEXT("Topology baseline contains a degenerate edge.");
									return false;
								}
								const double EdgeRatio = CandidateEdges[EdgeIndex] / BaselineEdges[EdgeIndex];
								OutScore.MaximumEdgeScaleDeviation = FMath::Max(
									OutScore.MaximumEdgeScaleDeviation,
									FMath::Abs(FMath::Loge(EdgeRatio)));
								if (!FMath::IsFinite(EdgeRatio)
									|| EdgeRatio < MinimumEdgeRatio
									|| EdgeRatio > MaximumEdgeRatio)
								{
									OutEvaluationError = TEXT("Topology candidate exceeded the local edge-stretch gate.");
									return false;
								}
							}
						}

						double TierMinimumGap = TNumericLimits<double>::Max();
						int32 TierPenetratingVertices = 0;
						if (!MeasureVertexClearancePrepared(
							ShapeBody,
							ShapeBodySpatial,
							ShapeBodyNormals,
							CandidateTierGarment,
							TierMinimumGap,
							TierPenetratingVertices))
						{
							OutEvaluationError = TEXT("Could not measure a topology repair candidate.");
							return false;
						}
						OutScore.MinimumGap = FMath::Min(OutScore.MinimumGap, TierMinimumGap);
						if (!MeshesIntersectPrepared(ShapeBodySpatial, CandidateTierGarment))
						{
							continue;
						}

						++OutScore.IntersectingTierCount;
						FRawIntersectionContacts TierContacts;
						if (!CollectRawIntersectingGarmentContactsPrepared(
							ShapeBody,
							ShapeBodySpatial,
							CandidateTierGarment,
							TierContacts))
						{
							OutEvaluationError = FString::Printf(
								TEXT("Tier %.3f reported an ambiguous coplanar intersection without raw contact primitives."),
								Tier);
							return false;
						}
						OutScore.ContactTriangleTierPairCount
							+= TierContacts.GarmentTriangleIDs.Num();
						for (int32 TriangleID : TierContacts.GarmentTriangleIDs)
						{
							OutContacts.GarmentTriangleIDs.Add(TriangleID);
							OutContacts.GarmentTriangleNormalSums.FindOrAdd(TriangleID)
								+= TierContacts.GarmentTriangleNormalSums.FindRef(TriangleID);
						}
						OutContacts.PrimitiveCount += TierContacts.PrimitiveCount;
						OutContacts.NormalizedMeasure += TierContacts.NormalizedMeasure;
					}
					OutScore.ContactTriangleCount = OutContacts.GarmentTriangleIDs.Num();
					OutScore.PrimitiveCount = OutContacts.PrimitiveCount;
					OutScore.NormalizedMeasure = OutContacts.NormalizedMeasure;
					return true;
				};

				auto MeasureCandidateDeltaAcrossTiers = [
					&GarmentMesh,
					&ClearanceDeltas,
					&ShapeBody,
					&ShapeBodySpatial,
					&ShapeBodyNormals,
					MinimumTopologyTierIndex](int32 VertexID, const FVector3d& CandidateDelta) -> double
				{
					double MinimumGap = TNumericLimits<double>::Max();
					for (int32 TierIndex = MinimumTopologyTierIndex;
						TierIndex < CertifiedClearanceTierCount;
						++TierIndex)
					{
						const double Tier = FMath::Lerp(
							CertifiedClearanceTierMin,
							CertifiedClearanceTierMax,
							static_cast<double>(TierIndex)
								/ static_cast<double>(CertifiedClearanceTierCount - 1));
						const FVector3d Position = GarmentMesh.GetVertex(VertexID)
							+ ClearanceDeltas[VertexID] * Tier
							+ CandidateDelta;
						double DistanceSquared = TNumericLimits<double>::Max();
						const int32 BodyTriangleID = ShapeBodySpatial.FindNearestTriangle(Position, DistanceSquared);
						if (BodyTriangleID == IndexConstants::InvalidID)
						{
							return -TNumericLimits<double>::Max();
						}
						const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
							ShapeBody,
							BodyTriangleID,
							Position);
						const FVector3d Normal = InterpolateNormal(
							ShapeBody,
							ShapeBodyNormals,
							BodyTriangleID,
							Query.TriangleBaryCoords);
						MinimumGap = FMath::Min(
							MinimumGap,
							(Position - Query.ClosestTrianglePoint).Dot(Normal));
					}
					return MinimumGap;
				};

				auto CompareCardinalTopology = [](
					const FIntersectionTopologyScore& CandidateScore,
					const FIntersectionTopologyScore& ReferenceScore) -> int32
				{
					if (CandidateScore.IntersectingTierCount != ReferenceScore.IntersectingTierCount)
					{
						return CandidateScore.IntersectingTierCount < ReferenceScore.IntersectingTierCount ? -1 : 1;
					}
					if (CandidateScore.ContactTriangleTierPairCount
						!= ReferenceScore.ContactTriangleTierPairCount)
					{
						return CandidateScore.ContactTriangleTierPairCount
							< ReferenceScore.ContactTriangleTierPairCount ? -1 : 1;
					}
					if (CandidateScore.ContactTriangleCount != ReferenceScore.ContactTriangleCount)
					{
						return CandidateScore.ContactTriangleCount < ReferenceScore.ContactTriangleCount ? -1 : 1;
					}
					return 0;
				};

				auto IsEligibleTopologyProgress = [&CompareCardinalTopology](
					const FIntersectionTopologyScore& CandidateScore,
					const FIntersectionTopologyScore& CurrentScore) -> bool
				{
					const int32 CardinalComparison = CompareCardinalTopology(CandidateScore, CurrentScore);
					if (CardinalComparison != 0)
					{
						return CardinalComparison < 0;
					}
					// Permit a bounded descent step only when the raw contact measure drops
					// materially. The 4cm topology budget prevents the old unbounded drift.
					return CandidateScore.NormalizedMeasure
						<= CurrentScore.NormalizedMeasure * 0.9999 - 1.e-6;
				};

				auto IsBetterEligibleCandidate = [&CompareCardinalTopology](
					const FIntersectionTopologyScore& CandidateScore,
					const FIntersectionTopologyScore& BestScore) -> bool
				{
					const int32 CardinalComparison = CompareCardinalTopology(CandidateScore, BestScore);
					if (CardinalComparison != 0)
					{
						return CardinalComparison < 0;
					}
					// Treat sub-millimetre differences as the same displacement class so
					// the selector can prefer a smoother, higher-quality field instead of
					// taking dozens of minimum-size steps that accumulate local strain.
					constexpr double TopologyDisplacementQualityBandCm = 0.05;
					if (FMath::Abs(
						CandidateScore.MaximumTopologyDisplacement
							- BestScore.MaximumTopologyDisplacement)
						> TopologyDisplacementQualityBandCm)
					{
						return CandidateScore.MaximumTopologyDisplacement
							< BestScore.MaximumTopologyDisplacement;
					}
					if (!FMath::IsNearlyEqual(CandidateScore.NormalizedMeasure, BestScore.NormalizedMeasure, 1.e-6))
					{
						return CandidateScore.NormalizedMeasure < BestScore.NormalizedMeasure;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.MinimumTriangleNormalDot,
						BestScore.MinimumTriangleNormalDot,
						1.e-6))
					{
						return CandidateScore.MinimumTriangleNormalDot
							> BestScore.MinimumTriangleNormalDot;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.MaximumAreaScaleDeviation,
						BestScore.MaximumAreaScaleDeviation,
						1.e-6))
					{
						return CandidateScore.MaximumAreaScaleDeviation
							< BestScore.MaximumAreaScaleDeviation;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.MaximumEdgeScaleDeviation,
						BestScore.MaximumEdgeScaleDeviation,
						1.e-6))
					{
						return CandidateScore.MaximumEdgeScaleDeviation
							< BestScore.MaximumEdgeScaleDeviation;
					}
					if (CandidateScore.PrimitiveCount != BestScore.PrimitiveCount)
					{
						return CandidateScore.PrimitiveCount < BestScore.PrimitiveCount;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.MaximumTopologyDisplacement,
						BestScore.MaximumTopologyDisplacement,
						1.e-6))
					{
						return CandidateScore.MaximumTopologyDisplacement
							< BestScore.MaximumTopologyDisplacement;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.TopologySquaredEnergy,
						BestScore.TopologySquaredEnergy,
						1.e-6))
					{
						return CandidateScore.TopologySquaredEnergy
							< BestScore.TopologySquaredEnergy;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.MaximumRepairMagnitude,
						BestScore.MaximumRepairMagnitude,
						1.e-6))
					{
						return CandidateScore.MaximumRepairMagnitude
							< BestScore.MaximumRepairMagnitude;
					}
					if (!FMath::IsNearlyEqual(
						CandidateScore.RepairSquaredSum,
						BestScore.RepairSquaredSum,
						1.e-6))
					{
						return CandidateScore.RepairSquaredSum
							< BestScore.RepairSquaredSum;
					}
					return CandidateScore.MinimumGap > BestScore.MinimumGap + 1.e-6;
				};

				constexpr int32 MaximumIntersectionRepairPasses = 24;
				bool bCandidateIntersectionsCleared = false;
				for (int32 IntersectionPass = 0;
					IntersectionPass < MaximumIntersectionRepairPasses;
					++IntersectionPass)
				{
					FIntersectionTopologyScore CurrentScore;
					FRawIntersectionContacts CurrentContacts;
					FString EvaluationError;
					if (!EvaluateIntersectionCandidate(
						RequestedDeltas,
						CurrentScore,
						CurrentContacts,
						EvaluationError))
					{
						OutError = FString::Printf(TEXT("Topology certification for %s failed closed: %s"), *Context, *EvaluationError);
						return false;
					}
					if (CurrentScore.IntersectingTierCount == 0)
					{
						bCandidateIntersectionsCleared = true;
						break;
					}

					TArray<FRawIntersectionComponent> Components;
					if (!BuildRawIntersectionComponents(GarmentMesh, CurrentContacts, Components))
					{
						OutError = FString::Printf(
							TEXT("Topology repair for %s could not build coherent raw contact components."),
							*Context);
						return false;
					}

					TArray<FVector3d> BestCandidateDeltas;
					FIntersectionTopologyScore BestCandidateScore = CurrentScore;
					bool bFoundImprovement = false;
					int32 BestSelection = INDEX_NONE;
					int32 BestDirectionVariant = 0;
					double BestStep = 0.0;
					double BestSmoothingBlend = 0.0;
					int32 BestSaturatedVertexCount = 0;
					int32 GeometryRejectedCandidateCount = 0;
					int32 BudgetRejectedCandidateCount = 0;
					int32 ClearanceRejectedCandidateCount = 0;
					int32 RmsRejectedCandidateCount = 0;
					int32 ProgressRejectedCandidateCount = 0;
					FString LastGeometryRejection;
					bool bHasBestNonEligibleCandidate = false;
					FIntersectionTopologyScore BestNonEligibleCandidate;
					TArray<double, TInlineAllocator<16>> CandidateSteps = {
						0.0,
						0.00390625,
						0.0078125,
						0.015625,
						0.03125,
						0.0625,
						0.125,
						0.25,
						0.50,
						1.0,
						2.0,
						3.0};
					// Persistent triangle crossings sometimes need one coherent move that
					// is larger than the old 3cm incremental ceiling. Every proposal is
					// still clamped to MaximumMorphRepairCm and clearance-certified.
					if (MaximumTopologyExtraRepair > 3.0 + 1.e-9)
					{
						CandidateSteps.AddUnique(MaximumTopologyExtraRepair);
					}
					CandidateSteps.Sort();
					constexpr double CandidateSmoothingBlends[] = {0.65, 0.35, 0.0};
					FVector3d CurrentGarmentCenter = FVector3d::Zero();
					int32 CurrentGarmentVertexCount = 0;
					for (int32 VertexID : GarmentMesh.VertexIndicesItr())
					{
						CurrentGarmentCenter += GarmentMesh.GetVertex(VertexID)
							+ ClearanceDeltas[VertexID] * TopologyReferenceTier
							+ RequestedDeltas[VertexID];
						++CurrentGarmentVertexCount;
					}
					if (CurrentGarmentVertexCount > 0)
					{
						CurrentGarmentCenter /= static_cast<double>(CurrentGarmentVertexCount);
					}
					for (int32 SelectionIndex = INDEX_NONE;
						SelectionIndex <= Components.Num() + 1;
						++SelectionIndex)
					{
						const bool bIndividualComponent = SelectionIndex >= 0
							&& SelectionIndex < Components.Num();
						const bool bAllComponents = SelectionIndex == INDEX_NONE;
						const bool bLocalizedAllComponents = SelectionIndex == Components.Num();
						const bool bRadialWholeGarment = SelectionIndex == Components.Num() + 1;
						const double ScopeMaximumTopologyRepair = bRadialWholeGarment
							? FMath::Min(MaximumTopologyExtraRepair, 1.0)
							: (bAllComponents
								? FMath::Min(MaximumTopologyExtraRepair, 2.0)
								: MaximumTopologyExtraRepair);
						if (bLocalizedAllComponents && CurrentScore.ContactTriangleCount > 8)
						{
							continue;
						}
						const int32 DirectionVariantCount = bIndividualComponent ? 9 : 1;
						for (int32 DirectionVariant = 0;
							DirectionVariant < DirectionVariantCount;
							++DirectionVariant)
						{
							for (double CandidateStep : CandidateSteps)
							{
								if (CandidateStep > ScopeMaximumTopologyRepair + 1.e-9)
								{
									continue;
								}
								if (DirectionVariant > 0 && CandidateStep <= 1.e-9)
								{
									continue;
								}
								TMap<int32, FVector3d> ProposedDirections;
								TMap<int32, double> ProposedWeights;
							if (bRadialWholeGarment)
							{
								for (int32 VertexID : GarmentMesh.VertexIndicesItr())
								{
									FVector3d RadialDirection = GarmentMesh.GetVertex(VertexID)
										+ ClearanceDeltas[VertexID] * TopologyReferenceTier
										+ RequestedDeltas[VertexID]
										- CurrentGarmentCenter;
									if (RadialDirection.Normalize())
									{
										ProposedDirections.Add(VertexID, RadialDirection);
										ProposedWeights.Add(VertexID, 1.0);
									}
								}
							}
							else for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
							{
								if (bIndividualComponent && ComponentIndex != SelectionIndex)
								{
									continue;
								}
								const FRawIntersectionComponent& Component = Components[ComponentIndex];
								FVector3d ComponentDirection = Component.Direction;
								if (bIndividualComponent && DirectionVariant > 0)
								{
									FVector3d AxialTangent = FVector3d(0.0, 0.0, 1.0)
										- Component.Direction * Component.Direction.Z;
									if (!AxialTangent.Normalize())
									{
										AxialTangent = FVector3d(1.0, 0.0, 0.0)
											- Component.Direction * Component.Direction.X;
										AxialTangent.Normalize();
									}
									FVector3d CircumferentialTangent =
										Component.Direction.Cross(AxialTangent);
									CircumferentialTangent.Normalize();
									const int32 ConeIndex = DirectionVariant - 1;
									const double TangentScale = ConeIndex < 4 ? 0.35 : 0.75;
									const int32 TangentIndex = ConeIndex % 4;
									const FVector3d TangentDirection = TangentIndex < 2
										? AxialTangent
										: CircumferentialTangent;
									const double TangentSign = (TangentIndex % 2) == 0 ? 1.0 : -1.0;
									ComponentDirection = Component.Direction
										+ TangentDirection * TangentScale * TangentSign;
									ComponentDirection.Normalize();
								}
								for (const TPair<int32, int32>& SupportVertex : Component.SupportDepthByVertex)
								{
									if ((bLocalizedAllComponents && SupportVertex.Value > 3)
										|| (bIndividualComponent && SupportVertex.Value > 6)
										|| (bAllComponents && SupportVertex.Value > 8))
									{
										continue;
									}
									double Falloff = FMath::Max(
										FMath::Exp(-static_cast<double>(SupportVertex.Value) * 0.25),
										0.04);
									if (bLocalizedAllComponents)
									{
										Falloff = FMath::Max(
											1.0 - static_cast<double>(SupportVertex.Value) * 0.25,
											0.25);
									}
									else if (bIndividualComponent)
									{
										const double SupportAlpha = FMath::Clamp(
											1.0 - static_cast<double>(SupportVertex.Value) / 7.0,
											0.0,
											1.0);
										Falloff = SupportAlpha * SupportAlpha * (3.0 - 2.0 * SupportAlpha);
									}
									ProposedDirections.FindOrAdd(SupportVertex.Key) += ComponentDirection * Falloff;
									double& ProposedWeight = ProposedWeights.FindOrAdd(SupportVertex.Key);
									ProposedWeight = FMath::Max(ProposedWeight, Falloff);
								}
							}

							for (double CandidateSmoothingBlend : CandidateSmoothingBlends)
							{
								if (bRadialWholeGarment && CandidateSmoothingBlend > 0.0)
								{
									continue;
								}
								TArray<FVector3d> CandidateDeltas = RequestedDeltas;
								int32 SaturatedVertexCount = 0;
								bool bCandidateFieldValid = true;
								if (CandidateSmoothingBlend > 0.0)
								{
									const TArray<FVector3d> BeforeSmoothing = CandidateDeltas;
									for (const TPair<int32, FVector3d>& ProposedDirection : ProposedDirections)
									{
										const int32 VertexID = ProposedDirection.Key;
										FVector3d NeighborRepairAverage = FVector3d::Zero();
										int32 NeighborCount = 0;
										for (int32 NeighborVertexID : GarmentMesh.VtxVerticesItr(VertexID))
										{
											NeighborRepairAverage += BeforeSmoothing[NeighborVertexID]
												- InitialRequestedDeltas[NeighborVertexID];
											++NeighborCount;
										}
										if (NeighborCount == 0)
										{
											continue;
										}
										NeighborRepairAverage /= static_cast<double>(NeighborCount);
										const FVector3d CurrentRepair = BeforeSmoothing[VertexID]
											- InitialRequestedDeltas[VertexID];
										FVector3d SmoothedRepair = CurrentRepair;
										double LowBlend = 0.0;
										double HighBlend = CandidateSmoothingBlend;
										for (int32 Refinement = 0; Refinement < 12; ++Refinement)
										{
											const double Blend = (LowBlend + HighBlend) * 0.5;
											const FVector3d ProposedRepair = CurrentRepair * (1.0 - Blend)
												+ NeighborRepairAverage * Blend;
											const FVector3d ProposedDelta = InitialRequestedDeltas[VertexID] + ProposedRepair;
											if (ProposedRepair.Length() <= MaximumRepair + 1.e-6
												&& (ProposedDelta - TopologyBaselineDeltas[VertexID]).Length()
													<= ScopeMaximumTopologyRepair + 1.e-6
												&& MeasureCandidateDeltaAcrossTiers(VertexID, ProposedDelta)
													>= CoherentRepairTargetClearance - ClearanceToleranceCm)
											{
												LowBlend = Blend;
												SmoothedRepair = ProposedRepair;
											}
											else
											{
												HighBlend = Blend;
											}
										}
										CandidateDeltas[VertexID] = InitialRequestedDeltas[VertexID] + SmoothedRepair;
									}
								}

								for (const TPair<int32, FVector3d>& ProposedDirection : ProposedDirections)
								{
									FVector3d Direction = ProposedDirection.Value;
									if (!Direction.Normalize())
									{
										continue;
									}
									const int32 VertexID = ProposedDirection.Key;
									const FVector3d CurrentRepair = CandidateDeltas[VertexID]
										- InitialRequestedDeltas[VertexID];
									const double DesiredDistance =
										CandidateStep * ProposedWeights.FindRef(VertexID);
									auto IsSafeDistance = [
										&InitialRequestedDeltas,
										&TopologyBaselineDeltas,
										&MeasureCandidateDeltaAcrossTiers,
										CurrentRepair,
										Direction,
										VertexID,
										MaximumRepair,
										ScopeMaximumTopologyRepair,
										CoherentRepairTargetClearance,
										ClearanceToleranceCm](double Distance, FVector3d& OutDelta) -> bool
									{
										const FVector3d Repair = CurrentRepair + Direction * Distance;
										OutDelta = InitialRequestedDeltas[VertexID] + Repair;
										return Repair.Length() <= MaximumRepair + 1.e-6
											&& (OutDelta - TopologyBaselineDeltas[VertexID]).Length()
												<= ScopeMaximumTopologyRepair + 1.e-6
											&& MeasureCandidateDeltaAcrossTiers(VertexID, OutDelta)
												>= CoherentRepairTargetClearance - ClearanceToleranceCm;
									};
									FVector3d AcceptedDelta = CandidateDeltas[VertexID];
									FVector3d DesiredDelta = AcceptedDelta;
									if (DesiredDistance > 1.e-9
										&& !IsSafeDistance(DesiredDistance, DesiredDelta))
									{
										double LowDistance = 0.0;
										double HighDistance = DesiredDistance;
										for (int32 Refinement = 0; Refinement < 12; ++Refinement)
										{
											const double MidDistance = (LowDistance + HighDistance) * 0.5;
											FVector3d MidDelta;
											if (IsSafeDistance(MidDistance, MidDelta))
											{
												LowDistance = MidDistance;
												AcceptedDelta = MidDelta;
											}
											else
											{
												HighDistance = MidDistance;
											}
										}
										++SaturatedVertexCount;
									}
									else if (DesiredDistance > 1.e-9)
									{
										AcceptedDelta = DesiredDelta;
									}
									CandidateDeltas[VertexID] = AcceptedDelta;
								}
								ReconcileNonManifoldVectorField(GarmentMesh, CandidateDeltas, ThicknessShell);
								for (int32 VertexID : GarmentMesh.VertexIndicesItr())
								{
									const FVector3d TotalRepair = CandidateDeltas[VertexID]
										- InitialRequestedDeltas[VertexID];
									const FVector3d TopologyRepair = CandidateDeltas[VertexID]
										- TopologyBaselineDeltas[VertexID];
									if (!FMath::IsFinite(CandidateDeltas[VertexID].X)
										|| !FMath::IsFinite(CandidateDeltas[VertexID].Y)
										|| !FMath::IsFinite(CandidateDeltas[VertexID].Z)
										|| TotalRepair.Length() > MaximumRepair + 1.e-6
										|| TopologyRepair.Length() > ScopeMaximumTopologyRepair + 1.e-6)
									{
										bCandidateFieldValid = false;
										break;
									}
								}
								if (!bCandidateFieldValid)
								{
									++BudgetRejectedCandidateCount;
									continue;
								}

								FIntersectionTopologyScore CandidateScore;
								FRawIntersectionContacts CandidateContacts;
								FString CandidateEvaluationError;
								if (!EvaluateIntersectionCandidate(
									CandidateDeltas,
									CandidateScore,
									CandidateContacts,
									CandidateEvaluationError))
								{
									++GeometryRejectedCandidateCount;
									LastGeometryRejection = CandidateEvaluationError;
									continue;
								}
								if (CandidateScore.MinimumGap
									< CoherentRepairTargetClearance - ClearanceToleranceCm)
								{
									++ClearanceRejectedCandidateCount;
									continue;
								}
								if (FMath::Sqrt(
										CandidateScore.TopologySquaredEnergy
											/ FMath::Max(1, GarmentMesh.VertexCount()))
									> MaximumTopologyRmsRepair + 1.e-6)
								{
									++RmsRejectedCandidateCount;
									continue;
								}
								if (!IsEligibleTopologyProgress(CandidateScore, CurrentScore))
								{
									++ProgressRejectedCandidateCount;
									if (!bHasBestNonEligibleCandidate
										|| CompareCardinalTopology(
											CandidateScore,
											BestNonEligibleCandidate) < 0
										|| (CompareCardinalTopology(
											CandidateScore,
											BestNonEligibleCandidate) == 0
											&& CandidateScore.NormalizedMeasure
												< BestNonEligibleCandidate.NormalizedMeasure))
									{
										BestNonEligibleCandidate = CandidateScore;
										bHasBestNonEligibleCandidate = true;
									}
									continue;
								}
								if (bFoundImprovement
									&& !IsBetterEligibleCandidate(CandidateScore, BestCandidateScore))
								{
									++ProgressRejectedCandidateCount;
									continue;
								}

								BestCandidateDeltas = MoveTemp(CandidateDeltas);
								BestCandidateScore = CandidateScore;
								BestSelection = SelectionIndex;
								BestDirectionVariant = DirectionVariant;
								BestStep = CandidateStep;
								BestSmoothingBlend = CandidateSmoothingBlend;
								BestSaturatedVertexCount = SaturatedVertexCount;
								bFoundImprovement = true;
							}
						}
					}
					}

					if (!bFoundImprovement)
					{
						OutError = FString::Printf(
							TEXT("Topology repair for %s made no strict progress at pass %d (tiers=%d tier-triangles=%d triangles=%d primitives=%d measure=%.6f maxTopology=%.4fcm maxRepair=%.4fcm gap=%.4fcm components=%d rejected budget/geometry/clearance/rms/progress=%d/%d/%d/%d/%d bestRejected=%d/%d/%d/%d/%.6f lastGeometry='%s')."),
							*Context,
							IntersectionPass + 1,
							CurrentScore.IntersectingTierCount,
							CurrentScore.ContactTriangleTierPairCount,
							CurrentScore.ContactTriangleCount,
							CurrentScore.PrimitiveCount,
							CurrentScore.NormalizedMeasure,
							CurrentScore.MaximumTopologyDisplacement,
							CurrentScore.MaximumRepairMagnitude,
							CurrentScore.MinimumGap,
							Components.Num(),
							BudgetRejectedCandidateCount,
							GeometryRejectedCandidateCount,
							ClearanceRejectedCandidateCount,
							RmsRejectedCandidateCount,
							ProgressRejectedCandidateCount,
							bHasBestNonEligibleCandidate ? BestNonEligibleCandidate.IntersectingTierCount : -1,
							bHasBestNonEligibleCandidate ? BestNonEligibleCandidate.ContactTriangleTierPairCount : -1,
							bHasBestNonEligibleCandidate ? BestNonEligibleCandidate.ContactTriangleCount : -1,
							bHasBestNonEligibleCandidate ? BestNonEligibleCandidate.PrimitiveCount : -1,
							bHasBestNonEligibleCandidate ? BestNonEligibleCandidate.NormalizedMeasure : -1.0,
							*LastGeometryRejection);
						return false;
					}

					RequestedDeltas = MoveTemp(BestCandidateDeltas);
					bAppliedCorrection = true;
					const FString SelectionLabel = BestSelection == INDEX_NONE
						? TEXT("all")
						: (BestSelection < Components.Num()
							? FString::Printf(TEXT("component-%d"), BestSelection)
							: (BestSelection == Components.Num()
								? TEXT("localized-all")
								: TEXT("radial")));
					UE_LOG(
						LogEFClothingFitCompiler,
						Display,
						TEXT("V24 topology transaction %d for %s committed selection=%s variant=%d step=%.4fcm smoothing=%.2f saturated=%d score %d/%d/%d/%d/%.6f/%.4f -> %d/%d/%d/%d/%.6f/%.4f quality normal=%.4f areaLog=%.4f edgeLog=%.4f."),
						IntersectionPass + 1,
						*Context,
						*SelectionLabel,
						BestDirectionVariant,
						BestStep,
						BestSmoothingBlend,
						BestSaturatedVertexCount,
						CurrentScore.IntersectingTierCount,
						CurrentScore.ContactTriangleTierPairCount,
						CurrentScore.ContactTriangleCount,
						CurrentScore.PrimitiveCount,
						CurrentScore.NormalizedMeasure,
						CurrentScore.MaximumTopologyDisplacement,
						BestCandidateScore.IntersectingTierCount,
						BestCandidateScore.ContactTriangleTierPairCount,
						BestCandidateScore.ContactTriangleCount,
						BestCandidateScore.PrimitiveCount,
						BestCandidateScore.NormalizedMeasure,
						BestCandidateScore.MaximumTopologyDisplacement,
						BestCandidateScore.MinimumTriangleNormalDot,
						BestCandidateScore.MaximumAreaScaleDeviation,
						BestCandidateScore.MaximumEdgeScaleDeviation);
					if (BestCandidateScore.IntersectingTierCount == 0)
					{
						bCandidateIntersectionsCleared = true;
						break;
					}
				}
				if (!bCandidateIntersectionsCleared)
				{
					OutError = FString::Printf(TEXT("Intersection repair ended without a certified result for %s."), *Context);
					return false;
				}
				if (!bAppliedCorrection)
				{
					OutError = FString::Printf(TEXT("Stored morph auto-sculpt produced no correction for %s."), *Context);
					return false;
				}
				ReconcileNonManifoldVectorField(GarmentMesh, RequestedDeltas, ThicknessShell);
				bOutRepaired = true;
				UE_LOG(
					LogEFClothingFitCompiler,
					Display,
					TEXT("V24 stored morph cook pass %d repaired %s at tier %.3f (gap %.4fcm, intersects=%s)."),
					CookPass + 1,
					*Context,
					FailingTier,
					FailingTierGap,
					bFailingTierIntersects ? TEXT("true") : TEXT("false"));
			}

			OutError = FString::Printf(TEXT("Stored morph certification ended without a result for %s."), *Context);
			return false;
		};

		int32 CandidateOrdinal = 0;
		for (const FMorphCandidate& Candidate : Candidates)
		{
			++CandidateOrdinal;
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("V24 morph certification %d/%d: %s"),
				CandidateOrdinal,
				Candidates.Num(),
				*Candidate.Name.ToString());
			const TVertexAttributesConstRef<FVector3f> BodyMorphDeltas = BodyAttributes.GetVertexMorphPositionDelta(Candidate.Name);
			FEFClothingMorphBinding Binding;
			Binding.BodyMorph = Candidate.Name;
			Binding.MinimumCertifiedValue = 0.0f;
			Binding.MaximumCertifiedValue = 1.0f;

			// Each key is stored as an absolute garment shape relative to the fitted
			// rest garment. Solve it incrementally from the nearest lower certified
			// key so anatomical folds and topology repairs are preserved as the body
			// changes, rather than being reconstructed independently from rest.
			TArray<FCompiledMorphKey> Keys;
			FCompiledMorphKey& RestKey = Keys.AddDefaulted_GetRef();
			RestKey.BodyValue = 0.0;
			RestKey.MorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
			bool bRepaired = false;
			double MorphMinimumGap = TNumericLimits<double>::Max();
			const FString MorphKey = FMD5::HashAnsiString(*Candidate.Name.ToString()).Left(12);
			const int32 CandidateBaseSampleCount = Candidate.MaxDelta > 4.0
				? 16
				: SampleCount;
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("V24 morph %s uses %d base sample(s) for maximum transferred displacement %.4fcm."),
				*Candidate.Name.ToString(),
				CandidateBaseSampleCount,
				Candidate.MaxDelta);

			// Existing garment morph names are deliberately not trusted, but many
			// body morphs do not make the already-fitted garment unsafe in this
			// particular surface region. Certify that case geometrically instead of
			// relying on a name list or displacement threshold. A fixed zero shape is
			// sampled eight times more densely than the published identity keys and
			// against every runtime offset tier. Any failed gap or triangle test falls
			// through to the full auto-sculpt below.
			const int32 NoOpProbeCount = CandidateBaseSampleCount * 8;
			TArray<FVector3d> ZeroMorphDeltas;
			ZeroMorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
			double NoOpMinimumGap = TNumericLimits<double>::Max();
			double NoOpMinimumClearanceMultiplier = CertifiedClearanceTierMin;
			bool bNoOpCertified = true;
			for (int32 ProbeIndex = 1; ProbeIndex <= NoOpProbeCount; ++ProbeIndex)
			{
				const double BodyValue = static_cast<double>(ProbeIndex)
					/ static_cast<double>(NoOpProbeCount);
				FDynamicMesh3 ProbeBody(BodyMesh);
				for (int32 VertexID : ProbeBody.VertexIndicesItr())
				{
					const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
					ProbeBody.SetVertex(
						VertexID,
						ProbeBody.GetVertex(VertexID)
							+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
				}
				double ProbeMinimumGap = TNumericLimits<double>::Max();
				double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
				bool bProbeIntersects = false;
				if (!MeasureRuntimeShapeAcrossOffsetTiers(
					ProbeBody,
					ZeroMorphDeltas,
					ProbeMinimumGap,
					bProbeIntersects,
					ProbeMinimumClearanceMultiplier)
					|| ProbeMinimumGap < DesiredClearance - ClearanceToleranceCm
					|| bProbeIntersects)
				{
					bNoOpCertified = false;
					break;
				}
				NoOpMinimumGap = FMath::Min(NoOpMinimumGap, ProbeMinimumGap);
				NoOpMinimumClearanceMultiplier = FMath::Max(
					NoOpMinimumClearanceMultiplier,
					ProbeMinimumClearanceMultiplier);
			}
			if (bNoOpCertified)
			{
				for (int32 SampleIndex = 1; SampleIndex <= CandidateBaseSampleCount; ++SampleIndex)
				{
					FEFClothingMorphSample& IdentitySample = Binding.Samples.AddDefaulted_GetRef();
					IdentitySample.BodyValue = static_cast<float>(SampleIndex)
						/ static_cast<float>(CandidateBaseSampleCount);
					IdentitySample.bIdentity = true;
					IdentitySample.MinimumClearanceMultiplier = static_cast<float>(
						NoOpMinimumClearanceMultiplier);
				}
				if (Candidate.bTransferred)
				{
					TransferredNames.Add(Candidate.Name);
				}
				++OutValidatedMorphCount;
				OutMinimumSampledMorphGap = FMath::Min(OutMinimumSampledMorphGap, NoOpMinimumGap);
				OutMinimumCertifiedOffsetGap = FMath::Min(OutMinimumCertifiedOffsetGap, NoOpMinimumGap);
				UE_LOG(
					LogEFClothingFitCompiler,
					Display,
					TEXT("V24 morph certified no-op %d/%d: %s | probes=%d | minGap=%.4fcm"),
					CandidateOrdinal,
					Candidates.Num(),
					*Candidate.Name.ToString(),
					NoOpProbeCount,
					NoOpMinimumGap);
				OutBindings.Add(MoveTemp(Binding));
				continue;
			}

			auto SolveKey = [&](double Alpha, FCompiledMorphKey& OutKey) -> bool
			{
				const FCompiledMorphKey* AnchorKey = &Keys[0];
				for (const FCompiledMorphKey& ExistingKey : Keys)
				{
					if (ExistingKey.BodyValue < Alpha - 1.e-9
						&& ExistingKey.BodyValue > AnchorKey->BodyValue)
					{
						AnchorKey = &ExistingKey;
					}
				}

				FDynamicMesh3 AnchorBody(BodyMesh);
				for (int32 VertexID : AnchorBody.VertexIndicesItr())
				{
					const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
					AnchorBody.SetVertex(
						VertexID,
						AnchorBody.GetVertex(VertexID)
							+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * AnchorKey->BodyValue);
				}
				FMeshNormals AnchorBodyNormals(&AnchorBody);
				AnchorBodyNormals.ComputeVertexNormals();

				FDynamicMesh3 SampleBody(BodyMesh);
				for (int32 VertexID : SampleBody.VertexIndicesItr())
				{
					const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
					SampleBody.SetVertex(
						VertexID,
						SampleBody.GetVertex(VertexID) + FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * Alpha);
				}
				FMeshNormals SampleBodyNormals(&SampleBody);
				SampleBodyNormals.ComputeVertexNormals();

				FDynamicMesh3 SampleGarment(GarmentMesh);
				TArray<FVector3d> SampleClearanceDirections;
				SampleClearanceDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
				for (int32 VertexID : SampleGarment.VertexIndicesItr())
				{
					const FSurfaceCorrespondence& Surface = Correspondence[VertexID];
					if (Surface.BodyTriangle == INDEX_NONE
						|| !SampleBody.IsTriangle(Surface.BodyTriangle)
						|| !AnchorBody.IsTriangle(Surface.BodyTriangle))
					{
						OutError = FString::Printf(TEXT("Morph %s has an invalid surface correspondence."), *Candidate.Name.ToString());
						return false;
					}
					const FIndex3i Triangle = SampleBody.GetTriangle(Surface.BodyTriangle);
					const FVector3d SurfacePoint =
						SampleBody.GetVertex(Triangle.A) * Surface.Barycentric.X
						+ SampleBody.GetVertex(Triangle.B) * Surface.Barycentric.Y
						+ SampleBody.GetVertex(Triangle.C) * Surface.Barycentric.Z;
					const FVector3d SurfaceNormal = InterpolateNormal(
						SampleBody,
						SampleBodyNormals,
						Surface.BodyTriangle,
						Surface.Barycentric);
					SampleClearanceDirections[VertexID] = SurfaceNormal;
					const FVector3d BaseFittedPosition = GarmentMesh.GetVertex(VertexID) + ClearanceDeltas[VertexID];
					const FIndex3i AnchorTriangle = AnchorBody.GetTriangle(Surface.BodyTriangle);
					const FVector3d AnchorSurfacePoint =
						AnchorBody.GetVertex(AnchorTriangle.A) * Surface.Barycentric.X
						+ AnchorBody.GetVertex(AnchorTriangle.B) * Surface.Barycentric.Y
						+ AnchorBody.GetVertex(AnchorTriangle.C) * Surface.Barycentric.Z;
					FVector3d AnchorSurfaceNormal = InterpolateNormal(
						AnchorBody,
						AnchorBodyNormals,
						Surface.BodyTriangle,
						Surface.Barycentric);
					FVector3d DeformedSurfaceNormal = SurfaceNormal;
					if (!AnchorSurfaceNormal.Normalize())
					{
						AnchorSurfaceNormal = DeformedSurfaceNormal;
					}
					if (!DeformedSurfaceNormal.Normalize())
					{
						DeformedSurfaceNormal = AnchorSurfaceNormal;
					}
					const FVector3d AnchorGarmentPosition = BaseFittedPosition + AnchorKey->MorphDeltas[VertexID];
					const FVector3d AnchorSurfaceOffset = AnchorGarmentPosition - AnchorSurfacePoint;
					const FVector3d TransportedSurfaceOffset = FQuat4d::FindBetweenNormals(
						AnchorSurfaceNormal,
						DeformedSurfaceNormal).RotateVector(AnchorSurfaceOffset);
					SampleGarment.SetVertex(
						VertexID,
						SurfacePoint + TransportedSurfaceOffset);
				}

				double MinimumGap = 0.0;
				int32 PenetratingVertices = 0;
				if (!MeasureVertexClearance(SampleBody, SampleGarment, MinimumGap, PenetratingVertices))
				{
					OutError = FString::Printf(TEXT("Could not measure morph %s at %.3f."), *Candidate.Name.ToString(), Alpha);
					return false;
				}

				if (MinimumGap < MorphSolveClearance - ClearanceToleranceCm)
				{
					TArray<FVector3d> SampleCorrections;
					double AnchoredMinimumGap = 0.0;
					bool bReachedAnchoredClearance = BuildAnchoredClearanceCorrections(
						SampleBody,
						SampleGarment,
						AnchoredRadialDirections,
						AnchoredClearanceDirections,
						Correspondence,
						MorphSolveClearance,
						StandardShapeRepair,
						SampleCorrections,
						AnchoredMinimumGap);
					if (!bReachedAnchoredClearance)
					{
						bReachedAnchoredClearance = BuildAnchoredClearanceCorrections(
							SampleBody,
							SampleGarment,
							AnchoredRadialDirections,
							AnchoredClearanceDirections,
							Correspondence,
							MorphFallbackClearance,
							StandardShapeRepair,
							SampleCorrections,
							AnchoredMinimumGap);
					}
					if (!bReachedAnchoredClearance)
					{
						double DirectionalMinimumGap = 0.0;
						bool bReachedDirectionalClearance = BuildDirectionalClearanceCorrections(
							SampleBody,
							SampleGarment,
							MorphFallbackClearance,
							StandardShapeRepair,
							SampleCorrections,
							DirectionalMinimumGap);
						if (!bReachedDirectionalClearance)
						{
							bReachedDirectionalClearance = BuildDirectionalClearanceCorrections(
								SampleBody,
								SampleGarment,
								DesiredClearance,
								StandardShapeRepair,
								SampleCorrections,
								DirectionalMinimumGap);
						}
						if (!bReachedDirectionalClearance)
						{
							OutError = FString::Printf(
								TEXT("Morph %s cannot maintain %.4fcm clearance at %.3f (anchored=%.4fcm directional=%.4fcm)."),
								*Candidate.Name.ToString(), DesiredClearance, Alpha, AnchoredMinimumGap, DirectionalMinimumGap);
							return false;
						}
					}
					if (SampleCorrections.Num() < GarmentMesh.MaxVertexID())
					{
						OutError = FString::Printf(TEXT("Morph %s produced an incomplete repair field at %.3f."), *Candidate.Name.ToString(), Alpha);
						return false;
					}
					for (int32 VertexID : SampleGarment.VertexIndicesItr())
					{
						if (!SampleCorrections[VertexID].IsNearlyZero(1.e-6))
						{
							bRepaired = true;
						}
						SampleGarment.SetVertex(VertexID, SampleGarment.GetVertex(VertexID) + SampleCorrections[VertexID]);
					}
				}

				if (!MeasureVertexClearance(SampleBody, SampleGarment, MinimumGap, PenetratingVertices)
					|| MinimumGap < DesiredClearance - ClearanceToleranceCm)
				{
					OutError = FString::Printf(
						TEXT("Morph %s failed final sample clearance at %.3f: %.4fcm < %.4fcm."),
						*Candidate.Name.ToString(), Alpha, MinimumGap, DesiredClearance);
					return false;
				}

				TArray<FVector3d> AbsoluteMorphDelta;
				AbsoluteMorphDelta.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
				for (int32 VertexID : SampleGarment.VertexIndicesItr())
				{
					const FVector3d BaseFittedPosition = GarmentMesh.GetVertex(VertexID) + ClearanceDeltas[VertexID];
					AbsoluteMorphDelta[VertexID] = SampleGarment.GetVertex(VertexID) - BaseFittedPosition;
				}
				OutKey.BodyValue = Alpha;
				OutKey.MorphDeltas = MoveTemp(AbsoluteMorphDelta);
				const FSkeletalMeshLODInfo* DerivedLODInfo = Derived->GetLODInfo(0);
				const double MorphThresholdPositionCm = FMath::Max(
					0.0,
					DerivedLODInfo
						? static_cast<double>(DerivedLODInfo->BuildSettings.MorphThresholdPosition)
						: 0.0);
				bool bContainsRenderableDelta = false;
				for (int32 VertexID : GarmentMesh.VertexIndicesItr())
				{
					bContainsRenderableDelta |= OutKey.MorphDeltas[VertexID].SquaredLength()
						> FMath::Square(MorphThresholdPositionCm);
				}
				if (!bContainsRenderableDelta)
				{
					OutKey.bIdentity = true;
					for (FVector3d& Delta : OutKey.MorphDeltas)
					{
						Delta = FVector3d::Zero();
					}
				}
				else
				{
					const int64 QuantizedBodyValue = FMath::RoundToInt64(Alpha * 1000000000.0);
					const FString MorphNameString = FString::Printf(
						TEXT("EFV2_%s_V%010lld"),
						*MorphKey,
						QuantizedBodyValue);
					const FName SampleMorphName(*MorphNameString);
					if (GeneratedSampleMorphNames.Contains(SampleMorphName))
					{
						OutError = FString::Printf(
							TEXT("Generated morph name collision for %s at value %.6f (%s)."),
							*Candidate.Name.ToString(),
							Alpha,
							*MorphNameString);
						return false;
					}
					GeneratedSampleMorphNames.Add(SampleMorphName);
					const FString StoredMorphContext = FString::Printf(
						TEXT("%s at %.6f"),
						*Candidate.Name.ToString(),
						Alpha);
					if (!WriteAndCertifyStoredShapeMorph(
						SampleBody,
						SampleClearanceDirections,
						SampleMorphName,
						StoredMorphContext,
						OutKey.MorphDeltas,
						bRepaired,
						OutKey.MinimumClearanceMultiplier))
					{
						return false;
					}
					OutKey.GarmentMorph = SampleMorphName;
				}

				double StoredMinimumGap = 0.0;
				double StoredMinimumClearanceMultiplier = CertifiedClearanceTierMin;
				bool bStoredShapeIntersects = false;
				if (!MeasureRuntimeShapeAcrossOffsetTiers(
					SampleBody,
					OutKey.MorphDeltas,
					StoredMinimumGap,
					bStoredShapeIntersects,
					StoredMinimumClearanceMultiplier))
				{
					OutError = FString::Printf(
						TEXT("Could not certify stored morph %s at %.3f across the runtime offset tiers."),
						*Candidate.Name.ToString(), Alpha);
					return false;
				}
				if (StoredMinimumGap < DesiredClearance - ClearanceToleranceCm || bStoredShapeIntersects)
				{
					OutError = FString::Printf(
						TEXT("Stored morph %s failed post-threshold offset certification at %.3f: gap %.4fcm, triangles=%s."),
						*Candidate.Name.ToString(),
						Alpha,
						StoredMinimumGap,
						bStoredShapeIntersects ? TEXT("INTERSECT") : TEXT("CLEAR"));
					return false;
				}
				OutKey.MinimumClearanceMultiplier = FMath::Max(
					OutKey.MinimumClearanceMultiplier,
					StoredMinimumClearanceMultiplier);
				MorphMinimumGap = FMath::Min(MorphMinimumGap, StoredMinimumGap);
				OutMinimumCertifiedOffsetGap = FMath::Min(OutMinimumCertifiedOffsetGap, StoredMinimumGap);
				return true;
			};

			for (int32 SampleIndex = 1; SampleIndex <= CandidateBaseSampleCount; ++SampleIndex)
			{
				FCompiledMorphKey Key;
				const double Alpha = static_cast<double>(SampleIndex) / static_cast<double>(CandidateBaseSampleCount);
				if (!SolveKey(Alpha, Key))
				{
					if (Keys.Num() <= 1)
					{
						return TransferredNames;
					}
					const FString PartialFailureReason = OutError;
					if (!Key.GarmentMorph.IsNone())
					{
						RemoveGeneratedMorph(Derived, Key.GarmentMorph);
						GeneratedSampleMorphNames.Remove(Key.GarmentMorph);
					}
					Binding.MaximumCertifiedValue = static_cast<float>(Keys.Last().BodyValue);
					OutError.Reset();
					UE_LOG(
						LogEFClothingFitCompiler,
						Warning,
						TEXT("V25 morph %s published with partial fail-closed range [0, %.6f]; value %.6f and above remain suppressed. Cause: %s"),
						*Candidate.Name.ToString(),
						Binding.MaximumCertifiedValue,
						Alpha,
						*PartialFailureReason);
					break;
				}
				Keys.Add(MoveTemp(Key));
			}
			const int32 CertifiedBaseSampleCount = Keys.Num() - 1;

			auto SortKeys = [&Keys]()
			{
				Keys.Sort([](const FCompiledMorphKey& A, const FCompiledMorphKey& B)
				{
					return A.BodyValue < B.BodyValue;
				});
			};
			SortKeys();

			// The base keys have already been cooked and certified. If neither linear
			// nor stepped playback can certify an interval, go directly to the
			// segment envelope: additional point samples are discarded by the
			// envelope builder and only repeat the same expensive topology work.
			const int32 MaximumAdaptiveSamplesPerMorph = CertifiedBaseSampleCount;
			auto MeasureKeyPair = [&](const FCompiledMorphKey& LowKey,
				const FCompiledMorphKey& HighKey,
				double NormalizedValue,
				bool bStep,
				double& OutGap,
				bool& bOutIntersects,
				double& OutMinimumClearanceMultiplier) -> bool
			{
				const double BodyValue = FMath::Lerp(LowKey.BodyValue, HighKey.BodyValue, NormalizedValue);
				FDynamicMesh3 ProbeBody(BodyMesh);
				for (int32 VertexID : ProbeBody.VertexIndicesItr())
				{
					const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
					ProbeBody.SetVertex(
						VertexID,
						ProbeBody.GetVertex(VertexID) + FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
				}
				TArray<FVector3d> ProbeMorphDeltas;
				ProbeMorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
				for (int32 VertexID : GarmentMesh.VertexIndicesItr())
				{
					ProbeMorphDeltas[VertexID] = bStep
						? (NormalizedValue <= 0.5 ? LowKey.MorphDeltas[VertexID] : HighKey.MorphDeltas[VertexID])
						: FMath::Lerp(LowKey.MorphDeltas[VertexID], HighKey.MorphDeltas[VertexID], NormalizedValue);
				}
				return MeasureRuntimeShapeAcrossOffsetTiers(
					ProbeBody,
					ProbeMorphDeltas,
					OutGap,
					bOutIntersects,
					OutMinimumClearanceMultiplier);
			};

			auto BuildSegmentEnvelopeFallback = [&]() -> bool
			{
				TArray<FCompiledMorphKey> EnvelopeKeys;
				FCompiledMorphKey& EnvelopeRestKey = EnvelopeKeys.AddDefaulted_GetRef();
				EnvelopeRestKey.BodyValue = 0.0;
				EnvelopeRestKey.bIdentity = true;
				EnvelopeRestKey.MorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
				for (const FCompiledMorphKey& SupersededKey : Keys)
				{
					if (!SupersededKey.GarmentMorph.IsNone())
					{
						RemoveGeneratedMorph(Derived, SupersededKey.GarmentMorph);
						GeneratedSampleMorphNames.Remove(SupersededKey.GarmentMorph);
					}
				}
				constexpr int32 EnvelopeProbeSubdivisions = 8;
				constexpr int32 MaximumEnvelopePasses = 8;

				for (int32 SegmentIndex = 0; SegmentIndex < CertifiedBaseSampleCount; ++SegmentIndex)
				{
					const double LowBodyValue = Binding.MaximumCertifiedValue
						* static_cast<double>(SegmentIndex)
						/ static_cast<double>(CertifiedBaseSampleCount);
					const double HighBodyValue = Binding.MaximumCertifiedValue
						* static_cast<double>(SegmentIndex + 1)
						/ static_cast<double>(CertifiedBaseSampleCount);
					const double CenterBodyValue = (LowBodyValue + HighBodyValue) * 0.5;
					const FCompiledMorphKey* SeedKey = &Keys[0];
					for (const FCompiledMorphKey& CandidateKey : Keys)
					{
						if (FMath::Abs(CandidateKey.BodyValue - CenterBodyValue)
							< FMath::Abs(SeedKey->BodyValue - CenterBodyValue))
						{
							SeedKey = &CandidateKey;
						}
					}

					FDynamicMesh3 EnvelopeGarment(GarmentMesh);
					TArray<FVector3d> EnvelopeRepairAccumulated;
					EnvelopeRepairAccumulated.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
					for (int32 VertexID : EnvelopeGarment.VertexIndicesItr())
					{
						EnvelopeGarment.SetVertex(
							VertexID,
							EnvelopeGarment.GetVertex(VertexID)
								+ ClearanceDeltas[VertexID]
								+ SeedKey->MorphDeltas[VertexID]);
					}

					for (int32 EnvelopePass = 0; EnvelopePass < MaximumEnvelopePasses; ++EnvelopePass)
					{
						bool bPassAppliedCorrection = false;
						for (int32 ProbeIndex = 0; ProbeIndex <= EnvelopeProbeSubdivisions; ++ProbeIndex)
						{
							const double ProbeFraction = static_cast<double>(ProbeIndex)
								/ static_cast<double>(EnvelopeProbeSubdivisions);
							const double BodyValue = FMath::Lerp(LowBodyValue, HighBodyValue, ProbeFraction);
							FDynamicMesh3 ProbeBody(BodyMesh);
							for (int32 VertexID : ProbeBody.VertexIndicesItr())
							{
								const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
								ProbeBody.SetVertex(
									VertexID,
									ProbeBody.GetVertex(VertexID)
										+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
							}
							double Gap = 0.0;
							int32 PenetratingVertices = 0;
							if (!MeasureVertexClearance(ProbeBody, EnvelopeGarment, Gap, PenetratingVertices))
							{
								OutError = FString::Printf(TEXT("Could not measure envelope fallback for morph %s."), *Candidate.Name.ToString());
								return false;
							}
							if (Gap >= MorphFallbackClearance - ClearanceToleranceCm)
							{
								continue;
							}

							TArray<FVector3d> Corrections;
							double RepairGap = 0.0;
							bool bRepairedEnvelope = BuildAnchoredClearanceCorrections(
								ProbeBody,
								EnvelopeGarment,
								AnchoredRadialDirections,
								AnchoredClearanceDirections,
								Correspondence,
								MorphFallbackClearance,
								StandardShapeRepair,
								Corrections,
								RepairGap);
							if (!bRepairedEnvelope)
							{
								bRepairedEnvelope = BuildAnchoredClearanceCorrections(
									ProbeBody,
									EnvelopeGarment,
									AnchoredRadialDirections,
									AnchoredClearanceDirections,
									Correspondence,
									DesiredClearance,
									StandardShapeRepair,
									Corrections,
									RepairGap);
							}
							if (!bRepairedEnvelope)
							{
								bRepairedEnvelope = BuildDirectionalClearanceCorrections(
									ProbeBody,
									EnvelopeGarment,
									DesiredClearance,
									StandardShapeRepair,
									Corrections,
									RepairGap);
							}
							if (!bRepairedEnvelope || Corrections.Num() < GarmentMesh.MaxVertexID())
							{
								OutError = FString::Printf(
									TEXT("Morph %s cannot build a certified envelope for segment %d at %.6f (gap %.4fcm)."),
									*Candidate.Name.ToString(), SegmentIndex, BodyValue, RepairGap);
								return false;
							}
							for (int32 VertexID : EnvelopeGarment.VertexIndicesItr())
							{
								const FVector3d AccumulatedRepair = EnvelopeRepairAccumulated[VertexID] + Corrections[VertexID];
								if (AccumulatedRepair.Length() > MaximumRepair + ClearanceToleranceCm)
								{
									OutError = FString::Printf(
										TEXT("Morph %s envelope segment %d exceeds %.4fcm cumulative repair at vertex %d."),
										*Candidate.Name.ToString(), SegmentIndex, MaximumRepair, VertexID);
									return false;
								}
								EnvelopeRepairAccumulated[VertexID] = AccumulatedRepair;
								EnvelopeGarment.SetVertex(
									VertexID,
									EnvelopeGarment.GetVertex(VertexID) + Corrections[VertexID]);
							}
							bPassAppliedCorrection = true;
							bRepaired = true;
						}
						if (!bPassAppliedCorrection)
						{
							break;
						}
					}

					double SegmentMinimumGap = TNumericLimits<double>::Max();
					for (int32 ProbeIndex = 0; ProbeIndex <= EnvelopeProbeSubdivisions; ++ProbeIndex)
					{
						const double ProbeFraction = static_cast<double>(ProbeIndex)
							/ static_cast<double>(EnvelopeProbeSubdivisions);
						const double BodyValue = FMath::Lerp(LowBodyValue, HighBodyValue, ProbeFraction);
						FDynamicMesh3 ProbeBody(BodyMesh);
						for (int32 VertexID : ProbeBody.VertexIndicesItr())
						{
							const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
							ProbeBody.SetVertex(
								VertexID,
								ProbeBody.GetVertex(VertexID)
									+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
						}
						double Gap = 0.0;
						int32 PenetratingVertices = 0;
						if (!MeasureVertexClearance(ProbeBody, EnvelopeGarment, Gap, PenetratingVertices)
							|| Gap < DesiredClearance - ClearanceToleranceCm)
						{
							OutError = FString::Printf(
								TEXT("Morph %s envelope segment %d failed final probe %.6f: %.4fcm < %.4fcm."),
								*Candidate.Name.ToString(), SegmentIndex, BodyValue, Gap, DesiredClearance);
							return false;
						}
						SegmentMinimumGap = FMath::Min(SegmentMinimumGap, Gap);
					}
					MorphMinimumGap = FMath::Min(MorphMinimumGap, SegmentMinimumGap);

					FCompiledMorphKey EnvelopeKey;
					EnvelopeKey.BodyValue = HighBodyValue;
					EnvelopeKey.bStepFromPrevious = true;
					EnvelopeKey.StepSwitchBodyValue = LowBodyValue;
					EnvelopeKey.MorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
					for (int32 VertexID : EnvelopeGarment.VertexIndicesItr())
					{
						const FVector3d BaseFittedPosition = GarmentMesh.GetVertex(VertexID) + ClearanceDeltas[VertexID];
						EnvelopeKey.MorphDeltas[VertexID] = EnvelopeGarment.GetVertex(VertexID) - BaseFittedPosition;
					}

					const FString EnvelopeNameString = FString::Printf(
						TEXT("EFV2_%s_ENV%02d"), *MorphKey, SegmentIndex + 1);
					const FName EnvelopeMorphName(*EnvelopeNameString);
					if (GeneratedSampleMorphNames.Contains(EnvelopeMorphName))
					{
						OutError = FString::Printf(TEXT("Generated envelope morph collision: %s."), *EnvelopeNameString);
						return false;
					}
					GeneratedSampleMorphNames.Add(EnvelopeMorphName);
					EnvelopeKey.GarmentMorph = EnvelopeMorphName;

					const TArray<FVector3d> InitialEnvelopeDeltas = EnvelopeKey.MorphDeltas;
					constexpr int32 MaximumStoredEnvelopeCookPasses = 4;
					double StoredEnvelopeMinimumGap = TNumericLimits<double>::Max();
					bool bStoredEnvelopeCertified = false;
					for (int32 StoredEnvelopePass = 0;
						StoredEnvelopePass < MaximumStoredEnvelopeCookPasses;
						++StoredEnvelopePass)
					{
						// A single envelope morph is shared by the whole body-value segment.
						// Cook it successively against every body probe; later probes may
						// alter the stored sparse deltas, so the complete set is rechecked
						// and cycled until one committed field satisfies all 9x9 contracts.
						for (int32 ProbeIndex = 0; ProbeIndex <= EnvelopeProbeSubdivisions; ++ProbeIndex)
						{
							const double ProbeFraction = static_cast<double>(ProbeIndex)
								/ static_cast<double>(EnvelopeProbeSubdivisions);
							const double BodyValue = FMath::Lerp(LowBodyValue, HighBodyValue, ProbeFraction);
							FDynamicMesh3 ProbeBody(BodyMesh);
							for (int32 VertexID : ProbeBody.VertexIndicesItr())
							{
								const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
								ProbeBody.SetVertex(
									VertexID,
									ProbeBody.GetVertex(VertexID)
										+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
							}
							FMeshNormals ProbeBodyNormals(&ProbeBody);
							ProbeBodyNormals.ComputeVertexNormals();
							TArray<FVector3d> ProbeClearanceDirections;
							ProbeClearanceDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
							for (int32 VertexID : GarmentMesh.VertexIndicesItr())
							{
								const FSurfaceCorrespondence& Surface = Correspondence[VertexID];
								if (Surface.BodyTriangle == INDEX_NONE || !ProbeBody.IsTriangle(Surface.BodyTriangle))
								{
									OutError = FString::Printf(
										TEXT("Envelope %s has invalid correspondence at probe %.6f."),
										*EnvelopeNameString,
										BodyValue);
									return false;
								}
								ProbeClearanceDirections[VertexID] = InterpolateNormal(
									ProbeBody,
									ProbeBodyNormals,
									Surface.BodyTriangle,
									Surface.Barycentric);
							}
							bool bProbeRepaired = false;
							double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
							const FString ProbeContext = FString::Printf(
								TEXT("%s probe %.6f"),
								*EnvelopeNameString,
								BodyValue);
							if (!WriteAndCertifyStoredShapeMorph(
								ProbeBody,
								ProbeClearanceDirections,
								EnvelopeMorphName,
								ProbeContext,
								EnvelopeKey.MorphDeltas,
								bProbeRepaired,
								ProbeMinimumClearanceMultiplier))
							{
								return false;
							}
							EnvelopeKey.MinimumClearanceMultiplier = FMath::Max(
								EnvelopeKey.MinimumClearanceMultiplier,
								ProbeMinimumClearanceMultiplier);
							bRepaired |= bProbeRepaired;
							for (int32 VertexID : GarmentMesh.VertexIndicesItr())
							{
								if ((EnvelopeKey.MorphDeltas[VertexID] - InitialEnvelopeDeltas[VertexID]).Length()
									> MaximumRepair + ClearanceToleranceCm)
								{
									OutError = FString::Printf(
										TEXT("Stored envelope %s exceeded %.4fcm cumulative repair at vertex %d."),
										*EnvelopeNameString,
										MaximumRepair,
										VertexID);
									return false;
								}
							}
						}

						StoredEnvelopeMinimumGap = TNumericLimits<double>::Max();
						bool bAllStoredProbesPassed = true;
						double FailedStoredProbeValue = LowBodyValue;
						double FailedStoredProbeGap = TNumericLimits<double>::Max();
						bool bFailedStoredProbeIntersects = false;
						for (int32 ProbeIndex = 0; ProbeIndex <= EnvelopeProbeSubdivisions; ++ProbeIndex)
						{
							const double ProbeFraction = static_cast<double>(ProbeIndex)
								/ static_cast<double>(EnvelopeProbeSubdivisions);
							const double BodyValue = FMath::Lerp(LowBodyValue, HighBodyValue, ProbeFraction);
							FDynamicMesh3 ProbeBody(BodyMesh);
							for (int32 VertexID : ProbeBody.VertexIndicesItr())
							{
								const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
								ProbeBody.SetVertex(
									VertexID,
									ProbeBody.GetVertex(VertexID)
										+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * BodyValue);
							}
							double StoredProbeGap = 0.0;
							double StoredProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
							bool bStoredProbeIntersects = false;
							const bool bStoredProbePassed = MeasureRuntimeShapeAcrossOffsetTiers(
								ProbeBody,
								EnvelopeKey.MorphDeltas,
								StoredProbeGap,
								bStoredProbeIntersects,
								StoredProbeMinimumClearanceMultiplier)
								&& StoredProbeGap >= DesiredClearance - ClearanceToleranceCm
								&& !bStoredProbeIntersects;
							StoredEnvelopeMinimumGap = FMath::Min(StoredEnvelopeMinimumGap, StoredProbeGap);
							EnvelopeKey.MinimumClearanceMultiplier = FMath::Max(
								EnvelopeKey.MinimumClearanceMultiplier,
								StoredProbeMinimumClearanceMultiplier);
							if (!bStoredProbePassed && bAllStoredProbesPassed)
							{
								bAllStoredProbesPassed = false;
								FailedStoredProbeValue = BodyValue;
								FailedStoredProbeGap = StoredProbeGap;
								bFailedStoredProbeIntersects = bStoredProbeIntersects;
							}
						}
						if (bAllStoredProbesPassed)
						{
							bStoredEnvelopeCertified = true;
							UE_LOG(
								LogEFClothingFitCompiler,
								Display,
								TEXT("V24 stored envelope %s certified after %d multi-body cook pass(es): minimum gap %.4fcm."),
								*EnvelopeNameString,
								StoredEnvelopePass + 1,
								StoredEnvelopeMinimumGap);
							break;
						}
						if (StoredEnvelopePass + 1 >= MaximumStoredEnvelopeCookPasses)
						{
							OutError = FString::Printf(
								TEXT("Stored envelope %s failed probe %.6f after %d multi-body cook passes: gap %.4fcm, triangles=%s."),
								*EnvelopeNameString,
								FailedStoredProbeValue,
								MaximumStoredEnvelopeCookPasses,
								FailedStoredProbeGap,
								bFailedStoredProbeIntersects ? TEXT("INTERSECT") : TEXT("CLEAR"));
							return false;
						}
					}
					if (!bStoredEnvelopeCertified)
					{
						OutError = FString::Printf(TEXT("Stored envelope %s ended without certification."), *EnvelopeNameString);
						return false;
					}
					MorphMinimumGap = FMath::Min(MorphMinimumGap, StoredEnvelopeMinimumGap);
					OutMinimumCertifiedOffsetGap = FMath::Min(
						OutMinimumCertifiedOffsetGap,
						StoredEnvelopeMinimumGap);
					EnvelopeKeys.Add(MoveTemp(EnvelopeKey));
				}
				Keys = MoveTemp(EnvelopeKeys);
				return true;
			};

			for (;;)
			{
				for (int32 KeyIndex = 1; KeyIndex < Keys.Num(); ++KeyIndex)
				{
					Keys[KeyIndex].bStepFromPrevious = false;
					Keys[KeyIndex].StepSwitchBodyValue = 0.0;
				}
				bool bInsertedAdaptiveKey = false;
				bool bBuiltEnvelopeFallback = false;
				for (int32 IntervalIndex = 0; IntervalIndex + 1 < Keys.Num(); ++IntervalIndex)
				{
					FCompiledMorphKey& LowKey = Keys[IntervalIndex];
					FCompiledMorphKey& HighKey = Keys[IntervalIndex + 1];
					bool bLinearIntervalPassed = true;
					double LinearMinimumClearanceMultiplier = FMath::Max(
						LowKey.MinimumClearanceMultiplier,
						HighKey.MinimumClearanceMultiplier);
					TArray<double, TInlineAllocator<3>> LinearGaps;
					for (double NormalizedValue : {0.25, 0.50, 0.75})
					{
						double Gap = 0.0;
						double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
						bool bIntersects = false;
						if (!MeasureKeyPair(
							LowKey,
							HighKey,
							NormalizedValue,
							false,
							Gap,
							bIntersects,
							ProbeMinimumClearanceMultiplier))
						{
							OutError = FString::Printf(TEXT("Could not measure a linear piecewise probe for morph %s."), *Candidate.Name.ToString());
							return TransferredNames;
						}
						LinearGaps.Add(Gap);
						LinearMinimumClearanceMultiplier = FMath::Max(
							LinearMinimumClearanceMultiplier,
							ProbeMinimumClearanceMultiplier);
						bLinearIntervalPassed &= Gap >= DesiredClearance - ClearanceToleranceCm && !bIntersects;
					}
					if (bLinearIntervalPassed)
					{
						LowKey.MinimumClearanceMultiplier = FMath::Max(
							LowKey.MinimumClearanceMultiplier,
							LinearMinimumClearanceMultiplier);
						HighKey.MinimumClearanceMultiplier = FMath::Max(
							HighKey.MinimumClearanceMultiplier,
							LinearMinimumClearanceMultiplier);
						for (double Gap : LinearGaps)
						{
							MorphMinimumGap = FMath::Min(MorphMinimumGap, Gap);
							OutMinimumCertifiedOffsetGap = FMath::Min(OutMinimumCertifiedOffsetGap, Gap);
						}
						continue;
					}

					bool bStepIntervalPassed = true;
					double StepMinimumClearanceMultiplier = FMath::Max(
						LowKey.MinimumClearanceMultiplier,
						HighKey.MinimumClearanceMultiplier);
					double WorstStepGap = TNumericLimits<double>::Max();
					double WorstStepBodyValue = (LowKey.BodyValue + HighKey.BodyValue) * 0.5;
					bool bWorstStepIntersects = false;
					for (int32 ProbeIndex = 1; ProbeIndex < 8; ++ProbeIndex)
					{
						const double NormalizedValue = static_cast<double>(ProbeIndex) / 8.0;
						double Gap = 0.0;
						double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
						bool bIntersects = false;
						if (!MeasureKeyPair(
							LowKey,
							HighKey,
							NormalizedValue,
							true,
							Gap,
							bIntersects,
							ProbeMinimumClearanceMultiplier))
						{
							OutError = FString::Printf(TEXT("Could not measure a stepped piecewise probe for morph %s."), *Candidate.Name.ToString());
							return TransferredNames;
						}
						StepMinimumClearanceMultiplier = FMath::Max(
							StepMinimumClearanceMultiplier,
							ProbeMinimumClearanceMultiplier);
						if ((bIntersects && !bWorstStepIntersects)
							|| (bIntersects == bWorstStepIntersects && Gap < WorstStepGap))
						{
							WorstStepGap = Gap;
							WorstStepBodyValue = FMath::Lerp(LowKey.BodyValue, HighKey.BodyValue, NormalizedValue);
							bWorstStepIntersects = bIntersects;
						}
						bStepIntervalPassed &= Gap >= DesiredClearance - ClearanceToleranceCm && !bIntersects;
					}
					if (bStepIntervalPassed)
					{
						const double SwitchBodyValue = (LowKey.BodyValue + HighKey.BodyValue) * 0.5;
						FDynamicMesh3 SwitchBody(BodyMesh);
						for (int32 VertexID : SwitchBody.VertexIndicesItr())
						{
							const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
							SwitchBody.SetVertex(
								VertexID,
								SwitchBody.GetVertex(VertexID)
									+ FVector3d(BodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * SwitchBodyValue);
						}
						for (const TArray<FVector3d>* SwitchSideDeltas : {&LowKey.MorphDeltas, &HighKey.MorphDeltas})
						{
							double SwitchSideGap = 0.0;
							double SwitchMinimumClearanceMultiplier = CertifiedClearanceTierMin;
							bool bSwitchSideIntersects = false;
							if (!MeasureRuntimeShapeAcrossOffsetTiers(
								SwitchBody,
								*SwitchSideDeltas,
								SwitchSideGap,
								bSwitchSideIntersects,
								SwitchMinimumClearanceMultiplier))
							{
								OutError = FString::Printf(
									TEXT("Could not measure a step-switch side for morph %s."),
									*Candidate.Name.ToString());
								return TransferredNames;
							}
							if (SwitchSideGap < DesiredClearance - ClearanceToleranceCm || bSwitchSideIntersects)
							{
								bStepIntervalPassed = false;
								WorstStepGap = SwitchSideGap;
								WorstStepBodyValue = SwitchBodyValue;
								bWorstStepIntersects = bSwitchSideIntersects;
								break;
							}
							StepMinimumClearanceMultiplier = FMath::Max(
								StepMinimumClearanceMultiplier,
								SwitchMinimumClearanceMultiplier);
							WorstStepGap = FMath::Min(WorstStepGap, SwitchSideGap);
						}
					}
					if (bStepIntervalPassed)
					{
						HighKey.bStepFromPrevious = true;
						HighKey.StepSwitchBodyValue = (LowKey.BodyValue + HighKey.BodyValue) * 0.5;
						LowKey.MinimumClearanceMultiplier = FMath::Max(
							LowKey.MinimumClearanceMultiplier,
							StepMinimumClearanceMultiplier);
						HighKey.MinimumClearanceMultiplier = FMath::Max(
							HighKey.MinimumClearanceMultiplier,
							StepMinimumClearanceMultiplier);
						MorphMinimumGap = FMath::Min(MorphMinimumGap, WorstStepGap);
						OutMinimumCertifiedOffsetGap = FMath::Min(OutMinimumCertifiedOffsetGap, WorstStepGap);
						continue;
					}
					if (Keys.Num() - 1 >= MaximumAdaptiveSamplesPerMorph)
					{
						if (!BuildSegmentEnvelopeFallback())
						{
							return TransferredNames;
						}
						bBuiltEnvelopeFallback = true;
						break;
					}
					FCompiledMorphKey AdaptiveKey;
					if (!SolveKey(WorstStepBodyValue, AdaptiveKey))
					{
						return TransferredNames;
					}
					Keys.Add(MoveTemp(AdaptiveKey));
					SortKeys();
					bInsertedAdaptiveKey = true;
					break;
				}
				if (bBuiltEnvelopeFallback || !bInsertedAdaptiveKey)
				{
					break;
				}
			}

			for (int32 KeyIndex = 1; KeyIndex < Keys.Num(); ++KeyIndex)
			{
				FEFClothingMorphSample& Sample = Binding.Samples.AddDefaulted_GetRef();
				Sample.BodyValue = static_cast<float>(Keys[KeyIndex].BodyValue);
				Sample.GarmentMorph = Keys[KeyIndex].GarmentMorph;
				Sample.bIdentity = Keys[KeyIndex].bIdentity;
				Sample.bStepFromPrevious = Keys[KeyIndex].bStepFromPrevious;
				Sample.StepSwitchBodyValue = static_cast<float>(Keys[KeyIndex].StepSwitchBodyValue);
				Sample.MinimumClearanceMultiplier = static_cast<float>(
					Keys[KeyIndex].MinimumClearanceMultiplier);
				if (!Sample.bIdentity)
				{
					Binding.GarmentMorph = Sample.GarmentMorph;
				}
			}

			if (Candidate.bTransferred)
			{
				TransferredNames.Add(Candidate.Name);
			}
			++OutValidatedMorphCount;
			if (bRepaired)
			{
				++OutRepairedMorphCount;
			}
			OutMinimumSampledMorphGap = FMath::Min(OutMinimumSampledMorphGap, MorphMinimumGap);
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("V24 morph certified %d/%d: %s | keys=%d | minGap=%.4fcm"),
				CandidateOrdinal,
				Candidates.Num(),
				*Candidate.Name.ToString(),
				Binding.Samples.Num(),
				MorphMinimumGap);
			OutBindings.Add(MoveTemp(Binding));
		}

		OutBindings.Sort([](const FEFClothingMorphBinding& A, const FEFClothingMorphBinding& B)
		{
			return A.BodyMorph.LexicalLess(B.BodyMorph);
		});

		if (!FMath::IsFinite(Options.MorphActivationEpsilon)
			|| !FMath::IsNearlyZero(Options.MorphActivationEpsilon, 0.0f)
			|| Options.MorphPairGridResolution != 4
			|| Options.MorphPairProbeCountPerAxis != 3)
		{
			OutError = TEXT("V24 morph-pair options must use epsilon 0, a 4x4 grid and 3 probes per axis.");
			return TransferredNames;
		}

		TArray<FEFClothingMorphPairCompileRequest> CanonicalPairRequests = Options.MorphPairRequests;
		for (FEFClothingMorphPairCompileRequest& PairRequest : CanonicalPairRequests)
		{
			if (PairRequest.SecondBodyMorph.LexicalLess(PairRequest.FirstBodyMorph))
			{
				Swap(PairRequest.FirstBodyMorph, PairRequest.SecondBodyMorph);
			}
		}
		CanonicalPairRequests.Sort([](
			const FEFClothingMorphPairCompileRequest& A,
			const FEFClothingMorphPairCompileRequest& B)
		{
			if (A.FirstBodyMorph != B.FirstBodyMorph)
			{
				return A.FirstBodyMorph.LexicalLess(B.FirstBodyMorph);
			}
			return A.SecondBodyMorph.LexicalLess(B.SecondBodyMorph);
		});

		TSet<FString> CompiledPairKeys;
		FMeshNormals RestBodyNormals(&BodyMesh);
		RestBodyNormals.ComputeVertexNormals();
		for (const FEFClothingMorphPairCompileRequest& PairRequest : CanonicalPairRequests)
		{
			if (PairRequest.FirstBodyMorph.IsNone()
				|| PairRequest.SecondBodyMorph.IsNone()
				|| PairRequest.FirstBodyMorph == PairRequest.SecondBodyMorph)
			{
				OutError = TEXT("V24 morph-pair request is empty or self-referential.");
				return TransferredNames;
			}
			const FString PairKeyString = PairRequest.FirstBodyMorph.ToString()
				+ TEXT("|") + PairRequest.SecondBodyMorph.ToString();
			if (CompiledPairKeys.Contains(PairKeyString))
			{
				OutError = FString::Printf(TEXT("Duplicate V24 morph-pair request: %s."), *PairKeyString);
				return TransferredNames;
			}
			CompiledPairKeys.Add(PairKeyString);

			const FEFClothingMorphBinding* FirstBinding = OutBindings.FindByPredicate(
				[&PairRequest](const FEFClothingMorphBinding& Binding)
				{
					return Binding.BodyMorph == PairRequest.FirstBodyMorph;
				});
			const FEFClothingMorphBinding* SecondBinding = OutBindings.FindByPredicate(
				[&PairRequest](const FEFClothingMorphBinding& Binding)
				{
					return Binding.BodyMorph == PairRequest.SecondBodyMorph;
				});
			const TVertexAttributesConstRef<FVector3f> FirstBodyMorphDeltas =
				BodyAttributes.GetVertexMorphPositionDelta(PairRequest.FirstBodyMorph);
			const TVertexAttributesConstRef<FVector3f> SecondBodyMorphDeltas =
				BodyAttributes.GetVertexMorphPositionDelta(PairRequest.SecondBodyMorph);
			if (!FirstBinding || !SecondBinding
				|| !FirstBodyMorphDeltas.IsValid() || !SecondBodyMorphDeltas.IsValid())
			{
				OutError = FString::Printf(
					TEXT("V24 morph-pair %s requires two complete individual bindings and body delta fields."),
					*PairKeyString);
				return TransferredNames;
			}

			FEFClothingMorphPairCertificate PairCertificate;
			PairCertificate.FirstBodyMorph = PairRequest.FirstBodyMorph;
			PairCertificate.SecondBodyMorph = PairRequest.SecondBodyMorph;
			PairCertificate.FirstMinimumCertifiedValue = FirstBinding->MinimumCertifiedValue;
			PairCertificate.FirstMaximumCertifiedValue = FirstBinding->MaximumCertifiedValue;
			PairCertificate.SecondMinimumCertifiedValue = SecondBinding->MinimumCertifiedValue;
			PairCertificate.SecondMaximumCertifiedValue = SecondBinding->MaximumCertifiedValue;
			PairCertificate.GridResolution = Options.MorphPairGridResolution;
			PairCertificate.ProbeCountPerAxis = Options.MorphPairProbeCountPerAxis;
			PairCertificate.CertifiedOffsetTierCount = CertifiedClearanceTierCount;
			PairCertificate.MinimumCertifiedGapCm = TNumericLimits<float>::Max();
			const FString PairMorphKey = FMD5::HashAnsiString(*PairKeyString).Left(12);

			auto BuildCombinedBody = [
				&BodyMesh,
				&BodyMapping,
				&FirstBodyMorphDeltas,
				&SecondBodyMorphDeltas](double FirstValue, double SecondValue)
			{
				FDynamicMesh3 CombinedBody(BodyMesh);
				for (int32 VertexID : CombinedBody.VertexIndicesItr())
				{
					const int32 OriginalVertexID = BodyMapping.GetOriginalNonManifoldVertexID(VertexID);
					CombinedBody.SetVertex(
						VertexID,
						CombinedBody.GetVertex(VertexID)
							+ FVector3d(FirstBodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * FirstValue
							+ FVector3d(SecondBodyMorphDeltas.Get(FVertexID(OriginalVertexID))) * SecondValue);
				}
				return CombinedBody;
			};

			for (int32 FirstCellIndex = 0;
				FirstCellIndex < PairCertificate.GridResolution;
				++FirstCellIndex)
			{
				for (int32 SecondCellIndex = 0;
					SecondCellIndex < PairCertificate.GridResolution;
					++SecondCellIndex)
				{
					FEFClothingMorphPairCell PairCell;
					PairCell.FirstCellIndex = FirstCellIndex;
					PairCell.SecondCellIndex = SecondCellIndex;
					PairCell.FirstMinimumValue = FMath::Lerp(
						PairCertificate.FirstMinimumCertifiedValue,
						PairCertificate.FirstMaximumCertifiedValue,
						static_cast<float>(FirstCellIndex)
							/ static_cast<float>(PairCertificate.GridResolution));
					PairCell.FirstMaximumValue = FMath::Lerp(
						PairCertificate.FirstMinimumCertifiedValue,
						PairCertificate.FirstMaximumCertifiedValue,
						static_cast<float>(FirstCellIndex + 1)
							/ static_cast<float>(PairCertificate.GridResolution));
					PairCell.SecondMinimumValue = FMath::Lerp(
						PairCertificate.SecondMinimumCertifiedValue,
						PairCertificate.SecondMaximumCertifiedValue,
						static_cast<float>(SecondCellIndex)
							/ static_cast<float>(PairCertificate.GridResolution));
					PairCell.SecondMaximumValue = FMath::Lerp(
						PairCertificate.SecondMinimumCertifiedValue,
						PairCertificate.SecondMaximumCertifiedValue,
						static_cast<float>(SecondCellIndex + 1)
							/ static_cast<float>(PairCertificate.GridResolution));
					const double FirstCenter = (PairCell.FirstMinimumValue + PairCell.FirstMaximumValue) * 0.5;
					const double SecondCenter = (PairCell.SecondMinimumValue + PairCell.SecondMaximumValue) * 0.5;
					FDynamicMesh3 SeedBody = BuildCombinedBody(FirstCenter, SecondCenter);
					FMeshNormals SeedBodyNormals(&SeedBody);
					SeedBodyNormals.ComputeVertexNormals();

					TArray<FVector3d> CellMorphDeltas;
					CellMorphDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
					for (int32 VertexID : GarmentMesh.VertexIndicesItr())
					{
						const FSurfaceCorrespondence& Surface = Correspondence[VertexID];
						if (Surface.BodyTriangle == INDEX_NONE
							|| !BodyMesh.IsTriangle(Surface.BodyTriangle)
							|| !SeedBody.IsTriangle(Surface.BodyTriangle))
						{
							OutError = FString::Printf(
								TEXT("V24 morph-pair %s cell %d,%d has an invalid surface correspondence."),
								*PairKeyString,
								FirstCellIndex,
								SecondCellIndex);
							return TransferredNames;
						}
						const FIndex3i RestTriangle = BodyMesh.GetTriangle(Surface.BodyTriangle);
						const FIndex3i SeedTriangle = SeedBody.GetTriangle(Surface.BodyTriangle);
						const FVector3d RestSurfacePoint =
							BodyMesh.GetVertex(RestTriangle.A) * Surface.Barycentric.X
							+ BodyMesh.GetVertex(RestTriangle.B) * Surface.Barycentric.Y
							+ BodyMesh.GetVertex(RestTriangle.C) * Surface.Barycentric.Z;
						const FVector3d SeedSurfacePoint =
							SeedBody.GetVertex(SeedTriangle.A) * Surface.Barycentric.X
							+ SeedBody.GetVertex(SeedTriangle.B) * Surface.Barycentric.Y
							+ SeedBody.GetVertex(SeedTriangle.C) * Surface.Barycentric.Z;
						FVector3d RestNormal = InterpolateNormal(
							BodyMesh, RestBodyNormals, Surface.BodyTriangle, Surface.Barycentric);
						FVector3d SeedNormal = InterpolateNormal(
							SeedBody, SeedBodyNormals, Surface.BodyTriangle, Surface.Barycentric);
						if (!RestNormal.Normalize())
						{
							RestNormal = SeedNormal;
						}
						if (!SeedNormal.Normalize())
						{
							SeedNormal = RestNormal;
						}
						const FVector3d BaseFittedPosition = GarmentMesh.GetVertex(VertexID)
							+ ClearanceDeltas[VertexID];
						const FVector3d RestSurfaceOffset = BaseFittedPosition - RestSurfacePoint;
						const FVector3d SeedGarmentPosition = SeedSurfacePoint
							+ FQuat4d::FindBetweenNormals(RestNormal, SeedNormal).RotateVector(RestSurfaceOffset);
						CellMorphDeltas[VertexID] = SeedGarmentPosition - BaseFittedPosition;
					}
					ReconcileNonManifoldVectorField(GarmentMesh, CellMorphDeltas, ThicknessShell);
					const TArray<FVector3d> InitialCellMorphDeltas = CellMorphDeltas;

					const FString PairCellMorphNameString = FString::Printf(
						TEXT("EFV2_P%s_%02d_%02d"),
						*PairMorphKey,
						FirstCellIndex,
						SecondCellIndex);
					const FName PairCellMorphName(*PairCellMorphNameString);
					if (GeneratedSampleMorphNames.Contains(PairCellMorphName))
					{
						OutError = FString::Printf(TEXT("Generated V24 pair-cell morph collision: %s."), *PairCellMorphNameString);
						return TransferredNames;
					}
					GeneratedSampleMorphNames.Add(PairCellMorphName);
					PairCell.GarmentMorph = PairCellMorphName;

					constexpr int32 MaximumPairCellCookPasses = 4;
					bool bCellCertified = false;
					double CellMinimumGap = TNumericLimits<double>::Max();
					for (int32 CellCookPass = 0;
						CellCookPass < MaximumPairCellCookPasses;
						++CellCookPass)
					{
						for (int32 FirstProbeIndex = 0;
							FirstProbeIndex < PairCertificate.ProbeCountPerAxis;
							++FirstProbeIndex)
						{
							const double FirstProbeFraction = static_cast<double>(FirstProbeIndex)
								/ static_cast<double>(PairCertificate.ProbeCountPerAxis - 1);
							const double FirstValue = FMath::Lerp(
								static_cast<double>(PairCell.FirstMinimumValue),
								static_cast<double>(PairCell.FirstMaximumValue),
								FirstProbeFraction);
							for (int32 SecondProbeIndex = 0;
								SecondProbeIndex < PairCertificate.ProbeCountPerAxis;
								++SecondProbeIndex)
							{
								const double SecondProbeFraction = static_cast<double>(SecondProbeIndex)
									/ static_cast<double>(PairCertificate.ProbeCountPerAxis - 1);
								const double SecondValue = FMath::Lerp(
									static_cast<double>(PairCell.SecondMinimumValue),
									static_cast<double>(PairCell.SecondMaximumValue),
									SecondProbeFraction);
								FDynamicMesh3 ProbeBody = BuildCombinedBody(FirstValue, SecondValue);
								FMeshNormals ProbeBodyNormals(&ProbeBody);
								ProbeBodyNormals.ComputeVertexNormals();
								TArray<FVector3d> ProbeClearanceDirections;
								ProbeClearanceDirections.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
								for (int32 VertexID : GarmentMesh.VertexIndicesItr())
								{
									const FSurfaceCorrespondence& Surface = Correspondence[VertexID];
									ProbeClearanceDirections[VertexID] = InterpolateNormal(
										ProbeBody,
										ProbeBodyNormals,
										Surface.BodyTriangle,
										Surface.Barycentric);
								}
								bool bProbeRepaired = false;
								double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
								const FString ProbeContext = FString::Printf(
									TEXT("V24 pair %s cell %d,%d probe %.6f,%.6f"),
									*PairKeyString,
									FirstCellIndex,
									SecondCellIndex,
									FirstValue,
									SecondValue);
								if (!WriteAndCertifyStoredShapeMorph(
									ProbeBody,
									ProbeClearanceDirections,
									PairCellMorphName,
									ProbeContext,
									CellMorphDeltas,
									bProbeRepaired,
									ProbeMinimumClearanceMultiplier))
								{
									return TransferredNames;
								}
								PairCell.MinimumClearanceMultiplier = FMath::Max(
									PairCell.MinimumClearanceMultiplier,
									static_cast<float>(ProbeMinimumClearanceMultiplier));
								for (int32 VertexID : GarmentMesh.VertexIndicesItr())
								{
									if ((CellMorphDeltas[VertexID] - InitialCellMorphDeltas[VertexID]).Length()
										> MaximumRepair + ClearanceToleranceCm)
									{
										OutError = FString::Printf(
											TEXT("V24 pair %s cell %d,%d exceeded %.4fcm cumulative repair at vertex %d."),
											*PairKeyString,
											FirstCellIndex,
											SecondCellIndex,
											MaximumRepair,
											VertexID);
										return TransferredNames;
									}
								}
							}
						}

						CellMinimumGap = TNumericLimits<double>::Max();
						bool bAllCellProbesPassed = true;
						double FailedFirstValue = PairCell.FirstMinimumValue;
						double FailedSecondValue = PairCell.SecondMinimumValue;
						double FailedGap = TNumericLimits<double>::Max();
						bool bFailedIntersects = false;
						for (int32 FirstProbeIndex = 0;
							FirstProbeIndex < PairCertificate.ProbeCountPerAxis;
							++FirstProbeIndex)
						{
							const double FirstProbeFraction = static_cast<double>(FirstProbeIndex)
								/ static_cast<double>(PairCertificate.ProbeCountPerAxis - 1);
							const double FirstValue = FMath::Lerp(
								static_cast<double>(PairCell.FirstMinimumValue),
								static_cast<double>(PairCell.FirstMaximumValue),
								FirstProbeFraction);
							for (int32 SecondProbeIndex = 0;
								SecondProbeIndex < PairCertificate.ProbeCountPerAxis;
								++SecondProbeIndex)
							{
								const double SecondProbeFraction = static_cast<double>(SecondProbeIndex)
									/ static_cast<double>(PairCertificate.ProbeCountPerAxis - 1);
								const double SecondValue = FMath::Lerp(
									static_cast<double>(PairCell.SecondMinimumValue),
									static_cast<double>(PairCell.SecondMaximumValue),
									SecondProbeFraction);
								FDynamicMesh3 ProbeBody = BuildCombinedBody(FirstValue, SecondValue);
								double ProbeMinimumGap = TNumericLimits<double>::Max();
								double ProbeMinimumClearanceMultiplier = CertifiedClearanceTierMin;
								bool bProbeIntersects = false;
								const bool bProbePassed = MeasureRuntimeShapeAcrossOffsetTiers(
									ProbeBody,
									CellMorphDeltas,
									ProbeMinimumGap,
									bProbeIntersects,
									ProbeMinimumClearanceMultiplier)
									&& ProbeMinimumGap >= DesiredClearance - ClearanceToleranceCm
									&& !bProbeIntersects;
								CellMinimumGap = FMath::Min(CellMinimumGap, ProbeMinimumGap);
								PairCell.MinimumClearanceMultiplier = FMath::Max(
									PairCell.MinimumClearanceMultiplier,
									static_cast<float>(ProbeMinimumClearanceMultiplier));
								if (!bProbePassed && bAllCellProbesPassed)
								{
									bAllCellProbesPassed = false;
									FailedFirstValue = FirstValue;
									FailedSecondValue = SecondValue;
									FailedGap = ProbeMinimumGap;
									bFailedIntersects = bProbeIntersects;
								}
							}
						}
						if (bAllCellProbesPassed)
						{
							bCellCertified = true;
							UE_LOG(
								LogEFClothingFitCompiler,
								Display,
								TEXT("V24 pair %s cell %d,%d certified after %d multi-body cook pass(es): minimum gap %.4fcm."),
								*PairKeyString,
								FirstCellIndex,
								SecondCellIndex,
								CellCookPass + 1,
								CellMinimumGap);
							break;
						}
						if (CellCookPass + 1 >= MaximumPairCellCookPasses)
						{
							OutError = FString::Printf(
								TEXT("V24 pair %s cell %d,%d failed probe %.6f,%.6f after %d passes: gap %.4fcm triangles=%s."),
								*PairKeyString,
								FirstCellIndex,
								SecondCellIndex,
								FailedFirstValue,
								FailedSecondValue,
								MaximumPairCellCookPasses,
								FailedGap,
								bFailedIntersects ? TEXT("INTERSECT") : TEXT("CLEAR"));
							return TransferredNames;
						}
					}
					if (!bCellCertified)
					{
						OutError = FString::Printf(
							TEXT("V24 pair %s cell %d,%d ended without certification."),
							*PairKeyString,
							FirstCellIndex,
							SecondCellIndex);
						return TransferredNames;
					}

					PairCell.MinimumCertifiedGapCm = static_cast<float>(CellMinimumGap);
					PairCell.CertifiedBodyProbeCount = PairCertificate.ProbeCountPerAxis
						* PairCertificate.ProbeCountPerAxis;
					PairCell.CertifiedOffsetEvaluationCount = PairCell.CertifiedBodyProbeCount
						* CertifiedClearanceTierCount;
					OutPairBodyProbeCount += PairCell.CertifiedBodyProbeCount;
					OutPairOffsetEvaluationCount += PairCell.CertifiedOffsetEvaluationCount;
					OutMinimumSampledPairGap = FMath::Min(OutMinimumSampledPairGap, CellMinimumGap);
					OutMinimumCertifiedOffsetGap = FMath::Min(OutMinimumCertifiedOffsetGap, CellMinimumGap);
					PairCertificate.MinimumCertifiedGapCm = FMath::Min(
						PairCertificate.MinimumCertifiedGapCm,
						PairCell.MinimumCertifiedGapCm);
					PairCertificate.Cells.Add(MoveTemp(PairCell));
				}
			}
			OutPairCertificates.Add(MoveTemp(PairCertificate));
		}
		if (OutPairCertificates.IsEmpty())
		{
			OutMinimumSampledPairGap = 0.0;
		}
		if (OutBindings.IsEmpty())
		{
			OutMinimumSampledMorphGap = DesiredClearance;
		}
		return TransferredNames;
	}

	static bool BuildConcurrentBoundsContract(
		const USkeletalMesh* Derived,
		const FDynamicMesh3& GarmentMesh,
		const TArray<FVector3d>& ClearanceDeltas,
		const TArray<FEFClothingMorphBinding>& Bindings,
		const TArray<FEFClothingMorphPairCertificate>& PairCertificates,
		FVector& OutBoxExpansion,
		float& OutSphereExpansion,
		double& OutMaximumMorphDisplacement,
		FString& OutError)
	{
		if (!IsValid(Derived) || ClearanceDeltas.Num() < GarmentMesh.MaxVertexID())
		{
			OutError = TEXT("Cannot build concurrent bounds contract from invalid fitted topology.");
			return false;
		}

		TArray<FVector3d> ExclusiveAxisMaximum;
		TArray<double> ExclusiveRadiusMaximum;
		ExclusiveAxisMaximum.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		ExclusiveRadiusMaximum.Init(0.0, GarmentMesh.MaxVertexID());
		OutMaximumMorphDisplacement = 0.0;

		auto AccumulateExclusiveMorph = [
			Derived,
			&GarmentMesh,
			&ExclusiveAxisMaximum,
			&ExclusiveRadiusMaximum,
			&OutMaximumMorphDisplacement,
			&OutError](FName MorphName) -> bool
		{
			if (MorphName.IsNone())
			{
				return true;
			}
			TArray<FVector3d> StoredDeltas;
			int32 IgnoredAlteredCount = 0;
			if (!ReadStoredMorphDeltas(
				Derived,
				MorphName,
				GarmentMesh,
				nullptr,
				StoredDeltas,
				IgnoredAlteredCount,
				OutError))
			{
				return false;
			}
			for (int32 VertexID : GarmentMesh.VertexIndicesItr())
			{
				const FVector3d& Delta = StoredDeltas[VertexID];
				ExclusiveAxisMaximum[VertexID].X = FMath::Max(
					ExclusiveAxisMaximum[VertexID].X, FMath::Abs(Delta.X));
				ExclusiveAxisMaximum[VertexID].Y = FMath::Max(
					ExclusiveAxisMaximum[VertexID].Y, FMath::Abs(Delta.Y));
				ExclusiveAxisMaximum[VertexID].Z = FMath::Max(
					ExclusiveAxisMaximum[VertexID].Z, FMath::Abs(Delta.Z));
				ExclusiveRadiusMaximum[VertexID] = FMath::Max(
					ExclusiveRadiusMaximum[VertexID], Delta.Length());
				OutMaximumMorphDisplacement = FMath::Max(
					OutMaximumMorphDisplacement, Delta.Length());
			}
			return true;
		};

		for (const FEFClothingMorphBinding& Binding : Bindings)
		{
			for (const FEFClothingMorphSample& Sample : Binding.Samples)
			{
				if (!Sample.bIdentity && !AccumulateExclusiveMorph(Sample.GarmentMorph))
				{
					return false;
				}
			}
		}
		for (const FEFClothingMorphPairCertificate& PairCertificate : PairCertificates)
		{
			for (const FEFClothingMorphPairCell& PairCell : PairCertificate.Cells)
			{
				if (!AccumulateExclusiveMorph(PairCell.GarmentMorph))
				{
					return false;
				}
			}
		}

		FVector3d MaximumAxisExpansion = FVector3d::Zero();
		double MaximumRadiusExpansion = 0.0;
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d& Clearance = ClearanceDeltas[VertexID];
			const FVector3d AxisExpansion(
				ExclusiveAxisMaximum[VertexID].X + FMath::Abs(Clearance.X) * CertifiedClearanceTierMax,
				ExclusiveAxisMaximum[VertexID].Y + FMath::Abs(Clearance.Y) * CertifiedClearanceTierMax,
				ExclusiveAxisMaximum[VertexID].Z + FMath::Abs(Clearance.Z) * CertifiedClearanceTierMax);
			MaximumAxisExpansion.X = FMath::Max(MaximumAxisExpansion.X, AxisExpansion.X);
			MaximumAxisExpansion.Y = FMath::Max(MaximumAxisExpansion.Y, AxisExpansion.Y);
			MaximumAxisExpansion.Z = FMath::Max(MaximumAxisExpansion.Z, AxisExpansion.Z);
			MaximumRadiusExpansion = FMath::Max(
				MaximumRadiusExpansion,
				ExclusiveRadiusMaximum[VertexID] + Clearance.Length() * CertifiedClearanceTierMax);
		}

		if (MaximumAxisExpansion.ContainsNaN() || !FMath::IsFinite(MaximumRadiusExpansion))
		{
			OutError = TEXT("Concurrent morph bounds contract produced a non-finite expansion.");
			return false;
		}
		OutBoxExpansion = FVector(MaximumAxisExpansion);
		OutSphereExpansion = static_cast<float>(MaximumRadiusExpansion);
		return true;
	}

	struct FSurfaceRenderTriangle
	{
		int32 LogicalTriangleIndex = INDEX_NONE;
		FIntVector RenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
	};

	struct FSurfaceRenderLOD
	{
		int32 LODIndex = INDEX_NONE;
		TArray<FVector3d> Positions;
		TArray<FVector3d> Normals;
		TArray<uint32> Indices;
		TArray<int32> MeshToImportVertexMap;
		TArray<TMap<int32, float>> SkinWeights;
		TBitArray<> ChaosDrivenVertices;
		FDynamicMesh3 Mesh;
		TArray<FSurfaceRenderTriangle> DynamicTriangles;
		FEFClothingSurfaceTopologyFingerprint Topology;
		int32 DegenerateTriangleCount = 0;
		int32 ExcludedTriangleCount = 0;
	};

	static FString BuildRenderTopologyFingerprint(
		const FSkeletalMeshLODRenderData& RenderLOD,
		const FSkeletalMeshLODModel& ImportedLOD,
		const TArray<uint32>& Indices,
		int32 LODIndex)
	{
		FMD5 Hash;
		auto Update = [&Hash](const void* Data, uint64 Size)
		{
			Hash.Update(static_cast<const uint8*>(Data), Size);
		};
		const int32 VertexCount = static_cast<int32>(RenderLOD.GetNumVertices());
		const int32 IndexCount = Indices.Num();
		const int32 SectionCount = RenderLOD.RenderSections.Num();
		Update(&LODIndex, sizeof(LODIndex));
		Update(&VertexCount, sizeof(VertexCount));
		Update(&IndexCount, sizeof(IndexCount));
		Update(&SectionCount, sizeof(SectionCount));
		if (!Indices.IsEmpty())
		{
			Update(Indices.GetData(), static_cast<uint64>(Indices.Num()) * sizeof(uint32));
		}
		if (!ImportedLOD.MeshToImportVertexMap.IsEmpty())
		{
			Update(
				ImportedLOD.MeshToImportVertexMap.GetData(),
				static_cast<uint64>(ImportedLOD.MeshToImportVertexMap.Num()) * sizeof(int32));
		}
		for (const FSkelMeshRenderSection& Section : RenderLOD.RenderSections)
		{
			Update(&Section.MaterialIndex, sizeof(Section.MaterialIndex));
			Update(&Section.BaseIndex, sizeof(Section.BaseIndex));
			Update(&Section.NumTriangles, sizeof(Section.NumTriangles));
			Update(&Section.BaseVertexIndex, sizeof(Section.BaseVertexIndex));
			Update(&Section.NumVertices, sizeof(Section.NumVertices));
			Update(&Section.bDisabled, sizeof(Section.bDisabled));
		}
		uint8 Digest[16] = {};
		Hash.Final(Digest);
		return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
	}

	static bool IsExcludedRenderSection(
		const USkeletalMesh* Mesh,
		const FSkelMeshRenderSection& Section,
		const TSet<FName>& ExcludedMaterialSlots)
	{
		if (Section.bDisabled || !Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex))
		{
			return Section.bDisabled;
		}
		const FSkeletalMaterial& Material = Mesh->GetMaterials()[Section.MaterialIndex];
		return ExcludedMaterialSlots.Contains(Material.MaterialSlotName)
			|| ExcludedMaterialSlots.Contains(Material.ImportedMaterialSlotName);
	}

	static bool BuildSurfaceRenderLOD(
		USkeletalMesh* Mesh,
		int32 LODIndex,
		const TArray<FName>& ExcludedMaterialSlots,
		FName SkinWeightProfileName,
		FSurfaceRenderLOD& OutLOD,
		FString& OutError)
	{
		OutError.Reset();
		if (!IsValid(Mesh))
		{
			OutError = TEXT("Surface binding render LOD requires a valid SkeletalMesh.");
			return false;
		}
		FSkinnedAssetCompilingManager::Get().FinishCompilation({Mesh});
		FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
		FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (!RenderData
			|| !ImportedModel
			|| !RenderData->LODRenderData.IsValidIndex(LODIndex)
			|| !ImportedModel->LODModels.IsValidIndex(LODIndex))
		{
			OutError = FString::Printf(
				TEXT("%s has no matching render/import data for LOD %d."),
				*Mesh->GetPathName(),
				LODIndex);
			return false;
		}

		const FSkeletalMeshLODRenderData& RenderLOD = RenderData->LODRenderData[LODIndex];
		const FSkeletalMeshLODModel& ImportedLOD = ImportedModel->LODModels[LODIndex];
		const int32 RenderVertexCount = static_cast<int32>(RenderLOD.GetNumVertices());
		if (RenderVertexCount <= 0
			|| ImportedLOD.MeshToImportVertexMap.Num() != RenderVertexCount)
		{
			OutError = FString::Printf(
				TEXT("%s LOD %d render/import map mismatch (%d vertices, %d map entries)."),
				*Mesh->GetPathName(),
				LODIndex,
				RenderVertexCount,
				ImportedLOD.MeshToImportVertexMap.Num());
			return false;
		}

		OutLOD.LODIndex = LODIndex;
		OutLOD.Positions.SetNum(RenderVertexCount);
		OutLOD.Normals.SetNum(RenderVertexCount);
		OutLOD.MeshToImportVertexMap = ImportedLOD.MeshToImportVertexMap;
		OutLOD.SkinWeights.SetNum(RenderVertexCount);
		OutLOD.ChaosDrivenVertices.Init(false, RenderVertexCount);
		OutLOD.Mesh.Clear();
		OutLOD.DynamicTriangles.Reset();
		OutLOD.DegenerateTriangleCount = 0;
		OutLOD.ExcludedTriangleCount = 0;
		if (static_cast<int32>(RenderLOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices())
			!= RenderVertexCount)
		{
			OutError = FString::Printf(
				TEXT("%s LOD %d tangent/render vertex count mismatch."),
				*Mesh->GetPathName(),
				LODIndex);
			return false;
		}
		for (int32 RenderVertexIndex = 0; RenderVertexIndex < RenderVertexCount; ++RenderVertexIndex)
		{
			const FVector3f Position = RenderLOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(RenderVertexIndex);
			FVector3d Normal(
				RenderLOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(RenderVertexIndex));
			if (Position.ContainsNaN() || Normal.ContainsNaN())
			{
				OutError = FString::Printf(
					TEXT("%s LOD %d contains a non-finite render position/tangent normal at vertex %d."),
					*Mesh->GetPathName(),
					LODIndex,
					RenderVertexIndex);
				return false;
			}
			// Some otherwise valid DAZ render seams contain a finite zero TangentZ.
			// Preserve that fact in the compiler model and use the oriented geometric
			// triangle normal only when an interpolated smooth normal is degenerate.
			// The runtime graph and GPU readback QA implement the same fail-safe rule;
			// the protected body asset is never rewritten to manufacture tangents.
			if (!Normal.Normalize())
			{
				Normal = FVector3d::Zero();
			}
			OutLOD.Positions[RenderVertexIndex] = FVector3d(Position);
			OutLOD.Normals[RenderVertexIndex] = Normal;
			const int32 DynamicVertexID = OutLOD.Mesh.AppendVertex(OutLOD.Positions[RenderVertexIndex]);
			if (DynamicVertexID != RenderVertexIndex)
			{
				OutError = TEXT("Dynamic render topology did not preserve exact render vertex indices.");
				return false;
			}
		}
		const FMeshDescription* MeshDescription = Mesh->GetMeshDescription(LODIndex);
		if (!MeshDescription)
		{
			OutError = FString::Printf(TEXT("%s LOD %d has no MeshDescription for skin-similarity classification."), *Mesh->GetPathName(), LODIndex);
			return false;
		}
		const FSkeletalMeshAttributesShared MeshAttributes(*MeshDescription);
		const FSkinWeightsVertexAttributesConstRef SkinWeights =
			MeshAttributes.GetVertexSkinWeights(SkinWeightProfileName);
		if (!SkinWeights.IsValid())
		{
			OutError = FString::Printf(
				TEXT("%s LOD %d has no %s skin weights for surface classification."),
				*Mesh->GetPathName(),
				LODIndex,
				SkinWeightProfileName.IsNone() ? TEXT("default") : *SkinWeightProfileName.ToString());
			return false;
		}
		for (int32 RenderVertexIndex = 0; RenderVertexIndex < RenderVertexCount; ++RenderVertexIndex)
		{
			const int32 ImportVertexIndex = OutLOD.MeshToImportVertexMap[RenderVertexIndex];
			if (!MeshDescription->Vertices().IsValid(FVertexID(ImportVertexIndex)))
			{
				OutError = FString::Printf(
					TEXT("%s LOD %d render vertex %d maps to invalid MeshDescription vertex %d."),
					*Mesh->GetPathName(),
					LODIndex,
					RenderVertexIndex,
					ImportVertexIndex);
				return false;
			}
			const FVertexBoneWeightsConst VertexWeights = SkinWeights.Get(FVertexID(ImportVertexIndex));
			for (int32 InfluenceIndex = 0; InfluenceIndex < VertexWeights.Num(); ++InfluenceIndex)
			{
				const UE::AnimationCore::FBoneWeight Influence = VertexWeights[InfluenceIndex];
				if (Influence.GetWeight() > 0.0f)
				{
					OutLOD.SkinWeights[RenderVertexIndex].Add(
						static_cast<int32>(Influence.GetBoneIndex()),
						Influence.GetWeight());
				}
			}
			if (OutLOD.SkinWeights[RenderVertexIndex].IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s LOD %d render vertex %d has no skin influences."), *Mesh->GetPathName(), LODIndex, RenderVertexIndex);
				return false;
			}
		}

		RenderLOD.MultiSizeIndexContainer.GetIndexBuffer(OutLOD.Indices);
		if (OutLOD.Indices.IsEmpty() || OutLOD.Indices.Num() % 3 != 0)
		{
			OutError = FString::Printf(TEXT("%s LOD %d has an invalid render index buffer."), *Mesh->GetPathName(), LODIndex);
			return false;
		}
		const int32 TriangleCount = OutLOD.Indices.Num() / 3;
		TBitArray<> IncludedTriangles(false, TriangleCount);
		TSet<FName> ExcludedSlotSet;
		for (const FName SlotName : ExcludedMaterialSlots)
		{
			ExcludedSlotSet.Add(SlotName);
		}
		for (const FSkelMeshRenderSection& Section : RenderLOD.RenderSections)
		{
			if (Section.BaseIndex % 3 != 0
				|| Section.BaseIndex + Section.NumTriangles * 3 > static_cast<uint32>(OutLOD.Indices.Num()))
			{
				OutError = FString::Printf(TEXT("%s LOD %d has an invalid render section range."), *Mesh->GetPathName(), LODIndex);
				return false;
			}
			const bool bExcluded = IsExcludedRenderSection(Mesh, Section, ExcludedSlotSet);
			if (Section.HasClothingData())
			{
				const int32 SectionVertexEnd = FMath::Min(
					RenderVertexCount,
					static_cast<int32>(Section.BaseVertexIndex + Section.NumVertices));
				for (int32 RenderVertexIndex = static_cast<int32>(Section.BaseVertexIndex);
					RenderVertexIndex < SectionVertexEnd;
					++RenderVertexIndex)
				{
					OutLOD.ChaosDrivenVertices[RenderVertexIndex] = true;
				}
			}
			for (uint32 LocalTriangleIndex = 0; LocalTriangleIndex < Section.NumTriangles; ++LocalTriangleIndex)
			{
				const int32 LogicalTriangleIndex = static_cast<int32>(Section.BaseIndex / 3 + LocalTriangleIndex);
				if (bExcluded)
				{
					++OutLOD.ExcludedTriangleCount;
				}
				else
				{
					IncludedTriangles[LogicalTriangleIndex] = true;
				}
			}
		}

		OutLOD.DynamicTriangles.SetNum(TriangleCount);
		for (int32 LogicalTriangleIndex = 0; LogicalTriangleIndex < TriangleCount; ++LogicalTriangleIndex)
		{
			if (!IncludedTriangles[LogicalTriangleIndex])
			{
				continue;
			}
			const int32 BaseIndex = LogicalTriangleIndex * 3;
			const FIntVector RenderVertices(
				static_cast<int32>(OutLOD.Indices[BaseIndex]),
				static_cast<int32>(OutLOD.Indices[BaseIndex + 1]),
				static_cast<int32>(OutLOD.Indices[BaseIndex + 2]));
			if (!OutLOD.Positions.IsValidIndex(RenderVertices.X)
				|| !OutLOD.Positions.IsValidIndex(RenderVertices.Y)
				|| !OutLOD.Positions.IsValidIndex(RenderVertices.Z))
			{
				OutError = FString::Printf(TEXT("%s LOD %d triangle %d references an invalid render vertex."), *Mesh->GetPathName(), LODIndex, LogicalTriangleIndex);
				return false;
			}
			const FVector3d& A = OutLOD.Positions[RenderVertices.X];
			const FVector3d& B = OutLOD.Positions[RenderVertices.Y];
			const FVector3d& C = OutLOD.Positions[RenderVertices.Z];
			FVector3d StableTangent = B - A;
			FVector3d StableNormal = StableTangent.Cross(C - A);
			FVector3d StableBitangent;
			bool bStableFrame = StableTangent.Normalize() && StableNormal.Normalize();
			if (bStableFrame)
			{
				StableBitangent = StableNormal.Cross(StableTangent);
				bStableFrame = StableBitangent.Normalize();
			}
			if (RenderVertices.X == RenderVertices.Y
				|| RenderVertices.Y == RenderVertices.Z
				|| RenderVertices.Z == RenderVertices.X
				|| !bStableFrame)
			{
				++OutLOD.DegenerateTriangleCount;
				continue;
			}
			const int32 DynamicTriangleID = OutLOD.Mesh.AppendTriangle(
				FIndex3i(RenderVertices.X, RenderVertices.Y, RenderVertices.Z));
			if (DynamicTriangleID < 0)
			{
				OutError = FString::Printf(TEXT("%s LOD %d could not append render triangle %d."), *Mesh->GetPathName(), LODIndex, LogicalTriangleIndex);
				return false;
			}
			if (!OutLOD.DynamicTriangles.IsValidIndex(DynamicTriangleID))
			{
				OutLOD.DynamicTriangles.SetNum(DynamicTriangleID + 1);
			}
			OutLOD.DynamicTriangles[DynamicTriangleID].LogicalTriangleIndex = LogicalTriangleIndex;
			OutLOD.DynamicTriangles[DynamicTriangleID].RenderVertexIndices = RenderVertices;
		}

		if (OutLOD.Mesh.TriangleCount() <= 0)
		{
			OutError = FString::Printf(TEXT("%s LOD %d exposes no usable surface triangles."), *Mesh->GetPathName(), LODIndex);
			return false;
		}
		OutLOD.Topology.LODIndex = LODIndex;
		OutLOD.Topology.RenderVertexCount = RenderVertexCount;
		OutLOD.Topology.RenderIndexCount = OutLOD.Indices.Num();
		OutLOD.Topology.TriangleCount = TriangleCount;
		OutLOD.Topology.SectionCount = RenderLOD.RenderSections.Num();
		OutLOD.Topology.TopologyFingerprint = BuildRenderTopologyFingerprint(RenderLOD, ImportedLOD, OutLOD.Indices, LODIndex);
		OutLOD.Topology.ContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(Mesh);
		return !OutLOD.Topology.TopologyFingerprint.IsEmpty()
			&& !OutLOD.Topology.ContentFingerprint.IsEmpty();
	}

	/**
	 * Builds only the render triangles deliberately removed from the active body
	 * surface by the catalog. The vertices remain in exact body render-index space,
	 * so proximity is derived from authored anatomy geometry rather than garment
	 * names, world coordinates or a protected-mesh edit.
	 */
	static bool BuildExcludedAnatomySurface(
		const FSurfaceRenderLOD& FullBodyLOD,
		const FSurfaceRenderLOD& ActiveBodyLOD,
		FDynamicMesh3& OutExcludedMesh,
		int32& OutExcludedTriangleCount,
		FString& OutError)
	{
		OutExcludedMesh.Clear();
		OutExcludedTriangleCount = 0;
		if (FullBodyLOD.Positions.Num() != ActiveBodyLOD.Positions.Num()
			|| FullBodyLOD.Topology.TopologyFingerprint
				!= ActiveBodyLOD.Topology.TopologyFingerprint)
		{
			OutError = TEXT("Excluded anatomy surface requires identical full/filtered render topology.");
			return false;
		}

		for (const FVector3d& Position : FullBodyLOD.Positions)
		{
			OutExcludedMesh.AppendVertex(Position);
		}
		TSet<int32> ActiveLogicalTriangles;
		for (int32 DynamicTriangleID : ActiveBodyLOD.Mesh.TriangleIndicesItr())
		{
			if (ActiveBodyLOD.DynamicTriangles.IsValidIndex(DynamicTriangleID))
			{
				const int32 LogicalTriangleIndex =
					ActiveBodyLOD.DynamicTriangles[DynamicTriangleID].LogicalTriangleIndex;
				if (LogicalTriangleIndex != INDEX_NONE)
				{
					ActiveLogicalTriangles.Add(LogicalTriangleIndex);
				}
			}
		}
		for (int32 DynamicTriangleID : FullBodyLOD.Mesh.TriangleIndicesItr())
		{
			if (!FullBodyLOD.DynamicTriangles.IsValidIndex(DynamicTriangleID))
			{
				continue;
			}
			const FSurfaceRenderTriangle& Triangle =
				FullBodyLOD.DynamicTriangles[DynamicTriangleID];
			if (Triangle.LogicalTriangleIndex == INDEX_NONE
				|| ActiveLogicalTriangles.Contains(Triangle.LogicalTriangleIndex))
			{
				continue;
			}
			if (OutExcludedMesh.AppendTriangle(FIndex3i(
				Triangle.RenderVertexIndices.X,
				Triangle.RenderVertexIndices.Y,
				Triangle.RenderVertexIndices.Z)) < 0)
			{
				OutError = FString::Printf(
					TEXT("Excluded anatomy surface could not append body triangle %d."),
					Triangle.LogicalTriangleIndex);
				return false;
			}
			++OutExcludedTriangleCount;
		}
		if (OutExcludedTriangleCount <= 0)
		{
			OutError = FString::Printf(
				TEXT("Body LOD %d has no usable catalog-excluded anatomy triangles."),
				ActiveBodyLOD.LODIndex);
			return false;
		}
		return true;
	}

	static bool BuildImportVertexClearanceDeltas(
		const FDynamicMesh3& GarmentMesh,
		const TArray<FVector3d>& ClearanceDeltas,
		int32 ImportVertexCount,
		TArray<FVector3d>& OutImportDeltas,
		FString& OutError)
	{
		OutImportDeltas.Init(FVector3d::Zero(), ImportVertexCount);
		TBitArray<> HasDelta(false, ImportVertexCount);
		const FNonManifoldMappingSupport Mapping(GarmentMesh);
		for (int32 DynamicVertexID : GarmentMesh.VertexIndicesItr())
		{
			const int32 ImportVertexID = Mapping.GetOriginalNonManifoldVertexID(DynamicVertexID);
			if (!OutImportDeltas.IsValidIndex(ImportVertexID) || !ClearanceDeltas.IsValidIndex(DynamicVertexID))
			{
				OutError = TEXT("Clearance render mapping references an invalid import/dynamic vertex.");
				return false;
			}
			const FVector3d& Delta = ClearanceDeltas[DynamicVertexID];
			if (HasDelta[ImportVertexID]
				&& (OutImportDeltas[ImportVertexID] - Delta).SquaredLength() > FMath::Square(1.e-5))
			{
				OutError = FString::Printf(TEXT("Clearance render mapping found unreconciled split import vertex %d."), ImportVertexID);
				return false;
			}
			OutImportDeltas[ImportVertexID] = Delta;
			HasDelta[ImportVertexID] = true;
		}
		for (int32 ImportVertexIndex = 0; ImportVertexIndex < HasDelta.Num(); ++ImportVertexIndex)
		{
			if (!HasDelta[ImportVertexIndex])
			{
				OutError = FString::Printf(
					TEXT("Clearance render mapping did not cover import vertex %d."),
					ImportVertexIndex);
				return false;
			}
		}
		return true;
	}

	static bool BuildSurfaceTangentFrame(
		const FSurfaceRenderLOD& SurfaceLOD,
		const FIntVector& RenderVertexIndices,
		const FVector3d& Barycentrics,
		FVector3d& OutTangent,
		FVector3d& OutBitangent,
		FVector3d& OutNormal)
	{
		if (!SurfaceLOD.Positions.IsValidIndex(RenderVertexIndices.X)
			|| !SurfaceLOD.Positions.IsValidIndex(RenderVertexIndices.Y)
			|| !SurfaceLOD.Positions.IsValidIndex(RenderVertexIndices.Z))
		{
			return false;
		}

		const FVector3d& A = SurfaceLOD.Positions[RenderVertexIndices.X];
		const FVector3d& B = SurfaceLOD.Positions[RenderVertexIndices.Y];
		const FVector3d& C = SurfaceLOD.Positions[RenderVertexIndices.Z];
		const FVector3d Edge01 = B - A;
		const FVector3d Edge02 = C - A;
		OutNormal = Edge01.Cross(Edge02);
		if (!OutNormal.Normalize())
		{
			return false;
		}
		// Schema 4 deliberately derives the surface frame only from the oriented
		// render triangle. Optimus may expose final Position while independently
		// falling back to static TangentZ for a secondary component read; a
		// position-only geometric frame therefore keeps compile, runtime and QA on
		// one deterministic post-animation contract, including DAZ seams.
		OutTangent = Edge01 - OutNormal * Edge01.Dot(OutNormal);
		if (!OutTangent.Normalize())
		{
			OutTangent = Edge02 - OutNormal * Edge02.Dot(OutNormal);
		}
		if (!OutTangent.Normalize())
		{
			return false;
		}
		OutBitangent = OutNormal.Cross(OutTangent);
		if (!OutBitangent.Normalize())
		{
			return false;
		}
		OutTangent = OutBitangent.Cross(OutNormal);
		return OutTangent.Normalize();
	}

	static bool FindSurfaceAnchor(
		const FSurfaceRenderLOD& BodyLOD,
		const FDynamicMeshAABBTree3& BodySpatial,
		const FVector3d& QueryPosition,
		int32& OutDynamicTriangleID,
		FVector3d& OutClosestPoint,
		FVector3d& OutBarycentrics,
		FVector3d& OutNormal,
		double& OutDistanceSquared,
		const TSet<int32>* RejectedTriangles = nullptr)
	{
		IMeshSpatial::FQueryOptions QueryOptions;
		if (RejectedTriangles)
		{
			QueryOptions.TriangleFilterF = [RejectedTriangles](int32 TriangleID)
			{
				return !RejectedTriangles->Contains(TriangleID);
			};
		}
		OutDistanceSquared = TNumericLimits<double>::Max();
		OutDynamicTriangleID = BodySpatial.FindNearestTriangle(QueryPosition, OutDistanceSquared, QueryOptions);
		if (OutDynamicTriangleID == IndexConstants::InvalidID
			|| !BodyLOD.DynamicTriangles.IsValidIndex(OutDynamicTriangleID)
			|| BodyLOD.DynamicTriangles[OutDynamicTriangleID].LogicalTriangleIndex == INDEX_NONE
			|| !FMath::IsFinite(OutDistanceSquared))
		{
			return false;
		}
		const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
			BodyLOD.Mesh,
			OutDynamicTriangleID,
			QueryPosition);
		OutClosestPoint = Query.ClosestTrianglePoint;
		OutBarycentrics = Query.TriangleBaryCoords;
		const FIndex3i BodyTriangle = BodyLOD.Mesh.GetTriangle(OutDynamicTriangleID);
		OutNormal = (BodyLOD.Mesh.GetVertex(BodyTriangle.B) - BodyLOD.Mesh.GetVertex(BodyTriangle.A)).Cross(
			BodyLOD.Mesh.GetVertex(BodyTriangle.C) - BodyLOD.Mesh.GetVertex(BodyTriangle.A));
		const double Sum = OutBarycentrics.X + OutBarycentrics.Y + OutBarycentrics.Z;
		return !OutClosestPoint.ContainsNaN()
			&& !OutBarycentrics.ContainsNaN()
			&& !OutNormal.ContainsNaN()
			&& OutNormal.Normalize()
			&& FMath::IsNearlyEqual(Sum, 1.0, 1.e-4);
	}

	static bool FindBestAnimatedSurfaceAnchor(
		const FSurfaceRenderLOD& BodyLOD,
		const FDynamicMeshAABBTree3& BodySpatial,
		const FVector3d& QueryPosition,
		FIntVector& OutOrientedRenderVertexIndices,
		FVector3d& OutClosestPoint,
		FVector3d& OutOrientedBarycentrics,
		FVector3d& OutTangent,
		FVector3d& OutBitangent,
		FVector3d& OutNormal,
		double& OutDistanceSquared)
	{
		// Render seams/material splits can place several triangles at exactly the
		// same skin surface with different winding. Evaluate only a bounded,
		// spatially-equivalent neighborhood and persist the oriented geometric
		// frame that best represents the exterior clearance.
		// This is topology/name agnostic and deliberately remains in the strict
		// nearest-surface neighborhood. A wider branch search can jump across DAZ
		// concavities and make the runtime face constraints geometrically invalid.
		static constexpr double EquivalentSurfaceToleranceCm = 0.05;
		static constexpr int32 MaximumEquivalentAnchorCandidates = 12;
		TSet<int32> RejectedTriangles;
		double NearestDistanceCm = TNumericLimits<double>::Max();
		double BestSignedGapCm = -TNumericLimits<double>::Max();
		bool bFound = false;
		OutDistanceSquared = TNumericLimits<double>::Max();

		for (int32 CandidateIndex = 0;
			CandidateIndex < MaximumEquivalentAnchorCandidates;
			++CandidateIndex)
		{
			int32 DynamicTriangleID = INDEX_NONE;
			FVector3d ClosestPoint;
			FVector3d Barycentrics;
			FVector3d FlatNormal;
			double DistanceSquared = TNumericLimits<double>::Max();
			if (!FindSurfaceAnchor(
				BodyLOD,
				BodySpatial,
				QueryPosition,
				DynamicTriangleID,
				ClosestPoint,
				Barycentrics,
				FlatNormal,
				DistanceSquared,
				&RejectedTriangles))
			{
				break;
			}
			RejectedTriangles.Add(DynamicTriangleID);
			const double DistanceCm = FMath::Sqrt(FMath::Max(0.0, DistanceSquared));
			if (CandidateIndex == 0)
			{
				NearestDistanceCm = DistanceCm;
			}
			else if (DistanceCm > NearestDistanceCm + EquivalentSurfaceToleranceCm)
			{
				break;
			}

			if (!BodyLOD.DynamicTriangles.IsValidIndex(DynamicTriangleID))
			{
				continue;
			}
			FIntVector OrientedIndices =
				BodyLOD.DynamicTriangles[DynamicTriangleID].RenderVertexIndices;
			FVector3d OrientedBarycentrics = Barycentrics;
			FVector3d Tangent;
			FVector3d Bitangent;
			FVector3d AnimatedNormal;
			if (!BuildSurfaceTangentFrame(
				BodyLOD,
				OrientedIndices,
				OrientedBarycentrics,
				Tangent,
				Bitangent,
				AnimatedNormal))
			{
				continue;
			}

			const FVector3d RestOffset = QueryPosition - ClosestPoint;
			if (RestOffset.Dot(AnimatedNormal) < 0.0)
			{
				Swap(OrientedIndices.Y, OrientedIndices.Z);
				Swap(OrientedBarycentrics.Y, OrientedBarycentrics.Z);
				if (!BuildSurfaceTangentFrame(
					BodyLOD,
					OrientedIndices,
					OrientedBarycentrics,
					Tangent,
					Bitangent,
					AnimatedNormal))
				{
					continue;
				}
			}
			const double SignedGapCm = RestOffset.Dot(AnimatedNormal);
			if (!FMath::IsFinite(SignedGapCm))
			{
				continue;
			}
			const bool bBetterBranch = SignedGapCm > BestSignedGapCm + 1.e-9
					|| (FMath::IsNearlyEqual(SignedGapCm, BestSignedGapCm, 1.e-9)
						&& DistanceSquared < OutDistanceSquared);
			if (!bFound || bBetterBranch)
			{
				bFound = true;
				BestSignedGapCm = SignedGapCm;
				OutOrientedRenderVertexIndices = OrientedIndices;
				OutClosestPoint = ClosestPoint;
				OutOrientedBarycentrics = OrientedBarycentrics;
				OutTangent = Tangent;
				OutBitangent = Bitangent;
				OutNormal = AnimatedNormal;
				OutDistanceSquared = DistanceSquared;
			}
		}
		return bFound;
	}

	static EEFClothingSurfaceVertexMode ClassifySurfaceVertex(
		EEFClothingFitPolicy Policy,
		double SignedGap,
		double TargetClearance,
		double NormalAlignment,
		double BoneWeightSimilarity,
		bool bChaosDriven)
	{
		switch (Policy)
		{
		case EEFClothingFitPolicy::Tight:
			return EEFClothingSurfaceVertexMode::SurfaceFollow;
		case EEFClothingFitPolicy::Hybrid:
			return EEFClothingSurfaceVertexMode::Hybrid;
		case EEFClothingFitPolicy::Loose:
		case EEFClothingFitPolicy::Rigid:
			return EEFClothingSurfaceVertexMode::CollisionOnly;
	default:
			break;
		}
		if (!bChaosDriven
			&& SignedGap <= TargetClearance + 0.40
			&& NormalAlignment >= 0.35
			&& BoneWeightSimilarity >= 0.65)
		{
			return EEFClothingSurfaceVertexMode::SurfaceFollow;
		}
		if (SignedGap <= TargetClearance + 1.50
			&& NormalAlignment >= 0.05
			&& BoneWeightSimilarity >= 0.25)
		{
			return EEFClothingSurfaceVertexMode::Hybrid;
		}
		return EEFClothingSurfaceVertexMode::CollisionOnly;
	}

	static double ComputeBoneWeightSimilarity(
		const TMap<int32, float>& GarmentWeights,
		const FSurfaceRenderLOD& BodyLOD,
		const FIntVector& BodyRenderVertexIndices,
		const FVector3d& BodyBarycentrics)
	{
		TMap<int32, double> InterpolatedBodyWeights;
		const int32 BodyVertices[3] = {
			BodyRenderVertexIndices.X,
			BodyRenderVertexIndices.Y,
			BodyRenderVertexIndices.Z};
		const double Barycentrics[3] = {
			BodyBarycentrics.X,
			BodyBarycentrics.Y,
			BodyBarycentrics.Z};
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			if (!BodyLOD.SkinWeights.IsValidIndex(BodyVertices[CornerIndex]))
			{
				return 0.0;
			}
			for (const TPair<int32, float>& Influence : BodyLOD.SkinWeights[BodyVertices[CornerIndex]])
			{
				InterpolatedBodyWeights.FindOrAdd(Influence.Key)
					+= Barycentrics[CornerIndex] * static_cast<double>(Influence.Value);
			}
		}
		double Similarity = 0.0;
		for (const TPair<int32, float>& Influence : GarmentWeights)
		{
			Similarity += FMath::Min(
				static_cast<double>(Influence.Value),
				InterpolatedBodyWeights.FindRef(Influence.Key));
		}
		return FMath::Clamp(Similarity, 0.0, 1.0);
	}

	static float SurfaceFollowWeight(
		EEFClothingSurfaceVertexMode Mode,
		double SignedGap,
		double TargetClearance)
	{
		if (Mode == EEFClothingSurfaceVertexMode::SurfaceFollow)
		{
			return static_cast<float>(FMath::Clamp(1.0 - FMath::Max(0.0, SignedGap - TargetClearance) / 1.5, 0.70, 1.0));
		}
		if (Mode == EEFClothingSurfaceVertexMode::Hybrid)
		{
			return static_cast<float>(FMath::Clamp(0.65 - FMath::Max(0.0, SignedGap - TargetClearance) / 4.0, 0.15, 0.65));
		}
		return 0.0f;
	}

	static bool BuildSurfaceLODPairBinding(
		const FSurfaceRenderLOD& GarmentLOD,
		const FSurfaceRenderLOD& BodyLOD,
		const FDynamicMesh3* ExcludedAnatomyMesh,
		const FEFClothingGarmentRow& CatalogRow,
		float BaseClearanceCm,
		float CompiledReserveCm,
		float DefaultMaximumCorrectionCm,
		FEFClothingSurfaceLODPairBinding& OutPair,
		FString& OutError,
		const bool bAllowCorrectableInitialPenetration = false)
	{
		OutError.Reset();
		OutPair = FEFClothingSurfaceLODPairBinding();
		OutPair.GarmentTopology = GarmentLOD.Topology;
		OutPair.BodyTopology = BodyLOD.Topology;
		OutPair.BaseClearanceCm = BaseClearanceCm;
		OutPair.CompiledReserveCm = CompiledReserveCm;
		const float TargetClearanceCm = FMath::Max(
			BaseClearanceCm,
			EFClothingMorphV26::IsAutomaticCentimeterValue(CatalogRow.FabricClearanceCm)
				? BaseClearanceCm
				: CatalogRow.FabricClearanceCm) + CompiledReserveCm;
		const float MaximumCorrectionCm = EFClothingMorphV26::IsAutomaticCentimeterValue(CatalogRow.MaximumCorrectionCm)
			? DefaultMaximumCorrectionCm
			: CatalogRow.MaximumCorrectionCm;
		if (!FMath::IsFinite(TargetClearanceCm)
			|| !FMath::IsFinite(MaximumCorrectionCm)
			|| MaximumCorrectionCm <= 0.0f)
		{
			OutError = TEXT("Surface binding clearance/correction policy is invalid.");
			return false;
		}

		FDynamicMeshAABBTree3 BodySpatial(&BodyLOD.Mesh, true);
		TUniquePtr<FDynamicMeshAABBTree3> ExcludedAnatomySpatial;
		if (ExcludedAnatomyMesh)
		{
			if (ExcludedAnatomyMesh->TriangleCount() <= 0)
			{
				OutError = TEXT("PreserveUpstream was requested with an empty excluded-anatomy surface.");
				return false;
			}
			ExcludedAnatomySpatial = MakeUnique<FDynamicMeshAABBTree3>(ExcludedAnatomyMesh, true);
		}
		FMeshNormals GarmentNormals(&GarmentLOD.Mesh);
		GarmentNormals.ComputeVertexNormals();
		const int32 GarmentVertexCount = GarmentLOD.Positions.Num();
		TArray<TSet<int32>> NeighborSets;
		NeighborSets.SetNum(GarmentVertexCount);
		for (int32 TriangleID : GarmentLOD.Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = GarmentLOD.Mesh.GetTriangle(TriangleID);
			NeighborSets[Triangle.A].Add(Triangle.B);
			NeighborSets[Triangle.A].Add(Triangle.C);
			NeighborSets[Triangle.B].Add(Triangle.A);
			NeighborSets[Triangle.B].Add(Triangle.C);
			NeighborSets[Triangle.C].Add(Triangle.A);
			NeighborSets[Triangle.C].Add(Triangle.B);
		}

		// Detect true garment openings in import-vertex space. Render seams may
		// duplicate vertices for UVs/materials, so treating DynamicMesh boundaries
		// directly would create false borders. An import edge with one incident face
		// is a real cuff/waist/leg opening; two rings receive a small extra reserve.
		TMap<uint64, int32> ImportEdgeIncidence;
		auto AddImportEdge = [&ImportEdgeIncidence](int32 A, int32 B)
		{
			if (A == B || A < 0 || B < 0)
			{
				return;
			}
			const uint32 Minimum = static_cast<uint32>(FMath::Min(A, B));
			const uint32 Maximum = static_cast<uint32>(FMath::Max(A, B));
			const uint64 Key = (static_cast<uint64>(Minimum) << 32) | Maximum;
			++ImportEdgeIncidence.FindOrAdd(Key);
		};
		for (int32 TriangleID : GarmentLOD.Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = GarmentLOD.Mesh.GetTriangle(TriangleID);
			const int32 ImportA = GarmentLOD.MeshToImportVertexMap[Triangle.A];
			const int32 ImportB = GarmentLOD.MeshToImportVertexMap[Triangle.B];
			const int32 ImportC = GarmentLOD.MeshToImportVertexMap[Triangle.C];
			AddImportEdge(ImportA, ImportB);
			AddImportEdge(ImportB, ImportC);
			AddImportEdge(ImportC, ImportA);
		}
		TSet<int32> BoundaryImportVertices;
		for (const TPair<uint64, int32>& Edge : ImportEdgeIncidence)
		{
			if (Edge.Value == 1)
			{
				BoundaryImportVertices.Add(static_cast<int32>(Edge.Key >> 32));
				BoundaryImportVertices.Add(static_cast<int32>(Edge.Key & 0xffffffffu));
			}
		}
		int32 ThicknessShellPreImportVertexCount = 0;
		TArray<TArray<int32>> ThicknessShellRenderVerticesByImportID;
		TArray<int32> ThicknessShellAuthoritativeInnerBySourceOrdinal;
		if (CatalogRow.bCreateThicknessShell)
		{
			int32 MaximumImportVertexID = INDEX_NONE;
			TSet<int32> UniqueImportVertexIDs;
			for (const int32 ImportVertexID : GarmentLOD.MeshToImportVertexMap)
			{
				if (ImportVertexID < 0)
				{
					OutError = TEXT("Thickness-shell render topology contains an invalid import vertex ID.");
					return false;
				}
				MaximumImportVertexID = FMath::Max(MaximumImportVertexID, ImportVertexID);
				UniqueImportVertexIDs.Add(ImportVertexID);
			}
			const int32 ImportVertexCount = MaximumImportVertexID + 1;
			if (ImportVertexCount <= 0
				|| (ImportVertexCount % 2) != 0
				|| UniqueImportVertexIDs.Num() != ImportVertexCount)
			{
				OutError = FString::Printf(
					TEXT("Thickness-shell render topology does not preserve compact paired import IDs (unique=%d range=%d)."),
					UniqueImportVertexIDs.Num(),
					ImportVertexCount);
				return false;
			}
			ThicknessShellPreImportVertexCount = ImportVertexCount / 2;
			ThicknessShellRenderVerticesByImportID.SetNum(ImportVertexCount);
			ThicknessShellAuthoritativeInnerBySourceOrdinal.Init(
				INDEX_NONE,
				ThicknessShellPreImportVertexCount);
			for (int32 RenderVertexIndex = 0;
				RenderVertexIndex < GarmentLOD.MeshToImportVertexMap.Num();
				++RenderVertexIndex)
			{
				ThicknessShellRenderVerticesByImportID[
					GarmentLOD.MeshToImportVertexMap[RenderVertexIndex]].Add(RenderVertexIndex);
			}
			int32 WallTriangleCount = 0;
			for (const int32 TriangleID : GarmentLOD.Mesh.TriangleIndicesItr())
			{
				const FIndex3i Triangle = GarmentLOD.Mesh.GetTriangle(TriangleID);
				const int32 ImportIDs[3] = {
					GarmentLOD.MeshToImportVertexMap[Triangle.A],
					GarmentLOD.MeshToImportVertexMap[Triangle.B],
					GarmentLOD.MeshToImportVertexMap[Triangle.C]};
				const bool bHasOuter = ImportIDs[0] < ThicknessShellPreImportVertexCount
					|| ImportIDs[1] < ThicknessShellPreImportVertexCount
					|| ImportIDs[2] < ThicknessShellPreImportVertexCount;
				const bool bHasInner = ImportIDs[0] >= ThicknessShellPreImportVertexCount
					|| ImportIDs[1] >= ThicknessShellPreImportVertexCount
					|| ImportIDs[2] >= ThicknessShellPreImportVertexCount;
				if (bHasOuter && bHasInner)
				{
					++WallTriangleCount;
					BoundaryImportVertices.Add(ImportIDs[0]);
					BoundaryImportVertices.Add(ImportIDs[1]);
					BoundaryImportVertices.Add(ImportIDs[2]);
				}
			}
			if (WallTriangleCount <= 0 || BoundaryImportVertices.IsEmpty())
			{
				OutError = TEXT("Thickness-shell binding could not recover any stitched border triangles.");
				return false;
			}
		}
		TBitArray<> BoundaryRiskVertices(false, GarmentVertexCount);
		for (int32 VertexIndex = 0; VertexIndex < GarmentVertexCount; ++VertexIndex)
		{
			BoundaryRiskVertices[VertexIndex] = BoundaryImportVertices.Contains(
				GarmentLOD.MeshToImportVertexMap[VertexIndex]);
		}
		for (int32 RingIndex = 0; RingIndex < 2; ++RingIndex)
		{
			TBitArray<> ExpandedBoundaryRisk = BoundaryRiskVertices;
			for (int32 VertexIndex = 0; VertexIndex < GarmentVertexCount; ++VertexIndex)
			{
				if (!BoundaryRiskVertices[VertexIndex])
				{
					continue;
				}
				for (const int32 NeighborIndex : NeighborSets[VertexIndex])
				{
					ExpandedBoundaryRisk[NeighborIndex] = true;
				}
			}
			BoundaryRiskVertices = MoveTemp(ExpandedBoundaryRisk);
		}

		OutPair.VertexBindings.SetNum(GarmentVertexCount);
		double TotalAnchorError = 0.0;
		double MinimumRestGap = TNumericLimits<double>::Max();
		for (int32 GarmentVertexIndex = 0; GarmentVertexIndex < GarmentVertexCount; ++GarmentVertexIndex)
		{
			const FVector3d& GarmentPosition = GarmentLOD.Positions[GarmentVertexIndex];
			FIntVector OrientedBodyRenderVertexIndices;
			FVector3d ClosestPoint;
			FVector3d OrientedBarycentrics;
			FVector3d Tangent;
			FVector3d Bitangent;
			FVector3d BodyNormal;
			double DistanceSquared = TNumericLimits<double>::Max();
			if (!FindBestAnimatedSurfaceAnchor(
				BodyLOD,
				BodySpatial,
				GarmentPosition,
				OrientedBodyRenderVertexIndices,
				ClosestPoint,
				OrientedBarycentrics,
				Tangent,
				Bitangent,
				BodyNormal,
				DistanceSquared))
			{
				++OutPair.Metrics.InvalidAnchorCount;
				OutError = FString::Printf(TEXT("Surface binding could not anchor garment render vertex %d."), GarmentVertexIndex);
				return false;
			}
			const FVector3d RestOffset = GarmentPosition - ClosestPoint;
			const double SignedGap = RestOffset.Dot(BodyNormal);
			bool bPreserveUpstream = false;
			if (ExcludedAnatomySpatial)
			{
				double ExcludedDistanceSquared = TNumericLimits<double>::Max();
				const int32 ExcludedTriangleID = ExcludedAnatomySpatial->FindNearestTriangle(
					GarmentPosition,
					ExcludedDistanceSquared);
				if (ExcludedTriangleID != IndexConstants::InvalidID
					&& FMath::IsFinite(ExcludedDistanceSquared))
				{
					const double ExcludedDistanceCm = FMath::Sqrt(
						FMath::Max(0.0, ExcludedDistanceSquared));
					// Preserve a compact geometric envelope around the exact optional
					// anatomy section removed by the catalog. This deliberately ignores
					// how close the adjacent base skin is: the central crotch is ambiguous
					// precisely because both surfaces coexist there. A hard percentage
					// gate below prevents this local domain from reaching glute/thigh.
					constexpr double MaximumExcludedAnatomyDistanceCm = 5.0;
					bPreserveUpstream = ExcludedDistanceCm
						<= MaximumExcludedAnatomyDistanceCm;
				}
			}
			const FVector3d GarmentNormal = GarmentNormals[GarmentVertexIndex];
			const double NormalAlignment = FMath::Abs(GarmentNormal.Dot(BodyNormal));
			const double BoneWeightSimilarity = ComputeBoneWeightSimilarity(
				GarmentLOD.SkinWeights[GarmentVertexIndex],
				BodyLOD,
				OrientedBodyRenderVertexIndices,
				OrientedBarycentrics);
			const bool bChaosDriven = GarmentVertexIndex >= 0
				&& GarmentVertexIndex < GarmentLOD.ChaosDrivenVertices.Num()
				&& GarmentLOD.ChaosDrivenVertices[GarmentVertexIndex];
			FEFClothingSurfaceVertexBinding& VertexBinding = OutPair.VertexBindings[GarmentVertexIndex];
			VertexBinding.GarmentRenderVertexIndex = GarmentVertexIndex;
			VertexBinding.BodyRenderVertexIndices = OrientedBodyRenderVertexIndices;
			VertexBinding.BodyBarycentrics = FVector3f(OrientedBarycentrics);
			VertexBinding.RestTangentFrameOffsetCm = FVector3f(
				static_cast<float>(RestOffset.Dot(Tangent)),
				static_cast<float>(RestOffset.Dot(Bitangent)),
				static_cast<float>(SignedGap));
			VertexBinding.RestSignedGapCm = static_cast<float>(SignedGap);
			VertexBinding.TargetClearanceCm = TargetClearanceCm;
			VertexBinding.MaximumCorrectionCm = MaximumCorrectionCm;
			VertexBinding.Mode = bPreserveUpstream
				? EEFClothingSurfaceVertexMode::PreserveUpstream
				: ClassifySurfaceVertex(
					CatalogRow.FitPolicy,
					SignedGap,
					TargetClearanceCm,
					NormalAlignment,
					BoneWeightSimilarity,
					bChaosDriven);
			VertexBinding.FollowWeight = SurfaceFollowWeight(VertexBinding.Mode, SignedGap, TargetClearanceCm);

			TArray<int32> Neighbors = NeighborSets[GarmentVertexIndex].Array();
			Neighbors.Sort();
			VertexBinding.NeighborRange.Offset = OutPair.NeighborRenderVertexIndices.Num();
			VertexBinding.NeighborRange.Count = Neighbors.Num();
			OutPair.NeighborRenderVertexIndices.Append(Neighbors);

			VertexBinding.CandidateRange.Offset = OutPair.CandidateTriangles.Num();
			TSet<int32> RejectedCandidateTriangles;
			constexpr int32 CandidateCountPerVertex = 4;
			for (int32 CandidateIndex = 0; CandidateIndex < CandidateCountPerVertex; ++CandidateIndex)
			{
				int32 CandidateDynamicTriangleID = INDEX_NONE;
				FVector3d CandidateClosestPoint;
				FVector3d CandidateBarycentrics;
				FVector3d CandidateNormal;
				double CandidateDistanceSquared = TNumericLimits<double>::Max();
				if (!FindSurfaceAnchor(
					BodyLOD,
					BodySpatial,
					GarmentPosition,
					CandidateDynamicTriangleID,
					CandidateClosestPoint,
					CandidateBarycentrics,
					CandidateNormal,
					CandidateDistanceSquared,
					&RejectedCandidateTriangles))
				{
					break;
				}
				RejectedCandidateTriangles.Add(CandidateDynamicTriangleID);
				const FSurfaceRenderTriangle& CandidateTriangle = BodyLOD.DynamicTriangles[CandidateDynamicTriangleID];
				FEFClothingSurfaceCandidateTriangle& Candidate = OutPair.CandidateTriangles.AddDefaulted_GetRef();
				Candidate.BodyTriangleIndex = CandidateTriangle.LogicalTriangleIndex;
				Candidate.BodyRenderVertexIndices = CandidateTriangle.RenderVertexIndices;
				if ((GarmentPosition - CandidateClosestPoint).Dot(CandidateNormal) < 0.0)
				{
					Swap(Candidate.BodyRenderVertexIndices.Y, Candidate.BodyRenderVertexIndices.Z);
				}
				Candidate.RestDistanceCm = static_cast<float>(FMath::Sqrt(FMath::Max(0.0, CandidateDistanceSquared)));
			}
			VertexBinding.CandidateRange.Count = OutPair.CandidateTriangles.Num() - VertexBinding.CandidateRange.Offset;
			if (VertexBinding.CandidateRange.Count <= 0)
			{
				++OutPair.Metrics.InvalidAnchorCount;
				OutError = FString::Printf(TEXT("Surface binding produced no candidate triangle for render vertex %d."), GarmentVertexIndex);
				return false;
			}

			const FVector3d Reconstructed = ClosestPoint
				+ Tangent * VertexBinding.RestTangentFrameOffsetCm.X
				+ Bitangent * VertexBinding.RestTangentFrameOffsetCm.Y
				+ BodyNormal * VertexBinding.RestTangentFrameOffsetCm.Z;
			const double AnchorError = (Reconstructed - GarmentPosition).Length();
			TotalAnchorError += AnchorError;
			OutPair.Metrics.MaximumAnchorErrorCm = FMath::Max(
				OutPair.Metrics.MaximumAnchorErrorCm,
				static_cast<float>(AnchorError));
		}

		// Auto mode is regularized over render adjacency so isolated distance/noise
		// cannot flip one vertex into a visibly different follow regime.
		if (CatalogRow.FitPolicy == EEFClothingFitPolicy::Auto)
		{
			for (int32 Iteration = 0; Iteration < 2; ++Iteration)
			{
				TArray<EEFClothingSurfaceVertexMode> SmoothedModes;
				SmoothedModes.SetNum(GarmentVertexCount);
				for (int32 VertexIndex = 0; VertexIndex < GarmentVertexCount; ++VertexIndex)
				{
					if (OutPair.VertexBindings[VertexIndex].Mode
						== EEFClothingSurfaceVertexMode::PreserveUpstream)
					{
						SmoothedModes[VertexIndex] = EEFClothingSurfaceVertexMode::PreserveUpstream;
						continue;
					}
					int32 Counts[3] = {};
					for (int32 NeighborIndex : NeighborSets[VertexIndex])
					{
						const int32 NeighborMode = static_cast<int32>(
							OutPair.VertexBindings[NeighborIndex].Mode);
						if (NeighborMode >= 0 && NeighborMode < UE_ARRAY_COUNT(Counts))
						{
							++Counts[NeighborMode];
						}
					}
					const int32 CurrentMode = static_cast<int32>(OutPair.VertexBindings[VertexIndex].Mode);
					int32 WinningMode = CurrentMode;
					for (int32 ModeIndex = 0; ModeIndex < 3; ++ModeIndex)
					{
						if (Counts[ModeIndex] > Counts[WinningMode])
						{
							WinningMode = ModeIndex;
						}
					}
					SmoothedModes[VertexIndex] = Counts[WinningMode] * 3 >= NeighborSets[VertexIndex].Num() * 2
						? static_cast<EEFClothingSurfaceVertexMode>(WinningMode)
						: OutPair.VertexBindings[VertexIndex].Mode;
				}
				for (int32 VertexIndex = 0; VertexIndex < GarmentVertexCount; ++VertexIndex)
				{
					OutPair.VertexBindings[VertexIndex].Mode = SmoothedModes[VertexIndex];
					OutPair.VertexBindings[VertexIndex].FollowWeight = SurfaceFollowWeight(
						SmoothedModes[VertexIndex],
						OutPair.VertexBindings[VertexIndex].RestSignedGapCm,
						TargetClearanceCm);
				}
			}
		}

		if (CatalogRow.bCreateThicknessShell)
		{
			if (ThicknessShellPreImportVertexCount <= 0
				|| ThicknessShellRenderVerticesByImportID.Num()
					!= ThicknessShellPreImportVertexCount * 2)
			{
				OutError = TEXT("Thickness-shell render pairing was not initialized.");
				return false;
			}
			const double MinimumSignedThicknessCm =
				MinimumCertifiedThicknessPointCm(
					EFClothingMorphV26::CompiledThicknessReferenceCm);
			for (int32 SourceOrdinal = 0;
				SourceOrdinal < ThicknessShellPreImportVertexCount;
				++SourceOrdinal)
			{
				const TArray<int32>& OuterRenderVertices =
					ThicknessShellRenderVerticesByImportID[SourceOrdinal];
				const TArray<int32>& InnerRenderVertices =
					ThicknessShellRenderVerticesByImportID[
						ThicknessShellPreImportVertexCount + SourceOrdinal];
				if (OuterRenderVertices.IsEmpty() || InnerRenderVertices.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Thickness-shell render pair %d is incomplete."),
						SourceOrdinal);
					return false;
				}

				auto ModePriority = [](const EEFClothingSurfaceVertexMode Mode)
				{
					return Mode == EEFClothingSurfaceVertexMode::PreserveUpstream
						? -1
						: static_cast<int32>(Mode);
				};
				int32 AuthoritativeInnerRenderVertex = InnerRenderVertices[0];
				for (const int32 CandidateInnerRenderVertex : InnerRenderVertices)
				{
					const FEFClothingSurfaceVertexBinding& Candidate =
						OutPair.VertexBindings[CandidateInnerRenderVertex];
					const FEFClothingSurfaceVertexBinding& Current =
						OutPair.VertexBindings[AuthoritativeInnerRenderVertex];
					const int32 CandidatePriority = ModePriority(Candidate.Mode);
					const int32 CurrentPriority = ModePriority(Current.Mode);
					if (CandidatePriority < CurrentPriority
						|| (CandidatePriority == CurrentPriority
							&& Candidate.FollowWeight > Current.FollowWeight + 1.e-6f)
						|| (CandidatePriority == CurrentPriority
							&& FMath::IsNearlyEqual(Candidate.FollowWeight, Current.FollowWeight, 1.e-6f)
							&& CandidateInnerRenderVertex < AuthoritativeInnerRenderVertex))
					{
						AuthoritativeInnerRenderVertex = CandidateInnerRenderVertex;
					}
				}
				ThicknessShellAuthoritativeInnerBySourceOrdinal[SourceOrdinal] =
					AuthoritativeInnerRenderVertex;
				const FEFClothingSurfaceVertexBinding AuthoritativeBinding =
					OutPair.VertexBindings[AuthoritativeInnerRenderVertex];
				const FVector3d AuthoritativeBarycentrics(
					AuthoritativeBinding.BodyBarycentrics.X,
					AuthoritativeBinding.BodyBarycentrics.Y,
					AuthoritativeBinding.BodyBarycentrics.Z);
				FVector3d Tangent;
				FVector3d Bitangent;
				FVector3d BodyNormal;
				if (!BuildSurfaceTangentFrame(
					BodyLOD,
					AuthoritativeBinding.BodyRenderVertexIndices,
					AuthoritativeBarycentrics,
					Tangent,
					Bitangent,
					BodyNormal))
				{
					OutError = FString::Printf(
						TEXT("Thickness-shell render pair %d has an invalid authoritative body frame."),
						SourceOrdinal);
					return false;
				}
				const FVector3d ClosestPoint =
					BodyLOD.Positions[AuthoritativeBinding.BodyRenderVertexIndices.X]
						* AuthoritativeBarycentrics.X
					+ BodyLOD.Positions[AuthoritativeBinding.BodyRenderVertexIndices.Y]
						* AuthoritativeBarycentrics.Y
					+ BodyLOD.Positions[AuthoritativeBinding.BodyRenderVertexIndices.Z]
						* AuthoritativeBarycentrics.Z;

				FVector3d AverageOuterPosition = FVector3d::Zero();
				for (const int32 RenderVertexIndex : OuterRenderVertices)
				{
					AverageOuterPosition += GarmentLOD.Positions[RenderVertexIndex];
				}
				AverageOuterPosition /= static_cast<double>(OuterRenderVertices.Num());
				FVector3d AverageInnerPosition = FVector3d::Zero();
				for (const int32 RenderVertexIndex : InnerRenderVertices)
				{
					AverageInnerPosition += GarmentLOD.Positions[RenderVertexIndex];
				}
				AverageInnerPosition /= static_cast<double>(InnerRenderVertices.Num());
				const double PairThicknessCm =
					(AverageOuterPosition - AverageInnerPosition).Length();
				const double SignedThicknessCm =
					(AverageOuterPosition - AverageInnerPosition).Dot(BodyNormal);
				// UE's iterative offset follows the garment surface, not the body normal.
				// On a curved but valid closed shell its layer vector may be tangential or
				// locally point toward the selected skin frame. Preserve that full 3D rest
				// vector and certify its Euclidean thickness; the unilateral runtime target
				// below still prevents either layer from crossing the animated skin.
				if (!FMath::IsFinite(PairThicknessCm)
					|| !FMath::IsFinite(SignedThicknessCm)
					|| PairThicknessCm + 1.e-6 < MinimumSignedThicknessCm)
				{
					OutError = FString::Printf(
						TEXT("Thickness-shell render pair %d collapses (distance/signed %.8f/%.8fcm, minimum %.8fcm)."),
						SourceOrdinal,
						PairThicknessCm,
						SignedThicknessCm,
						MinimumSignedThicknessCm);
					return false;
				}

				auto CoupleRenderVertex = [&OutPair, &GarmentLOD, &AuthoritativeBinding,
					AuthoritativeInnerRenderVertex, &ClosestPoint, &Tangent, &Bitangent,
					&BodyNormal](const int32 RenderVertexIndex, const bool bOuterLayer)
				{
					FEFClothingSurfaceVertexBinding& VertexBinding =
						OutPair.VertexBindings[RenderVertexIndex];
					VertexBinding.BodyRenderVertexIndices =
						AuthoritativeBinding.BodyRenderVertexIndices;
					VertexBinding.BodyBarycentrics = AuthoritativeBinding.BodyBarycentrics;
					VertexBinding.Mode = AuthoritativeBinding.Mode;
					VertexBinding.FollowWeight = AuthoritativeBinding.FollowWeight;
					VertexBinding.CandidateRange = AuthoritativeBinding.CandidateRange;
					VertexBinding.ThicknessReferenceRenderVertexIndex =
						AuthoritativeInnerRenderVertex;
					VertexBinding.bOuterThicknessLayer = bOuterLayer;
					const FVector3d RestOffset =
						GarmentLOD.Positions[RenderVertexIndex] - ClosestPoint;
					const double SignedGap = RestOffset.Dot(BodyNormal);
					VertexBinding.RestTangentFrameOffsetCm = FVector3f(
						static_cast<float>(RestOffset.Dot(Tangent)),
						static_cast<float>(RestOffset.Dot(Bitangent)),
						static_cast<float>(SignedGap));
					VertexBinding.RestSignedGapCm = static_cast<float>(SignedGap);
				};
				for (const int32 RenderVertexIndex : OuterRenderVertices)
				{
					CoupleRenderVertex(RenderVertexIndex, true);
				}
				for (const int32 RenderVertexIndex : InnerRenderVertices)
				{
					CoupleRenderVertex(RenderVertexIndex, false);
				}
			}
		}

		constexpr float BoundaryClearanceReserveCm = 0.35f;
		for (int32 VertexIndex = 0; VertexIndex < GarmentVertexCount; ++VertexIndex)
		{
			FEFClothingSurfaceVertexBinding& VertexBinding = OutPair.VertexBindings[VertexIndex];
			if (VertexBinding.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream)
			{
				VertexBinding.TargetClearanceCm = TargetClearanceCm;
				VertexBinding.FollowWeight = 0.0f;
				continue;
			}
			const bool bNeedsBoundaryReserve = BoundaryRiskVertices[VertexIndex]
				&& VertexBinding.Mode != EEFClothingSurfaceVertexMode::CollisionOnly;
			VertexBinding.TargetClearanceCm = TargetClearanceCm
				+ (bNeedsBoundaryReserve ? BoundaryClearanceReserveCm : 0.0f);
			const float RequiredInitialPushCm = FMath::Max(
				0.0f,
				VertexBinding.TargetClearanceCm - VertexBinding.RestSignedGapCm);
			if (VertexBinding.MaximumCorrectionCm + 1.e-4f
				< RequiredInitialPushCm)
			{
				OutError = FString::Printf(
					TEXT("Boundary reserve requires %.4fcm initial push beyond the correction budget at garment vertex %d."),
					RequiredInitialPushCm,
					VertexIndex);
				return false;
			}
			VertexBinding.FollowWeight = SurfaceFollowWeight(
				VertexBinding.Mode,
				VertexBinding.RestSignedGapCm,
				VertexBinding.TargetClearanceCm);
			OutPair.Metrics.MaximumInitialCorrectionCm = FMath::Max(
				OutPair.Metrics.MaximumInitialCorrectionCm,
				FMath::Max(
					0.0f,
					VertexBinding.TargetClearanceCm - VertexBinding.RestSignedGapCm));
			MinimumRestGap = FMath::Min(
				MinimumRestGap,
				static_cast<double>(VertexBinding.RestSignedGapCm));
		}

		if (CatalogRow.bCreateThicknessShell)
		{
			for (int32 SourceOrdinal = 0;
				SourceOrdinal < ThicknessShellPreImportVertexCount;
				++SourceOrdinal)
			{
				const int32 AuthoritativeInnerRenderVertex =
					ThicknessShellAuthoritativeInnerBySourceOrdinal[SourceOrdinal];
				if (!OutPair.VertexBindings.IsValidIndex(AuthoritativeInnerRenderVertex))
				{
					OutError = FString::Printf(
						TEXT("Thickness-shell render pair %d lost its authoritative inner binding."),
						SourceOrdinal);
					return false;
				}
				const FEFClothingSurfaceVertexBinding AuthoritativeBinding =
					OutPair.VertexBindings[AuthoritativeInnerRenderVertex];
				auto SynchronizeFinalConstraint = [
					&OutPair,
					&AuthoritativeBinding,
					&OutError,
					SourceOrdinal](
					const int32 RenderVertexIndex,
					const bool bOuterLayer)
				{
					FEFClothingSurfaceVertexBinding& VertexBinding =
						OutPair.VertexBindings[RenderVertexIndex];
					const float SignedLayerHeightCm = bOuterLayer
						? FMath::Max(
							0.0f,
							VertexBinding.RestSignedGapCm
								- AuthoritativeBinding.RestSignedGapCm)
						: 0.0f;
					VertexBinding.Mode = AuthoritativeBinding.Mode;
					// Inner and outer layers share the same unilateral correction, not
					// the same absolute body plane. Offsetting the outer target by its
					// signed layer height prevents Hybrid/CollisionOnly and boundary
					// reserve from crushing real thickness during animation.
					VertexBinding.TargetClearanceCm =
						AuthoritativeBinding.TargetClearanceCm + SignedLayerHeightCm;
					VertexBinding.MaximumCorrectionCm = AuthoritativeBinding.MaximumCorrectionCm;
					const float RequiredInitialPushCm = FMath::Max(
						0.0f,
						VertexBinding.TargetClearanceCm - VertexBinding.RestSignedGapCm);
					if (VertexBinding.MaximumCorrectionCm + 1.e-4f
						< RequiredInitialPushCm)
					{
						OutError = FString::Printf(
							TEXT("Thickness-shell pair %d requires %.6fcm initial push but only %.6fcm correction is certified."),
							SourceOrdinal,
							RequiredInitialPushCm,
							VertexBinding.MaximumCorrectionCm);
						return false;
					}
					VertexBinding.FollowWeight = VertexBinding.Mode
						== EEFClothingSurfaceVertexMode::PreserveUpstream
						? 0.0f
						: SurfaceFollowWeight(
							VertexBinding.Mode,
							VertexBinding.RestSignedGapCm,
							VertexBinding.TargetClearanceCm);
					return true;
				};
				for (const int32 RenderVertexIndex :
					ThicknessShellRenderVerticesByImportID[SourceOrdinal])
				{
					if (!SynchronizeFinalConstraint(RenderVertexIndex, true))
					{
						return false;
					}
				}
				for (const int32 RenderVertexIndex :
					ThicknessShellRenderVerticesByImportID[
						ThicknessShellPreImportVertexCount + SourceOrdinal])
				{
					if (!SynchronizeFinalConstraint(RenderVertexIndex, false))
					{
						return false;
					}
				}
			}

			OutPair.Metrics.MaximumInitialCorrectionCm = 0.0f;
			MinimumRestGap = TNumericLimits<double>::Max();
			for (const FEFClothingSurfaceVertexBinding& VertexBinding : OutPair.VertexBindings)
			{
				if (VertexBinding.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream)
				{
					continue;
				}
				OutPair.Metrics.MaximumInitialCorrectionCm = FMath::Max(
					OutPair.Metrics.MaximumInitialCorrectionCm,
					FMath::Max(
						0.0f,
						VertexBinding.TargetClearanceCm - VertexBinding.RestSignedGapCm));
				MinimumRestGap = FMath::Min(
					MinimumRestGap,
					static_cast<double>(VertexBinding.RestSignedGapCm));
			}
		}

		// Adaptive edge/interior witnesses close the vertex-only loophole. A
		// triangle whose spacing would exceed 0.5cm receives a barycentric grid.
		constexpr double MaximumWitnessSpacingCm = 0.5;
		constexpr int32 MaximumWitnessSubdivisions = 32;
		for (int32 GarmentDynamicTriangleID : GarmentLOD.Mesh.TriangleIndicesItr())
		{
			if (!GarmentLOD.DynamicTriangles.IsValidIndex(GarmentDynamicTriangleID))
			{
				continue;
			}
			const FSurfaceRenderTriangle& GarmentTriangle = GarmentLOD.DynamicTriangles[GarmentDynamicTriangleID];
			if (GarmentTriangle.LogicalTriangleIndex == INDEX_NONE)
			{
				continue;
			}
			const FIntVector& GarmentTriangleVertices = GarmentTriangle.RenderVertexIndices;
			if (OutPair.VertexBindings[GarmentTriangleVertices.X].Mode
					== EEFClothingSurfaceVertexMode::PreserveUpstream
				|| OutPair.VertexBindings[GarmentTriangleVertices.Y].Mode
					== EEFClothingSurfaceVertexMode::PreserveUpstream
				|| OutPair.VertexBindings[GarmentTriangleVertices.Z].Mode
					== EEFClothingSurfaceVertexMode::PreserveUpstream)
			{
				++OutPair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount;
				continue;
			}
			const FVector3d& A = GarmentLOD.Positions[GarmentTriangle.RenderVertexIndices.X];
			const FVector3d& B = GarmentLOD.Positions[GarmentTriangle.RenderVertexIndices.Y];
			const FVector3d& C = GarmentLOD.Positions[GarmentTriangle.RenderVertexIndices.Z];
			const double MaximumEdgeLength = FMath::Max3((B - A).Length(), (C - B).Length(), (A - C).Length());
			const int32 Subdivisions = FMath::Max(1, FMath::CeilToInt(MaximumEdgeLength / MaximumWitnessSpacingCm));
			if (Subdivisions > MaximumWitnessSubdivisions)
			{
				OutError = FString::Printf(
					TEXT("Garment LOD %d triangle %d needs %d witness subdivisions; source topology must be subdivided."),
					GarmentLOD.LODIndex,
					GarmentTriangle.LogicalTriangleIndex,
					Subdivisions);
				return false;
			}
			TArray<FVector3d> WitnessBarycentrics;
			if (Subdivisions == 1)
			{
				WitnessBarycentrics.Add(FVector3d(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
			}
			else
			{
				for (int32 I = 0; I <= Subdivisions; ++I)
				{
					for (int32 J = 0; J <= Subdivisions - I; ++J)
					{
						const int32 K = Subdivisions - I - J;
						if ((I == Subdivisions && J == 0 && K == 0)
							|| (J == Subdivisions && I == 0 && K == 0)
							|| (K == Subdivisions && I == 0 && J == 0))
						{
							continue;
						}
						WitnessBarycentrics.Add(FVector3d(
							static_cast<double>(I) / Subdivisions,
							static_cast<double>(J) / Subdivisions,
							static_cast<double>(K) / Subdivisions));
					}
				}
			}
			for (const FVector3d& GarmentBarycentrics : WitnessBarycentrics)
			{
				const FVector3d WitnessPosition = A * GarmentBarycentrics.X
					+ B * GarmentBarycentrics.Y
					+ C * GarmentBarycentrics.Z;
				FIntVector OrientedBodyRenderVertexIndices;
				FVector3d ClosestPoint;
				FVector3d BodyBarycentrics;
				FVector3d WitnessTangent;
				FVector3d WitnessBitangent;
				FVector3d BodyNormal;
				double DistanceSquared = TNumericLimits<double>::Max();
				if (!FindBestAnimatedSurfaceAnchor(
					BodyLOD,
					BodySpatial,
					WitnessPosition,
					OrientedBodyRenderVertexIndices,
					ClosestPoint,
					BodyBarycentrics,
					WitnessTangent,
					WitnessBitangent,
					BodyNormal,
					DistanceSquared))
				{
					OutError = FString::Printf(TEXT("Surface binding witness could not resolve body geometry for garment triangle %d."), GarmentTriangle.LogicalTriangleIndex);
					return false;
				}
				FEFClothingSurfaceWitness& Witness = OutPair.Witnesses.AddDefaulted_GetRef();
				Witness.GarmentTriangleIndex = GarmentTriangle.LogicalTriangleIndex;
				Witness.GarmentRenderVertexIndices = GarmentTriangle.RenderVertexIndices;
				Witness.GarmentBarycentrics = FVector3f(GarmentBarycentrics);
				Witness.BodyRenderVertexIndices = OrientedBodyRenderVertexIndices;
				Witness.BodyBarycentrics = FVector3f(BodyBarycentrics);
				// Boundary vertices carry their own opening reserve. Face witnesses keep
				// the fabric-wide clearance so the strip behind an opening is not inflated
				// or pushed across a different concave body surface.
				Witness.TargetClearanceCm = CatalogRow.bCreateThicknessShell
					? static_cast<float>(
						OutPair.VertexBindings[GarmentTriangleVertices.X].TargetClearanceCm
							* GarmentBarycentrics.X
						+ OutPair.VertexBindings[GarmentTriangleVertices.Y].TargetClearanceCm
							* GarmentBarycentrics.Y
						+ OutPair.VertexBindings[GarmentTriangleVertices.Z].TargetClearanceCm
							* GarmentBarycentrics.Z)
					: TargetClearanceCm;
				Witness.MaximumCorrectionCm = MaximumCorrectionCm;
			}
		}

		OutPair.Metrics.BoundRenderVertexCount = GarmentVertexCount;
		OutPair.Metrics.NeighborReferenceCount = OutPair.NeighborRenderVertexIndices.Num();
		OutPair.Metrics.CandidateTriangleCount = OutPair.CandidateTriangles.Num();
		OutPair.Metrics.WitnessCount = OutPair.Witnesses.Num();
		// BuildSurfaceRenderLOD removes zero-area source triangles before it builds
		// the DynamicMesh/BVH. Consequently no certified anchor, candidate or
		// witness can reference one. Keep the safety metric scoped to triangles
		// actually admitted to the binding and report the rejected source input
		// separately for auditability.
		OutPair.Metrics.DegenerateBodyTriangleCount = 0;
		OutPair.Metrics.ExcludedDegenerateBodyTriangleCount = BodyLOD.DegenerateTriangleCount;
		OutPair.Metrics.ExcludedBodyTriangleCount = BodyLOD.ExcludedTriangleCount;
		OutPair.Metrics.MinimumRestSignedGapCm = static_cast<float>(MinimumRestGap);
		OutPair.Metrics.MeanAnchorErrorCm = GarmentVertexCount > 0
			? static_cast<float>(TotalAnchorError / GarmentVertexCount)
			: 0.0f;
		for (const FEFClothingSurfaceVertexBinding& VertexBinding : OutPair.VertexBindings)
		{
			switch (VertexBinding.Mode)
			{
			case EEFClothingSurfaceVertexMode::SurfaceFollow:
				++OutPair.Metrics.SurfaceFollowVertexCount;
				break;
			case EEFClothingSurfaceVertexMode::Hybrid:
				++OutPair.Metrics.HybridVertexCount;
				break;
			case EEFClothingSurfaceVertexMode::CollisionOnly:
				++OutPair.Metrics.CollisionOnlyVertexCount;
				break;
			case EEFClothingSurfaceVertexMode::PreserveUpstream:
				++OutPair.Metrics.PreserveUpstreamVertexCount;
				break;
			}
		}
		const bool bPreserveDomainRequested = ExcludedAnatomyMesh != nullptr;
		const bool bPreserveDomainValid = !bPreserveDomainRequested
			? OutPair.Metrics.PreserveUpstreamVertexCount == 0
				&& OutPair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount == 0
			: OutPair.Metrics.PreserveUpstreamVertexCount > 0
				&& OutPair.Metrics.PreserveUpstreamVertexCount * 100
					<= GarmentVertexCount * 35
				&& OutPair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount > 0;
		const bool bRestGapIsCertifiable = bAllowCorrectableInitialPenetration
			? FMath::IsFinite(OutPair.Metrics.MinimumRestSignedGapCm)
			: OutPair.Metrics.MinimumRestSignedGapCm >= -0.02f;
		OutPair.bCertified = OutPair.Metrics.InvalidAnchorCount == 0
			&& OutPair.Metrics.BoundRenderVertexCount == GarmentLOD.Topology.RenderVertexCount
			// The fitted mesh is independently certified intersection-free. A small
			// render-normal clearance deficit is legal because SurfaceWrap is hidden
			// until its first unilateral GPU correction; the exact required push must
			// still fit inside the immutable per-garment correction budget.
			&& bRestGapIsCertifiable
			&& OutPair.Metrics.MaximumInitialCorrectionCm <= MaximumCorrectionCm + 1.e-4f
			&& OutPair.Metrics.MaximumAnchorErrorCm <= 0.001f
			&& OutPair.VertexBindings.Num() == GarmentLOD.Topology.RenderVertexCount
			&& !OutPair.CandidateTriangles.IsEmpty()
			&& !OutPair.Witnesses.IsEmpty()
			&& bPreserveDomainValid;
		if (!OutPair.bCertified)
		{
			OutError = FString::Printf(
				TEXT("Surface binding LOD pair %d/%d failed certification: bound=%d/%d invalid=%d minGap=%.4fcm target=%.4fcm initialCorrection=%.4f/%.4fcm anchorError=%.6fcm candidates=%d witnesses=%d preserve=%d excludedFaces=%d."),
				GarmentLOD.LODIndex,
				BodyLOD.LODIndex,
				OutPair.Metrics.BoundRenderVertexCount,
				GarmentLOD.Topology.RenderVertexCount,
				OutPair.Metrics.InvalidAnchorCount,
				OutPair.Metrics.MinimumRestSignedGapCm,
				TargetClearanceCm,
				OutPair.Metrics.MaximumInitialCorrectionCm,
				MaximumCorrectionCm,
				OutPair.Metrics.MaximumAnchorErrorCm,
				OutPair.CandidateTriangles.Num(),
				OutPair.Witnesses.Num(),
				OutPair.Metrics.PreserveUpstreamVertexCount,
				OutPair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount);
		}
		return OutPair.bCertified;
	}

	static bool BuildSurfaceBindingAsset(
		USkeletalMesh* SourceGarment,
		USkeletalMesh* FittedGarment,
		USkeletalMesh* BodySurface,
		const FDynamicMesh3& GarmentDynamicMesh,
		const TArray<FVector3d>& ClearanceDeltas,
		const FEFClothingGarmentRow& CatalogRow,
		const TArray<FName>& ExcludedBodySurfaceMaterialSlots,
		const FGuid& BuildGuid,
		const FString& OutputRoot,
		const FString& PublicationKey,
		float MinimumClearanceCm,
		float MaximumPushCm,
		UEFClothingSurfaceBinding*& OutBinding,
		EEFClothingFitMode& OutResolvedFitMode,
		FString& OutError)
	{
		OutBinding = nullptr;
		FSkinnedAssetCompilingManager::Get().FinishCompilation({FittedGarment, BodySurface});
		FSkeletalMeshRenderData* GarmentRenderData = FittedGarment->GetResourceForRendering();
		FSkeletalMeshRenderData* BodyRenderData = BodySurface->GetResourceForRendering();
		FSkeletalMeshModel* GarmentImportedModel = FittedGarment->GetImportedModel();
		if (!GarmentRenderData || !BodyRenderData || !GarmentImportedModel || GarmentImportedModel->LODModels.IsEmpty())
		{
			OutError = TEXT("Surface binding could not load fitted/body render data.");
			return false;
		}
		if (GarmentRenderData->LODRenderData.Num() != 1)
		{
			OutError = TEXT("V26 currently requires one fitted-garment render LOD; body LODs are all compiled.");
			return false;
		}
		const FMeshDescription* GarmentDescription = FittedGarment->GetMeshDescription(0);
		if (!GarmentDescription)
		{
			OutError = TEXT("Surface binding fitted garment lost its LOD0 MeshDescription.");
			return false;
		}
		TArray<FVector3d> ImportClearanceDeltas;
		if (!BuildImportVertexClearanceDeltas(
			GarmentDynamicMesh,
			ClearanceDeltas,
			GarmentDescription->Vertices().Num(),
			ImportClearanceDeltas,
			OutError))
		{
			return false;
		}

		FSurfaceRenderLOD GarmentLOD;
		if (!BuildSurfaceRenderLOD(FittedGarment, 0, {}, FitWeightProfileName, GarmentLOD, OutError))
		{
			return false;
		}
		const FSkeletalMeshAttributesShared GarmentAttributes(*GarmentDescription);
		const TVertexAttributesConstRef<FVector3f> StoredClearanceDeltas =
			GarmentAttributes.GetVertexMorphPositionDelta(ClearanceMorphName);
		if (!StoredClearanceDeltas.IsValid())
		{
			OutError = TEXT("Fitted garment has no serialized EF_AutoFit_Clearance field for render binding.");
			return false;
		}
		for (int32 RenderVertexIndex = 0; RenderVertexIndex < GarmentLOD.Positions.Num(); ++RenderVertexIndex)
		{
			const int32 ImportVertexIndex = GarmentLOD.MeshToImportVertexMap[RenderVertexIndex];
			if (!ImportClearanceDeltas.IsValidIndex(ImportVertexIndex)
				|| !GarmentDescription->Vertices().IsValid(FVertexID(ImportVertexIndex)))
			{
				OutError = FString::Printf(TEXT("Fitted render vertex %d maps to invalid import vertex %d."), RenderVertexIndex, ImportVertexIndex);
				return false;
			}
			const FVector3d StoredDelta(
				StoredClearanceDeltas.Get(FVertexID(ImportVertexIndex)));
			if (StoredDelta.ContainsNaN()
				|| (StoredDelta - ImportClearanceDeltas[ImportVertexIndex]).SquaredLength()
					> FMath::Square(1.e-5))
			{
				OutError = FString::Printf(
					TEXT("Fitted render vertex %d does not match the final serialized clearance delta for import vertex %d."),
					RenderVertexIndex,
					ImportVertexIndex);
				return false;
			}
			// Bind against the exact morph field Unreal will feed into the upstream
			// DAZ/skin deformer, never against a pre-serialization DynamicMesh ID.
			GarmentLOD.Positions[RenderVertexIndex] += StoredDelta;
			GarmentLOD.Mesh.SetVertex(RenderVertexIndex, GarmentLOD.Positions[RenderVertexIndex]);
		}

		const FString BindingName = FString::Printf(
			TEXT("DA_%s_%s_%s_%s_EFV26Surface_%s"),
			*SanitizeAssetName(SourceGarment->GetName()),
			*BuildSourceKey(SourceGarment),
			*SanitizeAssetName(BodySurface->GetName()),
			*BuildSourceKey(BodySurface),
			*PublicationKey);
		const FString BindingObjectPath = FString::Printf(TEXT("%s/%s.%s"), *OutputRoot, *BindingName, *BindingName);
		if (LoadObject<UEFClothingSurfaceBinding>(nullptr, *BindingObjectPath))
		{
			OutError = FString::Printf(TEXT("Fresh publication key collided with surface binding %s."), *BindingObjectPath);
			return false;
		}
		UEFClothingSurfaceBinding* Binding = FindOrCreateDataAsset<UEFClothingSurfaceBinding>(OutputRoot, BindingName);
		if (!Binding)
		{
			OutError = TEXT("Could not create the generated V26 surface binding asset.");
			return false;
		}
		Binding->SourceGarment = SourceGarment;
		Binding->FittedGarment = FittedGarment;
		Binding->BodySurface = BodySurface;
		Binding->BuildGuid = BuildGuid;
		Binding->CompilerVersion = CompilerVersion;
		Binding->SchemaVersion = EFClothingMorphV26::SurfaceBindingSchemaVersion;
		Binding->SourceContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(SourceGarment);
		Binding->FittedContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(FittedGarment);
		Binding->BodyContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(BodySurface);
		Binding->SourceSkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(SourceGarment);
		Binding->FittedSkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(FittedGarment);
		Binding->BodySkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(BodySurface);
		Binding->SharedSkeletonFingerprint = EFClothingSkeleton::BuildSharedSkeletonFingerprint(SourceGarment->GetSkeleton());
		Binding->ExcludedBodySurfaceMaterialSlots = ExcludedBodySurfaceMaterialSlots;
		Binding->LODPairBindings.Reset();
		const FGameplayTag GenitalCoverageTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("BodyCoverage.Pelvis.Genitals")),
			false);
		const bool bPreserveExcludedAnatomyUpstream = GenitalCoverageTag.IsValid()
			&& CatalogRow.CoverageTags.HasTagExact(GenitalCoverageTag)
			&& !CatalogRow.ExcludedBodyBoneBranches.IsEmpty()
			&& !ExcludedBodySurfaceMaterialSlots.IsEmpty();

		int64 TotalSurfaceFollow = 0;
		int64 TotalHybrid = 0;
		int64 TotalCollisionOnly = 0;
		for (int32 BodyLODIndex = 0; BodyLODIndex < BodyRenderData->LODRenderData.Num(); ++BodyLODIndex)
		{
			FSurfaceRenderLOD BodyLOD;
			if (!BuildSurfaceRenderLOD(BodySurface, BodyLODIndex, ExcludedBodySurfaceMaterialSlots, NAME_None, BodyLOD, OutError))
			{
				return false;
			}
			FDynamicMesh3 ExcludedAnatomyMesh;
			const FDynamicMesh3* ExcludedAnatomyMeshPtr = nullptr;
			if (bPreserveExcludedAnatomyUpstream)
			{
				FSurfaceRenderLOD FullBodyLOD;
				if (!BuildSurfaceRenderLOD(
					BodySurface,
					BodyLODIndex,
					{},
					NAME_None,
					FullBodyLOD,
					OutError))
				{
					return false;
				}
				int32 ExcludedAnatomyTriangleCount = 0;
				if (!BuildExcludedAnatomySurface(
					FullBodyLOD,
					BodyLOD,
					ExcludedAnatomyMesh,
					ExcludedAnatomyTriangleCount,
					OutError))
				{
					return false;
				}
				ExcludedAnatomyMeshPtr = &ExcludedAnatomyMesh;
			}
			FEFClothingSurfaceLODPairBinding& Pair = Binding->LODPairBindings.AddDefaulted_GetRef();
			if (!BuildSurfaceLODPairBinding(
				GarmentLOD,
				BodyLOD,
				ExcludedAnatomyMeshPtr,
				CatalogRow,
				MinimumClearanceCm,
				static_cast<float>(CompilerClearanceReserveCm),
				MaximumPushCm,
				Pair,
				OutError))
			{
				return false;
			}
			TotalSurfaceFollow += Pair.Metrics.SurfaceFollowVertexCount;
			TotalHybrid += Pair.Metrics.HybridVertexCount;
			TotalCollisionOnly += Pair.Metrics.CollisionOnlyVertexCount;
		}
		if (Binding->LODPairBindings.IsEmpty())
		{
			OutError = TEXT("Surface binding generated no body/garment LOD pairs.");
			return false;
		}

		switch (CatalogRow.FitPolicy)
		{
		case EEFClothingFitPolicy::Tight:
			OutResolvedFitMode = EEFClothingFitMode::Tight;
			break;
		case EEFClothingFitPolicy::Hybrid:
			OutResolvedFitMode = EEFClothingFitMode::Hybrid;
			break;
		case EEFClothingFitPolicy::Loose:
			OutResolvedFitMode = EEFClothingFitMode::Loose;
			break;
		case EEFClothingFitPolicy::Rigid:
			OutResolvedFitMode = EEFClothingFitMode::Rigid;
			break;
		default:
			if (TotalSurfaceFollow * 5 >= (TotalSurfaceFollow + TotalHybrid + TotalCollisionOnly) * 4)
			{
				OutResolvedFitMode = EEFClothingFitMode::Tight;
			}
			else if (TotalCollisionOnly * 5 >= (TotalSurfaceFollow + TotalHybrid + TotalCollisionOnly) * 3)
			{
				OutResolvedFitMode = EEFClothingFitMode::Loose;
			}
			else
			{
				OutResolvedFitMode = EEFClothingFitMode::Hybrid;
			}
			break;
		}
		Binding->MarkPackageDirty();
		if (!SaveAsset(Binding, OutError))
		{
			return false;
		}
		OutBinding = Binding;
		return true;
	}

	static bool ValidateSurfaceBinding(
		const UEFClothingSurfaceBinding* Binding,
		const UEFClothingFitProfile* Profile,
		FString& OutError)
	{
		if (!IsValid(Binding)
			|| !IsValid(Profile)
			|| Binding->SourceGarment.ToSoftObjectPath() != Profile->SourceGarment.ToSoftObjectPath()
			|| Binding->FittedGarment.ToSoftObjectPath() != Profile->FittedGarment.ToSoftObjectPath()
			|| Binding->BodySurface.ToSoftObjectPath() != Profile->BodySurface.ToSoftObjectPath()
			|| Binding->BuildGuid != Profile->BuildGuid
			|| Binding->CompilerVersion != CompilerVersion
			|| Binding->SchemaVersion != EFClothingMorphV26::SurfaceBindingSchemaVersion
			|| Binding->LODPairBindings.IsEmpty())
		{
			OutError = TEXT("V26 surface binding identity/schema does not match its fit profile.");
			return false;
		}
		USkeletalMesh* FittedGarment = Binding->FittedGarment.LoadSynchronous();
		USkeletalMesh* BodySurface = Binding->BodySurface.LoadSynchronous();
		USkeletalMesh* SourceGarment = Binding->SourceGarment.LoadSynchronous();
		if (!IsValid(FittedGarment)
			|| !IsValid(BodySurface)
			|| !IsValid(SourceGarment)
			|| Binding->SourceContentFingerprint != EFClothingSkeleton::BuildContentFingerprint(SourceGarment)
			|| Binding->FittedContentFingerprint != EFClothingSkeleton::BuildContentFingerprint(FittedGarment)
			|| Binding->BodyContentFingerprint != EFClothingSkeleton::BuildContentFingerprint(BodySurface)
			|| Binding->SourceSkeletonFingerprint != EFClothingSkeleton::BuildFingerprint(SourceGarment)
			|| Binding->FittedSkeletonFingerprint != EFClothingSkeleton::BuildFingerprint(FittedGarment)
			|| Binding->BodySkeletonFingerprint != EFClothingSkeleton::BuildFingerprint(BodySurface)
			|| SourceGarment->GetSkeleton() != FittedGarment->GetSkeleton()
			|| SourceGarment->GetSkeleton() != BodySurface->GetSkeleton()
			|| Binding->SharedSkeletonFingerprint
				!= EFClothingSkeleton::BuildSharedSkeletonFingerprint(SourceGarment->GetSkeleton()))
		{
			OutError = TEXT("V26 surface binding protected-mesh fingerprint or shared-skeleton identity is stale.");
			return false;
		}

		auto TopologyMatches = [](const FEFClothingSurfaceTopologyFingerprint& Stored, const FSurfaceRenderLOD& Actual)
		{
			return Stored.LODIndex == Actual.Topology.LODIndex
				&& Stored.RenderVertexCount == Actual.Topology.RenderVertexCount
				&& Stored.RenderIndexCount == Actual.Topology.RenderIndexCount
				&& Stored.TriangleCount == Actual.Topology.TriangleCount
				&& Stored.SectionCount == Actual.Topology.SectionCount
				&& Stored.TopologyFingerprint == Actual.Topology.TopologyFingerprint
				&& Stored.ContentFingerprint == Actual.Topology.ContentFingerprint;
		};
		TMap<int32, FSurfaceRenderLOD> GarmentLODs;
		TMap<int32, FSurfaceRenderLOD> BodyLODs;
		for (const FEFClothingSurfaceLODPairBinding& Pair : Binding->LODPairBindings)
		{
			if (!GarmentLODs.Contains(Pair.GarmentTopology.LODIndex))
			{
				FSurfaceRenderLOD ActualGarmentLOD;
				if (!BuildSurfaceRenderLOD(FittedGarment, Pair.GarmentTopology.LODIndex, {}, FitWeightProfileName, ActualGarmentLOD, OutError))
				{
					return false;
				}
				GarmentLODs.Add(Pair.GarmentTopology.LODIndex, MoveTemp(ActualGarmentLOD));
			}
			if (!BodyLODs.Contains(Pair.BodyTopology.LODIndex))
			{
				FSurfaceRenderLOD ActualBodyLOD;
				if (!BuildSurfaceRenderLOD(
					BodySurface,
					Pair.BodyTopology.LODIndex,
					Binding->ExcludedBodySurfaceMaterialSlots,
					NAME_None,
					ActualBodyLOD,
					OutError))
				{
					return false;
				}
				BodyLODs.Add(Pair.BodyTopology.LODIndex, MoveTemp(ActualBodyLOD));
			}
			if (!TopologyMatches(Pair.GarmentTopology, GarmentLODs.FindChecked(Pair.GarmentTopology.LODIndex))
				|| !TopologyMatches(Pair.BodyTopology, BodyLODs.FindChecked(Pair.BodyTopology.LODIndex)))
			{
				OutError = FString::Printf(
					TEXT("V26 surface binding render topology fingerprint is stale for LOD pair %d/%d."),
					Pair.GarmentTopology.LODIndex,
					Pair.BodyTopology.LODIndex);
				return false;
			}
		}
		TSet<uint64> UniqueLODPairs;
		for (const FEFClothingSurfaceLODPairBinding& Pair : Binding->LODPairBindings)
		{
			const uint64 PairKey = static_cast<uint64>(static_cast<uint32>(Pair.GarmentTopology.LODIndex)) << 32
				| static_cast<uint32>(Pair.BodyTopology.LODIndex);
			if (!Pair.bCertified
				|| UniqueLODPairs.Contains(PairKey)
				|| Pair.GarmentTopology.RenderVertexCount <= 0
				|| Pair.BodyTopology.RenderVertexCount <= 0
				|| Pair.VertexBindings.Num() != Pair.GarmentTopology.RenderVertexCount
				|| Pair.Metrics.BoundRenderVertexCount != Pair.VertexBindings.Num()
				|| Pair.Metrics.InvalidAnchorCount != 0
				|| Pair.Metrics.SurfaceFollowVertexCount
					+ Pair.Metrics.HybridVertexCount
					+ Pair.Metrics.CollisionOnlyVertexCount
					+ Pair.Metrics.PreserveUpstreamVertexCount
					!= Pair.VertexBindings.Num()
				|| Pair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount < 0
				|| !FMath::IsFinite(Pair.Metrics.MinimumRestSignedGapCm)
				|| Pair.Metrics.MinimumRestSignedGapCm < -0.02f
				|| !FMath::IsFinite(Pair.Metrics.MaximumInitialCorrectionCm)
				|| Pair.Metrics.MaximumInitialCorrectionCm < 0.0f
				|| Pair.Witnesses.IsEmpty())
			{
				OutError = FString::Printf(TEXT("V26 surface binding has an invalid/duplicate LOD pair %d/%d."), Pair.GarmentTopology.LODIndex, Pair.BodyTopology.LODIndex);
				return false;
			}
			UniqueLODPairs.Add(PairKey);
			float RecomputedMaximumInitialCorrectionCm = 0.0f;
			for (int32 VertexIndex = 0; VertexIndex < Pair.VertexBindings.Num(); ++VertexIndex)
			{
				const FEFClothingSurfaceVertexBinding& Vertex = Pair.VertexBindings[VertexIndex];
				const bool bPreserveUpstream = Vertex.Mode
					== EEFClothingSurfaceVertexMode::PreserveUpstream;
				bool bThicknessContractValid = !Profile->bCompiledThicknessShell
					? Vertex.ThicknessReferenceRenderVertexIndex == INDEX_NONE
						&& !Vertex.bOuterThicknessLayer
					: Pair.VertexBindings.IsValidIndex(
						Vertex.ThicknessReferenceRenderVertexIndex);
				if (bThicknessContractValid && Profile->bCompiledThicknessShell)
				{
					const FEFClothingSurfaceVertexBinding& InnerReference =
						Pair.VertexBindings[Vertex.ThicknessReferenceRenderVertexIndex];
					bThicknessContractValid = !InnerReference.bOuterThicknessLayer;
					if (bThicknessContractValid && Vertex.bOuterThicknessLayer)
					{
						bThicknessContractValid =
							(Vertex.RestTangentFrameOffsetCm
								- InnerReference.RestTangentFrameOffsetCm).Length()
								> UE_SMALL_NUMBER;
					}
				}
				const float BarycentricSum = Vertex.BodyBarycentrics.X + Vertex.BodyBarycentrics.Y + Vertex.BodyBarycentrics.Z;
				if (Vertex.GarmentRenderVertexIndex != VertexIndex
					|| !bThicknessContractValid
					|| Vertex.BodyRenderVertexIndices.X < 0
					|| Vertex.BodyRenderVertexIndices.Y < 0
					|| Vertex.BodyRenderVertexIndices.Z < 0
					|| Vertex.BodyRenderVertexIndices.X >= Pair.BodyTopology.RenderVertexCount
					|| Vertex.BodyRenderVertexIndices.Y >= Pair.BodyTopology.RenderVertexCount
					|| Vertex.BodyRenderVertexIndices.Z >= Pair.BodyTopology.RenderVertexCount
					|| !FMath::IsNearlyEqual(BarycentricSum, 1.0f, 1.e-3f)
					|| Vertex.NeighborRange.Offset < 0
					|| Vertex.NeighborRange.Count < 0
					|| Vertex.NeighborRange.Offset + Vertex.NeighborRange.Count > Pair.NeighborRenderVertexIndices.Num()
					|| Vertex.CandidateRange.Offset < 0
					|| Vertex.CandidateRange.Count <= 0
					|| Vertex.CandidateRange.Offset + Vertex.CandidateRange.Count > Pair.CandidateTriangles.Num()
					|| Vertex.BodyBarycentrics.ContainsNaN()
					|| Vertex.RestTangentFrameOffsetCm.ContainsNaN()
					|| !FMath::IsFinite(Vertex.RestSignedGapCm)
					|| !FMath::IsFinite(Vertex.TargetClearanceCm)
					|| Vertex.TargetClearanceCm <= 0.0f
					|| !FMath::IsFinite(Vertex.FollowWeight)
					|| Vertex.FollowWeight < 0.0f
					|| Vertex.FollowWeight > 1.0f
					|| !FMath::IsFinite(Vertex.MaximumCorrectionCm)
					|| Vertex.MaximumCorrectionCm <= 0.0f
					|| static_cast<uint8>(Vertex.Mode)
						> static_cast<uint8>(EEFClothingSurfaceVertexMode::PreserveUpstream)
					|| (bPreserveUpstream && !FMath::IsNearlyZero(Vertex.FollowWeight, 1.e-6f))
					|| (!bPreserveUpstream
						&& FMath::Max(0.0f, Vertex.TargetClearanceCm - Vertex.RestSignedGapCm)
							> Vertex.MaximumCorrectionCm + 1.e-4f))
				{
					OutError = FString::Printf(TEXT("V26 surface binding vertex %d has an invalid render/range contract."), VertexIndex);
					return false;
				}
				for (int32 NeighborOffset = 0; NeighborOffset < Vertex.NeighborRange.Count; ++NeighborOffset)
				{
					const int32 NeighborIndex = Pair.NeighborRenderVertexIndices[
						Vertex.NeighborRange.Offset + NeighborOffset];
					if (NeighborIndex < 0
						|| NeighborIndex >= Pair.GarmentTopology.RenderVertexCount
						|| NeighborIndex == VertexIndex)
					{
						OutError = FString::Printf(TEXT("V26 surface binding vertex %d has an invalid neighbor."), VertexIndex);
						return false;
					}
				}
				for (int32 CandidateOffset = 0; CandidateOffset < Vertex.CandidateRange.Count; ++CandidateOffset)
				{
					const FEFClothingSurfaceCandidateTriangle& Candidate = Pair.CandidateTriangles[
						Vertex.CandidateRange.Offset + CandidateOffset];
					if (Candidate.BodyTriangleIndex < 0
						|| Candidate.BodyTriangleIndex >= Pair.BodyTopology.TriangleCount
						|| Candidate.BodyRenderVertexIndices.X < 0
						|| Candidate.BodyRenderVertexIndices.Y < 0
						|| Candidate.BodyRenderVertexIndices.Z < 0
						|| Candidate.BodyRenderVertexIndices.X >= Pair.BodyTopology.RenderVertexCount
						|| Candidate.BodyRenderVertexIndices.Y >= Pair.BodyTopology.RenderVertexCount
						|| Candidate.BodyRenderVertexIndices.Z >= Pair.BodyTopology.RenderVertexCount
						|| !FMath::IsFinite(Candidate.RestDistanceCm)
						|| Candidate.RestDistanceCm < 0.0f)
					{
						OutError = FString::Printf(TEXT("V26 surface binding vertex %d has an invalid candidate triangle."), VertexIndex);
						return false;
					}
				}
				if (!bPreserveUpstream)
				{
					RecomputedMaximumInitialCorrectionCm = FMath::Max(
						RecomputedMaximumInitialCorrectionCm,
						FMath::Max(0.0f, Vertex.TargetClearanceCm - Vertex.RestSignedGapCm));
				}
			}
			if (!FMath::IsNearlyEqual(
				Pair.Metrics.MaximumInitialCorrectionCm,
				RecomputedMaximumInitialCorrectionCm,
				1.e-4f))
			{
				OutError = FString::Printf(
					TEXT("V26 surface binding initial-correction metric is stale for LOD pair %d/%d."),
					Pair.GarmentTopology.LODIndex,
					Pair.BodyTopology.LODIndex);
				return false;
			}
			for (const FEFClothingSurfaceWitness& Witness : Pair.Witnesses)
			{
				const float GarmentBarycentricSum = Witness.GarmentBarycentrics.X
					+ Witness.GarmentBarycentrics.Y + Witness.GarmentBarycentrics.Z;
				const float BodyBarycentricSum = Witness.BodyBarycentrics.X
					+ Witness.BodyBarycentrics.Y + Witness.BodyBarycentrics.Z;
				const bool bWitnessGarmentIndicesValid =
					Pair.VertexBindings.IsValidIndex(Witness.GarmentRenderVertexIndices.X)
					&& Pair.VertexBindings.IsValidIndex(Witness.GarmentRenderVertexIndices.Y)
					&& Pair.VertexBindings.IsValidIndex(Witness.GarmentRenderVertexIndices.Z);
				const bool bTouchesPreserveUpstream = bWitnessGarmentIndicesValid && (
					Pair.VertexBindings[Witness.GarmentRenderVertexIndices.X].Mode
						== EEFClothingSurfaceVertexMode::PreserveUpstream
					|| Pair.VertexBindings[Witness.GarmentRenderVertexIndices.Y].Mode
						== EEFClothingSurfaceVertexMode::PreserveUpstream
					|| Pair.VertexBindings[Witness.GarmentRenderVertexIndices.Z].Mode
						== EEFClothingSurfaceVertexMode::PreserveUpstream);
				if (Witness.GarmentTriangleIndex < 0
					|| Witness.GarmentTriangleIndex >= Pair.GarmentTopology.TriangleCount
					|| Witness.GarmentRenderVertexIndices.X < 0
					|| Witness.GarmentRenderVertexIndices.Y < 0
					|| Witness.GarmentRenderVertexIndices.Z < 0
					|| Witness.GarmentRenderVertexIndices.X >= Pair.GarmentTopology.RenderVertexCount
					|| Witness.GarmentRenderVertexIndices.Y >= Pair.GarmentTopology.RenderVertexCount
					|| Witness.GarmentRenderVertexIndices.Z >= Pair.GarmentTopology.RenderVertexCount
					|| !bWitnessGarmentIndicesValid
					|| Witness.BodyRenderVertexIndices.X < 0
					|| Witness.BodyRenderVertexIndices.Y < 0
					|| Witness.BodyRenderVertexIndices.Z < 0
					|| Witness.BodyRenderVertexIndices.X >= Pair.BodyTopology.RenderVertexCount
					|| Witness.BodyRenderVertexIndices.Y >= Pair.BodyTopology.RenderVertexCount
					|| Witness.BodyRenderVertexIndices.Z >= Pair.BodyTopology.RenderVertexCount
					|| Witness.GarmentBarycentrics.ContainsNaN()
					|| Witness.BodyBarycentrics.ContainsNaN()
					|| !FMath::IsNearlyEqual(GarmentBarycentricSum, 1.0f, 1.e-3f)
					|| !FMath::IsNearlyEqual(BodyBarycentricSum, 1.0f, 1.e-3f)
					|| !FMath::IsFinite(Witness.TargetClearanceCm)
					|| Witness.TargetClearanceCm <= 0.0f
					|| !FMath::IsFinite(Witness.MaximumCorrectionCm)
					|| Witness.MaximumCorrectionCm <= 0.0f
					|| bTouchesPreserveUpstream)
				{
					OutError = FString::Printf(
						TEXT("V26 surface binding has an invalid witness for LOD pair %d/%d."),
						Pair.GarmentTopology.LODIndex,
						Pair.BodyTopology.LODIndex);
					return false;
				}
			}
		}
		return true;
	}

	static bool IsAllowedNativeSourceOutputRoot(const FString& Root)
	{
		const FString AllowedRoot(EFClothingMorphV3::CompiledOutputRoot);
		return (Root == AllowedRoot
				|| Root.StartsWith(AllowedRoot + TEXT("/"), ESearchCase::CaseSensitive))
			&& !Root.Contains(TEXT(".."));
	}

	static FString BuildNativeSourceRowFingerprint(
		const FEFClothingGarmentRow& Row,
		const FEFClothingNativeSourceCompileOptions& Options)
	{
		// Runtime and editor must compare the exact same authoring fingerprint.
		// Compile-option budgets are certified independently in every LOD pair
		// below, so they must not create a second, runtime-invisible row hash.
		(void)Options;
		return Row.BuildCompileFingerprint();
	}

	static bool NativeTopologyMatches(
		const FEFClothingSurfaceTopologyFingerprint& Stored,
		const FSurfaceRenderLOD& Actual)
	{
		return Stored.LODIndex == Actual.Topology.LODIndex
			&& Stored.RenderVertexCount == Actual.Topology.RenderVertexCount
			&& Stored.RenderIndexCount == Actual.Topology.RenderIndexCount
			&& Stored.TriangleCount == Actual.Topology.TriangleCount
			&& Stored.SectionCount == Actual.Topology.SectionCount
			&& Stored.TopologyFingerprint == Actual.Topology.TopologyFingerprint
			&& Stored.ContentFingerprint == Actual.Topology.ContentFingerprint;
	}

	static bool ValidateNativeSourceBindingInternal(
		const UEFClothingSurfaceBinding* Binding,
		const FEFClothingGarmentRow& Row,
		USkeletalMesh* SourceGarment,
		USkeletalMesh* BodySurface,
		USkeletalMesh* CompatibilityReference,
		const FEFClothingNativeSourceCompileOptions& Options,
		FString& OutError)
	{
		OutError.Reset();
		if (!IsValid(Binding)
			|| !IsValid(SourceGarment)
			|| !IsValid(BodySurface)
			|| !IsValid(CompatibilityReference)
			|| Binding->CompilerVersion != EFClothingMorphV3::CompilerVersion
			|| Binding->SchemaVersion != EFClothingMorphV3::SurfaceBindingSchemaVersion
			|| !Binding->BuildGuid.IsValid()
			|| Binding->GarmentId != Row.GarmentId
			|| Binding->GarmentCompileFingerprint
				!= BuildNativeSourceRowFingerprint(Row, Options)
			|| Binding->SourceGarment.ToSoftObjectPath() != FSoftObjectPath(SourceGarment)
			|| !Binding->FittedGarment.IsNull()
			|| Binding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodySurface)
			|| Binding->LODPairBindings.IsEmpty())
		{
			OutError = TEXT("V3 native-source binding identity, schema or Director fingerprint is stale.");
			return false;
		}

		USkeleton* SharedSkeleton = SourceGarment->GetSkeleton();
		if (!IsValid(SharedSkeleton)
			|| BodySurface->GetSkeleton() != SharedSkeleton
			|| CompatibilityReference->GetSkeleton() != SharedSkeleton
			|| Binding->SourceContentFingerprint
				!= EFClothingSkeleton::BuildContentFingerprint(SourceGarment)
			|| Binding->BodyContentFingerprint
				!= EFClothingSkeleton::BuildContentFingerprint(BodySurface)
			|| Binding->SourceSkeletonFingerprint
				!= EFClothingSkeleton::BuildFingerprint(SourceGarment)
			|| Binding->BodySkeletonFingerprint
				!= EFClothingSkeleton::BuildFingerprint(BodySurface)
			|| Binding->SharedSkeletonFingerprint
				!= EFClothingSkeleton::BuildSharedSkeletonFingerprint(SharedSkeleton)
			|| !Binding->FittedContentFingerprint.IsEmpty()
			|| !Binding->FittedSkeletonFingerprint.IsEmpty())
		{
			OutError = TEXT("V3 native-source binding content or shared-skeleton fingerprint is stale.");
			return false;
		}

		TArray<FName> ExpectedExcludedSlots = Row.GetEffectiveBodySectionsToExclude();
		CanonicalizeMaterialSlotNames(ExpectedExcludedSlots);
		TArray<FName> StoredExcludedSlots = Binding->ExcludedBodySurfaceMaterialSlots;
		CanonicalizeMaterialSlotNames(StoredExcludedSlots);
		if (StoredExcludedSlots != ExpectedExcludedSlots)
		{
			OutError = TEXT("V3 native-source binding body-surface exclusions are stale.");
			return false;
		}

		// V3 is native-source authoritative. Legacy V2 clearance fields are
		// intentionally ignored so the Director's per-garment runtime control is
		// the only source of visible spacing.
		const float ExpectedBaseClearanceCm =
			EFClothingMorphV3::DefaultCollisionClearanceCm;
		const float ExpectedTargetClearanceCm = ExpectedBaseClearanceCm
			+ EFClothingMorphV3::CompiledClearanceReserveCm;
		const float ExpectedMaximumCorrectionCm =
			EFClothingMorphV26::IsAutomaticCentimeterValue(Row.MaximumCorrectionCm)
				? Options.MaximumPushCm
				: Row.MaximumCorrectionCm;
		if (!FMath::IsFinite(ExpectedBaseClearanceCm)
			|| !FMath::IsFinite(ExpectedMaximumCorrectionCm)
			|| ExpectedMaximumCorrectionCm <= 0.0f)
		{
			OutError = TEXT("V3 native-source compile-option clearance budget is invalid.");
			return false;
		}

		FSkinnedAssetCompilingManager::Get().FinishCompilation({SourceGarment, BodySurface});
		const FSkeletalMeshRenderData* GarmentRenderData = SourceGarment->GetResourceForRendering();
		const FSkeletalMeshRenderData* BodyRenderData = BodySurface->GetResourceForRendering();
		if (!GarmentRenderData || !BodyRenderData)
		{
			OutError = TEXT("V3 native-source freshness check could not access cooked render data.");
			return false;
		}
		const int32 ExpectedPairCount = GarmentRenderData->LODRenderData.Num()
			* BodyRenderData->LODRenderData.Num();
		if (Binding->LODPairBindings.Num() != ExpectedPairCount)
		{
			OutError = FString::Printf(
				TEXT("V3 native-source binding has %d LOD pairs; exact source/body topology requires %d."),
				Binding->LODPairBindings.Num(),
				ExpectedPairCount);
			return false;
		}

		TMap<int32, FSurfaceRenderLOD> GarmentLODs;
		TMap<int32, FSurfaceRenderLOD> BodyLODs;
		TSet<uint64> UniquePairs;
		for (const FEFClothingSurfaceLODPairBinding& Pair : Binding->LODPairBindings)
		{
			const int32 GarmentLODIndex = Pair.GarmentTopology.LODIndex;
			const int32 BodyLODIndex = Pair.BodyTopology.LODIndex;
			const uint64 PairKey =
				(static_cast<uint64>(static_cast<uint32>(GarmentLODIndex)) << 32)
				| static_cast<uint32>(BodyLODIndex);
			if (GarmentLODIndex < 0
				|| BodyLODIndex < 0
				|| UniquePairs.Contains(PairKey)
				|| !Pair.bCertified
				|| !FMath::IsNearlyEqual(
					Pair.BaseClearanceCm,
					ExpectedBaseClearanceCm,
					1.e-4f)
				|| !FMath::IsNearlyEqual(
					Pair.CompiledReserveCm,
					EFClothingMorphV3::CompiledClearanceReserveCm,
					1.e-4f)
				|| Pair.Metrics.InvalidAnchorCount != 0
				|| Pair.Metrics.DegenerateBodyTriangleCount != 0
				|| !FMath::IsFinite(Pair.Metrics.MinimumRestSignedGapCm)
				|| !FMath::IsFinite(Pair.Metrics.MaximumInitialCorrectionCm)
				|| Pair.Metrics.MaximumInitialCorrectionCm < 0.0f
				|| Pair.VertexBindings.Num() != Pair.GarmentTopology.RenderVertexCount
				|| Pair.Metrics.BoundRenderVertexCount != Pair.VertexBindings.Num()
				|| Pair.CandidateTriangles.IsEmpty()
				|| Pair.Witnesses.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("V3 native-source binding has an incomplete or duplicate LOD pair %d/%d."),
					GarmentLODIndex,
					BodyLODIndex);
				return false;
			}
			UniquePairs.Add(PairKey);

			if (!GarmentLODs.Contains(GarmentLODIndex))
			{
				FSurfaceRenderLOD ActualGarmentLOD;
				if (!BuildSurfaceRenderLOD(
					SourceGarment,
					GarmentLODIndex,
					{},
					Row.NativeSkinWeightProfile,
					ActualGarmentLOD,
					OutError))
				{
					return false;
				}
				GarmentLODs.Add(GarmentLODIndex, MoveTemp(ActualGarmentLOD));
			}
			if (!BodyLODs.Contains(BodyLODIndex))
			{
				FSurfaceRenderLOD ActualBodyLOD;
				if (!BuildSurfaceRenderLOD(
					BodySurface,
					BodyLODIndex,
					ExpectedExcludedSlots,
					NAME_None,
					ActualBodyLOD,
					OutError))
				{
					return false;
				}
				BodyLODs.Add(BodyLODIndex, MoveTemp(ActualBodyLOD));
			}
			if (!NativeTopologyMatches(Pair.GarmentTopology, GarmentLODs.FindChecked(GarmentLODIndex))
				|| !NativeTopologyMatches(Pair.BodyTopology, BodyLODs.FindChecked(BodyLODIndex)))
			{
				OutError = FString::Printf(
					TEXT("V3 native-source render topology changed for LOD pair %d/%d."),
					GarmentLODIndex,
					BodyLODIndex);
				return false;
			}

			for (int32 VertexIndex = 0; VertexIndex < Pair.VertexBindings.Num(); ++VertexIndex)
			{
				const FEFClothingSurfaceVertexBinding& Vertex = Pair.VertexBindings[VertexIndex];
				const bool bPreserve = Vertex.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream;
				const float BarycentricSum = Vertex.BodyBarycentrics.X
					+ Vertex.BodyBarycentrics.Y + Vertex.BodyBarycentrics.Z;
				if (Vertex.GarmentRenderVertexIndex != VertexIndex
					|| (!bPreserve && Vertex.Mode != EEFClothingSurfaceVertexMode::CollisionOnly)
					|| !FMath::IsNearlyZero(Vertex.FollowWeight, 1.e-6f)
					|| Vertex.ThicknessReferenceRenderVertexIndex != INDEX_NONE
					|| Vertex.bOuterThicknessLayer
					|| Vertex.BodyRenderVertexIndices.X < 0
					|| Vertex.BodyRenderVertexIndices.Y < 0
					|| Vertex.BodyRenderVertexIndices.Z < 0
					|| Vertex.BodyRenderVertexIndices.X >= Pair.BodyTopology.RenderVertexCount
					|| Vertex.BodyRenderVertexIndices.Y >= Pair.BodyTopology.RenderVertexCount
					|| Vertex.BodyRenderVertexIndices.Z >= Pair.BodyTopology.RenderVertexCount
					|| !FMath::IsNearlyEqual(BarycentricSum, 1.0f, 1.e-3f)
					|| !FMath::IsFinite(Vertex.RestSignedGapCm)
					|| !FMath::IsFinite(Vertex.TargetClearanceCm)
					|| !FMath::IsNearlyEqual(
						Vertex.TargetClearanceCm,
						ExpectedTargetClearanceCm,
						1.e-4f)
					|| !FMath::IsFinite(Vertex.MaximumCorrectionCm)
					|| !FMath::IsNearlyEqual(
						Vertex.MaximumCorrectionCm,
						ExpectedMaximumCorrectionCm,
						1.e-4f)
					|| (!bPreserve
						&& FMath::Max(0.0f, Vertex.TargetClearanceCm - Vertex.RestSignedGapCm)
							> Vertex.MaximumCorrectionCm + 1.e-4f))
				{
					OutError = FString::Printf(
						TEXT("V3 native-source LOD pair %d/%d has invalid vertex binding %d."),
						GarmentLODIndex,
						BodyLODIndex,
						VertexIndex);
					return false;
				}
			}
			for (int32 WitnessIndex = 0; WitnessIndex < Pair.Witnesses.Num(); ++WitnessIndex)
			{
				const FEFClothingSurfaceWitness& Witness = Pair.Witnesses[WitnessIndex];
				const float GarmentBarycentricSum = Witness.GarmentBarycentrics.X
					+ Witness.GarmentBarycentrics.Y + Witness.GarmentBarycentrics.Z;
				const float BodyBarycentricSum = Witness.BodyBarycentrics.X
					+ Witness.BodyBarycentrics.Y + Witness.BodyBarycentrics.Z;
				if (Witness.GarmentTriangleIndex < 0
					|| Witness.GarmentRenderVertexIndices.X < 0
					|| Witness.GarmentRenderVertexIndices.Y < 0
					|| Witness.GarmentRenderVertexIndices.Z < 0
					|| Witness.GarmentRenderVertexIndices.X >= Pair.GarmentTopology.RenderVertexCount
					|| Witness.GarmentRenderVertexIndices.Y >= Pair.GarmentTopology.RenderVertexCount
					|| Witness.GarmentRenderVertexIndices.Z >= Pair.GarmentTopology.RenderVertexCount
					|| Witness.BodyRenderVertexIndices.X < 0
					|| Witness.BodyRenderVertexIndices.Y < 0
					|| Witness.BodyRenderVertexIndices.Z < 0
					|| Witness.BodyRenderVertexIndices.X >= Pair.BodyTopology.RenderVertexCount
					|| Witness.BodyRenderVertexIndices.Y >= Pair.BodyTopology.RenderVertexCount
					|| Witness.BodyRenderVertexIndices.Z >= Pair.BodyTopology.RenderVertexCount
					|| !FMath::IsNearlyEqual(GarmentBarycentricSum, 1.0f, 1.e-3f)
					|| !FMath::IsNearlyEqual(BodyBarycentricSum, 1.0f, 1.e-3f)
					|| !FMath::IsNearlyEqual(
						Witness.TargetClearanceCm,
						ExpectedTargetClearanceCm,
						1.e-4f)
					|| !FMath::IsNearlyEqual(
						Witness.MaximumCorrectionCm,
						ExpectedMaximumCorrectionCm,
						1.e-4f))
				{
					OutError = FString::Printf(
						TEXT("V3 native-source LOD pair %d/%d has invalid witness %d."),
						GarmentLODIndex,
						BodyLODIndex,
						WitnessIndex);
					return false;
				}
			}
		}
		return true;
	}

	static bool BuildNativeSourceBindingAsset(
		const FEFClothingGarmentRow& Row,
		USkeletalMesh* SourceGarment,
		USkeletalMesh* BodySurface,
		USkeletalMesh* CompatibilityReference,
		const FEFClothingNativeSourceCompileOptions& Options,
		UEFClothingSurfaceBinding*& OutBinding,
		FString& OutError)
	{
		OutBinding = nullptr;
		OutError.Reset();
		if (!IsValid(SourceGarment)
			|| !IsValid(BodySurface)
			|| !IsValid(CompatibilityReference)
			|| !IsValid(SourceGarment->GetSkeleton())
			|| SourceGarment->GetSkeleton() != BodySurface->GetSkeleton()
			|| SourceGarment->GetSkeleton() != CompatibilityReference->GetSkeleton())
		{
			OutError = TEXT("V3 binding requires source, body and compatibility meshes to share the exact protected USkeleton object.");
			return false;
		}

		FSkinnedAssetCompilingManager::Get().FinishCompilation({SourceGarment, BodySurface});
		FSkeletalMeshRenderData* GarmentRenderData = SourceGarment->GetResourceForRendering();
		FSkeletalMeshRenderData* BodyRenderData = BodySurface->GetResourceForRendering();
		if (!GarmentRenderData
			|| !BodyRenderData
			|| GarmentRenderData->LODRenderData.IsEmpty()
			|| BodyRenderData->LODRenderData.IsEmpty())
		{
			OutError = TEXT("V3 binding could not load source/body render LOD data.");
			return false;
		}

		TArray<FName> ExcludedSurfaceSlots = Row.GetEffectiveBodySectionsToExclude();
		CanonicalizeMaterialSlotNames(ExcludedSurfaceSlots);
		// Any excluded body section defines a generic preserve-upstream domain.
		// This intentionally contains no garment, anatomy-tag or product-specific
		// branch: every catalog row receives identical solver behavior.
		const bool bPreserveExcludedAnatomyUpstream = !ExcludedSurfaceSlots.IsEmpty();

		// Never bake a hidden V2 gap into a V3 binding. The exact native mesh is
		// preserved at zero clearance and each garment adds only its visible
		// Director runtime values after animation.
		const float BaseClearanceCm =
			EFClothingMorphV3::DefaultCollisionClearanceCm;
		const float MaximumCorrectionCm = EFClothingMorphV26::IsAutomaticCentimeterValue(
			Row.MaximumCorrectionCm)
			? Options.MaximumPushCm
			: Row.MaximumCorrectionCm;
		if (!FMath::IsFinite(BaseClearanceCm)
			|| BaseClearanceCm < 0.0f
			|| !FMath::IsFinite(MaximumCorrectionCm)
			|| MaximumCorrectionCm
				< BaseClearanceCm + EFClothingMorphV3::CompiledClearanceReserveCm)
		{
			OutError = TEXT("V3 binding clearance/correction budget is invalid.");
			return false;
		}

		const FGuid BuildGuid = FGuid::NewGuid();
		const FString PublicationKey = BuildGuid.ToString(EGuidFormats::Digits);
		const FString BindingName = FString::Printf(
			TEXT("DA_%s_%s_%s_%s_EFV3Surface_%s"),
			*SanitizeAssetName(Row.GarmentId.ToString()),
			*BuildSourceKey(SourceGarment),
			*SanitizeAssetName(BodySurface->GetName()),
			*BuildSourceKey(BodySurface),
			*PublicationKey);
		const FString BindingObjectPath = FString::Printf(
			TEXT("%s/%s.%s"),
			*Options.OutputRoot,
			*BindingName,
			*BindingName);
		if (LoadObject<UEFClothingSurfaceBinding>(nullptr, *BindingObjectPath))
		{
			OutError = FString::Printf(
				TEXT("Fresh V3 publication key collided with %s."),
				*BindingObjectPath);
			return false;
		}
		UEFClothingSurfaceBinding* Binding = FindOrCreateDataAsset<UEFClothingSurfaceBinding>(
			Options.OutputRoot,
			BindingName);
		if (!IsValid(Binding))
		{
			OutError = TEXT("Could not create the immutable V3 source-surface binding asset.");
			return false;
		}

		Binding->GarmentId = Row.GarmentId;
		Binding->GarmentCompileFingerprint = BuildNativeSourceRowFingerprint(Row, Options);
		Binding->SourceGarment = SourceGarment;
		Binding->FittedGarment.Reset();
		Binding->BodySurface = BodySurface;
		Binding->BuildGuid = BuildGuid;
		Binding->CompilerVersion = EFClothingMorphV3::CompilerVersion;
		Binding->SchemaVersion = EFClothingMorphV3::SurfaceBindingSchemaVersion;
		Binding->SourceContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(SourceGarment);
		Binding->FittedContentFingerprint.Reset();
		Binding->BodyContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(BodySurface);
		Binding->SourceSkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(SourceGarment);
		Binding->FittedSkeletonFingerprint.Reset();
		Binding->BodySkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(BodySurface);
		Binding->SharedSkeletonFingerprint =
			EFClothingSkeleton::BuildSharedSkeletonFingerprint(SourceGarment->GetSkeleton());
		Binding->ExcludedBodySurfaceMaterialSlots = ExcludedSurfaceSlots;
		Binding->LODPairBindings.Reset();

		FEFClothingGarmentRow CollisionOnlyRow = Row;
		CollisionOnlyRow.FitPolicy = EEFClothingFitPolicy::Loose;
		CollisionOnlyRow.bCreateThicknessShell = false;
		CollisionOnlyRow.ShellThicknessCm = 0.0f;
		for (int32 GarmentLODIndex = 0;
			GarmentLODIndex < GarmentRenderData->LODRenderData.Num();
			++GarmentLODIndex)
		{
			FSurfaceRenderLOD GarmentLOD;
			if (!BuildSurfaceRenderLOD(
				SourceGarment,
				GarmentLODIndex,
				{},
				Row.NativeSkinWeightProfile,
				GarmentLOD,
				OutError))
			{
				return false;
			}

			for (int32 BodyLODIndex = 0;
				BodyLODIndex < BodyRenderData->LODRenderData.Num();
				++BodyLODIndex)
			{
				FSurfaceRenderLOD BodyLOD;
				if (!BuildSurfaceRenderLOD(
					BodySurface,
					BodyLODIndex,
					ExcludedSurfaceSlots,
					NAME_None,
					BodyLOD,
					OutError))
				{
					return false;
				}

				FDynamicMesh3 ExcludedAnatomyMesh;
				const FDynamicMesh3* ExcludedAnatomyMeshPtr = nullptr;
				if (bPreserveExcludedAnatomyUpstream)
				{
					FSurfaceRenderLOD FullBodyLOD;
					if (!BuildSurfaceRenderLOD(
						BodySurface,
						BodyLODIndex,
						{},
						NAME_None,
						FullBodyLOD,
						OutError))
					{
						return false;
					}
					int32 ExcludedTriangleCount = 0;
					if (!BuildExcludedAnatomySurface(
						FullBodyLOD,
						BodyLOD,
						ExcludedAnatomyMesh,
						ExcludedTriangleCount,
						OutError))
					{
						return false;
					}
					ExcludedAnatomyMeshPtr = &ExcludedAnatomyMesh;
				}

				FEFClothingSurfaceLODPairBinding& Pair =
					Binding->LODPairBindings.AddDefaulted_GetRef();
				if (!BuildSurfaceLODPairBinding(
					GarmentLOD,
					BodyLOD,
					ExcludedAnatomyMeshPtr,
					CollisionOnlyRow,
					BaseClearanceCm,
					EFClothingMorphV3::CompiledClearanceReserveCm,
					MaximumCorrectionCm,
					Pair,
					OutError,
					true))
				{
					return false;
				}
			}
		}

		Binding->MarkPackageDirty();
		if (!SaveAsset(Binding, OutError))
		{
			return false;
		}
		if (!ValidateNativeSourceBindingInternal(
			Binding,
			Row,
			SourceGarment,
			BodySurface,
			CompatibilityReference,
			Options,
			OutError))
		{
			return false;
		}
		OutBinding = Binding;
		return true;
	}
}

FGameplayTagContainer UEFClothingFitCompilerLibrary::MakeGameplayTagContainerFromNames(
	const TArray<FName>& TagNames)
{
	FGameplayTagContainer Result;
	for (const FName TagName : TagNames)
	{
		if (TagName.IsNone())
		{
			continue;
		}
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(TagName, false);
		if (Tag.IsValid())
		{
			Result.AddTag(Tag);
		}
	}
	return Result;
}

bool UEFClothingFitCompilerLibrary::UpgradeDirectorIdentityToSchema2(
	UEFClothingMorphDirectorPolicy* Director)
{
	return UpgradeDirectorIdentityToSchema3(Director);
}

bool UEFClothingFitCompilerLibrary::UpgradeDirectorIdentityToSchema3(
	UEFClothingMorphDirectorPolicy* Director)
{
	if (!IsValid(Director)
		|| (Director->SchemaVersion != 1
			&& Director->SchemaVersion != 2
			&& Director->SchemaVersion != 3)
		|| (!Director->DirectorId.IsNone() && Director->DirectorId != TEXT("EFClothingMorphV2")))
	{
		return false;
	}
	Director->SchemaVersion = 3;
	Director->DirectorId = TEXT("EFClothingMorphV2");
	Director->AuthoringGuide = FText::FromString(TEXT(
		"1) Create one index per garment/body pair. 2) Select and edit the original garment mesh with Unreal's native tools; never edit a generated SK_ mesh. "
		"3) Runtime Clearance moves only that garment outward. 4) Enable Adjustable Thickness once and compile its paired topology. "
		"Visible Thickness then updates immediately per garment without rebuilding or hiding it; 0.05 cm is thin fabric and 0.20 cm is visibly thicker. "
		"Structural shell options remain advanced compile settings. Every control belongs to its expanded garment index; this Director has no global garment tuning."));
	Director->MarkPackageDirty();
	return true;
}

bool UEFClothingFitCompilerLibrary::UpgradeDirectorIdentityToSchema4(
	UEFClothingMorphDirectorPolicy* Director)
{
	if (!IsValid(Director)
		|| Director->SchemaVersion < 1
		|| Director->SchemaVersion > 4
		|| (!Director->DirectorId.IsNone()
			&& Director->DirectorId != TEXT("EFClothingMorphV2")
			&& Director->DirectorId != TEXT("EFClothingMorphV3")))
	{
		return false;
	}

	Director->Modify();
	for (FEFClothingGarmentRow& Garment : Director->Garments)
	{
		// Backend is an internal implementation detail in the V3 Director UI.
		// Every enabled V3 row therefore selects the native-source surface guard
		// explicitly instead of inheriting a serialized V2 fallback value.
		Garment.Backend = EEFClothingSurfaceBackend::SurfaceWrapGPU;
		Garment.BodySectionsToExclude = Garment.GetEffectiveBodySectionsToExclude();
		EFClothingFitCompilerPrivate::CanonicalizeMaterialSlotNames(
			Garment.BodySectionsToExclude);

		// Schema <= 3 used this legacy switch as the authority for the runtime
		// clearance. Preserve that explicit opt-out during the one-time migration.
		if (!Garment.bEnableRuntimeTuning)
		{
			Garment.AdditionalClearanceCm = 0.0f;
		}

		// V3 never generates or swaps a thickened Skeletal Mesh. ShellThicknessCm
		// remains a non-destructive runtime surface-inflate value, while this old
		// topology-generation switch is permanently retired for the migrated row.
		Garment.bCreateThicknessShell = false;
		Garment.bFailClosedOnMissingLOD = false;
	}

	Director->SchemaVersion = 4;
	Director->DirectorId = TEXT("EFClothingMorphV3");
	Director->AuthoringGuide = FText::FromString(TEXT(
		"Add one entry to Garments for each garment/body pair. Editable Garment Mesh is always the authoritative source and may be changed with Unreal Engine's native Skeletal Mesh tools. "
		"Skin Clearance and Surface Inflate are immediate, non-destructive runtime controls owned by that garment entry. Native UE Offset values do nothing until Apply Native Offset to Editable Mesh is pressed. "
		"Refresh Binding rebuilds only project-owned runtime data; it never edits the body, its weights, or the shared skeleton."));
	Director->MarkPackageDirty();
	return true;
}

FEFClothingFitCompileResult UEFClothingFitCompilerLibrary::CompileFitProfile(
	USkeletalMesh* SourceGarment,
	USkeletalMesh* BodySurface,
	USkeletalMesh* CompatibilityReference,
	FEFClothingFitCompileOptions Options)
{
	using namespace EFClothingFitCompilerPrivate;
	using namespace UE::Geometry;

	FEFClothingFitCompileResult Result;
	bool bProtectedInputGuardArmed = false;
	USkeletalMesh* GuardedSource = nullptr;
	USkeletalMesh* GuardedBody = nullptr;
	USkeletalMesh* GuardedCompatibility = nullptr;
	USkeleton* GuardedSharedSkeleton = nullptr;
	FString GuardedSourceSkeletonFingerprint;
	FString GuardedBodySkeletonFingerprint;
	FString GuardedCompatibilitySkeletonFingerprint;
	FString GuardedSharedSkeletonFingerprint;
	FString GuardedSharedSkeletonEditorFingerprint;
	FString GuardedSourceContentFingerprint;
	FString GuardedBodyContentFingerprint;
	FString GuardedCompatibilityContentFingerprint;
	bool bGuardedSourcePackageDirty = false;
	bool bGuardedBodyPackageDirty = false;
	bool bGuardedCompatibilityPackageDirty = false;
	bool bGuardedSkeletonPackageDirty = false;
	auto ProtectedInputsUnchanged = [&]() -> bool
	{
		return !bProtectedInputGuardArmed
			|| (IsValid(GuardedSource)
				&& IsValid(GuardedBody)
				&& IsValid(GuardedCompatibility)
				&& IsValid(GuardedSharedSkeleton)
				&& GuardedSource->GetSkeleton() == GuardedSharedSkeleton
				&& GuardedBody->GetSkeleton() == GuardedSharedSkeleton
				&& GuardedCompatibility->GetSkeleton() == GuardedSharedSkeleton
				&& EFClothingSkeleton::BuildFingerprint(GuardedSource) == GuardedSourceSkeletonFingerprint
				&& EFClothingSkeleton::BuildFingerprint(GuardedBody) == GuardedBodySkeletonFingerprint
				&& EFClothingSkeleton::BuildFingerprint(GuardedCompatibility) == GuardedCompatibilitySkeletonFingerprint
				&& EFClothingSkeleton::BuildSharedSkeletonFingerprint(GuardedSharedSkeleton) == GuardedSharedSkeletonFingerprint
				&& EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(GuardedSharedSkeleton)
					== GuardedSharedSkeletonEditorFingerprint
				&& EFClothingSkeleton::BuildContentFingerprint(GuardedSource) == GuardedSourceContentFingerprint
				&& EFClothingSkeleton::BuildContentFingerprint(GuardedBody) == GuardedBodyContentFingerprint
				&& EFClothingSkeleton::BuildContentFingerprint(GuardedCompatibility)
					== GuardedCompatibilityContentFingerprint
				&& GuardedSource->GetOutermost()->IsDirty() == bGuardedSourcePackageDirty
				&& GuardedBody->GetOutermost()->IsDirty() == bGuardedBodyPackageDirty
				&& GuardedCompatibility->GetOutermost()->IsDirty() == bGuardedCompatibilityPackageDirty
				&& GuardedSharedSkeleton->GetOutermost()->IsDirty() == bGuardedSkeletonPackageDirty);
	};
	auto Fail = [&Result, &ProtectedInputsUnchanged](const FString& Message)
	{
		const bool bProtectedInputsPass = ProtectedInputsUnchanged();
		Result.Report = bProtectedInputsPass
			? FString::Printf(TEXT("FAIL: %s"), *Message)
			: FString::Printf(
				TEXT("FAIL: %s | PROTECTED_INPUT_GUARD_FAIL: source/body/compatibility/shared-skeleton state changed."),
				*Message);
		ensureAlwaysMsgf(
			bProtectedInputsPass,
			TEXT("EF Clothing Morph compiler detected protected input contamination on a failure path."));
		UE_LOG(LogEFClothingFitCompiler, Error, TEXT("%s"), *Result.Report);
		return Result;
	};

	if (!IsValid(SourceGarment) || !IsValid(BodySurface) || !IsValid(CompatibilityReference))
	{
		return Fail(TEXT("Source garment, body surface and compatibility reference are required."));
	}

	// Never inspect or duplicate partially compiled skeletal assets.
	FSkinnedAssetCompilingManager::Get().FinishCompilation({SourceGarment, BodySurface, CompatibilityReference});

	if (!IsAllowedOutputRoot(Options.OutputRoot))
	{
		return Fail(TEXT("OutputRoot must remain under /EFClothingMorph/_Internal/Compiled/V26."));
	}
	if (!FMath::IsFinite(Options.MinimumClearanceCm)
		|| !FMath::IsFinite(Options.MaximumPushCm)
		|| !FMath::IsFinite(Options.MinimumTransferredMorphDeltaCm)
		|| !FMath::IsFinite(Options.MaximumMorphRepairCm)
		|| Options.MinimumClearanceCm < 0.02f
		|| Options.MaximumPushCm < Options.MinimumClearanceCm
		|| Options.MinimumTransferredMorphDeltaCm <= 0.0f
		|| Options.MaximumMorphRepairCm < Options.MinimumClearanceCm
		|| Options.SmoothingIterations < 0
		|| Options.SmoothingIterations > 20
		|| Options.MaximumInfluences < 1
		|| Options.MaximumInfluences > 12
		|| Options.MaximumTransferredMorphs < 0
		|| Options.MaximumTransferredMorphs > 256
		|| Options.MorphClearanceSampleCount < 2
		|| Options.MorphClearanceSampleCount > 8)
	{
		return Fail(TEXT("Compile options contain a non-finite or out-of-contract value."));
	}
	if (!Options.bCopyBodyDeformerToDerived)
	{
		return Fail(TEXT("V26 profiles require exact body mesh-deformer parity before the late surface pass."));
	}
	if (SourceGarment == BodySurface || SourceGarment == CompatibilityReference)
	{
		return Fail(TEXT("Source garment cannot be one of the protected body/reference inputs."));
	}
	if (SourceGarment->GetNumSourceModels() != 1)
	{
		return Fail(TEXT("This compiler version certifies one-source-LOD garments only; multi-LOD input is rejected fail-closed."));
	}
	if (BodySurface->GetNumSourceModels() < 1)
	{
		return Fail(TEXT("Body surface has no source LOD0."));
	}
	if (SourceGarment->GetSkeleton() != BodySurface->GetSkeleton()
		|| SourceGarment->GetSkeleton() != CompatibilityReference->GetSkeleton())
	{
		return Fail(TEXT("All inputs must reference the exact same USkeleton object; no merge is permitted."));
	}

	FString SkeletonFailure;
	if (!EFClothingSkeleton::AreBoneHierarchiesCompatible(SourceGarment, BodySurface, &SkeletonFailure))
	{
		return Fail(FString::Printf(TEXT("Source/body reference skeleton mismatch: %s"), *SkeletonFailure));
	}
	if (!EFClothingSkeleton::AreSharedBoneHierarchiesCompatible(SourceGarment, CompatibilityReference, &SkeletonFailure))
	{
		return Fail(FString::Printf(TEXT("Source/compatibility skeleton mismatch: %s"), *SkeletonFailure));
	}

	const FString SourceFingerprintBefore = EFClothingSkeleton::BuildFingerprint(SourceGarment);
	const FString BodyFingerprintBefore = EFClothingSkeleton::BuildFingerprint(BodySurface);
	const FString CompatibilityFingerprintBefore = EFClothingSkeleton::BuildFingerprint(CompatibilityReference);
	USkeleton* const SharedSkeletonBefore = SourceGarment->GetSkeleton();
	const FString SharedSkeletonFingerprintBefore = EFClothingSkeleton::BuildSharedSkeletonFingerprint(SharedSkeletonBefore);
	const FString SharedSkeletonEditorFingerprintBefore =
		EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(SharedSkeletonBefore);
	if (SharedSkeletonFingerprintBefore.IsEmpty() || SharedSkeletonEditorFingerprintBefore.IsEmpty())
	{
		return Fail(TEXT("Could not fingerprint the shared USkeleton before compilation."));
	}
	GuardedSource = SourceGarment;
	GuardedBody = BodySurface;
	GuardedCompatibility = CompatibilityReference;
	GuardedSharedSkeleton = SharedSkeletonBefore;
	GuardedSourceSkeletonFingerprint = SourceFingerprintBefore;
	GuardedBodySkeletonFingerprint = BodyFingerprintBefore;
	GuardedCompatibilitySkeletonFingerprint = CompatibilityFingerprintBefore;
	GuardedSharedSkeletonFingerprint = SharedSkeletonFingerprintBefore;
	GuardedSharedSkeletonEditorFingerprint = SharedSkeletonEditorFingerprintBefore;
	GuardedSourceContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(SourceGarment);
	GuardedBodyContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(BodySurface);
	GuardedCompatibilityContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(CompatibilityReference);
	bGuardedSourcePackageDirty = SourceGarment->GetOutermost()->IsDirty();
	bGuardedBodyPackageDirty = BodySurface->GetOutermost()->IsDirty();
	bGuardedCompatibilityPackageDirty = CompatibilityReference->GetOutermost()->IsDirty();
	bGuardedSkeletonPackageDirty = SharedSkeletonBefore->GetOutermost()->IsDirty();
	bProtectedInputGuardArmed = true;

	FString Error;
	TArray<FName> ExcludedBodySurfaceMaterialSlots;
	TArray<FName> ExcludedBodyBoneBranches;
	TArray<FString> ExcludedBodyMorphPrefixes;
	FEFClothingGarmentRow CatalogRow;
	FName CatalogRowName = NAME_None;
	if (!ResolveCatalogSurfacePolicy(
		SourceGarment,
		BodySurface,
		ExcludedBodySurfaceMaterialSlots,
		ExcludedBodyBoneBranches,
		ExcludedBodyMorphPrefixes,
		CatalogRow,
		CatalogRowName,
		Error))
	{
		return Fail(Error);
	}
	if (SourceGarment->GetMeshClothingAssets().Num() > 0
		&& CatalogRow.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
	{
		return Fail(TEXT("Chaos Cloth garments require SurfaceWrapGPU so the unilateral constraint can execute after Chaos."));
	}
	if (CatalogRow.Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU)
	{
		Options.MinimumClearanceCm = FMath::Max(
			Options.MinimumClearanceCm,
			EFClothingMorphV26::DefaultBaseClearanceCm);
		if (!EFClothingMorphV26::IsAutomaticCentimeterValue(CatalogRow.FabricClearanceCm))
		{
			Options.MinimumClearanceCm = FMath::Max(
				Options.MinimumClearanceCm,
				CatalogRow.FabricClearanceCm);
		}
		if (!EFClothingMorphV26::IsAutomaticCentimeterValue(CatalogRow.MaximumCorrectionCm))
		{
			Options.MaximumPushCm = CatalogRow.MaximumCorrectionCm;
		}
		if (Options.MaximumPushCm
			< Options.MinimumClearanceCm + static_cast<float>(CompilerClearanceReserveCm))
		{
			return Fail(FString::Printf(
				TEXT("Catalog row %s allows %.4fcm correction but requires at least %.4fcm clearance plus reserve."),
				*CatalogRowName.ToString(),
				Options.MaximumPushCm,
				Options.MinimumClearanceCm + static_cast<float>(CompilerClearanceReserveCm)));
		}
	}
	const FString ArtifactKey = BuildArtifactKey(
		SourceGarment,
		BodySurface,
		CompatibilityReference,
		Options,
		CatalogRow,
		ExcludedBodySurfaceMaterialSlots,
		ExcludedBodyBoneBranches,
		ExcludedBodyMorphPrefixes);
	// Every invocation writes a fresh, unreferenced publication candidate. The
	// existing registry/profile/mesh stay byte-stable until the final registry
	// save commits the complete validated transaction.
	const FGuid PublicationGuid = FGuid::NewGuid();
	const FString PublicationKey = FString::Printf(
		TEXT("%s_%s"),
		*ArtifactKey,
		*PublicationGuid.ToString(EGuidFormats::Digits));
	USkeletalMesh* Derived = FindOrDuplicateDerived(SourceGarment, Options.OutputRoot, PublicationKey, Error);
	if (!Derived)
	{
		return Fail(Error);
	}
	if (Derived == SourceGarment || Derived == BodySurface || Derived == CompatibilityReference)
	{
		return Fail(TEXT("Compiler resolved a protected asset as its output."));
	}
	FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});
	if (Derived->GetSkeleton() != SharedSkeletonBefore)
	{
		return Fail(TEXT("Derived garment does not preserve the source USkeleton pointer."));
	}
	// Female and the generated garment consume the same effective ACF pose driver,
	// but each mesh contributes its own inverse bind. Rebind only this fresh output
	// to Female, including MeshDescription and inverse-bind matrices; never mutate
	// the shared USkeleton, Multiple, Female or the source garment.
	if (!RebindGeneratedMeshToBody(Derived, BodySurface, Error)
		|| Derived->GetSkeleton() != SharedSkeletonBefore
		|| !ValidateGeneratedBodyBindArtifacts(Derived, BodySurface, Error))
	{
		return Fail(FString::Printf(
			TEXT("Generated garment could not adopt the exact body bind pose: %s"),
			Error.IsEmpty() ? TEXT("unknown bind-pose mismatch") : *Error));
	}

	UDynamicMesh* BodyDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	UDynamicMesh* GarmentDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	if (!CopySourceLOD(BodySurface, BodyDynamicMesh, Error) || !CopySourceLOD(SourceGarment, GarmentDynamicMesh, Error))
	{
		return Fail(Error);
	}
	int32 ExcludedBodySurfaceTriangleCount = 0;
	FDynamicMesh3 ExcludedBodySurfaceMesh;
	if (!ExcludeBodySurfaceMaterialSlots(
		BodySurface,
		BodyDynamicMesh,
		ExcludedBodySurfaceMaterialSlots,
		ExcludedBodySurfaceTriangleCount,
		&ExcludedBodySurfaceMesh,
		Error))
	{
		return Fail(Error);
	}
	int32 SurfaceOriginalVertexCount = GarmentDynamicMesh->GetMeshRef().VertexCount();
	int32 SurfaceFinalVertexCount = SurfaceOriginalVertexCount;
	int32 SurfaceTopologySplitCount = 0;
	if (CatalogRow.Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU)
	{
		if (!DensifySurfaceGarmentTopology(
			GarmentDynamicMesh,
			SurfaceOriginalVertexCount,
			SurfaceFinalVertexCount,
			SurfaceTopologySplitCount,
			Error))
		{
			return Fail(Error);
		}
		if (SurfaceTopologySplitCount > 0)
		{
			if (SourceGarment->GetMeshClothingAssets().Num() > 0)
			{
				return Fail(TEXT("SurfaceWrap risk densification cannot publish a stale Chaos cloth topology; rebuildable clothing mappings are required for this garment."));
			}
			if (!WriteGeneratedSurfaceTopology(GarmentDynamicMesh, Derived, Error)
				|| Derived->GetSkeleton() != SharedSkeletonBefore
				|| !RebindGeneratedMeshToBody(Derived, BodySurface, Error)
				|| Derived->GetSkeleton() != SharedSkeletonBefore
				|| !ValidateGeneratedBodyBindArtifacts(Derived, BodySurface, Error))
			{
				return Fail(FString::Printf(
					TEXT("Generated SurfaceWrap topology could not preserve skeleton/body bind invariants: %s"),
					Error.IsEmpty() ? TEXT("unknown generated-topology failure") : *Error));
			}
			// The SkeletalMesh writer may canonicalize import IDs and seam groups. Use
			// the asset's committed SourceModel as the sole topology for every later
			// clearance, weight, morph, render-binding and fingerprint operation.
			UDynamicMesh* CanonicalGeneratedMesh = NewObject<UDynamicMesh>(GetTransientPackage());
			if (!CopySourceLOD(Derived, CanonicalGeneratedMesh, Error))
			{
				return Fail(FString::Printf(
					TEXT("Could not recapture committed generated SurfaceWrap topology: %s"),
					*Error));
			}
			GarmentDynamicMesh->SetMesh(CanonicalGeneratedMesh->GetMeshRef());
			SurfaceFinalVertexCount = GarmentDynamicMesh->GetMeshRef().VertexCount();
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("V26 SurfaceWrap densified generated LOD0 from %d to %d vertices with %d edge splits (max edge %.3fcm)."),
				SurfaceOriginalVertexCount,
				SurfaceFinalVertexCount,
				SurfaceTopologySplitCount,
				SurfaceRuntimeMaximumEdgeLengthCm);
		}
	}

	FString WeightMethod;
	TArray<FName> RequiredWeightedBones;
	int32 RemappedWeightedBoneCount = 0;
	int32 ReconciledSplitVertexCount = 0;
	TArray<FSurfaceCorrespondence> Correspondence;
	TArray<FVector3d> ClearanceDeltas;
	int32 PenetratingBefore = 0;
	int32 PenetratingAfter = 0;
	double MinimumBefore = 0.0;
	double MinimumAfter = 0.0;
	if (!BuildCorrespondenceAndClearance(
		BodyDynamicMesh->GetMeshRef(),
		GarmentDynamicMesh->GetMeshRef(),
		FMath::Max(static_cast<double>(Options.MinimumClearanceCm) + CompilerClearanceReserveCm, 0.02),
		FMath::Max(static_cast<double>(Options.MaximumPushCm), static_cast<double>(Options.MinimumClearanceCm)),
		FMath::Clamp(Options.SmoothingIterations, 0, 20),
		Correspondence,
		ClearanceDeltas,
		PenetratingBefore,
		PenetratingAfter,
		MinimumBefore,
		MinimumAfter,
		Error))
	{
		return Fail(FString::Printf(
			TEXT("Surface solve could not guarantee %.4fcm clearance within %.4fcm maximum push (measured %.4fcm): %s"),
			Options.MinimumClearanceCm + static_cast<float>(CompilerClearanceReserveCm),
			Options.MaximumPushCm,
			MinimumAfter,
			Error.IsEmpty() ? TEXT("clearance gate failed") : *Error));
	}

	FThicknessShellCompileData ThicknessShell;
	if (CatalogRow.bCreateThicknessShell)
	{
		if (SourceGarment->GetMeshClothingAssets().Num() > 0)
		{
			return Fail(TEXT("Real-thickness compilation cannot publish stale Chaos cloth mappings; this garment must be rebuilt without authored Chaos topology first."));
		}
		const TArray<FSurfaceCorrespondence> PreShellCorrespondence = Correspondence;
		const TArray<FVector3d> PreShellClearanceDeltas = ClearanceDeltas;
		const double ExcludedRegionCertificationRadiusCm = FMath::Min(
			3.0,
			static_cast<double>(Options.MinimumClearanceCm)
				+ CompilerClearanceReserveCm
				+ static_cast<double>(
					EFClothingMorphV26::CompiledThicknessReferenceCm)
				+ CompilerClearanceReserveCm);
		if (!BuildGeneratedThicknessShell(
			GarmentDynamicMesh,
			CatalogRow,
			PreShellCorrespondence,
			ExcludedBodySurfaceMesh.TriangleCount() > 0
				? &ExcludedBodySurfaceMesh
				: nullptr,
			ExcludedBodySurfaceTriangleCount,
			ExcludedRegionCertificationRadiusCm,
			ThicknessShell,
			Error))
		{
			return Fail(FString::Printf(
				TEXT("Generated real-thickness shell failed: %s"),
				*Error));
		}
		if (!WriteGeneratedSurfaceTopology(GarmentDynamicMesh, Derived, Error, true)
			|| Derived->GetSkeleton() != SharedSkeletonBefore
			|| !RebindGeneratedMeshToBody(Derived, BodySurface, Error)
			|| Derived->GetSkeleton() != SharedSkeletonBefore
			|| !ValidateGeneratedBodyBindArtifacts(Derived, BodySurface, Error))
		{
			return Fail(FString::Printf(
				TEXT("Generated real-thickness topology could not preserve skeleton/body bind invariants: %s"),
				Error.IsEmpty() ? TEXT("unknown shell publication failure") : *Error));
		}
		UDynamicMesh* CanonicalShellMesh = NewObject<UDynamicMesh>(GetTransientPackage());
		if (!CopySourceLOD(Derived, CanonicalShellMesh, Error))
		{
			return Fail(FString::Printf(
				TEXT("Could not recapture the committed real-thickness topology: %s"),
				*Error));
		}
		GarmentDynamicMesh->SetMesh(CanonicalShellMesh->GetMeshRef());
		if (!RebuildThicknessShellPairing(
			GarmentDynamicMesh->GetMeshRef(),
			ThicknessShell,
			Error))
		{
			return Fail(FString::Printf(
				TEXT("Committed real-thickness pairing is invalid: %s"),
				*Error));
		}
		// The skeletal-mesh writer is allowed to canonicalize seam vertices and
		// triangle IDs. Re-certify the committed SourceModel and store only this
		// post-publication evidence so editor validation and cooked render data agree.
		const FDynamicMesh3& CanonicalShell = GarmentDynamicMesh->GetMeshRef();
		FDynamicMesh3 CanonicalInnerLayer;
		TMap<FString, FSourceIntersectionWitnessEvidence>
			CanonicalBaselineEvidenceBySourcePair;
		int32 CanonicalBaselinePairCount = 0;
		FMeshBoundaryLoops CanonicalBoundaryLoops(&CanonicalShell, true);
		int32 CanonicalDegenerateTriangleCount = 0;
		for (const int32 TriangleID : CanonicalShell.TriangleIndicesItr())
		{
			CanonicalDegenerateTriangleCount +=
				CanonicalShell.GetTriArea(TriangleID) <= 1.e-10 ? 1 : 0;
		}
		const double CanonicalBaselineInheritanceRadiusCm =
			SurfaceRuntimeMaximumEdgeLengthCm * 0.5
			+ ThicknessShell.RequestedThicknessCm
			+ 0.01;
		const double CanonicalLocalRepairThicknessCeilingCm = FMath::Max(
			1.e-4,
			ThicknessShell.RequestedThicknessCm * 0.15);
		FThicknessShellIntersectionAudit CanonicalIntersectionAudit;
		FString CanonicalIntersectionPolicyError;
		const bool bCanonicalGeometryPass =
			!CanonicalBoundaryLoops.bAborted
			&& CanonicalBoundaryLoops.Loops.IsEmpty()
			&& CanonicalBoundaryLoops.Spans.IsEmpty()
			&& CanonicalDegenerateTriangleCount == 0
			&& ExtractThicknessShellInnerLayer(
				CanonicalShell,
				ThicknessShell.PreShellVertexCount,
				CanonicalInnerLayer,
				Error)
			&& CollectSelfIntersectionWitnessEvidence(
				CanonicalInnerLayer,
				ThicknessShell.PreShellVertexCount,
				CanonicalBaselineEvidenceBySourcePair,
				CanonicalBaselinePairCount,
				Error)
			&& MeasureThicknessShellIntersectionsV3(
				CanonicalShell,
				ThicknessShell.PreShellVertexCount,
				CanonicalBaselineEvidenceBySourcePair,
				CanonicalBaselineInheritanceRadiusCm,
				ExcludedBodySurfaceMesh.TriangleCount() > 0
					? &ExcludedBodySurfaceMesh
					: nullptr,
				ExcludedRegionCertificationRadiusCm,
				CanonicalLocalRepairThicknessCeilingCm,
				CanonicalIntersectionAudit,
				Error)
			&& ClassifyThicknessShellIntersectionsV3(
				CatalogRow,
				ExcludedBodySurfaceTriangleCount,
				CanonicalBaselinePairCount,
				CanonicalBaselineInheritanceRadiusCm,
				ThicknessShell.PreShellTriangleCount,
				CanonicalShell.TriangleCount(),
				ExcludedRegionCertificationRadiusCm,
				CanonicalIntersectionAudit,
				CanonicalIntersectionPolicyError);
		if (!bCanonicalGeometryPass)
		{
			return Fail(FString::Printf(
				TEXT("Committed real-thickness geometry failed canonical certification: geometry=%s policy=%s"),
				Error.IsEmpty() ? TEXT("invalid") : *Error,
				CanonicalIntersectionPolicyError.IsEmpty()
					? TEXT("invalid")
					: *CanonicalIntersectionPolicyError));
		}
		ThicknessShell.OpenBoundaryCountAfter = 0;
		ThicknessShell.DegenerateTriangleCount = CanonicalDegenerateTriangleCount;
		ThicknessShell.BaselineSourceIntersectionPairCount =
			CanonicalBaselinePairCount;
		ThicknessShell.BaselineInheritanceRadiusCm =
			CanonicalBaselineInheritanceRadiusCm;
		ThicknessShell.DetectedNonAdjacentIntersectionCount =
			CanonicalIntersectionAudit.DetectedPairCount;
		ThicknessShell.ToleratedInheritedSourceIntersectionCount =
			CanonicalIntersectionAudit.ToleratedInheritedSourcePairCount;
		ThicknessShell.ToleratedLocalRepairIntersectionCount =
			CanonicalIntersectionAudit.ToleratedLocalRepairPairCount;
		ThicknessShell.LocalRepairThicknessCeilingCm =
			CanonicalLocalRepairThicknessCeilingCm;
		ThicknessShell.ToleratedExcludedRegionIntersectionCount =
			CanonicalIntersectionAudit.ToleratedExcludedRegionPairCount;
		ThicknessShell.ExcludedRegionAffectedSourceTriangleCount =
			CanonicalIntersectionAudit.AffectedSourceTriangleCount;
		ThicknessShell.ExcludedRegionMaximumWitnessDistanceCm =
			CanonicalIntersectionAudit.MaximumExcludedRegionWitnessDistanceCm;
		ThicknessShell.bSelfIntersects =
			CanonicalIntersectionAudit.DetectedPairCount > 0;
		const FMeshDescription* ShellDescription = Derived->GetMeshDescription(0);
		const int32 ExpectedShellImportVertexCount = ThicknessShell.PreShellVertexCount * 2;
		if (!ShellDescription
			|| ShellDescription->Vertices().Num() != ExpectedShellImportVertexCount)
		{
			return Fail(FString::Printf(
				TEXT("Committed real-thickness import topology has %d vertices; expected exactly %d paired vertices."),
				ShellDescription ? ShellDescription->Vertices().Num() : 0,
				ExpectedShellImportVertexCount));
		}
		ThicknessShell.FinalShellVertexCount = ShellDescription->Vertices().Num();
		ThicknessShell.FinalShellTriangleCount = GarmentDynamicMesh->GetMeshRef().TriangleCount();
		SurfaceFinalVertexCount = GarmentDynamicMesh->GetMeshRef().VertexCount();

		Correspondence.SetNum(GarmentDynamicMesh->GetMeshRef().MaxVertexID());
		ClearanceDeltas.Init(
			FVector3d::Zero(),
			GarmentDynamicMesh->GetMeshRef().MaxVertexID());
		for (const int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
		{
			if (!ThicknessShell.SourceOrdinalByVertex.IsValidIndex(VertexID))
			{
				return Fail(FString::Printf(
					TEXT("Real-thickness vertex %d has no source ordinal."),
					VertexID));
			}
			const int32 SourceOrdinal = ThicknessShell.SourceOrdinalByVertex[VertexID];
			if (!ThicknessShell.SourceVertexIDByOrdinal.IsValidIndex(SourceOrdinal))
			{
				return Fail(FString::Printf(
					TEXT("Real-thickness vertex %d resolves invalid source ordinal %d."),
					VertexID,
					SourceOrdinal));
			}
			const int32 SourceVertexID = ThicknessShell.SourceVertexIDByOrdinal[SourceOrdinal];
			if (!PreShellCorrespondence.IsValidIndex(SourceVertexID)
				|| !PreShellClearanceDeltas.IsValidIndex(SourceVertexID))
			{
				return Fail(FString::Printf(
					TEXT("Real-thickness source vertex %d has no pre-shell fit data."),
					SourceVertexID));
			}
			Correspondence[VertexID] = PreShellCorrespondence[SourceVertexID];
			ClearanceDeltas[VertexID] = PreShellClearanceDeltas[SourceVertexID];
		}
		ReconcileNonManifoldVectorField(
			GarmentDynamicMesh->GetMeshRef(),
			ClearanceDeltas,
			&ThicknessShell);
		UE_LOG(
			LogEFClothingFitCompiler,
			Display,
			TEXT("V26 real-thickness shell compiled: requested %.4fcm measured %.4f/%.4f/%.4fcm vertices %d->%d triangles %d->%d loops=%d walls=%d."),
			ThicknessShell.RequestedThicknessCm,
			ThicknessShell.MinimumMeasuredThicknessCm,
			ThicknessShell.AverageMeasuredThicknessCm,
			ThicknessShell.MaximumMeasuredThicknessCm,
			ThicknessShell.PreShellVertexCount,
			ThicknessShell.FinalShellVertexCount,
			ThicknessShell.PreShellTriangleCount,
			ThicknessShell.FinalShellTriangleCount,
			ThicknessShell.BoundaryLoopCount,
			ThicknessShell.WallTriangleCount);
	}
	int32 CertifiedSkinWeightVertexCount = 0;

	const FDynamicMesh3& BodyRestMesh = BodyDynamicMesh->GetMeshRef();
	const FDynamicMeshAABBTree3 BodyRestSpatial(&BodyRestMesh, true);
	FMeshNormals BodyRestNormals(&BodyRestMesh);
	BodyRestNormals.ComputeVertexNormals();

	// Morph deltas are thresholded and rebuilt when they are committed to a
	// SkeletalMesh.  A direction that is safe at 1.0x is not necessarily
	// monotonic at larger offsets because the closest body triangle can change.
	// Certify the actual stored morph, then feed any failing tier back into an
	// auto-sculpt pass.  Nothing is published unless every runtime tier passes.
	constexpr int32 MaximumClearanceCookPasses = 16;
	constexpr double IntersectionNudgeCm = 0.05;
	TArray<FVector3d> RequestedClearanceDeltas = ClearanceDeltas;
	// Morph storage merges dynamic-mesh seam splits. Start from that exact
	// serializable field so the first cook and every repair share one truth.
	ReconcileNonManifoldVectorField(
		GarmentDynamicMesh->GetMeshRef(),
		RequestedClearanceDeltas,
		ThicknessShell.bEnabled ? &ThicknessShell : nullptr);
	for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
	{
		const FVector3d& Delta = RequestedClearanceDeltas[VertexID];
		if (!FMath::IsFinite(Delta.X)
			|| !FMath::IsFinite(Delta.Y)
			|| !FMath::IsFinite(Delta.Z)
			|| Delta.Length() > Options.MaximumPushCm + 1.e-6)
		{
			return Fail(FString::Printf(
				TEXT("Initial seam-reconciled clearance delta is invalid at vertex %d."),
				VertexID));
		}
	}
	UDynamicMesh* ClearanceDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	int32 PostThresholdAlteredDeltaCount = 0;
	double MinimumRestOffsetGap = TNumericLimits<double>::Max();
	bool bClearanceMorphCertified = false;
	TSet<uint32> FailedStoredMorphHashes;
	int32 PreviousIntersectingTierCount = TNumericLimits<int32>::Max();
	int32 PreviousFailedTierCount = TNumericLimits<int32>::Max();
	double PreviousMinimumGap = -TNumericLimits<double>::Max();
	for (int32 CookPass = 0; CookPass < MaximumClearanceCookPasses; ++CookPass)
	{
		ClearanceDynamicMesh->SetMesh(GarmentDynamicMesh->GetMeshRef());
		ClearanceDynamicMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			for (int32 VertexID : EditMesh.VertexIndicesItr())
			{
				EditMesh.SetVertex(
					VertexID,
					EditMesh.GetVertex(VertexID) + RequestedClearanceDeltas[VertexID]);
			}
		}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions, false);

		if (!WriteMorph(ClearanceDynamicMesh, Derived, ClearanceMorphName, Error))
		{
			return Fail(Error);
		}

		TArray<FVector3d> StoredClearanceDeltas;
		int32 CookPassAlteredDeltaCount = 0;
		if (!ReadStoredMorphDeltas(
			Derived,
			ClearanceMorphName,
			GarmentDynamicMesh->GetMeshRef(),
			&RequestedClearanceDeltas,
			StoredClearanceDeltas,
			CookPassAlteredDeltaCount,
			Error))
		{
			return Fail(Error);
		}

		double CookPassMinimumGap = TNumericLimits<double>::Max();
		int32 FailingTierIndex = INDEX_NONE;
		double FailingTierGap = TNumericLimits<double>::Max();
		int32 FailingTierPenetratingVertices = 0;
		bool bFailingTierIntersects = false;
		int32 FailedTierCount = 0;
		int32 IntersectingTierCount = 0;
		for (int32 TierIndex = 0; TierIndex < CertifiedClearanceTierCount; ++TierIndex)
		{
			const double Tier = FMath::Lerp(
				CertifiedClearanceTierMin,
				CertifiedClearanceTierMax,
				static_cast<double>(TierIndex) / static_cast<double>(CertifiedClearanceTierCount - 1));
			FDynamicMesh3 TierGarment(GarmentDynamicMesh->GetMeshRef());
			for (int32 VertexID : TierGarment.VertexIndicesItr())
			{
				TierGarment.SetVertex(
					VertexID,
					TierGarment.GetVertex(VertexID) + StoredClearanceDeltas[VertexID] * Tier);
			}
			double TierGap = 0.0;
			int32 TierPenetratingVertices = 0;
			const bool bMeasured = MeasureVertexClearancePrepared(
				BodyRestMesh,
				BodyRestSpatial,
				BodyRestNormals,
				TierGarment,
				TierGap,
				TierPenetratingVertices);
			const bool bIntersects = MeshesIntersectPrepared(BodyRestSpatial, TierGarment);
			const bool bTierFailed = !bMeasured
				|| TierGap < Options.MinimumClearanceCm + CompilerClearanceReserveCm - 0.001
				|| bIntersects;
			FailedTierCount += bTierFailed ? 1 : 0;
			IntersectingTierCount += bIntersects ? 1 : 0;
			if (bMeasured)
			{
				CookPassMinimumGap = FMath::Min(CookPassMinimumGap, TierGap);
			}
			if (TierIndex == 0 && bMeasured)
			{
				MinimumAfter = TierGap;
				PenetratingAfter = TierPenetratingVertices;
			}
			if (bTierFailed
				&& (FailingTierIndex == INDEX_NONE
					|| (bIntersects && !bFailingTierIntersects)
					|| (bIntersects == bFailingTierIntersects && TierGap < FailingTierGap)))
			{
				FailingTierIndex = TierIndex;
				FailingTierGap = TierGap;
				FailingTierPenetratingVertices = TierPenetratingVertices;
				bFailingTierIntersects = bIntersects;
			}
		}

		if (FailingTierIndex == INDEX_NONE)
		{
			ClearanceDeltas = MoveTemp(StoredClearanceDeltas);
			PostThresholdAlteredDeltaCount = CookPassAlteredDeltaCount;
			MinimumRestOffsetGap = CookPassMinimumGap;
			bClearanceMorphCertified = true;
			UE_LOG(
				LogEFClothingFitCompiler,
				Display,
				TEXT("V24 clearance morph certified after %d cook pass(es): minimum tier gap %.4fcm."),
				CookPass + 1,
				MinimumRestOffsetGap);
			break;
		}

		uint32 StoredMorphHash = 0;
		for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
		{
			StoredMorphHash = HashCombineFast(
				StoredMorphHash,
				GetTypeHash(StoredClearanceDeltas[VertexID]));
		}
		if (FailedStoredMorphHashes.Contains(StoredMorphHash))
		{
			return Fail(TEXT("Clearance auto-sculpt detected a repeated stored morph and stopped before cycling."));
		}
		FailedStoredMorphHashes.Add(StoredMorphHash);
		if (CookPass > 0)
		{
			const bool bImproved = IntersectingTierCount < PreviousIntersectingTierCount
				|| (IntersectingTierCount == PreviousIntersectingTierCount
					&& FailedTierCount < PreviousFailedTierCount)
				|| (IntersectingTierCount == PreviousIntersectingTierCount
					&& FailedTierCount == PreviousFailedTierCount
					&& CookPassMinimumGap > PreviousMinimumGap + 1.e-6);
			if (!bImproved)
			{
				UE_LOG(
					LogEFClothingFitCompiler,
					Warning,
					TEXT("Clearance auto-sculpt temporarily regressed on cook pass %d (intersections %d, failed tiers %d, minimum gap %.4fcm); continuing under cycle/max-pass guards."),
					CookPass + 1,
					IntersectingTierCount,
					FailedTierCount,
					CookPassMinimumGap);
			}
		}
		PreviousIntersectingTierCount = IntersectingTierCount;
		PreviousFailedTierCount = FailedTierCount;
		PreviousMinimumGap = CookPassMinimumGap;

		const double FailingTier = FMath::Lerp(
			CertifiedClearanceTierMin,
			CertifiedClearanceTierMax,
			static_cast<double>(FailingTierIndex) / static_cast<double>(CertifiedClearanceTierCount - 1));
		if (CookPass + 1 >= MaximumClearanceCookPasses)
		{
			return Fail(FString::Printf(
				TEXT("Stored clearance morph failed to converge at certified offset tier %.3f after %d cook passes: gap %.4fcm, penetrating=%d, intersects=%s."),
				FailingTier,
				MaximumClearanceCookPasses,
				FailingTierGap,
				FailingTierPenetratingVertices,
				bFailingTierIntersects ? TEXT("true") : TEXT("false")));
		}

		TArray<FVector3d> TierCorrections;
		TArray<FVector3d> ZeroShapeDeltas;
		TArray<FVector3d> RestClearanceDirections;
		ZeroShapeDeltas.Init(FVector3d::Zero(), GarmentDynamicMesh->GetMeshRef().MaxVertexID());
		RestClearanceDirections.Init(FVector3d::Zero(), GarmentDynamicMesh->GetMeshRef().MaxVertexID());
		for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
		{
			RestClearanceDirections[VertexID] = StoredClearanceDeltas[VertexID].IsNearlyZero(1.e-8)
				? Correspondence[VertexID].SurfaceNormal
				: StoredClearanceDeltas[VertexID];
		}
		double RepairGap = TNumericLimits<double>::Max();
		const double CertifiedRepairClearance =
			static_cast<double>(Options.MinimumClearanceCm) + CompilerClearanceReserveCm;
		bool bUsedBodyFrameConeDirections = false;
		bool bBuiltMultiTierRepair = BuildMultiTierShapeClearanceCorrections(
			BodyRestMesh,
			GarmentDynamicMesh->GetMeshRef(),
			StoredClearanceDeltas,
			ZeroShapeDeltas,
			RestClearanceDirections,
			CertifiedRepairClearance + 0.05,
			static_cast<double>(Options.MaximumPushCm),
			true,
			true,
			bUsedBodyFrameConeDirections,
			TierCorrections,
			RepairGap);
		if (!bBuiltMultiTierRepair)
		{
			bBuiltMultiTierRepair = BuildMultiTierShapeClearanceCorrections(
				BodyRestMesh,
				GarmentDynamicMesh->GetMeshRef(),
				StoredClearanceDeltas,
				ZeroShapeDeltas,
				RestClearanceDirections,
				CertifiedRepairClearance,
				static_cast<double>(Options.MaximumPushCm),
				true,
				true,
				bUsedBodyFrameConeDirections,
				TierCorrections,
				RepairGap);
		}
		if (!bBuiltMultiTierRepair)
		{
			return Fail(FString::Printf(
				TEXT("Auto-sculpt could not build one coherent correction for all certified tiers after tier %.3f failed within %.4fcm maximum push (gap %.4fcm)."),
				FailingTier,
				Options.MaximumPushCm,
				RepairGap));
		}

		RequestedClearanceDeltas = MoveTemp(StoredClearanceDeltas);
		bool bAppliedCorrection = false;
		for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
		{
			const FVector3d BaseCorrection = TierCorrections[VertexID];
			bAppliedCorrection |= !BaseCorrection.IsNearlyZero(1.e-8);
			const FVector3d CandidateDelta = RequestedClearanceDeltas[VertexID] + BaseCorrection;
			if (CandidateDelta.Length() > Options.MaximumPushCm + 1.e-6)
			{
				return Fail(FString::Printf(
					TEXT("Auto-sculpt repair at tier %.3f would exceed MaximumPushCm %.4f at vertex %d (%.4fcm)."),
					FailingTier,
					Options.MaximumPushCm,
					VertexID,
					CandidateDelta.Length()));
			}
			RequestedClearanceDeltas[VertexID] = CandidateDelta;
		}
		ReconcileNonManifoldVectorField(
			GarmentDynamicMesh->GetMeshRef(),
			RequestedClearanceDeltas,
			ThicknessShell.bEnabled ? &ThicknessShell : nullptr);
		for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
		{
			const FVector3d& Delta = RequestedClearanceDeltas[VertexID];
			if (!FMath::IsFinite(Delta.X)
				|| !FMath::IsFinite(Delta.Y)
				|| !FMath::IsFinite(Delta.Z)
				|| Delta.Length() > Options.MaximumPushCm + 1.e-6)
			{
				return Fail(FString::Printf(
					TEXT("Seam-reconciled clearance repair is invalid at vertex %d."),
					VertexID));
			}
		}

		// Rebuild the failing tier from the candidate that will actually be
		// serialized. The old implementation inspected pre-repair contacts and
		// could nudge a patch that the coherent solve had already cleared.
		FDynamicMesh3 CandidateFailingTierGarment(GarmentDynamicMesh->GetMeshRef());
		for (int32 VertexID : CandidateFailingTierGarment.VertexIndicesItr())
		{
			CandidateFailingTierGarment.SetVertex(
				VertexID,
				CandidateFailingTierGarment.GetVertex(VertexID)
					+ RequestedClearanceDeltas[VertexID] * FailingTier);
		}
		const bool bCandidateFailingTierIntersects = MeshesIntersectPrepared(
			BodyRestSpatial,
			CandidateFailingTierGarment);

		// Vertex clearance can be valid while a coarse garment triangle still
		// crosses the body. Find the crossing triangles, expand to their one-ring,
		// and nudge only that patch. Coplanar intersections are not returned by
		// FindAllIntersections, so TestIntersection remains authoritative and a
		// global nudge is the conservative fallback when localization is empty.
		if (bCandidateFailingTierIntersects)
		{
			TSet<int32> IntersectionVertexIDs;
			CollectIntersectingGarmentVerticesPrepared(
				BodyRestSpatial,
				CandidateFailingTierGarment,
				IntersectionVertexIDs);
			if (IntersectionVertexIDs.IsEmpty())
			{
				for (int32 VertexID : CandidateFailingTierGarment.VertexIndicesItr())
				{
					IntersectionVertexIDs.Add(VertexID);
				}
			}
			for (int32 VertexID : IntersectionVertexIDs)
			{
				double DistanceSquared = TNumericLimits<double>::Max();
				const FVector3d TierPosition = CandidateFailingTierGarment.GetVertex(VertexID);
				const int32 BodyTriangleID = BodyRestSpatial.FindNearestTriangle(TierPosition, DistanceSquared);
				if (BodyTriangleID == IndexConstants::InvalidID)
				{
					return Fail(TEXT("Auto-sculpt intersection repair could not find a rest-body triangle."));
				}
				const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(
					BodyRestMesh,
					BodyTriangleID,
					TierPosition);
				FVector3d Normal = InterpolateNormal(
					BodyRestMesh,
					BodyRestNormals,
					BodyTriangleID,
					Query.TriangleBaryCoords);
				if (!Normal.Normalize())
				{
					return Fail(TEXT("Auto-sculpt intersection repair encountered an invalid body normal."));
				}
				const FVector3d CandidateDelta = RequestedClearanceDeltas[VertexID]
					+ Normal * (IntersectionNudgeCm / FailingTier);
				if (CandidateDelta.Length() > Options.MaximumPushCm + 1.e-6)
				{
					return Fail(FString::Printf(
						TEXT("Intersection repair would exceed MaximumPushCm %.4f at vertex %d."),
						Options.MaximumPushCm,
						VertexID));
				}
				RequestedClearanceDeltas[VertexID] = CandidateDelta;
				bAppliedCorrection = true;
			}
		}
		if (!bAppliedCorrection)
		{
			return Fail(FString::Printf(
				TEXT("Clearance auto-sculpt produced no correction for failing tier %.3f."),
				FailingTier));
		}

		// Morph storage collapses non-manifold dynamic splits onto their original
		// MeshDescription vertex. Reconcile before writing so serialization order
		// cannot make the corrective solve oscillate at a seam.
		ReconcileNonManifoldVectorField(
			GarmentDynamicMesh->GetMeshRef(),
			RequestedClearanceDeltas,
			ThicknessShell.bEnabled ? &ThicknessShell : nullptr);

		UE_LOG(
			LogEFClothingFitCompiler,
			Display,
			TEXT("V24 clearance cook pass %d repaired offset tier %.3f (stored gap %.4fcm, intersects=%s)."),
			CookPass + 1,
			FailingTier,
			FailingTierGap,
			bFailingTierIntersects ? TEXT("true") : TEXT("false"));
	}
	if (!bClearanceMorphCertified)
	{
		return Fail(TEXT("Clearance morph certification ended without a certified stored result."));
	}
	if (!RebuildFinalSurfaceCorrespondence(
		BodyDynamicMesh->GetMeshRef(),
		GarmentDynamicMesh->GetMeshRef(),
		ClearanceDeltas,
		static_cast<double>(Options.MinimumClearanceCm) + CompilerClearanceReserveCm,
		Correspondence,
		Error))
	{
		return Fail(FString::Printf(
			TEXT("Final post-sculpt surface correspondence failed: %s"),
			*Error));
	}
	if (!ReconcileThicknessShellCorrespondence(ThicknessShell, Correspondence, Error))
	{
		return Fail(FString::Printf(
			TEXT("Final real-thickness weight/morph correspondence failed: %s"),
			*Error));
	}
	if (!TransferWeights(
		SourceGarment,
		BodySurface,
		CompatibilityReference,
		BodyDynamicMesh->GetMeshRef(),
		Correspondence,
		ExcludedBodyBoneBranches,
		GarmentDynamicMesh,
		FMath::Clamp(Options.MaximumInfluences, 1, 12),
		WeightMethod,
		RequiredWeightedBones,
		RemappedWeightedBoneCount,
		ReconciledSplitVertexCount,
		Error))
	{
		return Fail(Error);
	}
	if (!WriteSkinProfile(
		GarmentDynamicMesh,
		Derived,
		FMath::Clamp(Options.MaximumInfluences, 1, 12),
		CertifiedSkinWeightVertexCount,
		Error))
	{
		return Fail(Error);
	}

	int32 AdjustedVertexCount = 0;
	for (int32 VertexID : GarmentDynamicMesh->GetMeshRef().VertexIndicesItr())
	{
		AdjustedVertexCount += ClearanceDeltas[VertexID].SquaredLength() > FMath::Square(0.001) ? 1 : 0;
	}

	TArray<FEFClothingMorphBinding> MorphBindings;
	TArray<FEFClothingMorphPairCertificate> MorphPairCertificates;
	TArray<FName> MonitoredBodyMorphNames;
	int32 ValidatedMorphCount = 0;
	int32 RepairedMorphCount = 0;
	int32 PairBodyProbeCount = 0;
	int32 PairOffsetEvaluationCount = 0;
	double MinimumSampledMorphGap = 0.0;
	double MinimumSampledPairGap = 0.0;
	double MinimumCertifiedOffsetGap = 0.0;
	double MaximumMorphDisplacement = 0.0;
	int32 MorphPostThresholdAlteredDeltaCount = 0;
	TArray<FName> TransferredMorphNames = BakeMorphs(
		BodySurface,
		SourceGarment,
		Derived,
		BodyDynamicMesh,
		GarmentDynamicMesh,
		Correspondence,
		ClearanceDeltas,
		ThicknessShell.bEnabled ? &ThicknessShell : nullptr,
		ExcludedBodyMorphPrefixes,
		Options,
		MorphBindings,
		MorphPairCertificates,
		MonitoredBodyMorphNames,
		ValidatedMorphCount,
		RepairedMorphCount,
		PairBodyProbeCount,
		PairOffsetEvaluationCount,
		MinimumSampledMorphGap,
		MinimumSampledPairGap,
		MinimumCertifiedOffsetGap,
		MaximumMorphDisplacement,
		MorphPostThresholdAlteredDeltaCount,
		Error);
	if (!Error.IsEmpty())
	{
		return Fail(Error);
	}
	PostThresholdAlteredDeltaCount += MorphPostThresholdAlteredDeltaCount;
	MinimumCertifiedOffsetGap = FMath::Min(MinimumCertifiedOffsetGap, MinimumRestOffsetGap);
	if (!FMath::IsFinite(MinimumCertifiedOffsetGap)
		|| MinimumCertifiedOffsetGap < Options.MinimumClearanceCm - 0.001)
	{
		return Fail(FString::Printf(
			TEXT("Certified runtime offset range has an invalid minimum gap: %.4fcm."),
			MinimumCertifiedOffsetGap));
	}

	// Leader-pose curves can address every morph target present on the follower,
	// including source-garment shapes which have no EFV2 clearance certificate.
	// Keep only the stored clearance target and generated samples referenced by
	// the immutable profile. The source garment remains untouched.
	TSet<FName> CertifiedDerivedMorphNames;
	CertifiedDerivedMorphNames.Add(ClearanceMorphName);
	for (const FEFClothingMorphBinding& Binding : MorphBindings)
	{
		for (const FEFClothingMorphSample& Sample : Binding.Samples)
		{
			if (!Sample.bIdentity && !Sample.GarmentMorph.IsNone())
			{
				CertifiedDerivedMorphNames.Add(Sample.GarmentMorph);
			}
		}
	}
	for (const FEFClothingMorphPairCertificate& PairCertificate : MorphPairCertificates)
	{
		for (const FEFClothingMorphPairCell& PairCell : PairCertificate.Cells)
		{
			if (!PairCell.GarmentMorph.IsNone())
			{
				CertifiedDerivedMorphNames.Add(PairCell.GarmentMorph);
			}
		}
	}
	TSet<FName> RemovedUncertifiedMorphNames;
	for (const TObjectPtr<UMorphTarget>& MorphTarget : Derived->GetMorphTargets())
	{
		if (IsValid(MorphTarget) && !CertifiedDerivedMorphNames.Contains(MorphTarget->GetFName()))
		{
			RemovedUncertifiedMorphNames.Add(MorphTarget->GetFName());
		}
	}
	if (FMeshDescription* DerivedDescription = Derived->GetMeshDescription(0))
	{
		FSkeletalMeshAttributes DerivedAttributes(*DerivedDescription);
		constexpr bool bKeepExistingAttributes = true;
		DerivedAttributes.Register(bKeepExistingAttributes);
		for (FName MorphName : DerivedAttributes.GetMorphTargetNames())
		{
			if (!CertifiedDerivedMorphNames.Contains(MorphName))
			{
				RemovedUncertifiedMorphNames.Add(MorphName);
			}
		}
	}
	else
	{
		return Fail(TEXT("Derived garment lost LOD0 MeshDescription before certified-morph isolation."));
	}
	TArray<FName> UncertifiedMorphNames = RemovedUncertifiedMorphNames.Array();
	UncertifiedMorphNames.Sort(FNameLexicalLess());
	if (!RemoveDirectMorphsCommitted(Derived, UncertifiedMorphNames, Error))
	{
		return Fail(Error);
	}
	if (const FMeshDescription* DerivedDescription = Derived->GetMeshDescription(0))
	{
		const FSkeletalMeshAttributesShared DerivedAttributes(*DerivedDescription);
		for (FName RemovedMorphName : RemovedUncertifiedMorphNames)
		{
			if (Derived->FindMorphTarget(RemovedMorphName)
				|| DerivedAttributes.GetVertexMorphPositionDelta(RemovedMorphName).IsValid())
			{
				return Fail(FString::Printf(
					TEXT("Derived garment still contains uncertified morph %s."),
					*RemovedMorphName.ToString()));
			}
		}
	}
	else
	{
		return Fail(TEXT("Derived garment lost LOD0 MeshDescription after certified-morph isolation."));
	}

	FVector ConcurrentBoundsExpansion = FVector::ZeroVector;
	float ConcurrentSphereExpansion = 0.0f;
	if (!BuildConcurrentBoundsContract(
		Derived,
		GarmentDynamicMesh->GetMeshRef(),
		ClearanceDeltas,
		MorphBindings,
		MorphPairCertificates,
		ConcurrentBoundsExpansion,
		ConcurrentSphereExpansion,
		MaximumMorphDisplacement,
		Error))
	{
		return Fail(Error);
	}
	if (ThicknessShell.bEnabled)
	{
		// Imported bounds still originate from the immutable one-layer source.
		// Add the measured shell reach conservatively on every axis so neither the
		// outward layer nor its walls can be culled before the runtime deformer.
		const float ShellBoundsReserveCm = static_cast<float>(
			ThicknessShell.MaximumMeasuredThicknessCm);
		ConcurrentBoundsExpansion += FVector(ShellBoundsReserveCm);
		ConcurrentSphereExpansion += ShellBoundsReserveCm;
	}

	Derived->SetDefaultMeshDeformer(BodySurface->GetDefaultMeshDeformer());
	Derived->SetTargetMeshDeformers(BodySurface->GetTargetMeshDeformers());
	FSkeletalMeshLODInfo* DerivedDeformerLODInfo = Derived->GetLODInfo(0);
	const FSkeletalMeshLODInfo* BodyDeformerLODInfo = BodySurface->GetLODInfo(0);
	if (!DerivedDeformerLODInfo || !BodyDeformerLODInfo)
	{
		return Fail(TEXT("Generated/Female LOD0 deformer settings are unavailable."));
	}
	DerivedDeformerLODInfo->bAllowMeshDeformer = BodyDeformerLODInfo->bAllowMeshDeformer;
	DerivedDeformerLODInfo->bBuildHalfEdgeBuffers = BodyDeformerLODInfo->bBuildHalfEdgeBuffers;

	FBoxSphereBounds ExpandedBounds = SourceGarment->GetImportedBounds();
	ExpandedBounds.BoxExtent += ConcurrentBoundsExpansion;
	ExpandedBounds.SphereRadius += ConcurrentSphereExpansion;
	Derived->SetImportedBounds(ExpandedBounds);
	Derived->InvalidateDeriveDataCacheGUID();
	Derived->MarkPackageDirty();
	Derived->PostEditChange();
	FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});
	if (!ValidateGeneratedBodyBindArtifacts(Derived, BodySurface, Error)
		|| !ValidateGeneratedBodyDeformerParity(Derived, BodySurface, Error))
	{
		return Fail(Error);
	}
	if (!ProtectedInputsUnchanged())
	{
		return Fail(TEXT("Protected input guard failed before publishing the derived garment."));
	}
	if (!SaveAsset(Derived, Error))
	{
		return Fail(Error);
	}
	FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});
	if (!ValidateGeneratedBodyBindArtifacts(Derived, BodySurface, Error)
		|| !ValidateGeneratedBodyDeformerParity(Derived, BodySurface, Error))
	{
		return Fail(FString::Printf(
			TEXT("Saved derived garment failed body deformation validation: %s"),
			*Error));
	}
	const FMeshDescription* SavedDerivedDescription = Derived->GetMeshDescription(0);
	if (!SavedDerivedDescription)
	{
		return Fail(TEXT("Saved derived garment has no LOD0 MeshDescription."));
	}
	const FSkeletalMeshAttributesShared SavedDerivedAttributes(*SavedDerivedDescription);
	for (FName RemovedMorphName : UncertifiedMorphNames)
	{
		if (Derived->FindMorphTarget(RemovedMorphName)
			|| SavedDerivedAttributes.GetVertexMorphPositionDelta(RemovedMorphName).IsValid())
		{
			return Fail(FString::Printf(
				TEXT("Uncertified morph %s reappeared after derived save."),
				*RemovedMorphName.ToString()));
		}
	}

	UEFClothingSurfaceBinding* SurfaceBinding = nullptr;
	EEFClothingFitMode ResolvedFitMode = EEFClothingFitMode::Tight;
	if (CatalogRow.Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU)
	{
		if (!BuildSurfaceBindingAsset(
			SourceGarment,
			Derived,
			BodySurface,
			GarmentDynamicMesh->GetMeshRef(),
			ClearanceDeltas,
			CatalogRow,
			ExcludedBodySurfaceMaterialSlots,
			PublicationGuid,
			Options.OutputRoot,
			PublicationKey,
			Options.MinimumClearanceCm,
			Options.MaximumPushCm,
			SurfaceBinding,
			ResolvedFitMode,
			Error))
		{
			return Fail(FString::Printf(TEXT("V26 render-surface binding failed: %s"), *Error));
		}
	}
	else
	{
		switch (CatalogRow.FitPolicy)
		{
		case EEFClothingFitPolicy::Hybrid:
			ResolvedFitMode = EEFClothingFitMode::Hybrid;
			break;
		case EEFClothingFitPolicy::Loose:
			ResolvedFitMode = EEFClothingFitMode::Loose;
			break;
		case EEFClothingFitPolicy::Rigid:
			ResolvedFitMode = EEFClothingFitMode::Rigid;
			break;
		default:
			ResolvedFitMode = EEFClothingFitMode::Tight;
			break;
		}
	}

	const FString ProfileName = FString::Printf(
		TEXT("DA_%s_%s_%s_%s_EFV2Fit_%s"),
		*SanitizeAssetName(SourceGarment->GetName()),
		*BuildSourceKey(SourceGarment),
		*SanitizeAssetName(BodySurface->GetName()),
		*BuildSourceKey(BodySurface),
		*PublicationKey);
	const FString ProfileObjectPath = FString::Printf(
		TEXT("%s/%s.%s"),
		*Options.OutputRoot,
		*ProfileName,
		*ProfileName);
	if (LoadObject<UEFClothingFitProfile>(nullptr, *ProfileObjectPath))
	{
		return Fail(FString::Printf(
			TEXT("Fresh publication key collided with existing generated profile %s."),
			*ProfileObjectPath));
	}
	UEFClothingFitProfile* Profile = FindOrCreateDataAsset<UEFClothingFitProfile>(Options.OutputRoot, ProfileName);
	UEFClothingFitRegistry* Registry = FindOrCreateDataAsset<UEFClothingFitRegistry>(
		Options.OutputRoot,
		TEXT("DA_EFClothingFitRegistry"));
	if (!Profile || !Registry)
	{
		return Fail(TEXT("Could not create generated profile or registry DataAsset."));
	}

	Profile->SourceGarment = SourceGarment;
	Profile->FittedGarment = Derived;
	Profile->BodySurface = BodySurface;
	Profile->CompatibilityReference = CompatibilityReference;
	Profile->BuildGuid = PublicationGuid;
	Profile->CompilerVersion = CompilerVersion;
	Profile->GarmentCompileFingerprint = CatalogRow.BuildCompileFingerprint();
	Profile->FitMode = ResolvedFitMode;
	Profile->SurfaceBinding = SurfaceBinding;
	Profile->SkinWeightProfileName = FitWeightProfileName;
	Profile->RequiredWeightedBones = MoveTemp(RequiredWeightedBones);
	Profile->ExcludedBodySurfaceMaterialSlots = ExcludedBodySurfaceMaterialSlots;
	Profile->ExcludedBodyBoneBranches = ExcludedBodyBoneBranches;
	Profile->ExcludedBodyMorphPrefixes = ExcludedBodyMorphPrefixes;
	Profile->ExcludedBodySurfaceTriangleCount = ExcludedBodySurfaceTriangleCount;
	Profile->bCompiledThicknessShell = ThicknessShell.bEnabled;
	Profile->ThicknessShellAlgorithmVersion = ThicknessShell.bEnabled
		? ThicknessShellAlgorithmVersion
		: 0;
	Profile->CompiledThicknessCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.RequestedThicknessCm)
		: 0.0f;
	Profile->PreShellVertexCount = ThicknessShell.bEnabled
		? ThicknessShell.PreShellVertexCount
		: 0;
	Profile->PreShellTriangleCount = ThicknessShell.bEnabled
		? ThicknessShell.PreShellTriangleCount
		: 0;
	Profile->FinalShellVertexCount = ThicknessShell.bEnabled
		? ThicknessShell.FinalShellVertexCount
		: 0;
	Profile->FinalShellTriangleCount = ThicknessShell.bEnabled
		? ThicknessShell.FinalShellTriangleCount
		: 0;
	Profile->ShellVertexPairCount = ThicknessShell.bEnabled
		? ThicknessShell.PreShellVertexCount
		: 0;
	Profile->ShellBoundaryLoopCount = ThicknessShell.bEnabled
		? ThicknessShell.BoundaryLoopCount
		: 0;
	Profile->ShellWallTriangleCount = ThicknessShell.bEnabled
		? ThicknessShell.WallTriangleCount
		: 0;
	Profile->ShellOpenBoundaryCountAfter = ThicknessShell.bEnabled
		? ThicknessShell.OpenBoundaryCountAfter
		: 0;
	Profile->ShellDegenerateTriangleCount = ThicknessShell.bEnabled
		? ThicknessShell.DegenerateTriangleCount
		: 0;
	Profile->ShellDetectedNonAdjacentIntersectionCount = ThicknessShell.bEnabled
		? ThicknessShell.DetectedNonAdjacentIntersectionCount
		: 0;
	Profile->ShellBaselineSourceIntersectionPairCount = ThicknessShell.bEnabled
		? ThicknessShell.BaselineSourceIntersectionPairCount
		: 0;
	Profile->ShellToleratedInheritedSourceIntersectionCount = ThicknessShell.bEnabled
		? ThicknessShell.ToleratedInheritedSourceIntersectionCount
		: 0;
	Profile->ShellBaselineInheritanceRadiusCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.BaselineInheritanceRadiusCm)
		: 0.0f;
	Profile->ShellToleratedLocalRepairIntersectionCount = ThicknessShell.bEnabled
		? ThicknessShell.ToleratedLocalRepairIntersectionCount
		: 0;
	Profile->ShellLocalRepairThicknessCeilingCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.LocalRepairThicknessCeilingCm)
		: 0.0f;
	Profile->ShellToleratedExcludedRegionIntersectionCount = ThicknessShell.bEnabled
		? ThicknessShell.ToleratedExcludedRegionIntersectionCount
		: 0;
	Profile->ShellExcludedRegionAffectedSourceTriangleCount = ThicknessShell.bEnabled
		? ThicknessShell.ExcludedRegionAffectedSourceTriangleCount
		: 0;
	Profile->ShellExcludedRegionCertificationRadiusCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.ExcludedRegionCertificationRadiusCm)
		: 0.0f;
	Profile->ShellExcludedRegionMaximumWitnessDistanceCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.ExcludedRegionMaximumWitnessDistanceCm)
		: 0.0f;
	Profile->bShellSelfIntersects = ThicknessShell.bEnabled
		&& ThicknessShell.bSelfIntersects;
	Profile->ShellMinimumMeasuredThicknessCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.MinimumMeasuredThicknessCm)
		: 0.0f;
	Profile->ShellAverageMeasuredThicknessCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.AverageMeasuredThicknessCm)
		: 0.0f;
	Profile->ShellMaximumMeasuredThicknessCm = ThicknessShell.bEnabled
		? static_cast<float>(ThicknessShell.MaximumMeasuredThicknessCm)
		: 0.0f;
	Profile->ClearanceMorphName = ClearanceMorphName;
	Profile->DefaultClearanceValue = 1.0f;
	Profile->CertifiedClearanceMultiplierMin = static_cast<float>(CertifiedClearanceTierMin);
	Profile->CertifiedClearanceMultiplierMax = static_cast<float>(CertifiedClearanceTierMax);
	Profile->CertifiedClearanceTierCount = CertifiedClearanceTierCount;
	Profile->MinimumCertifiedOffsetGapCm = static_cast<float>(MinimumCertifiedOffsetGap);
	Profile->CompiledMinimumClearanceCm = Options.MinimumClearanceCm;
	Profile->CompiledClearanceReserveCm = static_cast<float>(CompilerClearanceReserveCm);
	Profile->CompiledMaxPushCm = Options.MaximumPushCm;
	Profile->CompiledMaximumMorphRepairCm = Options.MaximumMorphRepairCm;
	Profile->CompiledMaximumMorphDisplacementCm = static_cast<float>(MaximumMorphDisplacement);
	const FSkeletalMeshLODInfo* DerivedLODInfo = Derived->GetLODInfo(0);
	Profile->CompiledMorphThresholdPositionCm = DerivedLODInfo
		? DerivedLODInfo->BuildSettings.MorphThresholdPosition
		: 0.0f;
	Profile->PostThresholdAlteredDeltaCount = PostThresholdAlteredDeltaCount;
	Profile->CompiledConcurrentBoundsExpansionCm = ConcurrentBoundsExpansion;
	Profile->CompiledConcurrentSphereExpansionCm = ConcurrentSphereExpansion;
	int32 GeneratedMorphSampleCount = 0;
	int32 MaximumMorphSamplesPerBinding = 0;
	int32 SteppedMorphIntervalCount = 0;
	int32 IdentityMorphSampleCount = 0;
	for (const FEFClothingMorphBinding& Binding : MorphBindings)
	{
		GeneratedMorphSampleCount += Binding.Samples.Num();
		MaximumMorphSamplesPerBinding = FMath::Max(MaximumMorphSamplesPerBinding, Binding.Samples.Num());
		for (const FEFClothingMorphSample& Sample : Binding.Samples)
		{
			SteppedMorphIntervalCount += Sample.bStepFromPrevious ? 1 : 0;
			IdentityMorphSampleCount += Sample.bIdentity ? 1 : 0;
		}
	}
	int32 GeneratedPairCellMorphCount = 0;
	for (const FEFClothingMorphPairCertificate& PairCertificate : MorphPairCertificates)
	{
		GeneratedPairCellMorphCount += PairCertificate.Cells.Num();
	}
	Profile->MorphBindings = MoveTemp(MorphBindings);
	Profile->MonitoredBodyMorphNames = MoveTemp(MonitoredBodyMorphNames);
	Profile->MorphPairCertificates = MoveTemp(MorphPairCertificates);
	Profile->CompiledMorphActivationEpsilon = Options.MorphActivationEpsilon;
	Profile->SourceSkeletonFingerprint = SourceFingerprintBefore;
	Profile->BodySkeletonFingerprint = BodyFingerprintBefore;
	Profile->CompatibilitySkeletonFingerprint = CompatibilityFingerprintBefore;
	Profile->FittedSkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(Derived);
	Profile->SharedSkeletonFingerprint = SharedSkeletonFingerprintBefore;
	Profile->SharedSkeletonEditorFingerprint = SharedSkeletonEditorFingerprintBefore;
	Profile->SourcePackageGuid = SourceGarment->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Profile->BodyPackageGuid = BodySurface->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Profile->CompatibilityPackageGuid = CompatibilityReference->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Profile->FittedPackageGuid = Derived->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Profile->SourceContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(SourceGarment);
	Profile->BodyContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(BodySurface);
	Profile->CompatibilityContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(CompatibilityReference);
	Profile->FittedContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(Derived);
	Profile->CompiledLODCount = SourceGarment->GetNumSourceModels();
	const FMeshDescription* SourceMeshDescription = SourceGarment->GetMeshDescription(0);
	Profile->SourceVertexCount = SourceMeshDescription ? SourceMeshDescription->Vertices().Num() : 0;
	const FMeshDescription* FittedMeshDescription = Derived->GetMeshDescription(0);
	Profile->FittedVertexCount = FittedMeshDescription ? FittedMeshDescription->Vertices().Num() : 0;
	Profile->AdjustedVertexCount = AdjustedVertexCount;
	Profile->PenetratingVertexCountBefore = PenetratingBefore;
	Profile->PenetratingVertexCountAfter = PenetratingAfter;
	Profile->MinimumSignedGapBeforeCm = static_cast<float>(MinimumBefore);
	Profile->MinimumSignedGapAfterCm = static_cast<float>(MinimumAfter);
	Profile->TransferredMorphCount = TransferredMorphNames.Num();
	Profile->RemappedWeightedBoneCount = RemappedWeightedBoneCount;
	Profile->ReconciledSplitVertexCount = ReconciledSplitVertexCount;
	Profile->CertifiedSkinWeightVertexCount = CertifiedSkinWeightVertexCount;
	Profile->ClearanceValidatedMorphCount = ValidatedMorphCount;
	Profile->ClearanceRepairedMorphCount = RepairedMorphCount;
	Profile->MinimumSampledMorphGapCm = static_cast<float>(MinimumSampledMorphGap);
	Profile->MorphClearanceSampleCount = FMath::Clamp(Options.MorphClearanceSampleCount, 2, 8);
	Profile->GeneratedMorphSampleCount = GeneratedMorphSampleCount;
	Profile->MaximumMorphSamplesPerBinding = MaximumMorphSamplesPerBinding;
	Profile->SteppedMorphIntervalCount = SteppedMorphIntervalCount;
	Profile->IdentityMorphSampleCount = IdentityMorphSampleCount;
	Profile->CertifiedMorphPairCount = Profile->MorphPairCertificates.Num();
	Profile->GeneratedPairCellMorphCount = GeneratedPairCellMorphCount;
	Profile->PairBodyProbeCount = PairBodyProbeCount;
	Profile->PairOffsetEvaluationCount = PairOffsetEvaluationCount;
	Profile->MinimumSampledPairGapCm = static_cast<float>(MinimumSampledPairGap);
	Profile->MarkPackageDirty();
	if (!ProtectedInputsUnchanged())
	{
		return Fail(TEXT("Protected input guard failed before publishing the fit profile and registry."));
	}
	FString PrePublishValidationReport;
	if (!UEFClothingFitCompilerLibrary::ValidateCompiledProfile(Profile, PrePublishValidationReport))
	{
		return Fail(FString::Printf(
			TEXT("V26 staging profile failed pre-publication validation: %s"),
			*PrePublishValidationReport));
	}
	if (!SaveAsset(Profile, Error))
	{
		return Fail(Error);
	}
	FString SavedProfileValidationReport;
	if (!UEFClothingFitCompilerLibrary::ValidateCompiledProfile(Profile, SavedProfileValidationReport))
	{
		return Fail(FString::Printf(
			TEXT("V26 saved staging profile failed validation before registry publication: %s"),
			*SavedProfileValidationReport));
	}
	if (!ProtectedInputsUnchanged()
		|| SourceGarment->GetSkeleton() != SharedSkeletonBefore
		|| BodySurface->GetSkeleton() != SharedSkeletonBefore
		|| CompatibilityReference->GetSkeleton() != SharedSkeletonBefore
		|| EFClothingSkeleton::BuildSharedSkeletonFingerprint(SharedSkeletonBefore) != SharedSkeletonFingerprintBefore
		|| EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(SharedSkeletonBefore) != SharedSkeletonEditorFingerprintBefore
		|| EFClothingSkeleton::BuildFingerprint(SourceGarment) != SourceFingerprintBefore
		|| EFClothingSkeleton::BuildFingerprint(BodySurface) != BodyFingerprintBefore
		|| EFClothingSkeleton::BuildFingerprint(CompatibilityReference) != CompatibilityFingerprintBefore
		|| EFClothingSkeleton::BuildContentFingerprint(SourceGarment) != Profile->SourceContentFingerprint
		|| EFClothingSkeleton::BuildContentFingerprint(BodySurface) != Profile->BodyContentFingerprint
		|| EFClothingSkeleton::BuildContentFingerprint(CompatibilityReference) != Profile->CompatibilityContentFingerprint)
	{
		return Fail(TEXT("Protected skeleton integrity changed before registry publication; generated output was not published."));
	}

	if (!Options.bDeferRegistryPublication)
	{
		auto BuildRegistryProfileKey = [](const UEFClothingFitProfile* RegistryProfile, FString& OutKey) -> bool
		{
			if (!IsValid(RegistryProfile)
				|| RegistryProfile->SourceGarment.IsNull()
				|| RegistryProfile->BodySurface.IsNull())
			{
				return false;
			}
			OutKey = RegistryProfile->SourceGarment.ToSoftObjectPath().ToString()
				+ TEXT("|") + RegistryProfile->BodySurface.ToSoftObjectPath().ToString();
			return true;
		};

	// Publishing one fit must never double as registry cleanup. Reject malformed
	// or ambiguous pre-existing state instead of silently deleting unrelated rows.
	TSet<FString> ExistingRegistryKeys;
	for (int32 ProfileIndex = 0; ProfileIndex < Registry->Profiles.Num(); ++ProfileIndex)
	{
		const UEFClothingFitProfile* ExistingProfile = Registry->Profiles[ProfileIndex];
		FString ExistingKey;
		if (!BuildRegistryProfileKey(ExistingProfile, ExistingKey))
		{
			return Fail(FString::Printf(
				TEXT("Fit registry contains an invalid entry at index %d; publication was not attempted."),
				ProfileIndex));
		}
		if (ExistingRegistryKeys.Contains(ExistingKey))
		{
			return Fail(FString::Printf(
				TEXT("Fit registry contains duplicate key %s; publication was not attempted."),
				*ExistingKey));
		}
		ExistingRegistryKeys.Add(ExistingKey);
	}

	FString TargetRegistryKey;
	if (!BuildRegistryProfileKey(Profile, TargetRegistryKey))
	{
		return Fail(TEXT("Generated fit profile does not provide a valid registry key."));
	}
	TArray<TObjectPtr<UEFClothingFitProfile>> CandidateRegistryProfiles = Registry->Profiles;
	CandidateRegistryProfiles.RemoveAll([&BuildRegistryProfileKey, &TargetRegistryKey](
		const UEFClothingFitProfile* ExistingProfile)
	{
		FString ExistingKey;
		return BuildRegistryProfileKey(ExistingProfile, ExistingKey)
			&& ExistingKey == TargetRegistryKey;
	});
	CandidateRegistryProfiles.Add(Profile);
		CandidateRegistryProfiles.Sort([](const UEFClothingFitProfile& A, const UEFClothingFitProfile& B)
		{
			const FString AKey = A.SourceGarment.ToSoftObjectPath().ToString()
				+ TEXT("|") + A.BodySurface.ToSoftObjectPath().ToString();
			const FString BKey = B.SourceGarment.ToSoftObjectPath().ToString()
				+ TEXT("|") + B.BodySurface.ToSoftObjectPath().ToString();
			return AKey < BKey;
		});
	TSet<FString> CandidateRegistryKeys;
	int32 TargetRegistryEntryCount = 0;
	for (int32 ProfileIndex = 0; ProfileIndex < CandidateRegistryProfiles.Num(); ++ProfileIndex)
	{
		const UEFClothingFitProfile* CandidateProfile = CandidateRegistryProfiles[ProfileIndex];
		FString CandidateKey;
		if (!BuildRegistryProfileKey(CandidateProfile, CandidateKey)
			|| CandidateRegistryKeys.Contains(CandidateKey))
		{
			return Fail(FString::Printf(
				TEXT("Candidate fit registry contains an invalid or duplicate entry at index %d."),
				ProfileIndex));
		}
		CandidateRegistryKeys.Add(CandidateKey);
		if (CandidateKey == TargetRegistryKey)
		{
			++TargetRegistryEntryCount;
			if (CandidateProfile != Profile || CandidateProfile->BuildGuid != PublicationGuid)
			{
				return Fail(TEXT("Candidate fit registry target does not reference the staged publication."));
			}
		}
	}
	if (TargetRegistryEntryCount != 1)
	{
		return Fail(TEXT("Candidate fit registry must contain exactly one staged target entry."));
	}
		const TArray<TObjectPtr<UEFClothingFitProfile>> PreviousRegistryProfiles = Registry->Profiles;
		UPackage* RegistryPackage = Registry->GetOutermost();
		const bool bRegistryPackageDirtyBefore = RegistryPackage && RegistryPackage->IsDirty();
		Registry->Profiles = MoveTemp(CandidateRegistryProfiles);
		Registry->MarkPackageDirty();
		if (!SaveAsset(Registry, Error))
		{
			Registry->Profiles = PreviousRegistryProfiles;
			if (RegistryPackage)
			{
				RegistryPackage->SetDirtyFlag(bRegistryPackageDirtyBefore);
			}
			return Fail(Error);
		}
	}

	Result.bSuccess = true;
	Result.DerivedGarment = Derived;
	Result.Profile = Profile;
	Result.SurfaceBinding = SurfaceBinding;
	Result.Report = FString::Printf(
		TEXT("PASS | Source=%s | Derived=%s | SourceVertices=%d | FittedVertices=%d | Adjusted=%d | PenetratingBefore=%d | PenetratingAfter=%d | MinGapBefore=%.4fcm | MinGapAfter=%.4fcm | SurfacePolicy=%s backend=%d fitMode=%d binding=%s lodPairs=%d excludedSlots=%d excludedTriangles=%d excludedBoneBranches=%d excludedMorphPrefixes=%d | Weights=%s max%d required=%d remapped=%d | CommonMorphs=%d | TransferredMorphs=%d | MorphClearance=%d/%d repaired=%d samples=%d min=%.4fcm | Build=%s"),
		*SourceGarment->GetPathName(),
		*Derived->GetPathName(),
		Profile->SourceVertexCount,
		Profile->FittedVertexCount,
		AdjustedVertexCount,
		PenetratingBefore,
		PenetratingAfter,
		MinimumBefore,
		MinimumAfter,
		CatalogRowName.IsNone() ? TEXT("NoCatalogRow") : *CatalogRowName.ToString(),
		static_cast<int32>(CatalogRow.Backend),
		static_cast<int32>(Profile->FitMode),
		SurfaceBinding ? *SurfaceBinding->GetPathName() : TEXT("none"),
		SurfaceBinding ? SurfaceBinding->LODPairBindings.Num() : 0,
		Profile->ExcludedBodySurfaceMaterialSlots.Num(),
		Profile->ExcludedBodySurfaceTriangleCount,
		Profile->ExcludedBodyBoneBranches.Num(),
		Profile->ExcludedBodyMorphPrefixes.Num(),
		*WeightMethod,
		Options.MaximumInfluences,
		Profile->RequiredWeightedBones.Num(),
		Profile->RemappedWeightedBoneCount,
		Profile->MorphBindings.Num() - Profile->TransferredMorphCount,
		Profile->TransferredMorphCount,
		Profile->ClearanceValidatedMorphCount,
		Profile->MorphBindings.Num(),
		Profile->ClearanceRepairedMorphCount,
		Profile->MorphClearanceSampleCount,
		Profile->MinimumSampledMorphGapCm,
		*Profile->BuildGuid.ToString(EGuidFormats::DigitsWithHyphens));
	UE_LOG(LogEFClothingFitCompiler, Display, TEXT("%s"), *Result.Report);
	return Result;
}

FEFClothingCatalogCompileResult UEFClothingFitCompilerLibrary::CompileGarmentCatalog(
	UEFClothingMorphDirectorPolicy* Director,
	USkeletalMesh* CompatibilityReference,
	FEFClothingFitCompileOptions Options)
{
	using namespace EFClothingFitCompilerPrivate;

	FEFClothingCatalogCompileResult Result;
	auto Fail = [&Result](const FString& Message)
	{
		Result.bSuccess = false;
		Result.Report = FString::Printf(TEXT("FAIL: %s"), *Message);
		UE_LOG(LogEFClothingFitCompiler, Error, TEXT("%s"), *Result.Report);
		return Result;
	};
	if (!IsValid(Director))
	{
		return Fail(TEXT("CompileGarmentCatalog requires a valid EF Clothing Morph Director."));
	}
	if (Director->SchemaVersion >= 4)
	{
		return Fail(TEXT(
			"The legacy V26 generated-mesh compiler is disabled for a V3 Director. "
			"Use CompileNativeSourceCatalogV3 or Refresh Binding."));
	}
	if (!IsValid(CompatibilityReference))
	{
		return Fail(TEXT("CompileGarmentCatalog requires a compatibility reference mesh."));
	}
	if (!IsAllowedOutputRoot(Options.OutputRoot))
	{
		return Fail(TEXT("OutputRoot must remain under /EFClothingMorph/_Internal/Compiled/V26."));
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	UEFClothingMorphDirectorPolicy* ConfiguredDirector = Settings && !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	if (ConfiguredDirector != Director)
	{
		return Fail(TEXT("Batch publication is restricted to the exact Clothing Director configured in EFClothingMorphV2 settings."));
	}
	FString DirectorValidationError;
	if (!Director->ValidatePolicy(DirectorValidationError))
	{
		return Fail(FString::Printf(
			TEXT("Configured Clothing Director is invalid: %s"),
			*DirectorValidationError));
	}

	TArray<FName> EnabledRowNames;
	TMap<FName, const FEFClothingGarmentRow*> RowsById;
	TSet<FString> UniqueSourceBodyKeys;
	for (const FEFClothingGarmentRow& Row : Director->Garments)
	{
		if (Row.IsDisabledEmptyPlaceholder())
		{
			continue;
		}
		if (Row.GarmentId.IsNone())
		{
			return Fail(TEXT("Clothing Director contains a garment with an empty GarmentId."));
		}
		if (RowsById.Contains(Row.GarmentId))
		{
			return Fail(FString::Printf(
				TEXT("Clothing Director contains duplicate GarmentId %s."),
				*Row.GarmentId.ToString()));
		}
		RowsById.Add(Row.GarmentId, &Row);

		if (!Row.bEnabled)
		{
			continue;
		}
		const FSoftObjectPath SourcePath = Row.SourceGarment.ToSoftObjectPath();
		const FSoftObjectPath BodyPath = Row.BodySurface.ToSoftObjectPath();
		if (!SourcePath.IsNull() && !BodyPath.IsNull())
		{
			const FString SourceBodyKey = SourcePath.ToString() + TEXT("|") + BodyPath.ToString();
			if (UniqueSourceBodyKeys.Contains(SourceBodyKey))
			{
				return Fail(FString::Printf(
					TEXT("Clothing Director contains a duplicate enabled source/body pair at GarmentId %s."),
					*Row.GarmentId.ToString()));
			}
			UniqueSourceBodyKeys.Add(SourceBodyKey);
		}
		if (Row.Backend == EEFClothingSurfaceBackend::Disabled)
		{
			return Fail(FString::Printf(
				TEXT("Enabled Clothing Director garment %s selects the Disabled backend."),
				*Row.GarmentId.ToString()));
		}
		EnabledRowNames.Add(Row.GarmentId);
	}
	EnabledRowNames.Sort(FNameLexicalLess());
	Result.EnabledRowCount = EnabledRowNames.Num();
	if (EnabledRowNames.IsEmpty())
	{
		return Fail(TEXT("Clothing Director has no enabled garments; refusing to publish an empty registry."));
	}

	TArray<TObjectPtr<UEFClothingFitProfile>> StagedProfiles;
	StagedProfiles.Reserve(EnabledRowNames.Num());
	Result.Rows.Reserve(EnabledRowNames.Num());
	for (const FName RowName : EnabledRowNames)
	{
		FEFClothingCatalogCompileRowResult& RowResult = Result.Rows.AddDefaulted_GetRef();
		RowResult.RowName = RowName;
		const FEFClothingGarmentRow* const* FoundRow = RowsById.Find(RowName);
		const FEFClothingGarmentRow* Row = FoundRow ? *FoundRow : nullptr;
		if (!Row || Row->SourceGarment.IsNull() || Row->BodySurface.IsNull())
		{
			RowResult.Report = TEXT("FAIL: enabled row has a null source garment or body surface.");
			return Fail(FString::Printf(TEXT("Row %s: %s"), *RowName.ToString(), *RowResult.Report));
		}
		RowResult.bRequiresSurfaceBinding =
			Row->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU;
		Result.SurfaceWrapRowCount += RowResult.bRequiresSurfaceBinding ? 1 : 0;

		USkeletalMesh* SourceGarment = Row->SourceGarment.LoadSynchronous();
		USkeletalMesh* BodySurface = Row->BodySurface.LoadSynchronous();
		if (!IsValid(SourceGarment) || !IsValid(BodySurface))
		{
			RowResult.Report = TEXT("FAIL: source garment or body surface could not be loaded.");
			return Fail(FString::Printf(TEXT("Row %s: %s"), *RowName.ToString(), *RowResult.Report));
		}

		FEFClothingFitCompileOptions RowOptions = Options;
		RowOptions.bDeferRegistryPublication = true;
		if (Row->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			// The late surface constraint reads the body's final morph/JCM deformation,
			// so onboarding does not need the expensive V25 per-morph sample lattice.
			RowOptions.bCompileBodyMorphBindings = false;
			RowOptions.bTransferMissingBodyMorphs = false;
			RowOptions.MorphPairRequests.Reset();
		}

		const FEFClothingFitCompileResult CompileResult = CompileFitProfile(
			SourceGarment,
			BodySurface,
			CompatibilityReference,
			RowOptions);
		RowResult.bSuccess = CompileResult.bSuccess;
		RowResult.DerivedGarment = CompileResult.DerivedGarment;
		RowResult.Profile = CompileResult.Profile;
		RowResult.SurfaceBinding = CompileResult.SurfaceBinding;
		RowResult.Report = CompileResult.Report;
		if (!CompileResult.bSuccess || !IsValid(CompileResult.Profile))
		{
			return Fail(FString::Printf(
				TEXT("Row %s staging compile failed: %s"),
				*RowName.ToString(),
				*CompileResult.Report));
		}
		if (RowResult.bRequiresSurfaceBinding != IsValid(CompileResult.SurfaceBinding))
		{
			RowResult.bSuccess = false;
			RowResult.Report = TEXT("FAIL: backend/surface-binding publication contract mismatch.");
			return Fail(FString::Printf(TEXT("Row %s: %s"), *RowName.ToString(), *RowResult.Report));
		}
		FString ValidationReport;
		if (!ValidateCompiledProfile(CompileResult.Profile, ValidationReport))
		{
			RowResult.bSuccess = false;
			RowResult.Report = FString::Printf(TEXT("FAIL: staged validation: %s"), *ValidationReport);
			return Fail(FString::Printf(TEXT("Row %s: %s"), *RowName.ToString(), *RowResult.Report));
		}
		StagedProfiles.Add(CompileResult.Profile);
		++Result.CompiledRowCount;
	}

	if (Result.CompiledRowCount != Result.EnabledRowCount
		|| StagedProfiles.Num() != Result.EnabledRowCount
		|| Result.Rows.Num() != Result.EnabledRowCount)
	{
		return Fail(FString::Printf(
			TEXT("Catalog equality gate failed: enabled=%d compiled=%d profiles=%d tested=%d."),
			Result.EnabledRowCount,
			Result.CompiledRowCount,
			StagedProfiles.Num(),
			Result.Rows.Num()));
	}
	StagedProfiles.Sort([](const UEFClothingFitProfile& A, const UEFClothingFitProfile& B)
	{
		const FString AKey = A.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|") + A.BodySurface.ToSoftObjectPath().ToString();
		const FString BKey = B.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|") + B.BodySurface.ToSoftObjectPath().ToString();
		return AKey < BKey;
	});

	UEFClothingFitRegistry* Registry = FindOrCreateDataAsset<UEFClothingFitRegistry>(
		Options.OutputRoot,
		TEXT("DA_EFClothingFitRegistry"));
	if (!IsValid(Registry))
	{
		return Fail(TEXT("Could not resolve the generated fit registry for catalog publication."));
	}
	const TArray<TObjectPtr<UEFClothingFitProfile>> PreviousProfiles = Registry->Profiles;
	UPackage* RegistryPackage = Registry->GetOutermost();
	const bool bRegistryDirtyBefore = RegistryPackage && RegistryPackage->IsDirty();
	Registry->Profiles = StagedProfiles;
	Registry->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Registry, SaveError))
	{
		Registry->Profiles = PreviousProfiles;
		if (RegistryPackage)
		{
			RegistryPackage->SetDirtyFlag(bRegistryDirtyBefore);
		}
		return Fail(FString::Printf(TEXT("Atomic catalog registry save failed: %s"), *SaveError));
	}

	Result.bSuccess = true;
	Result.Registry = Registry;
	int32 ValidBindingCount = 0;
	int32 PassedRowCount = 0;
	for (const FEFClothingCatalogCompileRowResult& Row : Result.Rows)
	{
		ValidBindingCount += IsValid(Row.SurfaceBinding) ? 1 : 0;
		PassedRowCount += Row.bSuccess ? 1 : 0;
	}
	Result.Report = FString::Printf(
		TEXT("PASS | Director=%s | enabled=%d surface_wrap=%d valid_profiles=%d valid_bindings=%d tested=%d passed=%d | Registry=%s"),
		*Director->GetPathName(),
		Result.EnabledRowCount,
		Result.SurfaceWrapRowCount,
		StagedProfiles.Num(),
		ValidBindingCount,
		Result.Rows.Num(),
		PassedRowCount,
		*Registry->GetPathName());
	UE_LOG(LogEFClothingFitCompiler, Display, TEXT("%s"), *Result.Report);
	return Result;
}

FEFClothingNativeSourceFreshnessResult
UEFClothingFitCompilerLibrary::ValidateNativeSourceCatalogV3(
	UEFClothingMorphDirectorPolicy* Director,
	UEFClothingFitRegistry* Registry,
	USkeletalMesh* CompatibilityReference,
	FEFClothingNativeSourceCompileOptions Options)
{
	using namespace EFClothingFitCompilerPrivate;

	FEFClothingNativeSourceFreshnessResult Result;
	auto Fail = [&Result](const FString& Message)
	{
		Result.bFresh = false;
		Result.Report = FString::Printf(TEXT("STALE: %s"), *Message);
		return Result;
	};
	if (!IsValid(Director))
	{
		return Fail(TEXT("the configured Clothing Director is unavailable"));
	}
	if (!IsValid(Registry))
	{
		return Fail(TEXT("the V3 native-source binding registry is unavailable"));
	}
	if (!IsValid(CompatibilityReference)
		|| !IsValid(CompatibilityReference->GetSkeleton()))
	{
		return Fail(TEXT("the protected compatibility reference is unavailable"));
	}
	if (!IsAllowedNativeSourceOutputRoot(Options.OutputRoot))
	{
		return Fail(TEXT("the configured V3 output root is outside the project-owned internal mount"));
	}
	FString DirectorError;
	if (!Director->ValidatePolicy(DirectorError))
	{
		return Fail(FString::Printf(TEXT("the Clothing Director is invalid: %s"), *DirectorError));
	}
	if (!Registry->Profiles.IsEmpty())
	{
		return Fail(TEXT("a V3 binding-only registry contains generated V26 fit profiles"));
	}

	TSet<FName> EnabledIds;
	TSet<FString> EnabledPairs;
	for (const FEFClothingGarmentRow& Row : Director->Garments)
	{
		if (Row.IsDisabledEmptyPlaceholder() || !Row.bEnabled)
		{
			continue;
		}
		++Result.EnabledRowCount;
		if (Row.GarmentId.IsNone()
			|| EnabledIds.Contains(Row.GarmentId)
			|| Row.SourceGarment.IsNull()
			|| Row.BodySurface.IsNull()
			|| Row.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			return Fail(FString::Printf(
				TEXT("enabled row %s is duplicate, incomplete or does not use SurfaceWrapGPU"),
				*Row.GarmentId.ToString()));
		}
		EnabledIds.Add(Row.GarmentId);
		const FString PairKey = Row.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|") + Row.BodySurface.ToSoftObjectPath().ToString();
		if (EnabledPairs.Contains(PairKey))
		{
			return Fail(FString::Printf(
				TEXT("enabled row %s duplicates a source/body pair"),
				*Row.GarmentId.ToString()));
		}
		EnabledPairs.Add(PairKey);

		USkeletalMesh* SourceGarment = Row.SourceGarment.LoadSynchronous();
		USkeletalMesh* BodySurface = Row.BodySurface.LoadSynchronous();
		if (!IsValid(SourceGarment) || !IsValid(BodySurface))
		{
			return Fail(FString::Printf(
				TEXT("row %s could not load its exact source garment or reference body"),
				*Row.GarmentId.ToString()));
		}

		int32 MatchingBindingCount = 0;
		const UEFClothingSurfaceBinding* MatchingBinding = nullptr;
		for (const UEFClothingSurfaceBinding* Binding : Registry->NativeSourceBindings)
		{
			if (IsValid(Binding)
				&& Binding->SourceGarment.ToSoftObjectPath() == Row.SourceGarment.ToSoftObjectPath()
				&& Binding->BodySurface.ToSoftObjectPath() == Row.BodySurface.ToSoftObjectPath())
			{
				++MatchingBindingCount;
				MatchingBinding = Binding;
			}
		}
		if (MatchingBindingCount != 1)
		{
			return Fail(FString::Printf(
				TEXT("row %s has %d V3 bindings for its exact source/body pair (expected one)"),
				*Row.GarmentId.ToString(),
				MatchingBindingCount));
		}

		FString BindingError;
		if (!ValidateNativeSourceBindingInternal(
			MatchingBinding,
			Row,
			SourceGarment,
			BodySurface,
			CompatibilityReference,
			Options,
			BindingError))
		{
			return Fail(FString::Printf(
				TEXT("row %s: %s"),
				*Row.GarmentId.ToString(),
				*BindingError));
		}
		++Result.ValidBindingCount;
	}

	if (Result.EnabledRowCount <= 0)
	{
		return Fail(TEXT("the Clothing Director has no enabled garments"));
	}
	if (Registry->NativeSourceBindings.Num() != Result.EnabledRowCount
		|| Result.ValidBindingCount != Result.EnabledRowCount)
	{
		return Fail(FString::Printf(
			TEXT("catalog equality failed: enabled=%d registry=%d valid=%d"),
			Result.EnabledRowCount,
			Registry->NativeSourceBindings.Num(),
			Result.ValidBindingCount));
	}

	Result.bFresh = true;
	Result.Report = FString::Printf(
		TEXT("FRESH | Director=%s | enabled=%d valid_bindings=%d | Registry=%s"),
		*Director->GetPathName(),
		Result.EnabledRowCount,
		Result.ValidBindingCount,
		*Registry->GetPathName());
	return Result;
}

FEFClothingNativeSourceCatalogCompileResult
UEFClothingFitCompilerLibrary::CompileNativeSourceCatalogV3(
	UEFClothingMorphDirectorPolicy* Director,
	USkeletalMesh* CompatibilityReference,
	FEFClothingNativeSourceCompileOptions Options)
{
	using namespace EFClothingFitCompilerPrivate;

	FEFClothingNativeSourceCatalogCompileResult Result;
	auto Fail = [&Result](const FString& Message)
	{
		Result.bSuccess = false;
		Result.Report = FString::Printf(TEXT("FAIL: %s"), *Message);
		UE_LOG(LogEFClothingFitCompiler, Error, TEXT("%s"), *Result.Report);
		return Result;
	};
	if (!IsValid(Director))
	{
		return Fail(TEXT("CompileNativeSourceCatalogV3 requires a valid Clothing Director."));
	}
	if (!IsValid(CompatibilityReference)
		|| !IsValid(CompatibilityReference->GetSkeleton()))
	{
		return Fail(TEXT("CompileNativeSourceCatalogV3 requires the protected compatibility mesh."));
	}
	if (!IsAllowedNativeSourceOutputRoot(Options.OutputRoot))
	{
		return Fail(TEXT("V3 OutputRoot must remain under /EFClothingMorph/_Internal/Compiled/V3."));
	}
	if (!FMath::IsFinite(Options.MinimumClearanceCm)
		|| !FMath::IsNearlyZero(Options.MinimumClearanceCm, KINDA_SMALL_NUMBER)
		|| !FMath::IsFinite(Options.MaximumPushCm)
		|| Options.MaximumPushCm < 0.05f
		|| Options.MaximumPushCm > 10.0f)
	{
		return Fail(TEXT("V3 base clearance must remain zero; use the Director's per-garment Skin Clearance runtime value."));
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	UEFClothingMorphDirectorPolicy* ConfiguredDirector = Settings
		&& !Settings->DirectorPolicy.IsNull()
		? Settings->DirectorPolicy.LoadSynchronous()
		: nullptr;
	if (ConfiguredDirector != Director)
	{
		return Fail(TEXT("V3 publication is restricted to the exact configured Clothing Director."));
	}
	FString DirectorError;
	if (!Director->ValidatePolicy(DirectorError))
	{
		return Fail(FString::Printf(TEXT("Configured Clothing Director is invalid: %s"), *DirectorError));
	}

	TArray<const FEFClothingGarmentRow*> EnabledRows;
	TSet<FName> UniqueIds;
	TSet<FString> UniquePairs;
	for (const FEFClothingGarmentRow& Row : Director->Garments)
	{
		if (Row.IsDisabledEmptyPlaceholder() || !Row.bEnabled)
		{
			continue;
		}
		if (Row.GarmentId.IsNone()
			|| UniqueIds.Contains(Row.GarmentId)
			|| Row.SourceGarment.IsNull()
			|| Row.BodySurface.IsNull())
		{
			return Fail(FString::Printf(
				TEXT("Enabled Director row %s is duplicate or incomplete."),
				*Row.GarmentId.ToString()));
		}
		if (Row.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			return Fail(FString::Printf(
				TEXT("Enabled Director row %s must use SurfaceWrapGPU for the V3 source-authoritative registry."),
				*Row.GarmentId.ToString()));
		}
		const FString PairKey = Row.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|") + Row.BodySurface.ToSoftObjectPath().ToString();
		if (UniquePairs.Contains(PairKey))
		{
			return Fail(FString::Printf(
				TEXT("Enabled Director row %s duplicates a source/body pair."),
				*Row.GarmentId.ToString()));
		}
		UniqueIds.Add(Row.GarmentId);
		UniquePairs.Add(PairKey);
		EnabledRows.Add(&Row);
	}
	EnabledRows.Sort([](const FEFClothingGarmentRow& A, const FEFClothingGarmentRow& B)
	{
		return A.GarmentId.LexicalLess(B.GarmentId);
	});
	Result.EnabledRowCount = EnabledRows.Num();
	if (EnabledRows.IsEmpty())
	{
		return Fail(TEXT("Clothing Director has no enabled garments; refusing to publish an empty V3 registry."));
	}

	struct FProtectedMeshState
	{
		TWeakObjectPtr<USkeletalMesh> Mesh;
		FString ContentFingerprint;
		FString SkeletonFingerprint;
		bool bPackageDirty = false;
	};
	TArray<FProtectedMeshState> ProtectedMeshes;
	TSet<USkeletalMesh*> CapturedMeshes;
	auto CaptureMesh = [&ProtectedMeshes, &CapturedMeshes](USkeletalMesh* Mesh)
	{
		if (!IsValid(Mesh) || CapturedMeshes.Contains(Mesh))
		{
			return;
		}
		CapturedMeshes.Add(Mesh);
		FProtectedMeshState& State = ProtectedMeshes.AddDefaulted_GetRef();
		State.Mesh = Mesh;
		State.ContentFingerprint = EFClothingSkeleton::BuildContentFingerprint(Mesh);
		State.SkeletonFingerprint = EFClothingSkeleton::BuildFingerprint(Mesh);
		State.bPackageDirty = Mesh->GetOutermost()->IsDirty();
	};
	CaptureMesh(CompatibilityReference);
	for (const FEFClothingGarmentRow* Row : EnabledRows)
	{
		CaptureMesh(Row->SourceGarment.LoadSynchronous());
		CaptureMesh(Row->BodySurface.LoadSynchronous());
	}
	USkeleton* ProtectedSkeleton = CompatibilityReference->GetSkeleton();
	const FString SharedSkeletonFingerprintBefore =
		EFClothingSkeleton::BuildSharedSkeletonFingerprint(ProtectedSkeleton);
	const FString SharedSkeletonEditorFingerprintBefore =
		EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(ProtectedSkeleton);
	const bool bSkeletonPackageDirtyBefore = ProtectedSkeleton->GetOutermost()->IsDirty();
	auto ProtectedInputsUnchanged = [&]()
	{
		if (!IsValid(ProtectedSkeleton)
			|| EFClothingSkeleton::BuildSharedSkeletonFingerprint(ProtectedSkeleton)
				!= SharedSkeletonFingerprintBefore
			|| EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(ProtectedSkeleton)
				!= SharedSkeletonEditorFingerprintBefore
			|| ProtectedSkeleton->GetOutermost()->IsDirty() != bSkeletonPackageDirtyBefore)
		{
			return false;
		}
		for (const FProtectedMeshState& State : ProtectedMeshes)
		{
			USkeletalMesh* Mesh = State.Mesh.Get();
			if (!IsValid(Mesh)
				|| EFClothingSkeleton::BuildContentFingerprint(Mesh) != State.ContentFingerprint
				|| EFClothingSkeleton::BuildFingerprint(Mesh) != State.SkeletonFingerprint
				|| Mesh->GetOutermost()->IsDirty() != State.bPackageDirty)
			{
				return false;
			}
		}
		return true;
	};
	if (!ProtectedInputsUnchanged())
	{
		return Fail(TEXT("Protected source/body/compatibility state changed while the V3 guard was armed."));
	}

	const FString RegistryObjectPath = FString::Printf(
		TEXT("%s/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry"),
		*Options.OutputRoot);
	UEFClothingFitRegistry* Registry = LoadObject<UEFClothingFitRegistry>(
		nullptr,
		*RegistryObjectPath);
	if (!IsValid(Registry))
	{
		Registry = FindOrCreateDataAsset<UEFClothingFitRegistry>(
			Options.OutputRoot,
			TEXT("DA_EFClothingFitRegistry"));
	}
	if (!IsValid(Registry))
	{
		return Fail(TEXT("Could not create the internal V3 binding registry."));
	}
	Result.Registry = Registry;

	TArray<TObjectPtr<UEFClothingSurfaceBinding>> StagedBindings;
	StagedBindings.Reserve(EnabledRows.Num());
	Result.Rows.Reserve(EnabledRows.Num());
	for (const FEFClothingGarmentRow* Row : EnabledRows)
	{
		FEFClothingNativeSourceCompileRowResult& RowResult =
			Result.Rows.AddDefaulted_GetRef();
		RowResult.GarmentId = Row->GarmentId;
		USkeletalMesh* SourceGarment = Row->SourceGarment.LoadSynchronous();
		USkeletalMesh* BodySurface = Row->BodySurface.LoadSynchronous();
		if (!IsValid(SourceGarment)
			|| !IsValid(BodySurface)
			|| SourceGarment->GetSkeleton() != ProtectedSkeleton
			|| BodySurface->GetSkeleton() != ProtectedSkeleton)
		{
			RowResult.Report = TEXT("FAIL: source/body do not share the protected compatibility USkeleton.");
			return Fail(FString::Printf(
				TEXT("Row %s: %s"),
				*Row->GarmentId.ToString(),
				*RowResult.Report));
		}

		UEFClothingSurfaceBinding* Binding = nullptr;
		if (Options.bOnlyStale)
		{
			int32 MatchCount = 0;
			for (UEFClothingSurfaceBinding* Candidate : Registry->NativeSourceBindings)
			{
				if (IsValid(Candidate)
					&& Candidate->SourceGarment.ToSoftObjectPath()
						== Row->SourceGarment.ToSoftObjectPath()
					&& Candidate->BodySurface.ToSoftObjectPath()
						== Row->BodySurface.ToSoftObjectPath())
				{
					++MatchCount;
					Binding = Candidate;
				}
			}
			FString FreshnessError;
			if (MatchCount == 1
				&& ValidateNativeSourceBindingInternal(
					Binding,
					*Row,
					SourceGarment,
					BodySurface,
					CompatibilityReference,
					Options,
					FreshnessError))
			{
				RowResult.bReusedFreshBinding = true;
				++Result.ReusedFreshRowCount;
			}
			else
			{
				Binding = nullptr;
			}
		}

		if (!IsValid(Binding))
		{
			FString BuildError;
			if (!BuildNativeSourceBindingAsset(
				*Row,
				SourceGarment,
				BodySurface,
				CompatibilityReference,
				Options,
				Binding,
				BuildError))
			{
				RowResult.Report = FString::Printf(TEXT("FAIL: %s"), *BuildError);
				return Fail(FString::Printf(
					TEXT("Row %s native-source binding failed: %s"),
					*Row->GarmentId.ToString(),
					*BuildError));
			}
		}
		if (!ProtectedInputsUnchanged())
		{
			RowResult.Report = TEXT("FAIL: protected source/body/skeleton state changed during binding bake.");
			return Fail(FString::Printf(
				TEXT("Row %s violated the protected-input guard."),
				*Row->GarmentId.ToString()));
		}
		RowResult.bSuccess = true;
		RowResult.SurfaceBinding = Binding;
		RowResult.Report = RowResult.bReusedFreshBinding
			? TEXT("PASS: reused exact fresh native-source binding.")
			: TEXT("PASS: baked exact native-source binding without a generated Skeletal Mesh or fit profile.");
		StagedBindings.Add(Binding);
		++Result.CompiledRowCount;
	}

	if (Result.CompiledRowCount != Result.EnabledRowCount
		|| StagedBindings.Num() != Result.EnabledRowCount
		|| Result.Rows.Num() != Result.EnabledRowCount)
	{
		return Fail(FString::Printf(
			TEXT("V3 catalog equality failed before publication: enabled=%d resolved=%d bindings=%d tested=%d."),
			Result.EnabledRowCount,
			Result.CompiledRowCount,
			StagedBindings.Num(),
			Result.Rows.Num()));
	}
	StagedBindings.Sort([](const UEFClothingSurfaceBinding& A, const UEFClothingSurfaceBinding& B)
	{
		return A.GarmentId.LexicalLess(B.GarmentId);
	});

	const TArray<TObjectPtr<UEFClothingFitProfile>> PreviousProfiles = Registry->Profiles;
	const TArray<TObjectPtr<UEFClothingSurfaceBinding>> PreviousBindings =
		Registry->NativeSourceBindings;
	UPackage* RegistryPackage = Registry->GetOutermost();
	const bool bRegistryDirtyBefore = RegistryPackage && RegistryPackage->IsDirty();
	Registry->Profiles.Reset();
	Registry->NativeSourceBindings = StagedBindings;
	Registry->MarkPackageDirty();
	FString SaveError;
	if (!SaveAsset(Registry, SaveError))
	{
		Registry->Profiles = PreviousProfiles;
		Registry->NativeSourceBindings = PreviousBindings;
		if (RegistryPackage)
		{
			RegistryPackage->SetDirtyFlag(bRegistryDirtyBefore);
		}
		return Fail(FString::Printf(TEXT("Atomic V3 registry save failed: %s"), *SaveError));
	}

	const FEFClothingNativeSourceFreshnessResult Freshness =
		ValidateNativeSourceCatalogV3(Director, Registry, CompatibilityReference, Options);
	if (!Freshness.bFresh || !ProtectedInputsUnchanged())
	{
		Registry->Profiles = PreviousProfiles;
		Registry->NativeSourceBindings = PreviousBindings;
		Registry->MarkPackageDirty();
		FString RollbackError;
		if (!SaveAsset(Registry, RollbackError))
		{
			return Fail(FString::Printf(
				TEXT("V3 post-publication validation failed and registry rollback also failed: validation=%s rollback=%s"),
				*Freshness.Report,
				*RollbackError));
		}
		return Fail(FString::Printf(
			TEXT("V3 post-publication validation failed; previous registry restored: %s"),
			*Freshness.Report));
	}

	Result.bSuccess = true;
	Result.Report = FString::Printf(
		TEXT("PASS | V3 native source | Director=%s | enabled=%d resolved=%d reused=%d valid_bindings=%d profiles=0 | Registry=%s"),
		*Director->GetPathName(),
		Result.EnabledRowCount,
		Result.CompiledRowCount,
		Result.ReusedFreshRowCount,
		Freshness.ValidBindingCount,
		*Registry->GetPathName());
	UE_LOG(LogEFClothingFitCompiler, Display, TEXT("%s"), *Result.Report);
	return Result;
}

bool UEFClothingFitCompilerLibrary::ValidateCompiledProfile(UEFClothingFitProfile* Profile, FString& OutReport)
{
	if (!IsValid(Profile))
	{
		OutReport = TEXT("FAIL: Profile is invalid.");
		return false;
	}

	USkeletalMesh* Source = Profile->SourceGarment.LoadSynchronous();
	USkeletalMesh* Fitted = Profile->FittedGarment.LoadSynchronous();
	USkeletalMesh* Body = Profile->BodySurface.LoadSynchronous();
	USkeletalMesh* Compatibility = Profile->CompatibilityReference.LoadSynchronous();
	if (!Source || !Fitted || !Body || !Compatibility)
	{
		OutReport = TEXT("FAIL: One or more profile assets cannot be loaded.");
		return false;
	}
	TArray<FName> CatalogExcludedSurfaceSlots;
	TArray<FName> CatalogExcludedBoneBranches;
	TArray<FString> CatalogExcludedMorphPrefixes;
	FEFClothingGarmentRow CatalogRow;
	FName CatalogRowName = NAME_None;
	FString CatalogFailureReason;
	const bool bCatalogPass = EFClothingFitCompilerPrivate::ResolveCatalogSurfacePolicy(
		Source,
		Body,
		CatalogExcludedSurfaceSlots,
		CatalogExcludedBoneBranches,
		CatalogExcludedMorphPrefixes,
		CatalogRow,
		CatalogRowName,
		CatalogFailureReason);
	if (!bCatalogPass)
	{
		OutReport = FString::Printf(TEXT("FAIL: %s"), *CatalogFailureReason);
		return false;
	}
	const bool bSurfaceWrapProfile = CatalogRow.Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU;
	const EEFClothingFitMode ExpectedExplicitFitMode =
		CatalogRow.FitPolicy == EEFClothingFitPolicy::Hybrid ? EEFClothingFitMode::Hybrid
		: CatalogRow.FitPolicy == EEFClothingFitPolicy::Loose ? EEFClothingFitMode::Loose
		: CatalogRow.FitPolicy == EEFClothingFitPolicy::Rigid ? EEFClothingFitMode::Rigid
		: EEFClothingFitMode::Tight;
	const bool bFitModePass = CatalogRow.FitPolicy == EEFClothingFitPolicy::Auto
		? (Profile->FitMode == EEFClothingFitMode::Tight
			|| Profile->FitMode == EEFClothingFitMode::Hybrid
			|| Profile->FitMode == EEFClothingFitMode::Loose)
		: Profile->FitMode == ExpectedExplicitFitMode;
	UEFClothingSurfaceBinding* SurfaceBinding = Profile->SurfaceBinding.LoadSynchronous();
	FString SurfaceBindingFailureReason;
	const bool bSurfaceBindingPass = bSurfaceWrapProfile
		? (!Profile->SurfaceBinding.IsNull()
			&& EFClothingFitCompilerPrivate::ValidateSurfaceBinding(
				SurfaceBinding,
				Profile,
				SurfaceBindingFailureReason))
		: Profile->SurfaceBinding.IsNull();
	if (!bSurfaceBindingPass && SurfaceBindingFailureReason.IsEmpty())
	{
		SurfaceBindingFailureReason = bSurfaceWrapProfile
			? TEXT("SurfaceWrapGPU profile has no loadable V26 surface binding.")
			: TEXT("GeometryFitFallback profile must not reference a V26 surface binding.");
	}

	FString FailureReason;
	if (!EFClothingSkeleton::AreBoneHierarchiesCompatible(Source, Fitted, &FailureReason)
		|| !EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(Fitted, Body, &FailureReason)
		|| !EFClothingSkeleton::AreSharedBoneHierarchiesCompatible(Fitted, Compatibility, &FailureReason))
	{
		OutReport = FString::Printf(TEXT("FAIL: %s"), *FailureReason);
		return false;
	}
	if (!EFClothingFitCompilerPrivate::ValidateGeneratedBodyBindArtifacts(Fitted, Body, FailureReason)
		|| !EFClothingFitCompilerPrivate::ValidateGeneratedBodyDeformerParity(Fitted, Body, FailureReason))
	{
		OutReport = FString::Printf(TEXT("FAIL: %s"), *FailureReason);
		return false;
	}

	USkeleton* const SharedSkeleton = Source->GetSkeleton();
	const bool bSharedSkeletonPass = IsValid(SharedSkeleton)
		&& Fitted->GetSkeleton() == SharedSkeleton
		&& Body->GetSkeleton() == SharedSkeleton
		&& Compatibility->GetSkeleton() == SharedSkeleton
		&& EFClothingSkeleton::BuildSharedSkeletonFingerprint(SharedSkeleton) == Profile->SharedSkeletonFingerprint
		&& !Profile->SharedSkeletonEditorFingerprint.IsEmpty()
		&& EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(SharedSkeleton)
			== Profile->SharedSkeletonEditorFingerprint;
	const bool bSkeletonFingerprintPass = bSharedSkeletonPass
		&& EFClothingSkeleton::BuildFingerprint(Source) == Profile->SourceSkeletonFingerprint
		&& EFClothingSkeleton::BuildFingerprint(Body) == Profile->BodySkeletonFingerprint
		&& EFClothingSkeleton::BuildFingerprint(Compatibility) == Profile->CompatibilitySkeletonFingerprint
		&& EFClothingSkeleton::BuildFingerprint(Fitted) == Profile->FittedSkeletonFingerprint;
	const bool bContentFingerprintPass = EFClothingSkeleton::BuildContentFingerprint(Source) == Profile->SourceContentFingerprint
		&& EFClothingSkeleton::BuildContentFingerprint(Body) == Profile->BodyContentFingerprint
		&& EFClothingSkeleton::BuildContentFingerprint(Compatibility) == Profile->CompatibilityContentFingerprint
		&& EFClothingSkeleton::BuildContentFingerprint(Fitted) == Profile->FittedContentFingerprint;
	const bool bPackageIdentityPass = Source->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) == Profile->SourcePackageGuid
		&& Body->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) == Profile->BodyPackageGuid
		&& Compatibility->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) == Profile->CompatibilityPackageGuid
		&& Fitted->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) == Profile->FittedPackageGuid;
	const bool bProfileExists = Fitted->GetSkinWeightProfiles().ContainsByPredicate([Profile](const FSkinWeightProfileInfo& Info)
	{
		return Info.Name == Profile->SkinWeightProfileName;
	});
	UMorphTarget* ClearanceMorph = Fitted->FindMorphTarget(Profile->ClearanceMorphName);
	const bool bClearanceMorphExists = IsValid(ClearanceMorph)
		&& ClearanceMorph->HasDataForLOD(0)
		&& ClearanceMorph->GetNumDeltasForLOD(0) > 0;
	bool bThicknessShellPass = true;
	FString ThicknessShellFailureReason;
	auto RejectThicknessShell = [&bThicknessShellPass, &ThicknessShellFailureReason](const FString& Reason)
	{
		bThicknessShellPass = false;
		if (ThicknessShellFailureReason.IsEmpty())
		{
			ThicknessShellFailureReason = Reason;
		}
	};
	const FMeshDescription* FittedShellDescription = Fitted->GetMeshDescription(0);
	if (CatalogRow.bCreateThicknessShell)
	{
		if (!Profile->bCompiledThicknessShell
			|| Profile->ThicknessShellAlgorithmVersion != EFClothingFitCompilerPrivate::ThicknessShellAlgorithmVersion
			|| !FMath::IsFinite(Profile->CompiledThicknessCm)
			|| !FMath::IsNearlyEqual(
				Profile->CompiledThicknessCm,
				EFClothingMorphV26::CompiledThicknessReferenceCm,
				1.e-4f)
			|| Profile->PreShellVertexCount <= 0
			|| Profile->PreShellTriangleCount <= 0
			|| Profile->FinalShellVertexCount != Profile->PreShellVertexCount * 2
			|| Profile->FinalShellTriangleCount
				!= Profile->PreShellTriangleCount * 2 + Profile->ShellWallTriangleCount
			|| Profile->ShellVertexPairCount != Profile->PreShellVertexCount
			|| Profile->ShellBoundaryLoopCount <= 0
			|| Profile->ShellWallTriangleCount <= 0
			|| Profile->ShellOpenBoundaryCountAfter != 0
			|| Profile->ShellDegenerateTriangleCount != 0
			|| Profile->ShellDetectedNonAdjacentIntersectionCount < 0
			|| Profile->ShellBaselineSourceIntersectionPairCount < 0
			|| Profile->ShellToleratedInheritedSourceIntersectionCount < 0
			|| Profile->ShellToleratedLocalRepairIntersectionCount < 0
			|| Profile->ShellToleratedExcludedRegionIntersectionCount < 0
			|| Profile->ShellToleratedInheritedSourceIntersectionCount
				+ Profile->ShellToleratedLocalRepairIntersectionCount
				+ Profile->ShellToleratedExcludedRegionIntersectionCount
				> Profile->ShellDetectedNonAdjacentIntersectionCount
			|| !FMath::IsFinite(Profile->ShellBaselineInheritanceRadiusCm)
			|| Profile->ShellBaselineInheritanceRadiusCm <= 0.0f
			|| !FMath::IsFinite(Profile->ShellLocalRepairThicknessCeilingCm)
			|| Profile->ShellLocalRepairThicknessCeilingCm <= 0.0f
			|| Profile->ShellExcludedRegionAffectedSourceTriangleCount < 0
			|| !FMath::IsFinite(Profile->ShellExcludedRegionCertificationRadiusCm)
			|| Profile->ShellExcludedRegionCertificationRadiusCm <= 0.0f
			|| !FMath::IsFinite(Profile->ShellExcludedRegionMaximumWitnessDistanceCm)
			|| Profile->ShellExcludedRegionMaximumWitnessDistanceCm < 0.0f
			|| Profile->bShellSelfIntersects
				!= (Profile->ShellDetectedNonAdjacentIntersectionCount > 0)
			|| !FittedShellDescription
			|| FittedShellDescription->Vertices().Num() != Profile->FinalShellVertexCount
			|| FittedShellDescription->Triangles().Num() != Profile->FinalShellTriangleCount)
		{
			RejectThicknessShell(TEXT("stored shell topology/settings do not match the catalog or fitted MeshDescription"));
		}
		if (bThicknessShellPass)
		{
			UDynamicMesh* ValidationShellMesh = NewObject<UDynamicMesh>(GetTransientPackage());
			UDynamicMesh* ValidationBodyMesh = NewObject<UDynamicMesh>(GetTransientPackage());
			UE::Geometry::FDynamicMesh3 ValidationExcludedBodySurface;
			int32 ValidationExcludedBodyTriangleCount = 0;
			FString ShellReadError;
			if (!EFClothingFitCompilerPrivate::CopySourceLOD(
				Fitted,
				ValidationShellMesh,
				ShellReadError)
				|| !EFClothingFitCompilerPrivate::CopySourceLOD(
					Body,
					ValidationBodyMesh,
					ShellReadError)
				|| !EFClothingFitCompilerPrivate::ExcludeBodySurfaceMaterialSlots(
					Body,
					ValidationBodyMesh,
					CatalogExcludedSurfaceSlots,
					ValidationExcludedBodyTriangleCount,
					&ValidationExcludedBodySurface,
					ShellReadError))
			{
				RejectThicknessShell(FString::Printf(
					TEXT("could not recapture fitted shell/excluded anatomy: %s"),
					*ShellReadError));
			}
			else
			{
				const UE::Geometry::FDynamicMesh3& ValidationMesh = ValidationShellMesh->GetMeshRef();
				UE::Geometry::FMeshBoundaryLoops BoundaryLoops(&ValidationMesh, true);
				int32 DegenerateTriangleCount = 0;
				for (const int32 TriangleID : ValidationMesh.TriangleIndicesItr())
				{
					DegenerateTriangleCount += ValidationMesh.GetTriArea(TriangleID) <= 1.e-10 ? 1 : 0;
				}
				const double ExpectedExcludedRegionRadiusCm = FMath::Min(
					3.0,
					static_cast<double>(Profile->CompiledMinimumClearanceCm)
						+ static_cast<double>(Profile->CompiledClearanceReserveCm)
						+ static_cast<double>(Profile->CompiledThicknessCm)
						+ static_cast<double>(Profile->CompiledClearanceReserveCm));
				const double ExpectedBaselineInheritanceRadiusCm =
					EFClothingFitCompilerPrivate::SurfaceRuntimeMaximumEdgeLengthCm * 0.5
					+ static_cast<double>(Profile->CompiledThicknessCm)
					+ 0.01;
				const double ExpectedLocalRepairThicknessCeilingCm = FMath::Max(
					1.e-4,
					static_cast<double>(Profile->CompiledThicknessCm) * 0.15);
				UE::Geometry::FDynamicMesh3 ValidationInnerLayer;
				TMap<FString, EFClothingFitCompilerPrivate::FSourceIntersectionWitnessEvidence>
					ValidationBaselineWitnessPointsBySourcePair;
				int32 ValidationBaselinePairCount = 0;
				const bool bBaselineEvidenceValid =
					EFClothingFitCompilerPrivate::ExtractThicknessShellInnerLayer(
						ValidationMesh,
						Profile->PreShellVertexCount,
						ValidationInnerLayer,
						ShellReadError)
					&& EFClothingFitCompilerPrivate::CollectSelfIntersectionWitnessEvidence(
						ValidationInnerLayer,
						Profile->PreShellVertexCount,
						ValidationBaselineWitnessPointsBySourcePair,
						ValidationBaselinePairCount,
						ShellReadError);
				EFClothingFitCompilerPrivate::FThicknessShellIntersectionAudit IntersectionAudit;
				const bool bSelfIntersectionAuditValid = bBaselineEvidenceValid
					&&
					EFClothingFitCompilerPrivate::MeasureThicknessShellIntersectionsV3(
						ValidationMesh,
						Profile->PreShellVertexCount,
						ValidationBaselineWitnessPointsBySourcePair,
						ExpectedBaselineInheritanceRadiusCm,
						ValidationExcludedBodySurface.TriangleCount() > 0
							? &ValidationExcludedBodySurface
							: nullptr,
						ExpectedExcludedRegionRadiusCm,
						ExpectedLocalRepairThicknessCeilingCm,
						IntersectionAudit,
						ShellReadError);
				FString IntersectionPolicyError;
				const bool bIntersectionPolicyPass = bSelfIntersectionAuditValid
					&& EFClothingFitCompilerPrivate::ClassifyThicknessShellIntersectionsV3(
						CatalogRow,
						ValidationExcludedBodyTriangleCount,
						ValidationBaselinePairCount,
						ExpectedBaselineInheritanceRadiusCm,
						Profile->PreShellTriangleCount,
						Profile->FinalShellTriangleCount,
						ExpectedExcludedRegionRadiusCm,
						IntersectionAudit,
						IntersectionPolicyError);
				const bool bIntersectionEvidenceMatches =
					ValidationExcludedBodyTriangleCount
						== Profile->ExcludedBodySurfaceTriangleCount
					&& IntersectionAudit.DetectedPairCount
						== Profile->ShellDetectedNonAdjacentIntersectionCount
					&& ValidationBaselinePairCount
						== Profile->ShellBaselineSourceIntersectionPairCount
					&& IntersectionAudit.ToleratedInheritedSourcePairCount
						== Profile->ShellToleratedInheritedSourceIntersectionCount
					&& IntersectionAudit.ToleratedLocalRepairPairCount
						== Profile->ShellToleratedLocalRepairIntersectionCount
					&& FMath::IsNearlyEqual(
						Profile->ShellBaselineInheritanceRadiusCm,
						static_cast<float>(ExpectedBaselineInheritanceRadiusCm),
						1.e-4f)
					&& FMath::IsNearlyEqual(
						Profile->ShellLocalRepairThicknessCeilingCm,
						static_cast<float>(ExpectedLocalRepairThicknessCeilingCm),
						1.e-4f)
					&& IntersectionAudit.ToleratedExcludedRegionPairCount
						== Profile->ShellToleratedExcludedRegionIntersectionCount
					&& IntersectionAudit.AffectedSourceTriangleCount
						== Profile->ShellExcludedRegionAffectedSourceTriangleCount
					&& Profile->bShellSelfIntersects
						== (IntersectionAudit.DetectedPairCount > 0)
					&& FMath::IsNearlyEqual(
						Profile->ShellExcludedRegionCertificationRadiusCm,
						static_cast<float>(ExpectedExcludedRegionRadiusCm),
						1.e-4f)
					&& FMath::IsNearlyEqual(
						Profile->ShellExcludedRegionMaximumWitnessDistanceCm,
						static_cast<float>(IntersectionAudit.MaximumExcludedRegionWitnessDistanceCm),
						1.e-3f);
				EFClothingFitCompilerPrivate::FThicknessShellCompileData ValidationShell;
				ValidationShell.bEnabled = true;
				ValidationShell.PreShellVertexCount = Profile->PreShellVertexCount;
				ValidationShell.RequestedThicknessCm = Profile->CompiledThicknessCm;
				if (BoundaryLoops.bAborted
					|| !BoundaryLoops.Loops.IsEmpty()
					|| !BoundaryLoops.Spans.IsEmpty()
					|| DegenerateTriangleCount != 0
					|| !bIntersectionPolicyPass
					|| !bIntersectionEvidenceMatches
					|| !EFClothingFitCompilerPrivate::RebuildThicknessShellPairing(
						ValidationMesh,
						ValidationShell,
						ShellReadError))
				{
					RejectThicknessShell(FString::Printf(
					TEXT("recomputed shell geometry failed (open=%d degenerate=%d detected/inherited/excluded=%d/%d/%d baselinePairs/radius=%d/%.6fcm affectedResidualSource=%d maxExcludedWitness/radius=%.6f/%.6fcm outer/inner/cross=%d/%d/%d evidence=%s policy=%s reason=%s)"),
						BoundaryLoops.Loops.Num() + BoundaryLoops.Spans.Num(),
						DegenerateTriangleCount,
						IntersectionAudit.DetectedPairCount,
						IntersectionAudit.ToleratedInheritedSourcePairCount,
						IntersectionAudit.ToleratedExcludedRegionPairCount,
						ValidationBaselinePairCount,
						ExpectedBaselineInheritanceRadiusCm,
						IntersectionAudit.AffectedSourceTriangleCount,
						IntersectionAudit.MaximumExcludedRegionWitnessDistanceCm,
						ExpectedExcludedRegionRadiusCm,
						IntersectionAudit.OuterLayerPairCount,
						IntersectionAudit.InnerLayerPairCount,
						IntersectionAudit.CrossLayerOrWallPairCount,
						bIntersectionEvidenceMatches ? TEXT("match") : TEXT("mismatch"),
						bIntersectionPolicyPass ? TEXT("pass") : *IntersectionPolicyError,
						ShellReadError.IsEmpty() ? TEXT("none") : *ShellReadError));
				}
				else if (!FMath::IsNearlyEqual(
						Profile->ShellMinimumMeasuredThicknessCm,
						static_cast<float>(ValidationShell.MinimumMeasuredThicknessCm),
						1.e-3f)
					|| !FMath::IsNearlyEqual(
						Profile->ShellAverageMeasuredThicknessCm,
						static_cast<float>(ValidationShell.AverageMeasuredThicknessCm),
						1.e-3f)
					|| !FMath::IsNearlyEqual(
						Profile->ShellMaximumMeasuredThicknessCm,
						static_cast<float>(ValidationShell.MaximumMeasuredThicknessCm),
						1.e-3f))
				{
					RejectThicknessShell(TEXT("stored thickness measurements differ from fitted geometry readback"));
				}
			}
		}
	}
	else if (Profile->bCompiledThicknessShell
		|| Profile->ThicknessShellAlgorithmVersion != 0
		|| !FMath::IsNearlyZero(Profile->CompiledThicknessCm)
		|| Profile->PreShellVertexCount != 0
		|| Profile->PreShellTriangleCount != 0
		|| Profile->FinalShellVertexCount != 0
		|| Profile->FinalShellTriangleCount != 0
		|| Profile->ShellVertexPairCount != 0
		|| Profile->ShellBoundaryLoopCount != 0
		|| Profile->ShellWallTriangleCount != 0
		|| Profile->ShellOpenBoundaryCountAfter != 0
		|| Profile->ShellDegenerateTriangleCount != 0
		|| Profile->ShellDetectedNonAdjacentIntersectionCount != 0
		|| Profile->ShellBaselineSourceIntersectionPairCount != 0
		|| Profile->ShellToleratedInheritedSourceIntersectionCount != 0
		|| !FMath::IsNearlyZero(Profile->ShellBaselineInheritanceRadiusCm)
		|| Profile->ShellToleratedLocalRepairIntersectionCount != 0
		|| !FMath::IsNearlyZero(Profile->ShellLocalRepairThicknessCeilingCm)
		|| Profile->ShellToleratedExcludedRegionIntersectionCount != 0
		|| Profile->ShellExcludedRegionAffectedSourceTriangleCount != 0
		|| !FMath::IsNearlyZero(Profile->ShellExcludedRegionCertificationRadiusCm)
		|| !FMath::IsNearlyZero(Profile->ShellExcludedRegionMaximumWitnessDistanceCm)
		|| Profile->bShellSelfIntersects
		|| !FMath::IsNearlyZero(Profile->ShellMinimumMeasuredThicknessCm)
		|| !FMath::IsNearlyZero(Profile->ShellAverageMeasuredThicknessCm)
		|| !FMath::IsNearlyZero(Profile->ShellMaximumMeasuredThicknessCm))
	{
		RejectThicknessShell(TEXT("catalog disables thickness but the profile retains shell evidence"));
	}
	bool bSurfacePolicyPass = Profile->ExcludedBodySurfaceTriangleCount >= 0
		&& !Profile->GarmentCompileFingerprint.IsEmpty()
		&& Profile->GarmentCompileFingerprint == CatalogRow.BuildCompileFingerprint()
		&& Profile->ExcludedBodySurfaceMaterialSlots == CatalogExcludedSurfaceSlots
		&& Profile->ExcludedBodyBoneBranches == CatalogExcludedBoneBranches
		&& Profile->ExcludedBodyMorphPrefixes == CatalogExcludedMorphPrefixes;
	FName PreviousExcludedSlot = NAME_None;
	bool bHasPreviousExcludedSlot = false;
	TSet<FName> UniqueExcludedSlots;
	for (const FName ExcludedSlot : Profile->ExcludedBodySurfaceMaterialSlots)
	{
		int32 MaterialMatchCount = 0;
		for (const FSkeletalMaterial& Material : Body->GetMaterials())
		{
			MaterialMatchCount += (Material.MaterialSlotName == ExcludedSlot
				|| Material.ImportedMaterialSlotName == ExcludedSlot) ? 1 : 0;
		}
		if (ExcludedSlot.IsNone()
			|| UniqueExcludedSlots.Contains(ExcludedSlot)
			|| (bHasPreviousExcludedSlot && !PreviousExcludedSlot.LexicalLess(ExcludedSlot))
			|| MaterialMatchCount != 1)
		{
			bSurfacePolicyPass = false;
			break;
		}
		UniqueExcludedSlots.Add(ExcludedSlot);
		PreviousExcludedSlot = ExcludedSlot;
		bHasPreviousExcludedSlot = true;
	}
	bSurfacePolicyPass &= Profile->ExcludedBodySurfaceMaterialSlots.IsEmpty()
		? Profile->ExcludedBodySurfaceTriangleCount == 0
		: Profile->ExcludedBodySurfaceTriangleCount > 0;
	TSet<int32> ExcludedProfileBoneIndices;
	FName PreviousExcludedBoneRoot = NAME_None;
	bool bHasPreviousExcludedBoneRoot = false;
	for (const FName ExcludedRootName : Profile->ExcludedBodyBoneBranches)
	{
		const int32 RootIndex = Body->GetRefSkeleton().FindBoneIndex(ExcludedRootName);
		if (RootIndex <= 0
			|| (bHasPreviousExcludedBoneRoot && !PreviousExcludedBoneRoot.LexicalLess(ExcludedRootName)))
		{
			bSurfacePolicyPass = false;
			break;
		}
		for (int32 BoneIndex = 0; BoneIndex < Body->GetRefSkeleton().GetRawBoneNum(); ++BoneIndex)
		{
			for (int32 AncestorIndex = BoneIndex;
				AncestorIndex != INDEX_NONE;
				AncestorIndex = Body->GetRefSkeleton().GetParentIndex(AncestorIndex))
			{
				if (AncestorIndex == RootIndex)
				{
					ExcludedProfileBoneIndices.Add(BoneIndex);
					break;
				}
			}
		}
		PreviousExcludedBoneRoot = ExcludedRootName;
		bHasPreviousExcludedBoneRoot = true;
	}
	FString PreviousExcludedMorphPrefix;
	bool bHasPreviousExcludedMorphPrefix = false;
	for (const FString& Prefix : Profile->ExcludedBodyMorphPrefixes)
	{
		if (Prefix.IsEmpty()
			|| (bHasPreviousExcludedMorphPrefix && PreviousExcludedMorphPrefix >= Prefix))
		{
			bSurfacePolicyPass = false;
			break;
		}
		PreviousExcludedMorphPrefix = Prefix;
		bHasPreviousExcludedMorphPrefix = true;
	}
	bool bWeightedBonesPass = !Profile->RequiredWeightedBones.IsEmpty();
	FString WeightedBoneFailureReason = bWeightedBonesPass
		? FString()
		: TEXT("required weighted-bone list is empty");
	const FReferenceSkeleton& FittedReference = Fitted->GetRefSkeleton();
	for (int32 PoseMeshIndex = 0; PoseMeshIndex < 2; ++PoseMeshIndex)
	{
		const USkeletalMesh* PoseMesh = PoseMeshIndex == 0 ? Body : Compatibility;
		const TCHAR* PoseMeshLabel = PoseMeshIndex == 0 ? TEXT("Female") : TEXT("Multiple");
		const FReferenceSkeleton& PoseReference = PoseMesh->GetRefSkeleton();
		const bool bRequireExactPose = PoseMesh == Body;
		for (FName BoneName : Profile->RequiredWeightedBones)
		{
			const int32 FittedIndex = FittedReference.FindBoneIndex(BoneName);
			const int32 PoseIndex = PoseReference.FindBoneIndex(BoneName);
			const int32 BodyBoneIndex = Body->GetRefSkeleton().FindBoneIndex(BoneName);
			if (FittedIndex == INDEX_NONE
				|| PoseIndex == INDEX_NONE
				|| BodyBoneIndex == INDEX_NONE
				|| ExcludedProfileBoneIndices.Contains(BodyBoneIndex))
			{
				bWeightedBonesPass = false;
				WeightedBoneFailureReason = FString::Printf(
					TEXT("%s required bone %s is missing or belongs to an excluded branch (fitted=%d pose=%d)"),
					PoseMeshLabel,
					*BoneName.ToString(),
					FittedIndex,
					PoseIndex);
				break;
			}
			const int32 FittedParent = FittedReference.GetParentIndex(FittedIndex);
			const int32 PoseParent = PoseReference.GetParentIndex(PoseIndex);
			const FName FittedParentName = FittedParent == INDEX_NONE ? NAME_None : FittedReference.GetBoneName(FittedParent);
			const FName PoseParentName = PoseParent == INDEX_NONE ? NAME_None : PoseReference.GetBoneName(PoseParent);
			if (FittedParentName != PoseParentName)
			{
				bWeightedBonesPass = false;
				WeightedBoneFailureReason = FString::Printf(
					TEXT("%s required bone %s has parent mismatch (fitted=%s pose=%s)"),
					PoseMeshLabel,
					*BoneName.ToString(),
					*FittedParentName.ToString(),
					*PoseParentName.ToString());
				break;
			}
			if (bRequireExactPose
				&& !FittedReference.GetRefBonePose()[FittedIndex].Equals(
					PoseReference.GetRefBonePose()[PoseIndex], 0.001f))
			{
				bWeightedBonesPass = false;
				WeightedBoneFailureReason = FString::Printf(
					TEXT("%s required bone %s has a bind-pose mismatch"),
					PoseMeshLabel,
					*BoneName.ToString());
				break;
			}
		}
		if (!bWeightedBonesPass)
		{
			break;
		}
	}
	bool bBindingsPass = true;
	auto FittedContainsMorph = [Fitted](FName MorphName)
	{
		if (Fitted->FindMorphTarget(MorphName))
		{
			return true;
		}
		if (const FMeshDescription* Description = Fitted->GetMeshDescription(0))
		{
			const FSkeletalMeshAttributesShared Attributes(*Description);
			return Attributes.GetVertexMorphPositionDelta(MorphName).IsValid();
		}
		return false;
	};
	TSet<FName> UniqueBodyMorphNames;
	for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
	{
		if (Binding.BodyMorph.IsNone()
			|| Binding.BodyMorph == Profile->ClearanceMorphName
			|| UniqueBodyMorphNames.Contains(Binding.BodyMorph))
		{
			bBindingsPass = false;
			break;
		}
		UniqueBodyMorphNames.Add(Binding.BodyMorph);
	}
	TSet<FName> UniquePiecewiseMorphNames;
	int32 ActualGeneratedMorphSampleCount = 0;
	int32 ActualMaximumMorphSamplesPerBinding = 0;
	int32 ActualSteppedMorphIntervalCount = 0;
	int32 ActualIdentityMorphSampleCount = 0;
	for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
	{
		UMorphTarget* BodyMorph = Body->FindMorphTarget(Binding.BodyMorph);
		if (Binding.BodyMorph.IsNone()
			|| !FMath::IsFinite(Binding.MinimumCertifiedValue)
			|| !FMath::IsFinite(Binding.MaximumCertifiedValue)
			|| !FMath::IsFinite(Binding.Scale)
			|| !FMath::IsFinite(Binding.Bias)
			|| Binding.MinimumCertifiedValue > Binding.MaximumCertifiedValue
			|| !FMath::IsNearlyEqual(Binding.MinimumCertifiedValue, 0.0f, KINDA_SMALL_NUMBER)
			|| Binding.MaximumCertifiedValue <= Binding.MinimumCertifiedValue + KINDA_SMALL_NUMBER
			|| Binding.MaximumCertifiedValue > 1.0f + KINDA_SMALL_NUMBER
			|| !FMath::IsNearlyEqual(Binding.Scale, 1.0f, KINDA_SMALL_NUMBER)
			|| !FMath::IsNearlyZero(Binding.Bias, KINDA_SMALL_NUMBER)
			|| Binding.Samples.IsEmpty()
			|| Binding.Samples.Num() > 64
			|| FittedContainsMorph(Binding.BodyMorph)
			|| !IsValid(BodyMorph) || !BodyMorph->HasDataForLOD(0) || BodyMorph->GetNumDeltasForLOD(0) == 0)
		{
			bBindingsPass = false;
			break;
		}
		float PreviousBodyValue = Binding.MinimumCertifiedValue;
		FName ExpectedLegacyGarmentMorph = NAME_None;
		for (int32 SampleIndex = 0; SampleIndex < Binding.Samples.Num(); ++SampleIndex)
		{
			const FEFClothingMorphSample& Sample = Binding.Samples[SampleIndex];
			UMorphTarget* FittedMorph = Sample.bIdentity ? nullptr : Fitted->FindMorphTarget(Sample.GarmentMorph);
			const bool bIdentityContractPass = Sample.bIdentity && Sample.GarmentMorph.IsNone();
			const bool bGeneratedMorphContractPass = !Sample.bIdentity
				&& !Sample.GarmentMorph.IsNone()
				&& Sample.GarmentMorph != Profile->ClearanceMorphName
				&& !UniqueBodyMorphNames.Contains(Sample.GarmentMorph)
				&& !UniquePiecewiseMorphNames.Contains(Sample.GarmentMorph)
				&& IsValid(FittedMorph)
				&& FittedMorph->HasDataForLOD(0)
				&& FittedMorph->GetNumDeltasForLOD(0) > 0;
			if ((!bIdentityContractPass && !bGeneratedMorphContractPass)
				|| !FMath::IsFinite(Sample.BodyValue)
				|| !EFClothingMorphV25::IsCertifiedClearanceMultiplier(
					Sample.MinimumClearanceMultiplier)
				|| Sample.BodyValue <= PreviousBodyValue + KINDA_SMALL_NUMBER
				|| Sample.BodyValue > Binding.MaximumCertifiedValue + KINDA_SMALL_NUMBER
				|| (Sample.bStepFromPrevious
					&& (!FMath::IsFinite(Sample.StepSwitchBodyValue)
						|| Sample.StepSwitchBodyValue < PreviousBodyValue - KINDA_SMALL_NUMBER
						|| Sample.StepSwitchBodyValue > Sample.BodyValue + KINDA_SMALL_NUMBER)))
			{
				bBindingsPass = false;
				break;
			}
			if (Sample.bIdentity)
			{
				++ActualIdentityMorphSampleCount;
			}
			else
			{
				UniquePiecewiseMorphNames.Add(Sample.GarmentMorph);
				ExpectedLegacyGarmentMorph = Sample.GarmentMorph;
			}
			ActualSteppedMorphIntervalCount += Sample.bStepFromPrevious ? 1 : 0;
			PreviousBodyValue = Sample.BodyValue;
		}
		if (!bBindingsPass
			|| Binding.Samples.IsEmpty()
			|| !FMath::IsNearlyEqual(PreviousBodyValue, Binding.MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			|| Binding.GarmentMorph != ExpectedLegacyGarmentMorph)
		{
			bBindingsPass = false;
			break;
		}
		ActualGeneratedMorphSampleCount += Binding.Samples.Num();
		ActualMaximumMorphSamplesPerBinding = FMath::Max(
			ActualMaximumMorphSamplesPerBinding,
			Binding.Samples.Num());
	}
	bool bPairCertificatesPass = bBindingsPass
		&& Profile->CompiledMorphActivationEpsilon == 0.0f;
	TSet<FName> MonitoredBodyMorphNameSet;
	FName PreviousMonitoredBodyMorph = NAME_None;
	bool bHasPreviousMonitoredBodyMorph = false;
	for (FName MonitoredBodyMorph : Profile->MonitoredBodyMorphNames)
	{
		UMorphTarget* BodyMorph = Body->FindMorphTarget(MonitoredBodyMorph);
		const FString MonitoredMorphString = MonitoredBodyMorph.ToString();
		const bool bExcludedMorphNamespace = Profile->ExcludedBodyMorphPrefixes.ContainsByPredicate(
			[&MonitoredMorphString](const FString& Prefix)
			{
				return MonitoredMorphString.StartsWith(Prefix, ESearchCase::CaseSensitive);
			});
		if (MonitoredBodyMorph.IsNone()
			|| MonitoredBodyMorphNameSet.Contains(MonitoredBodyMorph)
			|| bExcludedMorphNamespace
			|| (bHasPreviousMonitoredBodyMorph
				&& !PreviousMonitoredBodyMorph.LexicalLess(MonitoredBodyMorph))
			|| !IsValid(BodyMorph)
			|| !BodyMorph->HasDataForLOD(0)
			|| BodyMorph->GetNumDeltasForLOD(0) <= 0)
		{
			bPairCertificatesPass = false;
			break;
		}
		MonitoredBodyMorphNameSet.Add(MonitoredBodyMorph);
		PreviousMonitoredBodyMorph = MonitoredBodyMorph;
		bHasPreviousMonitoredBodyMorph = true;
	}
	for (FName BoundBodyMorph : UniqueBodyMorphNames)
	{
		bPairCertificatesPass &= MonitoredBodyMorphNameSet.Contains(BoundBodyMorph);
	}
	TSet<FString> UniquePairKeys;
	TSet<FName> UniquePairCellMorphNames;
	int32 ActualGeneratedPairCellMorphCount = 0;
	int32 ActualPairBodyProbeCount = 0;
	int32 ActualPairOffsetEvaluationCount = 0;
	float ActualMinimumSampledPairGap = TNumericLimits<float>::Max();
	for (const FEFClothingMorphPairCertificate& Certificate : Profile->MorphPairCertificates)
	{
		const FEFClothingMorphBinding* FirstCertifiedBinding = Profile->MorphBindings.FindByPredicate(
			[&Certificate](const FEFClothingMorphBinding& Binding)
			{
				return Binding.BodyMorph == Certificate.FirstBodyMorph;
			});
		const FEFClothingMorphBinding* SecondCertifiedBinding = Profile->MorphBindings.FindByPredicate(
			[&Certificate](const FEFClothingMorphBinding& Binding)
			{
				return Binding.BodyMorph == Certificate.SecondBodyMorph;
			});
		const FString PairKey = Certificate.FirstBodyMorph.ToString()
			+ TEXT("\x1F") + Certificate.SecondBodyMorph.ToString();
		const bool bCertificateHeaderPass = !Certificate.FirstBodyMorph.IsNone()
			&& !Certificate.SecondBodyMorph.IsNone()
			&& Certificate.FirstBodyMorph.LexicalLess(Certificate.SecondBodyMorph)
			&& !UniquePairKeys.Contains(PairKey)
			&& UniqueBodyMorphNames.Contains(Certificate.FirstBodyMorph)
			&& UniqueBodyMorphNames.Contains(Certificate.SecondBodyMorph)
			&& MonitoredBodyMorphNameSet.Contains(Certificate.FirstBodyMorph)
			&& MonitoredBodyMorphNameSet.Contains(Certificate.SecondBodyMorph)
			&& FirstCertifiedBinding
			&& SecondCertifiedBinding
			&& FMath::IsNearlyEqual(Certificate.FirstMinimumCertifiedValue, FirstCertifiedBinding->MinimumCertifiedValue, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(Certificate.FirstMaximumCertifiedValue, FirstCertifiedBinding->MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(Certificate.SecondMinimumCertifiedValue, SecondCertifiedBinding->MinimumCertifiedValue, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(Certificate.SecondMaximumCertifiedValue, SecondCertifiedBinding->MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			&& Certificate.GridResolution == 4
			&& Certificate.ProbeCountPerAxis == 3
			&& Certificate.CertifiedOffsetTierCount
				== EFClothingFitCompilerPrivate::CertifiedClearanceTierCount
			&& Certificate.Cells.Num() == 16
			&& FMath::IsFinite(Certificate.MinimumCertifiedGapCm)
			&& Certificate.MinimumCertifiedGapCm >= Profile->CompiledMinimumClearanceCm - 0.001f;
		bPairCertificatesPass &= bCertificateHeaderPass;
		if (!bCertificateHeaderPass)
		{
			break;
		}
		UniquePairKeys.Add(PairKey);
		TSet<int32> UniqueCellCoordinates;
		float CertificateMinimumGap = TNumericLimits<float>::Max();
		const int32 ExpectedBodyProbeCount = Certificate.ProbeCountPerAxis * Certificate.ProbeCountPerAxis;
		const int32 ExpectedOffsetEvaluationCount = ExpectedBodyProbeCount * Certificate.CertifiedOffsetTierCount;
		for (const FEFClothingMorphPairCell& Cell : Certificate.Cells)
		{
			const int32 CoordinateKey = Cell.FirstCellIndex * Certificate.GridResolution + Cell.SecondCellIndex;
			const float ExpectedFirstMinimum = FMath::Lerp(
				Certificate.FirstMinimumCertifiedValue,
				Certificate.FirstMaximumCertifiedValue,
				static_cast<float>(Cell.FirstCellIndex) / static_cast<float>(Certificate.GridResolution));
			const float ExpectedFirstMaximum = FMath::Lerp(
				Certificate.FirstMinimumCertifiedValue,
				Certificate.FirstMaximumCertifiedValue,
				static_cast<float>(Cell.FirstCellIndex + 1) / static_cast<float>(Certificate.GridResolution));
			const float ExpectedSecondMinimum = FMath::Lerp(
				Certificate.SecondMinimumCertifiedValue,
				Certificate.SecondMaximumCertifiedValue,
				static_cast<float>(Cell.SecondCellIndex) / static_cast<float>(Certificate.GridResolution));
			const float ExpectedSecondMaximum = FMath::Lerp(
				Certificate.SecondMinimumCertifiedValue,
				Certificate.SecondMaximumCertifiedValue,
				static_cast<float>(Cell.SecondCellIndex + 1) / static_cast<float>(Certificate.GridResolution));
			UMorphTarget* CellMorph = Fitted->FindMorphTarget(Cell.GarmentMorph);
			const bool bCellPass = Cell.FirstCellIndex >= 0
				&& Cell.FirstCellIndex < Certificate.GridResolution
				&& Cell.SecondCellIndex >= 0
				&& Cell.SecondCellIndex < Certificate.GridResolution
				&& !UniqueCellCoordinates.Contains(CoordinateKey)
				&& !Cell.GarmentMorph.IsNone()
				&& Cell.GarmentMorph != Profile->ClearanceMorphName
				&& !UniqueBodyMorphNames.Contains(Cell.GarmentMorph)
				&& !UniquePiecewiseMorphNames.Contains(Cell.GarmentMorph)
				&& !UniquePairCellMorphNames.Contains(Cell.GarmentMorph)
				&& IsValid(CellMorph)
				&& CellMorph->HasDataForLOD(0)
				&& CellMorph->GetNumDeltasForLOD(0) > 0
				&& FMath::IsNearlyEqual(Cell.FirstMinimumValue, ExpectedFirstMinimum, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Cell.FirstMaximumValue, ExpectedFirstMaximum, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Cell.SecondMinimumValue, ExpectedSecondMinimum, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Cell.SecondMaximumValue, ExpectedSecondMaximum, KINDA_SMALL_NUMBER)
				&& FMath::IsFinite(Cell.MinimumCertifiedGapCm)
				&& Cell.MinimumCertifiedGapCm >= Profile->CompiledMinimumClearanceCm - 0.001f
				&& EFClothingMorphV25::IsCertifiedClearanceMultiplier(
					Cell.MinimumClearanceMultiplier)
				&& Cell.CertifiedBodyProbeCount == ExpectedBodyProbeCount
				&& Cell.CertifiedOffsetEvaluationCount == ExpectedOffsetEvaluationCount;
			bPairCertificatesPass &= bCellPass;
			if (!bCellPass)
			{
				break;
			}
			UniqueCellCoordinates.Add(CoordinateKey);
			UniquePairCellMorphNames.Add(Cell.GarmentMorph);
			CertificateMinimumGap = FMath::Min(CertificateMinimumGap, Cell.MinimumCertifiedGapCm);
			++ActualGeneratedPairCellMorphCount;
			ActualPairBodyProbeCount += Cell.CertifiedBodyProbeCount;
			ActualPairOffsetEvaluationCount += Cell.CertifiedOffsetEvaluationCount;
		}
		bPairCertificatesPass &= UniqueCellCoordinates.Num()
				== Certificate.GridResolution * Certificate.GridResolution
			&& FMath::IsNearlyEqual(
				Certificate.MinimumCertifiedGapCm,
				CertificateMinimumGap,
				0.001f);
		ActualMinimumSampledPairGap = FMath::Min(ActualMinimumSampledPairGap, CertificateMinimumGap);
		if (!bPairCertificatesPass)
		{
			break;
		}
	}
	if (Profile->MorphPairCertificates.IsEmpty())
	{
		ActualMinimumSampledPairGap = 0.0f;
	}
	bPairCertificatesPass &= Profile->CertifiedMorphPairCount == Profile->MorphPairCertificates.Num()
		&& Profile->GeneratedPairCellMorphCount == ActualGeneratedPairCellMorphCount
		&& Profile->PairBodyProbeCount == ActualPairBodyProbeCount
		&& Profile->PairOffsetEvaluationCount == ActualPairOffsetEvaluationCount
		&& FMath::IsFinite(Profile->MinimumSampledPairGapCm)
		&& (Profile->MorphPairCertificates.IsEmpty()
			? FMath::IsNearlyZero(Profile->MinimumSampledPairGapCm, KINDA_SMALL_NUMBER)
			: Profile->MinimumSampledPairGapCm >= Profile->CompiledMinimumClearanceCm - 0.001f
				&& FMath::IsNearlyEqual(
					Profile->MinimumSampledPairGapCm,
					ActualMinimumSampledPairGap,
					0.001f));
	constexpr float ClearanceToleranceCm = 0.001f;
	const FSkeletalMeshLODInfo* FittedLODInfo = Fitted->GetLODInfo(0);
	const FVector BoundsExpansion = Fitted->GetImportedBounds().BoxExtent - Source->GetImportedBounds().BoxExtent;
	const float SphereExpansion = Fitted->GetImportedBounds().SphereRadius - Source->GetImportedBounds().SphereRadius;
	const FVector& ContractBoundsExpansion = Profile->CompiledConcurrentBoundsExpansionCm;
	const bool bBoundsPass = !ContractBoundsExpansion.ContainsNaN()
		&& ContractBoundsExpansion.X >= 0.0f
		&& ContractBoundsExpansion.Y >= 0.0f
		&& ContractBoundsExpansion.Z >= 0.0f
		&& FMath::IsFinite(Profile->CompiledConcurrentSphereExpansionCm)
		&& Profile->CompiledConcurrentSphereExpansionCm >= 0.0f
		&& BoundsExpansion.X >= ContractBoundsExpansion.X - ClearanceToleranceCm
		&& BoundsExpansion.Y >= ContractBoundsExpansion.Y - ClearanceToleranceCm
		&& BoundsExpansion.Z >= ContractBoundsExpansion.Z - ClearanceToleranceCm
		&& SphereExpansion >= Profile->CompiledConcurrentSphereExpansionCm - ClearanceToleranceCm;
	const bool bMetricsPass = Profile->PenetratingVertexCountAfter == 0
		&& Profile->BuildGuid.IsValid()
		&& FMath::IsFinite(Profile->MinimumSignedGapAfterCm)
		&& FMath::IsFinite(Profile->CompiledMinimumClearanceCm)
		&& FMath::IsFinite(Profile->CompiledClearanceReserveCm)
		&& Profile->CompiledClearanceReserveCm > 0.0f
		&& FMath::IsNearlyEqual(
			Profile->CompiledClearanceReserveCm,
			static_cast<float>(EFClothingFitCompilerPrivate::CompilerClearanceReserveCm),
			KINDA_SMALL_NUMBER)
		&& Profile->MinimumSignedGapAfterCm
			>= Profile->CompiledMinimumClearanceCm + Profile->CompiledClearanceReserveCm - ClearanceToleranceCm
		&& Profile->SourceVertexCount > 0
		&& Profile->FittedVertexCount > 0
		&& Profile->ReconciledSplitVertexCount >= 0
		&& Profile->CertifiedSkinWeightVertexCount == Profile->FittedVertexCount
		&& Profile->CompiledLODCount == 1
		&& Profile->CompilerVersion == EFClothingFitCompilerPrivate::CompilerVersion
		&& bFitModePass
		&& (!bSurfaceWrapProfile
			|| Profile->CompiledMinimumClearanceCm
				>= EFClothingMorphV26::DefaultBaseClearanceCm - ClearanceToleranceCm)
		&& FMath::IsFinite(Profile->DefaultClearanceValue)
		&& FMath::IsNearlyEqual(Profile->DefaultClearanceValue, 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsFinite(Profile->CertifiedClearanceMultiplierMin)
		&& FMath::IsFinite(Profile->CertifiedClearanceMultiplierMax)
		&& FMath::IsNearlyEqual(
			Profile->CertifiedClearanceMultiplierMin,
			static_cast<float>(EFClothingFitCompilerPrivate::CertifiedClearanceTierMin),
			KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
			Profile->CertifiedClearanceMultiplierMax,
			static_cast<float>(EFClothingFitCompilerPrivate::CertifiedClearanceTierMax),
			KINDA_SMALL_NUMBER)
		&& Profile->CertifiedClearanceTierCount == EFClothingFitCompilerPrivate::CertifiedClearanceTierCount
		&& FMath::IsFinite(Profile->MinimumCertifiedOffsetGapCm)
		&& Profile->MinimumCertifiedOffsetGapCm >= Profile->CompiledMinimumClearanceCm - ClearanceToleranceCm
		&& FMath::IsFinite(Profile->CompiledMaximumMorphRepairCm)
		&& Profile->CompiledMaximumMorphRepairCm > 0.0f
		&& FMath::IsFinite(Profile->CompiledMaximumMorphDisplacementCm)
		&& Profile->CompiledMaximumMorphDisplacementCm >= 0.0f
		&& FMath::IsFinite(Profile->CompiledMorphThresholdPositionCm)
		&& Profile->CompiledMorphThresholdPositionCm >= 0.0f
		&& FittedLODInfo
		&& FMath::IsNearlyEqual(
			Profile->CompiledMorphThresholdPositionCm,
			FittedLODInfo->BuildSettings.MorphThresholdPosition,
			KINDA_SMALL_NUMBER)
		&& Profile->PostThresholdAlteredDeltaCount >= 0
		&& bThicknessShellPass
		&& bBoundsPass
		&& Profile->ClearanceValidatedMorphCount == Profile->MorphBindings.Num()
		&& Profile->MorphClearanceSampleCount >= 2
		&& Profile->MorphClearanceSampleCount <= 8
		&& Profile->GeneratedMorphSampleCount == ActualGeneratedMorphSampleCount
		&& Profile->MaximumMorphSamplesPerBinding == ActualMaximumMorphSamplesPerBinding
		&& Profile->SteppedMorphIntervalCount == ActualSteppedMorphIntervalCount
		&& Profile->IdentityMorphSampleCount == ActualIdentityMorphSampleCount
		&& (Profile->MorphBindings.IsEmpty()
			? Profile->MaximumMorphSamplesPerBinding == 0
			: Profile->MaximumMorphSamplesPerBinding >= 1)
		&& Profile->MaximumMorphSamplesPerBinding <= 64
		&& FMath::IsFinite(Profile->MinimumSampledMorphGapCm)
		&& Profile->MinimumSampledMorphGapCm
			>= Profile->CompiledMinimumClearanceCm - ClearanceToleranceCm;

	const bool bPass = bSkeletonFingerprintPass
		&& bContentFingerprintPass
		&& bPackageIdentityPass
		&& bCatalogPass
		&& bFitModePass
		&& bSurfaceBindingPass
		&& bProfileExists
		&& bClearanceMorphExists
		&& bThicknessShellPass
		&& bSurfacePolicyPass
		&& bWeightedBonesPass
		&& bBindingsPass
		&& bPairCertificatesPass
		&& bBoundsPass
		&& bMetricsPass;
	OutReport = FString::Printf(
		TEXT("%s | Skeletons=%s | Content=%s | Packages=%s | Catalog=%s:%s backend=%d mode=%d | SurfaceBinding=%s:%s | SkinProfile=%s | SurfacePolicy=%s:slots=%d triangles=%d branches=%d morphPrefixes=%d | Thickness=%s:enabled=%d requested=%.4fcm measured=%.4f/%.4f/%.4fcm pairs=%d walls=%d reason=%s | WeightedBones=%s:%d remapped=%d reason=%s | ClearanceMorph=%s | RestPenetration=%d | MinGap=%.4f/%.4fcm | MorphSamples=%.4fcm:%d repaired=%d | Bindings=%s:%d | Pairs=%s:%d cells=%d probes=%d tiers=%d | LODs=%d | Compiler=%d"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bSkeletonFingerprintPass ? TEXT("PASS") : TEXT("FAIL"),
		bContentFingerprintPass ? TEXT("PASS") : TEXT("FAIL"),
		bPackageIdentityPass ? TEXT("PASS") : TEXT("FAIL"),
		bCatalogPass && bFitModePass ? TEXT("PASS") : TEXT("FAIL"),
		*CatalogRowName.ToString(),
		static_cast<int32>(CatalogRow.Backend),
		static_cast<int32>(Profile->FitMode),
		bSurfaceBindingPass ? TEXT("PASS") : TEXT("FAIL"),
		bSurfaceBindingPass ? TEXT("none") : *SurfaceBindingFailureReason,
		bProfileExists ? TEXT("PASS") : TEXT("FAIL"),
		bSurfacePolicyPass ? TEXT("PASS") : TEXT("FAIL"),
		Profile->ExcludedBodySurfaceMaterialSlots.Num(),
		Profile->ExcludedBodySurfaceTriangleCount,
		Profile->ExcludedBodyBoneBranches.Num(),
		Profile->ExcludedBodyMorphPrefixes.Num(),
		bThicknessShellPass ? TEXT("PASS") : TEXT("FAIL"),
		Profile->bCompiledThicknessShell ? 1 : 0,
		Profile->CompiledThicknessCm,
		Profile->ShellMinimumMeasuredThicknessCm,
		Profile->ShellAverageMeasuredThicknessCm,
		Profile->ShellMaximumMeasuredThicknessCm,
		Profile->ShellVertexPairCount,
		Profile->ShellWallTriangleCount,
		bThicknessShellPass ? TEXT("none") : *ThicknessShellFailureReason,
		bWeightedBonesPass ? TEXT("PASS") : TEXT("FAIL"),
		Profile->RequiredWeightedBones.Num(),
		Profile->RemappedWeightedBoneCount,
		bWeightedBonesPass ? TEXT("none") : *WeightedBoneFailureReason,
		bClearanceMorphExists ? TEXT("PASS") : TEXT("FAIL"),
		Profile->PenetratingVertexCountAfter,
		Profile->MinimumSignedGapAfterCm,
		Profile->CompiledMinimumClearanceCm,
		Profile->MinimumSampledMorphGapCm,
		Profile->MorphClearanceSampleCount,
		Profile->ClearanceRepairedMorphCount,
		bBindingsPass ? TEXT("PASS") : TEXT("FAIL"),
		Profile->MorphBindings.Num(),
		bPairCertificatesPass ? TEXT("PASS") : TEXT("FAIL"),
		Profile->MorphPairCertificates.Num(),
		Profile->GeneratedPairCellMorphCount,
		Profile->PairBodyProbeCount,
		Profile->PairOffsetEvaluationCount,
		Profile->CompiledLODCount,
		Profile->CompilerVersion);
	return bPass;
}

FEFClothingFitValidationResult UEFClothingFitCompilerLibrary::ValidateCompiledProfileDetailed(
	UEFClothingFitProfile* Profile)
{
	FEFClothingFitValidationResult Result;
	Result.bSuccess = ValidateCompiledProfile(Profile, Result.Report);
	return Result;
}
