#pragma once

#include "CoreMinimal.h"
#include "ProjectDefeatTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EProjectKnockoutReason : uint8
{
	None UMETA(DisplayName = "None"),
	PainMaxed UMETA(DisplayName = "Pain Maxed"),
	GuardRecovery UMETA(DisplayName = "Guard Recovery"),
	Surrender UMETA(DisplayName = "Surrender"),
	TacticalRetreat UMETA(DisplayName = "Tactical Retreat"),
	DebugForced UMETA(DisplayName = "Debug Forced")
};

UENUM(BlueprintType)
enum class EProjectDefeatPhase : uint8
{
	None UMETA(DisplayName = "None"),
	KnockedOut UMETA(DisplayName = "Knocked Out"),
	Struggle UMETA(DisplayName = "Struggle"),
	DefeatedBlackout UMETA(DisplayName = "Defeated Blackout"),
	TravelPending UMETA(DisplayName = "Travel Pending"),
	DefeatedScene UMETA(DisplayName = "Defeated Scene")
};

UENUM(BlueprintType)
enum class EProjectDefeatReason : uint8
{
	None UMETA(DisplayName = "None"),
	LostStruggle UMETA(DisplayName = "Lost Struggle"),
	StruggleUnavailable UMETA(DisplayName = "Struggle Unavailable"),
	QualifiedLethalHit UMETA(DisplayName = "Qualified Lethal Hit"),
	RepeatKnockout UMETA(DisplayName = "Repeat Knockout"),
	QualifiedExecution UMETA(DisplayName = "Qualified Execution"),
	DebugForced UMETA(DisplayName = "Debug Forced"),
	TacticalRetreat UMETA(DisplayName = "Tactical Retreat")
};

UENUM(BlueprintType)
enum class EProjectPostDefeatPresentation : uint8
{
	None UMETA(DisplayName = "None"),
	MatureSoloVignette UMETA(DisplayName = "Mature Solo Vignette")
};

UENUM(BlueprintType)
enum class EProjectDefeatPublicState : uint8
{
	Normal UMETA(DisplayName = "Normal"),
	Injured UMETA(DisplayName = "Injured"),
	Downed UMETA(DisplayName = "Downed"),
	DefeatedScene UMETA(DisplayName = "Defeated Scene")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectStruggleRound
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	TObjectPtr<AActor> EnemyActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	FName EnemyClassName = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 EnemyLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 RoundIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 ChartSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 NoteCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 CunningLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 MaxMissCount = 5;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float StruggleSpeedMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float TimeScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float HitWindowSeconds = 0.15f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float TravelTimeSeconds = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float NoteSpacingMinSeconds = 0.32f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float NoteSpacingMaxSeconds = 0.56f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float DurationSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatSceneDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defeat")
	FName InteractionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defeat")
	bool bAllowCancel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defeat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CancelFadeSeconds = 0.20f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatCameraComponentSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bWasActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bUsePawnControlRotation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	float FieldOfView = 90.0f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatSpringArmSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FString ComponentName;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	float TargetArmLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FVector SocketOffset = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	FVector TargetOffset = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bUsePawnControlRotation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bInheritPitch = true;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bInheritYaw = true;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bInheritRoll = true;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bDoCollisionTest = true;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bEnableCameraLag = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bEnableCameraRotationLag = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	float CameraLagSpeed = 10.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	float CameraRotationLagSpeed = 10.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	float CameraLagMaxDistance = 0.0f;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatCameraInputSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	bool bHasPlayerInputSensitivity = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	float MouseSensitivityX = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	float MouseSensitivityY = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	bool bHasLegacyInputScales = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	float LegacyYawScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	float LegacyPitchScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Input")
	float LegacyRollScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bHasPawnCameraRig = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bPawnUseControllerRotationPitch = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bPawnUseControllerRotationYaw = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	bool bPawnUseControllerRotationRoll = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	TArray<FProjectDefeatCameraComponentSnapshot> CameraComponents;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Camera")
	TArray<FProjectDefeatSpringArmSnapshot> SpringArmComponents;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectDefeatTransferPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	FGuid TransferId;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	EProjectKnockoutReason KnockoutReason = EProjectKnockoutReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	EProjectDefeatReason DefeatReason = EProjectDefeatReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bPlayerCompletedStruggleMinigame = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bTechnicalFailure = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bCancelledBeforeResolution = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bPostDefeatPresentationEligible = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bPostDefeatPresentationResolved = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	bool bPostDefeatPresentationDowngradedByPolicy = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	float PostDefeatPresentationRoll = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	EProjectPostDefeatPresentation PostDefeatPresentation = EProjectPostDefeatPresentation::None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	FProjectDefeatSceneDefinition SceneDefinition;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	FProjectDefeatCameraInputSnapshot CameraInputSnapshot;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 RetainedEntryCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat")
	int32 RetainedEquipmentCount = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectPainDebugSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	EProjectDefeatPhase CurrentPhase = EProjectDefeatPhase::None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	float PainCurrent = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	float PainThreshold = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	float TimeUntilDecaySeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	float LastPainDelta = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	float LastHitWorldTimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	bool bAdvancedFlowEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	bool bLastHitQualified = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	bool bLosingActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	bool bHadKnockoutThisCombat = false;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	int32 ActiveCombatSessionId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	int32 LastKnockoutCombatSessionId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	FName LastDamageType = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	FString LastSourceActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	FString LastDamageCauserName;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	FString LastResolvedActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Defeat|Debug")
	FString LastQualificationReason;
};
