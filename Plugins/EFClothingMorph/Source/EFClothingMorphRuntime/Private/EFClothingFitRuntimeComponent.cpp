#include "EFClothingFitRuntimeComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingSurfaceDeformerProducer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "OptimusDeformer.h"
#include "Rendering/SkinWeightProfile.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingMorphV2, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarEFClothingMorphV2Enabled(
		TEXT("EFClothingMorph.V2.Enabled"),
		1,
		TEXT("Enables automatic EF Clothing Morph V2 derived garments (0=restore source garments, 1=enabled)."),
		ECVF_Default);

	const FName ManagedTag(TEXT("EFClothingMorphV2.Managed"));
	const FName PendingTag(TEXT("EFClothingMorphV2.Pending"));
	const FName CertifiedAutoFitProfileName(TEXT("EF_AutoFit"));
	constexpr double SkinProfileTimeoutSeconds = 8.0;

	FString MakeCatalogKey(const FSoftObjectPath& SourcePath, const FSoftObjectPath& BodyPath)
	{
		return SourcePath.ToString() + TEXT("|") + BodyPath.ToString();
	}

	TArray<FName> CanonicalMaterialSlots(const TArray<FName>& Input)
	{
		TSet<FName> UniqueSlots;
		for (const FName Slot : Input)
		{
			if (!Slot.IsNone())
			{
				UniqueSlots.Add(Slot);
			}
		}
		TArray<FName> Result = UniqueSlots.Array();
		Result.Sort(FNameLexicalLess());
		return Result;
	}

	bool DoesSkeletalMaterialMatchSlot(const FSkeletalMaterial& Material, FName SlotName)
	{
		if (Material.MaterialSlotName == SlotName)
		{
			return true;
		}
#if WITH_EDITORONLY_DATA
		return Material.ImportedMaterialSlotName == SlotName;
#else
		return false;
#endif
	}

	TArray<FString> CanonicalNonEmptyStrings(const TArray<FString>& Input)
	{
		TSet<FString> UniqueValues;
		for (const FString& Value : Input)
		{
			if (!Value.IsEmpty())
			{
				UniqueValues.Add(Value);
			}
		}
		TArray<FString> Result = UniqueValues.Array();
		Result.Sort();
		return Result;
	}

	bool IsImplementedBackend(const EEFClothingSurfaceBackend Backend)
	{
		return Backend == EEFClothingSurfaceBackend::GeometryFitFallback
			|| Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU;
	}

	bool HasCertifiedCatalogClearance(const FEFClothingGarmentRow& Row)
	{
		return FMath::IsFinite(Row.MinimumClearanceMultiplier)
			&& Row.MinimumClearanceMultiplier >= EFClothingMorphV25::ClearanceTierMin
			&& Row.MinimumClearanceMultiplier <= EFClothingMorphV25::ClearanceTierMax;
	}

	bool HasValidSurfaceCatalogContract(const FEFClothingGarmentRow& Row)
	{
		if (Row.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			return true;
		}
		const bool bFabricClearanceValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
			Row.FabricClearanceCm)
			|| (FMath::IsFinite(Row.FabricClearanceCm)
				&& Row.FabricClearanceCm >= 0.0f
				&& Row.FabricClearanceCm <= 5.0f);
		const bool bMaximumCorrectionValid = EFClothingMorphV26::IsAutomaticCentimeterValue(
			Row.MaximumCorrectionCm)
			|| (FMath::IsFinite(Row.MaximumCorrectionCm)
				&& Row.MaximumCorrectionCm > 0.0f
				&& Row.MaximumCorrectionCm <= 10.0f);
		return bFabricClearanceValid
			&& bMaximumCorrectionValid
			&& Row.bFailClosedOnMissingLOD;
	}

	/**
	 * SetLeaderPoseComponent intentionally collapses a follower onto the top-most
	 * leader in an existing modular-character chain. Female can therefore name
	 * Multiple as its leader while a fitted garment is assigned through Female.
	 * Resolve the value Unreal will actually store, and fail closed for cycles or
	 * non-skeletal links instead of treating a merely skeleton-compatible proxy as
	 * an authoritative pose driver.
	 */
	USkeletalMeshComponent* ResolveEffectivePoseDriver(USkeletalMeshComponent* BodyComponent)
	{
		if (!IsValid(BodyComponent))
		{
			return nullptr;
		}

		TSet<const USkeletalMeshComponent*> VisitedComponents;
		USkeletalMeshComponent* Current = BodyComponent;
		for (int32 Depth = 0; Depth < 128; ++Depth)
		{
			if (!IsValid(Current) || VisitedComponents.Contains(Current))
			{
				return nullptr;
			}
			VisitedComponents.Add(Current);

			USkinnedMeshComponent* NextLeader = Current->LeaderPoseComponent.Get();
			if (!NextLeader)
			{
				return Current;
			}
			if (!IsValid(NextLeader))
			{
				return nullptr;
			}

			USkeletalMeshComponent* NextSkeletalLeader = Cast<USkeletalMeshComponent>(NextLeader);
			if (!IsValid(NextSkeletalLeader))
			{
				return nullptr;
			}
			Current = NextSkeletalLeader;
		}

		return nullptr;
	}

	bool HasMatchingActiveLOD0Deformer(
		const USkeletalMeshComponent* GarmentComponent,
		const USkeletalMeshComponent* BodyComponent)
	{
		if (!IsValid(GarmentComponent) || !IsValid(BodyComponent))
		{
			return false;
		}
		if (GarmentComponent->GetComponentMeshDeformer().Get()
			!= BodyComponent->GetComponentMeshDeformer().Get())
		{
			return false;
		}
		const bool bGarmentHasLOD0Instance =
			GarmentComponent->GetMeshDeformerInstanceForLOD(0) != nullptr;
		const bool bBodyHasLOD0Instance =
			BodyComponent->GetMeshDeformerInstanceForLOD(0) != nullptr;
		if (bGarmentHasLOD0Instance != bBodyHasLOD0Instance)
		{
			return false;
		}
		const USkeletalMesh* GarmentMesh = GarmentComponent->GetSkeletalMeshAsset();
		const USkeletalMesh* BodyMesh = BodyComponent->GetSkeletalMeshAsset();
		const bool bConfiguredDeformer = (IsValid(GarmentMesh)
				&& GarmentMesh->GetDefaultMeshDeformer() != nullptr)
			|| (IsValid(BodyMesh) && BodyMesh->GetDefaultMeshDeformer() != nullptr)
			|| GarmentComponent->GetComponentMeshDeformer().Get() != nullptr
			|| BodyComponent->GetComponentMeshDeformer().Get() != nullptr;
		return !bConfiguredDeformer || bGarmentHasLOD0Instance;
	}

	bool IsCertifiedSkinProfileActive(
		const USkeletalMeshComponent* GarmentComponent,
		const UEFClothingFitProfile* Profile)
	{
		return IsValid(GarmentComponent)
			&& IsValid(Profile)
			&& Profile->SkinWeightProfileName == CertifiedAutoFitProfileName
			&& !GarmentComponent->IsSkinWeightProfilePending()
			&& GarmentComponent->IsUsingSkinWeightProfile()
			&& GarmentComponent->GetCurrentSkinWeightProfileName(ESkinWeightProfileLayer::Primary)
				== Profile->SkinWeightProfileName
			&& GarmentComponent->GetCurrentSkinWeightProfileName(ESkinWeightProfileLayer::Secondary).IsNone();
	}

	void GatherManagedMorphNames(const UEFClothingFitProfile* Profile, TSet<FName>& OutNames)
	{
		OutNames.Reset();
		if (!IsValid(Profile))
		{
			return;
		}

		OutNames.Add(Profile->ClearanceMorphName);
		for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
		{
			if (!Binding.BodyMorph.IsNone())
			{
				OutNames.Add(Binding.BodyMorph);
			}
			if (!Binding.GarmentMorph.IsNone())
			{
				OutNames.Add(Binding.GarmentMorph);
			}
			for (const FEFClothingMorphSample& Sample : Binding.Samples)
			{
				if (!Sample.GarmentMorph.IsNone())
				{
					OutNames.Add(Sample.GarmentMorph);
				}
			}
		}
		for (FName MonitoredBodyMorph : Profile->MonitoredBodyMorphNames)
		{
			if (!MonitoredBodyMorph.IsNone())
			{
				OutNames.Add(MonitoredBodyMorph);
			}
		}
		for (const FEFClothingMorphPairCertificate& Certificate : Profile->MorphPairCertificates)
		{
			for (const FEFClothingMorphPairCell& Cell : Certificate.Cells)
			{
				if (!Cell.GarmentMorph.IsNone())
				{
					OutNames.Add(Cell.GarmentMorph);
				}
			}
		}
	}
}

UEFClothingFitRuntimeComponent::UEFClothingFitRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.TickInterval = 0.0f;
}

void UEFClothingFitRuntimeComponent::BeginPlay()
{
	Super::BeginPlay();

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const float ConfiguredClearanceMultiplier = Settings ? Settings->ClearanceMultiplier : 1.0f;
	RuntimeClearanceMultiplier = FMath::IsFinite(ConfiguredClearanceMultiplier)
		? FMath::Clamp(ConfiguredClearanceMultiplier, 1.0f, 2.0f)
		: 1.0f;
	// Director authoring is exclusively per garment index. This transient value
	// remains zero unless a legacy caller explicitly uses the compatibility API.
	GlobalClearanceOffsetCm = 0.0f;
	ResolveCustomizationComponent();
	bLastRuntimeEnabled = Settings && Settings->bEnabled && CVarEFClothingMorphV2Enabled.GetValueOnGameThread() != 0;
	bStartupAssetsReady = false;
	bStartupAssetLoadFailed = false;
	NextReconcileAtSeconds = 0.0;
	NextMorphSyncAtSeconds = 0.0;
	LoadedRegistry = nullptr;
	LoadedDirectorPolicy = nullptr;
	LoadedSurfaceConstraintDeformer = nullptr;
	BuildCatalogIndex();
	RefreshViewportVisibilityBinding();
	// The Director is deliberately tiny. Load just its authored allow-list
	// synchronously before the first draw so an already-equipped source garment
	// (for example a default ACF slot) cannot render while the registry, graph
	// and bindings stream in. All heavyweight V26 assets remain asynchronous.
	if (bLastRuntimeEnabled && Settings && !Settings->DirectorPolicy.IsNull())
	{
		LoadedDirectorPolicy = Settings->DirectorPolicy.LoadSynchronous();
		FString DirectorValidationError;
		if (IsValid(LoadedDirectorPolicy)
			&& LoadedDirectorPolicy->ValidatePolicy(DirectorValidationError))
		{
			BuildCatalogIndex();
			GuardCatalogedSourceGarmentsBeforeRender();
		}
		else
		{
			if (IsValid(LoadedDirectorPolicy))
			{
				BuildCatalogIndex();
			}
			LoadedDirectorPolicy = nullptr;
			CatalogRowIndex.Reset();
			GarmentIdIndex.Reset();
			DuplicateCatalogKeys.Reset();
			GuardCatalogedSourceGarmentsBeforeRender();
			LastStatus = FString::Printf(
				TEXT("V26 startup visibility guard could not load the Clothing Director: %s"),
				*DirectorValidationError);
			UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		}
	}
	StartStartupAssetLoad();
}

void UEFClothingFitRuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindViewportVisibilityGuard();
	RestoreAllGarments();
	// Normal rollback deliberately advances through stable phases on separate
	// ticks. EndPlay has no future tick, so drain those deterministic phases now
	// and use a final synchronous best-effort release only for genuinely pending
	// async profile state. Never discard coverage/leader/bounds ownership records.
	for (int32 Pass = 0; Pass < 8 && !RestoringGarments.IsEmpty(); ++Pass)
	{
		ProcessPendingRestores();
	}
	if (!RestoringGarments.IsEmpty())
	{
		FinalizePendingRestoresForEndPlay();
	}
	if (CustomizationComponent && MorphStateAppliedHandle.IsValid())
	{
		CustomizationComponent->OnMorphStateApplied().Remove(MorphStateAppliedHandle);
		MorphStateAppliedHandle.Reset();
	}
	CustomizationComponent = nullptr;
	if (StartupAssetLoadHandle.IsValid())
	{
		StartupAssetLoadHandle->CancelHandle();
		StartupAssetLoadHandle.Reset();
	}
	if (RegistryPrefetchHandle.IsValid())
	{
		RegistryPrefetchHandle->CancelHandle();
	}
	RegistryPrefetchHandle.Reset();
	PendingProfilePrefetchPaths.Reset();
	InFlightProfilePrefetchPaths.Reset();
	RetainedProfileAssets.Reset();
	RetainedSurfaceRuntimeObjects.Reset();
	LoadedSurfaceConstraintDeformer = nullptr;
	bStartupAssetsReady = false;
	bStartupAssetLoadFailed = false;
	PrefetchingGarments.Reset();
	GarmentClearanceMultipliers.Reset();
	GarmentClearanceOffsetsCm.Reset();
	RejectedGarments.Reset();
	ObservedMeshAssignments.Reset();
	VisibilityGuards.Reset();
	CatalogRowIndex.Reset();
	GarmentIdIndex.Reset();
	DuplicateCatalogKeys.Reset();
	CatalogedSourcePaths.Reset();
	GuardedSourcePaths.Reset();
	BodyMaterialCoverage.Reset();
	LoadedDirectorPolicy = nullptr;
	LoadedRegistry = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UEFClothingFitRuntimeComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Rollback is deliberately independent from the enable CVar. A profile change
	// can complete asynchronously after V2 has been disabled, and the source must
	// remain fail-closed until both captured layers are restored exactly.
	ProcessPendingRestores();

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const bool bRuntimeEnabled = Settings && Settings->bEnabled && CVarEFClothingMorphV2Enabled.GetValueOnGameThread() != 0;
	if (bRuntimeEnabled != bLastRuntimeEnabled)
	{
		bLastRuntimeEnabled = bRuntimeEnabled;
		if (!bRuntimeEnabled)
		{
			RestoreAllGarments();
			LastStatus = TEXT("Disabled; source garments restored");
		}
		else
		{
			RejectedGarments.Reset();
			NextReconcileAtSeconds = 0.0;
		}
	}

	if (!bRuntimeEnabled || !GetWorld())
	{
		return;
	}

	if (!CustomizationComponent)
	{
		ResolveCustomizationComponent();
	}
	RefreshViewportVisibilityBinding();
	if (!bStartupAssetsReady)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	// Do not wait for the low-frequency maintenance pass when ACF (or any other
	// equipment system) creates a skeletal component or assigns a garment mesh.
	// The O(N) observation pass is intentionally cheap; the heavier reconciliation
	// still runs only on a real component/mesh edge or at the configured interval.
	const bool bMeshAssignmentsChanged = RefreshObservedMeshAssignments();
	MeshAssignmentEdgeCount += bMeshAssignmentsChanged ? 1u : 0u;
	if (bMeshAssignmentsChanged || Now >= NextReconcileAtSeconds)
	{
		ReconcileGarments();
		NextReconcileAtSeconds = Now + FMath::Max(Settings->ReconcileIntervalSeconds, 0.02f);
	}

	const float MorphSyncInterval = FMath::Max(Settings->MorphSyncIntervalSeconds, 0.0f);
	if (MorphSyncInterval <= 0.0f || Now >= NextMorphSyncAtSeconds)
	{
		SynchronizeMorphs();
		NextMorphSyncAtSeconds = MorphSyncInterval > 0.0f ? Now + MorphSyncInterval : Now;
	}

	// This call only enqueues the producer. The graph itself executes after the
	// normal DAZ/Chaos outputs in BeginInitViews on the render thread.
	TickSurfaceConstraints(DeltaTime);
}

bool UEFClothingFitRuntimeComponent::RefreshObservedMeshAssignments()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		const bool bHadAssignments = !ObservedMeshAssignments.IsEmpty();
		ObservedMeshAssignments.Reset();
		return bHadAssignments;
	}

	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(Owner);
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> SeenComponents;
	SeenComponents.Reserve(MeshComponents.Num());
	bool bChanged = ObservedMeshAssignments.Num() != MeshComponents.Num();

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		const TWeakObjectPtr<USkeletalMeshComponent> ComponentKey(MeshComponent);
		USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset();
		SeenComponents.Add(ComponentKey);
		const TWeakObjectPtr<USkeletalMesh>* PreviousMesh = ObservedMeshAssignments.Find(ComponentKey);
		if (!PreviousMesh || PreviousMesh->Get() != CurrentMesh)
		{
			bChanged = true;
		}
		ObservedMeshAssignments.Add(ComponentKey, CurrentMesh);
	}

	for (auto It = ObservedMeshAssignments.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !SeenComponents.Contains(It.Key()))
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}

	return bChanged;
}

void UEFClothingFitRuntimeComponent::RefreshViewportVisibilityBinding()
{
	UGameViewportClient* DesiredViewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (BoundGameViewportClient.Get() == DesiredViewport && ViewportBeginDrawHandle.IsValid())
	{
		return;
	}

	UnbindViewportVisibilityGuard();
	if (IsValid(DesiredViewport))
	{
		BoundGameViewportClient = DesiredViewport;
		ViewportBeginDrawHandle = DesiredViewport->OnBeginDraw().AddUObject(
			this,
			&UEFClothingFitRuntimeComponent::HandleViewportBeginDraw);
	}
}

void UEFClothingFitRuntimeComponent::UnbindViewportVisibilityGuard()
{
	if (UGameViewportClient* Viewport = BoundGameViewportClient.Get())
	{
		if (ViewportBeginDrawHandle.IsValid())
		{
			Viewport->OnBeginDraw().Remove(ViewportBeginDrawHandle);
		}
	}
	ViewportBeginDrawHandle.Reset();
	BoundGameViewportClient.Reset();
}

void UEFClothingFitRuntimeComponent::HandleViewportBeginDraw()
{
	GuardCatalogedSourceGarmentsBeforeRender();
}

void UEFClothingFitRuntimeComponent::GuardCatalogedSourceGarmentsBeforeRender()
{
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const bool bRuntimeEnabled = Settings
		&& Settings->bEnabled
		&& CVarEFClothingMorphV2Enabled.GetValueOnGameThread() != 0;
	if (!bRuntimeEnabled)
	{
		RestoreVisibilityGuards();
		return;
	}
	if (!GetOwner() || GuardedSourcePaths.IsEmpty() || bIsRestoring)
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> GuardedThisDraw;
	GuardedThisDraw.Reserve(MeshComponents.Num());
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent)
			|| AppliedGarments.Contains(MeshComponent)
			|| PrefetchingGarments.Contains(MeshComponent)
			|| RestoringGarments.Contains(MeshComponent))
		{
			continue;
		}

		USkeletalMesh* SourceMesh = MeshComponent->GetSkeletalMeshAsset();
		if (!IsValid(SourceMesh) || !GuardedSourcePaths.Contains(FSoftObjectPath(SourceMesh)))
		{
			continue;
		}

		const TWeakObjectPtr<USkeletalMeshComponent> ComponentKey(MeshComponent);
		GuardedThisDraw.Add(ComponentKey);
		FVisibilityGuardState* Guard = VisibilityGuards.Find(ComponentKey);
		bool bIngressEdge = false;
		if (!Guard)
		{
			FVisibilityGuardState NewGuard;
			NewGuard.SourceMesh = SourceMesh;
			NewGuard.bRenderInMainPassBeforeV2 = MeshComponent->bRenderInMainPass;
			Guard = &VisibilityGuards.Add(ComponentKey, MoveTemp(NewGuard));
			bIngressEdge = true;
		}
		else if (Guard->SourceMesh.Get() != SourceMesh)
		{
			// A reusable equipment slot may switch directly between two registered
			// garments while it is hidden. Preserve the original pre-V2 visibility.
			Guard->SourceMesh = SourceMesh;
			bIngressEdge = true;
		}

		MeshComponent->ComponentTags.AddUnique(PendingTag);
		if (MeshComponent->bRenderInMainPass)
		{
			MeshComponent->SetRenderInMainPass(false);
			Guard->bRenderSuppressedByV2 = true;
		}
		if (bIngressEdge)
		{
			NextReconcileAtSeconds = 0.0;
		}
	}

	for (auto It = VisibilityGuards.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* MeshComponent = It.Key().Get();
		if (IsValid(MeshComponent) && GuardedThisDraw.Contains(It.Key()))
		{
			continue;
		}
		if (IsValid(MeshComponent)
			&& (AppliedGarments.Contains(MeshComponent)
				|| PrefetchingGarments.Contains(MeshComponent)
				|| RestoringGarments.Contains(MeshComponent)))
		{
			continue;
		}

		if (IsValid(MeshComponent))
		{
			FVisibilityGuardState& Guard = It.Value();
			if (MeshComponent->GetSkeletalMeshAsset() == Guard.SourceMesh.Get()
				&& MeshComponent->ComponentTags.Contains(PendingTag)
				&& Guard.bRenderSuppressedByV2
				&& !MeshComponent->bRenderInMainPass)
			{
				MeshComponent->SetRenderInMainPass(Guard.bRenderInMainPassBeforeV2);
			}
			MeshComponent->ComponentTags.Remove(PendingTag);
		}
		It.RemoveCurrent();
	}
}

void UEFClothingFitRuntimeComponent::CaptureVisibilityContract(
	USkeletalMeshComponent* GarmentComponent,
	bool& bOutRenderInMainPassBeforeV2,
	bool& bOutRenderSuppressedByV2)
{
	bOutRenderInMainPassBeforeV2 = IsValid(GarmentComponent)
		? GarmentComponent->bRenderInMainPass
		: true;
	bOutRenderSuppressedByV2 = false;
	if (!IsValid(GarmentComponent))
	{
		return;
	}

	if (FVisibilityGuardState* Guard = VisibilityGuards.Find(GarmentComponent))
	{
		bOutRenderInMainPassBeforeV2 = Guard->bRenderInMainPassBeforeV2;
		bOutRenderSuppressedByV2 = Guard->bRenderSuppressedByV2;
		VisibilityGuards.Remove(GarmentComponent);
	}
	if (GarmentComponent->bRenderInMainPass)
	{
		GarmentComponent->SetRenderInMainPass(false);
		bOutRenderSuppressedByV2 = true;
	}
}

void UEFClothingFitRuntimeComponent::RestoreVisibilityGuards()
{
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FVisibilityGuardState>& Pair : VisibilityGuards)
	{
		USkeletalMeshComponent* MeshComponent = Pair.Key.Get();
		if (!IsValid(MeshComponent))
		{
			continue;
		}
		if (MeshComponent->GetSkeletalMeshAsset() == Pair.Value.SourceMesh.Get()
			&& MeshComponent->ComponentTags.Contains(PendingTag)
			&& Pair.Value.bRenderSuppressedByV2
			&& !MeshComponent->bRenderInMainPass)
		{
			MeshComponent->SetRenderInMainPass(Pair.Value.bRenderInMainPassBeforeV2);
		}
		MeshComponent->ComponentTags.Remove(PendingTag);
	}
	VisibilityGuards.Reset();
}

void UEFClothingFitRuntimeComponent::ForceReconcile()
{
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings || !Settings->bEnabled || CVarEFClothingMorphV2Enabled.GetValueOnGameThread() == 0)
	{
		RestoreAllGarments();
		LastStatus = TEXT("ForceReconcile ignored while V2 is disabled; source garments remain restored");
		return;
	}
	if (!bStartupAssetsReady)
	{
		LastStatus = bStartupAssetLoadFailed
			? TEXT("ForceReconcile rejected: V26 startup assets failed closed")
			: TEXT("ForceReconcile deferred: V26 startup assets are still loading");
		return;
	}
	RejectedGarments.Reset();
	ReconcileGarments();
	SynchronizeMorphs();
	TickSurfaceConstraints(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
}

int32 UEFClothingFitRuntimeComponent::GetAppliedGarmentCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		const USkeletalMeshComponent* Component = Pair.Key.Get();
		const USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(Pair.Value.BodyMesh.Get());
		if (IsValid(Component)
			&& IsValid(ExpectedPoseDriver)
			&& Pair.Value.FittedMesh.IsValid()
			&& Component->GetSkeletalMeshAsset() == Pair.Value.FittedMesh.Get()
			&& Component->LeaderPoseComponent.Get() == ExpectedPoseDriver
			&& Pair.Value.ValidatedLeaderComponent.Get() == ExpectedPoseDriver
			&& HasMatchingActiveLOD0Deformer(Component, Pair.Value.BodyMesh.Get())
			&& !Pair.Value.bWaitingForSkinProfile
			&& !Pair.Value.bMorphStateUnsafe
			&& (!Pair.Value.bUsesSurfaceWrapGPU
				|| Pair.Value.SurfaceRuntimeState == EEFClothingSurfaceRuntimeState::Ready)
			&& Pair.Value.bOwnsBoundsContract
			&& !Component->bUseBoundsFromLeaderPoseComponent
			&& Component->bComponentUseFixedSkelBounds
			&& IsCertifiedSkinProfileActive(Component, Pair.Value.Profile.Get()))
		{
			++Count;
		}
	}
	return Count;
}

