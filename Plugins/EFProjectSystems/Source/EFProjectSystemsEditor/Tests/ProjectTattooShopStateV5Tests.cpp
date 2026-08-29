#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "TattooShop/ProjectTattooShopSaveGame.h"
#include "TattooShop/ProjectTattooShopStateSubsystem.h"

namespace ProjectTattooShopStateV5TestsPrivate
{
	constexpr int32 SaveUserIndex = 0;

	/**
	 * Every execution owns fresh GUID slots. The subsystem's automation hook also
	 * rejects production names, so this fixture can never touch player state.
	 */
	class FScopedTattooAutomationSlots
	{
	public:
		FScopedTattooAutomationSlots()
		{
			const FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Primary = FString::Printf(TEXT("Automation_ProjectTattooShop_%s_Primary"), *RunId);
			Backup = FString::Printf(TEXT("Automation_ProjectTattooShop_%s_V4Backup"), *RunId);
		}

		~FScopedTattooAutomationSlots()
		{
			UGameplayStatics::DeleteGameInSlot(Primary, SaveUserIndex);
			UGameplayStatics::DeleteGameInSlot(Backup, SaveUserIndex);
		}

		const FString& GetPrimary() const { return Primary; }
		const FString& GetBackup() const { return Backup; }

	private:
		FString Primary;
		FString Backup;
	};

	FProjectTattooRecord MakeLegacyRecord(
		const FGuid& TattooId,
		const TCHAR* TexturePath,
		const FLinearColor& Color,
		const FVector2D& Scale,
		const FLinearColor& Offset,
		const float RotationRadians,
		const int32 LayerOrder)
	{
		FProjectTattooRecord Record;
		Record.TattooId = TattooId;
		Record.Parameters.TextureAssetPath = FSoftObjectPath(TexturePath);
		Record.Parameters.Color = Color;
		Record.Parameters.Scale = Scale;
		Record.Parameters.Offset = Offset;
		Record.Parameters.Rotation = RotationRadians;
		Record.Parameters.LayerOrder = LayerOrder;
		Record.Parameters.bEnabled = true;
		// Even an explicitly enabled legacy shared-material tint must migrate off.
		Record.Parameters.ScalarParameters.Add(FName(TEXT("TintBaseColor")), 1.0f);
		return Record;
	}

	const FProjectTattooRecord* FindRecord(
		const TArray<FProjectTattooRecord>& Records,
		const FGuid& TattooId)
	{
		return Records.FindByPredicate([&TattooId](const FProjectTattooRecord& Record)
		{
			return Record.TattooId == TattooId;
		});
	}

	const FProjectTattooRecord* FindRecord(
		const UProjectTattooShopSaveGame* Save,
		const FGuid& TattooId)
	{
		return Save ? FindRecord(Save->Records, TattooId) : nullptr;
	}

