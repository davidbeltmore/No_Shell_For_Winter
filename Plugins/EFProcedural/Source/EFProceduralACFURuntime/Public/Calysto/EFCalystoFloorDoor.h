#pragma once

#include "CoreMinimal.h"
#include "Actors/ACFBaseInteractableActor.h"
#include "EFCalystoFloorDoor.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * Project-owned Calysto endpoint door. It advances the unbounded run counter and reloads
 * the configured dungeon map without depending on EFLevelFlow or editing Calysto.
 * EFProceduralPCGRuntime keeps the interaction disabled until PCG and navigation are ready.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROCEDURALACFURUNTIME_API AEFCalystoFloorDoor : public AACFBaseInteractableActor
{
	GENERATED_BODY()

public:
	AEFCalystoFloorDoor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "EF|Calysto Dungeon")
	void SetEnabled(bool bEnabled);

	virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
	virtual void OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
	virtual FText GetInteractableName_Implementation() override;
	virtual bool CanBeInteracted_Implementation(APawn* Pawn) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EF|Calysto Dungeon")
	bool bIsEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EF|Calysto Dungeon", meta = (ClampMin = "1.0"))
	float InteractionRadius = 100.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

private:
	void EndPawnInteraction(APawn* Pawn) const;
	void HandleFloorTravelFailed();
	void RefreshInteractionState();

	bool bTravelRequested = false;
};
