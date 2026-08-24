#include "RuntimePerformance/ProjectRuntimePerformanceSubsystem.h"

#include "ACFAIController.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "BrainComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Combat/ProjectCombatTypes.h"
#include "Components/ACFDamageHandlerComponent.h"
#include "Components/ACFThreatManagerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CoreGlobals.h"
#include "Debug/ProjectGameplayDebugCommandExecutor.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "DirtyPawnComponent.h"
#include "EFProjectEnemySettings.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformMemory.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "Lockpicking/ProjectLockpickableComponent.h"
#include "Lockpicking/ProjectLockpickingSubsystem.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "RuntimePerformance/ProjectPerformanceBudgetSettings.h"
#include "RuntimePerformance/ProjectPerformanceBudgetSubsystem.h"
#include "RuntimePerformance/ProjectRuntimePerformanceSettings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "Survival/ProjectRuntimeReflectionLibrary.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectRuntimePerformance, Log, All);

namespace ProjectRuntimePerformancePrivate
{
	const FName DungeonSmoke58BenchmarkId(TEXT("DungeonSmoke58"));
	const FName DungeonNaturalGameplay58BenchmarkId(TEXT("DungeonNaturalGameplay58"));
	const FName DungeonAcceptance58BenchmarkId(TEXT("DungeonAcceptance58"));
	const FName DungeonFullStackDiagnostic58BenchmarkId(TEXT("DungeonFullStackDiagnostic58"));
	const FName DungeonCombatStableBenchmarkId(TEXT("DungeonCombatStable"));
	const FName DungeonGameplayRealBenchmarkId(TEXT("DungeonGameplayReal"));
	const FName DungeonFullStackOverloadBenchmarkId(TEXT("DungeonFullStackOverload"));
	const FName StageAcceptanceTraversal(TEXT("TraversalStreaming"));
	const FName StageAcceptanceCombat(TEXT("CombatEight"));
	const FName StageAcceptanceDirtyHud(TEXT("CombatDirtyPawnHud"));
	const FName StageNaturalDungeonBootstrap(TEXT("DungeonBootstrapPreReady"));
	const FName StageNaturalTraversal(TEXT("NaturalTraversal"));
	const FName StageExplorationUI(TEXT("ExplorationUI"));
	const FName StageEnemySoloMelee(TEXT("EnemySoloMelee"));
	const FName StageEnemySoloRanged(TEXT("EnemySoloRanged"));
	const FName StageEnemySoloMage(TEXT("EnemySoloMage"));
	const FName StageStackedCombatDirtyPawn(TEXT("StackedCombatDirtyPawn"));
	const FName StageDefeatStruggle(TEXT("DefeatStruggle"));
	const FName StageIntimacy(TEXT("Intimacy"));
	const FName StageLockpicking(TEXT("Lockpicking"));
	const FName StageYMenuActions(TEXT("YMenuActions"));
	const FName StageDebugCommandSweep(TEXT("DebugCommandSweep"));
	const FName PhysicalDamageType(TEXT("Physical"));
	const FName BenchmarkDamageType(TEXT("Benchmark"));
	const FName HealthName(TEXT("Health"));
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));
	const FName SleepName(TEXT("Sleep"));
	const FName MadnessName(TEXT("Madness"));
	const FName CurseName(TEXT("Curse"));
	const FName PainName(TEXT("Pain"));
	const FName BleedingName(TEXT("Bleeding"));
	const FName DizzyName(TEXT("Dizzy"));
	const FName DirtyName(TEXT("Dirty"));
	const FName SweatyName(TEXT("Sweaty"));
	const FName FrenzyName(TEXT("Frenzy"));
	const FName ExtremePainName(TEXT("ExtremePain"));
	const FName CursedName(TEXT("Cursed"));
	const FName SystemNeedsPressures(TEXT("Needs/Pressures"));
	const FName SystemSurvivalStatus(TEXT("Survival Status"));
	const FName SystemChronicles(TEXT("Chronicles/ActivityFeed"));
	const FName SystemDirtyPawn(TEXT("Dirty Pawn"));
	const FName SystemEnemyAiTargeting(TEXT("Enemy AI/Targeting"));
	const FName SystemCombatDamage(TEXT("Combat/Damage"));
	const FName SystemDefeatStruggle(TEXT("Defeat/Struggle"));
	const FName SystemIntimacy(TEXT("Intimacy"));
	const FName SystemLockpicking(TEXT("Lockpicking"));
	const FName SystemYMenuEmote(TEXT("YMenu/Emote"));
	const FName SystemDebugSweep(TEXT("Debug Sweep"));
	const FName SystemRuntimePerformanceBudget(TEXT("Runtime Performance Budget"));
	static constexpr int32 FullStackDirtyPawnWorkloadStepCount = 6;

	FName NormalizeBenchmarkId(const FName BenchmarkId)
	{
		if (BenchmarkId == DungeonCombatStableBenchmarkId || BenchmarkId == DungeonGameplayRealBenchmarkId)
		{
			return DungeonAcceptance58BenchmarkId;
		}
		if (BenchmarkId == DungeonFullStackOverloadBenchmarkId)
		{
			return DungeonFullStackDiagnostic58BenchmarkId;
		}
		return BenchmarkId;
	}

	struct FFullStackStageTemplate
	{
		FName StageId = NAME_None;
		const TCHAR* StageName = TEXT("");
		double DurationFraction = 0.0;
		bool bRequired = true;
	};

	const TArray<FFullStackStageTemplate>& GetAcceptanceStageTemplates()
	{
		static const TArray<FFullStackStageTemplate> Templates = {
			{ StageAcceptanceTraversal, TEXT("Traversal and Streaming"), 0.25, true },
			{ StageAcceptanceCombat, TEXT("Combat Eight Enemies"), 0.50, true },
			{ StageAcceptanceDirtyHud, TEXT("Combat, DirtyPawn and HUD"), 0.25, true },
		};
		return Templates;
	}

	const TArray<FFullStackStageTemplate>& GetNaturalGameplayStageTemplates()
	{
		static const TArray<FFullStackStageTemplate> Templates = {
			{ StageNaturalTraversal, TEXT("Natural Dungeon Traversal"), 1.0, true },
		};
		return Templates;
	}

	static const TArray<FFullStackStageTemplate>& GetFullStackStageTemplates()
	{
		static const TArray<FFullStackStageTemplate> Templates = {
			{ StageExplorationUI, TEXT("Exploration UI"), 0.10, true },
			{ StageEnemySoloMelee, TEXT("Enemy Solo Melee"), 0.10, true },
			{ StageEnemySoloRanged, TEXT("Enemy Solo Ranged"), 0.10, true },
			{ StageEnemySoloMage, TEXT("Enemy Solo Mage"), 0.10, true },
			{ StageStackedCombatDirtyPawn, TEXT("Stacked Combat Dirty Pawn"), 0.20, true },
			{ StageDefeatStruggle, TEXT("Defeat Struggle"), 0.12, true },
			{ StageIntimacy, TEXT("Intimacy"), 0.10, true },
			{ StageLockpicking, TEXT("Lockpicking"), 0.08, true },
			{ StageYMenuActions, TEXT("Y Menu Actions"), 0.08, true },
			{ StageDebugCommandSweep, TEXT("Debug Command Sweep"), 0.12, true },
		};
		return Templates;
	}

	static const TCHAR* BenchmarkFirstUsePreloadAssetPaths[] =
	{
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel.FF_Cinzel"),
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed.FF_BarlowSemiCondensed"),
		TEXT("/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium.FF_BarlowSemiCondensedMedium"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel.T_Struggle_TopPanel"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel.T_Struggle_MainPanel"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber.T_Struggle_TargetChamber"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing.T_Struggle_TargetRing"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse.T_Struggle_TargetPulse"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow.T_Struggle_Arrow"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak.T_Struggle_GlowStreak"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise.T_Struggle_Noise"),
		TEXT("/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette.T_Struggle_BackdropVignette"),
	};

	static void AddValidBenchmarkPreloadPath(TArray<FSoftObjectPath>& OutPaths, const TCHAR* Path)
	{
		const FSoftObjectPath SoftPath(Path);
		if (SoftPath.IsValid())
		{
			OutPaths.AddUnique(SoftPath);
		}
	}

	static bool IsFullStackCombatTargetingStage(const FName StageId)
	{
		return StageId == StageEnemySoloMelee
			|| StageId == StageEnemySoloRanged
			|| StageId == StageEnemySoloMage
			|| StageId == StageStackedCombatDirtyPawn
			|| StageId == StageDebugCommandSweep;
	}

	static bool SetBenchmarkACFControllerTarget(AController* Controller, AActor* Actor)
	{
		AACFAIController* ACFController = Cast<AACFAIController>(Controller);
		if (!ACFController)
		{
			return false;
		}

		if (Actor)
		{
			if (UACFThreatManagerComponent* ThreatManager = ACFController->GetThreatManager())
			{
				if (!ThreatManager->IsThreatening(Actor))
				{
					const float DefaultThreat = ThreatManager->GetDefaultThreatForActor(Actor);
					ThreatManager->AddThreat(Actor, DefaultThreat > 0.0f ? DefaultThreat : 1.0f);
				}
			}
		}

		ACFController->SetTarget(Actor);
		return true;
	}

	static FString SanitizeForFilename(const FString& Value)
	{
		FString Result = Value;
		const TCHAR* InvalidCharacters = TEXT("\\/:*?\"<>| .");
		for (const TCHAR* Character = InvalidCharacters; *Character; ++Character)
		{
			Result.ReplaceCharInline(*Character, TEXT('_'));
		}
		return Result;
	}

	static FString JsonToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	static FString JsonToCondensedString(const TSharedRef<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object, Writer);
		return Output;
	}

	static FString StripPIEPrefix(FString MapName)
	{
		if (MapName.StartsWith(TEXT("UEDPIE_"), ESearchCase::IgnoreCase))
		{
			TArray<FString> Parts;
			MapName.ParseIntoArray(Parts, TEXT("_"), false);
			if (Parts.Num() >= 3)
			{
				return Parts[2];
			}
		}
		return MapName;
	}
}

void UProjectRuntimePerformanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UProjectRuntimePerformanceSubsystem::Deinitialize()
{
	CancelBenchmark(TEXT("SubsystemDeinitialize"));
	Super::Deinitialize();
}

void UProjectRuntimePerformanceSubsystem::Tick(const float DeltaTime)
{
#if UE_BUILD_SHIPPING
	(void)DeltaTime;
#else
	MaybeStartCommandLineBenchmark();

	if (Phase == EBenchmarkPhase::Idle)
	{
		return;
	}

	TotalElapsedSeconds += DeltaTime;
	PhaseElapsedSeconds += DeltaTime;
	if (IsNaturalGameplay58Profile()
		&& (Phase == EBenchmarkPhase::PendingTravel || Phase == EBenchmarkPhase::Preparing))
	{
		CaptureNaturalBootstrapSample(DeltaTime);
	}

	if (Phase == EBenchmarkPhase::PendingTravel)
	{
		if (IsTargetMapLoaded())
		{
			BeginPreparing();
			return;
		}

		const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
		const float TimeoutSeconds = Settings ? FMath::Max(1.0f, Settings->PreparationTimeoutSeconds) : 90.0f;
		if (!bMapTravelRequested && GetWorld())
		{
			bMapTravelRequested = OpenConfiguredMap();
		}
		if (PhaseElapsedSeconds >= TimeoutSeconds)
		{
			FinishBenchmark(false, TEXT("MapTravelTimeout"));
		}
		return;
	}

	if (Phase == EBenchmarkPhase::Preparing)
	{
		TickPreparing(DeltaTime);
		return;
	}

	if (Phase == EBenchmarkPhase::Warmup || Phase == EBenchmarkPhase::Running)
	{
		TickSampling(DeltaTime);
	}
#endif
}

TStatId UProjectRuntimePerformanceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectRuntimePerformanceSubsystem, STATGROUP_Tickables);
}

bool UProjectRuntimePerformanceSubsystem::IsTickable() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonSmoke58(const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)bQuitOnFinish;
	return false;
#else
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	FProjectRuntimePerformanceBenchmarkRequest Request;
	Request.BenchmarkId = ProjectRuntimePerformancePrivate::DungeonSmoke58BenchmarkId;
	Request.DurationSeconds = Settings ? Settings->SmokeDurationSeconds : 30.0f;
	Request.EnemyCount = Settings ? Settings->AcceptanceEnemyCount : 8;
	Request.Seed = Settings ? Settings->AcceptanceSeed : 42;
	Request.QualityPreset = Settings ? Settings->DefaultQualityPreset : FName(TEXT("Balanced58"));
	Request.bQuitOnFinish = bQuitOnFinish;
	Request.bStrictScenarioFailures = true;
	return RequestBenchmark(Request);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonNaturalGameplay58(
	const float DurationSeconds,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)bQuitOnFinish;
	return false;
#else
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	FProjectRuntimePerformanceBenchmarkRequest Request;
	Request.BenchmarkId = ProjectRuntimePerformancePrivate::DungeonNaturalGameplay58BenchmarkId;
	Request.DurationSeconds = DurationSeconds;
	Request.EnemyCount = -1;
	Request.Seed = Settings ? Settings->AcceptanceSeed : 42;
	Request.QualityPreset = Settings ? Settings->DefaultQualityPreset : FName(TEXT("Balanced58"));
	Request.bQuitOnFinish = bQuitOnFinish;
	Request.bStrictScenarioFailures = true;
	return RequestBenchmark(Request);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonAcceptance58(
	const float DurationSeconds,
	const int32 EnemyCount,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)EnemyCount;
	(void)bQuitOnFinish;
	return false;
#else
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	FProjectRuntimePerformanceBenchmarkRequest Request;
	Request.BenchmarkId = ProjectRuntimePerformancePrivate::DungeonAcceptance58BenchmarkId;
	Request.DurationSeconds = DurationSeconds;
	Request.EnemyCount = EnemyCount;
	Request.Seed = Settings ? Settings->AcceptanceSeed : 42;
	Request.QualityPreset = Settings ? Settings->DefaultQualityPreset : FName(TEXT("Balanced58"));
	Request.bQuitOnFinish = bQuitOnFinish;
	Request.bStrictScenarioFailures = true;
	return RequestBenchmark(Request);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonFullStackDiagnostic58(
	const float DurationSeconds,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)bQuitOnFinish;
	return false;
#else
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	FProjectRuntimePerformanceBenchmarkRequest Request;
	Request.BenchmarkId = ProjectRuntimePerformancePrivate::DungeonFullStackDiagnostic58BenchmarkId;
	Request.DurationSeconds = DurationSeconds;
	Request.EnemyCount = Settings ? Settings->FullStackOverloadEnemyCap : 8;
	Request.Seed = Settings ? Settings->AcceptanceSeed : 42;
	Request.QualityPreset = Settings ? Settings->DefaultQualityPreset : FName(TEXT("Balanced58"));
	Request.bQuitOnFinish = bQuitOnFinish;
	Request.bStrictScenarioFailures = true;
	return RequestBenchmark(Request);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDefaultDungeonCombatBenchmark(
	const float DurationSeconds,
	const int32 EnemyCount,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)EnemyCount;
	(void)bQuitOnFinish;
	return false;
#else
	return RequestDungeonAcceptance58(DurationSeconds, EnemyCount >= 0 ? EnemyCount : 8, bQuitOnFinish);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonGameplayRealBenchmark(
	const float DurationSeconds,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)bQuitOnFinish;
	return false;
#else
	return RequestDungeonAcceptance58(DurationSeconds, 8, bQuitOnFinish);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestDungeonFullStackOverloadBenchmark(
	const float DurationSeconds,
	const int32 EnemyCount,
	const bool bQuitOnFinish)
{
#if UE_BUILD_SHIPPING
	(void)DurationSeconds;
	(void)EnemyCount;
	(void)bQuitOnFinish;
	return false;
#else
	(void)EnemyCount;
	return RequestDungeonFullStackDiagnostic58(DurationSeconds, bQuitOnFinish);
#endif
}

bool UProjectRuntimePerformanceSubsystem::CancelBenchmark(const FName ReasonId)
{
#if UE_BUILD_SHIPPING
	(void)ReasonId;
	return false;
#else
	if (Phase == EBenchmarkPhase::Idle)
	{
		return false;
	}

	FinishBenchmark(false, ReasonId.IsNone() ? TEXT("Cancelled") : ReasonId.ToString());
	return true;
#endif
}

bool UProjectRuntimePerformanceSubsystem::IsBenchmarkRunning() const
{
	return Phase != EBenchmarkPhase::Idle;
}

FString UProjectRuntimePerformanceSubsystem::GetLastBenchmarkSummary() const
{
	return LastSummaryJson;
}

FProjectRuntimePerformanceMetrics UProjectRuntimePerformanceSubsystem::BuildMetrics(const TArray<double>& FrameTimesMs)
{
	FProjectRuntimePerformanceMetrics Metrics;
	Metrics.FrameSampleCount = FrameTimesMs.Num();
	if (FrameTimesMs.IsEmpty())
	{
		return Metrics;
	}

	TArray<double> SortedAscending = FrameTimesMs;
	SortedAscending.Sort();

	TArray<double> SortedDescending = FrameTimesMs;
	SortedDescending.Sort([](const double Left, const double Right)
	{
		return Left > Right;
	});

	double TotalSeconds = 0.0;
	for (const double FrameMs : FrameTimesMs)
	{
		TotalSeconds += FMath::Max(FrameMs, SMALL_NUMBER) / 1000.0;
		if (FrameMs > 50.0)
		{
			++Metrics.HitchesOver50Ms;
		}
		if (FrameMs > 100.0)
		{
			++Metrics.HitchesOver100Ms;
		}
		if (FrameMs > 500.0)
		{
			++Metrics.HitchesOver500Ms;
		}
	}

	Metrics.AverageFps = TotalSeconds > SMALL_NUMBER ? static_cast<double>(FrameTimesMs.Num()) / TotalSeconds : 0.0;
	Metrics.MedianFps = 1000.0 / FMath::Max(PercentileSorted(SortedAscending, 0.5), SMALL_NUMBER);
	Metrics.MinFps = 1000.0 / FMath::Max(SortedAscending.Last(), SMALL_NUMBER);
	Metrics.OnePercentLowFps = AverageSlowestFps(SortedDescending, 0.01);
	Metrics.ZeroPointOnePercentLowFps = AverageSlowestFps(SortedDescending, 0.001);
	Metrics.P95FrameMs = PercentileSorted(SortedAscending, 0.95);
	Metrics.P99FrameMs = PercentileSorted(SortedAscending, 0.99);
	return Metrics;
}

double UProjectRuntimePerformanceSubsystem::PercentileSorted(const TArray<double>& SortedValues, const double Percentile)
{
	if (SortedValues.IsEmpty())
	{
		return 0.0;
	}

	const double ClampedPercentile = FMath::Clamp(Percentile, 0.0, 1.0);
	const double Position = ClampedPercentile * static_cast<double>(SortedValues.Num() - 1);
	const int32 LowerIndex = FMath::FloorToInt(Position);
	const int32 UpperIndex = FMath::CeilToInt(Position);
	if (LowerIndex == UpperIndex)
	{
		return SortedValues[LowerIndex];
	}

	const double Alpha = Position - static_cast<double>(LowerIndex);
	return FMath::Lerp(SortedValues[LowerIndex], SortedValues[UpperIndex], Alpha);
}

double UProjectRuntimePerformanceSubsystem::AverageSlowestFps(
	const TArray<double>& SortedFrameTimesDescending,
	const double Fraction)
{
	if (SortedFrameTimesDescending.IsEmpty())
	{
		return 0.0;
	}

	const int32 SampleCount = FMath::Max(1, FMath::CeilToInt(static_cast<double>(SortedFrameTimesDescending.Num()) * Fraction));
	double TotalSeconds = 0.0;
	for (int32 Index = 0; Index < SampleCount && SortedFrameTimesDescending.IsValidIndex(Index); ++Index)
	{
		TotalSeconds += FMath::Max(SortedFrameTimesDescending[Index], SMALL_NUMBER) / 1000.0;
	}

	return TotalSeconds > SMALL_NUMBER ? static_cast<double>(SampleCount) / TotalSeconds : 0.0;
}

bool UProjectRuntimePerformanceSubsystem::IsSupportedBenchmarkId(const FName BenchmarkId)
{
	return BenchmarkId == ProjectRuntimePerformancePrivate::DungeonSmoke58BenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonNaturalGameplay58BenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonAcceptance58BenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonFullStackDiagnostic58BenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonCombatStableBenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonGameplayRealBenchmarkId
		|| BenchmarkId == ProjectRuntimePerformancePrivate::DungeonFullStackOverloadBenchmarkId;
}

TArray<FName> UProjectRuntimePerformanceSubsystem::BuildFullStackStageIds()
{
	TArray<FName> StageIds;
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetFullStackStageTemplates())
	{
		StageIds.Add(Template.StageId);
	}
	return StageIds;
}

TArray<FName> UProjectRuntimePerformanceSubsystem::BuildAcceptanceStageIds()
{
	TArray<FName> StageIds;
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetAcceptanceStageTemplates())
	{
		StageIds.Add(Template.StageId);
	}
	return StageIds;
}

TArray<FName> UProjectRuntimePerformanceSubsystem::BuildNaturalGameplayStageIds()
{
	TArray<FName> StageIds;
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetNaturalGameplayStageTemplates())
	{
		StageIds.Add(Template.StageId);
	}
	return StageIds;
}

bool UProjectRuntimePerformanceSubsystem::ShouldFailForScenarioIssue(const bool bStrictScenarioFailures, const bool bRequiredStage)
{
	return bStrictScenarioFailures && bRequiredStage;
}

FProjectRuntimePerformanceGateResult UProjectRuntimePerformanceSubsystem::EvaluateAcceptanceMetrics(
	const FProjectRuntimePerformanceMetrics& Metrics,
	const double MinimumAverageFps,
	const double MinimumMedianFps,
	const double MinimumOnePercentLowFps,
	const double MaximumP99FrameMs,
	const int32 MaximumHitchesOver100Ms)
{
	FProjectRuntimePerformanceGateResult Result;
	auto Require = [&Result](const bool bCondition, const FString& Failure)
	{
		if (!bCondition)
		{
			Result.FailureReasons.Add(Failure);
		}
	};

	Require(Metrics.FrameSampleCount > 0, TEXT("NoMeasuredFrames"));
	Require(Metrics.AverageFps >= MinimumAverageFps, FString::Printf(TEXT("AverageFps<%.2f"), MinimumAverageFps));
	Require(Metrics.MedianFps >= MinimumMedianFps, FString::Printf(TEXT("MedianFps<%.2f"), MinimumMedianFps));
	Require(Metrics.OnePercentLowFps >= MinimumOnePercentLowFps, FString::Printf(TEXT("OnePercentLowFps<%.2f"), MinimumOnePercentLowFps));
	Require(Metrics.P99FrameMs <= MaximumP99FrameMs, FString::Printf(TEXT("P99FrameMs>%.2f"), MaximumP99FrameMs));
	Require(Metrics.HitchesOver100Ms <= MaximumHitchesOver100Ms, FString::Printf(TEXT("HitchesOver100Ms>%d"), MaximumHitchesOver100Ms));
	Result.bPassed = Result.FailureReasons.IsEmpty();
	return Result;
}

void UProjectRuntimePerformanceSubsystem::MaybeStartCommandLineBenchmark()
{
#if !UE_BUILD_SHIPPING
	if (bCommandLineChecked)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || ProjectRuntimePerformancePrivate::StripPIEPrefix(World->GetMapName()).Equals(TEXT("Untitled"), ESearchCase::IgnoreCase))
	{
		return;
	}

	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	if (!Settings || !Settings->bAutoStartFromCommandLine)
	{
		bCommandLineChecked = true;
		return;
	}

	FString BenchmarkName;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfProfile="), BenchmarkName)
		&& !FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfBenchmark="), BenchmarkName))
	{
		bCommandLineChecked = true;
		return;
	}

	bCommandLineChecked = true;

	const FName BenchmarkId = ProjectRuntimePerformancePrivate::NormalizeBenchmarkId(FName(*BenchmarkName));
	if (!IsSupportedBenchmarkId(BenchmarkId))
	{
		UE_LOG(LogProjectRuntimePerformance, Warning, TEXT("Unsupported benchmark id '%s'."), *BenchmarkName);
		return;
	}

	FProjectRuntimePerformanceBenchmarkRequest Request;
	Request.BenchmarkId = BenchmarkId;
	Request.DurationSeconds = -1.0f;
	Request.EnemyCount = BenchmarkId == ProjectRuntimePerformancePrivate::DungeonNaturalGameplay58BenchmarkId
		? -1
		: Settings->AcceptanceEnemyCount;
	Request.Seed = Settings->AcceptanceSeed;
	Request.QualityPreset = Settings->DefaultQualityPreset;
	Request.bQuitOnFinish = FParse::Param(FCommandLine::Get(), TEXT("ProjectPerfQuitOnFinish"));
	Request.bStrictScenarioFailures = FParse::Param(FCommandLine::Get(), TEXT("ProjectPerfStrictScenarioFailures"));

	float DurationOverride = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfDuration="), DurationOverride))
	{
		Request.DurationSeconds = DurationOverride;
	}

	int32 EnemyCountOverride = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfEnemyCount="), EnemyCountOverride))
	{
		Request.EnemyCount = EnemyCountOverride;
	}

	FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfSeed="), Request.Seed);
	FString QualityPresetOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfQualityPreset="), QualityPresetOverride))
	{
		Request.QualityPreset = FName(*QualityPresetOverride);
	}
	FParse::Value(FCommandLine::Get(), TEXT("ProjectPerfCommit="), Request.SourceCommit);

	if (BenchmarkId == ProjectRuntimePerformancePrivate::DungeonFullStackDiagnostic58BenchmarkId)
	{
		Request.bStrictScenarioFailures |= Settings->bStrictFullStackScenarioFailures;
		if (Request.EnemyCount < 0)
		{
			Request.EnemyCount = Settings->FullStackOverloadEnemyCap;
		}
	}

	RequestBenchmark(Request);
#endif
}

bool UProjectRuntimePerformanceSubsystem::RequestBenchmark(const FProjectRuntimePerformanceBenchmarkRequest& Request)
{
#if UE_BUILD_SHIPPING
	(void)Request;
	return false;
#else
	if (Phase != EBenchmarkPhase::Idle)
	{
		UE_LOG(LogProjectRuntimePerformance, Warning, TEXT("Benchmark request ignored because another run is active."));
		return false;
	}

	if (!IsSupportedBenchmarkId(Request.BenchmarkId))
	{
		UE_LOG(LogProjectRuntimePerformance, Warning, TEXT("Unsupported benchmark id '%s'."), *Request.BenchmarkId.ToString());
		return false;
	}

	FProjectRuntimePerformanceBenchmarkRequest NormalizedRequest = Request;
	NormalizedRequest.BenchmarkId = ProjectRuntimePerformancePrivate::NormalizeBenchmarkId(Request.BenchmarkId);
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	if (NormalizedRequest.Seed == 0)
	{
		NormalizedRequest.Seed = Settings ? Settings->AcceptanceSeed : 42;
	}
	if (NormalizedRequest.QualityPreset.IsNone())
	{
		NormalizedRequest.QualityPreset = Settings ? Settings->DefaultQualityPreset : FName(TEXT("Balanced58"));
	}
	if (NormalizedRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonAcceptance58BenchmarkId
		|| NormalizedRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonSmoke58BenchmarkId)
	{
		const int32 RequiredEnemyCount = Settings ? Settings->AcceptanceEnemyCount : 8;
		if (NormalizedRequest.EnemyCount < 0)
		{
			NormalizedRequest.EnemyCount = RequiredEnemyCount;
		}
		if (NormalizedRequest.EnemyCount != RequiredEnemyCount)
		{
			UE_LOG(
				LogProjectRuntimePerformance,
				Error,
				TEXT("UE 5.8 acceptance requires exactly %d enemies; requested=%d."),
				RequiredEnemyCount,
				NormalizedRequest.EnemyCount);
			return false;
		}
		NormalizedRequest.bStrictScenarioFailures = true;
	}
	else if (NormalizedRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonNaturalGameplay58BenchmarkId)
	{
		if (NormalizedRequest.EnemyCount >= 0)
		{
			UE_LOG(
				LogProjectRuntimePerformance,
				Error,
				TEXT("DungeonNaturalGameplay58 does not accept a benchmark enemy override; requested=%d."),
				NormalizedRequest.EnemyCount);
			return false;
		}
		NormalizedRequest.EnemyCount = -1;
		NormalizedRequest.bStrictScenarioFailures = true;
	}

	ResetRuntimeState();
	ActiveRequest = NormalizedRequest;
	ActiveRunId = BuildRunId();
	AppendEvent(TEXT("request_received"));

	if (!IsTargetMapLoaded())
	{
		Phase = EBenchmarkPhase::PendingTravel;
		PhaseElapsedSeconds = 0.0;
		bMapTravelRequested = false;
		if (GetWorld())
		{
			bMapTravelRequested = OpenConfiguredMap();
			if (!bMapTravelRequested)
			{
				FinishBenchmark(false, TEXT("FailedToOpenBenchmarkMap"));
				return false;
			}
		}
		return true;
	}

	BeginPreparing();
	return true;
#endif
}

void UProjectRuntimePerformanceSubsystem::BeginPreparing()
{
	Phase = EBenchmarkPhase::Preparing;
	PhaseElapsedSeconds = 0.0;
	if (!ApplyBenchmarkQualityPreset())
	{
		FinishBenchmark(false, TEXT("QualityPresetApplyFailed"));
		return;
	}
	AppendEvent(TEXT("preparing"));
}

void UProjectRuntimePerformanceSubsystem::StartWarmupOrRun()
{
	const float WarmupSeconds = FMath::Max(0.0f, ResolveActiveWarmupSeconds());

	if (!IsNaturalGameplay58Profile())
	{
		CleanupBenchmarkVisualState(true);
		StabilizeBenchmarkCamera(FullStackStartTransform);
	}
	BenchmarkElapsedSeconds = 0.0;
	PhaseElapsedSeconds = 0.0;
	Phase = WarmupSeconds > KINDA_SMALL_NUMBER ? EBenchmarkPhase::Warmup : EBenchmarkPhase::Running;
	AppendEvent(Phase == EBenchmarkPhase::Warmup ? TEXT("warmup_started") : TEXT("sampling_started"));
}

void UProjectRuntimePerformanceSubsystem::FinishBenchmark(const bool bSuccess, const FString& Reason)
{
	if (Phase == EBenchmarkPhase::Idle || bArtifactsWritten)
	{
		Phase = EBenchmarkPhase::Idle;
		return;
	}

	Phase = EBenchmarkPhase::Finishing;
	WriteArtifacts(bSuccess, Reason);
	bArtifactsWritten = true;

	const bool bQuitOnFinish = ActiveRequest.bQuitOnFinish;
	ResetRuntimeState();

	if (bQuitOnFinish)
	{
		if (UWorld* World = GetWorld())
		{
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
		}
	}
}

