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
	FProjectIntimacySessionProgressMathTest,
	"NoShellForWinter.Intimacy.SessionProgress.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacySessionProgressMathTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Session progress clamps below zero"), FMath::IsNearlyZero(
		UProjectIntimacySettings::ClampSessionProgress(-10.0f)));
	TestTrue(TEXT("Session progress preserves values in range"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ClampSessionProgress(55.0f),
		55.0f));
	TestTrue(TEXT("Session progress clamps to 100"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ClampSessionProgress(250.0f),
		100.0f));

	TestTrue(TEXT("Please has no progress without a successful hit"), FMath::IsNearlyZero(
		UProjectIntimacySettings::ComputePleaseProgressGain(0)));
	TestTrue(TEXT("Please grants five progress per successful hit"), FMath::IsNearlyEqual(
		UProjectIntimacySettings::ComputePleaseProgressGain(3),
		15.0f));

	float RemainingPeak = 0.0f;
	TestEqual(
		TEXT("Crossing one threshold emits one session peak"),
		UProjectIntimacySettings::ConsumeSessionPeak(10.0f, 20.0f, 25.0f, RemainingPeak),
		1);
	TestTrue(TEXT("A session peak preserves residual progress"), FMath::IsNearlyEqual(RemainingPeak, 5.0f));

	TestEqual(
		TEXT("A large gain can emit multiple local peaks"),
		UProjectIntimacySettings::ConsumeSessionPeak(20.0f, 60.0f, 25.0f, RemainingPeak),
		3);
	TestTrue(TEXT("Multiple peaks preserve residual progress"), FMath::IsNearlyEqual(RemainingPeak, 5.0f));

	TestTrue(
		TEXT("Peak anticipation remains neutral outside its window"),
		FMath::IsNearlyEqual(
			UProjectIntimacySettings::ComputeSessionPeakAnticipationMultiplier(19.0f, 25.0f),
			1.0f));
	TestTrue(
		TEXT("Peak anticipation increases inside its final window"),
		UProjectIntimacySettings::ComputeSessionPeakAnticipationMultiplier(22.0f, 25.0f) > 1.0f);
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
	TestTrue(TEXT("Combat state can revoke eligibility at runtime"), PartnerComponent->bOutsideCombat);
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
	"NoShellForWinter.Intimacy.Talk.NeutralLocalProgress",
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
		TestTrue(TEXT("Keep-going only adds local session progress"), FMath::IsNearlyEqual(
			MoreOption->SessionProgressGain,
			5.0f));
	}

	const FProjectIntimacyTalkOptionRow* ComplimentOption = Options.FindByPredicate([](const FProjectIntimacyTalkOptionRow& Row)
	{
		return Row.OptionId == TEXT("Talk.Compliment");
	});
	TestNotNull(TEXT("Fallback talk includes a compliment"), ComplimentOption);
	if (ComplimentOption)
	{
		TestTrue(TEXT("Compliment only adds local session progress"), FMath::IsNearlyEqual(
			ComplimentOption->SessionProgressGain,
			2.0f));
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
	"NoShellForWinter.Intimacy.Presentation.NeutralMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectIntimacyPresentationMetadataTest::RunTest(const FString& Parameters)
{
	TArray<FProjectIntimacyMediaCueRow> MediaRows;
	UProjectIntimacyDialogueLibrary::BuildFallbackMediaCues(MediaRows);
	const FProjectIntimacyMediaCueRow* PeakCue = MediaRows.FindByPredicate([](const FProjectIntimacyMediaCueRow& Row)
	{
		return Row.CueId == TEXT("SessionPeak.Preview");
	});
	TestNotNull(TEXT("Fallback media has a neutral session-peak cue"), PeakCue);
	if (PeakCue)
	{
		TestEqual(TEXT("Session-peak cue uses the neutral event id"), PeakCue->TriggerEventId, FName(TEXT("SessionPeak")));
	}

	TArray<FProjectSocialCardRow> SocialRows;
	UProjectIntimacyDialogueLibrary::BuildFallbackSocialCardRows(SocialRows);
	TestTrue(TEXT("Social card exposes local session progress"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("SessionProgress");
	}));
	TestTrue(TEXT("Social card exposes neutral peak history"), SocialRows.ContainsByPredicate([](const FProjectSocialCardRow& Row)
	{
		return Row.ValueId == TEXT("SessionPeakCount");
	}));
	const TSet<FName> AllowedValueIds =
	{
		FName(TEXT("Gender")),
		FName(TEXT("Personality")),
		FName(TEXT("SessionProgress")),
		FName(TEXT("Encounters")),
		FName(TEXT("SatisfiedWins")),
		FName(TEXT("SessionPeakCount")),
		FName(TEXT("FirstEncounter")),
		FName(TEXT("TotalIntimateTime"))
	};
	TestFalse(TEXT("Social card contains only the neutral allowlist"), SocialRows.ContainsByPredicate(
		[&AllowedValueIds](const FProjectSocialCardRow& Row)
		{
			return !AllowedValueIds.Contains(Row.ValueId);
		}));
	return true;
}

#endif
