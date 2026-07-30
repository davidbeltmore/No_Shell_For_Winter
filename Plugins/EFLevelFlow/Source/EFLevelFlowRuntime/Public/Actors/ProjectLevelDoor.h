#pragma once

#include "CoreMinimal.h"
#include "Actors/ACFBaseInteractableActor.h"
#include "ALSSavableInterface.h"
#include "ProjectLevelDoor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWorld;

/**
 * Project-owned replacement for the legacy DoorToLevel Blueprint parent.
 *
 * Visual, quest, and map-marker behavior stays composable in a Blueprint child;
 * this class owns only the stable ACF interaction and level-travel contract.
 */
UCLASS(BlueprintType, Blueprintable)
class EFLEVELFLOWRUNTIME_API AProjectLevelDoor : public AACFBaseInteractableActor, public IALSSavableInterface
{
	GENERATED_BODY()

public:
	AProjectLevelDoor();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "ACF")
	void SetEnabled(bool bEnabled);

	// IACFInteractableInterface. The inherited events remain BlueprintCallable.
	virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
	virtual void OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
	virtual FText GetInteractableName_Implementation() override;
	virtual bool CanBeInteracted_Implementation(APawn* Pawn) override;

	// IALSSavableInterface.
	virtual void OnSaved_Implementation() override;
	virtual void OnLoaded_Implementation() override;
	virtual bool ShouldBeIgnored_Implementation() override;
	virtual TArray<UActorComponent*> GetComponentsToSave_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF", meta = (DisplayName = "Interactable Name"))
	FText InteractableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "ACF", meta = (DisplayName = "Is Enabled"))
	bool IsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Flow|Travel")
	TSoftObjectPtr<UWorld> DestinationLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Flow|Travel")
	bool bAbsoluteTravel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Flow|Travel")
	FString TravelOptions;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Sphere;

private:
	void EndPawnInteraction(APawn* Pawn) const;
	bool IsDestinationAvailable() const;

	bool bDestinationAvailable = false;
	bool bTravelRequested = false;
};
