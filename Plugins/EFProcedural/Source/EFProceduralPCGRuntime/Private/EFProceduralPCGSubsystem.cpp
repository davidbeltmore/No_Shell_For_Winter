#include "EFProceduralPCGSubsystem.h"

#include "AIController.h"
#include "EFProceduralACFU.h"
#include "EFProceduralSettings.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "Components/BrushComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFProceduralPCGRuntime, Log, All);

namespace EFProceduralRuntimePrivate
{
	static FString NormalizeMapName(const FString& PackageName)
	{
		FString ShortMapName = FPackageName::GetShortName(PackageName);
		if (ShortMapName.StartsWith(TEXT("UEDPIE_")))
		{
			TArray<FString> NameParts;
			ShortMapName.ParseIntoArray(NameParts, TEXT("_"), true);
			if (NameParts.Num() >= 3)
			{
				NameParts.RemoveAt(0, 2);
				ShortMapName = FString::Join(NameParts, TEXT("_"));
			}
		}

		return ShortMapName;
	}

	static bool MatchesManagedMapName(const FString& ShortMapName, const FString& ManagedMapName)
	{
		return ShortMapName.Equals(ManagedMapName, ESearchCase::IgnoreCase)
			|| ShortMapName.StartsWith(ManagedMapName + TEXT("_"), ESearchCase::IgnoreCase);
	}

	static bool MatchesAnyHint(const FString& Source, const TArray<FString>& Hints)
	{
		for (const FString& Hint : Hints)
		{
			if (!Hint.IsEmpty() && Source.Contains(Hint, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static bool LooksLikeDungeonEnemyClass(const UClass* ActorClass, const UEFProceduralSettings* Settings)
	{
		if (!IsValid(ActorClass) || !Settings)
		{
			return false;
		}

		const FString ClassPath = ActorClass->GetPathName();
		return MatchesAnyHint(ClassPath, Settings->GetEnemyClassPathHintsResolved())
			|| MatchesAnyHint(ActorClass->GetName(), Settings->GetEnemyClassNameHintsResolved());
	}
}

void UEFProceduralPCGSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UEFProceduralRuntimeSubsystem>();
	Super::Initialize(Collection);

	PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this,
		&UEFProceduralPCGSubsystem::HandlePostWorldInitialization);

	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this,
		&UEFProceduralPCGSubsystem::HandleWorldCleanup);

	if (UEFProceduralRuntimeSubsystem* RuntimeSubsystem = GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>())
	{
		RuntimeSubsystem->RegisterProvider(this);
	}
}

void UEFProceduralPCGSubsystem::Deinitialize()
{
	if (GetGameInstance())
	{
		if (UEFProceduralRuntimeSubsystem* RuntimeSubsystem = GetGameInstance()->GetSubsystem<UEFProceduralRuntimeSubsystem>())
		{
			RuntimeSubsystem->UnregisterProvider(this);
		}
	}

	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);

	for (TPair<TObjectKey<UWorld>, FDungeonRuntimeState>& Pair : RuntimeStates)
	{
		FDungeonRuntimeState& RuntimeState = Pair.Value;
		if (AActor* DungeonActor = RuntimeState.DungeonActor.Get())
		{
			TArray<UPCGComponent*> PCGComponents;
			DungeonActor->GetComponents(PCGComponents);
			for (UPCGComponent* PCGComponent : PCGComponents)
			{
				if (!IsValid(PCGComponent))
				{
					continue;
				}

				PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
				PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
				PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
			}

			UNavigationSystemV1::UnregisterNavigationInvoker(*DungeonActor);
		}
	}

	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (UWorld* World = WorldContext.World())
			{
				if (FDelegateHandle* SpawnHandle = ActorSpawnHandles.Find(TObjectKey<UWorld>(World)))
				{
					World->RemoveOnActorSpawnedHandler(*SpawnHandle);
				}
			}
		}
	}

	RuntimeStates.Reset();
	ActorSpawnHandles.Reset();

	Super::Deinitialize();
}

