#pragma once

#include "CoreMinimal.h"
#include "EFCharacterCreationTypes.generated.h"

class USkeletalMesh;

UENUM(BlueprintType)
enum class ECharacterCustomizationTarget : uint8
{
	Auto = 0,
	Body = 1,
	Clothing = 3
};

UENUM(BlueprintType)
enum class ECharacterCreationGender : uint8
{
	NotApplicable = 0 UMETA(DisplayName = "Unknown"),
	Male = 1,
	Female = 2
};

USTRUCT(BlueprintType)
struct FMorphSliderEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	FName Category = TEXT("Body");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	FString Section;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	ECharacterCustomizationTarget Target = ECharacterCustomizationTarget::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	FName TargetComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	float MinValue = -3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	float MaxValue = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	float DefaultValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph")
	bool bAllowRandomize = true;
};

USTRUCT(BlueprintType)
struct FCharacterSkeletalMeshOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
};

USTRUCT(BlueprintType)
struct FCharacterGenderMeshOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gender")
	ECharacterCreationGender Gender = ECharacterCreationGender::NotApplicable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gender")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gender")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
};

USTRUCT(BlueprintType)
struct FCharacterCreationCategoryDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FName Category = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	TArray<FString> AutoAssignTokens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Category")
	int32 SortOrder = 0;
};

USTRUCT(BlueprintType)
struct FCharacterCreationCameraSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float Distance = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float HeightOffset = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float HorizontalOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FocusHeightFactor = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float YawOffset = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float PitchOffset = -6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FieldOfView = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float BlendTime = 0.35f;
};

USTRUCT(BlueprintType)
struct FCharacterMorphValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Morph")
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Morph")
	ECharacterCustomizationTarget Target = ECharacterCustomizationTarget::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Morph")
	FName TargetComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Morph")
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct FCharacterCustomizationState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Identity")
	FString CharacterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Identity")
	ECharacterCreationGender Gender = ECharacterCreationGender::Female;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	TArray<FCharacterMorphValue> MorphValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bShowClothes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bPauseAnimation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FLinearColor SkinColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FLinearColor IrisColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bHasSelectedBodyMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FSoftObjectPath SelectedBodyMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bHasSelectedHairMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FSoftObjectPath SelectedHairMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	bool bHasHairRelativeTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FTransform HairRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FName ActiveCategory = TEXT("Body");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "State")
	FString SearchFilter;
};

USTRUCT(BlueprintType)
struct FCharacterPresetData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Preset")
	FString PresetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Preset")
	FCharacterCustomizationState State;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Preset")
	FString SavedAtUtc;
};
