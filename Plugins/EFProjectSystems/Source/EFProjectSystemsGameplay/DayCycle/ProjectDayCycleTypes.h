#pragma once

#include "CoreMinimal.h"
#include "ProjectDayCycleTypes.generated.h"

UENUM(BlueprintType)
enum class EProjectDayPhase : uint8
{
	Morning UMETA(DisplayName = "Morning"),
	Afternoon UMETA(DisplayName = "Afternoon"),
	Night UMETA(DisplayName = "Night")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDayCycleSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	int32 DayNumber = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	int64 FloorNumber = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	float NormalizedDayProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	EProjectDayPhase Phase = EProjectDayPhase::Morning;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	float PhaseProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	float DayLengthSeconds = 600.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Day Cycle")
	bool bIsRunning = false;
};
