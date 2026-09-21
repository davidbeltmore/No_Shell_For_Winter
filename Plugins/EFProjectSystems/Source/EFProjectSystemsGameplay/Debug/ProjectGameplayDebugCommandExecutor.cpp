#include "Debug/ProjectGameplayDebugCommandExecutor.h"

#include "Combat/ProjectCombatAttributeComponent.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RuntimePerformance/ProjectRuntimePerformanceSubsystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectSurvivalStatusSettings.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectGameplayDebugCommands, Log, All);

namespace ProjectGameplayDebugCommandExecutorPrivate
{
	const FName HealthName(TEXT("Health"));
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName CurseName(TEXT("Curse"));
	const FName PainName(TEXT("Pain"));
	const FName DungeonHarnessStatusOptionId(TEXT("DungeonHarness.Status"));
	const FName DungeonHarnessClearIntentOptionId(TEXT("DungeonHarness.ClearIntent"));
	const FName ScaleBiasName(TEXT("Scale"));
	const FName BranchingBiasName(TEXT("Branching"));
	const FName DangerBiasName(TEXT("Danger"));
	const FName SafeBiasName(TEXT("Safe"));
	const FName AbundanceBiasName(TEXT("Abundance"));
	const FName MysteryBiasName(TEXT("Mystery"));
	const FName ClothingBiasName(TEXT("Clothing"));
	const FString DungeonHarnessStylePrefix(TEXT("DungeonHarness.Style."));
	const FString DungeonHarnessScaleBiasPrefix(TEXT("DungeonHarness.ScaleBias."));
	const FString DungeonHarnessBranchingBiasPrefix(TEXT("DungeonHarness.BranchingBias."));
	const FString DungeonHarnessDangerBiasPrefix(TEXT("DungeonHarness.DangerBias."));
	const FString DungeonHarnessSafeBiasPrefix(TEXT("DungeonHarness.SafeBias."));
	const FString DungeonHarnessAbundanceBiasPrefix(TEXT("DungeonHarness.AbundanceBias."));
	const FString DungeonHarnessMysteryBiasPrefix(TEXT("DungeonHarness.MysteryBias."));
	const FString DungeonHarnessClothingBiasPrefix(TEXT("DungeonHarness.ClothingBias."));
	const FString DungeonHarnessVolatilityPrefix(TEXT("DungeonHarness.Volatility."));

	UEFCalystoDungeonSubsystem* FindCalystoDungeonSubsystem(AActor* OwnerActor)
	{
		UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>() : nullptr;
	}

	bool IsNormalizedIntentBias(const float Bias)
	{
		return FMath::IsFinite(Bias) && Bias >= -1.0f && Bias <= 1.0f;
	}

	bool IsNormalizedVolatility(const float Volatility)
	{
		return FMath::IsFinite(Volatility) && Volatility >= -1.0f && Volatility <= 1.0f;
	}

	FString DescribeStyle(const FName StyleId)
	{
		return StyleId.IsNone() ? TEXT("Invalid") : StyleId.ToString();
	}

	FString DescribeResolvedCategories(const FEFCalystoPopulationPlanV6& Plan)
	{
		TArray<FEFCalystoPopulationCountV6> Counts = Plan.CategoryCounts;
		Counts.Sort([](const FEFCalystoPopulationCountV6& Left,
			const FEFCalystoPopulationCountV6& Right)
		{
			return Left.Id.LexicalLess(Right.Id);
		});
		TArray<FString> Records;
		for (const FEFCalystoPopulationCountV6& Count : Counts)
		{
			Records.Add(FString::Printf(TEXT("%s=%d"), *Count.Id.ToString(), Count.Count));
		}
		return Records.IsEmpty() ? TEXT("None") : FString::Join(Records, TEXT(" "));
	}

	FString DescribeThemeMix(const FEFCalystoRoomManifestV6& Manifest)
	{
		TMap<FName, int32> Counts;
		for (const FEFCalystoRoomContextV6& Room : Manifest.Rooms)
		{
			Counts.FindOrAdd(Room.ThemeId) += 1;
		}
		TArray<FName> ThemeIds;
		Counts.GetKeys(ThemeIds);
		ThemeIds.Sort([](const FName Left, const FName Right)
		{
			return Left.LexicalLess(Right);
		});
		TArray<FString> Records;
		for (const FName ThemeId : ThemeIds)
		{
			Records.Add(FString::Printf(
				TEXT("%s=%d"), *ThemeId.ToString(), Counts.FindChecked(ThemeId)));
		}
		return Records.IsEmpty() ? TEXT("Pending") : FString::Join(Records, TEXT(" "));
	}

	FString DescribeTravelKind(const EEFCalystoDungeonTravelKindV6 Kind)
	{
		switch (Kind)
		{
		case EEFCalystoDungeonTravelKindV6::NewRun: return TEXT("NewRun");
		case EEFCalystoDungeonTravelKindV6::Advance: return TEXT("Advance");
		case EEFCalystoDungeonTravelKindV6::Reroll: return TEXT("Reroll");
		case EEFCalystoDungeonTravelKindV6::Replay: return TEXT("Replay");
		case EEFCalystoDungeonTravelKindV6::Retry: return TEXT("Retry");
		case EEFCalystoDungeonTravelKindV6::RestartSameSeed: return TEXT("RestartSameSeed");
		case EEFCalystoDungeonTravelKindV6::DevelopmentJump: return TEXT("DevelopmentJump");
		case EEFCalystoDungeonTravelKindV6::RecoverToHub: return TEXT("RecoverToHub");
		default: return TEXT("None");
		}
	}

