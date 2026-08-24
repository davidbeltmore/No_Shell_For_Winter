#include "TattooShop/ProjectTattooShopStateSubsystem.h"

#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TattooShop/ProjectTattooShopSaveGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectTattooShopState, Log, All);

void UProjectTattooShopStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadState();
}

namespace ProjectTattooShopStatePrivate
{
	constexpr float LegacyScaleToSkinnedDecalSize = 22.0f;

	FString GetRuntimeTattooTextureDirectory()
	{
		FString Directory = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TattooShop"), TEXT("Texture")));
		FPaths::NormalizeDirectoryName(Directory);
		return Directory;
	}
}

FProjectTattooRecord* UProjectTattooShopStateSubsystem::FindRecord(const FGuid& TattooId)
{
	return Records.FindByPredicate([&TattooId](const FProjectTattooRecord& Record)
	{
		return Record.TattooId == TattooId;
	});
}

const FProjectTattooRecord* UProjectTattooShopStateSubsystem::FindRecord(const FGuid& TattooId) const
{
	return Records.FindByPredicate([&TattooId](const FProjectTattooRecord& Record)
	{
		return Record.TattooId == TattooId;
	});
}

bool UProjectTattooShopStateSubsystem::GetTattooRecord(
	const FGuid& TattooId,
	FProjectTattooRecord& OutRecord) const
{
	const FProjectTattooRecord* Existing = FindRecord(TattooId);
	if (!Existing)
	{
		return false;
	}
	OutRecord = *Existing;
	return true;
}

FGuid UProjectTattooShopStateSubsystem::BeginCreate(const FProjectTattooParameters& InitialParameters)
{
	FProjectTattooRecord Draft;
	do
	{
		Draft.TattooId = FGuid::NewGuid();
	}
	while (FindRecord(Draft.TattooId) != nullptr);

	Draft.Parameters = InitialParameters;
	Draft.Parameters.LayerOrder = GetNextLayerOrder();
	bool bNormalized = false;
	NormalizeRecord(Draft, bNormalized);
	Records.Add(Draft);
	CreatedDrafts.Add(Draft.TattooId);
	SortRecords();
	return Draft.TattooId;
}

bool UProjectTattooShopStateSubsystem::BeginEdit(const FGuid& TattooId)
{
	if (!TattooId.IsValid())
	{
		return false;
	}
	if (CreatedDrafts.Contains(TattooId) || EditSnapshots.Contains(TattooId))
	{
		return true;
	}
	const FProjectTattooRecord* Existing = FindRecord(TattooId);
	if (!Existing)
	{
		return false;
	}
	EditSnapshots.Add(TattooId, *Existing);
	return true;
}

bool UProjectTattooShopStateSubsystem::Preview(
	const FGuid& TattooId,
	const FProjectTattooParameters& Parameters)
{
	if (!IsTransactionActive(TattooId))
	{
		return false;
	}
	FProjectTattooRecord* Existing = FindRecord(TattooId);
	if (!Existing)
	{
		return false;
	}

	Existing->Parameters = Parameters;
	bool bNormalized = false;
	NormalizeRecord(*Existing, bNormalized);
	SortRecords();
	return true;
}

bool UProjectTattooShopStateSubsystem::Commit(const FGuid& TattooId)
{
	if (!IsTransactionActive(TattooId))
	{
		return false;
	}
	FProjectTattooRecord* Existing = FindRecord(TattooId);
	if (!Existing)
	{
		return false;
	}

	bool bNormalized = false;
	NormalizeRecord(*Existing, bNormalized);
	SortRecords();
	if (!SaveStateInternal(&TattooId, false))
	{
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Failed to commit TattooShop transaction %s; its cancel snapshot remains available."),
			*TattooId.ToString(EGuidFormats::Digits));
		return false;
	}

	EditSnapshots.Remove(TattooId);
	CreatedDrafts.Remove(TattooId);
	return true;
}

bool UProjectTattooShopStateSubsystem::Cancel(const FGuid& TattooId)
{
	if (CreatedDrafts.Remove(TattooId) > 0)
	{
		Records.RemoveAll([&TattooId](const FProjectTattooRecord& Record)
		{
			return Record.TattooId == TattooId;
		});
		EditSnapshots.Remove(TattooId);
		SortRecords();
		return true;
	}

	FProjectTattooRecord Snapshot;
	if (!EditSnapshots.RemoveAndCopyValue(TattooId, Snapshot))
	{
		return false;
	}
	if (FProjectTattooRecord* Existing = FindRecord(TattooId))
	{
		*Existing = Snapshot;
	}
	else
	{
		Records.Add(Snapshot);
	}
	SortRecords();
	return true;
}

