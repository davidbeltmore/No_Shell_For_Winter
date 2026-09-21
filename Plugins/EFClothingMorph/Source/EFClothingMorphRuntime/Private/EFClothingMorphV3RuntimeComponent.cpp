#include "EFClothingMorphV3RuntimeComponent.h"
#include "EFCharacterCreationAppearanceHooks.h"

#include "Animation/MeshDeformer.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFClothingFitProfile.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphSettings.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingSurfaceDeformerProducer.h"
#include "EFClothingSystemManifest.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/SecureHash.h"
#include "OptimusDeformer.h"
#include "OptimusDeformerDynamicInstanceManager.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEFClothingMorphV3, Log, All);

namespace EFClothingMorphV3RuntimePrivate
{
	constexpr double TransientRetrySeconds = 0.25;
	constexpr double StaleRetrySeconds = 2.0;
	constexpr double MinimumWatchdogSeconds = 0.25;
	constexpr double InitialBindingLoadRetrySeconds = 0.5;
	constexpr double MaximumBindingLoadRetrySeconds = 8.0;
	constexpr int32 MaximumBindingLoadBackoffExponent = 4;

	double ResolveWatchdogIntervalSeconds(const float ConfiguredIntervalSeconds)
	{
		if (!FMath::IsFinite(ConfiguredIntervalSeconds) || ConfiguredIntervalSeconds <= 0.0f)
		{
			return 0.0;
		}
		return FMath::Max(
			static_cast<double>(ConfiguredIntervalSeconds),
			MinimumWatchdogSeconds);
	}

	void GatherLayerRegionIds(
		const FEFClothingGarmentRow& Row,
		TSet<FName>& OutRegionIds)
	{
		OutRegionIds.Reset();
		for (const FName RegionId : Row.CoveredBodyRegionIds)
		{
			if (!RegionId.IsNone())
			{
				OutRegionIds.Add(RegionId);
			}
		}
		for (const FEFClothingRegionOccupancyRule& Occupancy : Row.LayerRule.OccupiedRegions)
		{
			if (!Occupancy.RegionId.IsNone())
			{
				OutRegionIds.Add(Occupancy.RegionId);
			}
		}
	}