bool UEFProceduralPCGSubsystem::IsManagedRuntimeWorld(const UWorld* World) const
{
	if (!IsValid(World) || !World->IsGameWorld())
	{
		return false;
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	const FString WorldPackageName = World->GetPackage()->GetName();
	const FString ShortMapName = EFProceduralRuntimePrivate::NormalizeMapName(WorldPackageName);

	for (const FString& MapName : Settings->GetManagedMapNamesResolved())
	{
		if (EFProceduralRuntimePrivate::MatchesManagedMapName(ShortMapName, MapName))
		{
			if (!ShortMapName.Equals(MapName, ESearchCase::IgnoreCase))
			{
				const TObjectKey<UWorld> WorldKey(World);
				if (!WarnedDerivedMapWorlds.Contains(WorldKey))
				{
					WarnedDerivedMapWorlds.Add(WorldKey);
					UE_LOG(
						LogEFProceduralPCGRuntime,
						Warning,
						TEXT("World %s matched managed runtime family %s via prefix. Consider moving inspection maps out of the runtime map folder."),
						*ShortMapName,
						*MapName);
				}
			}

			return true;
		}
	}

	return false;
}

bool UEFProceduralPCGSubsystem::IsDungeonRuntimeReady(UWorld* World)
{
	return IsLevelRuntimeReady(World);
}

bool UEFProceduralPCGSubsystem::IsLevelRuntimeReady(UWorld* World)
{
	if (!IsManagedRuntimeWorld(World))
	{
		return true;
	}

	if (FDungeonRuntimeState* RuntimeState = FindRuntimeState(World))
	{
		RefreshDungeonRuntimeState(World, *RuntimeState);
		return RuntimeState->bDungeonReady;
	}

	return false;
}

bool UEFProceduralPCGSubsystem::ResolvePlayerStartTransform(UWorld* World, FTransform& OutStartTransform) const
{
	if (AActor* StartPointActor = FindDungeonStartPoint(World))
	{
		OutStartTransform = StartPointActor->GetActorTransform();
		OutStartTransform.AddToTranslation(FVector(0.0f, 0.0f, 90.0f));
		return true;
	}

	return ResolveDungeonFallbackStartTransform(World, OutStartTransform);
}

bool UEFProceduralPCGSubsystem::ShouldPostProcessSpawnedActor(const AActor* SpawnedActor) const
{
	return ShouldFixSpawnedPawn(Cast<APawn>(SpawnedActor));
}

void UEFProceduralPCGSubsystem::PostProcessSpawnedActor(AActor* SpawnedActor)
{
	if (APawn* Pawn = Cast<APawn>(SpawnedActor))
	{
		TrySanitizeSpawnedPawn(Pawn, 0);
	}
}

void UEFProceduralPCGSubsystem::HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	if (!IsManagedRuntimeWorld(World))
	{
		return;
	}

	const TObjectKey<UWorld> WorldKey(World);
	if (!ActorSpawnHandles.Contains(WorldKey))
	{
		const FDelegateHandle SpawnHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::HandleWorldActorSpawned));
		ActorSpawnHandles.Add(WorldKey, SpawnHandle);
	}

	UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("Scheduling runtime dungeon bootstrap for world %s."), *World->GetName());
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::BootstrapDungeon, TWeakObjectPtr<UWorld>(World), 0));
}

void UEFProceduralPCGSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!IsValid(World))
	{
		return;
	}

	const TObjectKey<UWorld> WorldKey(World);
	WarnedDerivedMapWorlds.Remove(WorldKey);
	WarnedMissingDungeonConfigWorlds.Remove(WorldKey);
	if (FDelegateHandle* SpawnHandle = ActorSpawnHandles.Find(WorldKey))
	{
		World->RemoveOnActorSpawnedHandler(*SpawnHandle);
		ActorSpawnHandles.Remove(WorldKey);
	}

	if (FDungeonRuntimeState* RuntimeState = RuntimeStates.Find(WorldKey))
	{
		if (AActor* DungeonActor = RuntimeState->DungeonActor.Get())
		{
			TArray<UPCGComponent*> PCGComponents;
			DungeonActor->GetComponents(PCGComponents);
			for (UPCGComponent* PCGComponent : PCGComponents)
			{
				if (!IsValid(PCGComponent))
				{
					continue;
				}

				PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
				PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
				PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
			}

			UNavigationSystemV1::UnregisterNavigationInvoker(*DungeonActor);
		}

		RuntimeStates.Remove(WorldKey);
	}
}

void UEFProceduralPCGSubsystem::BootstrapDungeon(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex)
{
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();

	if (!WorldPtr.IsValid())
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	if (!IsManagedRuntimeWorld(World))
	{
		return;
	}

	FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
	RuntimeState.World = World;
	RuntimeState.bBootstrapStarted = true;
	if (RuntimeState.BootstrapStartTimeSeconds <= 0.0)
	{
		RuntimeState.BootstrapStartTimeSeconds = World->GetTimeSeconds();
	}

	if (AActor* ExistingDungeon = FindDungeonActor(World))
	{
		UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("Found existing dungeon actor %s. Runtime bootstrap will reuse it without re-randomizing."), *ExistingDungeon->GetName());
		TrackDungeonActor(World, ExistingDungeon, false);
		return;
	}

	const TSoftClassPtr<AActor> DungeonClassPtr = Settings->GetDungeonActorClassResolved();
	if (DungeonClassPtr.IsNull())
	{
		const TObjectKey<UWorld> WorldKey(World);
		if (!WarnedMissingDungeonConfigWorlds.Contains(WorldKey))
		{
			WarnedMissingDungeonConfigWorlds.Add(WorldKey);
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Warning,
				TEXT("Skipping runtime dungeon bootstrap for world %s because no DungeonActorClass is configured. Configure [/Script/EFProceduralRuntime.EFProceduralSettings] in DefaultGame.ini or assign a ProjectPreset."),
				*World->GetName());
		}
		return;
	}

	UClass* DungeonClass = DungeonClassPtr.LoadSynchronous();
	if (!IsValid(DungeonClass))
	{
		if (AttemptIndex < Settings->MaxDungeonBootstrapAttempts)
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(
				RetryHandle,
				FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::BootstrapDungeon, WorldPtr, AttemptIndex + 1),
				Settings->DungeonBootstrapRetrySeconds,
				false);
		}
		else
		{
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("Failed to load dungeon class after %d attempts."), AttemptIndex);
		}

		return;
	}

	RuntimeState = FDungeonRuntimeState();
	RuntimeState.World = World;
	RuntimeState.bBootstrapStarted = true;
	RuntimeState.BootstrapStartTimeSeconds = World->GetTimeSeconds();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = TEXT("BP_MassiveDungeonRuntime");

	AActor* DungeonActor = World->SpawnActor<AActor>(DungeonClass, FTransform::Identity, SpawnParameters);
	if (!IsValid(DungeonActor))
	{
		if (AttemptIndex < Settings->MaxDungeonBootstrapAttempts)
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(
				RetryHandle,
				FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::BootstrapDungeon, WorldPtr, AttemptIndex + 1),
				Settings->DungeonBootstrapRetrySeconds,
				false);
		}
		else
		{
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("Failed to spawn dungeon actor after %d attempts."), AttemptIndex);
		}

		return;
	}

	UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("Spawned runtime dungeon actor %s."), *DungeonActor->GetName());
	TrackDungeonActor(World, DungeonActor, true);
	RandomizeAndGenerateDungeon(DungeonActor);
	RefreshDungeonRuntimeState(World, FindOrAddRuntimeState(World));
}

