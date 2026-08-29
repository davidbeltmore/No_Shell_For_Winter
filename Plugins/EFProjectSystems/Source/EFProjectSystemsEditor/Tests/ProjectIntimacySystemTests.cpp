#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "GameFramework/Actor.h"
#include "Intimacy/ProjectIntimacyDialogueLibrary.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Intimacy/ProjectIntimacySettings.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Intimacy/ProjectIntimacyTypes.h"
#include "Intimacy/ProjectIntimacyZoneComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyProviderContractTest,
	"NoShellForWinter.Intimacy.ContentPolicy.ProviderContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyProviderContractTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("The Intimacy subsystem implements the optional mature-content provider boundary"),
		UProjectIntimacySubsystem::StaticClass()->ImplementsInterface(
			UProjectOptionalMatureContentProvider::StaticClass()));

	const UProjectIntimacySubsystem* DefaultSubsystem = GetDefault<UProjectIntimacySubsystem>();
	TestNotNull(TEXT("The Intimacy provider CDO is available"), DefaultSubsystem);
	if (DefaultSubsystem)
	{
		TestTrue(
			TEXT("The provider supports Intimacy Session"),
			DefaultSubsystem->SupportsMatureFeature(EProjectOptionalMatureFeature::IntimacySession));
		TestFalse(
			TEXT("The provider does not claim the private solo presentation"),
			DefaultSubsystem->SupportsMatureFeature(EProjectOptionalMatureFeature::PrivateSoloPresentation));
		TestFalse(
			TEXT("The provider does not claim mature defeat"),
			DefaultSubsystem->SupportsMatureFeature(EProjectOptionalMatureFeature::MatureDefeatVignette));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyClimaxMathTest,
	"NoShellForWinter.Intimacy.Climax.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyClimaxMathTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Climax clamps below zero"), FMath::IsNearlyZero(
		UProjectIntimacySettings::ClampClimax(-10.0f)));
	TestTrue(TEXT("Climax preserves values in range"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ClampClimax(55.0f),
		55.0f));
	TestTrue(TEXT("Climax clamps to 100"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ClampClimax(250.0f),
		100.0f));
	TestTrue(TEXT("HUD Climax normalizes against a configurable maximum"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::NormalizeClimaxPercent(40.0f, 80.0f),
		50.0f));
	TestTrue(TEXT("HUD Climax normalization caps at 100 percent"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::NormalizeClimaxPercent(120.0f, 80.0f),
		100.0f));

	TestTrue(TEXT("Please has no Climax gain without a successful hit"), FMath::IsNearlyZero(
		UProjectIntimacySettings::ComputePleaseClimaxGain(0)));
	TestTrue(TEXT("Please grants five Climax per successful hit"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ComputePleaseClimaxGain(3),
		15.0f));

	float RemainingClimax = 0.0f;
	TestEqual(
		TEXT("Crossing the Climax maximum emits one orgasm"),
		UProjectIntimacySettings::ConsumeClimax(90.0f, 20.0f, 100.0f, RemainingClimax),
		1);
	TestTrue(TEXT("An orgasm preserves residual Climax for the continuing session"), FMath::IsNearlyEqual(
		RemainingClimax,
		10.0f));

	TestEqual(
		TEXT("A large cosmetic gain can emit multiple orgasms without a terminal result"),
		UProjectIntimacySettings::ConsumeClimax(20.0f, 285.0f, 100.0f, RemainingClimax),
		3);
	TestTrue(TEXT("Multiple orgasms preserve residual Climax"), FMath::IsNearlyEqual(RemainingClimax, 5.0f));

	TestEqual(
		TEXT("Residual Climax can immediately continue into another orgasm cycle"),
		UProjectIntimacySettings::ConsumeClimax(RemainingClimax, 95.0f, 100.0f, RemainingClimax),
		1);
	TestTrue(TEXT("A completed follow-up cycle returns to zero Climax"), FMath::IsNearlyZero(RemainingClimax));

	TestTrue(
		TEXT("Climax anticipation remains neutral outside its window"),
		FMath::IsNearlyEqual(
			UProjectIntimacySettings::ComputeClimaxAnticipationMultiplier(94.0f, 100.0f),
			1.0f));
	TestTrue(
		TEXT("Climax anticipation increases inside its final window"),
		UProjectIntimacySettings::ComputeClimaxAnticipationMultiplier(98.0f, 100.0f) > 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacySessionLocalStateDefaultsTest,
	"NoShellForWinter.Intimacy.Climax.SessionLocalStateDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacySessionLocalStateDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectIntimacySessionSnapshot Snapshot;
	TestEqual(
		TEXT("A new snapshot starts by building Climax"),
		Snapshot.SessionState,
		EProjectIntimacySessionState::BuildingClimax);
	TestEqual(
		TEXT("Orgasm Rush remains a session-local state enum"),
		static_cast<uint8>(EProjectIntimacySessionState::OrgasmRush),
		static_cast<uint8>(1));
	TestTrue(TEXT("Player Climax starts at zero"), FMath::IsNearlyZero(Snapshot.PlayerClimax));
	TestTrue(TEXT("Partner Climax starts at zero"), FMath::IsNearlyZero(Snapshot.PartnerClimax));
	TestEqual(
		TEXT("An unset Orgasm Rush snapshot uses the partner as its neutral target"),
		Snapshot.OrgasmRushTarget,
		EProjectIntimacyClimaxTarget::Partner);
	TestFalse(TEXT("A new snapshot has no player Orgasm Rush"), Snapshot.bPlayerOrgasmRush);
	TestFalse(TEXT("A new snapshot has no partner Orgasm Rush"), Snapshot.bPartnerOrgasmRush);
	TestTrue(TEXT("Player Orgasm Rush time starts at zero"), FMath::IsNearlyZero(
		Snapshot.PlayerOrgasmRushRemaining));
	TestTrue(TEXT("Partner Orgasm Rush time starts at zero"), FMath::IsNearlyZero(
		Snapshot.PartnerOrgasmRushRemaining));
	TestEqual(TEXT("Player orgasm history starts at zero"), Snapshot.PlayerOrgasmCount, 0);
	TestEqual(TEXT("Partner orgasm history starts at zero"), Snapshot.PartnerOrgasmCount, 0);
	TestTrue(TEXT("The snapshot advertises one percent Curse reduction per second"), FMath::IsNearlyEqual(
		Snapshot.CurseReductionPercentPerSecond,
		1.0f));

	const UEnum* SessionStateEnum = StaticEnum<EProjectIntimacySessionState>();
	TestNotNull(TEXT("The session-local Orgasm Rush enum is reflected"), SessionStateEnum);
	if (SessionStateEnum)
	{
		TestEqual(
			TEXT("The Climax state machine has no terminal Satisfied state"),
			SessionStateEnum->GetValueByNameString(TEXT("Satisfied")),
			static_cast<int64>(INDEX_NONE));
		TestEqual(
			TEXT("The Climax state machine has no terminal Finished state"),
			SessionStateEnum->GetValueByNameString(TEXT("Finished")),
			static_cast<int64>(INDEX_NONE));
	}

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	TestNotNull(TEXT("Intimacy settings should be available"), Settings);
	if (Settings)
	{
		TestTrue(TEXT("Curse reduction defaults to one percent per second"), FMath::IsNearlyEqual(
			Settings->CurseReductionPercentPerSecond,
			1.0f));
		TestEqual(
			TEXT("Orgasm presentation uses the Climax media event"),
			Settings->OrgasmMediaEventId,
			FName(TEXT("Climax")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyEligibilityRulesTest,
	"NoShellForWinter.Intimacy.Eligibility.FailClosedMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyEligibilityRulesTest::RunTest(const FString& Parameters)
{
	FProjectIntimacyEligibilityContext Context;
	Context.bContentAllowed = true;
	Context.bCharismaMasteryUnlocked = true;
	Context.bPlayerAdultVerified = true;
	Context.bPartnerAdultVerified = true;
	Context.bExplicitConsent = true;
	Context.bPlayerAlive = true;
	Context.bPartnerAlive = true;
	Context.bPlayerConscious = true;
	Context.bPartnerConscious = true;
	Context.bPartnerNonHostile = true;
	Context.bOutsideCombat = true;
	Context.bZoneAllowed = true;

	TestEqual(
		TEXT("A fully valid adult consensual context is eligible"),
		UProjectIntimacySettings::EvaluateEligibility(Context),
		EProjectIntimacyEligibilityFailure::None);

	auto TestDenied = [this, &Context](
		const TCHAR* What,
		bool FProjectIntimacyEligibilityContext::* Flag,
		const EProjectIntimacyEligibilityFailure Expected)
	{
		Context.*Flag = false;
		TestEqual(What, UProjectIntimacySettings::EvaluateEligibility(Context), Expected);
		Context.*Flag = true;
	};

	TestDenied(TEXT("Content policy denial fails closed"), &FProjectIntimacyEligibilityContext::bContentAllowed,
		EProjectIntimacyEligibilityFailure::ContentDisabled);
	TestDenied(TEXT("Voluntary adult interactions require Charisma level 10"), &FProjectIntimacyEligibilityContext::bCharismaMasteryUnlocked,
		EProjectIntimacyEligibilityFailure::CharismaMasteryRequired);
	TestDenied(TEXT("Player adult verification is mandatory"), &FProjectIntimacyEligibilityContext::bPlayerAdultVerified,
		EProjectIntimacyEligibilityFailure::PlayerNotAdultVerified);
	TestDenied(TEXT("Partner adult verification is mandatory"), &FProjectIntimacyEligibilityContext::bPartnerAdultVerified,
		EProjectIntimacyEligibilityFailure::PartnerNotAdultVerified);
	TestDenied(TEXT("Explicit consent is mandatory"), &FProjectIntimacyEligibilityContext::bExplicitConsent,
		EProjectIntimacyEligibilityFailure::ConsentMissing);
	TestDenied(TEXT("Player must be alive"), &FProjectIntimacyEligibilityContext::bPlayerAlive,
		EProjectIntimacyEligibilityFailure::ParticipantDead);
	TestDenied(TEXT("Partner must be alive"), &FProjectIntimacyEligibilityContext::bPartnerAlive,
		EProjectIntimacyEligibilityFailure::ParticipantDead);
	TestDenied(TEXT("Player must be conscious"), &FProjectIntimacyEligibilityContext::bPlayerConscious,
		EProjectIntimacyEligibilityFailure::ParticipantUnconscious);
	TestDenied(TEXT("Partner must be conscious"), &FProjectIntimacyEligibilityContext::bPartnerConscious,
		EProjectIntimacyEligibilityFailure::ParticipantUnconscious);
	TestDenied(TEXT("Partner must be verified non-hostile"), &FProjectIntimacyEligibilityContext::bPartnerNonHostile,
		EProjectIntimacyEligibilityFailure::PartnerHostile);
	TestDenied(TEXT("Participants must be outside combat"), &FProjectIntimacyEligibilityContext::bOutsideCombat,
		EProjectIntimacyEligibilityFailure::InCombat);
	TestDenied(TEXT("The current zone must allow intimacy"), &FProjectIntimacyEligibilityContext::bZoneAllowed,
		EProjectIntimacyEligibilityFailure::ZoneNotAllowed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyPartnerSecureDefaultsTest,
	"NoShellForWinter.Intimacy.Eligibility.PartnerDefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyPartnerSecureDefaultsTest::RunTest(const FString& Parameters)
{
	UProjectIntimacyPartnerComponent* PartnerComponent = NewObject<UProjectIntimacyPartnerComponent>();
	TestNotNull(TEXT("Partner component should be constructible"), PartnerComponent);
	if (!PartnerComponent)
	{
		return false;
	}

	TestFalse(TEXT("Adult verification must be authored explicitly"), PartnerComponent->bAdultVerified);
	TestFalse(TEXT("Consent must be authored explicitly"), PartnerComponent->bExplicitConsent);
	TestFalse(TEXT("Non-hostile eligibility must be authored explicitly"), PartnerComponent->bNonHostileVerified);
	TestFalse(TEXT("Allowed zone must be authored explicitly"), PartnerComponent->bIntimacyZoneAllowed);
	TestFalse(TEXT("Social-companion identity must be authored explicitly"), PartnerComponent->bSocialCompanion);
	TestFalse(TEXT("A companion consent offer must be authored explicitly"), PartnerComponent->bOffersPlayerInitiatedConsent);
	TestTrue(TEXT("A newly configured participant starts conscious"), PartnerComponent->bConscious);
	TestFalse(TEXT("Combat eligibility fails closed until runtime authority verifies safety"), PartnerComponent->bOutsideCombat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyCharismaAndZoneGateTest,
	"NoShellForWinter.Intimacy.Eligibility.CharismaAndSpatialZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyCharismaAndZoneGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestFalse(
		TEXT("Charisma 9 does not unlock voluntary adult interactions"),
		UProjectIntimacySettings::MeetsAdultInteractionCharismaRequirement(9));
	TestTrue(
		TEXT("Charisma 10 unlocks voluntary adult interactions"),
		UProjectIntimacySettings::MeetsAdultInteractionCharismaRequirement(10));

	UProjectIntimacyZoneComponent* Zone = NewObject<UProjectIntimacyZoneComponent>();
	TestNotNull(TEXT("Intimacy zone component should be constructible"), Zone);
	if (Zone)
	{
		TestFalse(TEXT("Intimacy zones fail closed by default"), Zone->bAllowsIntimacy);
	}

	const FVector ZoneLocation = FVector::ZeroVector;
	TestFalse(
		TEXT("A disabled zone never authorizes participants"),
		UProjectIntimacyZoneComponent::AreLocationsWithinZone(
			ZoneLocation,
			650.0f,
			FVector(100.0f, 0.0f, 0.0f),
			FVector(200.0f, 0.0f, 0.0f),
			false));
	TestTrue(
		TEXT("An allowed zone authorizes two participants inside its radius"),
		UProjectIntimacyZoneComponent::AreLocationsWithinZone(
			ZoneLocation,
			650.0f,
			FVector(100.0f, 0.0f, 0.0f),
			FVector(200.0f, 0.0f, 0.0f),
			true));
	TestFalse(
		TEXT("One participant outside the zone denies the interaction"),
		UProjectIntimacyZoneComponent::AreLocationsWithinZone(
			ZoneLocation,
			650.0f,
			FVector(100.0f, 0.0f, 0.0f),
			FVector(651.0f, 0.0f, 0.0f),
			true));

	const UProjectIntimacySettings* Settings = UProjectIntimacySettings::Get();
	TestNotNull(TEXT("Intimacy settings should be available"), Settings);
	if (Settings)
	{
		TestTrue(
			TEXT("Charisma mastery enables the currently targeted adult-partner route"),
			Settings->bAllowCharismaMasteryTargetedPartners);
		TestTrue(
			TEXT("Charisma targeting has an explicit companion-class allowlist"),
			Settings->CharismaTargetedPartnerClasses.Num() > 0);
		bool bFoundCompanionClass = false;
		bool bFoundEnemyClass = false;
		for (const FSoftClassPath& ApprovedClass : Settings->CharismaTargetedPartnerClasses)
		{
			const FString ApprovedPath = ApprovedClass.ToString();
			bFoundCompanionClass |= ApprovedPath.Contains(TEXT("Companion"));
			bFoundEnemyClass |= ApprovedPath.Contains(TEXT("EnemyBP"));
		}
		TestTrue(TEXT("Charisma targeting includes authored companion classes"), bFoundCompanionClass);
		TestTrue(TEXT("Charisma targeting includes authored enemy classes"), bFoundEnemyClass);
		const FString CompanionPath = Settings->HubSocialCompanionClass.ToString();
		TestTrue(TEXT("The HUB product route resolves a companion class"), CompanionPath.Contains(TEXT("Companion")));
		TestFalse(TEXT("The HUB product route does not resolve an enemy class"), CompanionPath.Contains(TEXT("EnemyBP")));
		TestFalse(
			TEXT("HUB does not auto-spawn a social companion beside the player"),
			Settings->bAutoSpawnHubSocialCompanion);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyTalkOptionsTest,
	"NoShellForWinter.Intimacy.Talk.ClimaxFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyTalkOptionsTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("The neutral More action preserves its serialized value"),
		static_cast<uint8>(EProjectIntimacyTalkAction::More),
		static_cast<uint8>(6));
	TestEqual(
		TEXT("The neutral Back action preserves its serialized value"),
		static_cast<uint8>(EProjectIntimacyTalkAction::Back),
		static_cast<uint8>(8));

	TArray<FProjectIntimacyTalkOptionRow> Options;
	UProjectIntimacyDialogueLibrary::BuildFallbackTalkOptions(Options);

	const FProjectIntimacyTalkOptionRow* MoreOption = Options.FindByPredicate([](const FProjectIntimacyTalkOptionRow& Row)
	{
		return Row.OptionId == TEXT("Talk.More");
	});
	TestNotNull(TEXT("Fallback talk includes a neutral keep-going option"), MoreOption);
	if (MoreOption)
	{
		TestTrue(TEXT("Keep-going adds temporary partner Climax"), FMath::IsNearlyEqual(
			MoreOption->ClimaxGain,
			5.0f));
		TestEqual(
			TEXT("Keep-going targets the partner Climax meter"),
			MoreOption->ClimaxTarget,
			EProjectIntimacyClimaxTarget::Partner);
	}

	const FProjectIntimacyTalkOptionRow* ComplimentOption = Options.FindByPredicate([](const FProjectIntimacyTalkOptionRow& Row)
	{
		return Row.OptionId == TEXT("Talk.Compliment");
	});
	TestNotNull(TEXT("Fallback talk includes a compliment"), ComplimentOption);
	if (ComplimentOption)
	{
		TestTrue(TEXT("Compliment adds temporary partner Climax"), FMath::IsNearlyEqual(
			ComplimentOption->ClimaxGain,
			2.0f));
		TestEqual(
			TEXT("Compliment targets the partner Climax meter"),
			ComplimentOption->ClimaxTarget,
			EProjectIntimacyClimaxTarget::Partner);
	}

	const TSet<FName> AllowedOptionIds =
	{
		FName(TEXT("Talk.Speed.Slow")),
		FName(TEXT("Talk.Speed.Normal")),
		FName(TEXT("Talk.Speed.Intense")),
		FName(TEXT("Talk.More")),
		FName(TEXT("Talk.Compliment")),
		FName(TEXT("Talk.Back"))
	};
	TestFalse(TEXT("Fallback talk contains only the approved local-session actions"), Options.ContainsByPredicate(
		[&AllowedOptionIds](const FProjectIntimacyTalkOptionRow& Row)
		{
			return !AllowedOptionIds.Contains(Row.OptionId);
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectIntimacyPresentationMetadataTest,
	"NoShellForWinter.Intimacy.Presentation.ClimaxMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyPresentationMetadataTest::RunTest(const FString& Parameters)
{
	TArray<FProjectIntimacyMediaCueRow> MediaRows;
	UProjectIntimacyDialogueLibrary::BuildFallbackMediaCues(MediaRows);
	const FProjectIntimacyMediaCueRow* ClimaxCue = MediaRows.FindByPredicate([](const FProjectIntimacyMediaCueRow& Row)
	{
		return Row.CueId == TEXT("Climax.Preview");
	});
	TestNotNull(TEXT("Fallback media has a Climax cue"), ClimaxCue);
	if (ClimaxCue)
	{
		TestEqual(TEXT("Climax cue uses the Climax event id"), ClimaxCue->TriggerEventId, FName(TEXT("Climax")));
	}

	TArray<FProjectSocialCardRow> SocialRows;
	UProjectIntimacyDialogueLibrary::BuildFallbackSocialCardRows(SocialRows);
	TestTrue(TEXT("Social card exposes temporary player Climax"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("PlayerClimax");
	}));
	TestTrue(TEXT("Social card exposes temporary partner Climax"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("PartnerClimax");
	}));
	TestTrue(TEXT("Social card exposes player orgasm history"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("PlayerOrgasmCount");
	}));
	TestTrue(TEXT("Social card exposes partner orgasm history"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("PartnerOrgasmCount");
	}));
	const TSet<FName> AllowedValueIds =
	{
		FName(TEXT("Gender")),
		FName(TEXT("Personality")),
		FName(TEXT("PlayerClimax")),
		FName(TEXT("PartnerClimax")),
		FName(TEXT("Encounters")),
		FName(TEXT("PlayerOrgasmCount")),
		FName(TEXT("PartnerOrgasmCount")),
		FName(TEXT("FirstEncounter")),
		FName(TEXT("TotalIntimateTime"))
	};
	TestFalse(TEXT("Social card contains only the Climax allowlist"), SocialRows.ContainsByPredicate(
		[&AllowedValueIds](const FProjectSocialCardRow& Row)
		{
			return !AllowedValueIds.Contains(Row.ValueId);
		}));
	TestFalse(TEXT("Social card no longer exposes terminal satisfied-win semantics"), SocialRows.ContainsByPredicate(
		[](const FProjectSocialCardRow& Row)
		{
			return Row.ValueId == TEXT("SatisfiedWins");
		}));
	return true;
}

#endif