bool UProjectTattooShopStateSubsystem::Delete(const FGuid& TattooId)
{
	if (CreatedDrafts.Contains(TattooId))
	{
		return Cancel(TattooId);
	}

	const FProjectTattooRecord* Existing = FindRecord(TattooId);
	if (!Existing)
	{
		return false;
	}
	const FProjectTattooRecord RemovedRecord = *Existing;

	Records.RemoveAll([&TattooId](const FProjectTattooRecord& Record)
	{
		return Record.TattooId == TattooId;
	});
	if (!SaveStateInternal(&TattooId, true))
	{
		Records.Add(RemovedRecord);
		SortRecords();
		return false;
	}

	CreatedDrafts.Remove(TattooId);
	EditSnapshots.Remove(TattooId);
	UE_LOG(
		LogProjectTattooShopState,
		Verbose,
		TEXT("Deleted TattooShop record %s."),
		*TattooId.ToString(EGuidFormats::Digits));
	return true;
}

bool UProjectTattooShopStateSubsystem::IsTransactionActive(const FGuid& TattooId) const
{
	return CreatedDrafts.Contains(TattooId) || EditSnapshots.Contains(TattooId);
}

void UProjectTattooShopStateSubsystem::UpsertRecord(const FProjectTattooRecord& Record, const bool bSaveImmediately)
{
	if (!Record.TattooId.IsValid())
	{
		return;
	}

	FProjectTattooRecord NormalizedRecord = Record;
	bool bNormalized = false;
	NormalizeRecord(NormalizedRecord, bNormalized);
	if (FProjectTattooRecord* Existing = FindRecord(Record.TattooId))
	{
		*Existing = NormalizedRecord;
	}
	else
	{
		Records.Add(NormalizedRecord);
	}
	SortRecords();

	if (bSaveImmediately)
	{
		SaveState();
	}
}

bool UProjectTattooShopStateSubsystem::RemoveRecord(const FGuid& TattooId, const bool bSaveImmediately)
{
	const int32 Removed = Records.RemoveAll([&TattooId](const FProjectTattooRecord& Record)
	{
		return Record.TattooId == TattooId;
	});
	if (Removed > 0 && bSaveImmediately)
	{
		SaveState();
	}
	return Removed > 0;
}