AActor* UEFProceduralPCGSubsystem::FindDungeonActor(UWorld* World) const
{
	if (!IsValid(World))
	{
		return nullptr;
	}

	UClass* DungeonClass = UEFProceduralSettings::Get()->GetDungeonActorClassResolved().LoadSynchronous();

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (IsValid(DungeonClass) && Actor->IsA(DungeonClass))
		{
			return Actor;
		}
	}

	return nullptr;
}

void UEFProceduralPCGSubsystem::TrackDungeonActor(UWorld* World, AActor* DungeonActor, bool bGenerationTriggered)
{
	if (!IsValid(World) || !IsValid(DungeonActor))
	{
		return;
	}

	FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
	RuntimeState.World = World;
	RuntimeState.DungeonActor = DungeonActor;
	RuntimeState.bBootstrapStarted = true;
	if (RuntimeState.BootstrapStartTimeSeconds <= 0.0)
	{
		RuntimeState.BootstrapStartTimeSeconds = World->GetTimeSeconds();
	}

	RuntimeState.bPCGGenerationTriggered = RuntimeState.bPCGGenerationTriggered || bGenerationTriggered;
	RuntimeState.bPCGGenerationFinished = false;
	RuntimeState.bNavBoundsCreated = false;
	RuntimeState.bNavigationBuildRequested = false;
	RuntimeState.bNavigationReady = false;
	RuntimeState.bSpawnedPawnsRevalidatedAfterNav = false;
	RuntimeState.bDungeonReady = false;
	RuntimeState.PendingPCGComponents.Reset();

	TArray<UPCGComponent*> PCGComponents;
	DungeonActor->GetComponents(PCGComponents);
	for (UPCGComponent* PCGComponent : PCGComponents)
	{
		if (!IsValid(PCGComponent))
		{
			continue;
		}

		PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);

		PCGComponent->OnPCGGraphGeneratedDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentGenerated);
		PCGComponent->OnPCGGraphCancelledDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentCancelled);
		PCGComponent->OnPCGGraphCleanedDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentCleaned);

		if (bGenerationTriggered || PCGComponent->IsGenerating())
		{
			RuntimeState.PendingPCGComponents.Add(TObjectKey<UPCGComponent>(PCGComponent));
		}
	}

	if (PCGComponents.Num() == 0)
	{
		RuntimeState.bPCGGenerationTriggered = true;
	}

	RefreshDungeonRuntimeState(World, RuntimeState);
}

void UEFProceduralPCGSubsystem::RandomizeAndGenerateDungeon(AActor* DungeonActor)
{
	if (!IsValid(DungeonActor))
	{
		return;
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	const int32 DefaultPCGSeed = Settings->DefaultPCGSeed;

	const bool bRandomizedByBlueprint = TryInvokeActorFunction(DungeonActor, Settings->DungeonRandomizeFunctionNames);

	bool bAnyPCGSeedChanged = false;
	TArray<UPCGComponent*> PCGComponents;
	DungeonActor->GetComponents(PCGComponents);
	for (const UPCGComponent* PCGComponent : PCGComponents)
	{
		if (IsValid(PCGComponent) && PCGComponent->Seed != DefaultPCGSeed)
		{
			bAnyPCGSeedChanged = true;
			break;
		}
	}

	if (!bRandomizedByBlueprint || !bAnyPCGSeedChanged)
	{
		RandomizePCGComponentSeeds(DungeonActor, FMath::Rand());
	}

	const bool bRefreshedByBlueprint = TryInvokeActorFunction(DungeonActor, Settings->DungeonRefreshFunctionNames);

	if (!bRefreshedByBlueprint)
	{
		GeneratePCGComponents(DungeonActor);
	}
}

bool UEFProceduralPCGSubsystem::TryInvokeActorFunction(AActor* Actor, TConstArrayView<FName> CandidateNames) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	for (const FName& CandidateName : CandidateNames)
	{
		if (UFunction* Function = Actor->FindFunction(CandidateName))
		{
			Actor->ProcessEvent(Function, nullptr);
			UE_LOG(LogEFProceduralPCGRuntime, Verbose, TEXT("Called %s on %s."), *CandidateName.ToString(), *Actor->GetName());
			return true;
		}
	}

	return false;
}

