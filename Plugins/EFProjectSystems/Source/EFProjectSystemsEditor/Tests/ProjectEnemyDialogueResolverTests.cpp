#include "Dialogue/ProjectEnemyDialogueResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyDialogueNeutralFallbackTest,
	"NoShellForWinter.Chronicles.ProbabilityResolver.NeutralFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyDialogueNeutralFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		const FString Solo = ProjectEnemyDialogueResolver::PickSightBarkForRoll(
			nullptr,
			false,
			true,
			0.0f);
		const FString Group = ProjectEnemyDialogueResolver::PickSightBarkForRoll(
			nullptr,
			true,
			true,
			0.0f);
		TestFalse(TEXT("The neutral solo pool always returns a line."), Solo.IsEmpty());
		TestFalse(TEXT("The neutral group pool always returns a line."), Group.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyDialogueProbabilityBoundariesTest,
	"NoShellForWinter.Chronicles.ProbabilityResolver.Boundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyDialogueProbabilityBoundariesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(
		TEXT("Roll zero selects the original Chronicle pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, 0.0f));
	TestTrue(
		TEXT("A roll immediately below ten percent selects the original Chronicle pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, 0.099f));
	TestFalse(
		TEXT("The ten-percent boundary selects the neutral pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, 0.10f));
	TestFalse(
		TEXT("Streamer Safe always selects the neutral pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(true, 0.0f));
	TestFalse(
		TEXT("Invalid rolls fail closed to the neutral pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, std::numeric_limits<float>::quiet_NaN()));
	TestFalse(
		TEXT("Negative rolls fail closed to the neutral pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, -0.01f));
	TestFalse(
		TEXT("Rolls above one fail closed to the neutral pool."),
		ProjectEnemyDialogueResolver::ShouldUseOriginalBark(false, 1.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyDialogueSeededDistributionTest,
	"NoShellForWinter.Chronicles.ProbabilityResolver.SeededTenPercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyDialogueSeededDistributionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FRandomStream Stream(20260723);
	constexpr int32 SampleCount = 100000;
	int32 OriginalCount = 0;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		OriginalCount += ProjectEnemyDialogueResolver::ShouldUseOriginalBark(
			false,
			Stream.FRand()) ? 1 : 0;
	}

	const float ObservedRatio = static_cast<float>(OriginalCount) / static_cast<float>(SampleCount);
	TestTrue(
		TEXT("The seeded original-bark rate stays within a one-percent tolerance of ten percent."),
		FMath::Abs(ObservedRatio - 0.10f) <= 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyDialogueDefaultPolicyTest,
	"NoShellForWinter.Chronicles.ProbabilityResolver.DefaultFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyDialogueDefaultPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSet<FString> NeutralFallbackSolo = {
		TEXT("There you are."),
		TEXT("You chose the wrong path."),
		TEXT("This area is restricted."),
		TEXT("Your luck just ran out."),
		TEXT("Turn back while you can."),
		TEXT("I see you."),
		TEXT("You're not leaving easily."),
		TEXT("Let's finish this.")
	};
	TestEqual(
		TEXT("A missing actor/world resolves to the neutral fallback archetype."),
		static_cast<uint8>(ProjectEnemyDialogueResolver::ResolveArchetype(nullptr)),
		static_cast<uint8>(EProjectEnemyDialogueArchetype::Fallback));
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		const FString Bark = ProjectEnemyDialogueResolver::PickSightBark(nullptr, false);
		TestTrue(
			TEXT("A missing policy can only return a line from the neutral fallback pool."),
			NeutralFallbackSolo.Contains(Bark));
	}
	return true;
}

#endif
