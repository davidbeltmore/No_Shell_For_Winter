#include "EFClothingMorphV3RuntimeComponent.h"

#include "Animation/MeshDeformer.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFClothingFitProfile.h"
#include "EFClothingGarmentCatalog.h"
#include "EFClothingMorphDirectorPolicy.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "EFClothingSurfaceBinding.h"
#include "EFClothingSurfaceDeformerProducer.h"
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

	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("ef.ClothingMorph.V4.Enabled"),
		1,
		TEXT("Enables the independent multi-clothing EF Clothing Morph V4 runtime. 0 leaves every source mesh untouched and visible."),
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
	bAssetsReady = false;
	bAssetLoadFailed = false;
	NextReconcileSeconds = 0.0;
	LastStatus = TEXT("Loading V4 multi-clothing runtime assets; source clothes remain visible.");
	StartAssetLoad();
}

void UEFClothingMorphV3RuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StartupLoadHandle.IsValid())
	{
		StartupLoadHandle->CancelHandle();
		StartupLoadHandle.Reset();
	}
	ReleaseAllGarments();
	LoadedRegistry = nullptr;
	LoadedDirector = nullptr;
	LoadedSurfaceDeformer = nullptr;
	bAssetsReady = false;
	Super::EndPlay(EndPlayReason);
}

void UEFClothingMorphV3RuntimeComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const bool bRuntimeEnabled = Settings
		&& Settings->bEnabled
		&& EFClothingMorphV3RuntimePrivate::CVarEnabled.GetValueOnGameThread() != 0
		&& EFClothingMorphV3RuntimePrivate::CVarLegacyV3Enabled.GetValueOnGameThread() != 0;
	if (!bRuntimeEnabled)
	{
		if (!ManagedGarments.IsEmpty())
		{
			ReleaseAllGarments();
		}
		return;
	}
	if (!bAssetsReady)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now >= NextReconcileSeconds)
	{
		ReconcileGarments();
		NextReconcileSeconds = Now + FMath::Max(
			static_cast<double>(Settings->ReconcileIntervalSeconds),
			0.02);
	}
	TickSurfacePasses(DeltaTime);
}