void UEFProceduralPCGSubsystem::RandomizePCGComponentSeeds(AActor* DungeonActor, int32 BaseSeed) const
{
	TArray<UPCGComponent*> PCGComponents;
	DungeonActor->GetComponents(PCGComponents);

	for (int32 ComponentIndex = 0; ComponentIndex < PCGComponents.Num(); ++ComponentIndex)
	{
		UPCGComponent* PCGComponent = PCGComponents[ComponentIndex];
		if (!IsValid(PCGComponent))
		{
			continue;
		}

		PCGComponent->Seed = BaseSeed + (ComponentIndex * 9973);
		PCGComponent->NotifyPropertiesChangedFromBlueprint();
	}
}

void UEFProceduralPCGSubsystem::GeneratePCGComponents(AActor* DungeonActor) const
{
	TArray<UPCGComponent*> PCGComponents;
	DungeonActor->GetComponents(PCGComponents);

	for (UPCGComponent* PCGComponent : PCGComponents)
	{
		if (!IsValid(PCGComponent))
		{
			continue;
		}

		PCGComponent->GenerateLocal(true);
	}
}

UEFProceduralPCGSubsystem::FDungeonRuntimeState& UEFProceduralPCGSubsystem::FindOrAddRuntimeState(UWorld* World)
{
	return RuntimeStates.FindOrAdd(TObjectKey<UWorld>(World));
}

UEFProceduralPCGSubsystem::FDungeonRuntimeState* UEFProceduralPCGSubsystem::FindRuntimeState(UWorld* World)
{
	return IsValid(World) ? RuntimeStates.Find(TObjectKey<UWorld>(World)) : nullptr;
}

void UEFProceduralPCGSubsystem::RefreshDungeonRuntimeState(UWorld* World, FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World))
	{
		return;
	}

	RuntimeState.SpawnedPawns.RemoveAllSwap([](const TWeakObjectPtr<APawn>& PawnPtr)
	{
		return !PawnPtr.IsValid();
	});

	if (!RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bPCGGenerationTriggered
		&& RuntimeState.PendingPCGComponents.IsEmpty())
	{
		RuntimeState.bPCGGenerationFinished = true;
		UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("PCG generation finished for world %s. Preparing runtime navigation."), *World->GetName());
		EnsureRuntimeNavigation(World, RuntimeState);
	}

	if (RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bNavigationBuildRequested
		&& !RuntimeState.bNavigationReady)
	{
		if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			if (!NavigationSystem->IsNavigationBuildInProgress())
			{
				RuntimeState.bNavigationReady = true;
				UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("Runtime navigation finished building for world %s."), *World->GetName());
			}
		}
	}

	if (RuntimeState.bNavigationReady && !RuntimeState.bSpawnedPawnsRevalidatedAfterNav)
	{
		RuntimeState.bSpawnedPawnsRevalidatedAfterNav = true;
		RevalidateSpawnedPawns(World);
	}

	RuntimeState.bDungeonReady = RuntimeState.bPCGGenerationFinished
		&& (RuntimeState.bNavigationReady || !RuntimeState.bNavigationBuildRequested);
}

void UEFProceduralPCGSubsystem::HandlePCGComponentGenerated(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent);
}

void UEFProceduralPCGSubsystem::HandlePCGComponentCancelled(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent);
}

void UEFProceduralPCGSubsystem::HandlePCGComponentCleaned(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent);
}

void UEFProceduralPCGSubsystem::HandleTrackedPCGComponentComplete(UPCGComponent* PCGComponent)
{
	if (!IsValid(PCGComponent))
	{
		return;
	}

	if (UWorld* World = PCGComponent->GetWorld())
	{
		if (FDungeonRuntimeState* RuntimeState = FindRuntimeState(World))
		{
			const int32 RemovedCount = RuntimeState->PendingPCGComponents.Remove(TObjectKey<UPCGComponent>(PCGComponent));
			if (RemovedCount > 0)
			{
				RefreshDungeonRuntimeState(World, *RuntimeState);
			}
		}
	}
}

bool UEFProceduralPCGSubsystem::EnsureRuntimeNavigation(UWorld* World, FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World))
	{
		return false;
	}

	if (RuntimeState.bNavigationBuildRequested && RuntimeState.bNavBoundsCreated)
	{
		return true;
	}

	const FBox DungeonBounds = CollectDungeonBounds(World, RuntimeState);
	if (!DungeonBounds.IsValid)
	{
		UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Unable to derive runtime dungeon bounds in world %s. Navigation preparation will retry while loading waits."), *World->GetName());
		return false;
	}

	if (!EnsureNavMeshBoundsVolume(World, DungeonBounds, RuntimeState))
	{
		UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Failed to create/update runtime NavMeshBoundsVolume in world %s."), *World->GetName());
		return false;
	}

	if (AActor* DungeonActor = RuntimeState.DungeonActor.Get())
	{
		RegisterDungeonNavigationInvoker(DungeonActor, DungeonBounds);
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Unable to find UNavigationSystemV1 in world %s after preparing runtime nav bounds."), *World->GetName());
		return false;
	}

	if (ANavMeshBoundsVolume* NavBoundsVolume = RuntimeState.NavBoundsVolume.Get())
	{
		NavigationSystem->OnNavigationBoundsUpdated(NavBoundsVolume);
	}

	NavigationSystem->Build();
	RuntimeState.bNavigationBuildRequested = true;
	RuntimeState.bNavigationReady = !NavigationSystem->IsNavigationBuildInProgress();
	return true;
}

