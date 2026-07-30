#pragma once

#include "CoreMinimal.h"
#include "EFLevelFlowLoadingTheme.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/ObjectKey.h"
#include "EFLevelFlowSubsystem.generated.h"

class APlayerController;
class APawn;
class UAIPerceptionStimuliSourceComponent;
class UWorld;

UCLASS()
class EFLEVELFLOWRUNTIME_API UEFLevelFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FLevelLoadingSessionSnapshot
	{
		TWeakObjectPtr<UWorld> ActiveWorld;
		TWeakObjectPtr<APlayerController> PlayerController;
		TWeakObjectPtr<APawn> Pawn;
		bool bIsActive = false;
		bool bPawnPositioned = false;
		bool bWasMouseCursorVisible = false;
		bool bWasMoveInputIgnored = false;
		bool bWasLookInputIgnored = false;
		bool bHadSavedMovementState = false;
		int32 VisualRepairAttemptCount = 0;
		TEnumAsByte<EMovementMode> PreviousMovementMode = MOVE_Walking;
		uint8 PreviousCustomMovementMode = 0;
		float PreviousGravityScale = 1.0f;
		double LoadingStartTimeSeconds = 0.0;
	};

	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void TryStartLevelLoadingSequence(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	bool ShouldDelaySpawnForWorld(const UWorld* World) const;
	void StartLevelLoadingSequence(UWorld* World, APlayerController* PlayerController, APawn* Pawn);
	void TryFinishLevelLoadingSequence(TWeakObjectPtr<UWorld> WorldPtr, int32 AttemptIndex);
	void ResetLevelLoadingSequence(bool bRestoreGameplayState);
	bool TryResolveDungeonEntryTransform(UWorld* World, APawn* Pawn, FTransform& OutTransform) const;
	bool FindFloorAdjustedDungeonTransform(UWorld* World, APawn* Pawn, const FTransform& CandidateTransform, FTransform& OutTransform) const;
	bool IsDungeonEntryVisualReady(UWorld* World, APlayerController* PlayerController, APawn* Pawn) const;
	void RepairDungeonEntryVisualState(UWorld* World, APlayerController* PlayerController, APawn* Pawn);
	void RefreshDungeonEntryVisualState(TWeakObjectPtr<UWorld> WorldPtr, TWeakObjectPtr<APlayerController> PlayerControllerPtr, TWeakObjectPtr<APawn> PawnPtr, int32 AttemptIndex);
	void BeginDungeonEntryGracePeriod(UWorld* World, APawn* Pawn);
	void EndDungeonEntryGracePeriod();
	void ClearDungeonEntryGracePeriod(bool bRestoreSight);
	void ClearEnemyAwarenessOfPawn(UWorld* World, APawn* Pawn);
	void ApplyLoadingInputState(APlayerController* PlayerController, bool bEnableLoadingScreen);
	void FreezePawn(APawn* Pawn, bool bFreeze);
	void ShowLoadingScreen(APlayerController* PlayerController);
	void HideLoadingScreen();
	void HandleLoadingThemeChanged(const FEFLevelFlowLoadingTheme& Theme);

private:
	FDelegateHandle WorldBeginPlayHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle LoadingThemeChangedHandle;
	FLevelLoadingSessionSnapshot LoadingSnapshot;
	FTimerHandle LoadingTimerHandle;
	FTimerHandle DungeonEntryGraceTimerHandle;
	TWeakObjectPtr<UWorld> DungeonEntryGraceWorld;
	TWeakObjectPtr<APawn> DungeonEntryGracePawn;
	TWeakObjectPtr<UAIPerceptionStimuliSourceComponent> DungeonEntryGraceStimuliSource;
	mutable TSet<TObjectKey<UWorld>> WarnedDerivedMapWorlds;
};
