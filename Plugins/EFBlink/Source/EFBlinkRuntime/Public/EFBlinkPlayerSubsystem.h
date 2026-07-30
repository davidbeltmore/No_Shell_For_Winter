#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "EFBlinkPlayerSubsystem.generated.h"

class APlayerController;
class APawn;
class UEFBlinkMorphComponent;

UCLASS()
class EFBLINKRUNTIME_API UEFBlinkPlayerSubsystem : public UTickableWorldSubsystem
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
	void EnsureBlinkComponent(APawn* Pawn);
	void MarkMaintenanceRequired();

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPawn;

	UPROPERTY(Transient)
	TObjectPtr<UEFBlinkMorphComponent> TrackedBlinkComponent;

	bool bNeedsMaintenanceTick = true;
};