FBox UEFProceduralPCGSubsystem::CollectDungeonBounds(UWorld* World, const FDungeonRuntimeState& RuntimeState) const
{
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	FBox DungeonBounds(ForceInit);

	AActor* DungeonActor = RuntimeState.DungeonActor.Get();
	if (IsValid(DungeonActor))
	{
		const FBox ActorBounds = DungeonActor->GetComponentsBoundingBox(true);
		if (ActorBounds.IsValid)
		{
			DungeonBounds += ActorBounds;
		}
		else
		{
			DungeonBounds += DungeonActor->GetActorLocation();
		}
	}

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}

		bool bIncludeActor = (Actor == DungeonActor);
		if (!bIncludeActor && Actor->ActorHasTag(Settings->GeneratedActorTag))
		{
			bIncludeActor = true;
		}

		if (!bIncludeActor)
		{
			const FString ActorName = Actor->GetName();
			const FString ClassName = Actor->GetClass()->GetName();
			bIncludeActor = ActorName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase)
				|| ClassName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase);
		}

		if (!bIncludeActor)
		{
			if (const APawn* Pawn = Cast<APawn>(Actor))
			{
				bIncludeActor = ShouldFixSpawnedPawn(Pawn);
			}
		}

		if (!bIncludeActor)
		{
			continue;
		}

		const FBox ActorBounds = Actor->GetComponentsBoundingBox(true);
		if (ActorBounds.IsValid)
		{
			DungeonBounds += ActorBounds;
		}
		else
		{
			DungeonBounds += Actor->GetActorLocation();
		}
	}

	if (!DungeonBounds.IsValid)
	{
		return DungeonBounds;
	}

	FVector ExpandedExtent = DungeonBounds.GetExtent();
	ExpandedExtent.X = FMath::Max(ExpandedExtent.X + Settings->NavigationBoundsXYPadding, Settings->MinimumNavigationBoundsExtentXY);
	ExpandedExtent.Y = FMath::Max(ExpandedExtent.Y + Settings->NavigationBoundsXYPadding, Settings->MinimumNavigationBoundsExtentXY);
	ExpandedExtent.Z = FMath::Max(ExpandedExtent.Z + Settings->NavigationBoundsZPadding, Settings->MinimumNavigationBoundsExtentZ);

	const FVector Center = DungeonBounds.GetCenter();
	return FBox(Center - ExpandedExtent, Center + ExpandedExtent);
}

bool UEFProceduralPCGSubsystem::EnsureNavMeshBoundsVolume(UWorld* World, const FBox& DungeonBounds, FDungeonRuntimeState& RuntimeState)
{
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();

	if (!IsValid(World) || !DungeonBounds.IsValid)
	{
		return false;
	}

	ANavMeshBoundsVolume* NavBoundsVolume = RuntimeState.NavBoundsVolume.Get();
	if (!IsValid(NavBoundsVolume))
	{
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			if (It->GetFName() == Settings->RuntimeNavMeshVolumeName)
			{
				NavBoundsVolume = *It;
				RuntimeState.NavBoundsVolume = NavBoundsVolume;
				break;
			}
		}
	}

	const FVector DesiredLocation = DungeonBounds.GetCenter();
	const FVector DesiredScale = Settings->RuntimeNavMeshVolumeScale;
	if (!IsValid(NavBoundsVolume))
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, DesiredLocation, DesiredScale);
		NavBoundsVolume = World->SpawnActorDeferred<ANavMeshBoundsVolume>(
			ANavMeshBoundsVolume::StaticClass(),
			SpawnTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(NavBoundsVolume))
		{
			return false;
		}

		if (UBrushComponent* BrushComponent = NavBoundsVolume->GetBrushComponent())
		{
			BrushComponent->SetMobility(EComponentMobility::Movable);
			BrushComponent->SetGenerateOverlapEvents(false);
		}

		NavBoundsVolume->FinishSpawning(SpawnTransform);
		RuntimeState.NavBoundsVolume = NavBoundsVolume;
	}

	if (UBrushComponent* BrushComponent = NavBoundsVolume->GetBrushComponent())
	{
		if (BrushComponent->Mobility != EComponentMobility::Movable)
		{
			BrushComponent->SetMobility(EComponentMobility::Movable);
		}

		BrushComponent->SetGenerateOverlapEvents(false);
		BrushComponent->UpdateBounds();
	}

	NavBoundsVolume->SetActorRotation(FRotator::ZeroRotator);
	NavBoundsVolume->SetActorLocation(DesiredLocation, false, nullptr, ETeleportType::TeleportPhysics);
	NavBoundsVolume->SetActorScale3D(DesiredScale);
	NavBoundsVolume->SetActorHiddenInGame(true);

	if (UBrushComponent* BrushComponent = NavBoundsVolume->GetBrushComponent())
	{
		BrushComponent->UpdateBounds();
		BrushComponent->MarkRenderTransformDirty();
	}

	RuntimeState.bNavBoundsCreated = true;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("Runtime NavMeshBoundsVolume ready in world %s at %s with scale X=%.1f Y=%.1f Z=%.1f."),
		*World->GetName(),
		*DesiredLocation.ToCompactString(),
		DesiredScale.X,
		DesiredScale.Y,
		DesiredScale.Z);
	return true;
}