void UProjectRuntimePerformanceSubsystem::ResetRuntimeState()
{
	ApplyBenchmarkPlayerDamageGuard(nullptr, false);
	SetFullStackSceneCombatSuppressed(nullptr, false);
	SetFullStackEnemyAiPaused(false);

	if (ACameraActor* CameraActor = BenchmarkCameraActor.Get())
	{
		CameraActor->Destroy();
	}
	BenchmarkCameraActor.Reset();

	for (const TWeakObjectPtr<UProjectCombatAttributeComponent>& ComponentPtr : BoundDamageTelemetryComponents)
	{
		if (UProjectCombatAttributeComponent* Component = ComponentPtr.Get())
		{
			Component->OnDamageApplied.RemoveDynamic(this, &ThisClass::HandleBenchmarkDamageApplied);
		}
	}
	for (const TWeakObjectPtr<UACFDamageHandlerComponent>& ComponentPtr : BoundAcfDamageTelemetryComponents)
	{
		if (UACFDamageHandlerComponent* Component = ComponentPtr.Get())
		{
			Component->OnDamageReceived.RemoveDynamic(this, &ThisClass::HandleAcfDamageReceived);
		}
	}

	Phase = EBenchmarkPhase::Idle;
	ActiveRequest = FProjectRuntimePerformanceBenchmarkRequest();
	PhaseElapsedSeconds = 0.0;
	BenchmarkElapsedSeconds = 0.0;
	TotalElapsedSeconds = 0.0;
	VisualSafeStartRetrySeconds = 0.0;
	SpawnedEnemyCount = 0;
	MapTravelCount = 0;
	AsyncLoadingSampleCount = 0;
	PeakPhysicalMemoryBytes = 0;
	bArtifactsWritten = false;
	bMapTravelRequested = false;
	bVisualScreenshotRequested = false;
	bDamageGuardApplied = false;
	bDamageGuardPreviousCanBeDamaged = true;
	bDamageGuardPreviousAcfImmortal = false;
	bFullStackEnemyAiPaused = false;
	MetricsExclusionUntilSeconds = 0.0;
	bSyntheticBenchmarkWorkThisFrame = false;
	SyntheticBenchmarkReasonThisFrame.Reset();
	SyntheticBenchmarkWorkUntilSeconds = 0.0;
	SyntheticBenchmarkWorkReasonActive.Reset();
	SpawnedEnemies.Reset();
	ScenarioActors.Reset();
	CachedBenchmarkRuntimeEnemyClasses.Reset();
	bCachedBenchmarkRuntimeEnemyClassesResolved = false;
	BoundDamageTelemetryComponents.Reset();
	BoundAcfDamageTelemetryComponents.Reset();
	FullStackStages.Reset();
	FullStackSceneSuppressionStates.Reset();
	Samples.Reset();
	NaturalBootstrapSamples.Reset();
	SystemMetricAccumulators.Reset();
	RuntimeBenchmarkPreloadHandle.Reset();
	bBenchmarkPreloadRequested = false;
	bBenchmarkPreloadCompleted = false;
	CurrentWorldSnapshot = FProjectRuntimePerformanceWorldSnapshot();
	PeakWorldSnapshot = FProjectRuntimePerformanceWorldSnapshot();
	NaturalBootstrapWorldSnapshot = FProjectRuntimePerformanceWorldSnapshot();
	WorldSnapshotElapsedSeconds = 0.0;
	NaturalBootstrapSnapshotElapsedSeconds = 0.0;
	LastObservedMapName.Reset();
	FullStackStartTransform = FTransform::Identity;
	DamageGuardedActor.Reset();
	DamageGuardedAcfComponent.Reset();
	ActiveStageEnemy.Reset();
	ActiveLockpickComponent.Reset();
	ActiveFullStackStageIndex = INDEX_NONE;
	ActiveFullStackStageElapsedSeconds = 0.0;
	FullStackActionElapsedSeconds = 0.0;
	FullStackEnemyToPlayerDamageCount = 0;
	FullStackPlayerToEnemyDamageCount = 0;
	StageStartEnemyToPlayerDamageCount = 0;
	StageStartPlayerToEnemyDamageCount = 0;
	FullStackStatusApplyCount = 0;
	FullStackDirtyPaintApplyCount = 0;
	FullStackDebugCommandCount = 0;
	FullStackYMenuActionCount = 0;
	bFullStackScenarioFailed = false;
	bFullStackDefeatStarted = false;
	bFullStackDefeatRecovered = false;
	bFullStackIntimacyStarted = false;
	bFullStackIntimacyActiveSeen = false;
	bFullStackIntimacyAutomationAttempted = false;
	bFullStackIntimacyMenuTestCompleted = false;
	bFullStackIntimacyInteractionCompleted = false;
	bFullStackIntimacySessionPeakTriggered = false;
	bFullStackLockpickStarted = false;
	bFullStackLockpickConfirmed = false;
	bFullStackHudOpened = false;
	bFullStackDirtyWorkloadApplied = false;
	FullStackIntimacyAutomationStepCount = 0;
	FullStackIntimacyFailureReason.Reset();
	FullStackScenarioFailureReason.Reset();
	CurrentStageName.Reset();
	CurrentStageId = NAME_None;
	CurrentScenarioFlags.Reset();
	AcceptanceRoutePoints.Reset();
	AcceptanceRoutePointIndex = 0;
	NaturalRouteDirection = 1;
	NaturalRouteReversalCount = 0;
	NaturalInitialEnemyCount = 0;
	ActiveAcceptanceStageIndex = INDEX_NONE;
	AcceptanceStageElapsedSeconds = 0.0;
	AcceptanceIntegrityElapsedSeconds = 0.0;
	AcceptanceCombatInputElapsedSeconds = 0.0;
	AcceptanceStartLocation = FVector::ZeroVector;
	AcceptanceLastPlayerLocation = FVector::ZeroVector;
	AcceptanceTraversalDistance = 0.0;
	AcceptanceAttackInputCount = 0;
	AcceptanceDodgeInputCount = 0;
	bAcceptanceRouteBuilt = false;
	bAcceptanceEnemiesSpawned = false;
	bAcceptanceDirtyWorkloadStarted = false;
	bAcceptanceUiOpened = false;
	bAcceptanceScenarioFailed = false;
	AcceptanceScenarioFailureReason.Reset();
	NaturalGameplayIntegrityElapsedSeconds = 0.0;
	bNaturalGameplayScenarioFailed = false;
	NaturalGameplayScenarioFailureReason.Reset();
}

void UProjectRuntimePerformanceSubsystem::TickPreparing(const float DeltaTime)
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	const float TimeoutSeconds = Settings ? FMath::Max(1.0f, Settings->PreparationTimeoutSeconds) : 90.0f;
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

	if (!World || !PlayerPawn || !IsTargetMapLoaded() || !IsDungeonRuntimeReady())
	{
		if (PhaseElapsedSeconds >= TimeoutSeconds)
		{
			FinishBenchmark(false, FString::Printf(
				TEXT("PreparationTimeout world=%s player=%s targetMap=%s dungeonReady=%s"),
				World ? *World->GetName() : TEXT("None"),
				PlayerPawn ? *PlayerPawn->GetName() : TEXT("None"),
				IsTargetMapLoaded() ? TEXT("true") : TEXT("false"),
				IsDungeonRuntimeReady() ? TEXT("true") : TEXT("false")));
		}
		return;
	}

	if (IsNaturalGameplay58Profile())
	{
		if (!bBenchmarkPreloadRequested)
		{
			bBenchmarkPreloadRequested = true;
			bBenchmarkPreloadCompleted = true;
			ResolveConfiguredRuntimeEnemyClasses(false);
			TSharedPtr<FJsonObject> PreloadFields = MakeShared<FJsonObject>();
			PreloadFields->SetStringField(TEXT("reason"), TEXT("NaturalGameplayUsesProductRuntimeState"));
			PreloadFields->SetNumberField(TEXT("resident_enemy_class_count"), CachedBenchmarkRuntimeEnemyClasses.Num());
			AppendEvent(TEXT("targeted_preload_skipped"), PreloadFields);
		}
	}
	else
	{
		if (!bBenchmarkPreloadRequested)
		{
			bBenchmarkPreloadRequested = true;
			RequestBenchmarkTargetedPreload();
			return;
		}

		if (RuntimeBenchmarkPreloadHandle.IsValid() && !RuntimeBenchmarkPreloadHandle->HasLoadCompleted())
		{
			if (PhaseElapsedSeconds >= TimeoutSeconds)
			{
				FinishBenchmark(false, TEXT("TargetedPreloadTimeout"));
			}
			return;
		}

		if (!bBenchmarkPreloadCompleted)
		{
			ResolveConfiguredRuntimeEnemyClasses();
			bBenchmarkPreloadCompleted = true;

			TSharedPtr<FJsonObject> PreloadFields = MakeShared<FJsonObject>();
			PreloadFields->SetBoolField(TEXT("handle_valid"), RuntimeBenchmarkPreloadHandle.IsValid());
			PreloadFields->SetNumberField(TEXT("resident_enemy_class_count"), CachedBenchmarkRuntimeEnemyClasses.Num());
			PreloadFields->SetNumberField(TEXT("preparation_elapsed_seconds"), PhaseElapsedSeconds);
			AppendEvent(TEXT("targeted_preload_completed"), PreloadFields);
		}
	}

	VisualSafeStartRetrySeconds += DeltaTime;
	if (VisualSafeStartRetrySeconds < 0.5)
	{
		return;
	}
	VisualSafeStartRetrySeconds = 0.0;

	FTransform StartTransform = PlayerPawn->GetActorTransform();
	ResolveBenchmarkStartTransform(StartTransform);
	FTransform VisualSafeStartTransform = StartTransform;
	if (!ResolveVisualSafeStartTransform(StartTransform, VisualSafeStartTransform))
	{
		if (PhaseElapsedSeconds >= TimeoutSeconds)
		{
			FinishBenchmark(false, TEXT("VisualSafeStartTimeout"));
		}
		return;
	}
	TeleportPlayerToBenchmarkStart(VisualSafeStartTransform, !IsNaturalGameplay58Profile());
	FullStackStartTransform = VisualSafeStartTransform;
	BindDamageTelemetry(PlayerPawn);
	if (IsNaturalGameplay58Profile())
	{
		InitializeNaturalGameplayStage();
		bAcceptanceRouteBuilt = BuildAcceptanceRoute(PlayerPawn);
		AcceptanceStartLocation = PlayerPawn->GetActorLocation();
		AcceptanceLastPlayerLocation = AcceptanceStartLocation;
		if (!bAcceptanceRouteBuilt)
		{
			FinishBenchmark(false, TEXT("NaturalGameplayNavigationRouteUnavailable"));
			return;
		}
		SpawnedEnemyCount = 0;
		NaturalInitialEnemyCount = CountActiveBenchmarkEnemies();
	}
	else if (IsAcceptance58Profile() || IsSmoke58Profile())
	{
		RemovePreexistingRuntimeEnemiesForBenchmark();
		InitializeAcceptanceStages();
		bAcceptanceRouteBuilt = BuildAcceptanceRoute(PlayerPawn);
		AcceptanceStartLocation = PlayerPawn->GetActorLocation();
		AcceptanceLastPlayerLocation = AcceptanceStartLocation;
		if (!bAcceptanceRouteBuilt)
		{
			FinishBenchmark(false, TEXT("AcceptanceNavigationRouteUnavailable"));
			return;
		}
		SpawnedEnemyCount = 0;
	}
	else if (IsStableCombatProfile())
	{
		SpawnedEnemyCount = SpawnBenchmarkEnemies(VisualSafeStartTransform);
	}
	else if (IsFullStackOverloadProfile())
	{
		RemovePreexistingRuntimeEnemiesForBenchmark();
		InitializeFullStackStages();
		SpawnedEnemyCount = 0;
	}
	else
	{
		SpawnedEnemyCount = 0;
	}

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("benchmark_id"), ActiveRequest.BenchmarkId.ToString());
	Fields->SetNumberField(TEXT("spawned_enemy_count"), SpawnedEnemyCount);
	Fields->SetNumberField(TEXT("stage_count"), FullStackStages.Num());
	Fields->SetBoolField(TEXT("strict_scenario_failures"), ActiveRequest.bStrictScenarioFailures);
	Fields->SetBoolField(TEXT("vanilla_ai_perception"), !ShouldUseDirectBenchmarkEnemyTargeting());
	Fields->SetBoolField(TEXT("direct_ai_targeting"), ShouldUseDirectBenchmarkEnemyTargeting());
	Fields->SetBoolField(TEXT("benchmark_enemy_spawning"), !IsNaturalGameplay58Profile());
	Fields->SetBoolField(TEXT("native_player_camera"), IsNaturalGameplay58Profile());
	Fields->SetNumberField(TEXT("natural_enemy_count"), NaturalInitialEnemyCount);
	Fields->SetStringField(TEXT("map"), GetCurrentShortMapName());
	Fields->SetStringField(TEXT("start_location"), VisualSafeStartTransform.GetLocation().ToCompactString());
	AppendEvent(TEXT("scenario_ready"), Fields);

	StartWarmupOrRun();
}

void UProjectRuntimePerformanceSubsystem::CaptureNaturalBootstrapSample(const float DeltaTime)
{
	UWorld* World = GetWorld();
	NaturalBootstrapSnapshotElapsedSeconds += DeltaTime;
	if (World && (NaturalBootstrapSnapshotElapsedSeconds >= 1.0 || NaturalBootstrapSamples.IsEmpty()))
	{
		NaturalBootstrapSnapshotElapsedSeconds = 0.0;
		FProjectRuntimePerformanceWorldSnapshot Snapshot;
		Snapshot.UsedPhysicalMemoryBytes = ReadUsedPhysicalMemoryBytes();
		Snapshot.TexturePoolSizeMb = ReadTexturePoolSizeMb();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			++Snapshot.ActorCount;
			if (Actor->IsA<APawn>())
			{
				++Snapshot.PawnCount;
			}

			TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents(Actor);
			Actor->GetComponents(SkeletalMeshComponents);
			Snapshot.SkeletalMeshComponentCount += SkeletalMeshComponents.Num();

			TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
			Actor->GetComponents(NiagaraComponents);
			Snapshot.NiagaraComponentCount += NiagaraComponents.Num();
		}
		NaturalBootstrapWorldSnapshot = Snapshot;
	}

	FProjectRuntimePerformanceFrameSample Sample;
	Sample.FrameNumber = GFrameCounter;
	Sample.TimeSeconds = TotalElapsedSeconds;
	Sample.DeltaSeconds = DeltaTime;
	Sample.FrameMs = static_cast<double>(DeltaTime) * 1000.0;
	Sample.GameThreadMs = ReadGameThreadMs();
	Sample.RenderThreadMs = ReadRenderThreadMs();
	Sample.GpuMs = ReadGpuMs();
	Sample.Fps = DeltaTime > SMALL_NUMBER ? 1.0 / static_cast<double>(DeltaTime) : 0.0;
	Sample.StageId = ProjectRuntimePerformancePrivate::StageNaturalDungeonBootstrap;
	Sample.StageName = TEXT("Dungeon Bootstrap Before Ready");
	Sample.MapName = GetCurrentShortMapName();
	Sample.ActorCount = NaturalBootstrapWorldSnapshot.ActorCount;
	Sample.PawnCount = NaturalBootstrapWorldSnapshot.PawnCount;
	Sample.SkeletalMeshComponentCount = NaturalBootstrapWorldSnapshot.SkeletalMeshComponentCount;
	Sample.NiagaraComponentCount = NaturalBootstrapWorldSnapshot.NiagaraComponentCount;
	Sample.UsedMemoryMb = static_cast<double>(NaturalBootstrapWorldSnapshot.UsedPhysicalMemoryBytes) / (1024.0 * 1024.0);
	Sample.bAsyncLoading = IsAsyncLoading && IsAsyncLoading();
	if (World)
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			Sample.PlayerLocation = PlayerPawn->GetActorLocation();
			Sample.PlayerSpeedCmPerSecond = PlayerPawn->GetVelocity().Size2D();
		}
	}
	NaturalBootstrapSamples.Add(MoveTemp(Sample));
}

void UProjectRuntimePerformanceSubsystem::TickSampling(const float DeltaTime)
{
	bSyntheticBenchmarkWorkThisFrame = false;
	SyntheticBenchmarkReasonThisFrame.Reset();

	BenchmarkElapsedSeconds += DeltaTime;
	if (Phase == EBenchmarkPhase::Running && IsNaturalGameplay58Profile())
	{
		TickNaturalGameplayScenario(DeltaTime);
	}
	else if (Phase == EBenchmarkPhase::Running && (IsAcceptance58Profile() || IsSmoke58Profile()))
	{
		TickAcceptanceScenario(DeltaTime);
	}
	else if (Phase == EBenchmarkPhase::Running && IsFullStackOverloadProfile())
	{
		TickFullStackScenario(DeltaTime);
	}
	if (!IsNaturalGameplay58Profile())
	{
		CleanupBenchmarkVisualState(false);
	}
	ApplyAutopilot(DeltaTime);
	if (!IsNaturalGameplay58Profile())
	{
		UpdateBenchmarkCameraActor(false);
	}
	MaybeCaptureBenchmarkVisualScreenshot();

	PeakPhysicalMemoryBytes = FMath::Max(PeakPhysicalMemoryBytes, ReadUsedPhysicalMemoryBytes());
	WorldSnapshotElapsedSeconds += DeltaTime;
	if (WorldSnapshotElapsedSeconds >= 1.0 || Samples.IsEmpty())
	{
		WorldSnapshotElapsedSeconds = 0.0;
		UpdateWorldSnapshot();
	}

	FProjectRuntimePerformanceFrameSample Sample;
	Sample.FrameNumber = GFrameCounter;
	Sample.TimeSeconds = BenchmarkElapsedSeconds;
	Sample.DeltaSeconds = DeltaTime;
	Sample.FrameMs = static_cast<double>(DeltaTime) * 1000.0;
	Sample.GameThreadMs = ReadGameThreadMs();
	Sample.RenderThreadMs = ReadRenderThreadMs();
	Sample.GpuMs = ReadGpuMs();
	Sample.Fps = DeltaTime > SMALL_NUMBER ? 1.0 / static_cast<double>(DeltaTime) : 0.0;
	Sample.bWarmup = Phase == EBenchmarkPhase::Warmup;
	Sample.bExcludedFromMetrics = Phase == EBenchmarkPhase::Running && BenchmarkElapsedSeconds <= MetricsExclusionUntilSeconds;
	const bool bSyntheticBenchmarkWindow =
		Phase == EBenchmarkPhase::Running && BenchmarkElapsedSeconds <= SyntheticBenchmarkWorkUntilSeconds;
	Sample.bSyntheticBenchmarkWork = bSyntheticBenchmarkWorkThisFrame || bSyntheticBenchmarkWindow;
	Sample.SyntheticBenchmarkReason = bSyntheticBenchmarkWorkThisFrame
		? SyntheticBenchmarkReasonThisFrame
		: (bSyntheticBenchmarkWindow ? SyntheticBenchmarkWorkReasonActive : FString());
	Sample.bExpectedIntimacySuppression =
		CurrentStageId == ProjectRuntimePerformancePrivate::StageIntimacy
		&& bFullStackIntimacyStarted
		&& !FullStackSceneSuppressionStates.IsEmpty();
	Sample.StageId = CurrentStageId;
	Sample.StageName = CurrentStageName;
	Sample.MapName = GetCurrentShortMapName();
	Sample.ScenarioFlags = CurrentScenarioFlags;
	if (UWorld* World = GetWorld())
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			Sample.PlayerLocation = PlayerPawn->GetActorLocation();
			Sample.PlayerSpeedCmPerSecond = PlayerPawn->GetVelocity().Size2D();
		}
	}
	Sample.TraversalDistance = AcceptanceTraversalDistance;
	Sample.RoutePointIndex = AcceptanceRoutePointIndex;
	CopyWorldSnapshotToSample(Sample);
	Samples.Add(Sample);

	const bool bShouldRetryEnemyTargets =
		IsStableCombatProfile()
		|| (IsFullStackOverloadProfile()
			&& ProjectRuntimePerformancePrivate::IsFullStackCombatTargetingStage(CurrentStageId));
	if (bShouldRetryEnemyTargets && FMath::Fmod(BenchmarkElapsedSeconds, 1.0) < DeltaTime)
	{
		RefreshBenchmarkEnemyAI();
	}

	const float WarmupSeconds = FMath::Max(0.0f, ResolveActiveWarmupSeconds());
	const float DurationSeconds = ResolveActiveDurationSeconds();

	if (Phase == EBenchmarkPhase::Warmup && PhaseElapsedSeconds >= WarmupSeconds)
	{
		Phase = EBenchmarkPhase::Running;
		PhaseElapsedSeconds = 0.0;
		if (IsAcceptance58Profile() || IsSmoke58Profile())
		{
			BeginAcceptanceStage(0);
		}
		else if (IsNaturalGameplay58Profile())
		{
			BeginNaturalGameplayStage();
		}
		AppendEvent(TEXT("sampling_started"));
		return;
	}

	if (Phase == EBenchmarkPhase::Running && PhaseElapsedSeconds >= FMath::Max(1.0f, DurationSeconds))
	{
		if (IsNaturalGameplay58Profile())
		{
			CompleteNaturalGameplayStage();
			FString IntegrityFailure;
			if (!ValidateNaturalGameplayIntegrity(IntegrityFailure))
			{
				FailNaturalGameplayScenario(IntegrityFailure);
			}
		}
		else if (IsAcceptance58Profile() || IsSmoke58Profile())
		{
			if (FullStackStages.IsValidIndex(ActiveAcceptanceStageIndex))
			{
				CompleteAcceptanceStage(ActiveAcceptanceStageIndex);
			}
			FString IntegrityFailure;
			if (!ValidateAcceptanceIntegrity(IntegrityFailure))
			{
				FailAcceptanceScenario(IntegrityFailure);
			}
		}
		FinishBenchmark(
			!bNaturalGameplayScenarioFailed && !bAcceptanceScenarioFailed && !bFullStackScenarioFailed,
			bNaturalGameplayScenarioFailed
				? NaturalGameplayScenarioFailureReason
				: (bAcceptanceScenarioFailed
					? AcceptanceScenarioFailureReason
					: (bFullStackScenarioFailed ? FullStackScenarioFailureReason : TEXT("Completed"))));
	}
}

void UProjectRuntimePerformanceSubsystem::ApplyAutopilot(const float DeltaTime)
{
	if (IsNaturalGameplay58Profile())
	{
		ApplyNaturalGameplayAutopilot(DeltaTime);
		return;
	}

	if (IsAcceptance58Profile() || IsSmoke58Profile())
	{
		ApplyAcceptanceAutopilot(DeltaTime);
		return;
	}

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	AController* Controller = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	if (!PlayerPawn || !Controller)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
	}

	const float Time = static_cast<float>(BenchmarkElapsedSeconds);
	FRotator ControlRotation = Controller->GetControlRotation();
	ControlRotation.Pitch = 0.0f;
	ControlRotation.Roll = 0.0f;
	ControlRotation.Yaw += FMath::Sin(Time * 0.37f) * DeltaTime * 55.0f;
	Controller->SetControlRotation(ControlRotation);

	const FRotator MovementRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(MovementRotation).GetUnitAxis(EAxis::Y);
	PlayerPawn->AddMovementInput(Forward, 0.75f);
	PlayerPawn->AddMovementInput(Right, FMath::Sin(Time * 0.83f) * 0.45f);
}

void UProjectRuntimePerformanceSubsystem::ApplyAcceptanceAutopilot(const float DeltaTime)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	AController* Controller = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	if (!PlayerPawn || !Controller || Phase != EBenchmarkPhase::Running)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
	}

	const FVector CurrentLocation = PlayerPawn->GetActorLocation();
	if (!AcceptanceLastPlayerLocation.IsNearlyZero())
	{
		AcceptanceTraversalDistance += FVector::Dist2D(CurrentLocation, AcceptanceLastPlayerLocation);
	}
	AcceptanceLastPlayerLocation = CurrentLocation;

	if (CurrentStageId == ProjectRuntimePerformancePrivate::StageAcceptanceTraversal)
	{
		if (!AcceptanceRoutePoints.IsValidIndex(AcceptanceRoutePointIndex))
		{
			return;
		}

		FVector ToTarget = AcceptanceRoutePoints[AcceptanceRoutePointIndex] - CurrentLocation;
		ToTarget.Z = 0.0f;
		if (ToTarget.SizeSquared() < FMath::Square(120.0f))
		{
			++AcceptanceRoutePointIndex;
			if (!AcceptanceRoutePoints.IsValidIndex(AcceptanceRoutePointIndex))
			{
				return;
			}
			ToTarget = AcceptanceRoutePoints[AcceptanceRoutePointIndex] - CurrentLocation;
			ToTarget.Z = 0.0f;
		}

		if (!ToTarget.IsNearlyZero())
		{
			FRotator LookRotation = ToTarget.Rotation();
			LookRotation.Pitch = 0.0f;
			LookRotation.Roll = 0.0f;
			Controller->SetControlRotation(LookRotation);
			PlayerPawn->AddMovementInput(ToTarget.GetSafeNormal(), 1.0f);
		}
		return;
	}

	APawn* NearestEnemy = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			const double DistanceSquared = FVector::DistSquared2D(CurrentLocation, EnemyPawn->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				NearestEnemy = EnemyPawn;
			}
		}
	}

	if (!NearestEnemy)
	{
		return;
	}

	FVector ToEnemy = NearestEnemy->GetActorLocation() - CurrentLocation;
	ToEnemy.Z = 0.0f;
	if (ToEnemy.IsNearlyZero())
	{
		return;
	}

	FRotator LookRotation = ToEnemy.Rotation();
	LookRotation.Pitch = 0.0f;
	LookRotation.Roll = 0.0f;
	Controller->SetControlRotation(LookRotation);
	const FVector Forward = ToEnemy.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const float Distance = FMath::Sqrt(static_cast<float>(NearestDistanceSquared));
	const float ForwardScale = Distance > 500.0f ? 0.75f : (Distance < 240.0f ? -0.35f : 0.15f);
	PlayerPawn->AddMovementInput(Forward, ForwardScale);
	PlayerPawn->AddMovementInput(Right, FMath::Sin(static_cast<float>(BenchmarkElapsedSeconds) * 0.9f) * 0.55f);

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		AcceptanceCombatInputElapsedSeconds += DeltaTime;
		if (AcceptanceCombatInputElapsedSeconds >= 1.1)
		{
			AcceptanceCombatInputElapsedSeconds = 0.0;
			PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.0f));
			PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.0f));
			++AcceptanceAttackInputCount;
			if (AcceptanceAttackInputCount % 4 == 0)
			{
				PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Pressed, 1.0f));
				PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Released, 0.0f));
				++AcceptanceDodgeInputCount;
			}
		}
	}
}

void UProjectRuntimePerformanceSubsystem::ApplyNaturalGameplayAutopilot(const float DeltaTime)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	AController* Controller = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	if (!PlayerPawn || !Controller || Phase != EBenchmarkPhase::Running)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
	}

	const FVector CurrentLocation = PlayerPawn->GetActorLocation();
	if (!AcceptanceLastPlayerLocation.IsNearlyZero())
	{
		AcceptanceTraversalDistance += FVector::Dist2D(CurrentLocation, AcceptanceLastPlayerLocation);
	}
	AcceptanceLastPlayerLocation = CurrentLocation;

	if (!AcceptanceRoutePoints.IsValidIndex(AcceptanceRoutePointIndex))
	{
		return;
	}

	FVector ToTarget = AcceptanceRoutePoints[AcceptanceRoutePointIndex] - CurrentLocation;
	ToTarget.Z = 0.0f;
	if (ToTarget.SizeSquared() < FMath::Square(120.0f))
	{
		int32 NextPointIndex = AcceptanceRoutePointIndex + NaturalRouteDirection;
		if (!AcceptanceRoutePoints.IsValidIndex(NextPointIndex))
		{
			NaturalRouteDirection *= -1;
			++NaturalRouteReversalCount;
			NextPointIndex = AcceptanceRoutePointIndex + NaturalRouteDirection;
		}
		if (AcceptanceRoutePoints.IsValidIndex(NextPointIndex))
		{
			AcceptanceRoutePointIndex = NextPointIndex;
			ToTarget = AcceptanceRoutePoints[AcceptanceRoutePointIndex] - CurrentLocation;
			ToTarget.Z = 0.0f;
		}
	}

	if (!ToTarget.IsNearlyZero())
	{
		FRotator LookRotation = ToTarget.Rotation();
		LookRotation.Pitch = 0.0f;
		LookRotation.Roll = 0.0f;
		Controller->SetControlRotation(LookRotation);
		PlayerPawn->AddMovementInput(ToTarget.GetSafeNormal(), 1.0f);
	}

	(void)DeltaTime;
}

bool UProjectRuntimePerformanceSubsystem::IsSmoke58Profile() const
{
	return ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonSmoke58BenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsNaturalGameplay58Profile() const
{
	return ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonNaturalGameplay58BenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsAcceptance58Profile() const
{
	return ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonAcceptance58BenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsDiagnostic58Profile() const
{
	return ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonFullStackDiagnostic58BenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsStableCombatProfile() const
{
	return ActiveRequest.BenchmarkId.IsNone()
		|| ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonCombatStableBenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsRealGameplayProfile() const
{
	return ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonGameplayRealBenchmarkId;
}

bool UProjectRuntimePerformanceSubsystem::IsFullStackOverloadProfile() const
{
	return IsDiagnostic58Profile()
		|| ActiveRequest.BenchmarkId == ProjectRuntimePerformancePrivate::DungeonFullStackOverloadBenchmarkId;
}

float UProjectRuntimePerformanceSubsystem::ResolveActiveDurationSeconds() const
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	if (ActiveRequest.DurationSeconds > 0.0f)
	{
		return ActiveRequest.DurationSeconds;
	}

	if (!Settings)
	{
		return IsSmoke58Profile() ? 30.0f : (IsNaturalGameplay58Profile() ? 60.0f : 120.0f);
	}

	if (IsSmoke58Profile())
	{
		return Settings->SmokeDurationSeconds;
	}
	if (IsAcceptance58Profile())
	{
		return Settings->AcceptanceDurationSeconds;
	}
	if (IsNaturalGameplay58Profile())
	{
		return Settings->NaturalGameplayDurationSeconds;
	}
	if (IsDiagnostic58Profile())
	{
		return Settings->DiagnosticDurationSeconds;
	}
	if (IsStableCombatProfile())
	{
		return Settings->DefaultDurationSeconds;
	}

	if (IsFullStackOverloadProfile())
	{
		return Settings->FullStackOverloadDurationSeconds;
	}

	return Settings->RealGameplayDurationSeconds;
}

float UProjectRuntimePerformanceSubsystem::ResolveActiveWarmupSeconds() const
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	if (!Settings)
	{
		return IsSmoke58Profile() || IsNaturalGameplay58Profile() ? 10.0f : 15.0f;
	}
	if (IsSmoke58Profile())
	{
		return Settings->SmokeWarmupSeconds;
	}
	if (IsNaturalGameplay58Profile())
	{
		return Settings->NaturalGameplayWarmupSeconds;
	}
	return Settings->AcceptanceWarmupSeconds;
}

int32 UProjectRuntimePerformanceSubsystem::ResolveFullStackEnemyCap() const
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	const int32 RequestedEnemyCount = ActiveRequest.EnemyCount >= 0
		? ActiveRequest.EnemyCount
		: (Settings ? Settings->FullStackOverloadEnemyCap : 8);
	return FMath::Clamp(RequestedEnemyCount, 0, 16);
}

bool UProjectRuntimePerformanceSubsystem::IsTargetMapLoaded() const
{
	const FString CurrentShortMapName = GetCurrentShortMapName();
	if (CurrentShortMapName.IsEmpty())
	{
		return false;
	}

	const FString TargetPackage = GetConfiguredMapPackageName();
	const FString TargetShortMapName = FPackageName::GetShortName(TargetPackage);
	return CurrentShortMapName.Equals(TargetShortMapName, ESearchCase::IgnoreCase);
}

FString UProjectRuntimePerformanceSubsystem::GetConfiguredMapPackageName() const
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	FString MapPath = Settings ? Settings->DungeonCombatMap.ToString() : TEXT("/Game/Procedural/Maps/DungeonGeneration");
	if (MapPath.Contains(TEXT(".")))
	{
		MapPath.Split(TEXT("."), &MapPath, nullptr);
	}
	return MapPath;
}

FString UProjectRuntimePerformanceSubsystem::GetCurrentShortMapName() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FString();
	}
	return ProjectRuntimePerformancePrivate::StripPIEPrefix(FPackageName::GetShortName(World->GetMapName()));
}

bool UProjectRuntimePerformanceSubsystem::OpenConfiguredMap()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FString MapPackageName = GetConfiguredMapPackageName();
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("map"), MapPackageName);
	AppendEvent(TEXT("opening_map"), Fields);
	UGameplayStatics::OpenLevel(World, FName(*MapPackageName), true);
	return true;
}