	UProjectTattooShopSaveGame* LoadTattooSave(const FString& SlotName)
	{
		return Cast<UProjectTattooShopSaveGame>(
			UGameplayStatics::LoadGameFromSlot(SlotName, SaveUserIndex));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTattooShopStateV5DefaultsTest,
	"NoShellForWinter.TattooShop.StateV5.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTattooShopStateV5DefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FProjectTattooParameters Defaults;
	TestEqual(TEXT("Schema is v5"), UProjectTattooShopStateSubsystem::CurrentSaveVersion, 5);
	TestEqual(
		TEXT("Default placement is chest front"),
		Defaults.PlacementPreset,
		EProjectAutomaticTattooPlacementPreset::ChestFront);
	TestEqual(TEXT("Default anchor defers to the preset"), Defaults.AnchorBone, NAME_None);
	TestTrue(TEXT("Default X offset matches the automatic tattoo placement"), FMath::IsNearlyEqual(Defaults.OffsetX, 1.92f));
	TestTrue(TEXT("Default Y offset matches the automatic tattoo placement"), FMath::IsNearlyEqual(Defaults.OffsetY, 14.0f));
	TestTrue(TEXT("Default size matches the automatic tattoo placement"), FMath::IsNearlyEqual(Defaults.Size, 21.68f));
	TestTrue(TEXT("Default rotation is zero"), FMath::IsNearlyZero(Defaults.RotationDegrees));
	TestTrue(TEXT("Default projection distance is twelve"), FMath::IsNearlyEqual(Defaults.ProjectionDistance, 12.0f));
	TestTrue(TEXT("Default opacity is opaque"), FMath::IsNearlyEqual(Defaults.Opacity, 1.0f));
	TestTrue(TEXT("Default RGB is white"), Defaults.Color.Equals(FLinearColor::White));
	TestFalse(TEXT("Tint is opt-in and disabled by default"), Defaults.bUseTint);
	TestTrue(TEXT("A new tattoo is enabled by default"), Defaults.bEnabled);
	TestFalse(TEXT("A new tattoo has no missing runtime texture diagnostic"), Defaults.bRuntimeTextureMissing);

	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UProjectTattooShopStateSubsystem* State = GameInstance
		? NewObject<UProjectTattooShopStateSubsystem>(GameInstance)
		: nullptr;
	TestNotNull(TEXT("Automation slot guard fixture is constructible"), State);
	if (State)
	{
		TestFalse(
			TEXT("Automation hook rejects both production slot names"),
			State->ConfigureSaveSlotsForAutomation(
				TEXT("ProjectTattooShop_v1"),
				TEXT("ProjectTattooShop_v4_backup")));
		TestFalse(
			TEXT("Automation hook rejects a production primary mixed with an automation backup"),
			State->ConfigureSaveSlotsForAutomation(
				TEXT("ProjectTattooShop_v1"),
				TEXT("Automation_ProjectTattooShop_Guard_Backup")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTattooShopStateV5TransactionsAndMigrationTest,
	"NoShellForWinter.TattooShop.StateV5.TransactionsMigrationAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTattooShopStateV5TransactionsAndMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace ProjectTattooShopStateV5TestsPrivate;

	FScopedTattooAutomationSlots AutomationSlots;
	const FString& PrimarySlotName = AutomationSlots.GetPrimary();
	const FString& LegacyBackupSlotName = AutomationSlots.GetBackup();

	const FGuid FirstId = FGuid::NewGuid();
	const FGuid SecondId = FGuid::NewGuid();
	const FGuid MissingPngId = FGuid::NewGuid();
	const FLinearColor FirstColor(0.15f, 0.35f, 0.85f, 1.0f);
	const FLinearColor SecondColor(0.85f, 0.25f, 0.10f, 1.0f);

	UProjectTattooShopSaveGame* LegacySave = NewObject<UProjectTattooShopSaveGame>();
	LegacySave->Version = 4;
	LegacySave->Records.Add(MakeLegacyRecord(
		FirstId,
		TEXT("/Game/TattooShop/Texture/T_Bunny.T_Bunny"),
		FirstColor,
		FVector2D(0.5, 0.25),
		FLinearColor(2.0f, -3.0f, 0.0f, 0.0f),
		PI * 0.5f,
		2));
	LegacySave->Records.Add(MakeLegacyRecord(
		SecondId,
		TEXT("/Game/TattooShop/Texture/T_Heart.T_Heart"),
		SecondColor,
		FVector2D(0.25, 0.25),
		FLinearColor(-1.0f, 4.0f, 0.0f, 0.0f),
		0.0f,
		5));
	FProjectTattooRecord MissingPng = MakeLegacyRecord(
		MissingPngId,
		TEXT(""),
		FLinearColor::White,
		FVector2D(0.2, 0.2),
		FLinearColor::Black,
		0.0f,
		8);
	MissingPng.Parameters.TextureAssetPath.Reset();
	MissingPng.Parameters.RuntimeTextureId = FString::Printf(
		TEXT("Automation_Missing_%s.png"),
		*MissingPngId.ToString(EGuidFormats::Digits));
	LegacySave->Records.Add(MissingPng);
	if (!TestTrue(
		TEXT("A v4 fixture can be written"),
		UGameplayStatics::SaveGameToSlot(LegacySave, PrimarySlotName, SaveUserIndex)))
	{
		return false;
	}

	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UProjectTattooShopStateSubsystem* State = GameInstance
		? NewObject<UProjectTattooShopStateSubsystem>(GameInstance)
		: nullptr;
	TestNotNull(TEXT("Transient GameInstance exists"), GameInstance);
	TestNotNull(TEXT("Transient TattooShop state exists"), State);
	if (!State)
	{
		return false;
	}
	if (!TestTrue(
		TEXT("State accepts GUID-scoped automation slots"),
		State->ConfigureSaveSlotsForAutomation(PrimarySlotName, LegacyBackupSlotName)))
	{
		return false;
	}
	FSubsystemCollection<UGameInstanceSubsystem> Collection;
	State->Initialize(Collection);

	FProjectTattooRecord First;
	FProjectTattooRecord Second;
	FProjectTattooRecord Missing;
	TestTrue(TEXT("The first v4 record migrated"), State->GetTattooRecord(FirstId, First));
	TestTrue(TEXT("The second v4 record migrated"), State->GetTattooRecord(SecondId, Second));
	TestTrue(TEXT("The missing-PNG record remains independently addressable"), State->GetTattooRecord(MissingPngId, Missing));
	TestTrue(TEXT("Migration preserves first RGB"), First.Parameters.Color.Equals(FirstColor));
	TestTrue(TEXT("Migration preserves second RGB"), Second.Parameters.Color.Equals(SecondColor));
	TestFalse(TEXT("Migration disables legacy shared-material tint on first"), First.Parameters.bUseTint);
	TestFalse(TEXT("Migration disables legacy shared-material tint on second"), Second.Parameters.bUseTint);
	TestEqual(TEXT("Migration uses chest reference-pose placement"), First.Parameters.PlacementPreset, EProjectAutomaticTattooPlacementPreset::ChestFront);
	TestTrue(TEXT("Legacy Offset.R becomes OffsetX"), FMath::IsNearlyEqual(First.Parameters.OffsetX, 2.0f));
	TestTrue(TEXT("Legacy Offset.G becomes OffsetY"), FMath::IsNearlyEqual(First.Parameters.OffsetY, -3.0f));
	TestTrue(TEXT("Largest legacy scale maps to SkinnedDecal size"), FMath::IsNearlyEqual(First.Parameters.Size, 11.0f));
	TestTrue(TEXT("Legacy radians map to degrees"), FMath::IsNearlyEqual(First.Parameters.RotationDegrees, 90.0f));
	TestTrue(TEXT("Migration supplies projection distance"), FMath::IsNearlyEqual(First.Parameters.ProjectionDistance, 12.0f));
	TestTrue(TEXT("Migration retains legacy scale for rollback"), First.Parameters.Scale.Equals(FVector2D(0.5, 0.25)));
	TestTrue(TEXT("Only the missing PNG record receives the diagnostic"), Missing.Parameters.bRuntimeTextureMissing);
	TestFalse(TEXT("Only the missing PNG record is disabled"), Missing.Parameters.bEnabled);
	TestTrue(TEXT("A valid asset-backed record remains enabled"), First.Parameters.bEnabled && Second.Parameters.bEnabled);

	const UProjectTattooShopSaveGame* BackupSave = LoadTattooSave(LegacyBackupSlotName);
	const UProjectTattooShopSaveGame* MigratedSave = LoadTattooSave(PrimarySlotName);
	TestNotNull(TEXT("First v5 write creates a v4 backup"), BackupSave);
	TestNotNull(TEXT("Primary slot is readable after migration"), MigratedSave);
	if (BackupSave)
	{
		TestEqual(TEXT("Backup retains schema v4"), BackupSave->Version, 4);
		const FProjectTattooRecord* BackupFirst = FindRecord(BackupSave, FirstId);
		TestNotNull(TEXT("Backup retains the first record"), BackupFirst);
		if (BackupFirst)
		{
			TestTrue(TEXT("Backup retains original scale"), BackupFirst->Parameters.Scale.Equals(FVector2D(0.5, 0.25)));
			TestTrue(TEXT("Backup retains original selected color"), BackupFirst->Parameters.Color.Equals(FirstColor));
		}
	}
	if (MigratedSave)
	{
		TestEqual(TEXT("Primary save advances to schema v5"), MigratedSave->Version, 5);
	}

	// Two simultaneous edits prove that each GUID owns an independent snapshot.
	TestTrue(TEXT("First edit begins"), State->BeginEdit(FirstId));
	TestTrue(TEXT("Second edit begins independently"), State->BeginEdit(SecondId));
	FProjectTattooParameters FirstPreview = First.Parameters;
	FirstPreview.Size = 31.0f;
	FirstPreview.Color = FLinearColor::Green;
	FProjectTattooParameters SecondPreview = Second.Parameters;
	SecondPreview.Size = 47.0f;
	SecondPreview.Color = FLinearColor::Yellow;
	TestTrue(TEXT("First preview succeeds"), State->Preview(FirstId, FirstPreview));
	TestTrue(TEXT("Second preview succeeds"), State->Preview(SecondId, SecondPreview));
	TestTrue(TEXT("Committing first succeeds"), State->Commit(FirstId));

	const UProjectTattooShopSaveGame* OneCommittedSave = LoadTattooSave(PrimarySlotName);
	const FProjectTattooRecord* SavedFirst = FindRecord(OneCommittedSave, FirstId);
	const FProjectTattooRecord* SavedSecond = FindRecord(OneCommittedSave, SecondId);
	TestNotNull(TEXT("Committed first remains serialized"), SavedFirst);
	TestNotNull(TEXT("Uncommitted second remains serialized from its snapshot"), SavedSecond);
	if (SavedFirst && SavedSecond)
	{
		TestTrue(TEXT("First commit serializes only first preview"), FMath::IsNearlyEqual(SavedFirst->Parameters.Size, 31.0f));
		TestTrue(TEXT("First commit preserves the first preview RGB"), SavedFirst->Parameters.Color.Equals(FLinearColor::Green));
		TestTrue(TEXT("First commit does not serialize second preview"), FMath::IsNearlyEqual(SavedSecond->Parameters.Size, Second.Parameters.Size));
		TestTrue(TEXT("Second serialized RGB remains original"), SavedSecond->Parameters.Color.Equals(SecondColor));
	}
	FProjectTattooRecord InMemorySecond;
	TestTrue(TEXT("Second remains in memory while previewing"), State->GetTattooRecord(SecondId, InMemorySecond));
	TestTrue(TEXT("Second in-memory preview remains independent"), FMath::IsNearlyEqual(InMemorySecond.Parameters.Size, 47.0f));
	TestTrue(TEXT("Second cancel succeeds after first commit"), State->Cancel(SecondId));
	TestTrue(TEXT("Second cancel restores its own snapshot"), State->GetTattooRecord(SecondId, InMemorySecond)
		&& FMath::IsNearlyEqual(InMemorySecond.Parameters.Size, Second.Parameters.Size)
		&& InMemorySecond.Parameters.Color.Equals(SecondColor));

	// A generic save must serialize the committed snapshot, never an active preview.
	FProjectTattooRecord CommittedFirst;
	TestTrue(TEXT("Committed first can be read"), State->GetTattooRecord(FirstId, CommittedFirst));
	TestTrue(TEXT("A new edit can start after commit"), State->BeginEdit(FirstId));
	FProjectTattooParameters UncommittedFirstPreview = CommittedFirst.Parameters;
	UncommittedFirstPreview.Size = 45.0f;
	TestTrue(TEXT("Uncommitted first preview succeeds"), State->Preview(FirstId, UncommittedFirstPreview));
	TestTrue(TEXT("Generic SaveState succeeds during a preview"), State->SaveState());
	const UProjectTattooShopSaveGame* PreviewSafeSave = LoadTattooSave(PrimarySlotName);
	SavedFirst = FindRecord(PreviewSafeSave, FirstId);
	TestNotNull(TEXT("First remains serialized during preview"), SavedFirst);
	if (SavedFirst)
	{
		TestTrue(TEXT("Generic save excludes the uncommitted preview"), FMath::IsNearlyEqual(SavedFirst->Parameters.Size, 31.0f));
	}
	TestTrue(TEXT("Cancel restores the committed first value"), State->Cancel(FirstId));
	TestTrue(TEXT("Restored first is still 31"), State->GetTattooRecord(FirstId, CommittedFirst)
		&& FMath::IsNearlyEqual(CommittedFirst.Parameters.Size, 31.0f));

	// New drafts also remain transient until their own GUID is committed.
	FProjectTattooParameters DraftParameters;
	DraftParameters.TextureAssetPath = FSoftObjectPath(TEXT("/Game/TattooShop/Texture/T_Pentagram.T_Pentagram"));
	DraftParameters.Color = FLinearColor::Blue;
	const FGuid DraftId = State->BeginCreate(DraftParameters);
	TestTrue(TEXT("BeginCreate returns a valid GUID"), DraftId.IsValid());
	TestTrue(TEXT("BeginCreate returns a distinct GUID"), DraftId != FirstId && DraftId != SecondId);
	TestTrue(TEXT("Draft owns an active transaction"), State->IsTransactionActive(DraftId));
	TestTrue(TEXT("Generic save succeeds while draft exists"), State->SaveState());
	const UProjectTattooShopSaveGame* DraftSafeSave = LoadTattooSave(PrimarySlotName);
	TestNull(TEXT("Generic save excludes uncommitted draft"), FindRecord(DraftSafeSave, DraftId));
	TestTrue(TEXT("Draft commit succeeds"), State->Commit(DraftId));
	const UProjectTattooShopSaveGame* DraftCommittedSave = LoadTattooSave(PrimarySlotName);
	TestNotNull(TEXT("Committed draft is serialized"), FindRecord(DraftCommittedSave, DraftId));

	TestTrue(TEXT("Delete removes only the selected second GUID"), State->Delete(SecondId));
	const UProjectTattooShopSaveGame* DeleteSave = LoadTattooSave(PrimarySlotName);
	TestNull(TEXT("Deleted second GUID is absent from save"), FindRecord(DeleteSave, SecondId));
	TestNotNull(TEXT("Deleting second preserves first GUID"), FindRecord(DeleteSave, FirstId));
	TestNotNull(TEXT("Deleting second preserves committed draft GUID"), FindRecord(DeleteSave, DraftId));
	TestNotNull(TEXT("Deleting second preserves independent missing-PNG record"), FindRecord(DeleteSave, MissingPngId));
	return true;
}

#endif