	FString DescribeRunState(const EEFCalystoDungeonTravelStateV6 State)
	{
		switch (State)
		{
		case EEFCalystoDungeonTravelStateV6::Preloading: return TEXT("Preloading");
		case EEFCalystoDungeonTravelStateV6::Traveling: return TEXT("Traveling");
		case EEFCalystoDungeonTravelStateV6::Generating: return TEXT("Generating");
		case EEFCalystoDungeonTravelStateV6::Ready: return TEXT("Ready");
		case EEFCalystoDungeonTravelStateV6::Recovering: return TEXT("Recovering");
		case EEFCalystoDungeonTravelStateV6::Failed: return TEXT("Failed");
		default: return TEXT("Idle");
		}
	}

	bool IsPendingFloorState(const EEFCalystoDungeonTravelStateV6 State)
	{
		return State == EEFCalystoDungeonTravelStateV6::Preloading
			|| State == EEFCalystoDungeonTravelStateV6::Traveling
			|| State == EEFCalystoDungeonTravelStateV6::Generating
			|| State == EEFCalystoDungeonTravelStateV6::Recovering;
	}

	UProjectSurvivalNeedsComponent* FindNeedsComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectSurvivalNeedsComponent>() : nullptr;
	}

	UProjectSurvivalStatusComponent* FindStatusComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectSurvivalStatusComponent>() : nullptr;
	}

	UProjectInnerDoctrineComponent* FindInnerDoctrineComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>() : nullptr;
	}

	UProjectCombatAttributeComponent* FindCombatAttributeComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>() : nullptr;
	}

	UProjectRealtimeSnapshotComponent* FindRealtimeSnapshotComponent(AActor* OwnerActor)
	{
		return OwnerActor ? OwnerActor->FindComponentByClass<UProjectRealtimeSnapshotComponent>() : nullptr;
	}

	UProjectDefaultTattooSkinnedDecalSubsystem* FindAutomaticTattooSubsystem(AActor* OwnerActor)
	{
		UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	}

	bool TryFindAutomaticTattooSnapshot(
		AActor* OwnerActor,
		const FName RowName,
		FProjectAutomaticTattooRuntimeDebugSnapshot& OutSnapshot)
	{
		UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem = FindAutomaticTattooSubsystem(OwnerActor);
		if (!TattooSubsystem || RowName.IsNone())
		{
			return false;
		}

		TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
		TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
		for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
		{
			if (Snapshot.RowName == RowName)
			{
				OutSnapshot = Snapshot;
				return true;
			}
		}

		return false;
	}

	FString BuildAutomaticTattooStateText(const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot)
	{
		if (Snapshot.bForcedActiveForDebug)
		{
			return TEXT("Forced");
		}
		return Snapshot.bActive ? TEXT("Active") : TEXT("Locked");
	}
}

bool FProjectGameplayDebugCommandExecutor::TriggerImmediateDefeat(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UProjectDefeatFlowComponent* DefeatFlowComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>()
		: nullptr;
	return DefeatFlowComponent && DefeatFlowComponent->DebugTriggerImmediateDefeat(TEXT("GameplayDebug.ImmediateDefeat"));
#endif
}

bool FProjectGameplayDebugCommandExecutor::TriggerDownedMode(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UProjectDefeatFlowComponent* DefeatFlowComponent = OwnerActor
		? OwnerActor->FindComponentByClass<UProjectDefeatFlowComponent>()
		: nullptr;
	return DefeatFlowComponent
		&& DefeatFlowComponent->RequestKnockoutOrPendingCrawl(
			EProjectKnockoutReason::DebugForced,
			TEXT("GameplayDebug.DownedMode"));
#endif
}

bool FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	if (!OwnerActor)
	{
		return false;
	}

	if (UProjectRealtimeSnapshotComponent* RealtimeSnapshotComponent = FindRealtimeSnapshotComponent(OwnerActor))
	{
		float CurrentHealth = 0.f;
		float MaxHealth = 0.f;
		if (RealtimeSnapshotComponent->TryReadOwnerResource(HealthName, CurrentHealth, MaxHealth) && MaxHealth > KINDA_SMALL_NUMBER)
		{
			if (CurrentHealth >= MaxHealth - KINDA_SMALL_NUMBER)
			{
				return true;
			}

			const float AppliedDelta = RealtimeSnapshotComponent->ApplyOwnerResourceDelta(HealthName, MaxHealth - CurrentHealth, true);
			if (AppliedDelta > KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
	}

	if (UProjectCombatAttributeComponent* CombatAttributeComponent = FindCombatAttributeComponent(OwnerActor))
	{
		const FName HealthAttributeName = CombatAttributeComponent->HealthAttributeName;
		if (!HealthAttributeName.IsNone() && CombatAttributeComponent->HasAttribute(HealthAttributeName))
		{
			const float MaxHealth = FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(HealthAttributeName), 0.f);
			if (MaxHealth <= KINDA_SMALL_NUMBER)
			{
				return false;
			}

			CombatAttributeComponent->SetAttributeRecoveryBlocked(HealthAttributeName, false);
			if (CombatAttributeComponent->GetAttributeCurrentValue(HealthAttributeName) >= MaxHealth - KINDA_SMALL_NUMBER)
			{
				return true;
			}

			return CombatAttributeComponent->SetAttributeCurrentValue(HealthAttributeName, MaxHealth);
		}
	}

	return false;
#endif
}