bool UProjectRuntimePerformanceSubsystem::IsDungeonRuntimeReady() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	if (!GameInstance || !World)
	{
		return false;
	}

	UEFProceduralRuntimeSubsystem* ProceduralRuntimeSubsystem =
		GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>();
	return ProceduralRuntimeSubsystem && ProceduralRuntimeSubsystem->IsDungeonRuntimeReady(World);
}

bool UProjectRuntimePerformanceSubsystem::ResolveBenchmarkStartTransform(FTransform& OutTransform) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	if (!GameInstance || !World)
	{
		return false;
	}

	UEFProceduralRuntimeSubsystem* ProceduralRuntimeSubsystem =
		GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>();
	return ProceduralRuntimeSubsystem && ProceduralRuntimeSubsystem->ResolvePlayerStartTransform(World, OutTransform);
}

bool UProjectRuntimePerformanceSubsystem::ResolveVisualSafeStartTransform(
	const FTransform& RequestedTransform,
	FTransform& OutTransform)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutTransform = RequestedTransform;
		return false;
	}

	OutTransform = RequestedTransform;
	FVector CandidateLocation = FVector::ZeroVector;
	FString ResolveReason;
	const bool bResolved = ResolveGroundedBenchmarkLocation(RequestedTransform.GetLocation(), CandidateLocation, ResolveReason);
	if (!bResolved)
	{
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetStringField(TEXT("requested_location"), RequestedTransform.GetLocation().ToCompactString());
		Fields->SetStringField(TEXT("reason"), ResolveReason);
		Fields->SetStringField(TEXT("fallback_location"), RequestedTransform.GetLocation().ToCompactString());
		AppendEvent(TEXT("visual_safe_start_floor_fallback"), Fields);
		CandidateLocation = RequestedTransform.GetLocation();
		ResolveReason = FString::Printf(TEXT("ProceduralStartNoBlockingFloor fallback_reason=%s"), *ResolveReason);
	}

	FRotator SafeRotation = RequestedTransform.GetRotation().Rotator();
	SafeRotation.Pitch = 0.0f;
	SafeRotation.Roll = 0.0f;

	OutTransform.SetLocation(CandidateLocation);
	OutTransform.SetRotation(SafeRotation.Quaternion());

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("requested_location"), RequestedTransform.GetLocation().ToCompactString());
	Fields->SetStringField(TEXT("resolved_location"), CandidateLocation.ToCompactString());
	Fields->SetBoolField(TEXT("adjusted"), !CandidateLocation.Equals(RequestedTransform.GetLocation(), 1.0));
	Fields->SetStringField(TEXT("reason"), ResolveReason);
	AppendEvent(TEXT("visual_safe_start_resolved"), Fields);
	return true;
}

bool UProjectRuntimePerformanceSubsystem::ResolveGroundedBenchmarkLocation(
	const FVector& RequestedLocation,
	FVector& OutLocation,
	FString& OutReason) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutReason = TEXT("WorldMissing");
		return false;
	}

	float PawnHalfHeight = 92.0f;
	float PawnRadius = 34.0f;
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (const ACharacter* Character = Cast<ACharacter>(PlayerPawn))
		{
			if (const UCapsuleComponent* CapsuleComponent = Character->GetCapsuleComponent())
			{
				PawnHalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
				PawnRadius = CapsuleComponent->GetScaledCapsuleRadius();
			}
		}
	}

	FCollisionQueryParams QueryParams(TEXT("ProjectRuntimePerfGroundedLocation"), false);
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}
	if (ACameraActor* CameraActor = BenchmarkCameraActor.Get())
	{
		QueryParams.AddIgnoredActor(CameraActor);
	}
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			QueryParams.AddIgnoredActor(EnemyPawn);
		}
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	auto TryCandidate = [&](const FVector& Candidate, const TCHAR* SourceLabel) -> bool
	{
		FVector Projected = Candidate;
		if (UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation NavLocation;
			if (NavSystem->ProjectPointToNavigation(Candidate, NavLocation, FVector(900.0f, 900.0f, 900.0f)))
			{
				Projected = NavLocation.Location;
			}
		}

		FHitResult FloorHit;
		const FVector TraceStart = Projected + FVector(0.0f, 0.0f, 3200.0f);
		const FVector TraceEnd = Projected - FVector(0.0f, 0.0f, 5200.0f);
		const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(FMath::Max(8.0f, PawnRadius * 0.45f));
		const bool bHit = World->SweepSingleByObjectType(
			FloorHit,
			TraceStart,
			TraceEnd,
			FQuat::Identity,
			ObjectParams,
			ProbeShape,
			QueryParams);

		if (!bHit || !FloorHit.bBlockingHit || FloorHit.ImpactNormal.Z < 0.25f)
		{
			return false;
		}

		const FVector GroundedLocation = FloorHit.ImpactPoint + FVector(0.0f, 0.0f, PawnHalfHeight + 5.0f);
		if (World->OverlapBlockingTestByChannel(
			GroundedLocation,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(PawnRadius * 0.85f, PawnHalfHeight * 0.95f),
			QueryParams))
		{
			return false;
		}

		OutLocation = GroundedLocation;
		OutReason = FString::Printf(TEXT("%s floor=%s actor=%s"), SourceLabel, *FloorHit.ImpactPoint.ToCompactString(), *GetNameSafe(FloorHit.GetActor()));
		return true;
	};

	if (TryCandidate(RequestedLocation, TEXT("requested")))
	{
		return true;
	}

	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		if (TryCandidate(PlayerPawn->GetActorLocation(), TEXT("current_pawn")))
		{
			return true;
		}
	}

	static const float Radii[] = { 300.0f, 600.0f, 1000.0f, 1500.0f, 2200.0f, 3200.0f, 4600.0f };
	for (const float Radius : Radii)
	{
		const int32 StepCount = Radius <= 600.0f ? 8 : 16;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const float Angle = 2.0f * PI * static_cast<float>(StepIndex) / static_cast<float>(StepCount);
			const FVector Candidate = RequestedLocation + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			if (TryCandidate(Candidate, TEXT("radial_search")))
			{
				return true;
			}
		}
	}

	static const float GridStep = 600.0f;
	static const int32 GridRadiusSteps = 6;
	for (int32 Ring = 1; Ring <= GridRadiusSteps; ++Ring)
	{
		for (int32 XStep = -Ring; XStep <= Ring; ++XStep)
		{
			for (int32 YStep = -Ring; YStep <= Ring; ++YStep)
			{
				if (FMath::Max(FMath::Abs(XStep), FMath::Abs(YStep)) != Ring)
				{
					continue;
				}

				const FVector Candidate = RequestedLocation + FVector(
					static_cast<float>(XStep) * GridStep,
					static_cast<float>(YStep) * GridStep,
					0.0f);
				if (TryCandidate(Candidate, TEXT("grid_search")))
				{
					return true;
				}
			}
		}
	}

	OutReason = TEXT("NoBlockingFloorFoundNearRequestedLocation");
	return false;
}

void UProjectRuntimePerformanceSubsystem::TeleportPlayerToBenchmarkStart(
	const FTransform& StartTransform,
	const bool bUseBenchmarkCamera)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	PlayerPawn->SetActorTransform(StartTransform, false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* Character = Cast<ACharacter>(PlayerPawn))
	{
		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->StopMovementImmediately();
			MovementComponent->SetMovementMode(MOVE_Walking);
		}
		Character->UnCrouch(false);
	}

	if (bUseBenchmarkCamera)
	{
		CleanupBenchmarkVisualState(true);
		StabilizeBenchmarkCamera(StartTransform);
	}
	else if (APlayerController* PlayerController = Cast<APlayerController>(PlayerPawn->GetController()))
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->SetShowMouseCursor(false);
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->SetViewTarget(PlayerPawn);
	}
}

void UProjectRuntimePerformanceSubsystem::CleanupBenchmarkVisualState(const bool bCancelInteractions)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	APlayerController* PlayerController = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (!PlayerController && World)
	{
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}

	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->StopCameraFade();
		PlayerController->PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, false);
	}

	if (!bCancelInteractions)
	{
		return;
	}

	if (PlayerController)
	{
		if (!UpdateBenchmarkCameraActor(true) && PlayerPawn)
		{
			PlayerController->SetViewTarget(PlayerPawn);
		}
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->SetShowMouseCursor(false);
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->FlushPressedKeys();
	}

	if (ACharacter* Character = Cast<ACharacter>(PlayerPawn))
	{
		if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
			MovementComponent->StopMovementImmediately();
		}
	}

	if (UProjectLockpickingSubsystem* LockpickingSubsystem = World ? World->GetSubsystem<UProjectLockpickingSubsystem>() : nullptr)
	{
		LockpickingSubsystem->CloseLockpicking();
		LockpickingSubsystem->CloseLockpickPrompt();
	}

	RemoveBenchmarkBlockingMenus();

	if (UProjectIntimacySubsystem* IntimacySubsystem = World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr)
	{
		IntimacySubsystem->RequestCancelIntimacy();
	}

	if (UProjectEmoteSubsystem* EmoteSubsystem = World ? World->GetSubsystem<UProjectEmoteSubsystem>() : nullptr)
	{
		EmoteSubsystem->RequestCancelEmoteMenu();
		EmoteSubsystem->RequestCancelActiveEmote();
		EmoteSubsystem->CancelRuntimeAction(TEXT("RuntimePerformanceVisualCleanup"));
	}

	if (PlayerPawn)
	{
		if (UProjectEmoteComponent* EmoteComponent = PlayerPawn->FindComponentByClass<UProjectEmoteComponent>())
		{
			EmoteComponent->StopEmote();
		}
	}
}

void UProjectRuntimePerformanceSubsystem::RemoveBenchmarkBlockingMenus()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(), false);

	int32 RemovedCount = 0;
	for (UUserWidget* Widget : Widgets)
	{
		if (!Widget)
		{
			continue;
		}

		const FString WidgetName = Widget->GetName();
		const FString ClassName = Widget->GetClass() ? Widget->GetClass()->GetName() : FString();
		const bool bBlocksAutomation =
			WidgetName.Contains(TEXT("DeathMenu"))
			|| WidgetName.Contains(TEXT("ACF_Death"))
			|| WidgetName.Contains(TEXT("ANS_PopUp"))
			|| ClassName.Contains(TEXT("DeathMenu"))
			|| ClassName.Contains(TEXT("ACF_Death"))
			|| ClassName.Contains(TEXT("ANS_PopUp"));
		if (!bBlocksAutomation)
		{
			continue;
		}

		Widget->RemoveFromParent();
		++RemovedCount;
	}

	if (RemovedCount > 0)
	{
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetNumberField(TEXT("removed_count"), RemovedCount);
		AppendEvent(TEXT("blocking_menu_removed"), Fields);
	}
}

void UProjectRuntimePerformanceSubsystem::ApplyBenchmarkPlayerDamageGuard(APawn* PlayerPawn, const bool bEnabled)
{
	if (bEnabled && !IsFullStackOverloadProfile())
	{
		return;
	}

	if (bEnabled)
	{
		if (!PlayerPawn)
		{
			return;
		}

		if (bDamageGuardApplied && DamageGuardedActor.Get() != PlayerPawn)
		{
			ApplyBenchmarkPlayerDamageGuard(nullptr, false);
		}

		if (!bDamageGuardApplied)
		{
			DamageGuardedActor = PlayerPawn;
			bDamageGuardPreviousCanBeDamaged = PlayerPawn->CanBeDamaged();
			PlayerPawn->SetCanBeDamaged(false);

			if (UACFDamageHandlerComponent* DamageHandler = PlayerPawn->FindComponentByClass<UACFDamageHandlerComponent>())
			{
				DamageGuardedAcfComponent = DamageHandler;
				bDamageGuardPreviousAcfImmortal = DamageHandler->GetIsImmortal();
				DamageHandler->SetIsImmortal(true);
			}
			else
			{
				DamageGuardedAcfComponent.Reset();
				bDamageGuardPreviousAcfImmortal = false;
			}

			bDamageGuardApplied = true;
			return;
		}

		PlayerPawn->SetCanBeDamaged(false);
		if (UACFDamageHandlerComponent* DamageHandler = DamageGuardedAcfComponent.Get())
		{
			if (!DamageHandler->GetIsImmortal())
			{
				DamageHandler->SetIsImmortal(true);
			}
		}
		return;
	}

	if (!bDamageGuardApplied)
	{
		return;
	}

	if (AActor* GuardedActor = DamageGuardedActor.Get())
	{
		GuardedActor->SetCanBeDamaged(bDamageGuardPreviousCanBeDamaged);
	}

	if (UACFDamageHandlerComponent* DamageHandler = DamageGuardedAcfComponent.Get())
	{
		DamageHandler->SetIsImmortal(bDamageGuardPreviousAcfImmortal);
	}

	DamageGuardedActor.Reset();
	DamageGuardedAcfComponent.Reset();
	bDamageGuardApplied = false;
	bDamageGuardPreviousCanBeDamaged = true;
	bDamageGuardPreviousAcfImmortal = false;
}

void UProjectRuntimePerformanceSubsystem::SetFullStackEnemyAiPaused(const bool bPaused)
{
	if (!bPaused && !bFullStackEnemyAiPaused)
	{
		return;
	}

	bFullStackEnemyAiPaused = bPaused;
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		APawn* EnemyPawn = EnemyPtr.Get();
		if (!EnemyPawn)
		{
			continue;
		}

		if (AController* Controller = EnemyPawn->GetController())
		{
			Controller->StopMovement();
			if (AAIController* AIController = Cast<AAIController>(Controller))
			{
				AIController->ClearFocus(EAIFocusPriority::Gameplay);
				if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
				{
					if (bPaused)
					{
						if (!BrainComponent->IsPaused())
						{
							BrainComponent->PauseLogic(TEXT("ProjectRuntimePerformance.Intimacy"));
						}
					}
					else
					{
						BrainComponent->ResumeLogic(TEXT("ProjectRuntimePerformance.Intimacy"));
					}
				}
			}
		}

		if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn))
		{
			if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
			{
				MovementComponent->StopMovementImmediately();
			}
		}

		if (bPaused)
		{
			if (USkeletalMeshComponent* MeshComponent = EnemyPawn->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
				{
					AnimInstance->Montage_Stop(0.15f);
				}
			}
		}
	}

	AppendEvent(bPaused ? TEXT("full_stack_enemy_ai_paused") : TEXT("full_stack_enemy_ai_resumed"));
}

void UProjectRuntimePerformanceSubsystem::SetFullStackSceneCombatSuppressed(APawn* PartnerPawn, const bool bSuppressed)
{
	if (!bSuppressed)
	{
		if (FullStackSceneSuppressionStates.IsEmpty())
		{
			return;
		}

		for (const FProjectRuntimePerformanceEnemySceneSuppressionState& State : FullStackSceneSuppressionStates)
		{
			APawn* EnemyPawn = State.EnemyPawn.Get();
			if (!EnemyPawn)
			{
				continue;
			}

			EnemyPawn->SetActorHiddenInGame(State.bHidden);
			EnemyPawn->SetActorEnableCollision(State.bCollisionEnabled);
			EnemyPawn->SetCanBeDamaged(State.bCanBeDamaged);
			EnemyPawn->SetActorTickEnabled(State.bActorTickEnabled);

			if (UBrainComponent* BrainComponent = State.BrainComponent.Get())
			{
				if (!State.bBrainWasPaused)
				{
					BrainComponent->ResumeLogic(TEXT("ProjectRuntimePerformance.SceneSuppression"));
				}
			}

			if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn))
			{
				if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
				{
					MovementComponent->SetMovementMode(MOVE_Walking);
				}
			}
		}

		FullStackSceneSuppressionStates.Reset();
		AppendEvent(TEXT("full_stack_scene_combat_restored"));
		return;
	}

	FullStackSceneSuppressionStates.Reset();
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		APawn* EnemyPawn = EnemyPtr.Get();
		if (!EnemyPawn)
		{
			continue;
		}

		const bool bPartner = EnemyPawn == PartnerPawn;
		FProjectRuntimePerformanceEnemySceneSuppressionState State;
		State.EnemyPawn = EnemyPawn;
		State.bHidden = EnemyPawn->IsHidden();
		State.bCollisionEnabled = EnemyPawn->GetActorEnableCollision();
		State.bCanBeDamaged = EnemyPawn->CanBeDamaged();
		State.bActorTickEnabled = EnemyPawn->IsActorTickEnabled();
		State.bPartner = bPartner;

		if (AController* Controller = EnemyPawn->GetController())
		{
			Controller->StopMovement();
			ProjectRuntimePerformancePrivate::SetBenchmarkACFControllerTarget(Controller, nullptr);
			if (AAIController* AIController = Cast<AAIController>(Controller))
			{
				AIController->ClearFocus(EAIFocusPriority::Gameplay);
				if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
				{
					State.BrainComponent = BrainComponent;
					State.bBrainWasPaused = BrainComponent->IsPaused();
					if (!State.bBrainWasPaused)
					{
						BrainComponent->PauseLogic(TEXT("ProjectRuntimePerformance.SceneSuppression"));
					}
				}
			}
		}

		if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn))
		{
			if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
			{
				MovementComponent->StopMovementImmediately();
			}
		}

		if (USkeletalMeshComponent* MeshComponent = EnemyPawn->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.15f);
			}
		}

		EnemyPawn->SetCanBeDamaged(false);
		EnemyPawn->SetActorEnableCollision(false);
		if (!bPartner)
		{
			EnemyPawn->SetActorHiddenInGame(true);
			EnemyPawn->SetActorTickEnabled(false);
		}

		FullStackSceneSuppressionStates.Add(State);
	}

	AppendEvent(TEXT("full_stack_scene_combat_suppressed"));
}

void UProjectRuntimePerformanceSubsystem::StabilizeBenchmarkCamera(const FTransform& FocusTransform)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	APlayerController* PlayerController = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (!PlayerController && World)
	{
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}
	if (!PlayerController)
	{
		return;
	}

	FRotator CameraRotation = FocusTransform.GetRotation().Rotator();
	CameraRotation.Pitch = -8.0f;
	CameraRotation.Roll = 0.0f;
	PlayerController->SetControlRotation(CameraRotation);

	if (PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->StopCameraFade();
		PlayerController->PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, false);
	}

	UpdateBenchmarkCameraActor(true);
}

ACameraActor* UProjectRuntimePerformanceSubsystem::EnsureBenchmarkCameraActor()
{
	if (ACameraActor* ExistingCamera = BenchmarkCameraActor.Get())
	{
		return ExistingCamera;
	}

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!World || !PlayerPawn)
	{
		return nullptr;
	}

	FVector CameraLocation = PlayerPawn->GetActorLocation() - PlayerPawn->GetActorForwardVector() * 420.0f + FVector(0.0f, 0.0f, 170.0f);
	FRotator CameraRotation = (PlayerPawn->GetActorLocation() + FVector(0.0f, 0.0f, 95.0f) - CameraLocation).Rotation();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerPawn;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	ACameraActor* CameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation, CameraRotation, SpawnParameters);
	if (!CameraActor)
	{
		return nullptr;
	}

	if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(82.0f);
		CameraComponent->bConstrainAspectRatio = false;
	}

	BenchmarkCameraActor = CameraActor;
	TrackScenarioActor(CameraActor);

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("camera"), GetNameSafe(CameraActor));
	AppendEvent(TEXT("benchmark_camera_spawned"), Fields);
	return CameraActor;
}

bool UProjectRuntimePerformanceSubsystem::UpdateBenchmarkCameraActor(const bool bForceViewTarget)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	APlayerController* PlayerController = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (!PlayerController && World)
	{
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}

	ACameraActor* CameraActor = EnsureBenchmarkCameraActor();
	if (!World || !PlayerPawn || !PlayerController || !CameraActor)
	{
		return false;
	}

	const FRotator ControlRotation = PlayerController->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	const FVector FocusLocation = PlayerPawn->GetActorLocation() + FVector(0.0f, 0.0f, 105.0f);
	FVector DesiredLocation = FocusLocation - Forward * 430.0f + Right * 55.0f + FVector(0.0f, 0.0f, 145.0f);

	FCollisionQueryParams QueryParams(TEXT("ProjectRuntimePerfBenchmarkCamera"), false);
	QueryParams.AddIgnoredActor(PlayerPawn);
	QueryParams.AddIgnoredActor(CameraActor);
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			QueryParams.AddIgnoredActor(EnemyPawn);
		}
	}

	FHitResult CameraHit;
	if (World->LineTraceSingleByChannel(CameraHit, FocusLocation, DesiredLocation, ECC_Camera, QueryParams)
		&& CameraHit.bBlockingHit)
	{
		DesiredLocation = CameraHit.ImpactPoint + CameraHit.ImpactNormal * 24.0f;
	}

	const FRotator DesiredRotation = (FocusLocation - DesiredLocation).Rotation();
	CameraActor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(82.0f);
	}

	if (bForceViewTarget || PlayerController->GetViewTarget() != CameraActor)
	{
		PlayerController->SetViewTargetWithBlend(CameraActor, 0.0f);
	}

	if (PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->StopCameraFade();
		PlayerController->PlayerCameraManager->SetManualCameraFade(0.0f, FLinearColor::Black, false);
	}

	return true;
}

void UProjectRuntimePerformanceSubsystem::MaybeCaptureBenchmarkVisualScreenshot()
{
	if (bVisualScreenshotRequested || Phase != EBenchmarkPhase::Running || BenchmarkElapsedSeconds < 2.0)
	{
		return;
	}

	const FString RunDirectory = ResolveRunDirectory();
	IFileManager::Get().MakeDirectory(*RunDirectory, true);
	const FString ScreenshotPath = FPaths::Combine(RunDirectory, TEXT("visual_check.png"));
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
	bVisualScreenshotRequested = true;
	MetricsExclusionUntilSeconds = FMath::Max(MetricsExclusionUntilSeconds, BenchmarkElapsedSeconds + 1.0);

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("path"), ScreenshotPath);
	Fields->SetStringField(TEXT("stage_id"), CurrentStageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), CurrentStageName);
	Fields->SetStringField(TEXT("phase"), TEXT("running"));
	Fields->SetBoolField(TEXT("excluded_from_metrics"), true);
	Fields->SetNumberField(TEXT("exclusion_until_seconds"), MetricsExclusionUntilSeconds);
	AppendEvent(TEXT("visual_screenshot_requested"), Fields);
}

void UProjectRuntimePerformanceSubsystem::RequestBenchmarkTargetedPreload()
{
#if !UE_BUILD_SHIPPING
	if (!IsFullStackOverloadProfile()
		&& !IsStableCombatProfile()
		&& !IsAcceptance58Profile()
		&& !IsSmoke58Profile())
	{
		return;
	}

	TArray<FSoftObjectPath> AssetsToPreload;
	const UProjectPerformanceBudgetSettings* BudgetSettings = UProjectPerformanceBudgetSettings::Get();
	AssetsToPreload.Reserve((BudgetSettings ? BudgetSettings->AdditionalPreloadAssets.Num() : 0) + 32);

	if (const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get())
	{
		for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
		{
			if (EnemyClassPath.IsValid())
			{
				AssetsToPreload.AddUnique(EnemyClassPath);
			}
		}
	}

	if (const UProjectDefeatFlowSettings* DefeatSettings = UProjectDefeatFlowSettings::Get())
	{
		const FSoftObjectPath KnockoutWidgetPath = DefeatSettings->KnockoutStruggleWidgetClass.ToSoftObjectPath();
		if (KnockoutWidgetPath.IsValid())
		{
			AssetsToPreload.AddUnique(KnockoutWidgetPath);
		}
	}

	if (const UProjectIntimacySettings* IntimacySettings = UProjectIntimacySettings::Get())
	{
		if (IntimacySettings->SocialCardRowsTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->SocialCardRowsTable);
		}
		if (IntimacySettings->TalkOptionsTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->TalkOptionsTable);
		}
		if (IntimacySettings->PartnerResponsesTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->PartnerResponsesTable);
		}
		if (IntimacySettings->MediaCuesTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->MediaCuesTable);
		}
		if (IntimacySettings->PersonalitiesTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->PersonalitiesTable);
		}
		if (IntimacySettings->TalkAffinityTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->TalkAffinityTable);
		}
		if (IntimacySettings->ItemEffectsTable.IsValid())
		{
			AssetsToPreload.AddUnique(IntimacySettings->ItemEffectsTable);
		}
	}

	if (BudgetSettings)
	{
		for (const FSoftObjectPath& AssetPath : BudgetSettings->AdditionalPreloadAssets)
		{
			if (AssetPath.IsValid())
			{
				AssetsToPreload.AddUnique(AssetPath);
			}
		}
	}

	for (const TCHAR* AssetPath : ProjectRuntimePerformancePrivate::BenchmarkFirstUsePreloadAssetPaths)
	{
		ProjectRuntimePerformancePrivate::AddValidBenchmarkPreloadPath(AssetsToPreload, AssetPath);
	}

	if (AssetsToPreload.IsEmpty())
	{
		return;
	}

	RuntimeBenchmarkPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToPreload,
		FStreamableDelegate(),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("ProjectRuntimePerformanceTargetedPreload"));

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetNumberField(TEXT("asset_count"), AssetsToPreload.Num());
	Fields->SetBoolField(TEXT("runtime_budget_enabled"), false);
	Fields->SetStringField(TEXT("profile"), ActiveRequest.BenchmarkId.ToString());
	AppendEvent(TEXT("targeted_preload_requested"), Fields);
#endif
}

void UProjectRuntimePerformanceSubsystem::MarkBenchmarkSyntheticWork(const float ExclusionSeconds, const FString& Reason)
{
	if (Reason.IsEmpty())
	{
		return;
	}

	bSyntheticBenchmarkWorkThisFrame = true;
	SyntheticBenchmarkReasonThisFrame = Reason;
	SyntheticBenchmarkWorkUntilSeconds = FMath::Max(
		SyntheticBenchmarkWorkUntilSeconds,
		BenchmarkElapsedSeconds + FMath::Max(0.0f, ExclusionSeconds));
	SyntheticBenchmarkWorkReasonActive = Reason;
	MetricsExclusionUntilSeconds = FMath::Max(
		MetricsExclusionUntilSeconds,
		SyntheticBenchmarkWorkUntilSeconds);

	if (!CurrentScenarioFlags.Contains(Reason))
	{
		CurrentScenarioFlags = CurrentScenarioFlags.IsEmpty()
			? Reason
			: FString::Printf(TEXT("%s;%s"), *CurrentScenarioFlags, *Reason);
	}

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("reason"), Reason);
	Fields->SetStringField(TEXT("stage_id"), CurrentStageId.ToString());
	Fields->SetNumberField(TEXT("exclusion_until_seconds"), MetricsExclusionUntilSeconds);
	AppendEvent(TEXT("benchmark_synthetic_work"), Fields);
}

bool UProjectRuntimePerformanceSubsystem::ApplyBenchmarkQualityPreset()
{
	const FName Preset = ActiveRequest.QualityPreset.IsNone() ? FName(TEXT("Balanced58")) : ActiveRequest.QualityPreset;
	if (Preset == TEXT("Current") || Preset == TEXT("QualityCurrent"))
	{
		return true;
	}

	const bool bBalanced = Preset == TEXT("Balanced58");
	const bool bPerformance = Preset == TEXT("Performance58");
	if (!bBalanced && !bPerformance)
	{
		UE_LOG(LogProjectRuntimePerformance, Error, TEXT("Unsupported performance preset: %s"), *Preset.ToString());
		return false;
	}

	auto SetCVar = [](const TCHAR* Name, const float Value) -> bool
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!Variable)
		{
			UE_LOG(LogProjectRuntimePerformance, Warning, TEXT("Performance preset CVar is unavailable: %s"), Name);
			return false;
		}
		Variable->Set(Value, ECVF_SetByGameSetting);
		return true;
	};

	bool bApplied = true;
	bApplied &= SetCVar(TEXT("sg.ResolutionQuality"), bBalanced ? 100.0f : 85.0f);
	bApplied &= SetCVar(TEXT("sg.ViewDistanceQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.AntiAliasingQuality"), 2.0f);
	bApplied &= SetCVar(TEXT("sg.ShadowQuality"), bBalanced ? 2.0f : 0.0f);
	bApplied &= SetCVar(TEXT("sg.GlobalIlluminationQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.ReflectionQuality"), 1.0f);
	bApplied &= SetCVar(TEXT("sg.PostProcessQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.TextureQuality"), 3.0f);
	bApplied &= SetCVar(TEXT("sg.EffectsQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.FoliageQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.ShadingQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("sg.LandscapeQuality"), bBalanced ? 2.0f : 1.0f);
	bApplied &= SetCVar(TEXT("r.DynamicRes.OperationMode"), 2.0f);
	bApplied &= SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"), 80.0f);
	bApplied &= SetCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), 100.0f);
	bApplied &= SetCVar(TEXT("r.DynamicRes.FrameTimeBudget"), 16.6667f);
	bApplied &= SetCVar(TEXT("r.VSync"), 0.0f);
	IConsoleManager::Get().CallAllConsoleVariableSinks();

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("preset"), Preset.ToString());
	Fields->SetBoolField(TEXT("all_cvars_available"), bApplied);
	Fields->SetStringField(TEXT("effective_cvar_hash"), BuildEffectiveCVarHash());
	AppendEvent(TEXT("quality_preset_applied"), Fields);
	return bApplied;
}

FString UProjectRuntimePerformanceSubsystem::BuildEffectiveCVarSnapshot() const
{
	static const TCHAR* Names[] = {
		TEXT("sg.ResolutionQuality"),
		TEXT("sg.ViewDistanceQuality"),
		TEXT("sg.AntiAliasingQuality"),
		TEXT("sg.ShadowQuality"),
		TEXT("sg.GlobalIlluminationQuality"),
		TEXT("sg.ReflectionQuality"),
		TEXT("sg.PostProcessQuality"),
		TEXT("sg.TextureQuality"),
		TEXT("sg.EffectsQuality"),
		TEXT("sg.FoliageQuality"),
		TEXT("sg.ShadingQuality"),
		TEXT("sg.LandscapeQuality"),
		TEXT("r.DynamicRes.OperationMode"),
		TEXT("r.DynamicRes.MinScreenPercentage"),
		TEXT("r.DynamicRes.MaxScreenPercentage"),
		TEXT("r.DynamicRes.FrameTimeBudget"),
		TEXT("r.ScreenPercentage"),
		TEXT("r.VSync"),
		TEXT("r.ViewDistanceScale"),
		TEXT("r.SkeletalMeshLODBias"),
		TEXT("r.ShadowQuality"),
		TEXT("r.Shadow.CSM.MaxCascades"),
		TEXT("r.Shadow.MaxResolution"),
		TEXT("r.Shadow.DistanceScale"),
		TEXT("r.Shadow.Virtual.Enable"),
		TEXT("r.DistanceFieldShadowing"),
		TEXT("r.VolumetricFog"),
		TEXT("r.Lumen.DiffuseIndirect.Allow"),
		TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
		TEXT("r.Lumen.Reflections.Allow"),
		TEXT("r.SSR.Quality"),
		TEXT("r.Streaming.PoolSize"),
		TEXT("r.Streaming.MipBias"),
		TEXT("r.Streaming.AmortizeCPUToGPUCopy"),
		TEXT("r.Streaming.MaxNumTexturesToStreamPerFrame"),
		TEXT("fx.Niagara.QualityLevel"),
		TEXT("pcg.Quality"),
		TEXT("r.MaterialQualityLevel"),
		TEXT("r.RayTracing"),
		TEXT("r.Nanite.ProjectEnabled"),
		TEXT("r.CullInstances"),
		TEXT("r.CreateShadersOnLoad"),
		TEXT("r.PSOPrecaching"),
		TEXT("s.AsyncLoadingThreadEnabled"),
		TEXT("s.AsyncPostLoadEnabled"),
	};

	TArray<FString> Values;
	Values.Reserve(UE_ARRAY_COUNT(Names));
	for (const TCHAR* Name : Names)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		Values.Add(FString::Printf(TEXT("%s=%s"), Name, Variable ? *Variable->GetString() : TEXT("<missing>")));
	}
	return FString::Join(Values, TEXT(";"));
}

