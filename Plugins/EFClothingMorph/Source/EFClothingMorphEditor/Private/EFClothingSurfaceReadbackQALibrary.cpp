#include "EFClothingSurfaceReadbackQALibrary.h"

#include "Animation/MeshDeformerGeometryReadback.h"
#include "Animation/MeshDeformerInstance.h"
#include "Async/Async.h"
#include "Components/SkeletalMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "EFClothingSurfaceBinding.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "EngineGlobals.h"
#include "HAL/PlatformTime.h"
#include "MeshQueries.h"
#include "Misc/ScopeLock.h"
#include "OptimusDeformer.h"
#include "OptimusDeformerInstance.h"
#include "RawIndexBuffer.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/UObjectHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingSurfaceReadbackQA, Log, All);

namespace EFClothingSurfaceReadbackQA
{
	using namespace UE::Geometry;

	constexpr float VertexGapToleranceCm = -0.02f;
	constexpr float ClearanceResidualToleranceCm = -0.03f;

	struct FLODTopologySnapshot
	{
		int32 LODIndex = INDEX_NONE;
		int32 RenderVertexCount = 0;
		TArray<int32> TriangleIndices;
		int32 ExcludedOrHiddenSectionCount = 0;
		int32 ExcludedRestDegenerateTriangleCount = 0;
	};

	struct FWitnessSnapshot
	{
		FIntVector GarmentRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FVector3f GarmentBarycentrics = FVector3f::ZeroVector;
		FIntVector BodyRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FVector3f BodyBarycentrics = FVector3f::ZeroVector;
		float TargetClearanceCm = 0.0f;
	};

	struct FVertexAnchorSnapshot
	{
		FIntVector BodyRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FVector3f BodyBarycentrics = FVector3f::ZeroVector;
		FVector3f RestTangentFrameOffsetCm = FVector3f::ZeroVector;
		float TargetClearanceCm = 0.0f;
		float FollowWeight = 0.0f;
		float MaximumCorrectionCm = 0.0f;
		EEFClothingSurfaceVertexMode Mode = EEFClothingSurfaceVertexMode::CollisionOnly;
	};

	struct FLODBindingSnapshot
	{
		int32 GarmentLODIndex = INDEX_NONE;
		int32 BodyLODIndex = INDEX_NONE;
		int32 ExcludedDegenerateBodyTriangleCount = 0;
		TArray<FVertexAnchorSnapshot> VertexAnchors;
		TArray<FWitnessSnapshot> Witnesses;
	};

	struct FVertexArraySnapshot
	{
		int32 LODIndex = INDEX_NONE;
		TArray<FVector3f> Positions;
	};

	struct FReadbackState : TSharedFromThis<FReadbackState, ESPMode::ThreadSafe>
	{
		FCriticalSection Mutex;
		FEFClothingSurfaceReadbackQAResult Result;
		TMap<int32, FLODTopologySnapshot> GarmentTopologies;
		TMap<int32, FLODTopologySnapshot> BodyTopologies;
		TMap<uint64, FLODBindingSnapshot> LODBindings;
		FTransform BodyToGarment = FTransform::Identity;
		FVertexArraySnapshot BodyFinal;
		FVertexArraySnapshot GarmentBase;
		FVertexArraySnapshot GarmentFinal;
		int32 PendingSurfaceCandidateCallbacks = 0;
		bool bBodyReceived = false;
		bool bGarmentBaseReceived = false;
		bool bGarmentFinalReceived = false;
		bool bAnalysisStarted = false;
		double StartSeconds = 0.0;
	};

	FCriticalSection GRequestRegistryMutex;
	TMap<FString, TSharedPtr<FReadbackState, ESPMode::ThreadSafe>> GRequestRegistry;

	uint64 MakeLODKey(const int32 GarmentLODIndex, const int32 BodyLODIndex)
	{
		return (static_cast<uint64>(static_cast<uint32>(GarmentLODIndex)) << 32)
			| static_cast<uint32>(BodyLODIndex);
	}

