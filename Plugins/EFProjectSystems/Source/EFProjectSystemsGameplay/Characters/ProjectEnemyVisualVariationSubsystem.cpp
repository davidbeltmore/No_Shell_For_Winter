#include "Characters/ProjectEnemyVisualVariationSubsystem.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyVisualVariationSelection.h"
#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "EFCharacterCustomizationComponent.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEnemyVisualVariation, Log, All);

namespace ProjectEnemyVisualVariationSubsystemPrivate
{
	static TObjectKey<UObject> MakeObjectKey(const UObject* Object)
	{
		return TObjectKey<UObject>(const_cast<UObject*>(Object));
	}

	static TObjectKey<APawn> MakePawnKey(const APawn* Pawn)
	{
		return TObjectKey<APawn>(const_cast<APawn*>(Pawn));
	}

	static FString NormalizeMorphName(const FString& Value)
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

	static void AddMorphNameCandidate(TArray<FName>& Candidates, const FName Candidate)
	{
		if (!Candidate.IsNone())
		{
			Candidates.AddUnique(Candidate);
		}
	}

	static TArray<FName> BuildMorphNameCandidates(const FName PrimaryMorphName)
	{
		TArray<FName> Candidates;
		AddMorphNameCandidate(Candidates, PrimaryMorphName);
		return Candidates;
	}

	static bool CombinedMaterialNameContainsHint(const FString& CombinedMaterialName, const FString& Hint)
	{
		return !Hint.IsEmpty() && CombinedMaterialName.Contains(Hint, ESearchCase::IgnoreCase);
	}

	static void ApplyNativeSkinColorToPawnMeshes(
		APawn* Pawn,
		const UProjectEnemyVisualVariationSettings& Settings,
		const FLinearColor& SkinColor)
	{
		if (!IsValid(Pawn)
			|| !Settings.bApplySkinColorNatively
			|| Settings.EnemySkinColorParameterNames.IsEmpty()
			|| Settings.EnemySkinMaterialHints.IsEmpty())
		{
			return;
		}

		TInlineComponentArray<USkeletalMeshComponent*> MeshComponents(Pawn);
		Pawn->GetComponents(MeshComponents);

		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (!IsValid(MeshComponent))
			{
				continue;
			}

			const TArray<FName> MaterialSlotNames = MeshComponent->GetMaterialSlotNames();
			for (int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex)
			{
				const FString SlotName = MaterialSlotNames.IsValidIndex(MaterialIndex) ? MaterialSlotNames[MaterialIndex].ToString() : FString();
				UMaterialInterface* MaterialInterface = MeshComponent->GetMaterial(MaterialIndex);
				const FString MaterialName = IsValid(MaterialInterface) ? MaterialInterface->GetName() : FString();
				const FString MaterialPath = IsValid(MaterialInterface) ? MaterialInterface->GetPathName() : FString();
				const FString CombinedMaterialName = FString::Printf(TEXT("%s %s %s"), *SlotName, *MaterialName, *MaterialPath);

				const bool bMatchesSkinMaterial = Settings.EnemySkinMaterialHints.ContainsByPredicate(
					[&CombinedMaterialName](const FString& Hint)
					{
						return CombinedMaterialNameContainsHint(CombinedMaterialName, Hint);
					});
				if (!bMatchesSkinMaterial)
				{
					continue;
				}

				if (UMaterialInstanceDynamic* DynamicMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamic(MaterialIndex))
				{
					for (const FName ParameterName : Settings.EnemySkinColorParameterNames)
					{
						if (!ParameterName.IsNone())
						{
							DynamicMaterial->SetVectorParameterValue(ParameterName, SkinColor);
						}
					}
				}
			}
		}
	}
}

void UProjectEnemyVisualVariationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TargetEnemyClasses.Reset();
	OptionalMatureMorphTargetEnemyClasses.Reset();
	AllowedMorphNameSet.Reset();
	ProcessedActors.Reset();
	PendingActors.Reset();
	TrackedOptionalMatureMorphStates.Reset();
	ContentPolicySubsystem.Reset();
	bInitialPawnScanPending = true;
	bOptionalMatureMorphPolicyAllowed = false;
	LastOptionalMatureVisibilityPollTimeSeconds = -1.0;

	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	for (const TSoftClassPtr<APawn>& TargetEnemyClass : Settings->TargetEnemyClasses)
	{
		if (UClass* ResolvedClass = TargetEnemyClass.LoadSynchronous())
		{
			TargetEnemyClasses.AddUnique(ResolvedClass);
		}
		else
		{
			UE_LOG(
				LogProjectEnemyVisualVariation,
				Warning,
				TEXT("Could not resolve target enemy class '%s'."),
				*TargetEnemyClass.ToSoftObjectPath().ToString());
		}
	}

	for (const TSoftClassPtr<APawn>& TargetEnemyClass : Settings->OptionalMatureMorphTargetEnemyClasses)
	{
		if (UClass* ResolvedClass = TargetEnemyClass.LoadSynchronous())
		{
			OptionalMatureMorphTargetEnemyClasses.AddUnique(ResolvedClass);
		}
		else
		{
			UE_LOG(
				LogProjectEnemyVisualVariation,
				Warning,
				TEXT("Could not resolve optional mature morph target enemy class '%s'."),
				*TargetEnemyClass.ToSoftObjectPath().ToString());
		}
	}

	for (const FName MorphName : Settings->AllowedMorphNames)
	{
		if (!MorphName.IsNone())
		{
			AllowedMorphNameSet.Add(MorphName);
		}
	}

	if (UWorld* World = GetWorld(); IsValid(World) && World->IsGameWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &UProjectEnemyVisualVariationSubsystem::HandleActorSpawned));

		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UProjectContentPolicySubsystem* Policy = GameInstance->GetSubsystem<UProjectContentPolicySubsystem>())
			{
				ContentPolicySubsystem = Policy;
				bOptionalMatureMorphPolicyAllowed = IsOptionalMatureMorphPresentationAllowed();
				Policy->OnContentPolicyChanged.AddDynamic(
					this,
					&ThisClass::HandleContentPolicyChanged);
			}
		}
	}
}

void UProjectEnemyVisualVariationSubsystem::Deinitialize()
{
	if (UProjectContentPolicySubsystem* Policy = ContentPolicySubsystem.Get())
	{
		Policy->OnContentPolicyChanged.RemoveAll(this);
	}

	if (UWorld* World = GetWorld(); IsValid(World) && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	ActorSpawnedHandle.Reset();
	TargetEnemyClasses.Reset();
	OptionalMatureMorphTargetEnemyClasses.Reset();
	AllowedMorphNameSet.Reset();
	ProcessedActors.Reset();
	PendingActors.Reset();
	TrackedOptionalMatureMorphStates.Reset();
	ContentPolicySubsystem.Reset();
	bInitialPawnScanPending = false;
	bOptionalMatureMorphPolicyAllowed = false;
	LastOptionalMatureVisibilityPollTimeSeconds = -1.0;

	Super::Deinitialize();
}

void UProjectEnemyVisualVariationSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return;
	}

	if (bInitialPawnScanPending)
	{
		ProcessExistingPawns();
		bInitialPawnScanPending = false;
	}

	CleanupTrackedOptionalMatureMorphStates();

	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	const bool bPresentationAllowed = IsOptionalMatureMorphPresentationAllowed();
	if (!bPresentationAllowed)
	{
		if (bOptionalMatureMorphPolicyAllowed)
		{
			bOptionalMatureMorphPolicyAllowed = false;
			DisableOptionalMatureMorphsForExistingPawns();
		}
		return;
	}

	if (!Settings || TrackedOptionalMatureMorphStates.IsEmpty())
	{
		return;
	}

	const double CurrentTimeSeconds = World->GetTimeSeconds();
	const double VisibilityPollIntervalSeconds = FMath::Max(
		static_cast<double>(Settings->OptionalMatureVisibilityCheckIntervalSeconds),
		0.01);
	if (LastOptionalMatureVisibilityPollTimeSeconds < 0.0
		|| (CurrentTimeSeconds - LastOptionalMatureVisibilityPollTimeSeconds) >= VisibilityPollIntervalSeconds)
	{
		UpdateSessionDrivenOptionalMatureMorphs(static_cast<float>(CurrentTimeSeconds));
		LastOptionalMatureVisibilityPollTimeSeconds = CurrentTimeSeconds;
	}

	AdvanceOptionalMatureMorphs(DeltaTime);
}

TStatId UProjectEnemyVisualVariationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectEnemyVisualVariationSubsystem, STATGROUP_Tickables);
}

bool UProjectEnemyVisualVariationSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr
		&& World->IsGameWorld()
		&& (bInitialPawnScanPending || TrackedOptionalMatureMorphStates.Num() > 0);
}

bool UProjectEnemyVisualVariationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
	const bool bFeatureEnabled,
	const FProjectContentPolicySnapshot& Policy)
{
	return bFeatureEnabled && FProjectContentPolicyRules::IsIntimacyAllowed(Policy);
}

bool UProjectEnemyVisualVariationSubsystem::ShouldUseActiveOptionalMatureMorph(
	const bool bPresentationAllowed,
	const bool bActorInActiveIntimacySession,
	const bool bActorDead)
{
	return bPresentationAllowed
		&& bActorInActiveIntimacySession
		&& !bActorDead;
}