FString UProjectRuntimePerformanceSubsystem::BuildEffectiveCVarHash() const
{
	const FString Snapshot = BuildEffectiveCVarSnapshot();
	return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Snapshot));
}

int32 UProjectRuntimePerformanceSubsystem::SpawnBenchmarkEnemies(const FTransform& StartTransform)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	const UProjectRuntimePerformanceSettings* PerformanceSettings = UProjectRuntimePerformanceSettings::Get();
	if (!World || !PlayerPawn || !EnemySettings || EnemySettings->RuntimeEnemyClasses.IsEmpty())
	{
		return 0;
	}

	const int32 RequestedEnemyCount = ActiveRequest.EnemyCount >= 0
		? ActiveRequest.EnemyCount
		: (PerformanceSettings ? PerformanceSettings->EnemyCount : 4);
	const int32 ClampedEnemyCount = FMath::Clamp(RequestedEnemyCount, 0, 32);
	const float Radius = PerformanceSettings ? FMath::Max(100.0f, PerformanceSettings->EnemySpawnRadius) : 700.0f;
	const float HeightOffset = PerformanceSettings ? PerformanceSettings->EnemySpawnHeightOffset : 120.0f;
	const FVector Center = StartTransform.GetLocation();
	const UGameInstance* GameInstance = GetGameInstance();
	UEFProceduralRuntimeSubsystem* ProceduralRuntimeSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>()
		: nullptr;

	int32 SpawnCount = 0;
	for (int32 EnemyIndex = 0; EnemyIndex < ClampedEnemyCount; ++EnemyIndex)
	{
		const FSoftClassPath& EnemyClassPath = EnemySettings->RuntimeEnemyClasses[EnemyIndex % EnemySettings->RuntimeEnemyClasses.Num()];
		UClass* EnemyClass = Cast<UClass>(EnemyClassPath.TryLoad());
		if (!EnemyClass || !EnemyClass->IsChildOf(APawn::StaticClass()))
		{
			UE_LOG(LogProjectRuntimePerformance, Warning, TEXT("Benchmark enemy class failed to load or is not a pawn: %s"), *EnemyClassPath.ToString());
			continue;
		}

		const float AngleRadians = (2.0f * PI * static_cast<float>(EnemyIndex)) / FMath::Max(1, ClampedEnemyCount);
		const FVector SpawnOffset(FMath::Cos(AngleRadians) * Radius, FMath::Sin(AngleRadians) * Radius, HeightOffset);
		FVector SpawnLocation = Center + SpawnOffset;
		FString GroundReason;
		if (!ResolveGroundedBenchmarkLocation(SpawnLocation, SpawnLocation, GroundReason))
		{
			UE_LOG(
				LogProjectRuntimePerformance,
				Warning,
				TEXT("Benchmark enemy spawn skipped because no grounded location was found: index=%d requested=%s reason=%s"),
				EnemyIndex,
				*(Center + SpawnOffset).ToCompactString(),
				*GroundReason);
			continue;
		}
		const FRotator SpawnRotation = (Center - SpawnLocation).Rotation();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = PlayerPawn;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.ObjectFlags |= RF_Transient;

		APawn* EnemyPawn = World->SpawnActor<APawn>(EnemyClass, SpawnLocation, SpawnRotation, SpawnParameters);
		if (!EnemyPawn)
		{
			continue;
		}

		SpawnedEnemies.Add(EnemyPawn);
		if (ProceduralRuntimeSubsystem)
		{
			ProceduralRuntimeSubsystem->PostProcessSpawnedActor(EnemyPawn);
		}
		if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn))
		{
			if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
			{
				MovementComponent->SetMovementMode(MOVE_Walking);
			}
		}
		EnemyPawn->SpawnDefaultController();
		PrepareBenchmarkEnemyAI(EnemyPawn, PlayerPawn, true);
		++SpawnCount;
	}

	return SpawnCount;
}

APawn* UProjectRuntimePerformanceSubsystem::SpawnBenchmarkEnemyByClassHint(
	const FTransform& CenterTransform,
	const FName ClassHint,
	const int32 SpawnIndex,
	const bool bRequired)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	const UProjectRuntimePerformanceSettings* PerformanceSettings = UProjectRuntimePerformanceSettings::Get();
	if (!World || !PlayerPawn || !EnemySettings || EnemySettings->RuntimeEnemyClasses.IsEmpty())
	{
		FailFullStackStage(TEXT("EnemySettingsUnavailable"), bRequired);
		return nullptr;
	}

	FSoftClassPath SelectedClassPath;
	const FString HintString = ClassHint.ToString();
	FString ArchetypeHint = HintString;
	FString GenderHint;
	if (HintString.EndsWith(TEXT("Female"), ESearchCase::IgnoreCase))
	{
		ArchetypeHint.LeftChopInline(6);
		GenderHint = TEXT("Female");
	}
	else if (HintString.EndsWith(TEXT("Male"), ESearchCase::IgnoreCase))
	{
		ArchetypeHint.LeftChopInline(4);
		GenderHint = TEXT("Male");
	}
	for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
	{
		const FString ClassPathString = EnemyClassPath.ToString();
		const bool bArchetypeMatches = ArchetypeHint.IsEmpty()
			|| ClassPathString.Contains(ArchetypeHint, ESearchCase::IgnoreCase);
		const bool bGenderMatches = GenderHint.IsEmpty()
			|| (GenderHint.Equals(TEXT("Male"), ESearchCase::IgnoreCase)
				? ClassPathString.Contains(TEXT("Male"), ESearchCase::IgnoreCase)
					&& !ClassPathString.Contains(TEXT("Female"), ESearchCase::IgnoreCase)
				: ClassPathString.Contains(TEXT("Female"), ESearchCase::IgnoreCase));
		if (bArchetypeMatches && bGenderMatches)
		{
			SelectedClassPath = EnemyClassPath;
			break;
		}
	}

	if (!SelectedClassPath.IsValid())
	{
		if (bRequired)
		{
			FailFullStackStage(FString::Printf(TEXT("RequiredEnemyClassMissing:%s"), *ClassHint.ToString()), true);
		}
		return nullptr;
	}

	UClass* EnemyClass = Cast<UClass>(SelectedClassPath.TryLoad());
	if (!EnemyClass || !EnemyClass->IsChildOf(APawn::StaticClass()))
	{
		FailFullStackStage(FString::Printf(TEXT("EnemyClassInvalid:%s"), *SelectedClassPath.ToString()), bRequired);
		return nullptr;
	}

	const float Radius = PerformanceSettings ? FMath::Max(100.0f, PerformanceSettings->EnemySpawnRadius) : 700.0f;
	const float HeightOffset = PerformanceSettings ? PerformanceSettings->EnemySpawnHeightOffset : 120.0f;
	const FVector Center = CenterTransform.GetLocation();
	const float AngleRadians = (2.0f * PI * static_cast<float>(SpawnIndex)) / 8.0f;
	const FVector SpawnOffset(FMath::Cos(AngleRadians) * Radius, FMath::Sin(AngleRadians) * Radius, HeightOffset);
	FVector SpawnLocation = Center + SpawnOffset;
	FString GroundReason;
	if (!ResolveGroundedBenchmarkLocation(SpawnLocation, SpawnLocation, GroundReason))
	{
		FailFullStackStage(FString::Printf(TEXT("EnemySpawnGroundFailed:%s"), *ClassHint.ToString()), bRequired);
		return nullptr;
	}
	const FRotator SpawnRotation = (Center - SpawnLocation).Rotation();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerPawn;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	APawn* EnemyPawn = World->SpawnActor<APawn>(EnemyClass, SpawnLocation, SpawnRotation, SpawnParameters);
	if (!EnemyPawn)
	{
		FailFullStackStage(FString::Printf(TEXT("EnemySpawnFailed:%s"), *ClassHint.ToString()), bRequired);
		return nullptr;
	}

	SpawnedEnemies.Add(EnemyPawn);
	TrackScenarioActor(EnemyPawn);
	BindDamageTelemetry(EnemyPawn);

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UEFProceduralRuntimeSubsystem* ProceduralRuntimeSubsystem = GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>())
		{
			ProceduralRuntimeSubsystem->PostProcessSpawnedActor(EnemyPawn);
		}
	}

	EnemyPawn->SpawnDefaultController();
	if (ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn))
	{
		if (UCharacterMovementComponent* MovementComponent = EnemyCharacter->GetCharacterMovement())
		{
			MovementComponent->SetMovementMode(MOVE_Walking);
		}
	}
	PrepareBenchmarkEnemyAI(EnemyPawn, PlayerPawn, true);
	++SpawnedEnemyCount;

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("class_hint"), ClassHint.ToString());
	Fields->SetStringField(TEXT("enemy"), GetNameSafe(EnemyPawn));
	Fields->SetStringField(TEXT("class"), GetNameSafe(EnemyClass));
	Fields->SetStringField(TEXT("location"), SpawnLocation.ToCompactString());
	Fields->SetStringField(TEXT("ground_reason"), GroundReason);
	AppendEvent(TEXT("enemy_spawned"), Fields);
	return EnemyPawn;
}

int32 UProjectRuntimePerformanceSubsystem::EnsureFullStackEnemyPopulation(
	const FTransform& CenterTransform,
	const int32 DesiredEnemyCount)
{
	const int32 TargetCount = FMath::Clamp(DesiredEnemyCount, 0, ResolveFullStackEnemyCap());
	int32 AliveCount = 0;
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			++AliveCount;
		}
	}

	static const FName SpawnHints[] = {
		TEXT("MeleeMale"),
		TEXT("RangedMale"),
		TEXT("MageMale"),
		TEXT("DummyMale"),
		TEXT("Melee"),
		TEXT("Ranged"),
		TEXT("Mage"),
		TEXT("Dummy")
	};

	int32 SpawnIndex = AliveCount;
	while (AliveCount < TargetCount)
	{
		const FName Hint = SpawnHints[SpawnIndex % UE_ARRAY_COUNT(SpawnHints)];
		if (SpawnBenchmarkEnemyByClassHint(CenterTransform, Hint, SpawnIndex, false))
		{
			++AliveCount;
		}
		else if (SpawnIndex > TargetCount + UE_ARRAY_COUNT(SpawnHints))
		{
			break;
		}
		++SpawnIndex;
	}

	return AliveCount;
}

bool UProjectRuntimePerformanceSubsystem::PrepareBenchmarkEnemyAI(
	APawn* EnemyPawn,
	APawn* PlayerPawn,
	const bool bInitialSetup)
{
	const double TargetingStartSeconds = FPlatformTime::Seconds();
	if (!EnemyPawn || !PlayerPawn)
	{
		return false;
	}

	if (!EnemyPawn->GetController())
	{
		EnemyPawn->SpawnDefaultController();
	}

	AController* Controller = EnemyPawn->GetController();
	if (!Controller)
	{
		return false;
	}

	if (bInitialSetup)
	{
		const FVector ToPlayer = PlayerPawn->GetActorLocation() - EnemyPawn->GetActorLocation();
		if (!ToPlayer.IsNearlyZero())
		{
			FRotator LookRotation = ToPlayer.Rotation();
			LookRotation.Pitch = 0.0f;
			LookRotation.Roll = 0.0f;
			EnemyPawn->SetActorRotation(LookRotation);
			Controller->SetControlRotation(LookRotation);
		}
	}

	bool bTargetSet = true;
	if (ShouldUseDirectBenchmarkEnemyTargeting())
	{
		if (AAIController* AIController = Cast<AAIController>(Controller))
		{
			AIController->SetFocus(PlayerPawn);
			AIController->MoveToActor(PlayerPawn, 150.0f, true);
		}

		bTargetSet = ProjectRuntimePerformancePrivate::SetBenchmarkACFControllerTarget(Controller, PlayerPawn);
	}

	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemEnemyAiTargeting,
		(FPlatformTime::Seconds() - TargetingStartSeconds) * 1000.0,
		CurrentWorldSnapshot.ActiveCombatEnemyCount);
	return bTargetSet;
}

void UProjectRuntimePerformanceSubsystem::RefreshBenchmarkEnemyAI()
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			PrepareBenchmarkEnemyAI(EnemyPawn, PlayerPawn, false);
		}
	}
}

bool UProjectRuntimePerformanceSubsystem::ShouldUseDirectBenchmarkEnemyTargeting() const
{
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectPerfDirectAITargeting")))
	{
		return true;
	}

	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	return Settings
		&& Settings->bAllowBenchmarkDirectAITargeting
		&& !Settings->bUseVanillaAIPerceptionForBenchmarks;
}

void UProjectRuntimePerformanceSubsystem::ResolveConfiguredRuntimeEnemyClasses(const bool bAllowSynchronousLoad) const
{
	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	if (bCachedBenchmarkRuntimeEnemyClassesResolved || !EnemySettings)
	{
		return;
	}

	bCachedBenchmarkRuntimeEnemyClassesResolved = true;
	CachedBenchmarkRuntimeEnemyClasses.Reset();
	for (const FSoftClassPath& EnemyClassPath : EnemySettings->RuntimeEnemyClasses)
	{
		if (!EnemyClassPath.IsValid())
		{
			continue;
		}

		UClass* EnemyClass = Cast<UClass>(EnemyClassPath.ResolveObject());
		if (!EnemyClass && bAllowSynchronousLoad)
		{
			EnemyClass = Cast<UClass>(EnemyClassPath.TryLoad());
		}
		if (EnemyClass && EnemyClass->IsChildOf(APawn::StaticClass()))
		{
			CachedBenchmarkRuntimeEnemyClasses.AddUnique(EnemyClass);
		}
	}
}

bool UProjectRuntimePerformanceSubsystem::IsConfiguredRuntimeEnemyPawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	ResolveConfiguredRuntimeEnemyClasses(!IsNaturalGameplay58Profile());
	for (const TWeakObjectPtr<UClass>& EnemyClassPtr : CachedBenchmarkRuntimeEnemyClasses)
	{
		if (const UClass* EnemyClass = EnemyClassPtr.Get())
		{
			if (Pawn->GetClass()->IsChildOf(EnemyClass))
			{
				return true;
			}
		}
	}

	return false;
}

int32 UProjectRuntimePerformanceSubsystem::RemovePreexistingRuntimeEnemiesForBenchmark()
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!World || !PlayerPawn)
	{
		return 0;
	}

	TSet<APawn*> TrackedBenchmarkEnemies;
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			TrackedBenchmarkEnemies.Add(EnemyPawn);
		}
	}

	int32 RemovedCount = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!IsValid(Pawn) || Pawn == PlayerPawn || TrackedBenchmarkEnemies.Contains(Pawn) || !IsConfiguredRuntimeEnemyPawn(Pawn))
		{
			continue;
		}

		if (AController* Controller = Pawn->GetController())
		{
			Controller->StopMovement();
			Controller->Destroy();
		}
		Pawn->Destroy();
		++RemovedCount;
	}

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetNumberField(TEXT("removed_count"), RemovedCount);
	AppendEvent(TEXT("preexisting_runtime_enemies_removed"), Fields);
	return RemovedCount;
}

int32 UProjectRuntimePerformanceSubsystem::CountActiveBenchmarkEnemies() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	int32 ActiveEnemyCount = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Pawn = *It;
		if (IsValid(Pawn) && Pawn != PlayerPawn && IsConfiguredRuntimeEnemyPawn(Pawn))
		{
			++ActiveEnemyCount;
		}
	}
	return ActiveEnemyCount;
}

void UProjectRuntimePerformanceSubsystem::InitializeNaturalGameplayStage()
{
	FullStackStages.Reset();
	const double DurationSeconds = FMath::Max(1.0f, ResolveActiveDurationSeconds());
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetNaturalGameplayStageTemplates())
	{
		FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages.AddDefaulted_GetRef();
		Stage.StageId = Template.StageId;
		Stage.StageName = Template.StageName;
		Stage.DurationFraction = Template.DurationFraction;
		Stage.StartSeconds = 0.0;
		Stage.EndSeconds = DurationSeconds;
		Stage.bRequired = Template.bRequired;
	}
}

void UProjectRuntimePerformanceSubsystem::BeginNaturalGameplayStage()
{
	if (FullStackStages.IsEmpty())
	{
		FailNaturalGameplayScenario(TEXT("NaturalGameplayStageUnavailable"));
		return;
	}

	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[0];
	Stage.bStarted = true;
	CurrentStageId = Stage.StageId;
	CurrentStageName = Stage.StageName;
	CurrentScenarioFlags = TEXT("natural_dungeon_population;navigation_walk;native_player_camera");

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetNumberField(TEXT("natural_enemy_count"), NaturalInitialEnemyCount);
	Fields->SetNumberField(TEXT("route_point_count"), AcceptanceRoutePoints.Num());
	AppendEvent(TEXT("stage_started"), Fields);
}

void UProjectRuntimePerformanceSubsystem::CompleteNaturalGameplayStage()
{
	if (FullStackStages.IsEmpty())
	{
		return;
	}

	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[0];
	Stage.bCompleted = !Stage.bFailed;
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetBoolField(TEXT("success"), Stage.bCompleted);
	Fields->SetStringField(TEXT("failure_reason"), Stage.FailureReason);
	Fields->SetNumberField(TEXT("traversal_distance"), AcceptanceTraversalDistance);
	Fields->SetNumberField(TEXT("route_reversals"), NaturalRouteReversalCount);
	AppendEvent(TEXT("stage_completed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::TickNaturalGameplayScenario(const float DeltaTime)
{
	NaturalGameplayIntegrityElapsedSeconds += DeltaTime;
	if (NaturalGameplayIntegrityElapsedSeconds < 1.0)
	{
		return;
	}
	NaturalGameplayIntegrityElapsedSeconds = 0.0;

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn || !PlayerPawn->GetController())
	{
		FailNaturalGameplayScenario(TEXT("NaturalGameplayPlayerOrControllerMissing"));
	}
	if (SpawnedEnemyCount != 0 || !SpawnedEnemies.IsEmpty())
	{
		FailNaturalGameplayScenario(TEXT("NaturalGameplayBenchmarkEnemyContamination"));
	}
}

bool UProjectRuntimePerformanceSubsystem::ValidateNaturalGameplayIntegrity(FString& OutReason) const
{
	const UProjectPerformanceBudgetSettings* BudgetSettings = UProjectPerformanceBudgetSettings::Get();
	if (BudgetSettings
		&& (BudgetSettings->bEnableRuntimeBudgeting
			|| BudgetSettings->bApplyEnemyActorTickBudget
			|| BudgetSettings->bApplyEnemyAnimationBudget
			|| BudgetSettings->bApplyEnemyMovementTickBudget
			|| BudgetSettings->bCullExcessRuntimeEnemies
			|| BudgetSettings->bApplyWorldVfxBudget))
	{
		OutReason = TEXT("BLOCKED_BY_SAFETY:RuntimePerformanceBudgetEnabled");
		return false;
	}
	if (!bAcceptanceRouteBuilt || AcceptanceRoutePoints.Num() < 2)
	{
		OutReason = TEXT("NaturalGameplayRouteNotBuilt");
		return false;
	}
	if (AcceptanceTraversalDistance < 600.0)
	{
		OutReason = FString::Printf(TEXT("NaturalGameplayTraversalTooShort:%.1f"), AcceptanceTraversalDistance);
		return false;
	}
	if (SpawnedEnemyCount != 0 || !SpawnedEnemies.IsEmpty())
	{
		OutReason = TEXT("NaturalGameplayBenchmarkEnemyContamination");
		return false;
	}
	if (NaturalInitialEnemyCount <= 0)
	{
		OutReason = TEXT("NaturalGameplayDungeonGeneratedNoEnemies");
		return false;
	}
	if (BenchmarkCameraActor.IsValid())
	{
		OutReason = TEXT("NaturalGameplayBenchmarkCameraActive");
		return false;
	}
	if (AcceptanceAttackInputCount != 0 || AcceptanceDodgeInputCount != 0)
	{
		OutReason = TEXT("NaturalGameplaySyntheticCombatInputDetected");
		return false;
	}
	return true;
}

void UProjectRuntimePerformanceSubsystem::FailNaturalGameplayScenario(const FString& Reason)
{
	if (bNaturalGameplayScenarioFailed)
	{
		return;
	}
	bNaturalGameplayScenarioFailed = true;
	NaturalGameplayScenarioFailureReason = Reason;
	if (!FullStackStages.IsEmpty())
	{
		FullStackStages[0].bFailed = true;
		FullStackStages[0].FailureReason = Reason;
	}
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("reason"), Reason);
	AppendEvent(TEXT("natural_gameplay_scenario_failed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::InitializeAcceptanceStages()
{
	FullStackStages.Reset();
	const double DurationSeconds = FMath::Max(1.0f, ResolveActiveDurationSeconds());
	double CursorSeconds = 0.0;
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetAcceptanceStageTemplates())
	{
		FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages.AddDefaulted_GetRef();
		Stage.StageId = Template.StageId;
		Stage.StageName = Template.StageName;
		Stage.DurationFraction = Template.DurationFraction;
		Stage.StartSeconds = CursorSeconds;
		CursorSeconds += DurationSeconds * Template.DurationFraction;
		Stage.EndSeconds = CursorSeconds;
		Stage.bRequired = Template.bRequired;
	}
	if (!FullStackStages.IsEmpty())
	{
		FullStackStages.Last().EndSeconds = DurationSeconds;
	}
}

bool UProjectRuntimePerformanceSubsystem::BuildAcceptanceRoute(APawn* PlayerPawn)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!World || !PlayerPawn || !NavigationSystem)
	{
		return false;
	}

	const FVector Start = PlayerPawn->GetActorLocation();
	FRandomStream RandomStream(ActiveRequest.Seed);
	const float BaseAngleDegrees = RandomStream.FRandRange(0.0f, 360.0f);
	const float CandidateDistances[] = { 3200.0f, 2600.0f, 2000.0f, 1500.0f };
	TArray<FVector> BestPathPoints;
	double BestPathLength = 0.0;

	for (int32 DistanceIndex = 0; DistanceIndex < UE_ARRAY_COUNT(CandidateDistances); ++DistanceIndex)
	{
		for (int32 DirectionIndex = 0; DirectionIndex < 8; ++DirectionIndex)
		{
			const float AngleRadians = FMath::DegreesToRadians(BaseAngleDegrees + DirectionIndex * 45.0f);
			const FVector Candidate = Start + FVector(
				FMath::Cos(AngleRadians) * CandidateDistances[DistanceIndex],
				FMath::Sin(AngleRadians) * CandidateDistances[DistanceIndex],
				0.0f);
			FNavLocation ProjectedLocation;
			if (!NavigationSystem->ProjectPointToNavigation(Candidate, ProjectedLocation, FVector(350.0f, 350.0f, 500.0f)))
			{
				continue;
			}

			UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
				World,
				Start,
				ProjectedLocation.Location,
				PlayerPawn);
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
			{
				continue;
			}

			double PathLength = 0.0;
			for (int32 PointIndex = 1; PointIndex < Path->PathPoints.Num(); ++PointIndex)
			{
				PathLength += FVector::Dist(Path->PathPoints[PointIndex - 1], Path->PathPoints[PointIndex]);
			}
			if (PathLength > BestPathLength)
			{
				BestPathLength = PathLength;
				BestPathPoints = Path->PathPoints;
			}
		}
	}

	if (BestPathLength < 1000.0 || BestPathPoints.Num() < 2)
	{
		return false;
	}

	AcceptanceRoutePoints = MoveTemp(BestPathPoints);
	AcceptanceRoutePointIndex = 1;
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetNumberField(TEXT("seed"), ActiveRequest.Seed);
	Fields->SetNumberField(TEXT("point_count"), AcceptanceRoutePoints.Num());
	Fields->SetNumberField(TEXT("path_length"), BestPathLength);
	AppendEvent(
		IsNaturalGameplay58Profile()
			? TEXT("natural_navigation_route_resolved")
			: TEXT("acceptance_navigation_route_resolved"),
		Fields);
	return true;
}

int32 UProjectRuntimePerformanceSubsystem::SpawnAcceptanceEnemies(const FTransform& StartTransform)
{
	static const FName RequiredEnemyHints[] = {
		TEXT("MeleeMale"),
		TEXT("MeleeFemale"),
		TEXT("RangedMale"),
		TEXT("RangedFemale"),
		TEXT("MageMale"),
		TEXT("MageFemale"),
		TEXT("MeleeMale"),
		TEXT("RangedFemale"),
	};

	int32 SpawnCount = 0;
	for (int32 EnemyIndex = 0; EnemyIndex < UE_ARRAY_COUNT(RequiredEnemyHints); ++EnemyIndex)
	{
		if (SpawnBenchmarkEnemyByClassHint(StartTransform, RequiredEnemyHints[EnemyIndex], EnemyIndex, true))
		{
			++SpawnCount;
		}
	}
	return SpawnCount;
}

void UProjectRuntimePerformanceSubsystem::BeginAcceptanceStage(const int32 StageIndex)
{
	if (!FullStackStages.IsValidIndex(StageIndex))
	{
		FailAcceptanceScenario(TEXT("AcceptanceStageIndexInvalid"));
		return;
	}

	ActiveAcceptanceStageIndex = StageIndex;
	AcceptanceStageElapsedSeconds = 0.0;
	FullStackActionElapsedSeconds = 0.0;
	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[StageIndex];
	Stage.bStarted = true;
	CurrentStageId = Stage.StageId;
	CurrentStageName = Stage.StageName;
	CurrentScenarioFlags.Reset();

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetNumberField(TEXT("start_seconds"), Stage.StartSeconds);
	Fields->SetNumberField(TEXT("end_seconds"), Stage.EndSeconds);
	AppendEvent(TEXT("stage_started"), Fields);

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		FailAcceptanceScenario(TEXT("AcceptancePlayerMissing"));
		return;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageAcceptanceCombat)
	{
		FullStackStartTransform = PlayerPawn->GetActorTransform();
		SpawnedEnemyCount = SpawnAcceptanceEnemies(FullStackStartTransform);
		bAcceptanceEnemiesSpawned = SpawnedEnemyCount == 8;
		if (!bAcceptanceEnemiesSpawned)
		{
			FailAcceptanceScenario(FString::Printf(TEXT("AcceptanceEnemySpawnCount:%d/8"), SpawnedEnemyCount));
		}
	}
	else if (Stage.StageId == ProjectRuntimePerformancePrivate::StageAcceptanceDirtyHud)
	{
		bAcceptanceUiOpened = OpenFullStackHudAndChronicles();
		bAcceptanceDirtyWorkloadStarted = true;
		FullStackDirtyPaintApplyCount = 0;
		bFullStackDirtyWorkloadApplied = false;
		if (!bAcceptanceUiOpened || !ApplyFullStackDirtyPawnWorkload(PlayerPawn))
		{
			FailAcceptanceScenario(TEXT("AcceptanceDirtyPawnOrHudStartFailed"));
		}
	}
}

