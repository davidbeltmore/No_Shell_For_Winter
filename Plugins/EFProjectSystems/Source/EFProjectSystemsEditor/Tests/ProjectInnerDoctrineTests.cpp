#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineSettings.h"
#include "InnerDoctrine/ProjectInnerDoctrineTravelStateSubsystem.h"

#include "AbilitySystemComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "Curse/ProjectCurseSourceComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Survival/ProjectSurvivalNeedsComponent.h"
#include "Survival/ProjectSurvivalStatusComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
	struct FDoctrineTestActor
	{
		AActor* Owner = nullptr;
		UProjectSurvivalNeedsComponent* Needs = nullptr;
		UProjectSurvivalStatusComponent* Status = nullptr;
		UProjectCombatAttributeComponent* Combat = nullptr;
		UProjectInnerDoctrineComponent* Doctrine = nullptr;

		static FDoctrineTestActor Create()
		{
			FDoctrineTestActor Result;
			Result.Owner = NewObject<AActor>();
			Result.Needs = NewObject<UProjectSurvivalNeedsComponent>(Result.Owner);
			Result.Status = NewObject<UProjectSurvivalStatusComponent>(Result.Owner);
			Result.Combat = NewObject<UProjectCombatAttributeComponent>(Result.Owner);
			Result.Doctrine = NewObject<UProjectInnerDoctrineComponent>(Result.Owner);
			Result.Owner->AddInstanceComponent(Result.Needs);
			Result.Owner->AddInstanceComponent(Result.Status);
			Result.Owner->AddInstanceComponent(Result.Combat);
			Result.Owner->AddInstanceComponent(Result.Doctrine);
			return Result;
		}
	};

	FProjectCurseApplicationContext MakeCurse(
		const float Amount,
		const EProjectCurseSourceKind Source,
		const TCHAR* StableId,
		const bool bResistible = true,
		const bool bCanTrigger = true)
	{
		FProjectCurseApplicationContext Context;
		Context.Amount = Amount;
		Context.SourceKind = Source;
		const uint32 HashA = FCrc::StrCrc32(StableId);
		const uint32 HashB = FCrc::StrCrc32(StableId, 0x9E3779B9u);
		Context.ApplicationId = FGuid(
			HashA,
			HashB,
			HashA ^ 0xA5A5A5A5u,
			HashB ^ 0x5A5A5A5Au);
		Context.bResistible = bResistible;
		Context.bCanTriggerCursed = bCanTrigger;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDoctrineUpgradeAndSourceTest,
	"NoShellForWinter.InnerDoctrine.Progression.UpgradeAndSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDoctrineUpgradeAndSourceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UProjectInnerDoctrineComponent* Component = NewObject<UProjectInnerDoctrineComponent>();
	TestNotNull(TEXT("The canonical component is constructible."), Component);
	if (!Component)
	{
		return false;
	}

	const int32 FirstCost = Component->GetUpgradeCost(EProjectDoctrineAttribute::Willpower);
	TestEqual(TEXT("The first Doctrine upgrade costs 80 DXP."), FirstCost, 80);
	TestFalse(
		TEXT("An upgrade cannot be purchased without DXP."),
		Component->SpendDxpOnAttribute(EProjectDoctrineAttribute::Willpower));
	TestEqual(
		TEXT("Mature presentation never grants DXP."),
		Component->GrantDxp(
			TEXT("Presentation.Optional"),
			1000,
			EProjectDoctrineExperienceSource::MaturePresentation),
		0);
	TestEqual(
		TEXT("A combat grant is stored without a presentation bonus."),
		Component->GrantDxp(TEXT("Combat.Test"), FirstCost, EProjectDoctrineExperienceSource::Combat),
		FirstCost);
	TestTrue(
		TEXT("The paid upgrade succeeds."),
		Component->SpendDxpOnAttribute(EProjectDoctrineAttribute::Willpower));
	TestEqual(
		TEXT("Willpower reaches level one."),
		Component->GetAttributeLevel(EProjectDoctrineAttribute::Willpower),
		1);
	TestEqual(TEXT("The exact cost is consumed."), Component->GetCurrentRunDxp(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDoctrineAttributeMilestoneGridTest,
	"NoShellForWinter.InnerDoctrine.Progression.LevelGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDoctrineAttributeMilestoneGridTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 AttributeIndex = 0;
		AttributeIndex < static_cast<int32>(EProjectDoctrineAttribute::Count);
		++AttributeIndex)
	{
		const EProjectDoctrineAttribute Attribute =
			static_cast<EProjectDoctrineAttribute>(AttributeIndex);
		for (const int32 Level : { 0, 4, 5, 9, 10 })
		{
			UProjectInnerDoctrineComponent* Component = NewObject<UProjectInnerDoctrineComponent>();
			if (Level > 0)
			{
				TestTrue(
					FString::Printf(TEXT("Attribute %d can be raised to %d."), AttributeIndex, Level),
					Component->ApplyFreeAttributeLevels(Attribute, Level));
			}
			const FProjectInnerDoctrineSnapshot Snapshot = Component->BuildSnapshot();
			const FProjectDoctrineAttributeState* State =
				Snapshot.Attributes.FindByPredicate(
					[Attribute](const FProjectDoctrineAttributeState& Candidate)
					{
						return Candidate.Attribute == Attribute;
					});
			TestNotNull(TEXT("Every canonical attribute is represented in the snapshot."), State);
			if (!State)
			{
				continue;
			}
			TestEqual(TEXT("The snapshot preserves the exact level."), State->Level, Level);
			TestEqual(TEXT("Milestone V uses a level-five boundary."), State->bMilestone5Unlocked, Level >= 5);
			TestEqual(TEXT("Milestone X uses a level-ten boundary."), State->bMilestone10Unlocked, Level >= 10);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDoctrineMilestoneCatalogTest,
	"NoShellForWinter.InnerDoctrine.Progression.MilestoneCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDoctrineMilestoneCatalogTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();
	TestNotNull(TEXT("Canonical settings are available."), Settings);
	if (!Settings)
	{
		return false;
	}

	TestEqual(TEXT("Only the currently active milestone definitions are exposed."), Settings->MilestoneDefinitions.Num(), 7);
	TSet<FName> AbilityIds;
	for (const FProjectDoctrineMilestoneDefinition& Definition : Settings->MilestoneDefinitions)
	{
		TestFalse(TEXT("Every milestone has a stable ability id."), Definition.AbilityId.IsNone());
		TestTrue(
			TEXT("Every milestone level is V or X."),
			Definition.RequiredLevel == 5 || Definition.RequiredLevel == 10);
		AbilityIds.Add(Definition.AbilityId);
	}
	TestEqual(TEXT("All exposed milestone ids are unique."), AbilityIds.Num(), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDoctrineWillpowerMaxTest,
	"NoShellForWinter.InnerDoctrine.Willpower.DynamicMaximums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDoctrineWillpowerMaxTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FProjectInnerDoctrineDynamicMaxRule Rule;
	Rule.EntryName = TEXT("Hunger");
	Rule.bIsSensation = false;
	Rule.Attribute = EProjectDoctrineAttribute::Willpower;
	Rule.PercentBonusPerLevel = 0.015f;

	for (const int32 Level : { 0, 4, 5, 9, 10 })
	{
		float Flat = 0.f;
		float Multiplier = 1.f;
		UProjectInnerDoctrineSettings::AccumulateDynamicMaxRule(
			Rule,
			Level,
			Flat,
			Multiplier);
		TestTrue(
			FString::Printf(TEXT("Willpower level %d uses +1.5 percent per level."), Level),
			FMath::IsNearlyEqual(
				UProjectInnerDoctrineSettings::ComputeDynamicMaxValue(100.f, Flat, Multiplier),
				100.f * (1.f + 0.015f * static_cast<float>(Level)),
				0.001f));
	}

	TestFalse(
		TEXT("Curse has no dynamic maximum rule."),
		UProjectInnerDoctrineSettings::Get()->DynamicMaximumRules.ContainsByPredicate(
			[](const FProjectInnerDoctrineDynamicMaxRule& Candidate)
			{
				return Candidate.EntryName == TEXT("Curse");
			}));
	TestTrue(
		TEXT("Curse has a non-configurable maximum of 100."),
		FMath::IsNearlyEqual(
			NewObject<UProjectInnerDoctrineComponent>()->GetCurseMax(),
			ProjectCurse::Maximum));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCurseResistanceCompositionTest,
	"NoShellForWinter.InnerDoctrine.Curse.ResistanceComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCurseResistanceCompositionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UProjectInnerDoctrineSettings* Settings = UProjectInnerDoctrineSettings::Get();

	FProjectCurseApplicationContext Magic;
	Magic.Amount = 100.f;
	Magic.SourceKind = EProjectCurseSourceKind::Magic;
	Magic.bResistible = true;
	TestTrue(
		TEXT("Willpower, Faith and Rallying Presence compose multiplicatively."),
		FMath::IsNearlyEqual(
			UProjectInnerDoctrineComponent::ComputeCurseResistanceMultiplier(
				Magic,
				10,
				10,
				10,
				true,
				Settings),
			0.90f * 0.90f * 0.85f,
			0.0001f));

	FProjectCurseApplicationContext Trap = Magic;
	Trap.SourceKind = EProjectCurseSourceKind::Trap;
	TestTrue(
		TEXT("Trap resistance uses Willpower, Cunning and Rallying Presence."),
		FMath::IsNearlyEqual(
			UProjectInnerDoctrineComponent::ComputeCurseResistanceMultiplier(
				Trap,
				10,
				10,
				10,
				true,
				Settings),
			0.90f * 0.90f * 0.85f,
			0.0001f));

	UProjectInnerDoctrineSettings* FloorSettings = NewObject<UProjectInnerDoctrineSettings>();
	FloorSettings->WillpowerCurseResistancePerLevel = 0.20f;
	FloorSettings->WillpowerCurseResistanceCap = 1.f;
	FloorSettings->FaithCurseResistancePerLevel = 0.20f;
	FloorSettings->FaithCurseResistanceCap = 1.f;
	FloorSettings->CurseMinimumResistibleMultiplier = 0.25f;
	TestTrue(
		TEXT("Resistible Curse never falls below 25 percent of the original amount."),
		FMath::IsNearlyEqual(
			UProjectInnerDoctrineComponent::ComputeCurseResistanceMultiplier(
				Magic,
				10,
				10,
				0,
				true,
				FloorSettings),
			0.25f,
			0.0001f));

	Magic.bResistible = false;
	TestTrue(
		TEXT("A registered non-resistible context remains unmodified."),
		FMath::IsNearlyEqual(
			UProjectInnerDoctrineComponent::ComputeCurseResistanceMultiplier(
				Magic,
				10,
				10,
				10,
				true,
				Settings),
			1.f,
			0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCurseBoundariesAndIdempotenceTest,
	"NoShellForWinter.InnerDoctrine.Curse.BoundariesAndIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCurseBoundariesAndIdempotenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FDoctrineTestActor Fixture = FDoctrineTestActor::Create();
	TestNotNull(TEXT("The Doctrine fixture is valid."), Fixture.Doctrine);
	if (!Fixture.Doctrine)
	{
		return false;
	}

	for (int32 SourceIndex = static_cast<int32>(EProjectCurseSourceKind::Room);
		SourceIndex <= static_cast<int32>(EProjectCurseSourceKind::Debug);
		++SourceIndex)
	{
		FProjectCurseApplicationContext InvalidId;
		InvalidId.Amount = 10.f;
		InvalidId.SourceKind =
			static_cast<EProjectCurseSourceKind>(SourceIndex);
		InvalidId.bResistible = false;
		const FProjectCurseApplicationResult InvalidIdResult =
			Fixture.Doctrine->ApplyCurse(InvalidId);
		TestTrue(
			FString::Printf(
				TEXT("Curse source %d requires a valid application id."),
				SourceIndex),
			InvalidIdResult.bRejectedInvalidApplicationId);
	}
	TestTrue(
		TEXT("Invalid application ids, including Debug, cannot change Curse."),
		FMath::IsNearlyZero(Fixture.Doctrine->GetCurse()));

	const FProjectCurseApplicationContext First =
		MakeCurse(49.99f, EProjectCurseSourceKind::Debug, TEXT("Curse.49_99"), false);
	const FProjectCurseApplicationResult FirstResult = Fixture.Doctrine->ApplyCurse(First);
	TestTrue(TEXT("49.99 is retained exactly."), FMath::IsNearlyEqual(FirstResult.NewCurse, 49.99f, 0.001f));
	TestFalse(TEXT("49.99 does not trigger Cursed."), FirstResult.bTriggeredCursed);

	const FProjectCurseApplicationResult Duplicate = Fixture.Doctrine->ApplyCurse(First);
	TestTrue(TEXT("A repeated application id is rejected."), Duplicate.bDuplicate);
	TestTrue(TEXT("A duplicate cannot alter Curse."), FMath::IsNearlyEqual(Duplicate.NewCurse, 49.99f, 0.001f));

	Fixture.Doctrine->ApplyCurse(
		MakeCurse(0.01f, EProjectCurseSourceKind::Debug, TEXT("Curse.50"), false));
	TestTrue(TEXT("The warning boundary reaches 50."), FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 50.f));
	Fixture.Doctrine->ApplyCurse(
		MakeCurse(25.f, EProjectCurseSourceKind::Debug, TEXT("Curse.75"), false));
	TestTrue(TEXT("The critical boundary reaches 75."), FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 75.f));
	Fixture.Doctrine->ApplyCurse(
		MakeCurse(24.99f, EProjectCurseSourceKind::Debug, TEXT("Curse.99_99"), false));
	TestTrue(TEXT("99.99 remains below the trigger."), FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 99.99f, 0.001f));
	TestFalse(TEXT("99.99 is not Cursed."), Fixture.Doctrine->IsCursed());

	const FProjectCurseApplicationResult Trigger = Fixture.Doctrine->ApplyCurse(
		MakeCurse(0.01f, EProjectCurseSourceKind::Debug, TEXT("Curse.100"), false));
	TestTrue(TEXT("100 triggers exactly one Cursed episode."), Trigger.bTriggeredCursed);
	TestTrue(TEXT("The episode is active."), Fixture.Doctrine->IsCursed());

	const FProjectCurseApplicationResult DuringEpisode = Fixture.Doctrine->ApplyCurse(
		MakeCurse(10.f, EProjectCurseSourceKind::Debug, TEXT("Curse.NoRefresh"), false));
	TestFalse(TEXT("An active episode cannot create a parallel episode."), DuringEpisode.bTriggeredCursed);
	Fixture.Doctrine->AutomationCompleteCursedEpisode();
	TestFalse(TEXT("The completed episode is inactive."), Fixture.Doctrine->IsCursed());
	TestTrue(TEXT("Normal completion leaves 60 Curse."), FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 60.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCurseTravelPersistenceTest,
	"NoShellForWinter.InnerDoctrine.Curse.TravelPersistenceAndDefeatClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCurseTravelPersistenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UProjectInnerDoctrineTravelStateSubsystem* TravelState =
		NewObject<UProjectInnerDoctrineTravelStateSubsystem>(GameInstance);
	TestNotNull(TEXT("The in-memory Curse travel state is constructible."), TravelState);
	if (!TravelState)
	{
		return false;
	}

	TravelState->StoreCurseState(87.5f, true, 4.25f);
	FProjectCurseTravelState Restored;
	TestTrue(
		TEXT("A normal map transition exposes the stored state."),
		TravelState->TryGetCurseState(Restored));
	TestTrue(
		TEXT("Normal map travel preserves Curse without applying the defeat clamp."),
		FMath::IsNearlyEqual(Restored.Curse, 87.5f));
	TestTrue(TEXT("An active Cursed episode remains active across travel."), Restored.bCursedEpisodeActive);
	TestTrue(
		TEXT("The remaining Cursed duration is preserved."),
		FMath::IsNearlyEqual(Restored.CursedEpisodeRemainingSeconds, 4.25f));

	TravelState->StoreCurseState(140.f, false, 0.f);
	TestTrue(
		TEXT("Travel state also enforces the fixed maximum of 100."),
		TravelState->TryGetCurseState(Restored)
			&& FMath::IsNearlyEqual(Restored.Curse, ProjectCurse::Maximum));
	const uint64 PreviousLifecycleGeneration =
		TravelState->GetLifecycleGeneration();
	TravelState->NotifyNewGameStarted();
	TestFalse(
		TEXT("The explicit new-game bridge clears in-memory travel state."),
		TravelState->TryGetCurseState(Restored));
	TestTrue(
		TEXT("New game advances the lifecycle generation so an old pawn cannot restore stale state."),
		TravelState->GetLifecycleGeneration() > PreviousLifecycleGeneration);

	FDoctrineTestActor Fixture = FDoctrineTestActor::Create();
	const FProjectCurseApplicationResult DefeatFixtureCurse =
		Fixture.Doctrine->ApplyCurse(
			MakeCurse(
				87.5f,
				EProjectCurseSourceKind::Debug,
				TEXT("Travel.DefeatClamp"),
				false));
	TestTrue(
		TEXT("The defeat fixture receives its pre-defeat Curse through the canonical API."),
		FMath::IsNearlyEqual(DefeatFixtureCurse.AppliedAmount, 87.5f));
	Fixture.Doctrine->ClampCurseAfterDefeat_Implementation(60.f);
	TestTrue(
		TEXT("Only the explicit defeat path clamps Curse to 60."),
		FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 60.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCurseZonePresenceTest,
	"NoShellForWinter.InnerDoctrine.Curse.OverlappingZonePresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCurseZonePresenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UProjectInnerDoctrineComponent* Component =
		NewObject<UProjectInnerDoctrineComponent>();
	const FGuid FirstToken = FGuid::NewGuid();
	const FGuid SecondToken = FGuid::NewGuid();

	TestFalse(
		TEXT("An invalid zone token is rejected."),
		Component->RegisterCurseZonePresence(FGuid()));
	TestTrue(
		TEXT("The first zone presence is registered."),
		Component->RegisterCurseZonePresence(FirstToken));
	TestFalse(
		TEXT("The same overlap token is idempotent."),
		Component->RegisterCurseZonePresence(FirstToken));
	TestTrue(
		TEXT("A second overlapping zone has its own presence."),
		Component->RegisterCurseZonePresence(SecondToken));
	TestEqual(
		TEXT("Both overlapping zones remain represented."),
		Component->GetActiveCurseZoneCount(),
		2);
	TestTrue(
		TEXT("Leaving one zone removes only its own token."),
		Component->UnregisterCurseZonePresence(FirstToken));
	TestEqual(
		TEXT("The remaining overlap still blocks out-of-zone behavior."),
		Component->GetActiveCurseZoneCount(),
		1);
	TestFalse(
		TEXT("A repeated exit cannot underflow the count."),
		Component->UnregisterCurseZonePresence(FirstToken));
	TestTrue(
		TEXT("The last zone can leave cleanly."),
		Component->UnregisterCurseZonePresence(SecondToken));
	TestEqual(
		TEXT("No zone presence remains."),
		Component->GetActiveCurseZoneCount(),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectOffensiveCanonicalCurseSourceTest,
	"NoShellForWinter.InnerDoctrine.Offensive.CanonicalCurseSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectOffensiveCanonicalCurseSourceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AActor* TextTaggedActor = NewObject<AActor>();
	TextTaggedActor->Tags.Add(TEXT("Project.Curse.Source"));
	TestFalse(
		TEXT("A textual Actor Tag is not a canonical Curse source."),
		UProjectInnerDoctrineComponent::IsCanonicalCurseSourceActor(TextTaggedActor));

	AActor* ComponentActor = NewObject<AActor>();
	UProjectCurseSourceComponent* SourceComponent =
		NewObject<UProjectCurseSourceComponent>(ComponentActor);
	ComponentActor->AddInstanceComponent(SourceComponent);
	TestTrue(
		TEXT("The project-owned marker component identifies a Curse source."),
		UProjectInnerDoctrineComponent::IsCanonicalCurseSourceActor(ComponentActor));
	SourceComponent->SetCurseSourceEnabled(false);
	TestFalse(
		TEXT("A disabled marker component is not a Curse source."),
		UProjectInnerDoctrineComponent::IsCanonicalCurseSourceActor(ComponentActor));

	AActor* AbilityActor = NewObject<AActor>();
	UAbilitySystemComponent* AbilitySystem =
		NewObject<UAbilitySystemComponent>(AbilityActor);
	AbilityActor->AddInstanceComponent(AbilitySystem);
	AbilitySystem->AddLooseGameplayTag(ProjectCurseGameplayTags::Source);
	TestTrue(
		TEXT("The canonical Ability System Gameplay Tag identifies a Curse source."),
		UProjectInnerDoctrineComponent::IsCanonicalCurseSourceActor(AbilityActor));

	UProjectInnerDoctrineComponent* Doctrine =
		NewObject<UProjectInnerDoctrineComponent>();
	Doctrine->ApplyFreeAttributeLevels(EProjectDoctrineAttribute::Offensive, 5);
	FProjectCombatDamageSpec TaggedSpec;
	TaggedSpec.BaseDamage = 100.f;
	Doctrine->ModifyOutgoingDamageSpec(AbilityActor, TaggedSpec);
	TestTrue(
		TEXT("The temporarily blank Offensive milestone does not alter base damage."),
		FMath::IsNearlyEqual(TaggedSpec.BaseDamage, 100.f));

	FProjectCombatDamageSpec TextTagOnlySpec;
	TextTagOnlySpec.BaseDamage = 100.f;
	Doctrine->ModifyOutgoingDamageSpec(TextTaggedActor, TextTagOnlySpec);
	TestTrue(
		TEXT("A textual tag likewise receives no milestone multiplier."),
		FMath::IsNearlyEqual(TextTagOnlySpec.BaseDamage, 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectDoctrineLifecycleBridgeTest,
	"NoShellForWinter.InnerDoctrine.Hooks.FloorSleepAndServices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectDoctrineLifecycleBridgeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FDoctrineTestActor CunningFixture = FDoctrineTestActor::Create();
	CunningFixture.Doctrine->ApplyFreeAttributeLevels(
		EProjectDoctrineAttribute::Cunning,
		10);
	const FProjectCurseApplicationResult FirstTrap =
		CunningFixture.Doctrine->ApplyCurse(
			MakeCurse(10.f, EProjectCurseSourceKind::Trap, TEXT("Trap.Floor1.First")));
	TestTrue(
		TEXT("The temporarily blank Cunning Curse hook does not ignore the first Trap application."),
		FirstTrap.AppliedAmount > 0.f);
	const FProjectCurseApplicationResult SecondTrap =
		CunningFixture.Doctrine->ApplyCurse(
			MakeCurse(10.f, EProjectCurseSourceKind::Trap, TEXT("Trap.Floor1.Second")));
	TestTrue(
		TEXT("A later Trap application on the same floor is applied."),
		SecondTrap.AppliedAmount > 0.f);
	CunningFixture.Doctrine->NotifyDungeonFloorCompleted(TEXT("Floor.Transition.2"));
	const FProjectCurseApplicationResult FirstTrapOnNextFloor =
		CunningFixture.Doctrine->ApplyCurse(
			MakeCurse(10.f, EProjectCurseSourceKind::Trap, TEXT("Trap.Floor2.First")));
	TestTrue(
		TEXT("A floor transition does not create temporary Trap protection."),
		FirstTrapOnNextFloor.AppliedAmount > 0.f);
	CunningFixture.Doctrine->NotifyDungeonFloorCompleted(TEXT("Floor.Transition.2"));
	const FProjectCurseApplicationResult RepeatedTransitionTrap =
		CunningFixture.Doctrine->ApplyCurse(
			MakeCurse(10.f, EProjectCurseSourceKind::Trap, TEXT("Trap.Floor2.Second")));
	TestTrue(
		TEXT("Replaying the same transition id also leaves Trap applications unchanged."),
		RepeatedTransitionTrap.AppliedAmount > 0.f);

	FDoctrineTestActor WillpowerFixture = FDoctrineTestActor::Create();
	WillpowerFixture.Doctrine->ApplyFreeAttributeLevels(
		EProjectDoctrineAttribute::Willpower,
		5);
	WillpowerFixture.Needs->SetNeedCurrentValue(TEXT("Hunger"), 0.f, true);
	WillpowerFixture.Doctrine->ApplyCurse(
		MakeCurse(
			40.f,
			EProjectCurseSourceKind::Debug,
			TEXT("Floor.SecondBreath.Curse"),
			false));
	TestTrue(
		TEXT("Second Breath runs for a unique floor transition."),
		WillpowerFixture.Doctrine->NotifyDungeonFloorCompleted(
			TEXT("Floor.Transition.SecondBreath")));
	TestTrue(
		TEXT("Second Breath restores twenty percent of Hunger."),
		FMath::IsNearlyEqual(
			WillpowerFixture.Needs->GetNeedCurrentValue(TEXT("Hunger")),
			WillpowerFixture.Needs->GetNeedMaxValue(TEXT("Hunger")) * 0.20f));
	TestTrue(
		TEXT("Second Breath no longer modifies Curse."),
		FMath::IsNearlyEqual(WillpowerFixture.Doctrine->GetCurse(), 40.f));
	WillpowerFixture.Needs->SetNeedCurrentValue(TEXT("Hunger"), 0.f, true);
	TestFalse(
		TEXT("Second Breath cannot replay for the same transition id."),
		WillpowerFixture.Doctrine->NotifyDungeonFloorCompleted(
			TEXT("Floor.Transition.SecondBreath")));
	TestTrue(
		TEXT("The replay leaves Hunger unchanged."),
		FMath::IsNearlyZero(
			WillpowerFixture.Needs->GetNeedCurrentValue(TEXT("Hunger"))));

	FDoctrineTestActor FaithFixture = FDoctrineTestActor::Create();
	FaithFixture.Doctrine->ApplyFreeAttributeLevels(
		EProjectDoctrineAttribute::Faith,
		10);
	FaithFixture.Needs->SetSensationCurrentValue(TEXT("Madness"), 100.f, true);
	FaithFixture.Doctrine->ApplyCurse(
		MakeCurse(100.f, EProjectCurseSourceKind::Debug, TEXT("Sleep.Cursed"), false));
	TestTrue(TEXT("The sleep fixture begins Cursed."), FaithFixture.Doctrine->IsCursed());
	TestTrue(
		TEXT("The real sleep-completion bridge triggers Sanctified Rest."),
		FaithFixture.Doctrine->NotifySleepCompleted(TEXT("Test.Sleep.Completed")));
	TestTrue(TEXT("The neutral Sanctified Rest leaves Cursed unchanged."), FaithFixture.Doctrine->IsCursed());
	TestTrue(
		TEXT("Sanctified Rest removes half of maximum Madness."),
		FMath::IsNearlyEqual(
			FaithFixture.Needs->GetSensationCurrentValue(TEXT("Madness")),
			50.f));
	TestTrue(
		TEXT("Sanctified Rest no longer cleanses Curse."),
		FMath::IsNearlyEqual(FaithFixture.Doctrine->GetCurse(), 100.f));

	FDoctrineTestActor CharismaFixture = FDoctrineTestActor::Create();
	CharismaFixture.Doctrine->ApplyFreeAttributeLevels(
		EProjectDoctrineAttribute::Charisma,
		5);
	const FProjectCurseApplicationResult RapportFixtureCurse =
		CharismaFixture.Doctrine->ApplyCurse(
			MakeCurse(
				50.f,
				EProjectCurseSourceKind::Debug,
				TEXT("Service.Rapport.Curse"),
				false));
	TestTrue(
		TEXT("The Rapport fixture receives its Curse through the canonical API."),
		FMath::IsNearlyEqual(RapportFixtureCurse.AppliedAmount, 50.f));
	TestTrue(
		TEXT("The temporarily blank Charisma V slot gives no Curse-cleansing multiplier."),
		FMath::IsNearlyEqual(
			CharismaFixture.Doctrine->NotifyServiceOrShrineCleanse(
				20.f,
				TEXT("Shrine.Test")),
			20.f));
	TestTrue(
		TEXT("An unidentified service cannot invoke the Rapport bridge."),
		FMath::IsNearlyZero(
			CharismaFixture.Doctrine->NotifyServiceOrShrineCleanse(
				20.f,
				NAME_None)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCelerityRecoveredMomentumWindowTest,
	"NoShellForWinter.InnerDoctrine.Celerity.RecoveredMomentumWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCelerityRecoveredMomentumWindowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FDoctrineTestActor Fixture = FDoctrineTestActor::Create();
	Fixture.Doctrine->ApplyFreeAttributeLevels(EProjectDoctrineAttribute::Celerity, 10);
	Fixture.Doctrine->ApplyCurse(
		MakeCurse(100.f, EProjectCurseSourceKind::Debug, TEXT("Celerity.Cursed"), false));
	TestFalse(
		TEXT("Celerity X does not grant permanent Exhausted Recovery immunity before Cursed ends."),
		Fixture.Doctrine->AutomationIsRecoveredMomentumActive());
	Fixture.Doctrine->AutomationCompleteCursedEpisode();
	TestFalse(
		TEXT("Ending Cursed does not start a Celerity milestone window while the slot is blank."),
		Fixture.Doctrine->AutomationIsRecoveredMomentumActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectGuardRecoveryTest,
	"NoShellForWinter.InnerDoctrine.Defensive.GuardRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectGuardRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FDoctrineTestActor Fixture = FDoctrineTestActor::Create();
	AActor* Enemy = NewObject<AActor>();
	Enemy->Tags.Add(TEXT("Project.Enemy"));

	Fixture.Doctrine->ApplyFreeAttributeLevels(EProjectDoctrineAttribute::Defensive, 5);
	Fixture.Needs->SetSensationCurrentValue(
		TEXT("Pain"),
		Fixture.Needs->GetSensationMaxValue(TEXT("Pain")),
		true);
	Fixture.Doctrine->ApplyCurse(
		MakeCurse(100.f, EProjectCurseSourceKind::Debug, TEXT("Guard.Start"), false));

	float FlatNegated = 0.f;
	float GuardAbsorbed = 0.f;
	const float FirstOverflow = Fixture.Doctrine->ModifyIncomingHealthDamage(
		Enemy,
		TEXT("Physical"),
		30.f,
		FlatNegated,
		GuardAbsorbed);
	TestTrue(TEXT("Defensive applies after armor as a flat reduction."), FMath::IsNearlyEqual(FlatNegated, 20.f));
	TestFalse(TEXT("The temporarily blank Defensive V slot does not start Guard Recovery."), Fixture.Doctrine->IsGuardRecoveryActive());
	TestTrue(TEXT("The blank slot absorbs no damage."), FMath::IsNearlyZero(GuardAbsorbed));
	TestTrue(TEXT("Damage remaining after the normal flat reduction reaches health."), FMath::IsNearlyEqual(FirstOverflow, 10.f));
	TestTrue(TEXT("Pain is not rewritten by a disabled milestone."), FMath::IsNearlyEqual(Fixture.Needs->GetSensationCurrentValue(TEXT("Pain")), 100.f));
	TestTrue(TEXT("Curse is not rewritten by a disabled milestone."), FMath::IsNearlyEqual(Fixture.Doctrine->GetCurse(), 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCharismaDxpScopeTest,
	"NoShellForWinter.InnerDoctrine.Charisma.DxpScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCharismaDxpScopeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UProjectInnerDoctrineComponent* Component = NewObject<UProjectInnerDoctrineComponent>();
	Component->ApplyFreeAttributeLevels(EProjectDoctrineAttribute::Charisma, 10);

	TestEqual(
		TEXT("Charisma 10 gives ten percent extra Training DXP."),
		Component->GrantDxp(TEXT("Training"), 100, EProjectDoctrineExperienceSource::Training),
		110);
	TestEqual(
		TEXT("Charisma does not modify Combat DXP."),
		Component->GrantDxp(TEXT("Combat"), 100, EProjectDoctrineExperienceSource::Combat),
		100);
	TestEqual(
		TEXT("Optional mature presentation still gives zero at Charisma 10."),
		Component->GrantDxp(
			TEXT("Presentation"),
			100,
			EProjectDoctrineExperienceSource::MaturePresentation),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectCunningMathTest,
	"NoShellForWinter.InnerDoctrine.Cunning.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectCunningMathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(
		TEXT("Cunning level zero has no passive curve contribution."),
		FMath::IsNearlyEqual(UProjectInnerDoctrineSettings::ComputeCunningPassiveRatio(0, 10.f), 0.f));
	TestTrue(
		TEXT("Cunning level ten reaches half strength at a ten-point pivot."),
		FMath::IsNearlyEqual(UProjectInnerDoctrineSettings::ComputeCunningPassiveRatio(10, 10.f), 0.5f));
	TestEqual(
		TEXT("Steady Hands keeps five misses at level four."),
		UProjectInnerDoctrineSettings::ComputeCunningStruggleMaxMisses(4, 5, 5, 10),
		5);
	TestEqual(
		TEXT("Steady Hands allows ten misses at level five."),
		UProjectInnerDoctrineSettings::ComputeCunningStruggleMaxMisses(5, 5, 5, 10),
		10);
	return true;
}

#endif
