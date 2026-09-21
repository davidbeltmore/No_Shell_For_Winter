#include "EFClothingSurfaceDeformerProducer.h"

#include "Animation/MeshDeformerInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFClothingSurfaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/CoreDelegates.h"
#include "OptimusComponentSource.h"
#include "OptimusDeformer.h"
#include "OptimusDeformerDynamicInstanceManager.h"
#include "OptimusDeformerInstance.h"
#include "RenderingThread.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"
#include "Templates/Atomic.h"

namespace EFClothingSurfaceGraphContract
{
	const FName BodyBinding(TEXT("Body"));
	const FName BindingVertexCount(TEXT("EF_BindingVertexCount"));
	const FName BodyVertexCount(TEXT("EF_BodyVertexCount"));
	const FName GarmentLOD(TEXT("EF_GarmentLODIndex"));
	const FName BodyLOD(TEXT("EF_BodyLODIndex"));
	const FName BodyTriangleAndMode(TEXT("EF_BodyTriangleAndMode"));
	const FName BarycentricsAndFollowWeight(TEXT("EF_BarycentricsAndFollowWeight"));
	const FName RestOffsetAndClearanceCm(TEXT("EF_RestOffsetAndClearanceCm"));
	const FName MaximumCorrectionAndRestGapCm(TEXT("EF_MaximumCorrectionAndRestGapCm"));
	const FName ThicknessReferenceAndLayer(TEXT("EF_ThicknessReferenceAndLayer"));
	const FName NeighborReferenceCount(TEXT("EF_NeighborReferenceCount"));
	const FName NeighborRanges(TEXT("EF_NeighborRanges"));
	const FName NeighborIndices(TEXT("EF_NeighborIndices"));
	const FName WeldReferenceCount(TEXT("EF_WeldReferenceCount"));
	const FName WeldRanges(TEXT("EF_WeldRanges"));
	const FName WeldIndices(TEXT("EF_WeldIndices"));
	const FName LocalFeatureSizeCm(TEXT("EF_LocalFeatureSizeCm"));
	const FName UnderBreastGuardWeights(TEXT("EF_UnderBreastGuardWeights"));
	const FName LowerBodyMorphGuardWeights(TEXT("EF_LowerBodyMorphGuardWeights"));
	const FName WitnessCount(TEXT("EF_WitnessCount"));
	const FName WitnessReferenceCount(TEXT("EF_WitnessReferenceCount"));
	const FName WitnessRanges(TEXT("EF_WitnessRanges"));
	const FName WitnessIndices(TEXT("EF_WitnessIndices"));
	const FName WitnessGarmentVertices(TEXT("EF_WitnessGarmentVertices"));
	const FName WitnessGarmentBarycentricsAndClearanceCm(
		TEXT("EF_WitnessGarmentBarycentricsAndClearanceCm"));
	const FName WitnessBodyVertices(TEXT("EF_WitnessBodyVertices"));
	const FName WitnessBodyBarycentricsAndMaximumCorrectionCm(
		TEXT("EF_WitnessBodyBarycentricsAndMaximumCorrectionCm"));
	const FName GlobalClearanceOffsetCm(TEXT("EF_GlobalClearanceOffsetCm"));
	const FName GarmentClearanceOffsetCm(TEXT("EF_GarmentClearanceOffsetCm"));
	const FName GarmentInflateCm(TEXT("EF_GarmentInflateCm"));
	const FName MaximumCorrectionOverrideCm(TEXT("EF_MaximumCorrectionOverrideCm"));
	const FName MaximumAutomaticBodyShapeTravelCm(
		TEXT("EF_MaximumAutomaticBodyShapeTravelCm"));
	const FName BodyMorphActivity(TEXT("EF_BodyMorphActivity"));
	const FName BreastMorphActivity(TEXT("EF_BreastMorphActivity"));
	const FName UnderBreastClearanceMaxCm(TEXT("EF_UnderBreastClearanceMaxCm"));
	const FName LowerBodyMorphActivity(TEXT("EF_LowerBodyMorphActivity"));
	const FName LowerBodyMorphClearanceMaxCm(TEXT("EF_LowerBodyMorphClearanceMaxCm"));
	const FName DeltaTimeSeconds(TEXT("EF_DeltaTimeSeconds"));
	const FName BodyToGarmentTransform(TEXT("EF_BodyToGarmentTransform"));
}

struct UEFClothingSurfaceDeformerProducer::FDispatchTelemetry
{
	TAtomic<uint32> DispatchFailureCount { 0u };
	TAtomic<uint32> ImmediateEnqueueFallbackCount { 0u };
	TAtomic<uint32> RenderValidationFallbackCount { 0u };
	TAtomic<uint64> RenderValidatedSubmissionCount { 0u };
	TAtomic<uint32> LastValidatedFailureCount { 0u };
	TAtomic<uint64> LastRenderEnqueueMarkerSubmission { 0u };
	TAtomic<uint32> RenderPreflightMask { 0u };
	TAtomic<int32> GarmentActualLOD { INDEX_NONE };
	TAtomic<int32> BodyActualLOD { INDEX_NONE };
	TAtomic<int32> GarmentActualSectionCount { INDEX_NONE };
	TAtomic<int32> BodyActualSectionCount { INDEX_NONE };
	TAtomic<bool> bRenderConfirmationArmed { false };
	TAtomic<bool> bCancelled { false };
};

namespace
{
	constexpr int32 MaximumWitnessReferencesPerVertex = 256;
	constexpr uint32 MinimumPostReadyRecoverySubmissions = 8u;
	constexpr uint32 MaximumPostReadyRecoverySubmissions = 120u;
	constexpr double PostReadyRecoveryTimeoutSeconds = 1.0;
	constexpr uint32 MinimumInitialWarmupSubmissions = 30u;
	constexpr uint32 MaximumInitialWarmupSubmissions = 2400u;
	constexpr double InitialWarmupTimeoutSeconds = 10.0;
	constexpr double MaximumRecoveryDeltaSeconds = 0.25;

