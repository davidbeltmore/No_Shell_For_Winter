#include "Characters/ProjectEnemyLevelSubsystem.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelContextProvider.h"
#include "Characters/ProjectEnemyLevelLogic.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyTargetInfoComponent.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "Data/ACFCharacterInitializerComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ObjectKey.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEnemyLevelSubsystem, Log, All);

namespace ProjectEnemyLevelSubsystemPrivate
{
	static const FName DirectorAssignedLevelTag(TEXT("EF.Calysto.V4.DirectorAssignedLevel"));
	static const FString DirectorLogicalLevelPrefix(TEXT("EF.Calysto.V4.LogicalLevel."));

	enum class EDirectorLevelTagResult : uint8
	{
		Absent,
		Valid,
		Invalid
	};

	static TObjectKey<UObject> MakeObjectKey(const UObject* Object)
	{
		return TObjectKey<UObject>(const_cast<UObject*>(Object));
	}

	static EDirectorLevelTagResult TryResolveDirectorLogicalLevel(const APawn* Pawn, int32& OutLogicalLevel)
	{
		OutLogicalLevel = 0;
		if (!IsValid(Pawn) || !Pawn->ActorHasTag(DirectorAssignedLevelTag))
		{
			return EDirectorLevelTagResult::Absent;
		}

		int32 MatchingLevelTags = 0;
		for (const FName& Tag : Pawn->Tags)
		{
			const FString TagText = Tag.ToString();
			if (!TagText.StartsWith(DirectorLogicalLevelPrefix, ESearchCase::CaseSensitive))
			{
				continue;
			}

			++MatchingLevelTags;
			int64 ParsedLevel = 0;
			const FString LevelText = TagText.RightChop(DirectorLogicalLevelPrefix.Len());
			if (!LexTryParseString(ParsedLevel, *LevelText)
				|| ParsedLevel <= 0
				|| ParsedLevel > static_cast<int64>(MAX_int32))
			{
				return EDirectorLevelTagResult::Invalid;
			}
			OutLogicalLevel = static_cast<int32>(ParsedLevel);
		}

		return MatchingLevelTags == 1 && OutLogicalLevel > 0
			? EDirectorLevelTagResult::Valid
			: EDirectorLevelTagResult::Invalid;
	}

	static bool SetDeferredInitializerLevel(
		UACFCharacterInitializerComponent* Initializer,
		const int32 PhysicalLevel)
	{
		if (!IsValid(Initializer) || PhysicalLevel <= 0 || PhysicalLevel > 100)
		{
			return false;
		}

		FIntProperty* LevelProperty = FindFProperty<FIntProperty>(
			Initializer->GetClass(),
			TEXT("CharacterInitLevel"));
		if (!LevelProperty)
		{
			return false;
		}

		LevelProperty->SetPropertyValue_InContainer(Initializer, PhysicalLevel);
		return Initializer->GetAutoInitLevel() == PhysicalLevel;
	}

	static FString GetWorldMapName(const UWorld* World)
	{
		if (!World)
		{
			return FString();
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		return MapName;
	}
}

void UProjectEnemyLevelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bTargetEnemyClassesPending = !LoadSettings();
	ProcessedActors.Reset();
	PendingActors.Reset();
	SuppressedDirectorActors.Reset();
	PreparedDirectorEnemies.Reset();
	PendingEnemyInitializations.Reset();
	InitializationCohortStartFrame = 0;
	InitializationCohortScheduledCount = 0;
	InitializationCohortCompletedCount = 0;
	InitializationCohortFinalizeAttempts = 0;
	InitializationCohortPeakQueueDepth = 0;
	PendingPreparationCallbacks = 0;
	ContextProviders.Reset();
	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedTargetingFixComponent = nullptr;
	bInitialEnemyScanPending = true;
	bNeedsPlayerMaintenanceTick = true;

	if (UWorld* World = GetWorld(); IsValid(World) && World->IsGameWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	}
}

void UProjectEnemyLevelSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); IsValid(World) && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	DetachFromTrackedPlayerController();
	ActorSpawnedHandle.Reset();
	ContextProviders.Reset();
	TargetEnemyBaseClasses.Reset();
	ProcessedActors.Reset();
	PendingActors.Reset();
	SuppressedDirectorActors.Reset();
	PreparedDirectorEnemies.Reset();
	PendingEnemyInitializations.Reset();
	InitializationCohortStartFrame = 0;
	InitializationCohortScheduledCount = 0;
	InitializationCohortCompletedCount = 0;
	InitializationCohortFinalizeAttempts = 0;
	InitializationCohortPeakQueueDepth = 0;
	PendingPreparationCallbacks = 0;
	bInitialEnemyScanPending = false;
	bNeedsPlayerMaintenanceTick = false;
	bTargetEnemyClassesPending = false;

	Super::Deinitialize();
}

void UProjectEnemyLevelSubsystem::Tick(float DeltaTime)
{
	if (bTargetEnemyClassesPending && LoadSettings())
	{
		bTargetEnemyClassesPending = false;
		bInitialEnemyScanPending = true;
	}

	// Process the queue before the initial scan so actors discovered by that scan
	// retain the old next-frame initialization contract.
	ProcessPendingEnemyInitializations();

	if (bInitialEnemyScanPending && !bTargetEnemyClassesPending)
	{
		ProcessExistingEnemies();
		bInitialEnemyScanPending = false;
	}

	if (bNeedsPlayerMaintenanceTick)
	{
		bNeedsPlayerMaintenanceTick = !TryResolveRuntimeContext();
	}
}

