#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimationAsset.h"
#include "TimerManager.h"
#include "Locomotion/ProjectEmoteTypes.h"
#include "ProjectEmoteComponent.generated.h"

class ACharacter;
class ACameraActor;
class AController;
class AHUD;
class APlayerController;
class UClass;
class UActorComponent;
class UAnimInstance;
class UAnimMontage;
class UAnimSequenceBase;
class UBrainComponent;
class UCharacterMovementComponent;
class UDamageType;
class UProjectCombatAttributeComponent;
class UProjectEmoteMenuDataAsset;
class UProjectLocomotionOverrideComponent;
class UPrimitiveComponent;
class USkeletalMesh;
class USkinnedMeshComponent;
class USkeletalMeshComponent;

struct FProjectEmoteBoolSceneLockSnapshot
{
	TWeakObjectPtr<UObject> Target;
	FName PropertyName = NAME_None;
	bool bValue = false;
};

struct FProjectEmoteFloatSceneLockSnapshot
{
	TWeakObjectPtr<UObject> Target;
	FName PropertyName = NAME_None;
	double Value = 0.0;
};

struct FProjectEmoteLeaderPoseSceneLockSnapshot
{
	TWeakObjectPtr<USkeletalMeshComponent> Component;
	TWeakObjectPtr<USkinnedMeshComponent> LeaderPoseComponent;
};

struct FProjectEmoteBlueprintSceneRoleDefinition
{
	FName RoleName = NAME_None;
	FName ComponentName = NAME_None;
	TObjectPtr<USkeletalMesh> ReferenceMesh = nullptr;
	TObjectPtr<UAnimSequenceBase> Animation = nullptr;
	FTransform RelativeTransform = FTransform::Identity;
};

struct FProjectEmoteBlueprintSceneDefinition
{
	TObjectPtr<UClass> SceneClass = nullptr;
	FSoftObjectPath SceneBlueprintPath;
	FProjectEmoteBlueprintSceneRoleDefinition PrimaryRole;
	FProjectEmoteBlueprintSceneRoleDefinition TargetRole;
};

struct FProjectEmoteParticipantSceneState
{
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<UAnimInstance> AnimInstance;
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;
	TWeakObjectPtr<UBrainComponent> BrainComponent;
	FTransform CachedActorTransform = FTransform::Identity;
	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	bool bHadMovementComponent = false;
	bool bHadBrainComponent = false;
	bool bBrainWasPaused = false;

	void Reset()
	{
		Actor.Reset();
		Character.Reset();
		Mesh.Reset();
		AnimInstance.Reset();
		MovementComponent.Reset();
		BrainComponent.Reset();
		CachedActorTransform = FTransform::Identity;
		CachedMovementMode = MOVE_Walking;
		CachedCustomMovementMode = 0;
		bHadMovementComponent = false;
		bHadBrainComponent = false;
		bBrainWasPaused = false;
	}
};

struct FProjectEmoteEquipmentVisibilitySnapshot
{
	TWeakObjectPtr<AActor> Actor;
	bool bWasHiddenInGame = false;
	bool bWasCollisionEnabled = true;
};

struct FProjectEmoteEquipmentPrimitiveVisibilitySnapshot
{
	TWeakObjectPtr<UPrimitiveComponent> Component;
	bool bWasVisible = true;
	bool bWasHiddenInGame = false;
};

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectEmoteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectEmoteComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	bool StartEmote(EProjectEmoteType EmoteType);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	bool StartInteractionById(FName InteractionId);

	UFUNCTION(BlueprintCallable, Category = "Project|Emote|Runtime Action")
	bool StartRuntimeInteractionById(FName InteractionId);

	/**
	 * Starts the single-player presentation selected by an authoritative mature-defeat payload.
	 * This deliberately bypasses the voluntary Charisma gate while still honoring Streamer Safe.
	 */
	bool StartMatureDefeatPresentationById(FName InteractionId);

	/** Verifies the approved definition and its cooked presentation asset before the authority roll. */
	bool IsMatureDefeatPresentationAvailableById(FName InteractionId) const;

	UFUNCTION(BlueprintCallable, Category = "Project|Emote")
	void StopEmote(bool bRestoreTargetActor = true);

	void OverrideDelayedPostEmoteRecovery(float DelaySeconds, bool bMoveInputIgnored, bool bLookInputIgnored);

	void GetMenuInteractions(EProjectEmoteMenuCategory Category, TArray<FProjectEmoteInteractionDefinition>& OutInteractions) const;
	bool FindInteractionDefinition(FName InteractionId, FProjectEmoteInteractionDefinition& OutDefinition) const;
	void GetRootMenuNodes(TArray<FProjectEmoteMenuNodeDefinition>& OutNodes) const;
	void GetChildMenuNodes(FName ParentNodeId, TArray<FProjectEmoteMenuNodeDefinition>& OutNodes) const;
	bool FindMenuNodeDefinition(FName NodeId, FProjectEmoteMenuNodeDefinition& OutNode) const;
	bool FindActiveInteractionDefinition(FProjectEmoteInteractionDefinition& OutDefinition) const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmoteActive() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmoteTransitionPending() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsEmotePlaybackStarted() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	bool IsActiveInteractionBlueprintScene() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Debug")
	AActor* GetActiveBlueprintSceneVisualActor() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote|Debug")
	AActor* GetActiveBlueprintSceneTargetActor() const;

	/** Returns the actor preserved by the T-targeting bridge before an interaction starts. */
	UFUNCTION(BlueprintPure, Category = "Project|Emote|Intimacy")
	AActor* GetCurrentInteractionTargetActor() const;

	UFUNCTION(BlueprintCallable, Category = "Project|Emote|Intimacy")
	bool TriggerBlueprintSceneVisualSessionPeakCue();

