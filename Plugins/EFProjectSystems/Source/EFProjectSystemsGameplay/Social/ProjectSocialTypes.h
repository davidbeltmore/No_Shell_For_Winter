#pragma once

#include "CoreMinimal.h"
#include "ProjectSocialTypes.generated.h"

UENUM(BlueprintType)
enum class EProjectSocialEligibilityFailure : uint8
{
	None UMETA(DisplayName = "None"),
	ParticipantNotRegistered UMETA(DisplayName = "Participant Not Registered"),
	AdultVerificationRequired UMETA(DisplayName = "Adult Verification Required"),
	ExplicitConsentRequired UMETA(DisplayName = "Explicit Consent Required"),
	ParticipantNotAlive UMETA(DisplayName = "Participant Not Alive"),
	ParticipantNotConscious UMETA(DisplayName = "Participant Not Conscious"),
	Hostile UMETA(DisplayName = "Hostile"),
	InCombat UMETA(DisplayName = "In Combat"),
	UnsafeLocation UMETA(DisplayName = "Unsafe Location"),
	ParticipantNotCompanion UMETA(DisplayName = "Participant Not Companion"),
	ConsentNotOffered UMETA(DisplayName = "Consent Not Offered"),
	AffinityTooLow UMETA(DisplayName = "Affinity Too Low")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSocialParticipantState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	FName ParticipantId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bVerifiedAdult = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bAlive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bConscious = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bHostile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bInCombat = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bInSafeLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bRecruitable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bRecruitedCompanion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social")
	bool bOffersPlayerInitiatedIntimacy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social", meta = (ClampMin = "-100", ClampMax = "100"))
	int32 MinimumIntimacyAffinity = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Social", meta = (ClampMin = "-100", ClampMax = "100"))
	int32 Affinity = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSocialEligibilityResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Social")
	bool bEligible = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Social")
	EProjectSocialEligibilityFailure Failure = EProjectSocialEligibilityFailure::ParticipantNotRegistered;
};

struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSocialRules
{
	static FProjectSocialEligibilityResult EvaluateIntimacyEligibility(
		const FProjectSocialParticipantState& Initiator,
		const FProjectSocialParticipantState& Participant,
		bool bInitiatorConsented,
		bool bParticipantConsented);

	static FProjectSocialEligibilityResult EvaluateConsentOfferEligibility(
		const FProjectSocialParticipantState& Initiator,
		const FProjectSocialParticipantState& Participant,
		int32 MinimumAffinity);

	static bool CanStartDialogue(const FProjectSocialParticipantState& Participant);
	static bool CanRecruit(const FProjectSocialParticipantState& Participant);
	static bool IsLivingCompanion(const FProjectSocialParticipantState& Participant);
	static int32 ClampAffinity(int32 Affinity);
};
