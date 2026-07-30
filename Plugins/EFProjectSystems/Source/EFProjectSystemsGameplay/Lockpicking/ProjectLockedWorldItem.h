#pragma once

#include "CoreMinimal.h"
#include "Items/ACFWorldItem.h"
#include "ProjectLockedWorldItem.generated.h"

class UProjectLockpickableComponent;
class USoundBase;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectLockedWorldItem : public AACFWorldItem
{
	GENERATED_BODY()

public:
	AProjectLockedWorldItem();

	virtual void ProcessEvent(UFunction* Function, void* Parms) override;
	virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType = "") override;
	virtual void OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType = "") override;
	virtual bool CanBeInteracted_Implementation(APawn* Pawn) override;
	virtual FText GetInteractableName_Implementation() override;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	UProjectLockpickableComponent* GetLockpickableComponent() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Original")
	bool bReplayACFInteractionOnUnlock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Debug")
	bool bLogLockpickGate = false;

	/** Compatibility contract from the UE 5.7 ACFFullWorldItemBP parent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "ACF|Compatibility")
	TObjectPtr<USoundBase> PickupSound;

protected:
	UFUNCTION()
	void HandleUnlockedInteractionRequested(APawn* Pawn, const FString& InteractionType);

private:
	bool TryConsumeACFInteractionProcessEvent(UFunction* Function, void* Parms);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Lockpicking", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProjectLockpickableComponent> LockpickableComponent;
};
