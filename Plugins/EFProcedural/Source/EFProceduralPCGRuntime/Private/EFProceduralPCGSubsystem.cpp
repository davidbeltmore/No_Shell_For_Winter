#include "EFProceduralPCGSubsystem.h"
#include "Calysto/EFCalystoDirectorSettings.h"

#include "AIController.h"
#include "Calysto/EFCalystoAssignRoomThemeV6.h"
#include "Calysto/EFCalystoDecalPoolV6.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoPackagedSmokeSubsystem.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoPCGAdapter.h"
#include "Calysto/EFCalystoPCGRuntimeGraphV6.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Calysto/EFCalystoPopulationBridgeV6.h"
#include "Calysto/EFCalystoPopulationMaterializerV6.h"
#include "EFProceduralACFU.h"
#include "EFProceduralSettings.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/PCGBasePointData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Helpers/PCGHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Metadata/PCGMetadata.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFProceduralPCGRuntime, Log, All);

#if !UE_BUILD_SHIPPING
namespace EFProceduralPCGAutomationPrivate
{
	static void SuppressStartPointOnce(const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
		UEFProceduralPCGSubsystem* Subsystem = GameInstance
			? GameInstance->GetSubsystem<UEFProceduralPCGSubsystem>()
			: nullptr;
		if (!Args.IsEmpty()
			|| !Subsystem
			|| World->WorldType != EWorldType::PIE
			|| !FApp::IsUnattended())
		{
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("EF.Calysto.Automation.SuppressStartPointOnce is restricted to unattended PIE with no arguments."));
			return;
		}

		FString Error;
		if (!Subsystem->ArmDevelopmentSuppressStartPointOnceForAutomation(Error))
		{
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("Failed to arm start-point suppression: %s"),
				*Error);
			return;
		}
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Log,
			TEXT("CALYSTO_AUTOMATION_START_POINT_SUPPRESSION armed=true transient=true"));
	}

	static FAutoConsoleCommandWithWorldAndArgs SuppressStartPointOnceCommand(
		TEXT("EF.Calysto.Automation.SuppressStartPointOnce"),
		TEXT("Unattended PIE only. Suppress exactly one valid PCG StartPoint so deterministic V6 repair can be validated."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SuppressStartPointOnce));
}
#endif

namespace EFProceduralRuntimePrivate
{
	static constexpr float DungeonReadinessPollIntervalSeconds = 0.25f;
	static constexpr double DungeonReadinessTimeoutSeconds = 30.0;
	static constexpr float TopologyMinimumPathLength = 1200.0f;
	static constexpr float TopologyDoorApproachOffset = 90.0f;
	static constexpr float TopologyDoorMaximumApproachDistance = 140.0f;
	static constexpr int32 TopologyMaximumCandidateCount = 1024;
	static const FName TopologyRepairTag(TEXT("EF.Calysto.TopologyRepair.V6"));
	static const FString TopologyRepairHashTagPrefix(TEXT("EF.Calysto.TopologyRepairHash."));
	static const FName StartPointRepairTag(TEXT("EF.Calysto.StartPointRepair.V6"));
	static const FString StartPointRepairHashTagPrefix(TEXT("EF.Calysto.StartPointRepairHash."));

	static bool IsCalystoResourceCategory(const FName CategoryId)
	{
		const FString Category = CategoryId.ToString();
		return Category.Contains(TEXT("Food"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Loot"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Chest"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Potion"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Water"), ESearchCase::IgnoreCase) ||
			Category.Contains(TEXT("Armor"), ESearchCase::IgnoreCase);
	}

	struct FTopologyDoorCandidate
	{
		FVector ApproachLocation = FVector::ZeroVector;
		FVector ApproachDirection = FVector::ForwardVector;
		float Yaw = 0.0f;
		float PathLength = 0.0f;
		float OriginalDistanceSquared = 0.0f;
		FString Source;
		FString StableKey;
	};

	struct FTopologyStartCandidate
	{
		FVector Location = FVector::ZeroVector;
		float Yaw = 0.0f;
		double CenterDistanceSquared = 0.0;
		FString Source;
		FString StableKey;
	};

	static FString QuantizedTopologyTransform(const FVector& Location, const float Yaw)
	{
		constexpr double Quantization = 10.0;
		return FString::Printf(
			TEXT("%lld|%lld|%lld|%d"),
			FMath::RoundToInt64(Location.X / Quantization),
			FMath::RoundToInt64(Location.Y / Quantization),
			FMath::RoundToInt64(Location.Z / Quantization),
			FMath::RoundToInt(FRotator::ClampAxis(Yaw)));
	}

	static float MeasurePathLength(const TArray<FVector>& PathPoints)
	{
		float Result = 0.0f;
		for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
		{
			Result += FVector::Dist(PathPoints[Index - 1], PathPoints[Index]);
		}
		return Result;
	}

	static bool TryBuildTopologyCandidate(
		UWorld* World,
		UNavigationSystemV1* NavigationSystem,
		const FVector& ProjectedStart,
		const FVector& RawLocation,
		const FVector& OriginalEndpoint,
		const float SourceYaw,
		const bool bPreserveYaw,
		const FString& Source,
		const FBox& DungeonBounds,
		FTopologyDoorCandidate& OutCandidate)
	{
		if (!IsValid(World) || !NavigationSystem || !DungeonBounds.IsValid || RawLocation.ContainsNaN())
		{
			return false;
		}

		FNavLocation ProjectedCandidate;
		if (!NavigationSystem->ProjectPointToNavigation(
				RawLocation,
				ProjectedCandidate,
				FVector(260.0f, 260.0f, 900.0f))
			|| !DungeonBounds.IsInsideXY(ProjectedCandidate.Location))
		{
			return false;
		}

		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			ProjectedStart,
			ProjectedCandidate.Location);
		if (!IsValid(Path)
			|| !Path->IsValid()
			|| Path->IsPartial()
			|| Path->PathPoints.Num() < 2)
		{
			return false;
		}

		const FVector LastSegment = Path->PathPoints.Last() - Path->PathPoints[Path->PathPoints.Num() - 2];
		FVector ApproachDirection = LastSegment.GetSafeNormal2D();
		if (ApproachDirection.IsNearlyZero())
		{
			ApproachDirection = (ProjectedCandidate.Location - ProjectedStart).GetSafeNormal2D();
		}
		if (ApproachDirection.IsNearlyZero())
		{
			ApproachDirection = FVector::ForwardVector;
		}
		const float ResolvedYaw = bPreserveYaw && FMath::IsFinite(SourceYaw)
			? FRotator::ClampAxis(SourceYaw)
			: FRotator::ClampAxis(ApproachDirection.Rotation().Yaw);

		OutCandidate.ApproachLocation = ProjectedCandidate.Location;
		OutCandidate.ApproachDirection = ApproachDirection;
		OutCandidate.Yaw = ResolvedYaw;
		OutCandidate.PathLength = MeasurePathLength(Path->PathPoints);
		OutCandidate.OriginalDistanceSquared = FVector::DistSquared2D(ProjectedCandidate.Location, OriginalEndpoint);
		OutCandidate.Source = Source;
		OutCandidate.StableKey = FString::Printf(
			TEXT("%s|%s"),
			*Source,
			*QuantizedTopologyTransform(ProjectedCandidate.Location, ResolvedYaw));
		return FMath::IsFinite(OutCandidate.PathLength) && OutCandidate.PathLength > 0.0f;
	}

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

	static bool IsRuntimePCGComponent(const UPCGComponent* PCGComponent)
	{
		return IsValid(PCGComponent)
			&& !PCGComponent->IsEditorOnly()
			&& (!PCGComponent->GetOwner() || !PCGComponent->GetOwner()->IsEditorOnly());
	}
}

bool UEFProceduralPCGSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !UEFCalystoDirectorSettings::IsEnabled();
}

void UEFProceduralPCGSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UEFProceduralRuntimeSubsystem>();
	Collection.InitializeDependency<UEFCalystoDungeonSubsystem>();
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
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
		GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>())
	{
		DungeonSubsystem->OnDirectorWorldAccepted().AddUObject(
			this, &UEFProceduralPCGSubsystem::HandleDirectorWorldAccepted);
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	const UEFCalystoDungeonHarnessSettings* HarnessSettings =
		UEFCalystoDungeonHarnessSettings::Get();
	const TArray<FString> ManagedMaps = Settings
		? Settings->GetManagedMapNamesResolved()
		: TArray<FString>();
	const FString ConfiguredDungeonMap = HarnessSettings
		? EFProceduralRuntimePrivate::NormalizeMapName(
			HarnessSettings->DungeonMap.ToSoftObjectPath().GetLongPackageName())
		: FString();
	const bool bHarnessMapManaged = !ConfiguredDungeonMap.IsEmpty()
		&& ManagedMaps.ContainsByPredicate(
			[&ConfiguredDungeonMap](const FString& ManagedMap)
			{
				return EFProceduralRuntimePrivate::MatchesManagedMapName(
					ConfiguredDungeonMap, ManagedMap);
			});
	const bool bDungeonClassConfigured = Settings
		&& !Settings->GetDungeonActorClassResolved().IsNull();
	const bool bStartPointClassConfigured = Settings
		&& !Settings->GetStartPointActorClassResolved().IsNull();
	if (!bHarnessMapManaged || !bDungeonClassConfigured
		|| !bStartPointClassConfigured)
	{
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("CALYSTO_PCG_CONFIG_INVALID harness_map=%s managed_maps=%s "
				 "harness_map_managed=%s dungeon_class_configured=%s "
				 "start_point_class_configured=%s"),
			*ConfiguredDungeonMap,
			*FString::Join(ManagedMaps, TEXT(",")),
			bHarnessMapManaged ? TEXT("true") : TEXT("false"),
			bDungeonClassConfigured ? TEXT("true") : TEXT("false"),
			bStartPointClassConfigured ? TEXT("true") : TEXT("false"));
	}
}

#if !UE_BUILD_SHIPPING
bool UEFProceduralPCGSubsystem::ArmDevelopmentSuppressStartPointOnceForAutomation(FString& OutError)
{
	OutError.Reset();
	if (bDevelopmentSuppressStartPointOnceForAutomation)
	{
		OutError = TEXT("A one-shot StartPoint suppression is already armed.");
		return false;
	}
	bDevelopmentSuppressStartPointOnceForAutomation = true;
	return true;
}
#endif

void UEFProceduralPCGSubsystem::Deinitialize()
{
	if (GetGameInstance())
	{
		if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
			GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>())
		{
			DungeonSubsystem->OnDirectorWorldAccepted().RemoveAll(this);
		}
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
		if (UWorld* RuntimeWorld = RuntimeState.World.Get())
		{
			RuntimeWorld->GetTimerManager().ClearTimer(RuntimeState.DungeonReadinessPollHandle);
			RollbackMaterializedPopulationV6(RuntimeWorld, RuntimeState);
		}
		if (AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState.DecalPoolOwnerV6.Get())
		{
			if (PoolOwner->DecalPool)
			{
				PoolOwner->DecalPool->ReleaseAllDecals();
			}
			PoolOwner->Destroy();
		}
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
#if !UE_BUILD_SHIPPING
	bDevelopmentSuppressStartPointOnceForAutomation = false;
#endif

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

bool UEFProceduralPCGSubsystem::HasCurrentDungeonNavigationPath() const
{
	FVector StartLocation = FVector::ZeroVector;
	FVector DoorLocation = FVector::ZeroVector;
	return IsDungeonNavigationPathReady(GetWorld(), StartLocation, DoorLocation);
}

bool UEFProceduralPCGSubsystem::WasCurrentFloorDoorDisabledBeforeReadiness() const
{
	const UWorld* CurrentWorld = GetWorld();
	if (!IsValid(CurrentWorld))
	{
		return false;
	}
	if (const FDungeonRuntimeState* RuntimeState = RuntimeStates.Find(TObjectKey<UWorld>(const_cast<UWorld*>(CurrentWorld))))
	{
		return RuntimeState->bFloorDoorDisabledBeforeReadiness;
	}
	return false;
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
	SetFloorDoorsEnabled(World, false);

	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("Runtime dungeon world %s is waiting for the V6 DirectorWorldAccepted gate."),
		*World->GetName());
}

void UEFProceduralPCGSubsystem::HandleDirectorWorldAccepted(
	const int64 RunEpoch,
	const EEFCalystoDungeonTravelKindV6 TravelKind,
	const FEFCalystoResolvedFloorIntentV6& Intent)
{
	(void)TravelKind;
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	const bool bWorldValid = IsValid(World);
	const bool bGameWorld = bWorldValid && World->IsGameWorld();
	const bool bManagedWorld = IsManagedRuntimeWorld(World);
	const bool bEpochValid = RunEpoch > 0;
	const bool bIntentValid = Intent.bIsValid;
	const bool bIdentityValid = Intent.GenerationContext.RunEpoch == RunEpoch
		&& Intent.GenerationContext.TravelKind == TravelKind;
	const bool bGeneratorValid = Intent.GeneratorVersion == EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;
	const bool bIntentHashValid = !Intent.IntentHash.IsEmpty();
	if (!bManagedWorld || !bEpochValid || !bIntentValid || !bIdentityValid || !bGeneratorValid
		|| !bIntentHashValid)
	{
		const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
		const TArray<FString> ManagedMaps = Settings
			? Settings->GetManagedMapNamesResolved()
			: TArray<FString>();
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("V6 DirectorWorldAccepted supplied invalid bootstrap state: "
				 "world=%s world_valid=%s game_world=%s managed_world=%s "
				 "managed_maps=%s epoch=%lld epoch_valid=%s intent_valid=%s "
				 "identity_valid=%s generator=%d generator_valid=%s intent_hash_empty=%s."),
			*GetNameSafe(World),
			bWorldValid ? TEXT("true") : TEXT("false"),
			bGameWorld ? TEXT("true") : TEXT("false"),
			bManagedWorld ? TEXT("true") : TEXT("false"),
			*FString::Join(ManagedMaps, TEXT(",")),
			RunEpoch,
			bEpochValid ? TEXT("true") : TEXT("false"),
			bIntentValid ? TEXT("true") : TEXT("false"),
			bIdentityValid ? TEXT("true") : TEXT("false"),
			Intent.GeneratorVersion,
			bGeneratorValid ? TEXT("true") : TEXT("false"),
			bIntentHashValid ? TEXT("false") : TEXT("true"));
		if (IsValid(World))
		{
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem =
				GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>())
			{
				DungeonSubsystem->NotifyGenerationFailed(
					TEXT("V6_DIRECTOR_ACCEPTANCE_INVALID"),
					TEXT("PCG refused to bootstrap before a valid V6 DirectorWorldAccepted contract."));
			}
		}
		return;
	}

	UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!DungeonSubsystem
		|| !DungeonSubsystem->IsGenerationBootstrapCurrentV6(Intent.IntentHash))
	{
		// Multicast listeners run sequentially. A preceding subsystem may already
		// have rejected the accepted world and started fail-closed recovery. Do not
		// schedule stale PCG work or report a second failure against that retry.
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Warning,
			TEXT("V6 PCG bootstrap skipped because accepted intent %s no longer owns generation."),
			*Intent.IntentHash);
		return;
	}

	FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
	if (RuntimeState.bDirectorWorldAccepted)
	{
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Duplicate V6 DirectorWorldAccepted gate rejected for world %s intent=%s."),
			*World->GetName(),
			*Intent.IntentHash);
		return;
	}
	RuntimeState.World = World;
	RuntimeState.bDirectorWorldAccepted = true;
	RuntimeState.DirectorIntentHash = Intent.IntentHash;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("CALYSTO_V6_DIRECTOR_GATE_ACCEPTED world=%s epoch=%lld intent=%s"),
		*World->GetName(),
		RunEpoch,
		*Intent.IntentHash);
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(
			this,
			&UEFProceduralPCGSubsystem::BootstrapDungeon,
			TWeakObjectPtr<UWorld>(World),
			0));
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
		World->GetTimerManager().ClearTimer(RuntimeState->DungeonReadinessPollHandle);
		RollbackMaterializedPopulationV6(World, *RuntimeState);
		if (AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState->DecalPoolOwnerV6.Get())
		{
			if (PoolOwner->DecalPool)
			{
				PoolOwner->DecalPool->ReleaseAllDecals();
			}
			PoolOwner->Destroy();
			RuntimeState->DecalPoolOwnerV6.Reset();
		}
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
	if (!RuntimeState.bDirectorWorldAccepted)
	{
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Runtime dungeon bootstrap rejected before DirectorWorldAccepted for world %s."),
			*World->GetName());
		return;
	}
	if (RuntimeState.bPCGGenerationTriggered || RuntimeState.bPCGGenerationFinished)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(RuntimeState.DungeonReadinessPollHandle);
	RuntimeState.DungeonReadinessPollHandle.Invalidate();
	SetFloorDoorsEnabled(World, false);
	RuntimeState.World = World;
	RuntimeState.bBootstrapStarted = true;
	if (RuntimeState.BootstrapStartTimeSeconds <= 0.0)
	{
		RuntimeState.BootstrapStartTimeSeconds = World->GetTimeSeconds();
	}
	auto ReportTerminalBootstrapFailure = [World, &RuntimeState](const FName FailureCode, const FString& FailureMessage)
	{
		RuntimeState.bPCGGenerationTriggered = false;
		RuntimeState.bPCGGenerationFinished = false;
		RuntimeState.bPCGGenerationFailed = true;
		RuntimeState.bDungeonReady = false;
		RuntimeState.bRoomManifestReady = false;
		RuntimeState.bPostTopologyAssetsReady = false;
		RuntimeState.bFloorVisualAssetsReady = false;
		RuntimeState.bVisualsReady = false;
		RuntimeState.bDecalsRealized = false;
		RuntimeState.RealizedDecalCount = 0;
		RuntimeState.GenerationGateV6.Reset();
		RuntimeState.PendingPCGComponents.Reset();
		if (AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState.DecalPoolOwnerV6.Get())
		{
			if (PoolOwner->DecalPool)
			{
				PoolOwner->DecalPool->ReleaseAllDecals();
			}
			PoolOwner->Destroy();
			RuntimeState.DecalPoolOwnerV6.Reset();
		}
		if (!RuntimeState.bFailureReported)
		{
			RuntimeState.bFailureReported = true;
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr)
			{
				DungeonSubsystem->NotifyGenerationFailed(FailureCode, FailureMessage);
			}
		}
	};

	int32 ExistingDungeonCount = 0;
	AActor* ExistingDungeon = FindDungeonActor(World, ExistingDungeonCount);
	if (ExistingDungeonCount > 1)
	{
		RuntimeState.DungeonActor.Reset();
		const FString FailureMessage = FString::Printf(
			TEXT("Found %d dungeon actors in world %s; at most one is allowed."),
			ExistingDungeonCount,
			*World->GetName());
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Calysto controlled generation aborted: %s"),
			*FailureMessage);
		ReportTerminalBootstrapFailure(TEXT("MULTIPLE_DUNGEONS"), FailureMessage);
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
		ReportTerminalBootstrapFailure(
			TEXT("DUNGEON_CLASS_MISSING"),
			FString::Printf(TEXT("No DungeonActorClass is configured for managed world %s."), *World->GetName()));
		return;
	}

	UEFCalystoDungeonSubsystem* ActiveDungeonSubsystem = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorIntentV6 ActiveIntent = ActiveDungeonSubsystem
		? ActiveDungeonSubsystem->GetResolvedFloorIntentV6()
		: FEFCalystoResolvedFloorIntentV6();
	if (!ActiveDungeonSubsystem
		|| !ActiveDungeonSubsystem->IsGenerationBootstrapCurrentV6(
			RuntimeState.DirectorIntentHash)
		|| !ActiveIntent.bIsValid
		|| ActiveIntent.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion
		|| ActiveIntent.IntentHash != RuntimeState.DirectorIntentHash)
	{
		const FString FailureMessage = FString::Printf(
			TEXT("V6 Director intent token changed while bootstrap assets were loading (accepted=%s active=%s)."),
			*RuntimeState.DirectorIntentHash,
			*ActiveIntent.IntentHash);
		UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
		ReportTerminalBootstrapFailure(TEXT("V6_DIRECTOR_INTENT_DRIFT"), FailureMessage);
		return;
	}

	FString FloorVisualReadinessError;
	const EEFCalystoLoadPhaseState FloorVisualState =
		ActiveDungeonSubsystem->GetFloorVisualAssetLoadStateV6(FloorVisualReadinessError);
	if (FloorVisualState != EEFCalystoLoadPhaseState::Ready)
	{
		if (FloorVisualState == EEFCalystoLoadPhaseState::Failed)
		{
			const FString FailureMessage = FString::Printf(
				TEXT("Calysto V6 Floor Visual loading failed: %s"),
				*FloorVisualReadinessError);
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
			ReportTerminalBootstrapFailure(TEXT("V6_FLOOR_VISUAL_LOAD_FAILED"), FailureMessage);
			return;
		}
		if (RuntimeState.bFloorVisualLoadCompletionObserved &&
			FloorVisualState != EEFCalystoLoadPhaseState::Loading)
		{
			const FString FailureMessage = FString::Printf(
				TEXT("Calysto V6 Floor Visual loading completed in an invalid state: %s"),
				*FloorVisualReadinessError);
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
			ReportTerminalBootstrapFailure(TEXT("V6_FLOOR_VISUAL_LOAD_INVALID"), FailureMessage);
			return;
		}

		if (!RuntimeState.bFloorVisualLoadRequested)
		{
			RuntimeState.bFloorVisualLoadRequested = true;
			FString StartError;
			if (!ActiveDungeonSubsystem->EnsureFloorVisualAssetsV6(
					FStreamableDelegate::CreateUObject(
						this,
						&UEFProceduralPCGSubsystem::HandleFloorVisualAssetsReadyV6,
						WorldPtr,
						AttemptIndex,
						RuntimeState.DirectorIntentHash),
					StartError))
			{
				const FString FailureMessage = FString::Printf(
					TEXT("Calysto V6 Floor Visual loading could not start: %s"),
					*StartError);
				UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
				ReportTerminalBootstrapFailure(TEXT("V6_FLOOR_VISUAL_LOAD_START_FAILED"), FailureMessage);
				return;
			}
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Log,
				TEXT("CALYSTO_V6_FLOOR_VISUAL_WAIT world=%s floor_plan=%s state=%s"),
				*World->GetName(),
				*ActiveDungeonSubsystem->GetResolvedFloorPlanV6().FloorPlanHash,
				*FloorVisualReadinessError);
		}

		const double ElapsedSeconds = World->GetTimeSeconds() - RuntimeState.BootstrapStartTimeSeconds;
		if (ElapsedSeconds >= EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds)
		{
			const FString FailureMessage = FString::Printf(
				TEXT("Calysto V6 Floor Visual loading did not become ready within %.1f seconds: %s"),
				EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds,
				*FloorVisualReadinessError);
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
			ReportTerminalBootstrapFailure(TEXT("V6_FLOOR_VISUAL_LOAD_TIMEOUT"), FailureMessage);
			return;
		}

		World->GetTimerManager().SetTimer(
			RuntimeState.DungeonReadinessPollHandle,
			FTimerDelegate::CreateUObject(
				this,
				&UEFProceduralPCGSubsystem::BootstrapDungeon,
				WorldPtr,
				AttemptIndex),
			Settings->DungeonBootstrapRetrySeconds,
			false);
		return;
	}
	RuntimeState.bFloorVisualAssetsReady = true;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("CALYSTO_V6_FLOOR_VISUAL_READY world=%s floor_plan=%s"),
		*World->GetName(),
		*ActiveDungeonSubsystem->GetResolvedFloorPlanV6().FloorPlanHash);

	UClass* DungeonClass = DungeonClassPtr.Get();
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
			const FString FailureMessage = FString::Printf(TEXT("Failed to load dungeon class after %d attempts."), AttemptIndex);
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
			ReportTerminalBootstrapFailure(TEXT("DUNGEON_CLASS_LOAD_FAILED"), FailureMessage);
		}

		return;
	}

	if (IsValid(ExistingDungeon))
	{
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Log,
			TEXT("Found existing dungeon actor %s. Preload and frozen-plan compilation are complete; applying the controlled generation boundary."),
			*ExistingDungeon->GetName());
		ApplyCalystoHarnessAndGenerate(World, ExistingDungeon, false);
		return;
	}

	const bool bDirectorWorldAccepted = RuntimeState.bDirectorWorldAccepted;
	const bool bFloorVisualLoadRequested = RuntimeState.bFloorVisualLoadRequested;
	const bool bFloorVisualLoadCompletionObserved = RuntimeState.bFloorVisualLoadCompletionObserved;
	const bool bFloorVisualAssetsReady = RuntimeState.bFloorVisualAssetsReady;
	const double BootstrapStartTimeSeconds = RuntimeState.BootstrapStartTimeSeconds;
	FString DirectorIntentHash = MoveTemp(RuntimeState.DirectorIntentHash);
	RuntimeState = FDungeonRuntimeState();
	RuntimeState.World = World;
	RuntimeState.bDirectorWorldAccepted = bDirectorWorldAccepted;
	RuntimeState.bFloorVisualLoadRequested = bFloorVisualLoadRequested;
	RuntimeState.bFloorVisualLoadCompletionObserved = bFloorVisualLoadCompletionObserved;
	RuntimeState.bFloorVisualAssetsReady = bFloorVisualAssetsReady;
	RuntimeState.DirectorIntentHash = MoveTemp(DirectorIntentHash);
	RuntimeState.bBootstrapStarted = true;
	RuntimeState.BootstrapStartTimeSeconds = BootstrapStartTimeSeconds;

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
			const FString FailureMessage = FString::Printf(TEXT("Failed to spawn dungeon actor after %d attempts."), AttemptIndex);
			UE_LOG(LogEFProceduralPCGRuntime, Error, TEXT("%s"), *FailureMessage);
			ReportTerminalBootstrapFailure(TEXT("DUNGEON_SPAWN_FAILED"), FailureMessage);
		}

		return;
	}

	UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("Spawned runtime dungeon actor %s."), *DungeonActor->GetName());
	ApplyCalystoHarnessAndGenerate(World, DungeonActor, true);
}

