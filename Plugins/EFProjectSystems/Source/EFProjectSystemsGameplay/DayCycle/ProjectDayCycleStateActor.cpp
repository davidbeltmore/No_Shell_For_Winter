#include "DayCycle/ProjectDayCycleStateActor.h"

#include "DayCycle/ProjectDayCycleMath.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AProjectDayCycleStateActor::AProjectDayCycleStateActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetActorHiddenInGame(true);
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick = false;
}

void AProjectDayCycleStateActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !bCycleInitialized)
	{
		const UProjectDayCycleSettings* Settings = UProjectDayCycleSettings::Get();
		InitialDayNumber = FMath::Max(1, Settings ? Settings->InitialDayNumber : 1);
		DayLengthSeconds = FMath::Max(1.0f, Settings ? Settings->DayLengthSeconds : 600.0f);
		bCycleRunning = !Settings || Settings->bEnableDayCycle;
		CycleStartServerTimeSeconds = GetCurrentServerTimeSeconds();
		bCycleInitialized = true;
		ForceNetUpdate();
	}
}

void AProjectDayCycleStateActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, InitialDayNumber);
	DOREPLIFETIME(ThisClass, DayLengthSeconds);
	DOREPLIFETIME(ThisClass, CycleStartServerTimeSeconds);
	DOREPLIFETIME(ThisClass, bCycleRunning);
	DOREPLIFETIME(ThisClass, bCycleInitialized);
}

FProjectDayCycleSnapshot AProjectDayCycleStateActor::GetCurrentSnapshot() const
{
	const double ElapsedSeconds = bCycleInitialized && bCycleRunning
		? FMath::Max(0.0, static_cast<double>(GetCurrentServerTimeSeconds() - CycleStartServerTimeSeconds))
		: 0.0;
	return FProjectDayCycleMath::BuildSnapshot(InitialDayNumber, ElapsedSeconds, DayLengthSeconds, bCycleInitialized && bCycleRunning);
}

float AProjectDayCycleStateActor::GetCurrentServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}
