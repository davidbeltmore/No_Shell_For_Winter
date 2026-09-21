#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Items/ACFItem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectCalystoInventoryTravelFixtureSubsystem.generated.h"

class UACFEquipmentComponent;
class UACFItemFragment;
struct FInventoryItem;
struct FEFCalystoRealizedFloorManifestV6;
struct FEFCalystoResolvedFloorIntentV6;

/**
 * Development-only, GameInstance-persistent fixture for the V6 typed ACF
 * inventory travel contract.  It never runs implicitly: automation must arm it
 * after a Director floor is ready.  Shipping keeps the reflected API so calls
 * fail closed instead of silently changing a player's inventory.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCalystoInventoryTravelFixtureSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Replaces the live automation pawn's inventory with the exact V6 fixture.
	 * Returns a machine-readable line beginning with PASS or FAIL.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Companions|Automation")
	FString ArmInventoryTravelFixtureForAutomation();

	/** Audits the most recently accepted Replay/Reroll/Advance/NewRun floor. */
	UFUNCTION(BlueprintCallable, Category = "Project|Companions|Automation")
	FString AuditInventoryTravelFixtureForAutomation();

	UFUNCTION(BlueprintPure, Category = "Project|Companions|Automation")
	FString GetFrozenInventoryFixtureDocumentForAutomation() const;

	UFUNCTION(BlueprintPure, Category = "Project|Companions|Automation")
	FString GetFrozenInventoryFixtureHashForAutomation() const;

	UFUNCTION(BlueprintPure, Category = "Project|Companions|Automation")
	bool IsInventoryTravelFixtureArmedForAutomation() const { return bArmed; }

private:
	void BindDirectorEvents();
	void UnbindDirectorEvents();
	void BindTypedInventoryRestoreEvent();
	void UnbindTypedInventoryRestoreEvent();
	void BindInventoryDelegates(UACFEquipmentComponent* Equipment);
	void UnbindInventoryDelegates();
	UACFEquipmentComponent* ResolveEquipment() const;
	void HandleBeforeTypedInventoryRestore(UACFEquipmentComponent* Equipment);
	void HandleBeforeDirectorTravel(EEFCalystoDungeonTravelKindV6 TravelKind);
	void HandleDirectorWorldAccepted(
		int64 AcceptedRunEpoch,
		EEFCalystoDungeonTravelKindV6 TravelKind,
		const FEFCalystoResolvedFloorIntentV6& Intent);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoRealizedFloorManifestV6& Manifest);
	void HandleFloorTravelFailed();

	UFUNCTION()
	void HandleObservedInventoryChanged();

	UFUNCTION()
	void HandleObservedItemAdded(const FBaseItem& Item);

	UFUNCTION()
	void HandleObservedItemRemoved(const FBaseItem& Item);

	UFUNCTION()
	void HandleObservedCurrencyChanged(float NewValue, float Variation);

	bool ClearInventory(UACFEquipmentComponent* Equipment, FString& OutError);
	bool AddFixtureItems(UACFEquipmentComponent* Equipment, FString& OutError);
	bool AuditFixtureState(
		UACFEquipmentComponent* Equipment,
		bool bExpectRecall,
		FString& OutDocument,
		FString& OutHash,
		FString& OutError) const;
	FString BuildFixtureDocument(
		const UACFEquipmentComponent* Equipment,
		bool bIncludeRecall,
		float DocumentWeight) const;
	FString BuildItemState(const FInventoryItem& Item) const;
	static FString TravelKindName(EEFCalystoDungeonTravelKindV6 TravelKind);

private:
	TWeakObjectPtr<UACFEquipmentComponent> BoundEquipment;
	FGuid ArrowGuid;
	FGuid ShieldGuid;
	TArray<FGuid> RecallGuids;
	FString FrozenArrowState;
	FString FrozenShieldState;
	TMap<FGuid, FString> FrozenRecallStates;
	FString FrozenDocument;
	FString FrozenHash;
	FString ExpectedPostNewRunDocument;
	FString ExpectedPostNewRunHash;
	FGameplayTag ShieldSlot;
	FString ShieldBlockFragmentClassPath;
	float FrozenWeight = 0.0f;
	int32 FrozenMaxSlots = 0;
	int32 FrozenMaxWeight = 0;
	int64 LastAcceptedRunEpoch = 0;
	int64 LastReadyFloor = 0;
	EEFCalystoDungeonTravelKindV6 PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	EEFCalystoDungeonTravelKindV6 LastAcceptedTravelKind = EEFCalystoDungeonTravelKindV6::None;
	int32 InventoryChangedCount = 0;
	int32 ItemAddedCount = 0;
	int32 ItemRemovedCount = 0;
	int32 CurrencyChangedCount = 0;
	bool bArmed = false;
	bool bObservedFloorReady = false;
	bool bTravelFailed = false;
	bool bDirectorEventsBound = false;
	bool bInventoryRestoreEventBound = false;
};
