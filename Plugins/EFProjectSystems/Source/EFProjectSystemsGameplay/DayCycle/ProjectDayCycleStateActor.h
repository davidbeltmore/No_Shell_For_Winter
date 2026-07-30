#pragma once

#include "CoreMinimal.h"
#include "DayCycle/ProjectDayCycleTypes.h"
#include "GameFramework/Info.h"
#include "ProjectDayCycleStateActor.generated.h"

UCLASS(BlueprintType, NotPlaceable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectDayCycleStateActor : public AInfo
{
	GENERATED_BODY()

public:
	AProjectDayCycleStateActor();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	FProjectDayCycleSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	float GetCurrentServerTimeSeconds() const;

private:
	UPROPERTY(Replicated)
	int32 InitialDayNumber = 1;

	UPROPERTY(Replicated)
	float DayLengthSeconds = 600.0f;

	UPROPERTY(Replicated)
	float CycleStartServerTimeSeconds = 0.0f;

	UPROPERTY(Replicated)
	bool bCycleRunning = true;

	UPROPERTY(Replicated)
	bool bCycleInitialized = false;
};