	bool IsFinite(const FVector3f& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFinite(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	void SetFailed(
		const TSharedPtr<FReadbackState, ESPMode::ThreadSafe>& State,
		const FString& Error)
	{
		FScopeLock Lock(&State->Mutex);
		if (State->Result.State == EEFClothingSurfaceReadbackQAState::Ready
			|| State->Result.State == EEFClothingSurfaceReadbackQAState::Failed)
		{
			return;
		}
		State->Result.State = EEFClothingSurfaceReadbackQAState::Failed;
		State->Result.Error = Error;
		State->Result.TotalLatencyMs = static_cast<float>(
			(FPlatformTime::Seconds() - State->StartSeconds) * 1000.0);
	}

	bool SnapshotComponentTopologies(
		USkeletalMeshComponent* Component,
		const TSet<FName>& ExplicitlyExcludedMaterialSlots,
		TMap<int32, FLODTopologySnapshot>& OutTopologies,
		FString& OutError)
	{
		USkeletalMesh* Mesh = IsValid(Component) ? Component->GetSkeletalMeshAsset() : nullptr;
		const FSkeletalMeshRenderData* RenderData = IsValid(Mesh)
			? Mesh->GetResourceForRendering()
			: nullptr;
		if (!RenderData)
		{
			OutError = TEXT("Skeletal Mesh render data is unavailable.");
			return false;
		}

		for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
		{
			const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
			if (!LODData.MultiSizeIndexContainer.IsIndexBufferValid())
			{
				OutError = FString::Printf(TEXT("LOD %d has no valid cooked render index buffer."), LODIndex);
				return false;
			}

			const FRawStaticIndexBuffer16or32Interface* IndexBuffer =
				LODData.MultiSizeIndexContainer.GetIndexBuffer();
			FLODTopologySnapshot Snapshot;
			Snapshot.LODIndex = LODIndex;
			Snapshot.RenderVertexCount = LODData.GetNumVertices();

			for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
			{
				bool bExcluded = Section.bDisabled
					|| !Component->IsMaterialSectionShown(Section.MaterialIndex, LODIndex);
				if (Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex))
				{
					const FName SlotName = Mesh->GetMaterials()[Section.MaterialIndex].MaterialSlotName;
					bExcluded |= ExplicitlyExcludedMaterialSlots.Contains(SlotName);
				}
				if (bExcluded)
				{
					++Snapshot.ExcludedOrHiddenSectionCount;
					continue;
				}

				const uint32 SectionIndexCount = Section.NumTriangles * 3u;
				if (Section.BaseIndex + SectionIndexCount > static_cast<uint32>(IndexBuffer->Num()))
				{
					OutError = FString::Printf(TEXT("LOD %d section index range is out of bounds."), LODIndex);
					return false;
				}
				Snapshot.TriangleIndices.Reserve(
					Snapshot.TriangleIndices.Num() + static_cast<int32>(SectionIndexCount));
				for (uint32 TriangleOffset = 0; TriangleOffset < SectionIndexCount; TriangleOffset += 3u)
				{
					FIntVector Triangle;
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						const uint32 VertexIndex = IndexBuffer->Get(
							Section.BaseIndex + TriangleOffset + static_cast<uint32>(Corner));
						if (VertexIndex >= static_cast<uint32>(Snapshot.RenderVertexCount))
						{
							OutError = FString::Printf(
								TEXT("LOD %d render index %u is outside its vertex buffer."),
								LODIndex,
								VertexIndex);
							return false;
						}
						Triangle[Corner] = static_cast<int32>(VertexIndex);
					}
					const FVector3d A(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Triangle.X));
					const FVector3d B(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Triangle.Y));
					const FVector3d C(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Triangle.Z));
					FVector3d Tangent = B - A;
					FVector3d Normal = Tangent.Cross(C - A);
					if (Triangle.X == Triangle.Y
						|| Triangle.Y == Triangle.Z
						|| Triangle.Z == Triangle.X
						|| !Tangent.Normalize()
						|| !Normal.Normalize())
					{
						++Snapshot.ExcludedRestDegenerateTriangleCount;
						continue;
					}
					Snapshot.TriangleIndices.Add(Triangle.X);
					Snapshot.TriangleIndices.Add(Triangle.Y);
					Snapshot.TriangleIndices.Add(Triangle.Z);
				}
			}

			if (Snapshot.TriangleIndices.IsEmpty())
			{
				OutError = FString::Printf(TEXT("LOD %d has no visible, non-excluded triangles."), LODIndex);
				return false;
			}
			OutTopologies.Add(LODIndex, MoveTemp(Snapshot));
		}
		return !OutTopologies.IsEmpty();
	}

	void SnapshotBindings(
		const UEFClothingSurfaceBinding* SurfaceBinding,
		const float RuntimeOffsetCm,
		TMap<uint64, FLODBindingSnapshot>& OutBindings)
	{
		for (const FEFClothingSurfaceLODPairBinding& Pair : SurfaceBinding->LODPairBindings)
		{
			FLODBindingSnapshot Snapshot;
			Snapshot.GarmentLODIndex = Pair.GarmentTopology.LODIndex;
			Snapshot.BodyLODIndex = Pair.BodyTopology.LODIndex;
			Snapshot.ExcludedDegenerateBodyTriangleCount =
				Pair.Metrics.ExcludedDegenerateBodyTriangleCount;
			Snapshot.VertexAnchors.Reserve(Pair.VertexBindings.Num());
			for (const FEFClothingSurfaceVertexBinding& Vertex : Pair.VertexBindings)
			{
				float TargetCm = FMath::Max(Vertex.TargetClearanceCm, 0.0f);
				if (Vertex.Mode == EEFClothingSurfaceVertexMode::SurfaceFollow)
				{
					TargetCm = FMath::Max(TargetCm, Vertex.RestSignedGapCm);
				}
				FVertexAnchorSnapshot& OutAnchor = Snapshot.VertexAnchors.AddDefaulted_GetRef();
				OutAnchor.BodyRenderVertexIndices = Vertex.BodyRenderVertexIndices;
				OutAnchor.BodyBarycentrics = Vertex.BodyBarycentrics;
				OutAnchor.RestTangentFrameOffsetCm = Vertex.RestTangentFrameOffsetCm;
				OutAnchor.TargetClearanceCm = FMath::Max(TargetCm + RuntimeOffsetCm, 0.0f);
				OutAnchor.FollowWeight = Vertex.FollowWeight;
				OutAnchor.MaximumCorrectionCm = Vertex.MaximumCorrectionCm;
				OutAnchor.Mode = Vertex.Mode;
			}

			Snapshot.Witnesses.Reserve(Pair.Witnesses.Num());
			for (const FEFClothingSurfaceWitness& Witness : Pair.Witnesses)
			{
				FWitnessSnapshot& OutWitness = Snapshot.Witnesses.AddDefaulted_GetRef();
				OutWitness.GarmentRenderVertexIndices = Witness.GarmentRenderVertexIndices;
				OutWitness.GarmentBarycentrics = Witness.GarmentBarycentrics;
				OutWitness.BodyRenderVertexIndices = Witness.BodyRenderVertexIndices;
				OutWitness.BodyBarycentrics = Witness.BodyBarycentrics;
				OutWitness.TargetClearanceCm = FMath::Max(
					Witness.TargetClearanceCm + RuntimeOffsetCm,
					0.0f);
			}
			OutBindings.Add(MakeLODKey(Snapshot.GarmentLODIndex, Snapshot.BodyLODIndex), MoveTemp(Snapshot));
		}
	}

	bool CopyReadback(
		const FMeshDeformerGeometryReadbackVertexDataArrays& Data,
		FVertexArraySnapshot& OutSnapshot)
	{
		if (Data.LODIndex == INDEX_NONE || Data.Positions.IsEmpty())
		{
			return false;
		}
		OutSnapshot.LODIndex = Data.LODIndex;
		OutSnapshot.Positions = Data.Positions;
		return true;
	}

	bool BuildDynamicMesh(
		const TArray<FVector3d>& Positions,
		const FLODTopologySnapshot& Topology,
		FDynamicMesh3& OutMesh,
		int32& OutInvalidVertexCount,
		int32& OutSkippedTriangleCount,
		FString& OutError)
	{
		if (Positions.Num() != Topology.RenderVertexCount)
		{
			OutError = FString::Printf(
				TEXT("Readback vertex count %d does not match cooked LOD count %d."),
				Positions.Num(),
				Topology.RenderVertexCount);
			return false;
		}

		for (const FVector3d& Position : Positions)
		{
			if (Position.ContainsNaN())
			{
				++OutInvalidVertexCount;
				continue;
			}
			OutMesh.AppendVertex(Position);
		}
		if (OutInvalidVertexCount != 0 || OutMesh.VertexCount() != Positions.Num())
		{
			OutError = TEXT("Readback contains NaN/Inf positions.");
			return false;
		}

		for (int32 Index = 0; Index + 2 < Topology.TriangleIndices.Num(); Index += 3)
		{
			const FIndex3i Triangle(
				Topology.TriangleIndices[Index],
				Topology.TriangleIndices[Index + 1],
				Topology.TriangleIndices[Index + 2]);
			if (Triangle.A == Triangle.B || Triangle.B == Triangle.C || Triangle.C == Triangle.A)
			{
				++OutSkippedTriangleCount;
				continue;
			}
			const FVector3d A = Positions[Triangle.A];
			const FVector3d B = Positions[Triangle.B];
			const FVector3d C = Positions[Triangle.C];
			// Match the exact numerical degeneracy guard used by both EF GPU
			// kernels. UE_DOUBLE_SMALL_NUMBER is intentionally much larger and
			// mislabeled valid sub-millimeter tessellation helpers as zero-area.
			if (FVector3d::CrossProduct(B - A, C - A).SquaredLength() <= 1.0e-12)
			{
				++OutSkippedTriangleCount;
				continue;
			}
			if (OutMesh.AppendTriangle(Triangle) < 0)
			{
				++OutSkippedTriangleCount;
			}
		}
		if (OutMesh.TriangleCount() == 0)
		{
			OutError = TEXT("No valid triangles remained after constructing QA geometry.");
			return false;
		}
		return true;
	}

	float Percentile99(TArray<float>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0f;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(
			FMath::CeilToInt(0.99 * static_cast<double>(Values.Num())) - 1,
			0,
			Values.Num() - 1);
		return Values[Index];
	}

	bool NormalizeBarycentrics(
		const FVector3f& Input,
		FVector3d& OutNormalized)
	{
		if (!IsFinite(Input))
		{
			return false;
		}
		const double Sum = static_cast<double>(Input.X)
			+ static_cast<double>(Input.Y)
			+ static_cast<double>(Input.Z);
		if (!FMath::IsFinite(Sum) || FMath::Abs(Sum) <= 1.0e-8)
		{
			return false;
		}
		OutNormalized = FVector3d(Input) / Sum;
		return IsFinite(OutNormalized);
	}

	bool ResolveBoundAnimatedFrame(
		const TArray<FVector3d>& BodyPositions,
		const FIntVector& TriangleIndices,
		const FVector3f& Barycentrics,
		FVector3d& OutAnchor,
		FVector3d& OutTangent,
		FVector3d& OutBitangent,
		FVector3d& OutNormal)
	{
		if (!BodyPositions.IsValidIndex(TriangleIndices.X)
			|| !BodyPositions.IsValidIndex(TriangleIndices.Y)
			|| !BodyPositions.IsValidIndex(TriangleIndices.Z))
		{
			return false;
		}
		FVector3d NormalizedBarycentrics;
		if (!NormalizeBarycentrics(Barycentrics, NormalizedBarycentrics))
		{
			return false;
		}
		const FVector3d& A = BodyPositions[TriangleIndices.X];
		const FVector3d& B = BodyPositions[TriangleIndices.Y];
		const FVector3d& C = BodyPositions[TriangleIndices.Z];
		const FVector3d Edge01 = B - A;
		const FVector3d Edge02 = C - A;
		OutNormal = Edge01.Cross(Edge02);
		const double NormalLengthSquared = OutNormal.SquaredLength();
		if (!IsFinite(OutNormal) || NormalLengthSquared <= 1.0e-12)
		{
			return false;
		}
		// Schema 4 matches the GPU exactly: the oriented final body triangle is
		// the normal source. This deliberately avoids Optimus' independent
		// TangentZ static fallback for secondary-component reads.
		OutNormal /= FMath::Sqrt(NormalLengthSquared);
		OutTangent = Edge01 - OutNormal * Edge01.Dot(OutNormal);
		double TangentLengthSquared = OutTangent.SquaredLength();
		if (!IsFinite(OutTangent) || TangentLengthSquared <= 1.0e-12)
		{
			OutTangent = Edge02 - OutNormal * Edge02.Dot(OutNormal);
			TangentLengthSquared = OutTangent.SquaredLength();
		}
		if (!IsFinite(OutTangent) || TangentLengthSquared <= 1.0e-12)
		{
			return false;
		}
		OutTangent /= FMath::Sqrt(TangentLengthSquared);
		OutBitangent = OutNormal.Cross(OutTangent);
		const double BitangentLengthSquared = OutBitangent.SquaredLength();
		if (!IsFinite(OutBitangent) || BitangentLengthSquared <= 1.0e-12)
		{
			return false;
		}
		OutBitangent /= FMath::Sqrt(BitangentLengthSquared);
		// Match the HLSL's final Gram-Schmidt step: T = normalize((N x T) x N).
		OutTangent = OutBitangent.Cross(OutNormal);
		const double FinalTangentLengthSquared = OutTangent.SquaredLength();
		if (!IsFinite(OutTangent) || FinalTangentLengthSquared <= 1.0e-12)
		{
			return false;
		}
		OutTangent /= FMath::Sqrt(FinalTangentLengthSquared);
		OutAnchor = A * NormalizedBarycentrics.X
			+ B * NormalizedBarycentrics.Y
			+ C * NormalizedBarycentrics.Z;
		return IsFinite(OutAnchor)
			&& IsFinite(OutTangent)
			&& IsFinite(OutBitangent)
			&& IsFinite(OutNormal);
	}

	bool ResolveBoundAnimatedSurface(
		const TArray<FVector3d>& BodyPositions,
		const FIntVector& TriangleIndices,
		const FVector3f& Barycentrics,
		FVector3d& OutAnchor,
		FVector3d& OutNormal)
	{
		FVector3d IgnoredTangent;
		FVector3d IgnoredBitangent;
		return ResolveBoundAnimatedFrame(
			BodyPositions,
			TriangleIndices,
			Barycentrics,
			OutAnchor,
			IgnoredTangent,
			IgnoredBitangent,
			OutNormal);
	}

	bool ReconstructBaseCorrectedPosition(
		const TArray<FVector3d>& BodyPositions,
		const FVertexAnchorSnapshot& Binding,
		const FVector3d& GarmentPosition,
		FVector3d& OutCorrectedPosition)
	{
		OutCorrectedPosition = GarmentPosition;
		if (!IsFinite(GarmentPosition))
		{
			return false;
		}
		// PreserveUpstream is outside the surface-constraint domain. Its first-pass
		// output is exactly the normal DAZ/skinning/Chaos garment position.
		if (Binding.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream)
		{
			return true;
		}
		if (!IsFinite(Binding.RestTangentFrameOffsetCm)
			|| !FMath::IsFinite(Binding.TargetClearanceCm)
			|| !FMath::IsFinite(Binding.FollowWeight)
			|| !FMath::IsFinite(Binding.MaximumCorrectionCm))
		{
			return false;
		}

		FVector3d SurfaceAnchor;
		FVector3d SurfaceTangent;
		FVector3d SurfaceBitangent;
		FVector3d SurfaceNormal;
		if (!ResolveBoundAnimatedFrame(
			BodyPositions,
			Binding.BodyRenderVertexIndices,
			Binding.BodyBarycentrics,
			SurfaceAnchor,
			SurfaceTangent,
			SurfaceBitangent,
			SurfaceNormal))
		{
			// This is the base kernel's conservative position fallback.
			return true;
		}

		const FVector3d RestOffset(Binding.RestTangentFrameOffsetCm);
		const FVector3d SurfaceTarget = SurfaceAnchor
			+ SurfaceTangent * RestOffset.X
			+ SurfaceBitangent * RestOffset.Y
			+ SurfaceNormal * RestOffset.Z;
		const double FollowWeight = Binding.Mode == EEFClothingSurfaceVertexMode::CollisionOnly
			? 0.0
			: FMath::Clamp(static_cast<double>(Binding.FollowWeight), 0.0, 1.0);
		FVector3d FollowDelta = (SurfaceTarget - GarmentPosition) * FollowWeight;
		FollowDelta -= SurfaceNormal * FMath::Min(FollowDelta.Dot(SurfaceNormal), 0.0);
		const FVector3d CandidatePosition = GarmentPosition + FollowDelta;

		// The runtime override only selects the diagnostic threshold; the visual push
		// is intentionally unclamped. A valid compiled binding always has a positive
		// MaximumCorrectionCm, matching the kernel's normal path.
		if (Binding.MaximumCorrectionCm <= 0.0f)
		{
			return true;
		}
		const double TargetGapCm = FMath::Max(
			static_cast<double>(Binding.TargetClearanceCm),
			0.0);
		const double SignedGapCm = (CandidatePosition - SurfaceAnchor).Dot(SurfaceNormal);
		const double RequiredPushCm = FMath::Max(TargetGapCm - SignedGapCm, 0.0);
		const FVector3d CorrectedPosition = CandidatePosition + SurfaceNormal * RequiredPushCm;
		if (IsFinite(CorrectedPosition))
		{
			OutCorrectedPosition = CorrectedPosition;
		}
		return true;
	}

	bool FindBoundSignedGap(
		const TArray<FVector3d>& BodyPositions,
		const FIntVector& TriangleIndices,
		const FVector3f& Barycentrics,
		const FVector3d& QueryPoint,
		float& OutSignedGapCm)
	{
		FVector3d Anchor;
		FVector3d Normal;
		if (!ResolveBoundAnimatedSurface(
			BodyPositions,
			TriangleIndices,
			Barycentrics,
			Anchor,
			Normal))
		{
			return false;
		}
		OutSignedGapCm = static_cast<float>((QueryPoint - Anchor).Dot(Normal));
		return FMath::IsFinite(OutSignedGapCm);
	}

	void Analyze(const TSharedPtr<FReadbackState, ESPMode::ThreadSafe>& State)
	{
		const double AnalysisStart = FPlatformTime::Seconds();
		FEFClothingSurfaceReadbackQAResult Result;
		TMap<int32, FLODTopologySnapshot> GarmentTopologies;
		TMap<int32, FLODTopologySnapshot> BodyTopologies;
		TMap<uint64, FLODBindingSnapshot> Bindings;
		FTransform BodyToGarment;
		FVertexArraySnapshot BodyFinal;
		FVertexArraySnapshot GarmentBase;
		FVertexArraySnapshot GarmentFinal;
		{
			FScopeLock Lock(&State->Mutex);
			Result = State->Result;
			GarmentTopologies = State->GarmentTopologies;
			BodyTopologies = State->BodyTopologies;
			Bindings = State->LODBindings;
			BodyToGarment = State->BodyToGarment;
			BodyFinal = State->BodyFinal;
			GarmentBase = State->GarmentBase;
			GarmentFinal = State->GarmentFinal;
		}

		auto FailAnalysis = [&State](const FString& Error)
		{
			SetFailed(State, Error);
		};

		if (GarmentBase.LODIndex != GarmentFinal.LODIndex)
		{
			FailAnalysis(TEXT("Pre-EF and post-EF garment readbacks came from different LODs."));
			return;
		}
		const FLODTopologySnapshot* GarmentTopology = GarmentTopologies.Find(GarmentFinal.LODIndex);
		const FLODTopologySnapshot* BodyTopology = BodyTopologies.Find(BodyFinal.LODIndex);
		const FLODBindingSnapshot* Binding = Bindings.Find(
			MakeLODKey(GarmentFinal.LODIndex, BodyFinal.LODIndex));
		if (!GarmentTopology || !BodyTopology || !Binding)
		{
			FailAnalysis(TEXT("Returned GPU LOD pair has no exact cooked topology/binding snapshot."));
			return;
		}
		if (Binding->VertexAnchors.Num() != GarmentFinal.Positions.Num())
		{
			FailAnalysis(TEXT("V26 per-render-vertex clearance array does not match the final GPU buffer."));
			return;
		}
		if (BodyTopology->ExcludedRestDegenerateTriangleCount
			!= Binding->ExcludedDegenerateBodyTriangleCount)
		{
			FailAnalysis(TEXT("Live body rest-degenerate triangle exclusions differ from the V26 binding."));
			return;
		}
		Result.ExcludedDegenerateBodyTriangleCount =
			BodyTopology->ExcludedRestDegenerateTriangleCount;
		if (GarmentBase.Positions.Num() != GarmentFinal.Positions.Num())
		{
			FailAnalysis(TEXT("Pre-EF and post-EF garment vertex counts differ."));
			return;
		}
		TArray<FVector3d> BodyPositions;
		BodyPositions.Reserve(BodyFinal.Positions.Num());
		for (int32 VertexIndex = 0; VertexIndex < BodyFinal.Positions.Num(); ++VertexIndex)
		{
			const FVector3f& Position = BodyFinal.Positions[VertexIndex];
			const FVector3d TransformedPosition(
				BodyToGarment.TransformPosition(FVector(Position)));
			if (!IsFinite(Position)
				|| !IsFinite(TransformedPosition))
			{
				++Result.InvalidOrNonFiniteVertexCount;
				BodyPositions.Add(FVector3d::Zero());
			}
			else
			{
				// Graph/schema 4 derives every contact normal from these exact final
				// animated positions after transforming into garment-local space.
				BodyPositions.Add(TransformedPosition);
			}
		}

		TArray<FVector3d> GarmentPositions;
		GarmentPositions.Reserve(GarmentFinal.Positions.Num());
		for (const FVector3f& Position : GarmentFinal.Positions)
		{
			if (!IsFinite(Position))
			{
				++Result.InvalidOrNonFiniteVertexCount;
				GarmentPositions.Add(FVector3d::Zero());
			}
			else
			{
				GarmentPositions.Add(FVector3d(Position));
			}
		}
		if (Result.InvalidOrNonFiniteVertexCount != 0)
		{
			FailAnalysis(TEXT("One or more final GPU positions are non-finite."));
			return;
		}

		TArray<FVector3d> BaseCorrectedPositions;
		BaseCorrectedPositions.Reserve(GarmentBase.Positions.Num());
		for (int32 VertexIndex = 0; VertexIndex < GarmentBase.Positions.Num(); ++VertexIndex)
		{
			const FVector3f& PreEFPosition = GarmentBase.Positions[VertexIndex];
			if (!IsFinite(PreEFPosition))
			{
				FailAnalysis(TEXT("Pre-EF garment readback contains NaN/Inf positions."));
				return;
			}
			FVector3d BaseCorrectedPosition;
			if (!ReconstructBaseCorrectedPosition(
				BodyPositions,
				Binding->VertexAnchors[VertexIndex],
				FVector3d(PreEFPosition),
				BaseCorrectedPosition)
				|| !IsFinite(BaseCorrectedPosition))
			{
				FailAnalysis(TEXT("Failed to reconstruct the GPU BaseCorrectedPosition resource."));
				return;
			}
			BaseCorrectedPositions.Add(BaseCorrectedPosition);
		}

		FDynamicMesh3 BodyMesh;
		FDynamicMesh3 GarmentMesh;
		FString MeshError;
		if (!BuildDynamicMesh(
				BodyPositions,
				*BodyTopology,
				BodyMesh,
				Result.InvalidOrNonFiniteVertexCount,
				Result.SkippedDegenerateTriangleCount,
				MeshError)
			|| !BuildDynamicMesh(
				GarmentPositions,
				*GarmentTopology,
				GarmentMesh,
				Result.InvalidOrNonFiniteVertexCount,
				Result.SkippedDegenerateTriangleCount,
				MeshError))
		{
			FailAnalysis(MeshError);
			return;
		}

		// The optional-anatomy PreserveUpstream domain intentionally keeps the
		// upstream garment output and is therefore not part of the certified
		// skin/garment intersection surface. Remove every touching face before the
		// exact GeometryCore query; vertex IDs still match cooked render indices.
		TArray<int32> PreserveUpstreamTriangles;
		for (const int32 TriangleID : GarmentMesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = GarmentMesh.GetTriangle(TriangleID);
			if (Binding->VertexAnchors[Triangle.A].Mode == EEFClothingSurfaceVertexMode::PreserveUpstream
				|| Binding->VertexAnchors[Triangle.B].Mode == EEFClothingSurfaceVertexMode::PreserveUpstream
				|| Binding->VertexAnchors[Triangle.C].Mode == EEFClothingSurfaceVertexMode::PreserveUpstream)
			{
				PreserveUpstreamTriangles.Add(TriangleID);
			}
		}
		for (const int32 TriangleID : PreserveUpstreamTriangles)
		{
			if (GarmentMesh.RemoveTriangle(TriangleID, false, false) != EMeshResult::Ok)
			{
				FailAnalysis(TEXT("Failed to remove a PreserveUpstream garment triangle from certification."));
				return;
			}
		}
		Result.ExcludedPreserveUpstreamGarmentTriangleCount = PreserveUpstreamTriangles.Num();
		if (GarmentMesh.TriangleCount() == 0)
		{
			FailAnalysis(TEXT("PreserveUpstream excluded the complete garment topology; no certified surface remains."));
			return;
		}

		FDynamicMeshAABBTree3 BodyTree(&BodyMesh, true);
		FDynamicMeshAABBTree3 GarmentTree(&GarmentMesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			BodyTree.FindAllIntersections(GarmentTree);
		Result.TriangleIntersectionCount = Intersections.Points.Num()
			+ Intersections.Segments.Num()
			+ Intersections.Polygons.Num();
		int32 LoggedIntersectionCount = 0;
		auto LogIntersection = [
			&BodyMesh,
			&GarmentMesh,
			&LoggedIntersectionCount](
				const TCHAR* Kind,
				int32 BodyTriangleID,
				int32 GarmentTriangleID,
				const FVector3d& Center)
		{
			if (LoggedIntersectionCount >= 64
				|| !BodyMesh.IsTriangle(BodyTriangleID)
				|| !GarmentMesh.IsTriangle(GarmentTriangleID))
			{
				return;
			}
			const FIndex3i BodyVertices = BodyMesh.GetTriangle(BodyTriangleID);
			const FIndex3i GarmentVertices = GarmentMesh.GetTriangle(GarmentTriangleID);
			const FIndex3i GarmentEdges = GarmentMesh.GetTriEdges(GarmentTriangleID);
			const int32 BoundaryEdgeCount =
				(GarmentMesh.IsBoundaryEdge(GarmentEdges.A) ? 1 : 0)
				+ (GarmentMesh.IsBoundaryEdge(GarmentEdges.B) ? 1 : 0)
				+ (GarmentMesh.IsBoundaryEdge(GarmentEdges.C) ? 1 : 0);
			UE_LOG(
				LogEFClothingSurfaceReadbackQA,
				Warning,
				TEXT("TriangleIntersection kind=%s center=(%.6f,%.6f,%.6f) bodyTri=%d body=(%d,%d,%d) garmentTri=%d garment=(%d,%d,%d) boundaryEdges=%d"),
				Kind,
				Center.X,
				Center.Y,
				Center.Z,
				BodyTriangleID,
				BodyVertices.A,
				BodyVertices.B,
				BodyVertices.C,
				GarmentTriangleID,
				GarmentVertices.A,
				GarmentVertices.B,
				GarmentVertices.C,
				BoundaryEdgeCount);
			++LoggedIntersectionCount;
		};
		for (const MeshIntersection::FPointIntersection& Intersection : Intersections.Points)
		{
			LogIntersection(
				TEXT("Point"),
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				Intersection.Point);
		}
		for (const MeshIntersection::FSegmentIntersection& Intersection : Intersections.Segments)
		{
			LogIntersection(
				TEXT("Segment"),
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				(Intersection.Point[0] + Intersection.Point[1]) * 0.5);
		}
		for (const MeshIntersection::FPolygonIntersection& Intersection : Intersections.Polygons)
		{
			FVector3d Center = FVector3d::Zero();
			for (int32 PointIndex = 0; PointIndex < Intersection.Quantity; ++PointIndex)
			{
				Center += Intersection.Point[PointIndex];
			}
			if (Intersection.Quantity > 0)
			{
				Center /= static_cast<double>(Intersection.Quantity);
			}
			LogIntersection(
				TEXT("Polygon"),
				Intersection.TriangleID[0],
				Intersection.TriangleID[1],
				Center);
		}

		Result.MinimumVertexSkinGapCm = TNumericLimits<float>::Max();
		Result.MinimumBaseCorrectedVertexSkinGapCm = TNumericLimits<float>::Max();
		Result.MinimumBaseCorrectedClearanceResidualCm = TNumericLimits<float>::Max();
		Result.MinimumClearanceResidualCm = TNumericLimits<float>::Max();
		for (int32 VertexIndex = 0; VertexIndex < GarmentPositions.Num(); ++VertexIndex)
		{
			const FVertexAnchorSnapshot& VertexAnchor = Binding->VertexAnchors[VertexIndex];
			if (VertexAnchor.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream)
			{
				continue;
			}
			float BaseCorrectedGapCm = 0.0f;
			if (!FindBoundSignedGap(
				BodyPositions,
				VertexAnchor.BodyRenderVertexIndices,
				VertexAnchor.BodyBarycentrics,
				BaseCorrectedPositions[VertexIndex],
				BaseCorrectedGapCm))
			{
				FailAnalysis(TEXT("Failed to evaluate a reconstructed base-pass garment vertex."));
				return;
			}
			Result.MinimumBaseCorrectedVertexSkinGapCm = FMath::Min(
				Result.MinimumBaseCorrectedVertexSkinGapCm,
				BaseCorrectedGapCm);
			Result.BaseCorrectedVertexSkinGapViolationCount +=
				BaseCorrectedGapCm < VertexGapToleranceCm ? 1 : 0;
			const float BaseCorrectedResidualCm =
				BaseCorrectedGapCm - VertexAnchor.TargetClearanceCm;
			Result.MinimumBaseCorrectedClearanceResidualCm = FMath::Min(
				Result.MinimumBaseCorrectedClearanceResidualCm,
				BaseCorrectedResidualCm);
			Result.BaseCorrectedClearanceResidualViolationCount +=
				BaseCorrectedResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
			float GapCm = 0.0f;
			if (!FindBoundSignedGap(
				BodyPositions,
				VertexAnchor.BodyRenderVertexIndices,
				VertexAnchor.BodyBarycentrics,
				GarmentPositions[VertexIndex],
				GapCm))
			{
				FailAnalysis(TEXT("Failed to evaluate a certified body anchor for a garment vertex."));
				return;
			}
			Result.MinimumVertexSkinGapCm = FMath::Min(Result.MinimumVertexSkinGapCm, GapCm);
			Result.VertexSkinGapViolationCount += GapCm < VertexGapToleranceCm ? 1 : 0;
			const float ResidualCm = GapCm - VertexAnchor.TargetClearanceCm;
			Result.MinimumClearanceResidualCm = FMath::Min(Result.MinimumClearanceResidualCm, ResidualCm);
			Result.ClearanceResidualViolationCount +=
				ResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
		}

		Result.TriangleSampleCount = Binding->Witnesses.Num();
		Result.bTriangleSampleCoverageAvailable = !Binding->Witnesses.IsEmpty();
		Result.MinimumTriangleSampleSkinGapCm = Binding->Witnesses.IsEmpty()
			? 0.0f
			: TNumericLimits<float>::Max();
		Result.MinimumBaseCorrectedTriangleSampleSkinGapCm = Binding->Witnesses.IsEmpty()
			? 0.0f
			: TNumericLimits<float>::Max();
		int32 LoggedViolatingWitnessCount = 0;
		for (int32 WitnessIndex = 0; WitnessIndex < Binding->Witnesses.Num(); ++WitnessIndex)
		{
			const FWitnessSnapshot& Witness = Binding->Witnesses[WitnessIndex];
			const FIntVector& Indices = Witness.GarmentRenderVertexIndices;
			if (!GarmentPositions.IsValidIndex(Indices.X)
				|| !GarmentPositions.IsValidIndex(Indices.Y)
				|| !GarmentPositions.IsValidIndex(Indices.Z))
			{
				FailAnalysis(TEXT("A compiled V26 witness references an invalid final render vertex."));
				return;
			}
			FVector3d GarmentBarycentrics;
			if (!NormalizeBarycentrics(Witness.GarmentBarycentrics, GarmentBarycentrics))
			{
				FailAnalysis(TEXT("A compiled V26 witness has invalid garment barycentrics."));
				return;
			}
			const FVector3d Sample =
				GarmentPositions[Indices.X] * GarmentBarycentrics.X
				+ GarmentPositions[Indices.Y] * GarmentBarycentrics.Y
				+ GarmentPositions[Indices.Z] * GarmentBarycentrics.Z;
			const FVector3d BaseCorrectedSample =
				BaseCorrectedPositions[Indices.X] * GarmentBarycentrics.X
				+ BaseCorrectedPositions[Indices.Y] * GarmentBarycentrics.Y
				+ BaseCorrectedPositions[Indices.Z] * GarmentBarycentrics.Z;
			float BaseCorrectedGapCm = 0.0f;
			if (!FindBoundSignedGap(
				BodyPositions,
				Witness.BodyRenderVertexIndices,
				Witness.BodyBarycentrics,
				BaseCorrectedSample,
				BaseCorrectedGapCm))
			{
				FailAnalysis(TEXT("Failed to evaluate a reconstructed base-pass V26 witness."));
				return;
			}
			Result.MinimumBaseCorrectedTriangleSampleSkinGapCm = FMath::Min(
				Result.MinimumBaseCorrectedTriangleSampleSkinGapCm,
				BaseCorrectedGapCm);
			Result.BaseCorrectedTriangleSampleSkinGapViolationCount +=
				BaseCorrectedGapCm < VertexGapToleranceCm ? 1 : 0;
			const float BaseCorrectedResidualCm =
				BaseCorrectedGapCm - Witness.TargetClearanceCm;
			Result.MinimumBaseCorrectedClearanceResidualCm = FMath::Min(
				Result.MinimumBaseCorrectedClearanceResidualCm,
				BaseCorrectedResidualCm);
			Result.BaseCorrectedClearanceResidualViolationCount +=
				BaseCorrectedResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
			float GapCm = 0.0f;
			if (!FindBoundSignedGap(
				BodyPositions,
				Witness.BodyRenderVertexIndices,
				Witness.BodyBarycentrics,
				Sample,
				GapCm))
			{
				FailAnalysis(TEXT("Failed to evaluate a certified body anchor for a V26 witness."));
				return;
			}
			Result.MinimumTriangleSampleSkinGapCm = FMath::Min(
				Result.MinimumTriangleSampleSkinGapCm,
				GapCm);
			Result.TriangleSampleSkinGapViolationCount += GapCm < VertexGapToleranceCm ? 1 : 0;
			if (GapCm < VertexGapToleranceCm && LoggedViolatingWitnessCount < 64)
			{
				auto ResolveAnimatedNormal = [&BodyPositions](
					const FIntVector& Triangle,
					const FVector3f& Barycentrics,
					FVector3d& OutNormal) -> bool
				{
					FVector3d IgnoredAnchor;
					return ResolveBoundAnimatedSurface(
						BodyPositions,
						Triangle,
						Barycentrics,
						IgnoredAnchor,
						OutNormal);
				};
				FVector3d WitnessNormal = FVector3d::ZeroVector;
				FVector3d PrimaryNormals[3] = {};
				const int32 GarmentCornerIndices[3] = { Indices.X, Indices.Y, Indices.Z };
				double PrimaryWitnessDots[3] = { -2.0, -2.0, -2.0 };
				double WitnessPassPrimaryDisplacements[3] = {};
				double WitnessPassNormalDisplacements[3] = {};
				FVector3d WitnessPassDisplacements[3] = {};
				double ExpectedPrimaryDisplacements[3] = {};
				float PreEFGapCm = TNumericLimits<float>::Lowest();
				if (ResolveAnimatedNormal(
					Witness.BodyRenderVertexIndices,
					Witness.BodyBarycentrics,
					WitnessNormal))
				{
					const FVector3d PreEFSample =
						FVector3d(GarmentBase.Positions[Indices.X]) * GarmentBarycentrics.X
						+ FVector3d(GarmentBase.Positions[Indices.Y]) * GarmentBarycentrics.Y
						+ FVector3d(GarmentBase.Positions[Indices.Z]) * GarmentBarycentrics.Z;
					FindBoundSignedGap(
						BodyPositions,
						Witness.BodyRenderVertexIndices,
						Witness.BodyBarycentrics,
						PreEFSample,
						PreEFGapCm);
					double CoefficientSquaredSum = 0.0;
					for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
					{
						const int32 GarmentCorner = GarmentCornerIndices[CornerIndex];
						if (Binding->VertexAnchors.IsValidIndex(GarmentCorner)
							&& ResolveAnimatedNormal(
								Binding->VertexAnchors[GarmentCorner].BodyRenderVertexIndices,
								Binding->VertexAnchors[GarmentCorner].BodyBarycentrics,
								PrimaryNormals[CornerIndex]))
						{
							PrimaryWitnessDots[CornerIndex] = PrimaryNormals[CornerIndex].Dot(WitnessNormal);
							const FVector3d WitnessPassDisplacement =
								GarmentPositions[GarmentCorner]
								- BaseCorrectedPositions[GarmentCorner];
							WitnessPassDisplacements[CornerIndex] = WitnessPassDisplacement;
							WitnessPassPrimaryDisplacements[CornerIndex] =
								WitnessPassDisplacement.Dot(PrimaryNormals[CornerIndex]);
							WitnessPassNormalDisplacements[CornerIndex] =
								WitnessPassDisplacement.Dot(WitnessNormal);
							const double Coefficient = GarmentBarycentrics[CornerIndex]
								* FMath::Max(PrimaryWitnessDots[CornerIndex], 0.0);
							CoefficientSquaredSum += Coefficient * Coefficient;
						}
					}
					const double RequiredPushCm = FMath::Max(
						static_cast<double>(Witness.TargetClearanceCm - BaseCorrectedGapCm),
						0.0);
					if (CoefficientSquaredSum > 1.e-10)
					{
						for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
						{
							const double Coefficient = GarmentBarycentrics[CornerIndex]
								* FMath::Max(PrimaryWitnessDots[CornerIndex], 0.0);
							ExpectedPrimaryDisplacements[CornerIndex] =
								RequiredPushCm * Coefficient / CoefficientSquaredSum;
						}
					}
				}
				UE_LOG(
					LogEFClothingSurfaceReadbackQA,
					Warning,
					TEXT("WitnessViolation index=%d preEFGap=%.6f baseCorrectedGap=%.6f finalGap=%.6f target=%.6f garment=(%d,%d,%d) bary=(%.6f,%.6f,%.6f) body=(%d,%d,%d) primary_dot=(%.6f,%.6f,%.6f) expected_primary=(%.6f,%.6f,%.6f) witness_pass_primary=(%.6f,%.6f,%.6f) witness_pass_normal=(%.6f,%.6f,%.6f) witness_pass_xyz0=(%.6f,%.6f,%.6f) witness_pass_xyz1=(%.6f,%.6f,%.6f) witness_pass_xyz2=(%.6f,%.6f,%.6f)"),
					WitnessIndex,
					PreEFGapCm,
					BaseCorrectedGapCm,
					GapCm,
					Witness.TargetClearanceCm,
					Indices.X,
					Indices.Y,
					Indices.Z,
					Witness.GarmentBarycentrics.X,
					Witness.GarmentBarycentrics.Y,
					Witness.GarmentBarycentrics.Z,
					Witness.BodyRenderVertexIndices.X,
					Witness.BodyRenderVertexIndices.Y,
					Witness.BodyRenderVertexIndices.Z,
					PrimaryWitnessDots[0],
					PrimaryWitnessDots[1],
					PrimaryWitnessDots[2],
					ExpectedPrimaryDisplacements[0],
					ExpectedPrimaryDisplacements[1],
					ExpectedPrimaryDisplacements[2],
					WitnessPassPrimaryDisplacements[0],
					WitnessPassPrimaryDisplacements[1],
					WitnessPassPrimaryDisplacements[2],
					WitnessPassNormalDisplacements[0],
					WitnessPassNormalDisplacements[1],
					WitnessPassNormalDisplacements[2],
					WitnessPassDisplacements[0].X,
					WitnessPassDisplacements[0].Y,
					WitnessPassDisplacements[0].Z,
					WitnessPassDisplacements[1].X,
					WitnessPassDisplacements[1].Y,
					WitnessPassDisplacements[1].Z,
					WitnessPassDisplacements[2].X,
					WitnessPassDisplacements[2].Y,
					WitnessPassDisplacements[2].Z);
				++LoggedViolatingWitnessCount;
			}
			const float ResidualCm = GapCm - Witness.TargetClearanceCm;
			Result.MinimumClearanceResidualCm = FMath::Min(Result.MinimumClearanceResidualCm, ResidualCm);
			Result.ClearanceResidualViolationCount +=
				ResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
		}

		TArray<float> CorrectionMagnitudes;
		CorrectionMagnitudes.Reserve(GarmentFinal.Positions.Num());
		TArray<float> WitnessPassDisplacements;
		WitnessPassDisplacements.Reserve(GarmentFinal.Positions.Num());
		for (int32 VertexIndex = 0; VertexIndex < GarmentFinal.Positions.Num(); ++VertexIndex)
		{
			const float MagnitudeCm = FVector3f::Distance(
				GarmentFinal.Positions[VertexIndex],
				GarmentBase.Positions[VertexIndex]);
			CorrectionMagnitudes.Add(MagnitudeCm);
			Result.MaximumCorrectionMagnitudeCm = FMath::Max(
				Result.MaximumCorrectionMagnitudeCm,
				MagnitudeCm);
			const float WitnessPassDisplacementCm = static_cast<float>(
				(FVector3d(GarmentFinal.Positions[VertexIndex])
					- BaseCorrectedPositions[VertexIndex]).Length());
			WitnessPassDisplacements.Add(WitnessPassDisplacementCm);
			Result.MaximumWitnessPassDisplacementCm = FMath::Max(
				Result.MaximumWitnessPassDisplacementCm,
				WitnessPassDisplacementCm);
			// One micrometer expressed in Unreal centimeters.
			Result.WitnessPassMovedVertexCount += WitnessPassDisplacementCm > 1.0e-4f ? 1 : 0;
		}
		Result.CorrectionMagnitudeP99Cm = Percentile99(CorrectionMagnitudes);
		Result.WitnessPassDisplacementP99Cm = Percentile99(WitnessPassDisplacements);

		Result.GarmentLODIndex = GarmentFinal.LODIndex;
		Result.BodyLODIndex = BodyFinal.LODIndex;
		Result.GarmentRenderVertexCount = GarmentFinal.Positions.Num();
		Result.BodyRenderVertexCount = BodyFinal.Positions.Num();
		Result.TestedGarmentTriangleCount = GarmentMesh.TriangleCount();
		Result.TestedBodyTriangleCount = BodyMesh.TriangleCount();
		Result.ExcludedOrHiddenBodySectionCount = BodyTopology->ExcludedOrHiddenSectionCount;
		Result.bLODPairMatched = true;
		Result.AnalysisTimeMs = static_cast<float>(
			(FPlatformTime::Seconds() - AnalysisStart) * 1000.0);
		Result.TotalLatencyMs = static_cast<float>(
			(FPlatformTime::Seconds() - State->StartSeconds) * 1000.0);
		Result.State = EEFClothingSurfaceReadbackQAState::Ready;
		Result.Error.Reset();

		{
			FScopeLock Lock(&State->Mutex);
			if (State->Result.State != EEFClothingSurfaceReadbackQAState::Failed)
			{
				State->Result = MoveTemp(Result);
			}
		}
	}

	void TryStartAnalysis(const TSharedPtr<FReadbackState, ESPMode::ThreadSafe>& State)
	{
		bool bShouldStart = false;
		{
			FScopeLock Lock(&State->Mutex);
			if (State->Result.State == EEFClothingSurfaceReadbackQAState::PendingGPU
				&& State->bBodyReceived
				&& State->bGarmentBaseReceived
				&& State->bGarmentFinalReceived
				&& !State->bAnalysisStarted)
			{
				State->bAnalysisStarted = true;
				State->Result.State = EEFClothingSurfaceReadbackQAState::Analyzing;
				bShouldStart = true;
			}
		}
		if (bShouldStart)
		{
			Async(EAsyncExecution::ThreadPool, [State]() { Analyze(State); });
		}
	}

	void ReceiveSingleReadback(
		const TSharedPtr<FReadbackState, ESPMode::ThreadSafe>& State,
		const FMeshDeformerGeometryReadbackVertexDataArrays& Data,
		const bool bBody)
	{
		bool bValid = false;
		{
			FScopeLock Lock(&State->Mutex);
			FVertexArraySnapshot& Target = bBody ? State->BodyFinal : State->GarmentBase;
			bValid = CopyReadback(Data, Target);
			if (bValid)
			{
				(bBody ? State->bBodyReceived : State->bGarmentBaseReceived) = true;
			}
		}
		if (!bValid)
		{
			SetFailed(State, bBody
				? TEXT("Body Optimus readback returned no final render positions.")
				: TEXT("Pre-EF garment Optimus readback returned no render positions."));
			return;
		}
		TryStartAnalysis(State);
	}

	void ReceiveSurfaceReadback(
		const TSharedPtr<FReadbackState, ESPMode::ThreadSafe>& State,
		const FString& InstanceName,
		const FMeshDeformerGeometryReadbackVertexDataArrays& Data)
	{
		bool bAccepted = false;
		bool bAllCandidatesFailed = false;
		{
			FScopeLock Lock(&State->Mutex);
			if (!State->bGarmentFinalReceived
				&& CopyReadback(Data, State->GarmentFinal))
			{
				State->bGarmentFinalReceived = true;
				State->Result.SurfaceInstanceName = InstanceName;
				bAccepted = true;
			}
			State->PendingSurfaceCandidateCallbacks = FMath::Max(
				State->PendingSurfaceCandidateCallbacks - 1,
				0);
			bAllCandidatesFailed = !State->bGarmentFinalReceived
				&& State->PendingSurfaceCandidateCallbacks == 0;
		}
		if (bAllCandidatesFailed)
		{
			SetFailed(State, TEXT("No active EF surface Optimus instance produced final render positions."));
			return;
		}
		if (bAccepted)
		{
			TryStartAnalysis(State);
		}
	}
}