bool UProjectEnemyVisualVariationSubsystem::IsClassEligibleForVisualVariation(
	const UClass* ActorClass,
	const TArray<TSubclassOf<APawn>>& StandardTargetClasses,
	const TArray<TSubclassOf<APawn>>& OptionalMatureTargetClasses)
{
	if (!IsValid(ActorClass))
	{
		return false;
	}

	const auto MatchesRegisteredClass = [ActorClass](const TArray<TSubclassOf<APawn>>& RegisteredClasses)
	{
		for (const TSubclassOf<APawn>& RegisteredClass : RegisteredClasses)
		{
			if (RegisteredClass && ActorClass->IsChildOf(RegisteredClass.Get()))
			{
				return true;
			}
		}

		return false;
	};

	return MatchesRegisteredClass(StandardTargetClasses)
		|| MatchesRegisteredClass(OptionalMatureTargetClasses);
}

void UProjectEnemyVisualVariationSubsystem::ProcessExistingPawns()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		if (ShouldProcessPawn(*It))
		{
			QueueVariationApplication(*It, 0);
		}
	}
}

void UProjectEnemyVisualVariationSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	APawn* SpawnedPawn = Cast<APawn>(SpawnedActor);
	if (!ShouldProcessPawn(SpawnedPawn))
	{
		return;
	}

	QueueVariationApplication(SpawnedPawn, 0);
}

void UProjectEnemyVisualVariationSubsystem::QueueVariationApplication(APawn* Pawn, const int32 AttemptIndex)
{
	if (!IsValid(Pawn) || IsActorProcessed(Pawn) || IsActorPending(Pawn))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	MarkActorPending(Pawn);
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &UProjectEnemyVisualVariationSubsystem::TryApplyVariation, TWeakObjectPtr<APawn>(Pawn), AttemptIndex));
}

void UProjectEnemyVisualVariationSubsystem::TryApplyVariation(TWeakObjectPtr<APawn> PawnPtr, const int32 AttemptIndex)
{
	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(Pawn))
	{
		return;
	}

	ClearActorPending(Pawn);

	if (IsActorProcessed(Pawn) || !ShouldProcessPawn(Pawn))
	{
		return;
	}

	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	const int32 MaxRetryCount = FMath::Max(Settings->InitializationRetryCount, 0);

	auto RetryOrFail = [this, Pawn, AttemptIndex, MaxRetryCount](const FString& FailureReason)
	{
		if (AttemptIndex < MaxRetryCount)
		{
			QueueVariationApplication(Pawn, AttemptIndex + 1);
			return;
		}

		UE_LOG(
			LogProjectEnemyVisualVariation,
			Warning,
			TEXT("Enemy visual variation skipped for %s after %d attempts: %s"),
			*GetNameSafe(Pawn),
			AttemptIndex + 1,
			*FailureReason);

		MarkActorProcessed(Pawn);
	};

	UEFCharacterCustomizationComponent* CustomizationComponent = FindOrCreateCustomizationComponent(Pawn);
	if (!IsValid(CustomizationComponent))
	{
		RetryOrFail(TEXT("Could not create or find UEFCharacterCustomizationComponent."));
		return;
	}

	FString CompatibilityFailureReason;
	if (!CustomizationComponent->EvaluateCompatibilityForActor(Pawn, CompatibilityFailureReason))
	{
		RetryOrFail(CompatibilityFailureReason.IsEmpty()
			? TEXT("The spawned pawn is not yet compatible with the customization runtime.")
			: CompatibilityFailureReason);
		return;
	}

	TArray<FMorphSliderEntry> AllowedEntries;
	if (!GatherAllowedMorphEntries(CustomizationComponent, AllowedEntries))
	{
		RetryOrFail(TEXT("None of the allowed test morphs were discovered on the spawned pawn."));
		return;
	}

	for (const FMorphSliderEntry& Entry : AllowedEntries)
	{
		CustomizationComponent->ApplyMorph(Entry, 0.0f);
	}

	FProjectEnemyMorphSelectionContext SelectionContext;
	if (const UProjectEnemyLevelComponent* LevelComponent = Pawn->FindComponentByClass<UProjectEnemyLevelComponent>())
	{
		if (LevelComponent->HasAssignedLevel())
		{
			SelectionContext.bHasNormalizedEnemyLevel = true;
			SelectionContext.NormalizedEnemyLevel = LevelComponent->GetNormalizedLevel();
		}
	}

	FProjectEnemyMorphRollResult RollResult;
	if (!FProjectEnemyVisualVariationSelection::RollMorphVariation(AllowedEntries, *Settings, SelectionContext, RollResult)
		|| !RollResult.bIsValid
		|| !AllowedEntries.IsValidIndex(RollResult.SelectedEntryIndex))
	{
		RetryOrFail(TEXT("Could not roll a valid morph variation."));
		return;
	}

	const FMorphSliderEntry& SelectedEntry = AllowedEntries[RollResult.SelectedEntryIndex];
	CustomizationComponent->ApplyMorph(SelectedEntry, RollResult.MorphValue);
	const bool bIsOptionalMatureMorphTarget = IsOptionalMatureMorphTargetClass(Pawn->GetClass());
	if (bIsOptionalMatureMorphTarget)
	{
		ApplyConfiguredOptionalMatureMorphGroups(Pawn, CustomizationComponent);
	}

	const float SkinBrightnessMin = FMath::Min(Settings->SkinBrightnessMin, Settings->SkinBrightnessMax);
	const float SkinBrightnessMax = FMath::Max(Settings->SkinBrightnessMin, Settings->SkinBrightnessMax);
	const float SkinBrightness = FMath::FRandRange(SkinBrightnessMin, SkinBrightnessMax);
	const FLinearColor SkinColor(SkinBrightness, SkinBrightness, SkinBrightness, 1.0f);
	CustomizationComponent->SetSkinColor(SkinColor);
	ProjectEnemyVisualVariationSubsystemPrivate::ApplyNativeSkinColorToPawnMeshes(Pawn, *Settings, SkinColor);

	if (bIsOptionalMatureMorphTarget)
	{
		InitializeOptionalMatureMorphState(Pawn, CustomizationComponent);
	}
	MarkActorProcessed(Pawn);
}

