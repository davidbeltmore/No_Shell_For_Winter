#include "Calysto/ProjectCalystoFloorOutcomeSubsystem.h"

#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDirectorSubsystem.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Survival/ProjectRealtimeSnapshotComponent.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCalystoFloorOutcome, Log, All);

namespace ProjectCalystoFloorOutcomePrivate
{
	const FName EnemyTag(TEXT("EF.Calysto.Enemy"));
	const FName HealthName(TEXT("Health"));
	const FName HungerName(TEXT("Hunger"));
	const FName ThirstName(TEXT("Thirst"));

	constexpr float NeutralScore = 0.5f;
	constexpr double PaceBaseSeconds = 90.0;
	constexpr double PaceSecondsPerEdgeCell = 3.0;
	constexpr double PaceSecondsPerEnemy = 18.0;
}

void UProjectCalystoFloorOutcomeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UEFCalystoDirectorSettings::IsEnabled())
	{
		Collection.InitializeDependency<UEFCalystoDirectorSubsystem>();
		BoundDirector = GetGameInstance()->GetSubsystem<UEFCalystoDirectorSubsystem>();
		if (UEFCalystoDirectorSubsystem* Director = BoundDirector.Get())
		{
			Director->BeforeTravel.AddUObject(this, &ThisClass::HandleDirectorRequestPreparing);
			Director->FloorReady.AddUObject(this, &ThisClass::HandleDirectorContextReady);
			Director->RequestFailed.AddUObject(this, &ThisClass::HandleDirectorContextFailed);
		}
		return;
	}
	Collection.InitializeDependency<UEFCalystoDungeonSubsystem>();

	UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	DungeonSubsystem = Director;
	if (!Director)
	{
		UE_LOG(
			LogProjectCalystoFloorOutcome,
			Warning,
			TEXT("Dungeon Director V6 telemetry bridge could not resolve its runtime subsystem; outcomes remain neutral."));
		return;
	}

	Director->OnBeforeFloorAdvance().AddUObject(
		this,
		&ThisClass::HandleBeforeFloorAdvance);
	Director->OnFloorReady().AddUObject(this, &ThisClass::HandleFloorReady);
	Director->OnFloorTravelFailed().AddUObject(
		this,
		&ThisClass::HandleFloorTravelFailed);
}

void UProjectCalystoFloorOutcomeSubsystem::Deinitialize()
{
	UnbindTrackedPlayerDeath();
	if (UEFCalystoDirectorSubsystem* Director = BoundDirector.Get())
	{
		Director->BeforeTravel.RemoveAll(this);
		Director->FloorReady.RemoveAll(this);
		Director->RequestFailed.RemoveAll(this);
	}
	BoundDirector.Reset();
	if (UEFCalystoDungeonSubsystem* Director = DungeonSubsystem.Get())
	{
		Director->OnBeforeFloorAdvance().RemoveAll(this);
		Director->OnFloorReady().RemoveAll(this);
		Director->OnFloorTravelFailed().RemoveAll(this);
	}
	DungeonSubsystem.Reset();
	Super::Deinitialize();
}

void UProjectCalystoFloorOutcomeSubsystem::HandleDirectorRequestPreparing(const int64 FloorNumber)
{
	(void)FloorNumber;
	// Observations are not a committed adaptive outcome. Keep them intact while
	// the request may retry and detach the pawn owned by the departing world.
	UnbindTrackedPlayerDeath();
}

void UProjectCalystoFloorOutcomeSubsystem::HandleDirectorContextReady(const FEFCalystoDirectorSnapshot& Snapshot)
{
	if (Snapshot.State != EEFCalystoDirectorState::Ready || !Snapshot.bNativeFloorVerified) return;
	if (TrackedRunEpoch != Snapshot.LastCommittedRunEpoch
		|| TrackedFloorNumber != Snapshot.LastCommittedFloorNumber)
	{
		FloorDeaths = 0;
		FloorFailures = 0;
	}
	TrackedRunSeed = Snapshot.RunSeed;
	TrackedRunEpoch = Snapshot.LastCommittedRunEpoch;
	TrackedFloorNumber = Snapshot.LastCommittedFloorNumber;
	FloorReadySeconds = FPlatformTime::Seconds();
	BindTrackedPlayerDeath();
}

void UProjectCalystoFloorOutcomeSubsystem::HandleDirectorContextFailed(const FEFCalystoDirectorSnapshot& Snapshot)
{
	(void)Snapshot;
	UnbindTrackedPlayerDeath();
	HandleFloorTravelFailed();
}

