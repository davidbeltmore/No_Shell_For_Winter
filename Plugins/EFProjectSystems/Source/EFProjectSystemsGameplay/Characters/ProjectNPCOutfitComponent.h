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
    // Observations for bounded startup QA. No resource eviction or timing gate
    // depends on these fields; negative durations mean that stage did not run.
    double EngineSecondsAtStart = -1, EngineSecondsAtReady = -1;
    double DefinitionLoadSeconds = -1, ItemClassesLoadSeconds = -1;
    int32 ItemClassesRequested = 0, ItemClassesResidentAtRequest = 0;
    bool bDefinitionResidentAtRequest = false, bRestoredAtStart = false;
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
    void Apply();
    void Fail(const FString& Reason);
    UFUNCTION() void InventoryChanged();
};
