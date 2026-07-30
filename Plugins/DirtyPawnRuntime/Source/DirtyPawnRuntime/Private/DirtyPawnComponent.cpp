#include "DirtyPawnComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "Math/Transform.h"
#include "Math/Vector2D.h"
#include "UObject/ConstructorHelpers.h"

namespace DirtyPawnNames
{
	static const FName DiffuseTexture(TEXT("Diffuse Color Texture"));
	static const FName RoughnessTexture(TEXT("Specular Lobe 1 Roughness Texture"));
	static const FName AmbientOcclusionTexture(TEXT("Ambient Occlusion Weight Texture"));
	static const FName NormalTexture(TEXT("Detail Normal Map Texture"));
	static const FName TattooTexture(TEXT("Texture"));
	static const FName TattooColor(TEXT("Color"));
	static const FName TattooOpacity(TEXT("Opacity"));
	static const FName TattooOffsetU(TEXT("OffsetU"));
	static const FName TattooOffsetV(TEXT("OffsetV"));
	static const FName TattooScale(TEXT("Scale"));
	static const FName TattooScaleY(TEXT("ScaleY"));
}

namespace
{
	constexpr int32 MaxEnvironmentalPaintBands = 3;
	constexpr int32 MaxWetPaintBands = 3;
	constexpr int32 MaxWashPaintBands = 3;
	constexpr int32 MaxStainPaintBandsPerState = 3;
	constexpr float PaintBandMergeTolerance = 0.75f;
	constexpr float PaintBandVisibleThreshold = 0.001f;

	bool IsValidDirtyPawnCandidate(const UDirtyPawnComponent* Component)
	{
		return IsValid(Component)
			&& !Component->GetName().StartsWith(TEXT("TRASH_"))
			&& !Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
	}

	bool IsProjectTattooSkinBinding(const FDirtyPawnMaterialBinding& Binding)
	{
		const FString SlotName = Binding.MaterialSlotName.ToString().ToLower();
		return SlotName.Contains(TEXT("genesis9_body"))
			|| SlotName.Contains(TEXT("genesis9_arms"))
			|| SlotName.Contains(TEXT("genesis9_legs"))
			|| SlotName.Contains(TEXT("genesis9_head"));
	}

	int32 DirtyPawnComponentPreferenceScore(const UDirtyPawnComponent* Component)
	{
		if (!Component)
		{
			return MIN_int32;
		}

		int32 Score = 0;
		switch (Component->CreationMethod)
		{
		case EComponentCreationMethod::Native:
			Score += 900;
			break;
		case EComponentCreationMethod::SimpleConstructionScript:
			Score += 1000;
			break;
		case EComponentCreationMethod::UserConstructionScript:
			Score += 800;
			break;
		case EComponentCreationMethod::Instance:
		default:
			Score += 500;
			break;
		}

		const FString Name = Component->GetName();
		if (Name.Equals(TEXT("DirtyPawn"), ESearchCase::IgnoreCase))
		{
			Score += 80;
		}
		else if (Name.StartsWith(TEXT("DirtyPawn"), ESearchCase::IgnoreCase))
		{
			Score += 60;
		}

		if (Name.Contains(TEXT("Runtime"), ESearchCase::IgnoreCase))
		{
			Score += 20;
		}

		if (Component->IsDirtyPawnReady())
		{
			Score += 40;
		}

		Score += FMath::Min(Component->GetDirtyPawnMaterialBindingCount(), 64);
		return Score;
	}

	bool IsEnvironmentalPaintState(EDirtyPawnPaintState State)
	{
		return State == EDirtyPawnPaintState::Mud
			|| State == EDirtyPawnPaintState::Sand
			|| State == EDirtyPawnPaintState::Snow;
	}

	bool IsStainPaintState(EDirtyPawnPaintState State)
	{
		return State == EDirtyPawnPaintState::Blood
			|| State == EDirtyPawnPaintState::Smear
			|| State == EDirtyPawnPaintState::Dirt
			|| State == EDirtyPawnPaintState::Burn;
	}

	bool PaintBandsOverlap(float AMin, float AMax, float BMin, float BMax)
	{
		return FMath::Max(AMin, BMin) < FMath::Min(AMax, BMax) + KINDA_SMALL_NUMBER;
	}

	float PaintBandOverlapMin(float AMin, float BMin)
	{
		return FMath::Max(AMin, BMin);
	}

	float PaintBandOverlapMax(float AMax, float BMax)
	{
		return FMath::Min(AMax, BMax);
	}

	float SmootherStep(float T)
	{
		const float ClampedT = FMath::Clamp(T, 0.0f, 1.0f);
		return ClampedT * ClampedT * ClampedT * (ClampedT * (ClampedT * 6.0f - 15.0f) + 10.0f);
	}

	float EvaluateFadedAlpha(float StartAlpha, float TargetAlpha, float ElapsedSeconds, float DurationSeconds)
	{
		if (DurationSeconds <= 0.0f)
		{
			return FMath::Clamp(TargetAlpha, 0.0f, 1.0f);
		}

		const float T = FMath::Clamp(ElapsedSeconds / FMath::Max(DurationSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		return FMath::Clamp(FMath::Lerp(StartAlpha, TargetAlpha, SmootherStep(T)), 0.0f, 1.0f);
	}

	bool UpdateBandAlpha(FDirtyPawnPaintBand& Band, float DeltaSeconds)
	{
		if (!FMath::IsNearlyEqual(Band.LastTargetAlpha, Band.TargetAlpha, 0.001f))
		{
			Band.FadeStartAlpha = Band.Alpha;
			Band.FadeElapsedSeconds = 0.0f;
			Band.LastTargetAlpha = Band.TargetAlpha;
		}

		if (FMath::IsNearlyEqual(Band.Alpha, Band.TargetAlpha, 0.001f))
		{
			Band.Alpha = Band.TargetAlpha;
			return false;
		}

		if (Band.FadeDuration <= 0.0f)
		{
			Band.Alpha = Band.TargetAlpha;
			return true;
		}

		Band.FadeElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
		const float T = FMath::Clamp(Band.FadeElapsedSeconds / FMath::Max(Band.FadeDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Band.Alpha = EvaluateFadedAlpha(Band.FadeStartAlpha, Band.TargetAlpha, Band.FadeElapsedSeconds, Band.FadeDuration);

		if (T >= 1.0f || FMath::IsNearlyEqual(Band.Alpha, Band.TargetAlpha, 0.001f))
		{
			Band.Alpha = Band.TargetAlpha;
		}

		return true;
	}

	bool UpdateWashBandAlpha(FDirtyPawnWashBand& Band, float DeltaSeconds)
	{
		if (!FMath::IsNearlyEqual(Band.LastTargetAlpha, Band.TargetAlpha, 0.001f))
		{
			Band.FadeStartAlpha = Band.Alpha;
			Band.FadeElapsedSeconds = 0.0f;
			Band.LastTargetAlpha = Band.TargetAlpha;
		}

		if (FMath::IsNearlyEqual(Band.Alpha, Band.TargetAlpha, 0.001f))
		{
			Band.Alpha = Band.TargetAlpha;
			return false;
		}

		if (Band.FadeDuration <= 0.0f)
		{
			Band.Alpha = Band.TargetAlpha;
			return true;
		}

		Band.FadeElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
		const float T = FMath::Clamp(Band.FadeElapsedSeconds / FMath::Max(Band.FadeDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Band.Alpha = EvaluateFadedAlpha(Band.FadeStartAlpha, Band.TargetAlpha, Band.FadeElapsedSeconds, Band.FadeDuration);

		if (T >= 1.0f || FMath::IsNearlyEqual(Band.Alpha, Band.TargetAlpha, 0.001f))
		{
			Band.Alpha = Band.TargetAlpha;
		}

		return true;
	}

	const TCHAR* PaintStateParameterStem(EDirtyPawnPaintState State)
	{
		switch (State)
		{
		case EDirtyPawnPaintState::Blood:
			return TEXT("DPBlood");
		case EDirtyPawnPaintState::Smear:
			return TEXT("DPSmear");
		case EDirtyPawnPaintState::Dirt:
			return TEXT("DPDirt");
		case EDirtyPawnPaintState::Burn:
			return TEXT("DPBurn");
		default:
			return TEXT("");
		}
	}

	FString NormalizeDirtyPawnIdentifier(FString Text)
	{
		Text = Text.ToLower();
		Text.ReplaceInline(TEXT(" "), TEXT(""));
		Text.ReplaceInline(TEXT("_"), TEXT(""));
		Text.ReplaceInline(TEXT("-"), TEXT(""));
		return Text;
	}
}

UDirtyPawnComponent::UDirtyPawnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	SkinWrapperMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper.M_DirtyPawn_DAZSkinWrapper")));

	IncludeMaterialTokens = {
		TEXT("skin"),
		TEXT("body"),
		TEXT("arms"),
		TEXT("legs"),
		TEXT("torso"),
		TEXT("head"),
		TEXT("face"),
		TEXT("genesis"),
		TEXT("genesis8"),
		TEXT("genesis9"),
		TEXT("daz"),
		TEXT("fabric"),
		TEXT("cloth"),
		TEXT("clothes"),
		TEXT("fingernail")
	};

	ExcludeMaterialTokens = {
		TEXT("eye"),
		TEXT("eyemoisture"),
		TEXT("moisture"),
		TEXT("eyelash"),
		TEXT("lash"),
		TEXT("mouth"),
		TEXT("cavity"),
		TEXT("teeth"),
		TEXT("tooth"),
		TEXT("tongue"),
		TEXT("cornea"),
		TEXT("tear"),
		TEXT("lacrimal"),
		TEXT("pubichair")
	};
}

void UDirtyPawnComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoInitializeOnBeginPlay)
	{
		PreinitializeDirtyPawn();
	}
}

void UDirtyPawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveWaterSources.Empty();
	PendingWashBands.Empty();
	PendingWetPaintBands.Empty();
	bWashFadeClockActive = false;
	WashFadeClockSeconds = 0.0f;
	EnvironmentalPaintBands.Empty();
	WetPaintBands.Empty();
	StainPaintBands.Empty();
	WashPaintBands.Empty();
	MaterialBindings.Empty();
	Super::EndPlay(EndPlayReason);
}

void UDirtyPawnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PrewarmState != EDirtyPawnPrewarmState::Ready)
	{
		return;
	}

	DynamicMaterialTick(DeltaTime);
	InteriorCheck();
}

void UDirtyPawnComponent::PreinitializeDirtyPawn()
{
	if (UDirtyPawnComponent* CanonicalComponent = FindCanonicalDirtyPawnComponent(GetOwner()))
	{
		if (CanonicalComponent != this)
		{
			DisableAsDuplicateDirtyPawnComponent();
			return;
		}
		DisableNonCanonicalDirtyPawnComponents();
	}

	bUseMaterialWrapper = true;
	if (SkinWrapperMaterial.IsNull())
	{
		SkinWrapperMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper.M_DirtyPawn_DAZSkinWrapper")));
	}

	if (bLogSetupWarnings)
	{
		UE_LOG(LogTemp, Display, TEXT("[DirtyPawnRuntime] PreinitializeDirtyPawn owner=%s state=%d wrapper=%s"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(PrewarmState),
			*SkinWrapperMaterial.ToString());
	}

	if (PrewarmState == EDirtyPawnPrewarmState::Building)
	{
		return;
	}

	if (PrewarmState == EDirtyPawnPrewarmState::Ready && MaterialBindings.Num() > 0)
	{
		UpdateActorHeightFrame();
		MarkParametersDirty();
		PushAllParameters();
		return;
	}

	PrewarmState = EDirtyPawnPrewarmState::Building;
	RebuildDirtyPawnMaterials();

	if (MaterialBindings.Num() <= 0)
	{
		PrewarmState = EDirtyPawnPrewarmState::Failed;
		if (bLogSetupWarnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DirtyPawnRuntime] %s has no valid skin/fabric material bindings."), *GetNameSafe(GetOwner()));
		}
		return;
	}

	UpdateActorHeightFrame();
	PrewarmState = EDirtyPawnPrewarmState::Ready;
	MarkParametersDirty();
	PushAllParameters();

	if (bLogSetupWarnings)
	{
		UE_LOG(LogTemp, Display, TEXT("[DirtyPawnRuntime] Ready owner=%s bindings=%d height=%.2f bottom=%.2f top=%.2f"),
			*GetNameSafe(GetOwner()),
			MaterialBindings.Num(),
			DirtyPawnActorHeight,
			DirtyPawnActorBottom,
			DirtyPawnActorTop);
	}
}