int32 UEFClothingFitRuntimeComponent::GetPendingGarmentCount() const
{
	int32 Count = VisibilityGuards.Num() + PrefetchingGarments.Num() + RestoringGarments.Num();
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		const USkeletalMeshComponent* Component = Pair.Key.Get();
		const USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(Pair.Value.BodyMesh.Get());
		if (IsValid(Component)
			&& Component->GetSkeletalMeshAsset() == Pair.Value.FittedMesh.Get()
			&& (Pair.Value.bWaitingForSkinProfile
				|| Pair.Value.bMorphStateUnsafe
				|| (Pair.Value.bUsesSurfaceWrapGPU
					&& Pair.Value.SurfaceRuntimeState != EEFClothingSurfaceRuntimeState::Ready)
				|| !IsValid(ExpectedPoseDriver)
				|| Component->LeaderPoseComponent.Get() != ExpectedPoseDriver
				|| Pair.Value.ValidatedLeaderComponent.Get() != ExpectedPoseDriver
				|| !HasMatchingActiveLOD0Deformer(Component, Pair.Value.BodyMesh.Get())
				|| !IsCertifiedSkinProfileActive(Component, Pair.Value.Profile.Get())))
		{
			++Count;
		}
	}
	return Count;
}

FString UEFClothingFitRuntimeComponent::GetDebugSummary() const
{
	int32 CoveredSectionCount = 0;
	int32 CoverageReferenceCount = 0;
	int32 SurfaceLoadingCount = 0;
	int32 SurfaceWarmupCount = 0;
	int32 SurfaceReadyCount = 0;
	int32 SurfaceFailedCount = 0;
	uint64 SurfaceEnqueueCount = 0;
	uint64 SurfaceDispatchFailureCount = 0;
	uint64 SurfaceRenderValidatedSubmissionCount = 0;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		if (!Pair.Value.bUsesSurfaceWrapGPU)
		{
			continue;
		}
		SurfaceEnqueueCount += Pair.Value.SurfaceEnqueueCount;
		SurfaceDispatchFailureCount += Pair.Value.SurfaceDispatchFailureCount;
		if (const UEFClothingSurfaceDeformerProducer* Producer = Pair.Value.SurfaceProducer.Get())
		{
			SurfaceRenderValidatedSubmissionCount +=
				Producer->GetRenderValidatedSubmissionCount();
		}
		switch (Pair.Value.SurfaceRuntimeState)
		{
		case EEFClothingSurfaceRuntimeState::Loading: ++SurfaceLoadingCount; break;
		case EEFClothingSurfaceRuntimeState::WarmingUp: ++SurfaceWarmupCount; break;
		case EEFClothingSurfaceRuntimeState::Ready: ++SurfaceReadyCount; break;
		case EEFClothingSurfaceRuntimeState::Failed: ++SurfaceFailedCount; break;
		default: break;
		}
	}
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FBodyMaterialCoverageState>>& BodyPair
		: BodyMaterialCoverage)
	{
		CoveredSectionCount += BodyPair.Value.Num();
		for (const TPair<int32, FBodyMaterialCoverageState>& SectionPair : BodyPair.Value)
		{
			CoverageReferenceCount += FMath::Max(SectionPair.Value.RefCount, 0);
		}
	}
	const int32 GarmentDefinitionCount = LoadedDirectorPolicy
		? LoadedDirectorPolicy->Garments.Num()
		: 0;
	return FString::Printf(
		TEXT("Owner=%s | Startup=%s | Registry=%s | Director=%s | Garments=%d | Ready=%d | Pending=%d | Surface[L=%d W=%d R=%d F=%d Enqueue=%llu RenderValidated=%llu DispatchFail=%llu] | VisibilityGuards=%d | Reconciles=%llu | MeshEdges=%llu | CoverageSections=%d | CoverageRefs=%d | ClearanceMultiplier=%.3f | GlobalOffsetCm=%.3f/%.3f | Status=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		bStartupAssetsReady ? TEXT("Ready") : (bStartupAssetLoadFailed ? TEXT("Failed") : TEXT("Loading")),
		LoadedRegistry ? *LoadedRegistry->GetPathName() : TEXT("None"),
		LoadedDirectorPolicy ? *LoadedDirectorPolicy->GetPathName() : TEXT("None"),
		GarmentDefinitionCount,
		GetAppliedGarmentCount(),
		GetPendingGarmentCount(),
		SurfaceLoadingCount,
		SurfaceWarmupCount,
		SurfaceReadyCount,
		SurfaceFailedCount,
		SurfaceEnqueueCount,
		SurfaceRenderValidatedSubmissionCount,
		SurfaceDispatchFailureCount,
		VisibilityGuards.Num(),
		ReconcilePassCount,
		MeshAssignmentEdgeCount,
		CoveredSectionCount,
		CoverageReferenceCount,
		RuntimeClearanceMultiplier,
		ResolveGlobalSurfaceOffsetCm(),
		GetMaximumRuntimeAdditionalClearanceCm(),
		*LastStatus);
}

void UEFClothingFitRuntimeComponent::SetRuntimeClearanceMultiplier(float NewMultiplier)
{
	if (!FMath::IsFinite(NewMultiplier))
	{
		LastStatus = TEXT("Ignored non-finite global clearance multiplier");
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return;
	}
	RuntimeClearanceMultiplier = FMath::Clamp(NewMultiplier, 1.0f, 2.0f);
	SynchronizeMorphs();
}

void UEFClothingFitRuntimeComponent::SetGarmentClearanceMultiplier(
	USkeletalMeshComponent* GarmentComponent,
	float NewMultiplier)
{
	if (!IsValid(GarmentComponent)
		|| GarmentComponent->GetOwner() != GetOwner()
		|| !FMath::IsFinite(NewMultiplier))
	{
		if (!FMath::IsFinite(NewMultiplier))
		{
			LastStatus = TEXT("Ignored non-finite per-garment clearance multiplier");
			UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		}
		return;
	}
	GarmentClearanceMultipliers.Add(GarmentComponent, FMath::Clamp(NewMultiplier, 1.0f, 2.0f));
	SynchronizeMorphs();
}

void UEFClothingFitRuntimeComponent::ClearGarmentClearanceMultiplier(USkeletalMeshComponent* GarmentComponent)
{
	if (IsValid(GarmentComponent))
	{
		GarmentClearanceMultipliers.Remove(GarmentComponent);
		SynchronizeMorphs();
	}
}

void UEFClothingFitRuntimeComponent::SetGlobalClearanceOffsetCm(const float NewOffsetCm)
{
	if (!FMath::IsFinite(NewOffsetCm))
	{
		LastStatus = TEXT("Ignored non-finite global clearance offset");
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return;
	}
	const float SafeMaximum = GetMaximumRuntimeAdditionalClearanceCm();
	GlobalClearanceOffsetCm = FMath::Clamp(NewOffsetCm, 0.0f, SafeMaximum);
	if (NewOffsetCm > SafeMaximum)
	{
		LastStatus = FString::Printf(
			TEXT("Clamped global clearance offset to the Clothing Director safety budget %.3fcm"),
			SafeMaximum);
	}
}

void UEFClothingFitRuntimeComponent::SetGarmentClearanceOffsetCm(
	USkeletalMeshComponent* GarmentComponent,
	const float NewOffsetCm)
{
	if (!IsValid(GarmentComponent)
		|| GarmentComponent->GetOwner() != GetOwner()
		|| !FMath::IsFinite(NewOffsetCm))
	{
		if (!FMath::IsFinite(NewOffsetCm))
		{
			LastStatus = TEXT("Ignored non-finite per-garment clearance offset");
			UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		}
		return;
	}
	const float SafeMaximum = GetMaximumRuntimeAdditionalClearanceCm();
	GarmentClearanceOffsetsCm.Add(GarmentComponent, FMath::Clamp(NewOffsetCm, 0.0f, SafeMaximum));
	if (NewOffsetCm > SafeMaximum)
	{
		LastStatus = FString::Printf(
			TEXT("Clamped garment clearance offset to the Clothing Director safety budget %.3fcm"),
			SafeMaximum);
	}
}

void UEFClothingFitRuntimeComponent::ClearGarmentClearanceOffsetCm(
	USkeletalMeshComponent* GarmentComponent)
{
	if (IsValid(GarmentComponent))
	{
		GarmentClearanceOffsetsCm.Remove(GarmentComponent);
	}
}

EEFClothingSurfaceRuntimeState UEFClothingFitRuntimeComponent::GetGarmentSurfaceRuntimeState(
	const USkeletalMeshComponent* GarmentComponent) const
{
	if (!IsValid(GarmentComponent))
	{
		return EEFClothingSurfaceRuntimeState::Disabled;
	}
	const TWeakObjectPtr<USkeletalMeshComponent> ComponentKey(
		const_cast<USkeletalMeshComponent*>(GarmentComponent));
	if (const FAppliedGarmentState* State = AppliedGarments.Find(ComponentKey))
	{
		return State->SurfaceRuntimeState;
	}
	if (PrefetchingGarments.Contains(ComponentKey))
	{
		return EEFClothingSurfaceRuntimeState::Loading;
	}
	if (RejectedGarments.Contains(ComponentKey))
	{
		return EEFClothingSurfaceRuntimeState::Failed;
	}
	return EEFClothingSurfaceRuntimeState::Disabled;
}

void UEFClothingFitRuntimeComponent::ResolveCustomizationComponent()
{
	if (!GetOwner() || CustomizationComponent)
	{
		return;
	}

	CustomizationComponent = GetOwner()->FindComponentByClass<UEFCharacterCustomizationComponent>();
	if (CustomizationComponent && !MorphStateAppliedHandle.IsValid())
	{
		MorphStateAppliedHandle = CustomizationComponent->OnMorphStateApplied().AddUObject(
			this,
			&UEFClothingFitRuntimeComponent::HandleMorphStateApplied);

		// The world subsystem can attach V2 before a dynamically-created character
		// customization component exists. Acquire per-morph ownership retroactively
		// before that late component is allowed to replay values onto fitted meshes.
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> OwnershipFailures;
		for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
		{
			if (Pair.Value.bUsesSurfaceWrapGPU)
			{
				continue;
			}
			USkeletalMeshComponent* GarmentComponent = Pair.Key.Get();
			const UEFClothingFitProfile* Profile = Pair.Value.Profile.Get();
			TSet<FName> ManagedMorphNames;
			GatherManagedMorphNames(Profile, ManagedMorphNames);
			if (!IsValid(GarmentComponent)
				|| !IsValid(Profile)
				|| !CustomizationComponent->RegisterExternalMorphWriter(GarmentComponent, this, ManagedMorphNames))
			{
				OwnershipFailures.Add(Pair.Key);
			}
		}

		for (const TWeakObjectPtr<USkeletalMeshComponent>& FailedComponent : OwnershipFailures)
		{
			USkeletalMeshComponent* GarmentComponent = FailedComponent.Get();
			if (FAppliedGarmentState* State = AppliedGarments.Find(FailedComponent))
			{
				const UEFClothingFitProfile* Profile = State->Profile.Get();
				USkeletalMesh* SourceMesh = State->SourceMesh.Get();
				RestoreGarment(GarmentComponent, *State, true);
				AppliedGarments.Remove(FailedComponent);
				RememberProfileRejection(
					GarmentComponent,
					SourceMesh,
					Profile,
					TEXT("Late character-customization morph ownership conflict."));
			}
		}
		if (!OwnershipFailures.IsEmpty())
		{
			RefreshRetainedSourceMeshes();
		}
		if (!AppliedGarments.IsEmpty())
		{
			bIsRestoring = true;
			CustomizationComponent->ReapplyCurrentMorphState();
			bIsRestoring = false;
			SynchronizeMorphs();
		}
	}
}

void UEFClothingFitRuntimeComponent::HandleMorphStateApplied()
{
	if (bIsRestoring || !bStartupAssetsReady)
	{
		return;
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings || !Settings->bEnabled || CVarEFClothingMorphV2Enabled.GetValueOnGameThread() == 0)
	{
		return;
	}
	ReconcileGarments();
	SynchronizeMorphs();
}

void UEFClothingFitRuntimeComponent::StartStartupAssetLoad()
{
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings
		|| Settings->Registry.IsNull()
		|| Settings->DirectorPolicy.IsNull()
		|| Settings->SurfaceConstraintDeformer.IsNull())
	{
		bStartupAssetLoadFailed = true;
		LastStatus = TEXT("V26 startup failed closed: Registry, Clothing Director and SurfaceConstraintDeformer must all be configured");
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		return;
	}

	TArray<FSoftObjectPath> StartupPaths;
	StartupPaths.Reserve(3);
	StartupPaths.AddUnique(Settings->Registry.ToSoftObjectPath());
	StartupPaths.AddUnique(Settings->DirectorPolicy.ToSoftObjectPath());
	StartupPaths.AddUnique(Settings->SurfaceConstraintDeformer.ToSoftObjectPath());
	LastStatus = TEXT("Loading V26 Registry, Clothing Director and SurfaceConstraintDeformer asynchronously");
	StartupAssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		StartupPaths,
		FStreamableDelegate::CreateUObject(this, &UEFClothingFitRuntimeComponent::HandleStartupAssetsReady),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("EFClothingMorphV2Startup"));
	if (!StartupAssetLoadHandle.IsValid())
	{
		bStartupAssetLoadFailed = true;
		LastStatus = TEXT("V26 startup failed closed: async asset request could not be created");
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingFitRuntimeComponent::HandleStartupAssetsReady()
{
	if (bStartupAssetsReady || bStartupAssetLoadFailed)
	{
		StartupAssetLoadHandle.Reset();
		return;
	}

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	LoadedRegistry = Settings ? Settings->Registry.Get() : nullptr;
	LoadedDirectorPolicy = Settings ? Settings->DirectorPolicy.Get() : nullptr;
	LoadedSurfaceConstraintDeformer = Settings ? Settings->SurfaceConstraintDeformer.Get() : nullptr;
	if (!IsValid(LoadedRegistry)
		|| !IsValid(LoadedDirectorPolicy)
		|| !IsValid(LoadedSurfaceConstraintDeformer))
	{
		LoadedRegistry = nullptr;
		LoadedDirectorPolicy = nullptr;
		LoadedSurfaceConstraintDeformer = nullptr;
		CatalogRowIndex.Reset();
		GarmentIdIndex.Reset();
		DuplicateCatalogKeys.Reset();
		GuardCatalogedSourceGarmentsBeforeRender();
		bStartupAssetLoadFailed = true;
		LastStatus = TEXT("V26 startup failed closed: one or more required assets are missing, invalid or uncooked");
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		StartupAssetLoadHandle.Reset();
		return;
	}
	FString DirectorValidationError;
	if (!LoadedDirectorPolicy->ValidatePolicy(DirectorValidationError))
	{
		BuildCatalogIndex();
		LoadedRegistry = nullptr;
		LoadedDirectorPolicy = nullptr;
		LoadedSurfaceConstraintDeformer = nullptr;
		CatalogRowIndex.Reset();
		GarmentIdIndex.Reset();
		DuplicateCatalogKeys.Reset();
		GuardCatalogedSourceGarmentsBeforeRender();
		bStartupAssetLoadFailed = true;
		LastStatus = FString::Printf(
			TEXT("V26 startup failed closed: Clothing Director contract is invalid: %s"),
			*DirectorValidationError);
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		StartupAssetLoadHandle.Reset();
		return;
	}

	BuildCatalogIndex();
	bStartupAssetsReady = true;
	bStartupAssetLoadFailed = false;
	// The Director is now authoritative. Suppress any registered source garment
	// before the next viewport draw; reconciliation may safely occur next tick.
	GuardCatalogedSourceGarmentsBeforeRender();
	if (Settings->bPrefetchCompiledFitsOnBeginPlay)
	{
		StartProfilePrefetch(nullptr);
	}
	NextReconcileAtSeconds = 0.0;
	LastStatus = TEXT("V26 startup assets ready; waiting for garments");
	StartupAssetLoadHandle.Reset();
}

void UEFClothingFitRuntimeComponent::StartProfilePrefetch(const UEFClothingFitProfile* Profile)
{
	if (!IsValid(Profile) && !LoadedRegistry)
	{
		return;
	}

	auto QueueProfileAssets = [this](const UEFClothingFitProfile* Candidate)
	{
		if (!IsValid(Candidate))
		{
			return;
		}

		auto QueueAsset = [this](const FSoftObjectPath& AssetPath, UObject* LoadedAsset)
		{
			if (!AssetPath.IsValid())
			{
				return;
			}
			if (IsValid(LoadedAsset))
			{
				RetainedProfileAssets.AddUnique(LoadedAsset);
				PendingProfilePrefetchPaths.Remove(AssetPath);
				return;
			}
			if (!InFlightProfilePrefetchPaths.Contains(AssetPath))
			{
				PendingProfilePrefetchPaths.Add(AssetPath);
			}
		};

		QueueAsset(Candidate->FittedGarment.ToSoftObjectPath(), Candidate->FittedGarment.Get());
		QueueAsset(Candidate->CompatibilityReference.ToSoftObjectPath(), Candidate->CompatibilityReference.Get());
		if (!Candidate->SurfaceBinding.IsNull())
		{
			QueueAsset(Candidate->SurfaceBinding.ToSoftObjectPath(), Candidate->SurfaceBinding.Get());
			const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
			if (Settings)
			{
				QueueAsset(
					Settings->SurfaceConstraintDeformer.ToSoftObjectPath(),
					Settings->SurfaceConstraintDeformer.Get());
			}
		}
	};

	if (IsValid(Profile))
	{
		QueueProfileAssets(Profile);
	}
	else
	{
		for (const UEFClothingFitProfile* Candidate : LoadedRegistry->Profiles)
		{
			QueueProfileAssets(Candidate);
		}
	}

	// A newly equipped profile can be discovered while the registry-wide request
	// is still running. Keep those paths queued instead of dropping the request.
	LaunchNextProfilePrefetchBatch();
}

void UEFClothingFitRuntimeComponent::LaunchNextProfilePrefetchBatch()
{
	if (RegistryPrefetchHandle.IsValid() || PendingProfilePrefetchPaths.IsEmpty())
	{
		return;
	}

	TArray<FSoftObjectPath> AssetsToLoad;
	for (const FSoftObjectPath& AssetPath : PendingProfilePrefetchPaths)
	{
		if (UObject* LoadedAsset = AssetPath.ResolveObject())
		{
			RetainedProfileAssets.AddUnique(LoadedAsset);
			if (UOptimusDeformer* SurfaceDeformer = Cast<UOptimusDeformer>(LoadedAsset))
			{
				LoadedSurfaceConstraintDeformer = SurfaceDeformer;
			}
			continue;
		}
		AssetsToLoad.Add(AssetPath);
		InFlightProfilePrefetchPaths.Add(AssetPath);
	}
	PendingProfilePrefetchPaths.Reset();
	if (AssetsToLoad.IsEmpty())
	{
		return;
	}

	RegistryPrefetchHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UEFClothingFitRuntimeComponent::HandleRegistryAssetsReady),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("EFClothingMorphV2RegistryPrefetch"));
	if (!RegistryPrefetchHandle.IsValid())
	{
		for (const FSoftObjectPath& AssetPath : InFlightProfilePrefetchPaths)
		{
			PendingProfilePrefetchPaths.Add(AssetPath);
		}
		InFlightProfilePrefetchPaths.Reset();
		LastStatus = TEXT("Generated fit asset prefetch request could not be started");
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingFitRuntimeComponent::HandleRegistryAssetsReady()
{
	for (const FSoftObjectPath& AssetPath : InFlightProfilePrefetchPaths)
	{
		if (UObject* LoadedAsset = AssetPath.ResolveObject())
		{
			RetainedProfileAssets.AddUnique(LoadedAsset);
			if (UOptimusDeformer* SurfaceDeformer = Cast<UOptimusDeformer>(LoadedAsset))
			{
				LoadedSurfaceConstraintDeformer = SurfaceDeformer;
			}
		}
	}
	InFlightProfilePrefetchPaths.Reset();

	if (LoadedRegistry)
	{
		for (const UEFClothingFitProfile* Profile : LoadedRegistry->Profiles)
		{
			if (!IsValid(Profile))
			{
				continue;
			}
			if (UObject* Fitted = Profile->FittedGarment.Get())
			{
				RetainedProfileAssets.AddUnique(Fitted);
			}
			if (UObject* Compatibility = Profile->CompatibilityReference.Get())
			{
				RetainedProfileAssets.AddUnique(Compatibility);
			}
			if (UObject* SurfaceBinding = Profile->SurfaceBinding.Get())
			{
				RetainedProfileAssets.AddUnique(SurfaceBinding);
			}
		}
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (Settings)
	{
		LoadedSurfaceConstraintDeformer = Settings->SurfaceConstraintDeformer.Get();
		if (LoadedSurfaceConstraintDeformer)
		{
			RetainedProfileAssets.AddUnique(LoadedSurfaceConstraintDeformer);
		}
	}
	RegistryPrefetchHandle.Reset();
	NextReconcileAtSeconds = 0.0;
	if (!PendingProfilePrefetchPaths.IsEmpty())
	{
		// Do not touch any garment's StartedAtSeconds: later batches remain part of
		// the same fail-closed prefetch window.
		LastStatus = TEXT("Generated fit asset batch prefetched; continuing queued batch");
		LaunchNextProfilePrefetchBatch();
	}
	else
	{
		LastStatus = TEXT("Generated fit assets prefetched; waiting for garments");
	}
}

void UEFClothingFitRuntimeComponent::BuildCatalogIndex()
{
	CatalogRowIndex.Reset();
	GarmentIdIndex.Reset();
	DuplicateCatalogKeys.Reset();
	CatalogedSourcePaths.Reset();
	GuardedSourcePaths.Reset();
	if (!LoadedDirectorPolicy)
	{
		return;
	}

	for (int32 EntryIndex = 0; EntryIndex < LoadedDirectorPolicy->Garments.Num(); ++EntryIndex)
	{
		const FEFClothingGarmentRow* Row = &LoadedDirectorPolicy->Garments[EntryIndex];
		if (Row->IsDisabledEmptyPlaceholder())
		{
			continue;
		}
		if (Row->bEnabled && !Row->SourceGarment.IsNull())
		{
			GuardedSourcePaths.Add(Row->SourceGarment.ToSoftObjectPath());
		}
		const FName GarmentId = Row->GarmentId;
		if (GarmentId.IsNone() || GarmentIdIndex.Contains(GarmentId))
		{
			UE_LOG(
				LogEFClothingMorphV2,
				Error,
				TEXT("EFClothingMorphV2 Director has an empty or duplicate GarmentId at Index[%d]: %s"),
				EntryIndex,
				*GarmentId.ToString());
			GarmentIdIndex.Remove(GarmentId);
			continue;
		}
		GarmentIdIndex.Add(GarmentId, EntryIndex);
		if (!Row->bEnabled || !IsImplementedBackend(Row->Backend)
			|| !HasCertifiedCatalogClearance(*Row)
			|| !HasValidSurfaceCatalogContract(*Row)
			|| Row->SourceGarment.IsNull() || Row->BodySurface.IsNull())
		{
			if (Row && Row->bEnabled && !IsImplementedBackend(Row->Backend))
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Error,
					TEXT("EFClothingMorphV2 Director garment %s requests unsupported backend %d; entry rejected fail-closed."),
					*GarmentId.ToString(),
					static_cast<int32>(Row->Backend));
			}
			else if (Row && Row->bEnabled && !HasCertifiedCatalogClearance(*Row))
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Error,
					TEXT("EFClothingMorphV2 Director garment %s has MinimumClearanceMultiplier %.9g outside certified [%.3f, %.3f]; entry rejected fail-closed."),
					*GarmentId.ToString(),
					Row->MinimumClearanceMultiplier,
					EFClothingMorphV25::ClearanceTierMin,
					EFClothingMorphV25::ClearanceTierMax);
			}
			else if (Row && Row->bEnabled && !HasValidSurfaceCatalogContract(*Row))
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Error,
					TEXT("EFClothingMorphV2 Director garment %s has an invalid SurfaceWrap cm/fail-closed contract; entry rejected."),
					*GarmentId.ToString());
			}
			continue;
		}
		const FString Key = MakeCatalogKey(
			Row->SourceGarment.ToSoftObjectPath(),
			Row->BodySurface.ToSoftObjectPath());
		CatalogedSourcePaths.Add(Row->SourceGarment.ToSoftObjectPath());
		if (CatalogRowIndex.Contains(Key))
		{
			DuplicateCatalogKeys.Add(Key);
			CatalogRowIndex.Remove(Key);
			UE_LOG(
				LogEFClothingMorphV2,
				Error,
				TEXT("EFClothingMorphV2 catalog duplicate rejected: %s"),
				*Key);
			continue;
		}
		if (!DuplicateCatalogKeys.Contains(Key))
		{
			CatalogRowIndex.Add(Key, GarmentId);
		}
	}
}

const FEFClothingGarmentRow* UEFClothingFitRuntimeComponent::FindCatalogRowById(
	const FName GarmentId) const
{
	if (!LoadedDirectorPolicy || GarmentId.IsNone())
	{
		return nullptr;
	}
	const int32* EntryIndex = GarmentIdIndex.Find(GarmentId);
	if (!EntryIndex || !LoadedDirectorPolicy->Garments.IsValidIndex(*EntryIndex))
	{
		return nullptr;
	}
	const FEFClothingGarmentRow* Row = &LoadedDirectorPolicy->Garments[*EntryIndex];
	return Row->GarmentId == GarmentId ? Row : nullptr;
}

const FEFClothingGarmentRow* UEFClothingFitRuntimeComponent::FindCatalogRow(
	const USkeletalMesh* SourceMesh,
	const USkeletalMesh* BodyMesh,
	FName* OutRowName) const
{
	if (OutRowName)
	{
		*OutRowName = NAME_None;
	}
	if (!LoadedDirectorPolicy || !IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		return nullptr;
	}
	const FString Key = MakeCatalogKey(FSoftObjectPath(SourceMesh), FSoftObjectPath(BodyMesh));
	if (DuplicateCatalogKeys.Contains(Key))
	{
		return nullptr;
	}
	const FName* GarmentId = CatalogRowIndex.Find(Key);
	if (!GarmentId)
	{
		return nullptr;
	}
	const FEFClothingGarmentRow* Row = FindCatalogRowById(*GarmentId);
	if (!Row || !Row->bEnabled || !IsImplementedBackend(Row->Backend)
		|| !HasCertifiedCatalogClearance(*Row)
		|| !HasValidSurfaceCatalogContract(*Row))
	{
		return nullptr;
	}
	if (OutRowName)
	{
		*OutRowName = *GarmentId;
	}
	return Row;
}

