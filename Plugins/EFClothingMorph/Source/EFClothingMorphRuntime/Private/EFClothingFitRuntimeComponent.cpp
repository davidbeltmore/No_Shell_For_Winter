#include "EFClothingFitRuntimeComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFClothingFitProfile.h"
#include "EFClothingMorphV2Settings.h"
#include "EFClothingSkeletonFingerprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

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
	constexpr double SkinProfileTimeoutSeconds = 8.0;
}

UEFClothingFitRuntimeComponent::UEFClothingFitRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.TickInterval = 0.02f;
}

void UEFClothingFitRuntimeComponent::BeginPlay()
{
	Super::BeginPlay();

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	RuntimeClearanceMultiplier = Settings ? Settings->ClearanceMultiplier : 1.0f;
	if (Settings && !Settings->Registry.IsNull())
	{
		LoadedRegistry = Settings->Registry.LoadSynchronous();
	}

	ResolveCustomizationComponent();
	bLastRuntimeEnabled = Settings && Settings->bEnabled && CVarEFClothingMorphV2Enabled.GetValueOnGameThread() != 0;
	NextReconcileAtSeconds = 0.0;
	NextMorphSyncAtSeconds = 0.0;
	LastStatus = LoadedRegistry ? TEXT("Registry loaded; waiting for garments") : TEXT("Registry missing or not cooked") ;
}

void UEFClothingFitRuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreAllGarments();
	if (CustomizationComponent && MorphStateAppliedHandle.IsValid())
	{
		CustomizationComponent->OnMorphStateApplied().Remove(MorphStateAppliedHandle);
		MorphStateAppliedHandle.Reset();
	}
	CustomizationComponent = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UEFClothingFitRuntimeComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextReconcileAtSeconds)
	{
		ReconcileGarments();
		NextReconcileAtSeconds = Now + FMath::Max(Settings->ReconcileIntervalSeconds, 0.02f);
	}

	if (Now >= NextMorphSyncAtSeconds)
	{
		SynchronizeMorphs();
		NextMorphSyncAtSeconds = Now + FMath::Max(Settings->MorphSyncIntervalSeconds, 0.01f);
	}
}

void UEFClothingFitRuntimeComponent::ForceReconcile()
{
	ReconcileGarments();
	SynchronizeMorphs();
}

int32 UEFClothingFitRuntimeComponent::GetAppliedGarmentCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		if (Pair.Key.IsValid() && Pair.Value.FittedMesh.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

FString UEFClothingFitRuntimeComponent::GetDebugSummary() const
{
	return FString::Printf(
		TEXT("Owner=%s | Registry=%s | Applied=%d | Clearance=%.3f | Status=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		LoadedRegistry ? *LoadedRegistry->GetPathName() : TEXT("None"),
		GetAppliedGarmentCount(),
		RuntimeClearanceMultiplier,
		*LastStatus);
}

void UEFClothingFitRuntimeComponent::SetRuntimeClearanceMultiplier(float NewMultiplier)
{
	RuntimeClearanceMultiplier = FMath::Clamp(NewMultiplier, 0.0f, 2.0f);
	SynchronizeMorphs();
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
	}
}

void UEFClothingFitRuntimeComponent::HandleMorphStateApplied()
{
	ReconcileGarments();
	SynchronizeMorphs();
}

USkeletalMeshComponent* UEFClothingFitRuntimeComponent::ResolveBodyMesh(const UEFClothingFitProfile* Profile) const
{
	if (!IsValid(Profile) || !GetOwner())
	{
		return nullptr;
	}

	const FSoftObjectPath ExpectedBodyPath = Profile->BodySurface.ToSoftObjectPath();
	TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(GetOwner());
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (IsValid(MeshComponent) && IsValid(MeshComponent->GetSkeletalMeshAsset())
			&& FSoftObjectPath(MeshComponent->GetSkeletalMeshAsset()) == ExpectedBodyPath)
		{
			return MeshComponent;
		}
	}

	if (CustomizationComponent)
	{
		USkeletalMeshComponent* BodyComponent = CustomizationComponent->GetBodyMeshComponent();
		if (IsValid(BodyComponent) && IsValid(BodyComponent->GetSkeletalMeshAsset())
			&& FSoftObjectPath(BodyComponent->GetSkeletalMeshAsset()) == ExpectedBodyPath)
		{
			return BodyComponent;
		}
	}

	return nullptr;
}

