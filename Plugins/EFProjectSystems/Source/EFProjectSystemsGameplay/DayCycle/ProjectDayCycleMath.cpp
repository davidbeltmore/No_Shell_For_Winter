#include "DayCycle/ProjectDayCycleMath.h"

FProjectDayCycleSnapshot FProjectDayCycleMath::BuildSnapshot(
	const int32 InitialDayNumber,
	const double ElapsedSeconds,
	const float DayLengthSeconds,
	const bool bIsRunning)
{
	FProjectDayCycleSnapshot Snapshot;
	Snapshot.DayLengthSeconds = FMath::Max(1.0f, DayLengthSeconds);
	Snapshot.bIsRunning = bIsRunning;

	const double SafeElapsedSeconds = FMath::Max(0.0, ElapsedSeconds);
	const double CompletedDays = FMath::FloorToDouble(SafeElapsedSeconds / Snapshot.DayLengthSeconds);
	Snapshot.DayNumber = FMath::Max(1, InitialDayNumber) + FMath::FloorToInt(CompletedDays);

	const double SecondsIntoDay = FMath::Fmod(SafeElapsedSeconds, static_cast<double>(Snapshot.DayLengthSeconds));
	Snapshot.NormalizedDayProgress = FMath::Clamp(
		static_cast<float>(SecondsIntoDay / Snapshot.DayLengthSeconds),
		0.0f,
		1.0f);

	const float PhasePosition = Snapshot.NormalizedDayProgress * 3.0f;
	const int32 PhaseIndex = FMath::Clamp(FMath::FloorToInt(PhasePosition), 0, 2);
	Snapshot.Phase = static_cast<EProjectDayPhase>(PhaseIndex);
	Snapshot.PhaseProgress = FMath::Clamp(PhasePosition - static_cast<float>(PhaseIndex), 0.0f, 1.0f);
	return Snapshot;
}