void UEFProceduralPCGSubsystem::RegisterDungeonNavigationInvoker(AActor* DungeonActor, const FBox& DungeonBounds) const
{
	if (!IsValid(DungeonActor) || !DungeonBounds.IsValid)
	{
		return;
	}

	const float MinimumRadius = UEFProceduralSettings::Get()->MinimumNavigationInvokerRadius;
	const FVector BoundsExtent = DungeonBounds.GetExtent();
	const float GenerationRadius = FMath::Max(BoundsExtent.Size2D() + 600.0f, MinimumRadius);
	const float RemovalRadius = GenerationRadius + 1200.0f;
	UNavigationSystemV1::RegisterNavigationInvoker(*DungeonActor, GenerationRadius, RemovalRadius);
}

void UEFProceduralPCGSubsystem::RevalidateSpawnedPawns(UWorld* World)
{
	if (FDungeonRuntimeState* RuntimeState = FindRuntimeState(World))
	{
		for (const TWeakObjectPtr<APawn>& PawnPtr : RuntimeState->SpawnedPawns)
		{
			if (PawnPtr.IsValid())
			{
				TrySanitizeSpawnedPawn(PawnPtr, 0);
			}
		}
	}
}

AActor* UEFProceduralPCGSubsystem::FindDungeonStartPoint(UWorld* World) const
{
	if (!IsValid(World))
	{
		return nullptr;
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	if (UClass* StartPointClass = Settings->GetStartPointActorClassResolved().LoadSynchronous())
	{
		TArray<AActor*> StartPointActors;
		UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, StartPointActors);
		for (AActor* StartPointActor : StartPointActors)
		{
			if (IsValid(StartPointActor))
			{
				return StartPointActor;
			}
		}
	}

	AActor* BestCandidate = nullptr;
	int32 BestScore = 0;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}

		const FString ActorName = Actor->GetName();
		const FString ClassName = Actor->GetClass()->GetName();

		int32 Score = 0;
		if (ClassName.Contains(TEXT("BP_StartPoint"), ESearchCase::IgnoreCase))
		{
			Score += 300;
		}
		if (ActorName.Contains(TEXT("BP_StartPoint"), ESearchCase::IgnoreCase))
		{
			Score += 250;
		}
		if (ClassName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase))
		{
			Score += 150;
		}
		if (ActorName.Contains(TEXT("StartPoint"), ESearchCase::IgnoreCase))
		{
			Score += 100;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestCandidate = Actor;
		}
	}

	return BestCandidate;
}

bool UEFProceduralPCGSubsystem::ResolveDungeonFallbackStartTransform(UWorld* World, FTransform& OutStartTransform) const
{
	if (!IsValid(World) || !IsManagedRuntimeWorld(World))
	{
		return false;
	}

	const FDungeonRuntimeState* RuntimeState = RuntimeStates.Find(TObjectKey<UWorld>(World));
	if (!RuntimeState)
	{
		return false;
	}

	TArray<FVector> CandidateLocations;
	FRotator CandidateRotation = FRotator::ZeroRotator;
	const auto AddCandidate = [&CandidateLocations](const FVector& CandidateLocation)
	{
		const bool bAlreadyAdded = CandidateLocations.ContainsByPredicate([&CandidateLocation](const FVector& ExistingLocation)
		{
			return ExistingLocation.Equals(CandidateLocation, 10.0f);
		});

		if (!bAlreadyAdded)
		{
			CandidateLocations.Add(CandidateLocation);
		}
	};

	if (AActor* DungeonActor = RuntimeState->DungeonActor.Get())
	{
		CandidateRotation = DungeonActor->GetActorRotation();
		const FBox ActorBounds = DungeonActor->GetComponentsBoundingBox(true);
		if (ActorBounds.IsValid)
		{
			const FVector ActorCenter = ActorBounds.GetCenter();
			AddCandidate(ActorCenter);
			AddCandidate(FVector(ActorCenter.X, ActorCenter.Y, ActorBounds.Max.Z + 120.0f));
		}

		AddCandidate(DungeonActor->GetActorLocation());
	}

	const FBox DungeonBounds = CollectDungeonBounds(World, *RuntimeState);
	if (DungeonBounds.IsValid)
	{
		const FVector BoundsCenter = DungeonBounds.GetCenter();
		AddCandidate(BoundsCenter);
		AddCandidate(FVector(BoundsCenter.X, BoundsCenter.Y, DungeonBounds.Max.Z + 120.0f));
	}

	for (const FVector& CandidateLocation : CandidateLocations)
	{
		FVector ResolvedLocation;
		if (ProjectDungeonStartCandidate(World, CandidateLocation, ResolvedLocation))
		{
			OutStartTransform = FTransform(CandidateRotation, ResolvedLocation, FVector::OneVector);
			UE_LOG(LogEFProceduralPCGRuntime, Verbose, TEXT("Resolved fallback player start for world %s at %s."), *World->GetName(), *ResolvedLocation.ToCompactString());
			return true;
		}
	}

	return false;
}

