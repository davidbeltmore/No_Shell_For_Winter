#include "Companions/ProjectCalystoInventoryTravelFixtureSubsystem.h"

#include "ACFItemSystemFunctionLibrary.h"
#include "Calysto/EFCalystoDirectorSettings.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Companions/ProjectCompanionRevivalConsumable.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Components/ACFEquipmentComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Items/ACFItemFragment.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCalystoInventoryTravelFixture, Log, All);

namespace ProjectCalystoInventoryTravelFixturePrivate
{
	const TCHAR* ArrowClassPath =
		TEXT("/Game/FullSample/Blueprints/Items/Ammo/ACF_Arrow_BP.ACF_Arrow_BP_C");
	const TCHAR* ShieldClassPath =
		TEXT("/Game/FullSample/Blueprints/Items/Weapons/ACFShieldBP.ACFShieldBP_C");
	const TCHAR* RecallClassPath =
		TEXT("/Game/_Game/Items/Companions/BP_Item_WintersRecall.BP_Item_WintersRecall_C");
	constexpr float FixtureCurrency = 1234.5f;
	constexpr int32 FixtureArrowCount = 7;
	constexpr int32 FixtureRecallCount = 2;
	constexpr int32 FixtureMaxSlots = 37;
	constexpr int32 FixtureMaxWeight = 222;

	uint32 FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	FString GuidKey(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}

	bool IsRecallClass(const UClass* ItemClass)
	{
		return ItemClass && ItemClass->IsChildOf(UProjectCompanionRevivalConsumable::StaticClass());
	}

	void AppendSaveGameProperties(const UObject* Object, FString& InOutCanonical)
	{
		if (!Object)
		{
			InOutCanonical += TEXT("null;");
			return;
		}
		InOutCanonical += Object->GetClass()->GetPathName();
		InOutCanonical += TEXT("{");
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_SaveGame))
			{
				continue;
			}
			FString Exported;
			Property->ExportTextItem_Direct(
				Exported,
				Property->ContainerPtrToValuePtr<void>(Object),
				nullptr,
				const_cast<UObject*>(Object),
				PPF_None);
			InOutCanonical += Property->GetName();
			InOutCanonical += TEXT("=");
			InOutCanonical += Exported;
			InOutCanonical += TEXT(";");
		}
		InOutCanonical += TEXT("}");
	}

	FString MakeResult(const bool bPass, const FString& Payload)
	{
		return FString::Printf(TEXT("%s|%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Payload);
	}
}

bool UProjectCalystoInventoryTravelFixtureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// This fixture proves only the legacy inventory contract. It must never
	// initialize a disabled authority or be mistaken for unversioned validation.
	return Super::ShouldCreateSubsystem(Outer) && !UEFCalystoDirectorSettings::IsEnabled();
}

void UProjectCalystoInventoryTravelFixtureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	Collection.InitializeDependency<UEFCalystoDungeonSubsystem>();
	Collection.InitializeDependency<UProjectRunCompanionSubsystem>();
	BindDirectorEvents();
	BindTypedInventoryRestoreEvent();
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::Deinitialize()
{
	UnbindInventoryDelegates();
	UnbindTypedInventoryRestoreEvent();
	UnbindDirectorEvents();
	bArmed = false;
	Super::Deinitialize();
}