TStatId UProjectEnemyLevelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectEnemyLevelSubsystem, STATGROUP_Tickables);
}

bool UProjectEnemyLevelSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld()
		&& (bInitialEnemyScanPending
			|| bNeedsPlayerMaintenanceTick
			|| bTargetEnemyClassesPending
			|| !PendingEnemyInitializations.IsEmpty());
}

bool UProjectEnemyLevelSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UProjectEnemyLevelSubsystem::RegisterContextProvider(UObject* Provider)
{
	if (!IsValid(Provider) || !Provider->GetClass()->ImplementsInterface(UProjectEnemyLevelContextProvider::StaticClass()))
	{
		return;
	}

	ContextProviders.AddUnique(Provider);
}

void UProjectEnemyLevelSubsystem::UnregisterContextProvider(UObject* Provider)
{
	ContextProviders.Remove(Provider);
}

bool UProjectEnemyLevelSubsystem::IsEnemyInitializationPending(const APawn* Pawn) const
{
	return IsActorPending(Pawn);
}

bool UProjectEnemyLevelSubsystem::PrepareDeferredDirectorEnemy(
	APawn* DeferredPawn,
	const int32 ExpectedLogicalLevel,
	const int32 ExpectedPhysicalLevel,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!IsValid(DeferredPawn))
	{
		OutFailureReason = TEXT("Deferred Director enemy pawn is invalid.");
		return false;
	}

	auto FailClosed = [this, DeferredPawn, &OutFailureReason](const FString& Reason)
	{
		OutFailureReason = Reason;
		CancelQueuedEnemyInitialization(DeferredPawn);
		MarkActorProcessed(DeferredPawn);
		return false;
	};

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(DeferredPawn);
	if (SuppressedDirectorActors.Contains(ActorKey))
	{
		return FailClosed(TEXT("Deferred Director enemy was already rolled back and suppressed."));
	}

	if (DeferredPawn->HasActorBegunPlay())
	{
		return FailClosed(TEXT("PrepareDeferredDirectorEnemy must run before FinishSpawning/BeginPlay."));
	}

	if (DeferredPawn->GetWorld() != GetWorld()
		|| !DeferredPawn->GetWorld()
		|| !DeferredPawn->GetWorld()->IsGameWorld()
		|| DeferredPawn->GetNetMode() == NM_Client
		|| !DeferredPawn->HasAuthority())
	{
		return FailClosed(TEXT("Deferred Director enemy is not in this authoritative game world."));
	}

	if (DeferredPawn->IsTemplate()
		|| DeferredPawn->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
		|| !IsTargetEnemyClass(DeferredPawn->GetClass()))
	{
		return FailClosed(TEXT("Deferred Director enemy is not a configured enemy class."));
	}

	int32 TaggedLogicalLevel = 0;
	const ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult TagResult =
		ProjectEnemyLevelSubsystemPrivate::TryResolveDirectorLogicalLevel(DeferredPawn, TaggedLogicalLevel);
	if (TagResult != ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult::Valid
		|| TaggedLogicalLevel != ExpectedLogicalLevel)
	{
		return FailClosed(FString::Printf(
			TEXT("Deferred Director level tag mismatch: expected %d, tagged %d, tag_state=%d."),
			ExpectedLogicalLevel,
			TaggedLogicalLevel,
			static_cast<int32>(TagResult)));
	}

	const int32 ResolvedPhysicalLevel =
		UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(ExpectedLogicalLevel);
	if (ExpectedLogicalLevel <= 0
		|| ExpectedPhysicalLevel != ResolvedPhysicalLevel
		|| ExpectedPhysicalLevel <= 0
		|| ExpectedPhysicalLevel > 100)
	{
		return FailClosed(FString::Printf(
			TEXT("Deferred Director physical level mismatch: logical=%d expected_physical=%d required=%d."),
			ExpectedLogicalLevel,
			ExpectedPhysicalLevel,
			ResolvedPhysicalLevel));
	}

	if (const FProjectPreparedDirectorEnemyInitialization* Existing =
		PreparedDirectorEnemies.Find(ActorKey))
	{
		if (Existing->LogicalLevel == ExpectedLogicalLevel
			&& Existing->PhysicalLevel == ExpectedPhysicalLevel)
		{
			const UACFCharacterInitializerComponent* ExistingInitializer =
				DeferredPawn->FindComponentByClass<UACFCharacterInitializerComponent>();
			if (ExistingInitializer
				&& ExistingInitializer->GetAutoInitLevel() == ExpectedPhysicalLevel)
			{
				return true;
			}
		}

		return FailClosed(TEXT("Deferred Director enemy was prepared previously with conflicting or mutated level data."));
	}

	UACFCharacterInitializerComponent* Initializer =
		DeferredPawn->FindComponentByClass<UACFCharacterInitializerComponent>();
	if (!Initializer)
	{
		return FailClosed(TEXT("Deferred Director enemy has no UACFCharacterInitializerComponent."));
	}

	if (!ProjectEnemyLevelSubsystemPrivate::SetDeferredInitializerLevel(
		Initializer,
		ExpectedPhysicalLevel))
	{
		return FailClosed(TEXT("Could not set and verify CharacterInitLevel on the deferred Director enemy."));
	}

	const bool bReplacedQueuedInitialization = CancelQueuedEnemyInitialization(DeferredPawn);
	ProcessedActors.Remove(ActorKey);
	FProjectPreparedDirectorEnemyInitialization& Prepared =
		PreparedDirectorEnemies.Add(ActorKey);
	Prepared.Pawn = DeferredPawn;
	Prepared.LogicalLevel = ExpectedLogicalLevel;
	Prepared.PhysicalLevel = ExpectedPhysicalLevel;
	Prepared.bReplacedQueuedInitialization = bReplacedQueuedInitialization;
	return true;
}