void UEFProceduralPCGSubsystem::HandleFloorVisualAssetsReadyV6(
	TWeakObjectPtr<UWorld> WorldPtr,
	const int32 AttemptIndex,
	const FString ExpectedIntentHash)
{
	if (!WorldPtr.IsValid())
	{
		return;
	}
	FDungeonRuntimeState* RuntimeState = FindRuntimeState(WorldPtr.Get());
	UEFCalystoDungeonSubsystem* DungeonSubsystem = WorldPtr->GetGameInstance()
		? WorldPtr->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorIntentV6 ActiveIntent = DungeonSubsystem
		? DungeonSubsystem->GetResolvedFloorIntentV6()
		: FEFCalystoResolvedFloorIntentV6();
	if (!RuntimeState
		|| !RuntimeState->bDirectorWorldAccepted
		|| RuntimeState->DirectorIntentHash != ExpectedIntentHash
		|| !ActiveIntent.bIsValid
		|| ActiveIntent.IntentHash != ExpectedIntentHash)
	{
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Warning,
			TEXT("Ignored stale Calysto V6 Floor Visual callback for world %s expected_intent=%s active_intent=%s."),
			*WorldPtr->GetName(),
			*ExpectedIntentHash,
			*ActiveIntent.IntentHash);
		return;
	}

	RuntimeState->bFloorVisualLoadCompletionObserved = true;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("Calysto V6 Floor Visual asynchronous phase completed for world %s."),
		*WorldPtr->GetName());
	WorldPtr->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(
			this,
			&UEFProceduralPCGSubsystem::BootstrapDungeon,
			WorldPtr,
			AttemptIndex));
}

AActor* UEFProceduralPCGSubsystem::FindDungeonActor(
	UWorld* World,
	int32& OutDungeonActorCount) const
{
	OutDungeonActorCount = 0;
	if (!IsValid(World))
	{
		return nullptr;
	}

	UClass* DungeonClass = UEFProceduralSettings::Get()->GetDungeonActorClassResolved().Get();

	AActor* FirstDungeonActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (IsValid(DungeonClass) && Actor->IsA(DungeonClass))
		{
			++OutDungeonActorCount;
			if (!FirstDungeonActor)
			{
				FirstDungeonActor = Actor;
			}
		}
	}

	return FirstDungeonActor;
}

bool UEFProceduralPCGSubsystem::BuildRoomThemeGenerationConfigV6(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const TMap<FName, TObjectPtr<UObject>>& ThemeRoomTypes,
	const FTransform& DungeonTransform,
	FEFCalystoRoomThemeGenerationConfigV6& OutConfig,
	FString& OutError) const
{
	OutConfig = FEFCalystoRoomThemeGenerationConfigV6();
	OutError.Reset();
	if (FloorPlan.FloorSeed == 0
		|| FloorPlan.StyleId.IsNone()
		|| FloorPlan.PolicyHash.Len() != 64
		|| FloorPlan.FloorPlanHash.Len() != 64
		|| !FMath::IsFinite(FloorPlan.ThemeRoomChance)
		|| FloorPlan.ThemeRoomChance < 0.0f
		|| FloorPlan.ThemeRoomChance > 1.0f
		|| !FMath::IsFinite(FloorPlan.RoomIdentityQuantizationCm)
		|| FloorPlan.RoomIdentityQuantizationCm <= 0.0f
		|| FloorPlan.MaximumRoomRecords <= 0
		|| FloorPlan.DecalComponentPoolCapacity <= 0
		|| FloorPlan.DecalComponentPoolCapacity > UEFCalystoDecalPoolComponentV6::HardMaximumDecals
		|| DungeonTransform.ContainsNaN()
		|| !DungeonTransform.IsValid())
	{
		OutError = TEXT("The active Calysto V6 floor plan is incomplete or invalid at the PCG boundary.");
		return false;
	}
	if (FloorPlan.Themes.Num() != FloorPlan.ThemeAliasProbability.Num()
		|| FloorPlan.Themes.Num() != FloorPlan.ThemeAliasIndex.Num()
		|| (FloorPlan.ThemeRoomChance > 0.0f && FloorPlan.Themes.IsEmpty()))
	{
		OutError = TEXT("The frozen Calysto V6 Theme alias table does not match its resolved Theme profiles.");
		return false;
	}

	OutConfig.GenerationSeed = FloorPlan.FloorSeed;
	OutConfig.FloorPlanHash = FloorPlan.FloorPlanHash;
	OutConfig.ThemePresenceChance = FloorPlan.ThemeRoomChance;
	OutConfig.DungeonTransform = DungeonTransform;
	OutConfig.RoomIdentityGridCm = FloorPlan.RoomIdentityQuantizationCm;
	OutConfig.ProtectionToleranceCm = FMath::Max(2.0, FloorPlan.RoomIdentityQuantizationCm * 0.25);
	// The protected vendor graph exposes Start/End room connectors but no
	// pre-theme door-point stream. A fixed, fingerprinted AABB halo supplies
	// deterministic DoorClearance flags; physical door hits are also rejected.
	OutConfig.DoorClearanceRadiusCm = 200.0;
	OutConfig.Style.StyleId = FloorPlan.StyleId;
	OutConfig.Style.FloorMaterial = FloorPlan.StyleMaterials.FloorMaterial;
	OutConfig.Style.WallMaterial = FloorPlan.StyleMaterials.WallMaterial;
	OutConfig.Style.RoofMaterial = FloorPlan.StyleMaterials.RoofMaterial;
	OutConfig.ThemeAliasProbability = FloorPlan.ThemeAliasProbability;
	OutConfig.ThemeAliasIndex = FloorPlan.ThemeAliasIndex;
	OutConfig.Themes.Reserve(FloorPlan.Themes.Num());
	for (const FEFCalystoResolvedThemeProfileV6& ResolvedTheme : FloorPlan.Themes)
	{
		FEFCalystoPCGThemeProfileV6& Theme = OutConfig.Themes.AddDefaulted_GetRef();
		Theme.ThemeId = ResolvedTheme.ThemeId;
		Theme.SelectionWeight = ResolvedTheme.SelectionWeight;
		Theme.RoomType = ThemeRoomTypes.FindRef(ResolvedTheme.ThemeId);
		Theme.ArchitectureHash = ResolvedTheme.ArchitectureHash;
		Theme.FloorMaterial = ResolvedTheme.FloorMaterialMode == EEFCalystoMaterialResolutionModeV6::Override
			? ResolvedTheme.EffectiveMaterials.FloorMaterial
			: TSoftObjectPtr<UMaterialInstance>();
		Theme.WallMaterial = ResolvedTheme.WallMaterialMode == EEFCalystoMaterialResolutionModeV6::Override
			? ResolvedTheme.EffectiveMaterials.WallMaterial
			: TSoftObjectPtr<UMaterialInstance>();
		Theme.RoofMaterial = ResolvedTheme.RoofMaterialMode == EEFCalystoMaterialResolutionModeV6::Override
			? ResolvedTheme.EffectiveMaterials.RoofMaterial
			: TSoftObjectPtr<UMaterialInstance>();
		Theme.CatalogId = ResolvedTheme.ThemeId;
	}

	if (!OutConfig.Validate(OutError))
	{
		OutConfig = FEFCalystoRoomThemeGenerationConfigV6();
		return false;
	}
	return true;
}

bool UEFProceduralPCGSubsystem::EnsureDecalPoolV6(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState,
	FString& OutError)
{
	OutError.Reset();
	if (!IsValid(World) || !World->IsGameWorld())
	{
		OutError = TEXT("The Calysto V6 decal pool requires a valid game world.");
		return false;
	}
	const UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorPlanV6 FloorPlan = DungeonSubsystem
		? DungeonSubsystem->GetResolvedFloorPlanV6()
		: FEFCalystoResolvedFloorPlanV6();
	if (FloorPlan.FloorPlanHash.IsEmpty()
		|| FloorPlan.DecalComponentPoolCapacity <= 0
		|| FloorPlan.DecalComponentPoolCapacity > UEFCalystoDecalPoolComponentV6::HardMaximumDecals)
	{
		OutError = TEXT("The frozen Calysto V6 decal component capacity is invalid.");
		return false;
	}

	AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState.DecalPoolOwnerV6.Get();
	if (!IsValid(PoolOwner))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(
			World,
			AEFCalystoDecalPoolOwnerV6::StaticClass(),
			TEXT("EFCalystoDecalPoolOwnerV6"));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PoolOwner = World->SpawnActor<AEFCalystoDecalPoolOwnerV6>(
			AEFCalystoDecalPoolOwnerV6::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!IsValid(PoolOwner))
		{
			OutError = TEXT("Calysto V6 could not create its single transient decal pool owner.");
			return false;
		}
		RuntimeState.DecalPoolOwnerV6 = PoolOwner;
	}
	if (!PoolOwner->DecalPool
		|| !PoolOwner->DecalPool->InitializePool(FloorPlan.DecalComponentPoolCapacity))
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 could not initialize its decal component pool to capacity %d."),
			FloorPlan.DecalComponentPoolCapacity);
		return false;
	}
	RuntimeState.bDecalPoolReady = true;
	return true;
}

