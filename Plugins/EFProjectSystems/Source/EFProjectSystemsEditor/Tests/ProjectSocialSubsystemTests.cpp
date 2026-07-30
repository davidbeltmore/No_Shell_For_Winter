#include "Social/ProjectSocialTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Social/ProjectSocialSubsystem.h"

namespace ProjectSocialSubsystemTestsPrivate
{
	static FProjectSocialParticipantState MakeEligibleParticipant(const FName ParticipantId)
	{
		FProjectSocialParticipantState State;
		State.ParticipantId = ParticipantId;
		State.bVerifiedAdult = true;
		State.bAlive = true;
		State.bConscious = true;
		State.bInSafeLocation = true;
		if (ParticipantId != FName(TEXT("Player")))
		{
			State.bRecruitedCompanion = true;
			State.bOffersPlayerInitiatedIntimacy = true;
			State.MinimumIntimacyAffinity = 25;
			State.Affinity = 50;
		}
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSocialConsentIsBilateralTest,
	"NoShellForWinter.Social.ConsentIsBilateral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSocialConsentIsBilateralTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FProjectSocialParticipantState Initiator =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Player"));
	const FProjectSocialParticipantState Participant =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));

	const FProjectSocialEligibilityResult OneSided =
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, false);
	TestFalse(TEXT("One-sided consent is insufficient."), OneSided.bEligible);
	TestEqual(
		TEXT("One-sided consent reports the explicit-consent gate."),
		OneSided.Failure,
		EProjectSocialEligibilityFailure::ExplicitConsentRequired);

	const FProjectSocialEligibilityResult Bilateral =
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true);
	TestTrue(TEXT("Mutual explicit consent passes for otherwise eligible adults."), Bilateral.bEligible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSocialEligibilityBoundariesTest,
	"NoShellForWinter.Social.EligibilityBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSocialEligibilityBoundariesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectSocialParticipantState Initiator =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Player"));
	FProjectSocialParticipantState Participant =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));

	Participant.bVerifiedAdult = false;
	TestEqual(
		TEXT("Adult verification is mandatory."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::AdultVerificationRequired);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bConscious = false;
	TestEqual(
		TEXT("Consciousness is mandatory."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::ParticipantNotConscious);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bHostile = true;
	TestEqual(
		TEXT("Hostility blocks eligibility."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::Hostile);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bInCombat = true;
	TestEqual(
		TEXT("Combat blocks eligibility."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::InCombat);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bInSafeLocation = false;
	TestEqual(
		TEXT("Unsafe locations block eligibility."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::UnsafeLocation);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bRecruitedCompanion = false;
	TestEqual(
		TEXT("Only an explicit recruited companion is eligible."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::ParticipantNotCompanion);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bOffersPlayerInitiatedIntimacy = false;
	TestEqual(
		TEXT("A companion must explicitly offer player-initiated Intimacy."),
		FProjectSocialRules::EvaluateIntimacyEligibility(Initiator, Participant, true, true).Failure,
		EProjectSocialEligibilityFailure::ConsentNotOffered);

	Participant = ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.Affinity = 24;
	TestEqual(
		TEXT("Affinity below the authored threshold blocks consent."),
		FProjectSocialRules::EvaluateConsentOfferEligibility(Initiator, Participant, 25).Failure,
		EProjectSocialEligibilityFailure::AffinityTooLow);

	Participant.Affinity = 25;
	TestTrue(
		TEXT("Affinity at the threshold allows the companion to answer the request."),
		FProjectSocialRules::EvaluateConsentOfferEligibility(Initiator, Participant, 25).bEligible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSocialCompanionAndAffinityTest,
	"NoShellForWinter.Social.CompanionAndAffinity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSocialCompanionAndAffinityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectSocialParticipantState Participant =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	Participant.bRecruitedCompanion = true;
	TestTrue(TEXT("A living, conscious recruited actor is a valid companion."), FProjectSocialRules::IsLivingCompanion(Participant));

	Participant.bAlive = false;
	TestFalse(TEXT("A dead recruited actor is not a valid companion."), FProjectSocialRules::IsLivingCompanion(Participant));

	TestEqual(TEXT("Affinity clamps high."), FProjectSocialRules::ClampAffinity(150), 100);
	TestEqual(TEXT("Affinity clamps low."), FProjectSocialRules::ClampAffinity(-150), -100);

	const FProjectSocialParticipantState SecureDefaults;
	TestFalse(TEXT("A social participant is not a companion by default."), SecureDefaults.bRecruitedCompanion);
	TestFalse(TEXT("A social participant offers no Intimacy by default."), SecureDefaults.bOffersPlayerInitiatedIntimacy);
	TestEqual(TEXT("Default affinity threshold fails closed."), SecureDefaults.MinimumIntimacyAffinity, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectSocialAtomicConsentRequestTest,
	"NoShellForWinter.Social.AtomicPlayerInitiatedConsent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectSocialAtomicConsentRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	UProjectSocialSubsystem* SocialSubsystem = TestGameInstance
		? NewObject<UProjectSocialSubsystem>(TestGameInstance)
		: nullptr;
	AActor* Player = NewObject<AActor>();
	AActor* Companion = NewObject<AActor>();
	TestNotNull(TEXT("Test game instance should be constructible"), TestGameInstance);
	TestNotNull(TEXT("Social subsystem should be constructible"), SocialSubsystem);
	TestNotNull(TEXT("Player should be constructible"), Player);
	TestNotNull(TEXT("Companion should be constructible"), Companion);
	if (!SocialSubsystem || !Player || !Companion)
	{
		return false;
	}

	const FProjectSocialParticipantState PlayerState =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Player"));
	FProjectSocialParticipantState CompanionState =
		ProjectSocialSubsystemTestsPrivate::MakeEligibleParticipant(TEXT("Companion"));
	CompanionState.Affinity = 24;
	TestTrue(TEXT("Player registration succeeds"), SocialSubsystem->RegisterOrUpdateParticipant(Player, PlayerState));
	TestTrue(TEXT("Companion registration succeeds"), SocialSubsystem->RegisterOrUpdateParticipant(Companion, CompanionState));

	const FProjectSocialEligibilityResult Denied =
		SocialSubsystem->TryEstablishBilateralIntimacyConsent(Player, Companion, 25);
	TestFalse(TEXT("Low affinity denies the request"), Denied.bEligible);
	TestFalse(TEXT("Denied request leaves no player consent"), SocialSubsystem->HasExplicitIntimacyConsent(Player, Companion));
	TestFalse(TEXT("Denied request leaves no companion consent"), SocialSubsystem->HasExplicitIntimacyConsent(Companion, Player));

	CompanionState.Affinity = 50;
	TestTrue(TEXT("Updated companion registration succeeds"), SocialSubsystem->RegisterOrUpdateParticipant(Companion, CompanionState));
	const FProjectSocialEligibilityResult Accepted =
		SocialSubsystem->TryEstablishBilateralIntimacyConsent(Player, Companion, 25);
	TestTrue(TEXT("An eligible companion accepts the bilateral request"), Accepted.bEligible);
	TestTrue(TEXT("Accepted request records player consent"), SocialSubsystem->HasExplicitIntimacyConsent(Player, Companion));
	TestTrue(TEXT("Accepted request records companion consent"), SocialSubsystem->HasExplicitIntimacyConsent(Companion, Player));

	SocialSubsystem->ClearIntimacyConsentForParticipant(Companion);
	TestFalse(TEXT("Clearing the session removes player consent"), SocialSubsystem->HasExplicitIntimacyConsent(Player, Companion));
	TestFalse(TEXT("Clearing the session removes companion consent"), SocialSubsystem->HasExplicitIntimacyConsent(Companion, Player));
	return true;
}

#endif
