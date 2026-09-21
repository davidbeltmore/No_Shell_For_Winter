#include "Calysto/ProjectCalystoPatrolSubsystem.h"

#include "ACFAIController.h"
#include "Actors/ACFCharacter.h"
#include "Calysto/ProjectCalystoPatrolSettings.h"
#include "Components/ACFAIPatrolComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCalystoPatrol, Log, All);

namespace ProjectCalystoPatrolPrivate
{
	static const FName PopulationActorTag(TEXT("EF.Calysto.Population"));
	static const FName PopulationEnemyTag(TEXT("EF.Calysto.Population.Category.Enemy"));

	const FGameplayTag& GetWaitTag()
	{
		static const FGameplayTag WaitTag = FGameplayTag::RequestGameplayTag(TEXT("AIState.Wait"), false);
		return WaitTag;
	}

	const FGameplayTag& GetPatrolTag()
	{
		static const FGameplayTag PatrolTag = FGameplayTag::RequestGameplayTag(TEXT("AIState.Patrol"), false);
		return PatrolTag;
	}
}

void UProjectCalystoPatrolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ScanAccumulatorSeconds = 0.0f;
	bInvalidStateTagsLogged = false;
	RuntimeStates.Reset();
}

void UProjectCalystoPatrolSubsystem::Deinitialize()
{
	RuntimeStates.Reset();
	ScanAccumulatorSeconds = 0.0f;
	bInvalidStateTagsLogged = false;
	Super::Deinitialize();
}

void UProjectCalystoPatrolSubsystem::Tick(const float DeltaTime)
{
	const UProjectCalystoPatrolSettings* Settings = UProjectCalystoPatrolSettings::Get();
	if (!Settings || !Settings->bEnableDungeonEnemyPatrol || !IsDungeonWorld(*Settings))
	{
		return;
	}

	ScanAccumulatorSeconds += FMath::Max(0.0f, DeltaTime);
	if (ScanAccumulatorSeconds < FMath::Max(0.05f, Settings->ScanIntervalSeconds))
	{
		return;
	}
	ScanAccumulatorSeconds = 0.0f;
	RefreshPatrols(*Settings);
}

TStatId UProjectCalystoPatrolSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectCalystoPatrolSubsystem, STATGROUP_Tickables);
}

bool UProjectCalystoPatrolSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

bool UProjectCalystoPatrolSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectCalystoPatrolSubsystem::IsDungeonWorld(const UProjectCalystoPatrolSettings& Settings) const
{
	const UWorld* World = GetWorld();
	if (!World || Settings.DungeonMapNamePattern.IsEmpty())
	{
		return false;
	}

	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);
	return MapName.Contains(Settings.DungeonMapNamePattern, ESearchCase::IgnoreCase);
}

bool UProjectCalystoPatrolSubsystem::ShouldManagePawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || Pawn->IsActorBeingDestroyed())
	{
		return false;
	}

	if (const AACFCharacter* ACFCharacter = Cast<AACFCharacter>(Pawn))
	{
		if (!ACFCharacter->IsAlive())
		{
			return false;
		}
	}

	return Pawn->ActorHasTag(ProjectCalystoPatrolPrivate::PopulationActorTag)
		&& Pawn->ActorHasTag(ProjectCalystoPatrolPrivate::PopulationEnemyTag);
}

void UProjectCalystoPatrolSubsystem::RefreshPatrols(const UProjectCalystoPatrolSettings& Settings)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	PruneRuntimeState();
	for (TActorIterator<APawn> PawnIt(World); PawnIt; ++PawnIt)
	{
		APawn* Pawn = *PawnIt;
		if (!ShouldManagePawn(Pawn))
		{
			continue;
		}

		FProjectCalystoPatrolRuntimeState& RuntimeState = RuntimeStates.FindOrAdd(Pawn);
		if (World->GetTimeSeconds() < RuntimeState.NextAttemptSeconds)
		{
			continue;
		}
		TryStartOrResumePatrol(Pawn, RuntimeState, Settings);
	}
}

