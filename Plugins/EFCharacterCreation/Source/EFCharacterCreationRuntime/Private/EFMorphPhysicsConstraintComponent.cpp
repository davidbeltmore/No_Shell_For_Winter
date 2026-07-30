#include "EFMorphPhysicsConstraintComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "EFCharacterCreationSettings.h"
#include "EFCharacterCustomizationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

namespace EFMorphPhysicsConstraintPrivate
{
	static constexpr float BreastPositiveThreshold = 1.5f;
	static constexpr float GluteMaxLimit = 5.0f;
	static constexpr float GlutePositiveThreshold = 4.0f;
	static constexpr float ThighMaxLimit = 3.5f;
	static constexpr float ThighPositiveThreshold = 4.0f;

	static FString NormalizeMorphKey(const FString& Value)
	{
		FString WorkingValue = Value.ToLower();
		WorkingValue.TrimStartAndEndInline();

		if (WorkingValue.StartsWith(TEXT("morph-")))
		{
			WorkingValue.RightChopInline(6, EAllowShrinking::No);
		}
		else if (WorkingValue.StartsWith(TEXT("morph_")))
		{
			WorkingValue.RightChopInline(6, EAllowShrinking::No);
		}
		else if (WorkingValue.StartsWith(TEXT("morph ")))
		{
			WorkingValue.RightChopInline(6, EAllowShrinking::No);
		}
		else if (WorkingValue.StartsWith(TEXT("morph")))
		{
			WorkingValue.RightChopInline(5, EAllowShrinking::No);
		}

		FString Result;
		Result.Reserve(WorkingValue.Len());
		for (const TCHAR Character : WorkingValue)
		{
			if (FChar::IsAlnum(Character))
			{
				Result.AppendChar(Character);
			}
		}

		return Result;
	}

	static FEFMorphPhysicsWeightedMorph MakeWeightedMorph(const TCHAR* MorphName, const float Weight)
	{
		FEFMorphPhysicsWeightedMorph Morph;
		Morph.MorphName = FName(MorphName);
		Morph.Weight = Weight;
		return Morph;
	}

	static FString NormalizeSearchValue(const FString& Value)
	{
		FString WorkingValue = Value.ToLower();
		WorkingValue.TrimStartAndEndInline();
		return WorkingValue;
	}

