#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EFEquipmentTypes.generated.h"

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentSaleQuote
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Equipment") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGuid QuoteId;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGuid ItemGuid;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") int32 Count = 0;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") double TotalPrice = 0;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") TArray<FText> Lines;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FText Message;
};

UENUM(BlueprintType)
enum class EEFEquipmentColumn : uint8 { Outerwear, Underwear };

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentSlotDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Slot") FGameplayTag Slot;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Slot") FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Slot") TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Slot") EEFEquipmentColumn Column = EEFEquipmentColumn::Outerwear;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Slot") int32 Order = 0;
};

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentSlotView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FEFEquipmentSlotDefinition Definition;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGuid OwnerGuid;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FText ItemName;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") bool bReserved = false;
};

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Equipment") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FText Message;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") TArray<FGuid> DisplacedItems;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGameplayTag SelectedSlot;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") int64 Revision = 0;
    /** Transfer identity: splits retain SourceGuid and create a distinct DestinationGuid. */
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGuid SourceGuid;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") FGuid DestinationGuid;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") int32 MovedQuantity = 0;
    UPROPERTY(BlueprintReadOnly, Category="Equipment") int32 SourceRemainingQuantity = 0;
    static FEFEquipmentResult Failure(const FString& Text)
    { FEFEquipmentResult R; R.Message = FText::FromString(Text); return R; }
    static FEFEquipmentResult Success()
    { FEFEquipmentResult R; R.bSuccess = true; return R; }
};

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentWeightSummary
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Weight") double OriginalWeight = 0;
    UPROPERTY(BlueprintReadOnly, Category="Weight") double EffectiveWeight = 0;
    UPROPERTY(BlueprintReadOnly, Category="Weight") double BaseCapacity = 0;
    UPROPERTY(BlueprintReadOnly, Category="Weight") bool bOverburdened = false;
};

USTRUCT(BlueprintType)
struct EFEQUIPMENTRUNTIME_API FEFEquipmentMigrationRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Migration") TSoftClassPtr<class UACFItem> ItemClass;
    UPROPERTY(EditAnywhere, Category="Migration") FGameplayTag OldSlot;
    UPROPERTY(EditAnywhere, Category="Migration") FGameplayTag NewSlot;
};

/** Pure policy shared by runtime, authoring and automation. */
struct EFEQUIPMENTRUNTIME_API FEFEquipmentPolicy
{
    static bool SlotsConflict(TConstArrayView<FGameplayTag> A, TConstArrayView<FGameplayTag> B);
    static bool ValidateSlots(TConstArrayView<FEFEquipmentSlotDefinition> Slots, FString& Error);
    static double EffectiveContentWeight(double Original, double Reduction, bool bEquipped);
};
