#pragma once

#include "CoreMinimal.h"

class AActor;
class UProjectDefeatFlowSettings;

struct FProjectDefeatInventoryEntry
{
	FString EntryKey;
	FText DisplayName;
	FString ExportText;
	FString ComponentName;
	FString ComponentClassName;
	FName ContainerPropertyName = NAME_None;
	FName CountPropertyName = NAME_None;
	int32 ArrayIndex = INDEX_NONE;
	int32 Count = 1;
	bool bStackable = false;
	bool bEligibleForRandomLoss = true;
	bool bFromEquipment = false;
};

struct FProjectDefeatInventorySnapshot
{
	FString InventoryComponentName;
	FString InventoryComponentClassName;
	FName InventoryPropertyName = NAME_None;
	TArray<FProjectDefeatInventoryEntry> InventoryEntries;
	TArray<FProjectDefeatInventoryEntry> EquipmentEntries;

	bool HasAnyEntries() const
	{
		return InventoryEntries.Num() > 0 || EquipmentEntries.Num() > 0;
	}
};

struct FProjectDefeatInventoryPenaltyResult
{
	int32 PenalizedEntryCount = 0;
	TArray<FText> PenalizedDisplayNames;
};

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatInventoryBridge
{
public:
	static bool CaptureSnapshot(AActor* OwnerActor, const UProjectDefeatFlowSettings* Settings, FProjectDefeatInventorySnapshot& OutSnapshot);
	static bool ApplyModerateLoss(AActor* OwnerActor, const UProjectDefeatFlowSettings* Settings, int32 Seed, FProjectDefeatInventoryPenaltyResult& OutResult);
	static void BuildRetainedSubset(const FProjectDefeatInventorySnapshot& SourceSnapshot, const UProjectDefeatFlowSettings* Settings, int32 Seed, FProjectDefeatInventorySnapshot& OutRetainedSnapshot);
	static void BuildDefeatedRetainedSnapshotForCunning(const FProjectDefeatInventorySnapshot& SourceSnapshot, const UProjectDefeatFlowSettings* Settings, int32 Seed, int32 CunningLevel, int32 FullRetentionLevel, FProjectDefeatInventorySnapshot& OutRetainedSnapshot);
	static bool ApplyRetainedSubset(AActor* OwnerActor, const FProjectDefeatInventorySnapshot& RetainedSnapshot, const UProjectDefeatFlowSettings* Settings);
};