bool UEFProceduralPCGSubsystem::RealizeDecalsV6(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState,
	FString& OutError)
{
	OutError.Reset();
	UEFCalystoDungeonSubsystem* DungeonSubsystem = IsValid(World) && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	AActor* DungeonActor = RuntimeState.DungeonActor.Get();
	AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState.DecalPoolOwnerV6.Get();
	if (!DungeonSubsystem || !IsValid(DungeonActor) || !IsValid(PoolOwner)
		|| !PoolOwner->DecalPool || !RuntimeState.bDecalPoolReady)
	{
		OutError = TEXT("Calysto V6 decal realization requires its frozen floor, dungeon actor, and initialized pool.");
		return false;
	}

	const FEFCalystoResolvedFloorPlanV6 FloorPlan = DungeonSubsystem->GetResolvedFloorPlanV6();
	const FEFCalystoRoomManifestV6 Manifest = DungeonSubsystem->GetRoomManifestV6();
	if (FloorPlan.FloorPlanHash != RuntimeState.V6FloorPlanHash
		|| Manifest.FloorPlanHash != RuntimeState.V6FloorPlanHash)
	{
		OutError = TEXT("Calysto V6 rejected decal realization because the frozen floor or manifest drifted.");
		return false;
	}

	TArray<FEFCalystoDecalPlacementCandidateV6> Candidates;
	if (!FEFCalystoDecalPlacementMathV6::BuildCandidates(FloorPlan, Manifest, Candidates, OutError))
	{
		return false;
	}
	PoolOwner->DecalPool->ReleaseAllDecals();
	RuntimeState.RealizedDecalCount = 0;
	const FTransform DungeonTransform = DungeonActor->GetActorTransform();
	const FVector DungeonUp = DungeonTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (DungeonUp.IsNearlyZero())
	{
		OutError = TEXT("Calysto V6 dungeon transform has no valid up axis for decal normal validation.");
		return false;
	}

	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(TEXT("EFCalystoV6DecalTrace"), true);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActor(PoolOwner);
	int32 RejectedTraceCount = 0;
	for (const FEFCalystoDecalPlacementCandidateV6& Candidate : Candidates)
	{
		UMaterialInterface* Material = Candidate.Material.Get();
		UMaterial* BaseMaterial = IsValid(Material) ? Material->GetMaterial() : nullptr;
		if (!IsValid(Material) || !IsValid(BaseMaterial)
			|| BaseMaterial->MaterialDomain != MD_DeferredDecal)
		{
			PoolOwner->DecalPool->ReleaseAllDecals();
			OutError = FString::Printf(
				TEXT("Resident decal material %s is missing or is not a Deferred Decal before room %lld could be realized."),
				*Candidate.Material.ToSoftObjectPath().ToString(),
				Candidate.StableRoomId);
			return false;
		}

		const FVector TraceStart = DungeonTransform.TransformPosition(Candidate.LocalTraceStart);
		const FVector TraceEnd = DungeonTransform.TransformPosition(Candidate.LocalTraceEnd);
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectTypes, QueryParams)
			|| !Hit.bBlockingHit
			|| !IsValid(Hit.GetActor())
			|| !IsValid(Hit.GetComponent()))
		{
			++RejectedTraceCount;
			continue;
		}

		AActor* HitActor = Hit.GetActor();
		UPrimitiveComponent* HitComponent = Hit.GetComponent();
		const bool bGeneratedGeometry = HitActor == DungeonActor
			|| HitActor->ActorHasTag(PCGHelpers::DefaultPCGActorTag)
			|| HitComponent->ComponentHasTag(PCGHelpers::DefaultPCGTag);
		const auto IsDoorToken = [](const UObject* Object)
		{
			return IsValid(Object)
				&& (Object->GetName().Contains(TEXT("Door"), ESearchCase::IgnoreCase)
					|| Object->GetClass()->GetName().Contains(TEXT("Door"), ESearchCase::IgnoreCase));
		};
		if (!bGeneratedGeometry || IsDoorToken(HitActor) || IsDoorToken(HitComponent)
			|| HitActor->ActorHasTag(TEXT("Door")) || HitComponent->ComponentHasTag(TEXT("Door")))
		{
			++RejectedTraceCount;
			continue;
		}

		const FVector SurfaceNormal = Hit.ImpactNormal.GetSafeNormal();
		const double UpDot = FVector::DotProduct(SurfaceNormal, DungeonUp);
		const bool bNormalAccepted = Candidate.Surface == EEFCalystoPCGDecalSurfaceV6::Floor
			? UpDot >= 0.65
			: Candidate.Surface == EEFCalystoPCGDecalSurfaceV6::Roof
				? UpDot <= -0.65
				: FMath::Abs(UpDot) <= 0.35;
		if (SurfaceNormal.IsNearlyZero() || !bNormalAccepted)
		{
			++RejectedTraceCount;
			continue;
		}

		const FQuat SurfaceRotation = SurfaceNormal.Rotation().Quaternion();
		const FQuat RollRotation(SurfaceNormal, FMath::DegreesToRadians(Candidate.RollDegrees));
		FEFCalystoPooledDecalRequestV6 Request;
		Request.StableDecalId = Candidate.StableDecalId;
		Request.Surface = Candidate.Surface;
		Request.Material = Material;
		Request.WorldTransform = FTransform(
			RollRotation * SurfaceRotation,
			Hit.ImpactPoint + SurfaceNormal * 1.0,
			FVector::OneVector);
		Request.DecalSize = FVector(
			FMath::Max(8.0f, Candidate.SizeCm * 0.12f),
			Candidate.SizeCm,
			Candidate.SizeCm);
		Request.FadeStartDistanceCm = Candidate.FadeStartDistanceCm;
		Request.CullDistanceCm = Candidate.CullDistanceCm;
		FEFCalystoPooledDecalHandleV6 Handle;
		if (!PoolOwner->DecalPool->AcquireDecal(Request, Handle))
		{
			PoolOwner->DecalPool->ReleaseAllDecals();
			RuntimeState.RealizedDecalCount = 0;
			OutError = FString::Printf(
				TEXT("Calysto V6 decal pool rejected the deterministic request for room %lld."),
				Candidate.StableRoomId);
			return false;
		}
		++RuntimeState.RealizedDecalCount;
	}
	if (PoolOwner->DecalPool->GetActiveDecalCount() != RuntimeState.RealizedDecalCount)
	{
		PoolOwner->DecalPool->ReleaseAllDecals();
		RuntimeState.RealizedDecalCount = 0;
		OutError = TEXT("Calysto V6 decal pool active cardinality differs from the deterministic realization count.");
		return false;
	}

	RuntimeState.bDecalsRealized = true;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("CALYSTO_V6_DECALS_REALIZED candidates=%d active=%d trace_rejected=%d floor_plan=%s manifest=%s."),
		Candidates.Num(),
		RuntimeState.RealizedDecalCount,
		RejectedTraceCount,
		*FloorPlan.FloorPlanHash,
		*Manifest.ManifestHash);
	return true;
}

bool UEFProceduralPCGSubsystem::FreezeRoomManifestFromPCGOutputV6(
	UWorld* World,
	UPCGComponent* PCGComponent,
	FDungeonRuntimeState& RuntimeState,
	FString& OutError)
{
	OutError.Reset();
	UEFCalystoDungeonSubsystem* DungeonSubsystem = IsValid(World) && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!DungeonSubsystem || !IsValid(PCGComponent)
		|| RuntimeState.ControlledPCGComponent.Get() != PCGComponent)
	{
		OutError = TEXT("Calysto V6 cannot freeze room topology from an unowned PCG completion.");
		return false;
	}
	const FEFCalystoResolvedFloorPlanV6 FloorPlan = DungeonSubsystem->GetResolvedFloorPlanV6();
	if (FloorPlan.FloorPlanHash.IsEmpty()
		|| FloorPlan.FloorPlanHash != RuntimeState.V6FloorPlanHash
		|| FloorPlan.StyleId.IsNone()
		|| !RuntimeState.DungeonActor.IsValid())
	{
		OutError = TEXT("Calysto V6 rejected post-PCG topology because the frozen floor plan changed or is unavailable.");
		return false;
	}

	struct FObservedRoom
	{
		int32 Flags = 0;
		FName ThemeId = NAME_None;
		FName StyleId = NAME_None;
		FSoftObjectPath FloorMaterial;
		FSoftObjectPath WallMaterial;
		FSoftObjectPath RoofMaterial;
		int32 FloorMaterialAuthority = INDEX_NONE;
		int32 WallMaterialAuthority = INDEX_NONE;
		int32 RoofMaterialAuthority = INDEX_NONE;
		TObjectPtr<UObject> VendorRoomType;
		bool bVendorOverrideFloorMaterial = false;
		FSoftObjectPath VendorFloorMaterial;
		bool bVendorOverrideWallMaterial = false;
		FSoftObjectPath VendorWallMaterial;
		bool bVendorOverrideRoofMaterial = false;
		FSoftObjectPath VendorRoofMaterial;
	};
	TMap<int64, FObservedRoom> ObservedRooms;
	TArray<FEFCalystoRoomIdentityInputV6> RoomInputs;
	const FTransform DungeonTransform = RuntimeState.DungeonActor->GetActorTransform();
	int32 ContextDataSetCount = 0;

	for (const FPCGTaggedData& TaggedData : PCGComponent->GetGeneratedGraphOutput().TaggedData)
	{
		const bool bRoomContexts = TaggedData.Pin == FEFCalystoRoomThemePinsV6::RoomContexts
			|| TaggedData.Tags.Contains(TEXT("EF.RoomContexts.V6"));
		if (!bRoomContexts)
		{
			continue;
		}
		const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(TaggedData.Data);
		const UPCGMetadata* Metadata = PointData ? PointData->ConstMetadata() : nullptr;
		if (!PointData || !Metadata)
		{
			OutError = TEXT("The Calysto V6 Room Contexts graph output contains non-point data or no metadata.");
			return false;
		}
		++ContextDataSetCount;
		const FPCGMetadataAttribute<FString>* RoomIdAttribute =
			Metadata->GetConstTypedAttribute<FString>(FEFCalystoRoomThemeMetadataV6::RoomId);
		const FPCGMetadataAttribute<int32>* FlagsAttribute =
			Metadata->GetConstTypedAttribute<int32>(FEFCalystoRoomThemeMetadataV6::RoomFlags);
		const FPCGMetadataAttribute<FName>* TopologyAttribute =
			Metadata->GetConstTypedAttribute<FName>(FEFCalystoRoomThemeMetadataV6::TopologyKind);
		const FPCGMetadataAttribute<int32>* CollisionAttribute =
			Metadata->GetConstTypedAttribute<int32>(FEFCalystoRoomThemeMetadataV6::CollisionOrdinal);
		const FPCGMetadataAttribute<FName>* ThemeAttribute =
			Metadata->GetConstTypedAttribute<FName>(FEFCalystoRoomThemeMetadataV6::ThemeId);
		const FPCGMetadataAttribute<FName>* StyleAttribute =
			Metadata->GetConstTypedAttribute<FName>(FEFCalystoRoomThemeMetadataV6::StyleId);
		const FPCGMetadataAttribute<FSoftObjectPath>* FloorAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveFloorMaterial);
		const FPCGMetadataAttribute<FSoftObjectPath>* WallAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveWallMaterial);
		const FPCGMetadataAttribute<FSoftObjectPath>* RoofAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveRoofMaterial);
		const FPCGMetadataAttribute<int32>* FloorAuthorityAttribute =
			Metadata->GetConstTypedAttribute<int32>(FEFCalystoRoomThemeMetadataV6::FloorMaterialAuthority);
		const FPCGMetadataAttribute<int32>* WallAuthorityAttribute =
			Metadata->GetConstTypedAttribute<int32>(FEFCalystoRoomThemeMetadataV6::WallMaterialAuthority);
		const FPCGMetadataAttribute<int32>* RoofAuthorityAttribute =
			Metadata->GetConstTypedAttribute<int32>(FEFCalystoRoomThemeMetadataV6::RoofMaterialAuthority);
		const FPCGMetadataAttribute<FSoftObjectPath>* VendorRoomTypeAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorRoomType);
		const FPCGMetadataAttribute<bool>* VendorOverrideFloorAttribute =
			Metadata->GetConstTypedAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideFloorMaterial);
		const FPCGMetadataAttribute<FSoftObjectPath>* VendorFloorAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorFloorMaterial);
		const FPCGMetadataAttribute<bool>* VendorOverrideWallAttribute =
			Metadata->GetConstTypedAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideWallMaterial);
		const FPCGMetadataAttribute<FSoftObjectPath>* VendorWallAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorWallMaterial);
		const FPCGMetadataAttribute<bool>* VendorOverrideRoofAttribute =
			Metadata->GetConstTypedAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideRoofMaterial);
		const FPCGMetadataAttribute<FSoftObjectPath>* VendorRoofAttribute =
			Metadata->GetConstTypedAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorRoofMaterial);
		if (!RoomIdAttribute || !FlagsAttribute || !TopologyAttribute
			|| !CollisionAttribute || !ThemeAttribute || !StyleAttribute
			|| !FloorAttribute || !WallAttribute || !RoofAttribute
			|| !FloorAuthorityAttribute || !WallAuthorityAttribute || !RoofAuthorityAttribute
			|| !VendorRoomTypeAttribute
			|| !VendorOverrideFloorAttribute || !VendorFloorAttribute
			|| !VendorOverrideWallAttribute || !VendorWallAttribute
			|| !VendorOverrideRoofAttribute || !VendorRoofAttribute)
		{
			OutError = TEXT("The Calysto V6 Room Contexts output is missing one or more identity, Style, Theme, authority, or vendor-material attributes.");
			return false;
		}

		const FConstPCGPointValueRanges PointRanges(PointData);
		for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
		{
			const FPCGPoint& Point = PointRanges.GetPoint(PointIndex);
			const PCGMetadataEntryKey EntryKey = Point.MetadataEntry;
			int64 ObservedStableRoomId = 0;
			const FString RoomIdText = RoomIdAttribute->GetValueFromItemKey(EntryKey);
			if (!LexTryParseString(ObservedStableRoomId, *RoomIdText) || ObservedStableRoomId <= 0)
			{
				OutError = FString::Printf(TEXT("Calysto V6 Room Contexts contains invalid Stable Room ID '%s'."), *RoomIdText);
				return false;
			}

			const FBox LocalBounds = Point.GetLocalBounds()
				.TransformBy(Point.Transform)
				.TransformBy(DungeonTransform.Inverse());
			FEFCalystoRoomIdentityInputV6 Input;
			Input.LocalCenter = LocalBounds.GetCenter();
			Input.Extents = LocalBounds.GetExtent();
			Input.TopologyKind = TopologyAttribute->GetValueFromItemKey(EntryKey);
			Input.CollisionOrdinal = CollisionAttribute->GetValueFromItemKey(EntryKey);
			Input.RoomFlags = FlagsAttribute->GetValueFromItemKey(EntryKey);
			const int64 RecomputedStableRoomId = FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(
				FloorPlan.FloorSeed,
				Input,
				FloorPlan.RoomIdentityQuantizationCm);
			if (RecomputedStableRoomId != ObservedStableRoomId
				|| ObservedRooms.Contains(ObservedStableRoomId))
			{
				OutError = FString::Printf(
					TEXT("Calysto V6 Room Context identity mismatch or duplicate (observed=%lld recomputed=%lld)."),
					ObservedStableRoomId,
					RecomputedStableRoomId);
				return false;
			}
			RoomInputs.Add(Input);
			FObservedRoom& Observed = ObservedRooms.Add(ObservedStableRoomId);
			Observed.Flags = Input.RoomFlags;
			Observed.ThemeId = ThemeAttribute->GetValueFromItemKey(EntryKey);
			Observed.StyleId = StyleAttribute->GetValueFromItemKey(EntryKey);
			Observed.FloorMaterial = FloorAttribute->GetValueFromItemKey(EntryKey);
			Observed.WallMaterial = WallAttribute->GetValueFromItemKey(EntryKey);
			Observed.RoofMaterial = RoofAttribute->GetValueFromItemKey(EntryKey);
			Observed.FloorMaterialAuthority = FloorAuthorityAttribute->GetValueFromItemKey(EntryKey);
			Observed.WallMaterialAuthority = WallAuthorityAttribute->GetValueFromItemKey(EntryKey);
			Observed.RoofMaterialAuthority = RoofAuthorityAttribute->GetValueFromItemKey(EntryKey);
			Observed.VendorRoomType = VendorRoomTypeAttribute->GetValueFromItemKey(EntryKey).ResolveObject();
			Observed.bVendorOverrideFloorMaterial = VendorOverrideFloorAttribute->GetValueFromItemKey(EntryKey);
			Observed.VendorFloorMaterial = VendorFloorAttribute->GetValueFromItemKey(EntryKey);
			Observed.bVendorOverrideWallMaterial = VendorOverrideWallAttribute->GetValueFromItemKey(EntryKey);
			Observed.VendorWallMaterial = VendorWallAttribute->GetValueFromItemKey(EntryKey);
			Observed.bVendorOverrideRoofMaterial = VendorOverrideRoofAttribute->GetValueFromItemKey(EntryKey);
			Observed.VendorRoofMaterial = VendorRoofAttribute->GetValueFromItemKey(EntryKey);
		}
	}

	if (ContextDataSetCount <= 0 || RoomInputs.IsEmpty()
		|| RoomInputs.Num() > FloorPlan.MaximumRoomRecords)
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 expected 1..%d Room Context records, found %d across %d data sets."),
			FloorPlan.MaximumRoomRecords,
			RoomInputs.Num(),
			ContextDataSetCount);
		return false;
	}

	FEFCalystoRoomManifestV6 FrozenManifest;
	if (!DungeonSubsystem->BuildAndFreezeRoomManifestV6(RoomInputs, FrozenManifest, OutError))
	{
		return false;
	}
	if (FrozenManifest.FloorPlanHash != FloorPlan.FloorPlanHash
		|| FrozenManifest.FloorPlanHash != RuntimeState.V6FloorPlanHash
		|| FrozenManifest.StyleId != FloorPlan.StyleId)
	{
		OutError = FString::Printf(
			TEXT("Calysto V6 authoritative manifest belongs to a different floor plan (manifest=%s active=%s runtime=%s)."),
			*FrozenManifest.FloorPlanHash,
			*FloorPlan.FloorPlanHash,
			*RuntimeState.V6FloorPlanHash);
		return false;
	}
	if (FrozenManifest.Rooms.Num() != ObservedRooms.Num())
	{
		OutError = TEXT("Calysto V6 authoritative manifest cardinality differs from the PCG Room Context output.");
		return false;
	}
	TMap<FName, const FEFCalystoResolvedThemeProfileV6*> ThemesById;
	for (const FEFCalystoResolvedThemeProfileV6& Theme : FloorPlan.Themes)
	{
		if (Theme.ThemeId.IsNone() || Theme.ThemeId == UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId
			|| !IsValid(RuntimeState.ThemeRoomTypes.FindRef(Theme.ThemeId)) || ThemesById.Contains(Theme.ThemeId))
		{
			OutError = TEXT("Calysto V6 cannot verify PCG material metadata against an invalid or duplicate frozen Theme profile.");
			return false;
		}
		ThemesById.Add(Theme.ThemeId, &Theme);
	}
	for (const FEFCalystoRoomContextV6& Context : FrozenManifest.Rooms)
	{
		const FObservedRoom* Observed = ObservedRooms.Find(Context.StableRoomId);
		if (!Observed
			|| Observed->Flags != Context.RoomFlags
			|| Observed->ThemeId != Context.ThemeId
			|| Observed->StyleId != Context.StyleId
			|| Context.StyleId != FrozenManifest.StyleId
			|| Observed->FloorMaterial != Context.EffectiveMaterials.FloorMaterial.ToSoftObjectPath()
			|| Observed->WallMaterial != Context.EffectiveMaterials.WallMaterial.ToSoftObjectPath()
			|| Observed->RoofMaterial != Context.EffectiveMaterials.RoofMaterial.ToSoftObjectPath())
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 PCG metadata disagrees with the authoritative frozen manifest for room %lld."),
				Context.StableRoomId);
			return false;
		}

		const bool bObservedThemed = Observed->ThemeId != UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId;
		const FEFCalystoResolvedThemeProfileV6* ExpectedTheme = Context.bIsThemed
			? ThemesById.FindRef(Context.ThemeId)
			: nullptr;
		const TObjectPtr<UObject> ExpectedRoomType = Context.bIsThemed
			? RuntimeState.ThemeRoomTypes.FindRef(Context.ThemeId) : nullptr;
		if (bObservedThemed != Context.bIsThemed
			|| (Context.bIsThemed && !ExpectedTheme)
			|| (!Context.bIsThemed
				&& Context.ThemeId != UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId)
			|| Observed->VendorRoomType != ExpectedRoomType)
		{
			OutError = FString::Printf(
				TEXT("Calysto V6 PCG Theme/RoomType semantics disagree with the authoritative manifest for room %lld."),
				Context.StableRoomId);
			return false;
		}

		const auto VerifySurfaceContract = [&OutError, &Context](
			const TCHAR* SurfaceLabel,
			const int32 ObservedAuthority,
			const bool bObservedVendorOverride,
			const FSoftObjectPath& ObservedVendorMaterial,
			const EEFCalystoMaterialResolutionModeV6 ExpectedMode,
			const FSoftObjectPath& ExpectedEffectiveMaterial)
		{
			const bool bExpectedVendorOverride = Context.bIsThemed
				&& ExpectedMode == EEFCalystoMaterialResolutionModeV6::Override;
			const EEFCalystoMaterialAuthorityV6 ExpectedAuthority = bExpectedVendorOverride
				? EEFCalystoMaterialAuthorityV6::RoomTheme
				: EEFCalystoMaterialAuthorityV6::Style;
			const FSoftObjectPath ExpectedVendorMaterial = bExpectedVendorOverride
				? ExpectedEffectiveMaterial
				: FSoftObjectPath();
			if (ObservedAuthority != static_cast<int32>(ExpectedAuthority)
				|| bObservedVendorOverride != bExpectedVendorOverride
				|| ObservedVendorMaterial != ExpectedVendorMaterial)
			{
				OutError = FString::Printf(
					TEXT("Calysto V6 PCG %s authority/vendor override semantics disagree with the authoritative manifest for room %lld."),
					SurfaceLabel,
					Context.StableRoomId);
				return false;
			}
			return true;
		};
		const EEFCalystoMaterialResolutionModeV6 FloorMode = ExpectedTheme
			? ExpectedTheme->FloorMaterialMode
			: EEFCalystoMaterialResolutionModeV6::InheritStyle;
		const EEFCalystoMaterialResolutionModeV6 WallMode = ExpectedTheme
			? ExpectedTheme->WallMaterialMode
			: EEFCalystoMaterialResolutionModeV6::InheritStyle;
		const EEFCalystoMaterialResolutionModeV6 RoofMode = ExpectedTheme
			? ExpectedTheme->RoofMaterialMode
			: EEFCalystoMaterialResolutionModeV6::InheritStyle;
		if (!VerifySurfaceContract(
				TEXT("Floor"),
				Observed->FloorMaterialAuthority,
				Observed->bVendorOverrideFloorMaterial,
				Observed->VendorFloorMaterial,
				FloorMode,
				Context.EffectiveMaterials.FloorMaterial.ToSoftObjectPath())
			|| !VerifySurfaceContract(
				TEXT("Wall"),
				Observed->WallMaterialAuthority,
				Observed->bVendorOverrideWallMaterial,
				Observed->VendorWallMaterial,
				WallMode,
				Context.EffectiveMaterials.WallMaterial.ToSoftObjectPath())
			|| !VerifySurfaceContract(
				TEXT("Roof"),
				Observed->RoofMaterialAuthority,
				Observed->bVendorOverrideRoofMaterial,
				Observed->VendorRoofMaterial,
				RoofMode,
				Context.EffectiveMaterials.RoofMaterial.ToSoftObjectPath()))
		{
			return false;
		}
	}

	const FEFCalystoPopulationPlanV6& PopulationPlan = DungeonSubsystem->GetPopulationPlanV6();
	if (PopulationPlan.PopulationHash.IsEmpty()
		|| PopulationPlan.RoomManifestHash != FrozenManifest.ManifestHash
		|| PopulationPlan.FloorPlanHash != FloorPlan.FloorPlanHash)
	{
		OutError = TEXT("Calysto V6 DungeonSubsystem did not expose a population plan bound to the frozen room manifest.");
		return false;
	}
	TArray<FSoftObjectPath> PopulationPreloadPaths;
	if (!FEFCalystoPopulationMaterializerV6::GatherRequiredPreloadPaths(
			PopulationPlan,
			PopulationPreloadPaths,
			OutError))
	{
		return false;
	}
	if (!DungeonSubsystem->BeginPostTopologyContentLoadV6(
			PopulationPreloadPaths,
			OutError))
	{
		return false;
	}
	if (RuntimeState.bPopulationPlanReady
		&& RuntimeState.PopulationPlanV6.PopulationHash != PopulationPlan.PopulationHash)
	{
		OutError = TEXT("Calysto V6 rejected a second, different immutable population plan for this floor.");
		return false;
	}
	RuntimeState.PopulationPlanV6 = PopulationPlan;
	RuntimeState.V6PopulationMaterializationHash.Reset();
	RuntimeState.bPopulationPlanReady = true;
	RuntimeState.bPostTopologyLoadRequested = true;

	RuntimeState.bRoomManifestReady = true;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("CALYSTO_V6_PCG_MANIFEST_VERIFIED rooms=%d eligible=%d themed=%d data_sets=%d style=%s hash=%s population=%s actors=%d chest_contents=%d preload_paths=%d."),
		FrozenManifest.Rooms.Num(),
		FrozenManifest.EligibleRoomCount,
		FrozenManifest.ThemedRoomCount,
		ContextDataSetCount,
		*FrozenManifest.StyleId.ToString(),
		*FrozenManifest.ManifestHash,
		*RuntimeState.PopulationPlanV6.PopulationHash,
		RuntimeState.PopulationPlanV6.ActorDecisionCount,
		RuntimeState.PopulationPlanV6.ChestContentDecisionCount,
		PopulationPreloadPaths.Num());
	return true;
}