void UProjectRuntimePerformanceSubsystem::CompleteAcceptanceStage(const int32 StageIndex)
{
	if (!FullStackStages.IsValidIndex(StageIndex))
	{
		return;
	}
	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[StageIndex];
	Stage.bCompleted = !Stage.bFailed;
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetBoolField(TEXT("success"), !Stage.bFailed);
	Fields->SetStringField(TEXT("failure_reason"), Stage.FailureReason);
	AppendEvent(TEXT("stage_completed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::TickAcceptanceScenario(const float DeltaTime)
{
	if (!FullStackStages.IsValidIndex(ActiveAcceptanceStageIndex))
	{
		FailAcceptanceScenario(TEXT("AcceptanceStageScheduleUnavailable"));
		return;
	}

	AcceptanceStageElapsedSeconds += DeltaTime;
	FullStackActionElapsedSeconds += DeltaTime;
	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[ActiveAcceptanceStageIndex];

	if (Stage.StageId != ProjectRuntimePerformancePrivate::StageAcceptanceTraversal)
	{
		if (FMath::Fmod(AcceptanceStageElapsedSeconds, 0.5) < DeltaTime)
		{
			RefreshBenchmarkEnemyAI();
		}
		if (FMath::Fmod(AcceptanceStageElapsedSeconds, 2.0) < DeltaTime)
		{
			UWorld* World = GetWorld();
			if (APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr)
			{
				ApplyBenchmarkPlayerDamageGuard(PlayerPawn, true);
				RemoveBenchmarkBlockingMenus();
				FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
			}
		}
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageAcceptanceDirtyHud
		&& !bFullStackDirtyWorkloadApplied
		&& FullStackActionElapsedSeconds >= 0.65)
	{
		FullStackActionElapsedSeconds = 0.0;
		UWorld* World = GetWorld();
		if (APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr)
		{
			if (!ApplyFullStackDirtyPawnWorkload(PlayerPawn))
			{
				FailAcceptanceScenario(TEXT("AcceptanceDirtyPawnStepFailed"));
			}
		}
	}

	AcceptanceIntegrityElapsedSeconds += DeltaTime;
	if (AcceptanceIntegrityElapsedSeconds >= 1.0)
	{
		AcceptanceIntegrityElapsedSeconds = 0.0;
		FString IntegrityFailure;
		if (Stage.StageId != ProjectRuntimePerformancePrivate::StageAcceptanceTraversal
			&& !ValidateAcceptanceIntegrity(IntegrityFailure))
		{
			FailAcceptanceScenario(IntegrityFailure);
		}
	}

	if (bAcceptanceScenarioFailed && ActiveRequest.bStrictScenarioFailures)
	{
		FinishBenchmark(false, AcceptanceScenarioFailureReason);
		return;
	}

	if (PhaseElapsedSeconds >= Stage.EndSeconds)
	{
		CompleteAcceptanceStage(ActiveAcceptanceStageIndex);
		const int32 NextStageIndex = ActiveAcceptanceStageIndex + 1;
		if (FullStackStages.IsValidIndex(NextStageIndex))
		{
			BeginAcceptanceStage(NextStageIndex);
		}
	}
}

bool UProjectRuntimePerformanceSubsystem::ValidateAcceptanceIntegrity(FString& OutReason) const
{
	const UProjectPerformanceBudgetSettings* BudgetSettings = UProjectPerformanceBudgetSettings::Get();
	if (BudgetSettings
		&& (BudgetSettings->bEnableRuntimeBudgeting
			|| BudgetSettings->bApplyEnemyActorTickBudget
			|| BudgetSettings->bApplyEnemyAnimationBudget
			|| BudgetSettings->bApplyEnemyMovementTickBudget
			|| BudgetSettings->bCullExcessRuntimeEnemies
			|| BudgetSettings->bApplyWorldVfxBudget))
	{
		OutReason = TEXT("BLOCKED_BY_SAFETY:RuntimePerformanceBudgetEnabled");
		return false;
	}
	if (!bAcceptanceRouteBuilt || AcceptanceRoutePoints.Num() < 2)
	{
		OutReason = TEXT("AcceptanceRouteNotBuilt");
		return false;
	}
	if (CurrentStageId != ProjectRuntimePerformancePrivate::StageAcceptanceTraversal
		&& AcceptanceTraversalDistance < 600.0)
	{
		OutReason = FString::Printf(TEXT("AcceptanceTraversalTooShort:%.1f"), AcceptanceTraversalDistance);
		return false;
	}

	if (CurrentStageId != ProjectRuntimePerformancePrivate::StageAcceptanceTraversal)
	{
		int32 ValidEnemyCount = 0;
		for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
		{
			const APawn* EnemyPawn = EnemyPtr.Get();
			if (!EnemyPawn)
			{
				continue;
			}
			++ValidEnemyCount;
			if (!EnemyPawn->GetController())
			{
				OutReason = FString::Printf(TEXT("AcceptanceEnemyWithoutController:%s"), *GetNameSafe(EnemyPawn));
				return false;
			}
			if (EnemyPawn->IsHidden() || EnemyPawn->GetActorEnableCollision() == false)
			{
				OutReason = FString::Printf(TEXT("AcceptanceEnemySuppressed:%s"), *GetNameSafe(EnemyPawn));
				return false;
			}
			const ACharacter* EnemyCharacter = Cast<ACharacter>(EnemyPawn);
			const USkeletalMeshComponent* Mesh = EnemyCharacter ? EnemyCharacter->GetMesh() : nullptr;
			if (!Mesh || !Mesh->GetAnimInstance() || !Mesh->IsComponentTickEnabled())
			{
				OutReason = FString::Printf(TEXT("AcceptanceEnemyAnimationInactive:%s"), *GetNameSafe(EnemyPawn));
				return false;
			}
		}
		if (ValidEnemyCount != 8)
		{
			OutReason = FString::Printf(TEXT("AcceptanceEnemyCount:%d/8"), ValidEnemyCount);
			return false;
		}
	}

	if (CurrentStageId == ProjectRuntimePerformancePrivate::StageAcceptanceDirtyHud
		&& (!bAcceptanceUiOpened || !bAcceptanceDirtyWorkloadStarted))
	{
		OutReason = TEXT("AcceptanceHudOrDirtyPawnInactive");
		return false;
	}
	const bool bAtScenarioEnd = PhaseElapsedSeconds >= ResolveActiveDurationSeconds() - 0.25f;
	if (bAtScenarioEnd
		&& CurrentStageId == ProjectRuntimePerformancePrivate::StageAcceptanceDirtyHud
		&& !bFullStackDirtyWorkloadApplied)
	{
		OutReason = FString::Printf(TEXT("AcceptanceDirtyPawnIncomplete:%d/6"), FullStackDirtyPaintApplyCount);
		return false;
	}
	if (bAtScenarioEnd
		&& CurrentStageId == ProjectRuntimePerformancePrivate::StageAcceptanceDirtyHud
		&& FullStackEnemyToPlayerDamageCount + FullStackPlayerToEnemyDamageCount <= 0)
	{
		OutReason = TEXT("AcceptanceNoRealDamageEvents");
		return false;
	}

	OutReason.Reset();
	return true;
}

void UProjectRuntimePerformanceSubsystem::FailAcceptanceScenario(const FString& Reason)
{
	if (bAcceptanceScenarioFailed)
	{
		return;
	}
	bAcceptanceScenarioFailed = true;
	AcceptanceScenarioFailureReason = Reason;
	if (FullStackStages.IsValidIndex(ActiveAcceptanceStageIndex))
	{
		FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[ActiveAcceptanceStageIndex];
		Stage.bFailed = true;
		Stage.FailureReason = Reason;
	}
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), CurrentStageId.ToString());
	Fields->SetStringField(TEXT("reason"), Reason);
	Fields->SetBoolField(TEXT("strict"), ActiveRequest.bStrictScenarioFailures);
	AppendEvent(TEXT("acceptance_scenario_failed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::InitializeFullStackStages()
{
	FullStackStages.Reset();

	const double DurationSeconds = FMath::Max(1.0f, ResolveActiveDurationSeconds());
	double CursorSeconds = 0.0;
	for (const ProjectRuntimePerformancePrivate::FFullStackStageTemplate& Template : ProjectRuntimePerformancePrivate::GetFullStackStageTemplates())
	{
		FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages.AddDefaulted_GetRef();
		Stage.StageId = Template.StageId;
		Stage.StageName = Template.StageName;
		Stage.DurationFraction = Template.DurationFraction;
		Stage.StartSeconds = CursorSeconds;
		CursorSeconds += DurationSeconds * Template.DurationFraction;
		Stage.EndSeconds = CursorSeconds;
		Stage.bRequired = Template.bRequired;
	}

	if (!FullStackStages.IsEmpty())
	{
		FullStackStages.Last().EndSeconds = DurationSeconds;
	}
}

void UProjectRuntimePerformanceSubsystem::TickFullStackScenario(const float DeltaTime)
{
	if (FullStackStages.IsEmpty())
	{
		InitializeFullStackStages();
	}

	if (FullStackStages.IsEmpty())
	{
		FailFullStackStage(TEXT("FullStackStageScheduleEmpty"), true);
		return;
	}

	if (ActiveFullStackStageIndex == INDEX_NONE)
	{
		BeginFullStackStage(0);
	}

	if (!FullStackStages.IsValidIndex(ActiveFullStackStageIndex))
	{
		return;
	}

	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[ActiveFullStackStageIndex];
	if (bFullStackScenarioFailed && ShouldFailForScenarioIssue(ActiveRequest.bStrictScenarioFailures, Stage.bRequired))
	{
		FinishBenchmark(false, FullStackScenarioFailureReason.IsEmpty() ? TEXT("FullStackScenarioFailed") : FullStackScenarioFailureReason);
		return;
	}

	ActiveFullStackStageElapsedSeconds += DeltaTime;
	FullStackActionElapsedSeconds += DeltaTime;
	RunFullStackStageTick(DeltaTime);

	if (bFullStackScenarioFailed && ShouldFailForScenarioIssue(ActiveRequest.bStrictScenarioFailures, Stage.bRequired))
	{
		FinishBenchmark(false, FullStackScenarioFailureReason.IsEmpty() ? TEXT("FullStackScenarioFailed") : FullStackScenarioFailureReason);
		return;
	}

	if (PhaseElapsedSeconds >= Stage.EndSeconds)
	{
		if (!ValidateFullStackStageCompletion(Stage))
		{
			FailFullStackStage(
				FString::Printf(TEXT("StageValidationFailed:%s"), *Stage.StageId.ToString()),
				Stage.bRequired);
		}

		if (bFullStackScenarioFailed && ShouldFailForScenarioIssue(ActiveRequest.bStrictScenarioFailures, Stage.bRequired))
		{
			FinishBenchmark(false, FullStackScenarioFailureReason.IsEmpty() ? TEXT("FullStackScenarioFailed") : FullStackScenarioFailureReason);
			return;
		}

		const FName CompletedStageId = Stage.StageId;
		CompleteFullStackStage(ActiveFullStackStageIndex);
		CleanupCompletedFullStackStage(CompletedStageId);
		const int32 NextStageIndex = ActiveFullStackStageIndex + 1;
		if (FullStackStages.IsValidIndex(NextStageIndex))
		{
			BeginFullStackStage(NextStageIndex);
		}
	}
}

bool UProjectRuntimePerformanceSubsystem::BeginFullStackStage(const int32 StageIndex)
{
	if (!FullStackStages.IsValidIndex(StageIndex))
	{
		return false;
	}

	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[StageIndex];
	ActiveFullStackStageIndex = StageIndex;
	ActiveFullStackStageElapsedSeconds = 0.0;
	FullStackActionElapsedSeconds = 0.0;
	ActiveStageEnemy.Reset();
	StageStartEnemyToPlayerDamageCount = FullStackEnemyToPlayerDamageCount;
	StageStartPlayerToEnemyDamageCount = FullStackPlayerToEnemyDamageCount;
	CurrentStageId = Stage.StageId;
	CurrentStageName = Stage.StageName;
	CurrentScenarioFlags.Reset();
	Stage.bStarted = true;

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetNumberField(TEXT("start_seconds"), Stage.StartSeconds);
	Fields->SetNumberField(TEXT("end_seconds"), Stage.EndSeconds);
	AppendEvent(TEXT("stage_started"), Fields);

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		FailFullStackStage(TEXT("PlayerPawnMissing"), Stage.bRequired);
		return false;
	}

	CleanupBenchmarkVisualState(true);
	StabilizeBenchmarkCamera(FullStackStartTransform);

	const bool bDefeatStage = Stage.StageId == ProjectRuntimePerformancePrivate::StageDefeatStruggle;
	ApplyBenchmarkPlayerDamageGuard(PlayerPawn, !bDefeatStage);

	if (!bDefeatStage)
	{
		FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
		FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.05f);
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageExplorationUI)
	{
		return OpenFullStackHudAndChronicles();
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloMelee)
	{
		ActiveStageEnemy = SpawnBenchmarkEnemyByClassHint(FullStackStartTransform, TEXT("Melee"), StageIndex, true);
		return ActiveStageEnemy.IsValid();
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloRanged)
	{
		ActiveStageEnemy = SpawnBenchmarkEnemyByClassHint(FullStackStartTransform, TEXT("Ranged"), StageIndex, true);
		return ActiveStageEnemy.IsValid();
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloMage)
	{
		ActiveStageEnemy = SpawnBenchmarkEnemyByClassHint(FullStackStartTransform, TEXT("Mage"), StageIndex, true);
		return ActiveStageEnemy.IsValid();
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageStackedCombatDirtyPawn)
	{
		EnsureFullStackEnemyPopulation(FullStackStartTransform, ResolveFullStackEnemyCap());
		const bool bStatusesApplied = ApplyFullStackStatusesAndNeeds(PlayerPawn);
		FullStackDirtyPaintApplyCount = 0;
		bFullStackDirtyWorkloadApplied = false;
		return bStatusesApplied && ApplyFullStackDirtyPawnWorkload(PlayerPawn);
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageDefeatStruggle)
	{
		bFullStackDefeatRecovered = false;
		return StartFullStackDefeatStage(PlayerPawn);
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageIntimacy)
	{
		return StartFullStackIntimacyStage(PlayerPawn);
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageLockpicking)
	{
		return StartFullStackLockpickingStage(PlayerPawn);
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageYMenuActions)
	{
		EnsureFullStackEnemyPopulation(FullStackStartTransform, ResolveFullStackEnemyCap());
		return StartFullStackYMenuAction(PlayerPawn);
	}
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageDebugCommandSweep)
	{
		EnsureFullStackEnemyPopulation(FullStackStartTransform, ResolveFullStackEnemyCap());
		return ExecuteNextFullStackDebugCommand(PlayerPawn);
	}

	return true;
}

bool UProjectRuntimePerformanceSubsystem::ValidateFullStackStageCompletion(
	const FProjectRuntimePerformanceFullStackStage& Stage) const
{
	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloMelee
		|| Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloRanged
		|| Stage.StageId == ProjectRuntimePerformancePrivate::StageEnemySoloMage)
	{
		return ActiveStageEnemy.IsValid()
			&& (FullStackEnemyToPlayerDamageCount > StageStartEnemyToPlayerDamageCount
				|| FullStackPlayerToEnemyDamageCount > StageStartPlayerToEnemyDamageCount);
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageStackedCombatDirtyPawn)
	{
		return FullStackStatusApplyCount > 0
			&& FullStackDirtyPaintApplyCount >= ProjectRuntimePerformancePrivate::FullStackDirtyPawnWorkloadStepCount
			&& bFullStackDirtyWorkloadApplied;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageDefeatStruggle)
	{
		return bFullStackDefeatStarted && bFullStackDefeatRecovered;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageIntimacy)
	{
#if WITH_DEV_AUTOMATION_TESTS
		return bFullStackIntimacyStarted
			&& bFullStackIntimacyActiveSeen
			&& bFullStackIntimacyAutomationAttempted
			&& bFullStackIntimacyMenuTestCompleted
			&& bFullStackIntimacyInteractionCompleted
			&& bFullStackIntimacySessionPeakTriggered;
#else
		if (UWorld* World = GetWorld())
		{
			if (const UProjectIntimacySubsystem* IntimacySubsystem = World->GetSubsystem<UProjectIntimacySubsystem>())
			{
				return bFullStackIntimacyStarted && (bFullStackIntimacyActiveSeen || IntimacySubsystem->IsIntimacySessionActive());
			}
		}
		return bFullStackIntimacyStarted && bFullStackIntimacyActiveSeen;
#endif
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageLockpicking)
	{
		return bFullStackLockpickStarted && bFullStackLockpickConfirmed;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageYMenuActions)
	{
		return FullStackYMenuActionCount >= 1;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageDebugCommandSweep)
	{
		const double StageDurationSeconds = FMath::Max(0.0, Stage.EndSeconds - Stage.StartSeconds);
		const int32 RequiredCommandCount = StageDurationSeconds < 3.0 ? 1 : 3;
		return FullStackDebugCommandCount >= RequiredCommandCount;
	}

	if (Stage.StageId == ProjectRuntimePerformancePrivate::StageExplorationUI)
	{
		return bFullStackHudOpened;
	}

	return true;
}

void UProjectRuntimePerformanceSubsystem::CompleteFullStackStage(const int32 StageIndex)
{
	if (!FullStackStages.IsValidIndex(StageIndex))
	{
		return;
	}

	FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[StageIndex];
	Stage.bCompleted = !Stage.bFailed;

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
	Fields->SetStringField(TEXT("stage_name"), Stage.StageName);
	Fields->SetBoolField(TEXT("success"), !Stage.bFailed);
	Fields->SetStringField(TEXT("failure_reason"), Stage.FailureReason);
	AppendEvent(TEXT("stage_completed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::CleanupCompletedFullStackStage(const FName StageId)
{
	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

	const bool bNeedsInteractionCleanup =
		StageId == ProjectRuntimePerformancePrivate::StageDefeatStruggle
		|| StageId == ProjectRuntimePerformancePrivate::StageIntimacy
		|| StageId == ProjectRuntimePerformancePrivate::StageLockpicking
		|| StageId == ProjectRuntimePerformancePrivate::StageYMenuActions;

	if (StageId == ProjectRuntimePerformancePrivate::StageLockpicking)
	{
		if (UProjectLockpickingSubsystem* LockpickingSubsystem = World ? World->GetSubsystem<UProjectLockpickingSubsystem>() : nullptr)
		{
			LockpickingSubsystem->CloseLockpicking();
			LockpickingSubsystem->CloseLockpickPrompt();
		}
		ActiveLockpickComponent.Reset();
		CurrentScenarioFlags = TEXT("lockpick_completed");
	}

	if (bNeedsInteractionCleanup)
	{
		CleanupBenchmarkVisualState(true);
		if (StageId == ProjectRuntimePerformancePrivate::StageIntimacy)
		{
			SetFullStackSceneCombatSuppressed(nullptr, false);
			UpdateWorldSnapshot();
			WorldSnapshotElapsedSeconds = 0.0;
		}
		if (PlayerPawn)
		{
			StabilizeBenchmarkCamera(PlayerPawn->GetActorTransform());
			FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
			FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.05f);
		}
	}
}

void UProjectRuntimePerformanceSubsystem::FailFullStackStage(const FString& Reason, const bool bRequired)
{
	bFullStackScenarioFailed = true;
	FullStackScenarioFailureReason = Reason;
	if (FullStackStages.IsValidIndex(ActiveFullStackStageIndex))
	{
		FProjectRuntimePerformanceFullStackStage& Stage = FullStackStages[ActiveFullStackStageIndex];
		Stage.bFailed = true;
		Stage.FailureReason = Reason;
	}

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("stage_id"), CurrentStageId.ToString());
	Fields->SetStringField(TEXT("reason"), Reason);
	Fields->SetBoolField(TEXT("required"), bRequired);
	Fields->SetBoolField(TEXT("strict"), ActiveRequest.bStrictScenarioFailures);
	AppendEvent(TEXT("stage_failed"), Fields);
}

void UProjectRuntimePerformanceSubsystem::RunFullStackStageTick(const float DeltaTime)
{
	(void)DeltaTime;

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	if (ProjectRuntimePerformancePrivate::IsFullStackCombatTargetingStage(CurrentStageId))
	{
		if (APawn* EnemyPawn = ResolveStageEnemy())
		{
			if (FMath::Fmod(ActiveFullStackStageElapsedSeconds, 0.5) < DeltaTime)
			{
				PrepareBenchmarkEnemyAI(EnemyPawn, PlayerPawn, false);
			}
			const bool bDamageProbeStage =
				CurrentStageId == ProjectRuntimePerformancePrivate::StageEnemySoloMelee
				|| CurrentStageId == ProjectRuntimePerformancePrivate::StageEnemySoloRanged
				|| CurrentStageId == ProjectRuntimePerformancePrivate::StageEnemySoloMage;
			const bool bStageDamageObserved =
				FullStackEnemyToPlayerDamageCount > StageStartEnemyToPlayerDamageCount
				|| FullStackPlayerToEnemyDamageCount > StageStartPlayerToEnemyDamageCount;
			if (bDamageProbeStage && !bStageDamageObserved && FullStackActionElapsedSeconds >= 1.0)
			{
				FullStackActionElapsedSeconds = 0.0;
				MarkBenchmarkSyntheticWork(2.0f, TEXT("synthetic_damage_validation"));
				ApplyBenchmarkDamage(PlayerPawn, EnemyPawn, 0.25f, ProjectRuntimePerformancePrivate::BenchmarkDamageType);
				ApplyBenchmarkDamage(EnemyPawn, PlayerPawn, 0.25f, ProjectRuntimePerformancePrivate::BenchmarkDamageType);
			}
		}
	}

	if (CurrentStageId == ProjectRuntimePerformancePrivate::StageStackedCombatDirtyPawn
		&& !bFullStackDirtyWorkloadApplied
		&& FullStackActionElapsedSeconds >= 0.65)
	{
		FullStackActionElapsedSeconds = 0.0;
		ApplyFullStackDirtyPawnWorkload(PlayerPawn);
	}

	const bool bPreventAccidentalDefeat =
		CurrentStageId != ProjectRuntimePerformancePrivate::StageDefeatStruggle
		&& CurrentStageId != ProjectRuntimePerformancePrivate::StageIntimacy;
	if (bPreventAccidentalDefeat && FMath::Fmod(ActiveFullStackStageElapsedSeconds, 2.0) < DeltaTime)
	{
		ApplyBenchmarkPlayerDamageGuard(PlayerPawn, true);
		RemoveBenchmarkBlockingMenus();
		FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
		FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.05f);
	}

	if (CurrentStageId == ProjectRuntimePerformancePrivate::StageDefeatStruggle)
	{
#if WITH_DEV_AUTOMATION_TESTS
		if (FullStackActionElapsedSeconds >= 1.0)
		{
			FullStackActionElapsedSeconds = 0.0;
			if (UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>())
			{
				DefeatFlowComponent->AutomationCompleteActiveStruggleRound(true);
				FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
			}
		}

		if (!bFullStackDefeatRecovered && ActiveFullStackStageElapsedSeconds >= 3.0)
		{
			SetFullStackEnemyAiPaused(true);

			if (UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>())
			{
				DefeatFlowComponent->AutomationCompleteActiveStruggleRound(true);
				DefeatFlowComponent->AutomationRecoverFromKnockout();
				DefeatFlowComponent->AutomationRequestDefeatedSceneCancel();
			}

			RemoveBenchmarkBlockingMenus();
			CleanupBenchmarkVisualState(true);
			SetFullStackEnemyAiPaused(false);
			FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
			FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.05f);
			StabilizeBenchmarkCamera(PlayerPawn->GetActorTransform());
			bFullStackDefeatRecovered = true;
			CurrentScenarioFlags = TEXT("defeat_verified_recovered");
			AppendEvent(TEXT("defeat_recovered"));
		}
#endif
	}
	else if (CurrentStageId == ProjectRuntimePerformancePrivate::StageLockpicking)
	{
		if (UProjectLockpickableComponent* Lockpickable = ActiveLockpickComponent.Get())
		{
			if (!bFullStackLockpickConfirmed && Lockpickable->IsSessionActive() && ActiveFullStackStageElapsedSeconds >= 2.0)
			{
				EProjectLockpickInteractionGateResult GateResult = EProjectLockpickInteractionGateResult::RunOriginal;
				const FString ConfirmInteractionType = Lockpickable->BuildConfirmInteractionType();
				Lockpickable->TargetCenter = Lockpickable->GetCurrentPulseValue();
				Lockpickable->HandleACFLocalInteraction(PlayerPawn, ConfirmInteractionType);
				Lockpickable->HandleACFInteraction(PlayerPawn, ConfirmInteractionType, GateResult);
				bFullStackLockpickConfirmed = !Lockpickable->IsLocked();
				AppendEvent(bFullStackLockpickConfirmed ? TEXT("lockpick_succeeded") : TEXT("lockpick_confirmed"));
				if (bFullStackLockpickConfirmed)
				{
					if (UProjectLockpickingSubsystem* LockpickingSubsystem = World ? World->GetSubsystem<UProjectLockpickingSubsystem>() : nullptr)
					{
						LockpickingSubsystem->CloseLockpicking();
						LockpickingSubsystem->CloseLockpickPrompt();
					}
					ActiveLockpickComponent.Reset();
					CurrentScenarioFlags = TEXT("lockpick_completed");
					CleanupBenchmarkVisualState(true);
					StabilizeBenchmarkCamera(PlayerPawn->GetActorTransform());
				}
			}
		}
	}
	else if (CurrentStageId == ProjectRuntimePerformancePrivate::StageYMenuActions)
	{
		if (FullStackActionElapsedSeconds >= 2.5)
		{
			FullStackActionElapsedSeconds = 0.0;
			StartFullStackYMenuAction(PlayerPawn);
		}
	}
	else if (CurrentStageId == ProjectRuntimePerformancePrivate::StageDebugCommandSweep)
	{
		if (FullStackActionElapsedSeconds >= 0.75)
		{
			FullStackActionElapsedSeconds = 0.0;
			ExecuteNextFullStackDebugCommand(PlayerPawn);
		}
	}
	else if (CurrentStageId == ProjectRuntimePerformancePrivate::StageIntimacy)
	{
		if (UProjectIntimacySubsystem* IntimacySubsystem = World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr)
		{
			const bool bSessionActive = IntimacySubsystem->IsIntimacySessionActive();
			if (bSessionActive)
			{
				if (!bFullStackIntimacyActiveSeen)
				{
					bFullStackIntimacyActiveSeen = true;
					CurrentScenarioFlags = TEXT("intimacy_session_active;expected_intimacy_suppression=1");
					TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
					Fields->SetNumberField(TEXT("stage_elapsed_seconds"), ActiveFullStackStageElapsedSeconds);
					AppendEvent(TEXT("intimacy_session_active"), Fields);
				}

#if WITH_DEV_AUTOMATION_TESTS
				if (!bFullStackIntimacyAutomationAttempted)
				{
					bFullStackIntimacyAutomationAttempted = true;
					TSharedPtr<FJsonObject> StartFields = MakeShared<FJsonObject>();
					StartFields->SetNumberField(TEXT("stage_elapsed_seconds"), ActiveFullStackStageElapsedSeconds);
					AppendEvent(TEXT("intimacy_menu_automation_started"), StartFields);

					FString FailureReason;
					int32 StepCount = 0;
					bool bInteractionCompleted = false;
					bool bSessionPeakTriggered = false;
					const double AutomationStartSeconds = FPlatformTime::Seconds();
					const bool bAutomationSucceeded = IntimacySubsystem->AutomationRunMenuAndPleaseSmoke(
						FailureReason,
						StepCount,
						bInteractionCompleted,
						bSessionPeakTriggered);
					FullStackIntimacyAutomationStepCount += StepCount;
					bFullStackIntimacyMenuTestCompleted = bAutomationSucceeded;
					bFullStackIntimacyInteractionCompleted = bInteractionCompleted;
					bFullStackIntimacySessionPeakTriggered = bSessionPeakTriggered;

					RecordSystemMetric(
						ProjectRuntimePerformancePrivate::SystemIntimacy,
						(FPlatformTime::Seconds() - AutomationStartSeconds) * 1000.0,
						StepCount);

					TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
					Fields->SetBoolField(TEXT("success"), bAutomationSucceeded);
					Fields->SetBoolField(TEXT("session_interaction_completed"), bInteractionCompleted);
					Fields->SetBoolField(TEXT("session_peak_triggered"), bSessionPeakTriggered);
					Fields->SetNumberField(TEXT("step_count"), StepCount);
					Fields->SetStringField(TEXT("failure_reason"), FailureReason);
					AppendEvent(
						bAutomationSucceeded
							? TEXT("intimacy_menu_minigame_completed")
							: TEXT("intimacy_menu_minigame_failed"),
						Fields);

					if (!bAutomationSucceeded || !bInteractionCompleted || !bSessionPeakTriggered)
					{
						FullStackIntimacyFailureReason = FailureReason.IsEmpty()
							? TEXT("IntimacyMenuAutomationFailed")
							: FailureReason;
						TSharedPtr<FJsonObject> FailureFields = MakeShared<FJsonObject>();
						FailureFields->SetStringField(TEXT("reason"), FullStackIntimacyFailureReason);
						FailureFields->SetNumberField(TEXT("stage_elapsed_seconds"), ActiveFullStackStageElapsedSeconds);
						FailureFields->SetBoolField(TEXT("session_active"), IntimacySubsystem->IsIntimacySessionActive());
						AppendEvent(TEXT("intimacy_stage_failed_reason"), FailureFields);
					}

					CurrentScenarioFlags = FString::Printf(
						TEXT("intimacy_menu=%d;interaction=%d;session_peak=%d;steps=%d;expected_intimacy_suppression=1"),
						bAutomationSucceeded ? 1 : 0,
						bInteractionCompleted ? 1 : 0,
						bSessionPeakTriggered ? 1 : 0,
						StepCount);
				}
				else if (!bFullStackIntimacyAutomationAttempted && FMath::Fmod(ActiveFullStackStageElapsedSeconds, 3.0) < DeltaTime)
#else
				if (FMath::Fmod(ActiveFullStackStageElapsedSeconds, 3.0) < DeltaTime)
#endif
				{
					IntimacySubsystem->RequestConfirm();
				}
			}
			else
			{
				if (bFullStackIntimacyStarted && FMath::Fmod(ActiveFullStackStageElapsedSeconds, 3.0) < DeltaTime)
				{
					IntimacySubsystem->RequestConfirm();
				}

				if (bFullStackIntimacyStarted
					&& !bFullStackIntimacyActiveSeen
					&& ActiveFullStackStageElapsedSeconds >= 8.0
					&& !bFullStackScenarioFailed)
				{
					FullStackIntimacyFailureReason = TEXT("IntimacySessionNeverBecameActive");
					TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
					Fields->SetStringField(TEXT("reason"), FullStackIntimacyFailureReason);
					Fields->SetNumberField(TEXT("stage_elapsed_seconds"), ActiveFullStackStageElapsedSeconds);
					Fields->SetStringField(TEXT("scenario_flags"), CurrentScenarioFlags);
					AppendEvent(TEXT("intimacy_stage_failed_reason"), Fields);
					FailFullStackStage(FullStackIntimacyFailureReason, true);
				}
			}
		}
		else if (ActiveFullStackStageElapsedSeconds >= 1.0 && !bFullStackScenarioFailed)
		{
			FullStackIntimacyFailureReason = TEXT("IntimacySubsystemMissing");
			TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
			Fields->SetStringField(TEXT("reason"), FullStackIntimacyFailureReason);
			AppendEvent(TEXT("intimacy_stage_failed_reason"), Fields);
			FailFullStackStage(FullStackIntimacyFailureReason, true);
		}
	}
}

APawn* UProjectRuntimePerformanceSubsystem::ResolvePrimaryFullStackEnemy() const
{
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (APawn* EnemyPawn = EnemyPtr.Get())
		{
			return EnemyPawn;
		}
	}
	return nullptr;
}

APawn* UProjectRuntimePerformanceSubsystem::ResolveStageEnemy() const
{
	if (APawn* StageEnemy = ActiveStageEnemy.Get())
	{
		return StageEnemy;
	}
	return ResolvePrimaryFullStackEnemy();
}

void UProjectRuntimePerformanceSubsystem::TrackScenarioActor(AActor* Actor)
{
	if (Actor)
	{
		ScenarioActors.Add(Actor);
	}
}

void UProjectRuntimePerformanceSubsystem::BindDamageTelemetry(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (UProjectCombatAttributeComponent* CombatAttributeComponent = Actor->FindComponentByClass<UProjectCombatAttributeComponent>())
	{
		bool bAlreadyBound = false;
		for (const TWeakObjectPtr<UProjectCombatAttributeComponent>& ComponentPtr : BoundDamageTelemetryComponents)
		{
			bAlreadyBound |= ComponentPtr.Get() == CombatAttributeComponent;
		}
		if (!bAlreadyBound)
		{
			CombatAttributeComponent->OnDamageApplied.AddUniqueDynamic(this, &ThisClass::HandleBenchmarkDamageApplied);
			BoundDamageTelemetryComponents.Add(CombatAttributeComponent);
		}
	}

	if (UACFDamageHandlerComponent* DamageHandler = Actor->FindComponentByClass<UACFDamageHandlerComponent>())
	{
		bool bAlreadyBound = false;
		for (const TWeakObjectPtr<UACFDamageHandlerComponent>& ComponentPtr : BoundAcfDamageTelemetryComponents)
		{
			bAlreadyBound |= ComponentPtr.Get() == DamageHandler;
		}
		if (!bAlreadyBound)
		{
			DamageHandler->OnDamageReceived.AddUniqueDynamic(this, &ThisClass::HandleAcfDamageReceived);
			BoundAcfDamageTelemetryComponents.Add(DamageHandler);
		}
	}
}

bool UProjectRuntimePerformanceSubsystem::ApplyBenchmarkDamage(
	AActor* TargetActor,
	AActor* SourceActor,
	const float DamageAmount,
	const FName DamageType)
{
	const double StartSeconds = FPlatformTime::Seconds();
	UProjectCombatAttributeComponent* CombatAttributeComponent = TargetActor
		? TargetActor->FindComponentByClass<UProjectCombatAttributeComponent>()
		: nullptr;
	if (!CombatAttributeComponent)
	{
		return false;
	}

	const FName TargetAttribute = CombatAttributeComponent->HealthAttributeName.IsNone()
		? ProjectRuntimePerformancePrivate::HealthName
		: CombatAttributeComponent->HealthAttributeName;
	if (!CombatAttributeComponent->HasAttribute(TargetAttribute))
	{
		return false;
	}

	const float MaxHealth = FMath::Max(CombatAttributeComponent->GetAttributeMaxValue(TargetAttribute), 1.0f);
	if (CombatAttributeComponent->GetAttributeCurrentValue(TargetAttribute) < MaxHealth * 0.25f)
	{
		CombatAttributeComponent->SetAttributeRecoveryBlocked(TargetAttribute, false);
		CombatAttributeComponent->SetAttributeCurrentValue(TargetAttribute, MaxHealth);
	}

	FProjectCombatDamageSpec DamageSpec;
	DamageSpec.DamageType = DamageType.IsNone() ? ProjectRuntimePerformancePrivate::PhysicalDamageType : DamageType;
	DamageSpec.TargetAttribute = TargetAttribute;
	DamageSpec.BaseDamage = FMath::Max(0.1f, DamageAmount);
	DamageSpec.SourceActor = SourceActor;
	DamageSpec.DamageCauser = SourceActor;
	DamageSpec.bIgnoreArmor = true;

	const FProjectCombatDamageResult DamageResult = CombatAttributeComponent->ApplyDamage(DamageSpec);
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemCombatDamage,
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0,
		FullStackEnemyToPlayerDamageCount + FullStackPlayerToEnemyDamageCount);
	const bool bApplied = DamageResult.AppliedDamage > 0.0f;
	if (bApplied)
	{
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetStringField(TEXT("target"), GetNameSafe(TargetActor));
		Fields->SetStringField(TEXT("source"), GetNameSafe(SourceActor));
		Fields->SetNumberField(TEXT("applied_damage"), DamageResult.AppliedDamage);
		Fields->SetNumberField(TEXT("remaining"), DamageResult.RemainingValue);
		AppendEvent(TEXT("benchmark_damage_applied"), Fields);
	}
	return bApplied;
}

bool UProjectRuntimePerformanceSubsystem::OpenFullStackHudAndChronicles()
{
	UWorld* World = GetWorld();
	bool bOpened = false;
	if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World ? World->GetSubsystem<UProjectSurvivalNeedsSubsystem>() : nullptr)
	{
		const double NeedsStartSeconds = FPlatformTime::Seconds();
		bOpened |= NeedsSubsystem->SetNeedsHudVisible(true);
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemNeedsPressures,
			(FPlatformTime::Seconds() - NeedsStartSeconds) * 1000.0,
			CurrentWorldSnapshot.VisibleWidgetCount);
	}

	if (UProjectActivityFeedSubsystem* ActivityFeedSubsystem = World ? World->GetSubsystem<UProjectActivityFeedSubsystem>() : nullptr)
	{
		const double ChronicleStartSeconds = FPlatformTime::Seconds();
		if (!ActivityFeedSubsystem->IsFeedExpanded())
		{
			ActivityFeedSubsystem->RequestToggleExpanded();
		}
		ActivityFeedSubsystem->DebugAddSystemEntry(NSLOCTEXT("ProjectRuntimePerformance", "FullStackChronicleStart", "Full stack overload benchmark started."));
		ActivityFeedSubsystem->DebugAddSystemEntry(NSLOCTEXT("ProjectRuntimePerformance", "FullStackChronicleSystems", "Chronicles, needs, combat, status, and interaction systems are active."));
		bOpened = true;
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemChronicles,
			(FPlatformTime::Seconds() - ChronicleStartSeconds) * 1000.0,
			ActivityFeedSubsystem->GetStoredEntryCount());
	}

	bFullStackHudOpened = bOpened;
	CurrentScenarioFlags = bOpened ? TEXT("ui_open;chronicles_expanded") : TEXT("ui_missing");
	AppendEvent(bOpened ? TEXT("full_stack_ui_opened") : TEXT("full_stack_ui_missing"));
	return bOpened;
}

