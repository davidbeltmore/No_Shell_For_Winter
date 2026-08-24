#include "Survival/ProjectSurvivalStatusComponent.h"

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Engine/Texture2D.h"
#include "Math/RotationMatrix.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectSurvivalAttributeBridgeComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"
#include "Survival/ProjectSurvivalStatusSettings.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Locomotion/ProjectMovementModifierTags.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr float ProjectSurvivalStatusNeedEmptyThreshold = KINDA_SMALL_NUMBER;
	constexpr float ProjectSurvivalStatusHealthWriteTolerance = 0.01f;
	constexpr int32 ProjectSurvivalProceduralIconSize = 256;

	FString NormalizeStatusPropertyName(const FString& Value)
	{
		FString Normalized;
		Normalized.Reserve(Value.Len());
		for (const TCHAR Character : Value)
		{
			if (FChar::IsAlnum(Character))
			{
				Normalized.AppendChar(FChar::ToLower(Character));
			}
		}

		return Normalized;
	}

	bool IsGameplayAttributeDataProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == FGameplayAttributeData::StaticStruct();
	}

	bool PropertyNameMatches(const FName PropertyName, const TArray<FString>& CandidateNames)
	{
		const FString NormalizedPropertyName = NormalizeStatusPropertyName(PropertyName.ToString());
		for (const FString& CandidateName : CandidateNames)
		{
			if (NormalizedPropertyName == NormalizeStatusPropertyName(CandidateName))
			{
				return true;
			}
		}

		return false;
	}

	TArray<FString> GetHealthCandidateNames()
	{
		return {
			TEXT("Health"),
			TEXT("CurrentHealth"),
			TEXT("CurrentHP"),
			TEXT("HitPoints"),
			TEXT("HitPointsCurrent"),
			TEXT("HP")
		};
	}

	bool ClassLineageContainsAnyHint(const UClass* ActorClass, const TArray<FString>& Hints)
	{
		if (!ActorClass)
		{
			return false;
		}

		for (const UClass* CurrentClass = ActorClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			const FString ClassName = CurrentClass->GetName();
			for (const FString& Hint : Hints)
			{
				if (!Hint.IsEmpty() && ClassName.Contains(Hint, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}

		return false;
	}

	float GetCurrentWorldTimeSeconds(const UObject* WorldContextObject)
	{
		const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.f;
	}

	float ResolveInvertedMovementInput(const float RawInput)
	{
		return -RawInput;
	}

	bool EvaluateThresholdStatus(
		const bool bWasActive,
		const float NormalizedValue,
		const EProjectSurvivalStatusThresholdMode ThresholdMode,
		const float ActivationThresholdNormalized,
		const float DeactivationThresholdNormalized)
	{
		const float Value = FMath::Clamp(NormalizedValue, 0.f, 1.f);
		const float ActivationThreshold = FMath::Clamp(ActivationThresholdNormalized, 0.f, 1.f);
		const float DeactivationThreshold = FMath::Clamp(DeactivationThresholdNormalized, 0.f, 1.f);
		if (ThresholdMode == EProjectSurvivalStatusThresholdMode::AtOrAbove)
		{
			return bWasActive ? Value > DeactivationThreshold : Value >= ActivationThreshold;
		}

		return bWasActive ? Value < DeactivationThreshold : Value <= ActivationThreshold;
	}

	void SetPixel(TArray<FColor>& Pixels, const int32 Size, const int32 X, const int32 Y, const FColor Color)
	{
		if (X < 0 || Y < 0 || X >= Size || Y >= Size)
		{
			return;
		}

		Pixels[(Y * Size) + X] = Color;
	}

	void DrawFilledCircle(TArray<FColor>& Pixels, const int32 Size, const FVector2f Center, const float Radius, const FColor Color)
	{
		const float RadiusSquared = Radius * Radius;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f PixelCenter(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				if ((PixelCenter - Center).SizeSquared() <= RadiusSquared)
				{
					SetPixel(Pixels, Size, X, Y, Color);
				}
			}
		}
	}

	void DrawFilledEllipse(
		TArray<FColor>& Pixels,
		const int32 Size,
		const FVector2f Center,
		const float RadiusX,
		const float RadiusY,
		const FColor Color)
	{
		const float SafeRadiusX = FMath::Max(1.f, RadiusX);
		const float SafeRadiusY = FMath::Max(1.f, RadiusY);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f PixelCenter(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				const float NormalizedX = (PixelCenter.X - Center.X) / SafeRadiusX;
				const float NormalizedY = (PixelCenter.Y - Center.Y) / SafeRadiusY;
				if ((NormalizedX * NormalizedX) + (NormalizedY * NormalizedY) <= 1.f)
				{
					SetPixel(Pixels, Size, X, Y, Color);
				}
			}
		}
	}

	void ClearFilledCircle(TArray<FColor>& Pixels, const int32 Size, const FVector2f Center, const float Radius)
	{
		const float RadiusSquared = Radius * Radius;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f PixelCenter(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				if ((PixelCenter - Center).SizeSquared() <= RadiusSquared)
				{
					SetPixel(Pixels, Size, X, Y, FColor(0, 0, 0, 0));
				}
			}
		}
	}

	float DistancePointToSegment(const FVector2f Point, const FVector2f SegmentStart, const FVector2f SegmentEnd)
	{
		const FVector2f Segment = SegmentEnd - SegmentStart;
		const float SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= KINDA_SMALL_NUMBER)
		{
			return (Point - SegmentStart).Size();
		}

		const float T = FMath::Clamp(FVector2f::DotProduct(Point - SegmentStart, Segment) / SegmentLengthSquared, 0.f, 1.f);
		return (Point - (SegmentStart + (Segment * T))).Size();
	}

	void DrawLine(TArray<FColor>& Pixels, const int32 Size, const FVector2f Start, const FVector2f End, const float Thickness, const FColor Color)
	{
		const float HalfThickness = Thickness * 0.5f;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f PixelCenter(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				if (DistancePointToSegment(PixelCenter, Start, End) <= HalfThickness)
				{
					SetPixel(Pixels, Size, X, Y, Color);
				}
			}
		}
	}

	float Sign2D(const FVector2f PointA, const FVector2f PointB, const FVector2f PointC)
	{
		return (PointA.X - PointC.X) * (PointB.Y - PointC.Y) - (PointB.X - PointC.X) * (PointA.Y - PointC.Y);
	}

	void DrawFilledTriangle(
		TArray<FColor>& Pixels,
		const int32 Size,
		const FVector2f PointA,
		const FVector2f PointB,
		const FVector2f PointC,
		const FColor Color)
	{
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FVector2f PixelCenter(static_cast<float>(X) + 0.5f, static_cast<float>(Y) + 0.5f);
				const bool b1 = Sign2D(PixelCenter, PointA, PointB) < 0.f;
				const bool b2 = Sign2D(PixelCenter, PointB, PointC) < 0.f;
				const bool b3 = Sign2D(PixelCenter, PointC, PointA) < 0.f;
				if ((b1 == b2) && (b2 == b3))
				{
					SetPixel(Pixels, Size, X, Y, Color);
				}
			}
		}
	}

	FVector2f ToPixels(const float X, const float Y, const int32 Size)
	{
		return FVector2f(X * static_cast<float>(Size), Y * static_cast<float>(Size));
	}

	void DrawDropIcon(TArray<FColor>& Pixels, const int32 Size, const FVector2f Center, const float Radius, const FColor Color)
	{
		const FVector2f Top = Center + FVector2f(0.f, -(Radius * 1.4f));
		const FVector2f Left = Center + FVector2f(-(Radius * 0.82f), -(Radius * 0.2f));
		const FVector2f Right = Center + FVector2f(Radius * 0.82f, -(Radius * 0.2f));
		DrawFilledTriangle(Pixels, Size, Top, Left, Right, Color);
		DrawFilledCircle(Pixels, Size, Center + FVector2f(0.f, Radius * 0.20f), Radius, Color);
	}

	void DrawMinimalStatusIcon(TArray<FColor>& Pixels, const int32 Size, const FName MinimalIconName)
	{
		const FColor White(255, 255, 255, 255);
		Pixels.Init(FColor(0, 0, 0, 0), Size * Size);

		if (MinimalIconName == TEXT("Status.Thirst"))
		{
			DrawDropIcon(Pixels, Size, ToPixels(0.5f, 0.56f, Size), Size * 0.18f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Bleeding"))
		{
			DrawDropIcon(Pixels, Size, ToPixels(0.42f, 0.48f, Size), Size * 0.16f, White);
			DrawDropIcon(Pixels, Size, ToPixels(0.65f, 0.68f, Size), Size * 0.09f, White);
			DrawLine(Pixels, Size, ToPixels(0.26f, 0.78f, Size), ToPixels(0.74f, 0.78f, Size), Size * 0.08f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Exhausted"))
		{
			DrawFilledCircle(Pixels, Size, ToPixels(0.50f, 0.50f, Size), Size * 0.23f, White);
			ClearFilledCircle(Pixels, Size, ToPixels(0.60f, 0.43f, Size), Size * 0.21f);
			DrawFilledCircle(Pixels, Size, ToPixels(0.73f, 0.30f, Size), Size * 0.04f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Dizzy"))
		{
			DrawLine(Pixels, Size, ToPixels(0.30f, 0.32f, Size), ToPixels(0.70f, 0.32f, Size), Size * 0.07f, White);
			DrawLine(Pixels, Size, ToPixels(0.36f, 0.52f, Size), ToPixels(0.80f, 0.52f, Size), Size * 0.07f, White);
			DrawLine(Pixels, Size, ToPixels(0.22f, 0.72f, Size), ToPixels(0.64f, 0.72f, Size), Size * 0.07f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.22f, 0.32f, Size), Size * 0.05f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.82f, 0.52f, Size), Size * 0.05f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.68f, 0.72f, Size), Size * 0.05f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Fear"))
		{
			DrawLine(Pixels, Size, ToPixels(0.22f, 0.50f, Size), ToPixels(0.42f, 0.34f, Size), Size * 0.07f, White);
			DrawLine(Pixels, Size, ToPixels(0.42f, 0.34f, Size), ToPixels(0.78f, 0.50f, Size), Size * 0.07f, White);
			DrawLine(Pixels, Size, ToPixels(0.22f, 0.50f, Size), ToPixels(0.42f, 0.66f, Size), Size * 0.07f, White);
			DrawLine(Pixels, Size, ToPixels(0.42f, 0.66f, Size), ToPixels(0.78f, 0.50f, Size), Size * 0.07f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.50f, 0.50f, Size), Size * 0.08f, White);
			DrawLine(Pixels, Size, ToPixels(0.30f, 0.20f, Size), ToPixels(0.56f, 0.14f, Size), Size * 0.06f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Starving"))
		{
			DrawLine(Pixels, Size, ToPixels(0.34f, 0.18f, Size), ToPixels(0.34f, 0.80f, Size), Size * 0.06f, White);
			DrawLine(Pixels, Size, ToPixels(0.24f, 0.18f, Size), ToPixels(0.24f, 0.40f, Size), Size * 0.04f, White);
			DrawLine(Pixels, Size, ToPixels(0.34f, 0.18f, Size), ToPixels(0.34f, 0.40f, Size), Size * 0.04f, White);
			DrawLine(Pixels, Size, ToPixels(0.44f, 0.18f, Size), ToPixels(0.44f, 0.40f, Size), Size * 0.04f, White);
			DrawLine(Pixels, Size, ToPixels(0.66f, 0.18f, Size), ToPixels(0.66f, 0.80f, Size), Size * 0.06f, White);
			DrawFilledTriangle(Pixels, Size, ToPixels(0.56f, 0.18f, Size), ToPixels(0.76f, 0.18f, Size), ToPixels(0.66f, 0.42f, Size), White);
			return;
		}

		if (MinimalIconName == TEXT("Status.WellFed"))
		{
			DrawLine(Pixels, Size, ToPixels(0.22f, 0.48f, Size), ToPixels(0.78f, 0.48f, Size), Size * 0.055f, White);
			DrawFilledEllipse(Pixels, Size, ToPixels(0.50f, 0.57f, Size), Size * 0.25f, Size * 0.14f, White);
			DrawLine(Pixels, Size, ToPixels(0.36f, 0.76f, Size), ToPixels(0.64f, 0.76f, Size), Size * 0.05f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Alcoholized"))
		{
			DrawLine(Pixels, Size, ToPixels(0.43f, 0.18f, Size), ToPixels(0.57f, 0.18f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.46f, 0.20f, Size), ToPixels(0.46f, 0.35f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.54f, 0.20f, Size), ToPixels(0.54f, 0.35f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.46f, 0.35f, Size), ToPixels(0.34f, 0.48f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.54f, 0.35f, Size), ToPixels(0.66f, 0.48f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.34f, 0.48f, Size), ToPixels(0.34f, 0.82f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.66f, 0.48f, Size), ToPixels(0.66f, 0.82f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.34f, 0.82f, Size), ToPixels(0.66f, 0.82f, Size), Size * 0.055f, White);
			return;
		}

		if (MinimalIconName == TEXT("Status.Dirty"))
		{
			DrawFilledEllipse(Pixels, Size, ToPixels(0.50f, 0.52f, Size), Size * 0.27f, Size * 0.39f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.37f, 0.33f, Size), Size * 0.10f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.63f, 0.69f, Size), Size * 0.08f, White);
			DrawFilledCircle(Pixels, Size, ToPixels(0.57f, 0.27f, Size), Size * 0.055f, White);
			DrawLine(Pixels, Size, ToPixels(0.40f, 0.24f, Size), ToPixels(0.59f, 0.80f, Size), Size * 0.055f, White);
			ClearFilledCircle(Pixels, Size, ToPixels(0.58f, 0.43f, Size), Size * 0.08f);
			return;
		}

		DrawFilledCircle(Pixels, Size, ToPixels(0.5f, 0.5f, Size), Size * 0.20f, White);
	}
}

UProjectSurvivalStatusComponent::UProjectSurvivalStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UProjectSurvivalStatusComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveDependencies();
	InitializeRuntimeStates();
	UpdateStatuses(0.f);
}

void UProjectSurvivalStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindDamageObserver();

	if (AttributeBridgeComponent)
	{
		for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
		{
			if (Pair.Value.AttributeModifiers.Num() > 0)
			{
				AttributeBridgeComponent->ClearAllExternalAttributeMultipliersForSource(Pair.Key);
			}
		}
	}

	if (LocomotionOverrideComponent)
	{
		LocomotionOverrideComponent->ClearMovementSpeedModifiers(EProjectMovementModifierLayer::StatusPenalty);
	}

	ApplyHealthRecoveryBlock(false);

	if (bExhaustionSequenceActive)
	{
		bExhaustionSequenceActive = false;
		OnBlackoutChanged.Broadcast(false);
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController)
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
	}

	bInvertedMovementInputApplied = false;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UProjectSurvivalStatusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (NeedsDependencyRefresh())
	{
		ResolveDependencies();
	}

	if (!bRuntimeStatesInitialized)
	{
		InitializeRuntimeStates();
	}

	UpdateStatuses(DeltaTime);
}

void UProjectSurvivalStatusComponent::ForceRefresh()
{
	ResolveDependencies();
	InitializeRuntimeStates();
	UpdateStatuses(0.f);
}

bool UProjectSurvivalStatusComponent::ApplyStatus(const FName StatusName, const float DurationOverride, AActor* SourceActor)
{
	if (IsStatusImmune(StatusName))
	{
		ClearStatus(StatusName);
		return false;
	}

	const bool bApplied = ApplyTimedStatusInstance(StatusName, DurationOverride, SourceActor, false);
	if (bApplied)
	{
		UpdateStatuses(0.f);
	}

	return bApplied;
}

void UProjectSurvivalStatusComponent::ClearStatus(const FName StatusName)
{
	if (StatusName.IsNone())
	{
		return;
	}

	ForcedActiveStatusNames.Remove(StatusName);
	DebugAppliedStatusNames.Remove(StatusName);
	DebugBypassImmunityStatusNames.Remove(StatusName);
	if (CurrentDebugCycleStatusName == StatusName)
	{
		CurrentDebugCycleStatusName = NAME_None;
	}

	ClearTimedStatusInstance(StatusName, true, true);
	UpdateStatuses(0.f);
}

void UProjectSurvivalStatusComponent::ClearAllDebugStatuses()
{
	TArray<FName> DebugStatusNames = DebugAppliedStatusNames.Array();
	for (const FName StatusName : DebugStatusNames)
	{
		ClearTimedStatusInstance(StatusName, false, true);
		ForcedActiveStatusNames.Remove(StatusName);
		DebugBypassImmunityStatusNames.Remove(StatusName);
	}

	DebugAppliedStatusNames.Reset();
	DebugBypassImmunityStatusNames.Reset();
	CurrentDebugCycleStatusName = NAME_None;
	UpdateStatuses(0.f);
}

bool UProjectSurvivalStatusComponent::CycleDebugStatus()
{
	const TArray<FName>& DebugCycleStatusNames = GetProjectSurvivalStatusCatalog().DebugCycleStatusNames;
	if (DebugCycleStatusNames.Num() == 0)
	{
		return false;
	}

	int32 NextIndex = 0;
	if (!CurrentDebugCycleStatusName.IsNone())
	{
		const int32 CurrentIndex = DebugCycleStatusNames.IndexOfByKey(CurrentDebugCycleStatusName);
		if (CurrentIndex != INDEX_NONE)
		{
			NextIndex = CurrentIndex + 1;
		}
	}

	ClearAllDebugStatuses();

	if (!DebugCycleStatusNames.IsValidIndex(NextIndex))
	{
		return false;
	}

	const FName NextStatusName = DebugCycleStatusNames[NextIndex];
	const bool bApplied = ApplyTimedStatusInstance(NextStatusName, -1.f, GetOwner(), true);
	if (bApplied)
	{
		DebugAppliedStatusNames.Add(NextStatusName);
		CurrentDebugCycleStatusName = NextStatusName;
	}

	UpdateStatuses(0.f);
	return bApplied;
}

bool UProjectSurvivalStatusComponent::ApplyDebugStatus(const FName StatusName, const bool bBypassImmunity)
{
	if (StatusName.IsNone())
	{
		return false;
	}

	TArray<FName> SingleStatus;
	SingleStatus.Add(StatusName);
	return ApplyDebugStatuses(SingleStatus, bBypassImmunity);
}

bool UProjectSurvivalStatusComponent::ApplyDebugStatuses(const TArray<FName>& StatusNames, const bool bBypassImmunity)
{
	if (StatusNames.IsEmpty())
	{
		return false;
	}

	ForceRefresh();

	TArray<FName> AppliedStatusNames;
	AppliedStatusNames.Reserve(StatusNames.Num());
	for (const FName StatusName : StatusNames)
	{
		if (StatusName.IsNone() || !StatusDefinitionsByName.Contains(StatusName))
		{
			continue;
		}

		ForcedActiveStatusNames.Add(StatusName);
		DebugAppliedStatusNames.Add(StatusName);
		if (bBypassImmunity)
		{
			DebugBypassImmunityStatusNames.Add(StatusName);
		}
		else
		{
			DebugBypassImmunityStatusNames.Remove(StatusName);
		}
		AppliedStatusNames.AddUnique(StatusName);
	}

	if (AppliedStatusNames.IsEmpty())
	{
		return false;
	}

	UpdateStatuses(0.f);

	for (const FName StatusName : AppliedStatusNames)
	{
		if (IsStatusActive(StatusName))
		{
			return true;
		}
	}
	return false;
}

void UProjectSurvivalStatusComponent::SetForcedStatusActive(FName StatusName, const bool bActive)
{
	if (StatusName.IsNone())
	{
		return;
	}

	if (bActive && IsStatusImmune(StatusName))
	{
		ClearStatus(StatusName);
		return;
	}

	if (bActive)
	{
		ForcedActiveStatusNames.Add(StatusName);
	}
	else
	{
		ForcedActiveStatusNames.Remove(StatusName);
		DebugBypassImmunityStatusNames.Remove(StatusName);
	}

	UpdateStatuses(0.f);
}

void UProjectSurvivalStatusComponent::SetStatusImmunitySource(const FName SourceId, const TArray<FName>& StatusNames)
{
	if (SourceId.IsNone())
	{
		return;
	}

	TSet<FName> StatusesToRefresh;
	if (const TSet<FName>* ExistingStatuses = StatusImmunitiesBySource.Find(SourceId))
	{
		for (const FName StatusName : *ExistingStatuses)
		{
			StatusesToRefresh.Add(StatusName);
		}
	}

	TSet<FName> CleanStatusNames;
	for (const FName StatusName : StatusNames)
	{
		if (!StatusName.IsNone())
		{
			CleanStatusNames.Add(StatusName);
			StatusesToRefresh.Add(StatusName);
		}
	}

	if (CleanStatusNames.Num() > 0)
	{
		StatusImmunitiesBySource.Add(SourceId, CleanStatusNames);
	}
	else
	{
		StatusImmunitiesBySource.Remove(SourceId);
	}

	for (const FName StatusName : StatusesToRefresh)
	{
		if (!IsStatusImmune(StatusName))
		{
			continue;
		}

		ForcedActiveStatusNames.Remove(StatusName);
		DebugAppliedStatusNames.Remove(StatusName);
		DebugBypassImmunityStatusNames.Remove(StatusName);
		if (CurrentDebugCycleStatusName == StatusName)
		{
			CurrentDebugCycleStatusName = NAME_None;
		}
		ClearTimedStatusInstance(StatusName, true, true);
	}

	UpdateStatuses(0.f);
}

void UProjectSurvivalStatusComponent::ClearStatusImmunitySource(const FName SourceId)
{
	if (SourceId.IsNone() || !StatusImmunitiesBySource.Contains(SourceId))
	{
		return;
	}

	StatusImmunitiesBySource.Remove(SourceId);
	UpdateStatuses(0.f);
}

void UProjectSurvivalStatusComponent::TriggerExhaustionSequence(const float DurationOverrideSeconds)
{
	StartExhaustionSequence(DurationOverrideSeconds);
	UpdateStatuses(0.f);
}

bool UProjectSurvivalStatusComponent::IsStatusActive(FName StatusName) const
{
	return ActiveStatusByName.FindRef(StatusName);
}

bool UProjectSurvivalStatusComponent::IsStatusImmune(const FName StatusName) const
{
	if (StatusName.IsNone())
	{
		return false;
	}

	for (const TPair<FName, TSet<FName>>& Pair : StatusImmunitiesBySource)
	{
		if (Pair.Value.Contains(StatusName))
		{
			return true;
		}
	}

	return false;
}

bool UProjectSurvivalStatusComponent::IsBlackoutActive() const
{
	return bExhaustionSequenceActive;
}

bool UProjectSurvivalStatusComponent::IsHealthRecoveryBlocked() const
{
	return bHealthRecoveryBlocked;
}

float UProjectSurvivalStatusComponent::GetNeedDecayMultiplier(const FName NeedName) const
{
	if (NeedName.IsNone())
	{
		return 1.f;
	}

	float CombinedMultiplier = 1.f;
	for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
	{
		if (!ActiveStatusByName.FindRef(Pair.Key))
		{
			continue;
		}

		for (const FProjectSurvivalStatusNeedDecayModifier& Modifier : Pair.Value.NeedDecayModifiers)
		{
			if (Modifier.NeedName == NeedName)
			{
				CombinedMultiplier *= FMath::Max(0.f, Modifier.DecayMultiplier);
			}
		}
	}

	return CombinedMultiplier;
}

float UProjectSurvivalStatusComponent::GetSensationDeltaPerSecond(const FName SensationName) const
{
	if (SensationName.IsNone())
	{
		return 0.f;
	}

	float DeltaPerSecond = 0.f;
	for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
	{
		if (!ActiveStatusByName.FindRef(Pair.Key))
		{
			continue;
		}

		for (const FProjectSurvivalStatusSensationModifier& Modifier : Pair.Value.SensationModifiers)
		{
			if (Modifier.SensationName == SensationName)
			{
				DeltaPerSecond += Modifier.DeltaPerSecond;
			}
		}
	}

	return DeltaPerSecond;
}

bool UProjectSurvivalStatusComponent::HasResolvedHealthBinding() const
{
	return ResolvedHealthBinding.bResolved && (ResolvedHealthBinding.bGameplayAttributeData || ResolvedHealthBinding.bFloatProperty);
}

float UProjectSurvivalStatusComponent::GetCurrentHealthValue() const
{
	return ReadCurrentHealthValue();
}

FString UProjectSurvivalStatusComponent::GetResolvedHealthPropertyName() const
{
	return ResolvedHealthBinding.ResolvedPropertyName.ToString();
}

TArray<FProjectSurvivalStatusSnapshot> UProjectSurvivalStatusComponent::BuildActiveStatusSnapshots() const
{
	TArray<FProjectSurvivalStatusSnapshot> Snapshots;

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return Snapshots;
	}

	TArray<FProjectSurvivalStatusDefinition> LocalDefinitions;
	const TArray<FProjectSurvivalStatusDefinition>* Definitions = &RuntimeStatusDefinitions;
	if (!bRuntimeStatesInitialized)
	{
		LocalDefinitions = Settings->BuildResolvedStatusDefinitions();
		Definitions = &LocalDefinitions;
	}

	const float CurrentTimeSeconds = GetCurrentWorldTimeSeconds(this);
	for (const FProjectSurvivalStatusDefinition& Definition : *Definitions)
	{
		if (Definition.StatusName.IsNone() || !ActiveStatusByName.FindRef(Definition.StatusName))
		{
			continue;
		}

		float RemainingDurationSeconds = 0.f;
		const bool bTimed = IsTimedStatusActive(Definition.StatusName, CurrentTimeSeconds, &RemainingDurationSeconds);

		FProjectSurvivalStatusSnapshot Snapshot;
		Snapshot.StatusName = Definition.StatusName;
		Snapshot.DisplayName = Definition.DisplayName;
		Snapshot.Description = Definition.Description;
		Snapshot.SourceNeedName = Definition.SourceNeedName;
		Snapshot.SourceEntryName = Definition.SourceEntryName;
		Snapshot.SourceType = Definition.SourceType;
		Snapshot.MinimalIconName = Definition.MinimalIconName;
		Snapshot.DamagePerSecond = Definition.DamagePerSecond;
		Snapshot.RemainingDurationSeconds = RemainingDurationSeconds;
		Snapshot.bBlocksHealthRecovery = Definition.bBlocksHealthRecovery;
		Snapshot.bTriggersExhaustionSequence = Definition.bTriggersExhaustionSequence;
		Snapshot.bInvertMovementInput = Definition.bInvertMovementInput;
		Snapshot.MovementInputScale = Definition.MovementInputScale;
		Snapshot.bActive = true;
		Snapshot.bTimed = bTimed;
		Snapshot.IconTexture = LoadedIconTextures.FindRef(Definition.StatusName);
		Snapshot.Tint = Definition.Tint;
		Snapshot.HudPriority = Definition.HudPriority;
		Snapshot.HudSlotSize = Definition.HudSlotSize;
		Snapshot.HudIconSize = Definition.HudIconSize;
		Snapshot.HudIconSlotOffset = Definition.HudIconSlotOffset;
		Snapshot.HudSlotOffset = Definition.HudSlotOffset;
		Snapshot.HudNameFontAsset = Definition.HudNameFontAsset;
		Snapshot.HudDescriptionFontAsset = Definition.HudDescriptionFontAsset;
		Snapshot.HudMetaFontAsset = Definition.HudMetaFontAsset;
		Snapshot.HudNameFontSize = Definition.HudNameFontSize;
		Snapshot.HudDescriptionFontSize = Definition.HudDescriptionFontSize;
		Snapshot.HudMetaFontSize = Definition.HudMetaFontSize;
		Snapshot.HudNameTextColor = Definition.HudNameTextColor;
		Snapshot.HudDescriptionTextColor = Definition.HudDescriptionTextColor;
		Snapshot.HudMetaTextColor = Definition.HudMetaTextColor;
		Snapshot.HudNameTextOffset = Definition.HudNameTextOffset;
		Snapshot.HudDescriptionTextOffset = Definition.HudDescriptionTextOffset;
		Snapshot.HudDurationTextOffset = Definition.HudDurationTextOffset;
		Snapshot.HudDamageTextOffset = Definition.HudDamageTextOffset;
		Snapshots.Add(Snapshot);
	}

	return Snapshots;
}

TArray<FProjectSurvivalStatusSnapshot> UProjectSurvivalStatusComponent::BuildVisibleStatusSnapshots(
	const int32 MaxVisibleStatuses,
	int32& OutOverflowCount) const
{
	return SelectVisibleStatusSnapshotsForHud(BuildActiveStatusSnapshots(), MaxVisibleStatuses, OutOverflowCount);
}

TArray<FProjectSurvivalStatusSnapshot> UProjectSurvivalStatusComponent::SelectVisibleStatusSnapshotsForHud(
	const TArray<FProjectSurvivalStatusSnapshot>& ActiveSnapshots,
	const int32 MaxVisibleStatuses,
	int32& OutOverflowCount)
{
	TArray<FProjectSurvivalStatusSnapshot> SortedSnapshots = ActiveSnapshots;
	SortedSnapshots.Sort([](const FProjectSurvivalStatusSnapshot& Left, const FProjectSurvivalStatusSnapshot& Right)
	{
		if (Left.HudPriority != Right.HudPriority)
		{
			return Left.HudPriority > Right.HudPriority;
		}

		return Left.StatusName.LexicalLess(Right.StatusName);
	});

	const int32 SafeMaxVisibleStatuses = FMath::Max(1, MaxVisibleStatuses);
	OutOverflowCount = FMath::Max(0, SortedSnapshots.Num() - SafeMaxVisibleStatuses);
	if (SortedSnapshots.Num() > SafeMaxVisibleStatuses)
	{
		SortedSnapshots.SetNum(SafeMaxVisibleStatuses);
	}

	return SortedSnapshots;
}

void UProjectSurvivalStatusComponent::HandleOwnerDamageApplied(
	AActor* SourceActor,
	const FName DamageType,
	const float RequestedDamage,
	const float AppliedDamage,
	const float RemainingValue,
	const bool bKilledTarget)
{
	(void)DamageType;
	(void)RequestedDamage;
	(void)RemainingValue;
	(void)bKilledTarget;

	if (AppliedDamage <= 0.f || !SourceActor || SourceActor == GetOwner())
	{
		return;
	}

	bool bAppliedAnyStatus = false;
	const FProjectSurvivalStatusCatalog& Catalog = GetProjectSurvivalStatusCatalog();
	for (const FProjectSurvivalStatusIncomingHitRule& Rule : Catalog.IncomingHitRules)
	{
		if (Rule.StatusName.IsNone() || Rule.SourceClassNameHints.Num() == 0)
		{
			continue;
		}

		if (!ClassLineageContainsAnyHint(SourceActor->GetClass(), Rule.SourceClassNameHints))
		{
			continue;
		}

		if (Rule.ApplyChance < 1.f && FMath::FRand() > FMath::Clamp(Rule.ApplyChance, 0.f, 1.f))
		{
			continue;
		}

		bAppliedAnyStatus |= ApplyTimedStatusInstance(Rule.StatusName, -1.f, SourceActor, false);
	}

	if (bAppliedAnyStatus)
	{
		UpdateStatuses(0.f);
	}
}

void UProjectSurvivalStatusComponent::ResolveDependencies()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UnbindDamageObserver();
		NeedsComponent = nullptr;
		CombatAttributeComponent = nullptr;
		AbilitySystemComponent = nullptr;
		AttributeBridgeComponent = nullptr;
		LocomotionOverrideComponent = nullptr;
		ResolvedHealthBinding = FProjectSurvivalResolvedStatusHealthBinding();
		bDependenciesResolved = false;
		MarkStatusAttributeModifiersDirty();
		return;
	}

	UProjectSurvivalAttributeBridgeComponent* PreviousAttributeBridgeComponent = AttributeBridgeComponent;
	UProjectLocomotionOverrideComponent* PreviousLocomotionOverrideComponent = LocomotionOverrideComponent;
	NeedsComponent = Owner->FindComponentByClass<UProjectSurvivalNeedsComponent>();
	CombatAttributeComponent = Owner->FindComponentByClass<UProjectCombatAttributeComponent>();
	AbilitySystemComponent = Owner->FindComponentByClass<UAbilitySystemComponent>();
	AttributeBridgeComponent = Owner->FindComponentByClass<UProjectSurvivalAttributeBridgeComponent>();
	LocomotionOverrideComponent = Owner->FindComponentByClass<UProjectLocomotionOverrideComponent>();
	if (PreviousAttributeBridgeComponent != AttributeBridgeComponent)
	{
		MarkStatusAttributeModifiersDirty();
	}
	if (PreviousLocomotionOverrideComponent != LocomotionOverrideComponent)
	{
		bStatusMovementModifiersDirty = true;
	}

	BindDamageObserver();

	if (AbilitySystemComponent && (!ResolvedHealthBinding.bResolved || !ResolvedHealthBinding.AttributeSet.IsValid()))
	{
		ResolveHealthBinding();
	}
	else if (!AbilitySystemComponent)
	{
		ResolvedHealthBinding = FProjectSurvivalResolvedStatusHealthBinding();
	}

	bDependenciesResolved = NeedsComponent != nullptr && (CombatAttributeComponent != nullptr || AbilitySystemComponent != nullptr);
}