bool UEFProceduralPCGSubsystem::ApplyCalystoHarnessAndGenerate(
	UWorld* World,
	AActor* DungeonActor,
	const bool bDestroyActorOnFailure)
{
	if (!IsValid(World) || !IsValid(DungeonActor))
	{
		return false;
	}

	auto FailClosed = [this, World, DungeonActor, bDestroyActorOnFailure](const FString& Reason)
	{
		FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
		RuntimeState.World = World;
		RuntimeState.DungeonActor = DungeonActor;
		RuntimeState.bBootstrapStarted = true;
		World->GetTimerManager().ClearTimer(RuntimeState.DungeonReadinessPollHandle);
		RuntimeState.DungeonReadinessPollHandle.Invalidate();

		// A GenerateLocal call can complete (and broadcast) before the engine returns
		// its task id. Tear down every project-owned side effect before resetting the
		// bookkeeping so an invalid id or synchronous callback failure cannot leave a
		// half-accepted manifest, population, decal, delegate, or watchdog behind.
		if (RuntimeState.bPopulationMaterializationStarted
			|| !RuntimeState.V6PopulationMaterializationHash.IsEmpty())
		{
			RollbackMaterializedPopulationV6(World, RuntimeState);
		}
		if (AEFCalystoDecalPoolOwnerV6* PoolOwner = RuntimeState.DecalPoolOwnerV6.Get())
		{
			if (PoolOwner->DecalPool)
			{
				PoolOwner->DecalPool->ReleaseAllDecals();
			}
			PoolOwner->Destroy();
			RuntimeState.DecalPoolOwnerV6.Reset();
		}
		if (UPCGComponent* ControlledPCG = RuntimeState.ControlledPCGComponent.Get())
		{
			ControlledPCG->OnPCGGraphGeneratedDelegate.RemoveAll(this);
			ControlledPCG->OnPCGGraphCancelledDelegate.RemoveAll(this);
			ControlledPCG->OnPCGGraphCleanedDelegate.RemoveAll(this);
			ControlledPCG->CancelGeneration();
			ControlledPCG->CleanupLocalImmediate(true, true);
		}
		if (ANavMeshBoundsVolume* NavBounds = RuntimeState.NavBoundsVolume.Get())
		{
			NavBounds->Destroy();
		}
		UNavigationSystemV1::UnregisterNavigationInvoker(*DungeonActor);
		RollbackGenerationLaunchStateV6(RuntimeState);
		SetFloorDoorsEnabled(World, false);
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Calysto controlled generation aborted for %s: %s"),
			*DungeonActor->GetName(),
			*Reason);
		if (bDestroyActorOnFailure)
		{
			DungeonActor->Destroy();
			RuntimeState.DungeonActor.Reset();
		}
		if (!RuntimeState.bFailureReported)
		{
			RuntimeState.bFailureReported = true;
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr)
			{
				DungeonSubsystem->NotifyGenerationFailed(TEXT("ADAPTER_FAILED"), Reason);
			}
		}
		return false;
	};
	FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
	if (RuntimeState.GenerateLocalRequestCount != 0)
	{
		return FailClosed(FString::Printf(
			TEXT("GenerateLocal was already requested %d time(s) for this world; refusing to adapt or generate again."),
			RuntimeState.GenerateLocalRequestCount));
	}

	TArray<UPCGComponent*> RuntimePCGComponents;
	TArray<UPCGComponent*> AllPCGComponents;
	DungeonActor->GetComponents(AllPCGComponents);
	for (UPCGComponent* PCGComponent : AllPCGComponents)
	{
		if (EFProceduralRuntimePrivate::IsRuntimePCGComponent(PCGComponent))
		{
			RuntimePCGComponents.Add(PCGComponent);
		}
	}
	if (RuntimePCGComponents.Num() != 1)
	{
		return FailClosed(FString::Printf(
			TEXT("expected exactly one non-editor PCG component, found %d."),
			RuntimePCGComponents.Num()));
	}

	UPCGComponent* RuntimePCG = RuntimePCGComponents[0];
	UPCGGraph* RuntimeGraph = RuntimePCG->GetGraph();
	static const FString ExpectedGraphPath =
		TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");
	if (!IsValid(RuntimeGraph) || RuntimeGraph->GetPathName() != ExpectedGraphPath)
	{
		return FailClosed(FString::Printf(
			TEXT("runtime PCG graph is %s; expected %s."),
			*GetPathNameSafe(RuntimeGraph),
			*ExpectedGraphPath));
	}
	if (RuntimePCG->bGenerated
		|| RuntimePCG->IsGenerating()
		|| RuntimePCG->IsCleaningUp()
		|| RuntimePCG->GetGenerationTaskId() != InvalidPCGTaskId)
	{
		return FailClosed(TEXT("runtime PCG generation or cleanup started before the harness boundary was applied."));
	}
	if (RuntimePCG->GenerationTrigger != EPCGComponentGenerationTrigger::GenerateOnDemand)
	{
		return FailClosed(FString::Printf(
			TEXT("runtime PCG GenerationTrigger is %d; GenerateOnDemand is required."),
			static_cast<int32>(RuntimePCG->GenerationTrigger)));
	}
	const FEFCalystoPCGAdapterResult AdapterResult = FEFCalystoPCGAdapter::TryApply(DungeonActor);
	if (!AdapterResult.bApplied)
	{
		return FailClosed(AdapterResult.FailureReason);
	}
	if (RuntimePCG->bGenerated
		|| RuntimePCG->IsGenerating()
		|| RuntimePCG->IsCleaningUp()
		|| RuntimePCG->GetGenerationTaskId() != InvalidPCGTaskId
		|| RuntimePCG->GetGraph() != RuntimeGraph
		|| RuntimePCG->GenerationTrigger != EPCGComponentGenerationTrigger::GenerateOnDemand)
	{
		return FailClosed(TEXT("GetPiecesShape unexpectedly started or reconfigured runtime PCG generation."));
	}

	UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorPlanV6 FloorPlan = DungeonSubsystem
		? DungeonSubsystem->GetResolvedFloorPlanV6()
		: FEFCalystoResolvedFloorPlanV6();
	FEFCalystoRoomThemeGenerationConfigV6 ThemeConfig;
	FString V6Error;
	if (!DungeonSubsystem
		|| !BuildRoomThemeGenerationConfigV6(
			FloorPlan,
			AdapterResult.ThemeRoomTypes,
			DungeonActor->GetActorTransform(),
			ThemeConfig,
			V6Error))
	{
		return FailClosed(V6Error.IsEmpty()
			? TEXT("the immutable Calysto V6 floor plan is unavailable at the PCG boundary.")
			: V6Error);
	}
	const FEFCalystoPCGRuntimeGraphResultV6 V6GraphResult =
		FEFCalystoPCGRuntimeGraphBuilderV6::TryBuild(
			RuntimeGraph,
			DungeonActor,
			ThemeConfig);
	if (!V6GraphResult.bBuilt || !IsValid(V6GraphResult.RuntimeGraph))
	{
		return FailClosed(V6GraphResult.FailureReason.IsEmpty()
			? TEXT("failed to compose the authoritative transient Calysto V6 graph.")
			: V6GraphResult.FailureReason);
	}
	TSharedPtr<FEFCalystoPCGSingleGenerationGateV6> GenerationGate =
		FEFCalystoPCGSingleGenerationGateV6::CreateAndPrepare(
			RuntimePCG,
			V6GraphResult.RuntimeGraph,
			V6Error);
	if (!GenerationGate.IsValid())
	{
		return FailClosed(V6Error.IsEmpty()
			? TEXT("failed to create the Calysto V6 one-shot generation gate.")
			: V6Error);
	}

	// Bind first so even a synchronous completion cannot be missed. Then seed and issue
	// the one and only controlled generation request for this world.
	TrackDungeonActor(World, DungeonActor, RuntimePCG);
	if (RuntimeState.bPCGGenerationFailed
		|| RuntimeState.ControlledPCGComponent.Get() != RuntimePCG)
	{
		return FailClosed(TEXT("failed to bind the exact runtime PCG component before generation."));
	}
	RuntimeState.GenerationGateV6 = GenerationGate;
	RuntimeState.V6FloorPlanHash = FloorPlan.FloorPlanHash;
	RuntimeState.ThemeRoomTypes = AdapterResult.ThemeRoomTypes;
	RuntimeState.ThemeArchitectureLeases = AdapterResult.RuntimeStrongReferences;
	RuntimeState.V6GraphConfigurationFingerprint = V6GraphResult.ConfigurationFingerprint;
	if (!EnsureDecalPoolV6(World, RuntimeState, V6Error))
	{
		return FailClosed(V6Error);
	}

	if (!AdapterResult.bGetPiecesShapeInvoked)
	{
		return FailClosed(TEXT("adapter did not invoke the validated GetPiecesShape boundary."));
	}
	RuntimePCG->Seed = AdapterResult.PCGSeed;
	if (!ArmGenerateLocalRequestV6(RuntimeState, V6Error))
	{
		return FailClosed(V6Error.IsEmpty()
			? TEXT("The project-owned readiness trace could not arm the one controlled GenerateLocal request.")
			: V6Error);
	}

	// GenerateLocal may synchronously broadcast completion. GenerateLocal is already
	// the first readiness milestone, so that callback can now append PCGComplete
	// without observing a transiently invalid trace.
	const FPCGTaskId GenerationTaskId = GenerationGate->GenerateOnce(V6Error, true);
	RuntimeState.ControlledGenerationTaskId = GenerationTaskId;
	if (GenerationTaskId == InvalidPCGTaskId)
	{
		return FailClosed(V6Error.IsEmpty()
			? TEXT("PCG rejected the one controlled V6 GenerateLocal request before scheduling a task.")
			: V6Error);
	}
	if (RuntimeState.bPCGGenerationFailed || RuntimeState.bRuntimeReadinessFailed)
	{
		return FailClosed(TEXT("PCG completed synchronously but the authoritative completion callback failed closed."));
	}
	RuntimeState.DungeonReadinessPollStartTimeSeconds = World->GetTimeSeconds();
	ScheduleDungeonReadinessPoll(World, RuntimeState);
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("Calysto V6 requested GenerateLocal exactly once for %s with topology seed %d, floor plan %s, graph config %s, and task %llu."),
		*DungeonActor->GetName(),
		AdapterResult.PCGSeed,
		*FloorPlan.FloorPlanHash,
		*V6GraphResult.ConfigurationFingerprint,
		static_cast<unsigned long long>(GenerationTaskId));
	RefreshDungeonRuntimeState(World, RuntimeState);
	return true;
}

void UEFProceduralPCGSubsystem::TrackDungeonActor(
	UWorld* World,
	AActor* DungeonActor,
	UPCGComponent* ControlledPCGComponent)
{
	if (!IsValid(World) || !IsValid(DungeonActor) || !IsValid(ControlledPCGComponent))
	{
		return;
	}

	FDungeonRuntimeState& RuntimeState = FindOrAddRuntimeState(World);
	RuntimeState.World = World;
	RuntimeState.DungeonActor = DungeonActor;
	RuntimeState.ControlledPCGComponent = ControlledPCGComponent;
	RuntimeState.GenerationGateV6.Reset();
	RuntimeState.V6FloorPlanHash.Reset();
	RuntimeState.V6GraphConfigurationFingerprint.Reset();
	RuntimeState.PopulationPlanV6 = FEFCalystoPopulationPlanV6();
	RuntimeState.V6PopulationMaterializationHash.Reset();
	RuntimeState.bBootstrapStarted = true;
	if (RuntimeState.BootstrapStartTimeSeconds <= 0.0)
	{
		RuntimeState.BootstrapStartTimeSeconds = World->GetTimeSeconds();
	}

	RuntimeState.bPCGGenerationTriggered = true;
	RuntimeState.bPCGGenerationFinished = false;
	RuntimeState.bPCGGenerationFailed = false;
	RuntimeState.bRuntimeReadinessFailed = false;
	RuntimeState.bNavBoundsCreated = false;
	RuntimeState.bNavigationBuildRequested = false;
	RuntimeState.bNavigationReady = false;
	RuntimeState.bTopologyRepairAttempted = false;
	RuntimeState.bTopologyRepairApplied = false;
	RuntimeState.bTopologyRepairAwaitingNavRebuild = false;
	RuntimeState.bTopologyReady = false;
	RuntimeState.bRoomManifestReady = false;
	RuntimeState.bPopulationPlanReady = false;
	RuntimeState.bPostTopologyLoadRequested = false;
	RuntimeState.bPostTopologyAssetsReady = false;
	RuntimeState.bVisualsReady = false;
	RuntimeState.bDecalPoolReady = false;
	RuntimeState.bDecalsRealized = false;
	RuntimeState.RealizedDecalCount = 0;
	RuntimeState.TopologyRepairApproachLocation = FVector::ZeroVector;
	RuntimeState.bSpawnedPawnsRevalidatedAfterNav = false;
	RuntimeState.bPopulationMaterializationStarted = false;
	RuntimeState.bPopulationReady = false;
	RuntimeState.bCompanionRosterReady = false;
	RuntimeState.bFloorDoorDisabledBeforeReadiness = false;
	RuntimeState.bDungeonReady = false;
	RuntimeState.ReadinessTrace.Reset();
	RuntimeState.bFloorReadyNotified = false;
	RuntimeState.bFailureReported = false;
	RuntimeState.NavigationPreparationAttempts = 0;
	RuntimeState.NavigationPathValidationAttempts = 0;
	RuntimeState.TopologyRepairNavRebuildCount = 0;
	RuntimeState.ControlledGenerationTaskId = MAX_uint64;
	RuntimeState.DungeonReadinessPollStartTimeSeconds = -1.0;
	RuntimeState.PendingPCGComponents.Reset();

	TArray<UPCGComponent*> PCGComponents;
	DungeonActor->GetComponents(PCGComponents);
	int32 RuntimePCGComponentCount = 0;
	for (const UPCGComponent* PCGComponent : PCGComponents)
	{
		RuntimePCGComponentCount += EFProceduralRuntimePrivate::IsRuntimePCGComponent(PCGComponent) ? 1 : 0;
	}
	if (RuntimePCGComponentCount != 1
		|| !EFProceduralRuntimePrivate::IsRuntimePCGComponent(ControlledPCGComponent)
		|| ControlledPCGComponent->GetOwner() != DungeonActor)
	{
		RuntimeState.bPCGGenerationFailed = true;
		RuntimeState.bPCGGenerationTriggered = false;
		RuntimeState.ControlledPCGComponent.Reset();
		RuntimeState.PendingPCGComponents.Reset();
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Dungeon actor %s exposed %d runtime PCG components while tracking; exactly one is required."),
			*DungeonActor->GetName(),
			RuntimePCGComponentCount);
		return;
	}

	ControlledPCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
	ControlledPCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
	ControlledPCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
	ControlledPCGComponent->OnPCGGraphGeneratedDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentGenerated);
	ControlledPCGComponent->OnPCGGraphCancelledDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentCancelled);
	ControlledPCGComponent->OnPCGGraphCleanedDelegate.AddUObject(this, &UEFProceduralPCGSubsystem::HandlePCGComponentCleaned);
	RuntimeState.PendingPCGComponents.Add(TObjectKey<UPCGComponent>(ControlledPCGComponent));

	RefreshDungeonRuntimeState(World, RuntimeState);
}

UEFProceduralPCGSubsystem::FDungeonRuntimeState& UEFProceduralPCGSubsystem::FindOrAddRuntimeState(UWorld* World)
{
	return RuntimeStates.FindOrAdd(TObjectKey<UWorld>(World));
}