void UDirtyPawnComponent::RebuildDirtyPawnMaterials()
{
	MaterialBindings.Empty();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UMaterialInterface* WrapperMaterial = nullptr;
	if (bUseMaterialWrapper)
	{
		WrapperMaterial = SkinWrapperMaterial.LoadSynchronous();
		if (!WrapperMaterial && bLogSetupWarnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DirtyPawnRuntime] Wrapper material missing: %s"), *SkinWrapperMaterial.ToString());
		}
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents<USkeletalMeshComponent>(MeshComponents, true);

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || !MeshComponent->IsVisible() || !MeshComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInterface* OriginalMaterial = MeshComponent->GetMaterial(MaterialIndex);
			if (!ShouldAffectMaterial(MeshComponent, MaterialIndex, OriginalMaterial))
			{
				continue;
			}

			UMaterialInterface* ParentMaterial = WrapperMaterial ? WrapperMaterial : OriginalMaterial;
			if (!ParentMaterial)
			{
				continue;
			}

			UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(ParentMaterial, this);
			if (!DynamicMaterial)
			{
				continue;
			}

			if (WrapperMaterial && OriginalMaterial)
			{
				DynamicMaterial->CopyMaterialUniformParameters(OriginalMaterial);
				CopyTextureParameter(OriginalMaterial, DynamicMaterial, DirtyPawnNames::DiffuseTexture);
				CopyTextureParameter(OriginalMaterial, DynamicMaterial, DirtyPawnNames::RoughnessTexture);
				CopyTextureParameter(OriginalMaterial, DynamicMaterial, DirtyPawnNames::AmbientOcclusionTexture);
				CopyTextureParameter(OriginalMaterial, DynamicMaterial, DirtyPawnNames::NormalTexture);
			}

			FDirtyPawnMaterialBinding Binding;
			Binding.MeshComponent = MeshComponent;
			Binding.MaterialIndex = MaterialIndex;
			Binding.MaterialSlotName = MeshComponent->GetMaterialSlotNames().IsValidIndex(MaterialIndex)
				? MeshComponent->GetMaterialSlotNames()[MaterialIndex]
				: NAME_None;
			Binding.OriginalMaterial = OriginalMaterial;
			Binding.DynamicMaterial = DynamicMaterial;
			Binding.bFabric = Binding.MaterialSlotName.ToString().ToLower().Contains(TEXT("fabric"))
				|| GetNameSafe(OriginalMaterial).ToLower().Contains(TEXT("fabric"))
				|| GetNameSafe(MeshComponent).ToLower().Contains(TEXT("cloth"));
			Binding.bUseRotatedSweatUV = Binding.MaterialSlotName.ToString().Contains(TEXT("arms"), ESearchCase::IgnoreCase)
				|| GetNameSafe(OriginalMaterial).Contains(TEXT("arms"), ESearchCase::IgnoreCase);

			MeshComponent->SetMaterial(MaterialIndex, DynamicMaterial);
			MaterialBindings.Add(Binding);
		}
	}

	MarkParametersDirty();
}

int32 UDirtyPawnComponent::GetLiveDirtyPawnMaterialBindingCount() const
{
	int32 LiveCount = 0;
	for (const FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		const USkeletalMeshComponent* MeshComponent = Binding.MeshComponent.Get();
		if (!MeshComponent || !Binding.DynamicMaterial || Binding.MaterialIndex == INDEX_NONE)
		{
			continue;
		}

		if (MeshComponent->GetMaterial(Binding.MaterialIndex) == Binding.DynamicMaterial)
		{
			++LiveCount;
		}
	}
	return LiveCount;
}

UMaterialInstanceDynamic* UDirtyPawnComponent::ApplyTattooToBoundSkinMaterials(UTexture* TattooTexture, FLinearColor TattooColor, float Opacity, float OffsetU, float OffsetV, float Scale, float ScaleY)
{
	if (PrewarmState != EDirtyPawnPrewarmState::Ready || MaterialBindings.Num() <= 0)
	{
		PreinitializeDirtyPawn();
	}

	const float SafeOpacity = TattooTexture ? FMath::Clamp(Opacity, 0.0f, 1.0f) : 0.0f;
	const float SafeScale = FMath::IsNearlyZero(Scale) ? 0.35f : FMath::Abs(Scale);
	const float SafeScaleY = FMath::IsNearlyZero(ScaleY) ? SafeScale : FMath::Abs(ScaleY);
	UMaterialInstanceDynamic* FirstAppliedMID = nullptr;

	for (const FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		UMaterialInstanceDynamic* DynamicMaterial = Binding.DynamicMaterial;
		if (!DynamicMaterial || !IsProjectTattooSkinBinding(Binding))
		{
			continue;
		}

		if (TattooTexture)
		{
			DynamicMaterial->SetTextureParameterValue(DirtyPawnNames::TattooTexture, TattooTexture);
		}
		DynamicMaterial->SetVectorParameterValue(DirtyPawnNames::TattooColor, TattooColor);
		DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooOpacity, SafeOpacity);
		DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooOffsetU, OffsetU);
		DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooOffsetV, OffsetV);
		DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooScale, SafeScale);
		DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooScaleY, SafeScaleY);

		if (!FirstAppliedMID)
		{
			FirstAppliedMID = DynamicMaterial;
		}
	}

	return FirstAppliedMID;
}

void UDirtyPawnComponent::ClearTattooFromBoundSkinMaterials()
{
	for (const FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		if (Binding.DynamicMaterial && IsProjectTattooSkinBinding(Binding))
		{
			Binding.DynamicMaterial->SetScalarParameterValue(DirtyPawnNames::TattooOpacity, 0.0f);
		}
	}
}

UMaterialInstanceDynamic* UDirtyPawnComponent::GetFirstTattooBoundSkinMID() const
{
	for (const FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		if (Binding.DynamicMaterial && IsProjectTattooSkinBinding(Binding))
		{
			return Binding.DynamicMaterial;
		}
	}

	return nullptr;
}

float UDirtyPawnComponent::GetMaxPaintAlphaForState(EDirtyPawnPaintState State) const
{
	if (State == EDirtyPawnPaintState::Sweat)
	{
		return GetSweatVisualOpacity();
	}

	const TArray<FDirtyPawnPaintBand>* Bands = nullptr;
	if (State == EDirtyPawnPaintState::Wet)
	{
		Bands = &WetPaintBands;
	}
	else if (IsEnvironmentalPaintState(State))
	{
		Bands = &EnvironmentalPaintBands;
	}
	else if (IsStainPaintState(State))
	{
		Bands = &StainPaintBands;
	}

	if (!Bands)
	{
		return 0.0f;
	}

	float MaxAlpha = 0.0f;
	for (const FDirtyPawnPaintBand& Band : *Bands)
	{
		if (Band.State == State)
		{
			MaxAlpha = FMath::Max(MaxAlpha, FMath::Max(Band.Alpha, Band.TargetAlpha));
		}
	}
	return FMath::Clamp(MaxAlpha, 0.0f, 1.0f);
}

float UDirtyPawnComponent::GetMaxVisiblePaintAlphaForState(EDirtyPawnPaintState State) const
{
	if (State == EDirtyPawnPaintState::Sweat)
	{
		const float BodyTop = FMath::Max3(GetDirtyPawnBodyReferenceHeight(), StandingHeadHeight, CrouchHeadHeight);
		return FMath::Clamp(GetSweatVisualOpacity() * (1.0f - GetWashCoverageAlphaForRange(0.0f, BodyTop)), 0.0f, 1.0f);
	}

	const TArray<FDirtyPawnPaintBand>* Bands = nullptr;
	if (State == EDirtyPawnPaintState::Wet)
	{
		Bands = &WetPaintBands;
	}
	else if (IsEnvironmentalPaintState(State))
	{
		Bands = &EnvironmentalPaintBands;
	}
	else if (IsStainPaintState(State))
	{
		Bands = &StainPaintBands;
	}

	if (!Bands)
	{
		return 0.0f;
	}

	float MaxAlpha = 0.0f;
	for (const FDirtyPawnPaintBand& Band : *Bands)
	{
		if (Band.State != State)
		{
			continue;
		}

		const float RawAlpha = FMath::Max(Band.Alpha, Band.TargetAlpha);
		const float WashCoverage = GetWashCoverageAlphaForRange(Band.MinHeight, Band.MaxHeight);
		MaxAlpha = FMath::Max(MaxAlpha, RawAlpha * (1.0f - WashCoverage));
	}

	return FMath::Clamp(MaxAlpha, 0.0f, 1.0f);
}

bool UDirtyPawnComponent::HasActivePaintState(EDirtyPawnPaintState State, float MinAlpha) const
{
	return GetMaxPaintAlphaForState(State) >= FMath::Clamp(MinAlpha, 0.0f, 1.0f);
}

void UDirtyPawnComponent::SetSweatPoints(float NewSweatPoints)
{
	const float OldPoints = SweatPoints;
	const float OldVisualOpacity = GetSweatVisualOpacity();
	const float OldRoughnessAlpha = GetSweatRoughnessAlpha();
	const bool bOldPersistentFloorActive = bSweatPersistentFloorActive;
	const bool bOldSweatyStateActive = bSweatyStateActive;

	SweatMaxPoints = FMath::Max(SweatMaxPoints, 0.001f);
	SweatPoints = FMath::Clamp(NewSweatPoints, 0.0f, SweatMaxPoints);
	if (GetSweatNormalizedValue() >= FMath::Clamp(SweatPersistenceThreshold, 0.0f, 1.0f))
	{
		bSweatPersistentFloorActive = true;
	}
	UpdateSweatyStateFromSweat();

	if (!FMath::IsNearlyEqual(OldPoints, SweatPoints, 0.001f)
		|| !FMath::IsNearlyEqual(OldVisualOpacity, GetSweatVisualOpacity(), 0.001f)
		|| !FMath::IsNearlyEqual(OldRoughnessAlpha, GetSweatRoughnessAlpha(), 0.001f)
		|| bOldPersistentFloorActive != bSweatPersistentFloorActive
		|| bOldSweatyStateActive != bSweatyStateActive)
	{
		MarkParametersDirty();
	}
}

void UDirtyPawnComponent::AddSweatPoints(float DeltaSweatPoints)
{
	if (FMath::IsNearlyZero(DeltaSweatPoints))
	{
		return;
	}

	if (DeltaSweatPoints > 0.0f)
	{
		SweatIdleSeconds = 0.0f;
	}

	SetSweatPoints(SweatPoints + DeltaSweatPoints);
}

void UDirtyPawnComponent::ClearSweat()
{
	ClearSweatInternal(true);
}

float UDirtyPawnComponent::GetSweatNormalizedValue() const
{
	return FMath::Clamp(SweatPoints / FMath::Max(SweatMaxPoints, 0.001f), 0.0f, 1.0f);
}

float UDirtyPawnComponent::GetSweatVisualOpacity() const
{
	const float Normalized = GetSweatNormalizedValue();
	const float PersistentFloor = bSweatPersistentFloorActive
		? FMath::Clamp(SweatPersistentOpacityFloor, 0.0f, 1.0f)
		: 0.0f;
	return FMath::Clamp(FMath::Max(Normalized, PersistentFloor), 0.0f, 1.0f);
}

float UDirtyPawnComponent::GetSweatRoughnessAlpha() const
{
	return GetSweatNormalizedValue();
}

bool UDirtyPawnComponent::IsSweaty() const
{
	return bSweatyStateActive;
}

UDirtyPawnComponent* UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}

	TArray<UDirtyPawnComponent*> Components;
	Actor->GetComponents<UDirtyPawnComponent>(Components);

	UDirtyPawnComponent* BestComponent = nullptr;
	int32 BestLiveCount = -1;
	int32 BestScore = MIN_int32;

	for (UDirtyPawnComponent* Component : Components)
	{
		if (!IsValidDirtyPawnCandidate(Component))
		{
			continue;
		}

		const int32 LiveCount = Component->GetLiveDirtyPawnMaterialBindingCount();
		const int32 Score = DirtyPawnComponentPreferenceScore(Component);
		if (!BestComponent || LiveCount > BestLiveCount || (LiveCount == BestLiveCount && Score > BestScore))
		{
			BestComponent = Component;
			BestLiveCount = LiveCount;
			BestScore = Score;
		}
	}

	return BestComponent;
}

float UDirtyPawnComponent::ResolveBodyLocalHeightFromWorldZ(float WorldZ, bool bApplyCrouchSubmergeBonus, bool bForceCrouchedForDebug)
{
	UpdateActorHeightFrame();

	float BodyLocalHeight = WorldZ - DirtyPawnActorBottom + WetHeightOffset;
	if (bApplyCrouchSubmergeBonus && (bForceCrouchedForDebug || IsOwnerCrouched()))
	{
		BodyLocalHeight += GetCrouchSubmergeBonus();
	}

	return NormalizeNodeHeight(BodyLocalHeight);
}

float UDirtyPawnComponent::GetDirtyPawnBodyReferenceHeight() const
{
	return FMath::Max(bHasDirtyPawnHeightReference ? DirtyPawnReferenceHeight : DirtyPawnActorHeight, 1.0f);
}

void UDirtyPawnComponent::WaterOverlapEvent(AActor* WaterActorReference, float NodeHeight, bool bIsMud, bool bIsBleach)
{
	WaterOverlapBand(WaterActorReference, 0.0f, NodeHeight, bIsMud, bIsBleach);
}

void UDirtyPawnComponent::WaterOverlapBand(AActor* WaterActorReference, float NodeMinHeight, float NodeMaxHeight, bool bIsMud, bool bIsBleach)
{
	if (PrewarmState != EDirtyPawnPrewarmState::Ready)
	{
		if (bLogSetupWarnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DirtyPawnRuntime] WaterOverlapEvent skipped because %s is not ready."), *GetNameSafe(GetOwner()));
		}
		return;
	}

	if (WaterActorReference)
	{
		ActiveWaterSources.Add(WaterActorReference);
	}

	InWater = true;
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	if (bIsMud)
	{
		OverallFadeWetness_Target = 1.0f;
		L1_WaterHeight_Target = FMath::Max(L1_WaterHeight_Target, MaxHeight);
		L2_WaterHeight_Target = FMath::Max(L2_WaterHeight_Target, MaxHeight);
		ApplyWetBand(MinHeight, MaxHeight, 1.0f);

		InMud = true;
		MudOpacity = FMath::Max(MudOpacity, 1.0f);
		ApplyEnvironmentalBand(WaterActorReference, EDirtyPawnPaintState::Mud, MinHeight, MaxHeight, 1.0f, false);
	}
	else
	{
		CurrentlyWashing = true;
		QueueTwoPhaseWashBand(MinHeight, MaxHeight, true, true, true);
	}

	MarkParametersDirty();
}

