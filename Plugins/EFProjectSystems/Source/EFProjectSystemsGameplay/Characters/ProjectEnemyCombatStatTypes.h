#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ProjectEnemyCombatStatTypes.generated.h"

USTRUCT(BlueprintType)
struct FProjectEnemyCombatStatRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	FText Label = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	FName Section = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	float BaseValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	float FinalValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	FText ValueOverride = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	bool bIsAvailable = false;
};

USTRUCT(BlueprintType)
struct FProjectSocialCardRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	FName RowId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	FText Label = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	FName Section = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	int32 SortOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	FName ValueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	bool bShowBeforeFirstEncounter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	bool bShowAfterFirstEncounter = true;
};

USTRUCT(BlueprintType)
struct FProjectEnemyCombatStatSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|EnemyLevel")
	TArray<FProjectEnemyCombatStatRow> Rows;
};

USTRUCT(BlueprintType)
struct FProjectSocialCardSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|SocialCard")
	TArray<FProjectEnemyCombatStatRow> Rows;
};