bool FProjectGameplayDebugCommandExecutor::RestoreNeedsAndSensations(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	UProjectSurvivalNeedsComponent* NeedsComponent = FindNeedsComponent(OwnerActor);
	if (!NeedsComponent)
	{
		return false;
	}

	bool bApplied = false;
	bApplied |= NeedsComponent->SetNeedCurrentValue(HungerName, NeedsComponent->GetNeedMaxValue(HungerName), true);
	bApplied |= NeedsComponent->SetNeedCurrentValue(ThirstName, NeedsComponent->GetNeedMaxValue(ThirstName), true);
	bApplied |= NeedsComponent->SetNeedCurrentValue(SleepName, NeedsComponent->GetNeedMaxValue(SleepName), true);
	bApplied |= NeedsComponent->SetSensationCurrentValue(MadnessName, 0.f, true);
	if (UProjectInnerDoctrineComponent* DoctrineComponent = FindInnerDoctrineComponent(OwnerActor))
	{
		bApplied |= DoctrineComponent->CleanseCurse(DoctrineComponent->GetCurseMax()) > 0.f;
	}
	bApplied |= NeedsComponent->SetSensationCurrentValue(PainName, 0.f, true);
	return bApplied;
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetSensationToMax(AActor* OwnerActor, const FName SensationName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)SensationName;
	return false;
#else
	return SetSensationToPercent(OwnerActor, SensationName, 1.f);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedToZero(AActor* OwnerActor, const FName NeedName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)NeedName;
	return false;
#else
	return SetNeedToPercent(OwnerActor, NeedName, 0.f);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedOrSensationToPercent(
	AActor* OwnerActor,
	const FName EntryName,
	const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)EntryName;
	(void)Percent;
	return false;
#else
	if (EntryName == ProjectGameplayDebugCommandExecutorPrivate::CurseName)
	{
		return SetSensationToPercent(OwnerActor, EntryName, Percent);
	}

	UProjectSurvivalNeedsComponent* NeedsComponent = ProjectGameplayDebugCommandExecutorPrivate::FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || EntryName.IsNone())
	{
		return false;
	}

	if (NeedsComponent->HasNeed(EntryName))
	{
		return SetNeedToPercent(OwnerActor, EntryName, Percent);
	}

	if (NeedsComponent->HasSensation(EntryName))
	{
		return SetSensationToPercent(OwnerActor, EntryName, Percent);
	}

	return false;
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetNeedToPercent(AActor* OwnerActor, const FName NeedName, const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)NeedName;
	(void)Percent;
	return false;
#else
	UProjectSurvivalNeedsComponent* NeedsComponent = ProjectGameplayDebugCommandExecutorPrivate::FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || !NeedsComponent->HasNeed(NeedName))
	{
		return false;
	}

	const float ClampedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	return NeedsComponent->SetNeedCurrentValue(NeedName, NeedsComponent->GetNeedMaxValue(NeedName) * ClampedPercent, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetSensationToPercent(AActor* OwnerActor, const FName SensationName, const float Percent)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)SensationName;
	(void)Percent;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	if (SensationName == CurseName)
	{
		UProjectInnerDoctrineComponent* DoctrineComponent = FindInnerDoctrineComponent(OwnerActor);
		if (!DoctrineComponent)
		{
			return false;
		}

		const float TargetCurse = DoctrineComponent->GetCurseMax() * FMath::Clamp(Percent, 0.f, 1.f);
		DoctrineComponent->CleanseCurse(DoctrineComponent->GetCurseMax());
		if (TargetCurse <= KINDA_SMALL_NUMBER)
		{
			return true;
		}

		FProjectCurseApplicationContext Context;
		Context.Amount = TargetCurse;
		Context.SourceKind = EProjectCurseSourceKind::Debug;
		Context.ApplicationId = FGuid::NewGuid();
		Context.bResistible = false;
		Context.bCanTriggerCursed = true;
		return DoctrineComponent->ApplyCurse(Context).AppliedAmount > 0.f;
	}

	UProjectSurvivalNeedsComponent* NeedsComponent = FindNeedsComponent(OwnerActor);
	if (!NeedsComponent || !NeedsComponent->HasSensation(SensationName))
	{
		return false;
	}

	const float ClampedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	return NeedsComponent->SetSensationCurrentValue(SensationName, NeedsComponent->GetSensationMaxValue(SensationName) * ClampedPercent, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ForceApplyStatus(AActor* OwnerActor, const FName StatusName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)StatusName;
	return false;
#else
	UProjectSurvivalStatusComponent* StatusComponent = ProjectGameplayDebugCommandExecutorPrivate::FindStatusComponent(OwnerActor);
	return StatusComponent && StatusComponent->ApplyDebugStatus(StatusName, true);
#endif
}

bool FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(
	AActor* OwnerActor,
	const EProjectDoctrineAttribute Attribute,
	const int32 TargetLevel)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)Attribute;
	(void)TargetLevel;
	return false;
#else
	UProjectInnerDoctrineComponent* DoctrineComponent = ProjectGameplayDebugCommandExecutorPrivate::FindInnerDoctrineComponent(OwnerActor);
	if (!DoctrineComponent || TargetLevel <= 0 || Attribute == EProjectDoctrineAttribute::Count)
	{
		return false;
	}

	const int32 CurrentLevel = DoctrineComponent->GetAttributeLevel(Attribute);
	if (CurrentLevel >= TargetLevel)
	{
		return true;
	}

	return DoctrineComponent->ApplyFreeAttributeLevels(Attribute, TargetLevel - CurrentLevel);
