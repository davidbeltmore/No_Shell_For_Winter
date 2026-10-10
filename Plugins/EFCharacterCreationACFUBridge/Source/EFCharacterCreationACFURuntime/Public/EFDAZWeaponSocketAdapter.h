#pragma once

#include "Components/ActorComponent.h"
#include "Components/ACFEquipmentComponent.h"
#include "EFDAZWeaponSocketAdapter.generated.h"

/** Missing weapon sockets are resolved per actor; shared meshes remain untouched. */
UCLASS()
class EFCHARACTERCREATIONACFURUNTIME_API UEFDAZWeaponSocketAdapter : public UActorComponent
{
    GENERATED_BODY()
public:
    static void EnsureAndRefresh(AActor* Actor);
    void Refresh();
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION() void EquipmentChanged(const FEquipment& Equipment);
private:
    UPROPERTY(Transient) TObjectPtr<UACFEquipmentComponent> Equipment;
    bool bRefreshQueued = false;
};
