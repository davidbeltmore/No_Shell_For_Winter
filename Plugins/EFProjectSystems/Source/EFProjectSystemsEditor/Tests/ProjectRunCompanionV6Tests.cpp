#include "Companions/ProjectRunCompanionTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/ACFCharacter.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/ProjectCalystoPopulationBridge.h"
#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Misc/AutomationTest.h"

namespace ProjectRunCompanionV6TestsPrivate
{
	FProjectCompanionRunEntrySnapshot MakeEntry(const int32 Index)
	{
		FProjectCompanionRunEntrySnapshot Entry;
		Entry.Definition.StableCompanionId = FGuid(0xC011AB00 + Index, 0xD1EEC700, 0xA11CE000, Index);
		Entry.Definition.SourceSpawnId = *FString::Printf(TEXT("NPC:Test:%d"), Index);
		Entry.Definition.ContentId = *FString::Printf(TEXT("NPC.Companion.Test.%d"), Index);
		Entry.Definition.CatalogVariantId = *FString::Printf(TEXT("NPC.Companion.Test.Variant.%d"), Index);
		Entry.Definition.CharacterClass = TSoftClassPtr<AACFCharacter>(FSoftObjectPath(
			TEXT("/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C")));
		Entry.Definition.Archetype = TEXT("Generalist");
		Entry.Definition.Gender = TEXT("Female");
		Entry.Definition.DifficultyGrade = EProjectCompanionDifficultyGrade::Common;
		Entry.Definition.ResolvedLevel = 1 + Index;
		Entry.Definition.Lifecycle = EProjectCompanionLifecycle::Recruitable;
		Entry.State = EProjectCompanionRunState::Alive;
		return Entry;
	}