float UEFClothingFitRuntimeComponent::GetMaximumRuntimeAdditionalClearanceCm() const
{
	return EFClothingMorphV26::MaximumRuntimeAdditionalClearanceCm;
}

float UEFClothingFitRuntimeComponent::ResolveGlobalSurfaceOffsetCm() const
{
	const float CompatibilityGlobalOffsetCm = FMath::IsFinite(GlobalClearanceOffsetCm)
		? FMath::Max(GlobalClearanceOffsetCm, 0.0f)
		: 0.0f;
	return FMath::Clamp(
		CompatibilityGlobalOffsetCm,
		0.0f,
		GetMaximumRuntimeAdditionalClearanceCm());
}

float UEFClothingFitRuntimeComponent::ResolveDirectorGarmentOffsetCm(
	const FAppliedGarmentState& State) const
{
	if (!IsValid(LoadedDirectorPolicy))
	{
		return 0.0f;
	}
	const FEFClothingGarmentRow* Garment = FindCatalogRowById(State.CatalogRowName);
	if (!Garment
		|| !Garment->bEnableRuntimeTuning
		|| !FMath::IsFinite(Garment->AdditionalClearanceCm))
	{
		return 0.0f;
	}
	return FMath::Clamp(
		Garment->AdditionalClearanceCm,
		0.0f,
		GetMaximumRuntimeAdditionalClearanceCm());
}

float UEFClothingFitRuntimeComponent::ResolveDirectorGarmentVisibleThicknessCm(
	const FAppliedGarmentState& State) const
{
	const UEFClothingFitProfile* Profile = State.Profile.Get();
	if (!IsValid(Profile) || !Profile->bCompiledThicknessShell)
	{
		return 0.0f;
	}

	const FEFClothingGarmentRow* Garment = FindCatalogRowById(State.CatalogRowName);
	const float RequestedThicknessCm = Garment
		&& Garment->bCreateThicknessShell
		&& FMath::IsFinite(Garment->ShellThicknessCm)
		? Garment->ShellThicknessCm
		: EFClothingMorphV26::CompiledThicknessReferenceCm;
	return FMath::Clamp(
		RequestedThicknessCm,
		EFClothingMorphV26::MinimumRuntimeVisibleThicknessCm,
		EFClothingMorphV26::MaximumRuntimeVisibleThicknessCm);
}

void UEFClothingFitRuntimeComponent::AcquireBodyCoverage(
	FAppliedGarmentState& State,
	const FEFClothingGarmentRow* CatalogRow)
{
	USkeletalMeshComponent* BodyComponent = State.BodyMesh.Get();
	USkeletalMesh* BodyAsset = IsValid(BodyComponent) ? BodyComponent->GetSkeletalMeshAsset() : nullptr;
	if (!CatalogRow || !IsValid(BodyComponent) || !IsValid(BodyAsset))
	{
		return;
	}

	for (FName SlotName : CatalogRow->GetBodySectionsToHideInGameplay())
	{
		if (SlotName.IsNone())
		{
			continue;
		}
		int32 MaterialIndex = INDEX_NONE;
		const TArray<FSkeletalMaterial>& Materials = BodyAsset->GetMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			if (DoesSkeletalMaterialMatchSlot(Materials[Index], SlotName))
			{
				MaterialIndex = Index;
				break;
			}
		}
		if (MaterialIndex == INDEX_NONE)
		{
			UE_LOG(
				LogEFClothingMorphV2,
				Warning,
				TEXT("Catalog row %s requested missing body material slot %s on %s."),
				*State.CatalogRowName.ToString(),
				*SlotName.ToString(),
				*GetNameSafe(BodyAsset));
			continue;
		}

		if (State.CoveredBodyMaterialIndices.Contains(MaterialIndex))
		{
			continue;
		}
		TMap<int32, FBodyMaterialCoverageState>& BodySlots = BodyMaterialCoverage.FindOrAdd(BodyComponent);
		FBodyMaterialCoverageState& Coverage = BodySlots.FindOrAdd(MaterialIndex);
		if (Coverage.RefCount == 0)
		{
			Coverage.BodyAsset = BodyAsset;
			Coverage.MaterialIndex = MaterialIndex;
			Coverage.PreviousShownByLOD.Reset();
			const int32 LODCount = FMath::Max(BodyAsset->GetLODNum(), 1);
			Coverage.PreviousShownByLOD.Reserve(LODCount);
			for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
			{
				Coverage.PreviousShownByLOD.Add(
					BodyComponent->IsMaterialSectionShown(MaterialIndex, LODIndex));
				BodyComponent->ShowMaterialSection(MaterialIndex, INDEX_NONE, false, LODIndex);
			}
		}
		++Coverage.RefCount;
		State.CoveredBodyMaterialIndices.Add(MaterialIndex);
	}
}

void UEFClothingFitRuntimeComponent::ReleaseBodyCoverage(FAppliedGarmentState& State)
{
	const TWeakObjectPtr<USkeletalMeshComponent> BodyKey = State.BodyMesh;
	USkeletalMeshComponent* BodyComponent = BodyKey.Get();
	TMap<int32, FBodyMaterialCoverageState>* BodySlots = BodyMaterialCoverage.Find(BodyKey);
	if (!BodySlots)
	{
		State.CoveredBodyMaterialIndices.Reset();
		return;
	}

	for (const int32 MaterialIndex : State.CoveredBodyMaterialIndices)
	{
		FBodyMaterialCoverageState* Coverage = BodySlots->Find(MaterialIndex);
		if (!Coverage)
		{
			continue;
		}
		Coverage->RefCount = FMath::Max(Coverage->RefCount - 1, 0);
		if (Coverage->RefCount > 0)
		{
			continue;
		}
		if (IsValid(BodyComponent)
			&& BodyComponent->GetSkeletalMeshAsset() == Coverage->BodyAsset.Get())
		{
			for (int32 LODIndex = 0; LODIndex < Coverage->PreviousShownByLOD.Num(); ++LODIndex)
			{
				BodyComponent->ShowMaterialSection(
					Coverage->MaterialIndex,
					INDEX_NONE,
					Coverage->PreviousShownByLOD[LODIndex],
					LODIndex);
			}
		}
		BodySlots->Remove(MaterialIndex);
	}
	State.CoveredBodyMaterialIndices.Reset();
	if (BodySlots->IsEmpty())
	{
		BodyMaterialCoverage.Remove(BodyKey);
	}
}

USkeletalMeshComponent* UEFClothingFitRuntimeComponent::ResolveBodyMesh(const UEFClothingFitProfile* Profile) const
{
	if (!IsValid(Profile) || !GetOwner())
	{
		return nullptr;
	}

	const FSoftObjectPath ExpectedBodyPath = Profile->BodySurface.ToSoftObjectPath();
	const auto IsRenderableSurfaceComponent = [](const USkeletalMeshComponent* Candidate)
	{
		return IsValid(Candidate)
			&& Candidate->IsRegistered()
			&& Candidate->IsVisible()
			&& Candidate->bRenderInMainPass
			&& !Candidate->bHiddenInGame;
	};
	USkeletalMeshComponent* PreferredBodyComponent = nullptr;
	if (CustomizationComponent)
	{
		USkeletalMeshComponent* Candidate = CustomizationComponent->GetBodyMeshComponent();
		if (IsValid(Candidate)
			&& Candidate->GetOwner() == GetOwner()
			&& IsValid(Candidate->GetSkeletalMeshAsset())
			&& FSoftObjectPath(Candidate->GetSkeletalMeshAsset()) == ExpectedBodyPath)
		{
			PreferredBodyComponent = Candidate;
		}
	}
	// Character Creation's BodyMeshComponent is the authoritative pose/morph
	// owner, but ACF may intentionally keep it as a hidden animation driver. Such
	// a component has no FSkeletalMeshObject and therefore cannot be the surface
	// read binding. Prefer it only when it is also the component submitted to the
	// renderer; otherwise resolve the unique visible component with the same exact
	// body asset below. This never substitutes Multiple (or any other mesh).
	if (IsRenderableSurfaceComponent(PreferredBodyComponent))
	{
		return PreferredBodyComponent;
	}

	TArray<USkeletalMeshComponent*> ExactBodyComponents;
	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (IsValid(MeshComponent) && IsValid(MeshComponent->GetSkeletalMeshAsset())
			&& FSoftObjectPath(MeshComponent->GetSkeletalMeshAsset()) == ExpectedBodyPath)
		{
			ExactBodyComponents.Add(MeshComponent);
		}
	}
	if (ExactBodyComponents.Num() == 1)
	{
		return ExactBodyComponents[0];
	}

	// ACF can keep an invisible pose/selection driver and a visible final skin
	// component on the same actor, both referencing Female. SurfaceWrap must use
	// the geometry that is actually submitted as skin, never the hidden driver.
	// This remains name-agnostic and fail-closed when more than one final body is
	// renderable.
	USkeletalMeshComponent* UniqueRenderableBody = nullptr;
	for (USkeletalMeshComponent* Candidate : ExactBodyComponents)
	{
		if (!IsRenderableSurfaceComponent(Candidate))
		{
			continue;
		}
		if (UniqueRenderableBody && UniqueRenderableBody != Candidate)
		{
			return nullptr;
		}
		UniqueRenderableBody = Candidate;
	}
	return UniqueRenderableBody;
}

bool UEFClothingFitRuntimeComponent::IsProfileRejected(
	USkeletalMeshComponent* GarmentComponent,
	USkeletalMesh* SourceMesh,
	const UEFClothingFitProfile* Profile)
{
	if (!IsValid(GarmentComponent) || !IsValid(SourceMesh) || !IsValid(Profile))
	{
		return false;
	}

	FRejectedGarmentState* Rejection = RejectedGarments.Find(GarmentComponent);
	if (!Rejection)
	{
		return false;
	}

	USkeletalMeshComponent* BodyComponent = ResolveBodyMesh(Profile);
	USkeletalMesh* BodyMesh = IsValid(BodyComponent) ? BodyComponent->GetSkeletalMeshAsset() : nullptr;
	if (Rejection->SourceMesh.Get() == SourceMesh
		&& Rejection->BodyComponent.Get() == BodyComponent
		&& Rejection->BodyMesh.Get() == BodyMesh
		&& Rejection->ProfileBuildGuid == Profile->BuildGuid)
	{
		LastStatus = FString::Printf(TEXT("Fail-closed for %s: %s"), *GarmentComponent->GetName(), *Rejection->Reason);
		return true;
	}

	RejectedGarments.Remove(GarmentComponent);
	return false;
}

void UEFClothingFitRuntimeComponent::RememberProfileRejection(
	USkeletalMeshComponent* GarmentComponent,
	USkeletalMesh* SourceMesh,
	const UEFClothingFitProfile* Profile,
	const FString& Reason)
{
	if (!IsValid(GarmentComponent) || !IsValid(SourceMesh) || !IsValid(Profile))
	{
		return;
	}

	FRejectedGarmentState& Rejection = RejectedGarments.FindOrAdd(GarmentComponent);
	Rejection.SourceMesh = SourceMesh;
	if (USkeletalMeshComponent* BodyComponent = ResolveBodyMesh(Profile))
	{
		Rejection.BodyComponent = BodyComponent;
		Rejection.BodyMesh = BodyComponent->GetSkeletalMeshAsset();
	}
	else
	{
		Rejection.BodyComponent.Reset();
		Rejection.BodyMesh.Reset();
	}
	Rejection.ProfileBuildGuid = Profile->BuildGuid;
	Rejection.Reason = Reason;
}

void UEFClothingFitRuntimeComponent::ReconcileGarments()
{
	++ReconcilePassCount;
	RemoveStaleStates();
	if (!bStartupAssetsReady
		|| !LoadedRegistry
		|| !LoadedDirectorPolicy
		|| !LoadedSurfaceConstraintDeformer
		|| !GetOwner())
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}
		if (RestoringGarments.Contains(MeshComponent))
		{
			continue;
		}

		if (FPrefetchGarmentState* PrefetchState = PrefetchingGarments.Find(MeshComponent))
		{
			const UEFClothingFitProfile* PrefetchProfile = PrefetchState->Profile.Get();
			const bool bSourceStillAssigned = MeshComponent->GetSkeletalMeshAsset() == PrefetchState->SourceMesh.Get();
			const UEFClothingMorphV2Settings* RuntimeSettings = GetDefault<UEFClothingMorphV2Settings>();
			const bool bNeedsSurfaceAssets = IsValid(PrefetchProfile)
				&& PrefetchProfile->CompilerVersion == EFClothingMorphV26::CompilerVersion
				&& !PrefetchProfile->SurfaceBinding.IsNull();
			const bool bAssetsReady = IsValid(PrefetchProfile)
				&& PrefetchProfile->FittedGarment.IsValid()
				&& PrefetchProfile->CompatibilityReference.IsValid()
				&& (!bNeedsSurfaceAssets
					|| (PrefetchProfile->SurfaceBinding.IsValid()
						&& RuntimeSettings
						&& RuntimeSettings->SurfaceConstraintDeformer.IsValid()));
			const bool bExistingProfileReady = !PrefetchState->bWaitingForExistingSkinProfile
				|| !MeshComponent->IsSkinWeightProfilePending();
			const bool bTimedOut = GetWorld()
				&& GetWorld()->GetTimeSeconds() - PrefetchState->StartedAtSeconds > SkinProfileTimeoutSeconds;

			if (!bSourceStillAssigned || (bAssetsReady && bExistingProfileReady) || bTimedOut)
			{
				USkeletalMesh* PrefetchSource = PrefetchState->SourceMesh.Get();
				RestorePrefetchGarment(MeshComponent, *PrefetchState);
				PrefetchingGarments.Remove(MeshComponent);
				if (bTimedOut && bSourceStillAssigned && IsValid(PrefetchProfile))
				{
					RememberProfileRejection(
						MeshComponent,
						PrefetchSource,
						PrefetchProfile,
						TEXT("Generated fit asset prefetch timed out."));
					continue;
				}
			}
			else
			{
				if (MeshComponent->bRenderInMainPass)
				{
					MeshComponent->SetRenderInMainPass(false);
					PrefetchState->bRenderSuppressedByV2 = true;
				}
				continue;
			}
		}

		bool bSkipApplyThisCycle = false;
		if (FAppliedGarmentState* ExistingState = AppliedGarments.Find(MeshComponent))
		{
			if (MeshComponent->GetSkeletalMeshAsset() == ExistingState->FittedMesh.Get())
			{
				const UEFClothingFitProfile* ExistingProfile = ExistingState->Profile.Get();
				if (ExistingState->bOwnsBoundsContract
					&& (MeshComponent->bUseBoundsFromLeaderPoseComponent
						|| !MeshComponent->bComponentUseFixedSkelBounds
						|| !FMath::IsNearlyEqual(
							MeshComponent->BoundsScale,
							ExistingState->BoundsScaleAssignedByV2,
							KINDA_SMALL_NUMBER)))
				{
					USkeletalMesh* RejectedSource = ExistingState->SourceMesh.Get();
					const FString BoundsFailure = TEXT("Generated fitted-bounds ownership was changed externally.");
					RestoreGarment(MeshComponent, *ExistingState, true);
					AppliedGarments.Remove(MeshComponent);
					RememberProfileRejection(
						MeshComponent,
						RejectedSource,
						ExistingProfile,
						BoundsFailure);
					RefreshRetainedSourceMeshes();
					LastStatus = FString::Printf(
						TEXT("Released %s after fitted-bounds ownership conflict."),
						*MeshComponent->GetName());
					UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
					continue;
				}
				USkeletalMeshComponent* CurrentBody = ResolveBodyMesh(ExistingProfile);
				USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(CurrentBody);
				USkinnedMeshComponent* RawLeader = MeshComponent->LeaderPoseComponent.Get();
				if (IsValid(CurrentBody) && IsValid(ExpectedPoseDriver) && RawLeader != ExpectedPoseDriver)
				{
					// Assign through the exact Female body. Unreal stores its effective
					// top-most driver (normally Multiple), so ownership and validation must
					// track that observed driver while retaining the pre-V2 leader exactly.
					MeshComponent->SetLeaderPoseComponent(CurrentBody, true, false);
					if (MeshComponent->LeaderPoseComponent.Get() == ExpectedPoseDriver)
					{
						ExistingState->LeaderPoseAssignedByV2 = ExpectedPoseDriver;
						ExistingState->bAssignedLeaderPoseByV2 = true;
					}
				}
				USkeletalMeshComponent* CurrentLeader = Cast<USkeletalMeshComponent>(MeshComponent->LeaderPoseComponent.Get());
				USkeletalMesh* CurrentLeaderMesh = IsValid(CurrentLeader) ? CurrentLeader->GetSkeletalMeshAsset() : nullptr;
				const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				bool bNeedsFullValidation = CurrentBody != ExistingState->BodyMesh.Get()
					|| !IsValid(ExpectedPoseDriver)
					|| CurrentLeader != ExpectedPoseDriver
					|| CurrentLeader != ExistingState->ValidatedLeaderComponent.Get()
					|| CurrentLeaderMesh != ExistingState->ValidatedLeaderMesh.Get()
					|| Now - ExistingState->LastFullValidationAtSeconds >= 5.0;
				FString RevalidationFailure;
				const FEFClothingGarmentRow* CurrentCatalogRow = FindCatalogRow(
					ExistingState->SourceMesh.Get(),
					IsValid(CurrentBody) ? CurrentBody->GetSkeletalMeshAsset() : nullptr);
				const bool bCatalogBackendChanged = CurrentCatalogRow
					&& (CurrentCatalogRow->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU)
						!= ExistingState->bUsesSurfaceWrapGPU;
				bNeedsFullValidation = bNeedsFullValidation || bCatalogBackendChanged;
				bool bRevalidationPass = true;
				if (bNeedsFullValidation)
				{
					if (!CurrentCatalogRow)
					{
						bRevalidationPass = false;
						RevalidationFailure = TEXT("Current body/source pair is absent from the enabled garment catalog.");
					}
					else if (bCatalogBackendChanged)
					{
						bRevalidationPass = false;
						RevalidationFailure = TEXT("Catalog backend changed after equip; an atomic unequip/re-equip is required.");
					}
					else if (ExistingProfile->GarmentCompileFingerprint.IsEmpty()
						|| ExistingProfile->GarmentCompileFingerprint != CurrentCatalogRow->BuildCompileFingerprint())
					{
						bRevalidationPass = false;
						RevalidationFailure = TEXT("Compile-relevant Clothing Director settings changed after profile publication.");
					}
					else if (CanonicalMaterialSlots(ExistingProfile->ExcludedBodySurfaceMaterialSlots)
							!= CanonicalMaterialSlots(CurrentCatalogRow->ExcludedBodySurfaceMaterialSlots)
						|| CanonicalMaterialSlots(ExistingProfile->ExcludedBodyBoneBranches)
							!= CanonicalMaterialSlots(CurrentCatalogRow->ExcludedBodyBoneBranches)
						|| CanonicalNonEmptyStrings(ExistingProfile->ExcludedBodyMorphPrefixes)
							!= CanonicalNonEmptyStrings(CurrentCatalogRow->ExcludedBodyMorphPrefixes))
					{
						bRevalidationPass = false;
						RevalidationFailure = TEXT("Catalog surface/bone/morph exclusions changed after profile compilation.");
					}
					else
					{
						bRevalidationPass = ValidateProfileForComponents(
							ExistingProfile,
							MeshComponent,
							ExistingState->SourceMesh.Get(),
							CurrentBody,
							ExistingState->FittedMesh.Get(),
							RevalidationFailure);
					}
				}
				if (bNeedsFullValidation && !bRevalidationPass)
				{
					USkeletalMesh* RejectedSource = ExistingState->SourceMesh.Get();
					LastStatus = FString::Printf(
						TEXT("Profile invalidated on %s; restored source: %s"),
						*MeshComponent->GetName(),
						*RevalidationFailure);
					UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
					RestoreGarment(MeshComponent, *ExistingState, true);
					AppliedGarments.Remove(MeshComponent);
					RememberProfileRejection(
						MeshComponent,
						RejectedSource,
						ExistingProfile,
						RevalidationFailure);
					RefreshRetainedSourceMeshes();
					// RestoreGarment copies the state into RestoringGarments and the map
					// removal invalidates ExistingState. Never inspect that pointer (or its
					// profile) again during this reconciliation pass.
					continue;
				}
				else
				{
					if (CurrentBody != ExistingState->BodyMesh.Get())
					{
						// Move the reference-counted coverage token atomically in one
						// reconciliation tick; no rendered frame sees only one side.
						ReleaseBodyCoverage(*ExistingState);
						ExistingState->BodyMesh = CurrentBody;
						AcquireBodyCoverage(*ExistingState, CurrentCatalogRow);
					}
					if (bNeedsFullValidation)
					{
						ExistingState->ValidatedLeaderComponent = CurrentLeader;
						ExistingState->ValidatedLeaderMesh = CurrentLeaderMesh;
						ExistingState->LastFullValidationAtSeconds = Now;
					}
					if (CurrentCatalogRow)
					{
						ExistingState->CatalogMinimumClearanceMultiplier =
							CurrentCatalogRow->MinimumClearanceMultiplier;
						ExistingState->CatalogMaximumCorrectionCm = CurrentCatalogRow->MaximumCorrectionCm;
					}
				}

				const FName CurrentPrimaryProfile = MeshComponent->GetCurrentSkinWeightProfileName(
					ESkinWeightProfileLayer::Primary);
				const FName CurrentSecondaryProfile = MeshComponent->GetCurrentSkinWeightProfileName(
					ESkinWeightProfileLayer::Secondary);
				const bool bStableProfileStack = AppliedGarments.Contains(MeshComponent)
					&& !MeshComponent->IsSkinWeightProfilePending();
				const bool bPrimaryProfileAcquiredExternally = bStableProfileStack
					&& !CurrentPrimaryProfile.IsNone()
					&& CurrentPrimaryProfile != ExistingProfile->SkinWeightProfileName;
				const bool bSecondaryProfileAcquiredExternally = bStableProfileStack
					&& !CurrentSecondaryProfile.IsNone();
				if (bPrimaryProfileAcquiredExternally || bSecondaryProfileAcquiredExternally)
				{
					USkeletalMesh* RejectedSource = ExistingState->SourceMesh.Get();
					const FString OwnershipFailure = FString::Printf(
						TEXT("Skin-weight stack was acquired externally (Primary=%s, Secondary=%s)."),
						*CurrentPrimaryProfile.ToString(),
						*CurrentSecondaryProfile.ToString());
					RestoreGarment(MeshComponent, *ExistingState, true);
					AppliedGarments.Remove(MeshComponent);
					RememberProfileRejection(
						MeshComponent,
						RejectedSource,
						ExistingProfile,
						OwnershipFailure);
					RefreshRetainedSourceMeshes();
					LastStatus = FString::Printf(
						TEXT("Fail-suppressed %s after skin-profile ownership conflict: Primary=%s Secondary=%s"),
						*MeshComponent->GetName(),
						*CurrentPrimaryProfile.ToString(),
						*CurrentSecondaryProfile.ToString());
					UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
					bSkipApplyThisCycle = true;
				}

				if (AppliedGarments.Contains(MeshComponent)
					&& !IsCertifiedSkinProfileActive(MeshComponent, ExistingProfile))
				{
					if (!ExistingState->bWaitingForSkinProfile)
					{
						ExistingState->bWaitingForSkinProfile = true;
						ExistingState->SkinProfileWaitStartedAtSeconds = Now;
					}
					MeshComponent->ComponentTags.AddUnique(PendingTag);
					if (MeshComponent->bRenderInMainPass)
					{
						MeshComponent->SetRenderInMainPass(false);
						ExistingState->bRenderSuppressedByV2 = true;
					}
					if (!MeshComponent->IsSkinWeightProfilePending())
					{
						MeshComponent->SetSkinWeightProfile(
							ExistingProfile->SkinWeightProfileName,
							ESkinWeightProfileLayer::Primary);
					}
				}

				if (AppliedGarments.Contains(MeshComponent)
					&& ExistingState->bWaitingForSkinProfile
					&& IsCertifiedSkinProfileActive(MeshComponent, ExistingProfile))
				{
					ExistingState->bWaitingForSkinProfile = false;
					SynchronizeMorphs();
					MeshComponent->UpdateFollowerComponent();
					TryExposeReadyGarment(MeshComponent, *ExistingState);
				}
				else if (AppliedGarments.Contains(MeshComponent)
					&& ExistingState->bWaitingForSkinProfile && GetWorld()
					&& GetWorld()->GetTimeSeconds() - ExistingState->SkinProfileWaitStartedAtSeconds > SkinProfileTimeoutSeconds)
				{
					USkeletalMesh* RejectedSource = ExistingState->SourceMesh.Get();
					LastStatus = FString::Printf(TEXT("Skin profile timeout on %s; restored source"), *MeshComponent->GetName());
					UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
					RestoreGarment(MeshComponent, *ExistingState, true);
					AppliedGarments.Remove(MeshComponent);
					RememberProfileRejection(
						MeshComponent,
						RejectedSource,
						ExistingProfile,
						TEXT("Certified skin-weight profile did not become active before timeout."));
					RefreshRetainedSourceMeshes();
					bSkipApplyThisCycle = true;
				}
				if (AppliedGarments.Contains(MeshComponent) || bSkipApplyThisCycle)
				{
					continue;
				}
			}
			else
			{
				ReleaseSurfaceConstraint(*ExistingState);
				if (CustomizationComponent)
				{
					CustomizationComponent->UnregisterExternalMorphWriter(MeshComponent, this);
				}
				if (ExistingState->bRenderSuppressedByV2)
				{
					MeshComponent->SetRenderInMainPass(ExistingState->bRenderInMainPassBeforeV2);
				}
				if (ExistingState->bAssignedLeaderPoseByV2
					&& MeshComponent->LeaderPoseComponent.Get() == ExistingState->LeaderPoseAssignedByV2.Get())
				{
					MeshComponent->SetLeaderPoseComponent(
						ExistingState->PreviousLeaderPoseComponent.Get(),
						true,
						false);
				}
				ReleaseOwnedBoundsContract(MeshComponent, *ExistingState);
				ReleaseBodyCoverage(*ExistingState);
				MeshComponent->ComponentTags.Remove(ManagedTag);
				MeshComponent->ComponentTags.Remove(PendingTag);
				AppliedGarments.Remove(MeshComponent);
				RefreshRetainedSourceMeshes();
			}
		}

		USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset();
		if (!IsValid(CurrentMesh))
		{
			continue;
		}

		// A source garment may have independent Female/Male/body-variant fits.
		// Query the generated registry and authored catalog by exact source/body
		// key, avoiding an O(components * profiles) scan as the catalog grows.
		int32 ValidBodyCandidateCount = 0;
		int32 ProfilePairCount = 0;
		int32 CatalogPairCount = 0;
		int32 ResolvedBodyPairCount = 0;
		for (USkeletalMeshComponent* BodyCandidate : MeshComponents)
		{
			USkeletalMesh* CandidateBodyMesh = IsValid(BodyCandidate)
				? BodyCandidate->GetSkeletalMeshAsset()
				: nullptr;
			ValidBodyCandidateCount += IsValid(CandidateBodyMesh) ? 1 : 0;
			const UEFClothingFitProfile* Profile = LoadedRegistry->FindProfileForSourceAndBody(
				CurrentMesh,
				CandidateBodyMesh);
			const FEFClothingGarmentRow* CatalogRow = FindCatalogRow(CurrentMesh, CandidateBodyMesh);
			ProfilePairCount += IsValid(Profile) ? 1 : 0;
			CatalogPairCount += CatalogRow != nullptr ? 1 : 0;
			const bool bResolvedBodyPair = IsValid(Profile) && ResolveBodyMesh(Profile) == BodyCandidate;
			ResolvedBodyPairCount += bResolvedBodyPair ? 1 : 0;
			if (IsValid(Profile) && CatalogRow && bResolvedBodyPair)
			{
				if (IsProfileRejected(MeshComponent, CurrentMesh, Profile))
				{
					continue;
				}
				if (TryApplyProfile(MeshComponent, Profile))
				{
					break;
				}
				if (PrefetchingGarments.Contains(MeshComponent)
					|| RestoringGarments.Contains(MeshComponent))
				{
					break;
				}
			}
		}
		if (VisibilityGuards.Contains(MeshComponent)
			&& !AppliedGarments.Contains(MeshComponent)
			&& !PrefetchingGarments.Contains(MeshComponent)
			&& !RestoringGarments.Contains(MeshComponent))
		{
			LastStatus = FString::Printf(
				TEXT("Ingress guarded %s; waiting for exact pair (RegistryProfiles=%d BodyCandidates=%d ProfilePairs=%d CatalogPairs=%d ResolvedBodies=%d Source=%s)"),
				*MeshComponent->GetName(),
				LoadedRegistry->Profiles.Num(),
				ValidBodyCandidateCount,
				ProfilePairCount,
				CatalogPairCount,
				ResolvedBodyPairCount,
				*CurrentMesh->GetPathName());
		}
	}
}

