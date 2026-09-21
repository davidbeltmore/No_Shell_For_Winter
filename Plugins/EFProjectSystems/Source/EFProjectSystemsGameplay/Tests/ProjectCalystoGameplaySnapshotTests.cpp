#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/ProjectCalystoGameplaySnapshot.h"
#include "Calysto/ProjectCalystoFloorOutcomeSubsystem.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Tests/ProjectCalystoGameplaySnapshotTestTypes.h"
#include "Actors/ACFCharacter.h"
#include "Components/ACFEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "ItemActors/ACFItemActor.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/GarbageCollection.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_CalystoSnapshotFixtureSlot, "Tests.Calysto.SnapshotSlot");

namespace ProjectCalystoSnapshotTests
{
	FGuid Id(uint32 Value) { return FGuid(0x534E4150, 0x53484F54, 0, Value); }
	FACFInventoryList& List(UACFEquipmentComponent* Equipment)
	{
		return *FindFProperty<FStructProperty>(UACFInventoryComponent::StaticClass(), TEXT("InventoryList"))
			->ContainerPtrToValuePtr<FACFInventoryList>(Equipment);
	}
	TArray<FInventoryItem>& Items(UACFEquipmentComponent* Equipment)
	{
		return *FindFProperty<FArrayProperty>(FACFInventoryList::StaticStruct(), TEXT("Inventory"))
			->ContainerPtrToValuePtr<TArray<FInventoryItem>>(&List(Equipment));
	}
	TArray<FEquippedItem>& Equipped(UACFEquipmentComponent* Equipment)
	{
		auto* Data = FindFProperty<FStructProperty>(UACFEquipmentComponent::StaticClass(), TEXT("Equipment"))
			->ContainerPtrToValuePtr<FEquipment>(Equipment);
		return *FindFProperty<FArrayProperty>(FEquipment::StaticStruct(), TEXT("EquippedItems"))
			->ContainerPtrToValuePtr<TArray<FEquippedItem>>(Data);
	}
	float& Currency(UACFEquipmentComponent* Equipment)
	{
		return *FindFProperty<FFloatProperty>(UACFCurrencyComponent::StaticClass(), TEXT("CurrencyAmount"))
			->ContainerPtrToValuePtr<float>(Equipment);
	}
	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<UProjectRunCompanionSubsystem> Roster;
		TStrongObjectPtr<UProjectCalystoFloorOutcomeSubsystem> Outcomes;
		FFixture()
		{
			if (!GEngine) return;
			Instance.Reset(NewObject<UGameInstance>());
			Roster.Reset(NewObject<UProjectRunCompanionSubsystem>(Instance.Get()));
			Outcomes.Reset(NewObject<UProjectCalystoFloorOutcomeSubsystem>(Instance.Get()));
			CreateWorld();
		}
		void CreateWorld()
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(Instance.Get());
		}
		void DestroyWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			World->MarkObjectsPendingKill(); World = nullptr;
		}
		~FFixture() { DestroyWorld(); }
		UACFEquipmentComponent* Inventory(APawn* Actor, uint32 ItemId) const
		{
			auto* Equipment = Actor->FindComponentByClass<UACFEquipmentComponent>();
			if (!Equipment)
			{
				Equipment = NewObject<UACFEquipmentComponent>(Actor);
				Actor->AddInstanceComponent(Equipment); Equipment->RegisterComponent();
			}
			Equipment->SetIsReplicated(true); Equipment->SetIsInitialized(true); List(Equipment).Init(Actor);
			if (ItemId == 0) return Equipment;
			auto* Item = NewObject<UACFItem>(Actor); Item->SetItemOwner(Actor);
			auto* Fragment = NewObject<UProjectCalystoSnapshotFixtureFragment>(Actor);
			Fragment->SavedValues.Add(TEXT("Damage"), 7); Fragment->SavedValues.Add(TEXT("Defense"), 11);
			Item->Fragments.Add(Fragment); Equipment->RegisterFragmentsForItem(Item);
			auto& Entry = Items(Equipment).AddDefaulted_GetRef(); Entry.Item = Item; Entry.ItemClass = Item->GetClass();
			Entry.ForceGuid(Id(ItemId)); Entry.Count = 3; Entry.LastObservedCount = 3;
			List(Equipment).MarkItemDirty(Entry); Currency(Equipment) = 37.25f;
			return Equipment;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectCalystoGameplaySnapshotTest,
	"NoShellForWinter.CalystoDungeon.Director.GameplaySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FProjectCalystoGameplaySnapshotTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("SameWorldIdentityAndRollback")); Commands.Add(TEXT("SameWorld"));
	Names.Add(TEXT("PortableWorldReconstruction")); Commands.Add(TEXT("Portable"));
}
bool FProjectCalystoGameplaySnapshotTest::RunTest(const FString& Parameters)
{
	using namespace ProjectCalystoSnapshotTests;
	if (Parameters == TEXT("Portable"))
	{
		FFixture F; if (!F.World) return false;
		APawn* Player = F.World->SpawnActor<APawn>(); auto* Companion = F.World->SpawnActor<AACFCharacter>();
		if (!Player || !Companion) return false;
		auto* PlayerEquipment = F.Inventory(Player, 30); F.Inventory(Player, 31);
		auto* CompanionEquipment = F.Inventory(Companion, 32);
		auto* Fragment = CastChecked<UProjectCalystoSnapshotFixtureFragment>(Items(PlayerEquipment)[0].Item->Fragments[0]);
		Fragment->LinkedItem = Items(PlayerEquipment)[1].Item;
		Fragment->LinkedFragment = Items(CompanionEquipment)[0].Item->Fragments[0];
		auto& Record = F.Roster->Roster.Add(Id(33)); Record.Snapshot.Definition.StableCompanionId = Id(33);
		Record.Snapshot.Definition.ResolvedLevel = 19; Record.LiveActor = Companion; Record.CorpseActor = Companion;
		Record.bDesiredActiveParty = true; // The existing capture contract permits this exact projection alias.
		F.Roster->RunEpoch = 7; F.Roster->CurrentFloor = 2; F.Roster->CurrentGenerationSerial = 9;
		F.Outcomes->TrackedRunEpoch = 7; F.Outcomes->TrackedFloorNumber = 2; F.Outcomes->TrackedRunSeed = 715;
		F.Outcomes->FloorDeaths = 2; F.Outcomes->FloorFailures = 1;
		FString Error;
		Items(PlayerEquipment)[1].ForceGuid(Id(30));
		TestFalse(TEXT("Duplicate stable item identity rejects capture before owning travel"),
			FProjectCalystoGameplaySnapshot::Capture(Id(34), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error).IsValid());
		Items(PlayerEquipment)[1].ForceGuid(Id(31));
		auto Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(34), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
		if (!TestTrue(*Error, Snapshot.IsValid())) return false;
		const FString Hash = Snapshot->GetCanonicalHash();
		TestTrue(TEXT("Portable graph freezes while retaining same-world cancellation"), Snapshot->PrepareForTravel(Id(34), Error));
		TestEqual(TEXT("Preparation does not clear live inventory"), Items(PlayerEquipment).Num(), 2);
		Items(PlayerEquipment)[0].Count = 1;
		TestFalse(TEXT("Actual gameplay mutation rejects detachment"), Snapshot->DetachForTravel(Id(34), Error));
		TestTrue(TEXT("Prepared travel still permits exact same-world rollback"), Snapshot->Restore(Id(34), Error));
		TestTrue(TEXT("Prepared travel can be cancelled cleanly"), Snapshot->Release(Id(34), Error));
		// A fresh capture proves operational readiness and time origins are not gameplay hash inputs.
		F.Roster->bCompanionRosterReady = true; F.Roster->bFloorReady = true; F.Outcomes->FloorReadySeconds = 321.;
		Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(35), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
		if (!TestTrue(*Error, Snapshot.IsValid())) return false;
		TestEqual(TEXT("Readiness/time origins do not alter the canonical graph"), Snapshot->GetCanonicalHash(), Hash);
		TestTrue(TEXT("Second portable preparation succeeds"), Snapshot->PrepareForTravel(Id(35), Error));
		F.Roster->bCompanionRosterReady = false; F.Roster->bFloorReady = false; F.Roster->bGenerationOrTravelActive = true;
		++F.Roster->CurrentGenerationSerial; F.Outcomes->FloorReadySeconds = -1.;
		TestFalse(TEXT("Stale request cannot detach"), Snapshot->DetachForTravel(Id(34), Error));
		if (!TestTrue(*Error, Snapshot->DetachForTravel(Id(35), Error))) return false;
		TWeakObjectPtr<UWorld> SourceWorld = F.World; TWeakObjectPtr<UACFItem> SourceItem = Items(PlayerEquipment)[0].Item;
		TWeakObjectPtr<UACFItemFragment> SourceFragment = Fragment; TWeakObjectPtr<AActor> SourcePlayer = Player;
		F.DestroyWorld(); Player = nullptr; Companion = nullptr; PlayerEquipment = CompanionEquipment = nullptr; Fragment = nullptr;
		// A real collection after the owned world teardown proves the transport holds no source-world objects.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
		TestFalse(TEXT("Source world was collected"), SourceWorld.IsValid());
		TestFalse(TEXT("Source player was collected"), SourcePlayer.IsValid());
		TestFalse(TEXT("Source item was collected"), SourceItem.IsValid());
		TestFalse(TEXT("Source fragment was collected"), SourceFragment.IsValid());
		TestTrue(TEXT("World identity is gone after GC, not merely pending destruction"), SourceWorld.IsStale(false, true));
		TestTrue(TEXT("Item identity is gone after GC, not retained by portable bytes"), SourceItem.IsStale(false, true));
		TestFalse(TEXT("Detached state cannot falsely claim same-world rollback"), Snapshot->Restore(Id(35), Error));
		F.CreateWorld(); if (!F.World) return false;
		Player = F.World->SpawnActor<APawn>(); Companion = F.World->SpawnActor<AACFCharacter>();
		if (!Player || !Companion) return false;
		PlayerEquipment = F.Inventory(Player, 0); CompanionEquipment = F.Inventory(Companion, 0);
		Currency(PlayerEquipment) = 9.5f; Currency(CompanionEquipment) = 12.5f;
		TStrongObjectPtr<UProjectCalystoSnapshotFixtureFragment> Observer(NewObject<UProjectCalystoSnapshotFixtureFragment>(F.Instance.Get()));
		for (auto* Component : {PlayerEquipment, CompanionEquipment})
		{
			Component->OnInventoryChanged.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveChanged);
			Component->OnItemAdded.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveItem);
			Component->OnItemRemoved.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveItem);
			Component->OnCurrencyChanged.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveCurrency);
			Component->OnEquipmentChanged.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveEquipment);
		}
		F.Roster->OnRosterChanged.AddDynamic(Observer.Get(), &UProjectCalystoSnapshotFixtureFragment::ObserveChanged);
		PlayerEquipment->OnInventoryChanged.Broadcast(); TestEqual(TEXT("Destination event observer is active"), Observer->ObservedEvents, 1); Observer->ObservedEvents = 0;
		TArray<FProjectCalystoInventoryDestination> Destinations = {{FGuid(), false, Player}, {Id(33), false, Companion}};
		TestFalse(TEXT("Stale reconstruction rejects without writes"), Snapshot->ReconstructInWorld(Id(34), F.World, Destinations, Error));
		Destinations[1].Actor = nullptr;
		TestFalse(TEXT("Missing destination owner rejects atomically"), Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error));
		Destinations[1].Actor = Player;
		TestFalse(TEXT("Shared destination owner rejects atomically"), Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error));
		Destinations[1].Actor = Companion;
		CompanionEquipment->SetMaxInventorySlots(0);
		TestFalse(TEXT("Insufficient companion slots reject the whole graph"), Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error));
		TestEqual(TEXT("Player baseline remains empty after companion rejection"), Items(PlayerEquipment).Num(), 0);
		TestEqual(TEXT("Player baseline currency is untouched"), Currency(PlayerEquipment), 9.5f);
		CompanionEquipment->SetMaxInventorySlots(40); CompanionEquipment->SetMaxInventoryWeight(1);
		TestFalse(TEXT("Insufficient native weight rejects without overriding destination capacity"), Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error));
		TestEqual(TEXT("Companion baseline currency is untouched"), Currency(CompanionEquipment), 12.5f);
		CompanionEquipment->SetMaxInventoryWeight(180);
		// The real DungeonGeneration Player initializes a nonempty default
		// inventory before V7 restores the detached run. It must be replaced,
		// never appended, through ACF's public lifecycle.
		F.Inventory(Player, 40); F.Inventory(Companion, 41);
		TestEqual(TEXT("Player destination default baseline is present"), Items(PlayerEquipment).Num(), 1);
		TestEqual(TEXT("Companion destination default baseline is present"), Items(CompanionEquipment).Num(), 1);
		if (!TestTrue(*Error, Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error))) return false;
		TestEqual(TEXT("World-independent canonical hash is preserved"), Snapshot->GetCanonicalHash(), Hash);
		TestTrue(TEXT("Reconstructed exact native state verifies"), Snapshot->VerifyUnchanged(Id(35), Error));
		TestEqual(TEXT("Exactly two player items are reconstructed"), Items(PlayerEquipment).Num(), 2);
		TestEqual(TEXT("Exactly one companion item is reconstructed"), Items(CompanionEquipment).Num(), 1);
		TestEqual(TEXT("Stable item GUID survives travel"), Items(PlayerEquipment)[0].GetItemGuid(), Id(30));
		TestEqual(TEXT("Stable companion item GUID survives travel"), Items(CompanionEquipment)[0].GetItemGuid(), Id(32));
		for (auto* Component : {PlayerEquipment, CompanionEquipment}) for (const auto& Item : Items(Component))
		{
			TestEqual(TEXT("Native class is preserved"), Item.Item->GetClass(), UACFItem::StaticClass());
			TestEqual(TEXT("Exact stack count is preserved"), Item.Count, 3);
			TestFalse(TEXT("No equipped flag is fabricated"), Item.bIsEquipped);
			TestFalse(TEXT("No equipment slot is fabricated"), Item.EquipmentSlot.IsValid());
			TestEqual(TEXT("Exact fragment class is preserved"), Item.Item->Fragments[0]->GetClass(), UProjectCalystoSnapshotFixtureFragment::StaticClass());
			TestTrue(TEXT("Each reconstructed fragment is natively registered"), Component->GetRegisteredFragments().Contains(Item.Item->Fragments[0]));
		}
		Fragment = CastChecked<UProjectCalystoSnapshotFixtureFragment>(Items(PlayerEquipment)[0].Item->Fragments[0]);
		TestTrue(TEXT("Cross-item reference resolves to the destination graph"), Fragment->LinkedItem == Items(PlayerEquipment)[1].Item);
		TestTrue(TEXT("Cross-inventory fragment reference resolves to destination"), Fragment->LinkedFragment == Items(CompanionEquipment)[0].Item->Fragments[0]);
		TestEqual(TEXT("Saved fragment values survive native reconstruction"), Fragment->SavedValues.FindRef(TEXT("Defense")), 11);
		TestEqual(TEXT("Fractional currency survives travel"), Currency(PlayerEquipment), 37.25f);
		TestTrue(TEXT("Roster live projection uses the new native actor"), F.Roster->Roster.FindChecked(Id(33)).LiveActor.Get() == Companion);
		TestTrue(TEXT("Roster alias does not lose or duplicate an owner"), F.Roster->Roster.FindChecked(Id(33)).CorpseActor.Get() == Companion);
		TestFalse(TEXT("Duplicate reconstruction cannot append items"), Snapshot->ReconstructInWorld(Id(35), F.World, Destinations, Error));
		TestEqual(TEXT("Retry leaves exact membership"), Items(PlayerEquipment).Num(), 2);
		TestEqual(TEXT("Reconstruction emits no inventory/equipment/currency/roster event"), Observer->ObservedEvents, 0);
		TestTrue(TEXT("Reconstructed rejected state releases cleanly"), Snapshot->Release(Id(35), Error));
		Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(36), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
		if (!TestTrue(*Error, Snapshot.IsValid())) return false;
		Items(PlayerEquipment)[0].Count = 2; F.Outcomes->FloorDeaths = 5;
		TestFalse(TEXT("Changed accepted state cannot use rejected release"), Snapshot->Release(Id(36), Error));
		TestTrue(TEXT("Explicit accepted release validates current state without rollback"), Snapshot->ReleaseAccepted(Id(36), Error));
		TestEqual(TEXT("Accepted release preserves accepted count"), Items(PlayerEquipment)[0].Count, 2);
		TestEqual(TEXT("Accepted release never rewinds outcomes"), F.Outcomes->FloorDeaths, 5);
		TestTrue(TEXT("Exact accepted release is idempotent"), Snapshot->ReleaseAccepted(Id(36), Error));
		TestFalse(TEXT("Accepted lease can never become rejected cleanup"), Snapshot->Release(Id(36), Error));
		TestFalse(TEXT("Stale accepted release rejects"), Snapshot->ReleaseAccepted(Id(35), Error));
		return true;
	}
	FFixture F; if (!F.World) return false;
	APawn* Player = F.World->SpawnActor<APawn>(); AACFCharacter* Companion = F.World->SpawnActor<AACFCharacter>();
	if (!Player || !Companion) return false;
	auto* PlayerEquipment = F.Inventory(Player, 10); auto* CompanionEquipment = F.Inventory(Companion, 11);
	F.Inventory(Player, 12); // A second real item/fragment makes capture order observable.
	auto* OriginalItem = Items(PlayerEquipment)[0].Item;
	auto* Fragment = CastChecked<UProjectCalystoSnapshotFixtureFragment>(OriginalItem->Fragments[0]);
	auto* ItemActor = F.World->SpawnActor<AACFItemActor>(); if (!ItemActor) return false;
	ItemActor->SetItemOwner(Player); ItemActor->ItemDefinition = OriginalItem;
	Items(PlayerEquipment)[0].bIsEquipped = true; Items(PlayerEquipment)[0].EquipmentSlot = TAG_CalystoSnapshotFixtureSlot;
	auto& Slot = Equipped(PlayerEquipment).AddDefaulted_GetRef();
	Slot.ItemGuid = Id(10); Slot.ItemSlot = TAG_CalystoSnapshotFixtureSlot; Slot.Item = OriginalItem;
	Slot.ItemClass = OriginalItem->GetClass(); Slot.ItemActor = ItemActor; Slot.ReplicationID = 73;
	const int32 InventoryIdentity = Items(PlayerEquipment)[0].ReplicationID;
	const int32 CompanionIdentity = Items(CompanionEquipment)[0].ReplicationID;
	auto& Record = F.Roster->Roster.Add(Id(20)); Record.Snapshot.Definition.StableCompanionId = Id(20);
	Record.Snapshot.Definition.ResolvedLevel = 12; Record.LiveActor = Companion; Record.bDesiredActiveParty = true;
	F.Roster->RunEpoch = 17; F.Roster->CurrentFloor = 2; F.Roster->CurrentGenerationSerial = 23;
	F.Roster->bCompanionRosterReady = F.Roster->bFloorReady = true;
	F.Outcomes->TrackedRunSeed = 314; F.Outcomes->TrackedRunEpoch = 17; F.Outcomes->TrackedFloorNumber = 2;
	F.Outcomes->FloorDeaths = 1; F.Outcomes->FloorFailures = 2;
	for (auto* Equipment : { PlayerEquipment, CompanionEquipment })
	{
		Equipment->OnInventoryChanged.AddDynamic(Fragment, &UProjectCalystoSnapshotFixtureFragment::ObserveChanged);
		Equipment->OnItemAdded.AddDynamic(Fragment, &UProjectCalystoSnapshotFixtureFragment::ObserveItem);
		Equipment->OnItemRemoved.AddDynamic(Fragment, &UProjectCalystoSnapshotFixtureFragment::ObserveItem);
		Equipment->OnCurrencyChanged.AddDynamic(Fragment, &UProjectCalystoSnapshotFixtureFragment::ObserveCurrency);
	}
	F.Roster->OnRosterChanged.AddDynamic(Fragment, &UProjectCalystoSnapshotFixtureFragment::ObserveChanged);
	PlayerEquipment->OnInventoryChanged.Broadcast();
	TestEqual(TEXT("The real native event observer works"), Fragment->ObservedEvents, 1); Fragment->ObservedEvents = 0;
	FString Error;
	auto Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(1), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
	if (!TestTrue(*Error, Snapshot.IsValid())) return false;
	TestTrue(TEXT("Equipped travel freezes its native lifecycle bridge without source mutation"), Snapshot->PrepareForTravel(Id(1), Error));
	TestTrue(TEXT("Prepared equipped travel leaves exact same-world rollback intact"), Snapshot->VerifyUnchanged(Id(1), Error));
	const FString OriginalHash = Snapshot->GetCanonicalHash();
	TestFalse(TEXT("Actual nonempty state has a nonempty hash"), OriginalHash.IsEmpty());
	TestTrue(TEXT("Exact unchanged state verifies"), Snapshot->VerifyUnchanged(Id(1), Error));
	TestFalse(TEXT("An overlapping owner cannot capture"), FProjectCalystoGameplaySnapshot::Capture(Id(2), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error).IsValid());
	// TArray::Swap uses byte relocation, preserving native FastArray identities.
	Items(PlayerEquipment).Swap(0, 1);
	TestTrue(TEXT("Reordering actual inventory storage preserves the captured state"), Snapshot->VerifyUnchanged(Id(1), Error));
	TestTrue(TEXT("Unchanged lease releases"), Snapshot->Release(Id(1), Error));
	auto Reordered = FProjectCalystoGameplaySnapshot::Capture(Id(4), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
	if (!TestTrue(*Error, Reordered.IsValid())) return false;
	TestEqual(TEXT("Recapture canonicalizes object and fragment records independently of inventory order"), Reordered->GetCanonicalHash(), OriginalHash);
	Items(PlayerEquipment).Swap(0, 1);
	TestTrue(TEXT("Returning storage order preserves the recaptured state"), Reordered->VerifyUnchanged(Id(4), Error));
	TestTrue(TEXT("Reordered capture releases cleanly"), Reordered->Release(Id(4), Error));
	Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(2), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
	if (!TestTrue(*Error, Snapshot.IsValid())) return false;
	TestEqual(TEXT("Request IDs never affect canonical gameplay state"), Snapshot->GetCanonicalHash(), OriginalHash);
	Fragment->SavedValues.Reset(); Fragment->SavedValues.Add(TEXT("Defense"), 11); Fragment->SavedValues.Add(TEXT("Damage"), 7);
	TestTrue(TEXT("Equivalent map insertion order is canonical"), Snapshot->VerifyUnchanged(Id(2), Error));
	F.Roster->Roster.FindChecked(Id(20)).Snapshot.Definition.ResolvedLevel = 99;
	TestFalse(TEXT("Non-SaveGame roster fields are actual hash inputs"), Snapshot->VerifyUnchanged(Id(2), Error));
	TestTrue(TEXT("Roster-only mutation restores without events"), Snapshot->Restore(Id(2), Error));
	TestEqual(TEXT("Exact companion definition restores"), F.Roster->Roster.FindChecked(Id(20)).Snapshot.Definition.ResolvedLevel, 12);
	Items(PlayerEquipment)[0].Count = 1; Items(CompanionEquipment)[0].Count = 2;
	Currency(PlayerEquipment) = 0.5f; Fragment->Charges = 0;
	F.Outcomes->FloorDeaths = 5; F.Outcomes->FloorFailures = 9;
	TestFalse(TEXT("A stale token cannot restore mutated state"), Snapshot->Restore(Id(1), Error));
	TestEqual(TEXT("Stale rejection happens before any mutation"), Items(PlayerEquipment)[0].Count, 1);
	TestTrue(TEXT("The owned transaction restores all original owners"), Snapshot->Restore(Id(2), Error));
	TestEqual(TEXT("Player count restores"), Items(PlayerEquipment)[0].Count, 3);
	TestEqual(TEXT("Companion count restores"), Items(CompanionEquipment)[0].Count, 3);
	TestEqual(TEXT("Fractional currency restores exactly"), Currency(PlayerEquipment), 37.25f);
	TestEqual(TEXT("Saved fragment data restores"), Fragment->Charges, 7);
	TestEqual(TEXT("Deaths restore"), F.Outcomes->FloorDeaths, 1); TestEqual(TEXT("Failures restore"), F.Outcomes->FloorFailures, 2);
	TestTrue(TEXT("Original item UObject is preserved"), Items(PlayerEquipment)[0].Item == OriginalItem);
	TestEqual(TEXT("Player FastArray identity survives copy and dirty publication"), Items(PlayerEquipment)[0].ReplicationID, InventoryIdentity);
	TestEqual(TEXT("Companion FastArray identity survives"), Items(CompanionEquipment)[0].ReplicationID, CompanionIdentity);
	TestEqual(TEXT("Equipment FastArray identity survives"), Equipped(PlayerEquipment)[0].ReplicationID, 73);
	TestTrue(TEXT("Exact equipment actor survives"), Equipped(PlayerEquipment)[0].ItemActor == ItemActor);
	auto* Extra = NewObject<UProjectCalystoSnapshotFixtureFragment>(Player);
	OriginalItem->Fragments[0] = Extra;
	TestFalse(TEXT("Changing an item's fragment identity is observable"), Snapshot->VerifyUnchanged(Id(2), Error));
	TestTrue(TEXT("The original fragment array restores"), Snapshot->Restore(Id(2), Error));
	TestTrue(TEXT("Original fragment UObject is retained"), OriginalItem->Fragments[0] == Fragment);
	PlayerEquipment->UnregisterFragment(Fragment); PlayerEquipment->RegisterFragment(Extra);
	TestFalse(TEXT("Changing only the actual registry is observable"), Snapshot->VerifyUnchanged(Id(2), Error));
	TestTrue(TEXT("Actual fragment replication registry restores"), Snapshot->Restore(Id(2), Error));
	TestTrue(TEXT("Original fragment is registered"), PlayerEquipment->GetRegisteredFragments().Contains(Fragment));
	TestFalse(TEXT("Added fragment is unregistered"), PlayerEquipment->GetRegisteredFragments().Contains(Extra));
	Items(PlayerEquipment)[0].ReplicationID = 999;
	TestFalse(TEXT("FastArray identity changes are observable"), Snapshot->VerifyUnchanged(Id(2), Error));
	TestTrue(TEXT("Original FastArray identity restores explicitly"), Snapshot->Restore(Id(2), Error));
	TestEqual(TEXT("Restoration does not allocate a replacement identity"), Items(PlayerEquipment)[0].ReplicationID, InventoryIdentity);
	Currency(PlayerEquipment) = 9.f; ++F.Roster->CurrentGenerationSerial;
	TestFalse(TEXT("Changed generation ownership forbids restoration"), Snapshot->Restore(Id(2), Error));
	TestEqual(TEXT("Generation rejection precedes currency mutation"), Currency(PlayerEquipment), 9.f);
	--F.Roster->CurrentGenerationSerial; TestTrue(TEXT("Original owned generation can restore"), Snapshot->Restore(Id(2), Error));
	TestEqual(TEXT("Capture, verification and restore emitted no inventory/currency/roster event"), Fragment->ObservedEvents, 0);
	TestTrue(TEXT("Clean rejected lease releases"), Snapshot->Release(Id(2), Error));
	TestTrue(TEXT("Exact rejected release is idempotent"), Snapshot->Release(Id(2), Error));
	TestFalse(TEXT("Retired lease cannot be released by another request"), Snapshot->Release(Id(1), Error));
	Fragment->SavedLabel = FString::ChrN(65537, TEXT('X'));
	TestFalse(TEXT("Oversized saved text rejects before acquiring a lease"),
		FProjectCalystoGameplaySnapshot::Capture(Id(3), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error).IsValid());
	Fragment->SavedLabel = TEXT("Ledger");
	Snapshot = FProjectCalystoGameplaySnapshot::Capture(Id(3), F.World, Player, F.Roster.Get(), F.Outcomes.Get(), Error);
	if (!TestTrue(*Error, Snapshot.IsValid())) return false;
	Currency(PlayerEquipment) = 8.f; ItemActor->Destroy();
	TestFalse(TEXT("Destroyed original equipment actor is unrecoverable"), Snapshot->Restore(Id(3), Error));
	TestEqual(TEXT("Owner-loss validation occurs before restoration"), Currency(PlayerEquipment), 8.f);
	TestFalse(TEXT("Unrecoverable cleanup cannot claim clean release"), Snapshot->Release(Id(3), Error));
	Snapshot.Reset();
	return true;
}

#endif
