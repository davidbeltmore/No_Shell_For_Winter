#include "Defeat/ProjectDefeatTravelSubsystem.h"

#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Defeat/ProjectDefeatFlowComponent.h"
#include "Defeat/ProjectDefeatFlowSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "EFProceduralRuntimeSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectDefeatTravel, Log, All);

namespace ProjectDefeatTravelPrivate
{
	static FString StripPIEPrefix(FString MapName)
	{
		MapName = FPackageName::GetShortName(MapName);
		if (!MapName.StartsWith(TEXT("UEDPIE_"), ESearchCase::IgnoreCase))
		{
			return MapName;
		}

		TArray<FString> Parts;
		MapName.ParseIntoArray(Parts, TEXT("_"), false);
		if (Parts.Num() >= 3)
		{
			return Parts.Last();
		}

		return MapName;
	}
}

void UProjectDefeatTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

void UProjectDefeatTravelSubsystem::Deinitialize()
{
	ClearPendingTransfer();

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	Super::Deinitialize();
}

bool UProjectDefeatTravelSubsystem::BeginDefeatedTravel(
	UObject* WorldContextObject,
	const FProjectDefeatTransferPayload& Payload,
	const FProjectDefeatInventorySnapshot& RetainedInventorySnapshot)
{
	if (!WorldContextObject)
	{
		return false;
	}

	const UProjectDefeatFlowSettings* Settings = UProjectDefeatFlowSettings::Get();
	if (!Settings || Settings->DefeatedMapName.IsNone())
	{
		return false;
	}

	PendingPayload = Payload;
	PendingRetainedInventorySnapshot = RetainedInventorySnapshot;
	bHasPendingTransfer = true;
	PendingWorld = nullptr;

	if (Settings->bUseSameMapDefeatedTravelFastPath && ShouldUseSameMapFastPath(WorldContextObject, *Settings))
	{
		if (UWorld* World = WorldContextObject->GetWorld())
		{
			PendingWorld = World;
			World->GetTimerManager().ClearTimer(ArrivalPollTimerHandle);
			World->GetTimerManager().SetTimer(
				ArrivalPollTimerHandle,
				FTimerDelegate::CreateUObject(this, &ThisClass::PollArrivalReady),
				0.05f,
				true);
			UE_LOG(LogProjectDefeatTravel, Display, TEXT("Using same-map defeated travel fast path for %s."), *World->GetName());
			return true;
		}
	}

	// A full DungeonGeneration reload must retain and replay the active immutable
	// run context. The Calysto subsystem owns the URL options and recalculates the
	// PCG seed at the destination; a raw OpenLevel here would accidentally begin
	// a different run after defeat.
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		const FString TargetShortMapName =
			ProjectDefeatTravelPrivate::StripPIEPrefix(Settings->DefeatedMapName.ToString());
		if (TargetShortMapName.Equals(TEXT("DungeonGeneration"), ESearchCase::IgnoreCase))
		{
			UGameInstance* GameInstance = World->GetGameInstance();
			UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
				? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
				: nullptr;
			if (DungeonSubsystem && DungeonSubsystem->HasActiveRun())
			{
				if (DungeonSubsystem->RequestReplayCurrentFloor())
				{
					UE_LOG(
						LogProjectDefeatTravel,
						Display,
						TEXT("Replaying the active seeded dungeon floor for defeated travel."));
					return true;
				}

				ClearPendingTransfer();
				UE_LOG(
					LogProjectDefeatTravel,
					Error,
					TEXT("Active Calysto run rejected defeated replay; raw travel was not attempted."));
				return false;
			}
		}
	}

	UGameplayStatics::OpenLevel(WorldContextObject, Settings->DefeatedMapName);
	return true;
}

bool UProjectDefeatTravelSubsystem::HasPendingTransfer() const
{
	return bHasPendingTransfer;
}

void UProjectDefeatTravelSubsystem::ClearPendingTransfer()
{
	if (UWorld* World = PendingWorld.Get())
	{
		World->GetTimerManager().ClearTimer(ArrivalPollTimerHandle);
	}

	PendingWorld = nullptr;
	PendingPayload = FProjectDefeatTransferPayload();
	PendingRetainedInventorySnapshot = FProjectDefeatInventorySnapshot();
	bHasPendingTransfer = false;
}

void UProjectDefeatTravelSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!bHasPendingTransfer || !LoadedWorld || !LoadedWorld->IsGameWorld())
	{
		return;
	}

	PendingWorld = LoadedWorld;
	LoadedWorld->GetTimerManager().ClearTimer(ArrivalPollTimerHandle);
	LoadedWorld->GetTimerManager().SetTimer(
		ArrivalPollTimerHandle,
		FTimerDelegate::CreateUObject(this, &ThisClass::PollArrivalReady),
		0.25f,
		true);
}

bool UProjectDefeatTravelSubsystem::ShouldUseSameMapFastPath(
	UObject* WorldContextObject,
	const UProjectDefeatFlowSettings& Settings) const
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || Settings.DefeatedMapName.IsNone())
	{
		return false;
	}

	const FString CurrentShortMapName = ProjectDefeatTravelPrivate::StripPIEPrefix(World->GetMapName());
	const FString TargetShortMapName = ProjectDefeatTravelPrivate::StripPIEPrefix(Settings.DefeatedMapName.ToString());
	return !CurrentShortMapName.IsEmpty()
		&& !TargetShortMapName.IsEmpty()
		&& CurrentShortMapName.Equals(TargetShortMapName, ESearchCase::IgnoreCase);
}

void UProjectDefeatTravelSubsystem::PollArrivalReady()
{
	UWorld* World = PendingWorld.Get();
	if (!bHasPendingTransfer || !World || !World->IsGameWorld())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UEFProceduralRuntimeSubsystem* ProceduralRuntimeSubsystem = GameInstance ? GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>() : nullptr;
	if (ProceduralRuntimeSubsystem && !ProceduralRuntimeSubsystem->IsDungeonRuntimeReady(World))
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	UProjectDefeatFlowComponent* DefeatFlowComponent = PlayerPawn ? PlayerPawn->FindComponentByClass<UProjectDefeatFlowComponent>() : nullptr;
	if (!PlayerController || !PlayerPawn || !DefeatFlowComponent)
	{
		return;
	}

	FTransform SpawnTransform = PlayerPawn->GetActorTransform();
	if (ProceduralRuntimeSubsystem)
	{
		if (!ProceduralRuntimeSubsystem->ResolvePlayerStartTransform(World, SpawnTransform))
		{
			UE_LOG(LogProjectDefeatTravel, VeryVerbose, TEXT("Waiting for defeated travel player start in world %s."), *World->GetName());
			return;
		}
	}

	World->GetTimerManager().ClearTimer(ArrivalPollTimerHandle);
	DefeatFlowComponent->HandleDefeatedArrivalFromTravel(PendingPayload, PendingRetainedInventorySnapshot, SpawnTransform);
	ClearPendingTransfer();
}