bool UEFClothingFitRuntimeComponent::TryApplyProfile(
	USkeletalMeshComponent* GarmentComponent,
	const UEFClothingFitProfile* Profile)
{
	if (!IsValid(GarmentComponent) || !IsValid(Profile)
		|| AppliedGarments.Contains(GarmentComponent)
		|| RestoringGarments.Contains(GarmentComponent))
	{
		return false;
	}

	USkeletalMeshComponent* BodyComponent = ResolveBodyMesh(Profile);
	USkeletalMesh* SourceMesh = GarmentComponent->GetSkeletalMeshAsset();
	USkeletalMesh* FittedMesh = Profile->FittedGarment.Get();
	FName CatalogRowName = NAME_None;
	const FEFClothingGarmentRow* CatalogRow = FindCatalogRow(
		SourceMesh,
		IsValid(BodyComponent) ? BodyComponent->GetSkeletalMeshAsset() : nullptr,
		&CatalogRowName);
	if (!CatalogRow)
	{
		LastStatus = FString::Printf(
			TEXT("Rejected %s: source/body pair is absent or disabled in the Clothing Director"),
			*GetNameSafe(SourceMesh));
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	const bool bSurfaceWrapGPU = CatalogRow->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU;
	const UEFClothingMorphV2Settings* RuntimeSettings = GetDefault<UEFClothingMorphV2Settings>();
	if (bSurfaceWrapGPU
		&& (Profile->SurfaceBinding.IsNull()
			|| !RuntimeSettings
			|| RuntimeSettings->SurfaceConstraintDeformer.IsNull()))
	{
		LastStatus = FString::Printf(
			TEXT("Rejected %s: SurfaceWrapGPU requires an exact V26 binding and configured Deformer Graph"),
			*GetNameSafe(SourceMesh));
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, LastStatus);
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		return false;
	}
	if (Profile->GarmentCompileFingerprint.IsEmpty()
		|| Profile->GarmentCompileFingerprint != CatalogRow->BuildCompileFingerprint())
	{
		LastStatus = FString::Printf(
			TEXT("Rejected %s: compile-relevant Clothing Director settings changed; run Compile All Garments"),
			*GetNameSafe(SourceMesh));
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	if (CanonicalMaterialSlots(Profile->ExcludedBodySurfaceMaterialSlots)
			!= CanonicalMaterialSlots(CatalogRow->ExcludedBodySurfaceMaterialSlots)
		|| CanonicalMaterialSlots(Profile->ExcludedBodyBoneBranches)
			!= CanonicalMaterialSlots(CatalogRow->ExcludedBodyBoneBranches)
		|| CanonicalNonEmptyStrings(Profile->ExcludedBodyMorphPrefixes)
			!= CanonicalNonEmptyStrings(CatalogRow->ExcludedBodyMorphPrefixes))
	{
		LastStatus = FString::Printf(
			TEXT("Rejected %s: catalog surface/bone/morph exclusions differ from the compiled profile; recompile this source/body pair"),
			*GetNameSafe(SourceMesh));
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	if (!HasCertifiedCatalogClearance(*CatalogRow))
	{
		LastStatus = FString::Printf(
			TEXT("Rejected %s: catalog minimum clearance %.9g is outside certified [%.3f, %.3f]"),
			*GetNameSafe(SourceMesh),
			CatalogRow->MinimumClearanceMultiplier,
			EFClothingMorphV25::ClearanceTierMin,
			EFClothingMorphV25::ClearanceTierMax);
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	if (IsValid(BodyComponent) && IsValid(BodyComponent->GetSkeletalMeshAsset()))
	{
		const TArray<FSkeletalMaterial>& BodyMaterials = BodyComponent->GetSkeletalMeshAsset()->GetMaterials();
		for (const FName HiddenSlot : CatalogRow->GetBodySectionsToHideInGameplay())
		{
			int32 MatchCount = 0;
			for (const FSkeletalMaterial& Material : BodyMaterials)
			{
				MatchCount += DoesSkeletalMaterialMatchSlot(Material, HiddenSlot) ? 1 : 0;
			}
			if (MatchCount != 1)
			{
				LastStatus = FString::Printf(
					TEXT("Rejected %s: hidden body material slot %s resolves %d times"),
					*GetNameSafe(SourceMesh),
					*HiddenSlot.ToString(),
					MatchCount);
				UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
				return false;
			}
		}
	}
	if (GarmentComponent->IsSkinWeightProfilePending())
	{
		// SetSkeletalMesh clears both profile layers. Wait until the component can
		// report their stable names so V2 can restore the complete previous stack.
		FPrefetchGarmentState& PrefetchState = PrefetchingGarments.FindOrAdd(GarmentComponent);
		if (!PrefetchState.SourceMesh.IsValid())
		{
			PrefetchState.Profile = Profile;
			PrefetchState.SourceMesh = SourceMesh;
			CaptureVisibilityContract(
				GarmentComponent,
				PrefetchState.bRenderInMainPassBeforeV2,
				PrefetchState.bRenderSuppressedByV2);
			PrefetchState.bWaitingForExistingSkinProfile = true;
			PrefetchState.StartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
		}
		// This branch can coincide with unloaded fitted/compatibility assets.
		// StartProfilePrefetch deduplicates the async handle when already ready.
		StartProfilePrefetch(Profile);
		LastStatus = FString::Printf(
			TEXT("Source render suppressed while preserving existing skin profiles for %s"),
			*GarmentComponent->GetName());
		return false;
	}
	UEFClothingSurfaceBinding* SurfaceBinding = bSurfaceWrapGPU
		? Profile->SurfaceBinding.Get()
		: nullptr;
	if (bSurfaceWrapGPU && RuntimeSettings)
	{
		LoadedSurfaceConstraintDeformer = RuntimeSettings->SurfaceConstraintDeformer.Get();
	}
	if (!IsValid(FittedMesh)
		|| !Profile->CompatibilityReference.IsValid()
		|| (bSurfaceWrapGPU
			&& (!IsValid(SurfaceBinding) || !IsValid(LoadedSurfaceConstraintDeformer))))
	{
		FPrefetchGarmentState& PrefetchState = PrefetchingGarments.FindOrAdd(GarmentComponent);
		if (!PrefetchState.SourceMesh.IsValid())
		{
			PrefetchState.Profile = Profile;
			PrefetchState.SourceMesh = SourceMesh;
			CaptureVisibilityContract(
				GarmentComponent,
				PrefetchState.bRenderInMainPassBeforeV2,
				PrefetchState.bRenderSuppressedByV2);
			PrefetchState.StartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
		}
		StartProfilePrefetch(Profile);
		LastStatus = FString::Printf(TEXT("Source render suppressed while prefetching fit for %s"), *GarmentComponent->GetName());
		return false;
	}

	USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(BodyComponent);
	TWeakObjectPtr<USkinnedMeshComponent> PreviousLeaderPoseComponent = GarmentComponent->LeaderPoseComponent;
	bool bAssignedLeaderPoseByV2 = IsValid(BodyComponent)
		&& IsValid(ExpectedPoseDriver)
		&& GarmentComponent->LeaderPoseComponent.Get() != ExpectedPoseDriver;
	TWeakObjectPtr<USkinnedMeshComponent> ProvisionalLeaderPoseComponent;
	if (bAssignedLeaderPoseByV2)
	{
		// Assign through exact Female. SetLeaderPoseComponent collapses an existing
		// Female -> Multiple chain to the effective top-most driver, which is the
		// value V2 must subsequently own and verify.
		GarmentComponent->SetLeaderPoseComponent(BodyComponent, true, false);
		ProvisionalLeaderPoseComponent = GarmentComponent->LeaderPoseComponent;
	}
	auto RestoreProvisionalLeader = [GarmentComponent, PreviousLeaderPoseComponent, ProvisionalLeaderPoseComponent, &bAssignedLeaderPoseByV2]()
	{
		if (bAssignedLeaderPoseByV2
			&& IsValid(GarmentComponent)
			&& GarmentComponent->LeaderPoseComponent.Get() == ProvisionalLeaderPoseComponent.Get())
		{
			GarmentComponent->SetLeaderPoseComponent(PreviousLeaderPoseComponent.Get(), true, false);
		}
		bAssignedLeaderPoseByV2 = false;
	};

	FString FailureReason;
	if (!ValidateProfileForComponents(Profile, GarmentComponent, SourceMesh, BodyComponent, FittedMesh, FailureReason))
	{
		RestoreProvisionalLeader();
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, FailureReason);
		LastStatus = FString::Printf(TEXT("Rejected %s: %s"), *GarmentComponent->GetName(), *FailureReason);
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("EFClothingMorphV2 REJECT owner=%s component=%s reason=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("None"), *GarmentComponent->GetName(), *FailureReason);
		return false;
	}

	FAppliedGarmentState State;
	State.Profile = Profile;
	State.SourceMesh = GarmentComponent->GetSkeletalMeshAsset();
	State.FittedMesh = FittedMesh;
	State.BodyMesh = BodyComponent;
	State.ValidatedLeaderComponent = Cast<USkeletalMeshComponent>(GarmentComponent->LeaderPoseComponent.Get());
	State.ValidatedLeaderMesh = State.ValidatedLeaderComponent.IsValid()
		? State.ValidatedLeaderComponent->GetSkeletalMeshAsset()
		: nullptr;
	State.PreviousLeaderPoseComponent = PreviousLeaderPoseComponent;
	State.LeaderPoseAssignedByV2 = bAssignedLeaderPoseByV2
		? Cast<USkeletalMeshComponent>(GarmentComponent->LeaderPoseComponent.Get())
		: nullptr;
	State.bAssignedLeaderPoseByV2 = bAssignedLeaderPoseByV2;
	State.PreviousSkinWeightProfileLayers = GarmentComponent->GetCurrentSkinWeightProfileLayerNames();
	State.bCapturedPreviousSkinWeightProfiles = !GarmentComponent->IsSkinWeightProfilePending();
	State.bUseBoundsFromLeaderPoseBeforeV2 = GarmentComponent->bUseBoundsFromLeaderPoseComponent;
	State.bComponentUseFixedSkelBoundsBeforeV2 = GarmentComponent->bComponentUseFixedSkelBounds;
	State.ComponentBoundsScaleBeforeV2 = FMath::IsFinite(GarmentComponent->BoundsScale)
		? GarmentComponent->BoundsScale
		: 1.0f;
	State.BoundsScaleAssignedByV2 = State.ComponentBoundsScaleBeforeV2;
	CaptureVisibilityContract(
		GarmentComponent,
		State.bRenderInMainPassBeforeV2,
		State.bRenderSuppressedByV2);
	State.ApplyStartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	State.SkinProfileWaitStartedAtSeconds = State.ApplyStartedAtSeconds;
	State.LastFullValidationAtSeconds = State.ApplyStartedAtSeconds;
	State.CatalogRowName = CatalogRowName;
	State.CatalogMinimumClearanceMultiplier = CatalogRow->MinimumClearanceMultiplier;
	State.bUsesSurfaceWrapGPU = bSurfaceWrapGPU;
	State.SurfaceBinding = SurfaceBinding;
	State.SurfaceRuntimeState = bSurfaceWrapGPU
		? EEFClothingSurfaceRuntimeState::Loading
		: EEFClothingSurfaceRuntimeState::Disabled;
	State.CatalogMaximumCorrectionCm = FMath::IsFinite(CatalogRow->MaximumCorrectionCm)
		? CatalogRow->MaximumCorrectionCm
		: -1.0f;

	GarmentComponent->ComponentTags.AddUnique(PendingTag);
	GarmentComponent->SetSkeletalMesh(FittedMesh, true);
	if (GarmentComponent->LeaderPoseComponent.Get() != ExpectedPoseDriver)
	{
		GarmentComponent->SetLeaderPoseComponent(BodyComponent, true, false);
	}
	if (GarmentComponent->LeaderPoseComponent.Get() != ExpectedPoseDriver)
	{
		// SetSkeletalMesh is not allowed to leave even a hidden transition on an
		// uncertified driver. Restore the exact pre-V2 leader input before rolling
		// the source mesh back.
		GarmentComponent->SetLeaderPoseComponent(PreviousLeaderPoseComponent.Get(), true, false);
		State.bAssignedLeaderPoseByV2 = false;
		State.LeaderPoseAssignedByV2.Reset();
		RestoreGarment(GarmentComponent, State, true);
		FailureReason = TEXT("Fitted garment did not retain Female's effective LeaderPose driver after mesh assignment.");
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, FailureReason);
		LastStatus = FString::Printf(TEXT("Rejected %s: %s"), *GarmentComponent->GetName(), *FailureReason);
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	if (!HasMatchingActiveLOD0Deformer(GarmentComponent, BodyComponent))
	{
		GarmentComponent->SetLeaderPoseComponent(PreviousLeaderPoseComponent.Get(), true, false);
		State.bAssignedLeaderPoseByV2 = false;
		State.LeaderPoseAssignedByV2.Reset();
		RestoreGarment(GarmentComponent, State, true);
		FailureReason = TEXT("Fitted garment did not instantiate Female's effective LOD0 mesh deformer.");
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, FailureReason);
		LastStatus = FString::Printf(TEXT("Rejected %s: %s"), *GarmentComponent->GetName(), *FailureReason);
		UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
		return false;
	}
	GarmentComponent->bUseBoundsFromLeaderPoseComponent = false;
	GarmentComponent->bComponentUseFixedSkelBounds = true;
	if (bSurfaceWrapGPU && IsValid(SurfaceBinding))
	{
		ApplyReservedSurfaceBounds(
			GarmentComponent,
			State,
			SurfaceBinding,
			CatalogRow->MaximumCorrectionCm);
	}
	GarmentComponent->UpdateBounds();
	GarmentComponent->MarkRenderTransformDirty();
	State.bOwnsBoundsContract = true;

	if (!GarmentComponent->SetSkinWeightProfile(Profile->SkinWeightProfileName, ESkinWeightProfileLayer::Primary))
	{
		RestoreGarment(GarmentComponent, State, true);
		LastStatus = FString::Printf(TEXT("Failed to activate skin profile %s"), *Profile->SkinWeightProfileName.ToString());
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, LastStatus);
		return false;
	}

	if (!bSurfaceWrapGPU)
	{
		// V25 fallback starts at its highest certified baked tier. SurfaceWrapGPU
		// expresses the source of truth continuously in centimeters on the graph.
		const float ClearanceValue = ResolveClearanceValue(
			GarmentComponent,
			Profile,
			Profile->CertifiedClearanceMultiplierMax);
		GarmentComponent->SetMorphTarget(Profile->ClearanceMorphName, ClearanceValue, false);
		State.LastWrittenMorphValues.Add(Profile->ClearanceMorphName, ClearanceValue);
	}
	State.bWaitingForSkinProfile = !IsCertifiedSkinProfileActive(GarmentComponent, Profile);
	GarmentComponent->ComponentTags.AddUnique(ManagedTag);

	if (bSurfaceWrapGPU
		&& !TryInstallSurfaceConstraint(GarmentComponent, State, FailureReason))
	{
		RestoreGarment(GarmentComponent, State, true);
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, FailureReason);
		LastStatus = FString::Printf(
			TEXT("Rejected SurfaceWrapGPU %s: %s"),
			*GarmentComponent->GetName(),
			*FailureReason);
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		return false;
	}

	TSet<FName> ManagedMorphNames;
	if (!bSurfaceWrapGPU)
	{
		GatherManagedMorphNames(Profile, ManagedMorphNames);
	}
	if (!bSurfaceWrapGPU
		&& CustomizationComponent
		&& !CustomizationComponent->RegisterExternalMorphWriter(GarmentComponent, this, ManagedMorphNames))
	{
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("EFClothingMorphV2 could not acquire morph ownership for %s."), *GarmentComponent->GetName());
		RestoreGarment(GarmentComponent, State, true);
		RememberProfileRejection(GarmentComponent, SourceMesh, Profile, TEXT("Another system owns one or more generated morph bindings."));
		return false;
	}

	RejectedGarments.Remove(GarmentComponent);
	AcquireBodyCoverage(State, CatalogRow);
	AppliedGarments.Add(GarmentComponent, MoveTemp(State));
	RetainedProfileAssets.AddUnique(FittedMesh);
	if (UObject* CompatibilityAsset = Profile->CompatibilityReference.Get())
	{
		RetainedProfileAssets.AddUnique(CompatibilityAsset);
	}
	if (IsValid(SurfaceBinding))
	{
		RetainedProfileAssets.AddUnique(SurfaceBinding);
	}
	if (IsValid(LoadedSurfaceConstraintDeformer))
	{
		RetainedProfileAssets.AddUnique(LoadedSurfaceConstraintDeformer);
	}
	RefreshRetainedSourceMeshes();
	if (CustomizationComponent)
	{
		// SetSkeletalMesh clears active morph values. Replay all unowned morphs;
		// the per-morph writer contract leaves V2-owned bindings untouched.
		bIsRestoring = true;
		CustomizationComponent->ReapplyCurrentMorphState();
		bIsRestoring = false;
	}
	SynchronizeMorphs();
	// SetMorphTarget writes override curves; force the follower update before
	// exposing the component so the first visible frame already uses V2 shapes.
	GarmentComponent->UpdateFollowerComponent();
	if (IsCertifiedSkinProfileActive(GarmentComponent, Profile))
	{
		FAppliedGarmentState& AddedState = AppliedGarments.FindChecked(GarmentComponent);
		AddedState.bWaitingForSkinProfile = false;
		TryExposeReadyGarment(GarmentComponent, AddedState);
	}

	LastStatus = FString::Printf(TEXT("Applied %s -> %s"), *GetNameSafe(GarmentComponent->GetSkeletalMeshAsset()), *GetNameSafe(FittedMesh));
	UE_LOG(LogEFClothingMorphV2, Display,
		TEXT("EFClothingMorphV2 APPLY owner=%s component=%s source=%s fitted=%s profile=%s catalog=%s backend=%d build=%s pending=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		*GarmentComponent->GetName(),
		*GetNameSafe(AppliedGarments.FindChecked(GarmentComponent).SourceMesh.Get()),
		*GetNameSafe(FittedMesh),
		*Profile->SkinWeightProfileName.ToString(),
		*CatalogRowName.ToString(),
		static_cast<int32>(CatalogRow->Backend),
		*Profile->BuildGuid.ToString(EGuidFormats::DigitsWithHyphens),
		(AppliedGarments.FindChecked(GarmentComponent).bWaitingForSkinProfile
			|| AppliedGarments.FindChecked(GarmentComponent).bMorphStateUnsafe
			|| (AppliedGarments.FindChecked(GarmentComponent).bUsesSurfaceWrapGPU
				&& AppliedGarments.FindChecked(GarmentComponent).SurfaceRuntimeState
					!= EEFClothingSurfaceRuntimeState::Ready))
			? TEXT("true") : TEXT("false"));

	return true;
}

