#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFClothingMorphV3RuntimeComponent.generated.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSurfaceBinding;
class UEFClothingSurfaceDeformerProducer;
class UMeshDeformer;
class UOptimusDeformer;
class USkeletalMesh;
class USkeletalMeshComponent;
struct FEFClothingGarmentRow;
struct FStreamableHandle;

/**
 * Presentation state of the V4 multi-clothing runtime. The enum/class names
 * remain stable for serialized compatibility with the V3 rollout.
 *
 * Passthrough is deliberately safe and visible: the component continues to
 * render its original Skeletal Mesh with its normal Unreal deformation path.
 */
UENUM(BlueprintType)
enum class EEFClothingMorphV3RuntimeState : uint8
{
	Disabled,
	Loading,
	Passthrough,
	WarmingUp,
	Ready
};

/**
 * Source-first EF Clothing Morph runtime.
 *
 * This component never assigns a Skeletal Mesh, skin-weight profile, leader
 * pose, or garment visibility. It composes a late surface constraint over the
 * exact SourceGarment render data and falls back to visible upstream rendering
 * whenever a V3 binding cannot be used.
 */
UCLASS(ClassGroup = (EF), meta = (BlueprintSpawnableComponent))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphV3RuntimeComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFClothingMorphV3RuntimeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Re-evaluates the owner's exact source/body component pairs immediately. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4")
	void ForceReconcile();

	/**
	 * Overrides this component's Director-authored additional clearance in cm.
	 * This is a scalar-only change and never rebuilds or swaps a mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Live Fit")
	void SetGarmentClearanceOffsetCm(USkeletalMeshComponent* GarmentComponent, float ClearanceCm);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Live Fit")
	void ClearGarmentClearanceOffsetCm(USkeletalMeshComponent* GarmentComponent);

	/**
	 * Overrides this component's Director-authored outward inflate distance in cm.
	 * Inflate changes positions only; it does not create vertices or a second shell.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Live Fit")
	void SetGarmentInflateCm(USkeletalMeshComponent* GarmentComponent, float InflateCm);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V4|Live Fit")
	void ClearGarmentInflateCm(USkeletalMeshComponent* GarmentComponent);

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4")
	EEFClothingMorphV3RuntimeState GetGarmentRuntimeState(
		const USkeletalMeshComponent* GarmentComponent) const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V4")
	FString GetDebugSummary() const;

private:
	struct FManagedGarmentState
	{
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		TWeakObjectPtr<USkeletalMeshComponent> BodyComponent;
		TWeakObjectPtr<const UEFClothingSurfaceBinding> Binding;
		TWeakObjectPtr<UEFClothingSurfaceDeformerProducer> Producer;
		FName GarmentId = NAME_None;
		FString CompileFingerprint;
		float DirectorClearanceCm = 0.0f;
		float DirectorInflateCm = 0.0f;
		float MaximumCorrectionCm = -1.0f;
		int32 GarmentLODIndex = INDEX_NONE;
		int32 BodyLODIndex = INDEX_NONE;
		EEFClothingMorphV3RuntimeState RuntimeState = EEFClothingMorphV3RuntimeState::Loading;
		FString PassthroughReason;
		double NextInstallAttemptSeconds = 0.0;

		/** Exact component-level deformer override captured before V3 writes one. */
		bool bCapturedDeformerOverride = false;
		bool bHadComponentDeformerOverride = false;
		bool bAlwaysUseMeshDeformerBeforeV3 = false;
		bool bOwnsFallbackDeformerOverride = false;
		TWeakObjectPtr<UMeshDeformer> PreviousComponentDeformer;
		TWeakObjectPtr<UMeshDeformer> FallbackDeformerAssignedByV3;

		/** Conservative render-bounds reservation owned only while the GPU guard is active. */
		bool bOwnsBoundsContract = false;
		bool bUseBoundsFromLeaderPoseBeforeV3 = false;
		bool bComponentUseFixedSkelBoundsBeforeV3 = false;
		float ComponentBoundsScaleBeforeV3 = 1.0f;
		float BoundsScaleAssignedByV3 = 1.0f;
		float SurfaceMaximumCorrectionCm = 0.0f;

		/** Exact material sections hidden on the body while this garment is equipped. */
		TArray<int32> CoveredBodyMaterialIndices;
	};

	struct FBodyMaterialCoverageState
	{
		TWeakObjectPtr<USkeletalMesh> BodyAsset;
		int32 MaterialIndex = INDEX_NONE;
		int32 RefCount = 0;
		TArray<bool> PreviousShownByLOD;
	};

	void StartAssetLoad();
	void HandleAssetsReady();
	void ReconcileGarments();
	void TickSurfacePasses(float DeltaTimeSeconds);
	USkeletalMeshComponent* ResolveExactBodyComponent(const USkeletalMesh* ExpectedBody) const;
	bool ValidateNativeBinding(
		const FEFClothingGarmentRow& CatalogRow,
		USkeletalMesh* SourceMesh,
		USkeletalMesh* BodyMesh,
		const UEFClothingSurfaceBinding* Binding,
		int32 GarmentLODIndex,
		int32 BodyLODIndex,
		FString& OutFailureReason) const;
	bool TryInstallSurfaceConstraint(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State,
		const FEFClothingGarmentRow& CatalogRow,
		FString& OutFailureReason);
	void SetPassthrough(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State,
		const FString& Reason,
		double RetryDelaySeconds);
	void ReleaseProducer(FManagedGarmentState& State);
	void ReleaseGarment(USkeletalMeshComponent* GarmentComponent, FManagedGarmentState& State);
	void ReleaseAllGarments();
	bool CaptureComponentDeformerOverride(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State,
		FString& OutFailureReason);
	void RestoreComponentDeformerOverride(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State) const;
	void ReleaseFallbackDeformerOverride(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State);
	bool ApplyReservedSurfaceBounds(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State,
		const UEFClothingSurfaceBinding* SurfaceBinding,
		float CatalogMaximumCorrectionCm,
		FString& OutFailureReason);
	void ReleaseOwnedBoundsContract(
		USkeletalMeshComponent* GarmentComponent,
		FManagedGarmentState& State);
	void AcquireBodyCoverage(FManagedGarmentState& State, const FEFClothingGarmentRow& CatalogRow);
	void ReleaseBodyCoverage(FManagedGarmentState& State);
	float ResolveClearanceCm(
		const USkeletalMeshComponent* GarmentComponent,
		const FManagedGarmentState& State) const;
	float ResolveInflateCm(
		const USkeletalMeshComponent* GarmentComponent,
		const FManagedGarmentState& State) const;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingFitRegistry> LoadedRegistry;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingMorphDirectorPolicy> LoadedDirector;

	UPROPERTY(Transient)
	TObjectPtr<UOptimusDeformer> LoadedSurfaceDeformer;

	/** Keeps transient producer UObjects alive while Optimus references them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> RetainedRuntimeObjects;

	TSharedPtr<FStreamableHandle> StartupLoadHandle;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState> ManagedGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> ClearanceOverridesCm;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> InflateOverridesCm;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FBodyMaterialCoverageState>> BodyMaterialCoverage;
	TArray<FString> ClothingRowIssues;
	bool bAssetsReady = false;
	bool bAssetLoadFailed = false;
	double NextReconcileSeconds = 0.0;
	FString LastStatus;
};