bool UEFClothingSurfaceReadbackQALibrary::BeginFinalGeometryReadback(
	USkeletalMeshComponent* GarmentComponent,
	USkeletalMeshComponent* BodyComponent,
	UObject* ExpectedSurfaceDeformer,
	UEFClothingSurfaceBinding* SurfaceBinding,
	const float GlobalClearanceOffsetCm,
	const float GarmentClearanceOffsetCm,
	FString& OutRequestId,
	FString& OutError)
{
	using namespace EFClothingSurfaceReadbackQA;
	OutRequestId.Reset();
	OutError.Reset();

#if !WITH_EDITORONLY_DATA
	OutError = TEXT("Final geometry readback is compiled only with editor-only data.");
	return false;
#else
	if (!IsInGameThread())
	{
		OutError = TEXT("BeginFinalGeometryReadback must run on the game thread.");
		return false;
	}
	UOptimusDeformer* SurfaceDeformer = Cast<UOptimusDeformer>(ExpectedSurfaceDeformer);
	if (!IsValid(GarmentComponent)
		|| !IsValid(BodyComponent)
		|| !IsValid(SurfaceDeformer)
		|| !IsValid(SurfaceBinding))
	{
		OutError = TEXT("Garment, body, surface deformer and V26 binding are all required.");
		return false;
	}
	if (!GarmentComponent->IsRegistered()
		|| !BodyComponent->IsRegistered()
		|| GarmentComponent->GetScene() == nullptr
		|| GarmentComponent->GetScene() != BodyComponent->GetScene())
	{
		OutError = TEXT("Garment and body must be registered in the same live PIE scene.");
		return false;
	}
	USkeletalMesh* GarmentMesh = GarmentComponent->GetSkeletalMeshAsset();
	USkeletalMesh* BodyMesh = BodyComponent->GetSkeletalMeshAsset();
	if (!IsValid(GarmentMesh) || !IsValid(BodyMesh))
	{
		OutError = TEXT("Garment or body has no Skeletal Mesh asset.");
		return false;
	}
	if (SurfaceBinding->CompilerVersion != EFClothingMorphV26::CompilerVersion
		|| SurfaceBinding->SchemaVersion != EFClothingMorphV26::SurfaceBindingSchemaVersion
		|| SurfaceBinding->FittedGarment.ToSoftObjectPath() != FSoftObjectPath(GarmentMesh)
		|| SurfaceBinding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh))
	{
		OutError = TEXT("Binding identity/version does not exactly match the live garment/body assets.");
		return false;
	}
	if (!GarmentComponent->HasMeshDeformer() || !BodyComponent->HasMeshDeformer())
	{
		OutError = TEXT("Both components require active Mesh Deformers for UE 5.8 readback.");
		return false;
	}

	TArray<UOptimusDeformerInstance*> SurfaceCandidates;
	const FString ExpectedInstancePrefix = SurfaceDeformer->GetName() + TEXT("_Instance");
	ForEachObjectWithOuter(
		GarmentComponent,
		[&SurfaceCandidates, &ExpectedInstancePrefix, GarmentComponent](UObject* Object)
		{
			UOptimusDeformerInstance* Instance = Cast<UOptimusDeformerInstance>(Object);
			if (IsValid(Instance)
				&& Instance->GetMeshComponent() == GarmentComponent
				&& Instance->GetName().StartsWith(ExpectedInstancePrefix)
				&& !Instance->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
			{
				SurfaceCandidates.Add(Instance);
			}
		},
		EGetObjectsFlags::None);
	if (SurfaceCandidates.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("No %s Optimus instance exists on the garment component."),
			*ExpectedInstancePrefix);
		return false;
	}
	SurfaceCandidates.Sort([](const UOptimusDeformerInstance& A, const UOptimusDeformerInstance& B)
	{
		return A.GetName() > B.GetName();
	});

	TSharedPtr<FReadbackState, ESPMode::ThreadSafe> State =
		MakeShared<FReadbackState, ESPMode::ThreadSafe>();
	State->StartSeconds = FPlatformTime::Seconds();
	State->Result.State = EEFClothingSurfaceReadbackQAState::PendingGPU;
	State->Result.RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	State->Result.RequestedGameFrame = static_cast<int64>(GFrameCounter);
	State->Result.RequestedGarmentPoseRevision = static_cast<int64>(
		GarmentComponent->GetBoneTransformRevisionNumber());
	State->Result.RequestedBodyPoseRevision = static_cast<int64>(
		BodyComponent->GetBoneTransformRevisionNumber());
	State->Result.bUsedExactV26Binding = true;
	State->BodyToGarment = BodyComponent->GetComponentTransform().GetRelativeTransform(
		GarmentComponent->GetComponentTransform());
	if (State->BodyToGarment.ContainsNaN())
	{
		OutError = TEXT("Body-to-garment component transform is non-finite.");
		return false;
	}

	TSet<FName> ExcludedBodySlots;
	for (const FName SlotName : SurfaceBinding->ExcludedBodySurfaceMaterialSlots)
	{
		ExcludedBodySlots.Add(SlotName);
	}
	TSet<FName> NoExcludedGarmentSlots;
	if (!SnapshotComponentTopologies(
			GarmentComponent,
			NoExcludedGarmentSlots,
			State->GarmentTopologies,
			OutError)
		|| !SnapshotComponentTopologies(
			BodyComponent,
			ExcludedBodySlots,
			State->BodyTopologies,
			OutError))
	{
		return false;
	}
	const float RuntimeOffsetCm = FMath::Max(
		FMath::IsFinite(GlobalClearanceOffsetCm) ? GlobalClearanceOffsetCm : 0.0f,
		0.0f)
		+ FMath::Max(
			FMath::IsFinite(GarmentClearanceOffsetCm) ? GarmentClearanceOffsetCm : 0.0f,
			0.0f);
	SnapshotBindings(SurfaceBinding, RuntimeOffsetCm, State->LODBindings);
	if (State->LODBindings.IsEmpty())
	{
		OutError = TEXT("V26 binding contains no LOD-pair data.");
		return false;
	}

	State->PendingSurfaceCandidateCallbacks = SurfaceCandidates.Num();
	OutRequestId = State->Result.RequestId;
	{
		FScopeLock RegistryLock(&GRequestRegistryMutex);
		GRequestRegistry.Add(OutRequestId, State);
	}

	TUniquePtr<FMeshDeformerGeometryReadbackRequest> BodyRequest =
		MakeUnique<FMeshDeformerGeometryReadbackRequest>();
	BodyRequest->VertexDataArraysCallback_AnyThread =
		[State](const FMeshDeformerGeometryReadbackVertexDataArrays& Data)
		{
			ReceiveSingleReadback(State, Data, true);
		};
	const bool bBodyQueued = BodyComponent->RequestReadbackRenderGeometry(MoveTemp(BodyRequest));

	TUniquePtr<FMeshDeformerGeometryReadbackRequest> GarmentBaseRequest =
		MakeUnique<FMeshDeformerGeometryReadbackRequest>();
	GarmentBaseRequest->VertexDataArraysCallback_AnyThread =
		[State](const FMeshDeformerGeometryReadbackVertexDataArrays& Data)
		{
			ReceiveSingleReadback(State, Data, false);
		};
	const bool bGarmentBaseQueued = GarmentComponent->RequestReadbackRenderGeometry(
		MoveTemp(GarmentBaseRequest));

	bool bAnySurfaceQueued = false;
	for (UOptimusDeformerInstance* SurfaceInstance : SurfaceCandidates)
	{
		const FString InstanceName = SurfaceInstance->GetName();
		TUniquePtr<FMeshDeformerGeometryReadbackRequest> SurfaceRequest =
			MakeUnique<FMeshDeformerGeometryReadbackRequest>();
		SurfaceRequest->VertexDataArraysCallback_AnyThread =
			[State, InstanceName](const FMeshDeformerGeometryReadbackVertexDataArrays& Data)
			{
				ReceiveSurfaceReadback(State, InstanceName, Data);
			};
		UMeshDeformerInstance* PublicInterface = SurfaceInstance;
		bAnySurfaceQueued |= PublicInterface->RequestReadbackDeformerGeometry(MoveTemp(SurfaceRequest));
	}

	if (!bBodyQueued || !bGarmentBaseQueued || !bAnySurfaceQueued)
	{
		OutError = TEXT("UE 5.8 rejected one or more asynchronous deformer readback requests.");
		SetFailed(State, OutError);
		return false;
	}
	return true;
