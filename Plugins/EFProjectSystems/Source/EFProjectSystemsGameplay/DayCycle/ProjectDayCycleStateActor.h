#pragma once

#include "CoreMinimal.h"
#include "DayCycle/ProjectDayCycleTypes.h"
#include "GameFramework/Info.h"
#include "ALSSavableInterface.h"
#include "ProjectDayCycleStateActor.generated.h"

UCLASS(BlueprintType, NotPlaceable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectDayCycleStateActor : public AInfo, public IALSSavableInterface
{
	GENERATED_BODY()

public:
	AProjectDayCycleStateActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnLoaded_Implementation() override;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	double GetElapsedCalendarDays() const { return ElapsedCalendarDays; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	FProjectDayCycleSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Day Cycle")
	float GetCurrentServerTimeSeconds() const;

private:
	UPROPERTY(Replicated, SaveGame)
	int32 InitialDayNumber = 1;

	UPROPERTY(Replicated)
	float DayLengthSeconds = 1800.0f;

	UPROPERTY(Replicated, SaveGame)
	double ElapsedCalendarDays = 0.0;

	void PublishTravelState();

	UPROPERTY(Replicated)
	float CycleStartServerTimeSeconds = 0.0f;

	UPROPERTY(Replicated)
	bool bCycleRunning = true;

	UPROPERTY(Replicated)
	bool bCycleInitialized = false;
};