void UProjectSurvivalStatusComponent::BindDamageObserver()
{
	if (BoundDamageObservedCombatAttributeComponent == CombatAttributeComponent)
	{
		return;
	}

	UnbindDamageObserver();

	BoundDamageObservedCombatAttributeComponent = CombatAttributeComponent;
	if (BoundDamageObservedCombatAttributeComponent)
	{
		BoundDamageObservedCombatAttributeComponent->OnDamageApplied.AddUniqueDynamic(this, &ThisClass::HandleOwnerDamageApplied);
	}
}

void UProjectSurvivalStatusComponent::UnbindDamageObserver()
{
	if (BoundDamageObservedCombatAttributeComponent)
	{
		BoundDamageObservedCombatAttributeComponent->OnDamageApplied.RemoveDynamic(this, &ThisClass::HandleOwnerDamageApplied);
		BoundDamageObservedCombatAttributeComponent = nullptr;
	}
}

void UProjectSurvivalStatusComponent::InitializeRuntimeStates()
{
	if (bRuntimeStatesInitialized)
	{
		return;
	}

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return;
	}

	StatusDefinitionsByName.Reset();
	ActiveStatusByName.Reset();
	PendingDamageByStatus.Reset();
	LoadedIconTextures.Reset();
	RuntimeStatusDefinitions = Settings->BuildResolvedStatusDefinitions();

	for (const FProjectSurvivalStatusDefinition& Definition : RuntimeStatusDefinitions)
	{
		if (Definition.StatusName.IsNone() || StatusDefinitionsByName.Contains(Definition.StatusName))
		{
			continue;
		}

		StatusDefinitionsByName.Add(Definition.StatusName, Definition);
		ActiveStatusByName.Add(Definition.StatusName, false);
		PendingDamageByStatus.Add(Definition.StatusName, 0.f);
		LoadIconTexture(Definition);
	}

	bRuntimeStatesInitialized = true;
	MarkStatusAttributeModifiersDirty();
}