void UProjectCalystoFloorOutcomeSubsystem::HandleBeforeFloorAdvance(
	const int64 CompletedFloor,
	const FEFCalystoResolvedFloorIntentV6& CompletedIntent)
{
	UEFCalystoDungeonSubsystem* Director = DungeonSubsystem.Get();
	if (!Director || !CompletedIntent.bIsValid
		|| CompletedIntent.GenerationContext.FloorNumber != CompletedFloor)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// Deterministic packaged acceptance fixture: exercising the Director with no
	// telemetry is a supported production semantic and keeps the neutral EMA.
	// Restrict the switch to unattended Development runs so it cannot silently
	// suppress live-player adaptation in an interactive session. Shipping does
	// not compile this branch and therefore continues to submit real outcomes.
	FString PackagedSmokeScenario;
	if (FApp::IsUnattended()
		&& FParse::Param(FCommandLine::Get(), TEXT("CalystoV6PackagedSmoke"))
		&& FParse::Value(
			FCommandLine::Get(),
			TEXT("CalystoV6SmokeScenario="),
			PackagedSmokeScenario)
		&& PackagedSmokeScenario.Equals(TEXT("Natural"), ESearchCase::IgnoreCase)
		&& FParse::Param(FCommandLine::Get(), TEXT("CalystoV6DisableOutcomeTelemetry")))
	{
		UE_LOG(
			LogProjectCalystoFloorOutcome,
			Log,
			TEXT("Suppressed Floor %lld outcome for the unattended Director V6 neutral-telemetry fixture."),
			static_cast<long long>(CompletedFloor));
		return;
	}
#endif

	const FEFCalystoFloorOutcomeV6 Outcome = BuildOutcome(CompletedIntent);
	if (!Director->SubmitFloorOutcome(Outcome))
	{
		UE_LOG(
			LogProjectCalystoFloorOutcome,
			Warning,
			TEXT("Dungeon Director V6 rejected the synchronous Floor %lld outcome; Advance will continue with neutral telemetry."),
			static_cast<long long>(CompletedFloor));
		return;
	}

	UE_LOG(
		LogProjectCalystoFloorOutcome,
		Verbose,
		TEXT("Submitted V6 Floor %lld outcome: Combat=%.3f Survival=%.3f Resources=%.3f Pace=%.3f DeathsAndFailures=%.3f."),
		static_cast<long long>(CompletedFloor),
		Outcome.Combat,
		Outcome.Survival,
		Outcome.Resources,
		Outcome.Pace,
		Outcome.DeathsAndFailures);
}

void UProjectCalystoFloorOutcomeSubsystem::HandleFloorReady(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoRealizedFloorManifestV6& Manifest)
{
	(void)PCGSeed;
	(void)Manifest;
	if (!Intent.bIsValid || Intent.GenerationContext.FloorNumber != FloorNumber)
	{
		return;
	}

	const UEFCalystoDungeonSubsystem* Director = DungeonSubsystem.Get();
	const EEFCalystoDungeonTravelKindV6 TravelKind = Director
		? Director->GetSnapshot().TravelKind
		: EEFCalystoDungeonTravelKindV6::None;
	const bool bNewLogicalFloor = TrackedRunSeed != Intent.GenerationContext.RunSeed
		|| TrackedFloorNumber != FloorNumber
		|| TravelKind == EEFCalystoDungeonTravelKindV6::NewRun
		|| TravelKind == EEFCalystoDungeonTravelKindV6::RestartSameSeed
		|| TravelKind == EEFCalystoDungeonTravelKindV6::DevelopmentJump;
	if (bNewLogicalFloor)
	{
		FloorDeaths = 0;
		FloorFailures = 0;
	}
	TrackedRunSeed = Intent.GenerationContext.RunSeed;
	TrackedFloorNumber = FloorNumber;
	FloorReadySeconds = FPlatformTime::Seconds();
	BindTrackedPlayerDeath();
}

void UProjectCalystoFloorOutcomeSubsystem::HandleFloorTravelFailed()
{
	if (FloorFailures < MAX_int32)
	{
		++FloorFailures;
	}
}

void UProjectCalystoFloorOutcomeSubsystem::HandleTrackedPlayerDeath(AActor* SourceActor)
{
	(void)SourceActor;
	if (FloorDeaths < MAX_int32)
	{
		++FloorDeaths;
	}
}

void UProjectCalystoFloorOutcomeSubsystem::BindTrackedPlayerDeath()
{
	UnbindTrackedPlayerDeath();
	AActor* PlayerPawn = ResolveLocalPlayerPawn();
	UProjectCombatAttributeComponent* Combat = PlayerPawn
		? PlayerPawn->FindComponentByClass<UProjectCombatAttributeComponent>()
		: nullptr;
	if (!Combat)
	{
		return;
	}
	TrackedPlayerCombat = Combat;
	Combat->OnDeath.AddUniqueDynamic(
		this,
		&ThisClass::HandleTrackedPlayerDeath);
}