bool UProjectEnemyLevelSubsystem::InitializeDirectorEnemySynchronously(
	APawn* Pawn,
	const int32 ExpectedLogicalLevel,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!IsValid(Pawn))
	{
		OutFailureReason = TEXT("Director enemy pawn is invalid.");
		return false;
	}

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Pawn);
	auto FailClosed = [this, Pawn, &OutFailureReason](const FString& Reason)
	{
		OutFailureReason = Reason;
		CancelQueuedEnemyInitialization(Pawn);
		// A failed Director transaction must not silently fall back to the legacy
		// next-frame roll. The bridge will call RollbackDirectorEnemy and destroy it.
		MarkActorProcessed(Pawn);
		TryLogCompletedInitializationCohort();
		return false;
	};

	if (SuppressedDirectorActors.Contains(ActorKey))
	{
		return FailClosed(TEXT("Director enemy was already rolled back and is permanently suppressed."));
	}

	if (ExpectedLogicalLevel <= 0)
	{
		return FailClosed(TEXT("Expected Director logical level must be positive."));
	}

	if (Pawn->GetWorld() != GetWorld() || !Pawn->GetWorld() || !Pawn->GetWorld()->IsGameWorld())
	{
		return FailClosed(TEXT("Director enemy does not belong to this active game world."));
	}

	if (!Pawn->HasActorBegunPlay())
	{
		return FailClosed(TEXT("Director enemy must complete FinishSpawning/BeginPlay before synchronous initialization."));
	}

	if (Pawn->IsTemplate()
		|| Pawn->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
		|| Pawn->GetNetMode() == NM_Client
		|| !Pawn->HasAuthority()
		|| !IsTargetEnemyClass(Pawn->GetClass()))
	{
		return FailClosed(TEXT("Director enemy is not an authoritative configured enemy class."));
	}

	int32 TaggedLogicalLevel = 0;
	const ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult TagResult =
		ProjectEnemyLevelSubsystemPrivate::TryResolveDirectorLogicalLevel(Pawn, TaggedLogicalLevel);
	if (TagResult != ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult::Valid
		|| TaggedLogicalLevel != ExpectedLogicalLevel)
	{
		return FailClosed(FString::Printf(
			TEXT("Director level tag mismatch: expected %d, tagged %d, tag_state=%d."),
			ExpectedLogicalLevel,
			TaggedLogicalLevel,
			static_cast<int32>(TagResult)));
	}

	const FProjectPreparedDirectorEnemyInitialization* Prepared =
		PreparedDirectorEnemies.Find(ActorKey);
	const int32 ExpectedPhysicalLevel =
		UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(ExpectedLogicalLevel);
	if (!Prepared
		|| Prepared->Pawn.Get() != Pawn
		|| Prepared->LogicalLevel != ExpectedLogicalLevel
		|| Prepared->PhysicalLevel != ExpectedPhysicalLevel)
	{
		return FailClosed(TEXT("Director enemy has no matching pre-FinishSpawning preparation token."));
	}

	const UACFCharacterInitializerComponent* Initializer =
		Pawn->FindComponentByClass<UACFCharacterInitializerComponent>();
	if (!Initializer || Initializer->GetAutoInitLevel() != ExpectedPhysicalLevel)
	{
		return FailClosed(FString::Printf(
			TEXT("Director CharacterInitLevel mismatch after BeginPlay: expected %d, observed %d."),
			ExpectedPhysicalLevel,
			Initializer ? Initializer->GetAutoInitLevel() : INDEX_NONE));
	}

	// Idempotence is deliberate: HandleActorSpawned may have completed first,
	// but the materializer must never scale or notify the same pawn twice.
	if (IsActorProcessed(Pawn))
	{
		return ValidateDirectorEnemyInitialization(Pawn, ExpectedLogicalLevel, OutFailureReason);
	}

	bool bReplacedQueuedInitialization = Prepared->bReplacedQueuedInitialization;
	bReplacedQueuedInitialization |= CancelQueuedEnemyInitialization(Pawn);
	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	if (!Settings)
	{
		return FailClosed(TEXT("ProjectEnemyLevel settings are unavailable."));
	}

	UProjectEnemyLevelComponent* LevelComponent = FindOrCreateEnemyLevelComponent(Pawn);
	if (!LevelComponent)
	{
		return FailClosed(TEXT("Could not create the ProjectEnemyLevelComponent."));
	}

	if (LevelComponent->HasAssignedLevel())
	{
		if (LevelComponent->GetAssignedLevel() != ExpectedLogicalLevel)
		{
			return FailClosed(FString::Printf(
				TEXT("Existing logical level %d conflicts with Director level %d."),
				LevelComponent->GetAssignedLevel(),
				ExpectedLogicalLevel));
		}
	}
	else
	{
		const int32 PhysicalLevel = UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(ExpectedLogicalLevel);
		const float NormalizedLevel = FMath::Clamp(
			(static_cast<float>(PhysicalLevel) - 1.0f) / 99.0f,
			0.0f,
			1.0f);
		LevelComponent->SetAssignedLevelData(
			ExpectedLogicalLevel,
			ExpectedLogicalLevel,
			ExpectedLogicalLevel,
			ExpectedLogicalLevel,
			NormalizedLevel);
	}

	FString AscentSyncDiagnostic;
	LevelComponent->SyncAssignedLevelToAscent(AscentSyncDiagnostic);

	FString FailureReason;
	if (!LevelComponent->CaptureGameplayScalingBaseline(*Settings, FailureReason))
	{
		return FailClosed(FailureReason.IsEmpty()
			? TEXT("Could not capture the Director enemy scaling baseline.")
			: FailureReason);
	}

	if (!LevelComponent->ApplyGameplayScaling(*Settings, FailureReason))
	{
		return FailClosed(FailureReason.IsEmpty()
			? TEXT("Could not apply Director enemy gameplay scaling.")
			: FailureReason);
	}

	if (!LevelComponent->EnsurePreferredTargetPoint(*Settings, FailureReason))
	{
		return FailClosed(FailureReason.IsEmpty()
			? TEXT("Could not prepare the Director enemy target point.")
			: FailureReason);
	}

	UProjectEnemyTargetInfoComponent* TargetInfoComponent = FindOrCreateEnemyTargetInfoComponent(Pawn);
	if (!TargetInfoComponent)
	{
		return FailClosed(TEXT("Could not create the ProjectEnemyTargetInfoComponent."));
	}
	TargetInfoComponent->HideTargetInfo();

	MarkActorProcessed(Pawn);
	if (!ValidateDirectorEnemyInitialization(Pawn, ExpectedLogicalLevel, OutFailureReason))
	{
		return false;
	}

	if (bReplacedQueuedInitialization)
	{
		++InitializationCohortCompletedCount;
	}

	UE_LOG(
		LogProjectEnemyLevelSubsystem,
		Verbose,
		TEXT("Synchronously initialized V4 Director enemy %s -> logical=%d physical=%d. %s"),
		*GetNameSafe(Pawn),
		ExpectedLogicalLevel,
		LevelComponent->GetPhysicalAscentLevel(),
		AscentSyncDiagnostic.IsEmpty() ? TEXT("No ARS sync details.") : *AscentSyncDiagnostic);
	TryLogCompletedInitializationCohort();
	return true;
}

