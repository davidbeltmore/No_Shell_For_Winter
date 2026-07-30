#include "EFBlinkMorphComponent.h"

#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "EFBlinkRuntime.h"
#include "EFBlinkSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace EFBlinkMorphPrivate
{
	static FString MakeCombinedMeshLabel(const USkeletalMeshComponent* MeshComponent)
	{
		if (!MeshComponent)
		{
			return FString();
		}

		const USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset();
		return FString::Printf(TEXT("%s %s %s"),
			*MeshComponent->GetName(),
			*GetNameSafe(MeshAsset),
			MeshAsset ? *MeshAsset->GetPathName() : TEXT(""));
	}

	static bool ContainsToken(const FString& Source, const FString& Token)
	{
		return !Token.IsEmpty() && Source.Contains(Token, ESearchCase::IgnoreCase);
	}
}

UEFBlinkMorphComponent::UEFBlinkMorphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	MorphTargets = EFBlinkDefaults::MakeMorphTargets();
	PreferredMeshNameTokens = EFBlinkDefaults::MakePreferredMeshNameTokens();
}

void UEFBlinkMorphComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(false);
	RefreshTargetMesh();

	if (bStartEnabled)
	{
		StartBlink();
	}
}

void UEFBlinkMorphComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBlink(true);
	Super::EndPlay(EndPlayReason);
}

void UEFBlinkMorphComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bPulseActive)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (bRevalidateOnMeshChange && ShouldRevalidateResolvedMesh())
	{
		const bool bCanContinue = RefreshTargetMesh();
		if (!bCanContinue && bRequireAllMorphTargets)
		{
			bPulseActive = false;
			SetComponentTickEnabled(false);
			return;
		}
	}

	const float Duration = FMath::Max(PulseDurationSeconds, 0.02f);
	PulseElapsedSeconds += DeltaTime;

	const float NormalizedTime = FMath::Clamp(PulseElapsedSeconds / Duration, 0.0f, 1.0f);
	ApplyMorphAlpha(EvaluatePulseAlpha(NormalizedTime));

	if (PulseElapsedSeconds >= Duration)
	{
		ApplyMorphsAtRest();
		bPulseActive = false;
		SetComponentTickEnabled(false);
	}
}

void UEFBlinkMorphComponent::ApplySettingsFromDefaults()
{
	const UEFBlinkSettings* Settings = GetDefault<UEFBlinkSettings>();
	if (!Settings)
	{
		return;
	}

	bStartEnabled = Settings->bStartEnabled;
	MorphTargets = Settings->MorphTargets.Num() > 0 ? Settings->MorphTargets : EFBlinkDefaults::MakeMorphTargets();
	PulseIntervalSeconds = Settings->PulseIntervalSeconds;
	PulseDurationSeconds = Settings->PulseDurationSeconds;
	InitialDelaySeconds = Settings->InitialDelaySeconds;
	PreferredMeshNameTokens = Settings->PreferredMeshNameTokens.Num() > 0 ? Settings->PreferredMeshNameTokens : EFBlinkDefaults::MakePreferredMeshNameTokens();
	bRequireAllMorphTargets = Settings->bRequireAllMorphTargets;
	bLogValidation = Settings->bLogValidation;
}

void UEFBlinkMorphComponent::SetTargetMeshComponent(USkeletalMeshComponent* NewTargetMeshComponent)
{
	TargetMeshComponent = NewTargetMeshComponent;
	bLoggedMissingMorphs = false;
	bLoggedNoTargetMesh = false;
	RefreshTargetMesh();
}

bool UEFBlinkMorphComponent::RefreshTargetMesh()
{
	USkeletalMeshComponent* NewTargetMesh = nullptr;

	if (IsValid(TargetMeshComponent.Get()))
	{
		NewTargetMesh = TargetMeshComponent.Get();
	}

	if (!NewTargetMesh && !TargetMeshComponentName.IsNone())
	{
		NewTargetMesh = FindMeshComponentByName(TargetMeshComponentName);
	}

	if (!NewTargetMesh && bAutoResolveTargetMesh)
	{
		NewTargetMesh = ResolveBestTargetMesh();
	}

	const USkeletalMesh* NewMeshAsset = IsValid(NewTargetMesh) ? NewTargetMesh->GetSkeletalMeshAsset() : nullptr;
	const bool bTargetChanged = NewTargetMesh != ResolvedTargetMesh.Get() || NewMeshAsset != CachedResolvedMeshAsset.Get();
	if (bTargetChanged)
	{
		bLoggedMissingMorphs = false;
		bLoggedNoTargetMesh = false;
	}

	ResolvedTargetMesh = NewTargetMesh;
	CachedResolvedMeshAsset = const_cast<USkeletalMesh*>(NewMeshAsset);
	UpdateDebugState();

	LastMissingMorphTargets.Reset();
	bLastValidationPassed = ValidateMorphTargets(LastMissingMorphTargets);

	if (!bLastValidationPassed)
	{
		LogValidationFailureIfNeeded(LastMissingMorphTargets);
	}

	return IsValid(ResolvedTargetMesh.Get()) && (bLastValidationPassed || !bRequireAllMorphTargets);
}

