#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EFEquipmentWorldSubsystem.generated.h"

UCLASS()
class EFEQUIPMENTRUNTIME_API UEFEquipmentWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
private:
    void ScanPlayers();
    void ActorSpawned(AActor* Actor);
    FTimerHandle ScanTimer;
    FDelegateHandle SpawnHandle;
};
