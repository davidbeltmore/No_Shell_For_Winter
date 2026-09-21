#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "EFEquipmentTypes.h"
#include "EFEquipmentData.generated.h"

UCLASS(BlueprintType)
class EFEQUIPMENTRUNTIME_API UEFEquipmentSlotRegistry : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Equipment") TArray<FEFEquipmentSlotDefinition> Slots;
    UPROPERTY(EditAnywhere, Category="Migration") TArray<FEFEquipmentMigrationRule> MigrationRules;
    const FEFEquipmentSlotDefinition* Find(FGameplayTag Slot) const;
    UFUNCTION(BlueprintCallable, Category="Equipment") bool Validate(FString& Error) const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class EFEQUIPMENTRUNTIME_API UEFEquipmentRuleProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Equipment") TArray<FGameplayTag> AdditionalReservations;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container") bool bContainer = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container", meta=(EditCondition="bContainer", ClampMin="0.01")) double ContentCapacity = 10;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container", meta=(EditCondition="bContainer", ClampMin="0", ClampMax="0.5")) double WeightReduction = 0.1;
    UFUNCTION(BlueprintCallable, Category="Equipment") bool Validate(FString& Error) const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="EF Equipment"))
class EFEQUIPMENTRUNTIME_API UEFEquipmentSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Equipment") bool bEnabled = true;
    UPROPERTY(Config, EditAnywhere, Category="Equipment") TSoftObjectPtr<UEFEquipmentSlotRegistry> SlotRegistry = TSoftObjectPtr<UEFEquipmentSlotRegistry>(FSoftObjectPath(TEXT("/Game/_Game/Data/Equipment/DA_EquipmentSlots.DA_EquipmentSlots")));
};