void UProjectCalystoInventoryTravelFixtureSubsystem::BindDirectorEvents()
{
#if !UE_BUILD_SHIPPING
	if (bDirectorEventsBound || !GetGameInstance())
	{
		return;
	}
	UEFCalystoDungeonSubsystem* Director =
		GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>();
	if (!Director)
	{
		return;
	}
	Director->OnBeforeAnyDirectorTravel().AddUObject(
		this, &ThisClass::HandleBeforeDirectorTravel);
	Director->OnDirectorWorldAccepted().AddUObject(
		this, &ThisClass::HandleDirectorWorldAccepted);
	Director->OnFloorReady().AddUObject(this, &ThisClass::HandleFloorReady);
	Director->OnFloorTravelFailed().AddUObject(
		this, &ThisClass::HandleFloorTravelFailed);
	bDirectorEventsBound = true;
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::UnbindDirectorEvents()
{
	if (!bDirectorEventsBound || !GetGameInstance())
	{
		return;
	}
	if (UEFCalystoDungeonSubsystem* Director =
		GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>())
	{
		Director->OnBeforeAnyDirectorTravel().RemoveAll(this);
		Director->OnDirectorWorldAccepted().RemoveAll(this);
		Director->OnFloorReady().RemoveAll(this);
		Director->OnFloorTravelFailed().RemoveAll(this);
	}
	bDirectorEventsBound = false;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::BindTypedInventoryRestoreEvent()
{
#if !UE_BUILD_SHIPPING
	if (bInventoryRestoreEventBound || !GetGameInstance())
	{
		return;
	}
	UProjectRunCompanionSubsystem* Roster =
		GetGameInstance()->GetSubsystem<UProjectRunCompanionSubsystem>();
	if (!Roster)
	{
		return;
	}
	Roster->OnBeforeTypedInventoryRestore().AddUObject(
		this, &ThisClass::HandleBeforeTypedInventoryRestore);
	bInventoryRestoreEventBound = true;
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::UnbindTypedInventoryRestoreEvent()
{
	if (!bInventoryRestoreEventBound || !GetGameInstance())
	{
		return;
	}
	if (UProjectRunCompanionSubsystem* Roster =
		GetGameInstance()->GetSubsystem<UProjectRunCompanionSubsystem>())
	{
		Roster->OnBeforeTypedInventoryRestore().RemoveAll(this);
	}
	bInventoryRestoreEventBound = false;
}

UACFEquipmentComponent* UProjectCalystoInventoryTravelFixtureSubsystem::ResolveEquipment() const
{
	const UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<UACFEquipmentComponent>() : nullptr;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::BindInventoryDelegates(
	UACFEquipmentComponent* Equipment)
{
	UnbindInventoryDelegates();
	if (!Equipment)
	{
		return;
	}
	BoundEquipment = Equipment;
	Equipment->OnInventoryChanged.AddDynamic(
		this, &ThisClass::HandleObservedInventoryChanged);
	Equipment->OnItemAdded.AddDynamic(this, &ThisClass::HandleObservedItemAdded);
	Equipment->OnItemRemoved.AddDynamic(this, &ThisClass::HandleObservedItemRemoved);
	Equipment->OnCurrencyChanged.AddDynamic(
		this, &ThisClass::HandleObservedCurrencyChanged);
}

void UProjectCalystoInventoryTravelFixtureSubsystem::UnbindInventoryDelegates()
{
	if (UACFEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnInventoryChanged.RemoveDynamic(
			this, &ThisClass::HandleObservedInventoryChanged);
		Equipment->OnItemAdded.RemoveDynamic(
			this, &ThisClass::HandleObservedItemAdded);
		Equipment->OnItemRemoved.RemoveDynamic(
			this, &ThisClass::HandleObservedItemRemoved);
		Equipment->OnCurrencyChanged.RemoveDynamic(
			this, &ThisClass::HandleObservedCurrencyChanged);
	}
	BoundEquipment.Reset();
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleObservedInventoryChanged()
{
	++InventoryChangedCount;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleObservedItemAdded(
	const FBaseItem& Item)
{
	(void)Item;
	++ItemAddedCount;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleObservedItemRemoved(
	const FBaseItem& Item)
{
	(void)Item;
	++ItemRemovedCount;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleObservedCurrencyChanged(
	const float NewValue,
	const float Variation)
{
	(void)NewValue;
	(void)Variation;
	++CurrencyChangedCount;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleBeforeTypedInventoryRestore(
	UACFEquipmentComponent* Equipment)
{
#if !UE_BUILD_SHIPPING
	if (!bArmed)
	{
		return;
	}
	InventoryChangedCount = 0;
	ItemAddedCount = 0;
	ItemRemovedCount = 0;
	CurrencyChangedCount = 0;
	BindInventoryDelegates(Equipment);
#else
	(void)Equipment;
#endif
}

bool UProjectCalystoInventoryTravelFixtureSubsystem::ClearInventory(
	UACFEquipmentComponent* Equipment,
	FString& OutError)
{
	OutError.Reset();
	if (!Equipment || !Equipment->GetOwner() || !Equipment->GetOwner()->HasAuthority()
		|| !Equipment->GetIsInitialized())
	{
		OutError = TEXT("The authoritative initialized ACF equipment component is unavailable.");
		return false;
	}
	Equipment->ClearAllInventoryAndEquipment();
	Equipment->RefreshTotalWeight();
	if (!Equipment->GetInventory().IsEmpty()
		|| !Equipment->GetCurrentEquipment().GetEquippedItems().IsEmpty()
		|| !Equipment->GetRegisteredFragments().IsEmpty()
		|| !FMath::IsNearlyZero(Equipment->GetCurrentInventoryTotalWeight()))
	{
		OutError = TEXT("ACF did not clear inventory, equipment, fragments and weight atomically.");
		return false;
	}
	return true;
}

bool UProjectCalystoInventoryTravelFixtureSubsystem::AddFixtureItems(
	UACFEquipmentComponent* Equipment,
	FString& OutError)
{
	OutError.Reset();
	UClass* ArrowClass = Cast<UClass>(FSoftObjectPath(
		ProjectCalystoInventoryTravelFixturePrivate::ArrowClassPath).ResolveObject());
	UClass* ShieldClass = Cast<UClass>(FSoftObjectPath(
		ProjectCalystoInventoryTravelFixturePrivate::ShieldClassPath).ResolveObject());
	UClass* RecallClass = Cast<UClass>(FSoftObjectPath(
		ProjectCalystoInventoryTravelFixturePrivate::RecallClassPath).ResolveObject());
	if (!Equipment || !ArrowClass || !ShieldClass || !RecallClass
		|| !RecallClass->IsChildOf(UProjectCompanionRevivalConsumable::StaticClass()))
	{
		OutError = TEXT("The exact Arrow, Shield or Winter's Recall fixture class is not resident; preload the fixture before arming it.");
		return false;
	}

	Equipment->SetMaxInventorySlots(ProjectCalystoInventoryTravelFixturePrivate::FixtureMaxSlots);
	Equipment->SetMaxInventoryWeight(ProjectCalystoInventoryTravelFixturePrivate::FixtureMaxWeight);
	Equipment->SetCurrency(ProjectCalystoInventoryTravelFixturePrivate::FixtureCurrency);
	Equipment->AddItemToInventory(
		FBaseItem(TSubclassOf<UACFItem>(ArrowClass),
			ProjectCalystoInventoryTravelFixturePrivate::FixtureArrowCount),
		false);

	const FGuid RequestedShieldGuid = FGuid::NewGuid();
	Equipment->AddInventoryItem(FInventoryItem(FBaseItem(
		TSubclassOf<UACFItem>(ShieldClass), RequestedShieldGuid, 1)));
	for (int32 Index = 0;
		Index < ProjectCalystoInventoryTravelFixturePrivate::FixtureRecallCount;
		++Index)
	{
		Equipment->AddInventoryItem(FInventoryItem(FBaseItem(
			TSubclassOf<UACFItem>(RecallClass), FGuid::NewGuid(), 1)));
	}
	Equipment->RefreshTotalWeight();

	TArray<FInventoryItem> ArrowItems;
	TArray<FInventoryItem> ShieldItems;
	TArray<FInventoryItem> RecallItems;
	Equipment->GetAllItemsOfClassInInventory(
		TSubclassOf<UACFItem>(ArrowClass), ArrowItems);
	Equipment->GetAllItemsOfClassInInventory(
		TSubclassOf<UACFItem>(ShieldClass), ShieldItems);
	Equipment->GetAllItemsOfClassInInventory(
		TSubclassOf<UACFItem>(RecallClass), RecallItems);
	if (ArrowItems.Num() != 1
		|| ArrowItems[0].Count != ProjectCalystoInventoryTravelFixturePrivate::FixtureArrowCount
		|| ShieldItems.Num() != 1 || ShieldItems[0].Count != 1
		|| ShieldItems[0].GetItemGuid() != RequestedShieldGuid
		|| RecallItems.Num() != ProjectCalystoInventoryTravelFixturePrivate::FixtureRecallCount)
	{
		OutError = TEXT("ACF did not realize the exact Arrow, Shield and two-instance Recall fixture.");
		return false;
	}

	TSet<FGuid> RecallIds;
	for (const FInventoryItem& Recall : RecallItems)
	{
		if (!Recall.GetItemGuid().IsValid() || RecallIds.Contains(Recall.GetItemGuid())
			|| Recall.Count != 1 || !Recall.Item)
		{
			OutError = TEXT("The two Winter's Recall entries lack unique typed GUIDs or live item instances.");
			return false;
		}
		RecallIds.Add(Recall.GetItemGuid());
	}

	if (!ShieldItems[0].Item)
	{
		OutError = TEXT("The ACF Shield fixture has no live item instance.");
		return false;
	}
	TArray<FGameplayTag> ShieldSlots = ShieldItems[0].Item->GetPossibleItemSlots();
	ShieldSlots.RemoveAll([](const FGameplayTag& Slot) { return !Slot.IsValid(); });
	if (ShieldSlots.IsEmpty())
	{
		OutError = TEXT("The selected ACF Shield exposes no valid equipment slot.");
		return false;
	}
	ShieldSlot = ShieldSlots[0];
	Equipment->EquipItemFromInventoryInSlot(ShieldItems[0], ShieldSlot);

	FInventoryItem EquippedShield;
	if (!Equipment->GetItemByGuid(RequestedShieldGuid, EquippedShield)
		|| !EquippedShield.bIsEquipped || EquippedShield.EquipmentSlot != ShieldSlot)
	{
		OutError = TEXT("The ACF Shield did not preserve its GUID while becoming equipped.");
		return false;
	}
	int32 MatchingEquippedShieldCount = 0;
	for (const FEquippedItem& Equipped :
		Equipment->GetCurrentEquipment().GetEquippedItems())
	{
		MatchingEquippedShieldCount +=
			Equipped.ItemGuid == RequestedShieldGuid
			&& Equipped.ItemSlot == ShieldSlot ? 1 : 0;
	}
	if (MatchingEquippedShieldCount != 1)
	{
		OutError = TEXT("The ACF equipment list does not contain exactly one matching Shield projection.");
		return false;
	}

	ShieldBlockFragmentClassPath.Reset();
	for (const UACFItemFragment* Fragment : EquippedShield.Item->Fragments)
	{
		const FString ClassPath = GetPathNameSafe(Fragment ? Fragment->GetClass() : nullptr);
		if (Fragment && ClassPath.Contains(TEXT("Block"), ESearchCase::IgnoreCase))
		{
			ShieldBlockFragmentClassPath = ClassPath;
			break;
		}
	}
	if (ShieldBlockFragmentClassPath.IsEmpty())
	{
		OutError = TEXT("The equipped ACF Shield has no typed Block fragment to exercise travel serialization.");
		return false;
	}

	ArrowGuid = ArrowItems[0].GetItemGuid();
	ShieldGuid = RequestedShieldGuid;
	RecallGuids = RecallIds.Array();
	RecallGuids.Sort([](const FGuid& Left, const FGuid& Right)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::GuidKey(Left)
			< ProjectCalystoInventoryTravelFixturePrivate::GuidKey(Right);
	});
	return ArrowGuid.IsValid() && ShieldGuid.IsValid();
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::BuildItemState(
	const FInventoryItem& Item) const
{
	TArray<FString> FragmentClasses;
	FString SaveGameState;
	ProjectCalystoInventoryTravelFixturePrivate::AppendSaveGameProperties(
		Item.Item, SaveGameState);
	if (Item.Item)
	{
		for (const UACFItemFragment* Fragment : Item.Item->Fragments)
		{
			FragmentClasses.Add(GetPathNameSafe(Fragment ? Fragment->GetClass() : nullptr));
			ProjectCalystoInventoryTravelFixturePrivate::AppendSaveGameProperties(
				Fragment, SaveGameState);
		}
	}
	FragmentClasses.Sort();
	return FString::Printf(
		TEXT("%s,%s,%d,%d,%s,%d,%08X,%s,%s,%s"),
		*ProjectCalystoInventoryTravelFixturePrivate::GuidKey(Item.GetItemGuid()),
		*GetPathNameSafe(Item.ItemClass.Get()),
		Item.Count,
		Item.bIsEquipped ? 1 : 0,
		*Item.EquipmentSlot.ToString(),
		Item.ItemIndex,
		ProjectCalystoInventoryTravelFixturePrivate::FloatBits(Item.DropChancePercentage),
		*GetPathNameSafe(Item.Item ? Item.Item->GetClass() : nullptr),
		*FString::Join(FragmentClasses, TEXT("+")),
		*SaveGameState);
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::BuildFixtureDocument(
	const UACFEquipmentComponent* Equipment,
	const bool bIncludeRecall,
	const float DocumentWeight) const
{
	if (!Equipment || !FMath::IsFinite(DocumentWeight))
	{
		return FString();
	}
	TArray<FString> ItemStates;
	for (const FInventoryItem& Item : Equipment->GetInventory())
	{
		if (!bIncludeRecall
			&& ProjectCalystoInventoryTravelFixturePrivate::IsRecallClass(Item.ItemClass.Get()))
		{
			continue;
		}
		ItemStates.Add(BuildItemState(Item));
	}
	ItemStates.Sort();
	return FString::Printf(
		TEXT("ProjectCalystoInventoryFixtureV6|currency=%08X|weight=%08X|maxSlots=%d|maxWeight=%d|items=%s"),
		ProjectCalystoInventoryTravelFixturePrivate::FloatBits(
			Equipment->GetCurrentCurrencyAmount()),
		ProjectCalystoInventoryTravelFixturePrivate::FloatBits(DocumentWeight),
		Equipment->GetMaxInventorySlots(),
		Equipment->GetMaxInventoryWeight(),
		*FString::Join(ItemStates, TEXT(";")));
}

bool UProjectCalystoInventoryTravelFixtureSubsystem::AuditFixtureState(
	UACFEquipmentComponent* Equipment,
	const bool bExpectRecall,
	FString& OutDocument,
	FString& OutHash,
	FString& OutError) const
{
	OutDocument.Reset();
	OutHash.Reset();
	OutError.Reset();
	if (!Equipment || !Equipment->GetOwner() || !Equipment->GetOwner()->HasAuthority()
		|| !Equipment->GetIsInitialized())
	{
		OutError = TEXT("The destination authoritative ACF equipment component is unavailable.");
		return false;
	}

	const TArray<FInventoryItem> Items = Equipment->GetInventory();
	int32 ArrowMatches = 0;
	int32 ShieldMatches = 0;
	int32 RecallMatches = 0;
	for (const FInventoryItem& Item : Items)
	{
		const FString State = BuildItemState(Item);
		if (Item.GetItemGuid() == ArrowGuid)
		{
			++ArrowMatches;
			if (Item.Count != ProjectCalystoInventoryTravelFixturePrivate::FixtureArrowCount
				|| State != FrozenArrowState)
			{
				OutError = TEXT("The Arrow stack GUID, count or typed state drifted.");
				return false;
			}
		}
		else if (Item.GetItemGuid() == ShieldGuid)
		{
			++ShieldMatches;
			if (!Item.bIsEquipped || Item.EquipmentSlot != ShieldSlot
				|| State != FrozenShieldState)
			{
				OutError = TEXT("The equipped Shield GUID, slot, fragment or typed state drifted.");
				return false;
			}
		}
		else if (const FString* ExpectedRecall =
			FrozenRecallStates.Find(Item.GetItemGuid()))
		{
			++RecallMatches;
			if (!bExpectRecall || State != *ExpectedRecall)
			{
				OutError = TEXT("A Winter's Recall survived New Run or changed typed identity.");
				return false;
			}
		}
		else
		{
			OutError = TEXT("The fixture inventory contains an unexpected item GUID.");
			return false;
		}
	}
	const int32 ExpectedRecallMatches = bExpectRecall
		? ProjectCalystoInventoryTravelFixturePrivate::FixtureRecallCount : 0;
	if (ArrowMatches != 1 || ShieldMatches != 1
		|| RecallMatches != ExpectedRecallMatches
		|| Items.Num() != 2 + ExpectedRecallMatches)
	{
		OutError = TEXT("The fixture inventory has an unexpected item composition.");
		return false;
	}

	UProjectRunCompanionSubsystem* Roster = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProjectRunCompanionSubsystem>() : nullptr;
	FString ProductionHash;
	if (!Roster
		|| !Roster->AuditTypedInventoryForAutomation(
			Equipment, ProductionHash, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The production typed ACF inventory auditor is unavailable.");
		}
		return false;
	}

	OutDocument = BuildFixtureDocument(
		Equipment, bExpectRecall, Equipment->GetCurrentInventoryTotalWeight());
	OutHash = bExpectRecall
		? ProductionHash
		: UEFCalystoDungeonSubsystem::ComputeCanonicalHash(OutDocument);
	const FString& ExpectedDocument = bExpectRecall
		? FrozenDocument : ExpectedPostNewRunDocument;
	const FString& ExpectedHash = bExpectRecall
		? FrozenHash : ExpectedPostNewRunHash;
	if (OutDocument.IsEmpty() || OutDocument != ExpectedDocument
		|| OutHash.IsEmpty() || OutHash != ExpectedHash)
	{
		OutError = FString::Printf(
			TEXT("Fixture document/hash drift (expected=%s actual=%s)."),
			*ExpectedHash, *OutHash);
		return false;
	}
	return true;
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::ArmInventoryTravelFixtureForAutomation()
{
#if UE_BUILD_SHIPPING
	return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
		false, TEXT("shipping_rejects_inventory_fixture"));
#else
	if (bArmed)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, TEXT("fixture_already_armed"));
	}
	UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>() : nullptr;
	UACFEquipmentComponent* Equipment = ResolveEquipment();
	if (!Director || !Director->HasActiveRun() || Director->IsTravelRequestPending()
		|| !Director->GetResolvedFloorIntent().bIsValid)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, TEXT("director_or_floor_not_ready"));
	}

	BindInventoryDelegates(Equipment);
	FString Error;
	if (!ClearInventory(Equipment, Error) || !AddFixtureItems(Equipment, Error))
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(false, Error);
	}

	TArray<FInventoryItem> Items = Equipment->GetInventory();
	const FInventoryItem* Arrow = Items.FindByPredicate(
		[this](const FInventoryItem& Item) { return Item.GetItemGuid() == ArrowGuid; });
	const FInventoryItem* Shield = Items.FindByPredicate(
		[this](const FInventoryItem& Item) { return Item.GetItemGuid() == ShieldGuid; });
	if (!Arrow || !Shield)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, TEXT("fixture_identity_capture_failed"));
	}
	FrozenArrowState = BuildItemState(*Arrow);
	FrozenShieldState = BuildItemState(*Shield);
	FrozenRecallStates.Reset();
	float RecallWeight = 0.0f;
	for (const FInventoryItem& Item : Items)
	{
		if (!ProjectCalystoInventoryTravelFixturePrivate::IsRecallClass(Item.ItemClass.Get()))
		{
			continue;
		}
		FrozenRecallStates.Add(Item.GetItemGuid(), BuildItemState(Item));
		FItemDescriptor Descriptor;
		if (!UACFItemSystemFunctionLibrary::GetItemData(Item.ItemClass, Descriptor)
			|| !FMath::IsFinite(Descriptor.ItemWeight))
		{
			return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
				false, TEXT("recall_descriptor_invalid"));
		}
		RecallWeight += Descriptor.ItemWeight * Item.Count;
	}
	if (FrozenRecallStates.Num()
		!= ProjectCalystoInventoryTravelFixturePrivate::FixtureRecallCount)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, TEXT("recall_identity_capture_failed"));
	}

	FrozenWeight = Equipment->GetCurrentInventoryTotalWeight();
	FrozenMaxSlots = Equipment->GetMaxInventorySlots();
	FrozenMaxWeight = Equipment->GetMaxInventoryWeight();
	FrozenDocument = BuildFixtureDocument(Equipment, true, FrozenWeight);
	ExpectedPostNewRunDocument = BuildFixtureDocument(
		Equipment, false, FrozenWeight - RecallWeight);
	ExpectedPostNewRunHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(
		ExpectedPostNewRunDocument);
	UProjectRunCompanionSubsystem* Roster =
		GetGameInstance()->GetSubsystem<UProjectRunCompanionSubsystem>();
	if (!Roster || FrozenDocument.IsEmpty() || ExpectedPostNewRunDocument.IsEmpty()
		|| !Roster->AuditTypedInventoryForAutomation(
			Equipment, FrozenHash, Error)
		|| FrozenHash.IsEmpty() || ExpectedPostNewRunHash.IsEmpty())
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, Error.IsEmpty() ? TEXT("production_inventory_audit_failed") : Error);
	}

	LastAcceptedRunEpoch = Director->GetRunEpoch();
	LastReadyFloor = Director->GetCurrentFloor();
	PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	LastAcceptedTravelKind = EEFCalystoDungeonTravelKindV6::None;
	InventoryChangedCount = 0;
	ItemAddedCount = 0;
	ItemRemovedCount = 0;
	CurrencyChangedCount = 0;
	bObservedFloorReady = true;
	bTravelFailed = false;
	bArmed = true;
	return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
		true,
		FString::Printf(
			TEXT("armed=1|run_epoch=%lld|floor=%lld|hash=%s|items=4|arrow=7|recall=2|block_fragment=%s|max_slots=%d|max_weight=%d"),
			LastAcceptedRunEpoch,
			LastReadyFloor,
			*FrozenHash,
			*ShieldBlockFragmentClassPath,
			FrozenMaxSlots,
			FrozenMaxWeight));