	bool IsBreastAffectingMorphName(const FName MorphName)
	{
		const FString Name = MorphName.ToString();
		return Name.Contains(TEXT("Breast"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Voluptuous"), ESearchCase::IgnoreCase);
	}

	/**
	 * One-shot render-thread arm. We intentionally wait through two EndFrameRT
	 * callbacks: if the enqueue command arrived after BeginInitViews in the first
	 * render frame, ComputeFramework will consume it during the second one before
	 * this confirmation runs. The callback only proves successful render-graph
	 * validation/submission (including provider and shader validation), not GPU
	 * completion; obtaining the latter would require an asynchronous GPU fence or
	 * readback and is not needed for the component's readiness telemetry.
	 */
	struct FRenderSubmissionConfirmationArm
	{
		FDelegateHandle EndFrameDelegateHandle;
		int32 RemainingEndFrameCallbacks = 2;
	};

	UOptimusDeformerDynamicInstanceManager* ResolveDynamicManager(
		USkeletalMeshComponent* Component,
		const int32 LODIndex)
	{
		if (!IsValid(Component) || LODIndex < 0)
		{
			return nullptr;
		}

		return Cast<UOptimusDeformerDynamicInstanceManager>(
			Component->GetMeshDeformerInstanceForLOD(LODIndex));
	}

	bool SourceDeformerWrites(
		UOptimusDeformerDynamicInstanceManager* Manager,
		const EMeshDeformerOutputBuffer RequiredBuffers)
	{
		if (!IsValid(Manager))
		{
			return false;
		}

		UMeshDeformerInstance* SourceInstance = Manager->GetInstanceForSourceDeformer();
		return IsValid(SourceInstance)
			&& EnumHasAllFlags(SourceInstance->GetOutputBuffers(), RequiredBuffers);
	}

	bool IsFiniteVector3f(const FVector3f& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool ValidateWeldContract(
		const FEFClothingSurfaceLODPairBinding& Pair,
		const int32 VertexCount,
		FString& OutFailureReason)
	{
		if (Pair.Metrics.WeldReferenceCount != Pair.WeldRenderVertexIndices.Num()
			|| Pair.Metrics.WeldGroupCount < 0
			|| Pair.Metrics.WeldReferenceCount < 0)
		{
			OutFailureReason = TEXT("Surface binding weld metrics do not match the exact-split pool.");
			return false;
		}

		TMap<int32, int32> WeldGroupCountsByOffset;
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			if (!Pair.VertexBindings.IsValidIndex(VertexIndex))
			{
				OutFailureReason = TEXT("Surface binding weld validation encountered a missing vertex binding.");
				return false;
			}
			const FEFClothingSurfaceVertexBinding& Binding = Pair.VertexBindings[VertexIndex];
			if (!FMath::IsFinite(Binding.LocalFeatureSizeCm)
				|| Binding.LocalFeatureSizeCm <= 0.0f)
			{
				OutFailureReason = FString::Printf(
					TEXT("Surface binding vertex %d has an invalid local feature size."),
					VertexIndex);
				return false;
			}
			const FEFClothingSurfaceIndexRange& WeldRange = Binding.WeldRange;
			if (WeldRange.Count == 0)
			{
				if (WeldRange.Offset != 0)
				{
					OutFailureReason = FString::Printf(
						TEXT("Surface binding singleton vertex %d has a non-zero weld offset."),
						VertexIndex);
					return false;
				}
				continue;
			}
			if (WeldRange.Count < 2
				|| WeldRange.Count > 256
				|| WeldRange.Offset < 0
				|| WeldRange.Offset > Pair.WeldRenderVertexIndices.Num()
				|| WeldRange.Count
					> Pair.WeldRenderVertexIndices.Num() - WeldRange.Offset)
			{
				OutFailureReason = FString::Printf(
					TEXT("Surface binding vertex %d has an invalid weld range."),
					VertexIndex);
				return false;
			}
			bool bRangeContainsVertex = false;
			for (int32 LocalIndex = 0; LocalIndex < WeldRange.Count; ++LocalIndex)
			{
				bRangeContainsVertex |= Pair.WeldRenderVertexIndices[
					WeldRange.Offset + LocalIndex] == VertexIndex;
			}
			if (!bRangeContainsVertex)
			{
				OutFailureReason = FString::Printf(
					TEXT("Surface binding vertex %d points to a weld group that does not contain it."),
					VertexIndex);
				return false;
			}
			int32& StoredCount = WeldGroupCountsByOffset.FindOrAdd(
				WeldRange.Offset,
				WeldRange.Count);
			if (StoredCount != WeldRange.Count)
			{
				OutFailureReason = TEXT("Surface binding weld ranges disagree on a shared group size.");
				return false;
			}
		}

		TArray<int32> SortedGroupOffsets;
		WeldGroupCountsByOffset.GetKeys(SortedGroupOffsets);
		SortedGroupOffsets.Sort();
		if (SortedGroupOffsets.Num() != Pair.Metrics.WeldGroupCount)
		{
			OutFailureReason = TEXT("Surface binding weld-group evidence is stale.");
			return false;
		}
		int32 ExpectedOffset = 0;
		for (const int32 GroupOffset : SortedGroupOffsets)
		{
			const int32 GroupCount = WeldGroupCountsByOffset.FindChecked(GroupOffset);
			if (GroupOffset != ExpectedOffset || GroupCount < 2 || GroupCount > 256)
			{
				OutFailureReason = TEXT("Surface binding weld groups do not form a contiguous deterministic pool.");
				return false;
			}
			int32 PreviousVertexIndex = INDEX_NONE;
			bool bAnyPreserveUpstream = false;
			for (int32 LocalIndex = 0; LocalIndex < GroupCount; ++LocalIndex)
			{
				const int32 MemberVertexIndex =
					Pair.WeldRenderVertexIndices[GroupOffset + LocalIndex];
				if (MemberVertexIndex < 0
					|| MemberVertexIndex >= VertexCount
					|| MemberVertexIndex <= PreviousVertexIndex)
				{
					OutFailureReason = TEXT("Surface binding weld group is not a sorted set of valid render vertices.");
					return false;
				}
				PreviousVertexIndex = MemberVertexIndex;
				const FEFClothingSurfaceVertexBinding& Member =
					Pair.VertexBindings[MemberVertexIndex];
				if (Member.WeldRange.Offset != GroupOffset
					|| Member.WeldRange.Count != GroupCount)
				{
					OutFailureReason = TEXT("Surface binding weld member does not point back to its complete group.");
					return false;
				}
				bAnyPreserveUpstream |= Member.Mode
					== EEFClothingSurfaceVertexMode::PreserveUpstream;
			}

			const int32 AuthoritativeVertexIndex =
				Pair.WeldRenderVertexIndices[GroupOffset];
			const FEFClothingSurfaceVertexBinding& Authority =
				Pair.VertexBindings[AuthoritativeVertexIndex];
			auto NearlyEqualVector3 = [](const FVector3f& A, const FVector3f& B)
			{
				return FMath::IsNearlyEqual(A.X, B.X, 1.0e-5f)
					&& FMath::IsNearlyEqual(A.Y, B.Y, 1.0e-5f)
					&& FMath::IsNearlyEqual(A.Z, B.Z, 1.0e-5f);
			};
			for (int32 LocalIndex = 0; LocalIndex < GroupCount; ++LocalIndex)
			{
				const int32 MemberVertexIndex =
					Pair.WeldRenderVertexIndices[GroupOffset + LocalIndex];
				const FEFClothingSurfaceVertexBinding& Member =
					Pair.VertexBindings[MemberVertexIndex];
				if (Member.BodyRenderVertexIndices != Authority.BodyRenderVertexIndices
					|| !NearlyEqualVector3(Member.BodyBarycentrics, Authority.BodyBarycentrics)
					|| !NearlyEqualVector3(
						Member.RestTangentFrameOffsetCm,
						Authority.RestTangentFrameOffsetCm)
					|| !FMath::IsNearlyEqual(Member.RestSignedGapCm, Authority.RestSignedGapCm, 1.0e-5f)
					|| !FMath::IsNearlyEqual(Member.TargetClearanceCm, Authority.TargetClearanceCm, 1.0e-5f)
					|| !FMath::IsNearlyEqual(Member.MaximumCorrectionCm, Authority.MaximumCorrectionCm, 1.0e-5f)
					|| !FMath::IsNearlyEqual(Member.LocalFeatureSizeCm, Authority.LocalFeatureSizeCm, 1.0e-5f)
					|| !FMath::IsNearlyEqual(
						Member.UnderBreastGuardWeight,
						Authority.UnderBreastGuardWeight,
						1.0e-6f)
					|| !FMath::IsNearlyEqual(
						Member.LowerBodyMorphGuardWeight,
						Authority.LowerBodyMorphGuardWeight,
						1.0e-6f)
					|| Member.CandidateRange.Offset != Authority.CandidateRange.Offset
					|| Member.CandidateRange.Count != Authority.CandidateRange.Count
					|| Member.ThicknessReferenceRenderVertexIndex
						!= Authority.ThicknessReferenceRenderVertexIndex
					|| Member.bOuterThicknessLayer != Authority.bOuterThicknessLayer
					|| Member.Mode != Authority.Mode
					|| !FMath::IsNearlyEqual(Member.FollowWeight, Authority.FollowWeight, 1.0e-6f)
					|| (bAnyPreserveUpstream
						&& (Member.Mode != EEFClothingSurfaceVertexMode::PreserveUpstream
							|| !FMath::IsNearlyZero(Member.FollowWeight, 1.0e-6f))))
				{
					OutFailureReason = TEXT("Surface binding weld group does not share one canonical surface constraint.");
					return false;
				}
				for (int32 NeighborOffset = 0;
					NeighborOffset < Member.NeighborRange.Count;
					++NeighborOffset)
				{
					const int32 NeighborVertexIndex = Pair.NeighborRenderVertexIndices[
						Member.NeighborRange.Offset + NeighborOffset];
					if (NeighborVertexIndex >= AuthoritativeVertexIndex
						&& NeighborVertexIndex <= PreviousVertexIndex
						&& Pair.VertexBindings[NeighborVertexIndex].WeldRange.Offset == GroupOffset
						&& Pair.VertexBindings[NeighborVertexIndex].WeldRange.Count == GroupCount)
					{
						OutFailureReason = TEXT("Surface binding cohesion adjacency contains an exact weld twin.");
						return false;
					}
				}
			}
			ExpectedOffset += GroupCount;
		}
		if (ExpectedOffset != Pair.WeldRenderVertexIndices.Num())
		{
			OutFailureReason = TEXT("Surface binding weld ranges do not cover the exact-split pool.");
			return false;
		}
		return true;
	}
}

bool UEFClothingSurfaceDeformerProducer::Install(
	USkeletalMeshComponent* InGarmentComponent,
	USkeletalMeshComponent* InBodyComponent,
	UOptimusDeformer* InSurfaceDeformer,
	const UEFClothingSurfaceBinding* InSurfaceBinding,
	const FEFClothingSurfaceLODPairBinding& InLODPair,
	FString& OutFailureReason)
{
	check(IsInGameThread());
	Detach();
	OutFailureReason.Reset();

	if (!IsValid(InGarmentComponent)
		|| !IsValid(InBodyComponent)
		|| !IsValid(InSurfaceDeformer)
		|| !IsValid(InSurfaceBinding))
	{
		OutFailureReason = TEXT("Surface producer received an invalid garment, body, graph or binding asset.");
		return false;
	}
	if (InGarmentComponent == InBodyComponent)
	{
		OutFailureReason = TEXT("Surface producer requires distinct garment and body components.");
		return false;
	}
	if (InSurfaceDeformer->GetStatus() != EOptimusDeformerStatus::Compiled
		&& InSurfaceDeformer->GetStatus() != EOptimusDeformerStatus::CompiledWithWarnings)
	{
		OutFailureReason = TEXT("Surface constraint Deformer Graph is not compiled.");
		return false;
	}
	const UOptimusComponentSourceBinding* BodyBinding =
		InSurfaceDeformer->ResolveComponentBinding(EFClothingSurfaceGraphContract::BodyBinding);
	if (!BodyBinding || BodyBinding->IsPrimaryBinding()
		|| InSurfaceDeformer->GetComponentBindings().Num() != 2)
	{
		OutFailureReason = TEXT("Surface constraint graph must expose exactly Primary Garment plus non-primary Body bindings.");
		return false;
	}

	GarmentComponent = InGarmentComponent;
	BodyComponent = InBodyComponent;
	bTransportReferenceShape = !InSurfaceBinding->ReferenceBodySurface.IsNull();
	GarmentLODIndex = InLODPair.GarmentTopology.LODIndex;
	BodyLODIndex = InLODPair.BodyTopology.LODIndex;
	if (InSurfaceBinding->FindLODPair(GarmentLODIndex, BodyLODIndex) != &InLODPair)
	{
		OutFailureReason = TEXT("LOD binding does not belong to the supplied immutable surface asset.");
		Detach();
		return false;
	}
	LowerBodyMorphGuardExactBodyMorphNames.Reset();
	if (InSurfaceBinding->bLowerBodyMorphGuardEnabled)
	{
		for (const FName MorphName : InSurfaceBinding->LowerBodyMorphGuardExactBodyMorphNames)
		{
			if (!MorphName.IsNone())
			{
				LowerBodyMorphGuardExactBodyMorphNames.Add(MorphName);
			}
		}
	}
	LowerBodyMorphGuardMaximumClearanceCm = FMath::Clamp(
		InSurfaceBinding->bLowerBodyMorphGuardEnabled
			&& FMath::IsFinite(InSurfaceBinding->LowerBodyMorphGuardMaximumClearanceCm)
			? InSurfaceBinding->LowerBodyMorphGuardMaximumClearanceCm
			: 0.0f,
		0.0f,
		EFClothingMorphV4::MaximumAutomaticLowerBodyMorphClearanceCm);
	LowerBodyMorphGuardVertexCount = InLODPair.Metrics.LowerBodyMorphGuardVertexCount;
	const bool bLowerBodyGuardContractValid = InSurfaceBinding->bLowerBodyMorphGuardEnabled
		? (LowerBodyMorphGuardExactBodyMorphNames.Num()
				== InSurfaceBinding->LowerBodyMorphGuardExactBodyMorphNames.Num()
			&& LowerBodyMorphGuardMaximumClearanceCm > 0.0f
			&& LowerBodyMorphGuardVertexCount > 0
			&& LowerBodyMorphGuardVertexCount <= InLODPair.VertexBindings.Num())
		: (LowerBodyMorphGuardExactBodyMorphNames.IsEmpty()
			&& LowerBodyMorphGuardMaximumClearanceCm <= 0.0f
			&& LowerBodyMorphGuardVertexCount == 0);
	if (!bLowerBodyGuardContractValid)
	{
		OutFailureReason = TEXT("Surface binding lower-body morph guard metadata is incomplete or inconsistent.");
		Detach();
		return false;
	}

	if (!ValidateLiveLODTopology(InLODPair, OutFailureReason))
	{
		Detach();
		return false;
	}

	UOptimusDeformerDynamicInstanceManager* Manager = ResolveDynamicManager(
		InGarmentComponent,
		GarmentLODIndex);
	if (!SourceDeformerWrites(
		Manager,
		EMeshDeformerOutputBuffer::SkinnedMeshPosition
			| EMeshDeformerOutputBuffer::SkinnedMeshTangents))
	{
		OutFailureReason = TEXT("Garment LOD has no Optimus source writer for final positions and tangents.");
		Detach();
		return false;
	}
	UOptimusDeformerDynamicInstanceManager* BodyManager = ResolveDynamicManager(
		InBodyComponent,
		BodyLODIndex);
	if (!SourceDeformerWrites(
		BodyManager,
		EMeshDeformerOutputBuffer::SkinnedMeshPosition))
	{
		OutFailureReason = TEXT("Body LOD has no Optimus source writer for final animated positions.");
		Detach();
		return false;
	}

	DynamicManager = Manager;
	InstanceGuid = FGuid::NewGuid();
	Manager->AddProducerDeformer(this, InstanceGuid, InSurfaceDeformer);
	bRegisteredWithManager = true;

	UOptimusDeformerInstance* Instance = Manager->GetDeformerInstance(InstanceGuid);
	if (!IsValid(Instance))
	{
		OutFailureReason = TEXT("Optimus did not create the surface producer instance.");
		Detach();
		return false;
	}

	InstanceSettings = NewObject<UOptimusDeformerInstanceSettings>(this);
	InstanceSettings->InitializeSettings(InSurfaceDeformer, InGarmentComponent);
	InstanceSettings->ComponentResolver.BindUObject(
		this,
		&UEFClothingSurfaceDeformerProducer::ResolveComponentBinding);
	Instance->SetInstanceSettings(InstanceSettings);
	Instance->SetupFromDeformer(InSurfaceDeformer);
	SurfaceInstance = Instance;

	DispatchTelemetry = MakeShared<FDispatchTelemetry, ESPMode::ThreadSafe>();
	LastObservedDispatchFailureCount = 0;
	DispatchRecoverySubmissionCount = 0;
	DispatchRecoveryElapsedSeconds = 0.0;
	bAwaitingDispatchRecovery = false;
	bDispatchRecoveryStartedAfterReady = false;
	EnqueuedFrameCount = 0;

	if (!UploadImmutableBinding(InLODPair, OutFailureReason))
	{
		Detach();
		return false;
	}

	return true;
}

bool UEFClothingSurfaceDeformerProducer::EnqueueSurfacePass(
	const float DeltaTimeSeconds,
	const float GlobalClearanceOffsetCm,
	const float GarmentClearanceOffsetCm,
	const float GarmentInflateCm,
	const float MaximumCorrectionOverrideCm,
	FString& OutFailureReason)
{
	check(IsInGameThread());
	OutFailureReason.Reset();

	USkeletalMeshComponent* Garment = GarmentComponent.Get();
	USkeletalMeshComponent* Body = BodyComponent.Get();
	UOptimusDeformerDynamicInstanceManager* Manager = DynamicManager.Get();
	UOptimusDeformerInstance* Instance = SurfaceInstance.Get();
	if (!IsValid(Garment) || !IsValid(Body) || !IsValid(Manager) || !IsValid(Instance)
		|| !bRegisteredWithManager || Manager->GetDeformerInstance(InstanceGuid) != Instance)
	{
		OutFailureReason = TEXT("Surface producer instance or owning components were recreated or released.");
		return false;
	}
	if (!Garment->IsRegistered()
		|| !Body->IsRegistered()
		|| Garment->GetScene() == nullptr
		|| Body->GetScene() != Garment->GetScene())
	{
		OutFailureReason = TEXT("Garment/body render components or scene are not registered.");
		return false;
	}
	if (ResolveDynamicManager(Garment, GarmentLODIndex) != Manager
		|| !SourceDeformerWrites(
			Manager,
			EMeshDeformerOutputBuffer::SkinnedMeshPosition
				| EMeshDeformerOutputBuffer::SkinnedMeshTangents)
		|| !SourceDeformerWrites(
			ResolveDynamicManager(Body, BodyLODIndex),
			EMeshDeformerOutputBuffer::SkinnedMeshPosition))
	{
		OutFailureReason = TEXT("Garment/body active LOD source writer changed after surface installation.");
		return false;
	}
	if (!DispatchTelemetry.IsValid())
	{
		OutFailureReason = TEXT("Surface dispatch telemetry is unavailable.");
		return false;
	}

	const uint32 FailureCount = DispatchTelemetry->DispatchFailureCount.Load();
	const bool bCurrentFailureGenerationValidated =
		DispatchTelemetry->RenderValidatedSubmissionCount.Load() > 0
		&& DispatchTelemetry->LastValidatedFailureCount.Load() == FailureCount;
	if (bAwaitingDispatchRecovery && bCurrentFailureGenerationValidated)
	{
		bAwaitingDispatchRecovery = false;
		DispatchRecoverySubmissionCount = 0;
		DispatchRecoveryElapsedSeconds = 0.0;
		bDispatchRecoveryStartedAfterReady = false;
	}
	if (FailureCount != LastObservedDispatchFailureCount)
	{
		LastObservedDispatchFailureCount = FailureCount;
		if (!bRenderPreflightEnqueued
			&& DispatchTelemetry->RenderValidationFallbackCount.Load() > 0)
		{
			bRenderPreflightEnqueued = true;
			const TSharedPtr<FDispatchTelemetry, ESPMode::ThreadSafe> PreflightTelemetry = DispatchTelemetry;
			FSceneInterface* GarmentScene = Garment->GetScene();
			FSceneInterface* BodyScene = Body->GetScene();
			const FPrimitiveComponentId GarmentComponentId = Garment->GetPrimitiveSceneId();
			const FPrimitiveComponentId BodyComponentId = Body->GetPrimitiveSceneId();
			const int32 ExpectedGarmentLOD = GarmentLODIndex;
			const int32 ExpectedBodyLOD = BodyLODIndex;
			uint32 GameThreadMask = 0u;
			if (!Garment->IsRenderStateCreated()) GameThreadMask |= 1u << 0;
			if (!Body->IsRenderStateCreated()) GameThreadMask |= 1u << 1;
			if (!GarmentComponentId.IsValid()) GameThreadMask |= 1u << 2;
			if (!BodyComponentId.IsValid()) GameThreadMask |= 1u << 3;
			ENQUEUE_RENDER_COMMAND(EFClothingSurfaceRenderPreflight)(
				[PreflightTelemetry,
				 GarmentScene,
				 BodyScene,
				 GarmentComponentId,
				 BodyComponentId,
				 ExpectedGarmentLOD,
				 ExpectedBodyLOD,
				 GameThreadMask](FRHICommandListImmediate& RHICmdList)
				{
					if (!PreflightTelemetry.IsValid() || PreflightTelemetry->bCancelled.Load())
					{
						return;
					}
					uint32 Mask = GameThreadMask;
					FSkeletalMeshObject* GarmentObject =
						FSkeletalMeshDeformerHelpers::GetSkeletalMeshObject(GarmentScene, GarmentComponentId);
					FSkeletalMeshObject* BodyObject =
						FSkeletalMeshDeformerHelpers::GetSkeletalMeshObject(BodyScene, BodyComponentId);
					if (!GarmentObject)
					{
						Mask |= 1u << 4;
					}
					else
					{
						const int32 ActualLOD = GarmentObject->GetLOD();
						PreflightTelemetry->GarmentActualLOD.Store(ActualLOD);
						if (ActualLOD != ExpectedGarmentLOD) Mask |= 1u << 6;
						const FSkeletalMeshRenderData& RenderData = GarmentObject->GetSkeletalMeshRenderData();
						if (!RenderData.LODRenderData.IsValidIndex(ActualLOD))
						{
							Mask |= 1u << 8;
						}
						else
						{
							PreflightTelemetry->GarmentActualSectionCount.Store(
								RenderData.LODRenderData[ActualLOD].RenderSections.Num());
							if (FSkeletalMeshDeformerHelpers::GetIndexOfFirstAvailableSection(
								GarmentObject, ActualLOD) == INDEX_NONE)
							{
								Mask |= 1u << 10;
							}
						}
					}
					if (!BodyObject)
					{
						Mask |= 1u << 5;
					}
					else
					{
						const int32 ActualLOD = BodyObject->GetLOD();
						PreflightTelemetry->BodyActualLOD.Store(ActualLOD);
						if (ActualLOD != ExpectedBodyLOD) Mask |= 1u << 7;
						const FSkeletalMeshRenderData& RenderData = BodyObject->GetSkeletalMeshRenderData();
						if (!RenderData.LODRenderData.IsValidIndex(ActualLOD))
						{
							Mask |= 1u << 9;
						}
						else
						{
							PreflightTelemetry->BodyActualSectionCount.Store(
								RenderData.LODRenderData[ActualLOD].RenderSections.Num());
							if (FSkeletalMeshDeformerHelpers::GetIndexOfFirstAvailableSection(
								BodyObject, ActualLOD) == INDEX_NONE)
							{
								Mask |= 1u << 11;
							}
						}
					}
					PreflightTelemetry->RenderPreflightMask.Store(Mask);
				});
		}
		// ComputeFramework invokes the same fallback during shader warm-up and can
		// also invoke it for an already queued submission while ACF recreates the
		// component render state. Treat that transition as recoverable. Readiness is
		// revoked until a submission from the new failure generation is validated.
		if (!bCurrentFailureGenerationValidated && !bAwaitingDispatchRecovery)
		{
			bAwaitingDispatchRecovery = true;
			bDispatchRecoveryStartedAfterReady =
				DispatchTelemetry->RenderValidatedSubmissionCount.Load() > 0;
			DispatchRecoverySubmissionCount = 0;
			DispatchRecoveryElapsedSeconds = 0.0;
		}
	}
	if (bAwaitingDispatchRecovery)
	{
		DispatchRecoveryElapsedSeconds += FMath::Clamp(
			static_cast<double>(FMath::IsFinite(DeltaTimeSeconds) ? DeltaTimeSeconds : 0.0f),
			0.0,
			MaximumRecoveryDeltaSeconds);
		const uint32 MinimumRecoverySubmissions = bDispatchRecoveryStartedAfterReady
			? MinimumPostReadyRecoverySubmissions
			: MinimumInitialWarmupSubmissions;
		const uint32 MaximumRecoverySubmissions = bDispatchRecoveryStartedAfterReady
			? MaximumPostReadyRecoverySubmissions
			: MaximumInitialWarmupSubmissions;
		const double RecoveryTimeoutSeconds = bDispatchRecoveryStartedAfterReady
			? PostReadyRecoveryTimeoutSeconds
			: InitialWarmupTimeoutSeconds;
		if (DispatchRecoverySubmissionCount >= MaximumRecoverySubmissions
			|| (DispatchRecoverySubmissionCount >= MinimumRecoverySubmissions
				&& DispatchRecoveryElapsedSeconds >= RecoveryTimeoutSeconds))
		{
			OutFailureReason = FString::Printf(
				TEXT("Surface constraint graph did not recover within the bounded dispatch retry window (failures=%u, immediate=%u, render=%u)."),
				FailureCount,
				DispatchTelemetry->ImmediateEnqueueFallbackCount.Load(),
				DispatchTelemetry->RenderValidationFallbackCount.Load());
			return false;
		}
	}

	// Read Skinned Mesh exposes component-local positions. Transport the exact
	// final Director-selected body surface into garment-local space before
	// evaluating anchors.
	const FTransform BodyToGarment = Body->GetComponentTransform().GetRelativeTransform(
		Garment->GetComponentTransform());
	const FVector RelativeScale = BodyToGarment.GetScale3D();
	if (BodyToGarment.ContainsNaN()
		|| FMath::Abs(RelativeScale.X) <= UE_SMALL_NUMBER
		|| FMath::Abs(RelativeScale.Y) <= UE_SMALL_NUMBER
		|| FMath::Abs(RelativeScale.Z) <= UE_SMALL_NUMBER
		|| RelativeScale.X <= 0.0
		|| RelativeScale.Y <= 0.0
		|| RelativeScale.Z <= 0.0
		|| !FMath::IsNearlyEqual(RelativeScale.X, RelativeScale.Y, 1.0e-4)
		|| !FMath::IsNearlyEqual(RelativeScale.X, RelativeScale.Z, 1.0e-4))
	{
		OutFailureReason = TEXT("Body-to-garment transform is non-finite, mirrored, degenerate or non-uniform; animated surface normals cannot be certified.");
		return false;
	}

	float BodyMorphActivity = bTransportReferenceShape ? 1.0f : 0.0f;
	float BreastMorphActivity = 0.0f;
	float LowerBodyMorphActivity = 0.0f;
	for (const TPair<FName, float>& MorphCurve : Body->GetMorphTargetCurves())
	{
		if (!FMath::IsFinite(MorphCurve.Value))
		{
			LastBodyMorphActivity = 0.0f;
			LastBreastMorphActivity = 0.0f;
			LastLowerBodyMorphActivity = 0.0f;
			OutFailureReason = FString::Printf(
				TEXT("Body morph %s has a non-finite weight; the clothing surface pass was rejected fail-closed."),
				*MorphCurve.Key.ToString());
			return false;
		}
		BodyMorphActivity = FMath::Max(
			BodyMorphActivity,
			FMath::Abs(MorphCurve.Value));
		if (IsBreastAffectingMorphName(MorphCurve.Key))
		{
			BreastMorphActivity = FMath::Max(
				BreastMorphActivity,
				FMath::Abs(MorphCurve.Value));
		}
		if (LowerBodyMorphGuardExactBodyMorphNames.Contains(MorphCurve.Key))
		{
			// Lower-body fitting is expansion-only. Negative corrective/JCM values
			// must never pull a garment farther into the body.
			LowerBodyMorphActivity = FMath::Max(
				LowerBodyMorphActivity,
				FMath::Max(MorphCurve.Value, 0.0f));
		}
	}
	LastBodyMorphActivity = FMath::Clamp(BodyMorphActivity, 0.0f, 1.0f);
	LastBreastMorphActivity = FMath::Clamp(BreastMorphActivity, 0.0f, 1.0f);
	LastLowerBodyMorphActivity = LowerBodyMorphActivity > 1.0e-4f
		? FMath::Clamp(LowerBodyMorphActivity, 0.0f, 1.0f)
		: 0.0f;

	const bool bParametersAccepted =
		Instance->SetTransformVariable(
			EFClothingSurfaceGraphContract::BodyToGarmentTransform,
			BodyToGarment)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::GlobalClearanceOffsetCm,
			FMath::Clamp(
				FMath::IsFinite(GlobalClearanceOffsetCm) ? GlobalClearanceOffsetCm : 0.0f,
				0.0f,
				EFClothingMorphV4::MaximumRuntimeClearanceCm))
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::GarmentClearanceOffsetCm,
			FMath::Clamp(
				FMath::IsFinite(GarmentClearanceOffsetCm) ? GarmentClearanceOffsetCm : 0.0f,
				0.0f,
				EFClothingMorphV4::MaximumRuntimeClearanceCm))
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::GarmentInflateCm,
			FMath::Clamp(
				FMath::IsFinite(GarmentInflateCm)
					? GarmentInflateCm
					: 0.0f,
				0.0f,
				EFClothingMorphV4::MaximumRuntimeInflateCm))
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::MaximumCorrectionOverrideCm,
			FMath::IsFinite(MaximumCorrectionOverrideCm) ? MaximumCorrectionOverrideCm : -1.0f)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::MaximumAutomaticBodyShapeTravelCm,
			EFClothingMorphV4::MaximumAutomaticBodyShapeTravelCm)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::BodyMorphActivity,
			LastBodyMorphActivity)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::BreastMorphActivity,
			LastBreastMorphActivity)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::UnderBreastClearanceMaxCm,
			EFClothingMorphV4::MaximumAutomaticUnderBreastClearanceCm)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::LowerBodyMorphActivity,
			LastLowerBodyMorphActivity)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::LowerBodyMorphClearanceMaxCm,
			LowerBodyMorphGuardMaximumClearanceCm)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::DeltaTimeSeconds,
			FMath::Max(FMath::IsFinite(DeltaTimeSeconds) ? DeltaTimeSeconds : 0.0f, 0.0f));
	if (!bParametersAccepted)
	{
		OutFailureReason = TEXT("Surface graph is missing one or more EF runtime scalar variables.");
		return false;
	}

	UMeshDeformerInstance::FEnqueueWorkDesc Desc;
	Desc.Scene = Garment->GetScene();
	Desc.ExecutionGroup = UMeshDeformerInstance::ExecutionGroup_BeginInitViews;
	Desc.OwnerName = Garment->GetSkeletalMeshAsset()
		? Garment->GetSkeletalMeshAsset()->GetFName()
		: Garment->GetFName();
	const TSharedPtr<FDispatchTelemetry, ESPMode::ThreadSafe> Telemetry = DispatchTelemetry;
	const uint64 SubmissionOrdinal = EnqueuedFrameCount + 1;
	Desc.FallbackDelegate.BindLambda([Telemetry, SubmissionOrdinal]()
	{
		if (Telemetry.IsValid() && !Telemetry->bCancelled.Load())
		{
			++Telemetry->DispatchFailureCount;
			if (Telemetry->LastRenderEnqueueMarkerSubmission.Load() >= SubmissionOrdinal)
			{
				++Telemetry->RenderValidationFallbackCount;
			}
			else
			{
				++Telemetry->ImmediateEnqueueFallbackCount;
			}
		}
	});

	// UOptimusDeformerInstance narrows the override to protected, while the base
	// contract intentionally exposes EnqueueWork for composition.
	UMeshDeformerInstance* MeshDeformerInstance = Instance;
	MeshDeformerInstance->EnqueueWork(Desc);
	ENQUEUE_RENDER_COMMAND(EFClothingMarkSurfaceEnqueueStage)(
		[Telemetry, SubmissionOrdinal](FRHICommandListImmediate& RHICmdList)
		{
			if (Telemetry.IsValid() && !Telemetry->bCancelled.Load())
			{
				Telemetry->LastRenderEnqueueMarkerSubmission.Store(SubmissionOrdinal);
			}
		});
	++EnqueuedFrameCount;
	if (bAwaitingDispatchRecovery)
	{
		++DispatchRecoverySubmissionCount;
	}

	// ComputeFramework exposes failure but no success callback. Arm a one-shot
	// render-thread acknowledgement only for the first candidate submission. Its
	// fallback delegate runs during SubmitWork before EndFrameRT, so an unchanged
	// failure count at the second EndFrameRT is the strongest non-blocking public
	// confirmation that the surface kernel was validated and submitted.
	if (!DispatchTelemetry->bRenderConfirmationArmed.Load())
	{
		DispatchTelemetry->bRenderConfirmationArmed.Store(true);
		const uint32 FailureCountBeforeSubmission = FailureCount;
		const TSharedPtr<FDispatchTelemetry, ESPMode::ThreadSafe> ConfirmationTelemetry =
			DispatchTelemetry;
		const TSharedRef<FRenderSubmissionConfirmationArm, ESPMode::ThreadSafe> ConfirmationArm =
			MakeShared<FRenderSubmissionConfirmationArm, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(EFClothingArmSurfaceSubmissionConfirmation)(
			[ConfirmationTelemetry, ConfirmationArm, FailureCountBeforeSubmission](
				FRHICommandListImmediate& RHICmdList)
			{
				if (!ConfirmationTelemetry.IsValid()
					|| ConfirmationTelemetry->bCancelled.Load())
				{
					if (ConfirmationTelemetry.IsValid())
					{
						ConfirmationTelemetry->bRenderConfirmationArmed.Store(false);
					}
					return;
				}

				ConfirmationArm->EndFrameDelegateHandle = FCoreDelegates::OnEndFrameRT.AddLambda(
					[ConfirmationTelemetry, ConfirmationArm, FailureCountBeforeSubmission]()
					{
						check(IsInRenderingThread());
						--ConfirmationArm->RemainingEndFrameCallbacks;
						if (ConfirmationArm->RemainingEndFrameCallbacks > 0)
						{
							return;
						}

						FCoreDelegates::OnEndFrameRT.Remove(
							ConfirmationArm->EndFrameDelegateHandle);
						if (!ConfirmationTelemetry->bCancelled.Load()
							&& ConfirmationTelemetry->DispatchFailureCount.Load()
								== FailureCountBeforeSubmission)
						{
							ConfirmationTelemetry->LastValidatedFailureCount.Store(
								FailureCountBeforeSubmission);
							++ConfirmationTelemetry->RenderValidatedSubmissionCount;
						}
						ConfirmationTelemetry->bRenderConfirmationArmed.Store(false);
					});
			});
	}
	return true;
}