	static bool MatchesAnyHint(const FString& SourceString, const TArray<FString>& Hints)
	{
		const FString NormalizedSource = NormalizeSearchValue(SourceString);
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && NormalizedSource.Contains(NormalizeSearchValue(Hint)))
			{
				return true;
			}
		}

		return false;
	}

	static FString BuildMeshSourceString(const USkeletalMeshComponent* MeshComponent)
	{
		if (!IsValid(MeshComponent))
		{
			return FString();
		}

		const USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
		return FString::Printf(TEXT("%s %s %s"),
			*MeshComponent->GetName(),
			IsValid(SkeletalMesh) ? *SkeletalMesh->GetName() : TEXT(""),
			IsValid(SkeletalMesh) ? *SkeletalMesh->GetPathName() : TEXT(""));
	}

	static int32 ScoreMeshComponentForMorphPhysics(const AActor* Owner, const USkeletalMeshComponent* MeshComponent)
	{
		if (!IsValid(MeshComponent))
		{
			return MIN_int32 / 4;
		}

		const USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMeshAsset();
		if (!IsValid(SkeletalMesh))
		{
			return MIN_int32 / 4;
		}

		const UEFCharacterCreationSettings* Settings = UEFCharacterCreationSettings::Get();
		const FString ComponentName = MeshComponent->GetName();
		const FString MeshPath = SkeletalMesh->GetPathName();
		const FString SourceString = BuildMeshSourceString(MeshComponent);
		const FString NormalizedSource = NormalizeSearchValue(SourceString);

		int32 Score = 0;
		if (IsValid(MeshComponent->GetPhysicsAsset()))
		{
			Score += 1200;
		}
		else if (IsValid(SkeletalMesh->GetPhysicsAsset()))
		{
			Score += 800;
		}

		if (MatchesAnyHint(ComponentName, Settings->BodyMeshSelectionComponentHints))
		{
			Score += 900;
		}

		if (MatchesAnyHint(ComponentName, Settings->BodyMeshComponentHints))
		{
			Score += 350;
		}

		if (MatchesAnyHint(MeshPath, Settings->DazPathTokens))
		{
			Score += 1400;
		}

		if (NormalizedSource.Contains(TEXT("female")))
		{
			Score += 500;
		}

		if (NormalizedSource.Contains(TEXT("genesis")))
		{
			Score += 300;
		}

		if (NormalizedSource.Contains(TEXT("hair"))
			|| MatchesAnyHint(SourceString, Settings->HairComponentHints)
			|| MatchesAnyHint(SourceString, Settings->ExcludedHairNameTokens))
		{
			Score -= 4000;
		}

		if (NormalizedSource.Contains(TEXT("mannequin")))
		{
			Score -= 3500;
		}

		if (NormalizedSource.Contains(TEXT("charactermesh0")))
		{
			Score -= 600;
		}

		if (const ACharacter* CharacterOwner = Cast<ACharacter>(Owner))
		{
			if (CharacterOwner->GetMesh() == MeshComponent)
			{
				Score += 250;
			}
		}

		if (const USceneComponent* AttachParent = MeshComponent->GetAttachParent())
		{
			Score += 100;

			if (Cast<USkeletalMeshComponent>(AttachParent) != nullptr)
			{
				Score += 100;
			}
		}

		return Score;
	}

	static void AddConstraint(FEFMorphPhysicsConstraintGroup& Group, const TCHAR* ConstraintName)
	{
		Group.ConstraintNames.Add(FName(ConstraintName));
	}

	static void AddMorph(FEFMorphPhysicsConstraintGroup& Group, const TCHAR* MorphName, const float Weight)
	{
		Group.SourceMorphs.Add(MakeWeightedMorph(MorphName, Weight));
	}

	static TArray<FEFMorphPhysicsConstraintGroup> BuildRecommendedGroups()
	{
		TArray<FEFMorphPhysicsConstraintGroup> Groups;

		FEFMorphPhysicsConstraintGroup BreastGroup;
		BreastGroup.GroupName = TEXT("Breast");
		BreastGroup.PositiveThreshold = BreastPositiveThreshold;
		AddConstraint(BreastGroup, TEXT("l_pectoral"));
		AddConstraint(BreastGroup, TEXT("r_pectoral"));
		AddMorph(BreastGroup, TEXT("Breasts Large"), 1.0f);
		AddMorph(BreastGroup, TEXT("Breasts Large Extra"), 1.0f);
		AddMorph(BreastGroup, TEXT("Breasts Large High"), 1.0f);
		AddMorph(BreastGroup, TEXT("Breasts Diameter"), 1.0f);
		AddMorph(BreastGroup, TEXT("Breasts Width"), 1.0f);
		AddMorph(BreastGroup, TEXT("Pectorals Size"), 1.0f);
		AddMorph(BreastGroup, TEXT("Pectorals Diameter"), 0.85f);
		AddMorph(BreastGroup, TEXT("Pectorals Width"), 0.85f);
		AddMorph(BreastGroup, TEXT("Breasts Heavy"), 0.75f);
		AddMorph(BreastGroup, TEXT("Breasts Fullness Upper"), 0.75f);
		AddMorph(BreastGroup, TEXT("Breasts Fullness Lower"), 0.75f);
		Groups.Add(BreastGroup);

		FEFMorphPhysicsConstraintGroup GluteGroup;
		GluteGroup.GroupName = TEXT("Glute");
		GluteGroup.MaxLimit = GluteMaxLimit;
		GluteGroup.PositiveThreshold = GlutePositiveThreshold;
		AddConstraint(GluteGroup, TEXT("LeftGlute"));
		AddConstraint(GluteGroup, TEXT("RightGlute"));
		AddMorph(GluteGroup, TEXT("LeftGlute"), 1.0f);
		AddMorph(GluteGroup, TEXT("RightGlute"), 1.0f);
		AddMorph(GluteGroup, TEXT("Glute Size"), 1.0f);
		AddMorph(GluteGroup, TEXT("Glute Width"), 1.0f);
		AddMorph(GluteGroup, TEXT("Hip Size"), 0.85f);
		AddMorph(GluteGroup, TEXT("Glute Depth Upper"), 0.75f);
		AddMorph(GluteGroup, TEXT("Glute Depth Lower"), 0.75f);
		Groups.Add(GluteGroup);

		FEFMorphPhysicsConstraintGroup ThighGroup;
		ThighGroup.GroupName = TEXT("Thigh");
		ThighGroup.MaxLimit = ThighMaxLimit;
		ThighGroup.PositiveThreshold = ThighPositiveThreshold;
		AddConstraint(ThighGroup, TEXT("LeftThighFat"));
		AddConstraint(ThighGroup, TEXT("RightThighFat"));
		AddMorph(ThighGroup, TEXT("LeftThighFat"), 1.0f);
		AddMorph(ThighGroup, TEXT("RightThighFat"), 1.0f);
		AddMorph(ThighGroup, TEXT("Mass Thighs"), 1.0f);
		AddMorph(ThighGroup, TEXT("Thigh Depth"), 0.85f);
		AddMorph(ThighGroup, TEXT("Thigh Tone"), 0.60f);
		AddMorph(ThighGroup, TEXT("Taper Thigh A"), 0.50f);
		AddMorph(ThighGroup, TEXT("Taper Thigh B"), 0.50f);
		Groups.Add(ThighGroup);

		FEFMorphPhysicsConstraintGroup BellyGroup;
		BellyGroup.GroupName = TEXT("Belly");
		BellyGroup.MaxLimit = 5.0f;
		AddConstraint(BellyGroup, TEXT("Belly"));
		AddMorph(BellyGroup, TEXT("Belly"), 1.0f);
		AddMorph(BellyGroup, TEXT("Stomach Depth"), 1.0f);
		AddMorph(BellyGroup, TEXT("Stomach Depth Lower"), 1.0f);
		AddMorph(BellyGroup, TEXT("Mass Lower Torso"), 1.0f);
		AddMorph(BellyGroup, TEXT("Waist Width"), 0.85f);
		AddMorph(BellyGroup, TEXT("Waist Width Upper"), 0.75f);
		AddMorph(BellyGroup, TEXT("Waist Depth"), 0.75f);
		AddMorph(BellyGroup, TEXT("Stomach Soften"), 0.75f);
		AddMorph(BellyGroup, TEXT("Abdominals Width"), 0.60f);
		AddMorph(BellyGroup, TEXT("Bloat 1"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 2"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 3"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 4"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 5"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 6"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 7"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 8"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 9"), 1.0f);
		AddMorph(BellyGroup, TEXT("Bloat 10"), 1.0f);
		Groups.Add(BellyGroup);

		return Groups;
	}

	static USkeletalMeshComponent* ResolveMeshComponentByName(const AActor* Owner, const FName MeshComponentName)
	{
		if (!IsValid(Owner) || MeshComponentName.IsNone())
		{
			return nullptr;
		}

		TArray<USkeletalMeshComponent*> MeshComponents;
		Owner->GetComponents(MeshComponents);
		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (!IsValid(MeshComponent))
			{
				continue;
			}

			if (MeshComponent->GetFName() == MeshComponentName
				|| MeshComponent->GetName().Equals(MeshComponentName.ToString(), ESearchCase::IgnoreCase))
			{
				return MeshComponent;
			}
		}

		return nullptr;
	}
}