bool UProjectEnemyLevelSubsystem::ValidateDirectorEnemyInitialization(
	const APawn* Pawn,
	const int32 ExpectedLogicalLevel,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!IsValid(Pawn) || ExpectedLogicalLevel <= 0)
	{
		OutFailureReason = TEXT("Director enemy validation received an invalid pawn or logical level.");
		return false;
	}

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Pawn);
	if (SuppressedDirectorActors.Contains(ActorKey))
	{
		OutFailureReason = TEXT("Director enemy has been rolled back and suppressed.");
		return false;
	}

	if (Pawn->GetWorld() != GetWorld()
		|| !Pawn->HasActorBegunPlay()
		|| Pawn->IsActorBeingDestroyed())
	{
		OutFailureReason = TEXT("Director enemy is not a live actor in this subsystem's game world.");
		return false;
	}

	int32 TaggedLogicalLevel = 0;
	const ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult TagResult =
		ProjectEnemyLevelSubsystemPrivate::TryResolveDirectorLogicalLevel(Pawn, TaggedLogicalLevel);
	if (TagResult != ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult::Valid
		|| TaggedLogicalLevel != ExpectedLogicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director validation tag mismatch: expected %d, tagged %d."),
			ExpectedLogicalLevel,
			TaggedLogicalLevel);
		return false;
	}

	const int32 ExpectedPhysicalLevel =
		UProjectEnemyLevelComponent::ResolvePhysicalAscentLevel(ExpectedLogicalLevel);
	const FProjectPreparedDirectorEnemyInitialization* Prepared =
		PreparedDirectorEnemies.Find(ActorKey);
	if (!Prepared
		|| Prepared->Pawn.Get() != Pawn
		|| Prepared->LogicalLevel != ExpectedLogicalLevel
		|| Prepared->PhysicalLevel != ExpectedPhysicalLevel)
	{
		OutFailureReason = TEXT("Director enemy has no exact pre-FinishSpawning preparation token.");
		return false;
	}

	const UACFCharacterInitializerComponent* Initializer =
		Pawn->FindComponentByClass<UACFCharacterInitializerComponent>();
	if (!Initializer || Initializer->GetAutoInitLevel() != ExpectedPhysicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director CharacterInitLevel is not exact: expected %d, observed %d."),
			ExpectedPhysicalLevel,
			Initializer ? Initializer->GetAutoInitLevel() : INDEX_NONE);
		return false;
	}

	if (!IsActorProcessed(Pawn) || IsActorPending(Pawn))
	{
		OutFailureReason = TEXT("Director enemy is not finalized or still has queued initialization work.");
		return false;
	}

	for (const FProjectPendingEnemyInitialization& Pending : PendingEnemyInitializations)
	{
		if (Pending.ActorKey == ActorKey)
		{
			OutFailureReason = TEXT("Director enemy still exists in the deferred initialization queue.");
			return false;
		}
	}

	const UProjectEnemyLevelComponent* LevelComponent =
		Pawn->FindComponentByClass<UProjectEnemyLevelComponent>();
	if (!LevelComponent)
	{
		OutFailureReason = TEXT("Director enemy has no ProjectEnemyLevelComponent.");
		return false;
	}

	if (!LevelComponent->ValidateDirectorLevelState(ExpectedLogicalLevel, OutFailureReason))
	{
		return false;
	}

	if (LevelComponent->GetPhysicalAscentLevel() != ExpectedPhysicalLevel)
	{
		OutFailureReason = FString::Printf(
			TEXT("Director enemy physical level mismatch: expected %d, observed %d."),
			ExpectedPhysicalLevel,
			LevelComponent->GetPhysicalAscentLevel());
		return false;
	}

	const UProjectEnemyTargetInfoComponent* TargetInfoComponent =
		Pawn->FindComponentByClass<UProjectEnemyTargetInfoComponent>();
	if (!TargetInfoComponent || !TargetInfoComponent->IsRegistered())
	{
		OutFailureReason = TEXT("Director enemy target-info component is missing or unregistered.");
		return false;
	}

	return true;
}

