#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ProjectAutomaticTattooTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EProjectAutomaticTattooUnlockRule : uint8
{
	AlwaysOnSpawn UMETA(DisplayName = "Always On Spawn"),
	RuntimeReward UMETA(DisplayName = "Runtime Reward")
};

UENUM(BlueprintType)
enum class EProjectAutomaticTattooPlacementPreset : uint8
{
	ChestFront UMETA(DisplayName = "Chest Front"),
	AbdomenFront UMETA(DisplayName = "Abdomen Front"),
	PelvisFront UMETA(DisplayName = "Pelvis Front"),
	UpperBack UMETA(DisplayName = "Upper Back"),
	LowerBack UMETA(DisplayName = "Lower Back"),
	LeftUpperArm UMETA(DisplayName = "Left Upper Arm"),
	RightUpperArm UMETA(DisplayName = "Right Upper Arm"),
	LeftForearm UMETA(DisplayName = "Left Forearm"),
	RightForearm UMETA(DisplayName = "Right Forearm"),
	LeftThigh UMETA(DisplayName = "Left Thigh"),
	RightThigh UMETA(DisplayName = "Right Thigh"),
	// Legacy hidden aliases kept to preserve serialized enum values in existing DataTables.
	LeftUpperThigh UMETA(Hidden, DisplayName = "Deprecated Left Upper Thigh"),
	RightUpperThigh UMETA(Hidden, DisplayName = "Deprecated Right Upper Thigh"),
	LeftBackThigh UMETA(DisplayName = "Back Left Thigh"),
	RightBackThigh UMETA(DisplayName = "Back Right Thigh"),
	LeftCalf UMETA(DisplayName = "Left Calf"),
	RightCalf UMETA(DisplayName = "Right Calf"),
	LeftBackCalf UMETA(DisplayName = "Back Left Calf"),
	RightBackCalf UMETA(DisplayName = "Back Right Calf"),
	LeftHand UMETA(DisplayName = "Left Hand"),
	RightHand UMETA(DisplayName = "Right Hand"),
	LeftFoot UMETA(DisplayName = "Left Foot"),
	RightFoot UMETA(DisplayName = "Right Foot")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectAutomaticTattooTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	EProjectAutomaticTattooUnlockRule UnlockRule = EProjectAutomaticTattooUnlockRule::AlwaysOnSpawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock")
	FName RewardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unlock", meta = (MultiLine = "true"))
	FString UnlockDescription = TEXT("Appears automatically when the character spawns.");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	TSoftObjectPtr<UTexture2D> TattooTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EProjectAutomaticTattooPlacementPreset PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FName AnchorBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (UIMin = "-30.0", UIMax = "30.0"))
	float OffsetX = 1.92f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (UIMin = "-30.0", UIMax = "30.0"))
	float OffsetY = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "50.0"))
	float Size = 21.68f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (UIMin = "-180.0", UIMax = "180.0"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float ProjectionDistance = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	bool bEnabled = true;
};