UEFMorphPhysicsConstraintComponent::UEFMorphPhysicsConstraintComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	ConstraintGroups = EFMorphPhysicsConstraintPrivate::BuildRecommendedGroups();
	ApplyProjectConstraintGroupOverrides();
}

void UEFMorphPhysicsConstraintComponent::OnRegister()
{
	Super::OnRegister();

	ApplyProjectConstraintGroupOverrides();
	BindToCustomizationComponent();
	TargetMeshComponent = ResolveTargetMeshComponent();
	RefreshResolvedTargetMeshDebugName();
	bPollMorphStateEveryTick = !BoundCustomizationComponent.IsValid();
	SetComponentTickEnabled(bPollMorphStateEveryTick || bHasPendingInterpolation);
}

void UEFMorphPhysicsConstraintComponent::BeginPlay()
{
	Super::BeginPlay();

	BindToCustomizationComponent();
	RefreshFromCurrentMorphState();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UEFMorphPhysicsConstraintComponent::RefreshFromCurrentMorphState));
	}
}

void UEFMorphPhysicsConstraintComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromCustomizationComponent();
	Super::EndPlay(EndPlayReason);
}

void UEFMorphPhysicsConstraintComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPollMorphStateEveryTick)
	{
		RefreshFromCurrentMorphState();
	}

	if (!bHasPendingInterpolation || !bEnableLimitSmoothing || LimitInterpSpeed <= 0.0f)
	{
		SetComponentTickEnabled(bPollMorphStateEveryTick);
		return;
	}

	USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
	if (!IsValid(MeshComponent))
	{
		TargetMeshComponent = ResolveTargetMeshComponent();
		RefreshResolvedTargetMeshDebugName();
		MeshComponent = TargetMeshComponent.Get();
	}

	if (!IsValid(MeshComponent) || !IsValid(MeshComponent->GetPhysicsAsset()))
	{
		bHasPendingInterpolation = false;
		SetComponentTickEnabled(bPollMorphStateEveryTick);
		return;
	}

	bool bAppliedAnyLimit = false;
	bool bStillInterpolating = false;
	ConstraintLimitDebug.Reset();

	for (const TPair<FName, float>& Pair : TargetConstraintLimits)
	{
		const float* CurrentLimitPtr = CurrentConstraintLimits.Find(Pair.Key);
		if (CurrentLimitPtr == nullptr)
		{
			ConstraintLimitDebug.Add(FString::Printf(TEXT("%s Current=<unset> Target=%.2f"),
				*Pair.Key.ToString(),
				Pair.Value));
			continue;
		}

		float NewLimit = FMath::FInterpTo(*CurrentLimitPtr, Pair.Value, DeltaTime, LimitInterpSpeed);
		if (FMath::IsNearlyEqual(NewLimit, Pair.Value, 0.01f))
		{
			NewLimit = Pair.Value;
		}

		if (!FMath::IsNearlyEqual(NewLimit, *CurrentLimitPtr, KINDA_SMALL_NUMBER))
		{
			if (ApplyConstraintLimit(MeshComponent, Pair.Key, NewLimit))
			{
				CurrentConstraintLimits.FindOrAdd(Pair.Key) = NewLimit;
				bAppliedAnyLimit = true;
			}
		}

		const float StoredLimit = CurrentConstraintLimits.FindRef(Pair.Key);
		ConstraintLimitDebug.Add(FString::Printf(TEXT("%s Current=%.2f Target=%.2f"),
			*Pair.Key.ToString(),
			StoredLimit,
			Pair.Value));

		if (!FMath::IsNearlyEqual(StoredLimit, Pair.Value, 0.01f))
		{
			bStillInterpolating = true;
		}
	}

	if (bAppliedAnyLimit)
	{
		MeshComponent->WakeAllRigidBodies();
	}

	bHasPendingInterpolation = bStillInterpolating;
	SetComponentTickEnabled(bHasPendingInterpolation || bPollMorphStateEveryTick);
}

