#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimEnums.h"
#include "GameplayTagContainer.h"
#include "ProjectLocomotionOverrideComponent.generated.h"

class ACharacter;
class APawn;
class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;
class APlayerController;
class UCharacterMovementComponent;
class UMeshDeformer;
class USkeletalMesh;
class USkeletalMeshComponent;
struct FMontageBlendSettings;

UENUM(BlueprintType)
enum class EProjectMovementModifierLayer : uint8
{
	DoctrineBonus UMETA(DisplayName = "Doctrine Bonus"),
	StatusPenalty UMETA(DisplayName = "Status Penalty")
};

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLocomotionOverrideComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectLocomotionOverrideComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion")
	void ToggleWalkMode();

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion")
	void SetWalkModeEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion")
	void ToggleCrawlMode();

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion")
	void SetCrawlModeEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion")
	bool IsWalkModeEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion")
	bool IsCrawlModeActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	bool HasResolvedMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	bool HasResolvedSkeletalMeshComponent() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	bool HasResolvedAnimInstance() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString DescribeResolvedDependencies() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString GetResolvedSkeletalMeshComponentName() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString GetResolvedSkeletalMeshAssetPath() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	float GetCurrentResolvedWalkSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	float GetCurrentDesiredMoveSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	float GetCurrentEffectiveNormalMoveSpeed() const;

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion|Modifiers")
	void SetMovementSpeedModifier(FGameplayTag SourceTag, EProjectMovementModifierLayer Layer, float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion|Modifiers")
	void ClearMovementSpeedModifier(FGameplayTag SourceTag, EProjectMovementModifierLayer Layer);

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion|Modifiers")
	void ClearMovementSpeedModifiers(EProjectMovementModifierLayer Layer);

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion|Modifiers")
	void SetStatusPenaltyMitigation(FGameplayTag StatusModifierTag, float MitigationRatio);

	UFUNCTION(BlueprintCallable, Category = "Project|Locomotion|Modifiers")
	void ClearStatusPenaltyMitigation(FGameplayTag StatusModifierTag);

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Modifiers")
	float GetResolvedDoctrineMovementMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Modifiers")
	float GetResolvedStatusMovementMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	bool IsTransitionMovementLockActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString GetCurrentAnimationAssetName() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString GetActiveOverlayMontageName() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	bool IsCrawlMeshDeformerApplied() const;

	UFUNCTION(BlueprintPure, Category = "Project|Locomotion|Debug")
	FString GetActiveCrawlMeshDeformerName() const;

protected:
	void ResolveDependencies();
	void CacheOriginalMovementState();
	void CaptureNormalMovementStateForCustomMode();
	void RestoreNormalMovementStateAfterCustomMode();
	void CacheOriginalRotationState();
	void ApplyWalkModeJumpRestriction();
	void RestoreWalkModeJumpRestriction();
	void ApplyDesiredMovementSpeed();
	void RestoreNormalMovementSpeedOverride();
	void RefreshMovementTickState();
	bool HasActiveMovementSpeedModifiers() const;
	float ResolveRawNormalMoveSpeed() const;
	float ResolveCombinedMovementMultiplier() const;
	float ResolveEffectiveNormalMoveSpeed() const;
	void ApplyDesiredRotationBehavior();
	void UpdateControllerMoveInputIgnoreState();
	void UpdateCrawlManualInput(float DeltaTime);
	float ResolveWalkSpeed() const;
	float ResolveCrawlSpeed() const;
	void UpdateAnimationState(float DeltaTime);
	void EnterAnimationOverlayMode();
	void RestoreAnimationState();
	void PlayAnimationAsset(UAnimationAsset* AnimationAsset, bool bLooping);
	void PlayOneShotAnimation(UAnimationAsset* AnimationAsset, UAnimationAsset* NextLoopAnimation);
	void PlayCrawlEntryAnimation(UAnimationAsset* NextLoopAnimation);
	bool HandleHeldCrawlEntryPose(float CurrentTimeSeconds, UAnimationAsset* DesiredLoopAnimation, bool bMoving, bool bCrawling);
	void ClearAnimationPlaybackState();
	bool IsOneShotPlaying(float CurrentTimeSeconds) const;
	bool IsCrawlLoopAnimation(const UAnimationAsset* AnimationAsset) const;
	bool ShouldUseSeamlessCrawlLoopHandoff(const UAnimationAsset* NextAnimationAsset) const;
	float ResolveAnimationStartOffset(const UAnimationAsset* AnimationAsset, bool bSeamlessCrawlLoopHandoff) const;
	bool HasSignificantMovement() const;
	FVector ResolveMovementIntentVector(const FVector& Velocity) const;
	float ConsumeMovementDirectionDeltaDegrees(const FVector& Velocity, bool bMoving);
	bool TryPlayWalkPivot(float CurrentTimeSeconds, bool bMoving, float MovementDirectionDeltaDegrees, UAnimationAsset* DesiredLoopAnimation);
	UAnimationAsset* LoadAnimationAsset(const TSoftObjectPtr<UAnimationAsset>& AssetReference);
	void ApplyWalkModeRootMotionSettings(UAnimationAsset* AnimationAsset);
	UAnimationAsset* ResolveDesiredWalkLoopAnimation(bool bMoving);
	bool IsUsingMaleBodyMesh() const;
	UAnimationAsset* ResolveDesiredCrawlLoopAnimation() const;
	UAnimationAsset* ResolveDesiredLoopAnimation(bool bCrawling, bool bMoving);
	bool ShouldInterruptCurrentOneShot(UAnimationAsset* DesiredLoopAnimation, bool bCrawling, bool bMoving) const;
	UAnimInstance* ResolveAnimInstance() const;
	UAnimMontage* PlayDynamicSlotAnimation(
		UAnimSequenceBase* SequenceAsset,
		const FMontageBlendSettings& BlendInSettings,
		const FMontageBlendSettings& BlendOutSettings,
		float PlayRate,
		int32 LoopCount,
		bool bEnableAutoBlendOut,
		float BlendOutTriggerTime = -1.f,
		float StartTimeSeconds = 0.f);
	void StopOverlayPlayback(float BlendOutTime);
	void FlushRootMotionState();
	float GetAnimationDuration(UAnimationAsset* AnimationAsset) const;
	float ResolveOneShotHoldTime(UAnimationAsset* AnimationAsset) const;
	bool IsCrawlTransitionAnimation(const UAnimationAsset* AnimationAsset) const;
	bool IsMovementLockedByTransition(float CurrentTimeSeconds) const;
	void UpdateTransitionMovementLock(float CurrentTimeSeconds);
	void ReleaseTransitionMovementLock();
	APlayerController* ResolveOwningPlayerController() const;
	void UpdateCrawlMeshDeformerState();
	void RestoreCrawlMeshDeformer();
	bool IsSupportedCrawlDeformerMesh() const;
	UMeshDeformer* LoadCrawlMeshDeformer();

protected:
	UPROPERTY(EditAnywhere, Category = "Project|Locomotion", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float WalkSpeedMultiplier = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float CrawlSpeedMultiplier = 0.22f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MinimumWalkSpeed = 180.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MinimumCrawlSpeed = 95.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CrawlTransitionDuration = 3.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float CrawlTurnRateDegreesPerSecond = 180.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CrawlEntryPoseHoldDuration = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MovementActivationThreshold = 6.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay")
	FName OverlaySlotName = TEXT("DefaultSlot");

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendInTime = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendOutTime = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TransitionBlendInTime = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TransitionBlendOutTime = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Overlay", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float MaxTransitionDuration = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk", meta = (ClampMin = "15.0", UIMin = "15.0"))
	float PivotTriggerAngle = 105.f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PivotActionCooldown = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk")
	TSoftObjectPtr<UAnimationAsset> WalkLoopAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk")
	TSoftObjectPtr<UAnimationAsset> WalkIdleAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk")
	TSoftObjectPtr<UAnimationAsset> WalkPivotAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk|Male")
	TSoftObjectPtr<UAnimationAsset> MaleWalkLoopAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk|Male")
	TSoftObjectPtr<UAnimationAsset> MaleWalkIdleAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Walk|Male")
	TSoftObjectPtr<UAnimationAsset> MaleWalkPivotAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl")
	TSoftObjectPtr<UAnimationAsset> CrawlEntryAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl")
	TSoftObjectPtr<UAnimationAsset> CrawlExitAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl")
	TSoftObjectPtr<UAnimationAsset> CrawlIdleAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl")
	TSoftObjectPtr<UAnimationAsset> CrawlForwardAnimation;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl|Deformer")
	bool bEnableCrawlMeshDeformer = true;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl|Deformer")
	bool bDisableCrawlMeshDeformerWhileComponentPhysicsIsSimulating = true;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl|Deformer")
	TSoftObjectPtr<UMeshDeformer> CrawlMeshDeformer;

	UPROPERTY(EditAnywhere, Category = "Project|Locomotion|Crawl|Deformer")
	TArray<TSoftObjectPtr<USkeletalMesh>> CrawlDeformerSupportedMeshes;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	TWeakObjectPtr<APawn> CachedPawnOwner;
	TWeakObjectPtr<ACharacter> CachedCharacterOwner;
	TWeakObjectPtr<UAnimInstance> ActiveAnimInstance;

	TMap<FGameplayTag, float> DoctrineMovementModifiers;
	TMap<FGameplayTag, float> StatusMovementPenalties;
	TMap<FGameplayTag, float> StatusPenaltyMitigations;
	bool bNormalMovementSpeedOverrideApplied = false;
	float NormalMovementBaseMaxWalkSpeed = 0.f;
	float NormalMovementBaseMaxWalkSpeedCrouched = 0.f;
	float LastAppliedNormalMaxWalkSpeed = -1.f;
	float LastAppliedNormalMaxWalkSpeedCrouched = -1.f;
	float CustomModeEntryMaxWalkSpeed = 0.f;
	float CustomModeEntryMaxWalkSpeedCrouched = 0.f;

	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UAnimationAsset>> LoadedAnimationAssets;

	float OriginalMaxWalkSpeed = 0.f;
	float OriginalMaxWalkSpeedCrouched = 0.f;
	float OriginalAcfWalkStateSpeed = 0.f;
	float OriginalAcfWalkStateSwimSpeed = 0.f;
	float OneShotEndTimeSeconds = 0.f;
	float MovementLockUntilTime = 0.f;
	float CrawlEntryPoseHoldUntilTime = 0.f;
	float AccumulatedMovingTurnDelta = 0.f;
	float NextTurnActionAllowedTime = 0.f;
	int32 CachedJumpMaxCount = 0;
	uint8 TransitionLockedCustomMovementMode = 0;
	TEnumAsByte<enum ERootMotionMode::Type> OriginalRootMotionMode;
	TEnumAsByte<enum EMovementMode> TransitionLockedMovementMode = MOVE_Walking;
	FVector LastMovementDirection = FVector::ZeroVector;
	bool bOriginalMovementStateCached = false;
	bool bOriginalAcfWalkStateCached = false;
	bool bCustomModeMovementStateCached = false;
	bool bCustomModeAcfTargetLocomotionStateCached = false;
	bool bOriginalRootMotionModeCached = false;
	bool bOriginalRotationStateCached = false;
	bool bTransitionMovementLockApplied = false;
	bool bControllerMoveInputIgnored = false;
	bool bOriginalUseControllerRotationYaw = false;
	bool bOriginalUseControllerDesiredRotation = false;
	bool bOriginalOrientRotationToMovement = false;
	bool bWalkModeJumpRestricted = false;
	bool bCachedJumpMaxCountValid = false;
	bool bWalkModeEnabled = false;
	bool bCrawlModeEnabled = false;
	bool bHoldingCrawlEntryFinalPose = false;
	bool bHadMovementDirectionLastTick = false;
	bool bWasMovingLastTick = false;
	bool bWasCrawlingLastTick = false;
	bool bCurrentAnimationLooping = false;
	bool bLoggedMissingMovementState = false;
	bool bLoggedMissingAnimationState = false;

	UPROPERTY(Transient)
	float LastLoggedAppliedTargetSpeed = -1.f;

	UPROPERTY(Transient)
	uint8 CustomModeEntryAcfTargetLocomotionStateValue = 0;

	UPROPERTY(Transient)
	bool bLoggedDependencySummary = false;

	UPROPERTY(Transient)
	FString LastDependencySummary;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> CurrentAnimationAsset;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> PendingLoopAnimationAsset;

	TWeakObjectPtr<APlayerController> MoveInputIgnoredPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveOverlayMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCrawlEntryMontage;

	UPROPERTY(Transient)
	TObjectPtr<UMeshDeformer> LoadedCrawlMeshDeformer;

	UPROPERTY(Transient)
	TObjectPtr<UMeshDeformer> PreviousActiveMeshDeformer;

	TWeakObjectPtr<USkeletalMeshComponent> CrawlDeformerMeshComponent;
	bool bCrawlMeshDeformerApplied = false;
};