void UProjectTattooShopStateSubsystem::LoadState()
{
	Records.Reset();
	EditSnapshots.Reset();
	CreatedDrafts.Reset();
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		return;
	}

	const UProjectTattooShopSaveGame* Save = Cast<UProjectTattooShopSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!Save || Save->Version <= 0 || Save->Version > CurrentSaveVersion)
	{
		UE_LOG(LogProjectTattooShopState, Warning, TEXT("Ignored unsupported TattooShop save slot %s."), *SaveSlotName);
		return;
	}

	bool bMigratedLegacyState = false;
	for (FProjectTattooRecord Record : Save->Records)
	{
		if (Record.TattooId.IsValid())
		{
			// User_R was a runtime-uploaded TattooShop image that was accidentally
			// retained as if it were a default layer. It is no longer part of the
			// TattooShop catalogue; discard only records that point to that exact
			// filename and leave every other user PNG untouched.
			if (Record.Parameters.RuntimeTextureId.Equals(TEXT("User_R.png"), ESearchCase::IgnoreCase)
				|| Record.Parameters.RuntimeTextureId.Equals(TEXT("User_R"), ESearchCase::IgnoreCase))
			{
				UE_LOG(
					LogProjectTattooShopState,
					Display,
					TEXT("Removed obsolete TattooShop runtime layer User_R from persisted state."));
				bMigratedLegacyState = true;
				continue;
			}

			// Schema v1 briefly stored the native bridge's SkinnedDecal tuning
			// (0.58/0.58 at offset 0/0) as if it were M_TattooShop shader space.
			// Migrate only that exact injected signature; user-authored transforms
			// are deliberately left untouched.
			if (Save->Version == 1
				&& FMath::IsNearlyEqual(Record.Parameters.Scale.X, 0.58f, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Record.Parameters.Scale.Y, 0.58f, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyZero(Record.Parameters.Offset.R, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyZero(Record.Parameters.Offset.G, KINDA_SMALL_NUMBER))
			{
				Record.Parameters.Scale = FVector2D(0.1, 0.1);
				Record.Parameters.Offset.G = 0.5f;
				Record.Parameters.ScalarParameters.Add(FName(TEXT("Scale")), 0.1f);
				Record.Parameters.ScalarParameters.Add(FName(TEXT("ScaleY")), 0.1f);
				Record.Parameters.ScalarParameters.Add(FName(TEXT("WorldPositionOffset")), 0.1f);
				Record.Parameters.ScalarParameters.Add(FName(TEXT("DepthMin")), -200.0f);
				Record.Parameters.ScalarParameters.Add(FName(TEXT("DepthMax")), 200.0f);
				Record.Parameters.VectorParameters.Add(FName(TEXT("Offset")), Record.Parameters.Offset);
				bMigratedLegacyState = true;
			}

			// Schemas v2/v3 were the first material-space bridge, but inherited
			// M_TattooShop's TintBaseColor=0 default. That branch displays the raw
			// (usually black) tattoo RGB and ignores the selected base color. Upgrade
			// all records written before the project overlay material became the
			// authority; schema v4 preserves explicit user toggles after migration.
			if (Save->Version <= 3)
			{
				const FName TintBaseColorName(TEXT("TintBaseColor"));
				const float* SavedTint = Record.Parameters.ScalarParameters.Find(TintBaseColorName);
				if (!SavedTint || *SavedTint < 0.5f)
				{
					Record.Parameters.ScalarParameters.Add(TintBaseColorName, 1.0f);
					bMigratedLegacyState = true;
				}
			}

			if (Save->Version < CurrentSaveVersion)
			{
				MigrateRecordToV5(Record, Save->Version);
				bMigratedLegacyState = true;
			}

			bool bNormalized = false;
			NormalizeRecord(Record, bNormalized);
			bMigratedLegacyState |= bNormalized;
			Records.Add(Record);
		}
	}
	SortRecords();
	UE_LOG(LogProjectTattooShopState, Log, TEXT("Loaded %d independent TattooShop record(s), schema v%d."), Records.Num(), Save->Version);
	if (bMigratedLegacyState)
	{
		UE_LOG(LogProjectTattooShopState, Display, TEXT("Migrated legacy TattooShop placement/color defaults to schema v%d."), CurrentSaveVersion);
		if (!SaveState())
		{
			UE_LOG(LogProjectTattooShopState, Error, TEXT("TattooShop migration remains in memory because the v5 save could not be written safely."));
		}
	}
}

bool UProjectTattooShopStateSubsystem::SaveState() const
{
	return SaveStateInternal(nullptr, false);
}

#if WITH_DEV_AUTOMATION_TESTS
bool UProjectTattooShopStateSubsystem::ConfigureSaveSlotsForAutomation(
	const FString& InSaveSlotName,
	const FString& InLegacyV4BackupSlotName)
{
	static const FString ProductionSaveSlotName(TEXT("ProjectTattooShop_v1"));
	static const FString ProductionBackupSlotName(TEXT("ProjectTattooShop_v4_backup"));
	static const FString RequiredAutomationPrefix(TEXT("Automation_"));

	const bool bUnsafeNames = InSaveSlotName.IsEmpty()
		|| InLegacyV4BackupSlotName.IsEmpty()
		|| InSaveSlotName == InLegacyV4BackupSlotName
		|| !InSaveSlotName.StartsWith(RequiredAutomationPrefix)
		|| !InLegacyV4BackupSlotName.StartsWith(RequiredAutomationPrefix)
		|| InSaveSlotName.Equals(ProductionSaveSlotName, ESearchCase::IgnoreCase)
		|| InSaveSlotName.Equals(ProductionBackupSlotName, ESearchCase::IgnoreCase)
		|| InLegacyV4BackupSlotName.Equals(ProductionSaveSlotName, ESearchCase::IgnoreCase)
		|| InLegacyV4BackupSlotName.Equals(ProductionBackupSlotName, ESearchCase::IgnoreCase);
	if (bUnsafeNames)
	{
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Rejected unsafe TattooShop automation slots '%s' and '%s'."),
			*InSaveSlotName,
			*InLegacyV4BackupSlotName);
		return false;
	}

	SaveSlotName = InSaveSlotName;
	LegacyV4BackupSlotName = InLegacyV4BackupSlotName;
	return true;
}
#endif

bool UProjectTattooShopStateSubsystem::SaveStateInternal(
	const FGuid* TransactionToCommit,
	const bool bDeleteCommittedTransaction) const
{
	if (!EnsureV4BackupBeforeWrite())
	{
		return false;
	}

	UProjectTattooShopSaveGame* Save = Cast<UProjectTattooShopSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UProjectTattooShopSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}

	Save->Version = CurrentSaveVersion;
	Save->Records = BuildCommittedRecordsForSave(TransactionToCommit, bDeleteCommittedTransaction);
	return UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
}