bool UEFClothingFitRuntimeComponent::ValidateProfileForComponents(
	const UEFClothingFitProfile* Profile,
	USkeletalMeshComponent* GarmentComponent,
	USkeletalMesh* SourceMesh,
	USkeletalMeshComponent* BodyComponent,
	USkeletalMesh* FittedMesh,
	FString& OutFailureReason) const
{
	if (!IsValid(Profile) || !IsValid(GarmentComponent) || !IsValid(BodyComponent) || !IsValid(FittedMesh))
	{
		OutFailureReason = TEXT("Profile, fitted garment or exact body surface is unavailable.");
		return false;
	}

	USkeletalMesh* BodyMesh = BodyComponent->GetSkeletalMeshAsset();
	USkeletalMesh* CompatibilityMesh = Profile->CompatibilityReference.Get();
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		OutFailureReason = TEXT("Source garment or body mesh is invalid.");
		return false;
	}
	const FEFClothingGarmentRow* RuntimeCatalogRow = FindCatalogRow(SourceMesh, BodyMesh);
	const bool bSurfaceWrapGPU = RuntimeCatalogRow
		&& RuntimeCatalogRow->Backend == EEFClothingSurfaceBackend::SurfaceWrapGPU;
	constexpr float CertifiedTransformTolerance = 0.001f;
	const FTransform BodyToGarment = BodyComponent->GetComponentTransform().GetRelativeTransform(
		GarmentComponent->GetComponentTransform());
	const FVector BodyToGarmentScale = BodyToGarment.GetScale3D();
	if (bSurfaceWrapGPU
		&& (BodyToGarment.ContainsNaN()
			|| FMath::Abs(BodyToGarmentScale.X) <= UE_SMALL_NUMBER
			|| FMath::Abs(BodyToGarmentScale.Y) <= UE_SMALL_NUMBER
			|| FMath::Abs(BodyToGarmentScale.Z) <= UE_SMALL_NUMBER))
	{
		OutFailureReason = TEXT("Body-to-garment transform is non-finite or has degenerate scale.");
		return false;
	}
	if (!bSurfaceWrapGPU
		&& !GarmentComponent->GetComponentTransform().Equals(
			BodyComponent->GetComponentTransform(),
			CertifiedTransformTolerance))
	{
		OutFailureReason = TEXT("Garment and exact Female component transforms differ from the certified local-space contract.");
		return false;
	}
	if (!bSurfaceWrapGPU && BodyComponent->GetPredictedLODLevel() > 0)
	{
		OutFailureReason = TEXT("Exact Female is rendering an uncertified body LOD; V24 Tight profiles currently require LOD0.");
		return false;
	}
	bool bSurfacePolicyContractValid = Profile->ExcludedBodySurfaceTriangleCount >= 0
		&& (Profile->ExcludedBodySurfaceMaterialSlots.IsEmpty()
			? Profile->ExcludedBodySurfaceTriangleCount == 0
			: Profile->ExcludedBodySurfaceTriangleCount > 0)
		&& CanonicalMaterialSlots(Profile->ExcludedBodySurfaceMaterialSlots)
			== Profile->ExcludedBodySurfaceMaterialSlots
		&& CanonicalMaterialSlots(Profile->ExcludedBodyBoneBranches)
			== Profile->ExcludedBodyBoneBranches
		&& CanonicalNonEmptyStrings(Profile->ExcludedBodyMorphPrefixes)
			== Profile->ExcludedBodyMorphPrefixes;
	TSet<int32> ExcludedBodyBoneIndices;
	for (const FName ExcludedRootName : Profile->ExcludedBodyBoneBranches)
	{
		const int32 RootIndex = BodyMesh->GetRefSkeleton().FindBoneIndex(ExcludedRootName);
		if (RootIndex <= 0)
		{
			bSurfacePolicyContractValid = false;
			break;
		}
		for (int32 BoneIndex = 0; BoneIndex < BodyMesh->GetRefSkeleton().GetRawBoneNum(); ++BoneIndex)
		{
			for (int32 AncestorIndex = BoneIndex;
				AncestorIndex != INDEX_NONE;
				AncestorIndex = BodyMesh->GetRefSkeleton().GetParentIndex(AncestorIndex))
			{
				if (AncestorIndex == RootIndex)
				{
					ExcludedBodyBoneIndices.Add(BoneIndex);
					break;
				}
			}
		}
	}
	for (const FName RequiredBoneName : Profile->RequiredWeightedBones)
	{
		const int32 BodyBoneIndex = BodyMesh->GetRefSkeleton().FindBoneIndex(RequiredBoneName);
		if (BodyBoneIndex == INDEX_NONE || ExcludedBodyBoneIndices.Contains(BodyBoneIndex))
		{
			bSurfacePolicyContractValid = false;
			break;
		}
	}

	if (!IsValid(CompatibilityMesh))
	{
		OutFailureReason = TEXT("Compatibility reference is not prefetched; fit remains unapplied.");
		return false;
	}
	USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(BodyComponent);
	USkeletalMeshComponent* SkeletalLeader = Cast<USkeletalMeshComponent>(GarmentComponent->LeaderPoseComponent.Get());
	if (!IsValid(ExpectedPoseDriver))
	{
		OutFailureReason = TEXT("Exact Female LeaderPose chain is invalid, cyclic or contains a non-skeletal driver.");
		return false;
	}
	if (!IsValid(SkeletalLeader) || SkeletalLeader != ExpectedPoseDriver)
	{
		OutFailureReason = FString::Printf(
			TEXT("Garment LeaderPose did not resolve to Female's effective top-most driver (expected %s, actual %s)."),
			*GetNameSafe(ExpectedPoseDriver),
			*GetNameSafe(SkeletalLeader));
		return false;
	}

	const FVector BoundsExpansion = Profile->CompiledConcurrentBoundsExpansionCm;
	const bool bBackendProfileContractValid = bSurfaceWrapGPU
		? Profile->CompilerVersion == EFClothingMorphV26::CompilerVersion
			&& Profile->CompiledLODCount > 0
			&& Profile->CompiledLODCount == FittedMesh->GetLODNum()
			&& Profile->SurfaceBinding.IsValid()
		: (Profile->CompilerVersion == 25
			|| Profile->CompilerVersion == EFClothingMorphV26::CompilerVersion)
			&& Profile->CompiledLODCount == 1
			&& Profile->FitMode == EEFClothingFitMode::Tight
			&& (Profile->CompilerVersion != 25 || Profile->SurfaceBinding.IsNull());
	const bool bV25MorphBakeContractValid =
		FMath::IsFinite(Profile->DefaultClearanceValue)
		&& FMath::IsNearlyEqual(Profile->DefaultClearanceValue, 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsFinite(Profile->CertifiedClearanceMultiplierMin)
		&& FMath::IsFinite(Profile->CertifiedClearanceMultiplierMax)
		&& FMath::IsNearlyEqual(Profile->CertifiedClearanceMultiplierMin, 1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(Profile->CertifiedClearanceMultiplierMax, 2.0f, KINDA_SMALL_NUMBER)
		&& Profile->CertifiedClearanceTierCount == 9
		&& FMath::IsFinite(Profile->MinimumCertifiedOffsetGapCm)
		&& Profile->MinimumCertifiedOffsetGapCm >= Profile->CompiledMinimumClearanceCm - 0.001f
		&& FMath::IsFinite(Profile->CompiledMaximumMorphRepairCm)
		&& Profile->CompiledMaximumMorphRepairCm > 0.0f
		&& FMath::IsFinite(Profile->CompiledMaximumMorphDisplacementCm)
		&& Profile->CompiledMaximumMorphDisplacementCm >= 0.0f
		&& FMath::IsFinite(Profile->CompiledMorphThresholdPositionCm)
		&& Profile->CompiledMorphThresholdPositionCm >= 0.0f
		&& Profile->PostThresholdAlteredDeltaCount >= 0
		&& Profile->ClearanceValidatedMorphCount == Profile->MorphBindings.Num()
		&& Profile->MorphClearanceSampleCount >= 2
		&& Profile->MorphClearanceSampleCount <= 8
		&& Profile->GeneratedMorphSampleCount >= Profile->MorphBindings.Num()
		&& (Profile->MorphBindings.IsEmpty()
			? Profile->MaximumMorphSamplesPerBinding == 0
			: Profile->MaximumMorphSamplesPerBinding >= 1)
		&& Profile->MaximumMorphSamplesPerBinding <= 64
		&& Profile->SteppedMorphIntervalCount >= 0
		&& Profile->SteppedMorphIntervalCount <= Profile->GeneratedMorphSampleCount
		&& Profile->IdentityMorphSampleCount >= 0
		&& Profile->IdentityMorphSampleCount <= Profile->GeneratedMorphSampleCount
		&& FMath::IsFinite(Profile->MinimumSampledMorphGapCm)
		&& Profile->MinimumSampledMorphGapCm >= Profile->CompiledMinimumClearanceCm - 0.001f
		&& FMath::IsFinite(Profile->CompiledMorphActivationEpsilon)
		&& Profile->CompiledMorphActivationEpsilon == 0.0f;
	if (!bSurfacePolicyContractValid
		|| !bBackendProfileContractValid
		|| (!bSurfaceWrapGPU && !bV25MorphBakeContractValid)
		|| Profile->SkinWeightProfileName != CertifiedAutoFitProfileName
		|| !Profile->BuildGuid.IsValid()
		|| Profile->PenetratingVertexCountAfter != 0
		|| !FMath::IsFinite(Profile->MinimumSignedGapAfterCm)
		|| !FMath::IsFinite(Profile->CompiledMinimumClearanceCm)
		|| !FMath::IsFinite(Profile->CompiledClearanceReserveCm)
		|| Profile->CompiledClearanceReserveCm <= 0.0f
		|| !FMath::IsNearlyEqual(Profile->CompiledClearanceReserveCm, 0.10f, KINDA_SMALL_NUMBER)
		|| Profile->MinimumSignedGapAfterCm
			< Profile->CompiledMinimumClearanceCm + Profile->CompiledClearanceReserveCm - 0.001f
		|| Profile->RequiredWeightedBones.IsEmpty()
		|| Profile->ReconciledSplitVertexCount < 0
		|| Profile->CertifiedSkinWeightVertexCount <= 0
		|| (!bSurfaceWrapGPU
			&& Profile->CertifiedSkinWeightVertexCount != Profile->SourceVertexCount)
		|| !FMath::IsFinite(BoundsExpansion.X)
		|| !FMath::IsFinite(BoundsExpansion.Y)
		|| !FMath::IsFinite(BoundsExpansion.Z)
		|| BoundsExpansion.X < 0.0f
		|| BoundsExpansion.Y < 0.0f
		|| BoundsExpansion.Z < 0.0f
		|| !FMath::IsFinite(Profile->CompiledConcurrentSphereExpansionCm)
		|| Profile->CompiledConcurrentSphereExpansionCm < 0.0f)
	{
		OutFailureReason = TEXT("Profile compiler version, LOD or measured-clearance contract is invalid.");
		return false;
	}
	const FSkeletalMeshLODInfo* FittedLODInfo = FittedMesh->GetLODInfo(0);
	const FSkeletalMeshLODInfo* BodyLODInfo = BodyMesh->GetLODInfo(0);
	if (!FittedLODInfo
		|| !BodyLODInfo
		|| (!bSurfaceWrapGPU && !FMath::IsNearlyEqual(
			FittedLODInfo->BuildSettings.MorphThresholdPosition,
			Profile->CompiledMorphThresholdPositionCm,
			KINDA_SMALL_NUMBER)))
	{
		OutFailureReason = TEXT("Generated garment morph cook threshold differs from its V24 certificate.");
		return false;
	}

	if (Profile->SourceGarment.ToSoftObjectPath() != FSoftObjectPath(SourceMesh)
		|| Profile->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh)
		|| Profile->FittedGarment.ToSoftObjectPath() != FSoftObjectPath(FittedMesh)
		|| Profile->CompatibilityReference.ToSoftObjectPath() != FSoftObjectPath(CompatibilityMesh))
	{
		OutFailureReason = TEXT("Profile asset paths do not match the runtime mesh set.");
		return false;
	}
	if (bSurfaceWrapGPU)
	{
		const UEFClothingSurfaceBinding* SurfaceBinding = Profile->SurfaceBinding.Get();
		if (!IsValid(SurfaceBinding)
			|| SurfaceBinding->CompilerVersion != EFClothingMorphV26::CompilerVersion
			|| SurfaceBinding->SchemaVersion != EFClothingMorphV26::SurfaceBindingSchemaVersion
			|| SurfaceBinding->BuildGuid != Profile->BuildGuid
			|| SurfaceBinding->SourceGarment.ToSoftObjectPath() != FSoftObjectPath(SourceMesh)
			|| SurfaceBinding->FittedGarment.ToSoftObjectPath() != FSoftObjectPath(FittedMesh)
			|| SurfaceBinding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh)
			|| SurfaceBinding->SourceContentFingerprint != Profile->SourceContentFingerprint
			|| SurfaceBinding->FittedContentFingerprint != Profile->FittedContentFingerprint
			|| SurfaceBinding->BodyContentFingerprint != Profile->BodyContentFingerprint
			|| SurfaceBinding->SourceSkeletonFingerprint != Profile->SourceSkeletonFingerprint
			|| SurfaceBinding->FittedSkeletonFingerprint != Profile->FittedSkeletonFingerprint
			|| SurfaceBinding->BodySkeletonFingerprint != Profile->BodySkeletonFingerprint
			|| SurfaceBinding->SharedSkeletonFingerprint != Profile->SharedSkeletonFingerprint
			|| CanonicalMaterialSlots(SurfaceBinding->ExcludedBodySurfaceMaterialSlots)
				!= CanonicalMaterialSlots(Profile->ExcludedBodySurfaceMaterialSlots)
			|| SurfaceBinding->LODPairBindings.IsEmpty())
		{
			OutFailureReason = TEXT("V26 SurfaceBinding identity, fingerprints, exclusions or build certificate do not match the profile.");
			return false;
		}
	}
	if (FittedMesh->GetDefaultMeshDeformer() != BodyMesh->GetDefaultMeshDeformer()
		|| FittedMesh->GetTargetMeshDeformers() != BodyMesh->GetTargetMeshDeformers()
		|| FittedLODInfo->bAllowMeshDeformer != BodyLODInfo->bAllowMeshDeformer
		|| FittedLODInfo->bBuildHalfEdgeBuffers != BodyLODInfo->bBuildHalfEdgeBuffers
		|| FittedMesh->HasHalfEdgeBuffer(0) != BodyMesh->HasHalfEdgeBuffer(0))
	{
		OutFailureReason = TEXT("Generated garment does not preserve the exact Female mesh-deformer contract.");
		return false;
	}
	const TArray<FMatrix44f>& FittedInverseBind = FittedMesh->GetRefBasesInvMatrix();
	const TArray<FMatrix44f>& BodyInverseBind = BodyMesh->GetRefBasesInvMatrix();
	if (FittedInverseBind.Num() != BodyInverseBind.Num()
		|| FittedInverseBind.Num() != BodyMesh->GetRefSkeleton().GetRawBoneNum())
	{
		OutFailureReason = TEXT("Generated garment/Female inverse-bind counts differ.");
		return false;
	}
	for (int32 BoneIndex = 0; BoneIndex < FittedInverseBind.Num(); ++BoneIndex)
	{
		if (!FittedInverseBind[BoneIndex].Equals(BodyInverseBind[BoneIndex], 1.e-4f))
		{
			OutFailureReason = FString::Printf(
				TEXT("Generated garment inverse bind differs from Female at raw bone %d."),
				BoneIndex);
			return false;
		}
	}
	if (GarmentComponent->GetSkeletalMeshAsset() == FittedMesh
		&& !HasMatchingActiveLOD0Deformer(GarmentComponent, BodyComponent))
	{
		OutFailureReason = TEXT("Generated garment and Female do not use the same instantiated LOD0 deformer.");
		return false;
	}

	if (SourceMesh->GetSkeleton() != BodyMesh->GetSkeleton()
		|| SourceMesh->GetSkeleton() != FittedMesh->GetSkeleton()
		|| SourceMesh->GetSkeleton() != CompatibilityMesh->GetSkeleton())
	{
		OutFailureReason = TEXT("USkeleton object mismatch; fitting aborted fail-closed.");
		return false;
	}
	if (Profile->SharedSkeletonFingerprint.IsEmpty()
		|| EFClothingSkeleton::BuildSharedSkeletonFingerprint(SourceMesh->GetSkeleton())
			!= Profile->SharedSkeletonFingerprint)
	{
		OutFailureReason = TEXT("Shared USkeleton hierarchy, virtual-bone or retarget metadata changed after compilation.");
		return false;
	}
#if WITH_EDITOR
	if (Profile->SharedSkeletonEditorFingerprint.IsEmpty()
		|| EFClothingSkeleton::BuildSharedSkeletonEditorFingerprint(SourceMesh->GetSkeleton())
			!= Profile->SharedSkeletonEditorFingerprint)
	{
		OutFailureReason = TEXT("Shared USkeleton editor retarget-source/reference-pose metadata changed after compilation.");
		return false;
	}
#endif

	// The editable source mesh is authoring input, not the geometry rendered by V2.
	// Once profile + fitted mesh + binding provenance has passed above, validating
	// the fitted asset against *live* source bounds would make a native Deform edit
	// hide the garment before the editor freshness gate can rebuild it. Validate the
	// immutable fitted output itself here and keep using the last known-good bundle.
	const FBoxSphereBounds FittedBounds = FittedMesh->GetImportedBounds();
	if (FittedBounds.Origin.ContainsNaN()
		|| FittedBounds.BoxExtent.ContainsNaN()
		|| !FMath::IsFinite(FittedBounds.SphereRadius)
		|| FittedBounds.BoxExtent.X < 0.0
		|| FittedBounds.BoxExtent.Y < 0.0
		|| FittedBounds.BoxExtent.Z < 0.0
		|| FittedBounds.SphereRadius <= 0.0)
	{
		OutFailureReason = TEXT("Generated garment has invalid fitted bounds.");
		return false;
	}

	if (!Profile->SourceSkeletonFingerprint.IsEmpty()
		&& EFClothingSkeleton::BuildFingerprint(SourceMesh) != Profile->SourceSkeletonFingerprint)
	{
		OutFailureReason = TEXT("Source garment skeleton fingerprint changed after compilation.");
		return false;
	}

	if (!Profile->BodySkeletonFingerprint.IsEmpty()
		&& EFClothingSkeleton::BuildFingerprint(BodyMesh) != Profile->BodySkeletonFingerprint)
	{
		OutFailureReason = TEXT("Body skeleton fingerprint changed after compilation.");
		return false;
	}
	if (!Profile->CompatibilitySkeletonFingerprint.IsEmpty()
		&& EFClothingSkeleton::BuildFingerprint(CompatibilityMesh) != Profile->CompatibilitySkeletonFingerprint)
	{
		OutFailureReason = TEXT("Multiple compatibility fingerprint changed after compilation.");
		return false;
	}
	if (!Profile->FittedSkeletonFingerprint.IsEmpty()
		&& EFClothingSkeleton::BuildFingerprint(FittedMesh) != Profile->FittedSkeletonFingerprint)
	{
		OutFailureReason = TEXT("Generated garment skeleton fingerprint changed after compilation.");
		return false;
	}
	bool bSourceAuthoringStale = false;
#if WITH_EDITORONLY_DATA
	if (!Profile->SourcePackageGuid.IsEmpty()
		&& SourceMesh->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) != Profile->SourcePackageGuid)
	{
		// Native source-mesh edits intentionally do not invalidate the rendered
		// fitted/binding pair. The editor gate refreshes this bundle before Play or
		// packaging; if an edit occurs while running, the previous certified bundle
		// remains visible until that refresh.
		bSourceAuthoringStale = true;
	}
	if (!Profile->BodyPackageGuid.IsEmpty()
		&& BodyMesh->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) != Profile->BodyPackageGuid)
	{
		OutFailureReason = TEXT("Body surface package changed after fit compilation; recompile the artifact.");
		return false;
	}
	if (!Profile->CompatibilityPackageGuid.IsEmpty()
		&& CompatibilityMesh->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) != Profile->CompatibilityPackageGuid)
	{
		OutFailureReason = TEXT("Multiple compatibility package identity changed after fit compilation.");
		return false;
	}
	if (!Profile->FittedPackageGuid.IsEmpty()
		&& FittedMesh->GetOutermost()->GetPersistentGuid().ToString(EGuidFormats::DigitsWithHyphens) != Profile->FittedPackageGuid)
	{
		OutFailureReason = TEXT("Generated garment package identity changed after fit compilation.");
		return false;
	}
#endif

#if WITH_EDITOR
	if (EFClothingSkeleton::BuildContentFingerprint(SourceMesh) != Profile->SourceContentFingerprint)
	{
		bSourceAuthoringStale = true;
	}
	if (EFClothingSkeleton::BuildContentFingerprint(BodyMesh) != Profile->BodyContentFingerprint
		|| EFClothingSkeleton::BuildContentFingerprint(CompatibilityMesh) != Profile->CompatibilityContentFingerprint
		|| EFClothingSkeleton::BuildContentFingerprint(FittedMesh) != Profile->FittedContentFingerprint)
	{
		OutFailureReason = TEXT("Body, compatibility or generated mesh content changed after fit compilation; regenerate the derived artifact.");
		return false;
	}