void UEFMorphPhysicsConstraintComponent::RefreshFromCurrentMorphState()
{
	BindToCustomizationComponent();
	bPollMorphStateEveryTick = !BoundCustomizationComponent.IsValid();

	GroupDebugSummary.Reset();
	ConstraintLimitDebug.Reset();
	bHasPendingInterpolation = false;
	SetComponentTickEnabled(bPollMorphStateEveryTick);

	USkeletalMeshComponent* PreviousTargetMesh = TargetMeshComponent.Get();
	TargetMeshComponent = ResolveTargetMeshComponent();
	RefreshResolvedTargetMeshDebugName();
	if (PreviousTargetMesh != TargetMeshComponent.Get())
	{
		CurrentConstraintLimits.Reset();
	}

	USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
	if (!IsValid(MeshComponent) || !IsValid(MeshComponent->GetPhysicsAsset()))
	{
		if (bLogSetupWarnings && !bLoggedMissingMeshWarning)
		{
			UE_LOG(LogTemp, Warning, TEXT("EF Morph Physics Constraint Driver on %s could not resolve a skeletal mesh with a valid physics asset."),
				*GetNameSafe(GetOwner()));
			bLoggedMissingMeshWarning = true;
		}
		return;
	}

	bLoggedMissingMeshWarning = false;
	GroupDebugSummary.Add(BoundCustomizationComponent.IsValid()
		? TEXT("MorphSource=CustomizationComponent+Mesh")
		: TEXT("MorphSource=MeshComponentFallback"));

	TMap<FString, float> MorphLookup;
	RebuildMorphLookup(MorphLookup, MeshComponent);

	TargetConstraintLimits.Reset();
	for (const FEFMorphPhysicsConstraintGroup& Group : ConstraintGroups)
	{
		FString DebugLine;
		const float TargetLimit = ComputeGroupTargetLimit(Group, MorphLookup, DebugLine);
		GroupDebugSummary.Add(DebugLine);

		for (const FName ConstraintName : Group.ConstraintNames)
		{
			if (!ConstraintName.IsNone())
			{
				TargetConstraintLimits.Add(ConstraintName, TargetLimit);
			}
		}
	}

	for (auto It = CurrentConstraintLimits.CreateIterator(); It; ++It)
	{
		if (!TargetConstraintLimits.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	bool bAppliedAnyLimit = false;
	for (const TPair<FName, float>& Pair : TargetConstraintLimits)
	{
		const float* CurrentLimitPtr = CurrentConstraintLimits.Find(Pair.Key);
		const bool bApplyImmediately = !bEnableLimitSmoothing || LimitInterpSpeed <= 0.0f || CurrentLimitPtr == nullptr;

		if (bApplyImmediately)
		{
			if (ApplyConstraintLimit(MeshComponent, Pair.Key, Pair.Value))
			{
				CurrentConstraintLimits.FindOrAdd(Pair.Key) = Pair.Value;
				bAppliedAnyLimit = true;
			}
			else if (bLogSetupWarnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("EF Morph Physics Constraint Driver on %s could not find constraint %s on mesh %s."),
					*GetNameSafe(GetOwner()),
					*Pair.Key.ToString(),
					*GetNameSafe(MeshComponent));
			}
		}
		else if (!FMath::IsNearlyEqual(*CurrentLimitPtr, Pair.Value, 0.01f))
		{
			bHasPendingInterpolation = true;
		}
	}

	ConstraintLimitDebug.Reset();
	for (const TPair<FName, float>& Pair : TargetConstraintLimits)
	{
		const float CurrentLimit = CurrentConstraintLimits.Contains(Pair.Key) ? CurrentConstraintLimits.FindRef(Pair.Key) : Pair.Value;
		ConstraintLimitDebug.Add(FString::Printf(TEXT("%s Current=%.2f Target=%.2f"),
			*Pair.Key.ToString(),
			CurrentLimit,
			Pair.Value));
	}

	if (bAppliedAnyLimit)
	{
		MeshComponent->WakeAllRigidBodies();
	}

	SetComponentTickEnabled(bHasPendingInterpolation || bPollMorphStateEveryTick);
}

void UEFMorphPhysicsConstraintComponent::ResetToRecommendedConstraintGroups()
{
	ConstraintGroups = EFMorphPhysicsConstraintPrivate::BuildRecommendedGroups();
	ApplyProjectConstraintGroupOverrides();
	RefreshFromCurrentMorphState();
}

void UEFMorphPhysicsConstraintComponent::ApplyProjectConstraintGroupOverrides()
{
	for (FEFMorphPhysicsConstraintGroup& Group : ConstraintGroups)
	{
		if (Group.GroupName == TEXT("Glute"))
		{
			Group.MaxLimit = EFMorphPhysicsConstraintPrivate::GluteMaxLimit;
			Group.PositiveThreshold = EFMorphPhysicsConstraintPrivate::GlutePositiveThreshold;
		}
		else if (Group.GroupName == TEXT("Thigh"))
		{
			Group.MaxLimit = EFMorphPhysicsConstraintPrivate::ThighMaxLimit;
			Group.PositiveThreshold = EFMorphPhysicsConstraintPrivate::ThighPositiveThreshold;
		}
	}
}

FString UEFMorphPhysicsConstraintComponent::GetDebugSummary() const
{
	TArray<FString> Lines;
	Lines.Reserve(GroupDebugSummary.Num() + ConstraintLimitDebug.Num() + 1);
	Lines.Add(FString::Printf(TEXT("TargetMesh=%s"), *ResolvedTargetMeshDebugName.ToString()));
	Lines.Append(GroupDebugSummary);
	Lines.Append(ConstraintLimitDebug);
	return FString::Join(Lines, TEXT("\n"));
}

void UEFMorphPhysicsConstraintComponent::HandleMorphStateApplied()
{
	RefreshFromCurrentMorphState();
}

void UEFMorphPhysicsConstraintComponent::BindToCustomizationComponent()
{
	UEFCharacterCustomizationComponent* ResolvedCustomizationComponent = ResolveCustomizationComponent();
	if (ResolvedCustomizationComponent == BoundCustomizationComponent.Get())
	{
		return;
	}

	UnbindFromCustomizationComponent();
	BoundCustomizationComponent = ResolvedCustomizationComponent;

	if (ResolvedCustomizationComponent != nullptr)
	{
		ResolvedCustomizationComponent->OnMorphStateApplied().AddUObject(this, &UEFMorphPhysicsConstraintComponent::HandleMorphStateApplied);
	}
}

void UEFMorphPhysicsConstraintComponent::UnbindFromCustomizationComponent()
{
	if (UEFCharacterCustomizationComponent* CustomizationComponent = BoundCustomizationComponent.Get())
	{
		CustomizationComponent->OnMorphStateApplied().RemoveAll(this);
	}

	BoundCustomizationComponent = nullptr;
}

UEFCharacterCustomizationComponent* UEFMorphPhysicsConstraintComponent::ResolveCustomizationComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UEFCharacterCustomizationComponent>() : nullptr;
}

