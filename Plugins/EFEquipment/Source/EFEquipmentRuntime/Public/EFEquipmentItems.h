#pragma once

#include "CoreMinimal.h"
#include "Items/ACFArmor.h"
#include "Items/ACFAccessory.h"
#include "Items/ACFItemFragment.h"
#include "EFEquipmentItems.generated.h"

class UEFEquipmentRuleProfile;
class UEFContainerInventoryComponent;

/** Author one profile reference. Container payload is instance state, not another catalog. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class EFEQUIPMENTRUNTIME_API UEFEquipmentRulesFragment : public UACFItemFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category="Equipment") TObjectPtr<UEFEquipmentRuleProfile> Profile;
    UPROPERTY(SaveGame) int32 ContainerSchema = 1;
    UPROPERTY(SaveGame) TArray<uint8> ContainerPayload;
    UPROPERTY(Transient) TObjectPtr<UEFContainerInventoryComponent> RuntimeInventory;
    virtual void Serialize(FArchive& Ar) override;
    static UEFEquipmentRulesFragment* Find(const UACFItem* Item);
    bool FlushContainer(FString& Error);
};

/** Project-owned native parent for ACFArmorBP. NPC behavior remains native. */
UCLASS(Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentArmor : public UACFArmor
{
    GENERATED_BODY()
public:
    virtual bool CanBeEquipped_Implementation(UACFEquipmentComponent* Equipment) override;
    UPROPERTY(EditDefaultsOnly, Category="Equipment|Prototype") TSubclassOf<AACFItemActor> PrototypeActorClass;
    virtual TSubclassOf<AACFItemActor> GetItemActorClass_Implementation() const override;
    virtual void Internal_OnEquipped(APawn* Owner) override;
    virtual void Internal_OnUnEquipped() override;
private:
    bool bEffectsApplied = false;
};

UCLASS(Blueprintable)
class EFEQUIPMENTRUNTIME_API UEFEquipmentAccessory : public UACFAccessory
{
    GENERATED_BODY()
public:
    virtual bool CanBeEquipped_Implementation(UACFEquipmentComponent* Equipment) override;
    UPROPERTY(EditDefaultsOnly, Category="Equipment|Prototype") TSubclassOf<AACFItemActor> PrototypeActorClass;
    virtual TSubclassOf<AACFItemActor> GetItemActorClass_Implementation() const override;
    virtual void Internal_OnEquipped(APawn* Owner) override;
    virtual void Internal_OnUnEquipped() override;
private:
    bool bEffectsApplied = false;
};
