#include "EFClothingMorphWorldSubsystem.h"

#include "EFClothingMorphV3RuntimeComponent.h"
#include "EFClothingMorphV2Settings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

bool UEFClothingMorphWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World || IsRunningDedicatedServer())
	{
		return false;
	}

	return World->WorldType == EWorldType::Game
		|| World->WorldType == EWorldType::PIE
		|| World->WorldType == EWorldType::GamePreview;
}

void UEFClothingMorphWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UWorld* World = GetWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &UEFClothingMorphWorldSubsystem::HandleActorSpawned));

		for (TActorIterator<APawn> It(World); It; ++It)
		{
			ObservePawn(*It);
			AttachToPawn(*It);
		}

		// Possession may happen after OnActorSpawned. A low-frequency scan makes
		// player-only attachment deterministic without adding V3 to every NPC.
		World->GetTimerManager().SetTimer(
			EligiblePawnScanTimer,
			this,
			&UEFClothingMorphWorldSubsystem::ScanForEligiblePawns,
			0.5f,
			true,
			0.25f);
	}
}

void UEFClothingMorphWorldSubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<APawn>& WeakPawn : ControllerObservedPawns)
	{
		if (APawn* Pawn = WeakPawn.Get())
		{
			Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(
				this,
				&UEFClothingMorphWorldSubsystem::HandlePawnControllerChanged);
		}
	}
	ControllerObservedPawns.Reset();

	if (UWorld* World = GetWorld())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		}
		World->GetTimerManager().ClearTimer(EligiblePawnScanTimer);
	}
	ActorSpawnedHandle.Reset();
	Super::Deinitialize();
}

void UEFClothingMorphWorldSubsystem::ScanForEligiblePawns()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			ObservePawn(*It);
			AttachToPawn(*It);
		}
		for (auto It = ControllerObservedPawns.CreateIterator(); It; ++It)
		{
			if (!(*It).IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
}

void UEFClothingMorphWorldSubsystem::HandleActorSpawned(AActor* Actor)
{
	APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn || !GetWorld())
	{
		return;
	}
	ObservePawn(Pawn);

	const TWeakObjectPtr<APawn> WeakPawn(Pawn);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPawn]()
	{
		AttachToPawn(WeakPawn.Get());
	}));
}

void UEFClothingMorphWorldSubsystem::ObservePawn(APawn* Pawn)
{
	if (!IsValid(Pawn) || ControllerObservedPawns.Contains(Pawn))
	{
		return;
	}

	Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(
		this,
		&UEFClothingMorphWorldSubsystem::HandlePawnControllerChanged);
	ControllerObservedPawns.Add(Pawn);
}

void UEFClothingMorphWorldSubsystem::HandlePawnControllerChanged(
	APawn* Pawn,
	AController* OldController,
	AController* NewController)
{
	(void)OldController;
	(void)NewController;
	// Possession can occur between the spawn callback and the periodic safety
	// scan. Attach in the controller-change event so the viewport pre-draw guard
	// exists before newly equipped catalog garments need to be reconciled.
	AttachToPawn(Pawn);
}

void UEFClothingMorphWorldSubsystem::AttachToPawn(APawn* Pawn)
{
	if (!IsValid(Pawn) || Pawn->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
	{
		return;
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	if (!Settings || (!Settings->bEnableForNonPlayerPawns && !Pawn->IsPlayerControlled()))
	{
		return;
	}

	UEFClothingMorphV3RuntimeComponent* Component = NewObject<UEFClothingMorphV3RuntimeComponent>(
		Pawn,
		UEFClothingMorphV3RuntimeComponent::StaticClass(),
		TEXT("EFClothingMorphV3"));
	if (Component)
	{
		Pawn->AddInstanceComponent(Component);
		Component->RegisterComponent();
	}
}
