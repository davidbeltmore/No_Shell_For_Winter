#include "Companions/ProjectRunCompanionSubsystem.h"

#include "ALSSaveTypes.h"
#include "Actors/ACFCharacter.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoDungeonDirectorMathV4.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Calysto/ProjectCalystoPopulationBridgeV4.h"
#include "Companions/ProjectCompanionDeathProxyComponent.h"
#include "Companions/ProjectCompanionRevivalConsumable.h"
#include "Companions/ProjectCompanionRevivalMenuWidget.h"
#include "Companions/ProjectCompanionRuntimeAdapter.h"
#include "Companions/ProjectRecruitableCompanionComponent.h"
#include "Components/ACFCompanionGroupAIComponent.h"
#include "Components/ACFEquipmentComponent.h"
#include "Components/ACFInventoryComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Items/ACFItem.h"
#include "Items/ACFItemFragment.h"
#include "Social/ProjectSocialSubsystem.h"
#include "Social/ProjectSocialTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectRunCompanions, Log, All);

#define LOCTEXT_NAMESPACE "ProjectRunCompanionSubsystem"

namespace ProjectRunCompanionPrivate
{
	constexpr int32 MaximumActiveParty = 2;

	FString GuidKey(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}

	uint32 FloatBits(const float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	FString HashBytes(const TArray<uint8>& Bytes)
	{
		if (Bytes.IsEmpty())
		{
			return FString();
		}
		static constexpr TCHAR HexDigits[] = TEXT("0123456789abcdef");
		FString Hex;
		Hex.Reserve(Bytes.Num() * 2);
		for (const uint8 Byte : Bytes)
		{
			Hex.AppendChar(HexDigits[(Byte >> 4) & 0x0f]);
			Hex.AppendChar(HexDigits[Byte & 0x0f]);
		}
		return UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Hex);
	}

	void AppendSaveGameProperties(const UObject* Object, FString& InOutCanonical)
	{
		if (!Object)
		{
			InOutCanonical += TEXT("null;");
			return;
		}

		InOutCanonical += Object->GetClass()->GetPathName();
		InOutCanonical += TEXT("{");
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_SaveGame))
			{
				continue;
			}
			FString Exported;
			Property->ExportTextItem_Direct(
				Exported,
				Property->ContainerPtrToValuePtr<void>(Object),
				nullptr,
				const_cast<UObject*>(Object),
				PPF_None);
			InOutCanonical += Property->GetName();
			InOutCanonical += TEXT("=");
			InOutCanonical += Exported;
			InOutCanonical += TEXT(";");
		}
		InOutCanonical += TEXT("}");
	}

	EEFCalystoCompanionRosterStateV4 ToDirectorState(
		const EProjectCompanionRunState State,
		const bool bActiveParty)
	{
		switch (State)
		{
		case EProjectCompanionRunState::Alive:
			return bActiveParty
				? EEFCalystoCompanionRosterStateV4::ActiveParty
				: EEFCalystoCompanionRosterStateV4::RecruitedInactive;
		case EProjectCompanionRunState::PendingDead:
		case EProjectCompanionRunState::PendingRevival:
			return EEFCalystoCompanionRosterStateV4::PendingDead;
		case EProjectCompanionRunState::Dead:
		default:
			return EEFCalystoCompanionRosterStateV4::Dead;
		}
	}

	EEFCalystoGenderV4 ToDirectorGender(const FName Gender)
	{
		if (Gender.IsEqual(TEXT("Female"), ENameCase::IgnoreCase))
		{
			return EEFCalystoGenderV4::Female;
		}
		if (Gender.IsEqual(TEXT("Male"), ENameCase::IgnoreCase))
		{
			return EEFCalystoGenderV4::Male;
		}
		return EEFCalystoGenderV4::Any;
	}

	EEFCalystoRarityTierV4 ToDirectorGrade(const EProjectCompanionDifficultyGrade Grade)
	{
		switch (Grade)
		{
		case EProjectCompanionDifficultyGrade::Uncommon: return EEFCalystoRarityTierV4::Uncommon;
		case EProjectCompanionDifficultyGrade::Rare: return EEFCalystoRarityTierV4::Rare;
		case EProjectCompanionDifficultyGrade::Epic: return EEFCalystoRarityTierV4::Epic;
		case EProjectCompanionDifficultyGrade::Winter: return EEFCalystoRarityTierV4::Winter;
		case EProjectCompanionDifficultyGrade::Common:
		default: return EEFCalystoRarityTierV4::Common;
		}
	}

	EProjectCompanionDifficultyGrade FromDirectorGrade(const EEFCalystoRarityTierV4 Grade)
	{
		switch (Grade)
		{
		case EEFCalystoRarityTierV4::Uncommon: return EProjectCompanionDifficultyGrade::Uncommon;
		case EEFCalystoRarityTierV4::Rare: return EProjectCompanionDifficultyGrade::Rare;
		case EEFCalystoRarityTierV4::Epic: return EProjectCompanionDifficultyGrade::Epic;
		case EEFCalystoRarityTierV4::Winter: return EProjectCompanionDifficultyGrade::Winter;
		case EEFCalystoRarityTierV4::Common:
		default: return EProjectCompanionDifficultyGrade::Common;
		}
	}

	FName FromDirectorGender(const EEFCalystoGenderV4 Gender)
	{
		switch (Gender)
		{
		case EEFCalystoGenderV4::Female: return TEXT("Female");
		case EEFCalystoGenderV4::Male: return TEXT("Male");
		case EEFCalystoGenderV4::Any:
		default: return TEXT("Any");
		}
	}

	bool DefinitionsMatchExactly(
		const FProjectCompanionDefinition& Left,
		const FProjectCompanionDefinition& Right)
	{
		return Left.StableCompanionId == Right.StableCompanionId
			&& Left.SourceSpawnId == Right.SourceSpawnId
			&& Left.ContentId == Right.ContentId
			&& Left.CatalogVariantId == Right.CatalogVariantId
			&& Left.CharacterClass.ToSoftObjectPath()
				== Right.CharacterClass.ToSoftObjectPath()
			&& Left.Archetype == Right.Archetype
			&& Left.Gender == Right.Gender
			&& Left.DifficultyGrade == Right.DifficultyGrade
			&& Left.ResolvedLevel == Right.ResolvedLevel
			&& Left.Lifecycle == Right.Lifecycle
			&& Left.bRepairMissingStatisticsRow == Right.bRepairMissingStatisticsRow
			&& Left.StatisticsRepairDataTable.ToSoftObjectPath()
				== Right.StatisticsRepairDataTable.ToSoftObjectPath()
			&& Left.StatisticsRepairRow == Right.StatisticsRepairRow;
	}

	bool SetExactGroupMembership(
		UACFCompanionGroupAIComponent* Group,
		AACFCharacter* Character,
		const bool bShouldBelong,
		FString& OutError)
	{
		OutError.Reset();
		if (!Group || !IsValid(Character))
		{
			OutError = TEXT("The typed ACF companion group or live actor is unavailable.");
			return false;
		}

		const bool bAlreadyBelongs = Group->IsAlreadyInGroup(Character);
		if (bAlreadyBelongs == bShouldBelong)
		{
			return true;
		}

		const bool bOperationSucceeded = bShouldBelong
			? Group->AddExistingCharacterToGroup(Character)
			: Group->RemoveAgentFromGroup(Character);
		if (!bOperationSucceeded || Group->IsAlreadyInGroup(Character) != bShouldBelong)
		{
			OutError = bShouldBelong
				? TEXT("ACF could not atomically add the companion to the active group.")
				: TEXT("ACF could not atomically remove the companion from the active group.");
			return false;
		}
		return true;
	}
}

void UProjectRunCompanionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UEFCalystoDungeonSubsystem>();
	BindDirectorEvents();
}

void UProjectRunCompanionSubsystem::Deinitialize()
{
	CloseRevivalMenu();
	UnbindDirectorEvents();
	DestroyLiveRosterProjections();
	Roster.Reset();
	RetainedCompanionClasses.Reset();
	ResetInventoryTravelCapsule();
	Super::Deinitialize();
}

void UProjectRunCompanionSubsystem::BindDirectorEvents()
{
	if (bDirectorEventsBound || !GetGameInstance())
	{
		return;
	}
	UEFCalystoDungeonSubsystem* Director = GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>();
	if (!Director)
	{
		UE_LOG(LogProjectRunCompanions, Error, TEXT("Calysto Director subsystem is unavailable; companion travel gates remain fail-closed."));
		return;
	}
	Director->OnBeforeAnyDirectorTravel().AddUObject(this, &ThisClass::HandleBeforeDirectorTravel);
	Director->OnDirectorWorldAccepted().AddUObject(this, &ThisClass::HandleDirectorWorldAccepted);
	Director->OnNewRunInitialized().AddUObject(this, &ThisClass::HandleNewRunInitialized);
	Director->OnFloorReady().AddUObject(this, &ThisClass::HandleFloorReady);
	Director->OnFloorTravelFailed().AddUObject(this, &ThisClass::HandleFloorTravelFailed);
	bDirectorEventsBound = true;
}

void UProjectRunCompanionSubsystem::UnbindDirectorEvents()
{
	if (!bDirectorEventsBound || !GetGameInstance())
	{
		return;
	}
	if (UEFCalystoDungeonSubsystem* Director = GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>())
	{
		Director->OnBeforeAnyDirectorTravel().RemoveAll(this);
		Director->OnDirectorWorldAccepted().RemoveAll(this);
		Director->OnNewRunInitialized().RemoveAll(this);
		Director->OnFloorReady().RemoveAll(this);
		Director->OnFloorTravelFailed().RemoveAll(this);
	}
	bDirectorEventsBound = false;
}

bool UProjectRunCompanionSubsystem::RegisterRecruitedCompanion(
	const FProjectCompanionDefinition& Definition,
	AACFCharacter* LiveActor,
	const bool bJoinActiveParty)
{
	FString DefinitionError;
	if (!Definition.IsValid(DefinitionError)
		|| Definition.Lifecycle != EProjectCompanionLifecycle::Recruitable
		|| !IsValid(LiveActor)
		|| Roster.Contains(Definition.StableCompanionId))
	{
		UE_LOG(LogProjectRunCompanions, Warning, TEXT("Recruitment rejected: %s"),
			DefinitionError.IsEmpty() ? TEXT("invalid actor, lifecycle or duplicate stable ID") : *DefinitionError);
		return false;
	}

	APawn* PlayerPawn = ResolveLocalPlayerPawn();
	UACFCompanionGroupAIComponent* Group = UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(PlayerPawn);
	if (!Group)
	{
		return false;
	}
	if (bJoinActiveParty)
	{
		int32 ActiveLiving = 0;
		for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
		{
			if (Pair.Value.bDesiredActiveParty && Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive)
			{
				++ActiveLiving;
			}
		}
		if (ActiveLiving >= ProjectRunCompanionPrivate::MaximumActiveParty)
		{
			return false;
		}
	}

	const bool bWasInGroup = Group->IsAlreadyInGroup(LiveActor);
	FString GroupError;
	if (!ProjectRunCompanionPrivate::SetExactGroupMembership(
		Group, LiveActor, bJoinActiveParty, GroupError))
	{
		UE_LOG(LogProjectRunCompanions, Warning, TEXT("Recruitment group transaction rejected: %s"), *GroupError);
		return false;
	}

	FRuntimeCompanionRecord Record;
	Record.Snapshot.Definition = Definition;
	Record.Snapshot.State = EProjectCompanionRunState::Alive;
	Record.bDesiredActiveParty = bJoinActiveParty;
	Record.LiveActor = LiveActor;
	Roster.Add(Definition.StableCompanionId, Record);
	RetainedCompanionClasses.AddUnique(LiveActor->GetClass());

	if (!AttachDeathProxy(LiveActor, Definition.StableCompanionId))
	{
		Roster.Remove(Definition.StableCompanionId);
		FString RollbackError;
		if (!ProjectRunCompanionPrivate::SetExactGroupMembership(
			Group, LiveActor, bWasInGroup, RollbackError))
		{
			UE_LOG(LogProjectRunCompanions, Error,
				TEXT("Recruitment rollback could not restore ACF group membership: %s"),
				*RollbackError);
		}
		return false;
	}

	if (UProjectSocialSubsystem* Social = GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>())
	{
		Social->SetRecruitedCompanion(LiveActor, true);
	}
	OnRosterChanged.Broadcast();
	return true;
}