#endif
}

bool FProjectGameplayDebugCommandExecutor::StartRuntimeFpsBenchmark(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	if (!OwnerActor || !OwnerActor->GetWorld() || !OwnerActor->GetWorld()->GetGameInstance())
	{
		return false;
	}

	UProjectRuntimePerformanceSubsystem* PerformanceSubsystem =
		OwnerActor->GetWorld()->GetGameInstance()->GetSubsystem<UProjectRuntimePerformanceSubsystem>();
	return PerformanceSubsystem
		&& PerformanceSubsystem->RequestDefaultDungeonCombatBenchmark(-1.0f, -1, false);
#endif
}

bool FProjectGameplayDebugCommandExecutor::StartFullStackOverloadBenchmark(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	if (!OwnerActor || !OwnerActor->GetWorld() || !OwnerActor->GetWorld()->GetGameInstance())
	{
		return false;
	}

	UProjectRuntimePerformanceSubsystem* PerformanceSubsystem =
		OwnerActor->GetWorld()->GetGameInstance()->GetSubsystem<UProjectRuntimePerformanceSubsystem>();
	return PerformanceSubsystem
		&& PerformanceSubsystem->RequestDungeonFullStackOverloadBenchmark(-1.0f, -1, false);
#endif
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusLabel(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return FText::FromString(TEXT("Dungeon Harness Unavailable"));
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return FText::FromString(TEXT("Dungeon Harness Unavailable"));
	}

	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
	if (!Snapshot.bHasActiveRun)
	{
		return FText::FromString(TEXT("Status: No Active Dungeon Run"));
	}
	const FEFCalystoResolvedFloorIntentV6 FloorIntent = DungeonSubsystem->GetResolvedFloorIntent();
	const FEFCalystoRoomManifestV6 RoomManifest = DungeonSubsystem->GetRoomManifestV6();
	const FString ProfileSuffix = FloorIntent.bIsValid
		? FString::Printf(
			TEXT(" [%s | Themes %s]"),
			*ProjectGameplayDebugCommandExecutorPrivate::DescribeStyle(FloorIntent.StyleId),
			*ProjectGameplayDebugCommandExecutorPrivate::DescribeThemeMix(RoomManifest))
		: FString();
	if (ProjectGameplayDebugCommandExecutorPrivate::IsPendingFloorState(Snapshot.State))
	{
		return FText::FromString(FString::Printf(
			TEXT("Status: Floor %lld G%lld (%s)%s"),
			static_cast<long long>(Snapshot.FloorNumber),
			static_cast<long long>(Snapshot.GenerationSerial),
			*ProjectGameplayDebugCommandExecutorPrivate::DescribeRunState(Snapshot.State),
			*ProfileSuffix));
	}
	if (Snapshot.State == EEFCalystoDungeonTravelStateV6::Failed)
	{
		return FText::FromString(FString::Printf(
			TEXT("Status: Floor %lld G%lld (Failed: %s)%s"),
			static_cast<long long>(Snapshot.FloorNumber),
			static_cast<long long>(Snapshot.GenerationSerial),
			Snapshot.FailureReason.IsEmpty() ? TEXT("Unknown") : *Snapshot.FailureReason,
			*ProfileSuffix));
	}

	return FText::FromString(FString::Printf(
		TEXT("Status: Floor %lld G%lld%s"),
		static_cast<long long>(Snapshot.FloorNumber),
		static_cast<long long>(Snapshot.GenerationSerial),
		*ProfileSuffix));
#endif
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusDescription(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return FText::FromString(TEXT("Development-only Calysto controls are unavailable in Shipping."));
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return FText::FromString(TEXT("Calysto dungeon runtime is not available in this world."));
	}

	const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
	if (!Snapshot.bPolicyValid)
	{
		return FText::FromString(FString::Printf(
			TEXT("Dungeon Director V6 Data Asset invalid (fail closed): %s"),
			*Snapshot.PolicyError));
	}
	if (!Snapshot.bHasActiveRun)
	{
		return FText::FromString(TEXT("No run is active. Entering DungeonGeneration will start a new seeded run at Floor 1."));
	}

	const FEFCalystoDirectorIntentV6 QueuedIntent = DungeonSubsystem->GetNextFloorDirectorIntent();
	const FString QueuedStyleName = QueuedIntent.bHasPreferredStyle
		? ProjectGameplayDebugCommandExecutorPrivate::DescribeStyle(QueuedIntent.PreferredStyleId)
		: TEXT("Auto");
	const FString IntentState = Snapshot.bHasQueuedDirectorIntent
		? FString::Printf(
			TEXT("Queued Style=%s Scale=%+.2f Branch=%+.2f Danger=%+.2f Safety=%+.2f Abundance=%+.2f Mystery=%+.2f Clothing=%+.2f Volatility=%+.2f"),
			*QueuedStyleName,
			QueuedIntent.Scale,
			QueuedIntent.Branching,
			QueuedIntent.Danger,
			QueuedIntent.Safety,
			QueuedIntent.Abundance,
			QueuedIntent.Mystery,
			QueuedIntent.ClothingInfluence,
			QueuedIntent.Volatility)
		: TEXT("Autonomous Director");
	const FEFCalystoResolvedFloorIntentV6 FloorIntent = DungeonSubsystem->GetResolvedFloorIntent();
	const FEFCalystoRealizedFloorManifestV6 Manifest = DungeonSubsystem->GetRealizedFloorManifest();
	const FEFCalystoRoomManifestV6 RoomManifest = DungeonSubsystem->GetRoomManifestV6();
	const FEFCalystoPopulationPlanV6 PopulationPlan = DungeonSubsystem->GetPopulationPlanV6();
	const FString DirectorState = FString::Printf(
		TEXT("%s/%s"),
		*ProjectGameplayDebugCommandExecutorPrivate::DescribeTravelKind(Snapshot.TravelKind),
		*ProjectGameplayDebugCommandExecutorPrivate::DescribeRunState(Snapshot.State));
	const FString ResolvedStyleName = FloorIntent.bIsValid
		? ProjectGameplayDebugCommandExecutorPrivate::DescribeStyle(FloorIntent.StyleId)
		: TEXT("Pending");
	const FString ResolvedThemeMix =
		ProjectGameplayDebugCommandExecutorPrivate::DescribeThemeMix(RoomManifest);

	const FString IdentitySummary = FString::Printf(
		TEXT("V6 | Floor %lld | Generation %lld | RunEpoch %lld | RunSeed %lld | PCG %d | Style %s | Room Themes %s"),
		static_cast<long long>(Snapshot.FloorNumber),
		static_cast<long long>(Snapshot.GenerationSerial),
		static_cast<long long>(DungeonSubsystem->GetRunEpoch()),
		static_cast<long long>(Snapshot.RunSeed),
		FloorIntent.bIsValid ? FloorIntent.PCGSeed : Snapshot.PCGSeed,
		*ResolvedStyleName,
		*ResolvedThemeMix);
	const FString TraitSummary = FloorIntent.bIsValid
		? FString::Printf(
			TEXT("Intent Scale %+.2f Branch %+.2f Danger %+.2f Safety %+.2f Abundance %+.2f Mystery %+.2f Clothing %+.2f Volatility %+.2f"),
			FloorIntent.DirectorIntent.Scale,
			FloorIntent.DirectorIntent.Branching,
			FloorIntent.DirectorIntent.Danger,
			FloorIntent.DirectorIntent.Safety,
			FloorIntent.DirectorIntent.Abundance,
			FloorIntent.DirectorIntent.Mystery,
			FloorIntent.DirectorIntent.ClothingInfluence,
			FloorIntent.DirectorIntent.Volatility)
		: TEXT("Traits pending");
	const FString IntentSummary = FloorIntent.bIsValid
		? FString::Printf(
			TEXT("Intent Size %dx%d | Candidate Density %.0f%% | Side %.0f%% | Theme Chance %.0f%% | Limits E%d/Actors%d | Population %s"),
			FloorIntent.DungeonSize.X,
			FloorIntent.DungeonSize.Y,
			FloorIntent.CandidateDensity * 100.0f,
			FloorIntent.SidePathChance * 100.0f,
			FloorIntent.FloorPlan.ThemeRoomChance * 100.0f,
			FloorIntent.FloorPlan.GlobalBudgets.MaximumEnemies,
			FloorIntent.FloorPlan.GlobalBudgets.MaximumDirectorActors,
			*ProjectGameplayDebugCommandExecutorPrivate::DescribeResolvedCategories(PopulationPlan))
		: TEXT("Intent pending");
	const FString ManifestSummary = Manifest.bIsValid
		? FString::Printf(
			TEXT("Realized Anchors %d | Eligible Rooms %d | Themed Rooms %d | E%d F%d C%d Loot%d Event%d | Actors %d | Costs T %.1f/R %.1f"),
			Manifest.CandidateAnchorCount,
			RoomManifest.EligibleRoomCount,
			RoomManifest.ThemedRoomCount,
			PopulationPlan.EnemyCount,
			PopulationPlan.LooseFoodCount,
			PopulationPlan.ChestCount,
			PopulationPlan.LootActorCount,
			PopulationPlan.SpecialEventCount,
			Manifest.SpawnedActorCount,
			Manifest.RealizedThreatCost,
			Manifest.RealizedResourceCost)
		: TEXT("Manifest pending");
	const FString HashSummary = FString::Printf(
		TEXT("Hashes Policy %s | FloorPlan %s | Ecology %s | Outcome %s | Intent %s | Companion %s | Rooms %s | Anchor %s | Population %s | Manifest %s"),
		*FloorIntent.GenerationContext.PolicyHash,
		*FloorIntent.FloorPlan.FloorPlanHash,
		*FloorIntent.EcologyHash,
		*FloorIntent.FrozenOutcome.OutcomeHash,
		*FloorIntent.IntentHash,
		*Manifest.CompanionSnapshotHash,
		*Manifest.RoomManifestHash,
		*Manifest.AnchorTopologyHash,
		*Manifest.PopulationPlanHash,
		*Manifest.ManifestHash);

	return FText::FromString(FString::Printf(
		TEXT("%s | %s | %s | %s | %s | %s | %s"),
		*IdentitySummary,
		*TraitSummary,
		*IntentSummary,
		*ManifestSummary,
		*HashSummary,
		*IntentState,
		*DirectorState));
#endif
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessFloorChoiceLabel(
	AActor* OwnerActor,
	const int64 FloorNumber)
{
	FString Prefix;
#if !UE_BUILD_SHIPPING
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor))
	{
		const FEFCalystoDungeonSnapshotV6 Snapshot = DungeonSubsystem->GetSnapshot();
		if (ProjectGameplayDebugCommandExecutorPrivate::IsPendingFloorState(Snapshot.State)
			&& Snapshot.FloorNumber == FloorNumber)
		{
			Prefix = TEXT("[Pending] ");
		}
		else if (Snapshot.FloorNumber == FloorNumber)
		{
			Prefix = TEXT("[Current] ");
		}
	}
