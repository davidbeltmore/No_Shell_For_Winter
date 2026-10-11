#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Characters/ProjectNPCOutfit.h"
#include "ProjectNPCOutfitComponent.generated.h"

class APawn;
class UACFEquipmentComponent;
class UEFEquipmentCoordinatorComponent;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EProjectNPCOutfitState : uint8 { Pending, Loading, Equipped, Ready, Failed };

/** Owned NPC opt-in. No player controls, commerce, AI or animation replacement. */
UCLASS(ClassGroup=(NPC), meta=(BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCOutfitComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UProjectNPCOutfitComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outfit") bool bEnabled = true;
    /** Optional instance override. Empty inherits the roster; one choice is fixed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outfit") TArray<FProjectNPCOutfitChoice> OverrideChoices;
    UPROPERTY(BlueprintReadOnly, Category="Outfit") EProjectNPCOutfitState State = EProjectNPCOutfitState::Pending;
    UPROPERTY(BlueprintReadOnly, Category="Outfit") FName SelectedId;
    UPROPERTY(BlueprintReadOnly, Category="Outfit") FString Failure;
    UPROPERTY(BlueprintReadOnly, Category="Outfit") double ReadySeconds = 0;
    void Start(const TArray<FProjectNPCOutfitChoice>& Choices);
    void Advance();
    void CaptureInventory();
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UProjectNPCOutfit> Definition;
    UPROPERTY(Transient) TObjectPtr<UACFEquipmentComponent> Equipment;
    UPROPERTY(Transient) TObjectPtr<UEFEquipmentCoordinatorComponent> Coordinator;
    TSharedPtr<FStreamableHandle> Load;
    FString Identity;
    double Started = 0;
    int32 ObservedRestoreSerial = -1;
    bool bRestoring = false;
    bool bDirty = false;
    FTimerHandle CaptureTimer;
    void Apply();
    void Fail(const FString& Reason);
    UFUNCTION() void InventoryChanged();
};
