#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ProjectCurseTypes.generated.h"

class AActor;

namespace ProjectCurse
{
	inline constexpr float Maximum = 100.f;
}

UENUM(BlueprintType)
enum class EProjectCurseSourceKind : uint8
{
	Room UMETA(DisplayName = "Room"),
	EnemyAttack UMETA(DisplayName = "Enemy Attack"),
	Magic UMETA(DisplayName = "Magic"),
	Trap UMETA(DisplayName = "Trap"),
	Environment UMETA(DisplayName = "Environment"),
	Relic UMETA(DisplayName = "Relic"),
	Equipment UMETA(DisplayName = "Equipment"),
	Narrative UMETA(DisplayName = "Narrative"),
	Consumable UMETA(DisplayName = "Consumable"),
	CleansingFailure UMETA(DisplayName = "Cleansing Failure"),
	Debug UMETA(DisplayName = "Debug")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCurseApplicationContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse", meta = (ClampMin = "0.0"))
	float Amount = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	EProjectCurseSourceKind SourceKind = EProjectCurseSourceKind::Environment;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	FGameplayTagContainer SourceTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	FGuid ApplicationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	bool bResistible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curse")
	bool bCanTriggerCursed = true;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCurseApplicationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	float RequestedAmount = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	float AppliedAmount = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	float ResistanceMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	float NewCurse = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	bool bDuplicate = false;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	bool bRejectedInvalidApplicationId = false;

	UPROPERTY(BlueprintReadOnly, Category = "Curse")
	bool bTriggeredCursed = false;
};