bool UProjectRunCompanionSubsystem::SetCompanionActivePartyMembership(
	const FGuid StableCompanionId,
	const bool bActive)
{
	FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	if (!Record)
	{
		return false;
	}
	if (!bFloorReady || bGenerationOrTravelActive)
	{
		return false;
	}
	if (Record->Snapshot.State != EProjectCompanionRunState::Alive)
	{
		return !bActive && !Record->bDesiredActiveParty;
	}
	AACFCharacter* LiveActor = Record->LiveActor.Get();
	UACFCompanionGroupAIComponent* Group =
		UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(ResolveLocalPlayerPawn());
	if (!IsValid(LiveActor) || !Group)
	{
		return false;
	}
	if (bActive && !Record->bDesiredActiveParty)
	{
		int32 ActiveCount = 0;
		for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
		{
			ActiveCount += ((Pair.Value.bDesiredActiveParty
				&& Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive) ? 1 : 0);
		}
		if (ActiveCount >= ProjectRunCompanionPrivate::MaximumActiveParty)
		{
			return false;
		}
	}
	FString GroupError;
	if (!ProjectRunCompanionPrivate::SetExactGroupMembership(
		Group, LiveActor, bActive, GroupError))
	{
		UE_LOG(LogProjectRunCompanions, Warning,
			TEXT("Active-party transaction rejected for %s: %s"),
			*ProjectRunCompanionPrivate::GuidKey(StableCompanionId),
			*GroupError);
		return false;
	}
	if (Record->bDesiredActiveParty == bActive)
	{
		return true;
	}
	Record->bDesiredActiveParty = bActive;
	OnRosterChanged.Broadcast();
	return true;
}

FProjectCompanionRunSnapshot UProjectRunCompanionSubsystem::BuildSnapshot() const
{
	FProjectCompanionRunSnapshot Result;
	Result.RunEpoch = RunEpoch;
	Result.FloorNumber = CurrentFloor;
	Result.GenerationSerial = CurrentGenerationSerial;

	TArray<FGuid> SortedIds;
	Roster.GetKeys(SortedIds);
	SortedIds.Sort([](const FGuid& Left, const FGuid& Right)
	{
		return ProjectRunCompanionPrivate::GuidKey(Left) < ProjectRunCompanionPrivate::GuidKey(Right);
	});

	for (const FGuid& StableId : SortedIds)
	{
		const FRuntimeCompanionRecord& Record = Roster.FindChecked(StableId);
		Result.Entries.Add(Record.Snapshot);
		if (Record.bDesiredActiveParty
			&& Record.Snapshot.State == EProjectCompanionRunState::Alive)
		{
			Result.ActiveParty.Add(StableId);
		}
	}
	Result.RefreshHash();
	return Result;
}

FProjectCompanionRunSnapshot UProjectRunCompanionSubsystem::GetRunRosterSnapshot() const
{
	return BuildSnapshot();
}

FEFCalystoCompanionSnapshotV4 UProjectRunCompanionSubsystem::BuildDirectorSnapshot(
	const FProjectCompanionRunSnapshot& Source) const
{
	FEFCalystoCompanionSnapshotV4 Result;
	Result.bPlayerOwnsWintersRecall = PlayerOwnsWintersRecall(ResolveLocalPlayerPawn());

	TArray<FProjectCompanionRunEntrySnapshot> SortedEntries = Source.Entries;
	SortedEntries.Sort([](const FProjectCompanionRunEntrySnapshot& Left, const FProjectCompanionRunEntrySnapshot& Right)
	{
		return ProjectRunCompanionPrivate::GuidKey(Left.Definition.StableCompanionId)
			< ProjectRunCompanionPrivate::GuidKey(Right.Definition.StableCompanionId);
	});
	for (const FProjectCompanionRunEntrySnapshot& SourceEntry : SortedEntries)
	{
		const bool bActiveParty = Source.ActiveParty.Contains(SourceEntry.Definition.StableCompanionId)
			&& SourceEntry.State == EProjectCompanionRunState::Alive;
		FEFCalystoCompanionRecordV4& Entry = Result.Records.AddDefaulted_GetRef();
		Entry.StableCompanionId = SourceEntry.Definition.StableCompanionId;
		Entry.SourceSpawnId = SourceEntry.Definition.SourceSpawnId;
		Entry.SourceCatalogId = SourceEntry.Definition.ContentId;
		Entry.SourceVariantId = SourceEntry.Definition.CatalogVariantId;
		Entry.ActorClass = TSoftClassPtr<AActor>(SourceEntry.Definition.CharacterClass.ToSoftObjectPath());
		Entry.Archetype = SourceEntry.Definition.Archetype;
		Entry.Gender = ProjectRunCompanionPrivate::ToDirectorGender(SourceEntry.Definition.Gender);
		Entry.Grade = ProjectRunCompanionPrivate::ToDirectorGrade(SourceEntry.Definition.DifficultyGrade);
		Entry.State = ProjectRunCompanionPrivate::ToDirectorState(SourceEntry.State, bActiveParty);
	}
	return Result;
}

FEFCalystoCompanionSnapshotV4
UProjectRunCompanionSubsystem::BuildAcceptedRosterValidationSnapshot(
	const FProjectCompanionRunSnapshot& Source) const
{
	FEFCalystoCompanionSnapshotV4 Result = BuildDirectorSnapshot(Source);
	// bPlayerOwnsWintersRecall is an input to chest eligibility, not companion
	// roster topology. Replay intentionally keeps the original FloorIntent while
	// the independent typed ACF inventory capsule preserves items obtained or
	// consumed during play. Retain the accepted bit only for intent-hash
	// validation; the inventory capsule validates the live ownership separately.
	Result.bPlayerOwnsWintersRecall = bAcceptedIntentPlayerOwnsWintersRecall;
	return Result;
}

bool UProjectRunCompanionSubsystem::RestoreSnapshot(
	const FProjectCompanionRunSnapshot& Snapshot,
	FString& OutError)
{
	OutError.Reset();
	if (!Snapshot.IsValid(OutError))
	{
		return false;
	}

	TMap<FGuid, FRuntimeCompanionRecord> RestoredRoster;
	RestoredRoster.Reserve(Snapshot.Entries.Num());
	TArray<UClass*> ClassesToRetain;
	for (const FProjectCompanionRunEntrySnapshot& Entry : Snapshot.Entries)
	{
		FRuntimeCompanionRecord Record;
		Record.Snapshot = Entry;
		Record.bDesiredActiveParty = Snapshot.ActiveParty.Contains(Entry.Definition.StableCompanionId);
		RestoredRoster.Add(Entry.Definition.StableCompanionId, MoveTemp(Record));
		if (UClass* LoadedClass = Entry.Definition.CharacterClass.Get())
		{
			ClassesToRetain.AddUnique(LoadedClass);
		}
	}

	Roster = MoveTemp(RestoredRoster);
	for (UClass* LoadedClass : ClassesToRetain)
	{
		RetainedCompanionClasses.AddUnique(LoadedClass);
	}
	RunEpoch = Snapshot.RunEpoch;
	CurrentFloor = Snapshot.FloorNumber;
	CurrentGenerationSerial = Snapshot.GenerationSerial;
	return true;
}

void UProjectRunCompanionSubsystem::BroadcastAcceptedRosterChanges(
	const FProjectCompanionRunSnapshot& Previous,
	const FProjectCompanionRunSnapshot& Accepted)
{
	TMap<FGuid, EProjectCompanionRunState> PreviousStates;
	for (const FProjectCompanionRunEntrySnapshot& Entry : Previous.Entries)
	{
		PreviousStates.Add(Entry.Definition.StableCompanionId, Entry.State);
	}

	TArray<FGuid> ChangedIds;
	TMap<FGuid, EProjectCompanionRunState> AcceptedStates;
	for (const FProjectCompanionRunEntrySnapshot& Entry : Accepted.Entries)
	{
		AcceptedStates.Add(Entry.Definition.StableCompanionId, Entry.State);
		const EProjectCompanionRunState* PreviousState =
			PreviousStates.Find(Entry.Definition.StableCompanionId);
		if (PreviousState && *PreviousState != Entry.State)
		{
			ChangedIds.Add(Entry.Definition.StableCompanionId);
		}
	}
	ChangedIds.Sort([](const FGuid& Left, const FGuid& Right)
	{
		return ProjectRunCompanionPrivate::GuidKey(Left)
			< ProjectRunCompanionPrivate::GuidKey(Right);
	});
	for (const FGuid& StableId : ChangedIds)
	{
		OnDeathStateChanged.Broadcast(StableId, AcceptedStates.FindChecked(StableId));
	}
	if (Previous.SnapshotHash != Accepted.SnapshotHash)
	{
		OnRosterChanged.Broadcast();
	}
}

