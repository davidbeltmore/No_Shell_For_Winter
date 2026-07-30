#pragma once

#include "Components/ActorComponent.h"
#include "DirtyPawnComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UTexture;
class AActor;

UENUM(BlueprintType)
enum class EDirtyPawnPrewarmState : uint8
{
	NotStarted,
	Building,
	Ready,
	Failed
};

UENUM(BlueprintType)
enum class EDirtyPawnPaintState : uint8
{
	None,
	Wet,
	Mud,
	Sand,
	Snow,
	Blood,
	Smear,
	Dirt,
	Burn,
	Sweat
};

USTRUCT(BlueprintType)
struct FDirtyPawnPaintBand
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	EDirtyPawnPaintState State = EDirtyPawnPaintState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float MinHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float MaxHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float Alpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float TargetAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeDuration = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeStartAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeElapsedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float LastTargetAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float TimeSinceTouched = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float AutoFadeDelaySeconds = -1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float AutoFadeOutSeconds = -1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bAutoExpires = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bSpawnSnowMeltWetOnExpire = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bAutoExpiredByTimer = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDirtyPawnStateChangedSignature);

USTRUCT()
struct FDirtyPawnPendingWetBand
{
	GENERATED_BODY()

	UPROPERTY()
	float MinHeight = 0.0f;

	UPROPERTY()
	float MaxHeight = 0.0f;

	UPROPERTY()
	float Strength = 1.0f;

	UPROPERTY()
	float ElapsedSeconds = 0.0f;

	UPROPERTY()
	float MaxWaitSeconds = 0.75f;

	UPROPERTY()
	bool bWashBlood = true;

	UPROPERTY()
	bool bWashSmears = true;

	UPROPERTY()
	bool bWashSandSnow = true;
};

USTRUCT()
struct FDirtyPawnPendingWashBand
{
	GENERATED_BODY()

	UPROPERTY()
	float MinHeight = 0.0f;

	UPROPERTY()
	float MaxHeight = 0.0f;

	UPROPERTY()
	float ElapsedSeconds = 0.0f;

	UPROPERTY()
	float CoalesceSeconds = 0.16f;

	UPROPERTY()
	bool bWashBlood = true;

	UPROPERTY()
	bool bWashSmears = true;

	UPROPERTY()
	bool bWashSandSnow = true;
};

USTRUCT(BlueprintType)
struct FDirtyPawnWashBand
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float MinHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float MaxHeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float Alpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float TargetAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeDuration = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeStartAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float FadeElapsedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float LastTargetAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bCommitted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bWashBlood = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bWashSmears = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bWashSandSnow = true;
};

USTRUCT(BlueprintType)
struct FDirtyPawnMaterialBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	TObjectPtr<USkeletalMeshComponent> MeshComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	int32 MaterialIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	FName MaterialSlotName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	TObjectPtr<UMaterialInterface> OriginalMaterial = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bFabric = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bUseRotatedSweatUV = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float LocalActorBottom = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float LocalActorTop = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	float LocalActorHeight = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn")
	bool bHasStableLocalFrame = false;
};