#endif

	const FSoftObjectPath SourcePath(SourceMesh);
	if (bSourceAuthoringStale)
	{
		if (!WarnedStaleSourceGarmentPaths.Contains(SourcePath))
		{
			WarnedStaleSourceGarmentPaths.Add(SourcePath);
			UE_LOG(
				LogEFClothingMorphV2,
				Warning,
				TEXT("Editable source garment %s changed after compilation; keeping the last known-good fitted mesh and surface binding visible until the automatic editor refresh."),
				*SourcePath.ToString());
		}
	}
	else
	{
		WarnedStaleSourceGarmentPaths.Remove(SourcePath);
	}

	const bool bSkinProfileExists = FittedMesh->GetSkinWeightProfiles().ContainsByPredicate([Profile](const FSkinWeightProfileInfo& Info)
	{
		return Info.Name == Profile->SkinWeightProfileName;
	});
	if (!bSkinProfileExists)
	{
		OutFailureReason = TEXT("Generated EF_AutoFit skin-weight profile is missing.");
		return false;
	}

	// GeometryFitFallback retains the complete V25 morph-bake certificate. The
	// V26 surface backend follows the final animated body geometry directly and
	// deliberately does not depend on body-morph names, pair grids or baked
	// clearance tiers. EF_AutoFit and the rest sculpt remain mandatory above.
	if (!bSurfaceWrapGPU)
	{
	UMorphTarget* ClearanceMorph = FittedMesh->FindMorphTarget(Profile->ClearanceMorphName);
	if (!IsValid(ClearanceMorph) || ClearanceMorph->GetNumDeltasForLOD(0) == 0)
	{
		OutFailureReason = TEXT("Generated V25 clearance morph is missing.");
		return false;
	}
	TSet<FName> UniqueBodyMorphNames;
	for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
	{
		if (Binding.BodyMorph.IsNone()
			|| Binding.BodyMorph == Profile->ClearanceMorphName
			|| UniqueBodyMorphNames.Contains(Binding.BodyMorph))
		{
			OutFailureReason = TEXT("Piecewise profile has duplicate or reserved body-morph names.");
			return false;
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
		UMorphTarget* BodyMorph = BodyMesh->FindMorphTarget(Binding.BodyMorph);
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
			|| IsValid(FittedMesh->FindMorphTarget(Binding.BodyMorph))
			|| Binding.Samples.IsEmpty()
			|| Binding.Samples.Num() > 64
			|| !IsValid(BodyMorph) || !BodyMorph->HasDataForLOD(0) || BodyMorph->GetNumDeltasForLOD(0) == 0)
		{
			OutFailureReason = FString::Printf(
				TEXT("Piecewise morph binding %s has an invalid body target, certified range or sample count."),
				*Binding.BodyMorph.ToString());
			return false;
		}

		float PreviousBodyValue = Binding.MinimumCertifiedValue;
		FName ExpectedLegacyGarmentMorph = NAME_None;
		for (int32 SampleIndex = 0; SampleIndex < Binding.Samples.Num(); ++SampleIndex)
		{
			const FEFClothingMorphSample& Sample = Binding.Samples[SampleIndex];
			UMorphTarget* FittedMorph = Sample.bIdentity ? nullptr : FittedMesh->FindMorphTarget(Sample.GarmentMorph);
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
				OutFailureReason = FString::Printf(
					TEXT("Piecewise morph binding %s has an invalid or non-renderable sample at index %d."),
					*Binding.BodyMorph.ToString(),
					SampleIndex);
				return false;
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
		if (!FMath::IsNearlyEqual(PreviousBodyValue, Binding.MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			|| Binding.GarmentMorph != ExpectedLegacyGarmentMorph)
		{
			OutFailureReason = FString::Printf(
				TEXT("Piecewise morph binding %s does not cover its complete certified range."),
				*Binding.BodyMorph.ToString());
			return false;
		}
		ActualGeneratedMorphSampleCount += Binding.Samples.Num();
		ActualMaximumMorphSamplesPerBinding = FMath::Max(
			ActualMaximumMorphSamplesPerBinding,
			Binding.Samples.Num());
	}
	if (Profile->GeneratedMorphSampleCount != ActualGeneratedMorphSampleCount
		|| Profile->MaximumMorphSamplesPerBinding != ActualMaximumMorphSamplesPerBinding
		|| Profile->SteppedMorphIntervalCount != ActualSteppedMorphIntervalCount
		|| Profile->IdentityMorphSampleCount != ActualIdentityMorphSampleCount)
	{
		OutFailureReason = TEXT("Piecewise morph sample metrics do not match the generated bindings.");
		return false;
	}

	TSet<FName> MonitoredBodyMorphNames;
	FName PreviousMonitoredBodyMorph = NAME_None;
	bool bHasPreviousMonitoredBodyMorph = false;
	for (FName MonitoredBodyMorph : Profile->MonitoredBodyMorphNames)
	{
		UMorphTarget* BodyMorph = BodyMesh->FindMorphTarget(MonitoredBodyMorph);
		const FString MonitoredMorphString = MonitoredBodyMorph.ToString();
		const bool bExcludedMorphNamespace = Profile->ExcludedBodyMorphPrefixes.ContainsByPredicate(
			[&MonitoredMorphString](const FString& Prefix)
			{
				return MonitoredMorphString.StartsWith(Prefix, ESearchCase::CaseSensitive);
			});
		if (MonitoredBodyMorph.IsNone()
			|| MonitoredBodyMorph == Profile->ClearanceMorphName
			|| MonitoredBodyMorphNames.Contains(MonitoredBodyMorph)
			|| bExcludedMorphNamespace
			|| (bHasPreviousMonitoredBodyMorph
				&& !PreviousMonitoredBodyMorph.LexicalLess(MonitoredBodyMorph))
			|| !IsValid(BodyMorph)
			|| !BodyMorph->HasDataForLOD(0)
			|| BodyMorph->GetNumDeltasForLOD(0) <= 0)
		{
			OutFailureReason = TEXT("V24 monitored body-morph names are empty, duplicated, unsorted or absent from the exact body.");
			return false;
		}
		MonitoredBodyMorphNames.Add(MonitoredBodyMorph);
		PreviousMonitoredBodyMorph = MonitoredBodyMorph;
		bHasPreviousMonitoredBodyMorph = true;
	}
	for (FName BoundBodyMorph : UniqueBodyMorphNames)
	{
		if (!MonitoredBodyMorphNames.Contains(BoundBodyMorph))
		{
			OutFailureReason = FString::Printf(
				TEXT("V24 monitored body-morph contract omits binding %s."),
				*BoundBodyMorph.ToString());
			return false;
		}
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
			+ TEXT("\x1F")
			+ Certificate.SecondBodyMorph.ToString();
		if (Certificate.FirstBodyMorph.IsNone()
			|| Certificate.SecondBodyMorph.IsNone()
			|| !Certificate.FirstBodyMorph.LexicalLess(Certificate.SecondBodyMorph)
			|| UniquePairKeys.Contains(PairKey)
			|| !UniqueBodyMorphNames.Contains(Certificate.FirstBodyMorph)
			|| !UniqueBodyMorphNames.Contains(Certificate.SecondBodyMorph)
			|| !MonitoredBodyMorphNames.Contains(Certificate.FirstBodyMorph)
			|| !MonitoredBodyMorphNames.Contains(Certificate.SecondBodyMorph)
			|| !FirstCertifiedBinding
			|| !SecondCertifiedBinding
			|| !FMath::IsNearlyEqual(Certificate.FirstMinimumCertifiedValue, FirstCertifiedBinding->MinimumCertifiedValue, KINDA_SMALL_NUMBER)
			|| !FMath::IsNearlyEqual(Certificate.FirstMaximumCertifiedValue, FirstCertifiedBinding->MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			|| !FMath::IsNearlyEqual(Certificate.SecondMinimumCertifiedValue, SecondCertifiedBinding->MinimumCertifiedValue, KINDA_SMALL_NUMBER)
			|| !FMath::IsNearlyEqual(Certificate.SecondMaximumCertifiedValue, SecondCertifiedBinding->MaximumCertifiedValue, KINDA_SMALL_NUMBER)
			|| Certificate.GridResolution != 4
			|| Certificate.ProbeCountPerAxis != 3
			|| Certificate.CertifiedOffsetTierCount != EFClothingMorphV25::ClearanceTierCount
			|| Certificate.Cells.Num() != 16
			|| !FMath::IsFinite(Certificate.MinimumCertifiedGapCm)
			|| Certificate.MinimumCertifiedGapCm < Profile->CompiledMinimumClearanceCm - 0.001f)
		{
			OutFailureReason = TEXT("V24 pair certificate names, ranges, 4x4 grid, probes, tiers or gap are invalid.");
			return false;
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
			UMorphTarget* PairCellMorph = FittedMesh->FindMorphTarget(Cell.GarmentMorph);
			if (Cell.FirstCellIndex < 0 || Cell.FirstCellIndex >= Certificate.GridResolution
				|| Cell.SecondCellIndex < 0 || Cell.SecondCellIndex >= Certificate.GridResolution
				|| UniqueCellCoordinates.Contains(CoordinateKey)
				|| Cell.GarmentMorph.IsNone()
				|| Cell.GarmentMorph == Profile->ClearanceMorphName
				|| UniqueBodyMorphNames.Contains(Cell.GarmentMorph)
				|| UniquePiecewiseMorphNames.Contains(Cell.GarmentMorph)
				|| UniquePairCellMorphNames.Contains(Cell.GarmentMorph)
				|| !IsValid(PairCellMorph)
				|| !PairCellMorph->HasDataForLOD(0)
				|| PairCellMorph->GetNumDeltasForLOD(0) <= 0
				|| !FMath::IsNearlyEqual(Cell.FirstMinimumValue, ExpectedFirstMinimum, KINDA_SMALL_NUMBER)
				|| !FMath::IsNearlyEqual(Cell.FirstMaximumValue, ExpectedFirstMaximum, KINDA_SMALL_NUMBER)
				|| !FMath::IsNearlyEqual(Cell.SecondMinimumValue, ExpectedSecondMinimum, KINDA_SMALL_NUMBER)
				|| !FMath::IsNearlyEqual(Cell.SecondMaximumValue, ExpectedSecondMaximum, KINDA_SMALL_NUMBER)
				|| !FMath::IsFinite(Cell.MinimumCertifiedGapCm)
				|| Cell.MinimumCertifiedGapCm < Profile->CompiledMinimumClearanceCm - 0.001f
				|| !EFClothingMorphV25::IsCertifiedClearanceMultiplier(
					Cell.MinimumClearanceMultiplier)
				|| Cell.CertifiedBodyProbeCount != ExpectedBodyProbeCount
				|| Cell.CertifiedOffsetEvaluationCount != ExpectedOffsetEvaluationCount)
			{
				OutFailureReason = TEXT("V24 pair certificate contains an invalid, duplicate or non-renderable cell.");
				return false;
			}

			UniqueCellCoordinates.Add(CoordinateKey);
			UniquePairCellMorphNames.Add(Cell.GarmentMorph);
			CertificateMinimumGap = FMath::Min(CertificateMinimumGap, Cell.MinimumCertifiedGapCm);
			++ActualGeneratedPairCellMorphCount;
			ActualPairBodyProbeCount += Cell.CertifiedBodyProbeCount;
			ActualPairOffsetEvaluationCount += Cell.CertifiedOffsetEvaluationCount;
		}
		if (UniqueCellCoordinates.Num() != Certificate.GridResolution * Certificate.GridResolution
			|| !FMath::IsNearlyEqual(
				Certificate.MinimumCertifiedGapCm,
				CertificateMinimumGap,
				0.001f))
		{
			OutFailureReason = TEXT("V24 pair certificate does not cover every grid cell or reports a mismatched minimum gap.");
			return false;
		}
		ActualMinimumSampledPairGap = FMath::Min(ActualMinimumSampledPairGap, CertificateMinimumGap);
	}
	if (Profile->MorphPairCertificates.IsEmpty())
	{
		ActualMinimumSampledPairGap = 0.0f;
	}
	if (Profile->CertifiedMorphPairCount != Profile->MorphPairCertificates.Num()
		|| Profile->GeneratedPairCellMorphCount != ActualGeneratedPairCellMorphCount
		|| Profile->PairBodyProbeCount != ActualPairBodyProbeCount
		|| Profile->PairOffsetEvaluationCount != ActualPairOffsetEvaluationCount
		|| !FMath::IsFinite(Profile->MinimumSampledPairGapCm)
		|| (Profile->MorphPairCertificates.IsEmpty()
			? !FMath::IsNearlyZero(Profile->MinimumSampledPairGapCm, KINDA_SMALL_NUMBER)
			: Profile->MinimumSampledPairGapCm < Profile->CompiledMinimumClearanceCm - 0.001f
				|| !FMath::IsNearlyEqual(
					Profile->MinimumSampledPairGapCm,
					ActualMinimumSampledPairGap,
					0.001f)))
	{
		OutFailureReason = TEXT("V24 pair certificate metrics do not match their stored cells.");
		return false;
	}
	}

	auto ValidateWeightedBoneContract = [Profile, FittedMesh](
		const USkeletalMesh* PoseMesh,
		const TCHAR* PoseMeshLabel,
		bool bRequireBindPoseMatch,
		FString& FailureReason) -> bool
	{
		if (!IsValid(PoseMesh))
		{
			FailureReason = FString::Printf(TEXT("%s pose mesh is unavailable."), PoseMeshLabel);
			return false;
		}
		const FReferenceSkeleton& FittedReference = FittedMesh->GetRefSkeleton();
		const FReferenceSkeleton& PoseReference = PoseMesh->GetRefSkeleton();
		constexpr float ReferencePoseTolerance = 0.001f;
		for (FName BoneName : Profile->RequiredWeightedBones)
		{
			const int32 FittedIndex = FittedReference.FindBoneIndex(BoneName);
			const int32 PoseIndex = PoseReference.FindBoneIndex(BoneName);
			if (FittedIndex == INDEX_NONE || PoseIndex == INDEX_NONE)
			{
				FailureReason = FString::Printf(
					TEXT("%s cannot animate required weighted bone %s."),
					PoseMeshLabel,
					*BoneName.ToString());
				return false;
			}
			auto ParentName = [](const FReferenceSkeleton& Reference, int32 BoneIndex) -> FName
			{
				const int32 ParentIndex = Reference.GetParentIndex(BoneIndex);
				return ParentIndex == INDEX_NONE ? NAME_None : Reference.GetBoneName(ParentIndex);
			};
			if (ParentName(FittedReference, FittedIndex) != ParentName(PoseReference, PoseIndex))
			{
				FailureReason = FString::Printf(
					TEXT("%s hierarchy differs for weighted bone %s."),
					PoseMeshLabel,
					*BoneName.ToString());
				return false;
			}
			if (bRequireBindPoseMatch
				&& !FittedReference.GetRefBonePose()[FittedIndex].Equals(
					PoseReference.GetRefBonePose()[PoseIndex], ReferencePoseTolerance))
			{
				FailureReason = FString::Printf(
					TEXT("%s bind pose differs for weighted bone %s."),
					PoseMeshLabel,
					*BoneName.ToString());
				return false;
			}
		}
		return true;
	};

	// V24 transfers exact Female-indexed weights and inverse bind matrices. The
	// source garment may retain a different bind pose, while the generated mesh
	// must match Female exactly. Multiple and the effective runtime driver are
	// pose providers only: validate hierarchy and required weighted bones there,
	// never their independent mesh bind transforms.
	if (!EFClothingSkeleton::AreBoneHierarchiesCompatible(SourceMesh, FittedMesh, &OutFailureReason)
		|| !EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(FittedMesh, BodyMesh, &OutFailureReason)
		|| !EFClothingSkeleton::AreSharedBoneHierarchiesCompatible(FittedMesh, CompatibilityMesh, &OutFailureReason)
		|| !EFClothingSkeleton::AreSharedBoneHierarchiesCompatible(
			FittedMesh,
			SkeletalLeader->GetSkeletalMeshAsset(),
			&OutFailureReason))
	{
		return false;
	}
	if (!ValidateWeightedBoneContract(BodyMesh, TEXT("Body"), true, OutFailureReason)
		|| !ValidateWeightedBoneContract(CompatibilityMesh, TEXT("Compatibility"), false, OutFailureReason)
		|| !ValidateWeightedBoneContract(
			SkeletalLeader->GetSkeletalMeshAsset(),
			TEXT("Effective LeaderPose"),
			false,
			OutFailureReason))
	{
		return false;
	}

	return true;
}

void UEFClothingFitRuntimeComponent::GatherMorphPoseSources(
	USkeletalMeshComponent* BodyComponent,
	USkeletalMeshComponent* GarmentComponent,
	TArray<USkeletalMeshComponent*>& OutPoseSources) const
{
	(void)GarmentComponent;
	OutPoseSources.Reset();
	if (!IsValid(BodyComponent))
	{
		return;
	}

	USkeletalMeshComponent* Current = BodyComponent;
	for (int32 Depth = 0; IsValid(Current) && Depth < 8 && !OutPoseSources.Contains(Current); ++Depth)
	{
		OutPoseSources.Add(Current);
		Current = Cast<USkeletalMeshComponent>(Current->LeaderPoseComponent.Get());
	}
}

float UEFClothingFitRuntimeComponent::ResolveBodyMorphValue(
	USkeletalMeshComponent* BodyComponent,
	const TArray<USkeletalMeshComponent*>& PoseSources,
	FName MorphName) const
{
	if (!IsValid(BodyComponent) || MorphName.IsNone())
	{
		return 0.0f;
	}
	USkeletalMesh* BodyMesh = BodyComponent->GetSkeletalMeshAsset();
	if (IsValid(BodyMesh))
	{
		int32 MorphIndex = INDEX_NONE;
		if (BodyMesh->FindMorphTargetAndIndex(MorphName, MorphIndex)
			&& BodyComponent->MorphTargetWeights.IsValidIndex(MorphIndex))
		{
			// TG_PostUpdateWork observes the exact weight sent to Female's skinning
			// path after leader propagation, animation, post-process and explicit
			// overrides have been composed. This is authoritative even at zero: if
			// UE has not applied a newly-authored override yet, the body is not using
			// it this frame either. The chain scan is only for an unavailable array.
			const float EffectiveValue = BodyComponent->MorphTargetWeights[MorphIndex];
			return EffectiveValue;
		}
	}

	for (const USkeletalMeshComponent* PoseSource : PoseSources)
	{
		if (IsValid(PoseSource))
		{
			// Conservative pre-evaluation fallback: any explicit or animated value
			// on Female or its real leader chain is safer than exposing the rest
			// garment for a frame. The evaluated array above remains authoritative.
			const float ExplicitValue = PoseSource->GetMorphTarget(MorphName);
			if (ExplicitValue != 0.0f)
			{
				return ExplicitValue;
			}
			if (const UAnimInstance* AnimInstance = PoseSource->GetAnimInstance())
			{
				const float CurveValue = AnimInstance->GetCurveValue(MorphName);
				if (CurveValue != 0.0f)
				{
					return CurveValue;
				}
			}
		}
	}

	return 0.0f;
}

bool UEFClothingFitRuntimeComponent::TryInstallSurfaceConstraint(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!State.bUsesSurfaceWrapGPU)
	{
		return true;
	}

	USkeletalMeshComponent* BodyComponent = State.BodyMesh.Get();
	const UEFClothingFitProfile* Profile = State.Profile.Get();
	const UEFClothingSurfaceBinding* Binding = State.SurfaceBinding.Get();
	USkeletalMesh* GarmentMesh = IsValid(GarmentComponent)
		? GarmentComponent->GetSkeletalMeshAsset()
		: nullptr;
	USkeletalMesh* BodyMesh = IsValid(BodyComponent)
		? BodyComponent->GetSkeletalMeshAsset()
		: nullptr;
	if (!IsValid(GarmentComponent)
		|| !IsValid(BodyComponent)
		|| !IsValid(Profile)
		|| !IsValid(Binding)
		|| !IsValid(GarmentMesh)
		|| !IsValid(BodyMesh)
		|| !IsValid(LoadedSurfaceConstraintDeformer))
	{
		OutFailureReason = TEXT("Surface runtime assets, exact Female component or fitted garment are unavailable.");
		return false;
	}
	if (Binding->CompilerVersion != EFClothingMorphV26::CompilerVersion
		|| Binding->SchemaVersion != EFClothingMorphV26::SurfaceBindingSchemaVersion
		|| !Binding->BuildGuid.IsValid()
		|| Binding->BuildGuid != Profile->BuildGuid
		|| Binding->SourceGarment.ToSoftObjectPath() != Profile->SourceGarment.ToSoftObjectPath()
		|| Binding->FittedGarment.ToSoftObjectPath() != FSoftObjectPath(GarmentMesh)
		|| Binding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh)
		|| Binding->SharedSkeletonFingerprint != Profile->SharedSkeletonFingerprint
		|| CanonicalMaterialSlots(Binding->ExcludedBodySurfaceMaterialSlots)
			!= CanonicalMaterialSlots(Profile->ExcludedBodySurfaceMaterialSlots))
	{
		OutFailureReason = TEXT("Surface binding identity, version, skeleton, exclusions or build GUID do not match the active V26 profile.");
		return false;
	}

	const int32 GarmentLODIndex = FMath::Max(GarmentComponent->GetPredictedLODLevel(), 0);
	const int32 BodyLODIndex = FMath::Max(BodyComponent->GetPredictedLODLevel(), 0);
	const FEFClothingSurfaceLODPairBinding* LODPair = Binding->FindLODPair(
		GarmentLODIndex,
		BodyLODIndex);
	if (!LODPair || !LODPair->bCertified)
	{
		OutFailureReason = FString::Printf(
			TEXT("No certified SurfaceWrapGPU binding exists for garment LOD %d / body LOD %d."),
			GarmentLODIndex,
			BodyLODIndex);
		return false;
	}

	if (UEFClothingSurfaceDeformerProducer* ExistingProducer = State.SurfaceProducer.Get())
	{
		if (ExistingProducer->IsInstalledFor(
			GarmentComponent,
			BodyComponent,
			GarmentLODIndex,
			BodyLODIndex))
		{
			return true;
		}
	}
	ReleaseSurfaceConstraint(State);

	UEFClothingSurfaceDeformerProducer* Producer = NewObject<UEFClothingSurfaceDeformerProducer>(
		this,
		NAME_None,
		RF_Transient);
	if (!IsValid(Producer)
		|| !Producer->Install(
			GarmentComponent,
			BodyComponent,
			LoadedSurfaceConstraintDeformer,
			Binding,
			*LODPair,
			OutFailureReason))
	{
		if (IsValid(Producer))
		{
			Producer->Detach();
		}
		return false;
	}

	RetainedSurfaceRuntimeObjects.AddUnique(Producer);
	State.SurfaceProducer = Producer;
	State.SurfaceGarmentLODIndex = GarmentLODIndex;
	State.SurfaceBodyLODIndex = BodyLODIndex;
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	State.SurfaceWarmupFramesRemaining = FMath::Clamp(
		Settings ? Settings->SurfaceWarmupFrames : 2,
		1,
		8);
	State.SurfaceWarmupElapsedSeconds = 0.0f;
	State.bSurfaceAwaitingManagerInitialization = true;
	State.SurfaceRuntimeState = EEFClothingSurfaceRuntimeState::WarmingUp;
	State.SurfaceFailureReason.Reset();
	return true;
}

void UEFClothingFitRuntimeComponent::ReleaseSurfaceConstraint(FAppliedGarmentState& State)
{
	if (UEFClothingSurfaceDeformerProducer* Producer = State.SurfaceProducer.Get())
	{
		State.SurfaceDispatchFailureCount += Producer->GetDispatchFailureCount();
		Producer->Detach();
		RetainedSurfaceRuntimeObjects.RemoveSingleSwap(Producer);
	}
	State.SurfaceProducer.Reset();
	State.SurfaceGarmentLODIndex = INDEX_NONE;
	State.SurfaceBodyLODIndex = INDEX_NONE;
	State.SurfaceWarmupFramesRemaining = 0;
	State.SurfaceWarmupElapsedSeconds = 0.0f;
	State.bSurfaceAwaitingManagerInitialization = false;
}

float UEFClothingFitRuntimeComponent::ResolveSurfaceGarmentOffsetCm(
	USkeletalMeshComponent* GarmentComponent,
	const FAppliedGarmentState& State) const
{
	const UEFClothingFitProfile* Profile = State.Profile.Get();
	const UEFClothingSurfaceBinding* Binding = State.SurfaceBinding.Get();
	const FEFClothingSurfaceLODPairBinding* LODPair = IsValid(Binding)
		? Binding->FindLODPair(State.SurfaceGarmentLODIndex, State.SurfaceBodyLODIndex)
		: nullptr;
	const float BaseClearanceCm = FMath::Max(
		IsValid(Profile) && FMath::IsFinite(Profile->CompiledMinimumClearanceCm)
			? Profile->CompiledMinimumClearanceCm
			: 0.0f,
		LODPair && FMath::IsFinite(LODPair->BaseClearanceCm)
			? LODPair->BaseClearanceCm
			: EFClothingMorphV26::DefaultBaseClearanceCm);
	const float* LegacyGarmentMultiplier = GarmentClearanceMultipliers.Find(GarmentComponent);
	const float SafeGarmentMultiplier = LegacyGarmentMultiplier && FMath::IsFinite(*LegacyGarmentMultiplier)
		? FMath::Clamp(*LegacyGarmentMultiplier, 1.0f, 2.0f)
		: 1.0f;
	const float LegacyRequestedMultiplier = FMath::Clamp(
		FMath::Max(
			RuntimeClearanceMultiplier * SafeGarmentMultiplier,
			State.CatalogMinimumClearanceMultiplier),
		1.0f,
		2.0f);
	const float LegacyOffsetCm = (LegacyRequestedMultiplier - 1.0f) * BaseClearanceCm;
	const float* ExplicitGarmentOffsetCm = GarmentClearanceOffsetsCm.Find(GarmentComponent);
	const bool bHasExplicitGarmentOffset = ExplicitGarmentOffsetCm
		&& FMath::IsFinite(*ExplicitGarmentOffsetCm);
	const float ExplicitOffsetCm = bHasExplicitGarmentOffset
		? FMath::Max(*ExplicitGarmentOffsetCm, 0.0f)
		: 0.0f;
	const float MaximumRuntimeOffsetCm = GetMaximumRuntimeAdditionalClearanceCm();
	const float RemainingGarmentBudgetCm = FMath::Max(
		MaximumRuntimeOffsetCm - ResolveGlobalSurfaceOffsetCm(),
		0.0f);
	const float DirectorOffsetCm = ResolveDirectorGarmentOffsetCm(State);
	// An explicit per-component API call overrides authored tuning. Otherwise the
	// selected catalog index owns the offset; legacy multipliers may only raise
	// that request, never stack another copy of the same clearance on top.
	const float SelectedGarmentOffsetCm = bHasExplicitGarmentOffset
		? ExplicitOffsetCm
		: FMath::Max(DirectorOffsetCm, LegacyOffsetCm);
	return FMath::Clamp(
		SelectedGarmentOffsetCm,
		0.0f,
		RemainingGarmentBudgetCm);
}

void UEFClothingFitRuntimeComponent::ApplyReservedSurfaceBounds(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State,
	const UEFClothingSurfaceBinding* SurfaceBinding,
	const float CatalogMaximumCorrectionCm)
{
	if (!IsValid(GarmentComponent) || !IsValid(SurfaceBinding))
	{
		return;
	}

	float MaximumSurfaceCorrectionCm = 0.0f;
	for (const FEFClothingSurfaceLODPairBinding& LODPair : SurfaceBinding->LODPairBindings)
	{
		for (const FEFClothingSurfaceVertexBinding& VertexBinding : LODPair.VertexBindings)
		{
			if (FMath::IsFinite(VertexBinding.MaximumCorrectionCm))
			{
				MaximumSurfaceCorrectionCm = FMath::Max(
					MaximumSurfaceCorrectionCm,
					VertexBinding.MaximumCorrectionCm);
			}
		}
	}
	if (FMath::IsFinite(CatalogMaximumCorrectionCm) && CatalogMaximumCorrectionCm >= 0.0f)
	{
		MaximumSurfaceCorrectionCm = FMath::Min(
			MaximumSurfaceCorrectionCm,
			CatalogMaximumCorrectionCm);
	}
	State.SurfaceMaximumCorrectionCm = MaximumSurfaceCorrectionCm;

	USkeletalMesh* FittedMesh = State.FittedMesh.Get();
	if (!IsValid(FittedMesh))
	{
		return;
	}
	const FBoxSphereBounds FittedBounds = FittedMesh->GetImportedBounds();

	// Reserve the absolute hard cap rather than the current Director setting.
	// That lets a live policy change remain inside fixed bounds. More importantly,
	// BoundsScale expands each BoxExtent, not the bounding sphere. A flat garment
	// such as underwear can need its largest outward correction on its narrowest
	// axis, so calculate a conservative scale separately for all three axes.
	const float ReservedOutwardTravelCm = MaximumSurfaceCorrectionCm
		+ EFClothingMorphV26::MaximumRuntimeAdditionalClearanceCm
		+ EFClothingMorphV26::MaximumRuntimeVisibleThicknessCm;
	float RequiredBoundsScale = State.ComponentBoundsScaleBeforeV2;
	bool bHasUsableExtent = false;
	for (const float ExtentCm : {
		FittedBounds.BoxExtent.X,
		FittedBounds.BoxExtent.Y,
		FittedBounds.BoxExtent.Z})
	{
		if (!FMath::IsFinite(ExtentCm) || ExtentCm <= UE_SMALL_NUMBER)
		{
			continue;
		}
		bHasUsableExtent = true;
		RequiredBoundsScale = FMath::Max(
			RequiredBoundsScale,
			(ExtentCm + ReservedOutwardTravelCm) / ExtentCm);
	}
	if (!bHasUsableExtent)
	{
		const float FittedSphereRadiusCm = FittedBounds.SphereRadius;
		if (!FMath::IsFinite(FittedSphereRadiusCm) || FittedSphereRadiusCm <= UE_SMALL_NUMBER)
		{
			return;
		}
		RequiredBoundsScale = FMath::Max(
			RequiredBoundsScale,
			(FittedSphereRadiusCm + ReservedOutwardTravelCm) / FittedSphereRadiusCm);
	}
	State.BoundsScaleAssignedByV2 = RequiredBoundsScale;
	GarmentComponent->SetBoundsScale(State.BoundsScaleAssignedByV2);
}