void UEFClothingFitRuntimeComponent::ReconcileGarments()
{
	RemoveStaleStates();
	if (!LoadedRegistry || !GetOwner())
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

		if (FAppliedGarmentState* ExistingState = AppliedGarments.Find(MeshComponent))
		{
			if (MeshComponent->GetSkeletalMeshAsset() == ExistingState->FittedMesh.Get())
			{
				if (ExistingState->bWaitingForSkinProfile && !MeshComponent->IsSkinWeightProfilePending())
				{
					ExistingState->bWaitingForSkinProfile = false;
					MeshComponent->ComponentTags.Remove(PendingTag);
					MeshComponent->SetVisibility(ExistingState->bWasVisible, false);
					LastStatus = FString::Printf(TEXT("Applied and ready: %s"), *MeshComponent->GetName());
					UE_LOG(LogEFClothingMorphV2, Display, TEXT("EFClothingMorphV2 READY owner=%s component=%s fitted=%s"),
						*GetOwner()->GetName(), *MeshComponent->GetName(), *GetNameSafe(ExistingState->FittedMesh.Get()));
				}
				else if (ExistingState->bWaitingForSkinProfile && GetWorld()
					&& GetWorld()->GetTimeSeconds() - ExistingState->ApplyStartedAtSeconds > SkinProfileTimeoutSeconds)
				{
					LastStatus = FString::Printf(TEXT("Skin profile timeout on %s; restored source"), *MeshComponent->GetName());
					UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
					RestoreGarment(MeshComponent, *ExistingState, true);
					AppliedGarments.Remove(MeshComponent);
				}
				continue;
			}

			if (CustomizationComponent)
			{
				CustomizationComponent->UnregisterExternalMorphWriter(MeshComponent, this);
			}
			MeshComponent->ComponentTags.Remove(ManagedTag);
			MeshComponent->ComponentTags.Remove(PendingTag);
			AppliedGarments.Remove(MeshComponent);
		}

		USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset();
		if (!IsValid(CurrentMesh))
		{
			continue;
		}

		if (const UEFClothingFitProfile* Profile = LoadedRegistry->FindProfileForSource(CurrentMesh))
		{
			TryApplyProfile(MeshComponent, Profile);
		}
	}
}

