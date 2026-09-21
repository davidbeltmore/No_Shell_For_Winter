#pragma once

#include "Items/ACFItem.h"
#include "Items/ACFItemFragment.h"
#include "Components/ACFEquipmentComponent.h"
#include "ProjectCalystoGameplaySnapshotTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class UProjectEquipmentTravelFixtureItem final : public UACFItem
{
	GENERATED_BODY()
public:
	UProjectEquipmentTravelFixtureItem()
	{ auto Info = GetItemInfo(); Info.ItemWeight = 0.25f; Info.MaxInventoryStack = 1; SetItemDescriptor(Info); }
	UPROPERTY(SaveGame) FString SavedPayload = FString::ChrN(1024, TEXT('x'));
};

/** Transient component-test data; never selected by runtime content. */
UCLASS(Transient, NotBlueprintable)
class UProjectCalystoSnapshotFixtureFragment final : public UACFItemFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 Charges = 7;
	UPROPERTY(SaveGame) FString SavedLabel = TEXT("Ledger");
	UPROPERTY(SaveGame) TMap<FName, int32> SavedValues;
	UPROPERTY(SaveGame) TObjectPtr<UACFItem> LinkedItem;
	UPROPERTY(SaveGame) TObjectPtr<UACFItemFragment> LinkedFragment;
	int32 ObservedEvents = 0;
	UFUNCTION() void ObserveChanged() { ++ObservedEvents; }
	UFUNCTION() void ObserveCurrency(float Amount, float Variation) { ++ObservedEvents; }
	UFUNCTION() void ObserveItem(const FBaseItem& Item) { ++ObservedEvents; }
	UFUNCTION() void ObserveEquipment(const FEquipment& Equipment) { ++ObservedEvents; }
};