void UDirtyPawnComponent::EndWaterOverlap(AActor* WaterActorReference)
{
	if (WaterActorReference)
	{
		ActiveWaterSources.Remove(WaterActorReference);
	}

	if (ActiveWaterSources.Num() <= 0)
	{
		InWater = false;
		OverallFadeWetness_Target = 0.0f;
		const float InactiveHeight = GetReferenceInactiveHeight();
		L1_WaterHeight_Target = InactiveHeight;
		L2_WaterHeight_Target = InactiveHeight;
		for (FDirtyPawnPaintBand& Band : WetPaintBands)
		{
			Band.TargetAlpha = 0.0f;
			Band.FadeDuration = FMath::Max(WashWetFadeOutSeconds, 0.01f);
		}
		bWashFadeClockActive = false;
		WashFadeClockSeconds = 0.0f;
	}

	MarkParametersDirty();
}

void UDirtyPawnComponent::DynamicMaterialTick(float DeltaSeconds)
{
	UpdateActorHeightFrame();

	bool bChanged = false;
	bChanged |= UpdateSweatState(DeltaSeconds);
	if (bWashFadeClockActive)
	{
		WashFadeClockSeconds += FMath::Max(DeltaSeconds, 0.0f);
	}
	bChanged |= UpdatePendingWashBands(DeltaSeconds);
	bChanged |= UpdateUnifiedWashBands(DeltaSeconds);
	bChanged |= UpdatePaintBands(DeltaSeconds);
	bChanged |= UpdatePendingWetBands(DeltaSeconds);
	if (bWashFadeClockActive
		&& ActiveWaterSources.Num() <= 0
		&& PendingWashBands.Num() == 0
		&& PendingWetPaintBands.Num() == 0
		&& WashFadeClockSeconds > FMath::Max(WashMaterialFadeSeconds, WashWetDelayMaxSeconds))
	{
		bWashFadeClockActive = false;
		WashFadeClockSeconds = 0.0f;
	}
	bChanged |= InterpScalar(OverallFadeWetness, OverallFadeWetness_Target, DeltaSeconds, WetnessFadeSpeed);
	bChanged |= InterpScalar(L1_WaterHeight, L1_WaterHeight_Target, DeltaSeconds, WetnessFadeSpeed);
	bChanged |= InterpScalar(L2_WaterHeight, L2_WaterHeight_Target, DeltaSeconds, WetnessFadeSpeed);
	bChanged |= InterpScalar(MudHeight, MudHeight_Target, DeltaSeconds, MudWashSpeed);
	bChanged |= InterpScalar(MudWashHeight, MudWashHeight_Target, DeltaSeconds, MudFadeWashSpeed);
	bChanged |= InterpScalar(OverallFadeBlood, OverallFadeBlood_Target, DeltaSeconds, BloodFadeSpeed);
	bChanged |= InterpScalar(BloodHeight, BloodHeight_Target, DeltaSeconds, BloodFadeSpeed);
	bChanged |= InterpScalar(BloodWashHeight, BloodWashHeight_Target, DeltaSeconds, BloodFadeSpeed);
	bChanged |= InterpScalar(OverallFadeSmear, OverallFadeSmear_Target, DeltaSeconds, SmearFadeSpeed);
	bChanged |= InterpScalar(SmearHeight, SmearHeight_Target, DeltaSeconds, SmearFadeSpeed);
	bChanged |= InterpScalar(SmearWashHeight, SmearWashHeightTarget, DeltaSeconds, SmearFadeSpeed);
	bChanged |= InterpScalar(OverallFadeSand, OverallFadeSand_Target, DeltaSeconds, SandFadeSpeed);
	bChanged |= InterpScalar(SandHeight, SandHeight_Target, DeltaSeconds, SandFadeSpeed);
	bChanged |= InterpScalar(SandWashHeight, SandWashHeight_Target, DeltaSeconds, SandWashSpeed);
	bChanged |= InterpScalar(OverallFadeSnow, OverallFadeSnow_Target, DeltaSeconds, SnowFadeSpeed);
	bChanged |= InterpScalar(SnowHeight, SnowHeight_Target, DeltaSeconds, SnowFadeSpeed);
	bChanged |= InterpScalar(SnowWashHeight, SnowWashHeight_Target, DeltaSeconds, SnowWashSpeed);
	bChanged |= InterpScalar(OverallFadeDirt, OverallFadeDirt_Target, DeltaSeconds, DirtFadeSpeed);
	bChanged |= InterpScalar(DirtHeight, DirtHeight_Target, DeltaSeconds, DirtFadeSpeed);
	bChanged |= InterpScalar(DirtWashHeight, DirtWashHeight_Target, DeltaSeconds, DirtFadeSpeed);
	bChanged |= InterpScalar(OverallFadeBurn, OverallFadeBurn_Target, DeltaSeconds, BurnFadeSpeed);
	bChanged |= InterpScalar(BurnHeight, BurnHeight_Target, DeltaSeconds, BurnFadeSpeed);
	bChanged |= InterpScalar(BurnWashHeight, BurnWashHeight_Target, DeltaSeconds, BurnFadeSpeed);
	bChanged |= InterpScalar(HairWetness, HairWetness_Target, DeltaSeconds, WetnessFadeSpeed);
	bChanged |= InterpScalar(HairMud, HairMud_Target, DeltaSeconds, MudWashSpeed);
	bChanged |= InterpScalar(HairSmear, HairSmear_Target, DeltaSeconds, SmearFadeSpeed);

	SyncLegacyScalarsFromBands();
	FadeTransitioning = bChanged;

	if (bChanged || bParametersDirty || HasVisibleState())
	{
		PushAllParameters();
	}
}

void UDirtyPawnComponent::InteriorCheck()
{
	// Dirty Pawn exposes this event for Blueprint wiring. The C++ port keeps it as
	// an extension point because interior state only changes material wet/sand/snow
	// policy when project volumes call into this component.
}

void UDirtyPawnComponent::SmearEvent(float NodeHeight, float Strength)
{
	SmearBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::SmearBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyStainBand(EDirtyPawnPaintState::Smear, NodeMinHeight, NodeMaxHeight, Strength);
}

void UDirtyPawnComponent::BloodEvent(float NodeHeight, float Strength)
{
	BloodBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::BloodBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyStainBand(EDirtyPawnPaintState::Blood, NodeMinHeight, NodeMaxHeight, Strength);
}

void UDirtyPawnComponent::MudBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyEnvironmentalBand(GetOwner(), EDirtyPawnPaintState::Mud, NodeMinHeight, NodeMaxHeight, Strength, false);
}

void UDirtyPawnComponent::SandEvent(float NodeHeight, float Strength)
{
	SandBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::SandBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyEnvironmentalBand(GetOwner(), EDirtyPawnPaintState::Sand, NodeMinHeight, NodeMaxHeight, Strength, false);
}

void UDirtyPawnComponent::SnowEvent(float NodeHeight, float Strength)
{
	SnowBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::SnowBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyEnvironmentalBand(GetOwner(), EDirtyPawnPaintState::Snow, NodeMinHeight, NodeMaxHeight, Strength, false);
}

void UDirtyPawnComponent::DirtEvent(float NodeHeight, float Strength)
{
	DirtBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::DirtBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyStainBand(EDirtyPawnPaintState::Dirt, NodeMinHeight, NodeMaxHeight, Strength);
}

void UDirtyPawnComponent::BurnEvent(float NodeHeight, float Strength)
{
	BurnBandEvent(0.0f, NodeHeight, Strength);
}

void UDirtyPawnComponent::BurnBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	ApplyStainBand(EDirtyPawnPaintState::Burn, NodeMinHeight, NodeMaxHeight, Strength);
}

void UDirtyPawnComponent::FadeOutWashMudEvent(float NodeHeight)
{
	FadeOutWashMudBandEvent(0.0f, NodeHeight);
}

void UDirtyPawnComponent::FadeOutWashMudBandEvent(float NodeMinHeight, float NodeMaxHeight)
{
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	FadeOutBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Mud, MinHeight, MaxHeight, EnvironmentalFadeOutSeconds);
	CurrentlyWashing = true;
	MarkParametersDirty();
}

void UDirtyPawnComponent::SetFadeWashVariables(float NodeHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	SetFadeWashVariablesBand(0.0f, NodeHeight, bWashBlood, bWashSmears, bWashSandSnow);
}

void UDirtyPawnComponent::SetFadeWashVariablesBand(float NodeMinHeight, float NodeMaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	CurrentlyWashing = true;
	QueueTwoPhaseWashBand(MinHeight, MaxHeight, bWashBlood, bWashSmears, bWashSandSnow);
	MarkParametersDirty();
}

void UDirtyPawnComponent::SetFadeSandSnowVariables(float NodeHeight, bool bApplySand, bool bApplySnow)
{
	SetFadeSandSnowVariablesBand(0.0f, NodeHeight, bApplySand, bApplySnow);
}

void UDirtyPawnComponent::SetFadeSandSnowVariablesBand(float NodeMinHeight, float NodeMaxHeight, bool bApplySand, bool bApplySnow)
{
	if (bApplySand)
	{
		SandBandEvent(NodeMinHeight, NodeMaxHeight, 1.0f);
	}
	if (bApplySnow)
	{
		SnowBandEvent(NodeMinHeight, NodeMaxHeight, 1.0f);
	}
}

void UDirtyPawnComponent::SetFadeSandSnowVariables_Instant(float NodeHeight, bool bApplySand, bool bApplySnow)
{
	const float Height = NormalizeNodeHeight(NodeHeight);
	if (bApplySand)
	{
		ApplyEnvironmentalBand(GetOwner(), EDirtyPawnPaintState::Sand, 0.0f, Height, 1.0f, true);
	}
	if (bApplySnow)
	{
		ApplyEnvironmentalBand(GetOwner(), EDirtyPawnPaintState::Snow, 0.0f, Height, 1.0f, true);
	}
	MarkParametersDirty();
}

void UDirtyPawnComponent::ResetDirtyPawn()
{
	UpdateActorHeightFrame();
	const float InactiveHeight = GetReferenceInactiveHeight();

	ActiveWaterSources.Empty();
	PendingWashBands.Empty();
	PendingWetPaintBands.Empty();
	bWashFadeClockActive = false;
	WashFadeClockSeconds = 0.0f;
	EnvironmentalPaintBands.Empty();
	WetPaintBands.Empty();
	StainPaintBands.Empty();
	WashPaintBands.Empty();
	NextPaintBandPriority = 1;
	InWater = false;
	InMud = false;
	CurrentlyWashing = false;
	WashResetting = true;

	OverallFadeWetness = OverallFadeWetness_Target = 0.0f;
	OverallFadeBlood = OverallFadeBlood_Target = 0.0f;
	OverallFadeSmear = OverallFadeSmear_Target = 0.0f;
	OverallFadeSand = OverallFadeSand_Target = 0.0f;
	OverallFadeSnow = OverallFadeSnow_Target = 0.0f;
	OverallFadeDirt = OverallFadeDirt_Target = 0.0f;
	OverallFadeBurn = OverallFadeBurn_Target = 0.0f;
	MudOpacity = 0.0f;
	ClearSweatInternal(false);

	MudHeight = MudHeight_Target = InactiveHeight;
	MudWashHeight = MudWashHeight_Target = InactiveHeight;
	BloodHeight = BloodHeight_Target = InactiveHeight;
	BloodWashHeight = BloodWashHeight_Target = InactiveHeight;
	SmearHeight = SmearHeight_Target = InactiveHeight;
	SmearWashHeight = SmearWashHeightTarget = InactiveHeight;
	SandHeight = SandHeight_Target = InactiveHeight;
	SandWashHeight = SandWashHeight_Target = InactiveHeight;
	SnowHeight = SnowHeight_Target = InactiveHeight;
	SnowWashHeight = SnowWashHeight_Target = InactiveHeight;
	DirtHeight = DirtHeight_Target = InactiveHeight;
	DirtWashHeight = DirtWashHeight_Target = InactiveHeight;
	BurnHeight = BurnHeight_Target = InactiveHeight;
	BurnWashHeight = BurnWashHeight_Target = InactiveHeight;
	L1_WaterHeight = L1_WaterHeight_Target = InactiveHeight;
	L2_WaterHeight = L2_WaterHeight_Target = InactiveHeight;

	HairWetness = HairWetness_Target = 0.0f;
	HairMud = HairMud_Target = 0.0f;
	HairSmear = HairSmear_Target = 0.0f;
	HairIsSubmerged = false;

	MarkParametersDirty();
	PushAllParameters();
	WashResetting = false;
}