bool UProjectRunCompanionSubsystem::ApplyResolvedCompanionLevels(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	FString& OutError)
{
	OutError.Reset();
	if (!Intent.bIsValid || Intent.FloorNumber != CurrentFloor
		|| Intent.GenerationSerial != CurrentGenerationSerial
		|| Intent.ResolvedCompanionLevels.Num() != Roster.Num())
	{
		OutError = TEXT("The accepted intent does not provide exactly one frozen level for every roster record.");
		return false;
	}

	TMap<FGuid, int32> ValidatedLevels;
	ValidatedLevels.Reserve(Intent.ResolvedCompanionLevels.Num());
	for (const FEFCalystoResolvedCompanionLevelV4& Level : Intent.ResolvedCompanionLevels)
	{
		const FRuntimeCompanionRecord* Record = Roster.Find(Level.StableCompanionId);
		if (!Level.StableCompanionId.IsValid() || !Record
			|| ValidatedLevels.Contains(Level.StableCompanionId)
			|| Level.LogicalLevel < 1
			|| Level.PhysicalACFLevel != FMath::Min(Level.LogicalLevel, 100)
			|| ProjectRunCompanionPrivate::ToDirectorGrade(
				Record->Snapshot.Definition.DifficultyGrade) != Level.Grade)
		{
			OutError = FString::Printf(
				TEXT("Invalid, duplicated or grade-mismatched frozen level for companion %s."),
				*Level.StableCompanionId.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		ValidatedLevels.Add(Level.StableCompanionId, Level.LogicalLevel);
	}

	for (TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		const int32* LogicalLevel = ValidatedLevels.Find(Pair.Key);
		if (!LogicalLevel)
		{
			OutError = FString::Printf(
				TEXT("The frozen level array omitted roster companion %s."),
				*Pair.Key.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		Pair.Value.Snapshot.Definition.ResolvedLevel = *LogicalLevel;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::ResolveActiveIntentCompanionLevel(
	const FGuid StableCompanionId,
	FEFCalystoResolvedCompanionLevelV4& OutLevel,
	FString& OutError) const
{
	OutLevel = FEFCalystoResolvedCompanionLevelV4();
	OutError.Reset();
	const UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorIntentV4 Intent = Director
		? Director->GetResolvedFloorIntent()
		: FEFCalystoResolvedFloorIntentV4();
	if (!StableCompanionId.IsValid() || !Intent.bIsValid
		|| Intent.FloorNumber != CurrentFloor
		|| Intent.GenerationSerial != CurrentGenerationSerial)
	{
		OutError = TEXT("The active V4 intent does not match the current companion floor identity.");
		return false;
	}

	int32 MatchCount = 0;
	for (const FEFCalystoResolvedCompanionLevelV4& Candidate : Intent.ResolvedCompanionLevels)
	{
		if (Candidate.StableCompanionId == StableCompanionId)
		{
			OutLevel = Candidate;
			++MatchCount;
		}
	}
	const FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	if (MatchCount != 1 || !Record || OutLevel.LogicalLevel < 1
		|| OutLevel.PhysicalACFLevel != FMath::Min(OutLevel.LogicalLevel, 100)
		|| ProjectRunCompanionPrivate::ToDirectorGrade(
			Record->Snapshot.Definition.DifficultyGrade) != OutLevel.Grade)
	{
		OutError = TEXT("The selected companion has no unique, valid frozen level in the active V4 intent.");
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::ResolveSameFloorRecruitedRevivalLevel(
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FProjectCompanionRunSnapshot& FloorStart,
	const FProjectCompanionRunEntrySnapshot& CurrentRecord,
	FEFCalystoResolvedCompanionLevelV4& OutLevel,
	FString& OutError)
{
	OutLevel = FEFCalystoResolvedCompanionLevelV4();
	OutError.Reset();
	FString DefinitionError;
	FString FloorStartError;
	const FGuid StableId = CurrentRecord.Definition.StableCompanionId;
	if (!Intent.bIsValid || Intent.GeneratorVersion != 4
		|| !StableId.IsValid()
		|| !CurrentRecord.Definition.IsValid(DefinitionError)
		|| !FloorStart.IsValid(FloorStartError)
		|| FloorStart.FloorNumber != Intent.FloorNumber
		|| FloorStart.GenerationSerial != Intent.GenerationSerial
		|| CurrentRecord.State != EProjectCompanionRunState::PendingDead
		|| CurrentRecord.DeathFloor != Intent.FloorNumber
		|| CurrentRecord.DeathGenerationSerial != Intent.GenerationSerial
		|| CurrentRecord.Definition.Lifecycle != EProjectCompanionLifecycle::Recruitable)
	{
		OutError = TEXT("The same-floor revival record, floor-start snapshot or intent identity is invalid.");
		return false;
	}

	const bool bWasPresentAtFloorStart = FloorStart.Entries.ContainsByPredicate(
		[StableId](const FProjectCompanionRunEntrySnapshot& Entry)
		{
			return Entry.Definition.StableCompanionId == StableId;
		});
	const bool bWasSubmittedInIntent = Intent.CompanionSnapshot.Records.ContainsByPredicate(
		[StableId](const FEFCalystoCompanionRecordV4& Entry)
		{
			return Entry.StableCompanionId == StableId;
		});
	const bool bHasFrozenRosterLevel = Intent.ResolvedCompanionLevels.ContainsByPredicate(
		[StableId](const FEFCalystoResolvedCompanionLevelV4& Entry)
		{
			return Entry.StableCompanionId == StableId;
		});
	if (bWasPresentAtFloorStart || bWasSubmittedInIntent || bHasFrozenRosterLevel)
	{
		OutError = TEXT("The companion is not a uniquely new recruit from the active floor.");
		return false;
	}

	int32 MatchingDirectiveCount = 0;
	FEFCalystoSpawnInstanceDirectiveV4 MatchingDirective;
	FProjectCompanionDefinition MatchingDefinition;
	for (const FEFCalystoSpawnInstanceDirectiveV4& Directive : Intent.SpawnDirectives)
	{
		if (Directive.Category != EEFCalystoContentCategoryV4::NPC
			|| Directive.Lifecycle != EEFCalystoLifecycleV4::Recruitable
			|| Directive.StableCompanionId.IsValid())
		{
			continue;
		}

		FProjectCompanionDefinition CandidateDefinition;
		FString CandidateError;
		if (!FProjectCalystoPopulationBridgeV4::BuildRandomNPCDefinitionFromIntent(
			Intent, Directive, CandidateDefinition, CandidateError))
		{
			OutError = CandidateError.IsEmpty()
				? TEXT("A recruitable NPC directive in the active intent is invalid.")
				: CandidateError;
			return false;
		}
		if (CandidateDefinition.StableCompanionId == StableId)
		{
			++MatchingDirectiveCount;
			MatchingDirective = Directive;
			MatchingDefinition = MoveTemp(CandidateDefinition);
		}
	}

	if (MatchingDirectiveCount != 1
		|| !ProjectRunCompanionPrivate::DefinitionsMatchExactly(
			CurrentRecord.Definition, MatchingDefinition)
		|| MatchingDirective.LogicalLevel < 1
		|| MatchingDirective.PhysicalACFLevel
			!= FMath::Min(MatchingDirective.LogicalLevel, 100))
	{
		OutError = TEXT("The same-floor recruit does not match exactly one frozen NPC directive.");
		return false;
	}

	OutLevel.StableCompanionId = StableId;
	OutLevel.Grade = MatchingDirective.Tier;
	OutLevel.LogicalLevel = MatchingDirective.LogicalLevel;
	OutLevel.PhysicalACFLevel = MatchingDirective.PhysicalACFLevel;
	return true;
}

bool UProjectRunCompanionSubsystem::ResolveFrozenRevivalCompanionLevel(
	const FGuid StableCompanionId,
	FEFCalystoResolvedCompanionLevelV4& OutLevel,
	FString& OutError) const
{
	OutLevel = FEFCalystoResolvedCompanionLevelV4();
	OutError.Reset();
	const UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const FEFCalystoResolvedFloorIntentV4 Intent = Director
		? Director->GetResolvedFloorIntent()
		: FEFCalystoResolvedFloorIntentV4();
	if (!StableCompanionId.IsValid() || !Intent.bIsValid
		|| Intent.FloorNumber != CurrentFloor
		|| Intent.GenerationSerial != CurrentGenerationSerial)
	{
		OutError = TEXT("The active V4 intent does not match the current revival floor identity.");
		return false;
	}

	int32 FrozenRosterMatches = 0;
	for (const FEFCalystoResolvedCompanionLevelV4& Candidate : Intent.ResolvedCompanionLevels)
	{
		FrozenRosterMatches += Candidate.StableCompanionId == StableCompanionId ? 1 : 0;
	}
	if (FrozenRosterMatches == 1)
	{
		return ResolveActiveIntentCompanionLevel(StableCompanionId, OutLevel, OutError);
	}
	if (FrozenRosterMatches != 0)
	{
		OutError = TEXT("The active V4 intent contains duplicate frozen levels for the revival companion.");
		return false;
	}

	const FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	if (!Record)
	{
		OutError = TEXT("The same-floor revival companion is absent from the canonical roster.");
		return false;
	}
	return ResolveSameFloorRecruitedRevivalLevel(
		Intent, FloorStartSnapshot, Record->Snapshot, OutLevel, OutError);
}

void UProjectRunCompanionSubsystem::HandleBeforeDirectorTravel(const EEFCalystoDungeonTravelKindV4 TravelKind)
{
	UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!Director)
	{
		return;
	}

	if (RevivalTransaction.bActive)
	{
		FinishRevivalTransaction(false, LOCTEXT("TravelCancelledRevival", "Travel cancelled the revival without consuming the item."));
	}
	PendingTravelMode = ConvertTravelMode(TravelKind);
	if (PendingTravelMode == EProjectCompanionDirectorTravelMode::None)
	{
		Director->ReportDirectorTravelPreparationFailure(
			TEXT("COMPANION_TRAVEL_KIND_INVALID"),
			TEXT("The project companion adapter received an unsupported Director travel kind."));
		return;
	}
	if (TravelKind != EEFCalystoDungeonTravelKindV4::NewRun)
	{
		FString RecruitmentError;
		if (!SynchronizeRecruitmentsBeforeTravel(RecruitmentError))
		{
			Director->ReportDirectorTravelPreparationFailure(
				TEXT("COMPANION_RECRUITMENT_SYNC_FAILED"), RecruitmentError);
			PendingTravelMode = EProjectCompanionDirectorTravelMode::None;
			return;
		}
	}

	PreTravelSnapshot = BuildSnapshot();
	PendingDestinationSnapshot = PreTravelSnapshot;

	if (TravelKind == EEFCalystoDungeonTravelKindV4::Replay
		|| TravelKind == EEFCalystoDungeonTravelKindV4::Reroll)
	{
		PendingDestinationSnapshot = FloorStartSnapshot;
	}
	else if (TravelKind == EEFCalystoDungeonTravelKindV4::Advance
		|| TravelKind == EEFCalystoDungeonTravelKindV4::DebugJump)
	{
		for (FProjectCompanionRunEntrySnapshot& Entry : PendingDestinationSnapshot.Entries)
		{
			if (Entry.State == EProjectCompanionRunState::PendingDead)
			{
				Entry.State = EProjectCompanionRunState::Dead;
			}
		}
		PendingDestinationSnapshot.ActiveParty.RemoveAll(
			[&PendingSnapshot = PendingDestinationSnapshot](const FGuid& StableId)
			{
				const FProjectCompanionRunEntrySnapshot* Entry =
					PendingSnapshot.Entries.FindByPredicate(
						[&StableId](const FProjectCompanionRunEntrySnapshot& Candidate)
						{
							return Candidate.Definition.StableCompanionId == StableId;
						});
				return !Entry || Entry->State != EProjectCompanionRunState::Alive;
			});
		PendingDestinationSnapshot.RefreshHash();
	}

	if (TravelKind != EEFCalystoDungeonTravelKindV4::NewRun)
	{
		FString SnapshotError;
		if (!PendingDestinationSnapshot.IsValid(SnapshotError))
		{
			Director->ReportDirectorTravelPreparationFailure(
				TEXT("COMPANION_SNAPSHOT_INVALID"), SnapshotError);
			PendingDestinationSnapshot = FProjectCompanionRunSnapshot();
			PendingTravelMode = EProjectCompanionDirectorTravelMode::None;
			return;
		}
	}

	bFloorReady = false;
	bGenerationOrTravelActive = true;
	SetRosterReady(false);

	FEFCalystoCompanionSnapshotV4 DirectorSnapshot;
	if (TravelKind == EEFCalystoDungeonTravelKindV4::NewRun)
	{
		// New Run deliberately freezes an empty destination roster and no Recall.
		// The current run/inventory remain untouched until the destination world is
		// accepted, so a failed request can still restore the pre-travel state.
		DirectorSnapshot = FEFCalystoCompanionSnapshotV4();
	}
	else
	{
		DirectorSnapshot = BuildDirectorSnapshot(PendingDestinationSnapshot);
	}
	if (!Director->SubmitCompanionRunSnapshot(DirectorSnapshot))
	{
		Director->ReportDirectorTravelPreparationFailure(
			TEXT("COMPANION_SNAPSHOT_REJECTED"),
			TEXT("The Director rejected the canonical project companion snapshot."));
		bGenerationOrTravelActive = false;
		return;
	}

	FString InventoryError;
	if (!CaptureInventoryForTravel(PendingTravelMode, InventoryError))
	{
		Director->ReportDirectorTravelPreparationFailure(TEXT("INVENTORY_TRAVEL_CAPTURE_FAILED"), InventoryError);
		bGenerationOrTravelActive = false;
	}
}

void UProjectRunCompanionSubsystem::HandleNewRunInitialized(const int64 NewRunEpoch)
{
	ResetRunState(NewRunEpoch);
}

void UProjectRunCompanionSubsystem::HandleDirectorWorldAccepted(
	const int64 AcceptedRunEpoch,
	const EEFCalystoDungeonTravelKindV4 TravelKind,
	const FEFCalystoResolvedFloorIntentV4& Intent)
{
	UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!Director)
	{
		return;
	}
	const FProjectCompanionRunSnapshot PreviousSnapshot = BuildSnapshot();

	const FEFCalystoResolvedFloorIntentV4& IntentV4 = Intent;
	if (!IntentV4.bIsValid || AcceptedRunEpoch <= 0 || IntentV4.CompanionSnapshotHash.IsEmpty())
	{
		Director->NotifyGenerationFailed(
			TEXT("COMPANION_V4_INTENT_MISSING"),
			TEXT("The accepted world has no valid V4 intent or companion snapshot hash."));
		return;
	}

	if (TravelKind != EEFCalystoDungeonTravelKindV4::NewRun)
	{
		// A watchdog/recovery retry re-accepts the frozen intent without issuing a
		// new external travel-preparation event. Reuse the already frozen floor
		// start roster instead of treating an empty PendingDestinationSnapshot as
		// an empty run.
		const FProjectCompanionRunSnapshot& SnapshotToRestore =
			PendingDestinationSnapshot.RunEpoch > 0
				? PendingDestinationSnapshot
				: FloorStartSnapshot;
		FString SnapshotError;
		if (!RestoreSnapshot(SnapshotToRestore, SnapshotError))
		{
			Director->NotifyGenerationFailed(
				TEXT("COMPANION_SNAPSHOT_RESTORE_INVALID"), SnapshotError);
			return;
		}
	}
	RunEpoch = AcceptedRunEpoch;
	CurrentFloor = IntentV4.FloorNumber;
	CurrentGenerationSerial = IntentV4.GenerationSerial;
	CurrentCompanionSnapshotHash = IntentV4.CompanionSnapshotHash;
	bAcceptedIntentPlayerOwnsWintersRecall =
		IntentV4.CompanionSnapshot.bPlayerOwnsWintersRecall;
	bGenerationOrTravelActive = true;
	bFloorReady = false;
	SetRosterReady(false);

	FString InventoryError;
	if (!RestoreAndVerifyInventoryAfterTravel(InventoryError))
	{
		Director->NotifyGenerationFailed(TEXT("INVENTORY_TRAVEL_RESTORE_FAILED"), InventoryError);
		return;
	}
	if (TravelKind == EEFCalystoDungeonTravelKindV4::NewRun)
	{
		APawn* PlayerPawn = ResolveLocalPlayerPawn();
		if (!PurgeWintersRecallFromInventory(PlayerPawn, InventoryError))
		{
			Director->NotifyGenerationFailed(TEXT("NEW_RUN_RECALL_PURGE_FAILED"), InventoryError);
			return;
		}
	}

	FString CompanionLevelError;
	if (!ApplyResolvedCompanionLevels(IntentV4, CompanionLevelError))
	{
		Director->NotifyGenerationFailed(
			TEXT("COMPANION_LEVEL_CONTRACT_INVALID"),
			CompanionLevelError);
		return;
	}

	const FEFCalystoCompanionSnapshotV4 RealizedSnapshot =
		BuildAcceptedRosterValidationSnapshot(BuildSnapshot());
	const FString RealizedSnapshotHash = FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(RealizedSnapshot);
	if (RealizedSnapshotHash.IsEmpty() || RealizedSnapshotHash != IntentV4.CompanionSnapshotHash)
	{
		DestroyLiveRosterProjections();
		Director->NotifyGenerationFailed(
			TEXT("COMPANION_SNAPSHOT_DRIFT"),
			FString::Printf(TEXT("Expected companion snapshot %s but restored %s."),
				*IntentV4.CompanionSnapshotHash, *RealizedSnapshotHash));
		return;
	}

	CurrentCompanionSnapshotHash = RealizedSnapshotHash;
	const FProjectCompanionRunSnapshot AcceptedSnapshot = BuildSnapshot();
	FString AcceptedSnapshotError;
	if (!AcceptedSnapshot.IsValid(AcceptedSnapshotError))
	{
		Director->NotifyGenerationFailed(
			TEXT("COMPANION_ACCEPTED_SNAPSHOT_INVALID"), AcceptedSnapshotError);
		return;
	}
	FloorStartSnapshot = AcceptedSnapshot;
	PendingDestinationSnapshot = FProjectCompanionRunSnapshot();
	PendingTravelMode = EProjectCompanionDirectorTravelMode::None;
	if (TravelKind != EEFCalystoDungeonTravelKindV4::NewRun)
	{
		BroadcastAcceptedRosterChanges(PreviousSnapshot, AcceptedSnapshot);
	}
}

void UProjectRunCompanionSubsystem::HandleFloorReady(
	const int64 FloorNumber,
	const int32 PCGSeed,
	const FEFCalystoResolvedFloorIntentV4& Intent,
	const FEFCalystoRealizedFloorManifestV4& Manifest)
{
	(void)PCGSeed;
	(void)Intent;
	(void)Manifest;
	if (FloorNumber == CurrentFloor && bCompanionRosterReady)
	{
		bFloorReady = true;
		bGenerationOrTravelActive = false;
	}
}

void UProjectRunCompanionSubsystem::HandleFloorTravelFailed()
{
	if (bInventoryTravelCaptured)
	{
		FString TravelRollbackError;
		if (!VerifyInventoryAfterFailedTravel(TravelRollbackError))
		{
			UE_LOG(LogProjectRunCompanions, Error,
				TEXT("Failed travel could not verify the typed ACF equipment capsule exactly: %s"),
				*TravelRollbackError);
		}
	}
	ResetInventoryTravelCapsule();
	PendingDestinationSnapshot = FProjectCompanionRunSnapshot();
	PendingTravelMode = EProjectCompanionDirectorTravelMode::None;
	bGenerationOrTravelActive = false;
	bFloorReady = !CurrentCompanionSnapshotHash.IsEmpty();
	SetRosterReady(!CurrentCompanionSnapshotHash.IsEmpty());
}

void UProjectRunCompanionSubsystem::ResetRunState(const int64 NewRunEpoch)
{
	CloseRevivalMenu();
	DestroyLiveRosterProjections();
	Roster.Reset();
	RetainedCompanionClasses.Reset();
	FloorStartSnapshot = FProjectCompanionRunSnapshot();
	PreTravelSnapshot = FProjectCompanionRunSnapshot();
	PendingDestinationSnapshot = FProjectCompanionRunSnapshot();
	RevivalTransaction = FRevivalTransaction();
	RunEpoch = NewRunEpoch;
	CurrentFloor = 0;
	CurrentGenerationSerial = 0;
	CurrentCompanionSnapshotHash.Reset();
	bAcceptedIntentPlayerOwnsWintersRecall = false;
	SetRosterReady(false);
	OnRosterChanged.Broadcast();
}

void UProjectRunCompanionSubsystem::DestroyLiveRosterProjections()
{
	APawn* PlayerPawn = ResolveLocalPlayerPawn();
	UACFCompanionGroupAIComponent* Group = UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(PlayerPawn);
	for (TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		if (AACFCharacter* Character = Pair.Value.LiveActor.Get())
		{
			UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Character, Group);
		}
		Pair.Value.LiveActor.Reset();
	}
}

bool UProjectRunCompanionSubsystem::AttachDeathProxy(
	AACFCharacter* Character,
	const FGuid StableCompanionId)
{
	if (!Character || !StableCompanionId.IsValid())
	{
		return false;
	}
	UProjectCompanionDeathProxyComponent* Proxy = NewObject<UProjectCompanionDeathProxyComponent>(
		Character, TEXT("ProjectCompanionDeathProxy"), RF_Transient);
	if (!Proxy)
	{
		return false;
	}
	Character->AddInstanceComponent(Proxy);
	Proxy->RegisterComponent();
	if (!Proxy->InitializeProxy(StableCompanionId, this))
	{
		Proxy->DestroyComponent();
		return false;
	}
	return true;
}

void UProjectRunCompanionSubsystem::SetRosterReady(const bool bReady)
{
	if (bCompanionRosterReady == bReady)
	{
		return;
	}
	bCompanionRosterReady = bReady;
	OnCompanionRosterReady.Broadcast(bReady);
}

bool UProjectRunCompanionSubsystem::IsReadyForDirectorSnapshot(
	const FString& ExpectedSnapshotHash,
	FString& OutError) const
{
	OutError.Reset();
	if (!bCompanionRosterReady)
	{
		OutError = TEXT("The project companion roster has not completed restoration.");
		return false;
	}
	if (ExpectedSnapshotHash.IsEmpty() || CurrentCompanionSnapshotHash != ExpectedSnapshotHash)
	{
		OutError = FString::Printf(TEXT("Companion snapshot mismatch: expected=%s project=%s."),
			*ExpectedSnapshotHash, *CurrentCompanionSnapshotHash);
		return false;
	}
	const FEFCalystoCompanionSnapshotV4 Current =
		BuildAcceptedRosterValidationSnapshot(BuildSnapshot());
	if (FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(Current) != ExpectedSnapshotHash)
	{
		OutError = TEXT("The live project roster drifted after world acceptance.");
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::ResolveFrozenRosterProjection(
	const FEFCalystoSpawnInstanceDirectiveV4& Directive,
	FProjectCompanionDefinition& OutDefinition,
	FString& OutError) const
{
	OutDefinition = FProjectCompanionDefinition();
	OutError.Reset();
	if (Directive.Category != EEFCalystoContentCategoryV4::NPC
		|| !Directive.StableCompanionId.IsValid()
		|| Directive.Lifecycle != EEFCalystoLifecycleV4::Recruitable
		|| Directive.LogicalLevel < 1)
	{
		OutError = TEXT("The directive is not a valid active-party NPC projection.");
		return false;
	}

	const FRuntimeCompanionRecord* Record = Roster.Find(Directive.StableCompanionId);
	if (!Record || !Record->bDesiredActiveParty
		|| Record->Snapshot.State != EProjectCompanionRunState::Alive)
	{
		OutError = TEXT("The stable companion is absent, inactive or not alive in the frozen roster.");
		return false;
	}
	FEFCalystoResolvedCompanionLevelV4 FrozenLevel;
	if (!ResolveActiveIntentCompanionLevel(
		Directive.StableCompanionId, FrozenLevel, OutError))
	{
		return false;
	}

	const FProjectCompanionDefinition& Frozen = Record->Snapshot.Definition;
	if (Frozen.ContentId != Directive.CatalogId
		|| Frozen.CatalogVariantId != Directive.VariantId
		|| Frozen.CharacterClass.ToSoftObjectPath() != Directive.ActorClass.ToSoftObjectPath()
		|| Frozen.Archetype != Directive.Archetype
		|| ProjectRunCompanionPrivate::ToDirectorGender(Frozen.Gender) != Directive.Gender
		|| ProjectRunCompanionPrivate::ToDirectorGrade(Frozen.DifficultyGrade) != Directive.Tier
		|| FrozenLevel.Grade != Directive.Tier
		|| FrozenLevel.LogicalLevel != Directive.LogicalLevel
		|| FrozenLevel.PhysicalACFLevel != Directive.PhysicalACFLevel)
	{
		OutError = TEXT("The roster projection directive drifted from its frozen companion record.");
		return false;
	}

	OutDefinition = Frozen;
	OutDefinition.CharacterClass = TSoftClassPtr<AACFCharacter>(Directive.ActorClass.ToSoftObjectPath());
	OutDefinition.Archetype = Directive.Archetype;
	OutDefinition.Gender = ProjectRunCompanionPrivate::FromDirectorGender(Directive.Gender);
	OutDefinition.DifficultyGrade = ProjectRunCompanionPrivate::FromDirectorGrade(Directive.Tier);
	OutDefinition.ResolvedLevel = FrozenLevel.LogicalLevel;
	OutDefinition.Lifecycle = EProjectCompanionLifecycle::Recruitable;
	return OutDefinition.IsValid(OutError);
}

bool UProjectRunCompanionSubsystem::AdoptDirectorRosterProjection(
	AACFCharacter* Character,
	const FProjectCompanionDefinition& Definition,
	FString& OutError)
{
	OutError.Reset();
	FRuntimeCompanionRecord* Record = Roster.Find(Definition.StableCompanionId);
	if (!IsValid(Character) || !Record || !Record->bDesiredActiveParty
		|| Record->Snapshot.State != EProjectCompanionRunState::Alive
		|| Record->LiveActor.IsValid())
	{
		OutError = TEXT("The roster projection is invalid, duplicated, inactive or not alive.");
		return false;
	}

	const FProjectCompanionDefinition PreviousDefinition = Record->Snapshot.Definition;
	Record->Snapshot.Definition = Definition;
	Record->LiveActor = Character;
	Record->CorpseActor.Reset();
	RetainedCompanionClasses.AddUnique(Character->GetClass());
	if (!AttachDeathProxy(Character, Definition.StableCompanionId))
	{
		Record->Snapshot.Definition = PreviousDefinition;
		Record->LiveActor.Reset();
		OutError = TEXT("The roster projection could not bind its canonical death proxy.");
		return false;
	}
	return true;
}

void UProjectRunCompanionSubsystem::ReleaseDirectorRosterProjection(
	const FGuid StableCompanionId,
	AACFCharacter* Character)
{
	if (FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId))
	{
		if (!Character || Record->LiveActor.Get() == Character)
		{
			Record->LiveActor.Reset();
		}
	}
	SetRosterReady(false);
}

void UProjectRunCompanionSubsystem::RollbackUncommittedDirectorRecruitment(
	const FGuid StableCompanionId,
	AACFCharacter* Character)
{
	FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	const bool bExistedAtFloorStart = FloorStartSnapshot.Entries.ContainsByPredicate(
		[&StableCompanionId](const FProjectCompanionRunEntrySnapshot& Entry)
		{
			return Entry.Definition.StableCompanionId == StableCompanionId;
		});
	if (Record && !bExistedAtFloorStart && Record->LiveActor.Get() == Character)
	{
		Roster.Remove(StableCompanionId);
		OnRosterChanged.Broadcast();
	}
	SetRosterReady(false);
}

bool UProjectRunCompanionSubsystem::FinalizeDirectorRosterReadiness(
	const FString& ExpectedSnapshotHash,
	FString& OutError)
{
	OutError.Reset();
	if (ExpectedSnapshotHash.IsEmpty() || ExpectedSnapshotHash != CurrentCompanionSnapshotHash)
	{
		OutError = TEXT("The post-population companion hash does not match the accepted V4 intent.");
		return false;
	}
	const FString LiveHash = FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
		BuildAcceptedRosterValidationSnapshot(BuildSnapshot()));
	if (LiveHash != ExpectedSnapshotHash)
	{
		OutError = TEXT("The actor-independent roster drifted during V4 population.");
		return false;
	}

	TSet<const AACFCharacter*> SeenActors;
	int32 ActiveCount = 0;
	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		const FRuntimeCompanionRecord& Record = Pair.Value;
		const bool bMustBeProjected = Record.bDesiredActiveParty
			&& Record.Snapshot.State == EProjectCompanionRunState::Alive;
		AACFCharacter* Actor = Record.LiveActor.Get();
		if (bMustBeProjected)
		{
			++ActiveCount;
			if (!IsValid(Actor) || SeenActors.Contains(Actor)
				|| !Actor->IsA(Record.Snapshot.Definition.CharacterClass.Get())
				|| !Actor->FindComponentByClass<UProjectCompanionDeathProxyComponent>())
			{
				OutError = TEXT("An active-party record has no unique validated live projection.");
				return false;
			}
			SeenActors.Add(Actor);
		}
		else if (IsValid(Actor))
		{
			OutError = TEXT("An inactive/dead roster record unexpectedly has a live Director projection.");
			return false;
		}
	}
	if (ActiveCount > ProjectRunCompanionPrivate::MaximumActiveParty)
	{
		OutError = TEXT("The realized active party exceeds the hard cap of two.");
		return false;
	}
	SetRosterReady(true);
	return true;
}

APawn* UProjectRunCompanionSubsystem::ResolveLocalPlayerPawn() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		return Controller->GetPawn();
	}
	return nullptr;
}

UACFInventoryComponent* UProjectRunCompanionSubsystem::ResolveInventory(const APawn* PlayerPawn) const
{
	if (!PlayerPawn)
	{
		return nullptr;
	}
	if (UACFInventoryComponent* Inventory = PlayerPawn->FindComponentByClass<UACFInventoryComponent>())
	{
		return Inventory;
	}
	return PlayerPawn->GetController()
		? PlayerPawn->GetController()->FindComponentByClass<UACFInventoryComponent>()
		: nullptr;
}

UACFEquipmentComponent* UProjectRunCompanionSubsystem::ResolveEquipment(const APawn* PlayerPawn) const
{
	if (!PlayerPawn)
	{
		return nullptr;
	}
	if (UACFEquipmentComponent* Equipment =
		PlayerPawn->FindComponentByClass<UACFEquipmentComponent>())
	{
		return Equipment;
	}
	return PlayerPawn->GetController()
		? PlayerPawn->GetController()->FindComponentByClass<UACFEquipmentComponent>()
		: nullptr;
}

bool UProjectRunCompanionSubsystem::SynchronizeRecruitmentsBeforeTravel(FString& OutError)
{
	OutError.Reset();
	UACFCompanionGroupAIComponent* Group =
		UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(ResolveLocalPlayerPawn());
	if (!Group)
	{
		for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
		{
			if (Pair.Value.bDesiredActiveParty
				&& Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive)
			{
				OutError = TEXT("The active V4 roster exists but the typed ACF companion group is unavailable.");
				return false;
			}
		}
		return true;
	}

	TArray<FAIAgentsInfo> Agents;
	Group->GetGroupAgents(Agents);
	TSet<AACFCharacter*> SeenActors;
	for (const FAIAgentsInfo& Agent : Agents)
	{
		AACFCharacter* Character = Agent.AICharacter;
		if (!IsValid(Character) || SeenActors.Contains(Character))
		{
			OutError = TEXT("The live ACF companion group contains an invalid or duplicate actor.");
			return false;
		}
		SeenActors.Add(Character);

		if (UProjectRecruitableCompanionComponent* Recruitment =
			Character->FindComponentByClass<UProjectRecruitableCompanionComponent>())
		{
			if (!Recruitment->SynchronizeFromACFGroup(OutError))
			{
				return false;
			}
		}

		bool bFoundStableRecord = false;
		for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
		{
			if (Pair.Value.LiveActor.Get() == Character
				&& Pair.Value.bDesiredActiveParty
				&& Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive)
			{
				bFoundStableRecord = true;
				break;
			}
		}
		if (!bFoundStableRecord)
		{
			OutError = TEXT("A live ACF group actor has no living active-party record in the V4 roster.");
			return false;
		}
	}

	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		if (!Pair.Value.bDesiredActiveParty
			|| Pair.Value.Snapshot.State != EProjectCompanionRunState::Alive)
		{
			continue;
		}
		AACFCharacter* Character = Pair.Value.LiveActor.Get();
		if (!IsValid(Character) || !SeenActors.Contains(Character))
		{
			OutError = TEXT("A living active-party roster record is absent from the typed ACF group.");
			return false;
		}
	}
	return true;
}

bool UProjectRunCompanionSubsystem::AuditTypedInventoryForAutomation(
	UACFEquipmentComponent* Equipment,
	FString& OutCanonicalHash,
	FString& OutError) const
{
	OutCanonicalHash = ComputeInventoryHash(Equipment);
	if (OutCanonicalHash.IsEmpty())
	{
		OutError = TEXT("The production typed inventory canonical hash is unavailable.");
		return false;
	}
	return VerifyRestoredEquipment(Equipment, OutCanonicalHash, OutError);
}

FString UProjectRunCompanionSubsystem::ComputeInventoryHash(const UACFInventoryComponent* Inventory) const
{
	const UACFEquipmentComponent* Equipment = Cast<UACFEquipmentComponent>(Inventory);
	if (!Equipment || !Equipment->GetIsInitialized())
	{
		return FString();
	}
	TArray<FInventoryItem> Items = Equipment->GetInventory();
	Items.Sort([](const FInventoryItem& Left, const FInventoryItem& Right)
	{
		return ProjectRunCompanionPrivate::GuidKey(Left.GetItemGuid())
			< ProjectRunCompanionPrivate::GuidKey(Right.GetItemGuid());
	});

	const float Currency = Equipment->GetCurrentCurrencyAmount();
	const float Weight = Equipment->GetCurrentInventoryTotalWeight();
	if (!FMath::IsFinite(Currency) || !FMath::IsFinite(Weight))
	{
		return FString();
	}
	const int32 MaxSlots = Equipment->GetMaxInventorySlots();
	const int32 MaxWeight = Equipment->GetMaxInventoryWeight();
	if (MaxSlots < 0 || MaxWeight < 0)
	{
		return FString();
	}
	FString Canonical = FString::Printf(
		TEXT("ProjectACFInventoryTravelV4|currency=%08X|weight=%08X|maxSlots=%d|maxWeight=%d|"),
		ProjectRunCompanionPrivate::FloatBits(Currency),
		ProjectRunCompanionPrivate::FloatBits(Weight),
		MaxSlots,
		MaxWeight);
	for (const FInventoryItem& Entry : Items)
	{
		Canonical += FString::Printf(TEXT("%s|%s|%d|%d|%s|%d|%08X|"),
			*ProjectRunCompanionPrivate::GuidKey(Entry.GetItemGuid()),
			*GetPathNameSafe(Entry.ItemClass.Get()),
			Entry.Count,
			Entry.bIsEquipped ? 1 : 0,
			*Entry.EquipmentSlot.ToString(),
			Entry.ItemIndex,
			ProjectRunCompanionPrivate::FloatBits(Entry.DropChancePercentage));
		ProjectRunCompanionPrivate::AppendSaveGameProperties(Entry.Item, Canonical);
		if (Entry.Item)
		{
			for (const UACFItemFragment* Fragment : Entry.Item->Fragments)
			{
				ProjectRunCompanionPrivate::AppendSaveGameProperties(Fragment, Canonical);
			}
		}
	}
	return UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Canonical);
}

