#include "ContentPolicy/ProjectContentPolicyTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ProjectEnemyVisualVariationSubsystem.h"
#include "ContentPolicy/ProjectOptionalMatureContentProvider.h"
#include "Misc/AutomationTest.h"
#include "UI/ProjectEmoteSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectContentPolicySafeDefaultsTest,
	"NoShellForWinter.ContentPolicy.SafeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectContentPolicySafeDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FProjectContentPolicySnapshot Snapshot;
	TestEqual(TEXT("Charisma defaults to zero."), Snapshot.CharismaLevel, 0);
	TestFalse(TEXT("The Charisma unlock defaults off."), Snapshot.bMatureUnlockedByCharisma);
	TestFalse(TEXT("Intimacy is closed before Charisma 10."), FProjectContentPolicyRules::IsIntimacyAllowed(Snapshot));
	TestFalse(
		TEXT("Private solo presentation is closed before Charisma 10."),
		FProjectContentPolicyRules::IsFeatureAllowed(
			Snapshot,
			EProjectOptionalMatureFeature::PrivateSoloPresentation));
	TestTrue(
		TEXT("Mature defeat remains policy-eligible independently of Charisma."),
		FProjectContentPolicyRules::IsMatureDefeatAllowed(Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectContentPolicyCharismaUnlockTest,
	"NoShellForWinter.ContentPolicy.CharismaUnlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectContentPolicyCharismaUnlockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectContentPolicySnapshot Snapshot;
	Snapshot.CharismaLevel = 9;
	Snapshot.bMatureUnlockedByCharisma =
		FProjectContentPolicyRules::IsMatureUnlockedByCharismaLevel(Snapshot.CharismaLevel);
	TestFalse(TEXT("Charisma 9 does not unlock mature interactions."), Snapshot.bMatureUnlockedByCharisma);
	TestFalse(TEXT("Intimacy remains closed at Charisma 9."), FProjectContentPolicyRules::IsIntimacyAllowed(Snapshot));

	Snapshot.CharismaLevel = 10;
	Snapshot.bMatureUnlockedByCharisma =
		FProjectContentPolicyRules::IsMatureUnlockedByCharismaLevel(Snapshot.CharismaLevel);
	TestTrue(TEXT("Charisma 10 unlocks mature interactions."), Snapshot.bMatureUnlockedByCharisma);
	TestTrue(TEXT("The effective mature-content gate opens at Charisma 10."),
		FProjectContentPolicyRules::IsMatureContentUnlocked(Snapshot));
	TestTrue(TEXT("Intimacy opens at Charisma 10."), FProjectContentPolicyRules::IsIntimacyAllowed(Snapshot));
	TestTrue(
		TEXT("Private solo presentation follows the Charisma unlock."),
		FProjectContentPolicyRules::IsFeatureAllowed(
			Snapshot,
			EProjectOptionalMatureFeature::PrivateSoloPresentation));
	TestTrue(
		TEXT("Mature defeat remains eligible at Charisma 10."),
		FProjectContentPolicyRules::IsMatureDefeatAllowed(Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectContentPolicyStreamerSafeWinsTest,
	"NoShellForWinter.ContentPolicy.StreamerSafeWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectContentPolicyStreamerSafeWinsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectContentPolicySnapshot Snapshot;
	Snapshot.CharismaLevel = 10;
	Snapshot.bMatureUnlockedByCharisma = true;
	Snapshot.bStreamerSafeForced = true;

	TestFalse(
		TEXT("Streamer-safe closes the effective mature-content gate."),
		FProjectContentPolicyRules::IsMatureContentUnlocked(Snapshot));
	TestFalse(TEXT("Streamer-safe blocks intimacy."), FProjectContentPolicyRules::IsIntimacyAllowed(Snapshot));
	TestFalse(
		TEXT("Streamer-safe blocks private solo presentation."),
		FProjectContentPolicyRules::IsFeatureAllowed(
			Snapshot,
			EProjectOptionalMatureFeature::PrivateSoloPresentation));
	TestFalse(TEXT("Streamer-safe blocks mature defeat."), FProjectContentPolicyRules::IsMatureDefeatAllowed(Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectOptionalMatureMorphPolicyGateTest,
	"NoShellForWinter.ContentPolicy.OptionalMatureMorphs.FailClosedGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectOptionalMatureMorphPolicyGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FProjectContentPolicySnapshot Snapshot;
	TestFalse(
		TEXT("Optional mature morphs are denied before Charisma 10."),
		UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
			true,
			Snapshot));

	Snapshot.CharismaLevel = 10;
	Snapshot.bMatureUnlockedByCharisma = true;
	TestTrue(
		TEXT("Optional mature morphs follow the Charisma-gated Intimacy policy."),
		UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
			true,
			Snapshot));
	TestFalse(
		TEXT("A disabled capability remains denied even when Intimacy is allowed."),
		UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
			false,
			Snapshot));

	Snapshot.bStreamerSafeForced = true;
	TestFalse(
		TEXT("Streamer Safe overrides the optional mature morph capability."),
		UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
			true,
			Snapshot));

	Snapshot.bStreamerSafeForced = false;
	Snapshot.CharismaLevel = 9;
	Snapshot.bMatureUnlockedByCharisma = false;
	TestFalse(
		TEXT("Voluntary mature morph presentation cannot bypass the Charisma gate."),
		UProjectEnemyVisualVariationSubsystem::IsOptionalMatureMorphPresentationAllowedForPolicy(
			true,
			Snapshot));

	TestFalse(
		TEXT("Charisma permission alone keeps a non-participant neutral."),
		UProjectEnemyVisualVariationSubsystem::ShouldUseActiveOptionalMatureMorph(
			true,
			false,
			false));
	TestTrue(
		TEXT("The exact living Intimacy participant may use the active morph after the gate passes."),
		UProjectEnemyVisualVariationSubsystem::ShouldUseActiveOptionalMatureMorph(
			true,
			true,
			false));
	TestFalse(
		TEXT("A dead participant always returns to neutral."),
		UProjectEnemyVisualVariationSubsystem::ShouldUseActiveOptionalMatureMorph(
			true,
			true,
			true));
	TestFalse(
		TEXT("Streamer-safe or low Charisma permission keeps even a session participant neutral."),
		UProjectEnemyVisualVariationSubsystem::ShouldUseActiveOptionalMatureMorph(
			false,
			true,
			false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectPrivateSoloPresentationProviderContractTest,
	"NoShellForWinter.ContentPolicy.PrivateSolo.ProviderContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectPrivateSoloPresentationProviderContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(
		TEXT("The action subsystem implements the optional mature-content provider boundary."),
		UProjectEmoteSubsystem::StaticClass()->ImplementsInterface(
			UProjectOptionalMatureContentProvider::StaticClass()));

	const UProjectEmoteSubsystem* DefaultSubsystem = GetDefault<UProjectEmoteSubsystem>();
	TestNotNull(TEXT("The action provider CDO is available."), DefaultSubsystem);
	if (DefaultSubsystem)
	{
		TestTrue(
			TEXT("The action provider owns private solo presentation."),
			DefaultSubsystem->SupportsMatureFeature(
				EProjectOptionalMatureFeature::PrivateSoloPresentation));
		TestFalse(
			TEXT("The action provider does not claim Intimacy Session."),
			DefaultSubsystem->SupportsMatureFeature(
				EProjectOptionalMatureFeature::IntimacySession));
		TestFalse(
			TEXT("The action provider does not claim mature defeat."),
			DefaultSubsystem->SupportsMatureFeature(
				EProjectOptionalMatureFeature::MatureDefeatVignette));
	}

	return true;
}

#endif