bool UProjectSurvivalStatusComponent::NeedsDependencyRefresh() const
{
	if (!bDependenciesResolved)
	{
		return true;
	}

	if (!NeedsComponent)
	{
		return true;
	}

	if (!CombatAttributeComponent && !AbilitySystemComponent)
	{
		return true;
	}

	if (AbilitySystemComponent && (!ResolvedHealthBinding.bResolved || !ResolvedHealthBinding.AttributeSet.IsValid()))
	{
		return true;
	}

	return false;
}

void UProjectSurvivalStatusComponent::MarkStatusAttributeModifiersDirty()
{
	bStatusAttributeModifiersDirty = true;
	bStatusMovementModifiersDirty = true;
}

void UProjectSurvivalStatusComponent::UpdateStatuses(const float DeltaTime)
{
	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return;
	}

	if (!bRuntimeStatesInitialized)
	{
		InitializeRuntimeStates();
	}

	const float CurrentTimeSeconds = GetCurrentWorldTimeSeconds(this);
	PruneExpiredTimedStatuses(CurrentTimeSeconds);

	bool bShouldBlockHealthRecovery = false;

	for (const FProjectSurvivalStatusDefinition& Definition : RuntimeStatusDefinitions)
	{
		if (Definition.StatusName.IsNone())
		{
			continue;
		}

		const bool bImmune = IsStatusImmune(Definition.StatusName) && !DebugBypassImmunityStatusNames.Contains(Definition.StatusName);
		const bool bWasActive = ActiveStatusByName.FindRef(Definition.StatusName);
		bool bNeedIsEmpty = false;
		bool bThresholdActive = false;

		if (!bImmune && Definition.bTriggerAtNeedEmpty && NeedsComponent && !Definition.SourceNeedName.IsNone())
		{
			bNeedIsEmpty = NeedsComponent->GetNeedCurrentValue(Definition.SourceNeedName) <= ProjectSurvivalStatusNeedEmptyThreshold;
		}

		if (!bImmune && NeedsComponent && !Definition.SourceEntryName.IsNone() && Definition.SourceType != EProjectSurvivalStatusSourceType::None)
		{
			const float NormalizedValue = Definition.SourceType == EProjectSurvivalStatusSourceType::Sensation
				? NeedsComponent->GetSensationNormalizedValue(Definition.SourceEntryName)
				: NeedsComponent->GetNeedNormalizedValue(Definition.SourceEntryName);
			bThresholdActive = EvaluateThresholdStatus(
				bWasActive,
				NormalizedValue,
				Definition.ThresholdMode,
				Definition.ActivationThresholdNormalized,
				Definition.DeactivationThresholdNormalized);
		}

		const bool bTimedActive = !bImmune && IsTimedStatusActive(Definition.StatusName, CurrentTimeSeconds);
		const bool bForcedActive = !bImmune && ForcedActiveStatusNames.Contains(Definition.StatusName);
		if (Definition.bTriggersExhaustionSequence && (bNeedIsEmpty || bThresholdActive || bTimedActive || bForcedActive) && !bExhaustionSequenceActive)
		{
			float RemainingDurationSeconds = 0.f;
			IsTimedStatusActive(Definition.StatusName, CurrentTimeSeconds, &RemainingDurationSeconds);
			StartExhaustionSequence(RemainingDurationSeconds);
		}

		const bool bShouldBeActive = !bImmune && (ForcedActiveStatusNames.Contains(Definition.StatusName)
			|| bNeedIsEmpty
			|| bThresholdActive
			|| (Definition.bTriggersExhaustionSequence && bExhaustionSequenceActive)
			|| bTimedActive);

		if (bWasActive != bShouldBeActive)
		{
			ActiveStatusByName.Add(Definition.StatusName, bShouldBeActive);
			MarkStatusAttributeModifiersDirty();
			if (!bShouldBeActive)
			{
				PendingDamageByStatus.Add(Definition.StatusName, 0.f);
			}

			OnStatusChanged.Broadcast(Definition.StatusName, bShouldBeActive);
		}

		if (bShouldBeActive && Definition.bBlocksHealthRecovery)
		{
			bShouldBlockHealthRecovery = true;
		}

		if (bShouldBeActive && Definition.DamagePerSecond > 0.f && DeltaTime > 0.f)
		{
			float& PendingDamage = PendingDamageByStatus.FindOrAdd(Definition.StatusName);
			PendingDamage += Definition.DamagePerSecond * DeltaTime;

			const int32 WholeDamage = FMath::FloorToInt(PendingDamage);
			if (WholeDamage > 0)
			{
				ApplyPeriodicStatusDamage(Definition.StatusName, static_cast<float>(WholeDamage));
				PendingDamage -= static_cast<float>(WholeDamage);
			}
		}
	}

	ApplyHealthRecoveryBlock(bShouldBlockHealthRecovery);
	EnforceHealthRecoveryBlock();
	RefreshStatusAttributeModifiers();
	RefreshStatusMovementModifiers();
	UpdateInvertedMovementInput(DeltaTime);

	if (bExhaustionSequenceActive && DeltaTime > 0.f)
	{
		ExhaustionRemainingSeconds -= DeltaTime;
		if (ExhaustionRemainingSeconds <= 0.f)
		{
			FinishExhaustionSequence();
		}
	}
}