TArray<FProjectTattooRecord> UProjectTattooShopStateSubsystem::BuildCommittedRecordsForSave(
	const FGuid* TransactionToCommit,
	const bool bDeleteCommittedTransaction) const
{
	TArray<FProjectTattooRecord> CommittedRecords;
	CommittedRecords.Reserve(Records.Num() + EditSnapshots.Num());
	for (const FProjectTattooRecord& Record : Records)
	{
		const bool bIsCommittedTransaction = TransactionToCommit && Record.TattooId == *TransactionToCommit;
		if (CreatedDrafts.Contains(Record.TattooId) && !bIsCommittedTransaction)
		{
			continue;
		}
		if (bIsCommittedTransaction && bDeleteCommittedTransaction)
		{
			continue;
		}
		if (!bIsCommittedTransaction)
		{
			if (const FProjectTattooRecord* Snapshot = EditSnapshots.Find(Record.TattooId))
			{
				CommittedRecords.Add(*Snapshot);
				continue;
			}
		}
		CommittedRecords.Add(Record);
	}

	// A transaction may have removed its working record. Preserve all other
	// snapshots so an unrelated save can never commit a preview or deletion.
	for (const TPair<FGuid, FProjectTattooRecord>& Pair : EditSnapshots)
	{
		const bool bIsCommittedTransaction = TransactionToCommit && Pair.Key == *TransactionToCommit;
		if (bIsCommittedTransaction)
		{
			continue;
		}
		if (!CommittedRecords.ContainsByPredicate([&Pair](const FProjectTattooRecord& Record)
		{
			return Record.TattooId == Pair.Key;
		}))
		{
			CommittedRecords.Add(Pair.Value);
		}
	}

	CommittedRecords.Sort([](const FProjectTattooRecord& Left, const FProjectTattooRecord& Right)
	{
		if (Left.Parameters.LayerOrder != Right.Parameters.LayerOrder)
		{
			return Left.Parameters.LayerOrder < Right.Parameters.LayerOrder;
		}
		return Left.TattooId.ToString(EGuidFormats::Digits) < Right.TattooId.ToString(EGuidFormats::Digits);
	});
	return CommittedRecords;
}

void UProjectTattooShopStateSubsystem::SortRecords()
{
	Records.Sort([](const FProjectTattooRecord& Left, const FProjectTattooRecord& Right)
	{
		if (Left.Parameters.LayerOrder != Right.Parameters.LayerOrder)
		{
			return Left.Parameters.LayerOrder < Right.Parameters.LayerOrder;
		}
		return Left.TattooId.ToString(EGuidFormats::Digits) < Right.TattooId.ToString(EGuidFormats::Digits);
	});
}

void UProjectTattooShopStateSubsystem::NormalizeRecord(
	FProjectTattooRecord& Record,
	bool& bOutChanged) const
{
	bOutChanged = false;
	FProjectTattooParameters& Parameters = Record.Parameters;

	const float ClampedOpacity = FMath::Clamp(Parameters.Opacity, 0.0f, 1.0f);
	const float ClampedOffsetX = FMath::Clamp(Parameters.OffsetX, -30.0f, 30.0f);
	const float ClampedOffsetY = FMath::Clamp(Parameters.OffsetY, -30.0f, 30.0f);
	const float ClampedSize = FMath::Clamp(Parameters.Size, 1.0f, 50.0f);
	const float ClampedRotation = FMath::Clamp(
		FMath::UnwindDegrees(Parameters.RotationDegrees),
		-180.0f,
		180.0f);
	const float ClampedProjection = FMath::Clamp(Parameters.ProjectionDistance, 0.0f, 50.0f);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.Opacity, ClampedOpacity);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.OffsetX, ClampedOffsetX);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.OffsetY, ClampedOffsetY);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.Size, ClampedSize);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.RotationDegrees, ClampedRotation);
	bOutChanged |= !FMath::IsNearlyEqual(Parameters.ProjectionDistance, ClampedProjection);
	Parameters.Opacity = ClampedOpacity;
	Parameters.OffsetX = ClampedOffsetX;
	Parameters.OffsetY = ClampedOffsetY;
	Parameters.Size = ClampedSize;
	Parameters.RotationDegrees = ClampedRotation;
	Parameters.ProjectionDistance = ClampedProjection;

	const bool bPreviouslyMissing = Parameters.bRuntimeTextureMissing;
	Parameters.bRuntimeTextureMissing = false;
	if (!Parameters.TextureAssetPath.IsValid() && !Parameters.RuntimeTextureId.IsEmpty())
	{
		const FString SafeId = FPaths::GetCleanFilename(Parameters.RuntimeTextureId);
		const bool bUnsafeId = SafeId != Parameters.RuntimeTextureId;
		const FString FilePath = FPaths::Combine(ProjectTattooShopStatePrivate::GetRuntimeTattooTextureDirectory(), SafeId);
		Parameters.bRuntimeTextureMissing = bUnsafeId || !IFileManager::Get().FileExists(*FilePath);
		if (Parameters.bRuntimeTextureMissing)
		{
			if (Parameters.bEnabled)
			{
				Parameters.bEnabled = false;
				bOutChanged = true;
			}
			UE_LOG(
				LogProjectTattooShopState,
				Warning,
				TEXT("Disabled only TattooShop record %s because runtime PNG '%s' is missing or unsafe."),
				*Record.TattooId.ToString(EGuidFormats::Digits),
				*Parameters.RuntimeTextureId);
		}
	}
	bOutChanged |= bPreviouslyMissing != Parameters.bRuntimeTextureMissing;
}