bool UEFClothingFitRuntimeComponent::TryApplyProfile(
	USkeletalMeshComponent* GarmentComponent,
	const UEFClothingFitProfile* Profile)
{
	if (!IsValid(GarmentComponent) || !IsValid(Profile) || AppliedGarments.Contains(GarmentComponent))
	{
		return false;
	}

	USkeletalMeshComponent* BodyComponent = ResolveBodyMesh(Profile);
	USkeletalMesh* FittedMesh = Profile->FittedGarment.LoadSynchronous();
	FString FailureReason;
	if (!ValidateProfileForComponents(Profile, GarmentComponent, BodyComponent, FittedMesh, FailureReason))
	{
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
	State.bWasVisible = GarmentComponent->IsVisible();
	State.PreviousBoundsScale = GarmentComponent->BoundsScale;
	State.ApplyStartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	GarmentComponent->SetVisibility(false, false);
	GarmentComponent->ComponentTags.AddUnique(PendingTag);
	GarmentComponent->SetSkeletalMesh(FittedMesh, true);
	GarmentComponent->BoundsScale = FMath::Max(GarmentComponent->BoundsScale, 1.10f);

	if (!GarmentComponent->SetSkinWeightProfile(Profile->SkinWeightProfileName, ESkinWeightProfileLayer::Primary))
	{
		GarmentComponent->SetSkeletalMesh(State.SourceMesh.Get(), true);
		GarmentComponent->BoundsScale = State.PreviousBoundsScale;
		GarmentComponent->SetVisibility(State.bWasVisible, false);
		GarmentComponent->ComponentTags.Remove(PendingTag);
		LastStatus = FString::Printf(TEXT("Failed to activate skin profile %s"), *Profile->SkinWeightProfileName.ToString());
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("%s"), *LastStatus);
		return false;
	}

	const float ClearanceValue = Profile->DefaultClearanceValue * RuntimeClearanceMultiplier;
	GarmentComponent->SetMorphTarget(Profile->ClearanceMorphName, ClearanceValue, false);
	State.LastWrittenMorphValues.Add(Profile->ClearanceMorphName, ClearanceValue);
	State.bWaitingForSkinProfile = GarmentComponent->IsSkinWeightProfilePending();
	GarmentComponent->ComponentTags.AddUnique(ManagedTag);

	if (CustomizationComponent && !CustomizationComponent->RegisterExternalMorphWriter(GarmentComponent, this))
	{
		UE_LOG(LogEFClothingMorphV2, Error, TEXT("EFClothingMorphV2 could not acquire morph ownership for %s."), *GarmentComponent->GetName());
		RestoreGarment(GarmentComponent, State, true);
		return false;
	}

	AppliedGarments.Add(GarmentComponent, MoveTemp(State));
	if (!GarmentComponent->IsSkinWeightProfilePending())
	{
		FAppliedGarmentState& AddedState = AppliedGarments.FindChecked(GarmentComponent);
		AddedState.bWaitingForSkinProfile = false;
		GarmentComponent->ComponentTags.Remove(PendingTag);
		GarmentComponent->SetVisibility(AddedState.bWasVisible, false);
	}

	LastStatus = FString::Printf(TEXT("Applied %s -> %s"), *GetNameSafe(GarmentComponent->GetSkeletalMeshAsset()), *GetNameSafe(FittedMesh));
	UE_LOG(LogEFClothingMorphV2, Display,
		TEXT("EFClothingMorphV2 APPLY owner=%s component=%s source=%s fitted=%s profile=%s build=%s pending=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
		*GarmentComponent->GetName(),
		*GetNameSafe(AppliedGarments.FindChecked(GarmentComponent).SourceMesh.Get()),
		*GetNameSafe(FittedMesh),
		*Profile->SkinWeightProfileName.ToString(),
		*Profile->BuildGuid.ToString(EGuidFormats::DigitsWithHyphens),
		GarmentComponent->IsSkinWeightProfilePending() ? TEXT("true") : TEXT("false"));

	SynchronizeMorphs();
	return true;
}

bool UEFClothingFitRuntimeComponent::ValidateProfileForComponents(
	const UEFClothingFitProfile* Profile,
	USkeletalMeshComponent* GarmentComponent,
	USkeletalMeshComponent* BodyComponent,
	USkeletalMesh* FittedMesh,
	FString& OutFailureReason) const
{
	if (!IsValid(Profile) || !IsValid(GarmentComponent) || !IsValid(BodyComponent) || !IsValid(FittedMesh))
	{
		OutFailureReason = TEXT("Profile, fitted garment or exact body surface is unavailable.");
		return false;
	}

	USkeletalMesh* SourceMesh = GarmentComponent->GetSkeletalMeshAsset();
	USkeletalMesh* BodyMesh = BodyComponent->GetSkeletalMeshAsset();
	if (!IsValid(SourceMesh) || !IsValid(BodyMesh))
	{
		OutFailureReason = TEXT("Source garment or body mesh is invalid.");
		return false;
	}

	if (SourceMesh->GetSkeleton() != BodyMesh->GetSkeleton()
		|| SourceMesh->GetSkeleton() != FittedMesh->GetSkeleton())
	{
		OutFailureReason = TEXT("USkeleton object mismatch; fitting aborted fail-closed.");
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

	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (Settings && Settings->bRequireStrictReferenceSkeleton)
	{
		if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(SourceMesh, FittedMesh, &OutFailureReason))
		{
			return false;
		}
		if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(FittedMesh, BodyMesh, &OutFailureReason))
		{
			return false;
		}

		if (USkinnedMeshComponent* Leader = GarmentComponent->LeaderPoseComponent.Get())
		{
			if (USkeletalMeshComponent* SkeletalLeader = Cast<USkeletalMeshComponent>(Leader))
			{
				if (!EFClothingSkeleton::AreReferenceSkeletonsStrictlyCompatible(
					FittedMesh,
					SkeletalLeader->GetSkeletalMeshAsset(),
					&OutFailureReason))
				{
					return false;
				}
			}
		}
	}

	return true;
}

float UEFClothingFitRuntimeComponent::ResolveBodyMorphValue(
	USkeletalMeshComponent* BodyComponent,
	USkeletalMeshComponent* GarmentComponent,
	FName MorphName) const
{
	if (!IsValid(BodyComponent) || MorphName.IsNone())
	{
		return 0.0f;
	}

	float Value = BodyComponent->GetMorphTarget(MorphName);
	if (FMath::IsNearlyZero(Value))
	{
		if (const UAnimInstance* BodyAnim = BodyComponent->GetAnimInstance())
		{
			Value = BodyAnim->GetCurveValue(MorphName);
		}
	}

	if (FMath::IsNearlyZero(Value) && IsValid(GarmentComponent))
	{
		if (const USkeletalMeshComponent* Leader = Cast<USkeletalMeshComponent>(GarmentComponent->LeaderPoseComponent.Get()))
		{
			if (const UAnimInstance* LeaderAnim = Leader->GetAnimInstance())
			{
				Value = LeaderAnim->GetCurveValue(MorphName);
			}
		}
	}

	return Value;
}