UCLASS(ClassGroup = (DirtyPawn), meta = (BlueprintSpawnableComponent))
class DIRTYPAWNRUNTIME_API UDirtyPawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDirtyPawnComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn")
	void PreinitializeDirtyPawn();

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn")
	void RebuildDirtyPawnMaterials();

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void WaterOverlapEvent(AActor* WaterActorReference, float NodeHeight, bool bIsMud = false, bool bIsBleach = false);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void WaterOverlapBand(AActor* WaterActorReference, float NodeMinHeight, float NodeMaxHeight, bool bIsMud = false, bool bIsBleach = false);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void EndWaterOverlap(AActor* WaterActorReference);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void DynamicMaterialTick(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void InteriorCheck();

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SmearEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SmearBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void BloodEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void BloodBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void MudBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SandEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SandBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SnowEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SnowBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void DirtEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void DirtBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void BurnEvent(float NodeHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void BurnBandEvent(float NodeMinHeight, float NodeMaxHeight, float Strength = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void FadeOutWashMudEvent(float NodeHeight);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void FadeOutWashMudBandEvent(float NodeMinHeight, float NodeMaxHeight);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SetFadeWashVariables(float NodeHeight, bool bWashBlood = true, bool bWashSmears = true, bool bWashSandSnow = true);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SetFadeWashVariablesBand(float NodeMinHeight, float NodeMaxHeight, bool bWashBlood = true, bool bWashSmears = true, bool bWashSandSnow = true);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SetFadeSandSnowVariables(float NodeHeight, bool bApplySand, bool bApplySnow);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SetFadeSandSnowVariablesBand(float NodeMinHeight, float NodeMaxHeight, bool bApplySand, bool bApplySnow);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void SetFadeSandSnowVariables_Instant(float NodeHeight, bool bApplySand, bool bApplySnow);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Events")
	void ResetDirtyPawn();

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn")
	bool IsDirtyPawnReady() const { return PrewarmState == EDirtyPawnPrewarmState::Ready; }

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn")
	int32 GetDirtyPawnMaterialBindingCount() const { return MaterialBindings.Num(); }

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn")
	int32 GetLiveDirtyPawnMaterialBindingCount() const;

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Tattoo")
	UMaterialInstanceDynamic* ApplyTattooToBoundSkinMaterials(UTexture* TattooTexture, FLinearColor TattooColor, float Opacity, float OffsetU, float OffsetV, float Scale, float ScaleY);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Tattoo")
	void ClearTattooFromBoundSkinMaterials();

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Tattoo")
	UMaterialInstanceDynamic* GetFirstTattooBoundSkinMID() const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|State")
	float GetMaxPaintAlphaForState(EDirtyPawnPaintState State) const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|State")
	float GetMaxVisiblePaintAlphaForState(EDirtyPawnPaintState State) const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|State")
	bool HasActivePaintState(EDirtyPawnPaintState State, float MinAlpha = 0.05f) const;

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Sweat")
	void SetSweatPoints(float NewSweatPoints);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Sweat")
	void AddSweatPoints(float DeltaSweatPoints);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Sweat")
	void ClearSweat();

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Sweat")
	float GetSweatNormalizedValue() const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Sweat")
	float GetSweatVisualOpacity() const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Sweat")
	float GetSweatRoughnessAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Sweat")
	bool IsSweaty() const;

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn")
	static UDirtyPawnComponent* FindCanonicalDirtyPawnComponent(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Dirty Pawn|Debug")
	float ResolveBodyLocalHeightFromWorldZ(float WorldZ, bool bApplyCrouchSubmergeBonus = true, bool bForceCrouchedForDebug = false);

	UFUNCTION(BlueprintPure, Category = "Dirty Pawn|Debug")
	float GetDirtyPawnBodyReferenceHeight() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Setup")
	bool bAutoInitializeOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Setup")
	bool bUseMaterialWrapper = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Setup")
	TSoftObjectPtr<UMaterialInterface> SkinWrapperMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Setup")
	TArray<FString> IncludeMaterialTokens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Setup")
	TArray<FString> ExcludeMaterialTokens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Debug")
	bool bLogSetupWarnings = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Debug")
	EDirtyPawnPrewarmState PrewarmState = EDirtyPawnPrewarmState::NotStarted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Debug")
	TArray<FDirtyPawnMaterialBinding> MaterialBindings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Debug")
	float DirtyPawnActorBottom = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Debug")
	float DirtyPawnActorTop = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Debug")
	float DirtyPawnActorHeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float OverallFadeWetness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float OverallFadeWetness_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float WetnessFadeSpeed = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float WetHeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	float StandingHeadHeight = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	float CrouchHeadHeight = 155.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands")
	float EnvironmentalFadeInSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands")
	float EnvironmentalFadeOutSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands")
	float EnvironmentalAutoFadeDelaySeconds = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands")
	float EnvironmentalAutoFadeDurationSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnvironmentalDirtCompanionStrength = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SnowMeltWetStrength = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands", meta = (ClampMin = "0.01"))
	float SnowMeltWetFadeInSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands", meta = (ClampMin = "0.0"))
	float SnowMeltWetHoldSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands", meta = (ClampMin = "0.01"))
	float SnowMeltWetFadeOutSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Bands")
	float MinimumPaintBandHeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashMaterialFadeSeconds = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashWetDelayMaxSeconds = 1.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashWetStartDirtyAlpha = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashWetFadeInSeconds = 0.90f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashWetFadeOutSeconds = 1.60f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float CleanWaterWetFadeInSeconds = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashContactCoalesceSeconds = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float WashLateJoinMaxElapsedSeconds = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float UnifiedWashCommitThreshold = 0.985f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime|Wash")
	float UnifiedWashFadeOutSeconds = 0.25f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Runtime|Bands")
	TArray<FDirtyPawnPaintBand> EnvironmentalPaintBands;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Runtime|Bands")
	TArray<FDirtyPawnPaintBand> WetPaintBands;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Runtime|Bands")
	TArray<FDirtyPawnPaintBand> StainPaintBands;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Runtime|Bands")
	TArray<FDirtyPawnWashBand> WashPaintBands;

	UPROPERTY(BlueprintAssignable, Category = "Dirty Pawn|State")
	FDirtyPawnStateChangedSignature OnDirtyPawnStateChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L1_Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L1_WaterHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L1_WaterHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L1_Wetness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L2_Height = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L2_WaterHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L2_WaterHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Wet")
	float L2_Wetness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	bool InMud = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudWashSpeed = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudFadeWashSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Mud")
	float MudOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float OverallFadeBlood = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float OverallFadeBlood_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float BloodHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float BloodHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float BloodWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float BloodWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	float BloodFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Blood")
	bool CanWashBloodOffFabric = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float OverallFadeSmear = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float OverallFadeSmear_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float SmearHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float SmearHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float SmearWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float SmearWashHeightTarget = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Smear")
	float SmearFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float OverallFadeSand = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float OverallFadeSand_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sand")
	float SandWashSpeed = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float OverallFadeSnow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float OverallFadeSnow_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Snow")
	float SnowWashSpeed = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float OverallFadeDirt = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float OverallFadeDirt_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float DirtHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float DirtHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float DirtWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float DirtWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Dirt")
	float DirtFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float OverallFadeBurn = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float OverallFadeBurn_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float BurnHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float BurnHeight_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float BurnWashHeight = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float BurnWashHeight_Target = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Burn")
	float BurnFadeSpeed = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatPoints = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.001"))
	float SweatMaxPoints = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatMovementGainPerSecond = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatIntimacyGainPerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatDecayDelaySeconds = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatDecayPerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SweatPersistenceThreshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SweatPersistentOpacityFloor = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatRunningSpeedThreshold = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.001"))
	float SweatMapScale = 4.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Sweat", meta = (ClampMin = "0.0"))
	float SweatMapStrength = 1.35f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Sweat")
	float SweatIdleSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Sweat")
	bool bSweatPersistentFloorActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dirty Pawn|Sweat")
	bool bSweatyStateActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	bool InWater = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	bool CurrentlyWashing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	bool WashResetting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Runtime")
	bool FadeTransitioning = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairWetness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairWetness_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairMud = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairMud_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairSmear = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	float HairSmear_Target = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dirty Pawn|Hair")
	bool HairIsSubmerged = false;

protected:
	void UpdateActorHeightFrame();
	bool ShouldAffectMaterial(USkeletalMeshComponent* MeshComponent, int32 MaterialIndex, UMaterialInterface* Material) const;
	void CopyTextureParameter(UMaterialInterface* SourceMaterial, UMaterialInstanceDynamic* TargetMaterial, FName ParameterName) const;
	void PushAllParameters();
	void PushScalar(FName ParameterName, float Value);
	void PushBindingLocalHeightFrame(FDirtyPawnMaterialBinding& Binding) const;
	void EnsureStableBindingLocalHeightFrame(FDirtyPawnMaterialBinding& Binding) const;
	void MarkParametersDirty();
	bool InterpScalar(float& Current, float Target, float DeltaSeconds, float Speed) const;
	void ApplyWashHeight(float NodeHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow);
	void ApplyWashBand(float NodeMinHeight, float NodeMaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow, float FadeDuration = -1.0f, float SyncedFadeElapsedSeconds = -1.0f);
	void ApplyEnvironmentalBand(AActor* SourceActor, EDirtyPawnPaintState State, float NodeMinHeight, float NodeMaxHeight, float Strength, bool bInstant = false);
	void ApplyStainBand(EDirtyPawnPaintState State, float NodeMinHeight, float NodeMaxHeight, float Strength);
	void ApplyWetBand(float NodeMinHeight, float NodeMaxHeight, float Strength, float FadeDuration = 0.35f);
	void ApplyTemporaryWetBand(float MinHeight, float MaxHeight, float Strength, float FadeInSeconds, float HoldSeconds, float FadeOutSeconds);
	bool NormalizePaintBand(float NodeMinHeight, float NodeMaxHeight, float& OutMinHeight, float& OutMaxHeight) const;
	void CommitEnvironmentalBand(EDirtyPawnPaintState State, float MinHeight, float MaxHeight, float Strength);
	FDirtyPawnPaintBand* AddOrRefreshBand(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState State, float MinHeight, float MaxHeight, float Strength, float FadeDuration, bool bAutoExpires, float AutoFadeDelaySeconds = -1.0f, float AutoFadeOutSeconds = -1.0f, bool bSpawnSnowMeltWetOnExpire = false);
	void FadeOutBandRange(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState StateFilter, float MinHeight, float MaxHeight, float FadeDuration, float SyncedFadeElapsedSeconds = -1.0f);
	void QueueTwoPhaseWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow);
	void BeginTwoPhaseWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow);
	void QueuePendingWetBand(float MinHeight, float MaxHeight, float Strength, bool bWashBlood, bool bWashSmears, bool bWashSandSnow);
	void QueueUnifiedWashBand(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow);
	bool UpdateUnifiedWashBands(float DeltaSeconds);
	void CommitWashBand(const FDirtyPawnWashBand& WashBand);
	void ClearBandRange(TArray<FDirtyPawnPaintBand>& Bands, EDirtyPawnPaintState StateFilter, float MinHeight, float MaxHeight);
	bool UpdatePendingWashBands(float DeltaSeconds);
	bool UpdatePendingWetBands(float DeltaSeconds);
	float GetMaxWashableDirtyAlphaInRange(float MinHeight, float MaxHeight, bool bWashBlood, bool bWashSmears, bool bWashSandSnow) const;
	bool UpdateSweatState(float DeltaSeconds);
	bool ApplySweatActivity(float GainPerSecond, float DeltaSeconds);
	bool UpdateSweatyStateFromSweat();
	void ClearSweatInternal(bool bMarkDirty);
	bool UpdatePaintBands(float DeltaSeconds);
	void CompactPaintBands(TArray<FDirtyPawnPaintBand>& Bands, int32 MaxBands, bool bSortByPriority);
	void SyncLegacyScalarsFromBands();
	void PushPaintBandParameters(FDirtyPawnMaterialBinding& Binding) const;
	float GetWashCoverageAlphaForRange(float MinHeight, float MaxHeight) const;
	bool IsFullBodyWashBand(const FDirtyPawnWashBand& WashBand) const;
	float NormalizeNodeHeight(float NodeHeight) const;
	float GetReferenceInactiveHeight() const;
	float GetCrouchSubmergeBonus() const;
	bool IsOwnerCrouched() const;
	bool HasVisibleState() const;

private:
	void DisableAsDuplicateDirtyPawnComponent();
	void DisableNonCanonicalDirtyPawnComponents();

	bool bParametersDirty = false;
	bool bHasDirtyPawnHeightReference = false;
	float DirtyPawnReferenceBottom = 0.0f;
	float DirtyPawnReferenceTop = 0.0f;
	float DirtyPawnReferenceHeight = 1.0f;
	int32 NextPaintBandPriority = 1;
	bool bWashFadeClockActive = false;
	float WashFadeClockSeconds = 0.0f;
	TArray<FDirtyPawnPendingWashBand> PendingWashBands;
	TArray<FDirtyPawnPendingWetBand> PendingWetPaintBands;
	TSet<TWeakObjectPtr<AActor>> ActiveWaterSources;
};