	float ResolveLayerStackClearanceCm(
		const FEFClothingGarmentRow& TargetRow,
		const UEFClothingMorphDirectorPolicy* Director,
		const TSet<FName>& EquippedGarmentIds)
	{
		if (!IsValid(Director))
		{
			return 0.0f;
		}
		TSet<FName> TargetRegions;
		GatherLayerRegionIds(TargetRow, TargetRegions);
		if (TargetRegions.IsEmpty())
		{
			return 0.0f;
		}

		float StackClearanceCm = 0.0f;
		for (const FEFClothingGarmentRow& InnerRow : Director->Garments)
		{
			if (!InnerRow.bEnabled
				|| InnerRow.GarmentId == TargetRow.GarmentId
				|| InnerRow.GarmentId == TargetRow.AuthoredGarmentId
				|| !EquippedGarmentIds.Contains(InnerRow.GarmentId)
				|| static_cast<uint8>(InnerRow.LayerRule.Layer)
					>= static_cast<uint8>(TargetRow.LayerRule.Layer))
			{
				continue;
			}

			float InnerContributionCm = 0.0f;
			for (const FEFClothingRegionOccupancyRule& Occupancy : InnerRow.LayerRule.OccupiedRegions)
			{
				if (TargetRegions.Contains(Occupancy.RegionId))
				{
					InnerContributionCm = FMath::Max(
						InnerContributionCm,
						FMath::Max(Occupancy.OccupiedThicknessCm, 0.0f)
							+ FMath::Max(Occupancy.MinimumOuterGapCm, 0.0f));
				}
			}
			StackClearanceCm += InnerContributionCm;
		}
		return FMath::Clamp(
			StackClearanceCm,
			0.0f,
			EFClothingMorphV4::MaximumRuntimeClearanceCm);
	}

	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("ef.ClothingMorph.V4.Enabled"),
		1,
		TEXT("Enables the independent multi-clothing EF Clothing Morph V4 runtime. 0 leaves every source mesh untouched and visible."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarV5Enabled(
		TEXT("ef.ClothingMorph.V5.Enabled"),
		1,
		TEXT("Enables the single-table, streamable EF Clothing Morph V5.1 runtime. Legacy V4/V3 switches remain rollback aliases."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarLegacyV3Enabled(
		TEXT("ef.ClothingMorph.V3.Enabled"),
		1,
		TEXT("Legacy V3 rollback alias. Setting either the V3 or V4 switch to 0 disables the runtime."),
		ECVF_Default);

	bool DoesSkeletalMaterialMatchSlot(const FSkeletalMaterial& Material, const FName SlotName)
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

	bool ReadComponentDeformerOverrideFlag(
		const USkeletalMeshComponent* Component,
		bool& bOutHasComponentOverride)
	{
		bOutHasComponentOverride = false;
		if (!IsValid(Component))
		{
			return false;
		}

		// UE 5.8 exposes SetMeshDeformer(bool, ...) and the component deformer,
		// but not the override-enable bit. Reflection is used only to preserve the
		// exact pre-existing contract, including an explicit "None" override.
		const FBoolProperty* OverrideProperty = FindFProperty<FBoolProperty>(
			USkinnedMeshComponent::StaticClass(),
			TEXT("bSetMeshDeformer"));
		if (!OverrideProperty)
		{
			return false;
		}
		bOutHasComponentOverride = OverrideProperty->GetPropertyValue_InContainer(Component);
		return true;
	}

	bool RestoreInactiveComponentDeformerPointer(
		USkeletalMeshComponent* Component,
		UMeshDeformer* PreviousDeformer)
	{
		if (!IsValid(Component))
		{
			return false;
		}
		FObjectPropertyBase* DeformerProperty = FindFProperty<FObjectPropertyBase>(
			USkinnedMeshComponent::StaticClass(),
			TEXT("MeshDeformer"));
		if (!DeformerProperty)
		{
			return PreviousDeformer == nullptr;
		}
		DeformerProperty->SetObjectPropertyValue_InContainer(Component, PreviousDeformer);
		return true;
	}

	UOptimusDeformerDynamicInstanceManager* ResolveDynamicManager(
		USkeletalMeshComponent* Component,
		const int32 LODIndex)
	{
		return IsValid(Component) && LODIndex >= 0
			? Cast<UOptimusDeformerDynamicInstanceManager>(
				Component->GetMeshDeformerInstanceForLOD(LODIndex))
			: nullptr;
	}

	bool ValidateLiveRenderCounts(
		const USkeletalMesh* Mesh,
		const FEFClothingSurfaceTopologyFingerprint& Stored,
		FString& OutFailureReason)
	{
		const FSkeletalMeshRenderData* RenderData = IsValid(Mesh)
			? Mesh->GetResourceForRendering()
			: nullptr;
		if (!RenderData || !RenderData->LODRenderData.IsValidIndex(Stored.LODIndex))
		{
			OutFailureReason = TEXT("Cooked Skeletal Mesh render data is unavailable for the bound LOD.");
			return false;
		}

		const FSkeletalMeshLODRenderData& RenderLOD = RenderData->LODRenderData[Stored.LODIndex];
		const int32 IndexCount = RenderLOD.MultiSizeIndexContainer.IsIndexBufferValid()
			? RenderLOD.MultiSizeIndexContainer.GetIndexBuffer()->Num()
			: 0;
		if (Stored.RenderVertexCount != static_cast<int32>(RenderLOD.GetNumVertices())
			|| Stored.RenderIndexCount != IndexCount
			|| Stored.TriangleCount != IndexCount / 3
			|| Stored.SectionCount != RenderLOD.RenderSections.Num())
		{
			OutFailureReason = TEXT("The live clothing render counts no longer match the compiled V4 binding.");
			return false;
		}

#if WITH_EDITOR
		const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (!ImportedModel || !ImportedModel->LODModels.IsValidIndex(Stored.LODIndex))
		{
			OutFailureReason = TEXT("The editor source mesh has no imported LOD data for topology validation.");
			return false;
		}
		const FSkeletalMeshLODModel& ImportedLOD = ImportedModel->LODModels[Stored.LODIndex];
		TArray<uint32> Indices;
		RenderLOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
		if (ImportedLOD.MeshToImportVertexMap.Num() != Stored.RenderVertexCount)
		{
			OutFailureReason = TEXT("The editor source render/import vertex map is stale.");
			return false;
		}

		FMD5 Hash;
		auto UpdateHash = [&Hash](const void* Data, const uint64 Size)
		{
			Hash.Update(static_cast<const uint8*>(Data), Size);
		};
		const int32 LODIndex = Stored.LODIndex;
		const int32 VertexCount = static_cast<int32>(RenderLOD.GetNumVertices());
		const int32 LiveIndexCount = Indices.Num();
		const int32 SectionCount = RenderLOD.RenderSections.Num();
		UpdateHash(&LODIndex, sizeof(LODIndex));
		UpdateHash(&VertexCount, sizeof(VertexCount));
		UpdateHash(&LiveIndexCount, sizeof(LiveIndexCount));
		UpdateHash(&SectionCount, sizeof(SectionCount));
		if (!Indices.IsEmpty())
		{
			UpdateHash(Indices.GetData(), static_cast<uint64>(Indices.Num()) * sizeof(uint32));
		}
		if (!ImportedLOD.MeshToImportVertexMap.IsEmpty())
		{
			UpdateHash(
				ImportedLOD.MeshToImportVertexMap.GetData(),
				static_cast<uint64>(ImportedLOD.MeshToImportVertexMap.Num()) * sizeof(int32));
		}
		for (const FSkelMeshRenderSection& Section : RenderLOD.RenderSections)
		{
			UpdateHash(&Section.MaterialIndex, sizeof(Section.MaterialIndex));
			UpdateHash(&Section.BaseIndex, sizeof(Section.BaseIndex));
			UpdateHash(&Section.NumTriangles, sizeof(Section.NumTriangles));
			UpdateHash(&Section.BaseVertexIndex, sizeof(Section.BaseVertexIndex));
			UpdateHash(&Section.NumVertices, sizeof(Section.NumVertices));
			UpdateHash(&Section.bDisabled, sizeof(Section.bDisabled));
		}
		uint8 Digest[16] = {};
		Hash.Final(Digest);
		if (Stored.TopologyFingerprint != BytesToHex(Digest, UE_ARRAY_COUNT(Digest)))
		{
			OutFailureReason = TEXT("The exact clothing render topology changed after the V4 binding was built.");
			return false;
		}
#endif

		return true;
	}

	const TCHAR* StateToString(const EEFClothingMorphV3RuntimeState State)
	{
		switch (State)
		{
		case EEFClothingMorphV3RuntimeState::Disabled: return TEXT("Disabled");
		case EEFClothingMorphV3RuntimeState::Loading: return TEXT("Loading");
		case EEFClothingMorphV3RuntimeState::Passthrough: return TEXT("Passthrough");
		case EEFClothingMorphV3RuntimeState::WarmingUp: return TEXT("WarmingUp");
		case EEFClothingMorphV3RuntimeState::Ready: return TEXT("Ready");
		default: return TEXT("Unknown");
		}
	}
}

UEFClothingMorphV3RuntimeComponent::UEFClothingMorphV3RuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.TickInterval = 0.0f;
}

void UEFClothingMorphV3RuntimeComponent::BeginPlay()
{
	Super::BeginPlay();
	EFCharacterCreationGameplayHooks::GetOnBodyMeshesWillChange().AddWeakLambda(this, [this](AActor* Actor)
	{
		if (Actor == GetOwner())
		{
			ReleaseAllGarments();
			bReconcileRequested = true;
		}
	});
	if (const UEFClothingMorphSettings* V5Settings = GetDefault<UEFClothingMorphSettings>())
	{
		ReconcileWatchdogIntervalSeconds =
			V5Settings->GetEquipmentReconcileFallbackIntervalSeconds();
	}
	bAssetsReady = false;
	bAssetLoadFailed = false;
	bUsingV4FallbackRegistry = false;
	LoadedV4FallbackRegistry = nullptr;
	RequestedV4FallbackRegistryPath = FSoftObjectPath();
	bV4FallbackRegistryLoadAttempted = false;
	bV4FallbackRegistryLoadFailed = false;
	bReconcileRequested = true;
	NextReconcileSeconds = 0.0;
	NextSurfacePassSeconds = 0.0;
	LastStatus = TEXT("Loading V5 streamable clothing assets; source clothes remain visible.");
	StartAssetLoad();
}

void UEFClothingMorphV3RuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EFCharacterCreationGameplayHooks::GetOnBodyMeshesWillChange().RemoveAll(this);
	if (StartupLoadHandle.IsValid())
	{
		StartupLoadHandle->CancelHandle();
		StartupLoadHandle.Reset();
	}
	if (V4FallbackRegistryLoadHandle.IsValid())
	{
		V4FallbackRegistryLoadHandle->CancelHandle();
		V4FallbackRegistryLoadHandle.Reset();
	}
	for (TPair<FSoftObjectPath, TSharedPtr<FStreamableHandle>>& Pair : V5BindingLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	V5BindingLoadHandles.Reset();
	V5BindingLoadStartedSeconds.Reset();
	V5BindingRetryAfterSeconds.Reset();
	V5BindingFailureCounts.Reset();
	ReleaseAllGarments();
	LoadedRegistry = nullptr;
	LoadedV4FallbackRegistry = nullptr;
	LoadedDirector = nullptr;
	LoadedManifest = nullptr;
	LoadedSurfaceDeformer = nullptr;
	RequestedRegistryPath = FSoftObjectPath();
	RequestedV4FallbackRegistryPath = FSoftObjectPath();
	RequestedDirectorPath = FSoftObjectPath();
	RequestedManifestPath = FSoftObjectPath();
	bAssetsReady = false;
	bUsingV4FallbackRegistry = false;
	bV4FallbackRegistryLoadAttempted = false;
	bV4FallbackRegistryLoadFailed = false;
	bReconcileRequested = false;
	NextSurfacePassSeconds = 0.0;
	Super::EndPlay(EndPlayReason);
}

void UEFClothingMorphV3RuntimeComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UEFClothingMorphV2Settings* LegacySettings = GetDefault<UEFClothingMorphV2Settings>();
	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	const bool bRuntimeEnabled = Settings
		&& Settings->bEnabled
		&& LegacySettings
		&& LegacySettings->bEnabled
		&& EFClothingMorphV3RuntimePrivate::CVarV5Enabled.GetValueOnGameThread() != 0
		&& EFClothingMorphV3RuntimePrivate::CVarEnabled.GetValueOnGameThread() != 0
		&& EFClothingMorphV3RuntimePrivate::CVarLegacyV3Enabled.GetValueOnGameThread() != 0;
	if (!bRuntimeEnabled)
	{
		if (!ManagedGarments.IsEmpty())
		{
			ReleaseAllGarments();
		}
		// Rediscover immediately if the runtime is enabled again. This avoids
		// depending on a watchdog deadline while the feature is switched off.
		bReconcileRequested = true;
		return;
	}
	if (!bAssetsReady)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ExpireTimedOutV5BindingLoads(Now);
	const double WatchdogIntervalSeconds =
		EFClothingMorphV3RuntimePrivate::ResolveWatchdogIntervalSeconds(
			ReconcileWatchdogIntervalSeconds);
	const bool bWatchdogDue = WatchdogIntervalSeconds > 0.0
		&& Now >= NextReconcileSeconds;
	if (bReconcileRequested || bWatchdogDue)
	{
		ReconcileGarments();
		bReconcileRequested = false;
		NextReconcileSeconds = Now + WatchdogIntervalSeconds;
	}
	const double SurfacePassIntervalSeconds = Settings
		? static_cast<double>(Settings->GetMorphSyncIntervalSeconds())
		: 0.0;
	if (SurfacePassIntervalSeconds <= 0.0 || Now >= NextSurfacePassSeconds)
	{
		TickSurfacePasses(DeltaTime);
		NextSurfacePassSeconds = Now + SurfacePassIntervalSeconds;
	}
}

void UEFClothingMorphV3RuntimeComponent::ForceReconcile()
{
	NotifyEquipmentChanged();
	if (bAssetsReady)
	{
		ReconcileGarments();
		bReconcileRequested = false;
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		NextReconcileSeconds = Now
			+ EFClothingMorphV3RuntimePrivate::ResolveWatchdogIntervalSeconds(
				ReconcileWatchdogIntervalSeconds);
		TickSurfacePasses(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
	}
}

void UEFClothingMorphV3RuntimeComponent::NotifyEquipmentChanged()
{
	// Defer until the next tick so callers can finish adding/removing and
	// registering every SkeletalMeshComponent that belongs to one equip action.
	bReconcileRequested = true;
	NextReconcileSeconds = 0.0;
}

void UEFClothingMorphV3RuntimeComponent::SetGarmentClearanceOffsetCm(
	USkeletalMeshComponent* GarmentComponent,
	const float ClearanceCm)
{
	if (!IsValid(GarmentComponent) || GarmentComponent->GetOwner() != GetOwner())
	{
		return;
	}
	ClearanceOverridesCm.Add(
		GarmentComponent,
		FMath::Clamp(
			FMath::IsFinite(ClearanceCm) ? ClearanceCm : 0.0f,
			0.0f,
			EFClothingMorphV4::MaximumRuntimeClearanceCm));
}

void UEFClothingMorphV3RuntimeComponent::ClearGarmentClearanceOffsetCm(
	USkeletalMeshComponent* GarmentComponent)
{
	ClearanceOverridesCm.Remove(GarmentComponent);
}

void UEFClothingMorphV3RuntimeComponent::SetGarmentInflateCm(
	USkeletalMeshComponent* GarmentComponent,
	const float InflateCm)
{
	if (!IsValid(GarmentComponent) || GarmentComponent->GetOwner() != GetOwner())
	{
		return;
	}
	InflateOverridesCm.Add(
		GarmentComponent,
		FMath::Clamp(
			FMath::IsFinite(InflateCm) ? InflateCm : 0.0f,
			0.0f,
			EFClothingMorphV4::MaximumRuntimeInflateCm));
}

void UEFClothingMorphV3RuntimeComponent::ClearGarmentInflateCm(
	USkeletalMeshComponent* GarmentComponent)
{
	InflateOverridesCm.Remove(GarmentComponent);
}

EEFClothingMorphV3RuntimeState UEFClothingMorphV3RuntimeComponent::GetGarmentRuntimeState(
	const USkeletalMeshComponent* GarmentComponent) const
{
	if (!IsValid(GarmentComponent))
	{
		return EEFClothingMorphV3RuntimeState::Disabled;
	}
	const FManagedGarmentState* State = ManagedGarments.Find(
		const_cast<USkeletalMeshComponent*>(GarmentComponent));
	if (State)
	{
		return State->RuntimeState;
	}
	return bAssetsReady
		? EEFClothingMorphV3RuntimeState::Passthrough
		: EEFClothingMorphV3RuntimeState::Loading;
}

FString UEFClothingMorphV3RuntimeComponent::GetDebugSummary() const
{
	int32 PassthroughCount = 0;
	int32 WarmingUpCount = 0;
	int32 ReadyCount = 0;
	TArray<FString> GarmentSummaries;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState>& Pair : ManagedGarments)
	{
		const FManagedGarmentState& State = Pair.Value;
		PassthroughCount += State.RuntimeState == EEFClothingMorphV3RuntimeState::Passthrough ? 1 : 0;
		WarmingUpCount += State.RuntimeState == EEFClothingMorphV3RuntimeState::WarmingUp ? 1 : 0;
		ReadyCount += State.RuntimeState == EEFClothingMorphV3RuntimeState::Ready ? 1 : 0;
		const UEFClothingSurfaceDeformerProducer* Producer = State.Producer.Get();
		GarmentSummaries.Add(FString::Printf(
			TEXT("%s:%s[gLOD=%d,bLOD=%d,clear=%.3fcm,stack=%.3fcm,inflate=%.3fcm,morphActivity=%.3f,lowerMorph=%.3f,lowerReserve=%.3fcm,lowerGuardVerts=%d]%s"),
			*GetNameSafe(Pair.Key.Get()),
			EFClothingMorphV3RuntimePrivate::StateToString(State.RuntimeState),
			State.GarmentLODIndex,
			State.BodyLODIndex,
			ResolveClearanceCm(Pair.Key.Get(), State),
			State.LayerStackClearanceCm,
			ResolveInflateCm(Pair.Key.Get(), State),
			Producer ? Producer->GetLastBodyMorphActivity() : 0.0f,
			Producer ? Producer->GetLastLowerBodyMorphActivity() : 0.0f,
			Producer ? Producer->GetLastLowerBodyReserveCm() : 0.0f,
			Producer ? Producer->GetLowerBodyMorphGuardVertexCount() : 0,
			State.PassthroughReason.IsEmpty()
				? TEXT("")
				: *FString::Printf(TEXT(" reason=%s"), *State.PassthroughReason)));
	}
	GarmentSummaries.Sort();
	const FString SystemState = !bAssetsReady
		? (bAssetLoadFailed ? TEXT("Failed/Passthrough") : TEXT("Loading"))
		: (ClothingRowIssues.IsEmpty() && PassthroughCount == 0
			? TEXT("Ready")
			: TEXT("Degraded"));
	return FString::Printf(
		TEXT("EFClothingMorphV5 state=%s registry=%s managed=%d ready=%d warming=%d passthrough=%d issues=%d | %s | %s | %s"),
		*SystemState,
		bUsingV4FallbackRegistry ? TEXT("V4Fallback") : TEXT("V5Streamable"),
		ManagedGarments.Num(),
		ReadyCount,
		WarmingUpCount,
		PassthroughCount,
		ClothingRowIssues.Num(),
		*FString::Join(GarmentSummaries, TEXT("; ")),
		*FString::Join(ClothingRowIssues, TEXT("; ")),
		*LastStatus);
}

void UEFClothingMorphV3RuntimeComponent::StartAssetLoad()
{
	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	const UEFClothingMorphV2Settings* LegacySettings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings || !Settings->bEnabled || !LegacySettings || !LegacySettings->bEnabled)
	{
		LastStatus = TEXT("V5 disabled by project settings; source clothes remain visible.");
		return;
	}