void UProjectEnemyLevelSubsystem::RollbackDirectorEnemy(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Pawn);
	CancelQueuedEnemyInitialization(Pawn);
	ProcessedActors.Remove(ActorKey);
	PreparedDirectorEnemies.Remove(ActorKey);
	SuppressedDirectorActors.Add(ActorKey);
	TryLogCompletedInitializationCohort();
}

void UProjectEnemyLevelSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	TrackedPlayerPawn = NewPawn;
	TrackedTargetingFixComponent = nullptr;
	MarkMaintenanceRequired();
}

bool UProjectEnemyLevelSubsystem::LoadSettings()
{
	TargetEnemyBaseClasses.Reset();

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	if (!Settings)
	{
		return true;
	}

	bool bAllClassesResolved = true;
	for (const TSoftClassPtr<APawn>& BaseEnemyClass : Settings->TargetEnemyBaseClasses)
	{
		if (BaseEnemyClass.IsNull())
		{
			continue;
		}

		if (UClass* ResolvedClass = BaseEnemyClass.Get())
		{
			TargetEnemyBaseClasses.AddUnique(ResolvedClass);
		}
		else
		{
			bAllClassesResolved = false;
		}
	}

	return bAllClassesResolved;
}

void UProjectEnemyLevelSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	// A target class may have become resident since the previous async-safe pass.
	// Refreshing only queries already loaded classes and never blocks on disk IO.
	if (bTargetEnemyClassesPending && LoadSettings())
	{
		bTargetEnemyClassesPending = false;
		bInitialEnemyScanPending = true;
	}

	APawn* SpawnedPawn = Cast<APawn>(SpawnedActor);
	if (ShouldProcessPawn(SpawnedPawn))
	{
		QueueEnemyInitialization(SpawnedPawn, 0);
	}
}

void UProjectEnemyLevelSubsystem::QueueEnemyInitialization(APawn* Pawn, const int32 AttemptIndex)
{
	if (!IsValid(Pawn) || IsActorProcessed(Pawn) || IsActorPending(Pawn))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	if (PendingActors.IsEmpty())
	{
		InitializationCohortStartFrame = GFrameCounter;
		InitializationCohortScheduledCount = 0;
		InitializationCohortCompletedCount = 0;
		InitializationCohortFinalizeAttempts = 0;
		InitializationCohortPeakQueueDepth = 0;
	}

	MarkActorPending(Pawn);
	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Pawn);
	++InitializationCohortScheduledCount;
	++PendingPreparationCallbacks;
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(
			this,
			&ThisClass::PrepareEnemyInitialization,
			TWeakObjectPtr<APawn>(Pawn),
			ActorKey,
			AttemptIndex));
}

