#if WITH_DEV_AUTOMATION_TESTS
#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "DayCycle/ProjectDayCycleStateActor.h"
#include "DayCycle/ProjectDayCycleMath.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "DaySequenceSubsystem.h"
#include "Components/ACFAIRoutineComponent.h"
#include "Tasks/ACFRandomPatrolAroundPointTask.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTimeScaleTest, "NoShellForWinter.TimeControl.ScaleAndCalendar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectTimeScaleTest::RunTest(const FString&)
{
	TestEqual(TEXT("Default day is thirty minutes"), UProjectDayCycleSettings::Get()->DayLengthSeconds, 1800.f);
	for (float Speed : {1.f, 2.f, 4.f})
	{
		TestTrue(TEXT("Supported speed"), UProjectDayCycleSubsystem::IsSupportedSpeed(Speed));
		const double Simulation = (3600.0 / Speed) * Speed;
		TestEqual(TEXT("48 hour book at any speed"), UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours(Simulation, 1800.0), 48.0);
		TestEqual(TEXT("Three hour training"), UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours((225.0 / Speed) * Speed, 1800.0), 3.0);
	}
	TestFalse(TEXT("Zero is not pseudo-pause"), UProjectDayCycleSubsystem::IsSupportedSpeed(0.f));
	TestFalse(TEXT("Unapproved speed rejected"), UProjectDayCycleSubsystem::IsSupportedSpeed(3.f));
	TestEqual(TEXT("Invalid duration"), UProjectDayCycleSubsystem::SimulationSecondsToCalendarHours(1, 0), 0.0);
	const auto Day = FProjectDayCycleMath::BuildSnapshot(1, 1800, 1800);
	TestEqual(TEXT("Thirty minutes advances exactly one day"), Day.DayNumber, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTimeNeedsTest, "NoShellForWinter.TimeControl.NeedRateEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectTimeNeedsTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Needs = NewObject<UProjectSurvivalNeedsComponent>(Owner);
	Owner->AddInstanceComponent(Needs);
	Needs->RegisterComponent();
	Needs->ResetToDefaults();
	for (int32 I = 0; I < 100; ++I) Needs->TickComponent(1.f, LEVELTICK_All, nullptr);
	const float One = Needs->GetNeedCurrentValue(TEXT("Hunger"));
	Needs->ResetToDefaults();
	for (int32 I = 0; I < 25; ++I) Needs->TickComponent(4.f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Same simulated time consumes same hunger at x1 and x4"), FMath::IsNearlyEqual(One, Needs->GetNeedCurrentValue(TEXT("Hunger")), 0.002f));
	TestTrue(TEXT("Calendar consumption preserved after lengthening the day"), FMath::IsNearlyEqual(One, 100.f - 0.16f * 100.f / 3.f, 0.002f));
	const float Before = Needs->GetNeedCurrentValue(TEXT("Hunger"));
	Needs->TickComponent(0.f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Zero simulation time consumes nothing"), Needs->GetNeedCurrentValue(TEXT("Hunger")), Before);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectTimeSaveTest, "NoShellForWinter.TimeControl.ClockSaveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectTimeSaveTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Clock = World->SpawnActor<AProjectDayCycleStateActor>();
	// This isolated world has not routed actor initialization/BeginPlay.
	Clock->PostInitializeComponents();
	TestTrue(TEXT("Native time can be set"), Clock->SetTimeOfDay(12.f));
	TestTrue(TEXT("Half day"), FMath::IsNearlyEqual(Clock->GetElapsedCalendarDays(), 0.5, 0.00001));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive Save(Writer, false);
	Save.ArIsSaveGame = true;
	Clock->Serialize(Save);
	auto* Restored = World->SpawnActor<AProjectDayCycleStateActor>();
	Restored->PostInitializeComponents();
	FMemoryReader Reader(Bytes);
	FObjectAndNameAsStringProxyArchive Load(Reader, false);
	Load.ArIsSaveGame = true;
	Restored->Serialize(Load);
	Restored->OnLoaded_Implementation();
	TestTrue(TEXT("Calendar restored without wall-clock/offline credit"), FMath::IsNearlyEqual(Restored->GetElapsedCalendarDays(), 0.5, 0.00001));
	TestTrue(TEXT("Native time restored"), FMath::IsNearlyEqual(Restored->GetTimeOfDay(), 12.f));
	Restored->SetTimeOfDay(0.f);
	TestEqual(TEXT("Continues from saved day"), Restored->GetElapsedCalendarDays(), 1.0);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectACFNativeTimeTest, "NoShellForWinter.TimeControl.ACFNativeRoutine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectACFNativeTimeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Clock = World->SpawnActor<AProjectDayCycleStateActor>();
	Clock->PostInitializeComponents();
	World->GetSubsystem<UDaySequenceSubsystem>()->SetDaySequenceActor(Clock);
	auto* Controller = World->SpawnActor<AAIController>();
	Controller->Possess(World->SpawnActor<APawn>());
	auto* Routine = NewObject<UACFAIRoutineComponent>(Controller);
	Controller->AddInstanceComponent(Routine);
	Routine->RegisterComponent();
	auto* Schedule = NewObject<UACFAIRoutineDataAsset>(Routine);
	for (int Hour : {0, 8, 16})
	{
		FAIRoutineTask Entry;
		Entry.TaskTime.Hour = Hour;
		Entry.TaskTime.Minute = 0;
		// Native concrete tasks safely do nothing on this generic test controller.
		Entry.RoutineTask = NewObject<UACFRandomPatrolAroundPointTask>(Schedule);
		Schedule->DailyTasks.Add(Entry);
	}
	Routine->SetRoutineDataAsset(Schedule);
	for (int Index = 0; Index < 3; ++Index)
	{
		const float Hour = Index * 8.f + 1.f;
		TestTrue(TEXT("Native clock seek"), Clock->SetTimeOfDay(Hour));
		TestTrue(TEXT("ACF routine reads calendar clock"), FMath::IsNearlyEqual(Routine->GetTimeHours(), Hour, 0.0001f));
		TestTrue(TEXT("Native routine transitions"), Routine->UpdateByTime());
		TestEqual(TEXT("Correct native task selected"), Routine->GetLastStartedTask(), Schedule->DailyTasks[Index].RoutineTask);
		TestFalse(TEXT("No duplicate task start at same time"), Routine->UpdateByTime());
		TestTrue(TEXT("Calendar matches native time"), FMath::IsNearlyEqual(Clock->GetElapsedCalendarDays() * 24.0, static_cast<double>(Hour), 0.0001));
		TestEqual(TEXT("HUD phase matches routine slot"), static_cast<int32>(Clock->GetCurrentSnapshot().Phase), Index);
	}
	Clock->SetTimeOfDay(0.5f);
	TestEqual(TEXT("Native midnight advances the day"), Clock->GetCurrentSnapshot().DayNumber, 2);
	TestTrue(TEXT("Native routine wraps at midnight"), Routine->UpdateByTime());
	TestEqual(TEXT("Morning task after midnight"), Routine->GetLastStartedTask(), Schedule->DailyTasks[0].RoutineTask);
	Routine->StopAll();
	World->DestroyWorld(false);
	return true;
}
#endif
