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
#include "DaySequenceSubsystem.h"

AProjectDayCycleStateActor::AProjectDayCycleStateActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetActorHiddenInGame(true);
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick = true;
	// A clock-only native sequence: no sky, lighting or camera tracks.
	SetInitialTimeOfDay(0.f);
	SetTimePerCycle(0.5f);
}

void AProjectDayCycleStateActor::PostInitializeComponents()
{
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
	}
	SetDayLength(24.f);
	SetTimePerCycle(DayLengthSeconds / 3600.f);
	SetInitialTimeOfDay(static_cast<float>(FMath::Frac(ElapsedCalendarDays) * 24.0));
	SetRunDayCycle(bCycleRunning);
	Super::PostInitializeComponents();
}

void AProjectDayCycleStateActor::BeginPlay()
{
	Super::BeginPlay();
	RestoreNativeTime();
	if (auto* NativeClock = GetWorld()->GetSubsystem<UDaySequenceSubsystem>())
		NativeClock->SetDaySequenceActor(this);
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

void AProjectDayCycleStateActor::SequencePlayerUpdated(float CurrentTime, float PreviousTime)
{
	Super::SequencePlayerUpdated(CurrentTime, PreviousTime);
	if (HasAuthority() && bCycleInitialized && !bRestoringNativeTime && FMath::IsFinite(CurrentTime))
	{
		// DaySequence owns all sub-day advancement. Only retain its completed-day
		// epoch for the existing ALS save contract; never integrate DeltaSeconds.
		const float Hours = FMath::Fmod(FMath::Max(0.f, CurrentTime), 24.f);
		double Days = FMath::FloorToDouble(ElapsedCalendarDays);
		if (Hours + KINDA_SMALL_NUMBER < LastNativeTimeHours) Days += 1.0;
		ElapsedCalendarDays = Days + static_cast<double>(Hours) / 24.0;
		LastNativeTimeHours = Hours;
	}
}

void AProjectDayCycleStateActor::RestoreNativeTime()
{
	TGuardValue<bool> Restoring(bRestoringNativeTime, true);
	LastNativeTimeHours = static_cast<float>(FMath::Frac(ElapsedCalendarDays) * 24.0);
	SetTimeOfDay(LastNativeTimeHours);
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
	RestoreNativeTime();
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
