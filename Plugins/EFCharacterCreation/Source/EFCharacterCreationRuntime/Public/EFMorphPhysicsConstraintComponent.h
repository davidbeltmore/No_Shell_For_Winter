#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EFMorphPhysicsConstraintComponent.generated.h"

class UEFCharacterCustomizationComponent;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct EFCHARACTERCREATIONRUNTIME_API FEFMorphPhysicsWeightedMorph
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Morph Name"))
	FName MorphName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Weight"))
	float Weight = 1.0f;
};

USTRUCT(BlueprintType)
struct EFCHARACTERCREATIONRUNTIME_API FEFMorphPhysicsConstraintGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Group Name"))
	FName GroupName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Constraint Names", ToolTip = "Physics asset constraints that should receive the same linear limit result."))
	TArray<FName> ConstraintNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Source Morphs", ToolTip = "Explicit body morphs that contribute to this physics group."))
	TArray<FEFMorphPhysicsWeightedMorph> SourceMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.01", UIMin = "0.01", DisplayName = "Positive Threshold", ToolTip = "Positive accumulated weight required to fully reach Max Limit."))
	float PositiveThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.01", UIMin = "0.01", DisplayName = "Negative Threshold", ToolTip = "Negative accumulated weight required to fully reach Min Limit."))
	float NegativeThreshold = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Morph Dead Zone", ToolTip = "Ignores tiny morph values before they affect the accumulated sums."))
	float MorphDeadZone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Balance Dead Zone", ToolTip = "Keeps the group at Default Limit when positive and negative influence nearly cancel out."))
	float BalanceDeadZone = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Min Limit"))
	float MinLimit = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Default Limit"))
	float DefaultLimit = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Max Limit"))
	float MaxLimit = 7.0f;
};

UCLASS(ClassGroup = (Character), meta = (BlueprintSpawnableComponent, DisplayName = "EF Morph Physics Constraint Driver", ShortTooltip = "Drives selected physics constraint linear limits from explicit body morph groups."))
class EFCHARACTERCREATIONRUNTIME_API UEFMorphPhysicsConstraintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEFMorphPhysicsConstraintComponent();

	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Morph Physics")
	void RefreshFromCurrentMorphState();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Morph Physics")
	void ResetToRecommendedConstraintGroups();

	UFUNCTION(BlueprintPure, Category = "Morph Physics|Debug")
	FString GetDebugSummary() const;

protected:
	void HandleMorphStateApplied();
	void BindToCustomizationComponent();
	void UnbindFromCustomizationComponent();
	void ApplyProjectConstraintGroupOverrides();
	UEFCharacterCustomizationComponent* ResolveCustomizationComponent() const;
	USkeletalMeshComponent* ResolveTargetMeshComponent() const;
	void RebuildMorphLookup(TMap<FString, float>& OutMorphLookup, const USkeletalMeshComponent* MeshComponent) const;
	float ComputeGroupTargetLimit(const FEFMorphPhysicsConstraintGroup& Group, const TMap<FString, float>& MorphLookup, FString& OutDebugLine) const;
	bool ApplyConstraintLimit(USkeletalMeshComponent* MeshComponent, FName ConstraintName, float NewLimit) const;
	void RefreshResolvedTargetMeshDebugName();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Prefer EF Character Customization Body Mesh", ToolTip = "If enabled and the owner has UEFCharacterCustomizationComponent, this component will reuse its resolved body mesh first."))
	bool bPreferCharacterCustomizationBodyMesh = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Target Mesh Component Name", ToolTip = "Optional explicit skeletal mesh component name that owns the physics asset constraints. Leave empty to auto-resolve the body mesh."))
	FName TargetMeshComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Enable Limit Smoothing", ToolTip = "If enabled, constraint limits interpolate smoothly toward the newly computed target."))
	bool bEnableLimitSmoothing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Limit Interp Speed", ToolTip = "How fast the active constraint limit moves toward the new target. 0 applies immediately."))
	float LimitInterpSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Log Setup Warnings", ToolTip = "Logs one-time warnings when the customization component, body mesh, or constraints cannot be resolved."))
	bool bLogSetupWarnings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Morph Physics", meta = (DisplayName = "Constraint Groups"))
	TArray<FEFMorphPhysicsConstraintGroup> ConstraintGroups;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Resolved Target Mesh"))
	FName ResolvedTargetMeshDebugName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Group Debug"))
	TArray<FString> GroupDebugSummary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Debug", meta = (DisplayName = "Constraint Limit Debug"))
	TArray<FString> ConstraintLimitDebug;

private:
	TWeakObjectPtr<UEFCharacterCustomizationComponent> BoundCustomizationComponent;
	TWeakObjectPtr<USkeletalMeshComponent> TargetMeshComponent;
	TMap<FName, float> CurrentConstraintLimits;
	TMap<FName, float> TargetConstraintLimits;
	bool bHasPendingInterpolation = false;
	bool bPollMorphStateEveryTick = false;
	bool bLoggedMissingMeshWarning = false;
};
