#include "DungeonCurse/DungeonCurseManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DungeonCurse/DungeonCurseComponent.h"
#include "DungeonCurse/DungeonCurseTypes.h"
#include "Misc/AutomationTest.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonCurseNeedMultiplierTest,
	"NoShellForWinter.DungeonCurse.Needs.ExternalDecayMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonCurseNeedMultiplierTest::RunTest(const FString& Parameters)
{
	UProjectSurvivalNeedsComponent* NeedsComponent = NewObject<UProjectSurvivalNeedsComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("Needs component should be created"), NeedsComponent))
	{
		return false;
	}

	NeedsComponent->Needs = {
		FProjectSurvivalNeedState(TEXT("Hunger"), 100.0f, 100.0f, 10.0f, 0.0f),
		FProjectSurvivalNeedState(TEXT("Thirst"), 100.0f, 100.0f, 20.0f, 0.0f)
	};

	TestEqual(TEXT("Default external multiplier should be neutral"), NeedsComponent->GetExternalNeedDecayMultiplier(TEXT("Hunger")), 1.0f);
	TestTrue(TEXT("First hunger multiplier should apply"), NeedsComponent->SetExternalNeedDecayMultiplier(TEXT("CurseA"), TEXT("Hunger"), 1.2f));
	TestTrue(TEXT("Second hunger multiplier should apply"), NeedsComponent->SetExternalNeedDecayMultiplier(TEXT("CurseB"), TEXT("Hunger"), 1.5f));
	TestEqual(TEXT("External multipliers should stack multiplicatively"), NeedsComponent->GetExternalNeedDecayMultiplier(TEXT("Hunger")), 1.8f);
	TestEqual(TEXT("Base hunger decay should not be modified"), NeedsComponent->Needs[0].DecayPerSecond, 10.0f);

	TestTrue(TEXT("Clearing one source should succeed"), NeedsComponent->ClearExternalNeedDecayMultiplier(TEXT("CurseA"), TEXT("Hunger")));
	TestEqual(TEXT("Remaining multiplier should stay active"), NeedsComponent->GetExternalNeedDecayMultiplier(TEXT("Hunger")), 1.5f);
	TestTrue(TEXT("Clearing final source should succeed"), NeedsComponent->ClearExternalNeedDecayMultiplier(TEXT("CurseB"), TEXT("Hunger")));
	TestEqual(TEXT("Clearing all sources should restore neutral multiplier"), NeedsComponent->GetExternalNeedDecayMultiplier(TEXT("Hunger")), 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonCurseDefaultDefinitionsTest,
	"NoShellForWinter.DungeonCurse.Definitions.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonCurseDefaultDefinitionsTest::RunTest(const FString& Parameters)
{
	const ADungeonCurseManager* ManagerDefaults = GetDefault<ADungeonCurseManager>();
	if (!TestNotNull(TEXT("Dungeon curse manager defaults should exist"), ManagerDefaults))
	{
		return false;
	}

	TestFalse(TEXT("Sealed combat rooms should be disabled by default"), ManagerDefaults->bEnableSealedCombatRooms);

	bool bFoundLightReduction = false;
	bool bFoundMadness = false;
	bool bFoundCurseBuildup = false;
	bool bFoundHunger = false;
	bool bFoundThirst = false;
	bool bFoundMaxEnemies = false;
	bool bFoundWhisper = false;
	bool bFoundSealedPhase2Disabled = false;

	for (const FRoomCurseDefinition& Definition : ManagerDefaults->CurseDefinitions)
	{
		if (Definition.CurseType == ERoomCurseType::LightReduction && Definition.Weight > 0.0f && FMath::IsNearlyEqual(Definition.LightMultiplier, 0.5f))
		{
			bFoundLightReduction = true;
		}
		else if (Definition.CurseType == ERoomCurseType::MadnessPerSecond && Definition.Weight > 0.0f && FMath::IsNearlyEqual(Definition.MadnessPerSecond, 0.1f))
		{
			bFoundMadness = true;
		}
		else if (
			Definition.CurseType == ERoomCurseType::CurseBuildupPerSecond
			&& Definition.Weight > 0.0f
			&& FMath::IsNearlyEqual(Definition.CurseBuildupPerSecond, 1.0f)
			&& Definition.CurseSourceKind == EProjectCurseSourceKind::Room)
		{
			bFoundCurseBuildup = true;
		}
		else if (Definition.CurseType == ERoomCurseType::HungerDrainMultiplier && Definition.Weight > 0.0f && FMath::IsNearlyEqual(Definition.HungerDrainMultiplier, 1.2f))
		{
			bFoundHunger = true;
		}
		else if (Definition.CurseType == ERoomCurseType::ThirstDrainMultiplier && Definition.Weight > 0.0f && FMath::IsNearlyEqual(Definition.ThirstDrainMultiplier, 1.2f))
		{
			bFoundThirst = true;
		}
		else if (Definition.CurseType == ERoomCurseType::MaxLevelEnemies && Definition.Weight > 0.0f && Definition.bForceMaxLevelEnemies)
		{
			bFoundMaxEnemies = true;
		}
		else if (Definition.CurseType == ERoomCurseType::TynaWhisper && Definition.Weight > 0.0f)
		{
			bFoundWhisper = true;
		}
		else if (Definition.CurseType == ERoomCurseType::SealedCombatRoom_Phase2 && Definition.Weight <= 0.0f)
		{
			bFoundSealedPhase2Disabled = true;
		}
	}

	TestTrue(TEXT("LightReduction should be a Phase 1 default"), bFoundLightReduction);
	TestTrue(TEXT("MadnessPerSecond should be a Phase 1 default"), bFoundMadness);
	TestTrue(TEXT("CurseBuildupPerSecond should be a Phase 1 default"), bFoundCurseBuildup);
	TestTrue(TEXT("HungerDrainMultiplier should be a Phase 1 default"), bFoundHunger);
	TestTrue(TEXT("ThirstDrainMultiplier should be a Phase 1 default"), bFoundThirst);
	TestTrue(TEXT("MaxLevelEnemies should be a Phase 1 default"), bFoundMaxEnemies);
	TestTrue(TEXT("TynaWhisper should be a Phase 1 default"), bFoundWhisper);
	TestTrue(TEXT("SealedCombatRoom_Phase2 should be reserved with zero weight"), bFoundSealedPhase2Disabled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonCurseContextTest,
	"NoShellForWinter.DungeonCurse.Curse.ApplicationContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonCurseContextTest::RunTest(const FString& Parameters)
{
	UProjectSurvivalNeedsComponent* TargetObject =
		NewObject<UProjectSurvivalNeedsComponent>(GetTransientPackage());
	const FGuid ActivationId(0x10203040, 0x50607080, 0x90A0B0C0, 0xD0E0F001);
	const FName SourceID(TEXT("AutomationCurseRoom"));

	const FProjectCurseApplicationContext First = UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
		TargetObject,
		nullptr,
		SourceID,
		ActivationId,
		3,
		2.5f,
		EProjectCurseSourceKind::Environment);
	const FProjectCurseApplicationContext Duplicate = UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
		TargetObject,
		nullptr,
		SourceID,
		ActivationId,
		3,
		2.5f,
		EProjectCurseSourceKind::Environment);
	const FProjectCurseApplicationContext NextPulse = UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
		TargetObject,
		nullptr,
		SourceID,
		ActivationId,
		4,
		2.5f,
		EProjectCurseSourceKind::Environment);
	const FProjectCurseApplicationContext NewActivation = UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
		TargetObject,
		nullptr,
		SourceID,
		FGuid(0x10203040, 0x50607080, 0x90A0B0C0, 0xD0E0F002),
		3,
		2.5f,
		EProjectCurseSourceKind::Environment);
	const FProjectCurseApplicationContext SanitizedRoom = UDungeonCurseComponent::BuildPeriodicCurseApplicationContext(
		TargetObject,
		nullptr,
		SourceID,
		ActivationId,
		5,
		-1.0f,
		EProjectCurseSourceKind::Debug);

	TestTrue(TEXT("Periodic application id should be valid"), First.ApplicationId.IsValid());
	TestEqual(TEXT("The same activation pulse should be idempotent"), Duplicate.ApplicationId, First.ApplicationId);
	TestNotEqual(TEXT("Consecutive pulses should use distinct ids"), NextPulse.ApplicationId, First.ApplicationId);
	TestNotEqual(TEXT("Re-entering the same room should start a distinct activation"), NewActivation.ApplicationId, First.ApplicationId);
	TestEqual(TEXT("Configured environmental rooms should retain their source kind"), First.SourceKind, EProjectCurseSourceKind::Environment);
	TestEqual(TEXT("Unsupported room source kinds should fall back to Room"), SanitizedRoom.SourceKind, EProjectCurseSourceKind::Room);
	TestEqual(TEXT("Negative buildup should clamp to zero"), SanitizedRoom.Amount, 0.0f);
	TestTrue(TEXT("Room buildup should remain resistible"), First.bResistible);
	TestTrue(TEXT("Room buildup should be able to trigger Cursed"), First.bCanTriggerCursed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDungeonCurseDeterministicSelectionTest,
	"NoShellForWinter.DungeonCurse.Selection.DeterministicSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDungeonCurseDeterministicSelectionTest::RunTest(const FString& Parameters)
{
	const ADungeonCurseManager* ManagerDefaults = GetDefault<ADungeonCurseManager>();
	if (!TestNotNull(TEXT("Dungeon curse manager defaults should exist"), ManagerDefaults))
	{
		return false;
	}

	FDetectedDungeonCurseRoom Room;
	Room.RoomID = TEXT("AutomationRoom");
	Room.RoomType = EGeneratedRoomType::Combat;
	Room.bAllowCurses = true;
	Room.bAllowEnemyLevelCurse = true;
	Room.bAllowLightCurse = true;
	Room.bAllowInnerStateCurse = true;

	FRandomStream FirstStream(4242);
	FRandomStream SecondStream(4242);
	TSet<FName> FirstUsed;
	TSet<FName> SecondUsed;

	TArray<FName> FirstSequence;
	TArray<FName> SecondSequence;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FRoomCurseDefinition FirstCurse;
		FRoomCurseDefinition SecondCurse;
		const bool bFirstSelected = ADungeonCurseManager::SelectWeightedCurseDefinition(
			ManagerDefaults->CurseDefinitions,
			Room,
			true,
			false,
			FirstStream,
			FirstUsed,
			FirstCurse);
		const bool bSecondSelected = ADungeonCurseManager::SelectWeightedCurseDefinition(
			ManagerDefaults->CurseDefinitions,
			Room,
			true,
			false,
			SecondStream,
			SecondUsed,
			SecondCurse);

		TestEqual(TEXT("Both deterministic streams should agree on selection availability"), bFirstSelected, bSecondSelected);
		FirstSequence.Add(FirstCurse.CurseID);
		SecondSequence.Add(SecondCurse.CurseID);
	}

	TestEqual(TEXT("Deterministic selection should produce equal sequence length"), FirstSequence.Num(), SecondSequence.Num());
	for (int32 Index = 0; Index < FirstSequence.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Selection %d should match"), Index), FirstSequence[Index], SecondSequence[Index]);
		TestNotEqual(TEXT("Phase 2 sealed room should not be selected while disabled"), FirstSequence[Index], FName(TEXT("SealedCombatRoom_Phase2")));
	}

	return true;
}

#endif