void UProjectSurvivalStatusComponent::RefreshStatusAttributeModifiers()
{
	if (!bStatusAttributeModifiersDirty)
	{
		return;
	}

	if (!AttributeBridgeComponent)
	{
		return;
	}

	for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
	{
		const FProjectSurvivalStatusDefinition& Definition = Pair.Value;
		if (Definition.AttributeModifiers.Num() == 0)
		{
			continue;
		}

		if (!ActiveStatusByName.FindRef(Pair.Key))
		{
			AttributeBridgeComponent->ClearAllExternalAttributeMultipliersForSource(Pair.Key);
			continue;
		}

		for (const FProjectSurvivalStatusAttributeModifier& Modifier : Definition.AttributeModifiers)
		{
			if (!Modifier.AttributeName.IsNone())
			{
				AttributeBridgeComponent->SetExternalAttributeMultiplier(Pair.Key, Modifier.AttributeName, Modifier.Multiplier);
			}
		}
	}

	bStatusAttributeModifiersDirty = false;
}

void UProjectSurvivalStatusComponent::RefreshStatusMovementModifiers()
{
	if (!bStatusMovementModifiersDirty || !LocomotionOverrideComponent)
	{
		return;
	}

	LocomotionOverrideComponent->ClearMovementSpeedModifiers(EProjectMovementModifierLayer::StatusPenalty);
	for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
	{
		if (!ActiveStatusByName.FindRef(Pair.Key))
		{
			continue;
		}

		const float MovementMultiplier = FMath::Clamp(Pair.Value.MovementInputScale, 0.f, 1.f);
		if (MovementMultiplier < 1.f - KINDA_SMALL_NUMBER)
		{
			LocomotionOverrideComponent->SetMovementSpeedModifier(
				ProjectMovementModifierTags::ForStatus(Pair.Key),
				EProjectMovementModifierLayer::StatusPenalty,
				MovementMultiplier);
		}
	}

	bStatusMovementModifiersDirty = false;
}