#else
	(void)OwnerActor;
#endif
	return FText::FromString(FString::Printf(
		TEXT("%sFloor %lld"),
		*Prefix,
		static_cast<long long>(FloorNumber)));
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStyleChoiceLabel(
	AActor* OwnerActor,
	const bool bAuto,
	const FName StyleId)
{
	bool bSelected = false;
#if !UE_BUILD_SHIPPING
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor))
	{
		const FEFCalystoDirectorIntentV6 Intent = DungeonSubsystem->GetNextFloorDirectorIntent();
		bSelected = bAuto
			? !Intent.bHasPreferredStyle
			: Intent.bHasPreferredStyle && Intent.PreferredStyleId == StyleId;
	}
#else
	(void)OwnerActor;
#endif
	return FText::FromString(FString::Printf(
		TEXT("%s%s"),
		bSelected ? TEXT("[Selected] ") : TEXT(""),
		bAuto ? TEXT("Auto") : *ProjectGameplayDebugCommandExecutorPrivate::DescribeStyle(StyleId)));
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessBiasChoiceLabel(
	AActor* OwnerActor,
	const FName BiasName,
	const float Bias)
{
	bool bSelected = false;
#if !UE_BUILD_SHIPPING
	using namespace ProjectGameplayDebugCommandExecutorPrivate;
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
		FindCalystoDungeonSubsystem(OwnerActor))
	{
		const FEFCalystoDirectorIntentV6 Intent = DungeonSubsystem->GetNextFloorDirectorIntent();
		float EffectiveBias = 0.0f;
		if (BiasName == ScaleBiasName) { EffectiveBias = Intent.Scale; }
		else if (BiasName == BranchingBiasName) { EffectiveBias = Intent.Branching; }
		else if (BiasName == DangerBiasName) { EffectiveBias = Intent.Danger; }
		else if (BiasName == SafeBiasName) { EffectiveBias = Intent.Safety; }
		else if (BiasName == AbundanceBiasName) { EffectiveBias = Intent.Abundance; }
		else if (BiasName == MysteryBiasName) { EffectiveBias = Intent.Mystery; }
		else if (BiasName == ClothingBiasName) { EffectiveBias = Intent.ClothingInfluence; }
		bSelected = FMath::IsNearlyEqual(EffectiveBias, Bias);
	}