bool UEFProceduralPCGSubsystem::ProjectDungeonStartCandidate(UWorld* World, const FVector& CandidateLocation, FVector& OutResolvedLocation) const
{
	if (!IsValid(World))
	{
		return false;
	}

	FVector ProjectedLocation = CandidateLocation;
	bool bHasNavigationProjection = false;
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation NavLocation;
		if (NavigationSystem->ProjectPointToNavigation(CandidateLocation, NavLocation, FVector(1200.0f, 1200.0f, 2400.0f)))
		{
			ProjectedLocation = NavLocation.Location;
			bHasNavigationProjection = true;
		}
	}

	constexpr float CharacterGroundOffset = 96.0f;
	const FVector TraceStart = ProjectedLocation + FVector(0.0f, 0.0f, 2400.0f);
	const FVector TraceEnd = ProjectedLocation - FVector(0.0f, 0.0f, 4800.0f);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFProceduralPlayerStartGroundTrace), false);
	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		OutResolvedLocation = HitResult.ImpactPoint + FVector(0.0f, 0.0f, CharacterGroundOffset);
		return true;
	}

	if (bHasNavigationProjection)
	{
		OutResolvedLocation = ProjectedLocation + FVector(0.0f, 0.0f, CharacterGroundOffset);
		return true;
	}

	return false;
}

void UEFProceduralPCGSubsystem::HandleWorldActorSpawned(AActor* SpawnedActor)
{
	APawn* SpawnedPawn = Cast<APawn>(SpawnedActor);
	if (!IsValid(SpawnedPawn) || !ShouldFixSpawnedPawn(SpawnedPawn))
	{
		return;
	}

	if (UWorld* World = SpawnedPawn->GetWorld())
	{
		if (FDungeonRuntimeState* RuntimeState = FindRuntimeState(World))
		{
			RuntimeState->SpawnedPawns.Add(SpawnedPawn);
		}

		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::TrySanitizeSpawnedPawn, TWeakObjectPtr<APawn>(SpawnedPawn), 0));
	}
}

bool UEFProceduralPCGSubsystem::ShouldFixSpawnedPawn(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->IsPlayerControlled())
	{
		return false;
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	if (!FEFProceduralACFU::LooksLikeSupportedEnemyPawn(Pawn, Settings))
	{
		return Pawn->ActorHasTag(Settings->GeneratedActorTag);
	}

	return true;
}

void UEFProceduralPCGSubsystem::TrySanitizeSpawnedPawn(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex)
{
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();

	if (!PawnPtr.IsValid())
	{
		return;
	}

	APawn* Pawn = PawnPtr.Get();
	if (!ShouldFixSpawnedPawn(Pawn))
	{
		return;
	}

	UWorld* World = Pawn->GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	const FDungeonRuntimeState* RuntimeState = FindRuntimeState(World);
	const bool bRequireNavigation = RuntimeState && RuntimeState->bNavigationReady;
	if (SanitizeSpawnedPawnPlacement(Pawn, bRequireNavigation))
	{
		TryEnsurePawnController(PawnPtr, 0);
		return;
	}

	if (AttemptIndex < Settings->MaxSpawnSanitizationAttempts)
	{
		FTimerHandle RetryHandle;
		World->GetTimerManager().SetTimer(
			RetryHandle,
			FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::TrySanitizeSpawnedPawn, PawnPtr, AttemptIndex + 1),
			Settings->SpawnSanitizationRetrySeconds,
			false);
		return;
	}

	UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Spawn sanitization exhausted its retries for pawn %s. Keeping the best available placement."), *Pawn->GetName());
	TryEnsurePawnController(PawnPtr, 0);
}

bool UEFProceduralPCGSubsystem::SanitizeSpawnedPawnPlacement(APawn* Pawn, bool bRequireNavigation) const
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	TArray<FVector> CandidateLocations;
	if (!BuildSpawnCandidateLocations(Pawn, bRequireNavigation, CandidateLocations))
	{
		return false;
	}

	const FRotator PawnRotation = Pawn->GetActorRotation();
	for (const FVector& CandidateLocation : CandidateLocations)
	{
		if (Pawn->TeleportTo(CandidateLocation, PawnRotation, false, false))
		{
			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
				{
					MovementComponent->StopMovementImmediately();
				}
			}

			return true;
		}
	}

	return false;
}

