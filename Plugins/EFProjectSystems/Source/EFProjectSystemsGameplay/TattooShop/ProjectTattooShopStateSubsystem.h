#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TattooShop/ProjectAutomaticTattooTypes.h"
#include "ProjectTattooShopStateSubsystem.generated.h"

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectTattooParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FSoftObjectPath TextureAssetPath;

	/** Filename only, relative to the existing Saved/TattooShop/Texture directory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FString RuntimeTextureId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FLinearColor Color = FLinearColor::White;

	/** RGB is preserved as-authored unless this explicit opt-in tint is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	bool bUseTint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	/** Reference-pose SkinnedDecal placement used by schema v5 and later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement")
	EProjectAutomaticTattooPlacementPreset PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;

	/** Optional bone override. NAME_None lets the placement preset resolve its canonical bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement")
	FName AnchorBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float OffsetX = 1.92f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement", meta = (ClampMin = "-30.0", ClampMax = "30.0"))
	float OffsetY = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float Size = 21.68f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop|Placement", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float ProjectionDistance = 12.0f;

	/** Persisted diagnostic: a missing/unsafe runtime PNG disables only this record. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Project|TattooShop")
	bool bRuntimeTextureMissing = false;

	/** Legacy v1-v4 material-space fields retained for rollback and lossless migration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FVector2D Scale = FVector2D(1.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FLinearColor Offset = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	float Rotation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	int32 LayerOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	bool bEnabled = true;

	/** Complete legacy shader snapshot so cancel/save/load is lossless. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	TMap<FName, float> ScalarParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	TMap<FName, FLinearColor> VectorParameters;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectTattooRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Project|TattooShop")
	FGuid TattooId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Project|TattooShop")
	FProjectTattooParameters Parameters;
};

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTattooShopStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const TArray<FProjectTattooRecord>& GetRecords() const { return Records; }
	FProjectTattooRecord* FindRecord(const FGuid& TattooId);
	const FProjectTattooRecord* FindRecord(const FGuid& TattooId) const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|State")
	TArray<FProjectTattooRecord> GetTattooRecords() const { return Records; }

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|State")
	bool GetTattooRecord(const FGuid& TattooId, FProjectTattooRecord& OutRecord) const;

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|State")
	FProjectTattooParameters GetDefaultTattooParameters() const { return FProjectTattooParameters(); }

	/** Starts a new unsaved draft and returns its stable per-tattoo GUID. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	FGuid BeginCreate(const FProjectTattooParameters& InitialParameters);

	/** Captures one independent snapshot. Repeated calls do not replace the cancel baseline. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	bool BeginEdit(const FGuid& TattooId);

	/** Updates only the selected GUID in memory. Commit is the only operation that saves it. */
	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	bool Preview(const FGuid& TattooId, const FProjectTattooParameters& Parameters);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	bool Commit(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	bool Cancel(const FGuid& TattooId);

	UFUNCTION(BlueprintCallable, Category = "Project|TattooShop|State")
	bool Delete(const FGuid& TattooId);

	UFUNCTION(BlueprintPure, Category = "Project|TattooShop|State")
	bool IsTransactionActive(const FGuid& TattooId) const;

	void UpsertRecord(const FProjectTattooRecord& Record, bool bSaveImmediately);
	bool RemoveRecord(const FGuid& TattooId, bool bSaveImmediately);
	bool SaveState() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Must be called before Initialize. Production slot names are always rejected. */
	bool ConfigureSaveSlotsForAutomation(
		const FString& InSaveSlotName,
		const FString& InLegacyV4BackupSlotName);
#endif

	static constexpr int32 CurrentSaveVersion = 5;

private:
	void LoadState();
	void SortRecords();
	void NormalizeRecord(FProjectTattooRecord& Record, bool& bOutChanged) const;
	void MigrateRecordToV5(FProjectTattooRecord& Record, int32 SourceVersion) const;
	bool EnsureV4BackupBeforeWrite() const;
	bool SaveStateInternal(const FGuid* TransactionToCommit, bool bDeleteCommittedTransaction) const;
	TArray<FProjectTattooRecord> BuildCommittedRecordsForSave(
		const FGuid* TransactionToCommit,
		bool bDeleteCommittedTransaction) const;
	int32 GetNextLayerOrder() const;

	UPROPERTY(Transient)
	TArray<FProjectTattooRecord> Records;

	/** Existing-record snapshots. Draft GUIDs are tracked separately. */
	TMap<FGuid, FProjectTattooRecord> EditSnapshots;
	TSet<FGuid> CreatedDrafts;

	FString SaveSlotName = TEXT("ProjectTattooShop_v1");
	FString LegacyV4BackupSlotName = TEXT("ProjectTattooShop_v4_backup");
};
