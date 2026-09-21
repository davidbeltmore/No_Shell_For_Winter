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
    virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
};