bool UProjectRunCompanionSubsystem::SerializeEquipmentCapsule(
	UACFEquipmentComponent* Equipment,
	TArray<uint8>& OutBytes,
	FString& OutError) const
{
	OutBytes.Reset();
	OutError.Reset();
	if (!Equipment || !Equipment->GetOwner() || !Equipment->GetOwner()->HasAuthority()
		|| !Equipment->GetIsInitialized())
	{
		OutError = TEXT("The authoritative initialized ACF equipment component is unavailable.");
		return false;
	}
	FMemoryWriter Writer(OutBytes, true);
	{
		FALSSaveGameArchive Archive(Writer, false);
		Equipment->Serialize(Archive);
		if (Archive.IsError())
		{
			OutError = TEXT("ACF equipment serialization reported an archive error.");
			OutBytes.Reset();
			return false;
		}
	}
	Writer.Close();
	if (Writer.IsError() || OutBytes.IsEmpty())
	{
		OutError = TEXT("ACF equipment serialization produced no valid bytes.");
		OutBytes.Reset();
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::RestoreTypedEquipmentCapsule(
	UACFEquipmentComponent* Equipment,
	const TArray<uint8>& FrozenBytes,
	const FString& ExpectedInventoryHash,
	FString& OutError) const
{
	OutError.Reset();
	if (!Equipment || !Equipment->GetOwner() || !Equipment->GetOwner()->HasAuthority()
		|| !Equipment->GetIsInitialized() || FrozenBytes.IsEmpty()
		|| ExpectedInventoryHash.IsEmpty())
	{
		OutError = TEXT("The authoritative initialized ACF equipment restore contract is incomplete.");
		return false;
	}

	const TArray<FEquippedItem> ExistingEquipment =
		Equipment->GetCurrentEquipment().GetEquippedItems();
	TSet<FGuid> UnequippedIds;
	for (const FEquippedItem& EquippedItem : ExistingEquipment)
	{
		if (!EquippedItem.ItemGuid.IsValid() || UnequippedIds.Contains(EquippedItem.ItemGuid))
		{
			OutError = TEXT("The destination ACF equipment contains an invalid or duplicate GUID.");
			return false;
		}
		UnequippedIds.Add(EquippedItem.ItemGuid);
		Equipment->UnequipItemByGuid(EquippedItem.ItemGuid);
	}
	if (!Equipment->GetCurrentEquipment().GetEquippedItems().IsEmpty())
	{
		OutError = TEXT("ACF could not unequip the destination equipment before typed restoration.");
		return false;
	}
	const TArray<UACFItemFragment*> DestinationFragments = Equipment->GetRegisteredFragments();
	for (UACFItemFragment* Fragment : DestinationFragments)
	{
		if (!IsValid(Fragment) || !Equipment->UnregisterFragment(Fragment))
		{
			OutError = TEXT("ACF could not unregister a destination item fragment before typed restoration.");
			return false;
		}
	}
	if (!Equipment->GetRegisteredFragments().IsEmpty())
	{
		OutError = TEXT("The destination ACF fragment registry was not empty before typed restoration.");
		return false;
	}

	FMemoryReader Reader(FrozenBytes, true);
	{
		FALSSaveGameArchive Archive(Reader, false);
		Equipment->Serialize(Archive);
		if (Archive.IsError())
		{
			OutError = TEXT("ACF equipment deserialization reported an archive error.");
			return false;
		}
	}
	Reader.Close();
	if (Reader.IsError())
	{
		OutError = TEXT("ACF equipment deserialization could not read the typed capsule.");
		return false;
	}

	AActor* OwnerActor = Equipment->GetOwner();
	const TArray<FInventoryItem> RestoredItems = Equipment->GetInventory();
	TSet<FGuid> RestoredItemIds;
	TSet<UACFItemFragment*> ExpectedFragments;
	for (const FInventoryItem& Item : RestoredItems)
	{
		if (!Item.GetItemGuid().IsValid() || RestoredItemIds.Contains(Item.GetItemGuid())
			|| Item.Count <= 0 || !Item.ItemClass || !IsValid(Item.Item)
			|| Item.Item->GetClass() != Item.ItemClass.Get())
		{
			OutError = TEXT("The deserialized ACF inventory contains an invalid GUID, count, class or item instance.");
			return false;
		}
		RestoredItemIds.Add(Item.GetItemGuid());
		Item.Item->SetItemOwner(OwnerActor);
		for (UACFItemFragment* Fragment : Item.Item->Fragments)
		{
			if (!IsValid(Fragment) || ExpectedFragments.Contains(Fragment))
			{
				OutError = TEXT("The deserialized ACF inventory contains a null or duplicated item fragment.");
				return false;
			}
			if (Fragment->GetOuter() != OwnerActor
				&& !Fragment->Rename(
					nullptr,
					OwnerActor,
					REN_DontCreateRedirectors | REN_NonTransactional))
			{
				OutError = TEXT("An ACF item fragment could not be reparented to the destination actor.");
				return false;
			}
			ExpectedFragments.Add(Fragment);
		}
		Equipment->RegisterFragmentsForItem(Item.Item);
	}

	Equipment->RefreshTotalWeight();
	for (const FInventoryItem& Item : RestoredItems)
	{
		if (Item.bIsEquipped)
		{
			Equipment->EquipItemFromInventoryInSlot(Item, Item.EquipmentSlot);
		}
	}
	if (!VerifyRestoredEquipment(Equipment, ExpectedInventoryHash, OutError))
	{
		return false;
	}

	Equipment->OnInventoryChanged.Broadcast();
	Equipment->SetCurrency(Equipment->GetCurrentCurrencyAmount());
	return true;
}

bool UProjectRunCompanionSubsystem::RestoreRevivalInventoryCapsule(
	UACFEquipmentComponent* Equipment,
	const TArray<uint8>& FrozenBytes,
	const FString& FrozenBytesHash,
	const FString& ExpectedInventoryHash,
	FString& OutError) const
{
	OutError.Reset();
	if (!Equipment || !Equipment->GetOwner() || !Equipment->GetOwner()->HasAuthority()
		|| !Equipment->GetIsInitialized())
	{
		OutError = TEXT("The authoritative initialized ACF equipment component is unavailable for revival rollback.");
		return false;
	}
	if (FrozenBytes.IsEmpty() || ExpectedInventoryHash.IsEmpty()
		|| FrozenBytesHash.IsEmpty()
		|| ProjectRunCompanionPrivate::HashBytes(FrozenBytes) != FrozenBytesHash)
	{
		OutError = TEXT("The frozen revival inventory capsule failed its integrity check.");
		return false;
	}

	// A rejected ACF removal can be a clean no-op (for example, an item-data
	// lookup failed before mutation).  Preserve live item instances in that case.
	if (ComputeInventoryHash(Equipment) == ExpectedInventoryHash)
	{
		return VerifyRestoredEquipment(Equipment, ExpectedInventoryHash, OutError);
	}
	return RestoreTypedEquipmentCapsule(
		Equipment, FrozenBytes, ExpectedInventoryHash, OutError);
}

bool UProjectRunCompanionSubsystem::CaptureInventoryForTravel(
	const EProjectCompanionDirectorTravelMode TravelMode,
	FString& OutError)
{
	ResetInventoryTravelCapsule();
	OutError.Reset();
	APawn* PlayerPawn = ResolveLocalPlayerPawn();
	UACFEquipmentComponent* Equipment = ResolveEquipment(PlayerPawn);
	PreTravelInventoryHash = ComputeInventoryHash(Equipment);
	if (!PlayerPawn || !PlayerPawn->HasAuthority() || !Equipment
		|| TravelMode == EProjectCompanionDirectorTravelMode::None
		|| PreTravelInventoryHash.IsEmpty()
		|| !VerifyRestoredEquipment(Equipment, PreTravelInventoryHash, OutError)
		|| !SerializeEquipmentCapsule(Equipment, InventoryTravelCapsule, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The typed ACF equipment travel capsule is unavailable or uninitialized.");
		}
		ResetInventoryTravelCapsule();
		return false;
	}

	InventoryTravelComponentClassPath = Equipment->GetClass()->GetPathName();
	InventoryTravelComponentName = Equipment->GetFName();
	InventoryTravelRunEpoch = RunEpoch;
	InventoryTravelFloor = CurrentFloor;
	InventoryTravelGenerationSerial = CurrentGenerationSerial;
	const FString BytesHash = ProjectRunCompanionPrivate::HashBytes(InventoryTravelCapsule);
	InventoryTravelCapsuleHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
		TEXT("ProjectACFEquipmentTravelV4|%lld|%lld|%lld|%d|%s|%s|%s"),
		InventoryTravelRunEpoch,
		InventoryTravelFloor,
		InventoryTravelGenerationSerial,
		static_cast<int32>(TravelMode),
		*InventoryTravelComponentClassPath,
		*InventoryTravelComponentName.ToString(),
		*BytesHash));
	if (BytesHash.IsEmpty() || InventoryTravelCapsuleHash.Len() != 64)
	{
		OutError = TEXT("The typed ACF equipment travel capsule hash is invalid.");
		ResetInventoryTravelCapsule();
		return false;
	}
	bInventoryTravelCaptured = true;
	return true;
}