void UProjectSurvivalStatusComponent::UpdateInvertedMovementInput(const float DeltaTime)
{
	bool bShouldInvertMovementInput = false;

	for (const TPair<FName, FProjectSurvivalStatusDefinition>& Pair : StatusDefinitionsByName)
	{
		if (!Pair.Value.bInvertMovementInput || !ActiveStatusByName.FindRef(Pair.Key))
		{
			continue;
		}

		bShouldInvertMovementInput = true;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerPawn || !PlayerController || !PlayerController->IsLocalController() || bExhaustionSequenceActive)
	{
		if (!bShouldInvertMovementInput && bInvertedMovementInputApplied && PlayerController && !bExhaustionSequenceActive)
		{
			PlayerController->SetIgnoreMoveInput(false);
			bInvertedMovementInputApplied = false;
		}
		return;
	}

	if (!bShouldInvertMovementInput)
	{
		if (bInvertedMovementInputApplied)
		{
			PlayerController->SetIgnoreMoveInput(false);
			bInvertedMovementInputApplied = false;
		}
		return;
	}

	if (!bInvertedMovementInputApplied)
	{
		PlayerController->SetIgnoreMoveInput(true);
		bInvertedMovementInputApplied = true;
	}

	OwnerPawn->ConsumeMovementInputVector();

	float ForwardInput = 0.f;
	float RightInput = 0.f;
	if (PlayerController->IsInputKeyDown(EKeys::W))
	{
		ForwardInput += 1.f;
	}
	if (PlayerController->IsInputKeyDown(EKeys::S))
	{
		ForwardInput -= 1.f;
	}
	if (PlayerController->IsInputKeyDown(EKeys::D))
	{
		RightInput += 1.f;
	}
	if (PlayerController->IsInputKeyDown(EKeys::A))
	{
		RightInput -= 1.f;
	}

	if (FMath::IsNearlyZero(ForwardInput) && FMath::IsNearlyZero(RightInput))
	{
		return;
	}

	const FRotator ControlRotation = PlayerController->GetControlRotation();
	const FRotator YawOnlyRotation(0.f, ControlRotation.Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawOnlyRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawOnlyRotation).GetUnitAxis(EAxis::Y);

	if (!FMath::IsNearlyZero(ForwardInput))
	{
		OwnerPawn->AddMovementInput(
			ForwardDirection,
			ResolveInvertedMovementInput(ForwardInput),
			true);
	}

	if (!FMath::IsNearlyZero(RightInput))
	{
		OwnerPawn->AddMovementInput(
			RightDirection,
			ResolveInvertedMovementInput(RightInput),
			true);
	}

	(void)DeltaTime;
}