bool UProjectRuntimePerformanceSubsystem::ApplyFullStackStatusesAndNeeds(APawn* PlayerPawn)
{
	if (!PlayerPawn)
	{
		return false;
	}

	bool bApplied = false;
	const FName RequiredStatuses[] = {
		ProjectRuntimePerformancePrivate::BleedingName,
		ProjectRuntimePerformancePrivate::DizzyName,
		ProjectRuntimePerformancePrivate::DirtyName,
		ProjectRuntimePerformancePrivate::SweatyName,
		ProjectRuntimePerformancePrivate::FrenzyName,
		ProjectRuntimePerformancePrivate::ExtremePainName,
		ProjectRuntimePerformancePrivate::CursedName,
	};

	if (UProjectSurvivalStatusComponent* StatusComponent = PlayerPawn->FindComponentByClass<UProjectSurvivalStatusComponent>())
	{
		const double StatusStartSeconds = FPlatformTime::Seconds();
		TArray<FName> StatusNames;
		StatusNames.Reserve(UE_ARRAY_COUNT(RequiredStatuses));
		for (const FName StatusName : RequiredStatuses)
		{
			StatusNames.Add(StatusName);
		}

		StatusComponent->ApplyDebugStatuses(StatusNames, true);
		FullStackStatusApplyCount = 0;
		for (const FName StatusName : RequiredStatuses)
		{
			if (StatusComponent->IsStatusActive(StatusName))
			{
				++FullStackStatusApplyCount;
				bApplied = true;
			}
		}
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemSurvivalStatus,
			(FPlatformTime::Seconds() - StatusStartSeconds) * 1000.0,
			FullStackStatusApplyCount);
	}

	const double NeedsStartSeconds = FPlatformTime::Seconds();
	FProjectGameplayDebugCommandExecutor::SetNeedToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::HungerName, 0.15f);
	FProjectGameplayDebugCommandExecutor::SetNeedToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::ThirstName, 0.15f);
	FProjectGameplayDebugCommandExecutor::SetNeedToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::SleepName, 0.20f);
	FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::MadnessName, 0.85f);
	FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::CurseName, 0.90f);
	FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.75f);
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemNeedsPressures,
		(FPlatformTime::Seconds() - NeedsStartSeconds) * 1000.0,
		6);

	CurrentScenarioFlags = FString::Printf(TEXT("statuses_applied=%d;needs_pressure=1"), FullStackStatusApplyCount);
	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetNumberField(TEXT("status_apply_count"), FullStackStatusApplyCount);
	AppendEvent(TEXT("statuses_and_needs_applied"), Fields);
	return bApplied;
}

bool UProjectRuntimePerformanceSubsystem::ApplyFullStackDirtyPawnWorkload(APawn* PlayerPawn)
{
	UDirtyPawnComponent* DirtyPawnComponent = PlayerPawn
		? UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(PlayerPawn)
		: nullptr;
	if (!DirtyPawnComponent)
	{
		FailFullStackStage(TEXT("DirtyPawnComponentMissing"), true);
		return false;
	}

	const double DirtyPawnStartSeconds = FPlatformTime::Seconds();
	const int32 StepIndex = FullStackDirtyPaintApplyCount;
	FString StepName;
	switch (StepIndex)
	{
	case 0:
		DirtyPawnComponent->PreinitializeDirtyPawn();
		DirtyPawnComponent->WaterOverlapBand(PlayerPawn, 0.0f, 95.0f, false, false);
		StepName = TEXT("water");
		break;
	case 1:
		DirtyPawnComponent->MudBandEvent(0.0f, 80.0f, 1.0f);
		StepName = TEXT("mud");
		break;
	case 2:
		DirtyPawnComponent->BloodBandEvent(20.0f, 115.0f, 0.85f);
		StepName = TEXT("blood");
		break;
	case 3:
		DirtyPawnComponent->SmearBandEvent(15.0f, 110.0f, 0.7f);
		StepName = TEXT("smear");
		break;
	case 4:
		DirtyPawnComponent->DirtBandEvent(0.0f, 125.0f, 0.9f);
		StepName = TEXT("dirt");
		break;
	case 5:
		DirtyPawnComponent->SandBandEvent(0.0f, 85.0f, 0.8f);
		DirtyPawnComponent->SetSweatPoints(DirtyPawnComponent->SweatMaxPoints);
		DirtyPawnComponent->SetFadeWashVariablesBand(0.0f, 55.0f, true, true, true);
		StepName = TEXT("sand_sweat_wash");
		break;
	default:
		bFullStackDirtyWorkloadApplied = true;
		return true;
	}

	++FullStackDirtyPaintApplyCount;
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemDirtyPawn,
		(FPlatformTime::Seconds() - DirtyPawnStartSeconds) * 1000.0,
		FullStackDirtyPaintApplyCount);
	bFullStackDirtyWorkloadApplied =
		FullStackDirtyPaintApplyCount >= ProjectRuntimePerformancePrivate::FullStackDirtyPawnWorkloadStepCount;
	CurrentScenarioFlags = FString::Printf(
		TEXT("dirty_pawn_applied=%d/%d;dirty_step=%s"),
		FullStackDirtyPaintApplyCount,
		ProjectRuntimePerformancePrivate::FullStackDirtyPawnWorkloadStepCount,
		*StepName);

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetStringField(TEXT("step"), StepName);
	Fields->SetNumberField(TEXT("completed_steps"), FullStackDirtyPaintApplyCount);
	Fields->SetNumberField(TEXT("required_steps"), ProjectRuntimePerformancePrivate::FullStackDirtyPawnWorkloadStepCount);
	Fields->SetBoolField(TEXT("workload_completed"), bFullStackDirtyWorkloadApplied);
	AppendEvent(
		bFullStackDirtyWorkloadApplied ? TEXT("dirty_pawn_workload_applied") : TEXT("dirty_pawn_workload_step"),
		Fields);
	return true;
}

bool UProjectRuntimePerformanceSubsystem::StartFullStackDefeatStage(APawn* PlayerPawn)
{
	const double DefeatStartSeconds = FPlatformTime::Seconds();
	UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn
		? PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>()
		: nullptr;
	bFullStackDefeatStarted = DefeatFlowComponent
		&& DefeatFlowComponent->RequestKnockoutOrPendingCrawl(
			EProjectKnockoutReason::DebugForced,
			TEXT("RuntimePerformance.FullStack.DefeatStruggle"));
#if WITH_DEV_AUTOMATION_TESTS
	if (bFullStackDefeatStarted && DefeatFlowComponent)
	{
		DefeatFlowComponent->AutomationCompleteActiveStruggleRound(true);
	}
#endif
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemDefeatStruggle,
		(FPlatformTime::Seconds() - DefeatStartSeconds) * 1000.0,
		bFullStackDefeatStarted ? 1 : 0);
	CurrentScenarioFlags = bFullStackDefeatStarted ? TEXT("defeat_or_struggle_requested") : TEXT("defeat_request_failed");
	AppendEvent(bFullStackDefeatStarted ? TEXT("defeat_struggle_requested") : TEXT("defeat_struggle_failed"));
	return bFullStackDefeatStarted;
}

bool UProjectRuntimePerformanceSubsystem::StartFullStackIntimacyStage(APawn* PlayerPawn)
{
	const double IntimacyStartSeconds = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	bFullStackIntimacyStarted = false;
	bFullStackIntimacyActiveSeen = false;
	bFullStackIntimacyAutomationAttempted = false;
	bFullStackIntimacyMenuTestCompleted = false;
	bFullStackIntimacyInteractionCompleted = false;
	bFullStackIntimacySessionPeakTriggered = false;
	FullStackIntimacyAutomationStepCount = 0;
	FullStackIntimacyFailureReason.Reset();

	UProjectIntimacySubsystem* IntimacySubsystem =
		World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
	APawn* PartnerPawn = IntimacySubsystem
		? Cast<APawn>(IntimacySubsystem->GetHubSocialCompanionActor())
		: nullptr;
	if (!PlayerPawn || !PartnerPawn)
	{
		FullStackIntimacyFailureReason = TEXT("IntimacyPlayerOrSocialCompanionMissing");
		return false;
	}

	RemoveBenchmarkBlockingMenus();

	FString GroundReason;
	FVector PartnerLocation = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 170.0f;
	if (ResolveGroundedBenchmarkLocation(PartnerLocation, PartnerLocation, GroundReason))
	{
		PartnerPawn->SetActorLocation(PartnerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* PartnerController = PartnerPawn->GetController())
		{
			PartnerController->StopMovement();
		}
	}

	SetFullStackSceneCombatSuppressed(PartnerPawn, true);
	if (UProjectTargetingFixComponent* TargetingFixComponent = PlayerPawn->FindComponentByClass<UProjectTargetingFixComponent>())
	{
		TargetingFixComponent->DebugSetCurrentTargetActor(PartnerPawn);
	}
	if (UProjectEmoteComponent* EmoteComponent = PlayerPawn->FindComponentByClass<UProjectEmoteComponent>())
	{
#if WITH_EDITOR
		EmoteComponent->SetDebugBlueprintSceneTargetActor(PartnerPawn);
#endif
	}

	TSharedPtr<FJsonObject> RequestFields = MakeShared<FJsonObject>();
	RequestFields->SetStringField(TEXT("partner"), PartnerPawn->GetName());
	RequestFields->SetBoolField(TEXT("partner_component"), PartnerPawn->FindComponentByClass<UProjectIntimacyPartnerComponent>() != nullptr);
	RequestFields->SetBoolField(TEXT("target_set"), PlayerPawn->FindComponentByClass<UProjectTargetingFixComponent>() != nullptr);
	AppendEvent(TEXT("intimacy_request_started"), RequestFields);

	bool bQuickStartStarted = false;
	bool bFallbackStarted = false;
	if (IntimacySubsystem)
	{
		bQuickStartStarted = IntimacySubsystem->RequestQuickStartIntimacy();
		if (IntimacySubsystem->IsIntimacySessionActive())
		{
			bFullStackIntimacyActiveSeen = true;
		}
	}
	else
	{
		FullStackIntimacyFailureReason = TEXT("IntimacySubsystemMissing");
	}

	if (!bQuickStartStarted)
	{
		if (UProjectEmoteComponent* EmoteComponent = PlayerPawn->FindComponentByClass<UProjectEmoteComponent>())
		{
			bFallbackStarted = EmoteComponent->StartRuntimeInteractionById(TEXT("Actions.Together.0001Scene"));
		}
	}

	const bool bStarted = bQuickStartStarted || bFallbackStarted;
	bFullStackIntimacyStarted = bStarted;
	if (!bStarted)
	{
		if (FullStackIntimacyFailureReason.IsEmpty())
		{
			FullStackIntimacyFailureReason = TEXT("IntimacyQuickStartAndFallbackFailed");
		}
		SetFullStackSceneCombatSuppressed(nullptr, false);
	}
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemIntimacy,
		(FPlatformTime::Seconds() - IntimacyStartSeconds) * 1000.0,
		bFullStackIntimacyStarted ? 1 : 0);
	CurrentScenarioFlags = FString::Printf(
		TEXT("intimacy_requested=%d;quick_start=%d;fallback=%d;waiting_session=%d;expected_intimacy_suppression=1"),
		bStarted ? 1 : 0,
		bQuickStartStarted ? 1 : 0,
		bFallbackStarted ? 1 : 0,
		bStarted && !bFullStackIntimacyActiveSeen ? 1 : 0);

	TSharedPtr<FJsonObject> ResultFields = MakeShared<FJsonObject>();
	ResultFields->SetBoolField(TEXT("started"), bStarted);
	ResultFields->SetBoolField(TEXT("quick_start_started"), bQuickStartStarted);
	ResultFields->SetBoolField(TEXT("fallback_started"), bFallbackStarted);
	ResultFields->SetBoolField(TEXT("session_active_immediately"), bFullStackIntimacyActiveSeen);
	ResultFields->SetStringField(TEXT("failure_reason"), FullStackIntimacyFailureReason);
	AppendEvent(bStarted ? TEXT("intimacy_requested") : TEXT("intimacy_request_failed"), ResultFields);
	return bStarted;
}

bool UProjectRuntimePerformanceSubsystem::StartFullStackLockpickingStage(APawn* PlayerPawn)
{
	const double LockpickingStartSeconds = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	if (!World || !PlayerPawn)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerPawn;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;

	const FVector SpawnLocation = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 180.0f;
	AActor* LockActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLocation, PlayerPawn->GetActorRotation(), SpawnParameters);
	if (!LockActor)
	{
		FailFullStackStage(TEXT("LockpickActorSpawnFailed"), true);
		return false;
	}

	TrackScenarioActor(LockActor);
	UProjectLockpickableComponent* Lockpickable = NewObject<UProjectLockpickableComponent>(LockActor, TEXT("ProjectBenchmarkLockpickable"));
	if (!Lockpickable)
	{
		FailFullStackStage(TEXT("LockpickComponentCreateFailed"), true);
		return false;
	}

	LockActor->AddInstanceComponent(Lockpickable);
	Lockpickable->bStartsLocked = true;
	Lockpickable->bRequireLockpickItem = false;
	Lockpickable->bShowPromptBeforeLockpicking = false;
	Lockpickable->bExecuteOriginalOnSuccess = false;
	Lockpickable->Difficulty = 1;
	Lockpickable->TargetCenter = 0.5f;
	Lockpickable->RegisterComponent();
	Lockpickable->SetLocked(true);

	EProjectLockpickInteractionGateResult GateResult = EProjectLockpickInteractionGateResult::RunOriginal;
	Lockpickable->HandleACFLocalInteraction(PlayerPawn, Lockpickable->BuildBeginInteractionType());
	Lockpickable->HandleACFInteraction(PlayerPawn, Lockpickable->BuildBeginInteractionType(), GateResult);

	ActiveLockpickComponent = Lockpickable;
	bFullStackLockpickStarted = Lockpickable->IsSessionActive();
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemLockpicking,
		(FPlatformTime::Seconds() - LockpickingStartSeconds) * 1000.0,
		bFullStackLockpickStarted ? 1 : 0);
	CurrentScenarioFlags = bFullStackLockpickStarted ? TEXT("lockpick_active") : TEXT("lockpick_start_failed");
	AppendEvent(bFullStackLockpickStarted ? TEXT("lockpick_started") : TEXT("lockpick_start_failed"));
	return bFullStackLockpickStarted;
}

bool UProjectRuntimePerformanceSubsystem::StartFullStackYMenuAction(APawn* PlayerPawn)
{
	const double YMenuStartSeconds = FPlatformTime::Seconds();
	UProjectEmoteComponent* EmoteComponent = PlayerPawn ? PlayerPawn->FindComponentByClass<UProjectEmoteComponent>() : nullptr;
	if (!EmoteComponent)
	{
		return false;
	}

	if (EmoteComponent->IsEmoteActive())
	{
		EmoteComponent->StopEmote();
		return true;
	}

	EmoteComponent->RefreshMenuCatalog();
	TArray<FProjectEmoteInteractionDefinition> Interactions;
	EmoteComponent->GetMenuInteractions(EProjectEmoteMenuCategory::Actions, Interactions);
	if (Interactions.IsEmpty())
	{
		EmoteComponent->GetMenuInteractions(EProjectEmoteMenuCategory::Objects, Interactions);
	}

	if (Interactions.IsEmpty())
	{
		FailFullStackStage(TEXT("YMenuNoActionsAvailable"), true);
		return false;
	}

	const int32 ActionIndex = FullStackYMenuActionCount % Interactions.Num();
	const FName InteractionId = Interactions[ActionIndex].InteractionId;
	const bool bStarted = EmoteComponent->StartRuntimeInteractionById(InteractionId);
	if (bStarted)
	{
		++FullStackYMenuActionCount;
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetStringField(TEXT("interaction_id"), InteractionId.ToString());
		Fields->SetNumberField(TEXT("count"), FullStackYMenuActionCount);
		AppendEvent(TEXT("y_menu_action_started"), Fields);
	}

	CurrentScenarioFlags = FString::Printf(TEXT("y_menu_actions=%d"), FullStackYMenuActionCount);
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemYMenuEmote,
		(FPlatformTime::Seconds() - YMenuStartSeconds) * 1000.0,
		FullStackYMenuActionCount);
	return bStarted;
}

bool UProjectRuntimePerformanceSubsystem::ExecuteNextFullStackDebugCommand(APawn* PlayerPawn)
{
	const double DebugStartSeconds = FPlatformTime::Seconds();
	if (!PlayerPawn)
	{
		return false;
	}

	bool bExecuted = false;
	const int32 CommandIndex = FullStackDebugCommandCount % 8;
	switch (CommandIndex)
	{
	case 0:
		bExecuted = FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
		break;
	case 1:
		bExecuted = FProjectGameplayDebugCommandExecutor::RestoreNeedsAndSensations(PlayerPawn);
		break;
	case 2:
		bExecuted = FProjectGameplayDebugCommandExecutor::SetNeedToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::HungerName, 0.25f);
		break;
	case 3:
		bExecuted = FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.85f);
		break;
	case 4:
		bExecuted = FProjectGameplayDebugCommandExecutor::ForceApplyStatus(PlayerPawn, ProjectRuntimePerformancePrivate::BleedingName);
		break;
	case 5:
		bExecuted = FProjectGameplayDebugCommandExecutor::ForceApplyStatus(PlayerPawn, ProjectRuntimePerformancePrivate::DizzyName);
		break;
	case 6:
		bExecuted = FProjectGameplayDebugCommandExecutor::RaiseDoctrineAttributeToLevel(PlayerPawn, EProjectDoctrineAttribute::Cunning, 5);
		break;
	default:
		if (UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>())
		{
			bExecuted = DefeatFlowComponent->RequestKnockoutOrPendingCrawl(
				EProjectKnockoutReason::DebugForced,
				TEXT("RuntimePerformance.FullStack.DebugSweep.DownedMode"));
#if WITH_DEV_AUTOMATION_TESTS
			if (bExecuted)
			{
				DefeatFlowComponent->AutomationCompleteActiveStruggleRound(true);
				DefeatFlowComponent->AutomationRecoverFromKnockout();
			}
#else
			DefeatFlowComponent->TryRecoverFromKnockoutIfNoNearbyEnemy(TEXT("RuntimePerformance.FullStack.DebugSweep.Recover"));
#endif
#if WITH_DEV_AUTOMATION_TESTS
			DefeatFlowComponent->AutomationRequestDefeatedSceneCancel();
#endif
			FProjectGameplayDebugCommandExecutor::RestoreAcfHealth(PlayerPawn);
			FProjectGameplayDebugCommandExecutor::SetSensationToPercent(PlayerPawn, ProjectRuntimePerformancePrivate::PainName, 0.05f);
		}
		break;
	}

	if (bExecuted)
	{
		++FullStackDebugCommandCount;
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetNumberField(TEXT("command_index"), CommandIndex);
		Fields->SetNumberField(TEXT("debug_command_count"), FullStackDebugCommandCount);
		AppendEvent(TEXT("debug_command_executed"), Fields);
	}

	CurrentScenarioFlags = FString::Printf(TEXT("debug_commands=%d"), FullStackDebugCommandCount);
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemDebugSweep,
		(FPlatformTime::Seconds() - DebugStartSeconds) * 1000.0,
		FullStackDebugCommandCount);
	return bExecuted;
}

void UProjectRuntimePerformanceSubsystem::RecordSystemMetric(
	const FName SystemId,
	const double CpuMs,
	const int32 ActiveCount)
{
	if (SystemId.IsNone() || CpuMs < 0.0)
	{
		return;
	}

	FProjectRuntimePerformanceSystemAccumulator& Accumulator = SystemMetricAccumulators.FindOrAdd(SystemId);
	Accumulator.SystemId = SystemId;
	Accumulator.TotalCpuMs += CpuMs;
	Accumulator.CallCount += 1;
	Accumulator.ActiveCountPeak = FMath::Max(Accumulator.ActiveCountPeak, ActiveCount);
	Accumulator.CpuSamplesMs.Add(CpuMs);
}

TArray<TSharedPtr<FJsonValue>> UProjectRuntimePerformanceSubsystem::BuildSystemMetricValues(
	const double MeasuredDurationSeconds) const
{
	TArray<FName> SystemIds;
	SystemMetricAccumulators.GetKeys(SystemIds);
	SystemIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(SystemIds.Num());
	for (const FName& SystemId : SystemIds)
	{
		const FProjectRuntimePerformanceSystemAccumulator* Accumulator = SystemMetricAccumulators.Find(SystemId);
		if (!Accumulator)
		{
			continue;
		}

		TArray<double> CpuSamples = Accumulator->CpuSamplesMs;
		CpuSamples.Sort();
		const double AverageCpuMs = Accumulator->CallCount > 0
			? Accumulator->TotalCpuMs / static_cast<double>(Accumulator->CallCount)
			: 0.0;
		const double CallsPerSecond = MeasuredDurationSeconds > SMALL_NUMBER
			? static_cast<double>(Accumulator->CallCount) / MeasuredDurationSeconds
			: 0.0;

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("system_id"), SystemId.ToString());
		Object->SetNumberField(TEXT("cpu_total_ms"), Accumulator->TotalCpuMs);
		Object->SetNumberField(TEXT("cpu_average_ms"), AverageCpuMs);
		Object->SetNumberField(TEXT("cpu_p95_ms"), CpuSamples.IsEmpty() ? 0.0 : PercentileSorted(CpuSamples, 0.95));
		Object->SetNumberField(TEXT("cpu_p99_ms"), CpuSamples.IsEmpty() ? 0.0 : PercentileSorted(CpuSamples, 0.99));
		Object->SetNumberField(TEXT("call_count"), Accumulator->CallCount);
		Object->SetNumberField(TEXT("calls_per_second"), CallsPerSecond);
		Object->SetNumberField(TEXT("active_count_peak"), Accumulator->ActiveCountPeak);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}

	return Values;
}

void UProjectRuntimePerformanceSubsystem::WriteSystemMetricsCsv(
	const FString& RunDirectory,
	const double MeasuredDurationSeconds) const
{
	TArray<FName> SystemIds;
	SystemMetricAccumulators.GetKeys(SystemIds);
	SystemIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<FString> CsvLines;
	CsvLines.Reserve(SystemIds.Num() + 1);
	CsvLines.Add(TEXT("system_id,cpu_total_ms,cpu_average_ms,cpu_p95_ms,cpu_p99_ms,call_count,calls_per_second,active_count_peak"));
	for (const FName& SystemId : SystemIds)
	{
		const FProjectRuntimePerformanceSystemAccumulator* Accumulator = SystemMetricAccumulators.Find(SystemId);
		if (!Accumulator)
		{
			continue;
		}

		TArray<double> CpuSamples = Accumulator->CpuSamplesMs;
		CpuSamples.Sort();
		const double AverageCpuMs = Accumulator->CallCount > 0
			? Accumulator->TotalCpuMs / static_cast<double>(Accumulator->CallCount)
			: 0.0;
		const double CallsPerSecond = MeasuredDurationSeconds > SMALL_NUMBER
			? static_cast<double>(Accumulator->CallCount) / MeasuredDurationSeconds
			: 0.0;

		CsvLines.Add(FString::Printf(
			TEXT("%s,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%d"),
			*SystemId.ToString().Replace(TEXT(","), TEXT(";")),
			Accumulator->TotalCpuMs,
			AverageCpuMs,
			CpuSamples.IsEmpty() ? 0.0 : PercentileSorted(CpuSamples, 0.95),
			CpuSamples.IsEmpty() ? 0.0 : PercentileSorted(CpuSamples, 0.99),
			Accumulator->CallCount,
			CallsPerSecond,
			Accumulator->ActiveCountPeak));
	}

	FFileHelper::SaveStringArrayToFile(CsvLines, *FPaths::Combine(RunDirectory, TEXT("system_metrics.csv")));
}

void UProjectRuntimePerformanceSubsystem::HandleBenchmarkDamageApplied(
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

	if (AppliedDamage <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (SourceActor == PlayerPawn)
	{
		++FullStackPlayerToEnemyDamageCount;
		return;
	}

	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (EnemyPtr.Get() == SourceActor)
		{
			++FullStackEnemyToPlayerDamageCount;
			return;
		}
	}
}

