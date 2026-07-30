#pragma once

#include "CoreMinimal.h"
#include "ALSSavableInterface.h"
#include "GameFramework/Actor.h"
#include "Interfaces/ACFInteractableInterface.h"
#include "ProjectLockedInteractableActor.generated.h"

class UProjectLockpickableComponent;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectLockedInteractableActor : public AActor, public IACFInteractableInterface, public IALSSavableInterface
{
	GENERATED_BODY()

public:
	AProjectLockedInteractableActor();

	virtual void BeginPlay() override;
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	virtual void OnInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType = "") override;
	virtual void OnLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType = "") override;
	virtual void OnInteractableRegisteredByPawn_Implementation(APawn* Pawn) override;
	virtual void OnInteractableUnregisteredByPawn_Implementation(APawn* Pawn) override;
	virtual bool CanBeInteracted_Implementation(APawn* Pawn) override;
	virtual FText GetInteractableName_Implementation() override;

	virtual void OnSaved_Implementation() override;
	virtual void OnLoaded_Implementation() override;
	virtual bool ShouldBeIgnored_Implementation() override;
	virtual TArray<UActorComponent*> GetComponentsToSave_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	UProjectLockpickableComponent* GetLockpickableComponent() const;

	UFUNCTION(BlueprintCallable, Category = "ACF|Interaction")
	void ConfigureInteractionSphere();

	UFUNCTION(BlueprintCallable, Category = "ACF|Interaction")
	void RefreshCurrentInteractionOverlaps();

	UFUNCTION(BlueprintNativeEvent, Category = "ACF")
	void OnOriginalInteractedByPawn(APawn* Pawn, const FString& InteractionType);
	virtual void OnOriginalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintNativeEvent, Category = "ACF")
	void OnOriginalLocalInteractedByPawn(APawn* Pawn, const FString& InteractionType);
	virtual void OnOriginalLocalInteractedByPawn_Implementation(APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintNativeEvent, Category = "ACF")
	void OnOriginalInteractableRegisteredByPawn(APawn* Pawn);
	virtual void OnOriginalInteractableRegisteredByPawn_Implementation(APawn* Pawn);

	UFUNCTION(BlueprintNativeEvent, Category = "ACF")
	void OnOriginalInteractableUnregisteredByPawn(APawn* Pawn);
	virtual void OnOriginalInteractableUnregisteredByPawn_Implementation(APawn* Pawn);

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF", meta = (DisplayName = "Interactable Name"))
	FText InteractableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF", meta = (DisplayName = "Is Enabled"))
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction")
	bool bConfigureInteractionSphereForACF = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction", meta = (ClampMin = "0.0"))
	float InteractionSphereRadius = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction")
	TEnumAsByte<ECollisionChannel> InteractionSphereObjectChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction")
	TArray<TEnumAsByte<ECollisionChannel>> InteractionSphereOverlapChannels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction")
	bool bAutoRegisterWithPawnACFInteraction = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Interaction")
	bool bRefreshOverlapsOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Original")
	bool bReplayACFInteractionOnUnlock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF|Debug")
	bool bLogLockpickGate = false;

protected:
	UFUNCTION()
	void HandleUnlockedInteractionRequested(APawn* Pawn, const FString& InteractionType);

private:
	UFUNCTION()
	void HandleInteractionSphereBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleInteractionSphereEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	void RegisterWithPawnInteractionComponent(AActor* OtherActor, UPrimitiveComponent* OtherComp);
	void UnregisterFromPawnInteractionComponent(AActor* OtherActor, UPrimitiveComponent* OtherComp);
	bool TryConsumeACFInteractionProcessEvent(UFunction* Function, void* Parms);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> DefaultSceneRootComponent;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> InteractionSphere;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Lockpicking", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProjectLockpickableComponent> LockpickableComponent;
};
