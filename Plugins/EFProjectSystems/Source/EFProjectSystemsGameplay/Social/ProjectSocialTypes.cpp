#include "Social/ProjectSocialTypes.h"

namespace ProjectSocialTypesPrivate
{
	static FProjectSocialEligibilityResult Failure(const EProjectSocialEligibilityFailure Failure)
	{
		FProjectSocialEligibilityResult Result;
		Result.bEligible = false;
		Result.Failure = Failure;
		return Result;
	}

	static FProjectSocialEligibilityResult Eligible()
	{
		FProjectSocialEligibilityResult Result;
		Result.bEligible = true;
		Result.Failure = EProjectSocialEligibilityFailure::None;
		return Result;
	}
}

FProjectSocialEligibilityResult FProjectSocialRules::EvaluateConsentOfferEligibility(
	const FProjectSocialParticipantState& Initiator,
	const FProjectSocialParticipantState& Participant,
	const int32 MinimumAffinity)
{
	if (Initiator.ParticipantId.IsNone() || Participant.ParticipantId.IsNone())
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ParticipantNotRegistered);
	}

	if (!Initiator.bVerifiedAdult || !Participant.bVerifiedAdult)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::AdultVerificationRequired);
	}

	if (!Initiator.bAlive || !Participant.bAlive)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ParticipantNotAlive);
	}

	if (!Initiator.bConscious || !Participant.bConscious)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ParticipantNotConscious);
	}

	if (Initiator.bHostile || Participant.bHostile)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::Hostile);
	}

	if (Initiator.bInCombat || Participant.bInCombat)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::InCombat);
	}

	if (!Initiator.bInSafeLocation || !Participant.bInSafeLocation)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::UnsafeLocation);
	}

	if (!Participant.bRecruitedCompanion)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ParticipantNotCompanion);
	}

	if (!Participant.bOffersPlayerInitiatedIntimacy)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ConsentNotOffered);
	}

	const int32 RequiredAffinity = FMath::Max(
		FMath::Clamp(MinimumAffinity, -100, 100),
		FMath::Clamp(Participant.MinimumIntimacyAffinity, -100, 100));
	if (Participant.Affinity < RequiredAffinity)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::AffinityTooLow);
	}

	return ProjectSocialTypesPrivate::Eligible();
}

FProjectSocialEligibilityResult FProjectSocialRules::EvaluateIntimacyEligibility(
	const FProjectSocialParticipantState& Initiator,
	const FProjectSocialParticipantState& Participant,
	const bool bInitiatorConsented,
	const bool bParticipantConsented)
{
	const FProjectSocialEligibilityResult OfferResult =
		EvaluateConsentOfferEligibility(
			Initiator,
			Participant,
			Participant.MinimumIntimacyAffinity);
	if (!OfferResult.bEligible)
	{
		return OfferResult;
	}

	if (!bInitiatorConsented || !bParticipantConsented)
	{
		return ProjectSocialTypesPrivate::Failure(EProjectSocialEligibilityFailure::ExplicitConsentRequired);
	}

	return ProjectSocialTypesPrivate::Eligible();
}

bool FProjectSocialRules::CanStartDialogue(const FProjectSocialParticipantState& Participant)
{
	return !Participant.ParticipantId.IsNone()
		&& Participant.bAlive
		&& Participant.bConscious
		&& !Participant.bHostile
		&& !Participant.bInCombat;
}

bool FProjectSocialRules::CanRecruit(const FProjectSocialParticipantState& Participant)
{
	return CanStartDialogue(Participant)
		&& Participant.bRecruitable
		&& !Participant.bRecruitedCompanion;
}

bool FProjectSocialRules::IsLivingCompanion(const FProjectSocialParticipantState& Participant)
{
	return Participant.bRecruitedCompanion
		&& Participant.bAlive
		&& Participant.bConscious
		&& !Participant.bHostile;
}

int32 FProjectSocialRules::ClampAffinity(const int32 Affinity)
{
	return FMath::Clamp(Affinity, -100, 100);
}
