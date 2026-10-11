#pragma once
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectArrowImpactContext.generated.h"

struct FACFDamageEvent;
class UACMCollisionManagerComponent;

/** Carries arrow identity across ACF's synchronous trace -> damage call.
 * ACF forwards the shooter as DamageDealer, not the projectile. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectArrowImpactContext : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    void Mark(const FHitResult& Hit,AActor* Dealer);
    bool Consume(const FACFDamageEvent& Event);
private:
    void ActorSpawned(AActor* Actor);
    FDelegateHandle SpawnHandle;
    struct FPending { TWeakObjectPtr<AActor> Dealer; FVector Impact; FName Bone; uint64 Frame; };
    TMap<TWeakObjectPtr<AActor>,FPending> Pending;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectArrowImpactObserver : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectArrowImpactObserver();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void CollisionDetected(const FHitResult& Hit);
    TWeakObjectPtr<UACMCollisionManagerComponent> Collision;
};