void UProjectRuntimePerformanceSubsystem::HandleAcfDamageReceived(const FACFDamageEvent& DamageEvent)
{
	if (DamageEvent.FinalDamage <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	const APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (DamageEvent.DamageReceiver == PlayerPawn)
	{
		for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
		{
			if (EnemyPtr.Get() == DamageEvent.DamageDealer)
			{
				++FullStackEnemyToPlayerDamageCount;
				return;
			}
		}
	}
	if (DamageEvent.DamageDealer == PlayerPawn)
	{
		for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
		{
			if (EnemyPtr.Get() == DamageEvent.DamageReceiver)
			{
				++FullStackPlayerToEnemyDamageCount;
				return;
			}
		}
	}
}

void UProjectRuntimePerformanceSubsystem::UpdateWorldSnapshot()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FProjectRuntimePerformanceWorldSnapshot Snapshot;
	Snapshot.UsedPhysicalMemoryBytes = ReadUsedPhysicalMemoryBytes();
	Snapshot.TexturePoolSizeMb = ReadTexturePoolSizeMb();
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			++Snapshot.BenchmarkSpawnedEnemyCount;
		}
	}
	Snapshot.WorldRuntimeEnemyCount = CountActiveBenchmarkEnemies();
	Snapshot.RuntimeEnemyCount = Snapshot.WorldRuntimeEnemyCount;

	const UProjectPerformanceBudgetSettings* BudgetSettings = UProjectPerformanceBudgetSettings::Get();
	const bool bRuntimeBudgetingEnabled = BudgetSettings && BudgetSettings->bEnableRuntimeBudgeting;
	if (bRuntimeBudgetingEnabled)
	{
		if (UProjectPerformanceBudgetSubsystem* BudgetSubsystem = World->GetSubsystem<UProjectPerformanceBudgetSubsystem>())
		{
			const double BudgetStartSeconds = FPlatformTime::Seconds();
			const FProjectPerformanceBudgetSnapshot BudgetSnapshot = BudgetSubsystem->GetLastSnapshot();
			Snapshot.BudgetObservedEnemyCount = BudgetSnapshot.RuntimeEnemyCount;
			Snapshot.FullRateEnemyCount = BudgetSnapshot.FullRateEnemyCount;
			Snapshot.ActiveCombatEnemyCount =
				BudgetSnapshot.FullRateEnemyCount
				+ BudgetSnapshot.MidRateEnemyCount
				+ BudgetSnapshot.FarRateEnemyCount;
			Snapshot.BudgetRuntimeEnemyCount = BudgetSnapshot.RuntimeEnemyCount;
			Snapshot.BudgetFullRateEnemyCount = BudgetSnapshot.FullRateEnemyCount;
			Snapshot.BudgetMidRateEnemyCount = BudgetSnapshot.MidRateEnemyCount;
			Snapshot.BudgetFarRateEnemyCount = BudgetSnapshot.FarRateEnemyCount;
			Snapshot.BudgetSuspendedEnemyCount = BudgetSnapshot.SuspendedExcessEnemyCount;
			Snapshot.BudgetNiagaraComponentCount = BudgetSnapshot.BudgetedNiagaraComponentCount;
			RecordSystemMetric(
				ProjectRuntimePerformancePrivate::SystemRuntimePerformanceBudget,
				(FPlatformTime::Seconds() - BudgetStartSeconds) * 1000.0,
				Snapshot.ActiveCombatEnemyCount);
		}
	}
	else
	{
		Snapshot.FullRateEnemyCount = Snapshot.WorldRuntimeEnemyCount;
		Snapshot.ActiveCombatEnemyCount = Snapshot.WorldRuntimeEnemyCount;
	}
	if (Snapshot.ActiveCombatEnemyCount == 0)
	{
		Snapshot.ActiveCombatEnemyCount = Snapshot.WorldRuntimeEnemyCount;
	}

	const FString CurrentMapName = GetCurrentShortMapName();
	if (!LastObservedMapName.IsEmpty() && !CurrentMapName.Equals(LastObservedMapName, ESearchCase::IgnoreCase))
	{
		++MapTravelCount;
		TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetStringField(TEXT("from"), LastObservedMapName);
		Fields->SetStringField(TEXT("to"), CurrentMapName);
		AppendEvent(TEXT("map_changed"), Fields);
	}
	LastObservedMapName = CurrentMapName;
	Snapshot.MapTravelCount = MapTravelCount;

	const bool bAsyncLoading = IsAsyncLoading && IsAsyncLoading();
	if (bAsyncLoading)
	{
		++AsyncLoadingSampleCount;
	}
	Snapshot.AsyncLoadingSampleCount = AsyncLoadingSampleCount;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		++Snapshot.ActorCount;
		if (Actor->IsA<APawn>())
		{
			++Snapshot.PawnCount;
		}

		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (Pawn != PlayerPawn && IsConfiguredRuntimeEnemyPawn(Pawn))
			{
				if (!Pawn->GetController())
				{
					++Snapshot.RuntimeEnemyWithoutControllerCount;
				}
				if (Pawn->IsHidden())
				{
					++Snapshot.HiddenRuntimeEnemyCount;
				}
				if (!Pawn->GetActorEnableCollision())
				{
					++Snapshot.CollisionDisabledRuntimeEnemyCount;
				}
				if (const USkeletalMeshComponent* MeshComponent = Pawn->FindComponentByClass<USkeletalMeshComponent>())
				{
					if (!MeshComponent->IsComponentTickEnabled())
					{
						++Snapshot.EnemyMeshTickDisabledCount;
					}
					if (MeshComponent->GetForcedLOD() > 0)
					{
						++Snapshot.EnemyMeshForcedLodCount;
					}
					if (MeshComponent->bEnableUpdateRateOptimizations)
					{
						++Snapshot.EnemyMeshUpdateRateOptimizationCount;
					}
				}
			}
		}

		TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents(Actor);
		Actor->GetComponents(SkeletalMeshComponents);
		Snapshot.SkeletalMeshComponentCount += SkeletalMeshComponents.Num();

		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
		Actor->GetComponents(NiagaraComponents);
		Snapshot.NiagaraComponentCount += NiagaraComponents.Num();
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		const UUserWidget* Widget = *It;
		if (IsValid(Widget) && Widget->GetWorld() == World && Widget->IsInViewport())
		{
			++Snapshot.VisibleWidgetCount;
		}
	}

	if (const UProjectSurvivalStatusComponent* StatusComponent = PlayerPawn ? PlayerPawn->FindComponentByClass<UProjectSurvivalStatusComponent>() : nullptr)
	{
		const double StatusStartSeconds = FPlatformTime::Seconds();
		Snapshot.ActiveStatusCount = StatusComponent->BuildActiveStatusSnapshots().Num();
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemSurvivalStatus,
			(FPlatformTime::Seconds() - StatusStartSeconds) * 1000.0,
			Snapshot.ActiveStatusCount);
	}

	if (const UProjectActivityFeedSubsystem* ActivityFeedSubsystem = World->GetSubsystem<UProjectActivityFeedSubsystem>())
	{
		const double ChronicleStartSeconds = FPlatformTime::Seconds();
		Snapshot.ChronicleEntryCount = ActivityFeedSubsystem->GetStoredEntryCount();
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemChronicles,
			(FPlatformTime::Seconds() - ChronicleStartSeconds) * 1000.0,
			Snapshot.ChronicleEntryCount);
	}

	if (const UDirtyPawnComponent* DirtyPawnComponent = PlayerPawn ? UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(PlayerPawn) : nullptr)
	{
		const double DirtyPawnStartSeconds = FPlatformTime::Seconds();
		const EDirtyPawnPaintState DirtyStates[] = {
			EDirtyPawnPaintState::Wet,
			EDirtyPawnPaintState::Mud,
			EDirtyPawnPaintState::Sand,
			EDirtyPawnPaintState::Blood,
			EDirtyPawnPaintState::Smear,
			EDirtyPawnPaintState::Dirt,
		};
		for (const EDirtyPawnPaintState State : DirtyStates)
		{
			if (DirtyPawnComponent->HasActivePaintState(State, 0.05f)
				|| DirtyPawnComponent->GetMaxVisiblePaintAlphaForState(State) > 0.05f)
			{
				++Snapshot.DirtyPaintActiveCount;
			}
		}
		if (DirtyPawnComponent->IsSweaty())
		{
			++Snapshot.DirtyPaintActiveCount;
		}
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemDirtyPawn,
			(FPlatformTime::Seconds() - DirtyPawnStartSeconds) * 1000.0,
			Snapshot.DirtyPaintActiveCount);
	}

	if (const UProjectIntimacySubsystem* IntimacySubsystem = World->GetSubsystem<UProjectIntimacySubsystem>())
	{
		const double IntimacyStartSeconds = FPlatformTime::Seconds();
		Snapshot.bIntimacyActive = IntimacySubsystem->IsIntimacySessionActive();
		RecordSystemMetric(
			ProjectRuntimePerformancePrivate::SystemIntimacy,
			(FPlatformTime::Seconds() - IntimacyStartSeconds) * 1000.0,
			Snapshot.bIntimacyActive ? 1 : 0);
	}

	const double LockpickingStartSeconds = FPlatformTime::Seconds();
	Snapshot.bLockpickActive = ActiveLockpickComponent.IsValid() && ActiveLockpickComponent->IsSessionActive();
	RecordSystemMetric(
		ProjectRuntimePerformancePrivate::SystemLockpicking,
		(FPlatformTime::Seconds() - LockpickingStartSeconds) * 1000.0,
		Snapshot.bLockpickActive ? 1 : 0);
	Snapshot.DebugCommandCount = FullStackDebugCommandCount;

	CurrentWorldSnapshot = Snapshot;
	PeakWorldSnapshot.ActorCount = FMath::Max(PeakWorldSnapshot.ActorCount, Snapshot.ActorCount);
	PeakWorldSnapshot.PawnCount = FMath::Max(PeakWorldSnapshot.PawnCount, Snapshot.PawnCount);
	PeakWorldSnapshot.BenchmarkSpawnedEnemyCount = FMath::Max(PeakWorldSnapshot.BenchmarkSpawnedEnemyCount, Snapshot.BenchmarkSpawnedEnemyCount);
	PeakWorldSnapshot.WorldRuntimeEnemyCount = FMath::Max(PeakWorldSnapshot.WorldRuntimeEnemyCount, Snapshot.WorldRuntimeEnemyCount);
	PeakWorldSnapshot.BudgetObservedEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetObservedEnemyCount, Snapshot.BudgetObservedEnemyCount);
	PeakWorldSnapshot.ActiveCombatEnemyCount = FMath::Max(PeakWorldSnapshot.ActiveCombatEnemyCount, Snapshot.ActiveCombatEnemyCount);
	PeakWorldSnapshot.FullRateEnemyCount = FMath::Max(PeakWorldSnapshot.FullRateEnemyCount, Snapshot.FullRateEnemyCount);
	PeakWorldSnapshot.RuntimeEnemyCount = FMath::Max(PeakWorldSnapshot.RuntimeEnemyCount, Snapshot.RuntimeEnemyCount);
	PeakWorldSnapshot.BudgetRuntimeEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetRuntimeEnemyCount, Snapshot.BudgetRuntimeEnemyCount);
	PeakWorldSnapshot.BudgetFullRateEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetFullRateEnemyCount, Snapshot.BudgetFullRateEnemyCount);
	PeakWorldSnapshot.BudgetMidRateEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetMidRateEnemyCount, Snapshot.BudgetMidRateEnemyCount);
	PeakWorldSnapshot.BudgetFarRateEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetFarRateEnemyCount, Snapshot.BudgetFarRateEnemyCount);
	PeakWorldSnapshot.BudgetSuspendedEnemyCount = FMath::Max(PeakWorldSnapshot.BudgetSuspendedEnemyCount, Snapshot.BudgetSuspendedEnemyCount);
	PeakWorldSnapshot.BudgetNiagaraComponentCount = FMath::Max(PeakWorldSnapshot.BudgetNiagaraComponentCount, Snapshot.BudgetNiagaraComponentCount);
	PeakWorldSnapshot.RuntimeEnemyWithoutControllerCount = FMath::Max(PeakWorldSnapshot.RuntimeEnemyWithoutControllerCount, Snapshot.RuntimeEnemyWithoutControllerCount);
	PeakWorldSnapshot.HiddenRuntimeEnemyCount = FMath::Max(PeakWorldSnapshot.HiddenRuntimeEnemyCount, Snapshot.HiddenRuntimeEnemyCount);
	PeakWorldSnapshot.CollisionDisabledRuntimeEnemyCount = FMath::Max(PeakWorldSnapshot.CollisionDisabledRuntimeEnemyCount, Snapshot.CollisionDisabledRuntimeEnemyCount);
	PeakWorldSnapshot.EnemyMeshTickDisabledCount = FMath::Max(PeakWorldSnapshot.EnemyMeshTickDisabledCount, Snapshot.EnemyMeshTickDisabledCount);
	PeakWorldSnapshot.EnemyMeshForcedLodCount = FMath::Max(PeakWorldSnapshot.EnemyMeshForcedLodCount, Snapshot.EnemyMeshForcedLodCount);
	PeakWorldSnapshot.EnemyMeshUpdateRateOptimizationCount = FMath::Max(PeakWorldSnapshot.EnemyMeshUpdateRateOptimizationCount, Snapshot.EnemyMeshUpdateRateOptimizationCount);
	PeakWorldSnapshot.SkeletalMeshComponentCount = FMath::Max(PeakWorldSnapshot.SkeletalMeshComponentCount, Snapshot.SkeletalMeshComponentCount);
	PeakWorldSnapshot.NiagaraComponentCount = FMath::Max(PeakWorldSnapshot.NiagaraComponentCount, Snapshot.NiagaraComponentCount);
	PeakWorldSnapshot.VisibleWidgetCount = FMath::Max(PeakWorldSnapshot.VisibleWidgetCount, Snapshot.VisibleWidgetCount);
	PeakWorldSnapshot.ActiveStatusCount = FMath::Max(PeakWorldSnapshot.ActiveStatusCount, Snapshot.ActiveStatusCount);
	PeakWorldSnapshot.ChronicleEntryCount = FMath::Max(PeakWorldSnapshot.ChronicleEntryCount, Snapshot.ChronicleEntryCount);
	PeakWorldSnapshot.DirtyPaintActiveCount = FMath::Max(PeakWorldSnapshot.DirtyPaintActiveCount, Snapshot.DirtyPaintActiveCount);
	PeakWorldSnapshot.bIntimacyActive |= Snapshot.bIntimacyActive;
	PeakWorldSnapshot.bLockpickActive |= Snapshot.bLockpickActive;
	PeakWorldSnapshot.DebugCommandCount = FMath::Max(PeakWorldSnapshot.DebugCommandCount, Snapshot.DebugCommandCount);
	PeakWorldSnapshot.AsyncLoadingSampleCount = Snapshot.AsyncLoadingSampleCount;
	PeakWorldSnapshot.MapTravelCount = Snapshot.MapTravelCount;
	PeakWorldSnapshot.TexturePoolSizeMb = FMath::Max(PeakWorldSnapshot.TexturePoolSizeMb, Snapshot.TexturePoolSizeMb);
	PeakWorldSnapshot.UsedPhysicalMemoryBytes = FMath::Max(PeakWorldSnapshot.UsedPhysicalMemoryBytes, Snapshot.UsedPhysicalMemoryBytes);
}

void UProjectRuntimePerformanceSubsystem::CopyWorldSnapshotToSample(FProjectRuntimePerformanceFrameSample& Sample) const
{
	Sample.UsedMemoryMb = static_cast<double>(CurrentWorldSnapshot.UsedPhysicalMemoryBytes) / (1024.0 * 1024.0);
	Sample.ActorCount = CurrentWorldSnapshot.ActorCount;
	Sample.PawnCount = CurrentWorldSnapshot.PawnCount;
	Sample.BenchmarkSpawnedEnemyCount = CurrentWorldSnapshot.BenchmarkSpawnedEnemyCount;
	Sample.WorldRuntimeEnemyCount = CurrentWorldSnapshot.WorldRuntimeEnemyCount;
	Sample.BudgetObservedEnemyCount = CurrentWorldSnapshot.BudgetObservedEnemyCount;
	Sample.ActiveCombatEnemyCount = CurrentWorldSnapshot.ActiveCombatEnemyCount;
	Sample.FullRateEnemyCount = CurrentWorldSnapshot.FullRateEnemyCount;
	Sample.RuntimeEnemyCount = CurrentWorldSnapshot.RuntimeEnemyCount;
	Sample.BudgetRuntimeEnemyCount = CurrentWorldSnapshot.BudgetRuntimeEnemyCount;
	Sample.BudgetFullRateEnemyCount = CurrentWorldSnapshot.BudgetFullRateEnemyCount;
	Sample.BudgetMidRateEnemyCount = CurrentWorldSnapshot.BudgetMidRateEnemyCount;
	Sample.BudgetFarRateEnemyCount = CurrentWorldSnapshot.BudgetFarRateEnemyCount;
	Sample.BudgetSuspendedEnemyCount = CurrentWorldSnapshot.BudgetSuspendedEnemyCount;
	Sample.BudgetNiagaraComponentCount = CurrentWorldSnapshot.BudgetNiagaraComponentCount;
	Sample.RuntimeEnemyWithoutControllerCount = CurrentWorldSnapshot.RuntimeEnemyWithoutControllerCount;
	Sample.HiddenRuntimeEnemyCount = CurrentWorldSnapshot.HiddenRuntimeEnemyCount;
	Sample.CollisionDisabledRuntimeEnemyCount = CurrentWorldSnapshot.CollisionDisabledRuntimeEnemyCount;
	Sample.EnemyMeshTickDisabledCount = CurrentWorldSnapshot.EnemyMeshTickDisabledCount;
	Sample.EnemyMeshForcedLodCount = CurrentWorldSnapshot.EnemyMeshForcedLodCount;
	Sample.EnemyMeshUpdateRateOptimizationCount = CurrentWorldSnapshot.EnemyMeshUpdateRateOptimizationCount;
	Sample.SkeletalMeshComponentCount = CurrentWorldSnapshot.SkeletalMeshComponentCount;
	Sample.NiagaraComponentCount = CurrentWorldSnapshot.NiagaraComponentCount;
	Sample.VisibleWidgetCount = CurrentWorldSnapshot.VisibleWidgetCount;
	Sample.ActiveStatusCount = CurrentWorldSnapshot.ActiveStatusCount;
	Sample.ChronicleEntryCount = CurrentWorldSnapshot.ChronicleEntryCount;
	Sample.DirtyPaintActiveCount = CurrentWorldSnapshot.DirtyPaintActiveCount;
	Sample.bIntimacyActive = CurrentWorldSnapshot.bIntimacyActive;
	Sample.bLockpickActive = CurrentWorldSnapshot.bLockpickActive;
	Sample.DebugCommandCount = CurrentWorldSnapshot.DebugCommandCount;
	Sample.bAsyncLoading = IsAsyncLoading && IsAsyncLoading();
}

double UProjectRuntimePerformanceSubsystem::ReadGameThreadMs() const
{
	return FPlatformTime::ToMilliseconds(GGameThreadTime);
}

double UProjectRuntimePerformanceSubsystem::ReadRenderThreadMs() const
{
	return FPlatformTime::ToMilliseconds(GRenderThreadTime);
}

double UProjectRuntimePerformanceSubsystem::ReadGpuMs() const
{
	return FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
}

int32 UProjectRuntimePerformanceSubsystem::ReadTexturePoolSizeMb() const
{
	static const IConsoleVariable* TexturePoolSizeCvar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.PoolSize"));
	return TexturePoolSizeCvar ? TexturePoolSizeCvar->GetInt() : 0;
}

