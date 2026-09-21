#include "EFEquipmentWorldSubsystem.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "EFEquipmentData.h"
#include "EFEquipmentWorldItem.h"
#include "ACFInventorySettings.h"

bool UEFEquipmentWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}
void UEFEquipmentWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // ACF loads its hard WorldItemClass setting while its module registers CDOs.
    // A hard reference to our subclass there creates a script-registration cycle.
    // Resolve our configurable soft class only after game-world initialization.
    if (const auto* Settings = GetDefault<UEFEquipmentSettings>(); Settings->bEnabled)
    {
        if (auto* PickupClass = Settings->WorldPickupClass.LoadSynchronous())
            GetMutableDefault<UACFInventorySettings>()->WorldItemClass = PickupClass;
        else UE_LOG(LogTemp, Error, TEXT("EFEquipment: The configured world pickup class could not be loaded."));
    }
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
