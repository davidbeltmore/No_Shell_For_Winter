#include "Debug/ProjectGameplayDebugCommandExecutor.h"
#include "Calysto/ProjectCalystoFloorOutcomeSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonHarnessDebugNullWorldTest,
	"NoShellForWinter.GameplayDebug.DungeonHarness.V4.NullWorldSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonHarnessDebugNullWorldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestFalse(TEXT("Status refresh should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RefreshDungeonHarnessStatus(nullptr));
	TestFalse(TEXT("Next floor should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestAdvanceDungeonFloor(nullptr));
	TestFalse(TEXT("Floor travel should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestTravelToDungeonFloor(nullptr, 2));
	TestFalse(TEXT("Replay should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestReplayDungeonFloor(nullptr));
	TestFalse(TEXT("Reroll should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestRerollDungeonFloor(nullptr));
	TestFalse(TEXT("New random run should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestStartNewDungeonRun(nullptr));
	TestFalse(TEXT("Seed 42 run should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::RequestStartDungeonTestRun(nullptr));
	TestFalse(TEXT("Style intent should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredStyle(
			nullptr, false, EEFCalystoStyleV4::Compact));
	TestFalse(TEXT("Theme intent should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessPreferredTheme(
			nullptr, false, EEFCalystoThemeV4::Forge));
	TestFalse(TEXT("Scale intent should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			nullptr, TEXT("Scale"), 0.5f));
	TestFalse(TEXT("Volatility intent should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentVolatility(
			nullptr, 0.5f));
	TestFalse(TEXT("Clear intent should no-op without a runtime owner"),
		FProjectGameplayDebugCommandExecutor::ClearDungeonHarnessDirectorIntent(nullptr));
	TestFalse(TEXT("Out-of-range intent bias should be rejected"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			nullptr, TEXT("Danger"), 1.01f));
	TestFalse(TEXT("Unknown intent axis should be rejected"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentBias(
			nullptr, TEXT("ExactEnemyCount"), 0.0f));
	TestFalse(TEXT("Out-of-range volatility should be rejected"),
		FProjectGameplayDebugCommandExecutor::SetDungeonHarnessIntentVolatility(
			nullptr, -1.01f));
	TestFalse(TEXT("Unavailable status label should still be meaningful"),
		FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusLabel(nullptr).IsEmpty());
	TestFalse(TEXT("Unavailable status description should still be meaningful"),
		FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStatusDescription(nullptr).IsEmpty());
	TestFalse(TEXT("Style choice label should still be meaningful"),
		FProjectGameplayDebugCommandExecutor::GetDungeonHarnessStyleChoiceLabel(
			nullptr, true, EEFCalystoStyleV4::Standard).IsEmpty());
	TestFalse(TEXT("Theme choice label should still be meaningful"),
		FProjectGameplayDebugCommandExecutor::GetDungeonHarnessThemeChoiceLabel(
			nullptr, true, EEFCalystoThemeV4::Default).IsEmpty());
	TestFalse(TEXT("Bias choice label should still be meaningful"),
		FProjectGameplayDebugCommandExecutor::GetDungeonHarnessBiasChoiceLabel(
			nullptr, TEXT("Scale"), 0.0f).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonHarnessDebugPersistentClassificationTest,
	"NoShellForWinter.GameplayDebug.DungeonHarness.V4.PersistentClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonHarnessDebugPersistentClassificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	for (const FName PersistentCommand : {
		FName(TEXT("DungeonHarness.Status")),
		FName(TEXT("DungeonHarness.ClearIntent")),
		FName(TEXT("DungeonHarness.Style.Auto")),
		FName(TEXT("DungeonHarness.Style.Standard")),
		FName(TEXT("DungeonHarness.Style.Compact")),
		FName(TEXT("DungeonHarness.Style.Branching")),
		FName(TEXT("DungeonHarness.Theme.Auto")),
		FName(TEXT("DungeonHarness.Theme.Default")),
		FName(TEXT("DungeonHarness.Theme.Forge")),
		FName(TEXT("DungeonHarness.Theme.Shrine")),
		FName(TEXT("DungeonHarness.ScaleBias.-1.00")),
		FName(TEXT("DungeonHarness.BranchingBias.-0.50")),
		FName(TEXT("DungeonHarness.DangerBias.0.00")),
		FName(TEXT("DungeonHarness.SafeBias.0.50")),
		FName(TEXT("DungeonHarness.AbundanceBias.1.00")),
		FName(TEXT("DungeonHarness.MysteryBias.-0.50")),
		FName(TEXT("DungeonHarness.ClothingBias.0.50")),
		FName(TEXT("DungeonHarness.Volatility.-0.50")) })
	{
		TestTrue(
			*FString::Printf(TEXT("%s should keep the menu open"), *PersistentCommand.ToString()),
			FProjectGameplayDebugCommandExecutor::IsDungeonHarnessPersistentCommand(PersistentCommand));
	}

	for (const FName ClosingCommand : {
		FName(TEXT("DungeonHarness.NextFloor")),
		FName(TEXT("DungeonHarness.Replay")),
		FName(TEXT("DungeonHarness.Reroll")),
		FName(TEXT("DungeonHarness.NewRandomRun")),
		FName(TEXT("DungeonHarness.NewSeed42")),
		FName(TEXT("DungeonHarness.JumpFloor.1")),
		FName(TEXT("DungeonHarness.JumpFloor.10")),
		FName(TEXT("DungeonHarness.JumpFloor.25")),
		FName(TEXT("DungeonHarness.JumpFloor.50")),
		FName(TEXT("DungeonHarness.JumpFloor.100")),
		FName(TEXT("DungeonHarness.JumpFloor.101")),
		FName(TEXT("DungeonHarness.JumpFloor.125")),
		FName(TEXT("DungeonHarness.JumpFloor.500")),
		FName(TEXT("DungeonHarness.JumpFloor.1000")) })
	{
		TestFalse(
			*FString::Printf(TEXT("%s should close the menu"), *ClosingCommand.ToString()),
			FProjectGameplayDebugCommandExecutor::IsDungeonHarnessPersistentCommand(ClosingCommand));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonDirectorOutcomeNormalizationTest,
	"NoShellForWinter.GameplayDebug.DungeonHarness.V4.OutcomeNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonDirectorOutcomeNormalizationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(
		TEXT("A zero-enemy floor has neutral combat evidence"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(0, 0),
		0.5f);
	TestEqual(
		TEXT("Leaving every enemy alive is minimum combat completion"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(10, 10),
		0.0f);
	TestEqual(
		TEXT("Defeating half of the initial population is neutral combat completion"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(10, 5),
		0.5f);
	TestEqual(
		TEXT("Defeating the full initial population is maximum combat completion"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(10, 0),
		1.0f);
	TestEqual(
		TEXT("Impossible alive counts are clamped to the initial manifest"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(10, 50),
		0.0f);

	const FIntVector FloorSize(20, 20, 1);
	constexpr int32 InitialEnemies = 4;
	constexpr double ExpectedSeconds = 282.0;
	TestTrue(
		TEXT("Expected completion time resolves to neutral pace"),
		FMath::IsNearlyEqual(
			UProjectCalystoFloorOutcomeSubsystem::AutomationPaceScore(
				ExpectedSeconds, FloorSize, InitialEnemies),
			0.5f));
	TestTrue(
		TEXT("Faster completion is above neutral pace"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationPaceScore(
			ExpectedSeconds * 0.5, FloorSize, InitialEnemies) > 0.5f);
	TestTrue(
		TEXT("Slower completion is below neutral pace"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationPaceScore(
			ExpectedSeconds * 2.0, FloorSize, InitialEnemies) < 0.5f);
	TestEqual(
		TEXT("Missing floor-ready time stays neutral"),
		UProjectCalystoFloorOutcomeSubsystem::AutomationPaceScore(
			-1.0, FloorSize, InitialEnemies),
		0.5f);

	return true;
}

#endif
