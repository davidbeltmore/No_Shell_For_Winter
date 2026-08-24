#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Companions/ProjectRunCompanionTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectRunCompanionSubsystem.generated.h"

class AACFCharacter;
class APawn;
class UACFCompanionGroupAIComponent;
class UACFEquipmentComponent;
class UACFInventoryComponent;
class UProjectCompanionRevivalConsumable;
class UProjectCompanionRevivalMenuWidget;
struct FEFCalystoResolvedFloorIntentV4;

/**
 * Emitted after the destination ACF component and frozen capsule pass
 * validation, immediately before typed restoration. Project-owned observers
 * can bind to the destination object without relying on delegate order.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FProjectBeforeTypedInventoryRestoreSignature,
	UACFEquipmentComponent*);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectCompanionRosterChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FProjectCompanionDeathStateChangedSignature,
	FGuid, StableCompanionId,
	EProjectCompanionRunState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FProjectCompanionRevivalSelectionRequestedSignature,
	FGuid, TransactionId,
	const TArray<FProjectCompanionRevivalCandidate>&, Candidates);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProjectCompanionRevivalFinishedSignature,
	FGuid, TransactionId,
	bool, bSucceeded,
	FText, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectCompanionRosterReadySignature,
	bool, bReady);

/**
 * GameInstance authority for the V4 companion roster. It stores actor-independent
 * run state, while ACF remains authoritative for live AI groups and inventory.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectRunCompanionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Adds one recruited instance. Floor-local NPCs and duplicate
	 * StableCompanionIds are rejected; duplicate content/catalog variants are
	 * intentionally allowed because they represent different people. */
	UFUNCTION(BlueprintCallable, Category = "Project|Companions")
	bool RegisterRecruitedCompanion(
		const FProjectCompanionDefinition& Definition,
		AACFCharacter* LiveActor,
		bool bJoinActiveParty = true);

	UFUNCTION(BlueprintCallable, Category = "Project|Companions")
	bool SetCompanionActivePartyMembership(FGuid StableCompanionId, bool bActive);

	UFUNCTION(BlueprintPure, Category = "Project|Companions")
	FProjectCompanionRunSnapshot GetRunRosterSnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Project|Companions")
	TArray<FProjectCompanionRevivalCandidate> GetRevivalCandidates() const;

	UFUNCTION(BlueprintPure, Category = "Project|Companions")
	bool IsCompanionRosterReady() const { return bCompanionRosterReady; }

	/** Read-only bridge gate used after V4 population realization. */
	bool IsReadyForDirectorSnapshot(const FString& ExpectedSnapshotHash, FString& OutError) const;

	/**
	 * Project-owned Development fixture bridge.  It deliberately reuses the
	 * production canonical hash and typed fragment/equipment verifier instead of
	 * maintaining an automation-only serialization implementation.
	 */
	bool AuditTypedInventoryForAutomation(
		UACFEquipmentComponent* Equipment,
		FString& OutCanonicalHash,
		FString& OutError) const;

	FProjectBeforeTypedInventoryRestoreSignature& OnBeforeTypedInventoryRestore()
	{
		return BeforeTypedInventoryRestoreEvent;
	}

	/** Pure fail-closed validator for a recruit created and killed in this intent. */
	static bool ResolveSameFloorRecruitedRevivalLevel(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FProjectCompanionRunSnapshot& FloorStart,
		const FProjectCompanionRunEntrySnapshot& CurrentRecord,
		FEFCalystoResolvedCompanionLevelV4& OutLevel,
		FString& OutError);

	/** Resolve one active-party directive against the frozen local roster. */
	bool ResolveFrozenRosterProjection(
		const FEFCalystoSpawnInstanceDirectiveV4& Directive,
		FProjectCompanionDefinition& OutDefinition,
		FString& OutError) const;

	/** Adopt the already-finished bridge actor as the sole live projection for its stable ID. */
	bool AdoptDirectorRosterProjection(
		AACFCharacter* Character,
		const FProjectCompanionDefinition& Definition,
		FString& OutError);

	/** Rollback hook used when any later V4 population slot fails. */
	void ReleaseDirectorRosterProjection(FGuid StableCompanionId, AACFCharacter* Character);
	void RollbackUncommittedDirectorRecruitment(FGuid StableCompanionId, AACFCharacter* Character);

	/** Final post-population roster validation; this is the only path that marks readiness. */
	bool FinalizeDirectorRosterReadiness(const FString& ExpectedSnapshotHash, FString& OutError);

	UFUNCTION(BlueprintPure, Category = "Project|Companions")
	int64 GetRunEpoch() const { return RunEpoch; }

	UFUNCTION(BlueprintPure, Category = "Project|Companions")
	bool HasConfirmedDeadCompanion() const;

	/** Canonical death edge called by the per-instance death proxy; duplicate signals are ignored. */
	void ReportCompanionDeath(FGuid StableCompanionId, AActor* CorpseActor, FName SourceDomain);

	/** Transaction preflight used by the consumable's CanBeUsed override. */
	bool CanBeginRevival(
		const APawn* PlayerPawn,
		const UProjectCompanionRevivalConsumable* Item,
		FString& OutError) const;

	/** Freezes item GUID/count, run identity, world and sorted candidates, then opens the native menu. */
	bool BeginRevivalSelection(
		APawn* PlayerPawn,
		UProjectCompanionRevivalConsumable* Item,
		FGuid& OutTransactionId,
		FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Project|Companions|Revival")
	bool ConfirmPendingRevival(FGuid TransactionId, FGuid StableCompanionId);

	UFUNCTION(BlueprintCallable, Category = "Project|Companions|Revival")
	void CancelPendingRevival(FGuid TransactionId);

	UPROPERTY(BlueprintAssignable, Category = "Project|Companions")
	FProjectCompanionRosterChangedSignature OnRosterChanged;

	UPROPERTY(BlueprintAssignable, Category = "Project|Companions")
	FProjectCompanionDeathStateChangedSignature OnDeathStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Project|Companions|Revival")
	FProjectCompanionRevivalSelectionRequestedSignature OnRevivalSelectionRequested;

	UPROPERTY(BlueprintAssignable, Category = "Project|Companions|Revival")
	FProjectCompanionRevivalFinishedSignature OnRevivalFinished;

	UPROPERTY(BlueprintAssignable, Category = "Project|Companions")
	FProjectCompanionRosterReadySignature OnCompanionRosterReady;

private:
	struct FRuntimeCompanionRecord
	{
		FProjectCompanionRunEntrySnapshot Snapshot;
		bool bDesiredActiveParty = false;
		TWeakObjectPtr<AACFCharacter> LiveActor;
		TWeakObjectPtr<AActor> CorpseActor;
	};

	struct FRevivalTransaction
	{
		bool bActive = false;
		FGuid TransactionId;
		FGuid ItemGuid;
		int32 OriginalItemCount = 0;
		int64 FrozenRunEpoch = 0;
		int64 FrozenFloor = 0;
		int64 FrozenGenerationSerial = 0;
		TWeakObjectPtr<UWorld> FrozenWorld;
		TWeakObjectPtr<APawn> PlayerPawn;
		TWeakObjectPtr<UACFInventoryComponent> Inventory;
		TWeakObjectPtr<UProjectCompanionRevivalConsumable> Item;
		TArray<FGuid> EligibleCompanionIds;
	};

	struct FMenuInputSnapshot
	{
		bool bValid = false;
		bool bMoveIgnored = false;
		bool bLookIgnored = false;
		bool bMouseCursorVisible = false;
		bool bClickEventsEnabled = false;
		bool bMouseOverEventsEnabled = false;
	};

	void BindDirectorEvents();
	void UnbindDirectorEvents();
	void HandleBeforeDirectorTravel(EEFCalystoDungeonTravelKindV4 TravelKind);
	void HandleDirectorWorldAccepted(
		int64 AcceptedRunEpoch,
		EEFCalystoDungeonTravelKindV4 TravelKind,
		const FEFCalystoResolvedFloorIntentV4& Intent);
	void HandleNewRunInitialized(int64 NewRunEpoch);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest);
	void HandleFloorTravelFailed();

	FProjectCompanionRunSnapshot BuildSnapshot() const;
	FEFCalystoCompanionSnapshotV4 BuildDirectorSnapshot(
		const FProjectCompanionRunSnapshot& Source) const;
	/**
	 * Rebuilds the live roster while retaining the inventory-eligibility bit
	 * frozen by the accepted FloorIntent. Inventory has its own typed travel
	 * capsule and may legitimately change after that intent was compiled.
	 */
	FEFCalystoCompanionSnapshotV4 BuildAcceptedRosterValidationSnapshot(
		const FProjectCompanionRunSnapshot& Source) const;
	bool RestoreSnapshot(const FProjectCompanionRunSnapshot& Snapshot, FString& OutError);
	void BroadcastAcceptedRosterChanges(
		const FProjectCompanionRunSnapshot& Previous,
		const FProjectCompanionRunSnapshot& Accepted);
	bool ApplyResolvedCompanionLevels(
		const FEFCalystoResolvedFloorIntentV4& Intent,
		FString& OutError);
	bool ResolveActiveIntentCompanionLevel(
		FGuid StableCompanionId,
		FEFCalystoResolvedCompanionLevelV4& OutLevel,
		FString& OutError) const;
	bool ResolveFrozenRevivalCompanionLevel(
		FGuid StableCompanionId,
		FEFCalystoResolvedCompanionLevelV4& OutLevel,
		FString& OutError) const;
	void ResetRunState(int64 NewRunEpoch);
	void DestroyLiveRosterProjections();
	bool AttachDeathProxy(AACFCharacter* Character, FGuid StableCompanionId);
	void SetRosterReady(bool bReady);

	APawn* ResolveLocalPlayerPawn() const;
	UACFInventoryComponent* ResolveInventory(const APawn* PlayerPawn) const;
	UACFEquipmentComponent* ResolveEquipment(const APawn* PlayerPawn) const;
	bool SynchronizeRecruitmentsBeforeTravel(FString& OutError);
	bool CaptureInventoryForTravel(
		EProjectCompanionDirectorTravelMode TravelMode,
		FString& OutError);
	bool RestoreAndVerifyInventoryAfterTravel(FString& OutError);
	bool VerifyInventoryAfterFailedTravel(FString& OutError);
	bool SerializeEquipmentCapsule(
		UACFEquipmentComponent* Equipment,
		TArray<uint8>& OutBytes,
		FString& OutError) const;
	bool RestoreTypedEquipmentCapsule(
		UACFEquipmentComponent* Equipment,
		const TArray<uint8>& FrozenBytes,
		const FString& ExpectedInventoryHash,
		FString& OutError) const;
	bool RestoreRevivalInventoryCapsule(
		UACFEquipmentComponent* Equipment,
		const TArray<uint8>& FrozenBytes,
		const FString& FrozenBytesHash,
		const FString& ExpectedInventoryHash,
		FString& OutError) const;
	bool VerifyRestoredEquipment(
		UACFEquipmentComponent* Equipment,
		const FString& ExpectedInventoryHash,
		FString& OutError) const;
	void ResetInventoryTravelCapsule();
	FString ComputeInventoryHash(const UACFInventoryComponent* Inventory) const;
	bool PurgeWintersRecallFromInventory(APawn* PlayerPawn, FString& OutError) const;
	bool PlayerOwnsWintersRecall(const APawn* PlayerPawn) const;

	bool FindExactItemEntry(
		const APawn* PlayerPawn,
		const UProjectCompanionRevivalConsumable* Item,
		FGuid& OutGuid,
		int32& OutCount,
		UACFInventoryComponent*& OutInventory) const;
	bool IsPlayerOutsideCombat(const APawn* PlayerPawn) const;
	bool RevalidateRevivalTransaction(FGuid StableCompanionId, FString& OutError) const;
	void FinishRevivalTransaction(bool bSucceeded, const FText& Result);
	void OpenRevivalMenu(const TArray<FProjectCompanionRevivalCandidate>& Candidates, FString& OutError);
	void CloseRevivalMenu();
	void HandleRevivalMenuOption(FName OptionId);
	void HandleRevivalMenuCancel();
	void ApplyMenuInputCapture(APawn* PlayerPawn);
	void RestoreMenuInputCapture();

	static EProjectCompanionDirectorTravelMode ConvertTravelMode(EEFCalystoDungeonTravelKindV4 Kind);

