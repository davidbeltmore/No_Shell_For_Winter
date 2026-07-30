#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Templates/SubclassOf.h"
#include "ACFTrainingTypes.generated.h"

class UACFTrainingMinigameBase;
class UAnimationAsset;

UENUM(BlueprintType)
enum class EACFTrainingSessionResult : uint8
{
	Cancelled,
	Failed,
	Succeeded
};

USTRUCT(BlueprintType)
struct ACFTRAININGSYSTEM_API FACFTrainingScalarGate
{
	GENERATED_BODY()

	FACFTrainingScalarGate();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	FName ResourceName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "RPG"), Category = "ACF Training")
	FGameplayTag ResourceTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	bool bUseMinimumValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bUseMinimumValue"), Category = "ACF Training")
	float MinimumValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	bool bUseMaximumValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bUseMaximumValue"), Category = "ACF Training")
	float MaximumValue;
};

USTRUCT(BlueprintType)
struct ACFTRAININGSYSTEM_API FACFTrainingFutureRequirements
{
	GENERATED_BODY()

	FACFTrainingFutureRequirements();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Requirements")
	bool bEnableScalarRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnableScalarRequirements", TitleProperty = "ResourceName"), Category = "ACF Training|Requirements")
	TArray<FACFTrainingScalarGate> ScalarRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Requirements")
	bool bEnableFailureCooldown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnableFailureCooldown", ClampMin = "0.0"), Category = "ACF Training|Requirements")
	float FailureCooldownSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Requirements")
	bool bEnableInventoryTagRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnableInventoryTagRequirements"), Category = "ACF Training|Requirements")
	TArray<FGameplayTag> RequiredInventoryTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Requirements")
	bool bEnableNearbyActorRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnableNearbyActorRequirements"), Category = "ACF Training|Requirements")
	TArray<FGameplayTag> RequiredNearbyActorTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bEnableNearbyActorRequirements", ClampMin = "0.0"), Category = "ACF Training|Requirements")
	float NearbySearchRadius;
};

USTRUCT(BlueprintType)
struct ACFTRAININGSYSTEM_API FACFTrainingDefinition
{
	GENERATED_BODY()

	FACFTrainingDefinition();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	FName TrainingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories = "RPG.PrimaryAttributes"), Category = "ACF Training")
	FGameplayTag TargetPrimaryAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"), Category = "ACF Training")
	float SuccessReward;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Animation")
	TSoftObjectPtr<UAnimationAsset> TrainingAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Minigame")
	TSubclassOf<UACFTrainingMinigameBase> MinigameClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "100"), Category = "ACF Training|Minigame")
	int32 MinigameDifficulty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACF Training|Requirements")
	FACFTrainingFutureRequirements Requirements;
};

USTRUCT(BlueprintType)
struct ACFTRAININGSYSTEM_API FACFTrainingProgressEntry
{
	GENERATED_BODY()

	FACFTrainingProgressEntry();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "ACF Training")
	FName TrainingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "ACF Training")
	float Progress;
};

USTRUCT(BlueprintType)
struct ACFTRAININGSYSTEM_API FACFTrainingAttributeRewardEntry
{
	GENERATED_BODY()

	FACFTrainingAttributeRewardEntry();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (Categories = "RPG.PrimaryAttributes"), Category = "ACF Training")
	FGameplayTag TargetAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "ACF Training")
	float AccumulatedReward;
};