void UEFClothingSurfaceDeformerProducer::Detach()
{
	check(IsInGameThread());
	if (DispatchTelemetry.IsValid())
	{
		DispatchTelemetry->bCancelled.Store(true);
	}
	if (UOptimusDeformerInstance* Instance = SurfaceInstance.Get())
	{
		Instance->SetCanBeActive(false);
	}
	if (bRegisteredWithManager)
	{
		if (UOptimusDeformerDynamicInstanceManager* Manager = DynamicManager.Get())
		{
			Manager->OnObjectBeginDestroy(this);
		}
	}

	bRegisteredWithManager = false;
	SurfaceInstance.Reset();
	DynamicManager.Reset();
	InstanceSettings = nullptr;
	GarmentComponent.Reset();
	BodyComponent.Reset();
	GarmentLODIndex = INDEX_NONE;
	BodyLODIndex = INDEX_NONE;
	InstanceGuid.Invalidate();
	DispatchTelemetry.Reset();
	LastObservedDispatchFailureCount = 0;
	DispatchRecoverySubmissionCount = 0;
	DispatchRecoveryElapsedSeconds = 0.0;
	bAwaitingDispatchRecovery = false;
	bDispatchRecoveryStartedAfterReady = false;
	EnqueuedFrameCount = 0;
	bRenderPreflightEnqueued = false;
	LastBodyMorphActivity = 0.0f;
	LastBreastMorphActivity = 0.0f;
	LastLowerBodyMorphActivity = 0.0f;
	LowerBodyMorphGuardExactBodyMorphNames.Reset();
	LowerBodyMorphGuardMaximumClearanceCm = 0.0f;
	LowerBodyMorphGuardVertexCount = 0;
}