#if WITH_DEV_AUTOMATION_TESTS
float UProjectSurvivalStatusComponent::AutomationResolveInvertedMovementInput(
	const float RawInput)
{
	return ResolveInvertedMovementInput(RawInput);
}

bool UProjectSurvivalStatusComponent::AutomationEvaluateThresholdStatus(
	const bool bWasActive,
	const float NormalizedValue,
	const EProjectSurvivalStatusThresholdMode ThresholdMode,
	const float ActivationThresholdNormalized,
	const float DeactivationThresholdNormalized)
{
	return EvaluateThresholdStatus(
		bWasActive,
		NormalizedValue,
		ThresholdMode,
		ActivationThresholdNormalized,
		DeactivationThresholdNormalized);
}
#endif

bool UProjectSurvivalStatusComponent::IsTimedStatusActive(
	const FName StatusName,
	const float CurrentTimeSeconds,
	float* OutRemainingDurationSeconds) const
{
	const FProjectSurvivalTimedStatusRuntime* RuntimeState = TimedStatusByName.Find(StatusName);
	if (!RuntimeState)
	{
		if (OutRemainingDurationSeconds)
		{
			*OutRemainingDurationSeconds = 0.f;
		}
		return false;
	}

	const bool bActive = RuntimeState->HasAnyActiveInstance(CurrentTimeSeconds);
	if (OutRemainingDurationSeconds)
	{
		*OutRemainingDurationSeconds = RuntimeState->GetRemainingDurationSeconds(CurrentTimeSeconds);
	}

	return bActive;
}