void UProjectCalystoFloorOutcomeSubsystem::UnbindTrackedPlayerDeath()
{
	if (UProjectCombatAttributeComponent* Combat = TrackedPlayerCombat.Get())
	{
		Combat->OnDeath.RemoveDynamic(
			this,
			&ThisClass::HandleTrackedPlayerDeath);
	}
	TrackedPlayerCombat.Reset();
}

AActor* UProjectCalystoFloorOutcomeSubsystem::ResolveLocalPlayerPawn() const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World
		? World->GetFirstPlayerController()
		: nullptr;
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

FEFCalystoFloorOutcomeV6 UProjectCalystoFloorOutcomeSubsystem::BuildOutcome(
	const FEFCalystoResolvedFloorIntentV6& CompletedIntent) const
{
	using namespace ProjectCalystoFloorOutcomePrivate;
	FEFCalystoFloorOutcomeV6 Outcome;

	int32 InitialEnemyCount = 0;
	if (const UEFCalystoDungeonSubsystem* Director = DungeonSubsystem.Get())
	{
		const FEFCalystoRealizedFloorManifestV6 Manifest =
			Director->GetRealizedFloorManifest();
		if (Manifest.bIsValid
			&& Manifest.RunSeed == CompletedIntent.GenerationContext.RunSeed
			&& Manifest.FloorNumber == CompletedIntent.GenerationContext.FloorNumber
			&& Manifest.GenerationSerial == CompletedIntent.GenerationContext.GenerationSerial)
		{
			for (const FEFCalystoRealizedPopulationActorRecordV6& Actor : Manifest.Actors)
			{
				InitialEnemyCount += Actor.CategoryId.IsEqual(
					TEXT("Enemy"), ENameCase::IgnoreCase) ? 1 : 0;
			}
		}
	}

	Outcome.Combat = SanitizeUnit(
		ComputeCombatScore(InitialEnemyCount, CountAliveDungeonEnemies()));
	Outcome.Survival = SanitizeUnit(ResolveSurvivalScore());
	Outcome.Resources = SanitizeUnit(ResolveResourceScore());
	const double ElapsedSeconds = FloorReadySeconds >= 0.0
		? FPlatformTime::Seconds() - FloorReadySeconds
		: -1.0;
	Outcome.Pace = SanitizeUnit(
		ComputePaceScore(
			ElapsedSeconds,
			CompletedIntent.DungeonSize,
			InitialEnemyCount));
	const int32 DeathAndFailureCount = FMath::Max(FloorDeaths, 0) + FMath::Max(FloorFailures, 0);
	Outcome.DeathsAndFailures = FMath::Clamp(
		static_cast<float>(DeathAndFailureCount) / 3.0f, 0.0f, 1.0f);
	Outcome.OutcomeHash = FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(Outcome);
	return Outcome;
}

int32 UProjectCalystoFloorOutcomeSubsystem::CountAliveDungeonEnemies() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	const AActor* PlayerPawn = ResolveLocalPlayerPawn();
	const UProjectRealtimeSnapshotComponent* RealtimeSnapshot = PlayerPawn
		? PlayerPawn->FindComponentByClass<UProjectRealtimeSnapshotComponent>()
		: nullptr;
	int32 AliveCount = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()
			|| !Actor->ActorHasTag(ProjectCalystoFloorOutcomePrivate::EnemyTag))
		{
			continue;
		}

		if (const UProjectCombatAttributeComponent* Combat =
			Actor->FindComponentByClass<UProjectCombatAttributeComponent>())
		{
			if (Combat->IsDead())
			{
				continue;
			}
		}
		else if (RealtimeSnapshot)
		{
			float CurrentHealth = 0.0f;
			if (RealtimeSnapshot->TryReadActorHealth(Actor, CurrentHealth)
				&& FMath::IsFinite(CurrentHealth)
				&& CurrentHealth <= 0.0f)
			{
				continue;
			}
		}

		if (AliveCount < MAX_int32)
		{
			++AliveCount;
		}
	}
	return AliveCount;
}

