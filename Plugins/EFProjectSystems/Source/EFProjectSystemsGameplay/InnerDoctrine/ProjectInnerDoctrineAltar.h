#pragma once

#include "CoreMinimal.h"
#include "Actors/ACFBaseInteractableActor.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineAltar.generated.h"

class UProjectInnerDoctrineComponent;
class APawn;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectInnerDoctrineAltar : public AACFBaseInteractableActor
{
	GENERATED_BODY()

public:
	AProjectInnerDoctrineAltar();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Altar")
	bool SpendDxpOnAttribute(AActor* InteractingActor, EProjectDoctrineAttribute Attribute) const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Altar")
	int32 WithdrawMetaDxp(AActor* InteractingActor, int32 RequestedAmount) const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Altar")
	bool SetDoctrineMasteryMode(AActor* InteractingActor, bool bEnabled) const;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Altar")
	UProjectInnerDoctrineComponent* ResolveInnerDoctrineComponent(AActor* InteractingActor) const;

	virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;
	virtual void OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType) override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|Altar")
	bool OpenExchangeMenu(AActor* InteractingActor) const;

protected:
	/** Project-owned reconstruction of the UE 5.7 Altar presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inner Doctrine|Altar|Components")
	TObjectPtr<USphereComponent> InteractionVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inner Doctrine|Altar|Components")
	TObjectPtr<UStaticMeshComponent> AltarMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inner Doctrine|Altar|Components")
	TObjectPtr<UStaticMeshComponent> BookMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inner Doctrine|Altar|Components")
	TObjectPtr<UStaticMeshComponent> ClothMesh;
};
