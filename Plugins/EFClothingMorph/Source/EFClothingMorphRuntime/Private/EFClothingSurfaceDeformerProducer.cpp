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
#include "Templates/Atomic.h"

namespace EFClothingSurfaceGraphContract
{
	const FName BodyBinding(TEXT("Body"));
	const FName BindingVertexCount(TEXT("EF_BindingVertexCount"));
	const FName GarmentLOD(TEXT("EF_GarmentLODIndex"));
	const FName BodyLOD(TEXT("EF_BodyLODIndex"));
	const FName BodyTriangleAndMode(TEXT("EF_BodyTriangleAndMode"));
	const FName BarycentricsAndFollowWeight(TEXT("EF_BarycentricsAndFollowWeight"));
	const FName RestOffsetAndClearanceCm(TEXT("EF_RestOffsetAndClearanceCm"));
	const FName MaximumCorrectionAndRestGapCm(TEXT("EF_MaximumCorrectionAndRestGapCm"));
	const FName GlobalClearanceOffsetCm(TEXT("EF_GlobalClearanceOffsetCm"));
	const FName GarmentClearanceOffsetCm(TEXT("EF_GarmentClearanceOffsetCm"));
	const FName MaximumCorrectionOverrideCm(TEXT("EF_MaximumCorrectionOverrideCm"));
	const FName DeltaTimeSeconds(TEXT("EF_DeltaTimeSeconds"));
	const FName BodyToGarmentTransform(TEXT("EF_BodyToGarmentTransform"));
}

struct UEFClothingSurfaceDeformerProducer::FDispatchTelemetry
{
	TAtomic<uint32> DispatchFailureCount { 0u };
	TAtomic<uint64> RenderValidatedSubmissionCount { 0u };
	TAtomic<bool> bRenderConfirmationArmed { false };
	TAtomic<bool> bCancelled { false };
};

