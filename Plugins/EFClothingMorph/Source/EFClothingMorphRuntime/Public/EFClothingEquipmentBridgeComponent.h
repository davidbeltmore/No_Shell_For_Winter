#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "EFClothingEquipmentBridgeComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;

/** Project-owned ACF adapter. No Marketplace dependency or asset replacement.
 * Equipment events start a bounded readiness observation; only actual mesh or
 * visibility changes notify the fitter. Async callbacks cannot resurrect a
 * catalog garment whose exact equipment slot is no longer equipped.
 */
UCLASS(Transient)
class EFCLOTHINGMORPHRUNTIME_API UEFClothingEquipmentBridgeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UEFClothingEquipmentBridgeComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

    void RefreshEquipment();

    UFUNCTION(BlueprintPure, Category="EF Clothing Morph|Equipment")
    FString GetDebugSummary() const;

private:
    UFUNCTION()
    void HandleArmorChanged(FGameplayTag ArmorSlot);

    bool BindEquipment();
    void UnbindEquipment();
    void HandleRenderStateDirty(UActorComponent& Component);
    void Wake();
    void ObserveMeshes();
    bool ReadDesiredSlots();
    bool IsCatalogGarment(const USkeletalMesh* Mesh) const;

    struct FMeshState
    {
        TWeakObjectPtr<USkeletalMesh> Mesh;
        bool bVisible = false;
        bool operator==(const FMeshState& Other) const { return Mesh == Other.Mesh && bVisible == Other.bVisible; }
    };
    TWeakObjectPtr<UActorComponent> Equipment;
    TMap<FName, FSoftObjectPath> DesiredSlots;
    TMap<TWeakObjectPtr<USkeletalMeshComponent>, FMeshState> MeshStates;
    TSet<TWeakObjectPtr<USkeletalMeshComponent>> HiddenByBridge;
    FDelegateHandle RenderDirtyHandle;
    double ObserveUntil = 0.0;
    double NextSafetyScan = 0.0;
    bool bEquipmentDirty = true;
    bool bAuthoritativeSlots = false;
    bool bApplyingVisibility = false;
    uint32 EquipmentEvents = 0;
    uint32 Notifications = 0;
    uint32 Scans = 0;
    uint32 VisibilityCorrections = 0;
};
