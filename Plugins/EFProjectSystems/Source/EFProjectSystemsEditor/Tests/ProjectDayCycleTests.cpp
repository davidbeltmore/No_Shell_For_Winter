#if WITH_DEV_AUTOMATION_TESTS

#include "DayCycle/ProjectDayCycleMath.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDayCycleMathTest,
	"EFProjectSystems.DayCycle.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDayCycleMathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FProjectDayCycleSnapshot Start = FProjectDayCycleMath::BuildSnapshot(1, 0.0, 600.0f);
	TestEqual(TEXT("The cycle starts on day one"), Start.DayNumber, 1);
	TestEqual(TEXT("The cycle starts in the morning"), Start.Phase, EProjectDayPhase::Morning);
	TestEqual(TEXT("Start progress is zero"), Start.NormalizedDayProgress, 0.0f);

	const FProjectDayCycleSnapshot Afternoon = FProjectDayCycleMath::BuildSnapshot(1, 200.0, 600.0f);
	TestEqual(TEXT("One third begins the afternoon"), Afternoon.Phase, EProjectDayPhase::Afternoon);
	TestTrue(TEXT("Afternoon phase starts at zero"), FMath::IsNearlyZero(Afternoon.PhaseProgress));

	const FProjectDayCycleSnapshot Night = FProjectDayCycleMath::BuildSnapshot(1, 400.0, 600.0f);
	TestEqual(TEXT("Two thirds begins the night"), Night.Phase, EProjectDayPhase::Night);
	TestTrue(TEXT("Night phase starts at zero"), FMath::IsNearlyZero(Night.PhaseProgress));

	const FProjectDayCycleSnapshot NextDay = FProjectDayCycleMath::BuildSnapshot(1, 600.0, 600.0f);
	TestEqual(TEXT("Ten minutes advances the day counter"), NextDay.DayNumber, 2);
	TestEqual(TEXT("A new day returns to morning"), NextDay.Phase, EProjectDayPhase::Morning);
	TestTrue(TEXT("A new day returns progress to zero"), FMath::IsNearlyZero(NextDay.NormalizedDayProgress));

	const FProjectDayCycleSnapshot LaterDay = FProjectDayCycleMath::BuildSnapshot(3, 1500.0, 600.0f);
	TestEqual(TEXT("Completed cycles accumulate from the configured initial day"), LaterDay.DayNumber, 5);
	TestEqual(TEXT("Half a day is afternoon"), LaterDay.Phase, EProjectDayPhase::Afternoon);

	return true;
}

#endif
