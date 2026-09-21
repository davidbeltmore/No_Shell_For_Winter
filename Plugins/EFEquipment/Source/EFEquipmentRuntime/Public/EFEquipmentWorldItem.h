#pragma once
#include "CoreMinimal.h"
#include "Items/ACFWorldItem.h"
#include "EFEquipmentWorldItem.generated.h"

/** ACF world pickup with aggregate admission for satchels. */
UCLASS(Blueprintable)
class EFEQUIPMENTRUNTIME_API AEFEquipmentWorldItem : public AACFWorldItem
{
    GENERATED_BODY()
public:
    virtual TArray<UActorComponent*> GetComponentsToSave_Implementation() const override;
    virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
    UFUNCTION(BlueprintPure, Category="Equipment|Pickup") bool WasPickupCompleted() const { return bPickupCompleted; }
protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;
private:
    bool bPickupCompleted = false;
};