UEFProceduralPCGSubsystem::FDungeonRuntimeState* UEFProceduralPCGSubsystem::FindRuntimeState(UWorld* World)
{
	return IsValid(World) ? RuntimeStates.Find(TObjectKey<UWorld>(World)) : nullptr;
}

bool UEFProceduralPCGSubsystem::RecordReadinessMilestone(
	FDungeonRuntimeState& RuntimeState,
	const FName Milestone)
{
	static const FName ExpectedTrace[] =
	{
		TEXT("GenerateLocal"),
		TEXT("PCGComplete"),
		TEXT("ManifestReady"),
		TEXT("VisualsReady"),
		TEXT("NavigationPathReady"),
		TEXT("EnemyLevelsReady"),
		TEXT("PopulationRealized"),
		TEXT("CompanionRosterReady"),
		TEXT("DoorEnabled")
	};
	const int32 NextIndex = RuntimeState.ReadinessTrace.Num();
	if (NextIndex < 0 || NextIndex >= UE_ARRAY_COUNT(ExpectedTrace)
		|| Milestone != ExpectedTrace[NextIndex])
	{
		return false;
	}
	RuntimeState.ReadinessTrace.Add(Milestone);
	return true;
}

bool UEFProceduralPCGSubsystem::ArmGenerateLocalRequestV6(
	FDungeonRuntimeState& RuntimeState,
	FString& OutError)
{
	OutError.Reset();
	if (RuntimeState.GenerateLocalRequestCount != 0)
	{
		OutError = FString::Printf(
			TEXT("GenerateLocal was already requested %d time(s); the V6 one-shot launch cannot be armed again."),
			RuntimeState.GenerateLocalRequestCount);
		return false;
	}
	if (!RecordReadinessMilestone(RuntimeState, TEXT("GenerateLocal")))
	{
		OutError = TEXT("The project-owned readiness trace rejected GenerateLocal as its first milestone.");
		return false;
	}

	// Count the attempt before entering PCG. A synchronous completion callback may
	// execute inside GenerateLocalGetTaskId, and a rejected task must remain consumed
	// so no recovery path can issue a second request in the same world.
	++RuntimeState.GenerateLocalRequestCount;
	return true;
}

void UEFProceduralPCGSubsystem::RollbackGenerationLaunchStateV6(
	FDungeonRuntimeState& RuntimeState)
{
	RuntimeState.GenerationGateV6.Reset();
	RuntimeState.ControlledPCGComponent.Reset();
	RuntimeState.NavBoundsVolume.Reset();
	RuntimeState.DecalPoolOwnerV6.Reset();
	RuntimeState.SpawnedPawns.Reset();
	RuntimeState.PendingPCGComponents.Reset();
	RuntimeState.ReadinessTrace.Reset();
	RuntimeState.V6FloorPlanHash.Reset();
	RuntimeState.V6GraphConfigurationFingerprint.Reset();
	RuntimeState.PopulationPlanV6 = FEFCalystoPopulationPlanV6();
	RuntimeState.V6PopulationMaterializationHash.Reset();
	RuntimeState.TopologyRepairApproachLocation = FVector::ZeroVector;
	RuntimeState.bPCGGenerationTriggered = false;
	RuntimeState.bPCGGenerationFinished = false;
	RuntimeState.bPCGGenerationFailed = true;
	RuntimeState.bNavBoundsCreated = false;
	RuntimeState.bNavigationBuildRequested = false;
	RuntimeState.bNavigationReady = false;
	RuntimeState.bTopologyRepairAttempted = false;
	RuntimeState.bTopologyRepairApplied = false;
	RuntimeState.bTopologyRepairAwaitingNavRebuild = false;
	RuntimeState.bTopologyReady = false;
	RuntimeState.bRoomManifestReady = false;
	RuntimeState.bPopulationPlanReady = false;
	RuntimeState.bPostTopologyLoadRequested = false;
	RuntimeState.bPostTopologyAssetsReady = false;
	RuntimeState.bVisualsReady = false;
	RuntimeState.bDecalPoolReady = false;
	RuntimeState.bDecalsRealized = false;
	RuntimeState.RealizedDecalCount = 0;
	RuntimeState.bSpawnedPawnsRevalidatedAfterNav = false;
	RuntimeState.bPopulationMaterializationStarted = false;
	RuntimeState.bPopulationReady = false;
	RuntimeState.bCompanionRosterReady = false;
	RuntimeState.bDungeonReady = false;
	RuntimeState.bFloorReadyNotified = false;
	RuntimeState.ControlledGenerationTaskId = InvalidPCGTaskId;
	RuntimeState.NavigationPreparationAttempts = 0;
	RuntimeState.NavigationPathValidationAttempts = 0;
	RuntimeState.TopologyRepairNavRebuildCount = 0;
	RuntimeState.DungeonReadinessPollStartTimeSeconds = -1.0;
	RuntimeState.DungeonReadinessPollHandle.Invalidate();
}

void UEFProceduralPCGSubsystem::RefreshDungeonRuntimeState(UWorld* World, FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World))
	{
		return;
	}
	ON_SCOPE_EXIT
	{
		if ((RuntimeState.bPCGGenerationFailed || RuntimeState.bRuntimeReadinessFailed)
			&& (RuntimeState.bPopulationMaterializationStarted
				|| !RuntimeState.V6PopulationMaterializationHash.IsEmpty()))
		{
			RollbackMaterializedPopulationV6(World, RuntimeState);
		}
	};
	if (RuntimeState.bPCGGenerationFailed || RuntimeState.bRuntimeReadinessFailed)
	{
		RuntimeState.bDungeonReady = false;
		SetFloorDoorsEnabled(World, false);
		World->GetTimerManager().ClearTimer(RuntimeState.DungeonReadinessPollHandle);
		RuntimeState.DungeonReadinessPollHandle.Invalidate();
		return;
	}

	const bool bWasDungeonReady = RuntimeState.bDungeonReady;
	RuntimeState.SpawnedPawns.RemoveAllSwap([](const TWeakObjectPtr<APawn>& PawnPtr)
	{
		return !PawnPtr.IsValid();
	});

	if (!RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bPCGGenerationTriggered
		&& RuntimeState.PendingPCGComponents.IsEmpty())
	{
		RuntimeState.bPCGGenerationFinished = true;
		if (!RecordReadinessMilestone(RuntimeState, TEXT("PCGComplete")))
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
					? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
					: nullptr)
				{
					DungeonSubsystem->NotifyGenerationFailed(
						TEXT("READINESS_TRACE_ORDER_INVALID"),
						TEXT("The project-owned runtime trace rejected the PCGComplete milestone."));
				}
			}
			return;
		}
		UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("PCGComplete world=%s. Preparing runtime navigation."), *World->GetName());
	}
	if (RuntimeState.bPCGGenerationFinished && !RuntimeState.bRoomManifestReady)
	{
		RuntimeState.bRuntimeReadinessFailed = true;
		RuntimeState.bDungeonReady = false;
		SetFloorDoorsEnabled(World, false);
		if (!RuntimeState.bFailureReported)
		{
			RuntimeState.bFailureReported = true;
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr)
			{
				DungeonSubsystem->NotifyGenerationFailed(
					TEXT("V6_ROOM_MANIFEST_MISSING"),
					TEXT("PCG completed without a verified authoritative V6 room manifest."));
			}
		}
		return;
	}
	if (RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bRoomManifestReady
		&& !RuntimeState.ReadinessTrace.Contains(TEXT("ManifestReady")))
	{
		if (!RecordReadinessMilestone(RuntimeState, TEXT("ManifestReady")))
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			return;
		}
	}
	if (RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bRoomManifestReady
		&& !RuntimeState.bPostTopologyAssetsReady)
	{
		UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		FString AssetReadinessError;
		const EEFCalystoLoadPhaseState AssetState = DungeonSubsystem
			? DungeonSubsystem->GetPostTopologyAssetLoadStateV6(AssetReadinessError)
			: EEFCalystoLoadPhaseState::Failed;
		RuntimeState.bPostTopologyAssetsReady =
			AssetState == EEFCalystoLoadPhaseState::Ready;
		if (!DungeonSubsystem && AssetReadinessError.IsEmpty())
		{
			AssetReadinessError = TEXT("The Calysto Dungeon subsystem is unavailable while checking post-topology assets.");
		}
		if (AssetState == EEFCalystoLoadPhaseState::Failed ||
			AssetState == EEFCalystoLoadPhaseState::NotStarted)
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (DungeonSubsystem)
				{
					DungeonSubsystem->NotifyGenerationFailed(
						TEXT("POST_TOPOLOGY_ASSET_LOAD_FAILED"),
						AssetReadinessError.IsEmpty()
							? TEXT("The post-topology asset load failed before floor readiness.")
							: AssetReadinessError);
				}
			}
			return;
		}
	}
	if (RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bRoomManifestReady
		&& !RuntimeState.bFloorVisualAssetsReady)
	{
		UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		FString FloorVisualReadinessError;
		const EEFCalystoLoadPhaseState FloorVisualState = DungeonSubsystem
			? DungeonSubsystem->GetFloorVisualAssetLoadStateV6(FloorVisualReadinessError)
			: EEFCalystoLoadPhaseState::Failed;
		RuntimeState.bFloorVisualAssetsReady =
			FloorVisualState == EEFCalystoLoadPhaseState::Ready;
		if (!DungeonSubsystem && FloorVisualReadinessError.IsEmpty())
		{
			FloorVisualReadinessError = TEXT("The Calysto Dungeon subsystem is unavailable while checking Floor Visual assets.");
		}
		if (FloorVisualState == EEFCalystoLoadPhaseState::Failed ||
			FloorVisualState == EEFCalystoLoadPhaseState::NotStarted)
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (DungeonSubsystem)
				{
					DungeonSubsystem->NotifyGenerationFailed(
						TEXT("FLOOR_VISUAL_ASSET_LOAD_FAILED"),
						FloorVisualReadinessError.IsEmpty()
							? TEXT("The Floor Visual asset load failed before floor readiness.")
							: FloorVisualReadinessError);
				}
			}
			return;
		}
	}
	if (RuntimeState.bFloorVisualAssetsReady
		&& RuntimeState.bPostTopologyAssetsReady
		&& RuntimeState.bDecalPoolReady
		&& !RuntimeState.bDecalsRealized)
	{
		FString DecalError;
		if (!RealizeDecalsV6(World, RuntimeState, DecalError))
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
					? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
					: nullptr)
				{
					DungeonSubsystem->NotifyGenerationFailed(
						TEXT("V6_DECAL_REALIZATION_FAILED"), DecalError);
				}
			}
			return;
		}
	}
	if (RuntimeState.bFloorVisualAssetsReady
		&& RuntimeState.bPostTopologyAssetsReady
		&& RuntimeState.bDecalPoolReady
		&& RuntimeState.bDecalsRealized
		&& !RuntimeState.bVisualsReady)
	{
		if (!RecordReadinessMilestone(RuntimeState, TEXT("VisualsReady")))
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			return;
		}
		RuntimeState.bVisualsReady = true;
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Log,
			TEXT("VisualsReady world=%s floor_plan=%s graph_config=%s decal_pool_capacity=%d active_decals=%d."),
			*World->GetName(),
			*RuntimeState.V6FloorPlanHash,
			*RuntimeState.V6GraphConfigurationFingerprint,
			RuntimeState.DecalPoolOwnerV6.IsValid()
				&& RuntimeState.DecalPoolOwnerV6->DecalPool
				? RuntimeState.DecalPoolOwnerV6->DecalPool->MaximumDecals
				: 0,
			RuntimeState.RealizedDecalCount);
	}

	if (RuntimeState.bPCGGenerationFinished)
	{
		const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
		UClass* StartPointClass = Settings ? Settings->GetStartPointActorClassResolved().Get() : nullptr;
		TArray<AActor*> StartPointActors;
		if (IsValid(StartPointClass))
		{
			UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, StartPointActors);
			StartPointActors.RemoveAllSwap([](const AActor* Candidate)
			{
				return !IsValid(Candidate);
			});
		}
		int32 StartPointCount = StartPointActors.Num();
#if !UE_BUILD_SHIPPING
		if (bDevelopmentSuppressStartPointOnceForAutomation && StartPointCount == 1)
		{
			int32 AnchorCount = 0;
			int32 DoorCount = 0;
			for (TActorIterator<AEFCalystoPopulationAnchor> AnchorIt(World); AnchorIt; ++AnchorIt)
			{
				AnchorCount += IsValid(*AnchorIt) ? 1 : 0;
			}
			for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
			{
				DoorCount += IsValid(*DoorIt) ? 1 : 0;
			}
			bDevelopmentSuppressStartPointOnceForAutomation = false;
			if (AnchorCount > 0 || DoorCount > 0)
			{
				const FString SuppressedPath = StartPointActors[0]->GetPathName();
				StartPointActors[0]->Destroy();
				StartPointActors.Reset();
				StartPointCount = 0;
				UE_LOG(
					LogEFProceduralPCGRuntime,
					Warning,
					TEXT("CALYSTO_AUTOMATION_START_POINT_SUPPRESSION applied=true actor=%s anchors=%d doors=%d transient=true"),
					*SuppressedPath,
					AnchorCount,
					DoorCount);
			}
			else
			{
				UE_LOG(
					LogEFProceduralPCGRuntime,
					Error,
					TEXT("CALYSTO_AUTOMATION_START_POINT_SUPPRESSION rejected=true reason=no-structural-evidence"));
			}
		}
#endif
		if (!IsValid(StartPointClass) || StartPointCount > 1)
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("Calysto V6 exposed an invalid configured start-point contract after PCGComplete: count=%d class=%s."),
				StartPointCount,
				*GetPathNameSafe(StartPointClass));
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
					? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
					: nullptr)
				{
					const bool bClassInvalid = !IsValid(StartPointClass);
					const FName FailureCode = bClassInvalid
						? FName(TEXT("START_POINT_CLASS_INVALID"))
						: FName(TEXT("START_POINT_CARDINALITY"));
					const FString FailureMessage = bClassInvalid
						? FString(TEXT("The configured V6 start-point class was not loaded after PCGComplete."))
						: FString::Printf(
							TEXT("Expected at most one configured V6 start point before deterministic repair; found %d."),
							StartPointCount);
					DungeonSubsystem->NotifyGenerationFailed(
						FailureCode,
						FailureMessage);
				}
			}
			return;
		}
		if (StartPointCount == 0 && !RuntimeState.bNavigationBuildRequested)
		{
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Warning,
				TEXT("Calysto V6 produced no configured start point after PCGComplete; deterministic project-owned repair is deferred until NavMesh is idle."));
		}
	}

	if (RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bVisualsReady
		&& !RuntimeState.bNavigationBuildRequested)
	{
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
				++RuntimeState.NavigationPathValidationAttempts;
				ResolveOrRepairDungeonTopology(World, RuntimeState);
			}
		}
	}

	if (RuntimeState.bNavigationReady && !RuntimeState.bSpawnedPawnsRevalidatedAfterNav)
	{
		RuntimeState.bSpawnedPawnsRevalidatedAfterNav = true;
		RevalidateSpawnedPawns(World);
	}
	if (RuntimeState.bNavigationReady
		&& RuntimeState.bPopulationPlanReady
		&& RuntimeState.bPostTopologyLoadRequested
		&& RuntimeState.bPostTopologyAssetsReady
		&& !RuntimeState.bPopulationReady
		&& !TryMaterializePopulation(World, RuntimeState))
	{
		RuntimeState.bDungeonReady = false;
		SetFloorDoorsEnabled(World, false);
		return;
	}

	// Managed Calysto floors are never considered playable without confirmed runtime navigation.
	// This stays fail-closed if nav bounds or the navigation system cannot be prepared.
	RuntimeState.bDungeonReady = RuntimeState.bPCGGenerationFinished
		&& RuntimeState.bRoomManifestReady
		&& RuntimeState.bFloorVisualAssetsReady
		&& RuntimeState.bPostTopologyAssetsReady
		&& RuntimeState.bVisualsReady
		&& RuntimeState.bDecalsRealized
		&& RuntimeState.bTopologyReady
		&& RuntimeState.bNavigationReady
		&& RuntimeState.bPopulationReady
		&& RuntimeState.bCompanionRosterReady;
	if (RuntimeState.bDungeonReady && !bWasDungeonReady)
	{
		UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
			: nullptr;
		if (RuntimeState.bFloorReadyNotified || !DungeonSubsystem)
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("Dungeon PCG/navigation/population/companion roster became ready, but the active frozen V6 intent rejected its one-shot floor-ready notification in world %s."),
				*World->GetName());
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				if (DungeonSubsystem)
				{
					DungeonSubsystem->NotifyGenerationFailed(
						TEXT("FLOOR_READY_REJECTED"),
						FString::Printf(
							TEXT("The one-shot V6 floor-ready notification was rejected in world %s."),
							*World->GetName()));
				}
			}
			return;
		}

		SetFloorDoorsEnabled(World, true);
		const FEFCalystoResolvedFloorIntentV6 ReadyIntent = DungeonSubsystem->GetResolvedFloorIntentV6();
		const bool bTraceAccepted = RecordReadinessMilestone(RuntimeState, TEXT("DoorEnabled"));
		UEFCalystoPackagedSmokeSubsystem* PackagedSmokeSubsystem = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UEFCalystoPackagedSmokeSubsystem>()
			: nullptr;
		const bool bTracePersisted = !PackagedSmokeSubsystem
			|| (ReadyIntent.bIsValid
				&& PackagedSmokeSubsystem->RecordRuntimeReadinessTrace(
					World,
					ReadyIntent.GenerationContext.FloorNumber,
					ReadyIntent.GenerationContext.GenerationSerial,
					RuntimeState.ReadinessTrace));
		if (!bTraceAccepted || !bTracePersisted)
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("The project-owned runtime readiness trace failed closed at DoorEnabled in world %s."),
				*World->GetName());
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				DungeonSubsystem->NotifyGenerationFailed(
					TEXT("READINESS_TRACE_PERSIST_FAILED"),
					FString::Printf(
						TEXT("The exact V6 readiness trace could not be persisted in world %s."),
						*World->GetName()));
			}
			return;
		}
		// Persist the exact project-owned trace before NotifyFloorReady broadcasts
		// synchronously to packaged acceptance consumers.
		if (!DungeonSubsystem->NotifyFloorReady())
		{
			RuntimeState.bRuntimeReadinessFailed = true;
			RuntimeState.bDungeonReady = false;
			SetFloorDoorsEnabled(World, false);
			UE_LOG(
				LogEFProceduralPCGRuntime,
				Error,
				TEXT("The exact V6 readiness trace was persisted, but the one-shot floor-ready notification was rejected in world %s."),
				*World->GetName());
			if (!RuntimeState.bFailureReported)
			{
				RuntimeState.bFailureReported = true;
				DungeonSubsystem->NotifyGenerationFailed(
					TEXT("FLOOR_READY_REJECTED"),
					FString::Printf(
						TEXT("The one-shot V6 floor-ready notification was rejected in world %s."),
						*World->GetName()));
			}
			return;
		}
		RuntimeState.bFloorReadyNotified = true;
		// Readiness is terminal for this generation. Cancel the repeating
		// watchdog immediately so a timer already queued for the next frame
		// cannot turn an accepted floor back into a timeout failure.
		World->GetTimerManager().ClearTimer(RuntimeState.DungeonReadinessPollHandle);
		RuntimeState.DungeonReadinessPollHandle.Invalidate();
		UE_LOG(LogEFProceduralPCGRuntime, Log, TEXT("DoorEnabled world=%s. Dungeon runtime ready after PCGComplete <= NavigationPathReady <= EnemyLevelsReady <= PopulationRealized <= CompanionRosterReady."), *World->GetName());
	}
	else if (!RuntimeState.bDungeonReady && RuntimeState.bPCGGenerationFinished)
	{
		ScheduleDungeonReadinessPoll(World, RuntimeState);
	}
}