void UDirtyPawnComponent::UpdateActorHeightFrame()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FBox ActorBox(EForceInit::ForceInit);
	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents<USkeletalMeshComponent>(MeshComponents, true);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || !MeshComponent->IsVisible() || !MeshComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		const FString Name = GetNameSafe(MeshComponent).ToLower();
		if (Name.Contains(TEXT("eye")) || Name.Contains(TEXT("lash")) || Name.Contains(TEXT("mouth")) || Name.Contains(TEXT("teeth")))
		{
			continue;
		}

		ActorBox += MeshComponent->Bounds.GetBox();
	}

	if (!ActorBox.IsValid)
	{
		FVector Origin;
		FVector Extent;
		Owner->GetActorBounds(true, Origin, Extent);
		DirtyPawnActorBottom = Origin.Z - Extent.Z;
		DirtyPawnActorTop = Origin.Z + Extent.Z;
	}
	else
	{
		DirtyPawnActorBottom = ActorBox.Min.Z;
		DirtyPawnActorTop = ActorBox.Max.Z;
	}

	DirtyPawnActorHeight = FMath::Max(DirtyPawnActorTop - DirtyPawnActorBottom, 1.0f);

	if (!bHasDirtyPawnHeightReference)
	{
		DirtyPawnReferenceBottom = DirtyPawnActorBottom;
		DirtyPawnReferenceTop = DirtyPawnActorTop;
		DirtyPawnReferenceHeight = DirtyPawnActorHeight;
		bHasDirtyPawnHeightReference = true;
	}

	if (MudHeight == 0.0f && MudHeight_Target == 0.0f)
	{
		const float InactiveHeight = GetReferenceInactiveHeight();
		OverallFadeWetness = OverallFadeWetness_Target = 0.0f;
		OverallFadeBlood = OverallFadeBlood_Target = 0.0f;
		OverallFadeSmear = OverallFadeSmear_Target = 0.0f;
		OverallFadeSand = OverallFadeSand_Target = 0.0f;
		OverallFadeSnow = OverallFadeSnow_Target = 0.0f;
		OverallFadeDirt = OverallFadeDirt_Target = 0.0f;
		OverallFadeBurn = OverallFadeBurn_Target = 0.0f;
		MudOpacity = 0.0f;
		MudHeight = MudHeight_Target = InactiveHeight;
		MudWashHeight = MudWashHeight_Target = InactiveHeight;
		BloodHeight = BloodHeight_Target = InactiveHeight;
		BloodWashHeight = BloodWashHeight_Target = InactiveHeight;
		SmearHeight = SmearHeight_Target = InactiveHeight;
		SmearWashHeight = SmearWashHeightTarget = InactiveHeight;
		SandHeight = SandHeight_Target = InactiveHeight;
		SandWashHeight = SandWashHeight_Target = InactiveHeight;
		SnowHeight = SnowHeight_Target = InactiveHeight;
		SnowWashHeight = SnowWashHeight_Target = InactiveHeight;
		DirtHeight = DirtHeight_Target = InactiveHeight;
		DirtWashHeight = DirtWashHeight_Target = InactiveHeight;
		BurnHeight = BurnHeight_Target = InactiveHeight;
		BurnWashHeight = BurnWashHeight_Target = InactiveHeight;
		L1_WaterHeight = L1_WaterHeight_Target = InactiveHeight;
		L2_WaterHeight = L2_WaterHeight_Target = InactiveHeight;
	}
}

bool UDirtyPawnComponent::ShouldAffectMaterial(USkeletalMeshComponent* MeshComponent, int32 MaterialIndex, UMaterialInterface* Material) const
{
	if (!MeshComponent || !Material)
	{
		return false;
	}

	FString Tokens = GetNameSafe(MeshComponent).ToLower();
	Tokens += TEXT(" ");
	Tokens += GetNameSafe(Material).ToLower();
	Tokens += TEXT(" ");
	Tokens += Material->GetPathName().ToLower();
	if (MeshComponent->GetSkeletalMeshAsset())
	{
		Tokens += TEXT(" ");
		Tokens += GetNameSafe(MeshComponent->GetSkeletalMeshAsset()).ToLower();
		Tokens += TEXT(" ");
		Tokens += MeshComponent->GetSkeletalMeshAsset()->GetPathName().ToLower();
	}

	const TArray<FName>& SlotNames = MeshComponent->GetMaterialSlotNames();
	if (SlotNames.IsValidIndex(MaterialIndex))
	{
		Tokens += TEXT(" ");
		Tokens += SlotNames[MaterialIndex].ToString().ToLower();
	}

	if (NormalizeDirtyPawnIdentifier(Tokens).Contains(TEXT("pubichair")))
	{
		return false;
	}

	const bool bGenesisFaceCarrier = Tokens.Contains(TEXT("mouthcavity"))
		&& (Tokens.Contains(TEXT("genesis")) || Tokens.Contains(TEXT("daz")));
	if (bGenesisFaceCarrier)
	{
		return true;
	}

	for (const FString& ExcludeToken : ExcludeMaterialTokens)
	{
		if (!ExcludeToken.IsEmpty() && Tokens.Contains(ExcludeToken.ToLower()))
		{
			return false;
		}
	}

	for (const FString& IncludeToken : IncludeMaterialTokens)
	{
		if (!IncludeToken.IsEmpty() && Tokens.Contains(IncludeToken.ToLower()))
		{
			return true;
		}
	}

	return false;
}

void UDirtyPawnComponent::CopyTextureParameter(UMaterialInterface* SourceMaterial, UMaterialInstanceDynamic* TargetMaterial, FName ParameterName) const
{
	if (!SourceMaterial || !TargetMaterial || ParameterName.IsNone())
	{
		return;
	}

	UTexture* Texture = nullptr;
	if (SourceMaterial->GetTextureParameterValue(FMaterialParameterInfo(ParameterName), Texture) && Texture)
	{
		TargetMaterial->SetTextureParameterValue(ParameterName, Texture);
	}
}

void UDirtyPawnComponent::PushAllParameters()
{
	if (PrewarmState != EDirtyPawnPrewarmState::Ready)
	{
		return;
	}

	PushScalar(TEXT("DirtyPawnActorBottom"), DirtyPawnActorBottom);
	PushScalar(TEXT("DirtyPawnActorTop"), DirtyPawnActorTop);
	PushScalar(TEXT("DirtyPawnActorHeight"), DirtyPawnActorHeight);
	PushScalar(TEXT("OverallFadeWetness"), OverallFadeWetness);
	PushScalar(TEXT("OverallFadeWetness_Target"), OverallFadeWetness_Target);
	PushScalar(TEXT("WetHeightOffset"), WetHeightOffset);
	PushScalar(TEXT("L1_Height"), L1_Height);
	PushScalar(TEXT("L1_WaterHeight"), L1_WaterHeight);
	PushScalar(TEXT("L1_WaterHeight_Target"), L1_WaterHeight_Target);
	PushScalar(TEXT("L1_Wetness"), L1_Wetness);
	PushScalar(TEXT("L2_Height"), L2_Height);
	PushScalar(TEXT("L2_WaterHeight"), L2_WaterHeight);
	PushScalar(TEXT("L2_WaterHeight_Target"), L2_WaterHeight_Target);
	PushScalar(TEXT("L2_Wetness"), L2_Wetness);
	PushScalar(TEXT("MudHeight"), MudHeight);
	PushScalar(TEXT("MudHeight_Target"), MudHeight_Target);
	PushScalar(TEXT("MudWashHeight"), MudWashHeight);
	PushScalar(TEXT("MudWashHeight_Target"), MudWashHeight_Target);
	PushScalar(TEXT("MudOpacity"), MudOpacity);
	PushScalar(TEXT("OverallFadeBlood"), OverallFadeBlood);
	PushScalar(TEXT("OverallFadeBlood_Target"), OverallFadeBlood_Target);
	PushScalar(TEXT("BloodHeight"), BloodHeight);
	PushScalar(TEXT("BloodHeight_Target"), BloodHeight_Target);
	PushScalar(TEXT("BloodWashHeight"), BloodWashHeight);
	PushScalar(TEXT("BloodWashHeight_Target"), BloodWashHeight_Target);
	PushScalar(TEXT("OverallFadeSmear"), OverallFadeSmear);
	PushScalar(TEXT("OverallFadeSmear_Target"), OverallFadeSmear_Target);
	PushScalar(TEXT("SmearHeight"), SmearHeight);
	PushScalar(TEXT("SmearHeight_Target"), SmearHeight_Target);
	PushScalar(TEXT("SmearWashHeight"), SmearWashHeight);
	PushScalar(TEXT("SmearWashHeightTarget"), SmearWashHeightTarget);
	PushScalar(TEXT("OverallFadeSand"), OverallFadeSand);
	PushScalar(TEXT("OverallFadeSand_Target"), OverallFadeSand_Target);
	PushScalar(TEXT("SandHeight"), SandHeight);
	PushScalar(TEXT("SandHeight_Target"), SandHeight_Target);
	PushScalar(TEXT("SandWashHeight"), SandWashHeight);
	PushScalar(TEXT("SandWashHeight_Target"), SandWashHeight_Target);
	PushScalar(TEXT("OverallFadeSnow"), OverallFadeSnow);
	PushScalar(TEXT("OverallFadeSnow_Target"), OverallFadeSnow_Target);
	PushScalar(TEXT("SnowHeight"), SnowHeight);
	PushScalar(TEXT("SnowHeight_Target"), SnowHeight_Target);
	PushScalar(TEXT("SnowWashHeight"), SnowWashHeight);
	PushScalar(TEXT("SnowWashHeight_Target"), SnowWashHeight_Target);
	PushScalar(TEXT("OverallFadeDirt"), OverallFadeDirt);
	PushScalar(TEXT("OverallFadeDirt_Target"), OverallFadeDirt_Target);
	PushScalar(TEXT("DirtHeight"), DirtHeight);
	PushScalar(TEXT("DirtHeight_Target"), DirtHeight_Target);
	PushScalar(TEXT("DirtWashHeight"), DirtWashHeight);
	PushScalar(TEXT("DirtWashHeight_Target"), DirtWashHeight_Target);
	PushScalar(TEXT("OverallFadeBurn"), OverallFadeBurn);
	PushScalar(TEXT("OverallFadeBurn_Target"), OverallFadeBurn_Target);
	PushScalar(TEXT("BurnHeight"), BurnHeight);
	PushScalar(TEXT("BurnHeight_Target"), BurnHeight_Target);
	PushScalar(TEXT("BurnWashHeight"), BurnWashHeight);
	PushScalar(TEXT("BurnWashHeight_Target"), BurnWashHeight_Target);
	PushScalar(TEXT("DPSweatVisualOpacity"), GetSweatVisualOpacity());
	PushScalar(TEXT("DPSweatRoughnessAlpha"), GetSweatRoughnessAlpha());
	PushScalar(TEXT("DPSweatMapScale"), FMath::Max(SweatMapScale, 0.001f));
	PushScalar(TEXT("DPSweatMapStrength"), FMath::Max(SweatMapStrength, 0.0f));
	PushScalar(TEXT("InWater"), InWater ? 1.0f : 0.0f);
	PushScalar(TEXT("InMud"), InMud ? 1.0f : 0.0f);
	PushScalar(TEXT("CurrentlyWashing"), CurrentlyWashing ? 1.0f : 0.0f);
	PushScalar(TEXT("WashResetting"), WashResetting ? 1.0f : 0.0f);
	PushScalar(TEXT("FadeTransitioning"), FadeTransitioning ? 1.0f : 0.0f);
	PushScalar(TEXT("HairWetness"), HairWetness);
	PushScalar(TEXT("HairWetness_Target"), HairWetness_Target);
	PushScalar(TEXT("HairMud"), HairMud);
	PushScalar(TEXT("HairMud_Target"), HairMud_Target);
	PushScalar(TEXT("HairSmear"), HairSmear);
	PushScalar(TEXT("HairSmear_Target"), HairSmear_Target);
	PushScalar(TEXT("HairIsSubmerged"), HairIsSubmerged ? 1.0f : 0.0f);

	for (FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		PushBindingLocalHeightFrame(Binding);
	}

	bParametersDirty = false;
}

void UDirtyPawnComponent::PushScalar(FName ParameterName, float Value)
{
	for (FDirtyPawnMaterialBinding& Binding : MaterialBindings)
	{
		if (Binding.DynamicMaterial)
		{
			Binding.DynamicMaterial->SetScalarParameterValue(ParameterName, Value);
		}
	}
}

void UDirtyPawnComponent::PushBindingLocalHeightFrame(FDirtyPawnMaterialBinding& Binding) const
{
	USkeletalMeshComponent* MeshComponent = Binding.MeshComponent.Get();
	if (!MeshComponent || !Binding.DynamicMaterial)
	{
		return;
	}

	EnsureStableBindingLocalHeightFrame(Binding);

	const auto ToLocalHeight = [this, &Binding](float BodyLocalHeight)
	{
		const float ReferenceHeight = GetDirtyPawnBodyReferenceHeight();
		const float NormalizedHeight = BodyLocalHeight / ReferenceHeight;
		return Binding.LocalActorBottom + NormalizedHeight * Binding.LocalActorHeight;
	};

	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DirtyPawnLocalActorBottom"), Binding.LocalActorBottom);
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DirtyPawnLocalActorTop"), Binding.LocalActorTop);
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DirtyPawnLocalActorHeight"), Binding.LocalActorHeight);
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSweatUseRotatedUV"), Binding.bUseRotatedSweatUV ? 1.0f : 0.0f);
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPL1WaterLocalHeight"), ToLocalHeight(L1_WaterHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPL2WaterLocalHeight"), ToLocalHeight(L2_WaterHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPMudLocalHeight"), ToLocalHeight(MudHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPMudWashLocalHeight"), ToLocalHeight(MudWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPBloodLocalHeight"), ToLocalHeight(BloodHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPBloodWashLocalHeight"), ToLocalHeight(BloodWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSmearLocalHeight"), ToLocalHeight(SmearHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSmearWashLocalHeight"), ToLocalHeight(SmearWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSandLocalHeight"), ToLocalHeight(SandHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSandWashLocalHeight"), ToLocalHeight(SandWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSnowLocalHeight"), ToLocalHeight(SnowHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPSnowWashLocalHeight"), ToLocalHeight(SnowWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPDirtLocalHeight"), ToLocalHeight(DirtHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPDirtWashLocalHeight"), ToLocalHeight(DirtWashHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPBurnLocalHeight"), ToLocalHeight(BurnHeight));
	Binding.DynamicMaterial->SetScalarParameterValue(TEXT("DPBurnWashLocalHeight"), ToLocalHeight(BurnWashHeight));
	PushPaintBandParameters(Binding);
}

