#include "ContentPolicy/ProjectContentPolicyTypes.h"

bool FProjectContentPolicyRules::IsMatureUnlockedByCharismaLevel(const int32 CharismaLevel)
{
	return CharismaLevel >= MatureUnlockCharismaLevel;
}

bool FProjectContentPolicyRules::IsMatureContentUnlocked(
	const FProjectContentPolicySnapshot& Snapshot)
{
	return Snapshot.bMatureUnlockedByCharisma && !Snapshot.bStreamerSafeForced;
}

bool FProjectContentPolicyRules::IsFeatureAllowed(
	const FProjectContentPolicySnapshot& Snapshot,
	const EProjectOptionalMatureFeature Feature)
{
	if (Snapshot.bStreamerSafeForced)
	{
		return false;
	}

	switch (Feature)
	{
	case EProjectOptionalMatureFeature::IntimacySession:
	case EProjectOptionalMatureFeature::PrivateSoloPresentation:
		return Snapshot.bMatureUnlockedByCharisma;
	case EProjectOptionalMatureFeature::MatureDefeatVignette:
		return true;
	default:
		return false;
	}
}

bool FProjectContentPolicyRules::IsIntimacyAllowed(const FProjectContentPolicySnapshot& Snapshot)
{
	return IsFeatureAllowed(Snapshot, EProjectOptionalMatureFeature::IntimacySession);
}

bool FProjectContentPolicyRules::IsMatureDefeatAllowed(const FProjectContentPolicySnapshot& Snapshot)
{
	return IsFeatureAllowed(Snapshot, EProjectOptionalMatureFeature::MatureDefeatVignette);
}