bool UProjectSurvivalStatusComponent::ApplyTimedStatusInstance(
	const FName StatusName,
	const float DurationSeconds,
	AActor* SourceActor,
	const bool bDebugInstance)
{
	if (IsStatusImmune(StatusName))
	{
		return false;
	}

	const FProjectSurvivalStatusDefinition* Definition = StatusDefinitionsByName.Find(StatusName);
	if (!Definition)
	{
		return false;
	}

	const float EffectiveDurationSeconds = DurationSeconds > 0.f ? DurationSeconds : Definition->DurationSeconds;
	if (EffectiveDurationSeconds <= 0.f)
	{
		return false;
	}

	const float CurrentTimeSeconds = GetCurrentWorldTimeSeconds(this);
	FProjectSurvivalTimedStatusRuntime& RuntimeState = TimedStatusByName.FindOrAdd(StatusName);
	RuntimeState.ClearExpiredInstances(CurrentTimeSeconds);

	const bool bCurrentlyActive = bDebugInstance
		? (RuntimeState.bHasDebugInstance && CurrentTimeSeconds < RuntimeState.DebugEndTimeSeconds)
		: (RuntimeState.bHasGameplayInstance && CurrentTimeSeconds < RuntimeState.GameplayEndTimeSeconds);

	if (bCurrentlyActive && Definition->ReapplyPolicy == EProjectSurvivalStatusRefreshPolicy::IgnoreIfActive)
	{
		return false;
	}

	if (bDebugInstance)
	{
		RuntimeState.bHasDebugInstance = true;
		RuntimeState.DebugStartTimeSeconds = CurrentTimeSeconds;
		RuntimeState.DebugEndTimeSeconds = CurrentTimeSeconds + EffectiveDurationSeconds;
		RuntimeState.DebugSourceActor = SourceActor ? SourceActor : GetOwner();
		DebugAppliedStatusNames.Add(StatusName);
	}
	else
	{
		RuntimeState.bHasGameplayInstance = true;
		RuntimeState.GameplayStartTimeSeconds = CurrentTimeSeconds;
		RuntimeState.GameplayEndTimeSeconds = CurrentTimeSeconds + EffectiveDurationSeconds;
		RuntimeState.GameplaySourceActor = SourceActor;
	}

	return true;
}

void UProjectSurvivalStatusComponent::ClearTimedStatusInstance(
	const FName StatusName,
	const bool bClearGameplayInstance,
	const bool bClearDebugInstance)
{
	FProjectSurvivalTimedStatusRuntime* RuntimeState = TimedStatusByName.Find(StatusName);
	if (!RuntimeState)
	{
		return;
	}

	if (bClearGameplayInstance)
	{
		RuntimeState->bHasGameplayInstance = false;
		RuntimeState->GameplayStartTimeSeconds = 0.f;
		RuntimeState->GameplayEndTimeSeconds = 0.f;
		RuntimeState->GameplaySourceActor = nullptr;
	}

	if (bClearDebugInstance)
	{
		RuntimeState->bHasDebugInstance = false;
		RuntimeState->DebugStartTimeSeconds = 0.f;
		RuntimeState->DebugEndTimeSeconds = 0.f;
		RuntimeState->DebugSourceActor = nullptr;
	}

	if (!RuntimeState->bHasGameplayInstance && !RuntimeState->bHasDebugInstance)
	{
		TimedStatusByName.Remove(StatusName);
	}
}

void UProjectSurvivalStatusComponent::PruneExpiredTimedStatuses(const float CurrentTimeSeconds)
{
	TArray<FName> StatusNamesToRemove;
	for (TPair<FName, FProjectSurvivalTimedStatusRuntime>& Pair : TimedStatusByName)
	{
		Pair.Value.ClearExpiredInstances(CurrentTimeSeconds);
		if (!Pair.Value.bHasGameplayInstance && !Pair.Value.bHasDebugInstance)
		{
			if (!ForcedActiveStatusNames.Contains(Pair.Key))
			{
				DebugAppliedStatusNames.Remove(Pair.Key);
				DebugBypassImmunityStatusNames.Remove(Pair.Key);
			}
			StatusNamesToRemove.Add(Pair.Key);
		}
	}

	for (const FName StatusName : StatusNamesToRemove)
	{
		TimedStatusByName.Remove(StatusName);
		if (CurrentDebugCycleStatusName == StatusName)
		{
			CurrentDebugCycleStatusName = NAME_None;
		}
	}
}

void UProjectSurvivalStatusComponent::ApplyHealthRecoveryBlock(const bool bBlocked)
{
	if (bHealthRecoveryBlocked != bBlocked)
	{
		bHealthRecoveryBlocked = bBlocked;

		if (CombatAttributeComponent && !CombatAttributeComponent->HealthAttributeName.IsNone())
		{
			CombatAttributeComponent->SetAttributeRecoveryBlocked(CombatAttributeComponent->HealthAttributeName, bHealthRecoveryBlocked);
		}
	}

	if (!bHealthRecoveryBlocked)
	{
		ResolvedHealthBinding.bHasRecoveryBlockCeiling = false;
		return;
	}

	RefreshHealthRecoveryCeiling(!ResolvedHealthBinding.bHasRecoveryBlockCeiling);
}

void UProjectSurvivalStatusComponent::ApplyPeriodicStatusDamage(FName StatusName, const float DamageAmount)
{
	if (DamageAmount <= 0.f)
	{
		return;
	}

	if (ApplyHealthDelta(-DamageAmount))
	{
		return;
	}

	if (CombatAttributeComponent)
	{
		FProjectCombatDamageSpec DamageSpec;
		DamageSpec.DamageType = StatusName;
		DamageSpec.TargetAttribute = CombatAttributeComponent->HealthAttributeName;
		DamageSpec.BaseDamage = DamageAmount;
		DamageSpec.bIgnoreArmor = true;
		DamageSpec.SourceActor = GetOwner();
		DamageSpec.DamageCauser = GetOwner();
		CombatAttributeComponent->ApplyDamage(DamageSpec);
	}
}

void UProjectSurvivalStatusComponent::StartExhaustionSequence(const float DurationOverrideSeconds)
{
	if (bExhaustionSequenceActive)
	{
		return;
	}

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return;
	}

	bExhaustionSequenceActive = true;
	ExhaustionRemainingSeconds = FMath::Max(0.1f, DurationOverrideSeconds > 0.f ? DurationOverrideSeconds : Settings->ExhaustedBlackoutSeconds);
	OnBlackoutChanged.Broadcast(true);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController)
	{
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
	}

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
		{
			if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.15f);
			}
		}

		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->StopMovementImmediately();
			MovementComponent->DisableMovement();
		}
	}
}

void UProjectSurvivalStatusComponent::FinishExhaustionSequence()
{
	if (!bExhaustionSequenceActive)
	{
		return;
	}

	bExhaustionSequenceActive = false;
	ExhaustionRemainingSeconds = 0.f;
	OnBlackoutChanged.Broadcast(false);

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (Settings && NeedsComponent)
	{
		for (const FProjectSurvivalStatusDefinition& Definition : RuntimeStatusDefinitions)
		{
			if (!Definition.bTriggersExhaustionSequence || Definition.SourceNeedName.IsNone())
			{
				continue;
			}

			const float SleepMaxValue = NeedsComponent->GetNeedMaxValue(Definition.SourceNeedName);
			const float RestoredSleepValue = SleepMaxValue * FMath::Clamp(Settings->ExhaustedSleepRestorePercent, 0.f, 1.f);
			NeedsComponent->SetNeedCurrentValue(Definition.SourceNeedName, RestoredSleepValue, true);
			if (UProjectInnerDoctrineComponent* DoctrineComponent = GetOwner()
				? GetOwner()->FindComponentByClass<UProjectInnerDoctrineComponent>()
				: nullptr)
			{
				DoctrineComponent->NotifySleepCompleted(TEXT("ExhaustionSequence"));
			}
			break;
		}
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController)
	{
		if (!bInvertedMovementInputApplied)
		{
			PlayerController->SetIgnoreMoveInput(false);
		}
		PlayerController->SetIgnoreLookInput(false);
	}

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
		}
	}
}

