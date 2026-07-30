#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectCharacterBackgroundTypes.generated.h"

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDoctrineStartingLevelModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName AttributeID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	int32 StartingLevelDelta = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDxpGainModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName AttributeID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	float GainMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCharacterBackstoryData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName BackstoryID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> Advantages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> Disadvantages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FProjectDoctrineStartingLevelModifier> StartingLevels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TSoftObjectPtr<UTexture2D> PreviewImage;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCharacterProfessionData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FName ProfessionID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> Advantages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> Disadvantages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FProjectDxpGainModifier> GainModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TSoftObjectPtr<UTexture2D> PreviewImage;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCharacterBackgroundSummary
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	bool bValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FText BackstoryName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	FText ProfessionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> StartingLevelLines;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Background")
	TArray<FText> GainModifierLines;
};

namespace ProjectCharacterBackground
{
	EFPROJECTSYSTEMSGAMEPLAY_API bool TryResolveDoctrineAttribute(FName AttributeID, EProjectDoctrineAttribute& OutAttribute);
	EFPROJECTSYSTEMSGAMEPLAY_API FName GetDoctrineAttributeID(EProjectDoctrineAttribute Attribute);
	EFPROJECTSYSTEMSGAMEPLAY_API FText GetDoctrineAttributeDisplayText(EProjectDoctrineAttribute Attribute);
}