bool UEFClothingSurfaceDeformerProducer::IsInstalledFor(
	const USkeletalMeshComponent* InGarmentComponent,
	const USkeletalMeshComponent* InBodyComponent,
	const int32 InGarmentLODIndex,
	const int32 InBodyLODIndex) const
{
	return bRegisteredWithManager
		&& GarmentComponent.Get() == InGarmentComponent
		&& BodyComponent.Get() == InBodyComponent
		&& GarmentLODIndex == InGarmentLODIndex
		&& BodyLODIndex == InBodyLODIndex
		&& DynamicManager.IsValid()
		&& SurfaceInstance.IsValid();
}

uint32 UEFClothingSurfaceDeformerProducer::GetDispatchFailureCount() const
{
	return DispatchTelemetry.IsValid()
		? DispatchTelemetry->DispatchFailureCount.Load()
		: LastObservedDispatchFailureCount;
}

uint32 UEFClothingSurfaceDeformerProducer::GetImmediateEnqueueFallbackCount() const
{
	return DispatchTelemetry.IsValid()
		? DispatchTelemetry->ImmediateEnqueueFallbackCount.Load()
		: 0u;
}

uint32 UEFClothingSurfaceDeformerProducer::GetRenderValidationFallbackCount() const
{
	return DispatchTelemetry.IsValid()
		? DispatchTelemetry->RenderValidationFallbackCount.Load()
		: 0u;
}