float UProjectCalystoFloorOutcomeSubsystem::ResolveSurvivalScore() const
{
	using namespace ProjectCalystoFloorOutcomePrivate;
	AActor* PlayerPawn = ResolveLocalPlayerPawn();
	if (!PlayerPawn)
	{
		return NeutralScore;
	}

	if (const UProjectCombatAttributeComponent* Combat =
		PlayerPawn->FindComponentByClass<UProjectCombatAttributeComponent>())
	{
		if (Combat->HasAttribute(Combat->HealthAttributeName))
		{
			const float Current = Combat->GetAttributeCurrentValue(Combat->HealthAttributeName);
			const float Maximum = Combat->GetAttributeMaxValue(Combat->HealthAttributeName);
			if (FMath::IsFinite(Current) && FMath::IsFinite(Maximum) && Maximum > 0.0f)
			{
				return Current / Maximum;
			}
		}
	}

	if (UProjectRealtimeSnapshotComponent* Snapshot =
		PlayerPawn->FindComponentByClass<UProjectRealtimeSnapshotComponent>())
	{
		float Current = 0.0f;
		float Maximum = 0.0f;
		if (Snapshot->TryReadOwnerResource(HealthName, Current, Maximum)
			&& FMath::IsFinite(Current) && FMath::IsFinite(Maximum) && Maximum > 0.0f)
		{
			return Current / Maximum;
		}
	}
	return NeutralScore;
}

float UProjectCalystoFloorOutcomeSubsystem::ResolveResourceScore() const
{
	using namespace ProjectCalystoFloorOutcomePrivate;
	const AActor* PlayerPawn = ResolveLocalPlayerPawn();
	const UProjectSurvivalNeedsComponent* Needs = PlayerPawn
		? PlayerPawn->FindComponentByClass<UProjectSurvivalNeedsComponent>()
		: nullptr;
	if (!Needs)
	{
		return NeutralScore;
	}

	float Total = 0.0f;
	int32 SampleCount = 0;
	for (const FName NeedName : {HungerName, ThirstName})
	{
		if (!Needs->HasNeed(NeedName))
		{
			continue;
		}
		const float Value = Needs->GetNeedNormalizedValue(NeedName);
		if (FMath::IsFinite(Value))
		{
			Total += FMath::Clamp(Value, 0.0f, 1.0f);
			++SampleCount;
		}
	}
	return SampleCount > 0 ? Total / static_cast<float>(SampleCount) : NeutralScore;
}

float UProjectCalystoFloorOutcomeSubsystem::SanitizeUnit(
	const float Value,
	const float Fallback)
{
	const float SafeFallback = FMath::IsFinite(Fallback)
		? FMath::Clamp(Fallback, 0.0f, 1.0f)
		: ProjectCalystoFloorOutcomePrivate::NeutralScore;
	return FMath::IsFinite(Value)
		? FMath::Clamp(Value, 0.0f, 1.0f)
		: SafeFallback;
}

float UProjectCalystoFloorOutcomeSubsystem::ComputeCombatScore(
	const int32 InitialEnemyCount,
	const int32 AliveEnemyCount)
{
	if (InitialEnemyCount <= 0)
	{
		return ProjectCalystoFloorOutcomePrivate::NeutralScore;
	}
	const int32 SafeAliveCount = FMath::Clamp(AliveEnemyCount, 0, InitialEnemyCount);
	return static_cast<float>(InitialEnemyCount - SafeAliveCount)
		/ static_cast<float>(InitialEnemyCount);
}

float UProjectCalystoFloorOutcomeSubsystem::ComputePaceScore(
	const double ElapsedSeconds,
	const FIntVector& DungeonSize,
	const int32 InitialEnemyCount)
{
	using namespace ProjectCalystoFloorOutcomePrivate;
	if (!FMath::IsFinite(ElapsedSeconds) || ElapsedSeconds < 0.0)
	{
		return NeutralScore;
	}

	const double ExpectedSeconds = PaceBaseSeconds
		+ PaceSecondsPerEdgeCell * static_cast<double>(
			FMath::Max(DungeonSize.X, 0) + FMath::Max(DungeonSize.Y, 0))
		+ PaceSecondsPerEnemy * static_cast<double>(FMath::Max(InitialEnemyCount, 0));
	if (!FMath::IsFinite(ExpectedSeconds) || ExpectedSeconds <= 0.0)
	{
		return NeutralScore;
	}
	return static_cast<float>(
		ExpectedSeconds / (ExpectedSeconds + FMath::Max(ElapsedSeconds, 0.0)));
}

#if WITH_DEV_AUTOMATION_TESTS
float UProjectCalystoFloorOutcomeSubsystem::AutomationCombatScore(
	const int32 InitialEnemyCount,
	const int32 AliveEnemyCount)
{
	return ComputeCombatScore(InitialEnemyCount, AliveEnemyCount);
}

float UProjectCalystoFloorOutcomeSubsystem::AutomationPaceScore(
	const double ElapsedSeconds,
	const FIntVector& DungeonSize,
	const int32 InitialEnemyCount)
{
	return ComputePaceScore(ElapsedSeconds, DungeonSize, InitialEnemyCount);
}
#endif
