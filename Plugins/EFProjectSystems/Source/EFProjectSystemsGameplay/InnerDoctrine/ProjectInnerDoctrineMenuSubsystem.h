#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineMenuSubsystem.generated.h"

class AActor;
class APlayerController;
class APawn;
class UProjectInnerDoctrineComponent;
class UProjectInnerDoctrineExchangeMenuWidget;

struct FProjectDoctrineExchangeMenuStateSnapshot
{
	bool bHasSavedControllerState = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;
	bool bHasSavedMovementState = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	bool bPawnInputSuspended = false;
	bool bHasSavedProjectHudVisibility = false;
	bool bWasProjectHudVisible = false;
	bool bHasSavedPlayerHudVisibility = false;
	bool bWasPlayerHudVisible = true;
	bool bAppliedAcfHudDisable = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineMenuSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	bool OpenMenuForActor(AActor* InteractingActor);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI")
	void CloseMenu();

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI")
	bool IsMenuOpen() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	bool HasTrackedExchangeMenuWidget() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	bool IsTrackedExchangeMenuWidgetInViewport() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	FString GetTrackedExchangeMenuWidgetClassName() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	int32 GetTrackedExchangeMenuSelectedIndex() const;

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI|Debug")
	FProjectInnerDoctrineSnapshot GetTrackedExchangeMenuSnapshot() const;

protected:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleDxpChanged(int32 OldCurrentRunDxp, int32 NewCurrentRunDxp, int32 OldMetaBankDxp, int32 NewMetaBankDxp);

	UFUNCTION()
	void HandleAttributeLevelChanged(EProjectDoctrineAttribute Attribute, int32 OldLevel, int32 NewLevel, int32 NextLevelCost);

	UFUNCTION()
	void HandleMilestoneTriggered(FName AbilityId, EProjectDoctrineAttribute Attribute, int32 Level);

private:
	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController(bool bRemoveWidget);
	void EnsureInnerDoctrineComponent(APawn* Pawn);
	void EnsureExchangeMenuWidget(APlayerController* PlayerController);
	TSubclassOf<UProjectInnerDoctrineExchangeMenuWidget> ResolveExchangeMenuWidgetClass() const;
	void BindToTrackedComponent();
	void UnbindFromTrackedComponent();
	void ApplyMenuInputCapture();
	void RestoreMenuInputCapture();
	void ApplyMenuHudSuppression();
	void RestoreMenuHudSuppression();
	bool TrySetReflectedHudEnabled(class AHUD* HudActor, bool bEnabled) const;
	void RefreshMenuFocusNextTick();
	void RefreshMenuDisplay();
	bool ResolveLocalInteractionContext(AActor* InteractingActor);
	bool IsGamePauseBlockingMenu() const;
	void HandlePurchaseRequested(EProjectDoctrineAttribute Attribute);
	void HandleWithdrawRequested();
	void HandleCloseRequested();

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> TrackedInnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> BoundInnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineExchangeMenuWidget> TrackedExchangeMenuWidget;

	FProjectDoctrineExchangeMenuStateSnapshot MenuStateSnapshot;
	bool bMenuOpen = false;
};