#endif
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::AuditInventoryTravelFixtureForAutomation()
{
#if UE_BUILD_SHIPPING
	return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
		false, TEXT("shipping_rejects_inventory_fixture"));
#else
	if (!bArmed || LastAcceptedTravelKind == EEFCalystoDungeonTravelKindV6::None
		|| !bObservedFloorReady || bTravelFailed)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false, TEXT("fixture_has_no_successful_accepted_floor_to_audit"));
	}
	const bool bExpectRecall =
		LastAcceptedTravelKind != EEFCalystoDungeonTravelKindV6::NewRun
		&& LastAcceptedTravelKind != EEFCalystoDungeonTravelKindV6::RestartSameSeed;
	FString Document;
	FString Hash;
	FString Error;
	if (!AuditFixtureState(ResolveEquipment(), bExpectRecall, Document, Hash, Error))
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(false, Error);
	}
	const bool bObservedTypedRestore = InventoryChangedCount > 0
		&& CurrencyChangedCount > 0;
	const bool bObservedRecallPurge = bExpectRecall || ItemRemovedCount >= 2;
	if (!bObservedTypedRestore || !bObservedRecallPurge)
	{
		return ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
			false,
			FString::Printf(
				TEXT("missing_typed_delegate_evidence|inventory_changed=%d|item_removed=%d|currency_changed=%d"),
				InventoryChangedCount, ItemRemovedCount, CurrencyChangedCount));
	}
	const FString Result = ProjectCalystoInventoryTravelFixturePrivate::MakeResult(
		true,
		FString::Printf(
			TEXT("kind=%s|run_epoch=%lld|floor=%lld|hash=%s|inventory_changed=%d|item_added=%d|item_removed=%d|currency_changed=%d|recall_count=%d|weight_bits=%08X|max_slots=%d|max_weight=%d"),
			*TravelKindName(LastAcceptedTravelKind),
			LastAcceptedRunEpoch,
			LastReadyFloor,
			*Hash,
			InventoryChangedCount,
			ItemAddedCount,
			ItemRemovedCount,
			CurrencyChangedCount,
			bExpectRecall ? ProjectCalystoInventoryTravelFixturePrivate::FixtureRecallCount : 0,
			ProjectCalystoInventoryTravelFixturePrivate::FloatBits(
				ResolveEquipment()->GetCurrentInventoryTotalWeight()),
			ResolveEquipment()->GetMaxInventorySlots(),
			ResolveEquipment()->GetMaxInventoryWeight()));
	bObservedFloorReady = false;
	return Result;
