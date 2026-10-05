#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Survival/ProjectInnerStateMath.h"
#include "ProjectInnerStateExposureVolume.generated.h"

/** Map-independent environmental source. Overlapping volumes compose by source ID. */
UCLASS(Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectInnerStateExposureVolume : public AActor
{
    GENERATED_BODY()
public:
    AProjectInnerStateExposureVolume();
    void RefreshForActor(AActor* Actor) { Enter(this,Actor); }
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UBoxComponent> Bounds;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0",ClampMax="1")) double BreathableAir = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0",ClampMax="1")) double Contamination = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="-100",ClampMax="100")) double ComposureTarget = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State", meta=(ClampMin="0")) double EntryPoisonPoints = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") EProjectPoisonSpeed PoisonSpeed = EProjectPoisonSpeed::Slow;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") FName Toxin=TEXT("Environment");
private:
    UFUNCTION() void Enter(AActor* OverlappedActor,AActor* OtherActor);
    UFUNCTION() void Leave(AActor* OverlappedActor,AActor* OtherActor);
    TSet<TWeakObjectPtr<AActor>> Occupants;
};