	RequestedRegistryPath = !Settings->V5Registry.IsNull()
		? Settings->V5Registry.ToSoftObjectPath()
		: (Settings->bAllowV4Fallback
			? Settings->V4FallbackRegistry.ToSoftObjectPath()
			: FSoftObjectPath());
	RequestedDirectorPath = !Settings->Director.IsNull()
		? Settings->Director.ToSoftObjectPath()
		: LegacySettings->DirectorPolicy.ToSoftObjectPath();
	bUsingV4FallbackRegistry = RequestedRegistryPath
		== Settings->V4FallbackRegistry.ToSoftObjectPath();
	RequestedManifestPath = !bUsingV4FallbackRegistry && !Settings->V5SystemManifest.IsNull()
		? Settings->V5SystemManifest.ToSoftObjectPath()
		: FSoftObjectPath();

	TArray<FSoftObjectPath> AssetsToLoad;
	if (!RequestedRegistryPath.IsNull())
	{
		AssetsToLoad.AddUnique(RequestedRegistryPath);
	}
	if (!RequestedDirectorPath.IsNull())
	{
		AssetsToLoad.AddUnique(RequestedDirectorPath);
	}
	if (!LegacySettings->SurfaceConstraintDeformer.IsNull())
	{
		AssetsToLoad.AddUnique(LegacySettings->SurfaceConstraintDeformer.ToSoftObjectPath());
	}
	if (!RequestedManifestPath.IsNull())
	{
		AssetsToLoad.AddUnique(RequestedManifestPath);
	}
	const int32 ExpectedStartupAssetCount = bUsingV4FallbackRegistry ? 3 : 4;
	if (AssetsToLoad.Num() != ExpectedStartupAssetCount)
	{
		bAssetLoadFailed = true;
		LastStatus = TEXT("V5 manifest, registry, Director or surface graph is not configured; source clothes remain visible.");
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
		return;
	}

	StartupLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UEFClothingMorphV3RuntimeComponent::HandleAssetsReady),
		FStreamableManager::AsyncLoadHighPriority);
	if (!StartupLoadHandle.IsValid())
	{
		bAssetLoadFailed = true;
		LastStatus = TEXT("V5 asynchronous asset request could not start; source clothes remain visible.");
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingMorphV3RuntimeComponent::HandleAssetsReady()
{
	StartupLoadHandle.Reset();
	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	const UEFClothingMorphV2Settings* LegacySettings = GetDefault<UEFClothingMorphV2Settings>();
	LoadedRegistry = Cast<UEFClothingFitRegistry>(RequestedRegistryPath.ResolveObject());
	LoadedDirector = Cast<UEFClothingMorphDirectorPolicy>(RequestedDirectorPath.ResolveObject());
	LoadedManifest = Cast<UEFClothingSystemManifest>(RequestedManifestPath.ResolveObject());
	LoadedSurfaceDeformer = LegacySettings ? LegacySettings->SurfaceConstraintDeformer.Get() : nullptr;

	FString ManifestError;
	const bool bV5ManifestUsable = bUsingV4FallbackRegistry
		|| (IsValid(LoadedManifest)
			&& LoadedManifest->Validate(ManifestError)
			&& LoadedManifest->BindingRegistry.ToSoftObjectPath() == RequestedRegistryPath);
	const bool bV5RegistryUsable = IsValid(LoadedRegistry)
		&& (!LoadedRegistry->V5StreamableBindings.IsEmpty()
			|| !LoadedRegistry->NativeSourceBindings.IsEmpty())
		&& bV5ManifestUsable;
	if (!bV5RegistryUsable
		&& Settings
		&& Settings->bAllowV4Fallback
		&& !Settings->V4FallbackRegistry.IsNull()
		&& RequestedRegistryPath != Settings->V4FallbackRegistry.ToSoftObjectPath())
	{
		LoadedRegistry = Settings->V4FallbackRegistry.LoadSynchronous();
		bUsingV4FallbackRegistry = IsValid(LoadedRegistry);
		if (bUsingV4FallbackRegistry)
		{
			LoadedManifest = nullptr;
		}
	}

	FString DirectorError;
	if (!IsValid(LoadedRegistry)
		|| !IsValid(LoadedDirector)
		|| !IsValid(LoadedSurfaceDeformer)
		|| (!bUsingV4FallbackRegistry && !bV5ManifestUsable)
		|| !LoadedDirector->ValidateIdentity(DirectorError))
	{
		bAssetLoadFailed = true;
		bAssetsReady = false;
		LastStatus = FString::Printf(
			TEXT("V5 startup validation failed; source clothes remain visible. %s"),
			*(DirectorError.IsEmpty() ? ManifestError : DirectorError));
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
		return;
	}

	bAssetLoadFailed = false;
	bAssetsReady = true;
	LastStatus = bUsingV4FallbackRegistry
		? TEXT("V5 started with the V4 rollback registry; each clothing entry remains isolated and visible on failure.")
		: TEXT("V5 streamable registry loaded; binding payloads will load only for equipped garments.");
	ForceReconcile();
}

void UEFClothingMorphV3RuntimeComponent::RequestV4FallbackRegistryLoad()
{
	if (bUsingV4FallbackRegistry
		|| IsValid(LoadedV4FallbackRegistry)
		|| bV4FallbackRegistryLoadAttempted)
	{
		return;
	}

	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	if (!Settings || !Settings->bAllowV4Fallback || Settings->V4FallbackRegistry.IsNull())
	{
		return;
	}

	// The attempt flag is permanent for this component lifetime. A corrupt or
	// missing compatibility registry therefore cannot create an async retry loop.
	bV4FallbackRegistryLoadAttempted = true;
	bV4FallbackRegistryLoadFailed = false;
	RequestedV4FallbackRegistryPath = Settings->V4FallbackRegistry.ToSoftObjectPath();
	V4FallbackRegistryLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedV4FallbackRegistryPath,
		FStreamableDelegate::CreateUObject(
			this,
			&UEFClothingMorphV3RuntimeComponent::HandleV4FallbackRegistryLoadComplete),
		FStreamableManager::AsyncLoadHighPriority);
	if (!V4FallbackRegistryLoadHandle.IsValid())
	{
		bV4FallbackRegistryLoadFailed = true;
		LastStatus = TEXT("V5 could not start the lazy V4 fallback registry request; source clothing remains visible.");
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
		return;
	}

	LastStatus = TEXT("V5 has no exact binding metadata for an equipped garment; the V4 fallback registry is streaming while source clothing remains visible.");
}

void UEFClothingMorphV3RuntimeComponent::HandleV4FallbackRegistryLoadComplete()
{
	TSharedPtr<FStreamableHandle> CompletedHandle = V4FallbackRegistryLoadHandle;
	V4FallbackRegistryLoadHandle.Reset();
	UObject* LoadedObject = CompletedHandle.IsValid()
		? CompletedHandle->GetLoadedAsset()
		: RequestedV4FallbackRegistryPath.ResolveObject();
	LoadedV4FallbackRegistry = Cast<UEFClothingFitRegistry>(LoadedObject);
	bV4FallbackRegistryLoadFailed = !IsValid(LoadedV4FallbackRegistry);

	if (bV4FallbackRegistryLoadFailed)
	{
		LastStatus = TEXT("The lazy V4 fallback registry did not load; source clothing remains visible.");
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
	}
	else
	{
		LastStatus = TEXT("The lazy V4 fallback registry is available for garments without exact V5 metadata.");
	}
	NotifyEquipmentChanged();
}