FString UEFClothingSurfaceDeformerProducer::GetRenderPreflightSummary() const
{
	if (!DispatchTelemetry.IsValid())
	{
		return TEXT("unavailable");
	}
	return FString::Printf(
		TEXT("mask=0x%03x garmentLOD=%d garmentSections=%d bodyLOD=%d bodySections=%d"),
		DispatchTelemetry->RenderPreflightMask.Load(),
		DispatchTelemetry->GarmentActualLOD.Load(),
		DispatchTelemetry->GarmentActualSectionCount.Load(),
		DispatchTelemetry->BodyActualLOD.Load(),
		DispatchTelemetry->BodyActualSectionCount.Load());
}

uint64 UEFClothingSurfaceDeformerProducer::GetRenderValidatedSubmissionCount() const
{
	return DispatchTelemetry.IsValid()
		? DispatchTelemetry->RenderValidatedSubmissionCount.Load()
		: 0u;
}

bool UEFClothingSurfaceDeformerProducer::HasRenderValidatedSubmission() const
{
	return DispatchTelemetry.IsValid()
		&& DispatchTelemetry->RenderValidatedSubmissionCount.Load() > 0
		&& DispatchTelemetry->LastValidatedFailureCount.Load()
			== DispatchTelemetry->DispatchFailureCount.Load();
}

void UEFClothingSurfaceDeformerProducer::BeginDestroy()
{
	if (DispatchTelemetry.IsValid())
	{
		DispatchTelemetry->bCancelled.Store(true);
	}
	if (UOptimusDeformerInstance* Instance = SurfaceInstance.Get())
	{
		Instance->SetCanBeActive(false);
	}
	if (bRegisteredWithManager)
	{
		BeginDestroyEvent.Broadcast(this);
	}
	BeginDestroyEvent.Clear();
	bRegisteredWithManager = false;
	Super::BeginDestroy();
}

UActorComponent* UEFClothingSurfaceDeformerProducer::ResolveComponentBinding(
	const FName BindingName) const
{
	return BindingName == EFClothingSurfaceGraphContract::BodyBinding
		? BodyComponent.Get()
		: nullptr;
}