bool UProjectRunCompanionSubsystem::RestoreAndVerifyInventoryAfterTravel(FString& OutError)
{
	OutError.Reset();
	APawn* PlayerPawn = ResolveLocalPlayerPawn();
	UACFEquipmentComponent* Equipment = ResolveEquipment(PlayerPawn);
	if (!bInventoryTravelCaptured || !PlayerPawn || !PlayerPawn->HasAuthority()
		|| !Equipment || !Equipment->GetIsInitialized()
		|| Equipment->GetClass()->GetPathName() != InventoryTravelComponentClassPath
		|| Equipment->GetFName() != InventoryTravelComponentName)
	{
		OutError = TEXT("The destination ACF equipment component does not match the frozen travel capsule.");
		ResetInventoryTravelCapsule();
		return false;
	}

	const FString BytesHash = ProjectRunCompanionPrivate::HashBytes(InventoryTravelCapsule);
	const FString ExpectedCapsuleHash = UEFCalystoDungeonSubsystem::ComputeCanonicalHash(FString::Printf(
		TEXT("ProjectACFEquipmentTravelV4|%lld|%lld|%lld|%d|%s|%s|%s"),
		InventoryTravelRunEpoch,
		InventoryTravelFloor,
		InventoryTravelGenerationSerial,
		static_cast<int32>(PendingTravelMode),
		*InventoryTravelComponentClassPath,
		*InventoryTravelComponentName.ToString(),
		*BytesHash));
	if (BytesHash.IsEmpty() || ExpectedCapsuleHash != InventoryTravelCapsuleHash)
	{
		OutError = TEXT("The frozen ACF equipment travel capsule failed its SHA-256 integrity check.");
		ResetInventoryTravelCapsule();
		return false;
	}

	BeforeTypedInventoryRestoreEvent.Broadcast(Equipment);
	if (!RestoreTypedEquipmentCapsule(
		Equipment, InventoryTravelCapsule, PreTravelInventoryHash, OutError))
	{
		ResetInventoryTravelCapsule();
		return false;
	}
	ResetInventoryTravelCapsule();
	return true;
}