namespace
{
	/**
	 * One-shot render-thread arm. We intentionally wait through two EndFrameRT
	 * callbacks: if the enqueue command arrived after BeginInitViews in the first
	 * render frame, ComputeFramework will consume it during the second one before
	 * this confirmation runs. The callback only proves successful render-graph
	 * validation/submission (including provider and shader validation), not GPU
	 * completion; obtaining the latter would require an asynchronous GPU fence or
	 * readback and is not needed to keep the first raw garment frame hidden.
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
	GarmentLODIndex = InLODPair.GarmentTopology.LODIndex;
	BodyLODIndex = InLODPair.BodyTopology.LODIndex;
	if (InSurfaceBinding->FindLODPair(GarmentLODIndex, BodyLODIndex) != &InLODPair)
	{
		OutFailureReason = TEXT("LOD binding does not belong to the supplied immutable surface asset.");
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
	if (!SourceDeformerWrites(BodyManager, EMeshDeformerOutputBuffer::SkinnedMeshPosition))
	{
		OutFailureReason = TEXT("Body LOD has no Optimus source writer for the final animated surface.");
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
	if (FailureCount != LastObservedDispatchFailureCount)
	{
		LastObservedDispatchFailureCount = FailureCount;
		OutFailureReason = TEXT("Surface constraint graph reported a render dispatch fallback.");
		return false;
	}

	// Read Skinned Mesh exposes component-local positions. Transport the exact
	// final Female surface into garment-local space before evaluating anchors.
	const FTransform BodyToGarment = Body->GetComponentTransform().GetRelativeTransform(
		Garment->GetComponentTransform());
	const FVector RelativeScale = BodyToGarment.GetScale3D();
	if (BodyToGarment.ContainsNaN()
		|| FMath::Abs(RelativeScale.X) <= UE_SMALL_NUMBER
		|| FMath::Abs(RelativeScale.Y) <= UE_SMALL_NUMBER
		|| FMath::Abs(RelativeScale.Z) <= UE_SMALL_NUMBER)
	{
		OutFailureReason = TEXT("Body-to-garment transform is non-finite or has degenerate scale.");
		return false;
	}

	const bool bParametersAccepted =
		Instance->SetTransformVariable(
			EFClothingSurfaceGraphContract::BodyToGarmentTransform,
			BodyToGarment)
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::GlobalClearanceOffsetCm,
			FMath::Max(FMath::IsFinite(GlobalClearanceOffsetCm) ? GlobalClearanceOffsetCm : 0.0f, 0.0f))
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::GarmentClearanceOffsetCm,
			FMath::Max(FMath::IsFinite(GarmentClearanceOffsetCm) ? GarmentClearanceOffsetCm : 0.0f, 0.0f))
		&& Instance->SetFloatVariable(
			EFClothingSurfaceGraphContract::MaximumCorrectionOverrideCm,
			FMath::IsFinite(MaximumCorrectionOverrideCm) ? MaximumCorrectionOverrideCm : -1.0f)
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
	Desc.FallbackDelegate.BindLambda([Telemetry]()
	{
		if (Telemetry.IsValid())
		{
			++Telemetry->DispatchFailureCount;
		}
	});

	// UOptimusDeformerInstance narrows the override to protected, while the base
	// contract intentionally exposes EnqueueWork for composition.
	UMeshDeformerInstance* MeshDeformerInstance = Instance;
	MeshDeformerInstance->EnqueueWork(Desc);
	++EnqueuedFrameCount;

	// ComputeFramework exposes failure but no success callback. Arm a one-shot
	// render-thread acknowledgement only for the first candidate submission. Its
	// fallback delegate runs during SubmitWork before EndFrameRT, so an unchanged
	// failure count at the second EndFrameRT is the strongest non-blocking public
	// confirmation that the surface kernel was validated and submitted.
	if (!DispatchTelemetry->bRenderConfirmationArmed.Load()
		&& DispatchTelemetry->RenderValidatedSubmissionCount.Load() == 0)
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
	EnqueuedFrameCount = 0;
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

uint64 UEFClothingSurfaceDeformerProducer::GetRenderValidatedSubmissionCount() const
{
	return DispatchTelemetry.IsValid()
		? DispatchTelemetry->RenderValidatedSubmissionCount.Load()
		: 0u;
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
	const int32 VertexCount = InLODPair.VertexBindings.Num();
	BodyTriangleAndMode.Reserve(VertexCount);
	BarycentricsAndFollowWeight.Reserve(VertexCount);
	RestOffsetAndClearanceCm.Reserve(VertexCount);
	MaximumCorrectionAndRestGapCm.Reserve(VertexCount);

	for (const FEFClothingSurfaceVertexBinding& Binding : InLODPair.VertexBindings)
	{
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
	}

	const bool bUploadAccepted =
		Instance->SetIntVariable(EFClothingSurfaceGraphContract::BindingVertexCount, VertexCount)
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
			MaximumCorrectionAndRestGapCm);
	if (!bUploadAccepted)
	{
		OutFailureReason = TEXT("Surface graph variable schema does not match the EF V26 binding upload contract.");
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
		|| InLODPair.Metrics.SurfaceFollowVertexCount
			+ InLODPair.Metrics.HybridVertexCount
			+ InLODPair.Metrics.CollisionOnlyVertexCount != GarmentVertexCount
		|| InLODPair.Metrics.NeighborReferenceCount
			!= InLODPair.NeighborRenderVertexIndices.Num()
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
	}

	for (int32 VertexIndex = 0; VertexIndex < InLODPair.VertexBindings.Num(); ++VertexIndex)
	{
		const FEFClothingSurfaceVertexBinding& Binding = InLODPair.VertexBindings[VertexIndex];
		const bool bIndicesValid = Binding.GarmentRenderVertexIndex == VertexIndex
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
			&& Binding.NeighborRange.Offset <= InLODPair.NeighborRenderVertexIndices.Num()
			&& Binding.NeighborRange.Count
				<= InLODPair.NeighborRenderVertexIndices.Num() - Binding.NeighborRange.Offset
			&& Binding.CandidateRange.Offset >= 0
			&& Binding.CandidateRange.Count >= 0
			&& Binding.CandidateRange.Offset <= InLODPair.CandidateTriangles.Num()
			&& Binding.CandidateRange.Count
				<= InLODPair.CandidateTriangles.Num() - Binding.CandidateRange.Offset;
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
			&& FMath::IsFinite(Binding.MaximumCorrectionCm)
			&& Binding.MaximumCorrectionCm > 0.0f
			&& static_cast<uint8>(Binding.Mode)
				<= static_cast<uint8>(EEFClothingSurfaceVertexMode::CollisionOnly);
		if (!bIndicesValid || !bRangesValid || !bValuesValid)
		{
			OutFailureReason = FString::Printf(
				TEXT("Surface binding vertex %d is invalid for the active render topology."),
				VertexIndex);
			return false;
		}
	}

	return true;
}