bool UProjectEnemyVisualVariationSubsystem::ShouldProcessPawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	if (Pawn->IsTemplate() || Pawn->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	if (Pawn->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (!IsClassEligibleForVisualVariation(
		Pawn->GetClass(),
		TargetEnemyClasses,
		OptionalMatureMorphTargetEnemyClasses))
	{
		return false;
	}

	return true;
}

bool UProjectEnemyVisualVariationSubsystem::IsTargetEnemyClass(const UClass* ActorClass) const
{
	if (!IsValid(ActorClass))
	{
		return false;
	}

	for (const TSubclassOf<APawn>& TargetEnemyClass : TargetEnemyClasses)
	{
		if (TargetEnemyClass && ActorClass->IsChildOf(TargetEnemyClass.Get()))
		{
			return true;
		}
	}

	return false;
}

bool UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphTargetClass(const UClass* ActorClass) const
{
	if (!IsValid(ActorClass))
	{
		return false;
	}

	for (const TSubclassOf<APawn>& TargetEnemyClass : OptionalMatureMorphTargetEnemyClasses)
	{
		if (TargetEnemyClass && ActorClass->IsChildOf(TargetEnemyClass.Get()))
		{
			return true;
		}
	}

	return false;
}

UEFCharacterCustomizationComponent* UProjectEnemyVisualVariationSubsystem::FindOrCreateCustomizationComponent(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	if (UEFCharacterCustomizationComponent* ExistingComponent = Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>())
	{
		if (!ExistingComponent->IsRegistered())
		{
			ExistingComponent->RegisterComponent();
		}

		return ExistingComponent;
	}

	UEFCharacterCustomizationComponent* NewComponent = NewObject<UEFCharacterCustomizationComponent>(Pawn, TEXT("CharacterCustomizationComponent"));
	if (!IsValid(NewComponent))
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(NewComponent);
	NewComponent->RegisterComponent();
	return NewComponent;
}

bool UProjectEnemyVisualVariationSubsystem::GatherAllowedMorphEntries(
	const UEFCharacterCustomizationComponent* CustomizationComponent,
	TArray<FMorphSliderEntry>& OutEntries) const
{
	OutEntries.Reset();

	if (!IsValid(CustomizationComponent))
	{
		return false;
	}

	for (const FMorphSliderEntry& Entry : CustomizationComponent->GetAvailableMorphEntries())
	{
		if (AllowedMorphNameSet.Contains(Entry.MorphName))
		{
			OutEntries.Add(Entry);
		}
	}

	return OutEntries.Num() > 0;
}

bool UProjectEnemyVisualVariationSubsystem::GatherMorphEntriesForConfiguredName(
	const UEFCharacterCustomizationComponent* CustomizationComponent,
	const FName MorphName,
	TArray<FMorphSliderEntry>& OutEntries) const
{
	OutEntries.Reset();

	if (!IsValid(CustomizationComponent) || MorphName.IsNone())
	{
		return false;
	}

	const TArray<FName> CandidateNames = ProjectEnemyVisualVariationSubsystemPrivate::BuildMorphNameCandidates(MorphName);
	TSet<FString> NormalizedCandidates;
	for (const FName CandidateName : CandidateNames)
	{
		NormalizedCandidates.Add(ProjectEnemyVisualVariationSubsystemPrivate::NormalizeMorphName(CandidateName.ToString()));
	}

	for (const FMorphSliderEntry& Entry : CustomizationComponent->GetAvailableMorphEntries())
	{
		if (CandidateNames.Contains(Entry.MorphName)
			|| NormalizedCandidates.Contains(ProjectEnemyVisualVariationSubsystemPrivate::NormalizeMorphName(Entry.MorphName.ToString())))
		{
			OutEntries.Add(Entry);
		}
	}

	return OutEntries.Num() > 0;
}

void UProjectEnemyVisualVariationSubsystem::ApplyConfiguredOptionalMatureMorphGroups(
	APawn* Pawn,
	UEFCharacterCustomizationComponent* CustomizationComponent) const
{
	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	if (!Settings
		|| !IsValid(Pawn)
		|| !IsValid(CustomizationComponent))
	{
		return;
	}

	const int32 EnemyLevel = ResolveEnemyLevel(Pawn);

	auto GatherConfiguredMorphEntries =
		[this, CustomizationComponent](const TArray<FName>& MorphNames, TMap<FName, TArray<FMorphSliderEntry>>& OutEntriesByMorph, TArray<FName>& OutAvailableMorphNames)
	{
		OutEntriesByMorph.Reset();
		OutAvailableMorphNames.Reset();

		for (const FName MorphName : MorphNames)
		{
			TArray<FMorphSliderEntry> MatchingEntries;
			if (GatherMorphEntriesForConfiguredName(CustomizationComponent, MorphName, MatchingEntries))
			{
				OutAvailableMorphNames.Add(MorphName);
				OutEntriesByMorph.Add(MorphName, MoveTemp(MatchingEntries));
			}
		}
	};

	TArray<FName> GroupOneConfiguredNames;
	GroupOneConfiguredNames.Reserve(Settings->OptionalMatureGroupOneMorphEntries.Num());
	for (const FProjectEnemyLevelBiasedMorphEntry& GroupOneEntry : Settings->OptionalMatureGroupOneMorphEntries)
	{
		if (!GroupOneEntry.MorphName.IsNone())
		{
			GroupOneConfiguredNames.AddUnique(GroupOneEntry.MorphName);
		}
	}

	TMap<FName, TArray<FMorphSliderEntry>> GroupOneEntriesByMorph;
	TArray<FName> GroupOneAvailableMorphNames;
	GatherConfiguredMorphEntries(GroupOneConfiguredNames, GroupOneEntriesByMorph, GroupOneAvailableMorphNames);

	FProjectEnemyNamedMorphRollResult GroupOneRollResult;
	if (FProjectEnemyVisualVariationSelection::RollLevelBiasedPositiveMorph(
		GroupOneAvailableMorphNames,
		Settings->OptionalMatureGroupOneMorphEntries,
		EnemyLevel,
		GroupOneRollResult))
	{
		if (const TArray<FMorphSliderEntry>* SelectedEntries = GroupOneEntriesByMorph.Find(GroupOneRollResult.MorphName))
		{
			ApplyMorphEntriesWithValue(CustomizationComponent, *SelectedEntries, GroupOneRollResult.MorphValue);
		}
	}

	TMap<FName, TArray<FMorphSliderEntry>> GroupTwoEntriesByMorph;
	TArray<FName> GroupTwoAvailableMorphNames;
	GatherConfiguredMorphEntries(
		Settings->OptionalMatureGroupTwoMorphNames,
		GroupTwoEntriesByMorph,
		GroupTwoAvailableMorphNames);

	FProjectEnemyNamedMorphRollResult GroupTwoRollResult;
	if (FProjectEnemyVisualVariationSelection::RollBandBiasedPositiveMorph(
		GroupTwoAvailableMorphNames,
		Settings->OptionalMatureGroupTwoHighValueChance,
		Settings->OptionalMatureGroupTwoHighValueMin,
		Settings->OptionalMatureGroupTwoHighValueMax,
		Settings->OptionalMatureGroupTwoLowValueMin,
		Settings->OptionalMatureGroupTwoLowValueMax,
		GroupTwoRollResult))
	{
		if (const TArray<FMorphSliderEntry>* SelectedEntries = GroupTwoEntriesByMorph.Find(GroupTwoRollResult.MorphName))
		{
			ApplyMorphEntriesWithValue(CustomizationComponent, *SelectedEntries, GroupTwoRollResult.MorphValue);
		}
	}

	const float GroupThreeValue = FProjectEnemyVisualVariationSelection::EvaluateLinearLevelAlpha(
		EnemyLevel,
		Settings->OptionalMatureGroupThreeStartLevel,
		Settings->OptionalMatureGroupThreeMaxLevel);

	TArray<FMorphSliderEntry> GroupThreeFixedEntries;
	if (GatherMorphEntriesForConfiguredName(
		CustomizationComponent,
		Settings->OptionalMatureGroupThreeFixedMorphName,
		GroupThreeFixedEntries))
	{
		ApplyMorphEntriesWithValue(CustomizationComponent, GroupThreeFixedEntries, GroupThreeValue);
	}

	TArray<FMorphSliderEntry> GroupThreeConditionalEntries;
	if (GatherMorphEntriesForConfiguredName(
		CustomizationComponent,
		Settings->OptionalMatureGroupThreeConditionalMorphName,
		GroupThreeConditionalEntries))
	{
		const bool bApplyConditionalMorph = EnemyLevel >= Settings->OptionalMatureGroupThreeStartLevel
			&& FMath::FRand() <= FMath::Clamp(
				Settings->OptionalMatureGroupThreeConditionalMorphChance,
				0.0f,
				1.0f);
		ApplyMorphEntriesWithValue(
			CustomizationComponent,
			GroupThreeConditionalEntries,
			bApplyConditionalMorph ? GroupThreeValue : 0.0f);
	}
}

int32 UProjectEnemyVisualVariationSubsystem::ResolveEnemyLevel(const APawn* Pawn) const
{
	if (const UProjectEnemyLevelComponent* LevelComponent = IsValid(Pawn) ? Pawn->FindComponentByClass<UProjectEnemyLevelComponent>() : nullptr)
	{
		if (LevelComponent->HasAssignedLevel())
		{
			return FMath::Max(LevelComponent->GetAssignedLevel(), 1);
		}
	}

	return 1;
}

bool UProjectEnemyVisualVariationSubsystem::ApplyMorphEntriesWithValue(
	UEFCharacterCustomizationComponent* CustomizationComponent,
	const TArray<FMorphSliderEntry>& Entries,
	const float Value) const
{
	if (!IsValid(CustomizationComponent) || Entries.IsEmpty())
	{
		return false;
	}

	bool bAppliedAnyMorph = false;
	for (const FMorphSliderEntry& Entry : Entries)
	{
		const float EffectiveMinValue = FMath::Min(Entry.MinValue, Entry.MaxValue);
		const float EffectiveMaxValue = FMath::Max(Entry.MinValue, Entry.MaxValue);
		CustomizationComponent->ApplyMorph(Entry, FMath::Clamp(Value, EffectiveMinValue, EffectiveMaxValue));
		bAppliedAnyMorph = true;
	}

	return bAppliedAnyMorph;
}

void UProjectEnemyVisualVariationSubsystem::InitializeOptionalMatureMorphState(
	APawn* Pawn,
	UEFCharacterCustomizationComponent* CustomizationComponent)
{
	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	if (!Settings
		|| !IsValid(Pawn)
		|| !IsValid(CustomizationComponent))
	{
		return;
	}

	FProjectEnemyOptionalMatureMorphState MorphState;
	MorphState.Pawn = Pawn;
	MorphState.CustomizationComponent = CustomizationComponent;
	MorphState.CombatComponent = Pawn->FindComponentByClass<UProjectCombatAttributeComponent>();
	MorphState.CurrentPresentationAlpha = 0.0f;
	MorphState.bShouldShowActivePresentation = false;

	const bool bFoundNeutralBaseMorph = GatherMorphEntriesForConfiguredName(
		CustomizationComponent,
		Settings->NeutralBaseMorphName,
		MorphState.NeutralBaseEntries);
	const bool bFoundActivePresentationMorph = GatherMorphEntriesForConfiguredName(
		CustomizationComponent,
		Settings->ActivePresentationMorphName,
		MorphState.ActivePresentationEntries);
	if (!bFoundNeutralBaseMorph && !bFoundActivePresentationMorph)
	{
		UE_LOG(
			LogProjectEnemyVisualVariation,
			Warning,
			TEXT("Optional mature morph setup skipped for %s because neither configured runtime morph was found."),
			*GetNameSafe(Pawn));
		return;
	}

	if (!bFoundNeutralBaseMorph)
	{
		UE_LOG(
			LogProjectEnemyVisualVariation,
			Warning,
			TEXT("Optional mature neutral-base morph was not found on %s; only the active presentation morph can be animated."),
			*GetNameSafe(Pawn));
	}

	if (!bFoundActivePresentationMorph)
	{
		UE_LOG(
			LogProjectEnemyVisualVariation,
			Warning,
			TEXT("Optional mature active-presentation morph was not found on %s; only the neutral base morph can be animated."),
			*GetNameSafe(Pawn));
	}

	ApplyOptionalMatureMorphState(MorphState, 0.0f);
	TrackedOptionalMatureMorphStates.FindOrAdd(
		ProjectEnemyVisualVariationSubsystemPrivate::MakePawnKey(Pawn)) = MoveTemp(MorphState);
}

void UProjectEnemyVisualVariationSubsystem::UpdateSessionDrivenOptionalMatureMorphs(
	const float CurrentTimeSeconds)
{
	UWorld* World = GetWorld();
	const UProjectIntimacySubsystem* IntimacySubsystem =
		World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
	const bool bPresentationAllowed = IsOptionalMatureMorphPresentationAllowed();

	for (TPair<TObjectKey<APawn>, FProjectEnemyOptionalMatureMorphState>& Pair : TrackedOptionalMatureMorphStates)
	{
		FProjectEnemyOptionalMatureMorphState& MorphState = Pair.Value;
		APawn* EnemyPawn = MorphState.Pawn.Get();
		if (!IsValid(EnemyPawn))
		{
			continue;
		}

		const bool bIsDead = IsPawnDead(EnemyPawn, MorphState.CombatComponent.Get());
		const bool bActorInActiveSession = IntimacySubsystem
			&& IntimacySubsystem->IsActorInActiveIntimacySession(EnemyPawn);
		const bool bUseActivePresentation = ShouldUseActiveOptionalMatureMorph(
			bPresentationAllowed,
			bActorInActiveSession,
			bIsDead);
		if (MorphState.bShouldShowActivePresentation == bUseActivePresentation)
		{
			continue;
		}

		MorphState.bShouldShowActivePresentation = bUseActivePresentation;
		if (bUseActivePresentation)
		{
			UE_LOG(
				LogProjectEnemyVisualVariation,
				Log,
				TEXT("Optional mature morph transition activated for session participant %s at %.2fs."),
				*GetNameSafe(EnemyPawn),
				CurrentTimeSeconds);
		}
		else
		{
			UE_LOG(
				LogProjectEnemyVisualVariation,
				Log,
				TEXT("Optional mature morph transition returned to neutral for %s at %.2fs."),
				*GetNameSafe(EnemyPawn),
				CurrentTimeSeconds);
		}
	}
}

void UProjectEnemyVisualVariationSubsystem::AdvanceOptionalMatureMorphs(const float DeltaTime)
{
	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	if (!Settings || !IsOptionalMatureMorphPresentationAllowed() || DeltaTime <= 0.0f)
	{
		return;
	}

	TArray<TObjectKey<APawn>> KeysToRemove;
	for (TPair<TObjectKey<APawn>, FProjectEnemyOptionalMatureMorphState>& Pair : TrackedOptionalMatureMorphStates)
	{
		FProjectEnemyOptionalMatureMorphState& MorphState = Pair.Value;
		if (!MorphState.Pawn.IsValid() || !MorphState.CustomizationComponent.IsValid())
		{
			KeysToRemove.Add(Pair.Key);
			continue;
		}

		const float TargetPresentationAlpha = MorphState.bShouldShowActivePresentation ? 1.0f : 0.0f;
		const float NewPresentationAlpha = FMath::FInterpConstantTo(
			MorphState.CurrentPresentationAlpha,
			TargetPresentationAlpha,
			DeltaTime,
			Settings->OptionalMatureMorphTransitionSpeed);
		if (!FMath::IsNearlyEqual(
			NewPresentationAlpha,
			MorphState.CurrentPresentationAlpha,
			KINDA_SMALL_NUMBER))
		{
			MorphState.CurrentPresentationAlpha = NewPresentationAlpha;
			ApplyOptionalMatureMorphState(MorphState, MorphState.CurrentPresentationAlpha);
		}
	}

	for (const TObjectKey<APawn>& Key : KeysToRemove)
	{
		TrackedOptionalMatureMorphStates.Remove(Key);
	}
}

bool UProjectEnemyVisualVariationSubsystem::IsPawnDead(
	const APawn* Pawn,
	const UProjectCombatAttributeComponent* CombatComponent) const
{
	const UProjectCombatAttributeComponent* EffectiveCombatComponent = CombatComponent;
	if (!IsValid(EffectiveCombatComponent) && IsValid(Pawn))
	{
		EffectiveCombatComponent = Pawn->FindComponentByClass<UProjectCombatAttributeComponent>();
	}

	if (IsValid(EffectiveCombatComponent) && EffectiveCombatComponent->IsDead())
	{
		return true;
	}

	const UACFDamageHandlerComponent* DamageHandlerComponent = IsValid(Pawn)
		? Pawn->FindComponentByClass<UACFDamageHandlerComponent>()
		: nullptr;
	return IsValid(DamageHandlerComponent) && !DamageHandlerComponent->GetIsAlive();
}

void UProjectEnemyVisualVariationSubsystem::ApplyOptionalMatureMorphState(
	FProjectEnemyOptionalMatureMorphState& MorphState,
	const float PresentationAlpha) const
{
	UEFCharacterCustomizationComponent* CustomizationComponent = MorphState.CustomizationComponent.Get();
	if (!IsValid(CustomizationComponent))
	{
		return;
	}

	const float ClampedPresentationAlpha = IsOptionalMatureMorphPresentationAllowed()
		? FMath::Clamp(PresentationAlpha, 0.0f, 1.0f)
		: 0.0f;
	const float NeutralBaseValue = 1.0f - ClampedPresentationAlpha;
	const float ActivePresentationValue = ClampedPresentationAlpha;

	for (const FMorphSliderEntry& Entry : MorphState.NeutralBaseEntries)
	{
		CustomizationComponent->ApplyMorph(Entry, NeutralBaseValue);
	}

	for (const FMorphSliderEntry& Entry : MorphState.ActivePresentationEntries)
	{
		CustomizationComponent->ApplyMorph(Entry, ActivePresentationValue);
	}
}

void UProjectEnemyVisualVariationSubsystem::CleanupTrackedOptionalMatureMorphStates()
{
	TArray<TObjectKey<APawn>> KeysToRemove;
	for (const TPair<TObjectKey<APawn>, FProjectEnemyOptionalMatureMorphState>& Pair : TrackedOptionalMatureMorphStates)
	{
		const FProjectEnemyOptionalMatureMorphState& MorphState = Pair.Value;
		if (!MorphState.Pawn.IsValid() || !MorphState.CustomizationComponent.IsValid())
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	for (const TObjectKey<APawn>& Key : KeysToRemove)
	{
		TrackedOptionalMatureMorphStates.Remove(Key);
	}
}

bool UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowed() const
{
	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	if (!Settings || !Settings->bEnableOptionalMatureMorphPresentation)
	{
		return false;
	}

	const UProjectContentPolicySubsystem* Policy = ContentPolicySubsystem.Get();
	if (!Policy)
	{
		UWorld* World = GetWorld();
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		Policy = GameInstance ? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>() : nullptr;
	}

	// Optional mature morph presentation follows the same Charisma-ten gate as
	// voluntary Intimacy, while Streamer Safe always suppresses it.
	return Policy
		&& IsOptionalMatureMorphPresentationAllowedForPolicy(
			Settings->bEnableOptionalMatureMorphPresentation,
			Policy->GetPolicySnapshot());
}

void UProjectEnemyVisualVariationSubsystem::EnableOptionalMatureMorphsForExistingPawns()
{
	if (!IsOptionalMatureMorphPresentationAllowed())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	LastOptionalMatureVisibilityPollTimeSeconds = -1.0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!IsValid(Pawn)
			|| !IsActorProcessed(Pawn)
			|| !IsOptionalMatureMorphTargetClass(Pawn->GetClass())
			|| TrackedOptionalMatureMorphStates.Contains(
				ProjectEnemyVisualVariationSubsystemPrivate::MakePawnKey(Pawn)))
		{
			continue;
		}

		UEFCharacterCustomizationComponent* CustomizationComponent =
			Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>();
		if (!IsValid(CustomizationComponent))
		{
			continue;
		}

		ApplyConfiguredOptionalMatureMorphGroups(Pawn, CustomizationComponent);
		InitializeOptionalMatureMorphState(Pawn, CustomizationComponent);
	}
}

