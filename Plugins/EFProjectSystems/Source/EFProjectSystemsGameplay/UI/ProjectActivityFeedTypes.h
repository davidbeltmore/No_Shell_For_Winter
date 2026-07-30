#pragma once

#include "CoreMinimal.h"
#include "ProjectActivityFeedTypes.generated.h"

UENUM(BlueprintType)
enum class EProjectActivityFeedChannel : uint8
{
	System,
	Loot,
	Experience,
	Combat,
	Status,
	Dialogue
};

UENUM(BlueprintType)
enum class EProjectActivityFeedDetailMode : uint8
{
	Minimal,
	Balanced,
	Verbose
};

UENUM(BlueprintType)
enum class EProjectActivityFeedRenderStyle : uint8
{
	Auto,
	Standard,
	Gain,
	DialogueQuote
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectChronicleLayoutPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle|Layout")
	float MinimumRowHeight = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle|Layout")
	float MaximumTextWidth = 390.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle|Layout")
	float RowGap = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle|Layout")
	float InlinePrimaryWidthRatio = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chronicle|Layout")
	float LineHeightPercentage = 1.18f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectActivityFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	EProjectActivityFeedChannel Channel = EProjectActivityFeedChannel::System;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FText Message;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	int32 Sequence = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	EProjectActivityFeedRenderStyle RenderStyle = EProjectActivityFeedRenderStyle::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FString BadgeLabelOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FText PrimaryText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FText SecondaryText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FLinearColor AccentTintOverride = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
};