void UDirtyPawnComponent::EnsureStableBindingLocalHeightFrame(FDirtyPawnMaterialBinding& Binding) const
{
	if (Binding.bHasStableLocalFrame)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = Binding.MeshComponent.Get();
	if (!MeshComponent)
	{
		return;
	}

	const FTransform& ComponentToWorld = MeshComponent->GetComponentTransform();
	const FVector ComponentLocation = ComponentToWorld.GetLocation();
	const float FrameBottom = bHasDirtyPawnHeightReference ? DirtyPawnReferenceBottom : DirtyPawnActorBottom;
	const float FrameTop = bHasDirtyPawnHeightReference ? DirtyPawnReferenceTop : DirtyPawnActorTop;
	const FVector BottomWorld(ComponentLocation.X, ComponentLocation.Y, FrameBottom);
	const FVector TopWorld(ComponentLocation.X, ComponentLocation.Y, FrameTop);

	Binding.LocalActorBottom = ComponentToWorld.InverseTransformPosition(BottomWorld).Z;
	Binding.LocalActorTop = ComponentToWorld.InverseTransformPosition(TopWorld).Z;
	Binding.LocalActorHeight = FMath::Max(FMath::Abs(Binding.LocalActorTop - Binding.LocalActorBottom), 1.0f);

	if (Binding.LocalActorTop < Binding.LocalActorBottom)
	{
		Swap(Binding.LocalActorBottom, Binding.LocalActorTop);
	}

	Binding.bHasStableLocalFrame = true;
}

void UDirtyPawnComponent::MarkParametersDirty()
{
	bParametersDirty = true;
	OnDirtyPawnStateChanged.Broadcast();
}

bool UDirtyPawnComponent::InterpScalar(float& Current, float Target, float DeltaSeconds, float Speed) const
{
	if (FMath::IsNearlyEqual(Current, Target, 0.001f))
	{
		Current = Target;
		return false;
	}

	Current = FMath::FInterpTo(Current, Target, DeltaSeconds, FMath::Max(Speed, 0.01f));
	return true;
}

void UDirtyPawnComponent::ApplyWashHeight(float NodeHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	MudWashHeight_Target = FMath::Max(MudWashHeight_Target, NodeHeight);

	if (bWashBlood)
	{
		BloodWashHeight_Target = FMath::Max(BloodWashHeight_Target, NodeHeight);
	}

	if (bWashSmears)
	{
		SmearWashHeightTarget = FMath::Max(SmearWashHeightTarget, NodeHeight);
	}

	if (bWashSandSnow)
	{
		SandWashHeight_Target = FMath::Max(SandWashHeight_Target, NodeHeight);
		SnowWashHeight_Target = FMath::Max(SnowWashHeight_Target, NodeHeight);
	}

	DirtWashHeight_Target = FMath::Max(DirtWashHeight_Target, NodeHeight);
	BurnWashHeight_Target = FMath::Max(BurnWashHeight_Target, NodeHeight);
}

void UDirtyPawnComponent::BeginTwoPhaseWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	const float DirtyAlphaBeforeWash = GetMaxWashableDirtyAlphaInRange(MinHeight, MaxHeight, bWashBlood, bWashSmears, bWashSandSnow);
	if (DirtyAlphaBeforeWash <= WashWetStartDirtyAlpha)
	{
		OverallFadeWetness_Target = 1.0f;
		L1_WaterHeight_Target = FMath::Max(L1_WaterHeight_Target, MaxHeight);
		L2_WaterHeight_Target = FMath::Max(L2_WaterHeight_Target, MaxHeight);
		ApplyWetBand(MinHeight, MaxHeight, 1.0f, CleanWaterWetFadeInSeconds);
		return;
	}

	QueueUnifiedWashBand(MinHeight, MaxHeight, bWashBlood, bWashSmears, bWashSandSnow);
}

void UDirtyPawnComponent::QueueTwoPhaseWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	if (!bWashFadeClockActive)
	{
		bWashFadeClockActive = true;
		WashFadeClockSeconds = 0.0f;
	}

	const float CoalesceSeconds = FMath::Max(WashContactCoalesceSeconds, 0.0f);
	if (CoalesceSeconds <= KINDA_SMALL_NUMBER)
	{
		BeginTwoPhaseWashBand(MinHeight, MaxHeight, bWashBlood, bWashSmears, bWashSandSnow);
		return;
	}

	for (FDirtyPawnPendingWashBand& Pending : PendingWashBands)
	{
		if (PaintBandsOverlap(Pending.MinHeight, Pending.MaxHeight, MinHeight, MaxHeight))
		{
			Pending.MinHeight = FMath::Min(Pending.MinHeight, MinHeight);
			Pending.MaxHeight = FMath::Max(Pending.MaxHeight, MaxHeight);
			Pending.CoalesceSeconds = FMath::Max(Pending.CoalesceSeconds, CoalesceSeconds);
			Pending.bWashBlood = Pending.bWashBlood || bWashBlood;
			Pending.bWashSmears = Pending.bWashSmears || bWashSmears;
			Pending.bWashSandSnow = Pending.bWashSandSnow || bWashSandSnow;
			return;
		}
	}

	FDirtyPawnPendingWashBand NewPending;
	NewPending.MinHeight = MinHeight;
	NewPending.MaxHeight = MaxHeight;
	NewPending.ElapsedSeconds = 0.0f;
	NewPending.CoalesceSeconds = CoalesceSeconds;
	NewPending.bWashBlood = bWashBlood;
	NewPending.bWashSmears = bWashSmears;
	NewPending.bWashSandSnow = bWashSandSnow;
	PendingWashBands.Add(NewPending);
}

void UDirtyPawnComponent::QueueUnifiedWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	float NormalizedMin = 0.0f;
	float NormalizedMax = 0.0f;
	if (!NormalizePaintBand(MinHeight, MaxHeight, NormalizedMin, NormalizedMax))
	{
		return;
	}

	for (FDirtyPawnWashBand& Band : WashPaintBands)
	{
		if (!PaintBandsOverlap(Band.MinHeight - PaintBandMergeTolerance, Band.MaxHeight + PaintBandMergeTolerance, NormalizedMin, NormalizedMax))
		{
			continue;
		}

		Band.MinHeight = FMath::Min(Band.MinHeight, NormalizedMin);
		Band.MaxHeight = FMath::Max(Band.MaxHeight, NormalizedMax);
		Band.TargetAlpha = 1.0f;
		Band.FadeDuration = FMath::Max(WashMaterialFadeSeconds, 0.01f);
		Band.Priority = ++NextPaintBandPriority;
		Band.bCommitted = false;
		Band.bWashBlood = Band.bWashBlood || bWashBlood;
		Band.bWashSmears = Band.bWashSmears || bWashSmears;
		Band.bWashSandSnow = Band.bWashSandSnow || bWashSandSnow;
		MarkParametersDirty();
		return;
	}

	FDirtyPawnWashBand NewBand;
	NewBand.MinHeight = NormalizedMin;
	NewBand.MaxHeight = NormalizedMax;
	NewBand.Alpha = 0.0f;
	NewBand.TargetAlpha = 1.0f;
	NewBand.FadeStartAlpha = NewBand.Alpha;
	NewBand.FadeElapsedSeconds = 0.0f;
	NewBand.FadeDuration = FMath::Max(WashMaterialFadeSeconds, 0.01f);
	NewBand.LastTargetAlpha = NewBand.TargetAlpha;
	NewBand.Priority = ++NextPaintBandPriority;
	NewBand.bCommitted = false;
	NewBand.bWashBlood = bWashBlood;
	NewBand.bWashSmears = bWashSmears;
	NewBand.bWashSandSnow = bWashSandSnow;
	WashPaintBands.Add(NewBand);

	WashPaintBands.Sort([](const FDirtyPawnWashBand& A, const FDirtyPawnWashBand& B)
	{
		return A.Priority > B.Priority;
	});

	while (WashPaintBands.Num() > MaxWashPaintBands)
	{
		WashPaintBands.RemoveAt(WashPaintBands.Num() - 1);
	}

	MarkParametersDirty();
}

bool UDirtyPawnComponent::UpdateUnifiedWashBands(float DeltaSeconds)
{
	bool bChanged = false;

	for (int32 Index = WashPaintBands.Num() - 1; Index >= 0; --Index)
	{
		FDirtyPawnWashBand& Band = WashPaintBands[Index];
		bChanged |= UpdateWashBandAlpha(Band, DeltaSeconds);

		if (!Band.bCommitted && Band.Alpha >= FMath::Clamp(UnifiedWashCommitThreshold, 0.0f, 1.0f))
		{
			CommitWashBand(Band);
			Band.bCommitted = true;
			Band.TargetAlpha = 0.0f;
			Band.FadeStartAlpha = Band.Alpha;
			Band.FadeElapsedSeconds = 0.0f;
			Band.FadeDuration = FMath::Max(UnifiedWashFadeOutSeconds, 0.01f);
			Band.LastTargetAlpha = Band.TargetAlpha;
			bChanged = true;
		}

		if (Band.bCommitted && Band.Alpha <= PaintBandVisibleThreshold && Band.TargetAlpha <= PaintBandVisibleThreshold)
		{
			WashPaintBands.RemoveAt(Index);
			bChanged = true;
		}
	}

	return bChanged;
}

void UDirtyPawnComponent::CommitWashBand(const FDirtyPawnWashBand& WashBand)
{
	ClearBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Mud, WashBand.MinHeight, WashBand.MaxHeight);
	if (WashBand.bWashSandSnow)
	{
		ClearBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Sand, WashBand.MinHeight, WashBand.MaxHeight);
		ClearBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Snow, WashBand.MinHeight, WashBand.MaxHeight);
	}

	if (WashBand.bWashBlood)
	{
		ClearBandRange(StainPaintBands, EDirtyPawnPaintState::Blood, WashBand.MinHeight, WashBand.MaxHeight);
	}
	if (WashBand.bWashSmears)
	{
	ClearBandRange(StainPaintBands, EDirtyPawnPaintState::Smear, WashBand.MinHeight, WashBand.MaxHeight);
	}
	ClearBandRange(StainPaintBands, EDirtyPawnPaintState::Dirt, WashBand.MinHeight, WashBand.MaxHeight);
	ClearBandRange(StainPaintBands, EDirtyPawnPaintState::Burn, WashBand.MinHeight, WashBand.MaxHeight);
	if (IsFullBodyWashBand(WashBand))
	{
		ClearSweatInternal(false);
	}

	OverallFadeWetness_Target = ActiveWaterSources.Num() > 0 ? 1.0f : OverallFadeWetness_Target;
	L1_WaterHeight_Target = FMath::Max(L1_WaterHeight_Target, WashBand.MaxHeight);
	L2_WaterHeight_Target = FMath::Max(L2_WaterHeight_Target, WashBand.MaxHeight);
	ApplyWetBand(WashBand.MinHeight, WashBand.MaxHeight, 1.0f, WashWetFadeInSeconds);

	MarkParametersDirty();
}