#endif
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::GetFrozenInventoryFixtureDocumentForAutomation() const
{
	return FrozenDocument;
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::GetFrozenInventoryFixtureHashForAutomation() const
{
	return FrozenHash;
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleBeforeDirectorTravel(
	const EEFCalystoDungeonTravelKindV6 TravelKind)
{
#if !UE_BUILD_SHIPPING
	if (!bArmed)
	{
		return;
	}
	PendingTravelKind = TravelKind;
	bObservedFloorReady = false;
	bTravelFailed = false;
	InventoryChangedCount = 0;
	ItemAddedCount = 0;
	ItemRemovedCount = 0;
	CurrencyChangedCount = 0;
	UnbindInventoryDelegates();
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleDirectorWorldAccepted(
	const int64 AcceptedRunEpoch,
	const EEFCalystoDungeonTravelKindV6 TravelKind,
	const FEFCalystoResolvedFloorIntentV6& Intent)
{
#if !UE_BUILD_SHIPPING
	if (!bArmed)
	{
		return;
	}
	LastAcceptedRunEpoch = AcceptedRunEpoch;
	LastReadyFloor = Intent.GenerationContext.FloorNumber;
	LastAcceptedTravelKind = TravelKind;
	PendingTravelKind = TravelKind;
#else
	(void)AcceptedRunEpoch;
	(void)TravelKind;
	(void)Intent;
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleFloorReady(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV6& Intent,
	const FEFCalystoRealizedFloorManifestV6& Manifest)
{
	(void)PCGSeed;
	(void)Manifest;
#if !UE_BUILD_SHIPPING
	if (bArmed && LastAcceptedTravelKind != EEFCalystoDungeonTravelKindV6::None
		&& FloorNumber == LastReadyFloor
		&& Intent.GenerationContext.FloorNumber == LastReadyFloor)
	{
		bObservedFloorReady = true;
		PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	}
#else
	(void)FloorNumber;
	(void)Intent;
#endif
}

void UProjectCalystoInventoryTravelFixtureSubsystem::HandleFloorTravelFailed()
{
#if !UE_BUILD_SHIPPING
	if (bArmed)
	{
		bTravelFailed = true;
		bObservedFloorReady = false;
		PendingTravelKind = EEFCalystoDungeonTravelKindV6::None;
	}
#endif
}

FString UProjectCalystoInventoryTravelFixtureSubsystem::TravelKindName(
	const EEFCalystoDungeonTravelKindV6 TravelKind)
{
	switch (TravelKind)
	{
	case EEFCalystoDungeonTravelKindV6::NewRun:
		return TEXT("NewRun");
	case EEFCalystoDungeonTravelKindV6::RestartSameSeed:
		return TEXT("RestartSameSeed");
	case EEFCalystoDungeonTravelKindV6::Replay:
		return TEXT("Replay");
	case EEFCalystoDungeonTravelKindV6::Retry:
		return TEXT("Retry");
	case EEFCalystoDungeonTravelKindV6::Reroll:
		return TEXT("Reroll");
	case EEFCalystoDungeonTravelKindV6::Advance:
		return TEXT("Advance");
	case EEFCalystoDungeonTravelKindV6::DevelopmentJump:
		return TEXT("DevelopmentJump");
	case EEFCalystoDungeonTravelKindV6::RecoverToHub:
		return TEXT("RecoverToHub");
	default:
		return TEXT("None");
	}
}