bool UEFBlinkMorphComponent::ValidateMorphTargets(TArray<FName>& OutMissingMorphTargets) const
{
	OutMissingMorphTargets.Reset();

	const USkeletalMeshComponent* MeshComponent = ResolvedTargetMesh.Get();
	const USkeletalMesh* MeshAsset = IsValid(MeshComponent) ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset)
	{
		for (const FEFBlinkMorphTarget& MorphTarget : MorphTargets)
		{
			if (!MorphTarget.MorphName.IsNone())
			{
				OutMissingMorphTargets.Add(MorphTarget.MorphName);
			}
		}
		return false;
	}

	for (const FEFBlinkMorphTarget& MorphTarget : MorphTargets)
	{
		if (!MorphTarget.MorphName.IsNone() && !HasMorphTarget(MeshComponent, MorphTarget.MorphName))
		{
			OutMissingMorphTargets.Add(MorphTarget.MorphName);
		}
	}

	return OutMissingMorphTargets.Num() == 0;
}

void UEFBlinkMorphComponent::StartBlink()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	RefreshTargetMesh();

	const float Interval = FMath::Max(PulseIntervalSeconds, 0.1f);
	const float InitialDelay = FMath::Max(InitialDelaySeconds, 0.0f);
	World->GetTimerManager().ClearTimer(PulseTimerHandle);
	World->GetTimerManager().SetTimer(PulseTimerHandle, this, &ThisClass::TriggerPulseNow, Interval, true, InitialDelay);
}

void UEFBlinkMorphComponent::StopBlink(const bool bResetMorphs)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PulseTimerHandle);
	}

	if (bResetMorphs)
	{
		ApplyMorphsAtRest();
	}

	bPulseActive = false;
	SetComponentTickEnabled(false);
}

void UEFBlinkMorphComponent::TriggerPulseNow()
{
	if (bPulseActive)
	{
		return;
	}

	const bool bCanPulse = RefreshTargetMesh();
	if (!bCanPulse && bRequireAllMorphTargets)
	{
		return;
	}

	if (!IsValid(ResolvedTargetMesh.Get()))
	{
		return;
	}

	PulseElapsedSeconds = 0.0f;
	bPulseActive = true;
	ApplyMorphAlpha(0.0f);
	SetComponentTickEnabled(true);
}

bool UEFBlinkMorphComponent::IsReady() const
{
	return IsValid(ResolvedTargetMesh.Get()) && bLastValidationPassed;
}

bool UEFBlinkMorphComponent::IsBlinkRunning() const
{
	if (bPulseActive)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(PulseTimerHandle);
}

USkeletalMeshComponent* UEFBlinkMorphComponent::GetResolvedTargetMesh() const
{
	return ResolvedTargetMesh.Get();
}

USkeletalMeshComponent* UEFBlinkMorphComponent::FindMeshComponentByName(const FName ComponentName) const
{
	if (ComponentName.IsNone() || !GetOwner())
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	GetOwner()->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		if (MeshComponent->GetFName() == ComponentName || MeshComponent->GetName().Equals(ComponentName.ToString(), ESearchCase::IgnoreCase))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

USkeletalMeshComponent* UEFBlinkMorphComponent::ResolveBestTargetMesh() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);

	USkeletalMeshComponent* BestMeshComponent = nullptr;
	int32 BestScore = INDEX_NONE;
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const int32 Score = ScoreMeshComponent(MeshComponent);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestMeshComponent = MeshComponent;
		}
	}

	return BestMeshComponent;
}

int32 UEFBlinkMorphComponent::ScoreMeshComponent(const USkeletalMeshComponent* MeshComponent) const
{
	if (!IsValid(MeshComponent) || !MeshComponent->GetSkeletalMeshAsset())
	{
		return INDEX_NONE;
	}

	const int32 PresentMorphCount = CountPresentMorphTargets(MeshComponent);
	int32 Score = PresentMorphCount * 1000;
	if (PresentMorphCount == MorphTargets.Num() && PresentMorphCount > 0)
	{
		Score += 5000;
	}

	if (MeshComponent->IsVisible())
	{
		Score += 100;
	}

	if (MeshComponent->IsRegistered())
	{
		Score += 25;
	}

	const FString CombinedLabel = EFBlinkMorphPrivate::MakeCombinedMeshLabel(MeshComponent);
	for (const FString& PreferredToken : PreferredMeshNameTokens)
	{
		if (EFBlinkMorphPrivate::ContainsToken(CombinedLabel, PreferredToken))
		{
			Score += 100;
		}
	}

	if (EFBlinkMorphPrivate::ContainsToken(CombinedLabel, TEXT("/DazToUnreal/Female/"))
		|| EFBlinkMorphPrivate::ContainsToken(CombinedLabel, TEXT("Female.Female")))
	{
		Score += 500;
	}

	for (const FName ComponentTag : MeshComponent->ComponentTags)
	{
		if (EFBlinkMorphPrivate::ContainsToken(ComponentTag.ToString(), TEXT("Female")))
		{
			Score += 150;
		}
	}

	return Score;
}