bool UEFClothingSurfaceDeformerProducer::UploadImmutableBinding(
	const FEFClothingSurfaceLODPairBinding& InLODPair,
	FString& OutFailureReason)
{
	UOptimusDeformerInstance* Instance = SurfaceInstance.Get();
	if (!IsValid(Instance))
	{
		OutFailureReason = TEXT("Surface instance disappeared before binding upload.");
		return false;
	}

	TArray<FIntVector4> BodyTriangleAndMode;
	TArray<FVector4> BarycentricsAndFollowWeight;
	TArray<FVector4> RestOffsetAndClearanceCm;
	TArray<FVector2D> MaximumCorrectionAndRestGapCm;
	TArray<FIntPoint> ThicknessReferenceAndLayer;
	TArray<FIntPoint> NeighborRanges;
	TArray<int32> NeighborIndices;
	TArray<FIntPoint> WeldRanges;
	TArray<int32> WeldIndices;
	TArray<double> LocalFeatureSizeCm;
	TArray<double> UnderBreastGuardWeights;
	TArray<double> LowerBodyMorphGuardWeights;
	TArray<FIntPoint> WitnessRanges;
	TArray<int32> WitnessIndices;
	TArray<FIntVector4> WitnessGarmentVertices;
	TArray<FVector4> WitnessGarmentBarycentricsAndClearanceCm;
	TArray<FIntVector4> WitnessBodyVertices;
	TArray<FVector4> WitnessBodyBarycentricsAndMaximumCorrectionCm;
	const int32 VertexCount = InLODPair.VertexBindings.Num();
	BodyTriangleAndMode.Reserve(VertexCount);
	BarycentricsAndFollowWeight.Reserve(VertexCount);
	RestOffsetAndClearanceCm.Reserve(VertexCount);
	MaximumCorrectionAndRestGapCm.Reserve(VertexCount);
	ThicknessReferenceAndLayer.Reserve(VertexCount);
	NeighborRanges.Reserve(VertexCount);
	WeldRanges.Reserve(VertexCount);
	LocalFeatureSizeCm.Reserve(VertexCount);
	UnderBreastGuardWeights.Reserve(VertexCount);
	LowerBodyMorphGuardWeights.Reserve(VertexCount);
	const int32 NeighborReferenceCount = InLODPair.NeighborRenderVertexIndices.Num();
	int32 ObservedLowerBodyMorphGuardVertexCount = 0;
	NeighborIndices = InLODPair.NeighborRenderVertexIndices;
	if (InLODPair.Metrics.NeighborReferenceCount != NeighborReferenceCount)
	{
		OutFailureReason = TEXT("Surface binding neighbor-reference evidence does not match its adjacency pool.");
		return false;
	}

	int32 ExpectedNeighborOffset = 0;
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FEFClothingSurfaceVertexBinding& Binding = InLODPair.VertexBindings[VertexIndex];
		if (!FMath::IsFinite(Binding.LowerBodyMorphGuardWeight)
			|| Binding.LowerBodyMorphGuardWeight < 0.0f
			|| Binding.LowerBodyMorphGuardWeight > 1.0f)
		{
			OutFailureReason = FString::Printf(
				TEXT("Surface binding vertex %d has an invalid lower-body morph guard weight."),
				VertexIndex);
			return false;
		}
		ObservedLowerBodyMorphGuardVertexCount += Binding.LowerBodyMorphGuardWeight > 1.0e-4f ? 1 : 0;
		if (Binding.NeighborRange.Offset != ExpectedNeighborOffset
			|| Binding.NeighborRange.Count < 0
			|| Binding.NeighborRange.Count > 256
			|| Binding.NeighborRange.Offset < 0
			|| Binding.NeighborRange.Offset > NeighborReferenceCount
			|| Binding.NeighborRange.Count
				> NeighborReferenceCount - Binding.NeighborRange.Offset)
		{
			OutFailureReason = FString::Printf(
				TEXT("Surface binding vertex %d has an invalid or non-contiguous neighbor range."),
				VertexIndex);
			return false;
		}
		for (int32 LocalNeighborIndex = 0;
			LocalNeighborIndex < Binding.NeighborRange.Count;
			++LocalNeighborIndex)
		{
			const int32 NeighborVertexIndex = NeighborIndices[
				Binding.NeighborRange.Offset + LocalNeighborIndex];
			if (NeighborVertexIndex < 0
				|| NeighborVertexIndex >= VertexCount
				|| NeighborVertexIndex == VertexIndex)
			{
				OutFailureReason = FString::Printf(
					TEXT("Surface binding vertex %d has invalid neighbor %d."),
					VertexIndex,
					NeighborVertexIndex);
				return false;
			}
		}
		NeighborRanges.Emplace(Binding.NeighborRange.Offset, Binding.NeighborRange.Count);
		ExpectedNeighborOffset += Binding.NeighborRange.Count;
		WeldRanges.Emplace(Binding.WeldRange.Offset, Binding.WeldRange.Count);
		LocalFeatureSizeCm.Add(static_cast<double>(Binding.LocalFeatureSizeCm));
		UnderBreastGuardWeights.Add(static_cast<double>(Binding.UnderBreastGuardWeight));
		LowerBodyMorphGuardWeights.Add(static_cast<double>(Binding.LowerBodyMorphGuardWeight));

		BodyTriangleAndMode.Emplace(
			Binding.BodyRenderVertexIndices.X,
			Binding.BodyRenderVertexIndices.Y,
			Binding.BodyRenderVertexIndices.Z,
			static_cast<int32>(Binding.Mode));
		BarycentricsAndFollowWeight.Emplace(
			Binding.BodyBarycentrics.X,
			Binding.BodyBarycentrics.Y,
			Binding.BodyBarycentrics.Z,
			Binding.FollowWeight);
		RestOffsetAndClearanceCm.Emplace(
			Binding.RestTangentFrameOffsetCm.X,
			Binding.RestTangentFrameOffsetCm.Y,
			Binding.RestTangentFrameOffsetCm.Z,
			Binding.TargetClearanceCm);
		MaximumCorrectionAndRestGapCm.Emplace(
			Binding.MaximumCorrectionCm,
			Binding.RestSignedGapCm);
		ThicknessReferenceAndLayer.Emplace(
			Binding.ThicknessReferenceRenderVertexIndex,
			Binding.bOuterThicknessLayer ? 1 : 0);
	}
	if (ExpectedNeighborOffset != NeighborReferenceCount)
	{
		OutFailureReason = TEXT("Surface binding neighbor ranges do not cover the complete adjacency pool.");
		return false;
	}
	if (InLODPair.Metrics.LowerBodyMorphGuardVertexCount != ObservedLowerBodyMorphGuardVertexCount)
	{
		OutFailureReason = TEXT("Surface binding lower-body morph guard evidence is stale.");
		return false;
	}
	if (!ValidateWeldContract(InLODPair, VertexCount, OutFailureReason))
	{
		return false;
	}
	const int32 WeldReferenceCount = InLODPair.WeldRenderVertexIndices.Num();
	WeldIndices = InLODPair.WeldRenderVertexIndices;
	TArray<int32> WitnessReferenceCounts;
	WitnessReferenceCounts.Init(0, VertexCount);
	WitnessGarmentVertices.Reserve(InLODPair.Witnesses.Num());
	WitnessGarmentBarycentricsAndClearanceCm.Reserve(InLODPair.Witnesses.Num());
	WitnessBodyVertices.Reserve(InLODPair.Witnesses.Num());
	WitnessBodyBarycentricsAndMaximumCorrectionCm.Reserve(InLODPair.Witnesses.Num());
	constexpr float WitnessIncidenceWeightEpsilon = 1.0e-6f;
	for (const FEFClothingSurfaceWitness& Witness : InLODPair.Witnesses)
	{
		const int32 GarmentVertices[3] =
		{
			Witness.GarmentRenderVertexIndices.X,
			Witness.GarmentRenderVertexIndices.Y,
			Witness.GarmentRenderVertexIndices.Z
		};
		const float GarmentBarycentrics[3] =
		{
			Witness.GarmentBarycentrics.X,
			Witness.GarmentBarycentrics.Y,
			Witness.GarmentBarycentrics.Z
		};
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			if (GarmentBarycentrics[CornerIndex] <= WitnessIncidenceWeightEpsilon)
			{
				continue;
			}
			const int32 GarmentVertex = GarmentVertices[CornerIndex];
			if (!WitnessReferenceCounts.IsValidIndex(GarmentVertex))
			{
				OutFailureReason = TEXT("Witness incidence construction encountered an invalid garment vertex.");
				return false;
			}
			++WitnessReferenceCounts[GarmentVertex];
			if (WitnessReferenceCounts[GarmentVertex] > MaximumWitnessReferencesPerVertex)
			{
				OutFailureReason = FString::Printf(
					TEXT("Garment vertex %d has %d witness references; the certified maximum is %d."),
					GarmentVertex,
					WitnessReferenceCounts[GarmentVertex],
					MaximumWitnessReferencesPerVertex);
				return false;
			}
		}
		WitnessGarmentVertices.Emplace(
			GarmentVertices[0],
			GarmentVertices[1],
			GarmentVertices[2],
			Witness.GarmentTriangleIndex);
		WitnessGarmentBarycentricsAndClearanceCm.Emplace(
			Witness.GarmentBarycentrics.X,
			Witness.GarmentBarycentrics.Y,
			Witness.GarmentBarycentrics.Z,
			Witness.TargetClearanceCm);
		WitnessBodyVertices.Emplace(
			Witness.BodyRenderVertexIndices.X,
			Witness.BodyRenderVertexIndices.Y,
			Witness.BodyRenderVertexIndices.Z,
			0);
		WitnessBodyBarycentricsAndMaximumCorrectionCm.Emplace(
			Witness.BodyBarycentrics.X,
			Witness.BodyBarycentrics.Y,
			Witness.BodyBarycentrics.Z,
			Witness.MaximumCorrectionCm);
	}

	WitnessRanges.SetNum(VertexCount);
	int64 TotalWitnessReferenceCount64 = 0;
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const int32 ReferenceCount = WitnessReferenceCounts[VertexIndex];
		if (TotalWitnessReferenceCount64 > MAX_int32 - ReferenceCount)
		{
			OutFailureReason = TEXT("Witness incidence pool exceeds the supported 32-bit range.");
			return false;
		}
		WitnessRanges[VertexIndex] = FIntPoint(
			static_cast<int32>(TotalWitnessReferenceCount64),
			ReferenceCount);
		TotalWitnessReferenceCount64 += ReferenceCount;
	}
	const int32 TotalWitnessReferenceCount = static_cast<int32>(TotalWitnessReferenceCount64);
	WitnessIndices.Init(INDEX_NONE, TotalWitnessReferenceCount);
	TArray<int32> WitnessWriteCursors;
	WitnessWriteCursors.SetNumZeroed(VertexCount);
	for (int32 WitnessIndex = 0; WitnessIndex < InLODPair.Witnesses.Num(); ++WitnessIndex)
	{
		const FEFClothingSurfaceWitness& Witness = InLODPair.Witnesses[WitnessIndex];
		const FIntVector GarmentVertices = Witness.GarmentRenderVertexIndices;
		const int32 GarmentVertexIndices[3] =
		{
			GarmentVertices.X,
			GarmentVertices.Y,
			GarmentVertices.Z
		};
		const float GarmentBarycentrics[3] =
		{
			Witness.GarmentBarycentrics.X,
			Witness.GarmentBarycentrics.Y,
			Witness.GarmentBarycentrics.Z
		};
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			if (GarmentBarycentrics[CornerIndex] <= WitnessIncidenceWeightEpsilon)
			{
				continue;
			}
			const int32 GarmentVertexIndex = GarmentVertexIndices[CornerIndex];
			const int32 DestinationIndex = WitnessRanges[GarmentVertexIndex].X
				+ WitnessWriteCursors[GarmentVertexIndex]++;
			WitnessIndices[DestinationIndex] = WitnessIndex;
		}
	}
	if (WitnessIndices.Contains(INDEX_NONE))
	{
		OutFailureReason = TEXT("Witness incidence construction left an uninitialized reference.");
		return false;
	}

	const bool bUploadAccepted =
		Instance->SetIntVariable(EFClothingSurfaceGraphContract::BindingVertexCount, VertexCount)
		&& Instance->SetIntVariable(
			EFClothingSurfaceGraphContract::BodyVertexCount,
			InLODPair.BodyTopology.RenderVertexCount)
		&& Instance->SetIntVariable(EFClothingSurfaceGraphContract::GarmentLOD, GarmentLODIndex)
		&& Instance->SetIntVariable(EFClothingSurfaceGraphContract::BodyLOD, BodyLODIndex)
		&& Instance->SetInt4ArrayVariable(
			EFClothingSurfaceGraphContract::BodyTriangleAndMode,
			BodyTriangleAndMode)
		&& Instance->SetVector4ArrayVariable(
			EFClothingSurfaceGraphContract::BarycentricsAndFollowWeight,
			BarycentricsAndFollowWeight)
		&& Instance->SetVector4ArrayVariable(
			EFClothingSurfaceGraphContract::RestOffsetAndClearanceCm,
			RestOffsetAndClearanceCm)
		&& Instance->SetVector2ArrayVariable(
			EFClothingSurfaceGraphContract::MaximumCorrectionAndRestGapCm,
			MaximumCorrectionAndRestGapCm)
		&& Instance->SetInt2ArrayVariable(
			EFClothingSurfaceGraphContract::ThicknessReferenceAndLayer,
			ThicknessReferenceAndLayer)
		&& Instance->SetIntVariable(
			EFClothingSurfaceGraphContract::NeighborReferenceCount,
			NeighborReferenceCount)
		&& Instance->SetInt2ArrayVariable(
			EFClothingSurfaceGraphContract::NeighborRanges,
			NeighborRanges)
		&& Instance->SetIntArrayVariable(
			EFClothingSurfaceGraphContract::NeighborIndices,
			NeighborIndices)
		&& Instance->SetIntVariable(
			EFClothingSurfaceGraphContract::WeldReferenceCount,
			WeldReferenceCount)
		&& Instance->SetInt2ArrayVariable(
			EFClothingSurfaceGraphContract::WeldRanges,
			WeldRanges)
		&& Instance->SetIntArrayVariable(
			EFClothingSurfaceGraphContract::WeldIndices,
			WeldIndices)
		&& Instance->SetFloatArrayVariable(
			EFClothingSurfaceGraphContract::LocalFeatureSizeCm,
			LocalFeatureSizeCm)
		&& Instance->SetFloatArrayVariable(
			EFClothingSurfaceGraphContract::UnderBreastGuardWeights,
			UnderBreastGuardWeights)
		&& Instance->SetFloatArrayVariable(
			EFClothingSurfaceGraphContract::LowerBodyMorphGuardWeights,
			LowerBodyMorphGuardWeights)
		&& Instance->SetIntVariable(
			EFClothingSurfaceGraphContract::WitnessCount,
			InLODPair.Witnesses.Num())
		&& Instance->SetIntVariable(
			EFClothingSurfaceGraphContract::WitnessReferenceCount,
			TotalWitnessReferenceCount)
		&& Instance->SetInt2ArrayVariable(
			EFClothingSurfaceGraphContract::WitnessRanges,
			WitnessRanges)
		&& Instance->SetIntArrayVariable(
			EFClothingSurfaceGraphContract::WitnessIndices,
			WitnessIndices)
		&& Instance->SetInt4ArrayVariable(
			EFClothingSurfaceGraphContract::WitnessGarmentVertices,
			WitnessGarmentVertices)
		&& Instance->SetVector4ArrayVariable(
			EFClothingSurfaceGraphContract::WitnessGarmentBarycentricsAndClearanceCm,
			WitnessGarmentBarycentricsAndClearanceCm)
		&& Instance->SetInt4ArrayVariable(
			EFClothingSurfaceGraphContract::WitnessBodyVertices,
			WitnessBodyVertices)
		&& Instance->SetVector4ArrayVariable(
			EFClothingSurfaceGraphContract::WitnessBodyBarycentricsAndMaximumCorrectionCm,
			WitnessBodyBarycentricsAndMaximumCorrectionCm);
	if (!bUploadAccepted)
	{
		OutFailureReason = TEXT("Surface graph variable schema does not match the EF V4 binding upload contract.");
		return false;
	}

	return true;
}