void UEFProceduralPCGSubsystem::HandlePCGComponentGenerated(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent, true);
}

void UEFProceduralPCGSubsystem::HandlePCGComponentCancelled(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent, false);
}

void UEFProceduralPCGSubsystem::HandlePCGComponentCleaned(UPCGComponent* PCGComponent)
{
	HandleTrackedPCGComponentComplete(PCGComponent, false);
}

void UEFProceduralPCGSubsystem::HandleTrackedPCGComponentComplete(
	UPCGComponent* PCGComponent,
	const bool bSucceeded)
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
			if (RemovedCount == 0 && bSucceeded)
			{
				RuntimeState->bPCGGenerationFailed = true;
				RuntimeState->bPCGGenerationFinished = false;
				RuntimeState->bDungeonReady = false;
				SetFloorDoorsEnabled(World, false);
				UE_LOG(
					LogEFProceduralPCGRuntime,
					Error,
					TEXT("Duplicate or untracked successful PCG callback received from %s; V6 fails closed."),
					*PCGComponent->GetName());
				if (!RuntimeState->bFailureReported)
				{
					RuntimeState->bFailureReported = true;
					if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
						? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
						: nullptr)
					{
						DungeonSubsystem->NotifyGenerationFailed(
							TEXT("PCG_DUPLICATE_CALLBACK"),
							FString::Printf(TEXT("Duplicate successful PCG callback from %s."), *PCGComponent->GetName()));
					}
				}
				RefreshDungeonRuntimeState(World, *RuntimeState);
				return;
			}
			if (RemovedCount > 0 && bSucceeded)
			{
				FString ManifestError;
				if (!FreezeRoomManifestFromPCGOutputV6(
						World,
						PCGComponent,
						*RuntimeState,
						ManifestError))
				{
					RuntimeState->bPCGGenerationFailed = true;
					RuntimeState->bPCGGenerationFinished = false;
					RuntimeState->bRoomManifestReady = false;
					RuntimeState->bDungeonReady = false;
					SetFloorDoorsEnabled(World, false);
					UE_LOG(
						LogEFProceduralPCGRuntime,
						Error,
						TEXT("Calysto V6 rejected post-PCG Room Contexts from %s: %s"),
						*PCGComponent->GetName(),
						*ManifestError);
					if (!RuntimeState->bFailureReported)
					{
						RuntimeState->bFailureReported = true;
						if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
							? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
							: nullptr)
						{
							DungeonSubsystem->NotifyGenerationFailed(
								TEXT("V6_ROOM_MANIFEST_INVALID"),
								ManifestError);
						}
					}
					RefreshDungeonRuntimeState(World, *RuntimeState);
					return;
				}
			}
			if (!bSucceeded)
			{
				RuntimeState->bPCGGenerationFailed = true;
				RuntimeState->bPCGGenerationFinished = false;
				RuntimeState->bDungeonReady = false;
				UE_LOG(
					LogEFProceduralPCGRuntime,
					Error,
					TEXT("Runtime PCG component %s was cancelled or cleaned before a successful floor completed."),
					*PCGComponent->GetName());
				if (!RuntimeState->bFailureReported)
				{
					RuntimeState->bFailureReported = true;
					if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
						? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
						: nullptr)
					{
						DungeonSubsystem->NotifyGenerationFailed(
							TEXT("PCG_CANCELLED"),
							FString::Printf(TEXT("PCG component %s cancelled or cleaned."), *PCGComponent->GetName()));
					}
				}
			}
			if (RemovedCount > 0 || !bSucceeded)
			{
				RefreshDungeonRuntimeState(World, *RuntimeState);
			}
		}
	}
}

void UEFProceduralPCGSubsystem::ScheduleDungeonReadinessPoll(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World)
		|| RuntimeState.bDungeonReady
		|| RuntimeState.bPCGGenerationFailed
		|| RuntimeState.bRuntimeReadinessFailed
		|| World->GetTimerManager().IsTimerActive(RuntimeState.DungeonReadinessPollHandle))
	{
		return;
	}
	if (RuntimeState.DungeonReadinessPollStartTimeSeconds < 0.0)
	{
		RuntimeState.DungeonReadinessPollStartTimeSeconds = World->GetTimeSeconds();
	}

	World->GetTimerManager().SetTimer(
		RuntimeState.DungeonReadinessPollHandle,
		FTimerDelegate::CreateUObject(
			this,
			&UEFProceduralPCGSubsystem::PollDungeonReadiness,
			TWeakObjectPtr<UWorld>(World)),
		EFProceduralRuntimePrivate::DungeonReadinessPollIntervalSeconds,
		true);
}

void UEFProceduralPCGSubsystem::PollDungeonReadiness(TWeakObjectPtr<UWorld> WorldPtr)
{
	if (!WorldPtr.IsValid())
	{
		return;
	}

	UWorld* World = WorldPtr.Get();
	FDungeonRuntimeState* RuntimeState = FindRuntimeState(World);
	if (!RuntimeState)
	{
		return;
	}
	if (RuntimeState->bDungeonReady
		|| RuntimeState->bFloorReadyNotified
		|| RuntimeState->bPCGGenerationFailed
		|| RuntimeState->bRuntimeReadinessFailed)
	{
		World->GetTimerManager().ClearTimer(RuntimeState->DungeonReadinessPollHandle);
		RuntimeState->DungeonReadinessPollHandle.Invalidate();
		return;
	}

	// PCG normally broadcasts OnPCGGraphGeneratedDelegate after setting bGenerated and
	// clearing its generation task. Recover that terminal engine state if a callback is
	// lost, but never infer success while the task is still running. This keeps the
	// readiness contract fail-closed while preventing a completed graph from becoming a
	// false 30-second timeout solely because its delegate was missed.
	if (!RuntimeState->bPCGGenerationFinished && RuntimeState->bPCGGenerationTriggered)
	{
		if (UPCGComponent* PCGComponent = RuntimeState->ControlledPCGComponent.Get())
		{
			const bool bTrackedAsPending = RuntimeState->PendingPCGComponents.Contains(
				TObjectKey<UPCGComponent>(PCGComponent));
			if (bTrackedAsPending && !PCGComponent->IsGenerating() && PCGComponent->bGenerated)
			{
				UE_LOG(
					LogEFProceduralPCGRuntime,
					Warning,
					TEXT("Recovered a completed PCG component whose generated delegate was not observed: component=%s outputTags=%d."),
					*PCGComponent->GetName(),
					PCGComponent->GetGeneratedGraphOutput().TaggedData.Num());
				HandleTrackedPCGComponentComplete(PCGComponent, true);
				return;
			}
		}
	}

	const double PollElapsedSeconds = RuntimeState->DungeonReadinessPollStartTimeSeconds >= 0.0
		? World->GetTimeSeconds() - RuntimeState->DungeonReadinessPollStartTimeSeconds
		: 0.0;
	if (PollElapsedSeconds >= EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds)
	{
		bool bPCGStillGenerating = false;
		bool bPCGMarkedGenerated = false;
		uint64 GenerationTaskId = InvalidPCGTaskId;
		int32 GeneratedOutputTagCount = 0;
		RuntimeState->bRuntimeReadinessFailed = true;
		RuntimeState->bDungeonReady = false;
		if (UPCGComponent* PCGComponent = RuntimeState->ControlledPCGComponent.Get())
		{
			bPCGStillGenerating = PCGComponent->IsGenerating();
			bPCGMarkedGenerated = PCGComponent->bGenerated;
			GenerationTaskId = PCGComponent->GetGenerationTaskId();
			GeneratedOutputTagCount = PCGComponent->GetGeneratedGraphOutput().TaggedData.Num();
			PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
			PCGComponent->OnPCGGraphCancelledDelegate.RemoveAll(this);
			PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
			PCGComponent->CancelGeneration();
			PCGComponent->CleanupLocalImmediate(true, true);
		}
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("Dungeon runtime readiness timed out after %.1f seconds, %d navigation preparation attempts, and %d Start-to-Door path checks in world %s. PCG isGenerating=%s bGenerated=%s requestedTask=%llu currentTask=%llu outputTags=%d pending=%d. Floor doors remain disabled."),
			EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds,
			RuntimeState->NavigationPreparationAttempts,
			RuntimeState->NavigationPathValidationAttempts,
			*World->GetName(),
			bPCGStillGenerating ? TEXT("true") : TEXT("false"),
			bPCGMarkedGenerated ? TEXT("true") : TEXT("false"),
			static_cast<unsigned long long>(RuntimeState->ControlledGenerationTaskId),
			static_cast<unsigned long long>(GenerationTaskId),
			GeneratedOutputTagCount,
			RuntimeState->PendingPCGComponents.Num());
		if (!RuntimeState->bFailureReported)
		{
			RuntimeState->bFailureReported = true;
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr)
			{
				DungeonSubsystem->NotifyGenerationFailed(
					TEXT("GENERATION_TIMEOUT"),
					FString::Printf(
						TEXT("GenerateLocal/PCG/Start-to-Door navigation/population/companion roster did not become ready within %.1f seconds."),
						EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds));
			}
		}
	}

	RefreshDungeonRuntimeState(World, *RuntimeState);
	if (RuntimeState->bDungeonReady
		|| RuntimeState->bPCGGenerationFailed
		|| RuntimeState->bRuntimeReadinessFailed)
	{
		World->GetTimerManager().ClearTimer(RuntimeState->DungeonReadinessPollHandle);
		RuntimeState->DungeonReadinessPollHandle.Invalidate();
	}
}

void UEFProceduralPCGSubsystem::SetFloorDoorsEnabled(UWorld* World, const bool bEnabled) const
{
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			DoorIt->SetEnabled(bEnabled);
		}
	}
}

void UEFProceduralPCGSubsystem::RollbackMaterializedPopulationV6(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World) || !RuntimeState.bPopulationPlanReady)
	{
		return;
	}
	if (AActor* DungeonActor = RuntimeState.DungeonActor.Get())
	{
		FEFCalystoPopulationMaterializerV6::RollbackMaterializedPopulation(
			World,
			DungeonActor,
			RuntimeState.PopulationPlanV6);
	}
	if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr)
	{
		DungeonSubsystem->RollbackPopulationRealizedV6(
			RuntimeState.PopulationPlanV6.PopulationHash,
			RuntimeState.V6PopulationMaterializationHash);
	}
	RuntimeState.V6PopulationMaterializationHash.Reset();
	RuntimeState.bPopulationReady = false;
	RuntimeState.bCompanionRosterReady = false;
}

bool UEFProceduralPCGSubsystem::TryMaterializePopulation(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World)
		|| !RuntimeState.bPCGGenerationFinished
		|| !RuntimeState.bTopologyReady
		|| !RuntimeState.bNavigationReady
		|| !RuntimeState.bRoomManifestReady
		|| !RuntimeState.bPopulationPlanReady
		|| !RuntimeState.bPostTopologyLoadRequested
		|| !RuntimeState.bPostTopologyAssetsReady
		|| RuntimeState.bRuntimeReadinessFailed)
	{
		return false;
	}
	if (RuntimeState.bPopulationReady)
	{
		return true;
	}
	if (RuntimeState.bPopulationMaterializationStarted)
	{
		return false;
	}

	UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	AActor* DungeonActor = RuntimeState.DungeonActor.Get();
	auto FailPopulation = [this, World, DungeonSubsystem, &RuntimeState](const FString& Reason)
	{
		RollbackMaterializedPopulationV6(World, RuntimeState);
		RuntimeState.bRuntimeReadinessFailed = true;
		RuntimeState.bDungeonReady = false;
		SetFloorDoorsEnabled(World, false);
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("V6 PopulationRealized failed closed in world %s: %s"),
			*World->GetName(),
			*Reason);
		if (!RuntimeState.bFailureReported)
		{
			RuntimeState.bFailureReported = true;
			if (DungeonSubsystem)
			{
				DungeonSubsystem->NotifyGenerationFailed(TEXT("V6_POPULATION_FAILED"), Reason);
			}
		}
		return false;
	};
	if (!DungeonSubsystem || !IsValid(DungeonActor))
	{
		return FailPopulation(TEXT("Calysto subsystem or runtime dungeon actor is unavailable."));
	}

	const FEFCalystoRoomManifestV6 RoomManifest = DungeonSubsystem->GetRoomManifestV6();
	if (RoomManifest.ManifestHash.IsEmpty()
		|| RoomManifest.ManifestHash != RuntimeState.PopulationPlanV6.RoomManifestHash
		|| RoomManifest.FloorPlanHash != RuntimeState.V6FloorPlanHash)
	{
		return FailPopulation(TEXT("The immutable V6 population plan no longer matches the active room manifest."));
	}
	FString ValidationError;
	if (!FEFCalystoPopulationMaterializerV6::ValidatePlan(
			RoomManifest,
			RuntimeState.PopulationPlanV6,
			ValidationError))
	{
		return FailPopulation(ValidationError);
	}
	if (!FEFCalystoPopulationMaterializerV6::ValidatePreloadClosureResident(
			RuntimeState.PopulationPlanV6,
			ValidationError))
	{
		return FailPopulation(FString::Printf(
			TEXT("The retained PostTopologyContent lease is incomplete: %s"),
			*ValidationError));
	}

	TArray<FEFCalystoPlacementCandidateV6> PlacementCandidates;
	if (!RuntimeState.ControlledPCGComponent.IsValid()
		|| !FEFCalystoPopulationMaterializerV6::GatherNativePlacementCandidates(
			RuntimeState.ControlledPCGComponent->GetGeneratedGraphOutput(), DungeonActor->GetActorTransform(),
			RoomManifest, RuntimeState.PopulationPlanV6, PlacementCandidates, ValidationError))
	{
		return FailPopulation(ValidationError);
	}
	RuntimeState.bPopulationMaterializationStarted = true;
	const FEFCalystoPopulationMaterializationResultV6 Materialization =
		FEFCalystoPopulationMaterializerV6::Materialize(
			World,
			DungeonActor,
			RoomManifest,
			RuntimeState.PopulationPlanV6,
			MakeArrayView(PlacementCandidates));
	if (!Materialization.bSucceeded)
	{
		return FailPopulation(Materialization.FailureReason);
	}

	TMap<FString, const FEFCalystoPopulationDecisionV6*> ActorDecisions;
	for (const FEFCalystoRoomPopulationPlanV6& Room : RuntimeState.PopulationPlanV6.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.Kind == EEFCalystoPopulationDecisionKindV6::Actor)
			{
				ActorDecisions.Add(Decision.DecisionId.ToLower(), &Decision);
			}
		}
	}
	TArray<FEFCalystoRealizedPopulationActorRecordV6> RealizedActorRecords;
	RealizedActorRecords.Reserve(Materialization.RealizedActors.Num());
	for (const FEFCalystoRealizedPopulationActorV6& Realized : Materialization.RealizedActors)
	{
		const FEFCalystoPopulationDecisionV6* const* DecisionPtr =
			ActorDecisions.Find(Realized.DecisionId.ToLower());
		const FEFCalystoPopulationDecisionV6* Decision =
			DecisionPtr ? *DecisionPtr : nullptr;
		if (!Decision || !Realized.Actor.IsValid())
		{
			return FailPopulation(
				TEXT("The population materializer returned an actor without a live frozen decision."));
		}
		FEFCalystoRealizedPopulationActorRecordV6& Record =
			RealizedActorRecords.AddDefaulted_GetRef();
		Record.StableActorId = FName(*Decision->DecisionId);
		Record.StableRoomId = Decision->StableRoomId;
		Record.CategoryId = Decision->CategoryId;
		Record.CatalogEntryId = Decision->EntryId;
		Record.ActorClass = TSoftClassPtr<AActor>(Decision->ClassPath);
		Record.Transform = Realized.Transform;
		Record.Tier = Decision->Tier;
		Record.Lifecycle = Decision->Lifecycle;
		Record.ThreatCost = FMath::Max(0.0f, Decision->ThreatCost);
		Record.ResourceCost =
			EFProceduralRuntimePrivate::IsCalystoResourceCategory(Decision->CategoryId)
				? 1.0f : 0.0f;
		for (const FString& VerifiedContentId : Realized.VerifiedChestContentDecisionIds)
		{
			Record.VerifiedChestContentIds.Add(FName(*VerifiedContentId));
		}
	}
	if (RealizedActorRecords.Num() != Materialization.SpawnedActorCount)
	{
		return FailPopulation(
			TEXT("The materializer actor records do not match its realized actor count."));
	}
	RuntimeState.V6PopulationMaterializationHash = Materialization.MaterializationHash;
	if (!DungeonSubsystem->NotifyPopulationRealizedV6(
			Materialization.MaterializationHash,
			Materialization.CandidateAnchorCount,
			RealizedActorRecords))
	{
		return FailPopulation(TEXT("The Calysto subsystem rejected the realized V6 population transaction."));
	}
	if (!RecordReadinessMilestone(RuntimeState, TEXT("EnemyLevelsReady")))
	{
		return FailPopulation(
			TEXT("The project-owned runtime trace rejected the EnemyLevelsReady milestone."));
	}
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("EnemyLevelsReady world=%s enemies=%d population=%s."),
		*World->GetName(),
		RuntimeState.PopulationPlanV6.EnemyCount,
		*RuntimeState.PopulationPlanV6.PopulationHash);

	RuntimeState.bPopulationReady = true;
	if (!RecordReadinessMilestone(RuntimeState, TEXT("PopulationRealized")))
	{
		return FailPopulation(
			TEXT("The project-owned runtime trace rejected the PopulationRealized milestone."));
	}
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("PopulationRealizedV6 world=%s population=%s materialization=%s actors=%d enemies=%d chests=%d chest_contents=%d already_applied=%s."),
		*World->GetName(),
		*Materialization.PopulationHash,
		*Materialization.MaterializationHash,
		Materialization.SpawnedActorCount,
		RuntimeState.PopulationPlanV6.EnemyCount,
		RuntimeState.PopulationPlanV6.ChestCount,
		Materialization.VerifiedChestContentCount,
		Materialization.bAlreadyApplied ? TEXT("true") : TEXT("false"));

	// Companion persistence is still a separate project-owned gate. It is
	// intentionally validated only after the authoritative V6 transaction.
	const FEFCalystoResolvedFloorIntentV6 ActiveIntent =
		DungeonSubsystem->GetResolvedFloorIntentV6();
	FString CompanionReadinessError;
	if (!ActiveIntent.bIsValid
		|| !IEFCalystoPopulationBridgeV6::ValidateRegisteredCompanionRosterReady(
			World,
			RuntimeState.PopulationPlanV6,
			ActiveIntent.CompanionRoster.SnapshotHash,
			CompanionReadinessError))
	{
		return FailPopulation(FString::Printf(
			TEXT("CompanionRosterReady validation failed after V6 PopulationRealized: %s"),
			*CompanionReadinessError));
	}
	if (!DungeonSubsystem->NotifyCompanionRosterReady(
			ActiveIntent.CompanionRoster.SnapshotHash))
	{
		return FailPopulation(TEXT("The Calysto subsystem rejected the verified companion-roster snapshot."));
	}
	RuntimeState.bCompanionRosterReady = DungeonSubsystem->IsCompanionRosterReady();
	if (!RuntimeState.bCompanionRosterReady)
	{
		return FailPopulation(TEXT("The companion roster did not remain ready after V6 population acceptance."));
	}
	if (!RecordReadinessMilestone(RuntimeState, TEXT("CompanionRosterReady")))
	{
		return FailPopulation(
			TEXT("The project-owned runtime trace rejected the CompanionRosterReady milestone."));
	}
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Log,
		TEXT("CompanionRosterReady world=%s population=%s companion=%s."),
		*World->GetName(),
		*RuntimeState.PopulationPlanV6.PopulationHash,
		*ActiveIntent.CompanionRoster.SnapshotHash);
	return true;
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
	++RuntimeState.NavigationPreparationAttempts;

	const FBox DungeonBounds = CollectDungeonBounds(World, RuntimeState);
	if (!DungeonBounds.IsValid)
	{
		if (RuntimeState.NavigationPreparationAttempts == 1)
		{
			UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Unable to derive runtime dungeon bounds in world %s. Navigation preparation will retry for up to %.1f seconds."), *World->GetName(), EFProceduralRuntimePrivate::DungeonReadinessTimeoutSeconds);
		}
		return false;
	}

	if (!EnsureNavMeshBoundsVolume(World, DungeonBounds, RuntimeState))
	{
		if (RuntimeState.NavigationPreparationAttempts == 1)
		{
			UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Failed to create/update runtime NavMeshBoundsVolume in world %s. Navigation preparation will retry."), *World->GetName());
		}
		return false;
	}

	if (AActor* DungeonActor = RuntimeState.DungeonActor.Get())
	{
		RegisterDungeonNavigationInvoker(DungeonActor, DungeonBounds);
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		if (RuntimeState.NavigationPreparationAttempts == 1)
		{
			UE_LOG(LogEFProceduralPCGRuntime, Warning, TEXT("Unable to find UNavigationSystemV1 in world %s after preparing runtime nav bounds. Navigation preparation will retry."), *World->GetName());
		}
		return false;
	}

	if (ANavMeshBoundsVolume* NavBoundsVolume = RuntimeState.NavBoundsVolume.Get())
	{
		NavigationSystem->OnNavigationBoundsUpdated(NavBoundsVolume);
	}

	NavigationSystem->Build();
	RuntimeState.bNavigationBuildRequested = true;
	RuntimeState.bNavigationReady = false;
	return true;
}