USkeletalMeshComponent* UEFMorphPhysicsConstraintComponent::ResolveTargetMeshComponent() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	if (!TargetMeshComponentName.IsNone())
	{
		return EFMorphPhysicsConstraintPrivate::ResolveMeshComponentByName(Owner, TargetMeshComponentName);
	}

	if (bPreferCharacterCustomizationBodyMesh)
	{
		UEFCharacterCustomizationComponent* CustomizationComponent = BoundCustomizationComponent.IsValid()
			? BoundCustomizationComponent.Get()
			: ResolveCustomizationComponent();
		if (CustomizationComponent != nullptr)
		{
			USkeletalMeshComponent* BestCustomizationMesh = nullptr;
			int32 BestCustomizationScore = MIN_int32;

			auto TryCustomizationCandidate = [&](USkeletalMeshComponent* Candidate)
			{
				if (!IsValid(Candidate))
				{
					return;
				}

				const int32 CandidateScore = EFMorphPhysicsConstraintPrivate::ScoreMeshComponentForMorphPhysics(Owner, Candidate) + 5000;
				if (BestCustomizationMesh == nullptr || CandidateScore > BestCustomizationScore)
				{
					BestCustomizationMesh = Candidate;
					BestCustomizationScore = CandidateScore;
				}
			};

			TryCustomizationCandidate(CustomizationComponent->GetBodyMeshComponent());
			TryCustomizationCandidate(CustomizationComponent->GetBodyMeshSelectionComponent());

			if (BestCustomizationMesh != nullptr)
			{
				return BestCustomizationMesh;
			}
		}
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Owner->GetComponents(MeshComponents);
	USkeletalMeshComponent* BestMeshComponent = nullptr;
	int32 BestScore = MIN_int32;
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent))
		{
			continue;
		}

		const int32 CandidateScore = EFMorphPhysicsConstraintPrivate::ScoreMeshComponentForMorphPhysics(Owner, MeshComponent);
		if (BestMeshComponent == nullptr || CandidateScore > BestScore)
		{
			BestMeshComponent = MeshComponent;
			BestScore = CandidateScore;
		}
	}

	return BestMeshComponent;
}

