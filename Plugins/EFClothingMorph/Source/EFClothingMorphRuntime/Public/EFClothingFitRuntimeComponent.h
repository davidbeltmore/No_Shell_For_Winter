#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFClothingFitRuntimeComponent.generated.h"

class UEFCharacterCustomizationComponent;
class UEFClothingFitProfile;
class UEFClothingFitRegistry;
class UEFClothingMorphDirectorPolicy;
class UEFClothingSurfaceBinding;
class UEFClothingSurfaceDeformerProducer;
class UGameViewportClient;
class UOptimusDeformer;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedMeshComponent;
struct FStreamableHandle;
struct FEFClothingGarmentRow;

UENUM(BlueprintType)
enum class EEFClothingSurfaceRuntimeState : uint8
{
	Disabled,
	Loading,
	WarmingUp,
	Ready,
	Failed
};

UCLASS(ClassGroup = (EF), meta = (BlueprintSpawnableComponent))
class EFCLOTHINGMORPHRUNTIME_API UEFClothingFitRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFClothingFitRuntimeComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void ForceReconcile();

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2")
	int32 GetAppliedGarmentCount() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2")
	int32 GetPendingGarmentCount() const;

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2")
	FString GetDebugSummary() const;

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void SetRuntimeClearanceMultiplier(float NewMultiplier);

	/** Optional per-component offset multiplier; runtime rounds the product upward to a compiler-certified tier. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void SetGarmentClearanceMultiplier(USkeletalMeshComponent* GarmentComponent, float NewMultiplier);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2")
	void ClearGarmentClearanceMultiplier(USkeletalMeshComponent* GarmentComponent);

	/** Continuous global SurfaceWrapGPU offset in centimeters. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Surface Wrap")
	void SetGlobalClearanceOffsetCm(float NewOffsetCm);

	/** Continuous per-component SurfaceWrapGPU offset in centimeters. */
	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Surface Wrap")
	void SetGarmentClearanceOffsetCm(USkeletalMeshComponent* GarmentComponent, float NewOffsetCm);

	UFUNCTION(BlueprintCallable, Category = "EF Clothing Morph V2|Surface Wrap")
	void ClearGarmentClearanceOffsetCm(USkeletalMeshComponent* GarmentComponent);

	UFUNCTION(BlueprintPure, Category = "EF Clothing Morph V2|Surface Wrap")
	EEFClothingSurfaceRuntimeState GetGarmentSurfaceRuntimeState(
		const USkeletalMeshComponent* GarmentComponent) const;