void UProjectTattooShopStateSubsystem::MigrateRecordToV5(
	FProjectTattooRecord& Record,
	const int32 SourceVersion) const
{
	if (SourceVersion >= CurrentSaveVersion)
	{
		return;
	}

	FProjectTattooParameters& Parameters = Record.Parameters;
	Parameters.PlacementPreset = EProjectAutomaticTattooPlacementPreset::ChestFront;
	Parameters.AnchorBone = NAME_None;
	Parameters.OffsetX = Parameters.Offset.R;
	Parameters.OffsetY = Parameters.Offset.G;
	Parameters.Size = FMath::Clamp(
		FMath::Max(FMath::Abs(Parameters.Scale.X), FMath::Abs(Parameters.Scale.Y))
			* ProjectTattooShopStatePrivate::LegacyScaleToSkinnedDecalSize,
		1.0f,
		50.0f);
	Parameters.RotationDegrees = FMath::RadiansToDegrees(Parameters.Rotation);
	Parameters.ProjectionDistance = 12.0f;
	// Legacy TintBaseColor drove the shared material path that could blacken
	// unrelated layers. Preserve the selected Color for a future explicit opt-in,
	// but every migrated record starts with RGB tinting disabled.
	Parameters.bUseTint = false;
	Parameters.bRuntimeTextureMissing = false;
}

bool UProjectTattooShopStateSubsystem::EnsureV4BackupBeforeWrite() const
{
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		return true;
	}

	UProjectTattooShopSaveGame* ExistingSave = Cast<UProjectTattooShopSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!ExistingSave)
	{
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Refused to overwrite unreadable TattooShop save slot %s."),
			*SaveSlotName);
		return false;
	}
	if (ExistingSave->Version > CurrentSaveVersion)
	{
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Refused to overwrite newer TattooShop schema v%d in slot %s."),
			ExistingSave->Version,
			*SaveSlotName);
		return false;
	}
	if (ExistingSave->Version != 4)
	{
		return true;
	}

	if (UGameplayStatics::DoesSaveGameExist(LegacyV4BackupSlotName, 0))
	{
		const UProjectTattooShopSaveGame* ExistingBackup = Cast<UProjectTattooShopSaveGame>(
			UGameplayStatics::LoadGameFromSlot(LegacyV4BackupSlotName, 0));
		if (ExistingBackup && ExistingBackup->Version == 4)
		{
			return true;
		}
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Refused to overwrite invalid TattooShop backup slot %s; v5 save was not written."),
			*LegacyV4BackupSlotName);
		return false;
	}

	if (!UGameplayStatics::SaveGameToSlot(
		ExistingSave,
		LegacyV4BackupSlotName,
		0))
	{
		UE_LOG(
			LogProjectTattooShopState,
			Error,
			TEXT("Could not create TattooShop v4 backup slot %s; v5 save was not written."),
			*LegacyV4BackupSlotName);
		return false;
	}

	UE_LOG(
		LogProjectTattooShopState,
		Display,
		TEXT("Created non-destructive TattooShop v4 backup slot %s before the first v5 write."),
		*LegacyV4BackupSlotName);
	return true;
}

int32 UProjectTattooShopStateSubsystem::GetNextLayerOrder() const
{
	int32 HighestLayer = INDEX_NONE;
	for (const FProjectTattooRecord& Record : Records)
	{
		HighestLayer = FMath::Max(HighestLayer, Record.Parameters.LayerOrder);
	}
	return HighestLayer == MAX_int32 ? MAX_int32 : HighestLayer + 1;
}