int32 UEFBlinkMorphComponent::CountPresentMorphTargets(const USkeletalMeshComponent* MeshComponent) const
{
	int32 PresentCount = 0;
	for (const FEFBlinkMorphTarget& MorphTarget : MorphTargets)
	{
		if (!MorphTarget.MorphName.IsNone() && HasMorphTarget(MeshComponent, MorphTarget.MorphName))
		{
			++PresentCount;
		}
	}
	return PresentCount;
}

bool UEFBlinkMorphComponent::HasMorphTarget(const USkeletalMeshComponent* MeshComponent, const FName MorphName) const
{
	const USkeletalMesh* MeshAsset = IsValid(MeshComponent) ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	return !MorphName.IsNone() && MeshAsset && MeshAsset->FindMorphTarget(MorphName) != nullptr;
}

bool UEFBlinkMorphComponent::ShouldRevalidateResolvedMesh() const
{
	const USkeletalMeshComponent* MeshComponent = ResolvedTargetMesh.Get();
	if (!IsValid(MeshComponent))
	{
		return true;
	}

	return MeshComponent->GetSkeletalMeshAsset() != CachedResolvedMeshAsset.Get();
}

float UEFBlinkMorphComponent::EvaluatePulseAlpha(const float NormalizedTime) const
{
	const float Triangle = NormalizedTime <= 0.5f
		? NormalizedTime * 2.0f
		: (1.0f - NormalizedTime) * 2.0f;
	const float ClampedTriangle = FMath::Clamp(Triangle, 0.0f, 1.0f);
	return ClampedTriangle * ClampedTriangle * (3.0f - 2.0f * ClampedTriangle);
}

void UEFBlinkMorphComponent::ApplyMorphAlpha(const float Alpha)
{
	USkeletalMeshComponent* MeshComponent = ResolvedTargetMesh.Get();
	if (!IsValid(MeshComponent))
	{
		return;
	}

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	for (const FEFBlinkMorphTarget& MorphTarget : MorphTargets)
	{
		if (MorphTarget.MorphName.IsNone())
		{
			continue;
		}

		if (!HasMorphTarget(MeshComponent, MorphTarget.MorphName))
		{
			continue;
		}

		const float Value = FMath::Lerp(MorphTarget.RestValue, MorphTarget.VisibleValue, ClampedAlpha);
		MeshComponent->SetMorphTarget(MorphTarget.MorphName, Value, true);
	}

	LastPulseAlpha = ClampedAlpha;
	MeshComponent->MarkRenderDynamicDataDirty();
}

void UEFBlinkMorphComponent::ApplyMorphsAtRest()
{
	ApplyMorphAlpha(0.0f);
}

void UEFBlinkMorphComponent::UpdateDebugState()
{
	const USkeletalMeshComponent* MeshComponent = ResolvedTargetMesh.Get();
	const USkeletalMesh* MeshAsset = IsValid(MeshComponent) ? MeshComponent->GetSkeletalMeshAsset() : nullptr;

	ResolvedTargetMeshComponentName = IsValid(MeshComponent) ? MeshComponent->GetFName() : NAME_None;
	ResolvedTargetMeshAssetName = MeshAsset ? MeshAsset->GetPathName() : FString();
}

void UEFBlinkMorphComponent::LogValidationFailureIfNeeded(const TArray<FName>& MissingMorphTargets)
{
	if (!bLogValidation)
	{
		return;
	}

	if (!IsValid(ResolvedTargetMesh.Get()))
	{
		if (!bLoggedNoTargetMesh)
		{
			UE_LOG(LogEFBlink, Warning, TEXT("[Blink] %s has no skeletal mesh component with the requested morph targets."), *GetNameSafe(GetOwner()));
			bLoggedNoTargetMesh = true;
		}
		return;
	}

	if (MissingMorphTargets.Num() <= 0 || bLoggedMissingMorphs)
	{
		return;
	}

	TArray<FString> MissingNames;
	MissingNames.Reserve(MissingMorphTargets.Num());
	for (const FName MissingMorphTarget : MissingMorphTargets)
	{
		MissingNames.Add(MissingMorphTarget.ToString());
	}

	UE_LOG(LogEFBlink, Warning, TEXT("[Blink] Mesh %s on %s is missing morph target(s): %s"),
		*GetNameSafe(ResolvedTargetMesh.Get()),
		*GetNameSafe(GetOwner()),
		*FString::Join(MissingNames, TEXT(", ")));
	bLoggedMissingMorphs = true;
}
