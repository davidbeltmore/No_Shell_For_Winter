#include "EFClothingMorphWorldSubsystem.h"

#include "EFCharacterCreationAppearanceHooks.h"
#include "EFClothingRuntimeComponent.h"
#include "EFClothingEquipmentBridgeComponent.h"
#include "EFClothingMorphSettings.h"
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
		const UEFClothingMorphSettings* V5Settings = GetDefault<UEFClothingMorphSettings>();
		if (!V5Settings || V5Settings->bPreferEquipmentEvents)
		{
			EFCharacterCreationGameplayHooks::GetOnAppearanceMeshStateChanged().AddUObject(
				this,
				&UEFClothingMorphWorldSubsystem::HandleAppearanceMeshStateChanged);
		}
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &UEFClothingMorphWorldSubsystem::HandleActorSpawned));

		for (TActorIterator<APawn> It(World); It; ++It)
		{
			ObservePawn(*It);
			AttachToPawn(*It);
		}

		// Possession may happen after OnActorSpawned. A low-frequency scan makes
		// player-only attachment deterministic without adding V3 to every NPC.
		const float DiscoveryFallbackSeconds = V5Settings
			? V5Settings->GetWorldDiscoveryFallbackIntervalSeconds()
			: 0.5f;
		World->GetTimerManager().SetTimer(
			EligiblePawnScanTimer,
			this,
			&UEFClothingMorphWorldSubsystem::ScanForEligiblePawns,
			DiscoveryFallbackSeconds,
			true,
			FMath::Min(DiscoveryFallbackSeconds, 0.25f));
	}
}

void UEFClothingMorphWorldSubsystem::Deinitialize()
{
	EFCharacterCreationGameplayHooks::GetOnAppearanceMeshStateChanged().RemoveAll(this);
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

void UEFClothingMorphWorldSubsystem::HandleAppearanceMeshStateChanged(AActor* Actor)
{
	APawn* Pawn = Cast<APawn>(Actor);
	if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld())
	{
		return;
	}
	ObservePawn(Pawn);
	AttachToPawn(Pawn);
	if (UEFClothingMorphV3RuntimeComponent* Runtime =
		Pawn->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
	{
		Runtime->NotifyEquipmentChanged();
	}
	if (UEFClothingEquipmentBridgeComponent* Bridge = Pawn->FindComponentByClass<UEFClothingEquipmentBridgeComponent>())
	{
		Bridge->RefreshEquipment();
	}
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
	if (!IsValid(Pawn))
	{
		return;
	}
	const UEFClothingMorphV2Settings* Settings = GetDefault<UEFClothingMorphV2Settings>();
	const UEFClothingMorphSettings* V5Settings = GetDefault<UEFClothingMorphSettings>();
	if (!Settings
		|| !V5Settings
		|| !Settings->bEnabled
		|| !V5Settings->bEnabled
		|| (!Settings->bEnableForNonPlayerPawns && !Pawn->IsPlayerControlled()))
	{
		return;
	}

	if (!Pawn->FindComponentByClass<UEFClothingMorphV3RuntimeComponent>())
	{
		UEFClothingRuntimeComponent* Component = NewObject<UEFClothingRuntimeComponent>(
			Pawn,
			UEFClothingRuntimeComponent::StaticClass(),
			TEXT("EFClothingRuntime"));
		if (Component)
		{
			Pawn->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
	}
	if (V5Settings->bPreferEquipmentEvents && !Pawn->FindComponentByClass<UEFClothingEquipmentBridgeComponent>())
	{
		UEFClothingEquipmentBridgeComponent* Bridge = NewObject<UEFClothingEquipmentBridgeComponent>(Pawn, TEXT("EFClothingEquipmentBridge"));
		Pawn->AddInstanceComponent(Bridge);
		Bridge->RegisterComponent();
	}
}