void UProjectEnemyLevelSubsystem::PrepareEnemyInitialization(
	TWeakObjectPtr<APawn> PawnPtr,
	const TObjectKey<UObject> ActorKey,
	const int32 AttemptIndex)
{
	PendingPreparationCallbacks = FMath::Max(PendingPreparationCallbacks - 1, 0);

	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(Pawn))
	{
		PendingActors.Remove(ActorKey);
		TryLogCompletedInitializationCohort();
		return;
	}

	if (!ShouldProcessPawn(Pawn) || IsActorProcessed(Pawn))
	{
		ClearActorPending(Pawn);
		TryLogCompletedInitializationCohort();
		return;
	}

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	const int32 MaxRetryCount = Settings ? FMath::Max(Settings->InitializationRetryCount, 0) : 0;
	auto RetryOrWarn = [this, Pawn, AttemptIndex, MaxRetryCount](const FString& Message)
	{
		ClearActorPending(Pawn);
		if (AttemptIndex < MaxRetryCount)
		{
			QueueEnemyInitialization(Pawn, AttemptIndex + 1);
			return;
		}

		UE_LOG(
			LogProjectEnemyLevelSubsystem,
			Warning,
			TEXT("Enemy level preparation warning for %s after %d attempts: %s"),
			*GetNameSafe(Pawn),
			AttemptIndex + 1,
			*Message);
		MarkActorProcessed(Pawn);
	};

	if (!Settings)
	{
		RetryOrWarn(TEXT("ProjectEnemyLevel settings are unavailable."));
		TryLogCompletedInitializationCohort();
		return;
	}

	UProjectEnemyLevelComponent* LevelComponent = FindOrCreateEnemyLevelComponent(Pawn);
	if (!LevelComponent)
	{
		RetryOrWarn(TEXT("Could not create the ProjectEnemyLevelComponent."));
		TryLogCompletedInitializationCohort();
		return;
	}

	FProjectEnemyLevelRollResult RollResult;
	int32 DirectorLogicalLevel = 0;
	const ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult DirectorLevelResult =
		ProjectEnemyLevelSubsystemPrivate::TryResolveDirectorLogicalLevel(Pawn, DirectorLogicalLevel);
	if (DirectorLevelResult == ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult::Invalid)
	{
		RetryOrWarn(TEXT("The V4 Director level marker is present but its logical-level tag is missing, duplicated, or invalid."));
		TryLogCompletedInitializationCohort();
		return;
	}

	if (DirectorLevelResult == ProjectEnemyLevelSubsystemPrivate::EDirectorLevelTagResult::Valid)
	{
		RollResult.WorldTier = FMath::Max(DirectorLogicalLevel, 1);
		RollResult.MinRolledLevel = DirectorLogicalLevel;
		RollResult.MaxRolledLevel = DirectorLogicalLevel;
		RollResult.AssignedLevel = DirectorLogicalLevel;
		RollResult.NormalizedLevel = FMath::Clamp(
			(static_cast<float>(FMath::Min(DirectorLogicalLevel, 100)) - 1.0f) / 99.0f,
			0.0f,
			1.0f);
	}
	else
	{
		int32 WorldTier = Settings->DefaultWorldTier;
		ResolveWorldTierForWorld(Pawn->GetWorld(), WorldTier);
		const FProjectEnemyLevelContext LevelContext = FProjectEnemyLevelLogic::BuildLevelContext(WorldTier, *Settings);
		if (!FProjectEnemyLevelLogic::RollEnemyLevel(LevelContext, *Settings, RollResult))
		{
			RetryOrWarn(TEXT("Could not roll an enemy level for this world."));
			TryLogCompletedInitializationCohort();
			return;
		}
	}

	// Non-Director actors preserve the old next-tick roll. V4 actors instead
	// consume the immutable level already frozen in FloorIntent.
	LevelComponent->SetAssignedLevelData(
		RollResult.WorldTier,
		RollResult.MinRolledLevel,
		RollResult.MaxRolledLevel,
		RollResult.AssignedLevel,
		RollResult.NormalizedLevel);

	FProjectPendingEnemyInitialization& PendingInitialization = PendingEnemyInitializations.AddDefaulted_GetRef();
	PendingInitialization.Pawn = Pawn;
	PendingInitialization.ActorKey = ActorKey;
	PendingInitialization.AttemptIndex = AttemptIndex;
	PendingInitialization.EligibleFrameNumber = GFrameCounter + 1;
	InitializationCohortPeakQueueDepth = FMath::Max(
		InitializationCohortPeakQueueDepth,
		PendingEnemyInitializations.Num());
}

void UProjectEnemyLevelSubsystem::ProcessPendingEnemyInitializations()
{
	if (PendingEnemyInitializations.IsEmpty())
	{
		return;
	}

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	const int32 MaxInitializationsPerFrame = FMath::Max(
		Settings ? Settings->MaxEnemyInitializationsPerFrame : 1,
		1);
	int32 ReadyCount = 0;
	while (ReadyCount < PendingEnemyInitializations.Num()
		&& ReadyCount < MaxInitializationsPerFrame
		&& PendingEnemyInitializations[ReadyCount].EligibleFrameNumber <= GFrameCounter)
	{
		++ReadyCount;
	}

	if (ReadyCount <= 0)
	{
		return;
	}

	TArray<FProjectPendingEnemyInitialization> CurrentBatch;
	CurrentBatch.Append(PendingEnemyInitializations.GetData(), ReadyCount);
	PendingEnemyInitializations.RemoveAt(0, ReadyCount, EAllowShrinking::No);

	for (const FProjectPendingEnemyInitialization& PendingInitialization : CurrentBatch)
	{
		++InitializationCohortFinalizeAttempts;
		TryInitializeEnemy(
			PendingInitialization.Pawn,
			PendingInitialization.ActorKey,
			PendingInitialization.AttemptIndex);
	}


	TryLogCompletedInitializationCohort();
}