bool UProjectRunCompanionSubsystem::VerifyRestoredEquipment(
	UACFEquipmentComponent* Equipment,
	const FString& ExpectedInventoryHash,
	FString& OutError) const
{
	OutError.Reset();
	const FString RestoredInventoryHash = ComputeInventoryHash(Equipment);
	if (ExpectedInventoryHash.IsEmpty()
		|| RestoredInventoryHash.IsEmpty()
		|| RestoredInventoryHash != ExpectedInventoryHash)
	{
		OutError = FString::Printf(TEXT("Full typed ACF inventory hash drift: before=%s after=%s."),
			*ExpectedInventoryHash, *RestoredInventoryHash);
		return false;
	}
	if (!FMath::IsFinite(Equipment->GetCurrentCurrencyAmount())
		|| !FMath::IsFinite(Equipment->GetCurrentInventoryTotalWeight()))
	{
		OutError = TEXT("The restored ACF currency or inventory weight is not finite.");
		return false;
	}

	TSet<UACFItemFragment*> ExpectedFragments;
	for (const FInventoryItem& Item : Equipment->GetInventory())
	{
		if (!Item.Item)
		{
			OutError = TEXT("The restored ACF inventory has no live item instance.");
			return false;
		}
		for (UACFItemFragment* Fragment : Item.Item->Fragments)
		{
			if (!IsValid(Fragment) || Fragment->GetOuter() != Equipment->GetOwner()
				|| ExpectedFragments.Contains(Fragment))
			{
				OutError = TEXT("The restored ACF fragment owner or identity is invalid.");
				return false;
			}
			ExpectedFragments.Add(Fragment);
		}
	}
	const TArray<UACFItemFragment*> RegisteredFragmentArray = Equipment->GetRegisteredFragments();
	TSet<UACFItemFragment*> RegisteredFragments;
	for (UACFItemFragment* Fragment : RegisteredFragmentArray)
	{
		if (!IsValid(Fragment) || RegisteredFragments.Contains(Fragment))
		{
			OutError = TEXT("The restored ACF fragment registry contains an invalid or duplicate pointer.");
			return false;
		}
		RegisteredFragments.Add(Fragment);
	}
	bool bFragmentSetMismatch = RegisteredFragments.Num() != ExpectedFragments.Num();
	for (UACFItemFragment* ExpectedFragment : ExpectedFragments)
	{
		bFragmentSetMismatch |= !RegisteredFragments.Contains(ExpectedFragment);
	}
	if (bFragmentSetMismatch)
	{
		OutError = FString::Printf(
			TEXT("The restored ACF fragment registry differs from the inventory item graph (expected=%d registered=%d)."),
			ExpectedFragments.Num(),
			RegisteredFragments.Num());
		return false;
	}

	TMap<FGuid, FGameplayTag> ExpectedEquipment;
	TMap<FGuid, UClass*> ExpectedEquipmentClasses;
	for (const FInventoryItem& Item : Equipment->GetInventory())
	{
		if (Item.bIsEquipped)
		{
			if (!Item.GetItemGuid().IsValid() || !Item.EquipmentSlot.IsValid()
				|| !Item.ItemClass || !Item.Item
				|| Item.Item->GetClass() != Item.ItemClass.Get()
				|| ExpectedEquipment.Contains(Item.GetItemGuid()))
			{
				OutError = TEXT("The restored inventory has an invalid equipped-item identity, slot or class.");
				return false;
			}
			ExpectedEquipment.Add(Item.GetItemGuid(), Item.EquipmentSlot);
			ExpectedEquipmentClasses.Add(Item.GetItemGuid(), Item.ItemClass.Get());
		}
	}
	const TArray<FEquippedItem> RealizedEquipment =
		Equipment->GetCurrentEquipment().GetEquippedItems();
	if (RealizedEquipment.Num() != ExpectedEquipment.Num())
	{
		OutError = TEXT("The realized ACF equipment count differs from the frozen inventory flags.");
		return false;
	}
	for (const FEquippedItem& Item : RealizedEquipment)
	{
		const FGameplayTag* ExpectedSlot = ExpectedEquipment.Find(Item.ItemGuid);
		UClass* const* ExpectedClass = ExpectedEquipmentClasses.Find(Item.ItemGuid);
		if (!ExpectedSlot || !ExpectedClass
			|| *ExpectedSlot != Item.ItemSlot
			|| !Item.Item || !Item.ItemClass
			|| Item.ItemClass.Get() != *ExpectedClass
			|| Item.Item->GetClass() != *ExpectedClass)
		{
			OutError = TEXT("The realized ACF equipment GUID, slot, class or item instance drifted.");
			return false;
		}
	}

	// ACF recreates every UACFItem with NewObject while loading FACFInventoryList.
	// FObjectAndNameAsStringProxyArchive therefore emits different object-path bytes
	// after a valid restore.  Byte equality here would compare transient UObject
	// names, not saved gameplay state.  The frozen capsule itself was already
	// SHA-256 checked before deserialization; the canonical inventory hash above
	// verifies GUID/class/count/equipped slot and all SaveGame item/fragment data.
	TArray<uint8> VerificationBytes;
	if (!SerializeEquipmentCapsule(Equipment, VerificationBytes, OutError))
	{
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::VerifyInventoryAfterFailedTravel(FString& OutError)
{
	OutError.Reset();
	UACFEquipmentComponent* Equipment = ResolveEquipment(ResolveLocalPlayerPawn());
	if (!bInventoryTravelCaptured || !Equipment
		|| Equipment->GetClass()->GetPathName() != InventoryTravelComponentClassPath
		|| Equipment->GetFName() != InventoryTravelComponentName)
	{
		OutError = TEXT("Failed travel no longer has the exact source ACF equipment component.");
		ResetInventoryTravelCapsule();
		return false;
	}
	TArray<uint8> CurrentBytes;
	// No load or UObject recreation occurred on this path.  Keep byte identity in
	// addition to the semantic hash so a rejected travel cannot hide any mutation
	// of the source ACF component or its serialized SaveGame state.
	const bool bExact = ComputeInventoryHash(Equipment) == PreTravelInventoryHash
		&& SerializeEquipmentCapsule(Equipment, CurrentBytes, OutError)
		&& ProjectRunCompanionPrivate::HashBytes(CurrentBytes)
			== ProjectRunCompanionPrivate::HashBytes(InventoryTravelCapsule);
	if (!bExact && OutError.IsEmpty())
	{
		OutError = TEXT("The source ACF equipment changed while the Director travel request failed.");
	}
	ResetInventoryTravelCapsule();
	return bExact;
}

void UProjectRunCompanionSubsystem::ResetInventoryTravelCapsule()
{
	bInventoryTravelCaptured = false;
	PreTravelInventoryHash.Reset();
	InventoryTravelCapsule.Reset();
	InventoryTravelCapsuleHash.Reset();
	InventoryTravelComponentClassPath.Reset();
	InventoryTravelComponentName = NAME_None;
	InventoryTravelRunEpoch = 0;
	InventoryTravelFloor = 0;
	InventoryTravelGenerationSerial = 0;
}

bool UProjectRunCompanionSubsystem::PurgeWintersRecallFromInventory(
	APawn* PlayerPawn,
	FString& OutError) const
{
	OutError.Reset();
	UACFInventoryComponent* Inventory = ResolveInventory(PlayerPawn);
	if (!Inventory || !PlayerPawn || !PlayerPawn->HasAuthority())
	{
		OutError = TEXT("New Run cannot purge Winter's Recall without an authoritative initialized inventory.");
		return false;
	}
	const TArray<FInventoryItem> Items = Inventory->GetInventory();
	for (const FInventoryItem& Entry : Items)
	{
		if (Entry.ItemClass && Entry.ItemClass->IsChildOf(UProjectCompanionRevivalConsumable::StaticClass()))
		{
			Inventory->RemoveItem(Entry, Entry.Count);
		}
	}
	if (PlayerOwnsWintersRecall(PlayerPawn))
	{
		OutError = TEXT("ACF did not remove every Winter's Recall instance during New Run initialization.");
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::PlayerOwnsWintersRecall(const APawn* PlayerPawn) const
{
	const UACFInventoryComponent* Inventory = ResolveInventory(PlayerPawn);
	if (!Inventory)
	{
		return false;
	}
	for (const FInventoryItem& Entry : Inventory->GetInventory())
	{
		if (Entry.ItemClass && Entry.ItemClass->IsChildOf(UProjectCompanionRevivalConsumable::StaticClass())
			&& Entry.Count > 0)
		{
			return true;
		}
	}
	return false;
}

void UProjectRunCompanionSubsystem::ReportCompanionDeath(
	const FGuid StableCompanionId,
	AActor* CorpseActor,
	const FName SourceDomain)
{
	FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	if (!Record || Record->Snapshot.State != EProjectCompanionRunState::Alive)
	{
		return;
	}
	AACFCharacter* LiveCharacter = Record->LiveActor.Get();
	AACFCharacter* DeathCharacter = Cast<AACFCharacter>(CorpseActor);
	if (!DeathCharacter)
	{
		DeathCharacter = LiveCharacter;
	}
	bool bGroupRemovalVerified = true;
	UACFCompanionGroupAIComponent* Group =
		UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(ResolveLocalPlayerPawn());
	if (Group && IsValid(DeathCharacter) && Group->IsAlreadyInGroup(DeathCharacter))
	{
		const bool bRemoved = Group->RemoveAgentFromGroup(DeathCharacter);
		bGroupRemovalVerified = bRemoved && !Group->IsAlreadyInGroup(DeathCharacter);
	}
	else if (Record->bDesiredActiveParty && !Group)
	{
		bGroupRemovalVerified = false;
	}
	if (!bGroupRemovalVerified)
	{
		UE_LOG(LogProjectRunCompanions, Error,
			TEXT("Companion %s died but its typed ACF group removal could not be verified."),
			*ProjectRunCompanionPrivate::GuidKey(StableCompanionId));
		SetRosterReady(false);
	}

	Record->Snapshot.State = EProjectCompanionRunState::PendingDead;
	Record->Snapshot.DeathFloor = CurrentFloor;
	Record->Snapshot.DeathGenerationSerial = CurrentGenerationSerial;
	Record->bDesiredActiveParty = false;
	Record->CorpseActor = CorpseActor;
	Record->LiveActor.Reset();

	if (UProjectSocialSubsystem* Social = GetGameInstance()->GetSubsystem<UProjectSocialSubsystem>())
	{
		FProjectSocialParticipantState State;
		if (Social->TryGetParticipantState(CorpseActor, State))
		{
			State.bAlive = false;
			State.bConscious = false;
			State.bInCombat = false;
			Social->RegisterOrUpdateParticipant(CorpseActor, State);
		}
	}
	UE_LOG(LogProjectRunCompanions, Log, TEXT("Companion %s entered PendingDead from %s at floor %lld serial %lld."),
		*ProjectRunCompanionPrivate::GuidKey(StableCompanionId), *SourceDomain.ToString(),
		CurrentFloor, CurrentGenerationSerial);
	OnDeathStateChanged.Broadcast(StableCompanionId, EProjectCompanionRunState::PendingDead);
	OnRosterChanged.Broadcast();
}

bool UProjectRunCompanionSubsystem::HasConfirmedDeadCompanion() const
{
	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		if (Pair.Value.Snapshot.State == EProjectCompanionRunState::Dead)
		{
			return true;
		}
	}
	return false;
}

TArray<FProjectCompanionRevivalCandidate> UProjectRunCompanionSubsystem::GetRevivalCandidates() const
{
	TArray<FProjectCompanionRevivalCandidate> Result;
	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		const FProjectCompanionRunEntrySnapshot& Entry = Pair.Value.Snapshot;
		if (Entry.State != EProjectCompanionRunState::Dead
			&& Entry.State != EProjectCompanionRunState::PendingDead)
		{
			continue;
		}
		FProjectCompanionRevivalCandidate& Candidate = Result.AddDefaulted_GetRef();
		Candidate.StableCompanionId = Pair.Key;
		Candidate.Archetype = Entry.Definition.Archetype;
		Candidate.Gender = Entry.Definition.Gender;
		Candidate.DifficultyGrade = Entry.Definition.DifficultyGrade;
		Candidate.ResolvedLevel = Entry.Definition.ResolvedLevel;
		Candidate.bDeathPendingAdvance = Entry.State == EProjectCompanionRunState::PendingDead;
	}
	Result.Sort([](const FProjectCompanionRevivalCandidate& Left, const FProjectCompanionRevivalCandidate& Right)
	{
		return ProjectRunCompanionPrivate::GuidKey(Left.StableCompanionId)
			< ProjectRunCompanionPrivate::GuidKey(Right.StableCompanionId);
	});
	return Result;
}

bool UProjectRunCompanionSubsystem::FindExactItemEntry(
	const APawn* PlayerPawn,
	const UProjectCompanionRevivalConsumable* Item,
	FGuid& OutGuid,
	int32& OutCount,
	UACFInventoryComponent*& OutInventory) const
{
	OutGuid.Invalidate();
	OutCount = 0;
	OutInventory = ResolveInventory(PlayerPawn);
	if (!OutInventory || !Item)
	{
		return false;
	}
	for (const FInventoryItem& Entry : OutInventory->GetInventory())
	{
		if (Entry.Item == Item && Entry.Count > 0 && Entry.GetItemGuid().IsValid())
		{
			OutGuid = Entry.GetItemGuid();
			OutCount = Entry.Count;
			return true;
		}
	}
	return false;
}

bool UProjectRunCompanionSubsystem::IsPlayerOutsideCombat(const APawn* PlayerPawn) const
{
	if (!PlayerPawn || PlayerPawn->ActorHasTag(TEXT("Project.State.InCombat")))
	{
		return false;
	}
	const UACFCompanionGroupAIComponent* Group = UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(PlayerPawn);
	return Group && !Group->IsInBattle();
}

bool UProjectRunCompanionSubsystem::CanBeginRevival(
	const APawn* PlayerPawn,
	const UProjectCompanionRevivalConsumable* Item,
	FString& OutError) const
{
	OutError.Reset();
	UEFCalystoDungeonSubsystem* Director = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	if (!PlayerPawn || !PlayerPawn->HasAuthority() || !Director || !Director->HasActiveRun())
	{
		OutError = TEXT("Winter's Recall requires an authoritative active dungeon run.");
		return false;
	}
	if (!bFloorReady || !bCompanionRosterReady || bGenerationOrTravelActive
		|| Director->IsTravelRequestPending() || RevivalTransaction.bActive)
	{
		OutError = TEXT("The floor, companion roster, travel or revival transaction state is not ready.");
		return false;
	}
	if (!IsPlayerOutsideCombat(PlayerPawn))
	{
		OutError = TEXT("Winter's Recall cannot be used in combat.");
		return false;
	}
	if (GetRevivalCandidates().IsEmpty())
	{
		OutError = TEXT("There is no dead or pending-dead companion to revive.");
		return false;
	}
	int32 ActiveLiving = 0;
	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		ActiveLiving += ((Pair.Value.bDesiredActiveParty
			&& Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive) ? 1 : 0);
	}
	if (ActiveLiving >= ProjectRunCompanionPrivate::MaximumActiveParty)
	{
		OutError = TEXT("The active companion party is full; revival would exceed the hard cap of two.");
		return false;
	}
	FGuid ItemGuid;
	int32 ItemCount = 0;
	UACFInventoryComponent* Inventory = nullptr;
	if (!FindExactItemEntry(PlayerPawn, Item, ItemGuid, ItemCount, Inventory))
	{
		OutError = TEXT("The exact Winter's Recall item object is no longer present in ACF inventory.");
		return false;
	}
	UACFEquipmentComponent* Equipment = ResolveEquipment(PlayerPawn);
	TArray<uint8> RollbackProbe;
	if (!Equipment || static_cast<UACFInventoryComponent*>(Equipment) != Inventory
		|| ComputeInventoryHash(Equipment).IsEmpty()
		|| !SerializeEquipmentCapsule(Equipment, RollbackProbe, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Winter's Recall requires one initialized typed ACF equipment inventory for atomic rollback.");
		}
		return false;
	}
	UClass* CandidateClass = nullptr;
	for (const FProjectCompanionRevivalCandidate& Candidate : GetRevivalCandidates())
	{
		if (const FRuntimeCompanionRecord* Record = Roster.Find(Candidate.StableCompanionId))
		{
			CandidateClass = Record->Snapshot.Definition.CharacterClass.Get();
			if (CandidateClass)
			{
				break;
			}
		}
	}
	if (!CandidateClass || !UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(PlayerPawn))
	{
		OutError = TEXT("No eligible companion class/group is retained for a safe revival.");
		return false;
	}
	FTransform Probe;
	TArray<FVector> Reserved;
	if (!UProjectCompanionRuntimeAdapter::FindDeterministicSafeSpawnTransform(
		GetWorld(), PlayerPawn, GetRevivalCandidates()[0].StableCompanionId,
		CandidateClass, Reserved, Probe, OutError))
	{
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::BeginRevivalSelection(
	APawn* PlayerPawn,
	UProjectCompanionRevivalConsumable* Item,
	FGuid& OutTransactionId,
	FString& OutError)
{
	OutTransactionId.Invalidate();
	if (!CanBeginRevival(PlayerPawn, Item, OutError))
	{
		return false;
	}

	FGuid ItemGuid;
	int32 ItemCount = 0;
	UACFInventoryComponent* Inventory = nullptr;
	if (!FindExactItemEntry(PlayerPawn, Item, ItemGuid, ItemCount, Inventory))
	{
		OutError = TEXT("Exact inventory entry disappeared during transaction freeze.");
		return false;
	}

	const TArray<FProjectCompanionRevivalCandidate> Candidates = GetRevivalCandidates();
	RevivalTransaction = FRevivalTransaction();
	RevivalTransaction.bActive = true;
	RevivalTransaction.TransactionId = FGuid::NewGuid();
	RevivalTransaction.ItemGuid = ItemGuid;
	RevivalTransaction.OriginalItemCount = ItemCount;
	RevivalTransaction.FrozenRunEpoch = RunEpoch;
	RevivalTransaction.FrozenFloor = CurrentFloor;
	RevivalTransaction.FrozenGenerationSerial = CurrentGenerationSerial;
	RevivalTransaction.FrozenWorld = GetWorld();
	RevivalTransaction.PlayerPawn = PlayerPawn;
	RevivalTransaction.Inventory = Inventory;
	RevivalTransaction.Item = Item;
	for (const FProjectCompanionRevivalCandidate& Candidate : Candidates)
	{
		RevivalTransaction.EligibleCompanionIds.Add(Candidate.StableCompanionId);
	}

	OpenRevivalMenu(Candidates, OutError);
	if (!OutError.IsEmpty())
	{
		RevivalTransaction = FRevivalTransaction();
		return false;
	}
	OutTransactionId = RevivalTransaction.TransactionId;
	OnRevivalSelectionRequested.Broadcast(OutTransactionId, Candidates);
	return true;
}

bool UProjectRunCompanionSubsystem::RevalidateRevivalTransaction(
	const FGuid StableCompanionId,
	FString& OutError) const
{
	OutError.Reset();
	if (!RevivalTransaction.bActive || !StableCompanionId.IsValid()
		|| !RevivalTransaction.EligibleCompanionIds.Contains(StableCompanionId)
		|| RevivalTransaction.FrozenRunEpoch != RunEpoch
		|| RevivalTransaction.FrozenFloor != CurrentFloor
		|| RevivalTransaction.FrozenGenerationSerial != CurrentGenerationSerial
		|| RevivalTransaction.FrozenWorld.Get() != GetWorld()
		|| RevivalTransaction.PlayerPawn.Get() != ResolveLocalPlayerPawn())
	{
		OutError = TEXT("The frozen revival token no longer matches run, floor, serial, world or candidate.");
		return false;
	}
	if (!bFloorReady || !bCompanionRosterReady || bGenerationOrTravelActive
		|| !IsPlayerOutsideCombat(RevivalTransaction.PlayerPawn.Get()))
	{
		OutError = TEXT("The player, floor or companion-roster state changed while the revival menu was open.");
		return false;
	}
	const FRuntimeCompanionRecord* Record = Roster.Find(StableCompanionId);
	if (!Record || (Record->Snapshot.State != EProjectCompanionRunState::Dead
		&& Record->Snapshot.State != EProjectCompanionRunState::PendingDead))
	{
		OutError = TEXT("The selected companion is no longer revival-eligible.");
		return false;
	}
	int32 ActiveLiving = 0;
	for (const TPair<FGuid, FRuntimeCompanionRecord>& Pair : Roster)
	{
		ActiveLiving += ((Pair.Value.bDesiredActiveParty
			&& Pair.Value.Snapshot.State == EProjectCompanionRunState::Alive) ? 1 : 0);
	}
	if (ActiveLiving >= ProjectRunCompanionPrivate::MaximumActiveParty)
	{
		OutError = TEXT("The active party became full while the revival menu was open.");
		return false;
	}
	FInventoryItem ExactEntry;
	if (!RevivalTransaction.Inventory.IsValid()
		|| !RevivalTransaction.Inventory->GetItemByGuid(RevivalTransaction.ItemGuid, ExactEntry)
		|| ExactEntry.Item != RevivalTransaction.Item.Get()
		|| ExactEntry.Count != RevivalTransaction.OriginalItemCount)
	{
		OutError = TEXT("The exact inventory GUID/count/item object changed during the transaction.");
		return false;
	}
	return true;
}

bool UProjectRunCompanionSubsystem::ConfirmPendingRevival(
	const FGuid TransactionId,
	const FGuid StableCompanionId)
{
	if (!RevivalTransaction.bActive || TransactionId != RevivalTransaction.TransactionId)
	{
		return false;
	}

	FString Error;
	if (!RevalidateRevivalTransaction(StableCompanionId, Error))
	{
		FinishRevivalTransaction(false, FText::FromString(Error));
		return false;
	}
	FEFCalystoResolvedCompanionLevelV4 FrozenLevel;
	if (!ResolveFrozenRevivalCompanionLevel(StableCompanionId, FrozenLevel, Error))
	{
		FinishRevivalTransaction(false, FText::FromString(Error));
		return false;
	}

	FRuntimeCompanionRecord& Record = Roster.FindChecked(StableCompanionId);
	const EProjectCompanionRunState PreviousState = Record.Snapshot.State;
	const bool bPreviousActiveParty = Record.bDesiredActiveParty;
	const FProjectCompanionDefinition PreviousDefinition = Record.Snapshot.Definition;
	FProjectCompanionDefinition RevivalDefinition = PreviousDefinition;
	RevivalDefinition.ResolvedLevel = FrozenLevel.LogicalLevel;
	AActor* PreviousCorpse = Record.CorpseActor.Get();
	APawn* PlayerPawn = RevivalTransaction.PlayerPawn.Get();
	UACFCompanionGroupAIComponent* Group = UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(PlayerPawn);
	UClass* CharacterClass = RevivalDefinition.CharacterClass.Get();
	FTransform SpawnTransform;
	TArray<FVector> Reserved;
	if (!Group || !CharacterClass
		|| !UProjectCompanionRuntimeAdapter::FindDeterministicSafeSpawnTransform(
			GetWorld(), PlayerPawn, StableCompanionId, CharacterClass, Reserved, SpawnTransform, Error))
	{
		FinishRevivalTransaction(false, FText::FromString(Error.IsEmpty() ? TEXT("No safe revival point is available.") : Error));
		return false;
	}

	Record.Snapshot.State = EProjectCompanionRunState::PendingRevival;
	FProjectCompanionSpawnResult Spawn = UProjectCompanionRuntimeAdapter::SpawnAndRegisterCompanion(
		this, RevivalDefinition, SpawnTransform, Group);
	if (!Spawn.bSucceeded || !Spawn.SpawnedCharacter || !AttachDeathProxy(Spawn.SpawnedCharacter, StableCompanionId))
	{
		if (Spawn.SpawnedCharacter)
		{
			UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Spawn.SpawnedCharacter, Group);
		}
		Record.Snapshot.State = PreviousState;
		Record.bDesiredActiveParty = bPreviousActiveParty;
		Record.Snapshot.Definition = PreviousDefinition;
		FinishRevivalTransaction(false, FText::FromString(
			Spawn.Diagnostic.IsEmpty() ? TEXT("The fresh companion actor failed validation.") : Spawn.Diagnostic));
		return false;
	}

	FInventoryItem ExactEntry;
	UACFInventoryComponent* Inventory = RevivalTransaction.Inventory.Get();
	const int32 ExpectedRemaining = RevivalTransaction.OriginalItemCount - 1;
	if (!Inventory
		|| !Inventory->GetItemByGuid(RevivalTransaction.ItemGuid, ExactEntry)
		|| ExactEntry.Item != RevivalTransaction.Item.Get()
		|| ExactEntry.Count != RevivalTransaction.OriginalItemCount)
	{
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Spawn.SpawnedCharacter, Group);
		Record.Snapshot.State = PreviousState;
		Record.bDesiredActiveParty = bPreviousActiveParty;
		Record.Snapshot.Definition = PreviousDefinition;
		FinishRevivalTransaction(false, LOCTEXT("RecallMissingBeforeConsume", "The exact item disappeared; revival was rolled back."));
		return false;
	}

	UACFEquipmentComponent* TransactionEquipment = ResolveEquipment(PlayerPawn);
	TArray<uint8> FrozenInventoryBytes;
	const FString FrozenInventoryHash = ComputeInventoryHash(TransactionEquipment);
	FString InventoryCapsuleError;
	if (!TransactionEquipment
		|| static_cast<UACFInventoryComponent*>(TransactionEquipment) != Inventory
		|| FrozenInventoryHash.IsEmpty()
		|| !SerializeEquipmentCapsule(TransactionEquipment, FrozenInventoryBytes, InventoryCapsuleError))
	{
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Spawn.SpawnedCharacter, Group);
		Record.Snapshot.State = PreviousState;
		Record.bDesiredActiveParty = bPreviousActiveParty;
		Record.Snapshot.Definition = PreviousDefinition;
		const FString CapsuleDiagnostic = InventoryCapsuleError.IsEmpty()
			? FString(TEXT("The exact typed ACF inventory could not be frozen before consumption; revival was rolled back."))
			: InventoryCapsuleError;
		FinishRevivalTransaction(false, FText::FromString(CapsuleDiagnostic));
		return false;
	}
	const FString FrozenInventoryBytesHash =
		ProjectRunCompanionPrivate::HashBytes(FrozenInventoryBytes);
	if (FrozenInventoryBytesHash.IsEmpty())
	{
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Spawn.SpawnedCharacter, Group);
		Record.Snapshot.State = PreviousState;
		Record.bDesiredActiveParty = bPreviousActiveParty;
		Record.Snapshot.Definition = PreviousDefinition;
		FinishRevivalTransaction(false, LOCTEXT("RecallFreezeFailed", "The inventory rollback capsule is invalid; revival was rolled back."));
		return false;
	}
	Inventory->RemoveItem(ExactEntry, 1);

	FInventoryItem RemainingEntry;
	const bool bStillPresent = Inventory->GetItemByGuid(RevivalTransaction.ItemGuid, RemainingEntry);
	const bool bRemovalVerified = ExpectedRemaining == 0
		? !bStillPresent
		: bStillPresent
			&& RemainingEntry.Count == ExpectedRemaining
			&& RemainingEntry.Item == ExactEntry.Item
			&& RemainingEntry.ItemClass == ExactEntry.ItemClass;
	if (!bRemovalVerified)
	{
		UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(Spawn.SpawnedCharacter, Group);
		Record.Snapshot.State = PreviousState;
		Record.bDesiredActiveParty = bPreviousActiveParty;
		Record.Snapshot.Definition = PreviousDefinition;
		FString RestoreError;
		const bool bInventoryRestored = RestoreRevivalInventoryCapsule(
			TransactionEquipment,
			FrozenInventoryBytes,
			FrozenInventoryBytesHash,
			FrozenInventoryHash,
			RestoreError);
		if (!bInventoryRestored)
		{
			UE_LOG(LogProjectRunCompanions, Error,
				TEXT("Winter's Recall transaction %s failed to restore the exact ACF inventory after an invalid RemoveItem postcondition: %s"),
				*TransactionId.ToString(EGuidFormats::Digits),
				*RestoreError);
			FinishRevivalTransaction(false, FText::FromString(FString::Printf(
				TEXT("Critical inventory rollback failure; revival was rejected: %s"),
				*RestoreError)));
			return false;
		}
		FinishRevivalTransaction(false, LOCTEXT("RecallConsumeFailed", "ACF did not remove exactly one item; the exact inventory and revival were restored."));
		return false;
	}

	if (IsValid(PreviousCorpse) && PreviousCorpse != Spawn.SpawnedCharacter)
	{
		PreviousCorpse->Destroy();
	}
	Record.Snapshot.State = EProjectCompanionRunState::Alive;
	Record.Snapshot.Definition = RevivalDefinition;
	Record.bDesiredActiveParty = true;
	Record.Snapshot.DeathFloor = 0;
	Record.Snapshot.DeathGenerationSerial = 0;
	Record.LiveActor = Spawn.SpawnedCharacter;
	Record.CorpseActor.Reset();
	CurrentCompanionSnapshotHash = FEFCalystoDungeonDirectorMathV4::GetCompanionSnapshotHash(
		BuildDirectorSnapshot(BuildSnapshot()));
	OnDeathStateChanged.Broadcast(StableCompanionId, EProjectCompanionRunState::Alive);
	OnRosterChanged.Broadcast();
	FinishRevivalTransaction(true, LOCTEXT("RecallSucceeded", "The companion returned safely."));
	return true;
}