void UProjectCalystoPatrolSubsystem::TryStartOrResumePatrol(
	APawn* Pawn,
	FProjectCalystoPatrolRuntimeState& RuntimeState,
	const UProjectCalystoPatrolSettings& Settings)
{
	UWorld* World = GetWorld();
	if (!World || !ShouldManagePawn(Pawn))
	{
		return;
	}

	const double RetryAt = World->GetTimeSeconds() + FMath::Max(0.05f, Settings.RetryIntervalSeconds);
	AACFAIController* Controller = Cast<AACFAIController>(Pawn->GetController());
	UACFAIPatrolComponent* PatrolComponent = Pawn->FindComponentByClass<UACFAIPatrolComponent>();
	if (!Controller || !PatrolComponent)
	{
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}

	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Movement || Movement->MovementMode == MOVE_None)
		{
			RuntimeState.NextAttemptSeconds = RetryAt;
			return;
		}
	}

	// Path-following compatibility belongs to the enemy, not its Patrol state.
	// A pawn that acquires a target immediately can otherwise miss this setup
	// forever and send combat requests to the wrong Crowd NavData.
	if (Settings.bDisableCrowdSimulationForDungeonPatrol)
	{
		if (UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(Controller->GetPathFollowingComponent()))
		{
			if (CrowdFollowing->IsCrowdSimulationEnabled()
				&& CrowdFollowing->GetStatus() == EPathFollowingStatus::Idle)
			{
				CrowdFollowing->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
				UE_LOG(LogProjectCalystoPatrol, Log,
					TEXT("CALYSTO_V6_PATROL_CROWD_DISABLED pawn=%s controller=%s. ")
					TEXT("Using base path following for patrol and combat of this transient dungeon enemy."),
					*GetNameSafe(Pawn), *GetNameSafe(Controller));
			}
		}
	}

	if (Controller->IsInBattle() || Controller->HasTarget() || Controller->IsExecutingCommand())
	{
		// An external combat/command request can already have replaced our request.
		// Do not call StopMovement here: it could cancel that higher-priority movement.
		if (PatrolComponent->IsPatrolLoopActive())
		{
			PatrolComponent->StopPatrolLoop();
		}
		RuntimeState.bHasPatrolProgressObservation = false;
		RuntimeState.bHasLastMoveDestination = false;
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}

	const FGameplayTag& WaitTag = ProjectCalystoPatrolPrivate::GetWaitTag();
	const FGameplayTag& PatrolTag = ProjectCalystoPatrolPrivate::GetPatrolTag();
	const FGameplayTag CurrentState = Controller->GetAIState();
	if (!WaitTag.IsValid() || !PatrolTag.IsValid()
		|| (CurrentState != WaitTag && CurrentState != PatrolTag))
	{
		if ((!WaitTag.IsValid() || !PatrolTag.IsValid()) && !bInvalidStateTagsLogged)
		{
			UE_LOG(LogProjectCalystoPatrol, Warning,
				TEXT("CALYSTO_V6_PATROL_STATE_TAGS_INVALID wait_valid=%d patrol_valid=%d."),
				WaitTag.IsValid() ? 1 : 0, PatrolTag.IsValid() ? 1 : 0);
			bInvalidStateTagsLogged = true;
		}
		if (PatrolComponent->IsPatrolLoopActive())
		{
			PatrolComponent->StopPatrolLoop();
		}
		RuntimeState.bHasPatrolProgressObservation = false;
		RuntimeState.bHasLastMoveDestination = false;
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}

	auto RequestDirectMove = [&](const FVector& Destination)
	{
		const EPathFollowingRequestResult::Type Result = Controller->MoveToLocation(
			Destination,
			50.0f,
			true,
			true,
			true,
			false,
			nullptr,
			false);
		if (Result == EPathFollowingRequestResult::Failed)
		{
			return false;
		}

		RuntimeState.LastMoveDestination = Destination;
		RuntimeState.LastMoveRequestSeconds = World->GetTimeSeconds();
		RuntimeState.bHasLastMoveDestination = true;
		UE_LOG(LogProjectCalystoPatrol, Log,
			TEXT("CALYSTO_V6_PATROL_MOVE_REQUESTED pawn=%s destination=%s request_result=%d."),
			*GetNameSafe(Pawn), *Destination.ToCompactString(), static_cast<int32>(Result));
		return true;
	};

	if (CurrentState == PatrolTag && PatrolComponent->IsPatrolLoopActive())
	{
		const double Now = World->GetTimeSeconds();
		const FVector CurrentLocation = Pawn->GetActorLocation();
		if (!RuntimeState.bHasPatrolProgressObservation)
		{
			RuntimeState.LastPatrolProgressLocation = CurrentLocation;
			RuntimeState.LastPatrolProgressSeconds = Now;
			RuntimeState.bHasPatrolProgressObservation = true;
		}
		else if (FVector::DistSquared2D(CurrentLocation, RuntimeState.LastPatrolProgressLocation)
			>= FMath::Square(FMath::Max(1.0f, Settings.PatrolProgressDistance)))
		{
			RuntimeState.LastPatrolProgressLocation = CurrentLocation;
			RuntimeState.LastPatrolProgressSeconds = Now;
		}

		const UPathFollowingComponent* PathFollowingComponent = Controller->GetPathFollowingComponent();
		const bool bPathIdle = !PathFollowingComponent ||
			PathFollowingComponent->GetStatus() == EPathFollowingStatus::Idle;
		const FVector CurrentTarget = Controller->GetTargetPointLocationBK();
		const bool bHasNewTarget = !RuntimeState.bHasLastMoveDestination ||
			!CurrentTarget.Equals(RuntimeState.LastMoveDestination, 10.0f);
		if (bPathIdle && bHasNewTarget &&
			(Now - RuntimeState.LastMoveRequestSeconds) >= FMath::Max(0.05f, Settings.ScanIntervalSeconds))
		{
			if (RequestDirectMove(CurrentTarget))
			{
				RuntimeState.NextAttemptSeconds = RetryAt;
				return;
			}

			PatrolComponent->StopPatrolLoop();
			RuntimeState.bHasPatrolProgressObservation = false;
			RuntimeState.bHasLastMoveDestination = false;
			RuntimeState.NextAttemptSeconds = RetryAt;
			return;
		}

		const double StalledForSeconds = Now - RuntimeState.LastPatrolProgressSeconds;
		if (!bPathIdle && StalledForSeconds < FMath::Max(1.0f, Settings.PatrolStallRecoverySeconds))
		{
			RuntimeState.NextAttemptSeconds = RetryAt;
			return;
		}

		if (StalledForSeconds < FMath::Max(1.0f, Settings.PatrolStallRecoverySeconds))
		{
			RuntimeState.NextAttemptSeconds = RetryAt;
			return;
		}

		UE_LOG(LogProjectCalystoPatrol, Warning,
			TEXT("CALYSTO_V6_PATROL_STALL_RECOVERY pawn=%s path_idle=%d stalled_for=%.2f."),
			*GetNameSafe(Pawn), bPathIdle ? 1 : 0, StalledForSeconds);
		PatrolComponent->StopPatrolLoop();
		RuntimeState.bHasPatrolProgressObservation = false;
		RuntimeState.bHasLastMoveDestination = false;
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation ProjectedHome;
	if (!NavigationSystem || !NavigationSystem->ProjectPointToNavigation(
		Pawn->GetActorLocation(),
		ProjectedHome,
		Settings.NavigationProjectionExtent,
		&Pawn->GetNavAgentPropertiesRef()))
	{
		if (!RuntimeState.bNavigationFailureLogged)
		{
			UE_LOG(
				LogProjectCalystoPatrol,
				Warning,
				TEXT("CALYSTO_V6_PATROL_NAV_PENDING pawn=%s agent_radius=%.1f agent_height=%.1f location=%s."),
				*GetNameSafe(Pawn),
				Pawn->GetNavAgentPropertiesRef().AgentRadius,
				Pawn->GetNavAgentPropertiesRef().AgentHeight,
				*Pawn->GetActorLocation().ToCompactString());
			RuntimeState.bNavigationFailureLogged = true;
		}
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}

	if (RuntimeState.bNavigationFailureLogged)
	{
		UE_LOG(LogProjectCalystoPatrol, Log,
			TEXT("CALYSTO_V6_PATROL_NAV_READY pawn=%s home=%s."),
			*GetNameSafe(Pawn), *ProjectedHome.Location.ToCompactString());
		RuntimeState.bNavigationFailureLogged = false;
	}

	Controller->SetHomeLocation(ProjectedHome.Location);
	PatrolComponent->SetPatrolType(EPatrolType::ERandomPoint);
	PatrolComponent->SetRandomPatrolRadius(Settings.RandomPatrolRadius);
	PatrolComponent->SetWaitTimeAtPoint(Settings.WaitTimeAtPointSeconds);
	Controller->SetCurrentAIState(PatrolTag);
	PatrolComponent->StartPatrolLoop(false);
	if (!Controller->TryGoToNextWaypoint())
	{
		PatrolComponent->StopPatrolLoop();
		if (!RuntimeState.bDestinationFailureLogged)
		{
			UE_LOG(LogProjectCalystoPatrol, Warning,
				TEXT("CALYSTO_V6_PATROL_DESTINATION_PENDING pawn=%s home=%s radius=%.0f."),
				*GetNameSafe(Pawn), *ProjectedHome.Location.ToCompactString(), Settings.RandomPatrolRadius);
			RuntimeState.bDestinationFailureLogged = true;
		}
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}
	if (!RequestDirectMove(Controller->GetTargetPointLocationBK()))
	{
		PatrolComponent->StopPatrolLoop();
		if (!RuntimeState.bDestinationFailureLogged)
		{
			UE_LOG(LogProjectCalystoPatrol, Warning,
				TEXT("CALYSTO_V6_PATROL_MOVE_PENDING pawn=%s destination=%s."),
				*GetNameSafe(Pawn), *Controller->GetTargetPointLocationBK().ToCompactString());
			RuntimeState.bDestinationFailureLogged = true;
		}
		RuntimeState.bHasLastMoveDestination = false;
		RuntimeState.NextAttemptSeconds = RetryAt;
		return;
	}

	RuntimeState.LastPatrolProgressLocation = Pawn->GetActorLocation();
	RuntimeState.LastPatrolProgressSeconds = World->GetTimeSeconds();
	RuntimeState.bHasPatrolProgressObservation = true;
	RuntimeState.bDestinationFailureLogged = false;
	RuntimeState.NextAttemptSeconds = RetryAt;
	UE_LOG(LogProjectCalystoPatrol, Log,
		TEXT("CALYSTO_V6_PATROL_STARTED pawn=%s home=%s destination=%s radius=%.0f wait=%.2f."),
		*GetNameSafe(Pawn), *ProjectedHome.Location.ToCompactString(),
		*Controller->GetTargetPointLocationBK().ToCompactString(),
		Settings.RandomPatrolRadius, Settings.WaitTimeAtPointSeconds);
}

void UProjectCalystoPatrolSubsystem::PruneRuntimeState()
{
	for (auto It = RuntimeStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
