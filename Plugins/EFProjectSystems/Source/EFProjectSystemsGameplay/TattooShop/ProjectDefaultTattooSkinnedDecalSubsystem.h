#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "TattooShop/ProjectAutomaticTattooTypes.h"
#include "ProjectDefaultTattooSkinnedDecalSubsystem.generated.h"

class APawn;
class USkeletalMeshComponent;
class USkinnedDecalSampler;
class UTexture2D;

struct FProjectAutomaticTattooRuntimePlacementOverride
{
	EProjectAutomaticTattooPlacementPreset PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;
	FName AnchorBone = NAME_None;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	float Size = 1.0f;
	float RotationDegrees = 0.0f;
	float ProjectionDistance = 0.0f;
	bool bEnabled = true;
};

struct FProjectAutomaticTattooRuntimeDebugState
{
	bool bHasRuntimeOverride = false;
	FProjectAutomaticTattooRuntimePlacementOverride RuntimeOverride;
	bool bForcedActiveForDebug = false;
};

struct FProjectAutomaticTattooRuntimeDebugSnapshot
{
	FName RowName = NAME_None;
	FProjectAutomaticTattooTableRow DataTableRow;
	FProjectAutomaticTattooTableRow EffectiveRow;
	bool bActive = false;
	bool bForcedActiveForDebug = false;
	bool bHasRuntimeOverride = false;
	int32 DecalIndex = INDEX_NONE;
	int32 SubUV = INDEX_NONE;
	FString TattooTexturePath;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefaultTattooSkinnedDecalSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	bool ApplyTattooShopPreviewForAutomation(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	void ClearTattooShopPreviewForAutomation(APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	bool IsTattooShopPreviewForAutomationEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	bool HasActiveAutomaticTattoo(APawn* Pawn = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	bool IsAutomaticTattooUnlockedForAutomation() const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|Automation")
	int32 GetAutomaticTattooUnlockEncounterCountForAutomation() const;

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	bool RefreshAutomaticTattooForAutomation(APawn* Pawn = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|Automation")
	FString GetTattooLayerReportForAutomation(APawn* Pawn = nullptr) const;

	void GetAutomaticTattooRuntimeDebugSnapshots(TArray<FProjectAutomaticTattooRuntimeDebugSnapshot>& OutSnapshots) const;
	bool GetAutomaticTattooRuntimeDebugSnapshot(FName RowName, FProjectAutomaticTattooRuntimeDebugSnapshot& OutSnapshot) const;
	FProjectAutomaticTattooRuntimeDebugState CaptureAutomaticTattooRuntimeDebugState(FName RowName) const;
	bool RestoreAutomaticTattooRuntimeDebugState(APawn* Pawn, FName RowName, const FProjectAutomaticTattooRuntimeDebugState& State);
	bool SetAutomaticTattooRuntimeDebugPlacement(APawn* Pawn, FName RowName, const FProjectAutomaticTattooTableRow& TattooRow);
	bool AdjustAutomaticTattooRuntimeDebugPlacement(
		APawn* Pawn,
		FName RowName,
		float DeltaOffsetX,
		float DeltaOffsetY,
		float DeltaSize,
		float DeltaRotationDegrees,
		float DeltaProjectionDistance);
	bool ResetAutomaticTattooRuntimeDebugPlacement(APawn* Pawn, FName RowName);
	bool SetAutomaticTattooRuntimeDebugForcedActive(APawn* Pawn, FName RowName, bool bForcedActive);
	bool ToggleAutomaticTattooRuntimeDebugForcedActive(APawn* Pawn, FName RowName);
	FString BuildAutomaticTattooRuntimeDebugCopyText(FName RowName) const;

private:
	APawn* ResolveLocalPlayerPawn() const;
	bool IsTattooShopOpen() const;
	bool RestoreSkinnedDecalOverlayIfNeeded() const;
	bool EnsureAutomaticTattoo(APawn* Pawn);
	bool TryApplyTattooShopPreview(APawn* Pawn);
	bool ApplyTattooLayer(APawn* Pawn, int32 DecalIndex, const TCHAR* LayerName, FName RowName, const FProjectAutomaticTattooTableRow* TattooRow, int32 SubUV);
	bool ApplyAutomaticTattooLayer(APawn* Pawn, int32 DecalIndex, FName RowName, const FProjectAutomaticTattooTableRow* TattooRow, int32 SubUV);
	void ClearAutomaticTattoo(APawn* Pawn);
	void ClearTattooShopPreviewLayer(APawn* Pawn);
	void ClearProjectTattooLayers(APawn* Pawn);
	void ClearTattooLayer(APawn* Pawn, int32 DecalIndex);
	USkeletalMeshComponent* ResolveTargetMesh(APawn* Pawn) const;
	USkinnedDecalSampler* ResolveOrCreateSampler(APawn* Pawn) const;
	bool ConfigureSampler(USkinnedDecalSampler* Sampler, USkeletalMeshComponent* TargetMesh);
	bool ConfigureOverlayMaterial(USkinnedDecalSampler* Sampler, const TArray<FName>& RowNames, const TArray<const FProjectAutomaticTattooTableRow*>& TattooRows);
	bool IsAutomaticTattooRowActive(FName RowName, const FProjectAutomaticTattooTableRow* TattooRow) const;
	void ResolveAutomaticTattooRows(TArray<FName>& OutRowNames, TArray<const FProjectAutomaticTattooTableRow*>& OutRows, bool bOnlyActive) const;
	const FProjectAutomaticTattooTableRow* FindAutomaticTattooRow(FName RowName) const;
	FProjectAutomaticTattooTableRow BuildEffectiveTattooRow(FName RowName, const FProjectAutomaticTattooTableRow* TattooRow) const;
	void BuildEffectiveTattooRows(
		const TArray<FName>& RowNames,
		const TArray<const FProjectAutomaticTattooTableRow*>& SourceRows,
		TArray<FProjectAutomaticTattooTableRow>& OutEffectiveRows,
		TArray<const FProjectAutomaticTattooTableRow*>& OutEffectiveRowPtrs) const;
	bool RefreshAutomaticTattooAfterRuntimeDebugChange(APawn* Pawn);
	bool AppendTattooShopPreviewAtlasRow(TArray<FName>& InOutRowNames, TArray<const FProjectAutomaticTattooTableRow*>& InOutRows, FName* OutPreviewRowName = nullptr, const FProjectAutomaticTattooTableRow** OutPreviewRow = nullptr) const;
	UTexture2D* ResolveTattooTexture(const FProjectAutomaticTattooTableRow* TattooRow) const;
	UTexture2D* CreateAutomaticTattooAtlas(const TArray<FName>& RowNames, const TArray<const FProjectAutomaticTattooTableRow*>& TattooRows, int32& OutSubImagesX, int32& OutSubImagesY, TMap<FName, int32>& OutSubUVByRow, TMap<FName, int32>& OutCompositeSourceCountByRow, TMap<FName, FString>& OutCompositeGroupKeyByRow);
	UTexture2D* CreateMaskedTattooTexture(UTexture2D* SourceTexture);
	FName ResolveAnchorBone(const USkeletalMeshComponent* TargetMesh) const;
	FVector ComputeTattooLocation(const APawn* Pawn, const USkeletalMeshComponent* TargetMesh, FName AnchorBone, const FProjectAutomaticTattooTableRow* TattooRow) const;
	FQuat ComputeTattooRotation(const APawn* Pawn, const FProjectAutomaticTattooTableRow* TattooRow) const;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> AppliedPawn;

	UPROPERTY(Transient)
	TWeakObjectPtr<USkinnedDecalSampler> AppliedSampler;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RuntimeMaskedTattooTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RuntimeAutomaticTattooAtlasTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RuntimeNeutralCompactTexture;

	TMap<FName, int32> AutomaticTattooDecalIndices;
	TMap<FName, int32> AutomaticTattooSubUVByRow;
	TMap<FName, FString> AutomaticTattooPlacementSignatures;
	TMap<FName, FVector2D> AutomaticTattooEffectiveOffsets;
	TMap<FName, int32> AutomaticTattooCompositeSourceCountByRow;
	TMap<FName, FString> AutomaticTattooCompositeGroupKeyByRow;
	TMap<FName, FProjectAutomaticTattooRuntimePlacementOverride> RuntimeDebugPlacementOverrides;
	TSet<FName> RuntimeDebugForcedActiveRows;
	int32 AutomaticTattooAtlasSubImagesX = 1;
	int32 AutomaticTattooAtlasSubImagesY = 1;
	TWeakObjectPtr<USkinnedDecalSampler> CachedOverlaySampler;
	FString CachedOverlayMaterialSignature;
	int32 TattooShopPreviewDecalIndex = 10;
	float RetryCooldownSeconds = 0.0f;
	bool bTattooShopPreviewForAutomation = false;
	bool bTattooShopPreviewApplied = false;
};
