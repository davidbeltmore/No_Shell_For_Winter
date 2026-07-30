#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProjectCharacterBackgroundSubsystem.generated.h"

class AHUD;
class ACameraActor;
class APlayerController;
class APawn;
class UProjectCharacterBackgroundComponent;
class UProjectCharacterBackgroundCreationWidget;
class UProjectCharacterBackgroundSaveGame;
class UProjectInnerDoctrineComponent;

struct FProjectCharacterBackgroundInputSnapshot
{
	bool bHasSavedControllerState = false;
	bool bWasMouseCursorVisible = false;
	bool bWereClickEventsEnabled = false;
	bool bWereMouseOverEventsEnabled = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;
	bool bHasSavedMovementState = false;
	TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
	uint8 PreviousCustomMovementMode = 0;
	bool bPawnInputSuspended = false;
	bool bHasSavedPauseState = false;
	bool bWasPaused = false;
	bool bHasSavedProjectHudVisibility = false;
	bool bWasProjectHudVisible = false;
	bool bHasSavedPlayerHudVisibility = false;
	bool bWasPlayerHudVisible = true;
	bool bAppliedAcfHudDisable = false;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Character Background|UI")
	bool IsStoryMenuOpen() const { return bStoryMenuOpen; }

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Character Background|Automation")
	bool ConfirmFirstAvailableProfileForAutomation();

	UFUNCTION(BlueprintPure, Category = "Character Background|Automation")
	bool IsPhysicalCreatorActiveForAutomation() const;

	UFUNCTION(BlueprintCallable, Category = "Character Background|Automation")
	bool ApplyPhysicalCreatorPreviewCameraAutomationInputForAutomation(FVector2D OrbitDelta, float WheelDelta);
#endif

private:
	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void TryOpenForWorld(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	bool DoesWorldMatchStorySelectionMap(const UWorld* World) const;
	FString NormalizeMapName(const FString& PackageName) const;
	bool OpenStoryMenu(APlayerController* PlayerController, APawn* Pawn);
	void CloseStoryMenu(bool bRestoreInput);
	void ApplyStoryInputCapture();
	void RestoreStoryInputCapture();
	void ApplyHudSuppression();
	void RestoreHudSuppression();
	bool TrySetReflectedHudEnabled(AHUD* HudActor, bool bEnabled) const;
	UProjectCharacterBackgroundComponent* EnsureBackgroundComponent(APawn* Pawn) const;
	UProjectInnerDoctrineComponent* EnsureInnerDoctrineComponent(APawn* Pawn) const;
	TSubclassOf<UProjectCharacterBackgroundCreationWidget> ResolveCreationWidgetClass() const;
	UProjectCharacterBackgroundSaveGame* LoadProfileSave() const;
	bool SaveProfile(FName BackstoryID, FName ProfessionID, int32 ProfileRevision) const;
	void QueuePhysicalCharacterCreatorLaunch();
	void LaunchPhysicalCharacterCreator();
	void ApplyPhysicalCreatorCameraOverride();
	void RestorePhysicalCreatorCameraOverride();
	void MonitorPhysicalCreatorCameraOverride();
	void FinalizePhysicalCreatorPreviewCameraForStorySelection();
	void ForcePhysicalCreatorPreviewCameraViewTarget();
	ACameraActor* FindOwnedPhysicalCreatorPreviewCamera() const;
	void HandleStoryConfirmed(FName BackstoryID, FName ProfessionID, bool bChangingExistingProfile);

private:
	FDelegateHandle WorldInitializationHandle;
	FDelegateHandle WorldCleanupHandle;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCharacterBackgroundComponent> TrackedBackgroundComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> TrackedInnerDoctrineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCharacterBackgroundCreationWidget> StoryWidget;

	TSet<uint32> OpenedWorldIds;
	FProjectCharacterBackgroundInputSnapshot InputSnapshot;
	bool bStoryMenuOpen = false;
	bool bLoadedProfileConfirmed = false;
	FName LoadedProfileBackstoryID = NAME_None;
	FName LoadedProfileProfessionID = NAME_None;
	int32 LoadedProfileRevision = 0;
	bool bHasSavedPhysicalCreatorAutoManageCameraTarget = false;
	bool bWasPhysicalCreatorAutoManageCameraTarget = true;
};