void UProjectEnemyLevelSubsystem::TryInitializeEnemy(
	TWeakObjectPtr<APawn> PawnPtr,
	const TObjectKey<UObject> ActorKey,
	const int32 AttemptIndex)
{
	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(Pawn))
	{
		PendingActors.Remove(ActorKey);
		return;
	}

	ClearActorPending(Pawn);

	if (!ShouldProcessPawn(Pawn) || IsActorProcessed(Pawn))
	{
		return;
	}

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	const int32 MaxRetryCount = FMath::Max(Settings->InitializationRetryCount, 0);

	auto RetryOrWarn = [this, Pawn, AttemptIndex, MaxRetryCount](const FString& Message, const bool bMarkProcessedWhenExhausted)
	{
		if (AttemptIndex < MaxRetryCount)
		{
			QueueEnemyInitialization(Pawn, AttemptIndex + 1);
			return false;
		}

		UE_LOG(
			LogProjectEnemyLevelSubsystem,
			Warning,
			TEXT("Enemy level initialization warning for %s after %d attempts: %s"),
			*GetNameSafe(Pawn),
			AttemptIndex + 1,
			*Message);

		if (bMarkProcessedWhenExhausted)
		{
			MarkActorProcessed(Pawn);
		}

		return true;
	};

	UProjectEnemyLevelComponent* LevelComponent = Pawn->FindComponentByClass<UProjectEnemyLevelComponent>();
	if (!LevelComponent || !LevelComponent->HasAssignedLevel())
	{
		RetryOrWarn(TEXT("The prepared ProjectEnemyLevelComponent or assigned level is unavailable."), true);
		return;
	}

	FString DiagnosticMessage;
	LevelComponent->SyncAssignedLevelToAscent(DiagnosticMessage);

	FString BaselineFailureReason;
	if (!LevelComponent->CaptureGameplayScalingBaseline(*Settings, BaselineFailureReason))
	{
		if (!RetryOrWarn(BaselineFailureReason, false))
		{
			return;
		}
	}

	FString ScalingFailureReason;
	if (!LevelComponent->ApplyGameplayScaling(*Settings, ScalingFailureReason))
	{
		if (!RetryOrWarn(ScalingFailureReason, false))
		{
			return;
		}
	}

	FString TargetPointFailureReason;
	if (!LevelComponent->EnsurePreferredTargetPoint(*Settings, TargetPointFailureReason))
	{
		RetryOrWarn(TargetPointFailureReason, true);
		return;
	}

	UProjectEnemyTargetInfoComponent* TargetInfoComponent = FindOrCreateEnemyTargetInfoComponent(Pawn);
	if (!TargetInfoComponent)
	{
		RetryOrWarn(TEXT("Could not create the ProjectEnemyTargetInfoComponent."), true);
		return;
	}

	TargetInfoComponent->HideTargetInfo();

	UE_LOG(
		LogProjectEnemyLevelSubsystem,
		Verbose,
		TEXT("Initialized enemy %s -> level=%d tier=%d range=[%d,%d]. %s"),
		*GetNameSafe(Pawn),
		LevelComponent->GetAssignedLevel(),
		LevelComponent->GetWorldTier(),
		LevelComponent->GetMinRolledLevel(),
		LevelComponent->GetMaxRolledLevel(),
		DiagnosticMessage.IsEmpty() ? TEXT("No ARS sync details.") : *DiagnosticMessage);

	MarkActorProcessed(Pawn);
	++InitializationCohortCompletedCount;
}

void UProjectEnemyLevelSubsystem::TryLogCompletedInitializationCohort()
{
	if (!PendingActors.IsEmpty()
		|| !PendingEnemyInitializations.IsEmpty()
		|| PendingPreparationCallbacks > 0
		|| InitializationCohortStartFrame == 0)
	{
		return;
	}

	const uint64 FramesElapsed = GFrameCounter >= InitializationCohortStartFrame
		? (GFrameCounter - InitializationCohortStartFrame + 1)
		: 0;
	UE_LOG(
		LogProjectEnemyLevelSubsystem,
		Log,
		TEXT("Project enemy initialization cohort drained: scheduled=%d completed=%d finalize_attempts=%d peak_queue=%d frames=%llu max_per_frame=%d."),
		InitializationCohortScheduledCount,
		InitializationCohortCompletedCount,
		InitializationCohortFinalizeAttempts,
		InitializationCohortPeakQueueDepth,
		static_cast<unsigned long long>(FramesElapsed),
		FMath::Max(UProjectEnemyLevelSettings::Get()->MaxEnemyInitializationsPerFrame, 1));

	InitializationCohortStartFrame = 0;
}

bool UProjectEnemyLevelSubsystem::ShouldProcessPawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->IsTemplate() || Pawn->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	if (const UWorld* World = Pawn->GetWorld())
	{
		if (!World->IsGameWorld())
		{
			return false;
		}
	}

	if (Pawn->GetNetMode() == NM_Client)
	{
		return false;
	}

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Pawn);
	if (SuppressedDirectorActors.Contains(ActorKey)
		|| PreparedDirectorEnemies.Contains(ActorKey))
	{
		return false;
	}

	return IsTargetEnemyClass(Pawn->GetClass());
}

bool UProjectEnemyLevelSubsystem::IsTargetEnemyClass(const UClass* ActorClass) const
{
	if (!IsValid(ActorClass))
	{
		return false;
	}

	for (const TSubclassOf<APawn>& TargetEnemyBaseClass : TargetEnemyBaseClasses)
	{
		if (TargetEnemyBaseClass && ActorClass->IsChildOf(TargetEnemyBaseClass.Get()))
		{
			return true;
		}
	}

	return false;
}

void UProjectEnemyLevelSubsystem::ProcessExistingEnemies()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (ShouldProcessPawn(Pawn))
		{
			QueueEnemyInitialization(Pawn, 0);
		}
	}
}

bool UProjectEnemyLevelSubsystem::ResolveWorldTierForWorld(UWorld* World, int32& OutWorldTier) const
{
	for (const TWeakObjectPtr<UObject>& ProviderPtr : ContextProviders)
	{
		UObject* Provider = ProviderPtr.Get();
		if (!IsValid(Provider) || !Provider->GetClass()->ImplementsInterface(UProjectEnemyLevelContextProvider::StaticClass()))
		{
			continue;
		}

		int32 ProviderTier = 0;
		if (IProjectEnemyLevelContextProvider::Execute_TryResolveEnemyWorldTier(Provider, World, ProviderTier))
		{
			OutWorldTier = FMath::Max(ProviderTier, 1);
			return true;
		}
	}

	return FProjectEnemyLevelLogic::ResolveWorldTierForMapName(
		ProjectEnemyLevelSubsystemPrivate::GetWorldMapName(World),
		*UProjectEnemyLevelSettings::Get(),
		OutWorldTier);
}