void UEFClothingMorphV3RuntimeComponent::RequestV5BindingLoad(
	const FSoftObjectPath& BindingPath)
{
	if (BindingPath.IsNull())
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (const double* RetryAfterSeconds = V5BindingRetryAfterSeconds.Find(BindingPath))
	{
		if (Now < *RetryAfterSeconds)
		{
			return;
		}
	}
	if (UEFClothingSurfaceBinding* LoadedBinding =
		Cast<UEFClothingSurfaceBinding>(BindingPath.ResolveObject()))
	{
		FString IntegrityFailure;
		if (!ValidateV5BindingPayload(BindingPath, LoadedBinding, IntegrityFailure))
		{
			UE_LOG(
				LogEFClothingMorphV3,
				Warning,
				TEXT("V5 rejected loaded binding payload %s: %s"),
				*BindingPath.ToString(),
				*IntegrityFailure);
			RecordV5BindingLoadFailure(BindingPath);
			return;
		}
		RetainedRuntimeObjects.AddUnique(LoadedBinding);
		V5BindingRetryAfterSeconds.Remove(BindingPath);
		V5BindingFailureCounts.Remove(BindingPath);
		return;
	}
	if (V5BindingLoadHandles.Contains(BindingPath))
	{
		return;
	}
	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	const int32 MaximumConcurrentLoads = Settings
		? Settings->GetMaximumConcurrentBindingLoads()
		: 2;
	if (V5BindingLoadHandles.Num() >= MaximumConcurrentLoads)
	{
		return;
	}

	TSharedPtr<FStreamableHandle> LoadHandle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			BindingPath,
			FStreamableDelegate::CreateUObject(
				this,
				&UEFClothingMorphV3RuntimeComponent::HandleV5BindingLoadComplete,
				BindingPath),
			FStreamableManager::AsyncLoadHighPriority);
	if (!LoadHandle.IsValid())
	{
		RecordV5BindingLoadFailure(BindingPath);
		return;
	}

	V5BindingLoadHandles.Add(BindingPath, MoveTemp(LoadHandle));
	V5BindingLoadStartedSeconds.Add(BindingPath, Now);
	LastStatus = FString::Printf(
		TEXT("V5 streaming clothing binding %s; the source garment remains visible."),
		*BindingPath.ToString());
}

void UEFClothingMorphV3RuntimeComponent::HandleV5BindingLoadComplete(
	FSoftObjectPath BindingPath)
{
	TSharedPtr<FStreamableHandle> CompletedHandle;
	if (TSharedPtr<FStreamableHandle>* Handle = V5BindingLoadHandles.Find(BindingPath))
	{
		CompletedHandle = *Handle;
	}
	V5BindingLoadHandles.Remove(BindingPath);
	V5BindingLoadStartedSeconds.Remove(BindingPath);

	UObject* LoadedObject = CompletedHandle.IsValid()
		? CompletedHandle->GetLoadedAsset()
		: BindingPath.ResolveObject();
	UEFClothingSurfaceBinding* LoadedBinding = Cast<UEFClothingSurfaceBinding>(LoadedObject);
	FString IntegrityFailure;
	if (!IsValid(LoadedBinding)
		|| !ValidateV5BindingPayload(BindingPath, LoadedBinding, IntegrityFailure))
	{
		if (IsValid(LoadedBinding))
		{
			UE_LOG(
				LogEFClothingMorphV3,
				Warning,
				TEXT("V5 rejected streamed binding payload %s: %s"),
				*BindingPath.ToString(),
				*IntegrityFailure);
		}
		RecordV5BindingLoadFailure(BindingPath);
		NotifyEquipmentChanged();
		return;
	}

	// The registry deliberately owns only a soft reference. Retain the resolved
	// payload for as long as this runtime component can have producers using it.
	RetainedRuntimeObjects.AddUnique(LoadedBinding);
	V5BindingRetryAfterSeconds.Remove(BindingPath);
	V5BindingFailureCounts.Remove(BindingPath);
	LastStatus = FString::Printf(
		TEXT("V5 clothing binding streamed: %s."),
		*BindingPath.ToString());
	NotifyEquipmentChanged();
}

bool UEFClothingMorphV3RuntimeComponent::ValidateV5BindingPayload(
	const FSoftObjectPath& BindingPath,
	const UEFClothingSurfaceBinding* LoadedBinding,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(LoadedRegistry) || BindingPath.IsNull() || !IsValid(LoadedBinding))
	{
		OutFailureReason = TEXT("Registry, payload path, or loaded binding is invalid.");
		return false;
	}

	bool bFoundRecord = false;
	for (const FEFClothingV5StreamableBinding& Record : LoadedRegistry->V5StreamableBindings)
	{
		if (Record.Binding.ToSoftObjectPath() != BindingPath)
		{
			continue;
		}
		bFoundRecord = true;
		FString RecordFailure;
		if (!Record.ValidateLoadedPayload(LoadedBinding, &RecordFailure))
		{
			OutFailureReason = FString::Printf(
				TEXT("record %s failed integrity validation: %s"),
				*Record.StableBindingId.ToString(),
				*RecordFailure);
			return false;
		}
	}
	if (!bFoundRecord)
	{
		OutFailureReason = TEXT("No V5 registry record owns this payload path.");
		return false;
	}
	return true;
}

