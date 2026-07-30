#include "RuntimePerformance/ProjectRuntimePerformanceSettings.h"
#include "RuntimePerformance/ProjectRuntimePerformanceSubsystem.h"
#include "RuntimePerformance/ProjectPerformanceBudgetSettings.h"
#include "Debug/ProjectGameplayDebugCommandExecutor.h"
#include "Debug/ProjectGameplayDebugMenuWidget.h"
#include "Survival/ProjectSurvivalStatusComponent.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceMetricsTest,
	"Project.RuntimePerformance.Metrics.CalculateFrameStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceMetricsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<double> FrameTimesMs;
	for (int32 Index = 0; Index < 98; ++Index)
	{
		FrameTimesMs.Add(16.0);
	}
	FrameTimesMs.Add(50.0);
	FrameTimesMs.Add(120.0);

	const FProjectRuntimePerformanceMetrics Metrics =
		UProjectRuntimePerformanceSubsystem::BuildMetrics(FrameTimesMs);

	TestEqual(TEXT("Frame sample count"), Metrics.FrameSampleCount, 100);
	TestTrue(TEXT("Average FPS should be below the stable 62.5 FPS because of slow frames"), Metrics.AverageFps < 62.5);
	TestTrue(TEXT("Median FPS should stay near the stable frame rate"), FMath::IsNearlyEqual(Metrics.MedianFps, 62.5, 0.1));
	TestTrue(TEXT("Min FPS should reflect the slowest frame"), FMath::IsNearlyEqual(Metrics.MinFps, 1000.0 / 120.0, 0.01));
	TestTrue(TEXT("1% low FPS should be slower than average FPS"), Metrics.OnePercentLowFps < Metrics.AverageFps);
	TestEqual(TEXT("Hitches over 50ms should count strictly greater than 50ms"), Metrics.HitchesOver50Ms, 1);
	TestEqual(TEXT("Hitches over 100ms"), Metrics.HitchesOver100Ms, 1);
	TestEqual(TEXT("Hitches over 500ms"), Metrics.HitchesOver500Ms, 0);
	TestTrue(TEXT("P99 should land on the tail"), Metrics.P99FrameMs > 50.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceSettingsTest,
	"Project.RuntimePerformance.Settings.DefaultDungeonCombatBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	TestNotNull(TEXT("Runtime performance settings exist"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("Dungeon combat map should point at DungeonGeneration"), Settings->DungeonCombatMap.ToString().Contains(TEXT("DungeonGeneration")));
	TestTrue(TEXT("Default benchmark duration should be 180 seconds"), FMath::IsNearlyEqual(Settings->DefaultDurationSeconds, 180.0f));
	TestTrue(TEXT("Real gameplay benchmark duration should be 300 seconds"), FMath::IsNearlyEqual(Settings->RealGameplayDurationSeconds, 300.0f));
	TestTrue(TEXT("Warmup should be 10 seconds"), FMath::IsNearlyEqual(Settings->WarmupSeconds, 10.0f));
	TestEqual(TEXT("Default enemy count"), Settings->EnemyCount, 4);
	TestTrue(TEXT("Benchmarks should use vanilla ACF perception by default"), Settings->bUseVanillaAIPerceptionForBenchmarks);
	TestFalse(TEXT("Benchmarks should not force direct ACF targets by default"), Settings->bAllowBenchmarkDirectAITargeting);
	TestFalse(TEXT("Output path should not be empty"), Settings->OutputRelativePath.IsEmpty());
	TestTrue(TEXT("Stable combat profile should be supported"), UProjectRuntimePerformanceSubsystem::IsSupportedBenchmarkId(TEXT("DungeonCombatStable")));
	TestTrue(TEXT("Real gameplay profile should be supported"), UProjectRuntimePerformanceSubsystem::IsSupportedBenchmarkId(TEXT("DungeonGameplayReal")));
	TestTrue(TEXT("Full stack overload profile should be supported"), UProjectRuntimePerformanceSubsystem::IsSupportedBenchmarkId(TEXT("DungeonFullStackOverload")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceBudgetSettingsTest,
	"Project.RuntimePerformance.Settings.RuntimeBudgetDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceBudgetSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UProjectPerformanceBudgetSettings* Settings = UProjectPerformanceBudgetSettings::Get();
	TestNotNull(TEXT("Performance budget settings exist"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("Runtime budgeting should not affect vanilla gameplay by default"), Settings->bEnableRuntimeBudgeting);
	TestFalse(TEXT("Runtime combat preload should not affect vanilla gameplay by default"), Settings->bPreloadRuntimeCombatAssets);
	TestFalse(TEXT("Runtime preload should include project-owned assets"), Settings->AdditionalPreloadAssets.IsEmpty());
	TestTrue(TEXT("Dormant full-rate budget should allow a normal ACF encounter"), Settings->MaxFullRateEnemyAnimations >= 8);
	TestTrue(TEXT("Dormant awake enemy budget should not aggressively sleep the dungeon"), Settings->MaxAwakeDungeonEnemies >= 20);
	TestTrue(TEXT("Dormant runtime enemy cap should not cull normal dungeon spawns"), Settings->MaxRuntimeEnemyActors >= 32);
	TestTrue(TEXT("Dormant mid-rate budget should allow a normal ACF encounter"), Settings->MaxMidRateEnemyAnimations >= 8);
	TestTrue(TEXT("Budget update interval should not run every frame"), Settings->BudgetUpdateIntervalSeconds >= 0.25f);
	TestTrue(TEXT("Far visual tick should be throttled"), Settings->FarVisualTickInterval >= 0.25f);
	TestTrue(TEXT("Dormant enemies should tick slowly"), Settings->DormantEnemyTickInterval >= 0.75f);
	TestFalse(TEXT("Enemy actor tick budget should be disabled for vanilla gameplay"), Settings->bApplyEnemyActorTickBudget);
	TestFalse(TEXT("Enemy animation budget should be disabled for vanilla gameplay"), Settings->bApplyEnemyAnimationBudget);
	TestFalse(TEXT("Enemy movement tick budget should be disabled for vanilla gameplay"), Settings->bApplyEnemyMovementTickBudget);
	TestFalse(TEXT("Excess runtime enemies should not be culled by default"), Settings->bCullExcessRuntimeEnemies);
	TestFalse(TEXT("World VFX budget should be disabled for vanilla gameplay"), Settings->bApplyWorldVfxBudget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceDebugMenuTest,
	"Project.RuntimePerformance.DebugMenu.RuntimeFpsCommandSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceDebugMenuTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("Runtime FPS debug row class exists"), UProjectGameplayDebugRuntimeFpsBenchmarkRowWidget::StaticClass());
	TestFalse(TEXT("Runtime FPS command should no-op without an owner"), FProjectGameplayDebugCommandExecutor::StartRuntimeFpsBenchmark(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceFullStackProfileSupportedTest,
	"Project.RuntimePerformance.FullStackProfileSupported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceFullStackProfileSupportedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UProjectRuntimePerformanceSettings* Settings = UProjectRuntimePerformanceSettings::Get();
	TestNotNull(TEXT("Runtime performance settings exist"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestTrue(TEXT("Full stack profile is supported"), UProjectRuntimePerformanceSubsystem::IsSupportedBenchmarkId(TEXT("DungeonFullStackOverload")));
	TestTrue(TEXT("Full stack duration defaults to 540 seconds"), FMath::IsNearlyEqual(Settings->FullStackOverloadDurationSeconds, 540.0f));
	TestEqual(TEXT("Full stack enemy cap defaults to 8"), Settings->FullStackOverloadEnemyCap, 8);
	TestTrue(TEXT("Full stack strict failures default on"), Settings->bStrictFullStackScenarioFailures);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceFullStackStageScheduleTest,
	"Project.RuntimePerformance.FullStackStageSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceFullStackStageScheduleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FName> StageIds = UProjectRuntimePerformanceSubsystem::BuildFullStackStageIds();
	TestEqual(TEXT("Full stack stage count"), StageIds.Num(), 10);
	TestEqual(TEXT("First stage"), StageIds.IsValidIndex(0) ? StageIds[0] : NAME_None, FName(TEXT("ExplorationUI")));
	TestTrue(TEXT("Melee stage present"), StageIds.Contains(FName(TEXT("EnemySoloMelee"))));
	TestTrue(TEXT("Ranged stage present"), StageIds.Contains(FName(TEXT("EnemySoloRanged"))));
	TestTrue(TEXT("Mage stage present"), StageIds.Contains(FName(TEXT("EnemySoloMage"))));
	TestTrue(TEXT("Dirty Pawn stacked combat stage present"), StageIds.Contains(FName(TEXT("StackedCombatDirtyPawn"))));
	TestEqual(TEXT("Last stage"), StageIds.Num() > 0 ? StageIds.Last() : NAME_None, FName(TEXT("DebugCommandSweep")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceStrictScenarioFailureTest,
	"Project.RuntimePerformance.StrictScenarioFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceStrictScenarioFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Strict required issue should fail"), UProjectRuntimePerformanceSubsystem::ShouldFailForScenarioIssue(true, true));
	TestFalse(TEXT("Strict optional issue should not fail"), UProjectRuntimePerformanceSubsystem::ShouldFailForScenarioIssue(true, false));
	TestFalse(TEXT("Non-strict required issue should not fail"), UProjectRuntimePerformanceSubsystem::ShouldFailForScenarioIssue(false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceSegmentMetricsTest,
	"Project.RuntimePerformance.SegmentMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceSegmentMetricsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<double> SegmentFrameTimesMs = { 16.0, 16.0, 33.0, 50.0, 120.0 };
	const FProjectRuntimePerformanceMetrics SegmentMetrics =
		UProjectRuntimePerformanceSubsystem::BuildMetrics(SegmentFrameTimesMs);

	TestEqual(TEXT("Segment sample count"), SegmentMetrics.FrameSampleCount, 5);
	TestTrue(TEXT("Segment p99 captures tail"), SegmentMetrics.P99FrameMs > 100.0);
	TestEqual(TEXT("Segment hitch over 100ms"), SegmentMetrics.HitchesOver100Ms, 1);
	TestTrue(TEXT("Segment 1% low is below average"), SegmentMetrics.OnePercentLowFps < SegmentMetrics.AverageFps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceSyntheticMetricSeparationTest,
	"Project.RuntimePerformance.SyntheticMetricSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceSyntheticMetricSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<double> RealFrameTimesMs = { 16.0, 16.0, 20.0 };
	const TArray<double> SyntheticFrameTimesMs = { 120.0, 16.0 };
	const FProjectRuntimePerformanceMetrics RealMetrics =
		UProjectRuntimePerformanceSubsystem::BuildMetrics(RealFrameTimesMs);
	const FProjectRuntimePerformanceMetrics SyntheticMetrics =
		UProjectRuntimePerformanceSubsystem::BuildMetrics(SyntheticFrameTimesMs);

	TestEqual(TEXT("Real hitches over 100ms stay clean"), RealMetrics.HitchesOver100Ms, 0);
	TestEqual(TEXT("Synthetic hitches over 100ms are counted separately"), SyntheticMetrics.HitchesOver100Ms, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceStatusBatchApiTest,
	"Project.RuntimePerformance.StatusBatchApi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceStatusBatchApiTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(
		TEXT("Survival status component exposes batch debug status application"),
		UProjectSurvivalStatusComponent::StaticClass()->FindFunctionByName(TEXT("ApplyDebugStatuses")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRuntimePerformanceDebugMenuFullStackCommandTest,
	"Project.RuntimePerformance.DebugMenuFullStackCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRuntimePerformanceDebugMenuFullStackCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestNotNull(TEXT("Full stack debug row class exists"), UProjectGameplayDebugFullStackOverloadBenchmarkRowWidget::StaticClass());
	TestFalse(TEXT("Full stack command should no-op without an owner"), FProjectGameplayDebugCommandExecutor::StartFullStackOverloadBenchmark(nullptr));
	return true;
}