UProjectEnemyLevelComponent* UProjectEnemyLevelSubsystem::FindOrCreateEnemyLevelComponent(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	if (UProjectEnemyLevelComponent* ExistingComponent = Pawn->FindComponentByClass<UProjectEnemyLevelComponent>())
	{
		if (!ExistingComponent->IsRegistered())
		{
			ExistingComponent->RegisterComponent();
		}

		return ExistingComponent;
	}

	UProjectEnemyLevelComponent* NewComponent = NewObject<UProjectEnemyLevelComponent>(Pawn, UProjectEnemyLevelComponent::StaticClass(), TEXT("ProjectEnemyLevelComponent"));
	if (!NewComponent)
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(NewComponent);
	NewComponent->OnComponentCreated();
	NewComponent->RegisterComponent();
	NewComponent->Activate(true);
	return NewComponent;
}

UProjectEnemyTargetInfoComponent* UProjectEnemyLevelSubsystem::FindOrCreateEnemyTargetInfoComponent(APawn* Pawn) const
{
	if (!IsValid(Pawn))
	{
		return nullptr;
	}

	if (UProjectEnemyTargetInfoComponent* ExistingComponent = Pawn->FindComponentByClass<UProjectEnemyTargetInfoComponent>())
	{
		if (!ExistingComponent->IsRegistered())
		{
			ExistingComponent->RegisterComponent();
		}

		return ExistingComponent;
	}

	UProjectEnemyTargetInfoComponent* NewComponent = NewObject<UProjectEnemyTargetInfoComponent>(Pawn, UProjectEnemyTargetInfoComponent::StaticClass(), TEXT("ProjectEnemyTargetInfoComponent"));
	if (!NewComponent)
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(NewComponent);
	NewComponent->OnComponentCreated();
	NewComponent->RegisterComponent();
	NewComponent->Activate(true);
	return NewComponent;
}

void UProjectEnemyLevelSubsystem::MarkActorProcessed(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	ProcessedActors.Add(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
	PendingActors.Remove(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
}

bool UProjectEnemyLevelSubsystem::IsActorProcessed(const AActor* Actor) const
{
	return IsValid(Actor) && ProcessedActors.Contains(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
}

bool UProjectEnemyLevelSubsystem::IsActorPending(const AActor* Actor) const
{
	return IsValid(Actor) && PendingActors.Contains(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
}

void UProjectEnemyLevelSubsystem::MarkActorPending(const AActor* Actor)
{
	if (IsValid(Actor))
	{
		PendingActors.Add(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
	}
}

void UProjectEnemyLevelSubsystem::ClearActorPending(const AActor* Actor)
{
	if (IsValid(Actor))
	{
		PendingActors.Remove(ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor));
	}
}

bool UProjectEnemyLevelSubsystem::CancelQueuedEnemyInitialization(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const TObjectKey<UObject> ActorKey = ProjectEnemyLevelSubsystemPrivate::MakeObjectKey(Actor);
	const bool bWasPending = PendingActors.Remove(ActorKey) > 0;
	const int32 RemovedCount = PendingEnemyInitializations.RemoveAll(
		[&ActorKey](const FProjectPendingEnemyInitialization& Pending)
		{
			return Pending.ActorKey == ActorKey;
		});
	return bWasPending || RemovedCount > 0;
}

bool UProjectEnemyLevelSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController();
		return false;
	}

	AttachToPlayerController(PlayerController);
	EnsureTargetingFixComponent(PlayerController->GetPawn());
	return TrackedPlayerController && TrackedPlayerPawn && TrackedTargetingFixComponent;
}

void UProjectEnemyLevelSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		return;
	}

	DetachFromTrackedPlayerController();
	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
}

void UProjectEnemyLevelSubsystem::DetachFromTrackedPlayerController()
{
	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	TrackedTargetingFixComponent = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedPlayerController = nullptr;
}

void UProjectEnemyLevelSubsystem::EnsureTargetingFixComponent(APawn* Pawn)
{
	if (!Pawn)
	{
		TrackedPlayerPawn = nullptr;
		TrackedTargetingFixComponent = nullptr;
		return;
	}

	TrackedPlayerPawn = Pawn;
	UProjectTargetingFixComponent* TargetingFixComponent = Pawn->FindComponentByClass<UProjectTargetingFixComponent>();
	if (!TargetingFixComponent)
	{
		TargetingFixComponent = NewObject<UProjectTargetingFixComponent>(Pawn, UProjectTargetingFixComponent::StaticClass(), TEXT("ProjectTargetingFixComponent"));
		if (TargetingFixComponent)
		{
			Pawn->AddInstanceComponent(TargetingFixComponent);
			TargetingFixComponent->OnComponentCreated();
			TargetingFixComponent->RegisterComponent();
			TargetingFixComponent->Activate(true);
		}
	}

	TrackedTargetingFixComponent = TargetingFixComponent;
}

void UProjectEnemyLevelSubsystem::MarkMaintenanceRequired()
{
	bNeedsPlayerMaintenanceTick = true;
}