void UDirtyPawnComponent::ApplyWashBand(float NodeMinHeight, float NodeMaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow, float FadeDuration, float SyncedFadeElapsedSeconds)
{
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	const float EffectiveFadeDuration = FadeDuration > 0.0f ? FadeDuration : EnvironmentalFadeOutSeconds;

	FadeOutBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Mud, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);

	if (bWashSandSnow)
	{
		FadeOutBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Sand, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
		FadeOutBandRange(EnvironmentalPaintBands, EDirtyPawnPaintState::Snow, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
	}

	if (bWashBlood)
	{
		FadeOutBandRange(StainPaintBands, EDirtyPawnPaintState::Blood, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
	}

	if (bWashSmears)
	{
		FadeOutBandRange(StainPaintBands, EDirtyPawnPaintState::Smear, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
	}

	FadeOutBandRange(StainPaintBands, EDirtyPawnPaintState::Dirt, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
	FadeOutBandRange(StainPaintBands, EDirtyPawnPaintState::Burn, MinHeight, MaxHeight, EffectiveFadeDuration, SyncedFadeElapsedSeconds);
}

void UDirtyPawnComponent::QueuePendingWetBand(float MinHeight, float MaxHeight, float Strength, bool bWashBlood, bool bWashSmears, bool bWashSandSnow)
{
	for (FDirtyPawnPendingWetBand& Pending : PendingWetPaintBands)
	{
		if (PaintBandsOverlap(Pending.MinHeight, Pending.MaxHeight, MinHeight, MaxHeight))
		{
			Pending.MinHeight = FMath::Min(Pending.MinHeight, MinHeight);
			Pending.MaxHeight = FMath::Max(Pending.MaxHeight, MaxHeight);
			Pending.Strength = FMath::Max(Pending.Strength, Strength);
			Pending.MaxWaitSeconds = FMath::Max(Pending.MaxWaitSeconds, WashWetDelayMaxSeconds);
			Pending.bWashBlood = Pending.bWashBlood || bWashBlood;
			Pending.bWashSmears = Pending.bWashSmears || bWashSmears;
			Pending.bWashSandSnow = Pending.bWashSandSnow || bWashSandSnow;
			return;
		}
	}

	FDirtyPawnPendingWetBand NewPending;
	NewPending.MinHeight = MinHeight;
	NewPending.MaxHeight = MaxHeight;
	NewPending.Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	NewPending.ElapsedSeconds = 0.0f;
	NewPending.MaxWaitSeconds = FMath::Max(WashWetDelayMaxSeconds, 0.0f);
	NewPending.bWashBlood = bWashBlood;
	NewPending.bWashSmears = bWashSmears;
	NewPending.bWashSandSnow = bWashSandSnow;
	PendingWetPaintBands.Add(NewPending);
}

bool UDirtyPawnComponent::UpdatePendingWashBands(float DeltaSeconds)
{
	bool bChanged = false;

	for (int32 Index = PendingWashBands.Num() - 1; Index >= 0; --Index)
	{
		FDirtyPawnPendingWashBand& Pending = PendingWashBands[Index];
		Pending.ElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);
		if (Pending.ElapsedSeconds < Pending.CoalesceSeconds)
		{
			continue;
		}

		BeginTwoPhaseWashBand(Pending.MinHeight, Pending.MaxHeight, Pending.bWashBlood, Pending.bWashSmears, Pending.bWashSandSnow);
		PendingWashBands.RemoveAtSwap(Index);
		bChanged = true;
	}

	return bChanged;
}

bool UDirtyPawnComponent::UpdatePendingWetBands(float DeltaSeconds)
{
	bool bChanged = false;

	for (int32 Index = PendingWetPaintBands.Num() - 1; Index >= 0; --Index)
	{
		FDirtyPawnPendingWetBand& Pending = PendingWetPaintBands[Index];
		Pending.ElapsedSeconds += FMath::Max(DeltaSeconds, 0.0f);

		const float DirtyAlpha = GetMaxWashableDirtyAlphaInRange(Pending.MinHeight, Pending.MaxHeight, Pending.bWashBlood, Pending.bWashSmears, Pending.bWashSandSnow);
		if (DirtyAlpha > WashWetStartDirtyAlpha && Pending.ElapsedSeconds < Pending.MaxWaitSeconds)
		{
			continue;
		}

		OverallFadeWetness_Target = 1.0f;
		L1_WaterHeight_Target = FMath::Max(L1_WaterHeight_Target, Pending.MaxHeight);
		L2_WaterHeight_Target = FMath::Max(L2_WaterHeight_Target, Pending.MaxHeight);
		ApplyWetBand(Pending.MinHeight, Pending.MaxHeight, Pending.Strength, WashWetFadeInSeconds);
		PendingWetPaintBands.RemoveAtSwap(Index);
		bChanged = true;
	}

	return bChanged;
}

float UDirtyPawnComponent::GetMaxWashableDirtyAlphaInRange(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow) const
{
	float MaxAlpha = GetSweatVisualOpacity();

	auto IncludeEnvironmentalState = [bWashSandSnow](EDirtyPawnPaintState State)
	{
		return State == EDirtyPawnPaintState::Mud
			|| (bWashSandSnow && (State == EDirtyPawnPaintState::Sand || State == EDirtyPawnPaintState::Snow));
	};

	auto IncludeStainState = [bWashBlood, bWashSmears](EDirtyPawnPaintState State)
	{
		return (bWashBlood && State == EDirtyPawnPaintState::Blood)
			|| (bWashSmears && State == EDirtyPawnPaintState::Smear)
			|| State == EDirtyPawnPaintState::Dirt
			|| State == EDirtyPawnPaintState::Burn;
	};

	for (const FDirtyPawnPaintBand& Band : EnvironmentalPaintBands)
	{
		if (IncludeEnvironmentalState(Band.State) && PaintBandsOverlap(Band.MinHeight, Band.MaxHeight, MinHeight, MaxHeight))
		{
			MaxAlpha = FMath::Max(MaxAlpha, Band.Alpha);
		}
	}

	for (const FDirtyPawnPaintBand& Band : StainPaintBands)
	{
		if (IncludeStainState(Band.State) && PaintBandsOverlap(Band.MinHeight, Band.MaxHeight, MinHeight, MaxHeight))
		{
			MaxAlpha = FMath::Max(MaxAlpha, Band.Alpha);
		}
	}

	return MaxAlpha;
}

bool UDirtyPawnComponent::ApplySweatActivity(float GainPerSecond, float DeltaSeconds)
{
	if (GainPerSecond <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return false;
	}

	const float OldPoints = SweatPoints;
	const float OldVisualOpacity = GetSweatVisualOpacity();
	const float OldRoughnessAlpha = GetSweatRoughnessAlpha();
	const bool bOldPersistentFloorActive = bSweatPersistentFloorActive;
	const bool bOldSweatyStateActive = bSweatyStateActive;

	SweatMaxPoints = FMath::Max(SweatMaxPoints, 0.001f);
	SweatIdleSeconds = 0.0f;
	SweatPoints = FMath::Clamp(SweatPoints + GainPerSecond * DeltaSeconds, 0.0f, SweatMaxPoints);
	if (GetSweatNormalizedValue() >= FMath::Clamp(SweatPersistenceThreshold, 0.0f, 1.0f))
	{
		bSweatPersistentFloorActive = true;
	}
	UpdateSweatyStateFromSweat();

	return !FMath::IsNearlyEqual(OldPoints, SweatPoints, 0.001f)
		|| !FMath::IsNearlyEqual(OldVisualOpacity, GetSweatVisualOpacity(), 0.001f)
		|| !FMath::IsNearlyEqual(OldRoughnessAlpha, GetSweatRoughnessAlpha(), 0.001f)
		|| bOldPersistentFloorActive != bSweatPersistentFloorActive
		|| bOldSweatyStateActive != bSweatyStateActive;
}

bool UDirtyPawnComponent::UpdateSweatyStateFromSweat()
{
	if (!bSweatyStateActive && GetSweatNormalizedValue() >= 0.999f)
	{
		bSweatyStateActive = true;
		return true;
	}
	return false;
}

bool UDirtyPawnComponent::UpdateSweatState(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return false;
	}

	float GainPerSecond = 0.0f;
	AActor* Owner = GetOwner();
	if (Owner)
	{
		const FVector Velocity = Owner->GetVelocity();
		const float GroundSpeed = FVector2D(Velocity.X, Velocity.Y).Size();
		const float RunningThreshold = FMath::Max(SweatRunningSpeedThreshold, 0.0f);
		const bool bRunning = GroundSpeed >= RunningThreshold;

		bool bJumping = false;
		if (const ACharacter* CharacterOwner = Cast<ACharacter>(Owner))
		{
			if (const UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement())
			{
				bJumping = CharacterMovement->IsFalling();
			}
		}

		bool bAltRollActivity = false;
		if (const APawn* PawnOwner = Cast<APawn>(Owner))
		{
			if (const APlayerController* PlayerController = Cast<APlayerController>(PawnOwner->GetController()))
			{
				const bool bAltHeld = PlayerController->IsInputKeyDown(EKeys::LeftAlt)
					|| PlayerController->IsInputKeyDown(EKeys::RightAlt);
				const float MeaningfulSpeed = FMath::Max(RunningThreshold * 0.25f, 120.0f);
				bAltRollActivity = bAltHeld && GroundSpeed >= MeaningfulSpeed;
			}
		}

		if (bRunning || bJumping || bAltRollActivity)
		{
			GainPerSecond = FMath::Max(GainPerSecond, SweatMovementGainPerSecond);
		}
	}

	if (GainPerSecond > 0.0f)
	{
		return ApplySweatActivity(GainPerSecond, DeltaSeconds);
	}

	bool bChanged = false;
	if (SweatPoints > 0.0f)
	{
		SweatIdleSeconds += DeltaSeconds;
		if (SweatIdleSeconds >= FMath::Max(SweatDecayDelaySeconds, 0.0f) && SweatDecayPerSecond > 0.0f)
		{
			const float OldPoints = SweatPoints;
			const float OldVisualOpacity = GetSweatVisualOpacity();
			const float OldRoughnessAlpha = GetSweatRoughnessAlpha();

			SweatMaxPoints = FMath::Max(SweatMaxPoints, 0.001f);
			SweatPoints = FMath::Clamp(SweatPoints - SweatDecayPerSecond * DeltaSeconds, 0.0f, SweatMaxPoints);
			if (GetSweatNormalizedValue() >= FMath::Clamp(SweatPersistenceThreshold, 0.0f, 1.0f))
			{
				bSweatPersistentFloorActive = true;
			}

			bChanged = !FMath::IsNearlyEqual(OldPoints, SweatPoints, 0.001f)
				|| !FMath::IsNearlyEqual(OldVisualOpacity, GetSweatVisualOpacity(), 0.001f)
				|| !FMath::IsNearlyEqual(OldRoughnessAlpha, GetSweatRoughnessAlpha(), 0.001f);
		}
	}

	return bChanged;
}

void UDirtyPawnComponent::ClearSweatInternal(bool bMarkDirty)
{
	const bool bHadSweat = SweatPoints > 0.001f
		|| SweatIdleSeconds > 0.001f
		|| bSweatPersistentFloorActive
		|| bSweatyStateActive
		|| GetSweatVisualOpacity() > 0.001f
		|| GetSweatRoughnessAlpha() > 0.001f;

	SweatPoints = 0.0f;
	SweatIdleSeconds = 0.0f;
	bSweatPersistentFloorActive = false;
	bSweatyStateActive = false;

	if (bMarkDirty && bHadSweat)
	{
		MarkParametersDirty();
	}
}

void UDirtyPawnComponent::ApplyEnvironmentalBand(AActor* SourceActor, EDirtyPawnPaintState State, float NodeMinHeight, float NodeMaxHeight, float Strength, bool bInstant)
{
	(void)SourceActor;
	(void)bInstant;

	if (!IsEnvironmentalPaintState(State))
	{
		return;
	}

	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	const float ClampedStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	CommitEnvironmentalBand(State, MinHeight, MaxHeight, ClampedStrength);

	MarkParametersDirty();
}

void UDirtyPawnComponent::ApplyStainBand(EDirtyPawnPaintState State, float NodeMinHeight, float NodeMaxHeight, float Strength)
{
	if (!IsStainPaintState(State))
	{
		return;
	}

	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	AddOrRefreshBand(StainPaintBands, State, MinHeight, MaxHeight, FMath::Clamp(Strength, 0.0f, 1.0f), 1.0f, false);
	CompactPaintBands(StainPaintBands, MaxStainPaintBandsPerState * 4, true);
	MarkParametersDirty();
}

void UDirtyPawnComponent::ApplyWetBand(float NodeMinHeight, float NodeMaxHeight, float Strength, float FadeDuration)
{
	float MinHeight = 0.0f;
	float MaxHeight = 0.0f;
	if (!NormalizePaintBand(NodeMinHeight, NodeMaxHeight, MinHeight, MaxHeight))
	{
		return;
	}

	AddOrRefreshBand(WetPaintBands, EDirtyPawnPaintState::Wet, MinHeight, MaxHeight, FMath::Clamp(Strength, 0.0f, 1.0f), FMath::Max(FadeDuration, 0.01f), false);
	CompactPaintBands(WetPaintBands, MaxWetPaintBands, true);
}

void UDirtyPawnComponent::ApplyTemporaryWetBand(float MinHeight, float MaxHeight, float Strength, float FadeInSeconds, float HoldSeconds, float FadeOutSeconds)
{
	FDirtyPawnPaintBand* Band = AddOrRefreshBand(
		WetPaintBands,
		EDirtyPawnPaintState::Wet,
		MinHeight,
		MaxHeight,
		FMath::Clamp(Strength, 0.0f, 1.0f),
		FMath::Max(FadeInSeconds, 0.01f),
		true,
		FMath::Max(HoldSeconds, 0.0f),
		FMath::Max(FadeOutSeconds, 0.01f),
		false);

	if (Band)
	{
		Band->bAutoExpiredByTimer = false;
	}

	CompactPaintBands(WetPaintBands, MaxWetPaintBands, true);
	MarkParametersDirty();
}

bool UDirtyPawnComponent::NormalizePaintBand(float NodeMinHeight, float NodeMaxHeight, float& OutMinHeight, float& OutMaxHeight) const
{
	const float ReferenceHeight = GetDirtyPawnBodyReferenceHeight();
	const float MaximumBodyHeight = FMath::Max3(ReferenceHeight, StandingHeadHeight, CrouchHeadHeight) + 50.0f;
	const float Lower = FMath::Min(NodeMinHeight, NodeMaxHeight);
	const float Upper = FMath::Max(NodeMinHeight, NodeMaxHeight);

	if (Upper <= 0.0f || Lower >= MaximumBodyHeight)
	{
		return false;
	}

	OutMinHeight = FMath::Clamp(Lower, 0.0f, MaximumBodyHeight);
	OutMaxHeight = FMath::Clamp(Upper, 0.0f, MaximumBodyHeight);

	if (OutMaxHeight - OutMinHeight < MinimumPaintBandHeight)
	{
		OutMaxHeight = FMath::Min(OutMinHeight + FMath::Max(MinimumPaintBandHeight, 0.1f), MaximumBodyHeight);
	}

	return OutMaxHeight > OutMinHeight;
}

void UDirtyPawnComponent::CommitEnvironmentalBand(EDirtyPawnPaintState State, float MinHeight, float MaxHeight, float Strength)
{
	AddOrRefreshBand(
		EnvironmentalPaintBands,
		State,
		MinHeight,
		MaxHeight,
		Strength,
		EnvironmentalFadeInSeconds,
		true,
		-1.0f,
		-1.0f,
		State == EDirtyPawnPaintState::Snow);
	CompactPaintBands(EnvironmentalPaintBands, MaxEnvironmentalPaintBands, true);

	const float DirtCompanionStrength = FMath::Max(EnvironmentalDirtCompanionStrength, 0.65f);
	if ((State == EDirtyPawnPaintState::Mud || State == EDirtyPawnPaintState::Sand) && DirtCompanionStrength > PaintBandVisibleThreshold)
	{
		AddOrRefreshBand(
			StainPaintBands,
			EDirtyPawnPaintState::Dirt,
			MinHeight,
			MaxHeight,
			FMath::Clamp(Strength * DirtCompanionStrength, 0.0f, 1.0f),
			1.0f,
			false);
		CompactPaintBands(StainPaintBands, MaxStainPaintBandsPerState * 4, true);
	}
}

FDirtyPawnPaintBand* UDirtyPawnComponent::AddOrRefreshBand(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState State, float MinHeight, float MaxHeight, float Strength, float FadeDuration, bool bAutoExpires, float AutoFadeDelaySeconds, float AutoFadeOutSeconds, bool bSpawnSnowMeltWetOnExpire)
{
	for (FDirtyPawnPaintBand& Band : Bands)
	{
		if (Band.State != State)
		{
			continue;
		}

		if (PaintBandsOverlap(Band.MinHeight - PaintBandMergeTolerance, Band.MaxHeight + PaintBandMergeTolerance, MinHeight, MaxHeight))
		{
			Band.MinHeight = FMath::Min(Band.MinHeight, MinHeight);
			Band.MaxHeight = FMath::Max(Band.MaxHeight, MaxHeight);
			Band.TargetAlpha = FMath::Max(Band.TargetAlpha, Strength);
			Band.FadeDuration = FMath::Max(FadeDuration, 0.01f);
			Band.TimeSinceTouched = 0.0f;
			Band.AutoFadeDelaySeconds = AutoFadeDelaySeconds;
			Band.AutoFadeOutSeconds = AutoFadeOutSeconds;
			Band.bAutoExpires = bAutoExpires;
			Band.bSpawnSnowMeltWetOnExpire = bSpawnSnowMeltWetOnExpire;
			Band.bAutoExpiredByTimer = false;
			Band.Priority = ++NextPaintBandPriority;
			return &Band;
		}
	}

	FDirtyPawnPaintBand NewBand;
	NewBand.State = State;
	NewBand.MinHeight = MinHeight;
	NewBand.MaxHeight = MaxHeight;
	NewBand.Alpha = 0.0f;
	NewBand.TargetAlpha = Strength;
	NewBand.FadeDuration = FMath::Max(FadeDuration, 0.01f);
	NewBand.FadeStartAlpha = NewBand.Alpha;
	NewBand.FadeElapsedSeconds = 0.0f;
	NewBand.LastTargetAlpha = NewBand.TargetAlpha;
	NewBand.TimeSinceTouched = 0.0f;
	NewBand.AutoFadeDelaySeconds = AutoFadeDelaySeconds;
	NewBand.AutoFadeOutSeconds = AutoFadeOutSeconds;
	NewBand.Priority = ++NextPaintBandPriority;
	NewBand.bAutoExpires = bAutoExpires;
	NewBand.bSpawnSnowMeltWetOnExpire = bSpawnSnowMeltWetOnExpire;
	NewBand.bAutoExpiredByTimer = false;
	Bands.Add(NewBand);
	return &Bands.Last();
}

void UDirtyPawnComponent::FadeOutBandRange(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState StateFilter, float MinHeight, float MaxHeight, float FadeDuration, float SyncedFadeElapsedSeconds)
{
	TArray<FDirtyPawnPaintBand> SplitBands;
	const float EffectiveFadeDuration = FMath::Max(FadeDuration, 0.01f);
	const float SyncedElapsed = SyncedFadeElapsedSeconds >= 0.0f
		? FMath::Clamp(SyncedFadeElapsedSeconds, 0.0f, EffectiveFadeDuration)
		: -1.0f;

	for (FDirtyPawnPaintBand& Band : Bands)
	{
		if (StateFilter != EDirtyPawnPaintState::None && Band.State != StateFilter)
		{
			continue;
		}

		if (!PaintBandsOverlap(Band.MinHeight, Band.MaxHeight, MinHeight, MaxHeight))
		{
			continue;
		}

		const float OverlapMin = PaintBandOverlapMin(Band.MinHeight, MinHeight);
		const float OverlapMax = PaintBandOverlapMax(Band.MaxHeight, MaxHeight);
		const float OriginalMin = Band.MinHeight;
		const float OriginalMax = Band.MaxHeight;

		if (OriginalMin < OverlapMin - MinimumPaintBandHeight)
		{
			FDirtyPawnPaintBand Left = Band;
			Left.MinHeight = OriginalMin;
			Left.MaxHeight = OverlapMin;
			Left.Priority = ++NextPaintBandPriority;
			SplitBands.Add(Left);
		}

		if (OverlapMax < OriginalMax - MinimumPaintBandHeight)
		{
			FDirtyPawnPaintBand Right = Band;
			Right.MinHeight = OverlapMax;
			Right.MaxHeight = OriginalMax;
			Right.Priority = ++NextPaintBandPriority;
			SplitBands.Add(Right);
		}

		const bool bWasAlreadyFadingOut = Band.TargetAlpha <= PaintBandVisibleThreshold && Band.LastTargetAlpha <= PaintBandVisibleThreshold;

		Band.MinHeight = OverlapMin;
		Band.MaxHeight = OverlapMax;
		Band.TargetAlpha = 0.0f;
		Band.FadeDuration = EffectiveFadeDuration;
		Band.TimeSinceTouched = 0.0f;
		Band.bAutoExpiredByTimer = false;
		Band.bSpawnSnowMeltWetOnExpire = false;
		Band.Priority = ++NextPaintBandPriority;

		if (SyncedElapsed >= 0.0f)
		{
			if (!bWasAlreadyFadingOut)
			{
				Band.FadeStartAlpha = Band.Alpha;
				Band.FadeElapsedSeconds = 0.0f;
			}

			Band.LastTargetAlpha = 0.0f;
			Band.FadeElapsedSeconds = FMath::Max(Band.FadeElapsedSeconds, SyncedElapsed);
			Band.Alpha = EvaluateFadedAlpha(Band.FadeStartAlpha, Band.TargetAlpha, Band.FadeElapsedSeconds, Band.FadeDuration);
		}
	}

	Bands.Append(SplitBands);
}

void UDirtyPawnComponent::ClearBandRange(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState StateFilter, float MinHeight, float MaxHeight)
{
	TArray<FDirtyPawnPaintBand> KeptBands;
	KeptBands.Reserve(Bands.Num());

	for (const FDirtyPawnPaintBand& Band : Bands)
	{
		if (StateFilter != EDirtyPawnPaintState::None && Band.State != StateFilter)
		{
			KeptBands.Add(Band);
			continue;
		}

		if (!PaintBandsOverlap(Band.MinHeight, Band.MaxHeight, MinHeight, MaxHeight))
		{
			KeptBands.Add(Band);
			continue;
		}

		const float OverlapMin = PaintBandOverlapMin(Band.MinHeight, MinHeight);
		const float OverlapMax = PaintBandOverlapMax(Band.MaxHeight, MaxHeight);

		if (Band.MinHeight < OverlapMin - MinimumPaintBandHeight)
		{
			FDirtyPawnPaintBand Left = Band;
			Left.MaxHeight = OverlapMin;
			KeptBands.Add(Left);
		}

		if (OverlapMax < Band.MaxHeight - MinimumPaintBandHeight)
		{
			FDirtyPawnPaintBand Right = Band;
			Right.MinHeight = OverlapMax;
			KeptBands.Add(Right);
		}
	}

	Bands = MoveTemp(KeptBands);
}

bool UDirtyPawnComponent::UpdatePaintBands(float DeltaSeconds)
{
	bool bChanged = false;

	TArray<FDirtyPawnPaintBand> SnowMeltWetBands;

	auto UpdateBands = [this, DeltaSeconds, &bChanged, &SnowMeltWetBands](TArray<FDirtyPawnPaintBand>& Bands, bool bTrackSnowMelt)
	{
		for (FDirtyPawnPaintBand& Band : Bands)
		{
			if (Band.bAutoExpires && Band.TargetAlpha > PaintBandVisibleThreshold)
			{
				Band.TimeSinceTouched += DeltaSeconds;
				const float DelaySeconds = Band.AutoFadeDelaySeconds >= 0.0f ? Band.AutoFadeDelaySeconds : EnvironmentalAutoFadeDelaySeconds;
				const float FadeOutSeconds = Band.AutoFadeOutSeconds >= 0.0f ? Band.AutoFadeOutSeconds : EnvironmentalAutoFadeDurationSeconds;
				if (Band.TimeSinceTouched >= DelaySeconds)
				{
					Band.TargetAlpha = 0.0f;
					Band.FadeDuration = FMath::Max(FadeOutSeconds, 0.01f);
					Band.bAutoExpiredByTimer = true;
					if (bTrackSnowMelt && Band.State == EDirtyPawnPaintState::Snow && Band.bSpawnSnowMeltWetOnExpire)
					{
						SnowMeltWetBands.Add(Band);
						Band.bSpawnSnowMeltWetOnExpire = false;
					}
				}
			}

			bChanged |= UpdateBandAlpha(Band, DeltaSeconds);
		}

		for (int32 Index = Bands.Num() - 1; Index >= 0; --Index)
		{
			if (Bands[Index].Alpha <= PaintBandVisibleThreshold && Bands[Index].TargetAlpha <= PaintBandVisibleThreshold)
			{
				if (bTrackSnowMelt && Bands[Index].bAutoExpiredByTimer && Bands[Index].bSpawnSnowMeltWetOnExpire)
				{
					SnowMeltWetBands.Add(Bands[Index]);
				}
				Bands.RemoveAtSwap(Index);
				bChanged = true;
			}
		}
	};

	UpdateBands(EnvironmentalPaintBands, true);
	for (const FDirtyPawnPaintBand& SnowBand : SnowMeltWetBands)
	{
		ApplyTemporaryWetBand(
			SnowBand.MinHeight,
			SnowBand.MaxHeight,
			FMath::Max(SnowMeltWetStrength, 0.65f),
			FMath::Max(SnowMeltWetFadeInSeconds, 1.0f),
			FMath::Max(SnowMeltWetHoldSeconds, 10.0f),
			FMath::Max(SnowMeltWetFadeOutSeconds, 4.0f));
		bChanged = true;
	}
	UpdateBands(WetPaintBands, false);
	UpdateBands(StainPaintBands, false);

	CompactPaintBands(EnvironmentalPaintBands, MaxEnvironmentalPaintBands, true);
	CompactPaintBands(WetPaintBands, MaxWetPaintBands, true);
	CompactPaintBands(StainPaintBands, MaxStainPaintBandsPerState * 4, true);

	return bChanged;
}

void UDirtyPawnComponent::CompactPaintBands(TArray<FDirtyPawnPaintBand>& Bands, int32 MaxBands, bool bSortByPriority)
{
	if (&Bands == &EnvironmentalPaintBands)
	{
		Bands.Sort([](const FDirtyPawnPaintBand& A, const FDirtyPawnPaintBand& B)
		{
			return A.Priority > B.Priority;
		});

		int32 ActiveCount = 0;
		for (FDirtyPawnPaintBand& Band : Bands)
		{
			if (FMath::Max(Band.Alpha, Band.TargetAlpha) <= PaintBandVisibleThreshold)
			{
				continue;
			}

			++ActiveCount;
			if (ActiveCount > MaxBands && Band.TargetAlpha > PaintBandVisibleThreshold)
			{
				Band.TargetAlpha = 0.0f;
				Band.FadeDuration = FMath::Max(EnvironmentalFadeOutSeconds, 0.01f);
				Band.TimeSinceTouched = 0.0f;
				Band.bAutoExpiredByTimer = false;
				Band.bSpawnSnowMeltWetOnExpire = false;
			}
		}

		while (Bands.Num() > MaxBands + 3)
		{
			Bands.RemoveAt(Bands.Num() - 1);
		}
		return;
	}

	if (bSortByPriority)
	{
		Bands.Sort([](const FDirtyPawnPaintBand& A, const FDirtyPawnPaintBand& B)
		{
			if (!FMath::IsNearlyEqual(A.Alpha, B.Alpha, 0.001f))
			{
				return A.Alpha > B.Alpha;
			}
			return A.Priority > B.Priority;
		});
	}

	if (&Bands == &StainPaintBands)
	{
		for (EDirtyPawnPaintState State : { EDirtyPawnPaintState::Blood, EDirtyPawnPaintState::Smear, EDirtyPawnPaintState::Dirt, EDirtyPawnPaintState::Burn })
		{
			int32 Count = 0;
			for (int32 Index = 0; Index < Bands.Num(); ++Index)
			{
				if (Bands[Index].State != State)
				{
					continue;
				}

				++Count;
				if (Count > MaxStainPaintBandsPerState)
				{
					Bands.RemoveAt(Index);
					--Index;
				}
			}
		}
		return;
	}

	while (Bands.Num() > MaxBands)
	{
		Bands.RemoveAt(Bands.Num() - 1);
	}
}

void UDirtyPawnComponent::SyncLegacyScalarsFromBands()
{
	const float InactiveHeight = GetReferenceInactiveHeight();

	auto MaxHeightForState = [InactiveHeight](const TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState State, float& OutAlpha)
	{
		float MaxHeight = InactiveHeight;
		OutAlpha = 0.0f;
		for (const FDirtyPawnPaintBand& Band : Bands)
		{
			if (Band.State != State)
			{
				continue;
			}

			const float VisibleAlpha = FMath::Max(Band.Alpha, Band.TargetAlpha);
			if (VisibleAlpha <= PaintBandVisibleThreshold)
			{
				continue;
			}

			MaxHeight = FMath::Max(MaxHeight, Band.MaxHeight);
			OutAlpha = FMath::Max(OutAlpha, VisibleAlpha);
		}
		return MaxHeight;
	};

	float Alpha = 0.0f;
	MudHeight = MudHeight_Target = MaxHeightForState(EnvironmentalPaintBands, EDirtyPawnPaintState::Mud, Alpha);
	MudOpacity = Alpha;

	SandHeight = SandHeight_Target = MaxHeightForState(EnvironmentalPaintBands, EDirtyPawnPaintState::Sand, Alpha);
	OverallFadeSand = OverallFadeSand_Target = Alpha;

	SnowHeight = SnowHeight_Target = MaxHeightForState(EnvironmentalPaintBands, EDirtyPawnPaintState::Snow, Alpha);
	OverallFadeSnow = OverallFadeSnow_Target = Alpha;

	BloodHeight = BloodHeight_Target = MaxHeightForState(StainPaintBands, EDirtyPawnPaintState::Blood, Alpha);
	OverallFadeBlood = OverallFadeBlood_Target = Alpha;

	SmearHeight = SmearHeight_Target = MaxHeightForState(StainPaintBands, EDirtyPawnPaintState::Smear, Alpha);
	OverallFadeSmear = OverallFadeSmear_Target = Alpha;

	DirtHeight = DirtHeight_Target = MaxHeightForState(StainPaintBands, EDirtyPawnPaintState::Dirt, Alpha);
	OverallFadeDirt = OverallFadeDirt_Target = Alpha;

	BurnHeight = BurnHeight_Target = MaxHeightForState(StainPaintBands, EDirtyPawnPaintState::Burn, Alpha);
	OverallFadeBurn = OverallFadeBurn_Target = Alpha;

	float WetAlpha = 0.0f;
	const float WetHeight = MaxHeightForState(WetPaintBands, EDirtyPawnPaintState::Wet, WetAlpha);
	if (WetAlpha > PaintBandVisibleThreshold)
	{
		OverallFadeWetness = OverallFadeWetness_Target = WetAlpha;
		L1_WaterHeight = L1_WaterHeight_Target = WetHeight;
		L2_WaterHeight = L2_WaterHeight_Target = WetHeight;
	}
}

void UDirtyPawnComponent::PushPaintBandParameters(FDirtyPawnMaterialBinding& Binding) const
{
	if (!Binding.DynamicMaterial)
	{
		return;
	}

	const auto ToLocalHeight = [this, &Binding](float BodyLocalHeight)
	{
		const float ReferenceHeight = GetDirtyPawnBodyReferenceHeight();
		const float NormalizedHeight = BodyLocalHeight / ReferenceHeight;
		return Binding.LocalActorBottom + NormalizedHeight * Binding.LocalActorHeight;
	};

	const auto PushLayer = [&Binding, &ToLocalHeight](const TCHAR* Prefix, int32 Index, const FDirtyPawnPaintBand* Band)
	{
		const FString Base = FString::Printf(TEXT("%sLayer%d"), Prefix, Index);
		const float MinHeight = Band ? ToLocalHeight(Band->MinHeight) : -100000.0f;
		const float MaxHeight = Band ? ToLocalHeight(Band->MaxHeight) : -100000.0f;
		const float Alpha = Band ? FMath::Clamp(Band->Alpha, 0.0f, 1.0f) : 0.0f;

		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("MinLocalHeight"))), MinHeight);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("MaxLocalHeight"))), MaxHeight);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("Alpha"))), Alpha);
	};

	for (int32 Index = 0; Index < MaxEnvironmentalPaintBands; ++Index)
	{
		const FDirtyPawnPaintBand* Band = EnvironmentalPaintBands.IsValidIndex(Index) ? &EnvironmentalPaintBands[Index] : nullptr;
		PushLayer(TEXT("DPEnv"), Index, Band);
		const FString Base = FString::Printf(TEXT("DPEnvLayer%d"), Index);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("Mud"))), Band && Band->State == EDirtyPawnPaintState::Mud ? 1.0f : 0.0f);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("Sand"))), Band && Band->State == EDirtyPawnPaintState::Sand ? 1.0f : 0.0f);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("Snow"))), Band && Band->State == EDirtyPawnPaintState::Snow ? 1.0f : 0.0f);
	}

	for (int32 Index = 0; Index < MaxWetPaintBands; ++Index)
	{
		const FDirtyPawnPaintBand* Band = WetPaintBands.IsValidIndex(Index) ? &WetPaintBands[Index] : nullptr;
		PushLayer(TEXT("DPWet"), Index, Band);
	}

	for (int32 Index = 0; Index < MaxWashPaintBands; ++Index)
	{
		const FDirtyPawnWashBand* WashBand = WashPaintBands.IsValidIndex(Index) ? &WashPaintBands[Index] : nullptr;
		const FString Base = FString::Printf(TEXT("DPWashLayer%d"), Index);
		const float MinHeight = WashBand ? ToLocalHeight(WashBand->MinHeight) : -100000.0f;
		const float MaxHeight = WashBand ? ToLocalHeight(WashBand->MaxHeight) : -100000.0f;
		const float Alpha = WashBand ? FMath::Clamp(WashBand->Alpha, 0.0f, 1.0f) : 0.0f;

		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("MinLocalHeight"))), MinHeight);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("MaxLocalHeight"))), MaxHeight);
		Binding.DynamicMaterial->SetScalarParameterValue(FName(*(Base + TEXT("Alpha"))), Alpha);
	}

	for (EDirtyPawnPaintState State : { EDirtyPawnPaintState::Blood, EDirtyPawnPaintState::Smear, EDirtyPawnPaintState::Dirt, EDirtyPawnPaintState::Burn })
	{
		TArray<const FDirtyPawnPaintBand*> BandsForState;
		for (const FDirtyPawnPaintBand& Band : StainPaintBands)
		{
			if (Band.State == State)
			{
				BandsForState.Add(&Band);
			}
		}

		for (int32 Index = 0; Index < MaxStainPaintBandsPerState; ++Index)
		{
			const FDirtyPawnPaintBand* Band = BandsForState.IsValidIndex(Index) ? BandsForState[Index] : nullptr;
			PushLayer(PaintStateParameterStem(State), Index, Band);
		}
	}
}