	FProjectCompanionRunSnapshot MakeSnapshot(const int32 Count)
	{
		FProjectCompanionRunSnapshot Snapshot;
		Snapshot.RunEpoch = 17;
		Snapshot.FloorNumber = 8;
		Snapshot.GenerationSerial = 3;
		for (int32 Index = 1; Index <= Count; ++Index)
		{
			Snapshot.Entries.Add(MakeEntry(Index));
		}
		Snapshot.RefreshHash();
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRunCompanionSnapshotCanonicalTest,
	"NoShellForWinter.CalystoDungeon.V6.Companions.SnapshotCanonicalHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRunCompanionSnapshotCanonicalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace ProjectRunCompanionV6TestsPrivate;

	FProjectCompanionRunSnapshot Snapshot = MakeSnapshot(2);
	Snapshot.ActiveParty.Add(Snapshot.Entries[0].Definition.StableCompanionId);
	Snapshot.ActiveParty.Add(Snapshot.Entries[1].Definition.StableCompanionId);
	Snapshot.RefreshHash();
	FString Error;
	TestTrue(TEXT("A canonical living roster is valid"), Snapshot.IsValid(Error));
	TestEqual(TEXT("Snapshot hash is SHA-256 length"), Snapshot.SnapshotHash.Len(), 64);

	FProjectCompanionRunSnapshot Reordered = Snapshot;
	Reordered.Entries.Swap(0, 1);
	Reordered.ActiveParty.Swap(0, 1);
	Reordered.RefreshHash();
	TestEqual(TEXT("Entry and party authoring order do not affect the canonical hash"),
		Reordered.SnapshotHash, Snapshot.SnapshotHash);

	FProjectCompanionRunSnapshot NewEpoch = Snapshot;
	NewEpoch.RunEpoch = 999;
	NewEpoch.RefreshHash();
	TestEqual(TEXT("RunEpoch is intentionally excluded from deterministic roster hashing"),
		NewEpoch.SnapshotHash, Snapshot.SnapshotHash);
	TestTrue(TEXT("A different positive RunEpoch remains valid"), NewEpoch.IsValid(Error));

	FProjectCompanionRunSnapshot DifferentSpawn = Snapshot;
	DifferentSpawn.Entries[0].Definition.SourceSpawnId = TEXT("NPC:Test:DifferentSpawn");
	DifferentSpawn.RefreshHash();
	TestNotEqual(TEXT("The originating per-instance SpawnId participates in the canonical hash"),
		DifferentSpawn.SnapshotHash, Snapshot.SnapshotHash);

	FProjectCompanionRunSnapshot DifferentVariant = Snapshot;
	DifferentVariant.Entries[0].Definition.CatalogVariantId = TEXT("NPC.Companion.Test.OtherVariant");
	DifferentVariant.RefreshHash();
	TestNotEqual(TEXT("The reusable catalog variant participates in the canonical hash"),
		DifferentVariant.SnapshotHash, Snapshot.SnapshotHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectRunCompanionSnapshotFailClosedTest,
	"NoShellForWinter.CalystoDungeon.V6.Companions.SnapshotFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectRunCompanionSnapshotFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace ProjectRunCompanionV6TestsPrivate;
	FString Error;

	FProjectCompanionRunSnapshot Tampered = MakeSnapshot(1);
	Tampered.SnapshotHash[0] = Tampered.SnapshotHash[0] == TCHAR('0') ? TCHAR('1') : TCHAR('0');
	TestFalse(TEXT("A content/hash mismatch fails closed"), Tampered.IsValid(Error));

	FProjectCompanionRunSnapshot FirstGeneration = MakeSnapshot(0);
	FirstGeneration.FloorNumber = 1;
	FirstGeneration.GenerationSerial = 0;
	FirstGeneration.RefreshHash();
	TestTrue(TEXT("The canonical first V6 generation accepts serial zero"),
		FirstGeneration.IsValid(Error));

	FProjectCompanionRunSnapshot NegativeGeneration = FirstGeneration;
	NegativeGeneration.GenerationSerial = -1;
	NegativeGeneration.RefreshHash();
	TestFalse(TEXT("A negative generation serial fails closed"),
		NegativeGeneration.IsValid(Error));

	FProjectCompanionRunSnapshot FirstGenerationDeath = MakeSnapshot(1);
	FirstGenerationDeath.FloorNumber = 1;
	FirstGenerationDeath.GenerationSerial = 0;
	FirstGenerationDeath.Entries[0].State = EProjectCompanionRunState::PendingDead;
	FirstGenerationDeath.Entries[0].DeathFloor = 1;
	FirstGenerationDeath.Entries[0].DeathGenerationSerial = 0;
	FirstGenerationDeath.RefreshHash();
	TestTrue(TEXT("Death metadata may identify the canonical first generation"),
		FirstGenerationDeath.IsValid(Error));

	FProjectCompanionRunSnapshot MissingDeathMetadata = MakeSnapshot(1);
	MissingDeathMetadata.Entries[0].State = EProjectCompanionRunState::Dead;
	MissingDeathMetadata.RefreshHash();
	TestFalse(TEXT("Dead without death identity fails closed"), MissingDeathMetadata.IsValid(Error));

	FProjectCompanionRunSnapshot DeadPartyMember = MakeSnapshot(1);
	DeadPartyMember.Entries[0].State = EProjectCompanionRunState::Dead;
	DeadPartyMember.Entries[0].DeathFloor = 8;
	DeadPartyMember.Entries[0].DeathGenerationSerial = 3;
	DeadPartyMember.ActiveParty.Add(DeadPartyMember.Entries[0].Definition.StableCompanionId);
	DeadPartyMember.RefreshHash();
	TestFalse(TEXT("ActiveParty rejects dead entries"), DeadPartyMember.IsValid(Error));

	FProjectCompanionRunSnapshot TooManyActive = MakeSnapshot(3);
	for (const FProjectCompanionRunEntrySnapshot& Entry : TooManyActive.Entries)
	{
		TooManyActive.ActiveParty.Add(Entry.Definition.StableCompanionId);
	}
	TooManyActive.RefreshHash();
	TestFalse(TEXT("The active-party hard cap of two fails closed"), TooManyActive.IsValid(Error));

	FProjectCompanionRunSnapshot Duplicate = MakeSnapshot(2);
	Duplicate.Entries[1].Definition.StableCompanionId =
		Duplicate.Entries[0].Definition.StableCompanionId;
	Duplicate.RefreshHash();
	TestFalse(TEXT("Duplicate stable IDs fail closed"), Duplicate.IsValid(Error));

	FProjectCompanionRunSnapshot SharedVariant = MakeSnapshot(2);
	SharedVariant.Entries[1].Definition.ContentId =
		SharedVariant.Entries[0].Definition.ContentId;
	SharedVariant.Entries[1].Definition.CatalogVariantId =
		SharedVariant.Entries[0].Definition.CatalogVariantId;
	SharedVariant.RefreshHash();
	TestTrue(TEXT("Distinct stable companion instances may share one catalog entry and variant"),
		SharedVariant.IsValid(Error));

	FProjectCompanionRunSnapshot MissingSpawnIdentity = MakeSnapshot(1);
	MissingSpawnIdentity.Entries[0].Definition.SourceSpawnId = NAME_None;
	MissingSpawnIdentity.RefreshHash();
	TestFalse(TEXT("A recruited companion without its originating SpawnId fails closed"),
		MissingSpawnIdentity.IsValid(Error));

	FProjectCompanionRunSnapshot MissingVariantIdentity = MakeSnapshot(1);
	MissingVariantIdentity.Entries[0].Definition.CatalogVariantId = NAME_None;
	MissingVariantIdentity.RefreshHash();
	TestFalse(TEXT("A recruited companion without its concrete catalog variant fails closed"),
		MissingVariantIdentity.IsValid(Error));

	FProjectCompanionRunSnapshot UnknownState = MakeSnapshot(1);
	UnknownState.Entries[0].State = static_cast<EProjectCompanionRunState>(255);
	UnknownState.RefreshHash();
	TestFalse(TEXT("Unknown run-state enum values fail closed"), UnknownState.IsValid(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSameFloorCompanionRevivalFreezeTest,
	"NoShellForWinter.CalystoDungeon.V6.Companions.SameFloorRevivalFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSameFloorCompanionRevivalFreezeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEFCalystoResolvedFloorIntentV6 Intent;
	Intent.bIsValid = true;
	Intent.SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;
	Intent.GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;
	Intent.GenerationContext.RunSeed = 440044;
	Intent.GenerationContext.FloorNumber = 7;
	Intent.GenerationContext.GenerationSerial = 2;
	Intent.GenerationContext.PolicyHash = FString::ChrN(64, TEXT('A'));
	Intent.EcologyHash = TEXT("ecology-test");
	Intent.StyleId = TEXT("Standard");
	Intent.FloorPlan.FloorSeed = 987654;
	Intent.FloorPlan.StyleId = Intent.StyleId;
	Intent.FloorPlan.FloorPlanHash = FString::ChrN(64, TEXT('B'));

	FEFCalystoPopulationPlanV6 PopulationPlan;
	PopulationPlan.FloorSeed = Intent.FloorPlan.FloorSeed;
	PopulationPlan.FloorNumber = Intent.GenerationContext.FloorNumber;
	PopulationPlan.StyleId = Intent.StyleId;
	PopulationPlan.FloorPlanHash = Intent.FloorPlan.FloorPlanHash;
	PopulationPlan.RoomManifestHash = FString::ChrN(64, TEXT('C'));
	PopulationPlan.PopulationHash = FString::ChrN(64, TEXT('D'));

	FEFCalystoPopulationDecisionV6 Decision;
	Decision.Kind = EEFCalystoPopulationDecisionKindV6::Actor;
	Decision.StableRoomId = 101;
	Decision.StyleId = Intent.StyleId;
	Decision.ThemeId = TEXT("Forge");
	Decision.CategoryId = TEXT("NPC");
	Decision.EntryId = TEXT("NPC.Companion.Generalist.Female");
	Decision.Lifecycle = EEFCalystoLifecycleV6::Recruitable;
	Decision.ClassPath = FSoftObjectPath(
		TEXT("/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"));
	Decision.Tier = EEFCalystoRarityTierV6::Rare;
	Decision.DecisionId = TEXT("11111111111111111111111111111111AAA0");
	FEFCalystoRoomPopulationPlanV6& RoomPlan = PopulationPlan.Rooms.AddDefaulted_GetRef();
	RoomPlan.StableRoomId = Decision.StableRoomId;
	RoomPlan.ThemeId = Decision.ThemeId;
	RoomPlan.Decisions.Add(Decision);

	FProjectCompanionRunEntrySnapshot Current;
	FString Error;
	TestTrue(TEXT("The canonical bridge builds the same-floor recruit definition"),
		FProjectCalystoPopulationBridge::BuildRandomNPCDefinition(
			PopulationPlan, Decision, Current.Definition, Error));
	Current.State = EProjectCompanionRunState::PendingDead;
	Current.DeathFloor = Intent.GenerationContext.FloorNumber;
	Current.DeathGenerationSerial = Intent.GenerationContext.GenerationSerial;
	TestEqual(TEXT("The bridge preserves the deterministic source SpawnId"),
		Current.Definition.SourceSpawnId, FName(*Decision.DecisionId));
	TestEqual(TEXT("The bridge preserves the catalog content ID"),
		Current.Definition.ContentId, Decision.EntryId);
	TestEqual(TEXT("The V6 entry ID is the stable reusable catalog variant"),
		Current.Definition.CatalogVariantId, Decision.EntryId);

	FEFCalystoPopulationDecisionV6 SameVariantSecondInstance = Decision;
	SameVariantSecondInstance.DecisionId = TEXT("22222222222222222222222222222222AAA1");
	FProjectCompanionDefinition SameVariantDefinition;
	TestTrue(TEXT("A second spawn of the same catalog variant remains structurally valid"),
		FProjectCalystoPopulationBridge::BuildRandomNPCDefinition(
			PopulationPlan, SameVariantSecondInstance, SameVariantDefinition, Error));
	TestNotEqual(TEXT("Different SpawnIds produce different stable companion identities"),
		SameVariantDefinition.StableCompanionId, Current.Definition.StableCompanionId);
	TestEqual(TEXT("Different instances preserve the same reusable content ID"),
		SameVariantDefinition.ContentId, Current.Definition.ContentId);
	TestEqual(TEXT("Different instances preserve the same reusable variant ID"),
		SameVariantDefinition.CatalogVariantId, Current.Definition.CatalogVariantId);

	FProjectCompanionRunSnapshot FloorStart;
	FloorStart.RunEpoch = 3;
	FloorStart.FloorNumber = Intent.GenerationContext.FloorNumber;
	FloorStart.GenerationSerial = Intent.GenerationContext.GenerationSerial;
	FloorStart.RefreshHash();

	FEFCalystoResolvedCompanionLevelV6 FrozenLevel;
	TestTrue(TEXT("A uniquely new recruit killed in the same floor resolves"),
		UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
			Intent, PopulationPlan, FloorStart, Current, FrozenLevel, Error));
	TestEqual(TEXT("Revival keeps the frozen logical level"), FrozenLevel.LogicalLevel, 7);
	TestEqual(TEXT("Revival keeps the frozen physical ACF level"), FrozenLevel.PhysicalACFLevel, 7);
	TestEqual(TEXT("Revival keeps the decision tier"), FrozenLevel.Grade, EEFCalystoRarityTierV6::Rare);

	FProjectCompanionRunEntrySnapshot ConfirmedDead = Current;
	ConfirmedDead.State = EProjectCompanionRunState::Dead;
	TestFalse(TEXT("A confirmed-dead record cannot use the same-floor-only route"),
		UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
			Intent, PopulationPlan, FloorStart, ConfirmedDead, FrozenLevel, Error));

	FProjectCompanionRunSnapshot ExistingAtStart = FloorStart;
	ExistingAtStart.Entries.Add(Current);
	ExistingAtStart.RefreshHash();
	TestFalse(TEXT("A companion already present at floor start is rejected"),
		UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
			Intent, PopulationPlan, ExistingAtStart, Current, FrozenLevel, Error));

	FEFCalystoPopulationPlanV6 DuplicatePlan = PopulationPlan;
	DuplicatePlan.Rooms[0].Decisions.Add(Decision);
	TestFalse(TEXT("Duplicated source directives fail closed"),
		UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
			Intent, DuplicatePlan, FloorStart, Current, FrozenLevel, Error));

	FProjectCompanionRunEntrySnapshot Drifted = Current;
	Drifted.Definition.ResolvedLevel += 1;
	TestFalse(TEXT("Definition drift from the source directive fails closed"),
		UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
			Intent, PopulationPlan, FloorStart, Drifted, FrozenLevel, Error));

	FEFCalystoPopulationDecisionV6 RerollDecision = Decision;
	RerollDecision.DecisionId = TEXT("33333333333333333333333333333333AAA2");
	FProjectCompanionDefinition RerollDefinition;
	TestTrue(TEXT("The rerolled directive remains structurally valid"),
		FProjectCalystoPopulationBridge::BuildRandomNPCDefinition(
			PopulationPlan, RerollDecision, RerollDefinition, Error));
	TestNotEqual(TEXT("Reroll derives a different stable companion identity"),
		RerollDefinition.StableCompanionId, Current.Definition.StableCompanionId);
	return true;
}

#endif
