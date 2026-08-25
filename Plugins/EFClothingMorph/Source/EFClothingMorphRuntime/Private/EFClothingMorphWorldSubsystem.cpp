#include "EFClothingMorphWorldSubsystem.h"

#include "EFClothingFitRuntimeComponent.h"
#include "EFClothingMorphComponent.h"
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
			AttachToPawn(*It);
		}
	}
}

void UEFClothingMorphWorldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}
	ActorSpawnedHandle.Reset();
	Super::Deinitialize();
}

void UEFClothingMorphWorldSubsystem::HandleActorSpawned(AActor* Actor)
{
	APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn || !GetWorld())
	{
		return;
	}

	const TWeakObjectPtr<APawn> WeakPawn(Pawn);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakPawn]()
	{
		AttachToPawn(WeakPawn.Get());
	}));
}

void UEFClothingMorphWorldSubsystem::AttachToPawn(APawn* Pawn)
{
	if (!IsValid(Pawn) || Pawn->FindComponentByClass<UEFClothingFitRuntimeComponent>())
	{
		return;
	}

	// V1 remains available for explicitly authored legacy actors, but V2 owns this pawn at runtime.
	if (UEFClothingMorphComponent* LegacyComponent = Pawn->FindComponentByClass<UEFClothingMorphComponent>())
	{
		LegacyComponent->Deactivate();
		LegacyComponent->SetComponentTickEnabled(false);
	}

	UEFClothingFitRuntimeComponent* Component = NewObject<UEFClothingFitRuntimeComponent>(
		Pawn,
		UEFClothingFitRuntimeComponent::StaticClass(),
		TEXT("EFClothingMorphV2"));
	if (Component)
	{
		Pawn->AddInstanceComponent(Component);
		Component->RegisterComponent();
	}
}