float UDirtyPawnComponent::GetWashCoverageAlphaForRange(float MinHeight, float MaxHeight) const
{
	float RangeMin = 0.0f;
	float RangeMax = 0.0f;
	if (!NormalizePaintBand(MinHeight, MaxHeight, RangeMin, RangeMax))
	{
		return 0.0f;
	}

	TArray<float> Points;
	Points.Reserve(WashPaintBands.Num() * 2 + 2);
	Points.Add(RangeMin);
	Points.Add(RangeMax);
	for (const FDirtyPawnWashBand& WashBand : WashPaintBands)
	{
		if (WashBand.Alpha <= PaintBandVisibleThreshold)
		{
			continue;
		}

		if (!PaintBandsOverlap(WashBand.MinHeight, WashBand.MaxHeight, RangeMin, RangeMax))
		{
			continue;
		}

		Points.Add(FMath::Clamp(WashBand.MinHeight, RangeMin, RangeMax));
		Points.Add(FMath::Clamp(WashBand.MaxHeight, RangeMin, RangeMax));
	}

	Points.Sort();

	float MinCoverage = 1.0f;
	bool bAnySegment = false;
	for (int32 Index = 0; Index < Points.Num() - 1; ++Index)
	{
		const float SegmentMin = Points[Index];
		const float SegmentMax = Points[Index + 1];
		if (SegmentMax - SegmentMin <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float SegmentMid = (SegmentMin + SegmentMax) * 0.5f;
		float SegmentCoverage = 0.0f;
		for (const FDirtyPawnWashBand& WashBand : WashPaintBands)
		{
			if (WashBand.Alpha <= PaintBandVisibleThreshold)
			{
				continue;
			}

			if (SegmentMid >= WashBand.MinHeight - KINDA_SMALL_NUMBER && SegmentMid <= WashBand.MaxHeight + KINDA_SMALL_NUMBER)
			{
				SegmentCoverage = FMath::Max(SegmentCoverage, FMath::Clamp(WashBand.Alpha, 0.0f, 1.0f));
			}
		}

		MinCoverage = FMath::Min(MinCoverage, SegmentCoverage);
		bAnySegment = true;
	}

	return bAnySegment ? FMath::Clamp(MinCoverage, 0.0f, 1.0f) : 0.0f;
}

bool UDirtyPawnComponent::IsFullBodyWashBand(const FDirtyPawnWashBand& WashBand) const
{
	const float BodyTop = FMath::Max3(GetDirtyPawnBodyReferenceHeight(), StandingHeadHeight, CrouchHeadHeight);
	return WashBand.MinHeight <= MinimumPaintBandHeight
		&& WashBand.MaxHeight >= BodyTop - MinimumPaintBandHeight;
}

float UDirtyPawnComponent::NormalizeNodeHeight(float NodeHeight) const
{
	const float ReferenceHeight = GetDirtyPawnBodyReferenceHeight();
	const float MaximumBodyHeight = FMath::Max3(ReferenceHeight, StandingHeadHeight, CrouchHeadHeight) + 50.0f;
	return FMath::Clamp(NodeHeight, GetReferenceInactiveHeight(), MaximumBodyHeight);
}

float UDirtyPawnComponent::GetReferenceInactiveHeight() const
{
	return -100.0f;
}

float UDirtyPawnComponent::GetCrouchSubmergeBonus() const
{
	return FMath::Max(StandingHeadHeight - CrouchHeadHeight, 0.0f);
}

bool UDirtyPawnComponent::IsOwnerCrouched() const
{
	const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return false;
	}

	if (CharacterOwner->bIsCrouched)
	{
		return true;
	}

	const UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement();
	return CharacterMovement && CharacterMovement->IsCrouching();
}