bool UEFProceduralPCGSubsystem::BuildSpawnCandidateLocations(APawn* Pawn, bool bRequireNavigation, TArray<FVector>& OutCandidates) const
{
	OutCandidates.Reset();

	if (!IsValid(Pawn))
	{
		return false;
	}

	UWorld* World = Pawn->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = bRequireNavigation ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (bRequireNavigation && !NavigationSystem)
	{
		return false;
	}

	float CollisionRadius = 0.0f;
	float CollisionHalfHeight = 0.0f;
	Pawn->GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
	CollisionRadius = FMath::Max(CollisionRadius, 34.0f);
	CollisionHalfHeight = FMath::Max(CollisionHalfHeight, 88.0f);

	const FVector CurrentLocation = Pawn->GetActorLocation();
	const FVector QueryExtent(CollisionRadius * 2.0f, CollisionRadius * 2.0f, CollisionHalfHeight * 3.0f);
	const TArray<float> SearchRadii = {
		0.0f,
		FMath::Max(CollisionRadius * 1.5f, 100.0f),
		FMath::Max(CollisionRadius * 3.0f, 180.0f),
		300.0f,
		450.0f,
		650.0f
	};

	const TArray<FVector2D> SearchDirections = {
		FVector2D(1.0f, 0.0f),
		FVector2D(-1.0f, 0.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, -1.0f),
		FVector2D(0.707f, 0.707f),
		FVector2D(0.707f, -0.707f),
		FVector2D(-0.707f, 0.707f),
		FVector2D(-0.707f, -0.707f)
	};

	auto AddCandidate = [&](const FVector& RawLocation)
	{
		FVector CandidateLocation = RawLocation;
		if (NavigationSystem)
		{
			FNavLocation NavigationLocation;
			if (!NavigationSystem->ProjectPointToNavigation(RawLocation, NavigationLocation, QueryExtent))
			{
				return;
			}

			CandidateLocation = NavigationLocation.Location;
		}

		FVector SnappedLocation;
		if (SnapSpawnCandidateToGround(World, Pawn, CandidateLocation, SnappedLocation))
		{
			CandidateLocation = SnappedLocation;
		}

		CandidateLocation.Z += 2.0f;

		const bool bAlreadyAdded = OutCandidates.ContainsByPredicate([&CandidateLocation](const FVector& ExistingLocation)
		{
			return ExistingLocation.Equals(CandidateLocation, 5.0f);
		});

		if (!bAlreadyAdded)
		{
			OutCandidates.Add(CandidateLocation);
		}
	};

	AddCandidate(CurrentLocation);
	AddCandidate(CurrentLocation + FVector(0.0f, 0.0f, 180.0f));

	for (const float SearchRadius : SearchRadii)
	{
		for (const FVector2D& Direction : SearchDirections)
		{
			const FVector HorizontalOffset(Direction.X * SearchRadius, Direction.Y * SearchRadius, 0.0f);
			AddCandidate(CurrentLocation + HorizontalOffset);
			AddCandidate(CurrentLocation + HorizontalOffset + FVector(0.0f, 0.0f, 180.0f));
		}
	}

	return !OutCandidates.IsEmpty();
}

bool UEFProceduralPCGSubsystem::SnapSpawnCandidateToGround(UWorld* World, APawn* Pawn, const FVector& CandidateLocation, FVector& OutSnappedLocation) const
{
	if (!IsValid(World) || !IsValid(Pawn))
	{
		return false;
	}

	float CollisionRadius = 0.0f;
	float CollisionHalfHeight = 0.0f;
	Pawn->GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
	CollisionHalfHeight = FMath::Max(CollisionHalfHeight, 88.0f);

	const FVector TraceStart = CandidateLocation + FVector(0.0f, 0.0f, CollisionHalfHeight * 3.0f);
	const FVector TraceEnd = CandidateLocation - FVector(0.0f, 0.0f, CollisionHalfHeight * 6.0f);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EFProceduralSpawnGroundTrace), false, Pawn);
	QueryParams.AddIgnoredActor(Pawn);

	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Pawn, QueryParams))
	{
		OutSnappedLocation = HitResult.ImpactPoint + FVector(0.0f, 0.0f, CollisionHalfHeight);
		return true;
	}

	return false;
}

void UEFProceduralPCGSubsystem::TryEnsurePawnController(TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex)
{
	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();

	if (!PawnPtr.IsValid())
	{
		return;
	}

	APawn* Pawn = PawnPtr.Get();
	if (!ShouldFixSpawnedPawn(Pawn))
	{
		return;
	}

	if (Pawn->GetController())
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
			{
				if (MovementComponent->MovementMode == MOVE_None)
				{
					MovementComponent->SetMovementMode(MOVE_Walking);
				}
			}
		}

		return;
	}

	Pawn->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	ApplyFallbackAIControllerClass(Pawn);

	if (Pawn->GetNetMode() != NM_Client)
	{
		Pawn->SpawnDefaultController();
	}

	if (Pawn->GetController())
	{
		Pawn->Restart();

		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
			{
				if (MovementComponent->MovementMode == MOVE_None)
				{
					MovementComponent->SetMovementMode(MOVE_Walking);
				}
			}
		}

		UE_LOG(LogEFProceduralPCGRuntime, Verbose, TEXT("Assigned AI controller %s to spawned dungeon enemy %s."),
			*GetNameSafe(Pawn->GetController()),
			*Pawn->GetName());
		return;
	}

	if (AttemptIndex < Settings->MaxControllerFixAttempts)
	{
		if (UWorld* World = Pawn->GetWorld())
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(
				RetryHandle,
				FTimerDelegate::CreateUObject(this, &UEFProceduralPCGSubsystem::TryEnsurePawnController, PawnPtr, AttemptIndex + 1),
				Settings->ControllerFixRetrySeconds,
				false);
		}
	}
	else
	{
		UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Failed to assign an AI controller to spawned pawn %s after %d attempts."),
			*Pawn->GetName(),
			AttemptIndex);
	}
}

void UEFProceduralPCGSubsystem::ApplyFallbackAIControllerClass(APawn* Pawn) const
{
	FEFProceduralACFU::ApplyFallbackAIControllerClass(Pawn, UEFProceduralSettings::Get());
}