#else
	(void)OwnerActor;
	(void)BiasName;
#endif
	return FText::FromString(FString::Printf(
		TEXT("%s%+.0f%%"),
		bSelected ? TEXT("[Selected] ") : TEXT(""),
		Bias * 100.0f));
}

FText FProjectGameplayDebugCommandExecutor::GetDungeonHarnessVolatilityChoiceLabel(
	AActor* OwnerActor,
	const float Volatility)
{
	bool bSelected = false;
#if !UE_BUILD_SHIPPING
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor))
	{
		bSelected = FMath::IsNearlyEqual(
			DungeonSubsystem->GetNextFloorDirectorIntent().Volatility,
			Volatility);
	}
#else
	(void)OwnerActor;
#endif
	return FText::FromString(FString::Printf(
		TEXT("%s%+.0f%%"),
		bSelected ? TEXT("[Selected] ") : TEXT(""),
		Volatility * 100.0f));
}

bool FProjectGameplayDebugCommandExecutor::RefreshDungeonHarnessStatus(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return false;
	}

	UE_LOG(
		LogProjectGameplayDebugCommands,
		Display,
		TEXT("[GameplayDebug][DungeonHarness] %s"),
		*GetDungeonHarnessStatusDescription(OwnerActor).ToString());
	return true;
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestAdvanceDungeonFloor(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	return DungeonSubsystem && DungeonSubsystem->RequestAdvanceFloor();
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestTravelToDungeonFloor(
	AActor* OwnerActor,
	const int64 FloorNumber)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)FloorNumber;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem || FloorNumber < 1)
	{
		return false;
	}

	return DungeonSubsystem->RequestTravelToFloor(FloorNumber);
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestReplayDungeonFloor(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	return DungeonSubsystem && DungeonSubsystem->RequestReplayCurrentFloor();
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestRerollDungeonFloor(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	return DungeonSubsystem && DungeonSubsystem->RequestRerollCurrentFloor();
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestStartNewDungeonRun(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	return DungeonSubsystem && DungeonSubsystem->RequestStartNewRun();
#endif
}

bool FProjectGameplayDebugCommandExecutor::RequestStartDungeonTestRun(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	return DungeonSubsystem && DungeonSubsystem->RequestStartNewRunWithSeed(42);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredStyle(
	AActor* OwnerActor,
	const bool bAuto,
	const FName StyleId)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)bAuto;
	(void)StyleId;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return false;
	}
	FEFCalystoDirectorIntentV6 Intent = DungeonSubsystem->GetNextFloorDirectorIntent();
	Intent.bHasPreferredStyle = !bAuto;
	Intent.PreferredStyleId = bAuto ? NAME_None : StyleId;
	if (!DungeonSubsystem->SetNextFloorDirectorIntent(Intent))
	{
		return false;
	}
	const FEFCalystoDirectorIntentV6 AppliedIntent = DungeonSubsystem->GetNextFloorDirectorIntent();
	return AppliedIntent.bHasPreferredStyle == !bAuto
		&& (bAuto || AppliedIntent.PreferredStyleId == StyleId);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
	AActor* OwnerActor,
	const FName BiasName,
	const float Bias)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)BiasName;
	(void)Bias;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;
	if (!IsNormalizedIntentBias(Bias))
	{
		return false;
	}
	UEFCalystoDungeonSubsystem* DungeonSubsystem = FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return false;
	}
	FEFCalystoDirectorIntentV6 Intent = DungeonSubsystem->GetNextFloorDirectorIntent();
	float* SelectedBias = nullptr;
	if (BiasName == ScaleBiasName) { SelectedBias = &Intent.Scale; }
	else if (BiasName == BranchingBiasName) { SelectedBias = &Intent.Branching; }
	else if (BiasName == DangerBiasName) { SelectedBias = &Intent.Danger; }
	else if (BiasName == SafeBiasName) { SelectedBias = &Intent.Safety; }
	else if (BiasName == AbundanceBiasName) { SelectedBias = &Intent.Abundance; }
	else if (BiasName == MysteryBiasName) { SelectedBias = &Intent.Mystery; }
	else if (BiasName == ClothingBiasName) { SelectedBias = &Intent.ClothingInfluence; }
	if (!SelectedBias)
	{
		return false;
	}
	*SelectedBias = Bias;
	if (!DungeonSubsystem->SetNextFloorDirectorIntent(Intent))
	{
		return false;
	}
	const FEFCalystoDirectorIntentV6 AppliedIntent = DungeonSubsystem->GetNextFloorDirectorIntent();
	if (BiasName == ScaleBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Scale, Bias); }
	if (BiasName == BranchingBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Branching, Bias); }
	if (BiasName == DangerBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Danger, Bias); }
	if (BiasName == SafeBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Safety, Bias); }
	if (BiasName == AbundanceBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Abundance, Bias); }
	if (BiasName == MysteryBiasName) { return FMath::IsNearlyEqual(AppliedIntent.Mystery, Bias); }
	return FMath::IsNearlyEqual(AppliedIntent.ClothingInfluence, Bias);
