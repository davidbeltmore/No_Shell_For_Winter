#pragma once

#include "CoreMinimal.h"
#include "ProjectContentPolicyTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EProjectOptionalMatureFeature : uint8
{
	IntimacySession UMETA(DisplayName = "Intimacy Session"),
	PrivateSoloPresentation UMETA(DisplayName = "Private Solo Presentation"),
	MatureDefeatVignette UMETA(DisplayName = "Mature Defeat Vignette")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectContentPolicySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Content Policy")
	int32 CharismaLevel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Content Policy")
	bool bMatureUnlockedByCharisma = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Content Policy")
	bool bStreamerSafeForced = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectMaturePresentationRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Content Policy")
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Content Policy")
	EProjectOptionalMatureFeature Feature = EProjectOptionalMatureFeature::IntimacySession;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Content Policy")
	FName PresentationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Content Policy")
	TObjectPtr<AActor> PrimaryParticipant = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Content Policy")
	TObjectPtr<AActor> SecondaryParticipant = nullptr;
};

/**
 * Stateless rules shared by the runtime subsystem, providers and automation.
 * Voluntary features fail closed; Mature Defeat follows its independent
 * authoritative outcome and is suppressed only by Streamer Safe.
 */
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectContentPolicyRules
{
	static constexpr int32 MatureUnlockCharismaLevel = 10;

	static bool IsMatureUnlockedByCharismaLevel(int32 CharismaLevel);
	static bool IsMatureContentUnlocked(const FProjectContentPolicySnapshot& Snapshot);

	static bool IsFeatureAllowed(
		const FProjectContentPolicySnapshot& Snapshot,
		EProjectOptionalMatureFeature Feature);

	static bool IsIntimacyAllowed(const FProjectContentPolicySnapshot& Snapshot);
	static bool IsMatureDefeatAllowed(const FProjectContentPolicySnapshot& Snapshot);
};