UTexture2D* UProjectSurvivalStatusComponent::LoadIconTexture(const FProjectSurvivalStatusDefinition& Definition)
{
	if (UTexture2D* ExistingTexture = LoadedIconTextures.FindRef(Definition.StatusName))
	{
		return ExistingTexture;
	}

	UTexture2D* LoadedTexture = nullptr;
	if (!Definition.IconTextureAsset.IsNull())
	{
		LoadedTexture = Definition.IconTextureAsset.LoadSynchronous();
	}

	if (!LoadedTexture && !Definition.MinimalIconName.IsNone())
	{
		LoadedTexture = CreateProceduralIconTexture(Definition.MinimalIconName);
	}

	if (LoadedTexture)
	{
		LoadedIconTextures.Add(Definition.StatusName, LoadedTexture);
	}

	return LoadedTexture;
}

UTexture2D* UProjectSurvivalStatusComponent::CreateProceduralIconTexture(const FName MinimalIconName) const
{
	if (MinimalIconName.IsNone())
	{
		return nullptr;
	}

	TArray<FColor> Pixels;
	DrawMinimalStatusIcon(Pixels, ProjectSurvivalProceduralIconSize, MinimalIconName);

	UTexture2D* Texture = UTexture2D::CreateTransient(ProjectSurvivalProceduralIconSize, ProjectSurvivalProceduralIconSize, PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->LODGroup = TEXTUREGROUP_UI;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!TextureData)
	{
		Mip.BulkData.Unlock();
		return nullptr;
	}

	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

bool UProjectSurvivalStatusComponent::ResolveHealthBinding()
{
	if (!AbilitySystemComponent)
	{
		ResolvedHealthBinding = FProjectSurvivalResolvedStatusHealthBinding();
		return false;
	}

	if (ResolvedHealthBinding.bResolved && ResolvedHealthBinding.AttributeSet.IsValid())
	{
		return true;
	}

	ResolvedHealthBinding = FProjectSurvivalResolvedStatusHealthBinding();

	const TArray<UAttributeSet*>& AttributeSets = AbilitySystemComponent->GetSpawnedAttributes();
	const TArray<FString> CandidateNames = GetHealthCandidateNames();
	for (UAttributeSet* AttributeSet : AttributeSets)
	{
		if (!AttributeSet)
		{
			continue;
		}

		for (TFieldIterator<FProperty> It(AttributeSet->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || !PropertyNameMatches(Property->GetFName(), CandidateNames))
			{
				continue;
			}

			ResolvedHealthBinding.AttributeSet = AttributeSet;
			ResolvedHealthBinding.ResolvedPropertyName = Property->GetFName();
			ResolvedHealthBinding.ResolvedProperty = Property;
			ResolvedHealthBinding.bResolved = true;

			if (IsGameplayAttributeDataProperty(Property))
			{
				ResolvedHealthBinding.bGameplayAttributeData = true;
				ResolvedHealthBinding.GameplayAttribute = FGameplayAttribute(Property);
			}
			else if (CastField<FFloatProperty>(Property))
			{
				ResolvedHealthBinding.bFloatProperty = true;
			}

			if (ResolvedHealthBinding.bGameplayAttributeData || ResolvedHealthBinding.bFloatProperty)
			{
				return true;
			}

			ResolvedHealthBinding = FProjectSurvivalResolvedStatusHealthBinding();
		}
	}

	return false;
}

float UProjectSurvivalStatusComponent::ReadCurrentHealthValue() const
{
	if (ResolvedHealthBinding.bResolved && ResolvedHealthBinding.bGameplayAttributeData && AbilitySystemComponent)
	{
		return AbilitySystemComponent->GetNumericAttribute(ResolvedHealthBinding.GameplayAttribute);
	}

	if (ResolvedHealthBinding.bResolved && ResolvedHealthBinding.bFloatProperty && ResolvedHealthBinding.AttributeSet.IsValid())
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(ResolvedHealthBinding.ResolvedProperty))
		{
			return FloatProperty->GetPropertyValue_InContainer(ResolvedHealthBinding.AttributeSet.Get());
		}
	}

	if (CombatAttributeComponent)
	{
		return CombatAttributeComponent->GetAttributeCurrentValue(CombatAttributeComponent->HealthAttributeName);
	}

	return 0.f;
}

bool UProjectSurvivalStatusComponent::ApplyHealthDelta(const float DeltaAmount)
{
	if (FMath::IsNearlyZero(DeltaAmount, ProjectSurvivalStatusHealthWriteTolerance))
	{
		return false;
	}

	if (ResolveHealthBinding())
	{
		if (ResolvedHealthBinding.bGameplayAttributeData && AbilitySystemComponent)
		{
			if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
			{
				AbilitySystemComponent->ApplyModToAttribute(ResolvedHealthBinding.GameplayAttribute, EGameplayModOp::Additive, DeltaAmount);
			}
			else
			{
				AbilitySystemComponent->ApplyModToAttributeUnsafe(ResolvedHealthBinding.GameplayAttribute, EGameplayModOp::Additive, DeltaAmount);
			}

			return true;
		}

		if (ResolvedHealthBinding.bFloatProperty && ResolvedHealthBinding.AttributeSet.IsValid())
		{
			if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(ResolvedHealthBinding.ResolvedProperty))
			{
				const float CurrentValue = FloatProperty->GetPropertyValue_InContainer(ResolvedHealthBinding.AttributeSet.Get());
				FloatProperty->SetPropertyValue_InContainer(ResolvedHealthBinding.AttributeSet.Get(), CurrentValue + DeltaAmount);
				return true;
			}
		}
	}

	if (!CombatAttributeComponent)
	{
		return false;
	}

	return !FMath::IsNearlyZero(CombatAttributeComponent->ModifyAttribute(CombatAttributeComponent->HealthAttributeName, DeltaAmount), ProjectSurvivalStatusHealthWriteTolerance);
}

void UProjectSurvivalStatusComponent::RefreshHealthRecoveryCeiling(const bool bResetToCurrentValue)
{
	if (!bHealthRecoveryBlocked)
	{
		ResolvedHealthBinding.bHasRecoveryBlockCeiling = false;
		return;
	}

	const float CurrentHealth = ReadCurrentHealthValue();
	if (!ResolvedHealthBinding.bHasRecoveryBlockCeiling || bResetToCurrentValue)
	{
		ResolvedHealthBinding.RecoveryBlockCeiling = CurrentHealth;
		ResolvedHealthBinding.bHasRecoveryBlockCeiling = true;
		return;
	}

	if (CurrentHealth < ResolvedHealthBinding.RecoveryBlockCeiling - ProjectSurvivalStatusHealthWriteTolerance)
	{
		ResolvedHealthBinding.RecoveryBlockCeiling = CurrentHealth;
	}
}

void UProjectSurvivalStatusComponent::EnforceHealthRecoveryBlock()
{
	if (!bHealthRecoveryBlocked)
	{
		return;
	}

	RefreshHealthRecoveryCeiling(false);
	if (!ResolvedHealthBinding.bHasRecoveryBlockCeiling)
	{
		return;
	}

	const float CurrentHealth = ReadCurrentHealthValue();
	if (CurrentHealth > ResolvedHealthBinding.RecoveryBlockCeiling + ProjectSurvivalStatusHealthWriteTolerance)
	{
		const float ClampDelta = ResolvedHealthBinding.RecoveryBlockCeiling - CurrentHealth;
		if (ApplyHealthDelta(ClampDelta))
		{
			ResolvedHealthBinding.RecoveryBlockCeiling = ReadCurrentHealthValue();
		}
	}
	else if (CurrentHealth < ResolvedHealthBinding.RecoveryBlockCeiling - ProjectSurvivalStatusHealthWriteTolerance)
	{
		ResolvedHealthBinding.RecoveryBlockCeiling = CurrentHealth;
	}
}
