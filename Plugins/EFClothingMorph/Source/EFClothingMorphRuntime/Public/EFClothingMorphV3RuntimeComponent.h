#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphV3RuntimeComponent.generated.h"

class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSystemManifest;
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
class EFCLOTHINGMORPHRUNTIME_API UEFClothingMorphV3RuntimeComponent : public UActorComponent
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
	 * Marks the owner's equipment composition as changed. Reconciliation runs
	 * on the next component tick, after the equipment system has finished its
	 * own component mutations. The watchdog remains only as a low-frequency
	 * fallback for integrations that cannot emit this notification yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V5.1|Equipment")
	void NotifyEquipmentChanged();

	/**
	 * Low-frequency fallback for missed equipment notifications, in seconds.
	 * Set to 0 to make reconciliation fully event-driven.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EF Clothing Morph V5.1|Equipment", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float ReconcileWatchdogIntervalSeconds = 2.0f;

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
		TWeakObjectPtr<USkeletalMesh> BodyAsset;
		FEFClothingGarmentRow CatalogRow;
		TWeakObjectPtr<USkeletalMeshComponent> BodyComponent;
		TWeakObjectPtr<const UEFClothingSurfaceBinding> Binding;
		TWeakObjectPtr<UEFClothingSurfaceDeformerProducer> Producer;
		FName GarmentId = NAME_None;
		FSoftObjectPath BindingAssetPath;
		FString CompileFingerprint;
		int32 RequestedBindingLODIndex = INDEX_NONE;
		bool bUsesV5StreamableBinding = false;
		float DirectorClearanceCm = 0.0f;
		float LayerStackClearanceCm = 0.0f;
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
		TArray<FName> CoveredBodyBones;
		TWeakObjectPtr<USkeletalMeshComponent> BoneCoverageComponent;
		bool bOwnsBodyCoverageDeformer = false;
	};
	struct FBodyDeformerCoverageState
	{
		// Reuse the exact component override capture/restore contract.
		FManagedGarmentState Snapshot;
		int32 RefCount = 0;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FBodyDeformerCoverageState> BodyDeformerCoverage;

	struct FBodyBoneCoverageState
	{
		TWeakObjectPtr<USkeletalMesh> BodyAsset;
		int32 RefCount = 0;
		bool bPreviouslyHidden = false;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<FName, FBodyBoneCoverageState>> BodyBoneCoverage;

	struct FBodyMaterialCoverageState
	{
		TWeakObjectPtr<USkeletalMesh> BodyAsset;
		int32 MaterialIndex = INDEX_NONE;
		int32 RefCount = 0;
		TArray<bool> PreviousShownByLOD;
	};

	void StartAssetLoad();
	void HandleAssetsReady();
	void RequestV4FallbackRegistryLoad();
	void HandleV4FallbackRegistryLoadComplete();
	void RequestV5BindingLoad(const FSoftObjectPath& BindingPath);
	void HandleV5BindingLoadComplete(FSoftObjectPath BindingPath);
	bool ValidateV5BindingPayload(
		const FSoftObjectPath& BindingPath,
		const UEFClothingSurfaceBinding* LoadedBinding,
		FString& OutFailureReason) const;
	void RecordV5BindingLoadFailure(const FSoftObjectPath& BindingPath);
	void ExpireTimedOutV5BindingLoads(double NowSeconds);
	void EvictUnusedV5BindingPayloads();
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

	/** Lazily loaded only when a healthy V5 registry lacks an exact garment/LOD record. */
	UPROPERTY(Transient)
	TObjectPtr<UEFClothingFitRegistry> LoadedV4FallbackRegistry;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingMorphDirectorPolicy> LoadedDirector;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingSystemManifest> LoadedManifest;

	UPROPERTY(Transient)
	TObjectPtr<UOptimusDeformer> LoadedSurfaceDeformer;

	/** Keeps transient producer UObjects alive while Optimus references them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> RetainedRuntimeObjects;

	TSharedPtr<FStreamableHandle> StartupLoadHandle;
	TSharedPtr<FStreamableHandle> V4FallbackRegistryLoadHandle;
	FSoftObjectPath RequestedRegistryPath;
	FSoftObjectPath RequestedV4FallbackRegistryPath;
	FSoftObjectPath RequestedDirectorPath;
	FSoftObjectPath RequestedManifestPath;
	TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> V5BindingLoadHandles;
	TMap<FSoftObjectPath, double> V5BindingLoadStartedSeconds;
	TMap<FSoftObjectPath, double> V5BindingRetryAfterSeconds;
	TMap<FSoftObjectPath, int32> V5BindingFailureCounts;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState> ManagedGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> ClearanceOverridesCm;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> InflateOverridesCm;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FBodyMaterialCoverageState>> BodyMaterialCoverage;
	TArray<FString> ClothingRowIssues;
	bool bAssetsReady = false;
	bool bAssetLoadFailed = false;
	bool bUsingV4FallbackRegistry = false;
	bool bV4FallbackRegistryLoadAttempted = false;
	bool bV4FallbackRegistryLoadFailed = false;
	bool bReconcileRequested = true;
	double NextReconcileSeconds = 0.0;
	double NextSurfacePassSeconds = 0.0;
	FString LastStatus;
};
