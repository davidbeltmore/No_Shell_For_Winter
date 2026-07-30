#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "ProjectEmoteTypes.generated.h"

class UAnimationAsset;
class UBlueprint;
class UTexture2D;
class AActor;

UENUM(BlueprintType)
enum class EProjectEmoteType : uint8
{
	None UMETA(DisplayName = "None"),
	Dance UMETA(DisplayName = "Dance"),
	Sit UMETA(DisplayName = "Private Solo"),
	LookingBack UMETA(DisplayName = "Looking Back")
};

UENUM(BlueprintType)
enum class EProjectEmoteMenuCategory : uint8
{
	Root UMETA(DisplayName = "Root"),
	Actions UMETA(DisplayName = "Actions"),
	Objects UMETA(DisplayName = "Objects")
};

UENUM(BlueprintType)
enum class EProjectEmotePlaybackMode : uint8
{
	Looping UMETA(DisplayName = "Looping"),
	PlayOnce UMETA(DisplayName = "Play Once")
};

UENUM(BlueprintType)
enum class EProjectEmoteMenuNodeType : uint8
{
	Folder UMETA(DisplayName = "Folder"),
	Action UMETA(DisplayName = "Action"),
	Cancel UMETA(DisplayName = "Cancel"),
	Back UMETA(DisplayName = "Back")
};

UENUM(BlueprintType)
enum class EProjectEmoteMenuVisualMode : uint8
{
	Root UMETA(DisplayName = "Root"),
	Category UMETA(DisplayName = "Category"),
	AnimationList UMETA(DisplayName = "Animation List")
};

UENUM(BlueprintType)
enum class EProjectEmoteRuntimeActionSource : uint8
{
	Menu UMETA(DisplayName = "Menu"),
	Defeat UMETA(DisplayName = "Defeat"),
	Respawn UMETA(DisplayName = "Respawn"),
	Spawn UMETA(DisplayName = "Spawn"),
	Death UMETA(DisplayName = "Death"),
	Execution UMETA(DisplayName = "Execution"),
	Teleport UMETA(DisplayName = "Teleport"),
	Interaction UMETA(DisplayName = "Interaction"),
	Script UMETA(DisplayName = "Script")
};

UENUM(BlueprintType)
enum class EProjectEmoteRuntimeActionEndReason : uint8
{
	Completed UMETA(DisplayName = "Completed"),
	Cancelled UMETA(DisplayName = "Cancelled"),
	Interrupted UMETA(DisplayName = "Interrupted"),
	Failed UMETA(DisplayName = "Failed")
};

USTRUCT(BlueprintType)
struct FProjectEmoteRuntimeActionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	FName RuntimeActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	FName InteractionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	EProjectEmoteRuntimeActionSource Source = EProjectEmoteRuntimeActionSource::Script;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	bool bAllowCancel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	bool bCancelWithY = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	bool bRestoreMovementOnEnd = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Runtime Action")
	bool bHiddenFromMenu = true;
};

USTRUCT(BlueprintType)
struct FProjectEmoteEquipmentRequirement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FSoftObjectPath> AcceptedAssetPaths;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FString> AcceptedNameHints;
};

USTRUCT(BlueprintType)
struct FProjectEmoteActionEffects
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct FProjectEmoteRootOffsetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	bool bApplyRootOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FVector LocalActorOffset = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FProjectEmoteParticipantRootOffsetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName ParticipantId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SearchRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TSoftClassPtr<AActor> RequiredActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FVector LocalActorOffset = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FProjectEmoteBlueprintSceneSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	bool bUseBlueprintScene = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	TSoftObjectPtr<UBlueprint> SceneBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	FName PrimaryRoleName = TEXT("Female");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	FName TargetRoleName = TEXT("Male");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	bool bRequireCurrentTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	bool bPlaceTargetNearPrimaryOnEnd = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	FVector TargetEndLocalOffset = FVector(100.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Blueprint Scene")
	bool bTargetFacesPrimaryOnEnd = true;
};

USTRUCT(BlueprintType)
struct FProjectEmoteFreeCameraSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera")
	bool bUseFreeCameraDuringEmote = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BlendTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MoveSpeed = 560.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float BoostMoveSpeed = 1470.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float MouseSensitivity = 0.2808f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinPitch = -89.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPitch = 89.0f;
};

USTRUCT(BlueprintType)
struct FProjectEmoteSceneRuntimeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Playback")
	FName OverlaySlotName = TEXT("DefaultSlot");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Playback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendInTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Playback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendOutTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FadeToBlackDuration = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BlackScreenHoldDuration = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FadeFromBlackDuration = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Recovery", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PostEmoteRecoveryDelaySeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CombatMenuLockoutSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|HUD")
	bool bSuppressHudDuringEmote = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Pose")
	bool bApplyPreActionSettle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Pose")
	bool bApplyMinimalAnimSceneLock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Camera")
	FProjectEmoteFreeCameraSettings FreeCamera;
};

USTRUCT(BlueprintType)
struct FProjectEmoteInteractionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName InteractionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteMenuCategory MenuCategory = EProjectEmoteMenuCategory::Actions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmotePlaybackMode PlaybackMode = EProjectEmotePlaybackMode::Looping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteType LegacyEmoteType = EProjectEmoteType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TSoftObjectPtr<UAnimationAsset> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FProjectEmoteEquipmentRequirement> EquipmentRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName SourceNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName ParentNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	int32 SortOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteActionEffects Effects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteRootOffsetSettings RootOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FProjectEmoteParticipantRootOffsetSettings> AdditionalParticipantRootOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteBlueprintSceneSettings BlueprintScene;
};

USTRUCT(BlueprintType)
struct FProjectEmoteMenuNodeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName NodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName ParentNodeId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteMenuNodeType NodeType = EProjectEmoteMenuNodeType::Folder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FName VisualIconId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FName VisualAttribute = TEXT("None");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	TSoftObjectPtr<UTexture2D> MenuIconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 RequiredExtraNpcCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TSoftObjectPtr<UAnimationAsset> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmotePlaybackMode PlaybackMode = EProjectEmotePlaybackMode::Looping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteType LegacyEmoteType = EProjectEmoteType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteMenuCategory LegacyMenuCategory = EProjectEmoteMenuCategory::Actions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	int32 SortOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FProjectEmoteEquipmentRequirement> EquipmentRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteActionEffects Effects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteRootOffsetSettings RootOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	TArray<FProjectEmoteParticipantRootOffsetSettings> AdditionalParticipantRootOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FProjectEmoteBlueprintSceneSettings BlueprintScene;
};

USTRUCT(BlueprintType)
struct FProjectEmoteMenuOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote")
	EProjectEmoteMenuNodeType NodeType = EProjectEmoteMenuNodeType::Action;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FName VisualIconId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	FName VisualAttribute = TEXT("None");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation")
	TSoftObjectPtr<UTexture2D> MenuIconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Emote|Presentation", meta = (ClampMin = "0", ClampMax = "3", UIMin = "0", UIMax = "3"))
	int32 RequiredExtraNpcCount = 0;
};
