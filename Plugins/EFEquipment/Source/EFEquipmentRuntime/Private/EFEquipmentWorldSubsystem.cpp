#include "EFEquipmentWorldSubsystem.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

bool UEFEquipmentWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}
void UEFEquipmentWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UEFEquipmentWorldSubsystem::ActorSpawned));
    GetWorld()->GetTimerManager().SetTimer(ScanTimer, this, &UEFEquipmentWorldSubsystem::ScanPlayers, 0.25f, true);
}
void UEFEquipmentWorldSubsystem::Deinitialize()
{
    GetWorld()->GetTimerManager().ClearTimer(ScanTimer); GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle); Super::Deinitialize();
}
void UEFEquipmentWorldSubsystem::ActorSpawned(AActor* Actor)
{ if (UEFEquipmentCoordinatorComponent::IsManagedPlayer(Actor)) UEFEquipmentCoordinatorComponent::FindOrCreate(Actor); }
void UEFEquipmentWorldSubsystem::ScanPlayers()
{
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
        if (UEFEquipmentCoordinatorComponent::IsManagedPlayer(*It)) UEFEquipmentCoordinatorComponent::FindOrCreate(*It);
}
