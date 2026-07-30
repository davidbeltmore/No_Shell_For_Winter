#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectLocomotionOverrideSubsystem.generated.h"

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLocomotionOverrideSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

protected:
	bool TryResolveRuntimeContext();
	void AttachToPlayerController(class APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void EnsureLocomotionComponent(class APawn* Pawn);
	void MarkMaintenanceRequired();

	UFUNCTION()
	void HandlePossessedPawnChanged(class APawn* OldPawn, class APawn* NewPawn);

	void HandleWalkTogglePressed();
	void HandleCrawlTogglePressed();
	void HandleSurrenderPressed();

protected:
	UPROPERTY(Transient)
	TObjectPtr<class APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<class APawn> TrackedPawn;

	UPROPERTY(Transient)
	TObjectPtr<class UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UProjectLocomotionOverrideComponent> TrackedLocomotionComponent;

	bool bNeedsMaintenanceTick = true;
};