bool UEFProceduralPCGSubsystem::ResolveOrRepairDungeonTopology(
	UWorld* World,
	FDungeonRuntimeState& RuntimeState)
{
	if (!IsValid(World) || !RuntimeState.bPCGGenerationFinished || !RuntimeState.bNavigationBuildRequested)
	{
		return false;
	}

	auto FailClosed = [this, World, &RuntimeState](const TCHAR* FailureCode, const FString& Reason)
	{
		RuntimeState.bRuntimeReadinessFailed = true;
		RuntimeState.bNavigationReady = false;
		RuntimeState.bTopologyReady = false;
		RuntimeState.bDungeonReady = false;
		SetFloorDoorsEnabled(World, false);
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Error,
			TEXT("TopologyRepair failed closed world=%s code=%s: %s"),
			*World->GetName(),
			FailureCode,
			*Reason);
		if (!RuntimeState.bFailureReported)
		{
			RuntimeState.bFailureReported = true;
			if (UEFCalystoDungeonSubsystem* DungeonSubsystem = World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr)
			{
				DungeonSubsystem->NotifyGenerationFailed(FailureCode, Reason);
			}
		}
		return false;
	};

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem || NavigationSystem->IsNavigationBuildInProgress())
	{
		return false;
	}

	TArray<AEFCalystoFloorDoor*> Doors;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			Doors.Add(*DoorIt);
		}
	}
	Doors.Sort([](const AEFCalystoFloorDoor& Left, const AEFCalystoFloorDoor& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});

	if (Doors.Num() > 1)
	{
		RuntimeState.bTopologyRepairAttempted = true;
		return FailClosed(
			TEXT("FLOOR_DOOR_CARDINALITY"),
			FString::Printf(
				TEXT("PCGComplete and NavMesh idle exposed %d V6 floor doors; topology repair never deletes an ambiguous endpoint."),
				Doors.Num()));
	}

	if (RuntimeState.bTopologyRepairAwaitingNavRebuild)
	{
		if (RuntimeState.TopologyRepairNavRebuildCount != 1 || Doors.Num() != 1)
		{
			return FailClosed(
				TEXT("TOPOLOGY_REPAIR_POSTCONDITION"),
				FString::Printf(
					TEXT("Repair postcondition expected one rebuild and one door; rebuilds=%d doors=%d."),
					RuntimeState.TopologyRepairNavRebuildCount,
					Doors.Num()));
		}

		FVector StartLocation;
		FVector DoorLocation;
		if (!IsDungeonNavigationPathReady(
			World,
			StartLocation,
			DoorLocation,
			&RuntimeState.TopologyRepairApproachLocation))
		{
			return FailClosed(
				TEXT("TOPOLOGY_REPAIR_ROUTE_INVALID"),
				FString::Printf(
					TEXT("The repaired door %s still has no complete route ending within %.0f uu of its interaction origin after the one permitted nav rebuild."),
					*Doors[0]->GetPathName(),
					EFProceduralRuntimePrivate::TopologyDoorMaximumApproachDistance));
		}

		RuntimeState.bTopologyRepairAwaitingNavRebuild = false;
		RuntimeState.bTopologyReady = true;
		RuntimeState.bNavigationReady = true;
		if (!RecordReadinessMilestone(RuntimeState, TEXT("NavigationPathReady")))
		{
			return FailClosed(
				TEXT("READINESS_TRACE_ORDER_INVALID"),
				TEXT("The project-owned runtime trace rejected the repaired NavigationPathReady milestone."));
		}
		const AActor* ReadyStartPoint = FindDungeonStartPoint(World);
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Log,
			TEXT("NavigationPathReady world=%s topologyRepair=applied startPointRepair=%s start=%s door=%s attempts=%d rebuilds=%d."),
			*World->GetName(),
			IsValid(ReadyStartPoint) && ReadyStartPoint->ActorHasTag(EFProceduralRuntimePrivate::StartPointRepairTag)
				? TEXT("applied")
				: TEXT("none"),
			*StartLocation.ToCompactString(),
			*DoorLocation.ToCompactString(),
			RuntimeState.NavigationPathValidationAttempts,
			RuntimeState.TopologyRepairNavRebuildCount);
		return true;
	}

	if (RuntimeState.bTopologyRepairAttempted)
	{
		return RuntimeState.bTopologyReady && RuntimeState.bNavigationReady;
	}
	RuntimeState.bTopologyRepairAttempted = true;
	SetFloorDoorsEnabled(World, false);

	const FBox DungeonBounds = CollectDungeonBounds(World, RuntimeState);
	if (!DungeonBounds.IsValid)
	{
		return FailClosed(
			TEXT("TOPOLOGY_REPAIR_BOUNDS_INVALID"),
			TEXT("CollectDungeonBounds returned no bounded search region for topology repair."));
	}

	const UEFProceduralSettings* Settings = UEFProceduralSettings::Get();
	UClass* StartPointClass = Settings ? Settings->GetStartPointActorClassResolved().Get() : nullptr;
	if (!IsValid(StartPointClass) || !StartPointClass->IsChildOf(AActor::StaticClass()))
	{
		return FailClosed(
			TEXT("START_POINT_CLASS_INVALID"),
			FString::Printf(
				TEXT("The configured V6 start-point class must resolve to an AActor subclass; got %s."),
				*GetPathNameSafe(StartPointClass)));
	}

	TArray<AActor*> StartPoints;
	UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, StartPoints);
	StartPoints.RemoveAllSwap([](const AActor* Candidate)
	{
		return !IsValid(Candidate);
	});
	StartPoints.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	if (StartPoints.Num() > 1)
	{
		return FailClosed(
			TEXT("START_POINT_CARDINALITY"),
			FString::Printf(
				TEXT("NavMesh idle exposed %d configured V6 start points; deterministic repair never deletes an ambiguous entry."),
				StartPoints.Num()));
	}

	AActor* StartPoint = StartPoints.IsEmpty() ? nullptr : StartPoints[0];
	if (!IsValid(StartPoint))
	{
		TArray<EFProceduralRuntimePrivate::FTopologyStartCandidate> StartCandidates;
		StartCandidates.Reserve(EFProceduralRuntimePrivate::TopologyMaximumCandidateCount);
		const FVector BoundsCenter = DungeonBounds.GetCenter();
		const auto TryAddStartCandidate = [
			World,
			NavigationSystem,
			&DungeonBounds,
			&BoundsCenter,
			&StartCandidates](const FVector& RawLocation, const float RawYaw, const FString& Source)
		{
			if (RawLocation.ContainsNaN()
				|| StartCandidates.Num() >= EFProceduralRuntimePrivate::TopologyMaximumCandidateCount)
			{
				return;
			}

			FNavLocation Projected;
			if (!NavigationSystem->ProjectPointToNavigation(
					RawLocation,
					Projected,
					FVector(260.0f, 260.0f, 900.0f))
				|| !DungeonBounds.IsInsideXY(Projected.Location))
			{
				return;
			}

			FVector CandidateLocation(
				FMath::GridSnap(Projected.Location.X, 10.0),
				FMath::GridSnap(Projected.Location.Y, 10.0),
				Projected.Location.Z);
			constexpr float RepairPawnRadius = 42.0f;
			constexpr float RepairPawnHalfHeight = 88.0f;
			const FVector RepairPawnCenter = CandidateLocation
				+ FVector(0.0f, 0.0f, RepairPawnHalfHeight + 4.0f);
			FCollisionQueryParams ClearanceQuery(
				SCENE_QUERY_STAT(EFCalystoStartPointRepairClearance),
				false);
			if (World->OverlapBlockingTestByChannel(
					RepairPawnCenter,
					FQuat::Identity,
					ECC_Pawn,
					FCollisionShape::MakeCapsule(RepairPawnRadius, RepairPawnHalfHeight),
					ClearanceQuery))
			{
				return;
			}
			const bool bDuplicate = StartCandidates.ContainsByPredicate([&CandidateLocation](
				const EFProceduralRuntimePrivate::FTopologyStartCandidate& Existing)
			{
				return Existing.Location.Equals(CandidateLocation, 10.0f);
			});
			if (bDuplicate)
			{
				return;
			}

			EFProceduralRuntimePrivate::FTopologyStartCandidate& Candidate = StartCandidates.AddDefaulted_GetRef();
			Candidate.Location = CandidateLocation;
			Candidate.Yaw = FRotator::ClampAxis(FMath::IsFinite(RawYaw) ? RawYaw : 0.0f);
			Candidate.CenterDistanceSquared = FVector::DistSquared2D(CandidateLocation, BoundsCenter);
			Candidate.Source = Source;
			Candidate.StableKey = FString::Printf(
				TEXT("%s|%s"),
				*Source,
				*EFProceduralRuntimePrivate::QuantizedTopologyTransform(CandidateLocation, Candidate.Yaw));
		};

		int32 RawAnchorCount = 0;
		for (TActorIterator<AEFCalystoPopulationAnchor> AnchorIt(World); AnchorIt; ++AnchorIt)
		{
			if (IsValid(*AnchorIt))
			{
				++RawAnchorCount;
				TryAddStartCandidate(
					AnchorIt->GetActorLocation(),
					AnchorIt->GetActorRotation().Yaw,
					TEXT("PCG"));
			}
		}
		if (RawAnchorCount == 0 && Doors.IsEmpty())
		{
			return FailClosed(
				TEXT("START_POINT_REPAIR_NO_STRUCTURAL_SURFACE"),
				TEXT("PCGComplete exposed no start point, no population anchors, and no floor door; V6 will not fabricate an entry on an empty topology."));
		}

		const FVector BoundsSize = DungeonBounds.GetSize();
		const double GridStep = FMath::Clamp(FMath::Max(BoundsSize.X, BoundsSize.Y) / 48.0, 300.0, 750.0);
		const double GridStartX = FMath::GridSnap(DungeonBounds.Min.X, GridStep);
		const double GridStartY = FMath::GridSnap(DungeonBounds.Min.Y, GridStep);
		for (double Y = GridStartY;
			Y <= DungeonBounds.Max.Y && StartCandidates.Num() < EFProceduralRuntimePrivate::TopologyMaximumCandidateCount;
			Y += GridStep)
		{
			for (double X = GridStartX;
				X <= DungeonBounds.Max.X && StartCandidates.Num() < EFProceduralRuntimePrivate::TopologyMaximumCandidateCount;
				X += GridStep)
			{
				TryAddStartCandidate(FVector(X, Y, BoundsCenter.Z), 0.0f, TEXT("GRID"));
			}
		}

		StartCandidates.Sort([](
			const EFProceduralRuntimePrivate::FTopologyStartCandidate& Left,
			const EFProceduralRuntimePrivate::FTopologyStartCandidate& Right)
		{
			if (!FMath::IsNearlyEqual(Left.CenterDistanceSquared, Right.CenterDistanceSquared, 1.0))
			{
				return Left.CenterDistanceSquared < Right.CenterDistanceSquared;
			}
			return Left.StableKey < Right.StableKey;
		});
		if (StartCandidates.IsEmpty())
		{
			return FailClosed(
				TEXT("START_POINT_REPAIR_NO_CANDIDATE"),
				TEXT("No deterministic PCG-anchor or grid location projected to the idle runtime NavMesh."));
		}

		const EFProceduralRuntimePrivate::FTopologyStartCandidate& SelectedStart = StartCandidates[0];
		const FTransform StartTransform(
			FRotator(0.0f, SelectedStart.Yaw, 0.0f),
			SelectedStart.Location,
			FVector::OneVector);
		StartPoint = World->SpawnActorDeferred<AActor>(
			StartPointClass,
			StartTransform,
			RuntimeState.DungeonActor.Get(),
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(StartPoint))
		{
			return FailClosed(
				TEXT("START_POINT_REPAIR_SPAWN_FAILED"),
				FString::Printf(
					TEXT("Failed to spawn the configured start-point class %s exactly once."),
					*GetPathNameSafe(StartPointClass)));
		}
		StartPoint->SetFlags(RF_Transient);
		StartPoint->FinishSpawning(StartTransform);
		TArray<AActor*> StartPointActorTree;
		StartPointActorTree.Add(StartPoint);
		StartPoint->GetAllChildActors(StartPointActorTree, true);
		for (AActor* StartPointActor : StartPointActorTree)
		{
			if (!IsValid(StartPointActor))
			{
				continue;
			}
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(StartPointActor);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (IsValid(PrimitiveComponent))
				{
					PrimitiveComponent->SetCanEverAffectNavigation(false);
				}
			}
		}
		StartPoint->Tags.AddUnique(EFProceduralRuntimePrivate::StartPointRepairTag);
		StartPoint->Tags.RemoveAll([](const FName& Tag)
		{
			return Tag.ToString().StartsWith(EFProceduralRuntimePrivate::StartPointRepairHashTagPrefix);
		});
		const FString StartRepairHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
			TEXT("V6|START_SPAWN|%s|%s|%s"),
			*StartPointClass->GetPathName(),
			*SelectedStart.StableKey,
			*EFProceduralRuntimePrivate::QuantizedTopologyTransform(SelectedStart.Location, SelectedStart.Yaw)));
		if (StartRepairHash.IsEmpty())
		{
			StartPoint->Destroy();
			return FailClosed(
				TEXT("START_POINT_REPAIR_HASH_FAILED"),
				TEXT("Failed to hash the deterministic start-point repair record."));
		}
		StartPoint->Tags.AddUnique(FName(*(
			EFProceduralRuntimePrivate::StartPointRepairHashTagPrefix + StartRepairHash)));

		TArray<AActor*> RepairedStartPoints;
		UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, RepairedStartPoints);
		RepairedStartPoints.RemoveAllSwap([](const AActor* Candidate)
		{
			return !IsValid(Candidate);
		});
		if (RepairedStartPoints.Num() != 1 || RepairedStartPoints[0] != StartPoint)
		{
			return FailClosed(
				TEXT("START_POINT_REPAIR_POSTCONDITION"),
				FString::Printf(
					TEXT("Start-point repair must leave exactly its one configured actor; found %d."),
					RepairedStartPoints.Num()));
		}
		RuntimeState.bTopologyRepairApplied = true;
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Warning,
			TEXT("StartPointRepair applied world=%s action=spawn class=%s transform=%s candidate=%s candidates=%d repairHash=%s."),
			*World->GetName(),
			*StartPointClass->GetPathName(),
			*StartTransform.ToHumanReadableString(),
			*SelectedStart.StableKey,
			StartCandidates.Num(),
			*StartRepairHash);
	}

	FNavLocation ProjectedStart;
	if (!IsValid(StartPoint)
		|| !NavigationSystem->ProjectPointToNavigation(
			StartPoint->GetActorLocation(),
			ProjectedStart,
			FVector(600.0f, 600.0f, 1400.0f)))
	{
		return FailClosed(
			TEXT("TOPOLOGY_REPAIR_START_INVALID"),
			TEXT("The unique configured Start could not be projected before topology repair."));
	}

	FVector ExistingStart;
	FVector ExistingDoorLocation;
	if (Doors.Num() == 1 && IsDungeonNavigationPathReady(World, ExistingStart, ExistingDoorLocation))
	{
		RuntimeState.bTopologyReady = true;
		RuntimeState.bNavigationReady = true;
		if (!RecordReadinessMilestone(RuntimeState, TEXT("NavigationPathReady")))
		{
			return FailClosed(
				TEXT("READINESS_TRACE_ORDER_INVALID"),
				TEXT("The project-owned runtime trace rejected the native NavigationPathReady milestone."));
		}
		UE_LOG(
			LogEFProceduralPCGRuntime,
			Log,
			TEXT("NavigationPathReady world=%s topologyRepair=not-required startPointRepair=%s sourceDoors=1 start=%s door=%s attempts=%d."),
			*World->GetName(),
			StartPoint->ActorHasTag(EFProceduralRuntimePrivate::StartPointRepairTag)
				? TEXT("applied")
				: TEXT("none"),
			*ExistingStart.ToCompactString(),
			*ExistingDoorLocation.ToCompactString(),
			RuntimeState.NavigationPathValidationAttempts);
		return true;
	}

	const bool bDoorWasMissing = Doors.IsEmpty();
	const FVector OriginalEndpoint = bDoorWasMissing
		? ProjectedStart.Location
		: Doors[0]->GetActorLocation();
	const FTransform OldDoorTransform = bDoorWasMissing
		? FTransform::Identity
		: Doors[0]->GetActorTransform();
	const float PreservedYaw = bDoorWasMissing ? 0.0f : OldDoorTransform.Rotator().Yaw;

	auto AddCandidate = [World, NavigationSystem, &ProjectedStart, &OriginalEndpoint, &DungeonBounds](
		const FVector& RawLocation,
		const float Yaw,
		const bool bPreserveYaw,
		const FString& Source,
		TArray<EFProceduralRuntimePrivate::FTopologyDoorCandidate>& Candidates)
	{
		if (Candidates.Num() >= EFProceduralRuntimePrivate::TopologyMaximumCandidateCount)
		{
			return;
		}
		EFProceduralRuntimePrivate::FTopologyDoorCandidate Candidate;
		if (!EFProceduralRuntimePrivate::TryBuildTopologyCandidate(
				World,
				NavigationSystem,
				ProjectedStart.Location,
				RawLocation,
				OriginalEndpoint,
				Yaw,
				bPreserveYaw,
				Source,
				DungeonBounds,
				Candidate))
		{
			return;
		}
		const bool bDuplicate = Candidates.ContainsByPredicate([&Candidate](
			const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Existing)
		{
			return Existing.StableKey == Candidate.StableKey;
		});
		if (!bDuplicate)
		{
			Candidates.Add(MoveTemp(Candidate));
		}
	};

	TArray<EFProceduralRuntimePrivate::FTopologyDoorCandidate> Candidates;
	if (!bDoorWasMissing)
	{
		static const FVector2D NearDoorOffsets[] =
		{
			FVector2D(200.0, 0.0), FVector2D(-200.0, 0.0),
			FVector2D(0.0, 200.0), FVector2D(0.0, -200.0),
			FVector2D(300.0, 300.0), FVector2D(-300.0, 300.0),
			FVector2D(300.0, -300.0), FVector2D(-300.0, -300.0),
			FVector2D(500.0, 0.0), FVector2D(-500.0, 0.0),
			FVector2D(0.0, 500.0), FVector2D(0.0, -500.0)
		};
		for (int32 OffsetIndex = 0; OffsetIndex < UE_ARRAY_COUNT(NearDoorOffsets); ++OffsetIndex)
		{
			const FVector2D Offset = NearDoorOffsets[OffsetIndex];
			AddCandidate(
				OriginalEndpoint + FVector(Offset.X, Offset.Y, 0.0),
				PreservedYaw,
				true,
				FString::Printf(TEXT("NEAR_%02d"), OffsetIndex),
				Candidates);
		}
		Candidates.RemoveAllSwap([](const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Candidate)
		{
			return Candidate.PathLength < EFProceduralRuntimePrivate::TopologyMinimumPathLength;
		});
	}

	if (Candidates.IsEmpty())
	{
		struct FSortedAnchor
		{
			FVector Location = FVector::ZeroVector;
			float Yaw = 0.0f;
			FString StableKey;
		};
		TArray<FSortedAnchor> SortedAnchors;
		for (TActorIterator<AEFCalystoPopulationAnchor> AnchorIt(World); AnchorIt; ++AnchorIt)
		{
			if (!IsValid(*AnchorIt))
			{
				continue;
			}
			FSortedAnchor& Anchor = SortedAnchors.AddDefaulted_GetRef();
			const FVector RawLocation = AnchorIt->GetActorLocation();
			Anchor.Location = FVector(
				FMath::GridSnap(RawLocation.X, 10.0),
				FMath::GridSnap(RawLocation.Y, 10.0),
				FMath::GridSnap(RawLocation.Z, 10.0));
			Anchor.Yaw = static_cast<float>(FMath::RoundToInt(FRotator::ClampAxis(AnchorIt->GetActorRotation().Yaw)));
			Anchor.StableKey = EFProceduralRuntimePrivate::QuantizedTopologyTransform(Anchor.Location, Anchor.Yaw);
		}
		SortedAnchors.Sort([](const FSortedAnchor& Left, const FSortedAnchor& Right)
		{
			return Left.StableKey < Right.StableKey;
		});
		for (const FSortedAnchor& Anchor : SortedAnchors)
		{
			AddCandidate(
				Anchor.Location,
				bDoorWasMissing ? Anchor.Yaw : PreservedYaw,
				true,
				FString::Printf(TEXT("ANCHOR_%s"), *Anchor.StableKey),
				Candidates);
		}
		if (!bDoorWasMissing)
		{
			Candidates.RemoveAllSwap([](const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Candidate)
			{
				return Candidate.PathLength < EFProceduralRuntimePrivate::TopologyMinimumPathLength;
			});
		}
	}

	if (Candidates.IsEmpty())
	{
		const FVector BoundsSize = DungeonBounds.GetSize();
		const double Step = FMath::Clamp(FMath::Max(BoundsSize.X, BoundsSize.Y) / 36.0, 350.0, 800.0);
		const double StartX = FMath::CeilToDouble(DungeonBounds.Min.X / Step) * Step;
		const double StartY = FMath::CeilToDouble(DungeonBounds.Min.Y / Step) * Step;
		int32 GridOrdinal = 0;
		for (double Y = StartY;
			Y <= DungeonBounds.Max.Y && GridOrdinal < EFProceduralRuntimePrivate::TopologyMaximumCandidateCount;
			Y += Step)
		{
			for (double X = StartX;
				X <= DungeonBounds.Max.X && GridOrdinal < EFProceduralRuntimePrivate::TopologyMaximumCandidateCount;
				X += Step, ++GridOrdinal)
			{
				AddCandidate(
					FVector(X, Y, DungeonBounds.GetCenter().Z),
					PreservedYaw,
					!bDoorWasMissing,
					FString::Printf(TEXT("GRID_%04d"), GridOrdinal),
					Candidates);
			}
		}
		if (!bDoorWasMissing)
		{
			Candidates.RemoveAllSwap([](const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Candidate)
			{
				return Candidate.PathLength < EFProceduralRuntimePrivate::TopologyMinimumPathLength;
			});
		}
	}

	if (Candidates.IsEmpty())
	{
		return FailClosed(
			TEXT("TOPOLOGY_REPAIR_NO_CANDIDATE"),
			FString::Printf(
				TEXT("No deterministic reachable door candidate remained for sourceDoors=%d within bounds %s."),
				Doors.Num(),
				*DungeonBounds.ToString()));
	}

	Candidates.Sort([bDoorWasMissing](
		const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Left,
		const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Right)
	{
		if (bDoorWasMissing && !FMath::IsNearlyEqual(Left.PathLength, Right.PathLength, 0.1f))
		{
			return Left.PathLength > Right.PathLength;
		}
		if (!bDoorWasMissing
			&& !FMath::IsNearlyEqual(Left.OriginalDistanceSquared, Right.OriginalDistanceSquared, 1.0f))
		{
			return Left.OriginalDistanceSquared < Right.OriginalDistanceSquared;
		}
		return Left.StableKey < Right.StableKey;
	});
	const EFProceduralRuntimePrivate::FTopologyDoorCandidate& Selected = Candidates[0];
	FVector NewDoorLocation = Selected.ApproachLocation
		+ Selected.ApproachDirection * EFProceduralRuntimePrivate::TopologyDoorApproachOffset;
	NewDoorLocation.X = FMath::GridSnap(NewDoorLocation.X, 10.0);
	NewDoorLocation.Y = FMath::GridSnap(NewDoorLocation.Y, 10.0);
	NewDoorLocation.Z = Selected.ApproachLocation.Z;
	const FVector DoorScale = bDoorWasMissing ? FVector::OneVector : OldDoorTransform.GetScale3D();
	const FTransform NewDoorTransform(FRotator(0.0f, Selected.Yaw, 0.0f), NewDoorLocation, DoorScale);

	AEFCalystoFloorDoor* Door = bDoorWasMissing ? nullptr : Doors[0];
	if (bDoorWasMissing)
	{
		const UEFCalystoDungeonHarnessSettings* HarnessSettings = UEFCalystoDungeonHarnessSettings::Get();
		UClass* DoorClass = HarnessSettings ? HarnessSettings->DungeonFloorDoorClass.Get() : nullptr;
		if (!IsValid(DoorClass) || !DoorClass->IsChildOf(AEFCalystoFloorDoor::StaticClass()))
		{
			return FailClosed(
				TEXT("TOPOLOGY_REPAIR_DOOR_CLASS_INVALID"),
				FString::Printf(
					TEXT("HarnessSettings.DungeonFloorDoorClass must resolve to an AEFCalystoFloorDoor subclass; got %s."),
					*GetPathNameSafe(DoorClass)));
		}
		Door = World->SpawnActorDeferred<AEFCalystoFloorDoor>(
			DoorClass,
			NewDoorTransform,
			RuntimeState.DungeonActor.Get(),
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Door))
		{
			return FailClosed(
				TEXT("TOPOLOGY_REPAIR_DOOR_SPAWN_FAILED"),
				FString::Printf(TEXT("Failed to spawn configured door class %s exactly once."), *GetPathNameSafe(DoorClass)));
		}
		Door->SetEnabled(false);
		Door->FinishSpawning(NewDoorTransform);
	}
	else
	{
		Door->SetEnabled(false);
		Door->SetActorTransform(NewDoorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	Door->Tags.AddUnique(EFProceduralRuntimePrivate::TopologyRepairTag);
	Door->Tags.RemoveAll([](const FName& Tag)
	{
		return Tag.ToString().StartsWith(EFProceduralRuntimePrivate::TopologyRepairHashTagPrefix);
	});
	const FString RepairHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
		TEXT("V6|%s|%d|%s|%s|%s|%d"),
		bDoorWasMissing ? TEXT("SPAWN") : TEXT("RELOCATE"),
		bDoorWasMissing ? 0 : 1,
		*Selected.StableKey,
		*EFProceduralRuntimePrivate::QuantizedTopologyTransform(NewDoorLocation, Selected.Yaw),
		*EFProceduralRuntimePrivate::QuantizedTopologyTransform(Selected.ApproachLocation, Selected.Yaw),
		FMath::RoundToInt(Selected.PathLength / 10.0f)));
	if (RepairHash.IsEmpty())
	{
		return FailClosed(TEXT("TOPOLOGY_REPAIR_HASH_FAILED"), TEXT("Failed to hash the deterministic topology repair record."));
	}
	Door->Tags.AddUnique(FName(*(EFProceduralRuntimePrivate::TopologyRepairHashTagPrefix + RepairHash)));
	Door->SetEnabled(false);

	int32 ResultDoorCount = 0;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		ResultDoorCount += IsValid(*DoorIt) ? 1 : 0;
	}
	if (ResultDoorCount != 1)
	{
		return FailClosed(
			TEXT("TOPOLOGY_REPAIR_POSTCONDITION"),
			FString::Printf(TEXT("Repair must leave exactly one floor door; found %d."), ResultDoorCount));
	}
	if (RuntimeState.TopologyRepairNavRebuildCount != 0)
	{
		return FailClosed(
			TEXT("TOPOLOGY_REPAIR_DUPLICATE"),
			TEXT("The one-shot topology repair attempted more than one navigation rebuild."));
	}

	RuntimeState.TopologyRepairApproachLocation = Selected.ApproachLocation;
	NavigationSystem->Build();
	++RuntimeState.TopologyRepairNavRebuildCount;
	RuntimeState.bTopologyRepairApplied = true;
	RuntimeState.bTopologyRepairAwaitingNavRebuild = true;
	RuntimeState.bTopologyReady = false;
	RuntimeState.bNavigationReady = false;
	UE_LOG(
		LogEFProceduralPCGRuntime,
		Warning,
		TEXT("TopologyRepair applied world=%s action=%s sourceDoors=%d old=%s new=%s approach=%s approachDistance=%.1f candidate=%s candidates=%d path=%.1f repairHash=%s navRebuild=1."),
		*World->GetName(),
		bDoorWasMissing ? TEXT("spawn") : TEXT("relocate"),
		bDoorWasMissing ? 0 : 1,
		bDoorWasMissing ? TEXT("<missing>") : *OldDoorTransform.ToHumanReadableString(),
		*NewDoorTransform.ToHumanReadableString(),
		*Selected.ApproachLocation.ToCompactString(),
		FVector::Dist2D(NewDoorLocation, Selected.ApproachLocation),
		*Selected.StableKey,
		Candidates.Num(),
		Selected.PathLength,
		*RepairHash);
	return false;
}

