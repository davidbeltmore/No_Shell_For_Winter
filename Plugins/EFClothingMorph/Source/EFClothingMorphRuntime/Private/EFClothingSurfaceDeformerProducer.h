#pragma once

#include "CoreMinimal.h"
#include "Animation/MeshDeformerProducer.h"
#include "UObject/Object.h"
#include "EFClothingSurfaceDeformerProducer.generated.h"

class UEFClothingSurfaceBinding;
class UActorComponent;
class UOptimusDeformer;
class UOptimusDeformerDynamicInstanceManager;
class UOptimusDeformerInstance;
class UOptimusDeformerInstanceSettings;
class USkeletalMeshComponent;
struct FEFClothingSurfaceLODPairBinding;

/**
 * Per-garment transient owner for the EF surface-constraint Optimus instance.
 *
 * The producer is injected into the dynamic manager created by the garment's
 * existing DAZ deformer. It never replaces the component or mesh deformer.
 * Runtime enqueues this producer explicitly in BeginInitViews so both the
 * garment and secondary body bindings can read their final EndOfFrame output.
 */
UCLASS(Transient)
class UEFClothingSurfaceDeformerProducer final : public UObject, public IMeshDeformerProducer
{
	GENERATED_BODY()

public:
	bool Install(
		USkeletalMeshComponent* InGarmentComponent,
		USkeletalMeshComponent* InBodyComponent,
		UOptimusDeformer* InSurfaceDeformer,
		const UEFClothingSurfaceBinding* InSurfaceBinding,
		const FEFClothingSurfaceLODPairBinding& InLODPair,
		FString& OutFailureReason);

	bool EnqueueSurfacePass(
		float DeltaTimeSeconds,
		float GlobalClearanceOffsetCm,
		float GarmentClearanceOffsetCm,
		float MaximumCorrectionOverrideCm,
		FString& OutFailureReason);

	void Detach();

	bool IsInstalledFor(
		const USkeletalMeshComponent* InGarmentComponent,
		const USkeletalMeshComponent* InBodyComponent,
		int32 InGarmentLODIndex,
		int32 InBodyLODIndex) const;

	int32 GetGarmentLODIndex() const { return GarmentLODIndex; }
	int32 GetBodyLODIndex() const { return BodyLODIndex; }
	uint64 GetEnqueuedFrameCount() const { return EnqueuedFrameCount; }
	uint32 GetDispatchFailureCount() const;

	// IMeshDeformerProducer
	FMeshDeformerBeginDestroyEvent& OnBeginDestroy() override { return BeginDestroyEvent; }

	// UObject
	void BeginDestroy() override;

private:
	UActorComponent* ResolveComponentBinding(FName BindingName) const;
	bool UploadImmutableBinding(
		const FEFClothingSurfaceLODPairBinding& InLODPair,
		FString& OutFailureReason);
	bool ValidateLiveLODTopology(
		const FEFClothingSurfaceLODPairBinding& InLODPair,
		FString& OutFailureReason) const;

	UPROPERTY(Transient)
	TObjectPtr<UOptimusDeformerInstanceSettings> InstanceSettings;

	TWeakObjectPtr<USkeletalMeshComponent> GarmentComponent;
	TWeakObjectPtr<USkeletalMeshComponent> BodyComponent;
	TWeakObjectPtr<UOptimusDeformerDynamicInstanceManager> DynamicManager;
	TWeakObjectPtr<UOptimusDeformerInstance> SurfaceInstance;
	FGuid InstanceGuid;
	int32 GarmentLODIndex = INDEX_NONE;
	int32 BodyLODIndex = INDEX_NONE;
	uint64 EnqueuedFrameCount = 0;
	uint32 LastObservedDispatchFailureCount = 0;
	bool bRegisteredWithManager = false;

	struct FDispatchTelemetry;
	TSharedPtr<FDispatchTelemetry, ESPMode::ThreadSafe> DispatchTelemetry;
	FMeshDeformerBeginDestroyEvent BeginDestroyEvent;
};
