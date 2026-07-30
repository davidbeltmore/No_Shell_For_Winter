#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/ACFItem.h"
#include "ProjectLockpickableComponent.generated.h"

class APawn;
class UACFInventoryComponent;

UENUM(BlueprintType)
enum class EProjectLockpickInteractionGateResult : uint8
{
	Consumed UMETA(DisplayName = "Consumed"),
	RunOriginal UMETA(DisplayName = "Run Original")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectLockpickSessionSignature, APawn*, Pawn, int32, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectLockpickResultSignature, APawn*, Pawn, int32, SessionId, float, PulseValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectLockpickUnlockedInteractionSignature, APawn*, Pawn, const FString&, InteractionType);

UCLASS(ClassGroup = (Lockpicking), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectLockpickableComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Lockpicking")
	bool HandleACFInteraction(APawn* Pawn, const FString& InteractionType, EProjectLockpickInteractionGateResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|ACF", meta = (DisplayName = "Consume ACF Interaction If Locked"))
	bool ConsumeACFInteractionIfLocked(APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking")
	bool HandleACFLocalInteraction(APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|ACF", meta = (DisplayName = "Consume Local ACF Interaction If Locked"))
	bool ConsumeACFLocalInteractionIfLocked(APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool CanBeInteracted(APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking|ACF")
	bool ShouldAllowACFInteraction(APawn* Pawn, bool bOriginalCanInteract = true) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool CanPawnAttemptLockpick(APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool HasRequiredLockpick(APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	int32 GetRequiredLockpickCount(APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking|Inventory")
	FText GetRequiredItemDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool IsLocked() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool IsSessionActive() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool IsSessionActiveForPawn(APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	bool IsInFailureCooldown() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	int32 GetActiveSessionId() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	float GetActiveSpeedMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	float GetActiveTargetHalfRange() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	float GetCurrentPulseValue() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	float GetCurrentPulseValueForServerTime(float ServerTimeSeconds) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	FString BuildConfirmInteractionType() const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	FString BuildBeginInteractionType() const;

	UFUNCTION(BlueprintCallable, Category = "Lockpicking")
	void SetLocked(bool bInLocked);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|ACF")
	bool ReplayOwnerACFInteraction(APawn* Pawn, const FString& InteractionType) const;

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static bool IsLockpickBeginInteractionType(const FString& InteractionType);

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static bool IsLockpickConfirmInteractionType(const FString& InteractionType);

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static bool TryParseLockpickConfirmInteractionType(const FString& InteractionType, int32& OutSessionId);

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static float ComputeSpeedMultiplier(int32 InDifficulty, int32 InCunningLevel);

	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static float ComputeTargetHalfRange(int32 InDifficulty, int32 InCunningLevel);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking")
	bool bStartsLocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Difficulty = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking", meta = (ClampMin = "0.0"))
	float FailureCooldownSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetCenter = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking")
	bool bExecuteOriginalOnSuccess = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking|ACF")
	bool bReplayOwnerACFInteractionOnSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking|Prompt")
	bool bShowPromptBeforeLockpicking = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking|Inventory")
	bool bRequireLockpickItem = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking|Inventory")
	TSoftClassPtr<UACFItem> RequiredLockpickItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lockpicking|Inventory")
	FText RequiredItemDisplayName;

	UPROPERTY(BlueprintAssignable, Category = "Lockpicking")
	FProjectLockpickSessionSignature OnLockpickStarted;

	UPROPERTY(BlueprintAssignable, Category = "Lockpicking")
	FProjectLockpickResultSignature OnLockpickSucceeded;

	UPROPERTY(BlueprintAssignable, Category = "Lockpicking")
	FProjectLockpickResultSignature OnLockpickFailed;

	UPROPERTY(BlueprintAssignable, Category = "Lockpicking")
	FProjectLockpickUnlockedInteractionSignature OnUnlockedInteractionRequested;

private:
	void StartLockpickSession(APawn* Pawn, const FString& OriginalInteractionType);
	void ConfirmLockpickSession(APawn* Pawn, int32 SessionId);
	void CompleteLockpickSuccess(APawn* Pawn, float PulseValue);
	void CompleteLockpickFailure(APawn* Pawn, float PulseValue);
	void ClearActiveSession();
	UACFInventoryComponent* ResolveInventoryComponent(APawn* Pawn) const;
	int32 ResolveCunningLevel(APawn* Pawn) const;
	float ResolveServerWorldTimeSeconds() const;
	bool HasAuthority() const;

private:
	UPROPERTY(Replicated)
	bool bLocked = true;

	UPROPERTY(Replicated)
	int32 ActiveSessionId = INDEX_NONE;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> ActivePawn;

	UPROPERTY(Replicated)
	float ActiveServerStartTimeSeconds = 0.0f;

	UPROPERTY(Replicated)
	float ActiveSpeedMultiplier = 1.0f;

	UPROPERTY(Replicated)
	float ActiveTargetHalfRange = 0.1f;

	UPROPERTY(Replicated)
	int32 ActiveDifficulty = 50;

	UPROPERTY(Replicated)
	int32 ActiveCunningLevel = 0;

	UPROPERTY(Replicated)
	float LastFailureServerTimeSeconds = -100000.0f;

	int32 LastIssuedSessionId = 0;
	FString ActiveOriginalInteractionType;
};
