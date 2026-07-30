#include "Characters/ProjectEnemyLevelSubsystem.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelContextProvider.h"
#include "Characters/ProjectEnemyLevelLogic.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyTargetInfoComponent.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEnemyLevelSubsystem, Log, All);

namespace ProjectEnemyLevelSubsystemPrivate
{
	static TObjectKey<UObject> MakeObjectKey(const UObject* Object)
	{
		return TObjectKey<UObject>(const_cast<UObject*>(Object));
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

	LoadSettings();
	ProcessedActors.Reset();
	PendingActors.Reset();
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
	bInitialEnemyScanPending = false;
	bNeedsPlayerMaintenanceTick = false;

	Super::Deinitialize();
}

void UProjectEnemyLevelSubsystem::Tick(float DeltaTime)
{
	if (bInitialEnemyScanPending)
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
	return World && World->IsGameWorld() && (bInitialEnemyScanPending || bNeedsPlayerMaintenanceTick);
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

void UProjectEnemyLevelSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	TrackedPlayerPawn = NewPawn;
	TrackedTargetingFixComponent = nullptr;
	MarkMaintenanceRequired();
}

void UProjectEnemyLevelSubsystem::LoadSettings()
{
	TargetEnemyBaseClasses.Reset();

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	for (const TSoftClassPtr<APawn>& BaseEnemyClass : Settings->TargetEnemyBaseClasses)
	{
		if (UClass* ResolvedClass = BaseEnemyClass.LoadSynchronous())
		{
			TargetEnemyBaseClasses.AddUnique(ResolvedClass);
		}
	}
}

void UProjectEnemyLevelSubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
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

	if (UWorld* World = GetWorld())
	{
		MarkActorPending(Pawn);
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ThisClass::TryInitializeEnemy, TWeakObjectPtr<APawn>(Pawn), AttemptIndex));
	}
}

void UProjectEnemyLevelSubsystem::TryInitializeEnemy(TWeakObjectPtr<APawn> PawnPtr, const int32 AttemptIndex)
{
	APawn* Pawn = PawnPtr.Get();
	if (!IsValid(Pawn))
	{
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

	UProjectEnemyLevelComponent* LevelComponent = FindOrCreateEnemyLevelComponent(Pawn);
	if (!LevelComponent)
	{
		RetryOrWarn(TEXT("Could not create the ProjectEnemyLevelComponent."), true);
		return;
	}

	int32 WorldTier = Settings->DefaultWorldTier;
	ResolveWorldTierForWorld(Pawn->GetWorld(), WorldTier);

	const FProjectEnemyLevelContext LevelContext = FProjectEnemyLevelLogic::BuildLevelContext(WorldTier, *Settings);
	FProjectEnemyLevelRollResult RollResult;
	if (!FProjectEnemyLevelLogic::RollEnemyLevel(LevelContext, *Settings, RollResult))
	{
		RetryOrWarn(TEXT("Could not roll an enemy level for this world."), true);
		return;
	}

	LevelComponent->SetAssignedLevelData(
		RollResult.WorldTier,
		RollResult.MinRolledLevel,
		RollResult.MaxRolledLevel,
		RollResult.AssignedLevel,
		RollResult.NormalizedLevel);

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
		Log,
		TEXT("Initialized enemy %s -> level=%d tier=%d range=[%d,%d]. %s"),
		*GetNameSafe(Pawn),
		RollResult.AssignedLevel,
		RollResult.WorldTier,
		RollResult.MinRolledLevel,
		RollResult.MaxRolledLevel,
		DiagnosticMessage.IsEmpty() ? TEXT("No ARS sync details.") : *DiagnosticMessage);

	MarkActorProcessed(Pawn);
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