#if WITH_DEV_AUTOMATION_TESTS
	void AutomationApplyBlueprintSceneEquipmentSuppressionForTest(AActor* PlayerActor, AActor* PartnerActor);
	void AutomationRestoreBlueprintSceneEquipmentSuppressionForTest();
#endif
	UFUNCTION(BlueprintPure, Category = "Project|Emote|Automation")
	int32 AutomationGetSuppressedBlueprintSceneEquipmentCount() const;

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Project|Emote|Debug")
	void SetDebugBlueprintSceneTargetActor(AActor* TargetActor);
#endif

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	EProjectEmoteType GetActiveEmote() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	int32 GetActiveEmoteValue() const;

	UFUNCTION(BlueprintPure, Category = "Project|Emote")
	FName GetActiveInteractionId() const;

	bool IsCombatLockoutActive(float LockoutSeconds) const;
	void SetFreeCameraForwardInput(float Value);
	void SetFreeCameraRightInput(float Value);
	void SetFreeCameraVerticalInput(float Value);
	void SetFreeCameraBoostActive(bool bActive);
	void ClearFreeCameraMoveInput();
	void RefreshMenuCatalog();

private:
	void TryLoadDefaultMenuDataAsset();
	void ApplyMenuDataAssetRuntimeSettings();
	void InitializeDefaultInteractionCatalog();
	void RebuildMenuNodeCache();
	void BuildDefaultInteractionCatalog();
	bool IsGeneratedDefaultInteractionCatalog() const;
	void BuildDefaultMenuNodes();
	void BuildMenuNodesFromLegacyInteractions();
	void AppendTrainingMenuNodes();
	void AddMenuNode(const FProjectEmoteMenuNodeDefinition& Node);
	void AddFolderMenuNode(FName NodeId, FName ParentNodeId, const FText& DisplayName, int32 SortOrder);
	void AddCancelMenuNode(FName NodeId, FName ParentNodeId, const FText& DisplayName, int32 SortOrder);
	void AddActionMenuNodeFromInteraction(const FProjectEmoteInteractionDefinition& Interaction, FName ParentNodeId, FName OverrideNodeId = NAME_None, FText OverrideDisplayName = FText(), int32 OverrideSortOrder = INDEX_NONE);
	void AddBlueprintSceneMenuNode(FName NodeId, FName ParentNodeId, const FText& DisplayName, const FSoftObjectPath& BlueprintPath, int32 SortOrder, const FProjectEmoteRootOffsetSettings& RootOffset = FProjectEmoteRootOffsetSettings());
	bool ConvertMenuNodeToInteraction(const FProjectEmoteMenuNodeDefinition& Node, FProjectEmoteInteractionDefinition& OutDefinition) const;
	const FProjectEmoteMenuNodeDefinition* FindMenuNodeById(FName NodeId) const;
	const FProjectEmoteMenuNodeDefinition* FindFirstAvailableActionDescendant(FName ParentNodeId) const;
	void CollectActionDescendants(FName ParentNodeId, TArray<FProjectEmoteInteractionDefinition>& OutInteractions) const;
	bool IsMenuNodeCurrentlyAvailable(const FProjectEmoteMenuNodeDefinition& Node) const;
	bool StartInteraction(
		const FProjectEmoteInteractionDefinition& Definition,
		bool bBypassCombatLockout = false,
		bool bMatureDefeatPresentation = false);
	const FProjectEmoteInteractionDefinition* FindInteractionById(FName InteractionId) const;
	const FProjectEmoteInteractionDefinition* FindInteractionByLegacyType(EProjectEmoteType EmoteType) const;
	bool IsInteractionCurrentlyAvailable(
		const FProjectEmoteInteractionDefinition& Definition,
		bool bMatureDefeatPresentation = false) const;
	AActor* ResolveCurrentTargetActor() const;
	bool DoesRequirementMatchAnyArmorSlot(const FProjectEmoteEquipmentRequirement& Requirement) const;
	bool IsArmorSlotComponent(const UActorComponent* Component) const;
	bool DoesSlotComponentMatchRequirement(const UActorComponent* ArmorSlotComponent, const FProjectEmoteEquipmentRequirement& Requirement) const;
	bool DoesObjectValueMatchRequirement(const UObject* CandidateObject, const FProjectEmoteEquipmentRequirement& Requirement) const;
	bool DoesCandidateValueMatchRequirement(const FString& CandidateValue, const FProjectEmoteEquipmentRequirement& Requirement) const;

	void ResolveDependencies();
	APlayerController* ResolveOwningPlayerController() const;
	UAnimInstance* ResolveAnimInstance() const;
	UAnimationAsset* ResolveAnimationForInteraction(const FProjectEmoteInteractionDefinition& Definition);
	UAnimationAsset* LoadAnimationAsset(const TSoftObjectPtr<UAnimationAsset>& AssetReference);
	void SuspendLocomotionOverride();
	void RestoreLocomotionOverride();
	void BindDamageCancellationSources();
	void UnbindDamageCancellationSources();
	void RecordCombatImpact();
	void CancelEmoteForDamage(float AppliedDamage);
	void ApplyRootOffset(const FProjectEmoteInteractionDefinition& Definition);
	void RestoreRootOffset();
	void ApplyPreActionSettle(APlayerController* PlayerController);
	void RestorePreActionSettle(APlayerController* PlayerController);
	void ApplyHudSuppression(APlayerController* PlayerController);
	void RestoreHudSuppression();
	bool TrySetReflectedHudEnabled(AHUD* HudActor, bool bEnabled) const;
	void ApplyMinimalAnimSceneLock();
	void RestoreMinimalAnimSceneLock();
	void ApplyAnimSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMeshComponent);
	void ApplyBlueprintSceneEquipmentSuppression(AActor* PlayerActor, AActor* PartnerActor);
	void RefreshBlueprintSceneEquipmentSuppression();
	void RestoreBlueprintSceneEquipmentSuppression();
	void ApplyAnimInstanceSceneLock(UObject* AnimInstanceObject);
	void ApplyVisibleMeshLeaderPoseSceneLock();
	void ApplyVisibleMeshLeaderPoseSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMeshComponent);
	void RestoreVisibleMeshLeaderPoseSceneLock();
	void CacheAndSetBoolSceneLockProperty(UObject* Target, FName PropertyName, bool bNewValue);
	void CacheAndSetFloatSceneLockProperty(UObject* Target, FName PropertyName, double NewValue);
	bool PrepareBlueprintSceneInteraction(const FProjectEmoteInteractionDefinition& Definition, FProjectEmoteBlueprintSceneDefinition& OutSceneDefinition, AActor*& OutTargetActor, USkeletalMeshComponent*& OutTargetMesh);
	bool LoadBlueprintSceneDefinition(const FProjectEmoteBlueprintSceneSettings& Settings, FProjectEmoteBlueprintSceneDefinition& OutSceneDefinition) const;
	bool ExtractBlueprintSceneRoleFromComponent(USkeletalMeshComponent* Component, FName ComponentNameHint, const FProjectEmoteBlueprintSceneSettings& Settings, FProjectEmoteBlueprintSceneDefinition& InOutSceneDefinition) const;
	USkeletalMeshComponent* ResolveRuntimeMeshForBlueprintSceneRole(AActor* Actor, const FProjectEmoteBlueprintSceneRoleDefinition& Role) const;
	bool ValidateBlueprintSceneRoleAgainstRuntimeMesh(const FProjectEmoteBlueprintSceneRoleDefinition& Role, const USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const;
	void CacheAndFreezeTargetParticipant(FProjectEmoteParticipantSceneState& State, AActor* Actor, USkeletalMeshComponent* Mesh);
	void RestoreTargetParticipant(FProjectEmoteParticipantSceneState& State, const FTransform* OverrideActorTransform = nullptr);
	void ApplyBlueprintSceneParticipantTransform(AActor* Actor, USkeletalMeshComponent* Mesh, const FProjectEmoteBlueprintSceneRoleDefinition& Role, const FTransform& SceneAnchorTransform) const;
	UAnimMontage* PlayBlueprintSceneRoleMontage(const FProjectEmoteBlueprintSceneRoleDefinition& Role, USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const;
	void StopTargetBlueprintSceneMontage(float BlendOutTime);
	bool BuildBlueprintSceneTargetEndTransform(FTransform& OutTransform) const;
	AActor* SpawnBlueprintSceneVisualActor(const FProjectEmoteBlueprintSceneDefinition& SceneDefinition, const FTransform& SceneAnchorTransform);
	void ConfigureBlueprintSceneVisualActor(AActor* VisualActor, const FProjectEmoteBlueprintSceneDefinition& SceneDefinition) const;
	void PrepareBlueprintSceneVisualRoleMesh(USkeletalMeshComponent* MeshComponent) const;
	USkeletalMeshComponent* ResolveVisualSceneRoleMesh(AActor* VisualActor, const FProjectEmoteBlueprintSceneRoleDefinition& Role) const;
	void StartBlueprintSceneVisualPlayback();
	void PlayBlueprintSceneVisualRoleAnimation(const FProjectEmoteBlueprintSceneRoleDefinition& Role, USkeletalMeshComponent* VisualMesh, const TCHAR* RuntimeLabel) const;
	void ActivateBlueprintSceneVisualNiagara() const;
	void DestroyBlueprintSceneVisualActor();
	void RestoreBlueprintSceneState();
	void StartFreeCamera(APlayerController* PlayerController);
	void StopFreeCamera(APlayerController* PlayerController);
	AActor* ResolvePostEmoteViewTarget(APlayerController* PlayerController) const;
	void ScheduleDeferredViewTargetRestore(APlayerController* PlayerController);
	void RestorePostEmoteViewTarget();
	void UpdateFreeCamera(float DeltaTime);
	void HandleDeferredEmotePlaybackStart();
	void RestorePreEmoteState();
	void ScheduleDelayedPostEmoteRecovery();
	void ScheduleDelayedPostEmoteRecovery(float DelaySeconds, bool bMoveInputIgnored, bool bLookInputIgnored);
	void HandleDelayedPostEmoteRecovery();
	bool ShouldSkipDelayedPostEmoteRecovery() const;
	void StopActiveMontage(float BlendOutTime);
	void HandleActiveMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void PlayFadeToBlack() const;
	void PlayFadeFromBlack() const;

	UFUNCTION()
	void HandleOwnerAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	UFUNCTION()
	void HandleProjectDamageApplied(AActor* SourceActor, FName DamageType, float RequestedDamage, float AppliedDamage, float RemainingValue, bool bKilledTarget);

	UFUNCTION()
	void HandleProjectCombatAttributeChanged(FName AttributeName, float OldValue, float NewValue, float MaxValue);

private:
	UPROPERTY(EditAnywhere, Category = "Project|Emote|Catalog")
	TObjectPtr<UProjectEmoteMenuDataAsset> MenuDataAsset;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Catalog")
	TArray<FProjectEmoteInteractionDefinition> ActionInteractions;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Catalog")
	TArray<FProjectEmoteInteractionDefinition> ObjectInteractions;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Overlay")
	FName OverlaySlotName = TEXT("DefaultSlot");

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendInTime = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Overlay", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LoopBlendOutTime = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FadeToBlackDuration = 0.16f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float BlackScreenHoldDuration = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FadeFromBlackDuration = 0.22f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Presentation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PostEmoteRecoveryDelaySeconds = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Presentation")
	bool bSuppressHudDuringEmote = true;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Pose")
	bool bApplyPreActionSettle = true;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Pose")
	bool bApplyMinimalAnimSceneLock = true;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera")
	bool bUseFreeCameraDuringEmote = true;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FreeCameraBlendTime = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float FreeCameraMoveSpeed = 560.0f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float FreeCameraBoostMoveSpeed = 1470.0f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "0.001", UIMin = "0.001"))
	float FreeCameraMouseSensitivity = 0.2808f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float FreeCameraMinPitch = -89.0f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float FreeCameraMaxPitch = 89.0f;

	UPROPERTY(EditAnywhere, Category = "Project|Emote|Combat", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CombatMenuLockoutSeconds = 8.0f;

	TWeakObjectPtr<ACharacter> CachedCharacterOwner;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovementComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	TWeakObjectPtr<UAnimInstance> ActiveAnimInstance;
	TMap<FSoftObjectPath, TObjectPtr<UAnimationAsset>> LoadedAnimationAssets;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveOverlayMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveTargetOverlayMontage;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveBlueprintSceneVisualActor;

#if WITH_EDITOR
	TWeakObjectPtr<AActor> DebugBlueprintSceneTargetActor;
#endif

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ActiveFreeCameraActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SavedViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectCombatAttributeComponent> BoundCombatAttributeComponent;

	TWeakObjectPtr<UProjectLocomotionOverrideComponent> SuspendedLocomotionOverrideComponent;
	TWeakObjectPtr<AActor> TargetingActorToRestore;
	FTimerHandle DeferredEmoteStartTimerHandle;
	FTimerHandle DelayedPostEmoteRecoveryTimerHandle;
	FTimerHandle DeferredViewTargetRestoreTimerHandle;

	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	TEnumAsByte<EMovementMode> DelayedRecoveryMovementMode = MOVE_Walking;
	uint8 DelayedRecoveryCustomMovementMode = 0;
	bool bCachedControllerMoveInputIgnored = false;
	bool bCachedControllerLookInputIgnored = false;
	bool bDelayedRecoveryMoveInputIgnored = false;
	bool bDelayedRecoveryLookInputIgnored = false;
	bool bHasDelayedRecoveryState = false;
	bool bAppliedMoveInputIgnore = false;
	bool bForcedLookInputEnable = false;
	bool bAppliedLookInputIgnore = false;
	bool bPawnInputSuspended = false;
	bool bPreEmoteStateCached = false;
	bool bRootOffsetApplied = false;
	bool bCachedControlRotation = false;
	bool bAppliedNeutralControlRotation = false;
	bool bHudSuppressionApplied = false;
	bool bHasSavedPlayerHudVisibility = false;
	bool bWasPlayerHudVisible = true;
	bool bAppliedReflectedHudDisable = false;
	bool bLocomotionStateCached = false;
	bool bLocomotionWalkWasEnabled = false;
	bool bLocomotionCrawlWasEnabled = false;
	bool bMinimalAnimSceneLockApplied = false;
	bool bEmoteTransitionPending = false;
	bool bUsingGeneratedDefaultCatalog = false;
	bool bBlueprintSceneActive = false;
	bool bBlueprintScenePlayerTransformCached = false;
	float FreeCameraForwardInput = 0.0f;
	float FreeCameraRightInput = 0.0f;
	float FreeCameraVerticalInput = 0.0f;
	bool bFreeCameraBoostActive = false;
	float LastCombatImpactTimeSeconds = -FLT_MAX;
	FTransform CachedRootOffsetActorTransform = FTransform::Identity;
	FTransform CachedBlueprintScenePlayerTransform = FTransform::Identity;
	FRotator CachedControlRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AHUD> SuppressedHudActor;
	TArray<FProjectEmoteBoolSceneLockSnapshot> BoolSceneLockSnapshots;
	TArray<FProjectEmoteFloatSceneLockSnapshot> FloatSceneLockSnapshots;
	TArray<FProjectEmoteLeaderPoseSceneLockSnapshot> LeaderPoseSceneLockSnapshots;
	TArray<FProjectEmoteEquipmentVisibilitySnapshot> BlueprintSceneEquipmentSnapshots;
	TArray<FProjectEmoteEquipmentPrimitiveVisibilitySnapshot> BlueprintSceneEquipmentPrimitiveSnapshots;
	TArray<FProjectEmoteMenuNodeDefinition> CachedMenuNodes;
	TMap<FName, int32> CachedMenuNodeIndexById;
	mutable FProjectEmoteInteractionDefinition InteractionLookupScratch;
	FProjectEmoteBlueprintSceneDefinition ActiveBlueprintSceneDefinition;
	FProjectEmoteParticipantSceneState TargetParticipantState;

	FName ActiveInteractionId = NAME_None;
	FProjectEmoteInteractionDefinition ActiveInteractionDefinition;
	EProjectEmotePlaybackMode ActivePlaybackMode = EProjectEmotePlaybackMode::Looping;
	TSoftObjectPtr<UAnimationAsset> ActiveAnimation;

	UPROPERTY(Transient)
	EProjectEmoteType ActiveEmote = EProjectEmoteType::None;
};
