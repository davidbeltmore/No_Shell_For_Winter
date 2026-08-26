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
	};

	struct FWitnessSnapshot
	{
		FIntVector GarmentRenderVertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		FVector3f GarmentBarycentrics = FVector3f::ZeroVector;
		float TargetClearanceCm = 0.0f;
	};

	struct FLODBindingSnapshot
	{
		int32 GarmentLODIndex = INDEX_NONE;
		int32 BodyLODIndex = INDEX_NONE;
		TArray<float> VertexTargetClearanceCm;
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
				for (uint32 Offset = 0; Offset < SectionIndexCount; ++Offset)
				{
					const uint32 VertexIndex = IndexBuffer->Get(Section.BaseIndex + Offset);
					if (VertexIndex >= static_cast<uint32>(Snapshot.RenderVertexCount))
					{
						OutError = FString::Printf(
							TEXT("LOD %d render index %u is outside its vertex buffer."),
							LODIndex,
							VertexIndex);
						return false;
					}
					Snapshot.TriangleIndices.Add(static_cast<int32>(VertexIndex));
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
			Snapshot.VertexTargetClearanceCm.Reserve(Pair.VertexBindings.Num());
			for (const FEFClothingSurfaceVertexBinding& Vertex : Pair.VertexBindings)
			{
				float TargetCm = FMath::Max(Vertex.TargetClearanceCm, 0.0f);
				if (Vertex.Mode == EEFClothingSurfaceVertexMode::SurfaceFollow)
				{
					TargetCm = FMath::Max(TargetCm, Vertex.RestSignedGapCm);
				}
				Snapshot.VertexTargetClearanceCm.Add(
					FMath::Max(TargetCm + RuntimeOffsetCm, 0.0f));
			}

			Snapshot.Witnesses.Reserve(Pair.Witnesses.Num());
			for (const FEFClothingSurfaceWitness& Witness : Pair.Witnesses)
			{
				FWitnessSnapshot& OutWitness = Snapshot.Witnesses.AddDefaulted_GetRef();
				OutWitness.GarmentRenderVertexIndices = Witness.GarmentRenderVertexIndices;
				OutWitness.GarmentBarycentrics = Witness.GarmentBarycentrics;
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
			if (FVector3d::CrossProduct(B - A, C - A).SquaredLength() <= UE_DOUBLE_SMALL_NUMBER)
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

	bool FindSignedGap(
		const FDynamicMesh3& BodyMesh,
		const FDynamicMeshAABBTree3& BodyTree,
		const FVector3d& QueryPoint,
		float& OutSignedGapCm)
	{
		double DistanceSquared = TNumericLimits<double>::Max();
		const int32 TriangleId = BodyTree.FindNearestTriangle(QueryPoint, DistanceSquared);
		if (TriangleId == INDEX_NONE || !BodyMesh.IsTriangle(TriangleId))
		{
			return false;
		}
		const FIndex3i Indices = BodyMesh.GetTriangle(TriangleId);
		const FTriangle3d Triangle(
			BodyMesh.GetVertex(Indices.A),
			BodyMesh.GetVertex(Indices.B),
			BodyMesh.GetVertex(Indices.C));
		FDistPoint3Triangle3d DistanceQuery(QueryPoint, Triangle);
		DistanceQuery.GetSquared();
		FVector3d Normal = FVector3d::CrossProduct(
			Triangle.V[1] - Triangle.V[0],
			Triangle.V[2] - Triangle.V[0]);
		if (!Normal.Normalize())
		{
			return false;
		}
		OutSignedGapCm = static_cast<float>(
			FVector3d::DotProduct(QueryPoint - DistanceQuery.ClosestTrianglePoint, Normal));
		return FMath::IsFinite(OutSignedGapCm);
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
		if (Binding->VertexTargetClearanceCm.Num() != GarmentFinal.Positions.Num())
		{
			FailAnalysis(TEXT("V26 per-render-vertex clearance array does not match the final GPU buffer."));
			return;
		}
		if (GarmentBase.Positions.Num() != GarmentFinal.Positions.Num())
		{
			FailAnalysis(TEXT("Pre-EF and post-EF garment vertex counts differ."));
			return;
		}

		TArray<FVector3d> BodyPositions;
		BodyPositions.Reserve(BodyFinal.Positions.Num());
		for (const FVector3f& Position : BodyFinal.Positions)
		{
			if (!IsFinite(Position))
			{
				++Result.InvalidOrNonFiniteVertexCount;
				BodyPositions.Add(FVector3d::Zero());
			}
			else
			{
				BodyPositions.Add(FVector3d(BodyToGarment.TransformPosition(FVector(Position))));
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
			FailAnalysis(TEXT("One or more final GPU positions are NaN/Inf."));
			return;
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

		FDynamicMeshAABBTree3 BodyTree(&BodyMesh, true);
		FDynamicMeshAABBTree3 GarmentTree(&GarmentMesh, true);
		const MeshIntersection::FIntersectionsQueryResult Intersections =
			BodyTree.FindAllIntersections(GarmentTree);
		Result.TriangleIntersectionCount = Intersections.Points.Num()
			+ Intersections.Segments.Num()
			+ Intersections.Polygons.Num();

		Result.MinimumVertexSkinGapCm = TNumericLimits<float>::Max();
		Result.MinimumClearanceResidualCm = TNumericLimits<float>::Max();
		for (int32 VertexIndex = 0; VertexIndex < GarmentPositions.Num(); ++VertexIndex)
		{
			float GapCm = 0.0f;
			if (!FindSignedGap(BodyMesh, BodyTree, GarmentPositions[VertexIndex], GapCm))
			{
				FailAnalysis(TEXT("Failed to find a valid closest body triangle for a garment vertex."));
				return;
			}
			Result.MinimumVertexSkinGapCm = FMath::Min(Result.MinimumVertexSkinGapCm, GapCm);
			Result.VertexSkinGapViolationCount += GapCm < VertexGapToleranceCm ? 1 : 0;
			const float ResidualCm = GapCm - Binding->VertexTargetClearanceCm[VertexIndex];
			Result.MinimumClearanceResidualCm = FMath::Min(Result.MinimumClearanceResidualCm, ResidualCm);
			Result.ClearanceResidualViolationCount +=
				ResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
		}

		Result.TriangleSampleCount = Binding->Witnesses.Num();
		Result.bTriangleSampleCoverageAvailable = !Binding->Witnesses.IsEmpty();
		Result.MinimumTriangleSampleSkinGapCm = Binding->Witnesses.IsEmpty()
			? 0.0f
			: TNumericLimits<float>::Max();
		for (const FWitnessSnapshot& Witness : Binding->Witnesses)
		{
			const FIntVector& Indices = Witness.GarmentRenderVertexIndices;
			if (!GarmentPositions.IsValidIndex(Indices.X)
				|| !GarmentPositions.IsValidIndex(Indices.Y)
				|| !GarmentPositions.IsValidIndex(Indices.Z))
			{
				FailAnalysis(TEXT("A compiled V26 witness references an invalid final render vertex."));
				return;
			}
			const FVector3d Sample =
				GarmentPositions[Indices.X] * Witness.GarmentBarycentrics.X
				+ GarmentPositions[Indices.Y] * Witness.GarmentBarycentrics.Y
				+ GarmentPositions[Indices.Z] * Witness.GarmentBarycentrics.Z;
			float GapCm = 0.0f;
			if (!FindSignedGap(BodyMesh, BodyTree, Sample, GapCm))
			{
				FailAnalysis(TEXT("Failed to find a valid closest body triangle for a V26 witness."));
				return;
			}
			Result.MinimumTriangleSampleSkinGapCm = FMath::Min(
				Result.MinimumTriangleSampleSkinGapCm,
				GapCm);
			Result.TriangleSampleSkinGapViolationCount += GapCm < VertexGapToleranceCm ? 1 : 0;
			const float ResidualCm = GapCm - Witness.TargetClearanceCm;
			Result.MinimumClearanceResidualCm = FMath::Min(Result.MinimumClearanceResidualCm, ResidualCm);
			Result.ClearanceResidualViolationCount +=
				ResidualCm < ClearanceResidualToleranceCm ? 1 : 0;
		}

		TArray<float> CorrectionMagnitudes;
		CorrectionMagnitudes.Reserve(GarmentFinal.Positions.Num());
		for (int32 VertexIndex = 0; VertexIndex < GarmentFinal.Positions.Num(); ++VertexIndex)
		{
			if (!IsFinite(GarmentBase.Positions[VertexIndex]))
			{
				FailAnalysis(TEXT("Pre-EF garment readback contains NaN/Inf positions."));
				return;
			}
			const float MagnitudeCm = FVector3f::Distance(
				GarmentFinal.Positions[VertexIndex],
				GarmentBase.Positions[VertexIndex]);
			CorrectionMagnitudes.Add(MagnitudeCm);
			Result.MaximumCorrectionMagnitudeCm = FMath::Max(
				Result.MaximumCorrectionMagnitudeCm,
				MagnitudeCm);
		}
		Result.CorrectionMagnitudeP99Cm = Percentile99(CorrectionMagnitudes);

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
				? TEXT("Body Optimus readback returned no render positions.")
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
			if (!State->bGarmentFinalReceived && CopyReadback(Data, State->GarmentFinal))
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
		false);
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
