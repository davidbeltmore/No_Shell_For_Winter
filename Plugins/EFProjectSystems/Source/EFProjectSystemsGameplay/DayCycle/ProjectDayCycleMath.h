#pragma once

#include "CoreMinimal.h"
#include "DayCycle/ProjectDayCycleTypes.h"

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDayCycleMath
{
	static FProjectDayCycleSnapshot BuildSnapshot(
		int32 InitialDayNumber,
		double ElapsedSeconds,
		float DayLengthSeconds,
		bool bIsRunning = true);
};
