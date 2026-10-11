#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ProjectNPCOutfit.generated.h"

class UACFItem;

USTRUCT(BlueprintType)
struct FProjectNPCOutfitItem
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftClassPtr<UACFItem> Item;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag Destination;
};

/** Selection data only. ACF owns items, EFEquipment owns slot rules, the clothing Director owns fit. */
UCLASS(BlueprintType)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectNPCOutfit : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FProjectNPCOutfitItem> Items;
    bool Validate(FString& Error) const;
};

USTRUCT(BlueprintType)
struct FProjectNPCOutfitChoice
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UProjectNPCOutfit> Outfit;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Weight = 1.f;
};
