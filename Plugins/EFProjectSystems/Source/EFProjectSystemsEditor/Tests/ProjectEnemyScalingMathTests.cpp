#include "Characters/ProjectEnemyScalingMath.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyScalingMathLevelOneTest,
	"NoShellForWinter.Enemies.Leveling.ScalingMath.LevelOneKeepsBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyScalingMathLevelOneTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Level 1 health scaling should preserve baseline"), FProjectEnemyScalingMath::ScaleBaselineValue(100.0f, 1, 0.12f), 100.0f);
	TestEqual(TEXT("Level 1 damage scaling should preserve baseline"), FProjectEnemyScalingMath::ScaleBaselineValue(50.0f, 1, 0.08f), 50.0f);
	TestEqual(TEXT("Level 1 defense scaling should preserve baseline"), FProjectEnemyScalingMath::ScaleBaselineValue(25.0f, 1, 0.06f), 25.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyScalingMathTargetValueTest,
	"NoShellForWinter.Enemies.Leveling.ScalingMath.TargetValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyScalingMathTargetValueTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Level 2 health should gain 12%"), FMath::RoundToInt(FProjectEnemyScalingMath::ScaleBaselineValue(100.0f, 2, 0.12f)), 112);
	TestEqual(TEXT("Level 2 damage should gain 8%"), FMath::RoundToInt(FProjectEnemyScalingMath::ScaleBaselineValue(100.0f, 2, 0.08f)), 108);
	TestEqual(TEXT("Level 2 defense should gain 6%"), FMath::RoundToInt(FProjectEnemyScalingMath::ScaleBaselineValue(100.0f, 2, 0.06f)), 106);

	const float LevelFiveFromBaseline = FProjectEnemyScalingMath::ScaleBaselineValue(100.0f, 5, 0.12f);
	TestEqual(TEXT("Level 5 should scale from baseline, not from a previously scaled value"), FMath::RoundToInt(LevelFiveFromBaseline), 148);

	const FProjectEnemyScaledHealthValues HealthValues = FProjectEnemyScalingMath::ScaleHealthFromBaseline(200.0f, 0.75f, 3, 0.12f);
	TestEqual(TEXT("Health ratio should be preserved"), FMath::RoundToInt(HealthValues.TargetCurrentHealth), 186);
	TestEqual(TEXT("Health max should scale with level"), FMath::RoundToInt(HealthValues.TargetMaxHealth), 248);

	return true;
}

#endif