#endif
}

FEFClothingSurfaceReadbackQAResult UEFClothingSurfaceReadbackQALibrary::PollFinalGeometryReadback(
	const FString& RequestId)
{
	using namespace EFClothingSurfaceReadbackQA;
	TSharedPtr<FReadbackState, ESPMode::ThreadSafe> State;
	{
		FScopeLock RegistryLock(&GRequestRegistryMutex);
		State = GRequestRegistry.FindRef(RequestId);
	}
	if (!State.IsValid())
	{
		FEFClothingSurfaceReadbackQAResult Result;
		Result.RequestId = RequestId;
		Result.Error = TEXT("Unknown or released geometry readback request.");
		return Result;
	}
	FScopeLock Lock(&State->Mutex);
	return State->Result;
}

bool UEFClothingSurfaceReadbackQALibrary::ReleaseFinalGeometryReadback(
	const FString& RequestId)
{
	using namespace EFClothingSurfaceReadbackQA;
	FScopeLock RegistryLock(&GRequestRegistryMutex);
	return GRequestRegistry.Remove(RequestId) > 0;
}

void UEFClothingSurfaceReadbackQALibrary::ReleaseAllFinalGeometryReadbacks()
{
	using namespace EFClothingSurfaceReadbackQA;
	FScopeLock RegistryLock(&GRequestRegistryMutex);
	GRequestRegistry.Reset();
}