void UProjectRuntimePerformanceSubsystem::AppendEvent(const FString& EventName, const TSharedPtr<FJsonObject>& Fields)
{
	if (ActiveRunId.IsEmpty())
	{
		return;
	}

	const FString RunDirectory = ResolveRunDirectory();
	IFileManager::Get().MakeDirectory(*RunDirectory, true);

	const TSharedRef<FJsonObject> EventObject = MakeShared<FJsonObject>();
	EventObject->SetStringField(TEXT("event"), EventName);
	EventObject->SetStringField(TEXT("run_id"), ActiveRunId);
	EventObject->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
	EventObject->SetNumberField(TEXT("total_elapsed_seconds"), TotalElapsedSeconds);
	if (Fields.IsValid())
	{
		EventObject->SetObjectField(TEXT("fields"), Fields);
	}

	FString Line = ProjectRuntimePerformancePrivate::JsonToCondensedString(EventObject);
	Line.AppendChar(TEXT('\n'));
	FFileHelper::SaveStringToFile(Line, *FPaths::Combine(RunDirectory, TEXT("events.jsonl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void UProjectRuntimePerformanceSubsystem::WriteArtifacts(const bool bSuccess, const FString& Reason)
{
	const FString RunDirectory = ResolveRunDirectory();
	const FString OutputRoot = ResolveOutputRoot();
	IFileManager::Get().MakeDirectory(*RunDirectory, true);
	IFileManager::Get().MakeDirectory(*OutputRoot, true);
	UpdateWorldSnapshot();

	TArray<double> MeasuredFrameMs;
	TArray<double> MeasuredGameThreadMs;
	TArray<double> MeasuredRenderThreadMs;
	TArray<double> MeasuredGpuMs;
	TArray<double> SyntheticBenchmarkFrameMs;
	TArray<double> NaturalBootstrapFrameMs;
	TArray<double> NaturalBootstrapGameThreadMs;
	TArray<double> NaturalBootstrapRenderThreadMs;
	TArray<double> NaturalBootstrapGpuMs;
	int32 ExcludedFrameSampleCount = 0;
	int32 SyntheticBenchmarkFrameSampleCount = 0;
	int32 ExpectedIntimacySuppressionFrameSampleCount = 0;
	for (const FProjectRuntimePerformanceFrameSample& Sample : Samples)
	{
		if (Sample.bExcludedFromMetrics)
		{
			++ExcludedFrameSampleCount;
		}
		if (Sample.bSyntheticBenchmarkWork && !Sample.bWarmup)
		{
			++SyntheticBenchmarkFrameSampleCount;
			SyntheticBenchmarkFrameMs.Add(Sample.FrameMs);
		}
		if (Sample.bExpectedIntimacySuppression && !Sample.bWarmup)
		{
			++ExpectedIntimacySuppressionFrameSampleCount;
		}
		if (!Sample.bWarmup && !Sample.bExcludedFromMetrics)
		{
			MeasuredFrameMs.Add(Sample.FrameMs);
			if (Sample.GameThreadMs > 0.0)
			{
				MeasuredGameThreadMs.Add(Sample.GameThreadMs);
			}
			if (Sample.RenderThreadMs > 0.0)
			{
				MeasuredRenderThreadMs.Add(Sample.RenderThreadMs);
			}
			if (Sample.GpuMs > 0.0)
			{
				MeasuredGpuMs.Add(Sample.GpuMs);
			}
		}
	}
	for (const FProjectRuntimePerformanceFrameSample& Sample : NaturalBootstrapSamples)
	{
		NaturalBootstrapFrameMs.Add(Sample.FrameMs);
		if (Sample.GameThreadMs > 0.0)
		{
			NaturalBootstrapGameThreadMs.Add(Sample.GameThreadMs);
		}
		if (Sample.RenderThreadMs > 0.0)
		{
			NaturalBootstrapRenderThreadMs.Add(Sample.RenderThreadMs);
		}
		if (Sample.GpuMs > 0.0)
		{
			NaturalBootstrapGpuMs.Add(Sample.GpuMs);
		}
	}

	double MeasuredDurationSeconds = 0.0;
	for (const double FrameMs : MeasuredFrameMs)
	{
		MeasuredDurationSeconds += FrameMs / 1000.0;
	}

	LastMetrics = BuildMetrics(MeasuredFrameMs);
	const FProjectRuntimePerformanceMetrics SyntheticBenchmarkMetrics = BuildMetrics(SyntheticBenchmarkFrameMs);
	const FProjectRuntimePerformanceMetrics NaturalBootstrapMetrics = BuildMetrics(NaturalBootstrapFrameMs);

	auto AverageValue = [](const TArray<double>& Values) -> double
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		return Sum / static_cast<double>(Values.Num());
	};

	auto PercentileValue = [](TArray<double> Values, const double Percentile) -> double
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		Values.Sort();
		return UProjectRuntimePerformanceSubsystem::PercentileSorted(Values, Percentile);
	};

	TArray<FString> CsvLines;
	CsvLines.Reserve(Samples.Num() + 1);
	CsvLines.Add(TEXT("frame_number,time_seconds,delta_seconds,frame_ms,game_thread_ms,render_thread_ms,gpu_ms,fps,used_memory_mb,stage_id,stage_name,map_name,scenario_flags,player_x,player_y,player_z,player_speed_cm_s,traversal_distance,route_point_index,actor_count,pawn_count,benchmark_spawned_enemy_count,world_runtime_enemy_count,budget_observed_enemy_count,active_combat_enemy_count,full_rate_enemy_count,runtime_enemy_count,budget_runtime_enemy_count,budget_full_rate_enemy_count,budget_mid_rate_enemy_count,budget_far_rate_enemy_count,budget_suspended_enemy_count,budget_niagara_component_count,runtime_enemy_without_controller_count,hidden_runtime_enemy_count,collision_disabled_runtime_enemy_count,enemy_mesh_tick_disabled_count,enemy_mesh_forced_lod_count,enemy_mesh_update_rate_optimization_count,skeletal_mesh_component_count,niagara_component_count,visible_widget_count,active_status_count,chronicle_entry_count,dirty_paint_active_count,intimacy_active,lockpick_active,debug_command_count,async_loading,excluded_from_metrics,synthetic_benchmark_work,synthetic_reason,expected_intimacy_suppression,warmup"));
	for (const FProjectRuntimePerformanceFrameSample& Sample : Samples)
	{
		CsvLines.Add(FString::Printf(
			TEXT("%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d,%s,%s,%s,%s,%s,%s"),
			static_cast<unsigned long long>(Sample.FrameNumber),
			Sample.TimeSeconds,
			Sample.DeltaSeconds,
			Sample.FrameMs,
			Sample.GameThreadMs,
			Sample.RenderThreadMs,
			Sample.GpuMs,
			Sample.Fps,
			Sample.UsedMemoryMb,
			*Sample.StageId.ToString(),
			*Sample.StageName.Replace(TEXT(","), TEXT(";")),
			*Sample.MapName.Replace(TEXT(","), TEXT(";")),
			*Sample.ScenarioFlags.Replace(TEXT(","), TEXT(";")),
			Sample.PlayerLocation.X,
			Sample.PlayerLocation.Y,
			Sample.PlayerLocation.Z,
			Sample.PlayerSpeedCmPerSecond,
			Sample.TraversalDistance,
			Sample.RoutePointIndex,
			Sample.ActorCount,
			Sample.PawnCount,
			Sample.BenchmarkSpawnedEnemyCount,
			Sample.WorldRuntimeEnemyCount,
			Sample.BudgetObservedEnemyCount,
			Sample.ActiveCombatEnemyCount,
			Sample.FullRateEnemyCount,
			Sample.RuntimeEnemyCount,
			Sample.BudgetRuntimeEnemyCount,
			Sample.BudgetFullRateEnemyCount,
			Sample.BudgetMidRateEnemyCount,
			Sample.BudgetFarRateEnemyCount,
			Sample.BudgetSuspendedEnemyCount,
			Sample.BudgetNiagaraComponentCount,
			Sample.RuntimeEnemyWithoutControllerCount,
			Sample.HiddenRuntimeEnemyCount,
			Sample.CollisionDisabledRuntimeEnemyCount,
			Sample.EnemyMeshTickDisabledCount,
			Sample.EnemyMeshForcedLodCount,
			Sample.EnemyMeshUpdateRateOptimizationCount,
			Sample.SkeletalMeshComponentCount,
			Sample.NiagaraComponentCount,
			Sample.VisibleWidgetCount,
			Sample.ActiveStatusCount,
			Sample.ChronicleEntryCount,
			Sample.DirtyPaintActiveCount,
			Sample.bIntimacyActive ? TEXT("true") : TEXT("false"),
			Sample.bLockpickActive ? TEXT("true") : TEXT("false"),
			Sample.DebugCommandCount,
			Sample.bAsyncLoading ? TEXT("true") : TEXT("false"),
			Sample.bExcludedFromMetrics ? TEXT("true") : TEXT("false"),
			Sample.bSyntheticBenchmarkWork ? TEXT("true") : TEXT("false"),
			*Sample.SyntheticBenchmarkReason.Replace(TEXT(","), TEXT(";")),
			Sample.bExpectedIntimacySuppression ? TEXT("true") : TEXT("false"),
			Sample.bWarmup ? TEXT("true") : TEXT("false")));
	}
	FFileHelper::SaveStringArrayToFile(CsvLines, *FPaths::Combine(RunDirectory, TEXT("samples.csv")));

	TArray<FString> BootstrapCsvLines;
	BootstrapCsvLines.Reserve(NaturalBootstrapSamples.Num() + 1);
	BootstrapCsvLines.Add(TEXT("frame_number,total_elapsed_seconds,frame_ms,game_thread_ms,render_thread_ms,gpu_ms,fps,used_memory_mb,map_name,player_x,player_y,player_z,player_speed_cm_s,actor_count,pawn_count,skeletal_mesh_component_count,niagara_component_count,async_loading"));
	for (const FProjectRuntimePerformanceFrameSample& Sample : NaturalBootstrapSamples)
	{
		BootstrapCsvLines.Add(FString::Printf(
			TEXT("%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%s,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%s"),
			static_cast<unsigned long long>(Sample.FrameNumber),
			Sample.TimeSeconds,
			Sample.FrameMs,
			Sample.GameThreadMs,
			Sample.RenderThreadMs,
			Sample.GpuMs,
			Sample.Fps,
			Sample.UsedMemoryMb,
			*Sample.MapName.Replace(TEXT(","), TEXT(";")),
			Sample.PlayerLocation.X,
			Sample.PlayerLocation.Y,
			Sample.PlayerLocation.Z,
			Sample.PlayerSpeedCmPerSecond,
			Sample.ActorCount,
			Sample.PawnCount,
			Sample.SkeletalMeshComponentCount,
			Sample.NiagaraComponentCount,
			Sample.bAsyncLoading ? TEXT("true") : TEXT("false")));
	}
	if (!NaturalBootstrapSamples.IsEmpty())
	{
		FFileHelper::SaveStringArrayToFile(
			BootstrapCsvLines,
			*FPaths::Combine(RunDirectory, TEXT("bootstrap_samples.csv")));
	}
	WriteSystemMetricsCsv(RunDirectory, MeasuredDurationSeconds);

	int32 TrackedActiveEnemyCount = 0;
	for (const TWeakObjectPtr<APawn>& EnemyPtr : SpawnedEnemies)
	{
		if (EnemyPtr.IsValid())
		{
			++TrackedActiveEnemyCount;
		}
	}
	const int32 WorldActiveEnemyCount = CountActiveBenchmarkEnemies();
	const int32 ActiveEnemyCount = FMath::Max(TrackedActiveEnemyCount, WorldActiveEnemyCount);

	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	const UProjectPerformanceBudgetSettings* BudgetSettings = UProjectPerformanceBudgetSettings::Get();
	const bool bRuntimeBudgetingEnabled = BudgetSettings && BudgetSettings->bEnableRuntimeBudgeting;
	const bool bDirectAiTargeting = ShouldUseDirectBenchmarkEnemyTargeting();
	const double PeakMemoryMb = static_cast<double>(FMath::Max(PeakPhysicalMemoryBytes, ReadPeakPhysicalMemoryBytes())) / (1024.0 * 1024.0);
	const float DurationSeconds = ResolveActiveDurationSeconds();
	const FString EffectiveCVarSnapshot = BuildEffectiveCVarSnapshot();
	const FString EffectiveCVarHash = BuildEffectiveCVarHash();
	const FProjectRuntimePerformanceGateResult GateResult = EvaluateAcceptanceMetrics(
		LastMetrics,
		Settings ? Settings->MinimumAverageFps : 60.0,
		Settings ? Settings->MinimumMedianFps : 60.0,
		Settings ? Settings->MinimumOnePercentLowFps : 55.0,
		Settings ? Settings->MaximumP99FrameMs : 18.2,
		Settings ? Settings->MaximumHitchesOver100Ms : 0);
	const bool bAcceptanceProfile = IsAcceptance58Profile();
	const bool bAcceptanceGatePassed = bAcceptanceProfile
		&& bSuccess
		&& !bAcceptanceScenarioFailed
		&& GateResult.bPassed;

	auto BuildHitchValues = [](
		const TArray<FProjectRuntimePerformanceFrameSample>& SourceSamples,
		const bool bMeasuredOnly,
		int32& OutGameThreadDominant,
		int32& OutRenderThreadDominant,
		int32& OutGpuDominant,
		int32& OutMixedOrUnknown,
		int32& OutAsyncLoading) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<const FProjectRuntimePerformanceFrameSample*> HitchSamples;
		for (const FProjectRuntimePerformanceFrameSample& Sample : SourceSamples)
		{
			if (Sample.FrameMs <= 50.0
				|| (bMeasuredOnly && (Sample.bWarmup || Sample.bExcludedFromMetrics)))
			{
				continue;
			}
			HitchSamples.Add(&Sample);
		}
		HitchSamples.Sort([](
			const FProjectRuntimePerformanceFrameSample& Left,
			const FProjectRuntimePerformanceFrameSample& Right)
		{
			return Left.FrameMs > Right.FrameMs;
		});

		TArray<TSharedPtr<FJsonValue>> Values;
		const int32 ValueCount = FMath::Min(20, HitchSamples.Num());
		Values.Reserve(ValueCount);
		for (int32 Index = 0; Index < ValueCount; ++Index)
		{
			const FProjectRuntimePerformanceFrameSample& Sample = *HitchSamples[Index];
			const double LargestThreadMs = FMath::Max3(Sample.GameThreadMs, Sample.RenderThreadMs, Sample.GpuMs);
			FString DominantThread = TEXT("MixedOrUnknown");
			if (LargestThreadMs > 0.0 && LargestThreadMs >= Sample.FrameMs * 0.5)
			{
				if (LargestThreadMs == Sample.GameThreadMs)
				{
					DominantThread = TEXT("GameThread");
					++OutGameThreadDominant;
				}
				else if (LargestThreadMs == Sample.RenderThreadMs)
				{
					DominantThread = TEXT("RenderThread");
					++OutRenderThreadDominant;
				}
				else
				{
					DominantThread = TEXT("GPU");
					++OutGpuDominant;
				}
			}
			else
			{
				++OutMixedOrUnknown;
			}
			if (Sample.bAsyncLoading)
			{
				++OutAsyncLoading;
			}

			TSharedRef<FJsonObject> HitchObject = MakeShared<FJsonObject>();
			HitchObject->SetNumberField(TEXT("frame_number"), static_cast<double>(Sample.FrameNumber));
			HitchObject->SetNumberField(TEXT("time_seconds"), Sample.TimeSeconds);
			HitchObject->SetNumberField(TEXT("frame_ms"), Sample.FrameMs);
			HitchObject->SetNumberField(TEXT("game_thread_ms"), Sample.GameThreadMs);
			HitchObject->SetNumberField(TEXT("render_thread_ms"), Sample.RenderThreadMs);
			HitchObject->SetNumberField(TEXT("gpu_ms"), Sample.GpuMs);
			HitchObject->SetStringField(TEXT("observed_dominant_thread"), DominantThread);
			HitchObject->SetBoolField(TEXT("async_loading"), Sample.bAsyncLoading);
			HitchObject->SetStringField(TEXT("map"), Sample.MapName);
			HitchObject->SetStringField(TEXT("stage_id"), Sample.StageId.ToString());
			HitchObject->SetNumberField(TEXT("actor_count"), Sample.ActorCount);
			HitchObject->SetNumberField(TEXT("pawn_count"), Sample.PawnCount);
			HitchObject->SetNumberField(TEXT("world_runtime_enemy_count"), Sample.WorldRuntimeEnemyCount);
			HitchObject->SetNumberField(TEXT("niagara_component_count"), Sample.NiagaraComponentCount);
			HitchObject->SetNumberField(TEXT("player_x"), Sample.PlayerLocation.X);
			HitchObject->SetNumberField(TEXT("player_y"), Sample.PlayerLocation.Y);
			HitchObject->SetNumberField(TEXT("player_z"), Sample.PlayerLocation.Z);
			HitchObject->SetNumberField(TEXT("player_speed_cm_s"), Sample.PlayerSpeedCmPerSecond);
			HitchObject->SetNumberField(TEXT("traversal_distance"), Sample.TraversalDistance);
			HitchObject->SetNumberField(TEXT("route_point_index"), Sample.RoutePointIndex);
			Values.Add(MakeShared<FJsonValueObject>(HitchObject));
		}
		return Values;
	};

	int32 MeasuredGameThreadDominantHitches = 0;
	int32 MeasuredRenderThreadDominantHitches = 0;
	int32 MeasuredGpuDominantHitches = 0;
	int32 MeasuredMixedOrUnknownHitches = 0;
	int32 MeasuredAsyncLoadingHitches = 0;
	const TArray<TSharedPtr<FJsonValue>> MeasuredHitchValues = BuildHitchValues(
		Samples,
		true,
		MeasuredGameThreadDominantHitches,
		MeasuredRenderThreadDominantHitches,
		MeasuredGpuDominantHitches,
		MeasuredMixedOrUnknownHitches,
		MeasuredAsyncLoadingHitches);
	int32 BootstrapGameThreadDominantHitches = 0;
	int32 BootstrapRenderThreadDominantHitches = 0;
	int32 BootstrapGpuDominantHitches = 0;
	int32 BootstrapMixedOrUnknownHitches = 0;
	int32 BootstrapAsyncLoadingHitches = 0;
	const TArray<TSharedPtr<FJsonValue>> BootstrapHitchValues = BuildHitchValues(
		NaturalBootstrapSamples,
		false,
		BootstrapGameThreadDominantHitches,
		BootstrapRenderThreadDominantHitches,
		BootstrapGpuDominantHitches,
		BootstrapMixedOrUnknownHitches,
		BootstrapAsyncLoadingHitches);

	const TSharedRef<FJsonObject> MetricsObject = MakeShared<FJsonObject>();
	MetricsObject->SetNumberField(TEXT("average_fps"), LastMetrics.AverageFps);
	MetricsObject->SetNumberField(TEXT("median_fps"), LastMetrics.MedianFps);
	MetricsObject->SetNumberField(TEXT("min_fps"), LastMetrics.MinFps);
	MetricsObject->SetNumberField(TEXT("one_percent_low_fps"), LastMetrics.OnePercentLowFps);
	MetricsObject->SetNumberField(TEXT("zero_point_one_percent_low_fps"), LastMetrics.ZeroPointOnePercentLowFps);
	MetricsObject->SetNumberField(TEXT("p95_frame_ms"), LastMetrics.P95FrameMs);
	MetricsObject->SetNumberField(TEXT("p99_frame_ms"), LastMetrics.P99FrameMs);
	MetricsObject->SetNumberField(TEXT("hitches_over_50_ms"), LastMetrics.HitchesOver50Ms);
	MetricsObject->SetNumberField(TEXT("hitches_over_100_ms"), LastMetrics.HitchesOver100Ms);
	MetricsObject->SetNumberField(TEXT("hitches_over_500_ms"), LastMetrics.HitchesOver500Ms);
	MetricsObject->SetNumberField(TEXT("real_hitches_over_50_ms"), LastMetrics.HitchesOver50Ms);
	MetricsObject->SetNumberField(TEXT("real_hitches_over_100_ms"), LastMetrics.HitchesOver100Ms);
	MetricsObject->SetNumberField(TEXT("real_hitches_over_500_ms"), LastMetrics.HitchesOver500Ms);
	MetricsObject->SetNumberField(TEXT("benchmark_synthetic_frame_count"), SyntheticBenchmarkFrameSampleCount);
	MetricsObject->SetNumberField(TEXT("benchmark_synthetic_hitches_over_50_ms"), SyntheticBenchmarkMetrics.HitchesOver50Ms);
	MetricsObject->SetNumberField(TEXT("benchmark_synthetic_hitches_over_100_ms"), SyntheticBenchmarkMetrics.HitchesOver100Ms);
	MetricsObject->SetNumberField(TEXT("benchmark_synthetic_hitches_over_500_ms"), SyntheticBenchmarkMetrics.HitchesOver500Ms);
	MetricsObject->SetNumberField(TEXT("expected_intimacy_suppression_frame_count"), ExpectedIntimacySuppressionFrameSampleCount);
	MetricsObject->SetNumberField(TEXT("frame_sample_count"), LastMetrics.FrameSampleCount);
	MetricsObject->SetNumberField(TEXT("peak_memory_mb"), PeakMemoryMb);
	MetricsObject->SetNumberField(TEXT("average_game_thread_ms"), AverageValue(MeasuredGameThreadMs));
	MetricsObject->SetNumberField(TEXT("p95_game_thread_ms"), PercentileValue(MeasuredGameThreadMs, 0.95));
	MetricsObject->SetNumberField(TEXT("average_render_thread_ms"), AverageValue(MeasuredRenderThreadMs));
	MetricsObject->SetNumberField(TEXT("p95_render_thread_ms"), PercentileValue(MeasuredRenderThreadMs, 0.95));
	MetricsObject->SetNumberField(TEXT("average_gpu_ms"), AverageValue(MeasuredGpuMs));
	MetricsObject->SetNumberField(TEXT("p95_gpu_ms"), PercentileValue(MeasuredGpuMs, 0.95));
	MetricsObject->SetNumberField(TEXT("top_hitches_game_thread_dominant"), MeasuredGameThreadDominantHitches);
	MetricsObject->SetNumberField(TEXT("top_hitches_render_thread_dominant"), MeasuredRenderThreadDominantHitches);
	MetricsObject->SetNumberField(TEXT("top_hitches_gpu_dominant"), MeasuredGpuDominantHitches);
	MetricsObject->SetNumberField(TEXT("top_hitches_mixed_or_unknown"), MeasuredMixedOrUnknownHitches);
	MetricsObject->SetNumberField(TEXT("top_hitches_during_async_loading"), MeasuredAsyncLoadingHitches);
	MetricsObject->SetNumberField(TEXT("texture_pool_size_mb"), CurrentWorldSnapshot.TexturePoolSizeMb);
	MetricsObject->SetNumberField(TEXT("actor_count_peak"), PeakWorldSnapshot.ActorCount);
	MetricsObject->SetNumberField(TEXT("pawn_count_peak"), PeakWorldSnapshot.PawnCount);
	MetricsObject->SetNumberField(TEXT("benchmark_spawned_enemy_count_peak"), PeakWorldSnapshot.BenchmarkSpawnedEnemyCount);
	MetricsObject->SetNumberField(TEXT("world_runtime_enemy_count_peak"), PeakWorldSnapshot.WorldRuntimeEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_observed_enemy_count_peak"), PeakWorldSnapshot.BudgetObservedEnemyCount);
	MetricsObject->SetNumberField(TEXT("active_combat_enemy_count_peak"), PeakWorldSnapshot.ActiveCombatEnemyCount);
	MetricsObject->SetNumberField(TEXT("full_rate_enemy_count_peak"), PeakWorldSnapshot.FullRateEnemyCount);
	MetricsObject->SetNumberField(TEXT("runtime_enemy_count_peak"), PeakWorldSnapshot.RuntimeEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_runtime_enemy_count_peak"), PeakWorldSnapshot.BudgetRuntimeEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_full_rate_enemy_count_peak"), PeakWorldSnapshot.BudgetFullRateEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_mid_rate_enemy_count_peak"), PeakWorldSnapshot.BudgetMidRateEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_far_rate_enemy_count_peak"), PeakWorldSnapshot.BudgetFarRateEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_suspended_enemy_count_peak"), PeakWorldSnapshot.BudgetSuspendedEnemyCount);
	MetricsObject->SetNumberField(TEXT("budget_niagara_component_count_peak"), PeakWorldSnapshot.BudgetNiagaraComponentCount);
	MetricsObject->SetNumberField(TEXT("runtime_enemy_without_controller_count_peak"), PeakWorldSnapshot.RuntimeEnemyWithoutControllerCount);
	MetricsObject->SetNumberField(TEXT("hidden_runtime_enemy_count_peak"), PeakWorldSnapshot.HiddenRuntimeEnemyCount);
	MetricsObject->SetNumberField(TEXT("collision_disabled_runtime_enemy_count_peak"), PeakWorldSnapshot.CollisionDisabledRuntimeEnemyCount);
	MetricsObject->SetNumberField(TEXT("enemy_mesh_tick_disabled_count_peak"), PeakWorldSnapshot.EnemyMeshTickDisabledCount);
	MetricsObject->SetNumberField(TEXT("enemy_mesh_forced_lod_count_peak"), PeakWorldSnapshot.EnemyMeshForcedLodCount);
	MetricsObject->SetNumberField(TEXT("enemy_mesh_update_rate_optimization_count_peak"), PeakWorldSnapshot.EnemyMeshUpdateRateOptimizationCount);
	MetricsObject->SetBoolField(TEXT("runtime_budget_enabled"), bRuntimeBudgetingEnabled);
	MetricsObject->SetBoolField(TEXT("vanilla_ai_perception"), !bDirectAiTargeting);
	MetricsObject->SetBoolField(TEXT("direct_ai_targeting"), bDirectAiTargeting);
	MetricsObject->SetNumberField(TEXT("skeletal_mesh_component_count_peak"), PeakWorldSnapshot.SkeletalMeshComponentCount);
	MetricsObject->SetNumberField(TEXT("niagara_component_count_peak"), PeakWorldSnapshot.NiagaraComponentCount);
	MetricsObject->SetNumberField(TEXT("visible_widget_count_peak"), PeakWorldSnapshot.VisibleWidgetCount);
	MetricsObject->SetNumberField(TEXT("active_status_count_peak"), PeakWorldSnapshot.ActiveStatusCount);
	MetricsObject->SetNumberField(TEXT("chronicle_entry_count_peak"), PeakWorldSnapshot.ChronicleEntryCount);
	MetricsObject->SetNumberField(TEXT("dirty_paint_active_count_peak"), PeakWorldSnapshot.DirtyPaintActiveCount);
	MetricsObject->SetBoolField(TEXT("intimacy_active_seen"), bFullStackIntimacyActiveSeen || PeakWorldSnapshot.bIntimacyActive);
	MetricsObject->SetBoolField(TEXT("intimacy_menu_minigame_attempted"), bFullStackIntimacyAutomationAttempted);
	MetricsObject->SetBoolField(TEXT("intimacy_menu_minigame_completed"), bFullStackIntimacyMenuTestCompleted);
	MetricsObject->SetBoolField(TEXT("intimacy_session_interaction_completed"), bFullStackIntimacyInteractionCompleted);
	MetricsObject->SetBoolField(TEXT("intimacy_session_peak_triggered"), bFullStackIntimacySessionPeakTriggered);
	MetricsObject->SetNumberField(TEXT("intimacy_automation_step_count"), FullStackIntimacyAutomationStepCount);
	MetricsObject->SetStringField(TEXT("intimacy_stage_failed_reason"), FullStackIntimacyFailureReason);
	MetricsObject->SetBoolField(TEXT("lockpick_active_seen"), PeakWorldSnapshot.bLockpickActive);
	MetricsObject->SetNumberField(TEXT("debug_command_count"), FullStackDebugCommandCount);
	MetricsObject->SetNumberField(TEXT("enemy_to_player_damage_events"), FullStackEnemyToPlayerDamageCount);
	MetricsObject->SetNumberField(TEXT("player_to_enemy_damage_events"), FullStackPlayerToEnemyDamageCount);
	MetricsObject->SetNumberField(TEXT("async_loading_sample_count"), AsyncLoadingSampleCount);
	MetricsObject->SetNumberField(TEXT("map_travel_count"), MapTravelCount);

	const TSharedRef<FJsonObject> NaturalBootstrapMetricsObject = MakeShared<FJsonObject>();
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("sample_count"), NaturalBootstrapMetrics.FrameSampleCount);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("average_fps"), NaturalBootstrapMetrics.AverageFps);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("median_fps"), NaturalBootstrapMetrics.MedianFps);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("one_percent_low_fps"), NaturalBootstrapMetrics.OnePercentLowFps);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("p95_frame_ms"), NaturalBootstrapMetrics.P95FrameMs);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("p99_frame_ms"), NaturalBootstrapMetrics.P99FrameMs);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("hitches_over_50_ms"), NaturalBootstrapMetrics.HitchesOver50Ms);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("hitches_over_100_ms"), NaturalBootstrapMetrics.HitchesOver100Ms);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("hitches_over_500_ms"), NaturalBootstrapMetrics.HitchesOver500Ms);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("average_game_thread_ms"), AverageValue(NaturalBootstrapGameThreadMs));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("p95_game_thread_ms"), PercentileValue(NaturalBootstrapGameThreadMs, 0.95));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("average_render_thread_ms"), AverageValue(NaturalBootstrapRenderThreadMs));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("p95_render_thread_ms"), PercentileValue(NaturalBootstrapRenderThreadMs, 0.95));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("average_gpu_ms"), AverageValue(NaturalBootstrapGpuMs));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("p95_gpu_ms"), PercentileValue(NaturalBootstrapGpuMs, 0.95));
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("top_hitches_game_thread_dominant"), BootstrapGameThreadDominantHitches);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("top_hitches_render_thread_dominant"), BootstrapRenderThreadDominantHitches);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("top_hitches_gpu_dominant"), BootstrapGpuDominantHitches);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("top_hitches_mixed_or_unknown"), BootstrapMixedOrUnknownHitches);
	NaturalBootstrapMetricsObject->SetNumberField(TEXT("top_hitches_during_async_loading"), BootstrapAsyncLoadingHitches);

	TArray<TSharedPtr<FJsonValue>> SegmentValues;
	if (!FullStackStages.IsEmpty())
	{
		for (const FProjectRuntimePerformanceFullStackStage& Stage : FullStackStages)
		{
			TArray<double> SegmentFrameMs;
			double SegmentPeakMemoryMb = 0.0;
			int32 SegmentRuntimeEnemyPeak = 0;
			int32 SegmentBenchmarkSpawnedEnemyPeak = 0;
			int32 SegmentWorldRuntimeEnemyPeak = 0;
			int32 SegmentBudgetObservedEnemyPeak = 0;
			int32 SegmentActiveCombatEnemyPeak = 0;
			int32 SegmentFullRateEnemyPeak = 0;
			int32 SegmentEnemyWithoutControllerPeak = 0;
			int32 SegmentHiddenRuntimeEnemyPeak = 0;
			int32 SegmentCollisionDisabledRuntimeEnemyPeak = 0;
			int32 SegmentEnemyMeshTickDisabledPeak = 0;
			int32 SegmentEnemyMeshForcedLodPeak = 0;
			int32 SegmentEnemyMeshUpdateRateOptimizationPeak = 0;
			int32 SegmentVisibleWidgetPeak = 0;
			int32 SegmentStatusPeak = 0;
			int32 SegmentDirtyPeak = 0;
			int32 SegmentChroniclePeak = 0;
			int32 SegmentSyntheticFrameCount = 0;
			int32 SegmentExpectedIntimacySuppressionFrameCount = 0;
			bool bSegmentIntimacySeen = false;
			bool bSegmentLockpickSeen = false;
			for (const FProjectRuntimePerformanceFrameSample& Sample : Samples)
			{
				if (Sample.bWarmup || Sample.StageId != Stage.StageId)
				{
					continue;
				}
				if (Sample.bSyntheticBenchmarkWork)
				{
					++SegmentSyntheticFrameCount;
				}
				if (Sample.bExpectedIntimacySuppression)
				{
					++SegmentExpectedIntimacySuppressionFrameCount;
				}
				if (Sample.bExcludedFromMetrics)
				{
					continue;
				}

				SegmentFrameMs.Add(Sample.FrameMs);
				SegmentPeakMemoryMb = FMath::Max(SegmentPeakMemoryMb, Sample.UsedMemoryMb);
				SegmentBenchmarkSpawnedEnemyPeak = FMath::Max(SegmentBenchmarkSpawnedEnemyPeak, Sample.BenchmarkSpawnedEnemyCount);
				SegmentWorldRuntimeEnemyPeak = FMath::Max(SegmentWorldRuntimeEnemyPeak, Sample.WorldRuntimeEnemyCount);
				SegmentBudgetObservedEnemyPeak = FMath::Max(SegmentBudgetObservedEnemyPeak, Sample.BudgetObservedEnemyCount);
				SegmentActiveCombatEnemyPeak = FMath::Max(SegmentActiveCombatEnemyPeak, Sample.ActiveCombatEnemyCount);
				SegmentFullRateEnemyPeak = FMath::Max(SegmentFullRateEnemyPeak, Sample.FullRateEnemyCount);
				SegmentEnemyWithoutControllerPeak = FMath::Max(SegmentEnemyWithoutControllerPeak, Sample.RuntimeEnemyWithoutControllerCount);
				SegmentHiddenRuntimeEnemyPeak = FMath::Max(SegmentHiddenRuntimeEnemyPeak, Sample.HiddenRuntimeEnemyCount);
				SegmentCollisionDisabledRuntimeEnemyPeak = FMath::Max(SegmentCollisionDisabledRuntimeEnemyPeak, Sample.CollisionDisabledRuntimeEnemyCount);
				SegmentEnemyMeshTickDisabledPeak = FMath::Max(SegmentEnemyMeshTickDisabledPeak, Sample.EnemyMeshTickDisabledCount);
				SegmentEnemyMeshForcedLodPeak = FMath::Max(SegmentEnemyMeshForcedLodPeak, Sample.EnemyMeshForcedLodCount);
				SegmentEnemyMeshUpdateRateOptimizationPeak = FMath::Max(SegmentEnemyMeshUpdateRateOptimizationPeak, Sample.EnemyMeshUpdateRateOptimizationCount);
				SegmentRuntimeEnemyPeak = FMath::Max(SegmentRuntimeEnemyPeak, Sample.RuntimeEnemyCount);
				SegmentVisibleWidgetPeak = FMath::Max(SegmentVisibleWidgetPeak, Sample.VisibleWidgetCount);
				SegmentStatusPeak = FMath::Max(SegmentStatusPeak, Sample.ActiveStatusCount);
				SegmentDirtyPeak = FMath::Max(SegmentDirtyPeak, Sample.DirtyPaintActiveCount);
				SegmentChroniclePeak = FMath::Max(SegmentChroniclePeak, Sample.ChronicleEntryCount);
				bSegmentIntimacySeen |= Sample.bIntimacyActive;
				bSegmentLockpickSeen |= Sample.bLockpickActive;
			}

			const FProjectRuntimePerformanceMetrics SegmentMetrics = BuildMetrics(SegmentFrameMs);
			TSharedRef<FJsonObject> SegmentObject = MakeShared<FJsonObject>();
			SegmentObject->SetStringField(TEXT("stage_id"), Stage.StageId.ToString());
			SegmentObject->SetStringField(TEXT("stage_name"), Stage.StageName);
			SegmentObject->SetBoolField(TEXT("success"), Stage.bCompleted && !Stage.bFailed);
			SegmentObject->SetStringField(TEXT("failure_reason"), Stage.FailureReason);
			SegmentObject->SetNumberField(TEXT("start_seconds"), Stage.StartSeconds);
			SegmentObject->SetNumberField(TEXT("end_seconds"), Stage.EndSeconds);
			SegmentObject->SetNumberField(TEXT("sample_count"), SegmentMetrics.FrameSampleCount);
			SegmentObject->SetNumberField(TEXT("average_fps"), SegmentMetrics.AverageFps);
			SegmentObject->SetNumberField(TEXT("median_fps"), SegmentMetrics.MedianFps);
			SegmentObject->SetNumberField(TEXT("one_percent_low_fps"), SegmentMetrics.OnePercentLowFps);
			SegmentObject->SetNumberField(TEXT("zero_point_one_percent_low_fps"), SegmentMetrics.ZeroPointOnePercentLowFps);
			SegmentObject->SetNumberField(TEXT("p95_frame_ms"), SegmentMetrics.P95FrameMs);
			SegmentObject->SetNumberField(TEXT("p99_frame_ms"), SegmentMetrics.P99FrameMs);
			SegmentObject->SetNumberField(TEXT("hitches_over_50_ms"), SegmentMetrics.HitchesOver50Ms);
			SegmentObject->SetNumberField(TEXT("hitches_over_100_ms"), SegmentMetrics.HitchesOver100Ms);
			SegmentObject->SetNumberField(TEXT("hitches_over_500_ms"), SegmentMetrics.HitchesOver500Ms);
			SegmentObject->SetNumberField(TEXT("peak_memory_mb"), SegmentPeakMemoryMb);
			SegmentObject->SetNumberField(TEXT("benchmark_spawned_enemy_count_peak"), SegmentBenchmarkSpawnedEnemyPeak);
			SegmentObject->SetNumberField(TEXT("world_runtime_enemy_count_peak"), SegmentWorldRuntimeEnemyPeak);
			SegmentObject->SetNumberField(TEXT("budget_observed_enemy_count_peak"), SegmentBudgetObservedEnemyPeak);
			SegmentObject->SetNumberField(TEXT("active_combat_enemy_count_peak"), SegmentActiveCombatEnemyPeak);
			SegmentObject->SetNumberField(TEXT("full_rate_enemy_count_peak"), SegmentFullRateEnemyPeak);
			SegmentObject->SetNumberField(TEXT("runtime_enemy_without_controller_count_peak"), SegmentEnemyWithoutControllerPeak);
			SegmentObject->SetNumberField(TEXT("hidden_runtime_enemy_count_peak"), SegmentHiddenRuntimeEnemyPeak);
			SegmentObject->SetNumberField(TEXT("collision_disabled_runtime_enemy_count_peak"), SegmentCollisionDisabledRuntimeEnemyPeak);
			SegmentObject->SetNumberField(TEXT("enemy_mesh_tick_disabled_count_peak"), SegmentEnemyMeshTickDisabledPeak);
			SegmentObject->SetNumberField(TEXT("enemy_mesh_forced_lod_count_peak"), SegmentEnemyMeshForcedLodPeak);
			SegmentObject->SetNumberField(TEXT("enemy_mesh_update_rate_optimization_count_peak"), SegmentEnemyMeshUpdateRateOptimizationPeak);
			SegmentObject->SetNumberField(TEXT("runtime_enemy_count_peak"), SegmentRuntimeEnemyPeak);
			SegmentObject->SetNumberField(TEXT("visible_widget_count_peak"), SegmentVisibleWidgetPeak);
			SegmentObject->SetNumberField(TEXT("active_status_count_peak"), SegmentStatusPeak);
			SegmentObject->SetNumberField(TEXT("dirty_paint_active_count_peak"), SegmentDirtyPeak);
			SegmentObject->SetNumberField(TEXT("chronicle_entry_count_peak"), SegmentChroniclePeak);
			SegmentObject->SetNumberField(TEXT("benchmark_synthetic_frame_count"), SegmentSyntheticFrameCount);
			SegmentObject->SetNumberField(TEXT("expected_intimacy_suppression_frame_count"), SegmentExpectedIntimacySuppressionFrameCount);
			SegmentObject->SetBoolField(TEXT("intimacy_active_seen"), bSegmentIntimacySeen);
			SegmentObject->SetBoolField(TEXT("lockpick_active_seen"), bSegmentLockpickSeen);
			SegmentValues.Add(MakeShared<FJsonValueObject>(SegmentObject));
		}
	}

	const TSharedRef<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
	MetadataObject->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	MetadataObject->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	MetadataObject->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
	MetadataObject->SetStringField(TEXT("configuration"), LexToString(FApp::GetBuildConfiguration()));
	MetadataObject->SetStringField(TEXT("map"), GetCurrentShortMapName());
	MetadataObject->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand());
	MetadataObject->SetStringField(TEXT("gpu"), FPlatformMisc::GetPrimaryGPUBrand());
	MetadataObject->SetNumberField(
		TEXT("ram_mb"),
		static_cast<double>(FPlatformMemory::GetConstants().TotalPhysical) / (1024.0 * 1024.0));
	MetadataObject->SetNumberField(TEXT("resolution_x"), GSystemResolution.ResX);
	MetadataObject->SetNumberField(TEXT("resolution_y"), GSystemResolution.ResY);
	MetadataObject->SetStringField(TEXT("rhi"), FApp::GetGraphicsRHI());
	MetadataObject->SetStringField(TEXT("source_commit"), ActiveRequest.SourceCommit);
	MetadataObject->SetNumberField(TEXT("seed"), ActiveRequest.Seed);
	MetadataObject->SetStringField(TEXT("quality_preset"), ActiveRequest.QualityPreset.ToString());
	MetadataObject->SetStringField(TEXT("effective_cvar_hash"), EffectiveCVarHash);
	MetadataObject->SetStringField(TEXT("effective_cvars"), EffectiveCVarSnapshot);
	MetadataObject->SetStringField(TEXT("gpu_driver"), TEXT("PENDING_RUNNER_METADATA"));

	const TSharedRef<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
	SummaryObject->SetStringField(TEXT("run_id"), ActiveRunId);
	SummaryObject->SetStringField(TEXT("benchmark_id"), ActiveRequest.BenchmarkId.ToString());
	SummaryObject->SetBoolField(TEXT("success"), bSuccess);
	SummaryObject->SetStringField(TEXT("reason"), Reason);
	SummaryObject->SetStringField(TEXT("utc_completed"), FDateTime::UtcNow().ToIso8601());
	SummaryObject->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
	SummaryObject->SetNumberField(TEXT("measured_duration_seconds"), MeasuredDurationSeconds);
	SummaryObject->SetNumberField(TEXT("warmup_seconds"), ResolveActiveWarmupSeconds());
	SummaryObject->SetNumberField(TEXT("seed"), ActiveRequest.Seed);
	SummaryObject->SetStringField(TEXT("quality_preset"), ActiveRequest.QualityPreset.ToString());
	SummaryObject->SetStringField(TEXT("effective_cvar_hash"), EffectiveCVarHash);
	SummaryObject->SetBoolField(TEXT("gate_evaluated"), bAcceptanceProfile);
	SummaryObject->SetBoolField(TEXT("gate_pass"), bAcceptanceGatePassed);
	SummaryObject->SetBoolField(
		TEXT("scenario_pass"),
		bSuccess && !bNaturalGameplayScenarioFailed && !bAcceptanceScenarioFailed && !bFullStackScenarioFailed);
	SummaryObject->SetStringField(
		TEXT("scenario_failure_reason"),
		bNaturalGameplayScenarioFailed
			? NaturalGameplayScenarioFailureReason
			: (bAcceptanceScenarioFailed ? AcceptanceScenarioFailureReason : FullStackScenarioFailureReason));
	SummaryObject->SetBoolField(TEXT("natural_gameplay_profile"), IsNaturalGameplay58Profile());
	SummaryObject->SetBoolField(TEXT("benchmark_enemy_spawning_enabled"), !IsNaturalGameplay58Profile());
	SummaryObject->SetBoolField(TEXT("targeted_benchmark_preload_skipped"), IsNaturalGameplay58Profile());
	SummaryObject->SetBoolField(TEXT("uses_native_player_camera"), IsNaturalGameplay58Profile() && !BenchmarkCameraActor.IsValid());
	SummaryObject->SetNumberField(TEXT("natural_initial_enemy_count"), NaturalInitialEnemyCount);
	SummaryObject->SetNumberField(TEXT("natural_final_enemy_count"), WorldActiveEnemyCount);
	SummaryObject->SetNumberField(TEXT("natural_route_reversal_count"), NaturalRouteReversalCount);
	SummaryObject->SetNumberField(TEXT("natural_traversal_distance"), AcceptanceTraversalDistance);
	SummaryObject->SetBoolField(TEXT("acceptance_route_built"), bAcceptanceRouteBuilt);
	SummaryObject->SetNumberField(TEXT("acceptance_route_point_count"), AcceptanceRoutePoints.Num());
	SummaryObject->SetNumberField(TEXT("acceptance_traversal_distance"), AcceptanceTraversalDistance);
	SummaryObject->SetNumberField(
		TEXT("real_damage_event_count"),
		FullStackEnemyToPlayerDamageCount + FullStackPlayerToEnemyDamageCount);
	SummaryObject->SetNumberField(TEXT("real_attack_input_count"), AcceptanceAttackInputCount);
	SummaryObject->SetNumberField(TEXT("real_dodge_input_count"), AcceptanceDodgeInputCount);
	SummaryObject->SetNumberField(TEXT("dirty_pawn_workload_steps"), FullStackDirtyPaintApplyCount);
	SummaryObject->SetBoolField(TEXT("hud_opened"), bAcceptanceUiOpened || bFullStackHudOpened);
	SummaryObject->SetNumberField(TEXT("enemy_spawn_count"), SpawnedEnemyCount);
	SummaryObject->SetNumberField(TEXT("active_enemy_count"), ActiveEnemyCount);
	SummaryObject->SetNumberField(TEXT("tracked_active_enemy_count"), TrackedActiveEnemyCount);
	SummaryObject->SetNumberField(TEXT("world_active_enemy_count"), WorldActiveEnemyCount);
	SummaryObject->SetNumberField(TEXT("benchmark_spawned_enemy_count"), CurrentWorldSnapshot.BenchmarkSpawnedEnemyCount);
	SummaryObject->SetNumberField(TEXT("world_runtime_enemy_count"), CurrentWorldSnapshot.WorldRuntimeEnemyCount);
	SummaryObject->SetNumberField(TEXT("budget_observed_enemy_count"), CurrentWorldSnapshot.BudgetObservedEnemyCount);
	SummaryObject->SetNumberField(TEXT("active_combat_enemy_count"), CurrentWorldSnapshot.ActiveCombatEnemyCount);
	SummaryObject->SetNumberField(TEXT("full_rate_enemy_count"), CurrentWorldSnapshot.FullRateEnemyCount);
	SummaryObject->SetNumberField(TEXT("budget_suspended_enemy_count"), CurrentWorldSnapshot.BudgetSuspendedEnemyCount);
	SummaryObject->SetBoolField(TEXT("runtime_budget_enabled"), bRuntimeBudgetingEnabled);
	SummaryObject->SetStringField(TEXT("benchmark_ai_policy"), bDirectAiTargeting ? TEXT("DirectTargetingOptIn") : TEXT("VanillaACF"));
	SummaryObject->SetBoolField(TEXT("vanilla_ai_perception"), !bDirectAiTargeting);
	SummaryObject->SetBoolField(TEXT("direct_ai_targeting"), bDirectAiTargeting);
	SummaryObject->SetStringField(TEXT("run_directory"), RunDirectory);
	SummaryObject->SetStringField(TEXT("samples_path"), FPaths::Combine(RunDirectory, TEXT("samples.csv")));
	SummaryObject->SetStringField(TEXT("bootstrap_samples_path"), FPaths::Combine(RunDirectory, TEXT("bootstrap_samples.csv")));
	SummaryObject->SetStringField(TEXT("events_path"), FPaths::Combine(RunDirectory, TEXT("events.jsonl")));
	SummaryObject->SetStringField(TEXT("system_metrics_path"), FPaths::Combine(RunDirectory, TEXT("system_metrics.csv")));
	SummaryObject->SetStringField(TEXT("visual_screenshot_path"), FPaths::Combine(RunDirectory, TEXT("visual_check.png")));
	SummaryObject->SetBoolField(TEXT("visual_screenshot_excluded_from_metrics"), true);
	SummaryObject->SetStringField(TEXT("visual_screenshot_phase"), TEXT("running"));
	SummaryObject->SetNumberField(TEXT("excluded_frame_sample_count"), ExcludedFrameSampleCount);
	SummaryObject->SetNumberField(TEXT("benchmark_synthetic_frame_count"), SyntheticBenchmarkFrameSampleCount);
	SummaryObject->SetNumberField(TEXT("expected_intimacy_suppression_frame_count"), ExpectedIntimacySuppressionFrameSampleCount);
	SummaryObject->SetObjectField(TEXT("metrics"), MetricsObject);
	SummaryObject->SetObjectField(TEXT("natural_bootstrap_metrics"), NaturalBootstrapMetricsObject);
	SummaryObject->SetArrayField(TEXT("top_measured_hitches"), MeasuredHitchValues);
	SummaryObject->SetArrayField(TEXT("top_bootstrap_hitches"), BootstrapHitchValues);
	SummaryObject->SetObjectField(TEXT("metadata"), MetadataObject);
	SummaryObject->SetArrayField(TEXT("segments"), SegmentValues);
	SummaryObject->SetArrayField(TEXT("system_metrics"), BuildSystemMetricValues(MeasuredDurationSeconds));
	TArray<TSharedPtr<FJsonValue>> GateFailureValues;
	for (const FString& FailureReason : GateResult.FailureReasons)
	{
		GateFailureValues.Add(MakeShared<FJsonValueString>(FailureReason));
	}
	SummaryObject->SetArrayField(TEXT("gate_failure_reasons"), GateFailureValues);

	LastSummaryJson = ProjectRuntimePerformancePrivate::JsonToString(SummaryObject);
	FFileHelper::SaveStringToFile(LastSummaryJson, *FPaths::Combine(RunDirectory, TEXT("summary.json")));
	FFileHelper::SaveStringToFile(LastSummaryJson, *FPaths::Combine(OutputRoot, TEXT("latest.json")));

	TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
	Fields->SetBoolField(TEXT("success"), bSuccess);
	Fields->SetStringField(TEXT("reason"), Reason);
	Fields->SetNumberField(TEXT("average_fps"), LastMetrics.AverageFps);
	AppendEvent(TEXT("finished"), Fields);
}

FString UProjectRuntimePerformanceSubsystem::BuildRunId() const
{
	const FString BenchmarkName = ActiveRequest.BenchmarkId.IsNone()
		? ProjectRuntimePerformancePrivate::DungeonAcceptance58BenchmarkId.ToString()
		: ActiveRequest.BenchmarkId.ToString();
	return FString::Printf(
		TEXT("%s_%s"),
		*ProjectRuntimePerformancePrivate::SanitizeForFilename(BenchmarkName),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
}

FString UProjectRuntimePerformanceSubsystem::ResolveOutputRoot() const
{
	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	const FString RelativePath = Settings ? Settings->OutputRelativePath : TEXT("Automation/Performance58");
	return FPaths::ConvertRelativePathToFull(FPaths::IsRelative(RelativePath)
		? FPaths::Combine(FPaths::ProjectSavedDir(), RelativePath)
		: RelativePath);
}

FString UProjectRuntimePerformanceSubsystem::ResolveRunDirectory() const
{
	return FPaths::Combine(ResolveOutputRoot(), ActiveRunId.IsEmpty() ? TEXT("unknown_run") : ActiveRunId);
}

uint64 UProjectRuntimePerformanceSubsystem::ReadUsedPhysicalMemoryBytes() const
{
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	return Stats.UsedPhysical;
}

uint64 UProjectRuntimePerformanceSubsystem::ReadPeakPhysicalMemoryBytes() const
{
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	return Stats.PeakUsedPhysical;
}