#endif
}

bool FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentVolatility(
	AActor* OwnerActor,
	const float Volatility)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)Volatility;
	return false;
#else
	using namespace ProjectGameplayDebugCommandExecutorPrivate;
	if (!IsNormalizedVolatility(Volatility))
	{
		return false;
	}
	UEFCalystoDungeonSubsystem* DungeonSubsystem = FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return false;
	}
	FEFCalystoDirectorIntentV6 Intent = DungeonSubsystem->GetNextFloorDirectorIntent();
	Intent.Volatility = Volatility;
	if (!DungeonSubsystem->SetNextFloorDirectorIntent(Intent))
	{
		return false;
	}
	return FMath::IsNearlyEqual(
		DungeonSubsystem->GetNextFloorDirectorIntent().Volatility,
		Volatility);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ClearDungeonHarnessDirectorIntent(AActor* OwnerActor)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return false;
#else
	UEFCalystoDungeonSubsystem* DungeonSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindCalystoDungeonSubsystem(OwnerActor);
	if (!DungeonSubsystem)
	{
		return false;
	}
	DungeonSubsystem->ClearNextFloorDirectorIntent();
	return !DungeonSubsystem->GetSnapshot().bHasQueuedDirectorIntent;
#endif
}

bool FProjectGameplayDebugCommandExecutor::IsDungeonHarnessPersistentCommand(const FName OptionId)
{
	using namespace ProjectGameplayDebugCommandExecutorPrivate;

	if (OptionId == DungeonHarnessStatusOptionId
		|| OptionId == DungeonHarnessClearIntentOptionId)
	{
		return true;
	}

	const FString OptionString = OptionId.ToString();
	return OptionString.StartsWith(DungeonHarnessStylePrefix)
		|| OptionString.StartsWith(DungeonHarnessScaleBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessBranchingBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessDangerBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessSafeBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessAbundanceBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessMysteryBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessClothingBiasPrefix)
		|| OptionString.StartsWith(DungeonHarnessVolatilityPrefix);
}

TArray<FName> FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowNames(AActor* OwnerActor)
{
	TArray<FName> RowNames;
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	if (!TattooSubsystem)
	{
		return RowNames;
	}

	TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
	TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
	RowNames.Reserve(Snapshots.Num());
	for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
	{
		RowNames.Add(Snapshot.RowName);
	}
	RowNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
#endif
	return RowNames;
}

FText FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowLabel(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	return FText::FromName(RowName);
#else
	FProjectAutomaticTattooRuntimeDebugSnapshot Snapshot;
	if (!ProjectGameplayDebugCommandExecutorPrivate::TryFindAutomaticTattooSnapshot(OwnerActor, RowName, Snapshot))
	{
		return FText::FromName(RowName);
	}

	const FString OverrideSuffix = Snapshot.bHasRuntimeOverride ? TEXT(" *") : TEXT("");
	return FText::FromString(FString::Printf(
		TEXT("%s [%s]%s"),
		*RowName.ToString(),
		*ProjectGameplayDebugCommandExecutorPrivate::BuildAutomaticTattooStateText(Snapshot),
		*OverrideSuffix));
#endif
}