void UEFClothingFitRuntimeComponent::TryExposeReadyGarment(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State)
{
	if (!bStartupAssetsReady)
	{
		return;
	}
	const UEFClothingFitProfile* Profile = State.Profile.Get();
	USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(State.BodyMesh.Get());
	if (!IsValid(GarmentComponent)
		|| !IsValid(Profile)
		|| !IsValid(ExpectedPoseDriver)
		|| State.bWaitingForSkinProfile
		|| State.bMorphStateUnsafe
		|| !IsCertifiedSkinProfileActive(GarmentComponent, Profile)
		|| GarmentComponent->LeaderPoseComponent.Get() != ExpectedPoseDriver
		|| (State.bUsesSurfaceWrapGPU
			&& State.SurfaceRuntimeState != EEFClothingSurfaceRuntimeState::Ready))
	{
		return;
	}
	const bool bWasPending = GarmentComponent->ComponentTags.Contains(PendingTag)
		|| State.bRenderSuppressedByV2;
	if (!bWasPending)
	{
		return;
	}

	GarmentComponent->ComponentTags.Remove(PendingTag);
	if (State.bRenderSuppressedByV2)
	{
		GarmentComponent->SetRenderInMainPass(State.bRenderInMainPassBeforeV2);
		State.bRenderSuppressedByV2 = false;
	}
	LastStatus = FString::Printf(TEXT("Applied and ready: %s"), *GarmentComponent->GetName());
	UE_LOG(
		LogEFClothingMorphV2,
		Display,
		TEXT("EFClothingMorphV2 READY owner=%s component=%s fitted=%s surfaceState=%d garmentLOD=%d bodyLOD=%d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		*GarmentComponent->GetName(),
		*GetNameSafe(State.FittedMesh.Get()),
		static_cast<int32>(State.SurfaceRuntimeState),
		State.SurfaceGarmentLODIndex,
		State.SurfaceBodyLODIndex);
}

void UEFClothingFitRuntimeComponent::FailSurfaceConstraint(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State,
	const FString& FailureReason)
{
	if (IsValid(GarmentComponent))
	{
		GarmentComponent->ComponentTags.AddUnique(PendingTag);
		if (GarmentComponent->bRenderInMainPass)
		{
			GarmentComponent->SetRenderInMainPass(false);
			State.bRenderSuppressedByV2 = true;
		}
	}
	State.SurfaceRuntimeState = EEFClothingSurfaceRuntimeState::Failed;
	State.SurfaceFailureReason = FailureReason;
	ReleaseSurfaceConstraint(State);
	LastStatus = FString::Printf(
		TEXT("SurfaceWrapGPU failed closed for %s: %s"),
		*GetNameSafe(GarmentComponent),
		*FailureReason);
	UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
}

void UEFClothingFitRuntimeComponent::TickSurfaceConstraints(const float DeltaTimeSeconds)
{
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		USkeletalMeshComponent* GarmentComponent = Pair.Key.Get();
		FAppliedGarmentState& State = Pair.Value;
		if (!State.bUsesSurfaceWrapGPU || State.SurfaceRuntimeState == EEFClothingSurfaceRuntimeState::Failed)
		{
			continue;
		}
		USkeletalMeshComponent* BodyComponent = State.BodyMesh.Get();
		if (!IsValid(GarmentComponent)
			|| !IsValid(BodyComponent)
			|| GarmentComponent->GetSkeletalMeshAsset() != State.FittedMesh.Get())
		{
			FailSurfaceConstraint(GarmentComponent, State, TEXT("Garment/body component or fitted mesh changed during surface execution."));
			continue;
		}

		const int32 GarmentLODIndex = FMath::Max(GarmentComponent->GetPredictedLODLevel(), 0);
		const int32 BodyLODIndex = FMath::Max(BodyComponent->GetPredictedLODLevel(), 0);
		UEFClothingSurfaceDeformerProducer* Producer = State.SurfaceProducer.Get();
		if (!IsValid(Producer)
			|| !Producer->IsInstalledFor(GarmentComponent, BodyComponent, GarmentLODIndex, BodyLODIndex))
		{
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
			if (GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(false);
				State.bRenderSuppressedByV2 = true;
			}
			FString InstallFailure;
			if (!TryInstallSurfaceConstraint(GarmentComponent, State, InstallFailure))
			{
				FailSurfaceConstraint(GarmentComponent, State, InstallFailure);
				continue;
			}
			Producer = State.SurfaceProducer.Get();
		}
		if (State.bSurfaceAwaitingManagerInitialization)
		{
			// AddProducerDeformer marks resources pending; the owning manager allocates
			// them during this frame's normal EndOfFrame update. Dispatch starts next
			// frame and the garment remains hidden throughout.
			State.bSurfaceAwaitingManagerInitialization = false;
			continue;
		}

		FString DispatchFailure;
		const float MaximumCorrectionOverrideCm = State.CatalogMaximumCorrectionCm >= 0.0f
			? FMath::Clamp(State.CatalogMaximumCorrectionCm, 0.0f, 10.0f)
			: -1.0f;
		if (!IsValid(Producer)
			|| !Producer->EnqueueSurfacePass(
				DeltaTimeSeconds,
				ResolveGlobalSurfaceOffsetCm(),
				ResolveSurfaceGarmentOffsetCm(GarmentComponent, State),
				ResolveDirectorGarmentVisibleThicknessCm(State),
				MaximumCorrectionOverrideCm,
				DispatchFailure))
		{
			FailSurfaceConstraint(
				GarmentComponent,
				State,
				DispatchFailure.IsEmpty() ? TEXT("Surface producer dispatch failed.") : DispatchFailure);
			continue;
		}

		++State.SurfaceEnqueueCount;
		if (State.SurfaceRuntimeState == EEFClothingSurfaceRuntimeState::WarmingUp)
		{
			const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
			const int32 RequiredValidatedFrames = FMath::Clamp(
				Settings ? Settings->SurfaceWarmupFrames : 2,
				1,
				8);
			const uint64 ValidatedFrames = Producer->GetRenderValidatedSubmissionCount();
			State.SurfaceWarmupFramesRemaining = FMath::Max(
				RequiredValidatedFrames - static_cast<int32>(FMath::Min<uint64>(ValidatedFrames, RequiredValidatedFrames)),
				0);
			State.SurfaceWarmupElapsedSeconds += FMath::Max(DeltaTimeSeconds, 0.0f);
			// Never expose a raw garment merely because N game frames elapsed.
			// Each producer acknowledgement is written on the render thread only
			// after ComputeFramework had an opportunity to validate/submit the
			// BeginInitViews graph without invoking its fallback delegate.
			if (State.SurfaceWarmupFramesRemaining == 0)
			{
				State.SurfaceRuntimeState = EEFClothingSurfaceRuntimeState::Ready;
			}
			else
			{
				const float WarmupTimeoutSeconds = FMath::Clamp(
					Settings ? Settings->SurfaceShaderWarmupTimeoutSeconds : 15.0f,
					1.0f,
					60.0f);
				if (State.SurfaceWarmupElapsedSeconds >= WarmupTimeoutSeconds)
				{
					FailSurfaceConstraint(
						GarmentComponent,
						State,
						FString::Printf(
							TEXT("Surface shader warm-up timed out after %.2fs (%llu/%d validated frames, %u fallbacks: immediate=%u render-validation=%u; %s)."),
							State.SurfaceWarmupElapsedSeconds,
							ValidatedFrames,
							RequiredValidatedFrames,
							Producer->GetDispatchFailureCount(),
							Producer->GetImmediateEnqueueFallbackCount(),
							Producer->GetRenderValidationFallbackCount(),
							*Producer->GetRenderPreflightSummary()));
					continue;
				}
			}
		}
		TryExposeReadyGarment(GarmentComponent, State);
	}
}

void UEFClothingFitRuntimeComponent::SynchronizeMorphs()
{
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const float ConfiguredEpsilon = Settings ? Settings->MorphWriteEpsilon : 0.0005f;
	const float Epsilon = FMath::IsFinite(ConfiguredEpsilon)
		? FMath::Max(ConfiguredEpsilon, 0.0f)
		: 0.0005f;

	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		USkeletalMeshComponent* GarmentComponent = Pair.Key.Get();
		FAppliedGarmentState& State = Pair.Value;
		const UEFClothingFitProfile* Profile = State.Profile.Get();
		USkeletalMeshComponent* BodyComponent = State.BodyMesh.Get();
		if (!IsValid(GarmentComponent) || !IsValid(Profile) || !IsValid(BodyComponent)
			|| GarmentComponent->GetSkeletalMeshAsset() != State.FittedMesh.Get())
		{
			continue;
		}
		USkeletalMesh* ExpectedBodyMesh = Profile->BodySurface.Get();
		USkeletalMeshComponent* ExpectedPoseDriver = ResolveEffectivePoseDriver(BodyComponent);
		constexpr float CertifiedTransformTolerance = 0.001f;
		const FTransform BodyToGarment = BodyComponent->GetComponentTransform().GetRelativeTransform(
			GarmentComponent->GetComponentTransform());
		const FVector RelativeScale = BodyToGarment.GetScale3D();
		const bool bSurfaceTransformValid = !BodyToGarment.ContainsNaN()
			&& FMath::Abs(RelativeScale.X) > UE_SMALL_NUMBER
			&& FMath::Abs(RelativeScale.Y) > UE_SMALL_NUMBER
			&& FMath::Abs(RelativeScale.Z) > UE_SMALL_NUMBER;
		const bool bLODContractValid = State.bUsesSurfaceWrapGPU
			|| BodyComponent->GetPredictedLODLevel() <= 0;
		const bool bTransformContractValid = State.bUsesSurfaceWrapGPU
			? bSurfaceTransformValid
			: GarmentComponent->GetComponentTransform().Equals(
				BodyComponent->GetComponentTransform(),
				CertifiedTransformTolerance);
		if (!IsValid(ExpectedBodyMesh)
			|| !IsValid(ExpectedPoseDriver)
			|| BodyComponent->GetSkeletalMeshAsset() != ExpectedBodyMesh
			|| ResolveBodyMesh(Profile) != BodyComponent
			|| !bLODContractValid
			|| !HasMatchingActiveLOD0Deformer(GarmentComponent, BodyComponent)
			|| !bTransformContractValid)
		{
			const bool bWasAlreadyUnsafe = State.bMorphStateUnsafe;
			State.bMorphStateUnsafe = true;
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
			if (GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(false);
				State.bRenderSuppressedByV2 = true;
			}
			NextReconcileAtSeconds = 0.0;
			LastStatus = FString::Printf(
				TEXT("Fail-suppressed %s: exact body identity, effective pose driver, certified LOD/deformer or transform contract changed"),
				*GarmentComponent->GetName());
			if (!bWasAlreadyUnsafe)
			{
				UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
			}
			continue;
		}
		if (GarmentComponent->LeaderPoseComponent.Get() != ExpectedPoseDriver)
		{
			// Morph synchronization may run more often than the full reconciliation
			// pass. Assign through Female, then verify Unreal stored the effective
			// top-most driver before allowing any fitted render frame.
			GarmentComponent->SetLeaderPoseComponent(BodyComponent, true, false);
			if (GarmentComponent->LeaderPoseComponent.Get() == ExpectedPoseDriver)
			{
				State.LeaderPoseAssignedByV2 = ExpectedPoseDriver;
				State.bAssignedLeaderPoseByV2 = true;
			}
		}
		USkeletalMeshComponent* CurrentPoseDriver = Cast<USkeletalMeshComponent>(
			GarmentComponent->LeaderPoseComponent.Get());
		USkeletalMesh* CurrentPoseDriverMesh = IsValid(CurrentPoseDriver)
			? CurrentPoseDriver->GetSkeletalMeshAsset()
			: nullptr;
		if (CurrentPoseDriver != ExpectedPoseDriver
			|| State.ValidatedLeaderComponent.Get() != ExpectedPoseDriver
			|| State.ValidatedLeaderMesh.Get() != CurrentPoseDriverMesh)
		{
			const bool bWasAlreadyUnsafe = State.bMorphStateUnsafe;
			State.bMorphStateUnsafe = true;
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
			if (GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(false);
				State.bRenderSuppressedByV2 = true;
			}
			NextReconcileAtSeconds = 0.0;
			LastStatus = FString::Printf(
				TEXT("Fail-suppressed %s: effective LeaderPose driver is unverified"),
				*GarmentComponent->GetName());
			if (!bWasAlreadyUnsafe)
			{
				UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
			}
			continue;
		}

		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (!IsCertifiedSkinProfileActive(GarmentComponent, Profile))
		{
			if (!State.bWaitingForSkinProfile)
			{
				State.bWaitingForSkinProfile = true;
				State.SkinProfileWaitStartedAtSeconds = Now;
			}
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
			if (GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(false);
				State.bRenderSuppressedByV2 = true;
			}
			continue;
		}

		const bool bWasWaitingForSkinProfile = State.bWaitingForSkinProfile;
		State.bWaitingForSkinProfile = false;
		if (State.bUsesSurfaceWrapGPU)
		{
			const bool bRecoveredFromUnsafeState = State.bMorphStateUnsafe;
			State.bMorphStateUnsafe = false;
			if (bWasWaitingForSkinProfile || bRecoveredFromUnsafeState)
			{
				GarmentComponent->UpdateFollowerComponent();
			}
			TryExposeReadyGarment(GarmentComponent, State);
			continue;
		}

		TArray<USkeletalMeshComponent*> PoseSources;
		GatherMorphPoseSources(BodyComponent, GarmentComponent, PoseSources);

		TMap<FName, const FEFClothingMorphBinding*> BindingsByBodyMorph;
		BindingsByBodyMorph.Reserve(Profile->MorphBindings.Num());
		TMap<FName, float> DesiredMorphValues;
		DesiredMorphValues.Reserve(
			1 + Profile->GeneratedMorphSampleCount + Profile->GeneratedPairCellMorphCount);
		for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
		{
			BindingsByBodyMorph.Add(Binding.BodyMorph, &Binding);
			for (const FEFClothingMorphSample& Sample : Binding.Samples)
			{
				if (!Sample.bIdentity && !Sample.GarmentMorph.IsNone())
				{
					DesiredMorphValues.Add(Sample.GarmentMorph, 0.0f);
				}
			}
		}
		for (const FEFClothingMorphPairCertificate& Certificate : Profile->MorphPairCertificates)
		{
			for (const FEFClothingMorphPairCell& Cell : Certificate.Cells)
			{
				DesiredMorphValues.Add(Cell.GarmentMorph, 0.0f);
			}
		}
		float RequiredAutomaticClearanceMultiplier = Profile->CertifiedClearanceMultiplierMin;

		TMap<FName, float> ResolvedBodyMorphValues;
		ResolvedBodyMorphValues.Reserve(Profile->MonitoredBodyMorphNames.Num());
		TArray<FName, TInlineAllocator<64>> ActiveBodyMorphNames;
		FName UnsafeMorphName = NAME_None;
		float UnsafeMorphValue = 0.0f;
		FString UnsafeMorphReason;
		for (FName MonitoredBodyMorph : Profile->MonitoredBodyMorphNames)
		{
			const float BodyValue = ResolveBodyMorphValue(BodyComponent, PoseSources, MonitoredBodyMorph);
			const FEFClothingMorphBinding* const* BindingPtr = BindingsByBodyMorph.Find(MonitoredBodyMorph);
			const FEFClothingMorphBinding* Binding = BindingPtr ? *BindingPtr : nullptr;
			const float ResolvedValue = Binding
				? BodyValue * Binding->Scale + Binding->Bias
				: BodyValue;
			if (!FMath::IsFinite(BodyValue) || !FMath::IsFinite(ResolvedValue))
			{
				UnsafeMorphName = MonitoredBodyMorph;
				UnsafeMorphValue = ResolvedValue;
				UnsafeMorphReason = TEXT("non-finite monitored body-morph value");
				break;
			}
			if (Binding
				&& (ResolvedValue < Binding->MinimumCertifiedValue
					|| ResolvedValue > Binding->MaximumCertifiedValue))
			{
				UnsafeMorphName = MonitoredBodyMorph;
				UnsafeMorphValue = ResolvedValue;
				UnsafeMorphReason = TEXT("value outside its one-dimensional V24 certificate");
				break;
			}
			ResolvedBodyMorphValues.Add(MonitoredBodyMorph, ResolvedValue);
			if (FMath::Abs(ResolvedValue) > Profile->CompiledMorphActivationEpsilon)
			{
				if (!Binding)
				{
					UnsafeMorphName = MonitoredBodyMorph;
					UnsafeMorphValue = ResolvedValue;
					UnsafeMorphReason = TEXT("active monitored morph has no renderable V24 binding");
					break;
				}
				ActiveBodyMorphNames.Add(MonitoredBodyMorph);
			}
		}

		ActiveBodyMorphNames.Sort(FNameLexicalLess());
		auto ApplyOneDimensionalBinding = [
			&DesiredMorphValues,
			&RequiredAutomaticClearanceMultiplier](
			const FEFClothingMorphBinding& Binding,
			float Value)
		{
			TArray<float, TInlineAllocator<8>> SampleWeights;
			SampleWeights.Init(0.0f, Binding.Samples.Num());
			if (Value <= Binding.MinimumCertifiedValue + KINDA_SMALL_NUMBER)
			{
				// The implicit fitted-rest key is represented by all generated weights at zero.
			}
			else if (Value <= Binding.Samples[0].BodyValue)
			{
				const float Blend = FMath::Clamp(
					Value / FMath::Max(Binding.Samples[0].BodyValue, KINDA_SMALL_NUMBER),
					0.0f,
					1.0f);
				const float StepThreshold = FMath::Clamp(
					(Binding.Samples[0].StepSwitchBodyValue - Binding.MinimumCertifiedValue)
					/ FMath::Max(Binding.Samples[0].BodyValue - Binding.MinimumCertifiedValue, KINDA_SMALL_NUMBER),
					0.0f,
					1.0f);
				SampleWeights[0] = Binding.Samples[0].bStepFromPrevious
					? (Blend <= StepThreshold ? 0.0f : 1.0f)
					: Blend;
			}
			else if (Value >= Binding.Samples.Last().BodyValue)
			{
				SampleWeights.Last() = 1.0f;
			}
			else
			{
				for (int32 HighIndex = 1; HighIndex < Binding.Samples.Num(); ++HighIndex)
				{
					const FEFClothingMorphSample& LowSample = Binding.Samples[HighIndex - 1];
					const FEFClothingMorphSample& HighSample = Binding.Samples[HighIndex];
					if (Value <= HighSample.BodyValue)
					{
						const float Blend = FMath::Clamp(
							(Value - LowSample.BodyValue)
							/ FMath::Max(HighSample.BodyValue - LowSample.BodyValue, KINDA_SMALL_NUMBER),
							0.0f,
							1.0f);
						if (HighSample.bStepFromPrevious)
						{
							const float StepThreshold = FMath::Clamp(
								(HighSample.StepSwitchBodyValue - LowSample.BodyValue)
								/ FMath::Max(HighSample.BodyValue - LowSample.BodyValue, KINDA_SMALL_NUMBER),
								0.0f,
								1.0f);
							SampleWeights[HighIndex - 1] = Blend <= StepThreshold ? 1.0f : 0.0f;
							SampleWeights[HighIndex] = Blend <= StepThreshold ? 0.0f : 1.0f;
						}
						else
						{
							SampleWeights[HighIndex - 1] = 1.0f - Blend;
							SampleWeights[HighIndex] = Blend;
						}
						break;
					}
				}
			}

			for (int32 SampleIndex = 0; SampleIndex < Binding.Samples.Num(); ++SampleIndex)
			{
				const FEFClothingMorphSample& Sample = Binding.Samples[SampleIndex];
				if (SampleWeights[SampleIndex] > 0.0f)
				{
					RequiredAutomaticClearanceMultiplier = FMath::Max(
						RequiredAutomaticClearanceMultiplier,
						Sample.MinimumClearanceMultiplier);
				}
				if (!Sample.bIdentity && !Sample.GarmentMorph.IsNone())
				{
					DesiredMorphValues.FindOrAdd(Sample.GarmentMorph) = SampleWeights[SampleIndex];
				}
			}
		};

		if (UnsafeMorphReason.IsEmpty())
		{
			if (ActiveBodyMorphNames.Num() == 1)
			{
				const FName ActiveMorph = ActiveBodyMorphNames[0];
				const FEFClothingMorphBinding* const* BindingPtr = BindingsByBodyMorph.Find(ActiveMorph);
				const float* ActiveValue = ResolvedBodyMorphValues.Find(ActiveMorph);
				if (!BindingPtr || !*BindingPtr || !ActiveValue)
				{
					UnsafeMorphName = ActiveMorph;
					UnsafeMorphReason = TEXT("single active morph has no complete V24 binding");
				}
				else
				{
					ApplyOneDimensionalBinding(**BindingPtr, *ActiveValue);
				}
			}
			else if (ActiveBodyMorphNames.Num() == 2)
			{
				const FEFClothingMorphPairCertificate* MatchingCertificate = nullptr;
				for (const FEFClothingMorphPairCertificate& Certificate : Profile->MorphPairCertificates)
				{
					if (Certificate.FirstBodyMorph == ActiveBodyMorphNames[0]
						&& Certificate.SecondBodyMorph == ActiveBodyMorphNames[1])
					{
						if (MatchingCertificate)
						{
							MatchingCertificate = nullptr;
							UnsafeMorphReason = TEXT("active pair resolves to duplicate V24 certificates");
							break;
						}
						MatchingCertificate = &Certificate;
					}
				}
				if (UnsafeMorphReason.IsEmpty() && !MatchingCertificate)
				{
					UnsafeMorphReason = FString::Printf(
						TEXT("active morph pair %s + %s has no V24 certificate"),
						*ActiveBodyMorphNames[0].ToString(),
						*ActiveBodyMorphNames[1].ToString());
				}
				if (UnsafeMorphReason.IsEmpty())
				{
					const float* FirstValue = ResolvedBodyMorphValues.Find(MatchingCertificate->FirstBodyMorph);
					const float* SecondValue = ResolvedBodyMorphValues.Find(MatchingCertificate->SecondBodyMorph);
					if (!FirstValue || !SecondValue
						|| *FirstValue < MatchingCertificate->FirstMinimumCertifiedValue
						|| *FirstValue > MatchingCertificate->FirstMaximumCertifiedValue
						|| *SecondValue < MatchingCertificate->SecondMinimumCertifiedValue
						|| *SecondValue > MatchingCertificate->SecondMaximumCertifiedValue)
					{
						UnsafeMorphReason = TEXT("active pair values are outside the V24 pair certificate");
					}
					else
					{
						auto ResolveCellIndex = [](float Value, float Minimum, float Maximum, int32 Resolution)
						{
							const float NormalizedValue = FMath::Clamp(
								(Value - Minimum) / FMath::Max(Maximum - Minimum, KINDA_SMALL_NUMBER),
								0.0f,
								1.0f);
							return FMath::Clamp(FMath::FloorToInt(NormalizedValue * Resolution), 0, Resolution - 1);
						};
						const int32 FirstCellIndex = ResolveCellIndex(
							*FirstValue,
							MatchingCertificate->FirstMinimumCertifiedValue,
							MatchingCertificate->FirstMaximumCertifiedValue,
							MatchingCertificate->GridResolution);
						const int32 SecondCellIndex = ResolveCellIndex(
							*SecondValue,
							MatchingCertificate->SecondMinimumCertifiedValue,
							MatchingCertificate->SecondMaximumCertifiedValue,
							MatchingCertificate->GridResolution);
						const FEFClothingMorphPairCell* SelectedCell = nullptr;
						for (const FEFClothingMorphPairCell& Cell : MatchingCertificate->Cells)
						{
							if (Cell.FirstCellIndex == FirstCellIndex && Cell.SecondCellIndex == SecondCellIndex)
							{
								if (SelectedCell)
								{
									SelectedCell = nullptr;
									UnsafeMorphReason = TEXT("active pair cell selection is ambiguous");
									break;
								}
								SelectedCell = &Cell;
							}
						}
						if (UnsafeMorphReason.IsEmpty() && !SelectedCell)
						{
							UnsafeMorphReason = TEXT("active pair has no certified cell");
						}
						if (UnsafeMorphReason.IsEmpty())
						{
							DesiredMorphValues.FindOrAdd(SelectedCell->GarmentMorph) = 1.0f;
							RequiredAutomaticClearanceMultiplier = FMath::Max(
								RequiredAutomaticClearanceMultiplier,
								SelectedCell->MinimumClearanceMultiplier);
						}
					}
				}
			}
			else if (ActiveBodyMorphNames.Num() > 2)
			{
				UnsafeMorphReason = FString::Printf(
					TEXT("%d simultaneous monitored body morphs exceed the V24 pair certificate"),
					ActiveBodyMorphNames.Num());
			}
		}
		DesiredMorphValues.Add(
			Profile->ClearanceMorphName,
			ResolveClearanceValue(
				GarmentComponent,
				Profile,
				RequiredAutomaticClearanceMultiplier));

		TSet<FName> MorphNamesToWrite;
		for (const TPair<FName, float>& DesiredMorph : DesiredMorphValues)
		{
			MorphNamesToWrite.Add(DesiredMorph.Key);
		}
		for (const TPair<FName, float>& PreviousMorph : State.LastWrittenMorphValues)
		{
			MorphNamesToWrite.Add(PreviousMorph.Key);
		}
		TArray<FName> SortedMorphNamesToWrite = MorphNamesToWrite.Array();
		SortedMorphNamesToWrite.Sort(FNameLexicalLess());
		bool bWroteMorphValues = false;
		for (FName MorphName : SortedMorphNamesToWrite)
		{
			const float DesiredValue = DesiredMorphValues.FindRef(MorphName);
			const float* PreviousValue = State.LastWrittenMorphValues.Find(MorphName);
			const bool bRequiredExactClear = DesiredValue == 0.0f
				&& PreviousValue
				&& *PreviousValue != 0.0f;
			if (!PreviousValue
				|| bRequiredExactClear
				|| FMath::Abs(DesiredValue - *PreviousValue) > Epsilon)
			{
				GarmentComponent->SetMorphTarget(MorphName, DesiredValue, DesiredValue == 0.0f);
				State.LastWrittenMorphValues.Add(MorphName, DesiredValue);
				bWroteMorphValues = true;
			}
		}

		if (!UnsafeMorphReason.IsEmpty())
		{
			const bool bWasAlreadyUnsafe = State.bMorphStateUnsafe;
			State.bMorphStateUnsafe = true;
			GarmentComponent->ComponentTags.AddUnique(PendingTag);
			if (GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(false);
				State.bRenderSuppressedByV2 = true;
			}
			LastStatus = UnsafeMorphName.IsNone()
				? FString::Printf(
					TEXT("Fail-suppressed %s: %s"),
					*GarmentComponent->GetName(),
					*UnsafeMorphReason)
				: FString::Printf(
					TEXT("Fail-suppressed %s: morph %s value %.9g: %s"),
					*GarmentComponent->GetName(),
					*UnsafeMorphName.ToString(),
					UnsafeMorphValue,
					*UnsafeMorphReason);
			if (!bWasAlreadyUnsafe)
			{
				UE_LOG(LogEFClothingMorphV2, Warning, TEXT("%s"), *LastStatus);
			}
			continue;
		}

		const bool bRecoveredFromUnsafeMorph = State.bMorphStateUnsafe;
		State.bMorphStateUnsafe = false;
		if (bWroteMorphValues || bRecoveredFromUnsafeMorph || bWasWaitingForSkinProfile)
		{
			GarmentComponent->UpdateFollowerComponent();
		}
		if (bRecoveredFromUnsafeMorph || bWasWaitingForSkinProfile)
		{
			TryExposeReadyGarment(GarmentComponent, State);
		}
	}
}