void UEFClothingFitRuntimeComponent::SynchronizeMorphs()
{
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const float Epsilon = Settings ? Settings->MorphWriteEpsilon : 0.0005f;

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

		const float ClearanceValue = Profile->DefaultClearanceValue * RuntimeClearanceMultiplier;
		const float PreviousClearance = State.LastWrittenMorphValues.FindRef(Profile->ClearanceMorphName);
		if (FMath::Abs(ClearanceValue - PreviousClearance) > Epsilon)
		{
			GarmentComponent->SetMorphTarget(Profile->ClearanceMorphName, ClearanceValue, FMath::IsNearlyZero(ClearanceValue));
			State.LastWrittenMorphValues.Add(Profile->ClearanceMorphName, ClearanceValue);
		}

		for (const FEFClothingMorphBinding& Binding : Profile->MorphBindings)
		{
			if (Binding.BodyMorph.IsNone() || Binding.GarmentMorph.IsNone())
			{
				continue;
			}

			const float Value = ResolveBodyMorphValue(BodyComponent, GarmentComponent, Binding.BodyMorph) * Binding.Scale + Binding.Bias;
			const float* PreviousValue = State.LastWrittenMorphValues.Find(Binding.GarmentMorph);
			if (!PreviousValue || FMath::Abs(Value - *PreviousValue) > Epsilon)
			{
				GarmentComponent->SetMorphTarget(Binding.GarmentMorph, Value, FMath::IsNearlyZero(Value));
				State.LastWrittenMorphValues.Add(Binding.GarmentMorph, Value);
			}
		}
	}
}

void UEFClothingFitRuntimeComponent::RemoveStaleStates()
{
	for (auto It = AppliedGarments.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* GarmentComponent = It.Key().Get();
		FAppliedGarmentState& State = It.Value();
		if (!IsValid(GarmentComponent))
		{
			It.RemoveCurrent();
			continue;
		}

		if (GarmentComponent->GetSkeletalMeshAsset() != State.FittedMesh.Get())
		{
			if (CustomizationComponent)
			{
				CustomizationComponent->UnregisterExternalMorphWriter(GarmentComponent, this);
			}
			GarmentComponent->ComponentTags.Remove(ManagedTag);
			GarmentComponent->ComponentTags.Remove(PendingTag);
			GarmentComponent->BoundsScale = State.PreviousBoundsScale;
			It.RemoveCurrent();
		}
	}
}

void UEFClothingFitRuntimeComponent::RestoreGarment(
	USkeletalMeshComponent* GarmentComponent,
	FAppliedGarmentState& State,
	bool bRestoreSourceMesh)
{
	if (!IsValid(GarmentComponent))
	{
		return;
	}

	if (CustomizationComponent)
	{
		CustomizationComponent->UnregisterExternalMorphWriter(GarmentComponent, this);
	}

	if (const UEFClothingFitProfile* Profile = State.Profile.Get())
	{
		GarmentComponent->SetMorphTarget(Profile->ClearanceMorphName, 0.0f, true);
	}
	GarmentComponent->ClearSkinWeightProfile(ESkinWeightProfileLayer::Primary);
	if (bRestoreSourceMesh && GarmentComponent->GetSkeletalMeshAsset() == State.FittedMesh.Get())
	{
		GarmentComponent->SetSkeletalMesh(State.SourceMesh.Get(), true);
	}
	GarmentComponent->BoundsScale = State.PreviousBoundsScale;
	GarmentComponent->ComponentTags.Remove(ManagedTag);
	GarmentComponent->ComponentTags.Remove(PendingTag);
	GarmentComponent->SetVisibility(State.bWasVisible, false);
}

void UEFClothingFitRuntimeComponent::RestoreAllGarments()
{
	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, FAppliedGarmentState>& Pair : AppliedGarments)
	{
		RestoreGarment(Pair.Key.Get(), Pair.Value, true);
	}
	AppliedGarments.Reset();
}