void UProjectRunCompanionSubsystem::CancelPendingRevival(const FGuid TransactionId)
{
	if (RevivalTransaction.bActive && RevivalTransaction.TransactionId == TransactionId)
	{
		FinishRevivalTransaction(false, LOCTEXT("RecallCancelled", "Revival cancelled; the item was not consumed."));
	}
}

void UProjectRunCompanionSubsystem::FinishRevivalTransaction(
	const bool bSucceeded,
	const FText& Result)
{
	const FGuid FinishedId = RevivalTransaction.TransactionId;
	CloseRevivalMenu();
	RevivalTransaction = FRevivalTransaction();
	OnRevivalFinished.Broadcast(FinishedId, bSucceeded, Result);
}

void UProjectRunCompanionSubsystem::OpenRevivalMenu(
	const TArray<FProjectCompanionRevivalCandidate>& Candidates,
	FString& OutError)
{
	OutError.Reset();
	APawn* PlayerPawn = RevivalTransaction.PlayerPawn.Get();
	APlayerController* PlayerController = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		OutError = TEXT("Winter's Recall requires a local player controller for its selection menu.");
		return;
	}
	RevivalMenuWidget = CreateWidget<UProjectCompanionRevivalMenuWidget>(
		PlayerController, UProjectCompanionRevivalMenuWidget::StaticClass(), TEXT("ProjectCompanionRevivalMenuWidget"));
	if (!RevivalMenuWidget || !RevivalMenuWidget->ConfigureCandidates(Candidates))
	{
		RevivalMenuWidget = nullptr;
		OutError = TEXT("The project-owned revival menu could not represent the frozen candidate set.");
		return;
	}
	RevivalMenuWidget->OnOptionConfirmed.AddUObject(this, &ThisClass::HandleRevivalMenuOption);
	RevivalMenuWidget->OnCancelRequested.AddUObject(this, &ThisClass::HandleRevivalMenuCancel);
	RevivalMenuWidget->OnBackRequested.AddUObject(this, &ThisClass::HandleRevivalMenuCancel);
	if (!RevivalMenuWidget->AddToPlayerScreen(90))
	{
		RevivalMenuWidget->AddToViewport(90);
	}
	ApplyMenuInputCapture(PlayerPawn);
	RevivalMenuWidget->FocusInitialOption();
}