void UProjectEnemyVisualVariationSubsystem::DisableOptionalMatureMorphsForExistingPawns()
{
	for (TPair<TObjectKey<APawn>, FProjectEnemyOptionalMatureMorphState>& Pair : TrackedOptionalMatureMorphStates)
	{
		FProjectEnemyOptionalMatureMorphState& MorphState = Pair.Value;
		MorphState.bShouldShowActivePresentation = false;
		MorphState.CurrentPresentationAlpha = 0.0f;
		ApplyOptionalMatureMorphState(MorphState, 0.0f);
	}

	LastOptionalMatureVisibilityPollTimeSeconds = -1.0;
}

void UProjectEnemyVisualVariationSubsystem::HandleContentPolicyChanged(
	const FProjectContentPolicySnapshot Policy)
{
	const UProjectEnemyVisualVariationSettings* Settings = UProjectEnemyVisualVariationSettings::Get();
	const bool bAllowed = Settings
		&& IsOptionalMatureMorphPresentationAllowedForPolicy(
			Settings->bEnableOptionalMatureMorphPresentation,
			Policy);
	if (bAllowed == bOptionalMatureMorphPolicyAllowed)
	{
		return;
	}

	bOptionalMatureMorphPolicyAllowed = bAllowed;
	if (bAllowed)
	{
		EnableOptionalMatureMorphsForExistingPawns();
	}
	else
	{
		DisableOptionalMatureMorphsForExistingPawns();
	}
}

void UProjectEnemyVisualVariationSubsystem::MarkActorProcessed(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	ProcessedActors.Add(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
	PendingActors.Remove(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
}

bool UProjectEnemyVisualVariationSubsystem::IsActorProcessed(const AActor* Actor) const
{
	return IsValid(Actor) && ProcessedActors.Contains(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
}

bool UProjectEnemyVisualVariationSubsystem::IsActorPending(const AActor* Actor) const
{
	return IsValid(Actor) && PendingActors.Contains(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
}

void UProjectEnemyVisualVariationSubsystem::MarkActorPending(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	PendingActors.Add(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
}

void UProjectEnemyVisualVariationSubsystem::ClearActorPending(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	PendingActors.Remove(ProjectEnemyVisualVariationSubsystemPrivate::MakeObjectKey(Actor));
}
