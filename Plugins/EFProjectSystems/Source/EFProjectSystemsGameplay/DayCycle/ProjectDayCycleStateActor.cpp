#include "DayCycle/ProjectDayCycleStateActor.h"

#include "DayCycle/ProjectDayCycleMath.h"
#include "DayCycle/ProjectDayCycleSettings.h"
#include "DayCycle/ProjectDayCycleSubsystem.h"
#include "DayCycle/ProjectDayCycleTravelSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
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
	PrimaryActorTick.bCanEverTick = true;
}

void AProjectDayCycleStateActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !bCycleInitialized)
	{
		const UProjectDayCycleSettings* Settings = UProjectDayCycleSettings::Get();
		InitialDayNumber = FMath::Max(1, Settings ? Settings->InitialDayNumber : 1);
		DayLengthSeconds = FMath::Max(1.0f, Settings ? Settings->DayLengthSeconds : 1800.0f);
		if (UGameInstance* GI = GetGameInstance())
		{
			if (const auto* Travel = GI->GetSubsystem<UProjectDayCycleTravelSubsystem>(); Travel && Travel->bHasState)
			{
				ElapsedCalendarDays = Travel->ElapsedCalendarDays;
				InitialDayNumber = Travel->InitialDayNumber;
			}
		}
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
	DOREPLIFETIME(ThisClass, ElapsedCalendarDays);
}

void AProjectDayCycleStateActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bCycleRunning && !UGameplayStatics::IsGamePaused(this) && FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.f)
	{
		ElapsedCalendarDays += static_cast<double>(DeltaSeconds) / FMath::Max(1.f, DayLengthSeconds);
	}
}

void AProjectDayCycleStateActor::PublishTravelState()
{
	if (HasAuthority() && GetGameInstance())
	{
		auto* Travel = GetGameInstance()->GetSubsystem<UProjectDayCycleTravelSubsystem>();
		Travel->ElapsedCalendarDays = ElapsedCalendarDays;
		Travel->InitialDayNumber = InitialDayNumber;
		Travel->bHasState = true;
	}
}

void AProjectDayCycleStateActor::EndPlay(const EEndPlayReason::Type Reason)
{
	PublishTravelState();
	Super::EndPlay(Reason);
}

void AProjectDayCycleStateActor::OnLoaded_Implementation()
{
	ElapsedCalendarDays = FMath::IsFinite(ElapsedCalendarDays) ? FMath::Max(0.0, ElapsedCalendarDays) : 0.0;
	InitialDayNumber = FMath::Max(1, InitialDayNumber);
	PublishTravelState();
	if (auto* Clock = GetWorld()->GetSubsystem<UProjectDayCycleSubsystem>()) Clock->RequestTimeSpeed(1.f);
	ForceNetUpdate();
}

FProjectDayCycleSnapshot AProjectDayCycleStateActor::GetCurrentSnapshot() const
{
	return FProjectDayCycleMath::BuildSnapshot(InitialDayNumber, ElapsedCalendarDays * DayLengthSeconds,
		DayLengthSeconds, bCycleInitialized && bCycleRunning && !UGameplayStatics::IsGamePaused(this));
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
