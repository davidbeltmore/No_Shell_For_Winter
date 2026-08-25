#include "EFClothingFitCompilerLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "EFClothingFitProfile.h"
#include "EFClothingSkeletonFingerprint.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "MeshQueries.h"
#include "Misc/PackageName.h"
#include "Operations/TransferBoneWeights.h"
#include "Rendering/SkinWeightProfile.h"
#include "SkeletalMeshAttributes.h"
#include "SkinnedAssetCompiler.h"
#include "Spatial/MeshAABBTree3.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingFitCompiler, Log, All);

namespace EFClothingFitCompilerPrivate
{
	using namespace UE::Geometry;

	static constexpr int32 CompilerVersion = 2;
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
	};

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
		return Root.StartsWith(TEXT("/Game/_Generated/EFClothingMorphV2"), ESearchCase::CaseSensitive)
			&& !Root.Contains(TEXT(".."));
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

	static USkeletalMesh* FindOrDuplicateDerived(
		USkeletalMesh* SourceGarment,
		const FString& OutputRoot,
		FString& OutError)
	{
		const FString AssetName = FString::Printf(TEXT("SK_%s_EFV2"), *SanitizeAssetName(SourceGarment->GetName()));
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *OutputRoot, *AssetName, *AssetName);
		if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *ObjectPath))
		{
			if (Existing == SourceGarment || Existing->GetOutermost() == SourceGarment->GetOutermost())
			{
				OutError = TEXT("Generated asset resolves to the protected source package.");
				return nullptr;
			}
			return Existing;
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
		double& OutMinimumAfter)
	{
		FDynamicMeshAABBTree3 BodySpatial(&BodyMesh, true);
		FMeshNormals BodyNormals(&BodyMesh);
		BodyNormals.ComputeVertexNormals();

		OutCorrespondence.SetNum(GarmentMesh.MaxVertexID());
		OutDeltas.Init(FVector3d::Zero(), GarmentMesh.MaxVertexID());
		OutPenetratingBefore = 0;
		OutMinimumBefore = TNumericLimits<double>::Max();

		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d Position = GarmentMesh.GetVertex(VertexID);
			double DistanceSquared = TNumericLimits<double>::Max();
			const int32 TriangleID = BodySpatial.FindNearestTriangle(Position, DistanceSquared);
			if (TriangleID == IndexConstants::InvalidID)
			{
				return false;
			}

			const FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(BodyMesh, TriangleID, Position);
			const FVector3d Normal = InterpolateNormal(BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			const double SignedGap = (Position - Query.ClosestTrianglePoint).Dot(Normal);

			FSurfaceCorrespondence& Correspondence = OutCorrespondence[VertexID];
			Correspondence.BodyTriangle = TriangleID;
			Correspondence.Barycentric = Query.TriangleBaryCoords;
			Correspondence.SurfaceNormal = Normal;
			Correspondence.ClosestPoint = Query.ClosestTrianglePoint;
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

		// Re-project after smoothing so smoothing cannot reintroduce rest-pose penetration.
		OutPenetratingAfter = 0;
		OutMinimumAfter = TNumericLimits<double>::Max();
		for (int32 VertexID : GarmentMesh.VertexIndicesItr())
		{
			const FVector3d BasePosition = GarmentMesh.GetVertex(VertexID);
			FVector3d FittedPosition = BasePosition + OutDeltas[VertexID];
			double DistanceSquared = TNumericLimits<double>::Max();
			int32 TriangleID = BodySpatial.FindNearestTriangle(FittedPosition, DistanceSquared);
			FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(BodyMesh, TriangleID, FittedPosition);
			FVector3d Normal = InterpolateNormal(BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
			double SignedGap = (FittedPosition - Query.ClosestTrianglePoint).Dot(Normal);
			if (SignedGap < DesiredClearance)
			{
				const double RemainingCapacity = FMath::Max(0.0, MaximumPush - OutDeltas[VertexID].Length());
				const double Correction = FMath::Min(DesiredClearance - SignedGap, RemainingCapacity);
				OutDeltas[VertexID] += Normal * Correction;
				FittedPosition = BasePosition + OutDeltas[VertexID];
				TriangleID = BodySpatial.FindNearestTriangle(FittedPosition, DistanceSquared);
				Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(BodyMesh, TriangleID, FittedPosition);
				Normal = InterpolateNormal(BodyMesh, BodyNormals, TriangleID, Query.TriangleBaryCoords);
				SignedGap = (FittedPosition - Query.ClosestTrianglePoint).Dot(Normal);
			}

			OutMinimumAfter = FMath::Min(OutMinimumAfter, SignedGap);
			if (SignedGap < 0.0)
			{
				++OutPenetratingAfter;
			}
		}

		return true;
	}

	static bool TransferWeights(
		const FDynamicMesh3& BodyMesh,
		UDynamicMesh* GarmentDynamicMesh,
		int32 MaximumInfluences,
		FString& OutMethod,
		FString& OutError)
	{
		auto RunTransfer = [&](FTransferBoneWeights::ETransferBoneWeightsMethod Method) -> bool
		{
			FTransferBoneWeights Transfer(&BodyMesh, FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName);
			Transfer.TransferMethod = Method;
			Transfer.MaxNumInfluences = FMath::Clamp(MaximumInfluences, 1, 12);
			Transfer.bUseParallel = true;
			Transfer.LayeredMeshSupport = false;
			if (Method == FTransferBoneWeights::ETransferBoneWeightsMethod::InpaintWeights)
			{
				Transfer.SearchRadius = 0.05 * GarmentDynamicMesh->GetMeshRef().GetBounds().DiagonalLength();
				Transfer.NormalThreshold = FMath::DegreesToRadians(35.0);
				Transfer.NumSmoothingIterations = 10;
				Transfer.SmoothingStrength = 0.1f;
			}
			if (Transfer.Validate() != EOperationValidationResult::Ok)
			{
				return false;
			}

			bool bTransferred = false;
			GarmentDynamicMesh->EditMesh([&](FDynamicMesh3& EditMesh)
			{
				bTransferred = Transfer.TransferWeightsToMesh(EditMesh, FitWeightProfileName);
			}, EDynamicMeshChangeType::AttributeEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
			return bTransferred;
		};

		if (RunTransfer(FTransferBoneWeights::ETransferBoneWeightsMethod::InpaintWeights))
		{
			OutMethod = TEXT("InpaintWeights");
		}
		else if (RunTransfer(FTransferBoneWeights::ETransferBoneWeightsMethod::ClosestPointOnSurface))
		{
			OutMethod = TEXT("ClosestPointOnSurface fallback");
		}
		else
		{
			OutError = TEXT("Both InpaintWeights and ClosestPointOnSurface weight transfer failed.");
			return false;
		}

		bool bWeightsValid = true;
		GarmentDynamicMesh->ProcessMesh([&](const FDynamicMesh3& Mesh)
		{
			const FDynamicMeshVertexSkinWeightsAttribute* Weights = Mesh.Attributes()
				? Mesh.Attributes()->GetSkinWeightsAttribute(FitWeightProfileName)
				: nullptr;
			if (!Weights)
			{
				bWeightsValid = false;
				return;
			}

			for (int32 VertexID : Mesh.VertexIndicesItr())
			{
				UE::AnimationCore::FBoneWeights VertexWeights;
				Weights->GetValue(VertexID, VertexWeights);
				if (VertexWeights.Num() == 0 || VertexWeights.Num() > MaximumInfluences)
				{
					bWeightsValid = false;
					break;
				}
			}
		});

		if (!bWeightsValid)
		{
			OutError = TEXT("Transferred weights contain an unweighted vertex or exceed the influence limit.");
		}
		return bWeightsValid;
	}

	static bool WriteSkinProfile(UDynamicMesh* GarmentDynamicMesh, USkeletalMesh* Derived, FString& OutError)
	{
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
				ProfileInfo.DefaultProfile = true;
				ProfileInfo.DefaultProfileFromLODIndex = 0;
			}
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

	static TArray<FName> BakeMorphs(
		USkeletalMesh* BodySurface,
		USkeletalMesh* SourceGarment,
		USkeletalMesh* Derived,
		const UDynamicMesh* BodyDynamicMesh,
		const UDynamicMesh* GarmentDynamicMesh,
		const TArray<FSurfaceCorrespondence>& Correspondence,
		const FEFClothingFitCompileOptions& Options,
		TArray<FEFClothingMorphBinding>& OutBindings,
		FString& OutError)
	{
		TArray<FName> TransferredNames;
		const FMeshDescription* BodyDescription = BodySurface->GetMeshDescription(0);
		const FMeshDescription* GarmentDescription = SourceGarment->GetMeshDescription(0);
		if (!BodyDescription || !GarmentDescription)
		{
			OutError = TEXT("Body or garment MeshDescription disappeared during morph baking.");
			return TransferredNames;
		}

		const FSkeletalMeshAttributesShared BodyAttributes(*BodyDescription);
		const FSkeletalMeshAttributesShared GarmentAttributes(*GarmentDescription);
		const TArray<FName> BodyMorphNames = BodyAttributes.GetMorphTargetNames();
		const TSet<FName> ExistingGarmentMorphNames(GarmentAttributes.GetMorphTargetNames());

		for (FName BodyMorphName : BodyMorphNames)
		{
			if (ExistingGarmentMorphNames.Contains(BodyMorphName))
			{
				FEFClothingMorphBinding& Binding = OutBindings.AddDefaulted_GetRef();
				Binding.BodyMorph = BodyMorphName;
				Binding.GarmentMorph = BodyMorphName;
			}
		}

		if (!Options.bTransferMissingBodyMorphs || Options.MaximumTransferredMorphs <= 0)
		{
			return TransferredNames;
		}

		const FDynamicMesh3& BodyMesh = BodyDynamicMesh->GetMeshRef();
		const FDynamicMesh3& GarmentMesh = GarmentDynamicMesh->GetMeshRef();
		const FNonManifoldMappingSupport BodyMapping(BodyMesh);
		TArray<FMorphCandidate> Candidates;

		for (FName MorphName : BodyMorphNames)
		{
			if (ExistingGarmentMorphNames.Contains(MorphName) || MorphName == ClearanceMorphName)
			{
				continue;
			}

			const TVertexAttributesConstRef<FVector3f> MorphDeltas = BodyAttributes.GetVertexMorphPositionDelta(MorphName);
			if (!MorphDeltas.IsValid())
			{
				continue;
			}

			double MaxDelta = 0.0;
			for (int32 VertexID : GarmentMesh.VertexIndicesItr())
			{
				MaxDelta = FMath::Max(MaxDelta, GetTransferredBodyMorphDelta(
					BodyMesh, BodyMapping, MorphDeltas, Correspondence[VertexID]).Length());
			}

			if (MaxDelta >= Options.MinimumTransferredMorphDeltaCm)
			{
				Candidates.Add({MorphName, MaxDelta});
			}
		}

		Candidates.Sort([](const FMorphCandidate& A, const FMorphCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.MaxDelta, B.MaxDelta))
			{
				return A.MaxDelta > B.MaxDelta;
			}
			return A.Name.LexicalLess(B.Name);
		});
		Candidates.SetNum(FMath::Min(Candidates.Num(), Options.MaximumTransferredMorphs));

		for (const FMorphCandidate& Candidate : Candidates)
		{
			const TVertexAttributesConstRef<FVector3f> MorphDeltas = BodyAttributes.GetVertexMorphPositionDelta(Candidate.Name);
			UDynamicMesh* MorphMesh = NewObject<UDynamicMesh>(GetTransientPackage());
			MorphMesh->SetMesh(GarmentMesh);
			MorphMesh->EditMesh([&](FDynamicMesh3& EditMesh)
			{
				for (int32 VertexID : EditMesh.VertexIndicesItr())
				{
					const FVector3d Delta = GetTransferredBodyMorphDelta(
						BodyMesh, BodyMapping, MorphDeltas, Correspondence[VertexID]);
					EditMesh.SetVertex(VertexID, EditMesh.GetVertex(VertexID) + Delta);
				}
			}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions, false);

			FString MorphError;
			if (WriteMorph(MorphMesh, Derived, Candidate.Name, MorphError))
			{
				TransferredNames.Add(Candidate.Name);
				FEFClothingMorphBinding& Binding = OutBindings.AddDefaulted_GetRef();
				Binding.BodyMorph = Candidate.Name;
				Binding.GarmentMorph = Candidate.Name;
			}
			else
			{
				UE_LOG(LogEFClothingFitCompiler, Warning, TEXT("Skipping morph %s: %s"), *Candidate.Name.ToString(), *MorphError);
			}
		}

		OutBindings.Sort([](const FEFClothingMorphBinding& A, const FEFClothingMorphBinding& B)
		{
			return A.BodyMorph.LexicalLess(B.BodyMorph);
		});
		return TransferredNames;
	}
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
	auto Fail = [&Result](const FString& Message)
	{
		Result.Report = FString::Printf(TEXT("FAIL: %s"), *Message);
		UE_LOG(LogEFClothingFitCompiler, Error, TEXT("%s"), *Result.Report);
		return Result;
	};

	if (!IsValid(SourceGarment) || !IsValid(BodySurface) || !IsValid(CompatibilityReference))
	{
		return Fail(TEXT("Source garment, body surface and compatibility reference are required."));
	}
	if (!IsAllowedOutputRoot(Options.OutputRoot))
	{
		return Fail(TEXT("OutputRoot must remain under /Game/_Generated/EFClothingMorphV2."));
	}
	if (SourceGarment == BodySurface || SourceGarment == CompatibilityReference)
	{
		return Fail(TEXT("Source garment cannot be one of the protected body/reference inputs."));
	}
	if (SourceGarment->GetSkeleton() != BodySurface->GetSkeleton()
		|| SourceGarment->GetSkeleton() != CompatibilityReference->GetSkeleton())
	{
		return Fail(TEXT("All inputs must reference the exact same USkeleton object; no merge is permitted."));
	}

	FString SkeletonFailure;
	if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(SourceGarment, BodySurface, &SkeletonFailure))
	{
		return Fail(FString::Printf(TEXT("Source/body reference skeleton mismatch: %s"), *SkeletonFailure));
	}

	const FString SourceFingerprintBefore = EFClothingSkeleton::BuildFingerprint(SourceGarment);
	const FString BodyFingerprintBefore = EFClothingSkeleton::BuildFingerprint(BodySurface);
	const FString CompatibilityFingerprintBefore = EFClothingSkeleton::BuildFingerprint(CompatibilityReference);
	USkeleton* const SharedSkeletonBefore = SourceGarment->GetSkeleton();

	FString Error;
	USkeletalMesh* Derived = FindOrDuplicateDerived(SourceGarment, Options.OutputRoot, Error);
	if (!Derived)
	{
		return Fail(Error);
	}
	if (Derived == SourceGarment || Derived == BodySurface || Derived == CompatibilityReference)
	{
		return Fail(TEXT("Compiler resolved a protected asset as its output."));
	}
	if (Derived->GetSkeleton() != SharedSkeletonBefore)
	{
		return Fail(TEXT("Derived garment does not preserve the source USkeleton pointer."));
	}

	UDynamicMesh* BodyDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	UDynamicMesh* GarmentDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	if (!CopySourceLOD(BodySurface, BodyDynamicMesh, Error) || !CopySourceLOD(SourceGarment, GarmentDynamicMesh, Error))
	{
		return Fail(Error);
	}

	FString WeightMethod;
	if (!TransferWeights(
		BodyDynamicMesh->GetMeshRef(),
		GarmentDynamicMesh,
		FMath::Clamp(Options.MaximumInfluences, 1, 12),
		WeightMethod,
		Error))
	{
		return Fail(Error);
	}

	TArray<FSurfaceCorrespondence> Correspondence;
	TArray<FVector3d> ClearanceDeltas;
	int32 PenetratingBefore = 0;
	int32 PenetratingAfter = 0;
	double MinimumBefore = 0.0;
	double MinimumAfter = 0.0;
	if (!BuildCorrespondenceAndClearance(
		BodyDynamicMesh->GetMeshRef(),
		GarmentDynamicMesh->GetMeshRef(),
		FMath::Max(static_cast<double>(Options.MinimumClearanceCm), 0.02),
		FMath::Max(static_cast<double>(Options.MaximumPushCm), static_cast<double>(Options.MinimumClearanceCm)),
		FMath::Clamp(Options.SmoothingIterations, 0, 20),
		Correspondence,
		ClearanceDeltas,
		PenetratingBefore,
		PenetratingAfter,
		MinimumBefore,
		MinimumAfter))
	{
		return Fail(TEXT("Surface correspondence or clearance solve failed."));
	}

	if (!WriteSkinProfile(GarmentDynamicMesh, Derived, Error))
	{
		return Fail(Error);
	}

	UDynamicMesh* ClearanceDynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	ClearanceDynamicMesh->SetMesh(GarmentDynamicMesh->GetMeshRef());
	int32 AdjustedVertexCount = 0;
	ClearanceDynamicMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		for (int32 VertexID : EditMesh.VertexIndicesItr())
		{
			if (ClearanceDeltas[VertexID].SquaredLength() > FMath::Square(0.001))
			{
				++AdjustedVertexCount;
			}
			EditMesh.SetVertex(VertexID, EditMesh.GetVertex(VertexID) + ClearanceDeltas[VertexID]);
		}
	}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions, false);

	if (!WriteMorph(ClearanceDynamicMesh, Derived, ClearanceMorphName, Error))
	{
		return Fail(Error);
	}

	TArray<FEFClothingMorphBinding> MorphBindings;
	TArray<FName> TransferredMorphNames = BakeMorphs(
		BodySurface,
		SourceGarment,
		Derived,
		BodyDynamicMesh,
		GarmentDynamicMesh,
		Correspondence,
		Options,
		MorphBindings,
		Error);
	if (!Error.IsEmpty())
	{
		return Fail(Error);
	}

	if (Options.bCopyBodyDeformerToDerived)
	{
		Derived->SetDefaultMeshDeformer(BodySurface->GetDefaultMeshDeformer());
		Derived->SetTargetMeshDeformers(BodySurface->GetTargetMeshDeformers());
	}

	FBoxSphereBounds ExpandedBounds = Derived->GetImportedBounds();
	ExpandedBounds.BoxExtent += FVector(Options.MaximumPushCm);
	ExpandedBounds.SphereRadius += Options.MaximumPushCm;
	Derived->SetImportedBounds(ExpandedBounds);
	Derived->InvalidateDeriveDataCacheGUID();
	Derived->MarkPackageDirty();
	Derived->PostEditChange();
	FSkinnedAssetCompilingManager::Get().FinishCompilation({Derived});

	const FString ProfileName = FString::Printf(TEXT("DA_%s_EFV2Fit"), *SanitizeAssetName(SourceGarment->GetName()));
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
	Profile->BuildGuid = FGuid::NewGuid();
	Profile->CompilerVersion = CompilerVersion;
	Profile->FitMode = EEFClothingFitMode::Tight;
	Profile->SkinWeightProfileName = FitWeightProfileName;
	Profile->ClearanceMorphName = ClearanceMorphName;
	Profile->DefaultClearanceValue = 1.0f;
	Profile->CompiledMinimumClearanceCm = Options.MinimumClearanceCm;
	Profile->CompiledMaxPushCm = Options.MaximumPushCm;
	Profile->MorphBindings = MoveTemp(MorphBindings);
	Profile->SourceSkeletonFingerprint = SourceFingerprintBefore;
	Profile->BodySkeletonFingerprint = BodyFingerprintBefore;
	Profile->CompatibilitySkeletonFingerprint = CompatibilityFingerprintBefore;
	Profile->SourceVertexCount = GarmentDynamicMesh->GetMeshRef().VertexCount();
	Profile->AdjustedVertexCount = AdjustedVertexCount;
	Profile->PenetratingVertexCountBefore = PenetratingBefore;
	Profile->PenetratingVertexCountAfter = PenetratingAfter;
	Profile->MinimumSignedGapBeforeCm = static_cast<float>(MinimumBefore);
	Profile->MinimumSignedGapAfterCm = static_cast<float>(MinimumAfter);
	Profile->TransferredMorphCount = TransferredMorphNames.Num();
	Profile->MarkPackageDirty();

	Registry->Profiles.RemoveAll([SourceGarment](const UEFClothingFitProfile* ExistingProfile)
	{
		return !IsValid(ExistingProfile) || ExistingProfile->MatchesSource(SourceGarment);
	});
	Registry->Profiles.Add(Profile);
	Registry->Profiles.Sort([](const UEFClothingFitProfile& A, const UEFClothingFitProfile& B)
	{
		return A.SourceGarment.ToSoftObjectPath().ToString() < B.SourceGarment.ToSoftObjectPath().ToString();
	});
	Registry->MarkPackageDirty();

	if (!SaveAsset(Derived, Error) || !SaveAsset(Profile, Error) || !SaveAsset(Registry, Error))
	{
		return Fail(Error);
	}

	if (SourceGarment->GetSkeleton() != SharedSkeletonBefore
		|| BodySurface->GetSkeleton() != SharedSkeletonBefore
		|| CompatibilityReference->GetSkeleton() != SharedSkeletonBefore
		|| EFClothingSkeleton::BuildFingerprint(SourceGarment) != SourceFingerprintBefore
		|| EFClothingSkeleton::BuildFingerprint(BodySurface) != BodyFingerprintBefore
		|| EFClothingSkeleton::BuildFingerprint(CompatibilityReference) != CompatibilityFingerprintBefore)
	{
		return Fail(TEXT("Protected skeleton integrity changed during compile; generated output is invalid."));
	}

	Result.bSuccess = true;
	Result.DerivedGarment = Derived;
	Result.Profile = Profile;
	Result.Report = FString::Printf(
		TEXT("PASS | Source=%s | Derived=%s | Vertices=%d | Adjusted=%d | PenetratingBefore=%d | PenetratingAfter=%d | MinGapBefore=%.4fcm | MinGapAfter=%.4fcm | Weights=%s max%d | CommonMorphs=%d | TransferredMorphs=%d | Build=%s"),
		*SourceGarment->GetPathName(),
		*Derived->GetPathName(),
		Profile->SourceVertexCount,
		AdjustedVertexCount,
		PenetratingBefore,
		PenetratingAfter,
		MinimumBefore,
		MinimumAfter,
		*WeightMethod,
		Options.MaximumInfluences,
		Profile->MorphBindings.Num() - Profile->TransferredMorphCount,
		Profile->TransferredMorphCount,
		*Profile->BuildGuid.ToString(EGuidFormats::DigitsWithHyphens));
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

	FString FailureReason;
	if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(Source, Fitted, &FailureReason)
		|| !EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(Fitted, Body, &FailureReason))
	{
		OutReport = FString::Printf(TEXT("FAIL: %s"), *FailureReason);
		return false;
	}

	const bool bFingerprintPass = EFClothingSkeleton::BuildFingerprint(Source) == Profile->SourceSkeletonFingerprint
		&& EFClothingSkeleton::BuildFingerprint(Body) == Profile->BodySkeletonFingerprint
		&& EFClothingSkeleton::BuildFingerprint(Compatibility) == Profile->CompatibilitySkeletonFingerprint;
	const bool bProfileExists = Fitted->GetSkinWeightProfiles().ContainsByPredicate([Profile](const FSkinWeightProfileInfo& Info)
	{
		return Info.Name == Profile->SkinWeightProfileName;
	});
	const bool bClearanceMorphExists = Fitted->FindMorphTarget(Profile->ClearanceMorphName) != nullptr;
	const bool bMetricsPass = Profile->PenetratingVertexCountAfter == 0
		&& Profile->MinimumSignedGapAfterCm >= -0.001f
		&& Profile->SourceVertexCount > 0;

	const bool bPass = bFingerprintPass && bProfileExists && bClearanceMorphExists && bMetricsPass;
	OutReport = FString::Printf(
		TEXT("%s | Fingerprints=%s | SkinProfile=%s | ClearanceMorph=%s | RestPenetration=%d | MinGap=%.4fcm | Bindings=%d"),
		bPass ? TEXT("PASS") : TEXT("FAIL"),
		bFingerprintPass ? TEXT("PASS") : TEXT("FAIL"),
		bProfileExists ? TEXT("PASS") : TEXT("FAIL"),
		bClearanceMorphExists ? TEXT("PASS") : TEXT("FAIL"),
		Profile->PenetratingVertexCountAfter,
		Profile->MinimumSignedGapAfterCm,
		Profile->MorphBindings.Num());
	return bPass;
}