void UEFMorphPhysicsConstraintComponent::RebuildMorphLookup(TMap<FString, float>& OutMorphLookup, const USkeletalMeshComponent* MeshComponent) const
{
	OutMorphLookup.Reset();

	if (const UEFCharacterCustomizationComponent* CustomizationComponent = BoundCustomizationComponent.Get())
	{
		const FCharacterCustomizationState State = CustomizationComponent->CaptureCurrentState();
		for (const FCharacterMorphValue& MorphValue : State.MorphValues)
		{
			if (MorphValue.Target == ECharacterCustomizationTarget::Clothing || MorphValue.MorphName.IsNone())
			{
				continue;
			}

			const FString MorphKey = EFMorphPhysicsConstraintPrivate::NormalizeMorphKey(MorphValue.MorphName.ToString());
			if (MorphKey.IsEmpty())
			{
				continue;
			}

			const float ClampedValue = FMath::Clamp(MorphValue.Value, -1.0f, 1.0f);
			if (const float* ExistingValue = OutMorphLookup.Find(MorphKey))
			{
				if (FMath::Abs(ClampedValue) > FMath::Abs(*ExistingValue))
				{
					OutMorphLookup.Add(MorphKey, ClampedValue);
				}
			}
			else
			{
				OutMorphLookup.Add(MorphKey, ClampedValue);
			}
		}
	}

	if (!IsValid(MeshComponent))
	{
		return;
	}

	for (const FEFMorphPhysicsConstraintGroup& Group : ConstraintGroups)
	{
		for (const FEFMorphPhysicsWeightedMorph& SourceMorph : Group.SourceMorphs)
		{
			if (SourceMorph.MorphName.IsNone())
			{
				continue;
			}

			const FString MorphKey = EFMorphPhysicsConstraintPrivate::NormalizeMorphKey(SourceMorph.MorphName.ToString());
			if (MorphKey.IsEmpty())
			{
				continue;
			}

			const float MeshMorphValue = FMath::Clamp(MeshComponent->GetMorphTarget(SourceMorph.MorphName), -1.0f, 1.0f);
			if (const float* ExistingValue = OutMorphLookup.Find(MorphKey))
			{
				if (FMath::Abs(MeshMorphValue) > FMath::Abs(*ExistingValue) || FMath::IsNearlyZero(*ExistingValue, KINDA_SMALL_NUMBER))
				{
					OutMorphLookup.Add(MorphKey, MeshMorphValue);
				}
			}
			else
			{
				OutMorphLookup.Add(MorphKey, MeshMorphValue);
			}
		}
	}
}