bool UEFProceduralPCGSubsystem::IsDungeonNavigationPathReady(
	UWorld* World,
	FVector& OutStartLocation,
	FVector& OutDoorLocation,
	const FVector* RequiredApproachLocation) const
{
	OutStartLocation = FVector::ZeroVector;
	OutDoorLocation = FVector::ZeroVector;
	if (!IsValid(World))
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	AActor* StartPoint = FindDungeonStartPoint(World);
	if (!NavigationSystem || !IsValid(StartPoint))
	{
		return false;
	}

	FNavLocation ProjectedStart;
	if (!NavigationSystem->ProjectPointToNavigation(
			StartPoint->GetActorLocation(),
			ProjectedStart,
			FVector(600.0f, 600.0f, 1400.0f)))
	{
		return false;
	}
	OutStartLocation = ProjectedStart.Location;

	TArray<AEFCalystoFloorDoor*> Doors;
	for (TActorIterator<AEFCalystoFloorDoor> DoorIt(World); DoorIt; ++DoorIt)
	{
		if (IsValid(*DoorIt))
		{
			Doors.Add(*DoorIt);
		}
	}
	Doors.Sort([](const AEFCalystoFloorDoor& Left, const AEFCalystoFloorDoor& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	if (Doors.Num() != 1)
	{
		return false;
	}
	for (AEFCalystoFloorDoor* Door : Doors)
	{
		FNavLocation ProjectedDoor;
		const FVector ProjectionOrigin = RequiredApproachLocation
			? *RequiredApproachLocation
			: Door->GetActorLocation();
		const FVector ProjectionExtent = RequiredApproachLocation
			? FVector(120.0f, 120.0f, 800.0f)
			: FVector(180.0f, 180.0f, 800.0f);
		if (!NavigationSystem->ProjectPointToNavigation(
				ProjectionOrigin,
				ProjectedDoor,
				ProjectionExtent)
			|| (RequiredApproachLocation
				&& FVector::DistSquared2D(ProjectedDoor.Location, *RequiredApproachLocation)
					> FMath::Square(60.0f))
			|| FVector::DistSquared2D(ProjectedDoor.Location, Door->GetActorLocation())
				> FMath::Square(EFProceduralRuntimePrivate::TopologyDoorMaximumApproachDistance))
		{
			continue;
		}

		UNavigationPath* NavigationPath = UNavigationSystemV1::FindPathToLocationSynchronously(
			World,
			ProjectedStart.Location,
			ProjectedDoor.Location);
		if (IsValid(NavigationPath)
			&& NavigationPath->IsValid()
			&& !NavigationPath->IsPartial()
			&& NavigationPath->PathPoints.Num() >= 2
			&& FVector::DistSquared2D(NavigationPath->PathPoints.Last(), Door->GetActorLocation())
				<= FMath::Square(EFProceduralRuntimePrivate::TopologyDoorMaximumApproachDistance))
		{
			OutDoorLocation = Door->GetActorLocation();
			return true;
		}
	}
	return false;
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
	if (UClass* StartPointClass = Settings->GetStartPointActorClassResolved().Get())
	{
		TArray<AActor*> StartPointActors;
		UGameplayStatics::GetAllActorsOfClass(World, StartPointClass, StartPointActors);
		StartPointActors.RemoveAllSwap([](const AActor* StartPointActor)
		{
			return !IsValid(StartPointActor);
		});
		StartPointActors.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});
		if (StartPointActors.Num() == 1)
		{
			return StartPointActors[0];
		}
		return nullptr;
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

		if (Score > BestScore
			|| (Score == BestScore && Score > 0 && IsValid(BestCandidate)
				&& Actor->GetPathName() < BestCandidate->GetPathName()))
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
	if (AEFCalystoFloorDoor* FloorDoor = Cast<AEFCalystoFloorDoor>(SpawnedActor))
	{
		if (UWorld* World = FloorDoor->GetWorld())
		{
			FDungeonRuntimeState* RuntimeState = FindRuntimeState(World);
			const bool bReady = RuntimeState && RuntimeState->bDungeonReady;
			FloorDoor->SetEnabled(bReady);
			if (RuntimeState && !bReady)
			{
				RuntimeState->bFloorDoorDisabledBeforeReadiness = true;
			}
		}
		return;
	}

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
	if (Pawn->ActorHasTag(FName(TEXT("EF.Calysto.Population"))))
	{
		// V6 already projected and collision-validated this deterministic placement.
		// Moving it after the manifest was hashed would break replay guarantees.
		TryEnsurePawnController(PawnPtr, 0);
		return;
	}
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

	UWorld* World = Pawn->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = bRequireNavigation
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
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
	const FRotator PawnRotation = Pawn->GetActorRotation();
	TArray<FVector, TInlineAllocator<18>> AttemptedRawLocations;
	TArray<FVector, TInlineAllocator<18>> AttemptedResolvedLocations;

	auto TryCandidate = [&](const FVector& RawLocation)
	{
		const bool bRawAlreadyAttempted = AttemptedRawLocations.ContainsByPredicate([&RawLocation](const FVector& ExistingLocation)
		{
			return ExistingLocation.Equals(RawLocation, 0.1f);
		});
		if (bRawAlreadyAttempted)
		{
			return false;
		}
		AttemptedRawLocations.Add(RawLocation);

		FVector CandidateLocation = RawLocation;
		if (NavigationSystem)
		{
			FNavLocation NavigationLocation;
			if (!NavigationSystem->ProjectPointToNavigation(RawLocation, NavigationLocation, QueryExtent))
			{
				return false;
			}
			CandidateLocation = NavigationLocation.Location;
		}

		FVector SnappedLocation;
		if (SnapSpawnCandidateToGround(World, Pawn, CandidateLocation, SnappedLocation))
		{
			CandidateLocation = SnappedLocation;
		}
		CandidateLocation.Z += 2.0f;

		const bool bResolvedAlreadyAttempted = AttemptedResolvedLocations.ContainsByPredicate([&CandidateLocation](const FVector& ExistingLocation)
		{
			return ExistingLocation.Equals(CandidateLocation, 5.0f);
		});
		if (bResolvedAlreadyAttempted)
		{
			return false;
		}
		AttemptedResolvedLocations.Add(CandidateLocation);

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

		return false;
	};

	if (TryCandidate(CurrentLocation)
		|| TryCandidate(CurrentLocation + FVector(0.0f, 0.0f, 180.0f)))
	{
		return true;
	}

	const float SearchRadii[] =
	{
		FMath::Max(CollisionRadius * 1.5f, 100.0f),
		FMath::Max(CollisionRadius * 3.0f, 180.0f),
		300.0f,
		450.0f,
		650.0f
	};
	static const FVector2D SearchDirections[] =
	{
		FVector2D(1.0f, 0.0f),
		FVector2D(-1.0f, 0.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, -1.0f),
		FVector2D(0.707f, 0.707f),
		FVector2D(0.707f, -0.707f),
		FVector2D(-0.707f, 0.707f),
		FVector2D(-0.707f, -0.707f)
	};

	for (const float SearchRadius : SearchRadii)
	{
		for (const FVector2D& Direction : SearchDirections)
		{
			const FVector HorizontalOffset(Direction.X * SearchRadius, Direction.Y * SearchRadius, 0.0f);
			if (TryCandidate(CurrentLocation + HorizontalOffset)
				|| TryCandidate(CurrentLocation + HorizontalOffset + FVector(0.0f, 0.0f, 180.0f)))
			{
				return true;
			}
		}
	}

	return false;
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