void UProjectRunCompanionSubsystem::CloseRevivalMenu()
{
	if (RevivalMenuWidget)
	{
		RevivalMenuWidget->OnOptionConfirmed.RemoveAll(this);
		RevivalMenuWidget->OnCancelRequested.RemoveAll(this);
		RevivalMenuWidget->OnBackRequested.RemoveAll(this);
		RevivalMenuWidget->RemoveFromParent();
		RevivalMenuWidget = nullptr;
	}
	RestoreMenuInputCapture();
}

void UProjectRunCompanionSubsystem::HandleRevivalMenuOption(const FName OptionId)
{
	if (!RevivalTransaction.bActive || !RevivalMenuWidget)
	{
		return;
	}
	if (OptionId == UProjectCompanionRevivalMenuWidget::GetCancelOptionId())
	{
		CancelPendingRevival(RevivalTransaction.TransactionId);
		return;
	}
	FGuid StableId;
	if (!RevivalMenuWidget->TryResolveCandidate(OptionId, StableId))
	{
		FinishRevivalTransaction(false, LOCTEXT("InvalidMenuCandidate", "The selected companion token is invalid; the item was not consumed."));
		return;
	}
	ConfirmPendingRevival(RevivalTransaction.TransactionId, StableId);
}

void UProjectRunCompanionSubsystem::HandleRevivalMenuCancel()
{
	if (RevivalTransaction.bActive)
	{
		CancelPendingRevival(RevivalTransaction.TransactionId);
	}
}

void UProjectRunCompanionSubsystem::ApplyMenuInputCapture(APawn* PlayerPawn)
{
	APlayerController* Controller = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (!Controller)
	{
		return;
	}
	MenuInputSnapshot.bValid = true;
	MenuInputSnapshot.bMoveIgnored = Controller->IsMoveInputIgnored();
	MenuInputSnapshot.bLookIgnored = Controller->IsLookInputIgnored();
	MenuInputSnapshot.bMouseCursorVisible = Controller->bShowMouseCursor;
	MenuInputSnapshot.bClickEventsEnabled = Controller->bEnableClickEvents;
	MenuInputSnapshot.bMouseOverEventsEnabled = Controller->bEnableMouseOverEvents;
	Controller->SetIgnoreMoveInput(true);
	Controller->SetIgnoreLookInput(true);
	Controller->bShowMouseCursor = false;
	Controller->bEnableClickEvents = false;
	Controller->bEnableMouseOverEvents = false;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(RevivalMenuWidget ? RevivalMenuWidget->TakeWidget() : TSharedPtr<SWidget>());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->SetInputMode(InputMode);
}

void UProjectRunCompanionSubsystem::RestoreMenuInputCapture()
{
	APawn* PlayerPawn = RevivalTransaction.PlayerPawn.Get();
	APlayerController* Controller = PlayerPawn ? Cast<APlayerController>(PlayerPawn->GetController()) : nullptr;
	if (Controller && MenuInputSnapshot.bValid)
	{
		Controller->SetIgnoreMoveInput(MenuInputSnapshot.bMoveIgnored);
		Controller->SetIgnoreLookInput(MenuInputSnapshot.bLookIgnored);
		Controller->bShowMouseCursor = MenuInputSnapshot.bMouseCursorVisible;
		Controller->bEnableClickEvents = MenuInputSnapshot.bClickEventsEnabled;
		Controller->bEnableMouseOverEvents = MenuInputSnapshot.bMouseOverEventsEnabled;
		FInputModeGameOnly InputMode;
		Controller->SetInputMode(InputMode);
	}
	MenuInputSnapshot = FMenuInputSnapshot();
}

EProjectCompanionDirectorTravelMode UProjectRunCompanionSubsystem::ConvertTravelMode(
	const EEFCalystoDungeonTravelKindV4 Kind)
{
	switch (Kind)
	{
	case EEFCalystoDungeonTravelKindV4::NewRun: return EProjectCompanionDirectorTravelMode::NewRun;
	case EEFCalystoDungeonTravelKindV4::Replay: return EProjectCompanionDirectorTravelMode::Replay;
	case EEFCalystoDungeonTravelKindV4::Reroll: return EProjectCompanionDirectorTravelMode::Reroll;
	case EEFCalystoDungeonTravelKindV4::Advance: return EProjectCompanionDirectorTravelMode::Advance;
	case EEFCalystoDungeonTravelKindV4::DebugJump: return EProjectCompanionDirectorTravelMode::DebugJump;
	default: return EProjectCompanionDirectorTravelMode::None;
	}
}

#undef LOCTEXT_NAMESPACE
