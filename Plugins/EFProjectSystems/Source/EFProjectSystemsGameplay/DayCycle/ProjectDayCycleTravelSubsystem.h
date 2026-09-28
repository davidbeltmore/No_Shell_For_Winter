#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectDayCycleTravelSubsystem.generated.h"

// Memory-only handoff between worlds. Disk persistence belongs to the savable clock actor.
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDayCycleTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	double ElapsedCalendarDays = 0.0;
	int32 InitialDayNumber = 1;
	bool bHasState = false;
};
