#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectPlayerMorphPhysicsSubsystem.generated.h"

class APawn;
class APlayerController;
class UEFCharacterCustomizationComponent;
class UEFMorphPhysicsConstraintComponent;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectPlayerMorphPhysicsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

protected:
	bool TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController();
	void EnsureMorphPhysicsComponent(APawn* Pawn);
	bool EnsureMaleNeutralMorph(APawn* Pawn);
	void BindCustomizationComponent(UEFCharacterCustomizationComponent* CustomizationComponent);
	void UnbindCustomizationComponent();
	void MarkMaintenanceRequired();

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void HandleCustomizationMorphStateApplied();

protected:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPawn;

	UPROPERTY(Transient)
	TObjectPtr<UEFMorphPhysicsConstraintComponent> TrackedMorphPhysicsComponent;

	UPROPERTY(Transient)
	TObjectPtr<UEFCharacterCustomizationComponent> TrackedCustomizationComponent;

	FDelegateHandle CustomizationMorphStateHandle;
	bool bNeedsMaintenanceTick = true;
	bool bApplyingMaleNeutralMorph = false;
	uint8 LastObservedGenderValue = MAX_uint8;
};
