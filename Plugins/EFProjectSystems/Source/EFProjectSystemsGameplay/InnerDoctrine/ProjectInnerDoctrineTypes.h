#pragma once

#include "CoreMinimal.h"
#include "Survival/ProjectCurseTypes.h"
#include "ProjectInnerDoctrineTypes.generated.h"

UENUM(BlueprintType)
enum class EProjectDoctrineAttribute : uint8
{
	Willpower UMETA(DisplayName = "Willpower"),
	Offensive UMETA(DisplayName = "Offensive"),
	Defensive UMETA(DisplayName = "Defensive"),
	Faith UMETA(DisplayName = "Faith"),
	Cunning UMETA(DisplayName = "Cunning"),
	Celerity UMETA(DisplayName = "Celerity"),
	Charisma UMETA(DisplayName = "Charisma"),
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EProjectDoctrineExperienceSource : uint8
{
	Combat UMETA(DisplayName = "Combat"),
	Survival UMETA(DisplayName = "Survival"),
	Exploration UMETA(DisplayName = "Exploration"),
	Training UMETA(DisplayName = "Training"),
	Dialogue UMETA(DisplayName = "Dialogue"),
	Quest UMETA(DisplayName = "Quest"),
	Utility UMETA(DisplayName = "Utility"),
	MaturePresentation UMETA(DisplayName = "Mature Presentation")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDoctrineAttributeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 NextLevelCost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bMilestone5Unlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bMilestone10Unlocked = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDoctrineMilestoneDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FName AbilityId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 RequiredLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FText Description;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDoctrineMilestoneState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	FProjectDoctrineMilestoneDefinition Definition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bUnlocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bOnCooldown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float CooldownRemainingSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectInnerDoctrineSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 CurrentRunDxp = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	int32 MetaBankDxp = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bDoctrineMasteryMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float Madness = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float MadnessMax = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float Curse = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float CurseMax = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float Pain = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float PainMax = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bCursed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	bool bGuardRecoveryActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	float GuardRecoveryPoolRemaining = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TArray<FProjectDoctrineAttributeState> Attributes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Doctrine")
	TArray<FProjectDoctrineMilestoneState> Milestones;
};