private:
	struct FAppliedGarmentState
	{
		TWeakObjectPtr<const UEFClothingFitProfile> Profile;
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		TWeakObjectPtr<USkeletalMesh> FittedMesh;
		TWeakObjectPtr<USkeletalMeshComponent> BodyMesh;
		TWeakObjectPtr<USkeletalMeshComponent> ValidatedLeaderComponent;
		TWeakObjectPtr<USkeletalMesh> ValidatedLeaderMesh;
		/** Exact leader present before V2 assigned through the profile body component. */
		TWeakObjectPtr<USkinnedMeshComponent> PreviousLeaderPoseComponent;
		/** Effective top-most leader Unreal stored and V2 owns while the garment is managed. */
		TWeakObjectPtr<USkeletalMeshComponent> LeaderPoseAssignedByV2;
		/** Both profile layers active on the source garment before SetSkeletalMesh cleared them. */
		TArray<FName> PreviousSkinWeightProfileLayers;
		bool bCapturedPreviousSkinWeightProfiles = false;
		bool bUseBoundsFromLeaderPoseBeforeV2 = false;
		bool bComponentUseFixedSkelBoundsBeforeV2 = false;
		float ComponentBoundsScaleBeforeV2 = 1.0f;
		float BoundsScaleAssignedByV2 = 1.0f;
		bool bOwnsBoundsContract = false;
		bool bRenderInMainPassBeforeV2 = true;
		bool bRenderSuppressedByV2 = false;
		bool bWaitingForSkinProfile = false;
		bool bMorphStateUnsafe = false;
		bool bAssignedLeaderPoseByV2 = false;
		/** Fail-closed rollback state: the source stays hidden until its complete profile stack is stable. */
		bool bSourceMeshRestoreRequested = false;
		bool bSourceMeshRestored = false;
		bool bSourceMorphStateReplayed = false;
		bool bRestoreFailureLogged = false;
		double ApplyStartedAtSeconds = 0.0;
		double SkinProfileWaitStartedAtSeconds = 0.0;
		double LastFullValidationAtSeconds = 0.0;
		double RestoreStartedAtSeconds = 0.0;
		TMap<FName, float> LastWrittenMorphValues;
		FName CatalogRowName = NAME_None;
		/** Exact material indices owned on BodyMesh; aliases share one ref-count token. */
		TArray<int32> CoveredBodyMaterialIndices;
		float CatalogMinimumClearanceMultiplier = 1.0f;
		bool bUsesSurfaceWrapGPU = false;
		TWeakObjectPtr<const UEFClothingSurfaceBinding> SurfaceBinding;
		TWeakObjectPtr<UEFClothingSurfaceDeformerProducer> SurfaceProducer;
		EEFClothingSurfaceRuntimeState SurfaceRuntimeState = EEFClothingSurfaceRuntimeState::Disabled;
		int32 SurfaceGarmentLODIndex = INDEX_NONE;
		int32 SurfaceBodyLODIndex = INDEX_NONE;
		int32 SurfaceWarmupFramesRemaining = 0;
		float SurfaceWarmupElapsedSeconds = 0.0f;
		bool bSurfaceAwaitingManagerInitialization = false;
		float CatalogMaximumCorrectionCm = -1.0f;
		/** Certified correction budget plus Director offset reserve owns component bounds. */
		float SurfaceMaximumCorrectionCm = 0.0f;
		uint64 SurfaceEnqueueCount = 0;
		uint32 SurfaceDispatchFailureCount = 0;
		FString SurfaceFailureReason;
	};

	struct FBodyMaterialCoverageState
	{
		TWeakObjectPtr<USkeletalMesh> BodyAsset;
		int32 MaterialIndex = INDEX_NONE;
		int32 RefCount = 0;
		TArray<bool> PreviousShownByLOD;
	};

	struct FPrefetchGarmentState
	{
		TWeakObjectPtr<const UEFClothingFitProfile> Profile;
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		bool bRenderInMainPassBeforeV2 = true;
		bool bRenderSuppressedByV2 = false;
		bool bWaitingForExistingSkinProfile = false;
		double StartedAtSeconds = 0.0;
	};

	struct FRejectedGarmentState
	{
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		TWeakObjectPtr<USkeletalMeshComponent> BodyComponent;
		TWeakObjectPtr<USkeletalMesh> BodyMesh;
		FGuid ProfileBuildGuid;
		FString Reason;
	};

	/** Visibility-only ingress ownership captured after any runtime mesh assignment and before viewport draw. */
	struct FVisibilityGuardState
	{
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		bool bRenderInMainPassBeforeV2 = true;
		bool bRenderSuppressedByV2 = false;
	};

	void ReconcileGarments();
	/**
	 * Cheap O(component-count) edge detector used every frame. Dynamic equipment can
	 * create or reuse a skeletal component between periodic reconciliation passes;
	 * observing pointer/mesh assignment changes lets V2 suppress it before render.
	 */
	bool RefreshObservedMeshAssignments();
	void RefreshViewportVisibilityBinding();
	void UnbindViewportVisibilityGuard();
	void HandleViewportBeginDraw();
	void GuardCatalogedSourceGarmentsBeforeRender();
	void CaptureVisibilityContract(
		USkeletalMeshComponent* GarmentComponent,
		bool& bOutRenderInMainPassBeforeV2,
		bool& bOutRenderSuppressedByV2);
	void RestoreVisibilityGuards();
	void SynchronizeMorphs();
	void TickSurfaceConstraints(float DeltaTimeSeconds);
	bool TryInstallSurfaceConstraint(
		USkeletalMeshComponent* GarmentComponent,
		FAppliedGarmentState& State,
		FString& OutFailureReason);
	void ReleaseSurfaceConstraint(FAppliedGarmentState& State);
	void TryExposeReadyGarment(
		USkeletalMeshComponent* GarmentComponent,
		FAppliedGarmentState& State);
	void FailSurfaceConstraint(
		USkeletalMeshComponent* GarmentComponent,
		FAppliedGarmentState& State,
		const FString& FailureReason);
	float ResolveSurfaceGarmentOffsetCm(
		USkeletalMeshComponent* GarmentComponent,
		const FAppliedGarmentState& State) const;
	void ResolveCustomizationComponent();
	void HandleMorphStateApplied();
	void StartStartupAssetLoad();
	void HandleStartupAssetsReady();
	void StartProfilePrefetch(const UEFClothingFitProfile* Profile);
	void LaunchNextProfilePrefetchBatch();
	void HandleRegistryAssetsReady();
	void BuildCatalogIndex();
	const FEFClothingGarmentRow* FindCatalogRow(
		const USkeletalMesh* SourceMesh,
		const USkeletalMesh* BodyMesh,
		FName* OutRowName = nullptr) const;
	const FEFClothingGarmentRow* FindCatalogRowById(FName GarmentId) const;
	float GetMaximumRuntimeAdditionalClearanceCm() const;
	float ResolveGlobalSurfaceOffsetCm() const;
	float ResolveDirectorGarmentOffsetCm(const FAppliedGarmentState& State) const;
	void ApplyReservedSurfaceBounds(
		USkeletalMeshComponent* GarmentComponent,
		FAppliedGarmentState& State,
		const UEFClothingSurfaceBinding* SurfaceBinding,
		float CatalogMaximumCorrectionCm);
	void AcquireBodyCoverage(FAppliedGarmentState& State, const FEFClothingGarmentRow* CatalogRow);
	void ReleaseBodyCoverage(FAppliedGarmentState& State);
	USkeletalMeshComponent* ResolveBodyMesh(const UEFClothingFitProfile* Profile) const;
	bool TryApplyProfile(USkeletalMeshComponent* GarmentComponent, const UEFClothingFitProfile* Profile);
	void RemoveStaleStates();
	void ReleaseOwnedBoundsContract(USkeletalMeshComponent* GarmentComponent, FAppliedGarmentState& State);
	void RestoreGarment(USkeletalMeshComponent* GarmentComponent, FAppliedGarmentState& State, bool bRestoreSourceMesh);
	void ProcessPendingRestores();
	void FinalizePendingRestoresForEndPlay();
	void RestorePrefetchGarment(USkeletalMeshComponent* GarmentComponent, FPrefetchGarmentState& State);
	void RestoreAllGarments();
	void RefreshRetainedSourceMeshes();
	bool IsProfileRejected(
		USkeletalMeshComponent* GarmentComponent,
		USkeletalMesh* SourceMesh,
		const UEFClothingFitProfile* Profile);
	void RememberProfileRejection(
		USkeletalMeshComponent* GarmentComponent,
		USkeletalMesh* SourceMesh,
		const UEFClothingFitProfile* Profile,
		const FString& Reason);
	bool ValidateProfileForComponents(
		const UEFClothingFitProfile* Profile,
		USkeletalMeshComponent* GarmentComponent,
		USkeletalMesh* SourceMesh,
		USkeletalMeshComponent* BodyComponent,
		USkeletalMesh* FittedMesh,
		FString& OutFailureReason) const;
	float ResolveBodyMorphValue(
		USkeletalMeshComponent* BodyComponent,
		const TArray<USkeletalMeshComponent*>& PoseSources,
		FName MorphName) const;
	void GatherMorphPoseSources(
		USkeletalMeshComponent* BodyComponent,
		USkeletalMeshComponent* GarmentComponent,
		TArray<USkeletalMeshComponent*>& OutPoseSources) const;
	float ResolveClearanceValue(
		USkeletalMeshComponent* GarmentComponent,
		const UEFClothingFitProfile* Profile,
		float RequiredMinimumMultiplier) const;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingFitRegistry> LoadedRegistry;

	UPROPERTY(Transient)
	TObjectPtr<UEFClothingMorphDirectorPolicy> LoadedDirectorPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UEFCharacterCustomizationComponent> CustomizationComponent;

	/** Hard references needed for reliable CVar rollback after the component switches to a derived mesh. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMesh>> RetainedSourceMeshes;

	/** Prefetched fitted/compatibility assets stay resident without game-thread loads at equip time. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> RetainedProfileAssets;

	/** Keeps transient producers alive while their dynamic Optimus instances are registered. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> RetainedSurfaceRuntimeObjects;

	UPROPERTY(Transient)
	TObjectPtr<UOptimusDeformer> LoadedSurfaceConstraintDeformer;

	/** Pins Registry + Director + Surface Graph until the initial async callback validates all three. */
	TSharedPtr<FStreamableHandle> StartupAssetLoadHandle;
	TSharedPtr<FStreamableHandle> RegistryPrefetchHandle;
	TSet<FSoftObjectPath> PendingProfilePrefetchPaths;
	TSet<FSoftObjectPath> InFlightProfilePrefetchPaths;

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState> AppliedGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState> RestoringGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FPrefetchGarmentState> PrefetchingGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FRejectedGarmentState> RejectedGarments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<USkeletalMesh>> ObservedMeshAssignments;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FVisibilityGuardState> VisibilityGuards;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> GarmentClearanceMultipliers;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, float> GarmentClearanceOffsetsCm;
	TMap<FString, FName> CatalogRowIndex;
	TMap<FName, int32> GarmentIdIndex;
	TSet<FString> DuplicateCatalogKeys;
	TSet<FSoftObjectPath> CatalogedSourcePaths;
	/** Enabled source meshes remain guarded even when the rest of their Director contract is invalid. */
	TSet<FSoftObjectPath> GuardedSourcePaths;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FBodyMaterialCoverageState>> BodyMaterialCoverage;
	TWeakObjectPtr<UGameViewportClient> BoundGameViewportClient;
	FDelegateHandle ViewportBeginDrawHandle;
	FDelegateHandle MorphStateAppliedHandle;
	double NextReconcileAtSeconds = 0.0;
	double NextMorphSyncAtSeconds = 0.0;
	uint64 ReconcilePassCount = 0;
	uint64 MeshAssignmentEdgeCount = 0;
	float RuntimeClearanceMultiplier = 1.0f;
	float GlobalClearanceOffsetCm = 0.0f;
	bool bLastRuntimeEnabled = false;
	bool bStartupAssetsReady = false;
	bool bStartupAssetLoadFailed = false;
	bool bIsRestoring = false;
	FString LastStatus = TEXT("Not initialized");
};