bool UEFClothingSurfaceDeformerProducer::ValidateLiveLODTopology(
	const FEFClothingSurfaceLODPairBinding& InLODPair,
	FString& OutFailureReason) const
{
	const USkeletalMeshComponent* Garment = GarmentComponent.Get();
	const USkeletalMeshComponent* Body = BodyComponent.Get();
	const USkeletalMesh* GarmentMesh = IsValid(Garment) ? Garment->GetSkeletalMeshAsset() : nullptr;
	const USkeletalMesh* BodyMesh = IsValid(Body) ? Body->GetSkeletalMeshAsset() : nullptr;
	const FSkeletalMeshRenderData* GarmentRenderData = IsValid(GarmentMesh)
		? GarmentMesh->GetResourceForRendering()
		: nullptr;
	const FSkeletalMeshRenderData* BodyRenderData = IsValid(BodyMesh)
		? BodyMesh->GetResourceForRendering()
		: nullptr;
	if (!GarmentRenderData
		|| !BodyRenderData
		|| !GarmentRenderData->LODRenderData.IsValidIndex(GarmentLODIndex)
		|| !BodyRenderData->LODRenderData.IsValidIndex(BodyLODIndex))
	{
		OutFailureReason = TEXT("Surface binding references unavailable cooked render LOD data.");
		return false;
	}

	const FSkeletalMeshLODRenderData& GarmentLOD = GarmentRenderData->LODRenderData[GarmentLODIndex];
	const FSkeletalMeshLODRenderData& BodyLOD = BodyRenderData->LODRenderData[BodyLODIndex];
	const int32 GarmentVertexCount = GarmentLOD.GetNumVertices();
	const int32 BodyVertexCount = BodyLOD.GetNumVertices();
	const int32 GarmentIndexCount = GarmentLOD.MultiSizeIndexContainer.IsIndexBufferValid()
		? GarmentLOD.MultiSizeIndexContainer.GetIndexBuffer()->Num()
		: 0;
	const int32 BodyIndexCount = BodyLOD.MultiSizeIndexContainer.IsIndexBufferValid()
		? BodyLOD.MultiSizeIndexContainer.GetIndexBuffer()->Num()
		: 0;
	const bool bTopologyMetadataMatches =
		!InLODPair.GarmentTopology.TopologyFingerprint.IsEmpty()
		&& !InLODPair.GarmentTopology.ContentFingerprint.IsEmpty()
		&& !InLODPair.BodyTopology.TopologyFingerprint.IsEmpty()
		&& !InLODPair.BodyTopology.ContentFingerprint.IsEmpty()
		&& InLODPair.GarmentTopology.RenderIndexCount == GarmentIndexCount
		&& InLODPair.GarmentTopology.TriangleCount == GarmentIndexCount / 3
		&& InLODPair.GarmentTopology.SectionCount == GarmentLOD.RenderSections.Num()
		&& InLODPair.BodyTopology.RenderIndexCount == BodyIndexCount
		&& InLODPair.BodyTopology.TriangleCount == BodyIndexCount / 3
		&& InLODPair.BodyTopology.SectionCount == BodyLOD.RenderSections.Num();
	if (!InLODPair.bCertified
		|| !bTopologyMetadataMatches
		|| InLODPair.Metrics.InvalidAnchorCount != 0
		|| InLODPair.Metrics.DegenerateBodyTriangleCount != 0
		|| !FMath::IsFinite(InLODPair.Metrics.MinimumRestSignedGapCm)
		|| InLODPair.Metrics.MinimumRestSignedGapCm < -0.02f
		|| !FMath::IsFinite(InLODPair.Metrics.MaximumInitialCorrectionCm)
		|| InLODPair.Metrics.MaximumInitialCorrectionCm < 0.0f
		|| InLODPair.Metrics.SurfaceFollowVertexCount
			+ InLODPair.Metrics.HybridVertexCount
			+ InLODPair.Metrics.CollisionOnlyVertexCount
			+ InLODPair.Metrics.PreserveUpstreamVertexCount != GarmentVertexCount
		|| InLODPair.Metrics.ExcludedPreserveUpstreamGarmentTriangleCount < 0
		|| InLODPair.Metrics.NeighborReferenceCount
			!= InLODPair.NeighborRenderVertexIndices.Num()
		|| InLODPair.Metrics.WeldReferenceCount
			!= InLODPair.WeldRenderVertexIndices.Num()
		|| InLODPair.Metrics.WeldGroupCount < 0
		|| InLODPair.Metrics.CandidateTriangleCount != InLODPair.CandidateTriangles.Num()
		|| InLODPair.Metrics.WitnessCount != InLODPair.Witnesses.Num()
		|| InLODPair.VertexBindings.Num() != GarmentVertexCount
		|| InLODPair.Metrics.BoundRenderVertexCount != GarmentVertexCount
		|| InLODPair.GarmentTopology.RenderVertexCount != GarmentVertexCount
		|| InLODPair.BodyTopology.RenderVertexCount != BodyVertexCount)
	{
		OutFailureReason = TEXT("Surface binding certification or live render-vertex counts do not match.");
		return false;
	}
	for (const int32 NeighborVertexIndex : InLODPair.NeighborRenderVertexIndices)
	{
		if (NeighborVertexIndex < 0 || NeighborVertexIndex >= GarmentVertexCount)
		{
			OutFailureReason = TEXT("Surface binding contains an invalid garment-neighbor render index.");
			return false;
		}
	}
	for (const FEFClothingSurfaceCandidateTriangle& Candidate : InLODPair.CandidateTriangles)
	{
		if (Candidate.BodyTriangleIndex < 0
			|| Candidate.BodyTriangleIndex >= BodyIndexCount / 3
			|| Candidate.BodyRenderVertexIndices.X < 0
			|| Candidate.BodyRenderVertexIndices.Y < 0
			|| Candidate.BodyRenderVertexIndices.Z < 0
			|| Candidate.BodyRenderVertexIndices.X >= BodyVertexCount
			|| Candidate.BodyRenderVertexIndices.Y >= BodyVertexCount
			|| Candidate.BodyRenderVertexIndices.Z >= BodyVertexCount
			|| !FMath::IsFinite(Candidate.RestDistanceCm)
			|| Candidate.RestDistanceCm < 0.0f)
		{
			OutFailureReason = TEXT("Surface binding contains an invalid fallback body-triangle candidate.");
			return false;
		}
	}
	for (const FEFClothingSurfaceWitness& Witness : InLODPair.Witnesses)
	{
		const float GarmentBarycentricSum = Witness.GarmentBarycentrics.X
			+ Witness.GarmentBarycentrics.Y
			+ Witness.GarmentBarycentrics.Z;
		const float BodyBarycentricSum = Witness.BodyBarycentrics.X
			+ Witness.BodyBarycentrics.Y
			+ Witness.BodyBarycentrics.Z;
		if (Witness.GarmentTriangleIndex < 0
			|| Witness.GarmentTriangleIndex >= GarmentIndexCount / 3
			|| Witness.GarmentRenderVertexIndices.X < 0
			|| Witness.GarmentRenderVertexIndices.Y < 0
			|| Witness.GarmentRenderVertexIndices.Z < 0
			|| Witness.GarmentRenderVertexIndices.X >= GarmentVertexCount
			|| Witness.GarmentRenderVertexIndices.Y >= GarmentVertexCount
			|| Witness.GarmentRenderVertexIndices.Z >= GarmentVertexCount
			|| Witness.BodyRenderVertexIndices.X < 0
			|| Witness.BodyRenderVertexIndices.Y < 0
			|| Witness.BodyRenderVertexIndices.Z < 0
			|| Witness.BodyRenderVertexIndices.X >= BodyVertexCount
			|| Witness.BodyRenderVertexIndices.Y >= BodyVertexCount
			|| Witness.BodyRenderVertexIndices.Z >= BodyVertexCount
			|| !IsFiniteVector3f(Witness.GarmentBarycentrics)
			|| !IsFiniteVector3f(Witness.BodyBarycentrics)
			|| !FMath::IsNearlyEqual(GarmentBarycentricSum, 1.0f, 1.0e-3f)
			|| !FMath::IsNearlyEqual(BodyBarycentricSum, 1.0f, 1.0e-3f)
			|| !FMath::IsFinite(Witness.TargetClearanceCm)
			|| Witness.TargetClearanceCm < 0.0f
			|| !FMath::IsFinite(Witness.MaximumCorrectionCm)
			|| Witness.MaximumCorrectionCm <= 0.0f)
		{
			OutFailureReason = TEXT("Surface binding contains an invalid edge/face witness.");
			return false;
		}
		if (InLODPair.VertexBindings[Witness.GarmentRenderVertexIndices.X].Mode
				== EEFClothingSurfaceVertexMode::PreserveUpstream
			|| InLODPair.VertexBindings[Witness.GarmentRenderVertexIndices.Y].Mode
				== EEFClothingSurfaceVertexMode::PreserveUpstream
			|| InLODPair.VertexBindings[Witness.GarmentRenderVertexIndices.Z].Mode
				== EEFClothingSurfaceVertexMode::PreserveUpstream)
		{
			OutFailureReason = TEXT("Surface binding witness touches a PreserveUpstream triangle.");
			return false;
		}
	}

	float RecomputedMaximumInitialCorrectionCm = 0.0f;
	int32 RecomputedUnderBreastGuardVertexCount = 0;
	for (int32 VertexIndex = 0; VertexIndex < InLODPair.VertexBindings.Num(); ++VertexIndex)
	{
		const FEFClothingSurfaceVertexBinding& Binding = InLODPair.VertexBindings[VertexIndex];
		if (Binding.UnderBreastGuardWeight > 1.0e-4f)
		{
			++RecomputedUnderBreastGuardVertexCount;
		}
		const bool bThicknessReferenceValid =
			Binding.ThicknessReferenceRenderVertexIndex == INDEX_NONE
				? !Binding.bOuterThicknessLayer
				: InLODPair.VertexBindings.IsValidIndex(
					Binding.ThicknessReferenceRenderVertexIndex)
					&& !InLODPair.VertexBindings[
						Binding.ThicknessReferenceRenderVertexIndex].bOuterThicknessLayer;
		const bool bIndicesValid = Binding.GarmentRenderVertexIndex == VertexIndex
			&& bThicknessReferenceValid
			&& Binding.BodyRenderVertexIndices.X >= 0
			&& Binding.BodyRenderVertexIndices.Y >= 0
			&& Binding.BodyRenderVertexIndices.Z >= 0
			&& Binding.BodyRenderVertexIndices.X < BodyVertexCount
			&& Binding.BodyRenderVertexIndices.Y < BodyVertexCount
			&& Binding.BodyRenderVertexIndices.Z < BodyVertexCount;
		const float BarycentricSum = Binding.BodyBarycentrics.X
			+ Binding.BodyBarycentrics.Y
			+ Binding.BodyBarycentrics.Z;
		const bool bRangesValid = Binding.NeighborRange.Offset >= 0
			&& Binding.NeighborRange.Count >= 0
			&& Binding.NeighborRange.Count <= 256
			&& Binding.NeighborRange.Offset <= InLODPair.NeighborRenderVertexIndices.Num()
			&& Binding.NeighborRange.Count
				<= InLODPair.NeighborRenderVertexIndices.Num() - Binding.NeighborRange.Offset
			&& Binding.CandidateRange.Offset >= 0
			&& Binding.CandidateRange.Count >= 0
			&& Binding.CandidateRange.Offset <= InLODPair.CandidateTriangles.Num()
			&& Binding.CandidateRange.Count
				<= InLODPair.CandidateTriangles.Num() - Binding.CandidateRange.Offset
			&& Binding.WeldRange.Offset >= 0
			&& Binding.WeldRange.Count >= 0
			&& Binding.WeldRange.Offset <= InLODPair.WeldRenderVertexIndices.Num()
			&& Binding.WeldRange.Count
				<= InLODPair.WeldRenderVertexIndices.Num() - Binding.WeldRange.Offset
			&& (Binding.WeldRange.Count == 0
				|| (Binding.WeldRange.Count >= 2 && Binding.WeldRange.Count <= 256));
		const bool bValuesValid = IsFiniteVector3f(Binding.BodyBarycentrics)
			&& IsFiniteVector3f(Binding.RestTangentFrameOffsetCm)
			&& FMath::IsNearlyEqual(BarycentricSum, 1.0f, 1.0e-3f)
			&& Binding.BodyBarycentrics.X >= -1.0e-3f
			&& Binding.BodyBarycentrics.Y >= -1.0e-3f
			&& Binding.BodyBarycentrics.Z >= -1.0e-3f
			&& FMath::IsFinite(Binding.RestSignedGapCm)
			&& FMath::IsFinite(Binding.TargetClearanceCm)
			&& Binding.TargetClearanceCm >= 0.0f
			&& FMath::IsFinite(Binding.FollowWeight)
			&& Binding.FollowWeight >= 0.0f
			&& Binding.FollowWeight <= 1.0f
			&& FMath::IsFinite(Binding.UnderBreastGuardWeight)
			&& Binding.UnderBreastGuardWeight >= 0.0f
			&& Binding.UnderBreastGuardWeight <= 1.0f
			&& FMath::IsFinite(Binding.LocalFeatureSizeCm)
			&& Binding.LocalFeatureSizeCm > 0.0f
			&& FMath::IsFinite(Binding.MaximumCorrectionCm)
			&& Binding.MaximumCorrectionCm > 0.0f
			&& (Binding.Mode == EEFClothingSurfaceVertexMode::PreserveUpstream
				|| FMath::Max(0.0f, Binding.TargetClearanceCm - Binding.RestSignedGapCm)
					<= Binding.MaximumCorrectionCm + 1.0e-4f)
			&& static_cast<uint8>(Binding.Mode)
				<= static_cast<uint8>(EEFClothingSurfaceVertexMode::PreserveUpstream)
			&& (Binding.Mode != EEFClothingSurfaceVertexMode::PreserveUpstream
				|| (FMath::IsNearlyZero(Binding.FollowWeight, 1.0e-6f)
					&& FMath::IsNearlyZero(Binding.UnderBreastGuardWeight, 1.0e-6f)));
		if (!bIndicesValid || !bRangesValid || !bValuesValid)
		{
			OutFailureReason = FString::Printf(
				TEXT("Surface binding vertex %d is invalid for the active render topology."),
				VertexIndex);
			return false;
		}
		if (Binding.Mode != EEFClothingSurfaceVertexMode::PreserveUpstream)
		{
			RecomputedMaximumInitialCorrectionCm = FMath::Max(
				RecomputedMaximumInitialCorrectionCm,
				FMath::Max(0.0f, Binding.TargetClearanceCm - Binding.RestSignedGapCm));
		}
	}
	if (!ValidateWeldContract(InLODPair, GarmentVertexCount, OutFailureReason))
	{
		return false;
	}
	if (RecomputedUnderBreastGuardVertexCount
		!= InLODPair.Metrics.UnderBreastGuardVertexCount)
	{
		OutFailureReason = TEXT("Surface binding inframammary-guard metrics are stale.");
		return false;
	}
	if (!FMath::IsNearlyEqual(
		InLODPair.Metrics.MaximumInitialCorrectionCm,
		RecomputedMaximumInitialCorrectionCm,
		1.0e-4f))
	{
		OutFailureReason = TEXT("Surface binding initial-correction evidence is stale.");
		return false;
	}

	return true;
}
