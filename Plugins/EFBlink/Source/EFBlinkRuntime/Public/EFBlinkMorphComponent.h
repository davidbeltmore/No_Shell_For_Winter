#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFBlinkTypes.h"
#include "TimerManager.h"
#include "EFBlinkMorphComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (Character), meta = (BlueprintSpawnableComponent, DisplayName = "EF Blink Morph", ShortTooltip = "Pulses selected eye morph targets and validates the target mesh."))
class EFBLINKRUNTIME_API UEFBlinkMorphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFBlinkMorphComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	void ApplySettingsFromDefaults();

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	void SetTargetMeshComponent(USkeletalMeshComponent* NewTargetMeshComponent);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "EF|Blink")
	bool RefreshTargetMesh();

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	bool ValidateMorphTargets(TArray<FName>& OutMissingMorphTargets) const;

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	void StartBlink();

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	void StopBlink(bool bResetMorphs = true);

	UFUNCTION(BlueprintCallable, Category = "EF|Blink")
	void TriggerPulseNow();

	UFUNCTION(BlueprintPure, Category = "EF|Blink")
	bool IsReady() const;

	UFUNCTION(BlueprintPure, Category = "EF|Blink")
	bool IsPulseActive() const { return bPulseActive; }

	UFUNCTION(BlueprintPure, Category = "EF|Blink")
	bool IsBlinkRunning() const;

	UFUNCTION(BlueprintPure, Category = "EF|Blink")
	bool ShouldStartEnabled() const { return bStartEnabled; }

	UFUNCTION(BlueprintPure, Category = "EF|Blink")
	USkeletalMeshComponent* GetResolvedTargetMesh() const;

protected:
	USkeletalMeshComponent* FindMeshComponentByName(FName ComponentName) const;
	USkeletalMeshComponent* ResolveBestTargetMesh() const;
	int32 ScoreMeshComponent(const USkeletalMeshComponent* MeshComponent) const;
	int32 CountPresentMorphTargets(const USkeletalMeshComponent* MeshComponent) const;
	bool HasMorphTarget(const USkeletalMeshComponent* MeshComponent, FName MorphName) const;
	bool ShouldRevalidateResolvedMesh() const;
	float EvaluatePulseAlpha(float NormalizedTime) const;
	void ApplyMorphAlpha(float Alpha);
	void ApplyMorphsAtRest();
	void UpdateDebugState();
	void LogValidationFailureIfNeeded(const TArray<FName>& MissingMorphTargets);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh", meta = (DisplayName = "Target Mesh Component", ToolTip = "Explicit mesh component to drive. Leave empty to auto-pick the visible mesh that has the requested morph targets."))
	TObjectPtr<USkeletalMeshComponent> TargetMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh", meta = (DisplayName = "Target Mesh Component Name", ToolTip = "Optional component name to prefer before auto-picking."))
	FName TargetMeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh", meta = (DisplayName = "Auto Resolve Target Mesh"))
	bool bAutoResolveTargetMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target Mesh", meta = (DisplayName = "Preferred Mesh Name Tokens"))
	TArray<FString> PreferredMeshNameTokens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morphs", meta = (DisplayName = "Morph Targets"))
	TArray<FEFBlinkMorphTarget> MorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", UIMin = "0.1", DisplayName = "Pulse Interval Seconds"))
	float PulseIntervalSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.02", UIMin = "0.02", DisplayName = "Pulse Duration Seconds"))
	float PulseDurationSeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Initial Delay Seconds"))
	float InitialDelaySeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime", meta = (DisplayName = "Start Enabled"))
	bool bStartEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (DisplayName = "Require All Morph Targets"))
	bool bRequireAllMorphTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (DisplayName = "Revalidate On Mesh Change"))
	bool bRevalidateOnMeshChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Validation", meta = (DisplayName = "Log Validation"))
	bool bLogValidation = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Target Mesh Component"))
	FName ResolvedTargetMeshComponentName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Target Mesh Asset"))
	FString ResolvedTargetMeshAssetName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Missing Morph Targets"))
	TArray<FName> LastMissingMorphTargets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Validation Passed"))
	bool bLastValidationPassed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Last Pulse Alpha"))
	float LastPulseAlpha = 0.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ResolvedTargetMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> CachedResolvedMeshAsset = nullptr;

	FTimerHandle PulseTimerHandle;
	float PulseElapsedSeconds = 0.0f;
	bool bPulseActive = false;
	bool bLoggedMissingMorphs = false;
	bool bLoggedNoTargetMesh = false;
};