float UEFMorphPhysicsConstraintComponent::ComputeGroupTargetLimit(const FEFMorphPhysicsConstraintGroup& Group, const TMap<FString, float>& MorphLookup, FString& OutDebugLine) const
{
	float PositiveSum = 0.0f;
	float NegativeSum = 0.0f;

	for (const FEFMorphPhysicsWeightedMorph& SourceMorph : Group.SourceMorphs)
	{
		if (SourceMorph.MorphName.IsNone())
		{
			continue;
		}

		const FString MorphKey = EFMorphPhysicsConstraintPrivate::NormalizeMorphKey(SourceMorph.MorphName.ToString());
		const float RawValue = MorphLookup.FindRef(MorphKey);
		float WeightedValue = FMath::Clamp(RawValue, -1.0f, 1.0f) * FMath::Max(SourceMorph.Weight, 0.0f);
		if (FMath::Abs(WeightedValue) < Group.MorphDeadZone)
		{
			WeightedValue = 0.0f;
		}

		if (WeightedValue > 0.0f)
		{
			PositiveSum += WeightedValue;
		}
		else if (WeightedValue < 0.0f)
		{
			NegativeSum += FMath::Abs(WeightedValue);
		}
	}

	const float PositiveFill = FMath::Clamp(PositiveSum / FMath::Max(Group.PositiveThreshold, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float NegativeFill = FMath::Clamp(NegativeSum / FMath::Max(Group.NegativeThreshold, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	const float Balance = PositiveFill - NegativeFill;

	float TargetLimit = Group.DefaultLimit;
	if (!FMath::IsNearlyZero(Balance, Group.BalanceDeadZone))
	{
		if (Balance > 0.0f)
		{
			TargetLimit = FMath::Lerp(Group.DefaultLimit, Group.MaxLimit, Balance);
		}
		else
		{
			TargetLimit = FMath::Lerp(Group.DefaultLimit, Group.MinLimit, -Balance);
		}
	}

	TargetLimit = FMath::Clamp(TargetLimit, FMath::Min(Group.MinLimit, Group.MaxLimit), FMath::Max(Group.MinLimit, Group.MaxLimit));

	OutDebugLine = FString::Printf(TEXT("%s Pos=%.2f/%.2f Neg=%.2f/%.2f Fill=%.2f/%.2f Balance=%.2f Limit=%.2f"),
		*Group.GroupName.ToString(),
		PositiveSum,
		Group.PositiveThreshold,
		NegativeSum,
		Group.NegativeThreshold,
		PositiveFill,
		NegativeFill,
		Balance,
		TargetLimit);

	return TargetLimit;
}

bool UEFMorphPhysicsConstraintComponent::ApplyConstraintLimit(USkeletalMeshComponent* MeshComponent, const FName ConstraintName, const float NewLimit) const
{
	if (!IsValid(MeshComponent) || ConstraintName.IsNone())
	{
		return false;
	}

	if (FConstraintInstance* ConstraintInstance = MeshComponent->FindConstraintInstance(ConstraintName))
	{
		ConstraintInstance->SetLinearLimitSize(NewLimit);
		return true;
	}

	FConstraintInstanceAccessor ConstraintAccessor = MeshComponent->GetConstraintByName(ConstraintName, true);
	if (FConstraintInstance* ConstraintInstance = ConstraintAccessor.Get())
	{
		ConstraintInstance->SetLinearLimitSize(NewLimit);
		return true;
	}

	const int32 ConstraintIndex = MeshComponent->FindConstraintIndex(ConstraintName);
	if (ConstraintIndex != INDEX_NONE)
	{
		if (FConstraintInstance* ConstraintInstance = MeshComponent->GetConstraintInstanceByIndex(ConstraintIndex))
		{
			ConstraintInstance->SetLinearLimitSize(NewLimit);
			return true;
		}
	}

	return false;
}

void UEFMorphPhysicsConstraintComponent::RefreshResolvedTargetMeshDebugName()
{
	ResolvedTargetMeshDebugName = TargetMeshComponent.IsValid() ? TargetMeshComponent->GetFName() : NAME_None;
}