void UEFClothingFitRuntimeComponent::RemoveStaleStates()
{
	for (auto MultiplierIt = GarmentClearanceMultipliers.CreateIterator(); MultiplierIt; ++MultiplierIt)
	{
		USkeletalMeshComponent* GarmentComponent = MultiplierIt.Key().Get();
		if (!IsValid(GarmentComponent) || GarmentComponent->GetOwner() != GetOwner())
		{
			MultiplierIt.RemoveCurrent();
		}
	}
	for (auto OffsetIt = GarmentClearanceOffsetsCm.CreateIterator(); OffsetIt; ++OffsetIt)
	{
		USkeletalMeshComponent* GarmentComponent = OffsetIt.Key().Get();
		if (!IsValid(GarmentComponent) || GarmentComponent->GetOwner() != GetOwner())
		{
			OffsetIt.RemoveCurrent();
		}
	}

	for (auto PrefetchIt = PrefetchingGarments.CreateIterator(); PrefetchIt; ++PrefetchIt)
	{
		USkeletalMeshComponent* GarmentComponent = PrefetchIt.Key().Get();
		if (!IsValid(GarmentComponent) || !PrefetchIt.Value().SourceMesh.IsValid())
		{
			if (IsValid(GarmentComponent))
			{
				RestorePrefetchGarment(GarmentComponent, PrefetchIt.Value());
			}
			PrefetchIt.RemoveCurrent();
		}
	}

	for (auto RejectionIt = RejectedGarments.CreateIterator(); RejectionIt; ++RejectionIt)
	{
		if (!RejectionIt.Key().IsValid() || !RejectionIt.Value().SourceMesh.IsValid())
		{
			RejectionIt.RemoveCurrent();
		}
	}

	bool bRemovedState = false;
	for (auto It = AppliedGarments.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* GarmentComponent = It.Key().Get();
		FAppliedGarmentState& State = It.Value();
		if (!IsValid(GarmentComponent))
		{
			ReleaseSurfaceConstraint(State);
			ReleaseBodyCoverage(State);
			It.RemoveCurrent();
			bRemovedState = true;
			continue;
		}

		if (GarmentComponent->GetSkeletalMeshAsset() != State.FittedMesh.Get())
		{
			const bool bCurrentMeshIsCapturedSource =
				GarmentComponent->GetSkeletalMeshAsset() == State.SourceMesh.Get();
			ReleaseSurfaceConstraint(State);
			ReleaseBodyCoverage(State);
			if (CustomizationComponent)
			{
				CustomizationComponent->UnregisterExternalMorphWriter(GarmentComponent, this);
			}
			if (bCurrentMeshIsCapturedSource
				&& GarmentComponent->ComponentTags.Contains(PendingTag)
				&& State.bRenderSuppressedByV2
				&& !GarmentComponent->bRenderInMainPass)
			{
				GarmentComponent->SetRenderInMainPass(State.bRenderInMainPassBeforeV2);
			}
			if (bCurrentMeshIsCapturedSource
				&& State.bAssignedLeaderPoseByV2
				&& GarmentComponent->LeaderPoseComponent.Get() == State.LeaderPoseAssignedByV2.Get())
			{
				GarmentComponent->SetLeaderPoseComponent(
					State.PreviousLeaderPoseComponent.Get(),
					true,
					false);
			}
			if (bCurrentMeshIsCapturedSource)
			{
				ReleaseOwnedBoundsContract(GarmentComponent, State);
			}
			else
			{
				State.bOwnsBoundsContract = false;
			}
			GarmentComponent->ComponentTags.Remove(ManagedTag);
			GarmentComponent->ComponentTags.Remove(PendingTag);
			It.RemoveCurrent();
			bRemovedState = true;
		}
	}
	if (bRemovedState)
	{
		RefreshRetainedSourceMeshes();
		if (CustomizationComponent && !bIsRestoring)
		{
			bIsRestoring = true;
			CustomizationComponent->ReapplyCurrentMorphState();
			bIsRestoring = false;
		}
	}
}

void UEFClothingFitRuntimeComponent::ReleaseOwnedBoundsContract(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State)
{
	if (!State.bOwnsBoundsContract)
	{
		return;
	}

	// The pair is one indivisible ownership token. If another system changed
	// either flag, V2 relinquishes both without overwriting the external choice.
	const bool bStillOwnsExactContract = IsValid(GarmentComponent)
		&& !GarmentComponent->bUseBoundsFromLeaderPoseComponent
		&& GarmentComponent->bComponentUseFixedSkelBounds
		&& FMath::IsNearlyEqual(
			GarmentComponent->BoundsScale,
			State.BoundsScaleAssignedByV2,
			KINDA_SMALL_NUMBER);
	State.bOwnsBoundsContract = false;
	if (!bStillOwnsExactContract)
	{
		return;
	}

	GarmentComponent->bUseBoundsFromLeaderPoseComponent = State.bUseBoundsFromLeaderPoseBeforeV2;
	GarmentComponent->bComponentUseFixedSkelBounds = State.bComponentUseFixedSkelBoundsBeforeV2;
	GarmentComponent->SetBoundsScale(State.ComponentBoundsScaleBeforeV2);
}

void UEFClothingFitRuntimeComponent::RestorePrefetchGarment(
	USkeletalMeshComponent* GarmentComponent,
	FPrefetchGarmentState& State)
{
	if (!IsValid(GarmentComponent))
	{
		return;
	}
	if (GarmentComponent->GetSkeletalMeshAsset() == State.SourceMesh.Get()
		&& GarmentComponent->ComponentTags.Contains(PendingTag)
		&& State.bRenderSuppressedByV2
		&& !GarmentComponent->bRenderInMainPass)
	{
		GarmentComponent->SetRenderInMainPass(State.bRenderInMainPassBeforeV2);
		State.bRenderSuppressedByV2 = false;
	}
	GarmentComponent->ComponentTags.Remove(PendingTag);
}

void UEFClothingFitRuntimeComponent::RestoreGarment(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State,
	bool bRestoreSourceMesh)
{
	// Detach the late producer before any mesh/deformer swap. This leaves the
	// existing DAZ dynamic manager and its settings untouched.
	ReleaseSurfaceConstraint(State);
	if (!IsValid(GarmentComponent))
	{
		ReleaseBodyCoverage(State);
		return;
	}

	if (CustomizationComponent)
	{
		CustomizationComponent->UnregisterExternalMorphWriter(GarmentComponent, this);
	}

	// Remove only the morph curves owned by V2. This also clears persistent zero
	// overrides used by legacy derivatives without disturbing unrelated systems.
	for (const TPair<FName, float>& WrittenMorph : State.LastWrittenMorphValues)
	{
		if (!WrittenMorph.Key.IsNone())
		{
			GarmentComponent->SetMorphTarget(WrittenMorph.Key, 0.0f, true);
		}
	}
	State.LastWrittenMorphValues.Reset();

	// A fitted component is never exposed while ownership is being released.
	// In particular, do not clear or overwrite an externally-acquired Secondary
	// layer: wait for its owner to release it before switching the source mesh.
	if (GarmentComponent->bRenderInMainPass)
	{
		GarmentComponent->SetRenderInMainPass(false);
		State.bRenderSuppressedByV2 = true;
	}
	GarmentComponent->ComponentTags.AddUnique(PendingTag);
	State.bSourceMeshRestoreRequested = bRestoreSourceMesh;
	State.bSourceMeshRestored = GarmentComponent->GetSkeletalMeshAsset() == State.SourceMesh.Get();
	State.bSourceMorphStateReplayed = false;
	State.bRestoreFailureLogged = false;
	State.RestoreStartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RestoringGarments.Add(GarmentComponent, State);
	RefreshRetainedSourceMeshes();
	ProcessPendingRestores();
}

void UEFClothingFitRuntimeComponent::ProcessPendingRestores()
{
	bool bRemovedRestore = false;
	for (auto It = RestoringGarments.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* GarmentComponent = It.Key().Get();
		FAppliedGarmentState& State = It.Value();
		if (!IsValid(GarmentComponent))
		{
			ReleaseBodyCoverage(State);
			It.RemoveCurrent();
			bRemovedRestore = true;
			continue;
		}

		GarmentComponent->ComponentTags.AddUnique(PendingTag);
		if (GarmentComponent->bRenderInMainPass)
		{
			GarmentComponent->SetRenderInMainPass(false);
			State.bRenderSuppressedByV2 = true;
		}

		USkeletalMesh* SourceMesh = State.SourceMesh.Get();
		USkeletalMesh* FittedMesh = State.FittedMesh.Get();
		USkeletalMesh* CurrentMesh = GarmentComponent->GetSkeletalMeshAsset();
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		auto ReportBlockedRestore = [&](const FString& Reason)
		{
			LastStatus = FString::Printf(
				TEXT("Fail-closed restore pending for %s: %s"),
				*GarmentComponent->GetName(),
				*Reason);
			if (!State.bRestoreFailureLogged
				&& Now - State.RestoreStartedAtSeconds >= SkinProfileTimeoutSeconds)
			{
				State.bRestoreFailureLogged = true;
				UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
			}
		};

		if (!IsValid(SourceMesh) || !State.bSourceMeshRestoreRequested)
		{
			ReportBlockedRestore(TEXT("captured source mesh is unavailable"));
			continue;
		}

		if (CurrentMesh == FittedMesh)
		{
			if (GarmentComponent->IsSkinWeightProfilePending())
			{
				continue;
			}

			const FName CurrentPrimary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Primary);
			const FName CurrentSecondary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Secondary);
			if (!CurrentSecondary.IsNone())
			{
				ReportBlockedRestore(FString::Printf(
					TEXT("external Secondary=%s still owns the fitted stack"),
					*CurrentSecondary.ToString()));
				continue;
			}

			const UEFClothingFitProfile* AppliedProfile = State.Profile.Get();
			if (!CurrentPrimary.IsNone())
			{
				if (IsValid(AppliedProfile) && CurrentPrimary == AppliedProfile->SkinWeightProfileName)
				{
					GarmentComponent->ClearSkinWeightProfile(ESkinWeightProfileLayer::Primary);
				}
				else
				{
					ReportBlockedRestore(FString::Printf(
						TEXT("external Primary=%s still owns the fitted stack"),
						*CurrentPrimary.ToString()));
				}
				continue;
			}

			GarmentComponent->SetSkeletalMesh(SourceMesh, true);
			State.bSourceMeshRestored = true;
			CurrentMesh = GarmentComponent->GetSkeletalMeshAsset();
			// SetSkeletalMesh clears both layers. Profile restoration starts on a
			// later stable tick, never in the transition frame.
			continue;
		}

		if (CurrentMesh != SourceMesh)
		{
			// Another system replaced the whole mesh. Relinquish without writing
			// its skin stack, leader, bounds or presentation choices.
			State.bOwnsBoundsContract = false;
			ReleaseBodyCoverage(State);
			GarmentComponent->ComponentTags.Remove(ManagedTag);
			GarmentComponent->ComponentTags.Remove(PendingTag);
			UE_LOG(
				LogEFClothingMorphV2,
				Warning,
				TEXT("EFClothingMorphV2 relinquished rollback for %s after an external mesh replacement."),
				*GarmentComponent->GetName());
			It.RemoveCurrent();
			bRemovedRestore = true;
			continue;
		}

		State.bSourceMeshRestored = true;
		if (State.bAssignedLeaderPoseByV2
			&& GarmentComponent->LeaderPoseComponent.Get() == State.LeaderPoseAssignedByV2.Get())
		{
			GarmentComponent->SetLeaderPoseComponent(
				State.PreviousLeaderPoseComponent.Get(),
				true,
				false);
		}
		State.PreviousLeaderPoseComponent.Reset();
		State.LeaderPoseAssignedByV2.Reset();
		State.bAssignedLeaderPoseByV2 = false;
		ReleaseOwnedBoundsContract(GarmentComponent, State);

		if (!State.bSourceMorphStateReplayed && CustomizationComponent && !bIsRestoring)
		{
			bIsRestoring = true;
			CustomizationComponent->ReapplyCurrentMorphState();
			bIsRestoring = false;
			State.bSourceMorphStateReplayed = true;
		}
		else if (!CustomizationComponent)
		{
			State.bSourceMorphStateReplayed = true;
		}

		if (!State.bSourceMorphStateReplayed || GarmentComponent->IsSkinWeightProfilePending())
		{
			continue;
		}

		const FName DesiredPrimary = State.bCapturedPreviousSkinWeightProfiles
			&& State.PreviousSkinWeightProfileLayers.IsValidIndex(0)
			? State.PreviousSkinWeightProfileLayers[0]
			: NAME_None;
		const FName DesiredSecondary = State.bCapturedPreviousSkinWeightProfiles
			&& State.PreviousSkinWeightProfileLayers.IsValidIndex(1)
			? State.PreviousSkinWeightProfileLayers[1]
			: NAME_None;
		const FName CurrentPrimary = GarmentComponent->GetCurrentSkinWeightProfileName(
			ESkinWeightProfileLayer::Primary);
		const FName CurrentSecondary = GarmentComponent->GetCurrentSkinWeightProfileName(
			ESkinWeightProfileLayer::Secondary);

		if (CurrentPrimary != DesiredPrimary)
		{
			if (CurrentPrimary.IsNone() && !DesiredPrimary.IsNone())
			{
				if (!GarmentComponent->SetSkinWeightProfile(DesiredPrimary, ESkinWeightProfileLayer::Primary))
				{
					ReportBlockedRestore(FString::Printf(
						TEXT("could not request captured Primary=%s"),
						*DesiredPrimary.ToString()));
				}
			}
			else
			{
				ReportBlockedRestore(FString::Printf(
					TEXT("external Primary=%s conflicts with captured Primary=%s"),
					*CurrentPrimary.ToString(),
					*DesiredPrimary.ToString()));
			}
			continue;
		}

		if (CurrentSecondary != DesiredSecondary)
		{
			if (CurrentSecondary.IsNone() && !DesiredSecondary.IsNone())
			{
				if (!GarmentComponent->SetSkinWeightProfile(DesiredSecondary, ESkinWeightProfileLayer::Secondary))
				{
					ReportBlockedRestore(FString::Printf(
						TEXT("could not request captured Secondary=%s"),
						*DesiredSecondary.ToString()));
				}
			}
			else
			{
				ReportBlockedRestore(FString::Printf(
					TEXT("external Secondary=%s conflicts with captured Secondary=%s"),
					*CurrentSecondary.ToString(),
					*DesiredSecondary.ToString()));
			}
			continue;
		}

		if (GarmentComponent->IsSkinWeightProfilePending())
		{
			continue;
		}

		GarmentComponent->UpdateFollowerComponent();
		// The original garment is complete and will become visible below. Keep
		// auxiliary anatomy hidden throughout every asynchronous rollback frame,
		// then restore its exact pre-equip visibility immediately before exposure.
		ReleaseBodyCoverage(State);
		GarmentComponent->ComponentTags.Remove(ManagedTag);
		GarmentComponent->ComponentTags.Remove(PendingTag);
		if (State.bRenderSuppressedByV2)
		{
			GarmentComponent->SetRenderInMainPass(State.bRenderInMainPassBeforeV2);
			State.bRenderSuppressedByV2 = false;
		}
		LastStatus = FString::Printf(
			TEXT("Source restored with exact skin stack: %s (Primary=%s Secondary=%s)"),
			*GarmentComponent->GetName(),
			*DesiredPrimary.ToString(),
			*DesiredSecondary.ToString());
		It.RemoveCurrent();
		bRemovedRestore = true;
	}

	if (bRemovedRestore)
	{
		RefreshRetainedSourceMeshes();
	}
}

void UEFClothingFitRuntimeComponent::RestoreAllGarments()
{
	bIsRestoring = true;
	RestoreVisibilityGuards();
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FPrefetchGarmentState>& Pair : PrefetchingGarments)
	{
		RestorePrefetchGarment(Pair.Key.Get(), Pair.Value);
	}
	PrefetchingGarments.Reset();
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		RestoreGarment(Pair.Key.Get(), Pair.Value, true);
	}
	AppliedGarments.Reset();
	bIsRestoring = false;
	ProcessPendingRestores();
	RefreshRetainedSourceMeshes();
}

void UEFClothingFitRuntimeComponent::FinalizePendingRestoresForEndPlay()
{
	bIsRestoring = true;
	bool bReplayCustomizationState = false;
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : RestoringGarments)
	{
		USkeletalMeshComponent* GarmentComponent = Pair.Key.Get();
		FAppliedGarmentState& State = Pair.Value;
		ReleaseBodyCoverage(State);
		if (!IsValid(GarmentComponent))
		{
			continue;
		}

		if (CustomizationComponent)
		{
			CustomizationComponent->UnregisterExternalMorphWriter(GarmentComponent, this);
		}

		USkeletalMesh* SourceMesh = State.SourceMesh.Get();
		USkeletalMesh* CurrentMesh = GarmentComponent->GetSkeletalMeshAsset();
		const bool bCurrentMeshOwnedByV2 = CurrentMesh == State.FittedMesh.Get();
		const bool bCurrentMeshIsCapturedSource = CurrentMesh == SourceMesh;
		if (!bCurrentMeshOwnedByV2 && !bCurrentMeshIsCapturedSource)
		{
			// A different equipment system replaced the whole mesh. Coverage and
			// V2 tokens are released, but presentation/skin/leader/bounds on that
			// external mesh are outside this component's ownership.
			GarmentComponent->ComponentTags.Remove(ManagedTag);
			GarmentComponent->ComponentTags.Remove(PendingTag);
			State.bOwnsBoundsContract = false;
			continue;
		}

		for (const TPair<FName, float>& WrittenMorph : State.LastWrittenMorphValues)
		{
			if (!WrittenMorph.Key.IsNone())
			{
				GarmentComponent->SetMorphTarget(WrittenMorph.Key, 0.0f, true);
			}
		}

		if (IsValid(SourceMesh) && bCurrentMeshOwnedByV2)
		{
			// The derivative and its EF_AutoFit layer are V2-owned. At component
			// teardown there is no later tick in which to finish the ordinary
			// staged swap. Never destroy a layer acquired by another system while
			// V2 was active; that rare conflict remains hidden and is reported.
			const UEFClothingFitProfile* AppliedProfile = State.Profile.Get();
			const FName CurrentPrimary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Primary);
			const FName CurrentSecondary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Secondary);
			const bool bPrimaryOwnedByV2 = CurrentPrimary.IsNone()
				|| (IsValid(AppliedProfile) && CurrentPrimary == AppliedProfile->SkinWeightProfileName);
			if (bPrimaryOwnedByV2 && CurrentSecondary.IsNone())
			{
				GarmentComponent->SetSkeletalMesh(SourceMesh, true);
				CurrentMesh = GarmentComponent->GetSkeletalMeshAsset();
			}
			else
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Warning,
					TEXT("EndPlay left %s fail-closed: external skin stack Primary=%s Secondary=%s"),
					*GarmentComponent->GetName(),
					*CurrentPrimary.ToString(),
					*CurrentSecondary.ToString());
			}
		}

		const bool bCurrentMeshStillOwned = CurrentMesh == State.FittedMesh.Get()
			|| CurrentMesh == SourceMesh;
		if (bCurrentMeshStillOwned
			&& State.bAssignedLeaderPoseByV2
			&& GarmentComponent->LeaderPoseComponent.Get() == State.LeaderPoseAssignedByV2.Get())
		{
			GarmentComponent->SetLeaderPoseComponent(
				State.PreviousLeaderPoseComponent.Get(),
				true,
				false);
		}
		if (bCurrentMeshStillOwned)
		{
			ReleaseOwnedBoundsContract(GarmentComponent, State);
		}
		else
		{
			State.bOwnsBoundsContract = false;
		}

		if (CurrentMesh == SourceMesh)
		{
			const FName DesiredPrimary = State.bCapturedPreviousSkinWeightProfiles
				&& State.PreviousSkinWeightProfileLayers.IsValidIndex(0)
				? State.PreviousSkinWeightProfileLayers[0]
				: NAME_None;
			const FName DesiredSecondary = State.bCapturedPreviousSkinWeightProfiles
				&& State.PreviousSkinWeightProfileLayers.IsValidIndex(1)
				? State.PreviousSkinWeightProfileLayers[1]
				: NAME_None;
			const FName CurrentPrimary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Primary);
			const FName CurrentSecondary = GarmentComponent->GetCurrentSkinWeightProfileName(
				ESkinWeightProfileLayer::Secondary);
			if (CurrentPrimary.IsNone() && !DesiredPrimary.IsNone())
			{
				GarmentComponent->SetSkinWeightProfile(DesiredPrimary, ESkinWeightProfileLayer::Primary);
			}
			else if (CurrentPrimary != DesiredPrimary)
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Warning,
					TEXT("EndPlay preserved external Primary=%s instead of captured Primary=%s on %s"),
					*CurrentPrimary.ToString(),
					*DesiredPrimary.ToString(),
					*GarmentComponent->GetName());
			}
			if (CurrentSecondary.IsNone() && !DesiredSecondary.IsNone())
			{
				GarmentComponent->SetSkinWeightProfile(DesiredSecondary, ESkinWeightProfileLayer::Secondary);
			}
			else if (CurrentSecondary != DesiredSecondary)
			{
				UE_LOG(
					LogEFClothingMorphV2,
					Warning,
					TEXT("EndPlay preserved external Secondary=%s instead of captured Secondary=%s on %s"),
					*CurrentSecondary.ToString(),
					*DesiredSecondary.ToString(),
					*GarmentComponent->GetName());
			}
			bReplayCustomizationState = true;
		}

		if (CurrentMesh == SourceMesh
			&& GarmentComponent->ComponentTags.Contains(PendingTag)
			&& State.bRenderSuppressedByV2
			&& !GarmentComponent->bRenderInMainPass)
		{
			GarmentComponent->SetRenderInMainPass(State.bRenderInMainPassBeforeV2);
		}
		GarmentComponent->ComponentTags.Remove(ManagedTag);
		GarmentComponent->ComponentTags.Remove(PendingTag);
	}
	RestoringGarments.Reset();
	if (bReplayCustomizationState && CustomizationComponent)
	{
		CustomizationComponent->ReapplyCurrentMorphState();
	}
	bIsRestoring = false;
	RefreshRetainedSourceMeshes();
}

float UEFClothingFitRuntimeComponent::ResolveClearanceValue(
	USkeletalMeshComponent* GarmentComponent,
	const UEFClothingFitProfile* Profile,
	float RequiredMinimumMultiplier) const
{
	if (!IsValid(Profile))
	{
		return 0.0f;
	}
	const float* GarmentOverride = GarmentClearanceMultipliers.Find(GarmentComponent);
	const float GlobalMultiplier = FMath::IsFinite(RuntimeClearanceMultiplier)
		? FMath::Clamp(RuntimeClearanceMultiplier, 1.0f, 2.0f)
		: 1.0f;
	const float GarmentMultiplier = GarmentOverride && FMath::IsFinite(*GarmentOverride)
		? FMath::Clamp(*GarmentOverride, 1.0f, 2.0f)
		: 1.0f;
	const FAppliedGarmentState* AppliedState = AppliedGarments.Find(GarmentComponent);
	const float CatalogMinimum = AppliedState
		? AppliedState->CatalogMinimumClearanceMultiplier
		: EFClothingMorphV25::ClearanceTierMin;
	const float TierMinimum = Profile->CertifiedClearanceMultiplierMin;
	const float TierMaximum = Profile->CertifiedClearanceMultiplierMax;
	const int32 TierCount = Profile->CertifiedClearanceTierCount;
	if (!FMath::IsFinite(Profile->DefaultClearanceValue)
		|| !FMath::IsFinite(TierMinimum)
		|| !FMath::IsFinite(TierMaximum)
		|| TierMaximum < TierMinimum
		|| TierCount < 2)
	{
		return 0.0f;
	}

	const float SafeRequiredMinimum = FMath::IsFinite(RequiredMinimumMultiplier)
		? FMath::Clamp(RequiredMinimumMultiplier, TierMinimum, TierMaximum)
		: TierMaximum;
	const float RequestedValue = FMath::Clamp(
		FMath::Max(
			Profile->DefaultClearanceValue * GlobalMultiplier * GarmentMultiplier,
			FMath::Max(SafeRequiredMinimum, CatalogMinimum)),
		TierMinimum,
		TierMaximum);
	const float TierStep = (TierMaximum - TierMinimum) / static_cast<float>(TierCount - 1);
	if (!FMath::IsFinite(TierStep) || TierStep <= SMALL_NUMBER)
	{
		return TierMinimum;
	}
	// Round upward so an arbitrary public offset request never interpolates
	// below the next compiler-certified clearance tier.
	const int32 TierIndex = FMath::Clamp(
		FMath::CeilToInt((RequestedValue - TierMinimum) / TierStep),
		0,
		TierCount - 1);
	return TierMinimum + static_cast<float>(TierIndex) * TierStep;
}

void UEFClothingFitRuntimeComponent::RefreshRetainedSourceMeshes()
{
	TArray<TObjectPtr<USkeletalMesh>> NewRetainedMeshes;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		if (USkeletalMesh* SourceMesh = Pair.Value.SourceMesh.Get())
		{
			NewRetainedMeshes.AddUnique(SourceMesh);
		}
	}
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : RestoringGarments)
	{
		if (USkeletalMesh* SourceMesh = Pair.Value.SourceMesh.Get())
		{
			NewRetainedMeshes.AddUnique(SourceMesh);
		}
	}
	RetainedSourceMeshes = MoveTemp(NewRetainedMeshes);
}