void UEFClothingMorphV3RuntimeComponent::RecordV5BindingLoadFailure(
	const FSoftObjectPath& BindingPath)
{
	int32& FailureCount = V5BindingFailureCounts.FindOrAdd(BindingPath);
	FailureCount = FMath::Min(
		FailureCount + 1,
		EFClothingMorphV3RuntimePrivate::MaximumBindingLoadBackoffExponent + 1);
	const int32 BackoffExponent = FMath::Clamp(
		FailureCount - 1,
		0,
		EFClothingMorphV3RuntimePrivate::MaximumBindingLoadBackoffExponent);
	const double RetryDelaySeconds = FMath::Min(
		EFClothingMorphV3RuntimePrivate::InitialBindingLoadRetrySeconds
			* static_cast<double>(1 << BackoffExponent),
		EFClothingMorphV3RuntimePrivate::MaximumBindingLoadRetrySeconds);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	V5BindingRetryAfterSeconds.Add(BindingPath, Now + RetryDelaySeconds);
	LastStatus = FString::Printf(
		TEXT("V5 clothing binding load failed for %s; visible passthrough will retry in %.1f seconds."),
		*BindingPath.ToString(),
		RetryDelaySeconds);
	if (FailureCount <= 3)
	{
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingMorphV3RuntimeComponent::ExpireTimedOutV5BindingLoads(
	const double NowSeconds)
{
	if (V5BindingLoadHandles.IsEmpty())
	{
		return;
	}
	const UEFClothingMorphSettings* Settings = GetDefault<UEFClothingMorphSettings>();
	const double TimeoutSeconds = Settings
		? static_cast<double>(Settings->GetBindingLoadTimeoutSeconds())
		: 5.0;
	TArray<FSoftObjectPath> TimedOutPaths;
	for (const TPair<FSoftObjectPath, double>& Pair : V5BindingLoadStartedSeconds)
	{
		if (NowSeconds - Pair.Value >= TimeoutSeconds)
		{
			TimedOutPaths.Add(Pair.Key);
		}
	}
	for (const FSoftObjectPath& BindingPath : TimedOutPaths)
	{
		if (TSharedPtr<FStreamableHandle>* Handle = V5BindingLoadHandles.Find(BindingPath))
		{
			if (Handle->IsValid())
			{
				(*Handle)->CancelHandle();
			}
		}
		V5BindingLoadHandles.Remove(BindingPath);
		V5BindingLoadStartedSeconds.Remove(BindingPath);
		RecordV5BindingLoadFailure(BindingPath);
	}
	if (!TimedOutPaths.IsEmpty())
	{
		NotifyEquipmentChanged();
	}
}

USkeletalMeshComponent* UEFClothingMorphV3RuntimeComponent::ResolveExactBodyComponent(
	const USkeletalMesh* ExpectedBody) const
{
	if (!GetOwner() || !IsValid(ExpectedBody))
	{
		return nullptr;
	}

	USkeletalMeshComponent* UniqueRenderableBody = nullptr;
	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	for (USkeletalMeshComponent* Candidate : MeshComponents)
	{
		if (!IsValid(Candidate)
			|| Candidate->GetSkeletalMeshAsset() != ExpectedBody
			|| !Candidate->IsRegistered()
			|| !Candidate->IsVisible()
			|| !Candidate->bRenderInMainPass
			|| Candidate->bHiddenInGame)
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

void UEFClothingMorphV3RuntimeComponent::ReconcileGarments()
{
	if (!GetOwner() || !IsValid(LoadedRegistry) || !IsValid(LoadedDirector))
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	TSet<FName> EquippedGarmentIds;
	for (const FEFClothingGarmentRow& CandidateRow : LoadedDirector->Garments)
	{
		USkeletalMesh* CandidateMesh = CandidateRow.bEnabled
			? CandidateRow.SourceGarment.Get()
			: nullptr;
		if (!IsValid(CandidateMesh))
		{
			continue;
		}
		for (USkeletalMeshComponent* CandidateComponent : MeshComponents)
		{
			if (IsValid(CandidateComponent)
				&& CandidateComponent->GetSkeletalMeshAsset() == CandidateMesh
				&& CandidateComponent->IsRegistered()
				&& CandidateComponent->IsVisible()
				&& CandidateComponent->bRenderInMainPass
				&& !CandidateComponent->bHiddenInGame)
			{
				EquippedGarmentIds.Add(CandidateRow.GarmentId);
				break;
			}
		}
	}
	// A component can change mesh in place. Release every old-body owner first,
	// otherwise material indices/bone ownership can leak into the new body.
	for (auto It = ManagedGarments.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* Body = It.Value().BodyComponent.Get();
		if (!IsValid(Body) || Body->GetSkeletalMeshAsset() != It.Value().BodyAsset.Get())
		{
			ReleaseGarment(It.Key().Get(), It.Value());
			It.RemoveCurrent();
		}
	}
	const TArray<FEFClothingGarmentRow> BodyVariants = LoadedDirector->BuildBodyVariants();
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> ObservedGarments;
	TSet<FName> ObservedClothingIds;
	TSet<FString> ObservedSourceBodyPairs;
	ClothingRowIssues.Reset();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	for (int32 ClothingIndex = 0; ClothingIndex < BodyVariants.Num(); ++ClothingIndex)
	{
		const FEFClothingGarmentRow& CatalogRow = BodyVariants[ClothingIndex];
		if (!CatalogRow.bEnabled)
		{
			continue;
		}
		FString ClothingValidationError;
		if (!CatalogRow.ValidateClothingForUse(ClothingValidationError))
		{
			ClothingRowIssues.Add(FString::Printf(
				TEXT("Clothes[%d]: %s"),
				ClothingIndex,
				*ClothingValidationError));
			continue;
		}
		if (CatalogRow.Backend != EEFClothingSurfaceBackend::SurfaceWrapGPU)
		{
			ClothingRowIssues.Add(FString::Printf(
				TEXT("%s: the V4 live fit requires Automatic Surface Fit."),
				*CatalogRow.GarmentId.ToString()));
			continue;
		}
		if (!ResolveExactBodyComponent(CatalogRow.BodySurface.Get())) { continue; }
		if (ObservedClothingIds.Contains(CatalogRow.GarmentId))
		{
			ClothingRowIssues.Add(FString::Printf(
				TEXT("%s: duplicate Clothing Name; the first entry remains active."),
				*CatalogRow.GarmentId.ToString()));
			continue;
		}
		const FString SourceBodyPair =
			CatalogRow.SourceGarment.ToSoftObjectPath().ToString()
			+ TEXT("|")
			+ CatalogRow.BodySurface.ToSoftObjectPath().ToString();
		if (ObservedSourceBodyPairs.Contains(SourceBodyPair))
		{
			ClothingRowIssues.Add(FString::Printf(
				TEXT("%s: duplicate Clothing Mesh and Body Mesh pair; the first entry remains active."),
				*CatalogRow.GarmentId.ToString()));
			continue;
		}
		ObservedClothingIds.Add(CatalogRow.GarmentId);
		ObservedSourceBodyPairs.Add(SourceBodyPair);

		USkeletalMesh* SourceMesh = CatalogRow.SourceGarment.Get();
		USkeletalMesh* BodyMesh = CatalogRow.BodySurface.Get();
		USkeletalMeshComponent* BodyComponent = ResolveExactBodyComponent(BodyMesh);
		if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
		{
			continue;
		}

		for (USkeletalMeshComponent* GarmentComponent : MeshComponents)
		{
			if (!IsValid(GarmentComponent)
				|| GarmentComponent == BodyComponent
				|| GarmentComponent->GetSkeletalMeshAsset() != SourceMesh
				|| !GarmentComponent->IsRegistered()
				|| !GarmentComponent->IsVisible()
				|| !GarmentComponent->bRenderInMainPass
				|| GarmentComponent->bHiddenInGame)
			{
				continue;
			}
			ObservedGarments.Add(GarmentComponent);

			// V5 publishes one soft payload record per explicitly requested garment
			// LOD. Current content is LOD0, but selecting from the component state
			// here keeps future chunks independent and streamable.
			const int32 RequestedBindingLODIndex =
				FMath::Max(GarmentComponent->GetPredictedLODLevel(), 0);
			const FEFClothingV5StreamableBinding* V5BindingRecord =
				LoadedRegistry->FindV5StreamableBinding(
					CatalogRow.GarmentId,
					SourceMesh,
					BodyMesh,
					RequestedBindingLODIndex);
			const bool bUsesV5StreamableBinding = V5BindingRecord != nullptr;
			FSoftObjectPath BindingAssetPath;
			const UEFClothingSurfaceBinding* Binding = nullptr;
			if (bUsesV5StreamableBinding)
			{
				BindingAssetPath = V5BindingRecord->Binding.ToSoftObjectPath();
				RequestV5BindingLoad(BindingAssetPath);
				Binding = V5BindingRecord->Binding.Get();
			}
			else
			{
				// Exact V4 compatibility is lazy when V5 is healthy. The secondary
				// hard-reference registry is requested only after an equipped garment
				// lacks V5 metadata for its requested LOD, never during healthy startup.
				const UEFClothingFitRegistry* NativeBindingRegistry = bUsingV4FallbackRegistry
					? LoadedRegistry.Get()
					: LoadedV4FallbackRegistry.Get();
				if (!bUsingV4FallbackRegistry && !IsValid(NativeBindingRegistry))
				{
					RequestV4FallbackRegistryLoad();
					NativeBindingRegistry = LoadedV4FallbackRegistry.Get();
				}
				Binding = IsValid(NativeBindingRegistry)
					? NativeBindingRegistry->FindNativeSourceBinding(
						CatalogRow.GarmentId,
						SourceMesh,
						BodyMesh)
					: nullptr;
				if (IsValid(Binding))
				{
					BindingAssetPath = FSoftObjectPath(Binding);
				}
			}

			FManagedGarmentState* Existing = ManagedGarments.Find(GarmentComponent);
			const FString CompileFingerprint = CatalogRow.BuildCompileFingerprint();
			const bool bIdentityChanged = Existing
				&& (Existing->SourceMesh.Get() != SourceMesh
					|| Existing->BodyComponent.Get() != BodyComponent
					|| Existing->Binding.Get() != Binding
					|| Existing->GarmentId != CatalogRow.GarmentId
					|| Existing->BindingAssetPath != BindingAssetPath
					|| Existing->RequestedBindingLODIndex != RequestedBindingLODIndex
					|| Existing->bUsesV5StreamableBinding != bUsesV5StreamableBinding
					|| Existing->CompileFingerprint != CompileFingerprint);
			if (bIdentityChanged)
			{
				ReleaseGarment(GarmentComponent, *Existing);
				ManagedGarments.Remove(GarmentComponent);
				Existing = nullptr;
			}

			if (!Existing)
			{
				FManagedGarmentState NewState;
				NewState.SourceMesh = SourceMesh;
				NewState.BodyComponent = BodyComponent;
				NewState.BodyAsset = BodyMesh;
				NewState.CatalogRow = CatalogRow;
				NewState.Binding = Binding;
				NewState.GarmentId = CatalogRow.GarmentId;
				NewState.BindingAssetPath = BindingAssetPath;
				NewState.CompileFingerprint = CompileFingerprint;
				NewState.RequestedBindingLODIndex = RequestedBindingLODIndex;
				NewState.bUsesV5StreamableBinding = bUsesV5StreamableBinding;
				NewState.RuntimeState = EEFClothingMorphV3RuntimeState::Loading;
				Existing = &ManagedGarments.Add(GarmentComponent, MoveTemp(NewState));
				AcquireBodyCoverage(*Existing, CatalogRow);
			}

			if (Existing->CatalogRow.BodySectionsToExclude != CatalogRow.BodySectionsToExclude
				|| Existing->CatalogRow.BodyBoneBranchesToHide != CatalogRow.BodyBoneBranchesToHide)
			{
				ReleaseBodyCoverage(*Existing);
				AcquireBodyCoverage(*Existing, CatalogRow);
			}
			Existing->CatalogRow = CatalogRow;
			Existing->LayerStackClearanceCm =
				EFClothingMorphV3RuntimePrivate::ResolveLayerStackClearanceCm(
					CatalogRow,
					LoadedDirector,
					EquippedGarmentIds);
			Existing->DirectorClearanceCm = FMath::Clamp(
				FMath::IsFinite(CatalogRow.AdditionalClearanceCm)
					? CatalogRow.AdditionalClearanceCm + Existing->LayerStackClearanceCm
					: 0.0f,
				0.0f,
				EFClothingMorphV4::MaximumRuntimeClearanceCm);
			// V4 treats the per-clothing runtime thickness value as a topology-free
			// visual inflate. It is always live; the source remains single-layer.
			Existing->DirectorInflateCm = FMath::Clamp(
				FMath::IsFinite(CatalogRow.ShellThicknessCm)
					? CatalogRow.ShellThicknessCm
					: 0.0f,
				0.0f,
				EFClothingMorphV4::MaximumRuntimeInflateCm);
			Existing->MaximumCorrectionCm = FMath::IsFinite(CatalogRow.MaximumCorrectionCm)
				? CatalogRow.MaximumCorrectionCm
				: -1.0f;

			if (!IsValid(BodyComponent))
			{
				SetPassthrough(
					GarmentComponent,
					*Existing,
					TEXT("No unique visible component uses the exact Director reference body."),
					EFClothingMorphV3RuntimePrivate::StaleRetrySeconds);
				continue;
			}
			if (!IsValid(Binding))
			{
				FString MissingBindingReason =
					TEXT("No V5 metadata or V4 fallback binding is published for this clothing name/source/body/LOD combination.");
				if (bUsesV5StreamableBinding)
				{
					if (V5BindingLoadHandles.Contains(BindingAssetPath))
					{
						MissingBindingReason = FString::Printf(
							TEXT("V5 binding payload for requested garment LOD %d is streaming; source clothing remains visible."),
							RequestedBindingLODIndex);
					}
					else if (V5BindingRetryAfterSeconds.Contains(BindingAssetPath))
					{
						MissingBindingReason = FString::Printf(
							TEXT("V5 binding payload load failed for requested garment LOD %d; visible passthrough is waiting for its bounded retry."),
							RequestedBindingLODIndex);
					}
					else
					{
						MissingBindingReason = FString::Printf(
							TEXT("V5 binding payload for requested garment LOD %d is unavailable; visible passthrough will retry."),
							RequestedBindingLODIndex);
					}
				}
				else if (V4FallbackRegistryLoadHandle.IsValid())
				{
					MissingBindingReason = FString::Printf(
						TEXT("No exact V5 metadata exists for requested garment LOD %d; the V4 fallback registry is streaming and source clothing remains visible."),
						RequestedBindingLODIndex);
				}
				else if (bV4FallbackRegistryLoadFailed)
				{
					MissingBindingReason = FString::Printf(
						TEXT("No exact V5 metadata exists for requested garment LOD %d and the one-shot V4 fallback registry load failed; source clothing remains visible."),
						RequestedBindingLODIndex);
				}
				SetPassthrough(
					GarmentComponent,
					*Existing,
					MissingBindingReason,
					EFClothingMorphV3RuntimePrivate::StaleRetrySeconds);
				continue;
			}
			if (!Existing->Producer.IsValid() && Now >= Existing->NextInstallAttemptSeconds)
			{
				FString InstallFailure;
				if (!TryInstallSurfaceConstraint(GarmentComponent, *Existing, CatalogRow, InstallFailure))
				{
					SetPassthrough(
						GarmentComponent,
						*Existing,
						InstallFailure,
						EFClothingMorphV3RuntimePrivate::TransientRetrySeconds);
				}
			}
		}
	}

	for (auto It = ManagedGarments.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* GarmentComponent = It.Key().Get();
		if (!IsValid(GarmentComponent) || !ObservedGarments.Contains(It.Key()))
		{
			ReleaseGarment(GarmentComponent, It.Value());
			ClearanceOverridesCm.Remove(It.Key());
			InflateOverridesCm.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	for (auto It = ClearanceOverridesCm.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = InflateOverridesCm.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	EvictUnusedV5BindingPayloads();
}

void UEFClothingMorphV3RuntimeComponent::EvictUnusedV5BindingPayloads()
{
	TSet<FSoftObjectPath> ActiveBindingPaths;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState>& Pair : ManagedGarments)
	{
		if (Pair.Value.bUsesV5StreamableBinding && !Pair.Value.BindingAssetPath.IsNull())
		{
			ActiveBindingPaths.Add(Pair.Value.BindingAssetPath);
		}
	}
	RetainedRuntimeObjects.RemoveAll([&ActiveBindingPaths](const TObjectPtr<UObject>& Object)
	{
		const UEFClothingSurfaceBinding* Binding = Cast<UEFClothingSurfaceBinding>(Object.Get());
		return IsValid(Binding)
			&& !ActiveBindingPaths.Contains(FSoftObjectPath(Binding));
	});
}

bool UEFClothingMorphV3RuntimeComponent::ValidateNativeBinding(
	const FEFClothingGarmentRow& CatalogRow,
	USkeletalMesh* SourceMesh,
	USkeletalMesh* BodyMesh,
	const UEFClothingSurfaceBinding* Binding,
	const int32 GarmentLODIndex,
	const int32 BodyLODIndex,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh) || !IsValid(Binding))
	{
		OutFailureReason = TEXT("Clothing mesh, body mesh or V4 binding is unavailable.");
		return false;
	}
	if (Binding->CompilerVersion != EFClothingMorphV4::CompilerVersion
		|| Binding->SchemaVersion != EFClothingMorphV4::SurfaceBindingSchemaVersion
		|| Binding->GarmentId != CatalogRow.GarmentId
		|| Binding->GarmentCompileFingerprint != CatalogRow.BuildCompileFingerprint()
		|| Binding->SourceGarment.ToSoftObjectPath() != FSoftObjectPath(SourceMesh)
		|| Binding->BodySurface.ToSoftObjectPath() != FSoftObjectPath(BodyMesh)
		|| Binding->ReferenceBodySurface != CatalogRow.ReferenceBodySurface
		|| !Binding->FittedGarment.IsNull())
	{
		OutFailureReason = TEXT("Binding is not a V30/schema-10 multi-clothing contract for this exact Director entry.");
		return false;
	}
	if (SourceMesh->GetSkeleton() != BodyMesh->GetSkeleton()
		|| Binding->SourceSkeletonFingerprint.IsEmpty()
		|| Binding->BodySkeletonFingerprint.IsEmpty()
		|| Binding->SharedSkeletonFingerprint.IsEmpty()
		|| EFClothingSkeleton::BuildFingerprint(SourceMesh) != Binding->SourceSkeletonFingerprint
		|| EFClothingSkeleton::BuildFingerprint(BodyMesh) != Binding->BodySkeletonFingerprint
		|| EFClothingSkeleton::BuildSharedSkeletonFingerprint(SourceMesh->GetSkeleton())
			!= Binding->SharedSkeletonFingerprint)
	{
		OutFailureReason = TEXT("Clothing/body skeleton identity changed after the V4 binding was built.");
		return false;
	}
#if WITH_EDITOR
	if (EFClothingSkeleton::BuildContentFingerprint(SourceMesh) != Binding->SourceContentFingerprint
		|| EFClothingSkeleton::BuildContentFingerprint(BodyMesh) != Binding->BodyContentFingerprint)
	{
		OutFailureReason = TEXT("Clothing/body geometry changed; update this clothing's fit data. The original mesh remains visible.");
		return false;
	}
#endif

	const FEFClothingSurfaceLODPairBinding* LODPair = Binding->FindLODPair(
		GarmentLODIndex,
		BodyLODIndex);
	if (!LODPair || !LODPair->bCertified)
	{
		OutFailureReason = TEXT("The active clothing/body LOD pair has no certified V4 binding.");
		return false;
	}
	if (!EFClothingMorphV3RuntimePrivate::ValidateLiveRenderCounts(
			SourceMesh,
			LODPair->GarmentTopology,
			OutFailureReason)
		|| !EFClothingMorphV3RuntimePrivate::ValidateLiveRenderCounts(
			BodyMesh,
			LODPair->BodyTopology,
			OutFailureReason))
	{
		return false;
	}
	return true;
}

bool UEFClothingMorphV3RuntimeComponent::TryInstallSurfaceConstraint(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State,
	const FEFClothingGarmentRow& CatalogRow,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	USkeletalMesh* SourceMesh = State.SourceMesh.Get();
	USkeletalMeshComponent* BodyComponent = State.BodyComponent.Get();
	USkeletalMesh* BodyMesh = IsValid(BodyComponent) ? BodyComponent->GetSkeletalMeshAsset() : nullptr;
	const UEFClothingSurfaceBinding* Binding = State.Binding.Get();
	if (!IsValid(GarmentComponent)
		|| GarmentComponent->GetSkeletalMeshAsset() != SourceMesh
		|| !IsValid(BodyComponent)
		|| !IsValid(LoadedSurfaceDeformer))
	{
		OutFailureReason = TEXT("The exact clothing component, body component or V4 surface graph is unavailable.");
		return false;
	}

	const int32 GarmentLODIndex = FMath::Max(GarmentComponent->GetPredictedLODLevel(), 0);
	const int32 BodyLODIndex = FMath::Max(BodyComponent->GetPredictedLODLevel(), 0);
	if (!ValidateNativeBinding(
		CatalogRow,
		SourceMesh,
		BodyMesh,
		Binding,
		GarmentLODIndex,
		BodyLODIndex,
		OutFailureReason))
	{
		return false;
	}
	if (!ApplyReservedSurfaceBounds(
		GarmentComponent,
		State,
		Binding,
		State.MaximumCorrectionCm,
		OutFailureReason))
	{
		return false;
	}

	if (!EFClothingMorphV3RuntimePrivate::ResolveDynamicManager(GarmentComponent, GarmentLODIndex))
	{
		if (!State.bOwnsFallbackDeformerOverride)
		{
			if (!CaptureComponentDeformerOverride(GarmentComponent, State, OutFailureReason))
			{
				return false;
			}
			// Prefer the garment's own native Unreal deformer contract. A source
			// without one may inherit the exact Director-selected reference body's
			// generic writer only when both meshes already passed shared-skeleton
			// validation; no asset is edited and the override is restored verbatim.
			UMeshDeformer* SourceWriterDeformer = SourceMesh->GetDefaultMeshDeformer();
			const bool bInheritedReferenceWriter = !IsValid(SourceWriterDeformer);
			if (bInheritedReferenceWriter)
			{
				SourceWriterDeformer = BodyMesh->GetDefaultMeshDeformer();
			}
			if (!IsValid(SourceWriterDeformer))
			{
				OutFailureReason = TEXT("Neither the editable garment nor its Director reference body provides an Optimus source writer.");
				return false;
			}
			GarmentComponent->SetMeshDeformer(SourceWriterDeformer);
			State.FallbackDeformerAssignedByV3 = SourceWriterDeformer;
			State.bOwnsFallbackDeformerOverride = true;
			if (bInheritedReferenceWriter)
			{
				UE_LOG(
					LogEFClothingMorphV3,
					Display,
					TEXT("V4 assigned the Director reference body's generic source writer to %s; the exact component override will be restored on release."),
					*GarmentComponent->GetName());
			}
		}
		if (!EFClothingMorphV3RuntimePrivate::ResolveDynamicManager(GarmentComponent, GarmentLODIndex))
		{
			// Render-state recreation may make the new manager visible on the next
			// component tick. Keep upstream rendering visible while warming up; do
			// not classify this expected transition as a failed passthrough.
			State.RuntimeState = EEFClothingMorphV3RuntimeState::WarmingUp;
			State.NextInstallAttemptSeconds = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0)
				+ EFClothingMorphV3RuntimePrivate::TransientRetrySeconds;
			return true;
		}
	}

	ReleaseProducer(State);
	const FEFClothingSurfaceLODPairBinding* LODPair = Binding->FindLODPair(
		GarmentLODIndex,
		BodyLODIndex);
	if (!LODPair)
	{
		OutFailureReason = TEXT("The certified V4 LOD pair disappeared before producer installation.");
		return false;
	}

	UEFClothingSurfaceDeformerProducer* Producer = NewObject<UEFClothingSurfaceDeformerProducer>(this);
	if (!IsValid(Producer))
	{
		OutFailureReason = TEXT("Could not allocate the transient V4 surface producer.");
		return false;
	}
	RetainedRuntimeObjects.AddUnique(Producer);
	if (!Producer->Install(
		GarmentComponent,
		BodyComponent,
		LoadedSurfaceDeformer,
		Binding,
		*LODPair,
		OutFailureReason))
	{
		Producer->Detach();
		RetainedRuntimeObjects.Remove(Producer);
		return false;
	}

	State.Producer = Producer;
	State.GarmentLODIndex = GarmentLODIndex;
	State.BodyLODIndex = BodyLODIndex;
	State.RuntimeState = EEFClothingMorphV3RuntimeState::WarmingUp;
	State.PassthroughReason.Reset();
	State.NextInstallAttemptSeconds = 0.0;
	LastStatus = FString::Printf(
		TEXT("V4 independent surface producer installed for %s (LOD %d/%d)."),
		*GetNameSafe(GarmentComponent),
		GarmentLODIndex,
		BodyLODIndex);
	return true;
}

void UEFClothingMorphV3RuntimeComponent::TickSurfacePasses(const float DeltaTimeSeconds)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState>& Pair : ManagedGarments)
	{
		USkeletalMeshComponent* GarmentComponent = Pair.Key.Get();
		FManagedGarmentState& State = Pair.Value;
		USkeletalMeshComponent* BodyComponent = State.BodyComponent.Get();
		UEFClothingSurfaceDeformerProducer* Producer = State.Producer.Get();
		if (!IsValid(GarmentComponent)
			|| GarmentComponent->GetSkeletalMeshAsset() != State.SourceMesh.Get()
			|| !IsValid(BodyComponent))
		{
			ReleaseProducer(State);
			continue;
		}

		const int32 GarmentLODIndex = FMath::Max(GarmentComponent->GetPredictedLODLevel(), 0);
		const int32 BodyLODIndex = FMath::Max(BodyComponent->GetPredictedLODLevel(), 0);
		if (IsValid(Producer)
			&& !Producer->IsInstalledFor(
				GarmentComponent,
				BodyComponent,
				GarmentLODIndex,
				BodyLODIndex))
		{
			SetPassthrough(
				GarmentComponent,
				State,
				TEXT("LOD changed; the exact V3 pair will be rebound without hiding the source."),
				0.0);
			Producer = nullptr;
		}
		if (!IsValid(Producer))
		{
			if (Now >= State.NextInstallAttemptSeconds)
			{
				const FEFClothingGarmentRow* Row = &State.CatalogRow;
				FString InstallFailure;
				if (!Row || !TryInstallSurfaceConstraint(GarmentComponent, State, *Row, InstallFailure))
				{
					SetPassthrough(
						GarmentComponent,
						State,
						Row ? InstallFailure : TEXT("Director garment index is no longer available."),
						EFClothingMorphV3RuntimePrivate::TransientRetrySeconds);
				}
			}
			continue;
		}

		FString EnqueueFailure;
		if (!Producer->EnqueueSurfacePass(
			DeltaTimeSeconds,
			0.0f,
			ResolveClearanceCm(GarmentComponent, State),
			ResolveInflateCm(GarmentComponent, State),
			State.MaximumCorrectionCm,
			EnqueueFailure))
		{
			SetPassthrough(
				GarmentComponent,
				State,
				EnqueueFailure,
				EFClothingMorphV3RuntimePrivate::TransientRetrySeconds);
			continue;
		}
		if (Producer->HasRenderValidatedSubmission())
		{
			const bool bBecameReady =
				State.RuntimeState != EEFClothingMorphV3RuntimeState::Ready;
			State.RuntimeState = EEFClothingMorphV3RuntimeState::Ready;
			State.PassthroughReason.Reset();
			if (bBecameReady)
			{
				LastStatus = FString::Printf(
					TEXT("V4 clothing surface guard is Ready for %s (skin gap %.3f cm, surface volume %.3f cm)."),
					*GetNameSafe(GarmentComponent),
					ResolveClearanceCm(GarmentComponent, State),
					ResolveInflateCm(GarmentComponent, State));
				UE_LOG(LogEFClothingMorphV3, Display, TEXT("%s"), *LastStatus);
			}
		}
		else
		{
			State.RuntimeState = EEFClothingMorphV3RuntimeState::WarmingUp;
		}
	}
}

void UEFClothingMorphV3RuntimeComponent::SetPassthrough(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State,
	const FString& Reason,
	const double RetryDelaySeconds)
{
	const bool bReasonChanged = State.PassthroughReason != Reason;
	ReleaseProducer(State);
	ReleaseFallbackDeformerOverride(GarmentComponent, State);
	ReleaseOwnedBoundsContract(GarmentComponent, State);
	State.RuntimeState = EEFClothingMorphV3RuntimeState::Passthrough;
	State.PassthroughReason = Reason;
	State.NextInstallAttemptSeconds = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0)
		+ FMath::Max(RetryDelaySeconds, 0.0);
	LastStatus = FString::Printf(
		TEXT("V3 passthrough visible for %s: %s"),
		*GetNameSafe(GarmentComponent),
		*Reason);
	if (bReasonChanged)
	{
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingMorphV3RuntimeComponent::ReleaseProducer(FManagedGarmentState& State)
{
	if (UEFClothingSurfaceDeformerProducer* Producer = State.Producer.Get())
	{
		Producer->Detach();
		RetainedRuntimeObjects.Remove(Producer);
	}
	State.Producer.Reset();
	State.GarmentLODIndex = INDEX_NONE;
	State.BodyLODIndex = INDEX_NONE;
}

bool UEFClothingMorphV3RuntimeComponent::CaptureComponentDeformerOverride(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State,
	FString& OutFailureReason)
{
	if (State.bCapturedDeformerOverride)
	{
		return true;
	}
	bool bHadOverride = false;
	if (!EFClothingMorphV3RuntimePrivate::ReadComponentDeformerOverrideFlag(
		GarmentComponent,
		bHadOverride))
	{
		OutFailureReason = TEXT("UE component deformer override state could not be captured exactly; V3 refused to mutate it.");
		return false;
	}
	State.bCapturedDeformerOverride = true;
	State.bHadComponentDeformerOverride = bHadOverride;
	State.bAlwaysUseMeshDeformerBeforeV3 = GarmentComponent->GetAlwaysUseMeshDeformer();
	State.PreviousComponentDeformer = GarmentComponent->GetComponentMeshDeformer();
	if (UMeshDeformer* Previous = State.PreviousComponentDeformer.Get())
	{
		RetainedRuntimeObjects.AddUnique(Previous);
	}
	return true;
}

void UEFClothingMorphV3RuntimeComponent::RestoreComponentDeformerOverride(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State) const
{
	if (!State.bCapturedDeformerOverride || !State.bOwnsFallbackDeformerOverride)
	{
		return;
	}

	bool bCurrentOverride = false;
	const bool bCanReadCurrent = EFClothingMorphV3RuntimePrivate::ReadComponentDeformerOverrideFlag(
		GarmentComponent,
		bCurrentOverride);
	const bool bStillOwned = IsValid(GarmentComponent)
		&& bCanReadCurrent
		&& bCurrentOverride
		&& GarmentComponent->GetComponentMeshDeformer() == State.FallbackDeformerAssignedByV3.Get();
	if (bStillOwned)
	{
		if (State.bHadComponentDeformerOverride)
		{
			// Public UE API preserves both a real override and an explicit None.
			GarmentComponent->SetMeshDeformer(State.PreviousComponentDeformer.Get());
		}
		else
		{
			// Unset restores mesh-default selection. Preserve the otherwise
			// inaccessible dormant pointer as part of the exact component snapshot.
			UMeshDeformer* PreviousDeformer = State.PreviousComponentDeformer.Get();
			GarmentComponent->UnsetMeshDeformer();
			if (!EFClothingMorphV3RuntimePrivate::RestoreInactiveComponentDeformerPointer(
				GarmentComponent,
				PreviousDeformer))
			{
				UE_LOG(
					LogEFClothingMorphV3,
					Warning,
					TEXT("V4 restored mesh-default deformer selection for %s but could not restore its dormant component pointer."),
					*GarmentComponent->GetName());
			}
		}
		// SetMeshDeformer does not change this flag, but explicitly restoring the
		// captured value keeps the complete UE component deformer contract exact.
		GarmentComponent->SetAlwaysUseMeshDeformer(State.bAlwaysUseMeshDeformerBeforeV3);
	}
	else if (IsValid(GarmentComponent))
	{
		UE_LOG(
			LogEFClothingMorphV3,
			Warning,
			TEXT("V4 relinquished deformer restoration for %s because another system changed the component override."),
			*GarmentComponent->GetName());
	}
}

void UEFClothingMorphV3RuntimeComponent::ReleaseFallbackDeformerOverride(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State)
{
	if (!State.bCapturedDeformerOverride && !State.bOwnsFallbackDeformerOverride)
	{
		return;
	}
	RestoreComponentDeformerOverride(GarmentComponent, State);
	if (UMeshDeformer* Previous = State.PreviousComponentDeformer.Get())
	{
		RetainedRuntimeObjects.Remove(Previous);
	}
	State.PreviousComponentDeformer.Reset();
	State.FallbackDeformerAssignedByV3.Reset();
	State.bCapturedDeformerOverride = false;
	State.bHadComponentDeformerOverride = false;
	State.bAlwaysUseMeshDeformerBeforeV3 = false;
	State.bOwnsFallbackDeformerOverride = false;
}

bool UEFClothingMorphV3RuntimeComponent::ApplyReservedSurfaceBounds(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State,
	const UEFClothingSurfaceBinding* SurfaceBinding,
	const float CatalogMaximumCorrectionCm,
	FString& OutFailureReason)
{
	if (State.bOwnsBoundsContract)
	{
		return true;
	}
	USkeletalMesh* SourceMesh = State.SourceMesh.Get();
	if (!IsValid(GarmentComponent) || !IsValid(SourceMesh) || !IsValid(SurfaceBinding))
	{
		OutFailureReason = TEXT("V4 could not reserve render bounds for the exact editable clothing mesh.");
		return false;
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
	MaximumSurfaceCorrectionCm = FMath::Clamp(MaximumSurfaceCorrectionCm, 0.0f, 10.0f);

	const FBoxSphereBounds SourceBounds = SourceMesh->GetImportedBounds();
	// CollisionOnly is intentionally the stable V4 default, but the late GPU pass
	// can now transport a close garment with a body surface that expands under a
	// large morph. Reserve that bounded automatic travel up front so an otherwise
	// correct breast/body-shape correction cannot be culled by the source mesh's
	// much smaller imported bounds.
	const float ReservedOutwardTravelCm = MaximumSurfaceCorrectionCm
		+ EFClothingMorphV4::MaximumRuntimeClearanceCm
		+ EFClothingMorphV4::MaximumRuntimeInflateCm
		+ EFClothingMorphV4::MaximumAutomaticBodyShapeTravelCm;
	const float PreviousBoundsScale = FMath::IsFinite(GarmentComponent->BoundsScale)
		? GarmentComponent->BoundsScale
		: 1.0f;
	float RequiredBoundsScale = PreviousBoundsScale;
	bool bHasUsableExtent = false;
	for (const float ExtentCm : {
		SourceBounds.BoxExtent.X,
		SourceBounds.BoxExtent.Y,
		SourceBounds.BoxExtent.Z})
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
		const float RadiusCm = SourceBounds.SphereRadius;
		if (!FMath::IsFinite(RadiusCm) || RadiusCm <= UE_SMALL_NUMBER)
		{
			OutFailureReason = TEXT("The editable clothing mesh has no usable imported bounds; V4 left it in visible passthrough.");
			return false;
		}
		RequiredBoundsScale = FMath::Max(
			RequiredBoundsScale,
			(RadiusCm + ReservedOutwardTravelCm) / RadiusCm);
	}

	State.bUseBoundsFromLeaderPoseBeforeV3 = GarmentComponent->bUseBoundsFromLeaderPoseComponent;
	State.bComponentUseFixedSkelBoundsBeforeV3 = GarmentComponent->bComponentUseFixedSkelBounds;
	State.ComponentBoundsScaleBeforeV3 = PreviousBoundsScale;
	State.BoundsScaleAssignedByV3 = RequiredBoundsScale;
	State.SurfaceMaximumCorrectionCm = MaximumSurfaceCorrectionCm;
	GarmentComponent->bUseBoundsFromLeaderPoseComponent = false;
	GarmentComponent->bComponentUseFixedSkelBounds = true;
	GarmentComponent->SetBoundsScale(RequiredBoundsScale);
	GarmentComponent->UpdateBounds();
	GarmentComponent->MarkRenderTransformDirty();
	State.bOwnsBoundsContract = true;
	return true;
}

void UEFClothingMorphV3RuntimeComponent::ReleaseOwnedBoundsContract(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State)
{
	if (!State.bOwnsBoundsContract)
	{
		return;
	}

	const bool bStillOwnsExactContract = IsValid(GarmentComponent)
		&& !GarmentComponent->bUseBoundsFromLeaderPoseComponent
		&& GarmentComponent->bComponentUseFixedSkelBounds
		&& FMath::IsNearlyEqual(
			GarmentComponent->BoundsScale,
			State.BoundsScaleAssignedByV3,
			KINDA_SMALL_NUMBER);
	State.bOwnsBoundsContract = false;
	if (!bStillOwnsExactContract)
	{
		return;
	}

	GarmentComponent->bUseBoundsFromLeaderPoseComponent = State.bUseBoundsFromLeaderPoseBeforeV3;
	GarmentComponent->bComponentUseFixedSkelBounds = State.bComponentUseFixedSkelBoundsBeforeV3;
	GarmentComponent->SetBoundsScale(State.ComponentBoundsScaleBeforeV3);
	GarmentComponent->UpdateBounds();
	GarmentComponent->MarkRenderTransformDirty();
}

void UEFClothingMorphV3RuntimeComponent::ReleaseGarment(
	USkeletalMeshComponent* GarmentComponent,
	FManagedGarmentState& State)
{
	ReleaseProducer(State);
	ReleaseBodyCoverage(State);
	ReleaseFallbackDeformerOverride(GarmentComponent, State);
	ReleaseOwnedBoundsContract(GarmentComponent, State);
}

void UEFClothingMorphV3RuntimeComponent::ReleaseAllGarments()
{
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FManagedGarmentState>& Pair : ManagedGarments)
	{
		ReleaseGarment(Pair.Key.Get(), Pair.Value);
	}
	ManagedGarments.Reset();
	ClearanceOverridesCm.Reset();
	InflateOverridesCm.Reset();
	BodyMaterialCoverage.Reset();
	BodyBoneCoverage.Reset();
	BodyDeformerCoverage.Reset();
	RetainedRuntimeObjects.Reset();
}

void UEFClothingMorphV3RuntimeComponent::AcquireBodyCoverage(
	FManagedGarmentState& State,
	const FEFClothingGarmentRow& CatalogRow)
{
	USkeletalMeshComponent* BodyComponent = State.BodyComponent.Get();
	USkeletalMesh* BodyAsset = IsValid(BodyComponent) ? BodyComponent->GetSkeletalMeshAsset() : nullptr;
	if (!IsValid(BodyComponent) || !IsValid(BodyAsset))
	{
		return;
	}

	// A follower cannot own bone visibility: Unreal consumes its pose leader's
	// visibility instead. Keep the actual owner so a body swap releases the
	// same component that was modified, even when the visible mesh has changed.
	USkeletalMeshComponent* BoneOwner = BodyComponent;
	TSet<USkinnedMeshComponent*> Visited;
	while (BoneOwner && BoneOwner->LeaderPoseComponent.IsValid())
	{
		if (Visited.Contains(BoneOwner)) { BoneOwner = nullptr; break; }
		Visited.Add(BoneOwner);
		BoneOwner = Cast<USkeletalMeshComponent>(BoneOwner->LeaderPoseComponent.Get());
	}
	State.BoneCoverageComponent = BoneOwner;
	if (!CatalogRow.BodyBoneBranchesToHide.IsEmpty() && !CatalogRow.BoneCoverageDeformer.IsNull()
		&& !State.bOwnsBodyCoverageDeformer)
	{
		FBodyDeformerCoverageState* Existing = BodyDeformerCoverage.Find(BodyComponent);
		if (!Existing)
		{
			bool bOverride = false;
			const bool bKnownOverride = EFClothingMorphV3RuntimePrivate::ReadComponentDeformerOverrideFlag(BodyComponent, bOverride);
			UMeshDeformer* Active = bOverride ? BodyComponent->GetComponentMeshDeformer().Get() : BodyAsset->GetDefaultMeshDeformer();
			if (bKnownOverride && Active
				&& FSoftObjectPath(Active) == CatalogRow.BoneCoverageDeformerSource.ToSoftObjectPath())
			{
				if (UMeshDeformer* Adapter = CatalogRow.BoneCoverageDeformer.LoadSynchronous())
				{
					FBodyDeformerCoverageState NewCoverage;
					FString Error;
					if (CaptureComponentDeformerOverride(BodyComponent, NewCoverage.Snapshot, Error))
					{
						NewCoverage.Snapshot.bOwnsFallbackDeformerOverride = true;
						NewCoverage.Snapshot.FallbackDeformerAssignedByV3 = Adapter;
						BodyComponent->SetMeshDeformer(Adapter);
						Existing = &BodyDeformerCoverage.Add(BodyComponent, MoveTemp(NewCoverage));
					}
				}
			}
		}
		if (Existing) { ++Existing->RefCount; State.bOwnsBodyCoverageDeformer = true; }
	}
	for (const FName Bone : CatalogRow.BodyBoneBranchesToHide)
	{
		if (!BoneOwner || BoneOwner->GetBoneIndex(Bone) == INDEX_NONE || State.CoveredBodyBones.Contains(Bone)) { continue; }
		FBodyBoneCoverageState& Coverage = BodyBoneCoverage.FindOrAdd(BoneOwner).FindOrAdd(Bone);
		if (Coverage.RefCount == 0)
		{
			Coverage.BodyAsset = BoneOwner->GetSkeletalMeshAsset();
			Coverage.bPreviouslyHidden = BoneOwner->IsBoneHiddenByName(Bone);
			BoneOwner->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
		}
		++Coverage.RefCount;
		State.CoveredBodyBones.Add(Bone);
	}

	// Geometry exclusions belong only to the compiler/binding.  Runtime section
	// visibility is controlled exclusively by the author-facing Body Hiding
	// list, so an excluded auxiliary surface can remain visible in gameplay.
	for (const FName SlotName : CatalogRow.GetBodySectionsToHideInGameplay())
	{
		if (SlotName.IsNone())
		{
			continue;
		}
		int32 MaterialIndex = INDEX_NONE;
		const TArray<FSkeletalMaterial>& Materials = BodyAsset->GetMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			if (EFClothingMorphV3RuntimePrivate::DoesSkeletalMaterialMatchSlot(Materials[Index], SlotName))
			{
				MaterialIndex = Index;
				break;
			}
		}
		if (MaterialIndex == INDEX_NONE || State.CoveredBodyMaterialIndices.Contains(MaterialIndex))
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

void UEFClothingMorphV3RuntimeComponent::ReleaseBodyCoverage(FManagedGarmentState& State)
{
	const TWeakObjectPtr<USkeletalMeshComponent> BodyKey = State.BodyComponent;
	USkeletalMeshComponent* BodyComponent = BodyKey.Get();
	const TWeakObjectPtr<USkeletalMeshComponent> BoneKey = State.BoneCoverageComponent;
	USkeletalMeshComponent* BoneOwner = BoneKey.Get();
	if (TMap<FName, FBodyBoneCoverageState>* Bones = BodyBoneCoverage.Find(BoneKey))
	{
		for (const FName Bone : State.CoveredBodyBones)
		{
			FBodyBoneCoverageState* Coverage = Bones->Find(Bone);
			if (!Coverage || --Coverage->RefCount > 0) { continue; }
			if (IsValid(BoneOwner) && BoneOwner->GetSkeletalMeshAsset() == Coverage->BodyAsset.Get()
				&& !Coverage->bPreviouslyHidden)
			{
				BoneOwner->UnHideBoneByName(Bone);
			}
			Bones->Remove(Bone);
		}
		if (Bones->IsEmpty()) { BodyBoneCoverage.Remove(BoneKey); }
	}
	State.CoveredBodyBones.Reset();
	State.BoneCoverageComponent.Reset();
	if (State.bOwnsBodyCoverageDeformer)
	{
		if (FBodyDeformerCoverageState* Coverage = BodyDeformerCoverage.Find(BodyKey))
		{
			if (--Coverage->RefCount <= 0)
			{
				ReleaseFallbackDeformerOverride(BodyComponent, Coverage->Snapshot);
				BodyDeformerCoverage.Remove(BodyKey);
			}
		}
		State.bOwnsBodyCoverageDeformer = false;
	}
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

float UEFClothingMorphV3RuntimeComponent::ResolveClearanceCm(
	const USkeletalMeshComponent* GarmentComponent,
	const FManagedGarmentState& State) const
{
	const float* Override = ClearanceOverridesCm.Find(
		const_cast<USkeletalMeshComponent*>(GarmentComponent));
	return FMath::Clamp(
		Override ? *Override : State.DirectorClearanceCm,
		0.0f,
		EFClothingMorphV4::MaximumRuntimeClearanceCm);
}

float UEFClothingMorphV3RuntimeComponent::ResolveInflateCm(
	const USkeletalMeshComponent* GarmentComponent,
	const FManagedGarmentState& State) const
{
	const float* Override = InflateOverridesCm.Find(
		const_cast<USkeletalMeshComponent*>(GarmentComponent));
	return FMath::Clamp(
		Override ? *Override : State.DirectorInflateCm,
		0.0f,
		EFClothingMorphV4::MaximumRuntimeInflateCm);
}
