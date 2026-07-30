#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "ProjectTogetherSceneTestSubsystem.generated.h"

class AAIController;
class ACharacter;
class APlayerController;
class UAnimInstance;
class UAnimMontage;
class UAnimSequenceBase;
class UBrainComponent;
class UCharacterMovementComponent;
class UInputComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedMeshComponent;

struct FProjectTogetherSceneRoleDefinition
{
	FName RoleName = NAME_None;
	FName ComponentName = NAME_None;
	TObjectPtr<USkeletalMesh> ReferenceMesh = nullptr;
	TObjectPtr<UAnimSequenceBase> Animation = nullptr;
	FTransform RelativeTransform = FTransform::Identity;
};

struct FProjectTogetherSceneDefinition
{
	FProjectTogetherSceneRoleDefinition Female;
	FProjectTogetherSceneRoleDefinition Male;
};

struct FProjectTogetherBoolSceneLockSnapshot
{
	TWeakObjectPtr<UObject> Target;
	FName PropertyName = NAME_None;
	bool bValue = false;
};

struct FProjectTogetherFloatSceneLockSnapshot
{
	TWeakObjectPtr<UObject> Target;
	FName PropertyName = NAME_None;
	double Value = 0.0;
};

struct FProjectTogetherLeaderPoseSnapshot
{
	TWeakObjectPtr<USkeletalMeshComponent> Component;
	TWeakObjectPtr<USkinnedMeshComponent> LeaderPoseComponent;
};

struct FProjectTogetherParticipantState
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
	bool bPawnInputSuspended = false;
	bool bCachedPlayerControllerState = false;
	bool bWasMoveInputIgnored = false;
	bool bWasLookInputIgnored = false;
	bool bAppliedMoveInputIgnore = false;
	bool bAppliedLookInputIgnore = false;

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
		bPawnInputSuspended = false;
		bCachedPlayerControllerState = false;
		bWasMoveInputIgnored = false;
		bWasLookInputIgnored = false;
		bAppliedMoveInputIgnore = false;
		bAppliedLookInputIgnore = false;
	}
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTogetherSceneTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UProjectTogetherSceneTestSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UFUNCTION()
	void HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void TryResolveRuntimeContext();
	void AttachToPlayerController(APlayerController* PlayerController);
	void DetachFromTrackedPlayerController(bool bStopActiveScene);
	void BindInputToTrackedPlayerController();
	void UnbindInputFromTrackedPlayerController();
	void HandleToggleScenePressed();

	bool StartTogetherScene();
	void StopTogetherScene();
	bool ResolveTargetActor(AActor*& OutTargetActor) const;
	bool LoadSceneDefinition(FProjectTogetherSceneDefinition& OutDefinition) const;
	bool ExtractRoleFromComponent(USkeletalMeshComponent* Component, FName ComponentNameHint, FProjectTogetherSceneRoleDefinition& InOutFemale, FProjectTogetherSceneRoleDefinition& InOutMale) const;
	USkeletalMeshComponent* ResolveRuntimeMesh(AActor* Actor, const FProjectTogetherSceneRoleDefinition& Role) const;
	bool ValidateRoleAgainstRuntimeMesh(const FProjectTogetherSceneRoleDefinition& Role, const USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const;

	void CacheAndFreezeParticipant(FProjectTogetherParticipantState& State, AActor* Actor, USkeletalMeshComponent* Mesh, APlayerController* PlayerController);
	void RestoreParticipant(FProjectTogetherParticipantState& State, APlayerController* PlayerController);
	void ApplyParticipantTransform(const FProjectTogetherParticipantState& State, const FProjectTogetherSceneRoleDefinition& Role, const FTransform& SceneAnchorTransform) const;
	UAnimMontage* PlayRoleMontage(const FProjectTogetherSceneRoleDefinition& Role, USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const;
	void StopRoleMontage(FProjectTogetherParticipantState& State, TObjectPtr<UAnimMontage>& Montage) const;

	void ApplyAnimSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMesh);
	void ApplyAnimInstanceSceneLock(UObject* AnimInstanceObject);
	void ApplyVisibleMeshLeaderPoseSceneLock(AActor* Actor, USkeletalMeshComponent* SourceMesh);
	void RestoreAnimSceneLock();
	void CacheAndSetBoolSceneLockProperty(UObject* Target, FName PropertyName, bool bNewValue);
	void CacheAndSetFloatSceneLockProperty(UObject* Target, FName PropertyName, double NewValue);

private:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> TrackedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TrackedPlayerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> TrackedInputComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActivePlayerMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveEnemyMontage;

	FSoftObjectPath SceneBlueprintPath;
	FProjectTogetherParticipantState PlayerParticipantState;
	FProjectTogetherParticipantState EnemyParticipantState;
	TArray<FProjectTogetherBoolSceneLockSnapshot> BoolSceneLockSnapshots;
	TArray<FProjectTogetherFloatSceneLockSnapshot> FloatSceneLockSnapshots;
	TArray<FProjectTogetherLeaderPoseSnapshot> LeaderPoseSnapshots;
	bool bSceneActive = false;
};