bool UDirtyPawnComponent::HasVisibleState() const
{
	const float VisibleHeightThreshold = GetReferenceInactiveHeight() + 50.0f;
	const auto HasVisibleBand = [](const TArray<FDirtyPawnPaintBand>& Bands)
	{
		for (const FDirtyPawnPaintBand& Band : Bands)
		{
			if (Band.Alpha > PaintBandVisibleThreshold || Band.TargetAlpha > PaintBandVisibleThreshold)
			{
				return true;
			}
		}
		return false;
	};
	const auto HasVisibleWashBand = [](const TArray<FDirtyPawnWashBand>& Bands)
	{
		for (const FDirtyPawnWashBand& Band : Bands)
		{
			if (Band.Alpha > PaintBandVisibleThreshold || Band.TargetAlpha > PaintBandVisibleThreshold)
			{
				return true;
			}
		}
		return false;
	};

	return OverallFadeWetness > 0.001f
		|| MudHeight > VisibleHeightThreshold
		|| OverallFadeBlood > 0.001f
		|| OverallFadeSmear > 0.001f
		|| OverallFadeSand > 0.001f
		|| OverallFadeSnow > 0.001f
		|| OverallFadeDirt > 0.001f
		|| OverallFadeBurn > 0.001f
		|| GetSweatVisualOpacity() > 0.001f
		|| HasVisibleBand(EnvironmentalPaintBands)
		|| HasVisibleBand(WetPaintBands)
		|| HasVisibleBand(StainPaintBands)
		|| HasVisibleWashBand(WashPaintBands);
}

void UDirtyPawnComponent::DisableAsDuplicateDirtyPawnComponent()
{
	bAutoInitializeOnBeginPlay = false;
	SetComponentTickEnabled(false);
	ActiveWaterSources.Empty();

	if (GetLiveDirtyPawnMaterialBindingCount() <= 0)
	{
		MaterialBindings.Empty();
	}

	if (PrewarmState != EDirtyPawnPrewarmState::Ready || GetLiveDirtyPawnMaterialBindingCount() <= 0)
	{
		PrewarmState = EDirtyPawnPrewarmState::Failed;
	}

	if (bLogSetupWarnings)
	{
		UE_LOG(LogTemp, Display, TEXT("[DirtyPawnRuntime] Disabled duplicate component %s on %s"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
	}
}

void UDirtyPawnComponent::DisableNonCanonicalDirtyPawnComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UDirtyPawnComponent*> Components;
	Owner->GetComponents<UDirtyPawnComponent>(Components);
	for (UDirtyPawnComponent* Component : Components)
	{
		if (Component && Component != this && IsValidDirtyPawnCandidate(Component))
		{
			Component->DisableAsDuplicateDirtyPawnComponent();
		}
	}
}