FText FProjectGameplayDebugCommandExecutor::GetAutomaticTattooDebugRowDescription(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return FText::GetEmpty();
#else
	FProjectAutomaticTattooRuntimeDebugSnapshot Snapshot;
	if (!ProjectGameplayDebugCommandExecutorPrivate::TryFindAutomaticTattooSnapshot(OwnerActor, RowName, Snapshot))
	{
		return FText::GetEmpty();
	}

	const FProjectAutomaticTattooTableRow& Row = Snapshot.EffectiveRow;
	return FText::FromString(FString::Printf(
		TEXT("AT runtime values: X %.2f | Y %.2f | Size %.2f | Rot %.1f | Projection %.2f. %s"),
		Row.OffsetX,
		Row.OffsetY,
		Row.Size,
		Row.RotationDegrees,
		Row.ProjectionDistance,
		*Row.UnlockDescription));
#endif
}

bool FProjectGameplayDebugCommandExecutor::AdjustAutomaticTattooPlacement(
	AActor* OwnerActor,
	const FName RowName,
	const float DeltaOffsetX,
	const float DeltaOffsetY,
	const float DeltaSize,
	const float DeltaRotationDegrees,
	const float DeltaProjectionDistance)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	(void)DeltaOffsetX;
	(void)DeltaOffsetY;
	(void)DeltaSize;
	(void)DeltaRotationDegrees;
	(void)DeltaProjectionDistance;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem
		&& TattooSubsystem->AdjustAutomaticTattooRuntimeDebugPlacement(
			Cast<APawn>(OwnerActor),
			RowName,
			DeltaOffsetX,
			DeltaOffsetY,
			DeltaSize,
			DeltaRotationDegrees,
			DeltaProjectionDistance);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ResetAutomaticTattooPlacement(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem && TattooSubsystem->ResetAutomaticTattooRuntimeDebugPlacement(Cast<APawn>(OwnerActor), RowName);
#endif
}

bool FProjectGameplayDebugCommandExecutor::ToggleAutomaticTattooForcedActive(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	return TattooSubsystem && TattooSubsystem->ToggleAutomaticTattooRuntimeDebugForcedActive(Cast<APawn>(OwnerActor), RowName);
#endif
}

bool FProjectGameplayDebugCommandExecutor::CopyAutomaticTattooPlacementValues(AActor* OwnerActor, const FName RowName)
{
#if UE_BUILD_SHIPPING
	(void)OwnerActor;
	(void)RowName;
	return false;
#else
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		ProjectGameplayDebugCommandExecutorPrivate::FindAutomaticTattooSubsystem(OwnerActor);
	if (!TattooSubsystem)
	{
		return false;
	}

	const FString CopyText = TattooSubsystem->BuildAutomaticTattooRuntimeDebugCopyText(RowName);
	if (CopyText.IsEmpty())
	{
		return false;
	}

	FPlatformApplicationMisc::ClipboardCopy(*CopyText);
	UE_LOG(LogProjectGameplayDebugCommands, Display, TEXT("[GameplayDebug][AT] Copied Automatic Tattoo values: %s"), *CopyText);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			FColor::Cyan,
			FString::Printf(TEXT("Copied AT values: %s"), *RowName.ToString()));
	}
	return true;
#endif
}

TArray<FName> FProjectGameplayDebugCommandExecutor::GetAvailableStatusNames()
{
	TArray<FName> StatusNames;
	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	if (!Settings)
	{
		return StatusNames;
	}

	for (const FProjectSurvivalStatusDefinition& Definition : Settings->BuildResolvedStatusDefinitions())
	{
		if (!Definition.StatusName.IsNone())
		{
			StatusNames.AddUnique(Definition.StatusName);
		}
	}

	StatusNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
	return StatusNames;
}

TArray<EProjectDoctrineAttribute> FProjectGameplayDebugCommandExecutor::GetDebugAttributes()
{
	return {
		EProjectDoctrineAttribute::Willpower,
		EProjectDoctrineAttribute::Offensive,
		EProjectDoctrineAttribute::Defensive,
		EProjectDoctrineAttribute::Faith,
		EProjectDoctrineAttribute::Cunning,
		EProjectDoctrineAttribute::Celerity,
		EProjectDoctrineAttribute::Charisma
	};
}

FName FProjectGameplayDebugCommandExecutor::GetAttributeId(const EProjectDoctrineAttribute Attribute)
{
	switch (Attribute)
	{
	case EProjectDoctrineAttribute::Willpower:
		return TEXT("Willpower");
	case EProjectDoctrineAttribute::Offensive:
		return TEXT("Offensive");
	case EProjectDoctrineAttribute::Defensive:
		return TEXT("Defensive");
	case EProjectDoctrineAttribute::Faith:
		return TEXT("Faith");
	case EProjectDoctrineAttribute::Cunning:
		return TEXT("Cunning");
	case EProjectDoctrineAttribute::Celerity:
		return TEXT("Celerity");
	case EProjectDoctrineAttribute::Charisma:
		return TEXT("Charisma");
	default:
		return NAME_None;
	}
}

FText FProjectGameplayDebugCommandExecutor::GetAttributeDisplayName(const EProjectDoctrineAttribute Attribute)
{
	const FName AttributeId = GetAttributeId(Attribute);
	return AttributeId.IsNone() ? FText::GetEmpty() : FText::FromName(AttributeId);
}
