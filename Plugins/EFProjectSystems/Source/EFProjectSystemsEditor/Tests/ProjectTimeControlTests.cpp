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
	Clock->Tick(900.f);
	TestEqual(TEXT("Half day"), Clock->GetElapsedCalendarDays(), 0.5);
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive Save(Writer, false);
	Save.ArIsSaveGame = true;
	Clock->Serialize(Save);
	auto* Restored = World->SpawnActor<AProjectDayCycleStateActor>();
	FMemoryReader Reader(Bytes);
	FObjectAndNameAsStringProxyArchive Load(Reader, false);
	Load.ArIsSaveGame = true;
	Restored->Serialize(Load);
	Restored->OnLoaded_Implementation();
	TestEqual(TEXT("Calendar restored without wall-clock/offline credit"), Restored->GetElapsedCalendarDays(), 0.5);
	Restored->Tick(900.f);
	TestEqual(TEXT("Continues from saved day"), Restored->GetElapsedCalendarDays(), 1.0);
	World->DestroyWorld(false);
	return true;
}
#endif
