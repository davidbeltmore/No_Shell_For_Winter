#pragma once

#include "CoreMinimal.h"
#include "Items/ACFConsumable.h"
#include "ProjectCompanionRevivalConsumable.generated.h"

class UTexture2D;

/**
 * Winter's Recall is deliberately non-consuming at the ACF layer. The run
 * companion subsystem owns the complete validate/spawn/remove transaction and
 * removes exactly one frozen inventory GUID only after a successful revival.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCompanionRevivalConsumable : public UACFConsumable
{
	GENERATED_BODY()

public:
	UProjectCompanionRevivalConsumable();

	static FName GetStableItemId();

	virtual bool CanBeUsed_Implementation(const APawn* Pawn) const override;

	/** Soft source retained for cook validation; resolved only when this item class is loaded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Companions|Revival",
		meta = (DisplayName = "Reference Icon", ToolTip = "Initial icon from the FullSample blue potion. This does not register Winter's Recall as food."))
	TSoftObjectPtr<UTexture2D> IconSource;

protected:
	virtual void OnItemUsed_Implementation(APawn* Target) override;
};