private:
	TMap<FGuid, FRuntimeCompanionRecord> Roster;

	/** Strong project-owned references keep recruited Blueprint classes available across OpenLevel. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UClass>> RetainedCompanionClasses;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCompanionRevivalMenuWidget> RevivalMenuWidget;

	FProjectCompanionRunSnapshot FloorStartSnapshot;
	FProjectCompanionRunSnapshot PreTravelSnapshot;
	FProjectCompanionRunSnapshot PendingDestinationSnapshot;
	FRevivalTransaction RevivalTransaction;
	FMenuInputSnapshot MenuInputSnapshot;

	int64 RunEpoch = 0;
	int64 CurrentFloor = 0;
	int64 CurrentGenerationSerial = 0;
	EProjectCompanionDirectorTravelMode PendingTravelMode = EProjectCompanionDirectorTravelMode::None;
	FString PreTravelInventoryHash;
	TArray<uint8> InventoryTravelCapsule;
	FString InventoryTravelCapsuleHash;
	FString InventoryTravelComponentClassPath;
	FName InventoryTravelComponentName = NAME_None;
	int64 InventoryTravelRunEpoch = 0;
	int64 InventoryTravelFloor = 0;
	int64 InventoryTravelGenerationSerial = 0;
	FString CurrentCompanionSnapshotHash;
	bool bAcceptedIntentPlayerOwnsWintersRecall = false;
	bool bInventoryTravelCaptured = false;
	bool bCompanionRosterReady = false;
	bool bFloorReady = false;
	bool bGenerationOrTravelActive = false;
	bool bDirectorEventsBound = false;
	FProjectBeforeTypedInventoryRestoreSignature BeforeTypedInventoryRestoreEvent;
};