void UEFClothingMorphV3RuntimeComponent::ForceReconcile()
{
	NextReconcileSeconds = 0.0;
	if (bAssetsReady)
	{
		ReconcileGarments();
		TickSurfacePasses(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
	}
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
		GarmentSummaries.Add(FString::Printf(
			TEXT("%s:%s[gLOD=%d,bLOD=%d,clear=%.3fcm,inflate=%.3fcm]%s"),
			*GetNameSafe(Pair.Key.Get()),
			EFClothingMorphV3RuntimePrivate::StateToString(State.RuntimeState),
			State.GarmentLODIndex,
			State.BodyLODIndex,
			ResolveClearanceCm(Pair.Key.Get(), State),
			ResolveInflateCm(Pair.Key.Get(), State),
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
		TEXT("EFClothingMorphV4 state=%s managed=%d ready=%d warming=%d passthrough=%d issues=%d | %s | %s | %s"),
		*SystemState,
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
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings || !Settings->bEnabled)
	{
		LastStatus = TEXT("V4 disabled by project settings; source clothes remain visible.");
		return;
	}

	TArray<FSoftObjectPath> AssetsToLoad;
	if (!Settings->Registry.IsNull())
	{
		AssetsToLoad.AddUnique(Settings->Registry.ToSoftObjectPath());
	}
	if (!Settings->DirectorPolicy.IsNull())
	{
		AssetsToLoad.AddUnique(Settings->DirectorPolicy.ToSoftObjectPath());
	}
	if (!Settings->SurfaceConstraintDeformer.IsNull())
	{
		AssetsToLoad.AddUnique(Settings->SurfaceConstraintDeformer.ToSoftObjectPath());
	}
	if (AssetsToLoad.Num() != 3)
	{
		bAssetLoadFailed = true;
		LastStatus = TEXT("V4 registry, Director or surface graph is not configured; source clothes remain visible.");
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
		LastStatus = TEXT("V4 asynchronous asset request could not start; source clothes remain visible.");
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
	}
}

void UEFClothingMorphV3RuntimeComponent::HandleAssetsReady()
{
	StartupLoadHandle.Reset();
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	LoadedRegistry = Settings ? Settings->Registry.Get() : nullptr;
	LoadedDirector = Settings ? Settings->DirectorPolicy.Get() : nullptr;
	LoadedSurfaceDeformer = Settings ? Settings->SurfaceConstraintDeformer.Get() : nullptr;

	FString DirectorError;
	if (!IsValid(LoadedRegistry)
		|| !IsValid(LoadedDirector)
		|| !IsValid(LoadedSurfaceDeformer)
		|| !LoadedDirector->ValidateIdentity(DirectorError))
	{
		bAssetLoadFailed = true;
		bAssetsReady = false;
		LastStatus = FString::Printf(
			TEXT("V4 startup validation failed; source clothes remain visible. %s"),
			*DirectorError);
		UE_LOG(LogEFClothingMorphV3, Warning, TEXT("%s"), *LastStatus);
		return;
	}

	bAssetLoadFailed = false;
	bAssetsReady = true;
	LastStatus = TEXT("V4 multi-clothing assets loaded; each clothing entry will be resolved independently.");
	ForceReconcile();
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
	TSet<TWeakObjectPtr<USkeletalMeshComponent>> ObservedGarments;
	TSet<FName> ObservedClothingIds;
	TSet<FString> ObservedSourceBodyPairs;
	ClothingRowIssues.Reset();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	for (int32 ClothingIndex = 0; ClothingIndex < LoadedDirector->Garments.Num(); ++ClothingIndex)
	{
		const FEFClothingGarmentRow& CatalogRow = LoadedDirector->Garments[ClothingIndex];
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

		const UEFClothingSurfaceBinding* Binding = LoadedRegistry->FindNativeSourceBinding(
			CatalogRow.GarmentId,
			SourceMesh,
			BodyMesh);
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

			FManagedGarmentState* Existing = ManagedGarments.Find(GarmentComponent);
			const FString CompileFingerprint = CatalogRow.BuildCompileFingerprint();
			const bool bIdentityChanged = Existing
				&& (Existing->SourceMesh.Get() != SourceMesh
					|| Existing->BodyComponent.Get() != BodyComponent
					|| Existing->Binding.Get() != Binding
					|| Existing->GarmentId != CatalogRow.GarmentId
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
				NewState.Binding = Binding;
				NewState.GarmentId = CatalogRow.GarmentId;
				NewState.CompileFingerprint = CompileFingerprint;
				NewState.RuntimeState = EEFClothingMorphV3RuntimeState::Loading;
				Existing = &ManagedGarments.Add(GarmentComponent, MoveTemp(NewState));
				AcquireBodyCoverage(*Existing, CatalogRow);
			}

			Existing->DirectorClearanceCm = FMath::Clamp(
				FMath::IsFinite(CatalogRow.AdditionalClearanceCm)
					? CatalogRow.AdditionalClearanceCm
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
				SetPassthrough(
					GarmentComponent,
					*Existing,
					TEXT("No V4 binding is published for this clothing name/source/body combination."),
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
		|| !Binding->FittedGarment.IsNull())
	{
		OutFailureReason = TEXT("Binding is not a V28/schema-8 multi-clothing contract for this exact Director entry.");
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
				const FEFClothingGarmentRow* Row = LoadedDirector
					? LoadedDirector->FindGarmentById(State.GarmentId)
					: nullptr;
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
	const float ReservedOutwardTravelCm = MaximumSurfaceCorrectionCm
		+ EFClothingMorphV4::MaximumRuntimeClearanceCm
		+ EFClothingMorphV4::MaximumRuntimeInflateCm;
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
