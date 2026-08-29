#include "Companions/ProjectRunCompanionTypes.h"

#include "Actors/ACFCharacter.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Engine/DataTable.h"

namespace ProjectRunCompanionTypesPrivate
{
	FString GuidKey(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}

	bool IsKnownDifficultyGrade(const EProjectCompanionDifficultyGrade Grade)
	{
		switch (Grade)
		{
		case EProjectCompanionDifficultyGrade::Common:
		case EProjectCompanionDifficultyGrade::Uncommon:
		case EProjectCompanionDifficultyGrade::Rare:
		case EProjectCompanionDifficultyGrade::Epic:
		case EProjectCompanionDifficultyGrade::Winter:
			return true;
		default:
			return false;
		}
	}

	bool IsKnownLifecycle(const EProjectCompanionLifecycle Lifecycle)
	{
		return Lifecycle == EProjectCompanionLifecycle::FloorLocal
			|| Lifecycle == EProjectCompanionLifecycle::Recruitable;
	}

	bool IsKnownRunState(const EProjectCompanionRunState State)
	{
		switch (State)
		{
		case EProjectCompanionRunState::Alive:
		case EProjectCompanionRunState::PendingDead:
		case EProjectCompanionRunState::Dead:
		case EProjectCompanionRunState::PendingRevival:
			return true;
		default:
			return false;
		}
	}
}

FProjectCompanionDefinition::FProjectCompanionDefinition()
	: StatisticsRepairDataTable(FSoftObjectPath(
		TEXT("/AscentCombatFramework/Configuration/ACF_SampleAttributesInit_DT.ACF_SampleAttributesInit_DT")))
{
}

bool FProjectCompanionDefinition::IsValid(FString& OutError) const
{
	OutError.Reset();
	if (!StableCompanionId.IsValid())
	{
		OutError = TEXT("StableCompanionId is required.");
		return false;
	}
	if (SourceSpawnId.IsNone())
	{
		OutError = TEXT("SourceSpawnId is required and must identify the originating spawn instance.");
		return false;
	}
	if (ContentId.IsNone())
	{
		OutError = TEXT("ContentId is required.");
		return false;
	}
	if (CatalogVariantId.IsNone())
	{
		OutError = TEXT("CatalogVariantId is required.");
		return false;
	}
	if (CharacterClass.IsNull())
	{
		OutError = FString::Printf(TEXT("%s has no CharacterClass."), *ContentId.ToString());
		return false;
	}
	if (ResolvedLevel < 1)
	{
		OutError = FString::Printf(TEXT("%s has an invalid logical level."), *ContentId.ToString());
		return false;
	}
	if (bRepairMissingStatisticsRow && (StatisticsRepairDataTable.IsNull() || StatisticsRepairRow.IsNone()))
	{
		OutError = FString::Printf(TEXT("%s has an incomplete statistics repair contract."), *ContentId.ToString());
		return false;
	}
	return true;
}

FString FProjectCompanionRunSnapshot::ComputeCanonicalHash() const
{
	TArray<FProjectCompanionRunEntrySnapshot> SortedEntries = Entries;
	SortedEntries.Sort([](
		const FProjectCompanionRunEntrySnapshot& Left,
		const FProjectCompanionRunEntrySnapshot& Right)
	{
		return ProjectRunCompanionTypesPrivate::GuidKey(Left.Definition.StableCompanionId)
			< ProjectRunCompanionTypesPrivate::GuidKey(Right.Definition.StableCompanionId);
	});

	TSet<FGuid> ActivePartySet;
	for (const FGuid& ActiveId : ActiveParty)
	{
		ActivePartySet.Add(ActiveId);
	}

	FString Canonical = FString::Printf(
		TEXT("ProjectCompanionRosterV4|%lld|%lld|entries=%d|party=%d|"),
		FloorNumber,
		GenerationSerial,
		SortedEntries.Num(),
		ActiveParty.Num());
	for (const FProjectCompanionRunEntrySnapshot& Entry : SortedEntries)
	{
		const FProjectCompanionDefinition& Definition = Entry.Definition;
		Canonical += FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%s|%s|%d|%lld|%lld|%d|"),
			*ProjectRunCompanionTypesPrivate::GuidKey(Definition.StableCompanionId),
			*Definition.SourceSpawnId.ToString(),
			*Definition.ContentId.ToString(),
			*Definition.CatalogVariantId.ToString(),
			*Definition.CharacterClass.ToSoftObjectPath().ToString(),
			*Definition.Archetype.ToString(),
			*Definition.Gender.ToString(),
			static_cast<int32>(Definition.DifficultyGrade),
			Definition.ResolvedLevel,
			static_cast<int32>(Definition.Lifecycle),
			Definition.bRepairMissingStatisticsRow ? 1 : 0,
			*Definition.StatisticsRepairDataTable.ToSoftObjectPath().ToString(),
			*Definition.StatisticsRepairRow.ToString(),
			static_cast<int32>(Entry.State),
			Entry.DeathFloor,
			Entry.DeathGenerationSerial,
			ActivePartySet.Contains(Definition.StableCompanionId) ? 1 : 0);
	}
	return UEFCalystoDungeonSubsystem::ComputeCanonicalHash(Canonical);
}

void FProjectCompanionRunSnapshot::RefreshHash()
{
	SnapshotHash = ComputeCanonicalHash();
}

bool FProjectCompanionRunSnapshot::IsValid(FString& OutError) const
{
	OutError.Reset();
	if (RunEpoch <= 0 || FloorNumber <= 0 || GenerationSerial <= 0 || SnapshotHash.Len() != 64)
	{
		OutError = TEXT("Companion snapshot identity or SHA-256 is invalid.");
		return false;
	}

	TSet<FGuid> SeenIds;
	for (const FProjectCompanionRunEntrySnapshot& Entry : Entries)
	{
		FString EntryError;
		if (!Entry.Definition.IsValid(EntryError))
		{
			OutError = EntryError;
			return false;
		}
		if (!ProjectRunCompanionTypesPrivate::IsKnownDifficultyGrade(Entry.Definition.DifficultyGrade)
			|| !ProjectRunCompanionTypesPrivate::IsKnownLifecycle(Entry.Definition.Lifecycle)
			|| Entry.Definition.Lifecycle != EProjectCompanionLifecycle::Recruitable
			|| !ProjectRunCompanionTypesPrivate::IsKnownRunState(Entry.State))
		{
			OutError = FString::Printf(
				TEXT("Companion %s has an invalid grade, lifecycle or run state."),
				*Entry.Definition.StableCompanionId.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		const bool bAlive = Entry.State == EProjectCompanionRunState::Alive;
		if ((bAlive && (Entry.DeathFloor != 0 || Entry.DeathGenerationSerial != 0))
			|| (!bAlive && (Entry.DeathFloor <= 0
				|| Entry.DeathGenerationSerial <= 0
				|| Entry.DeathFloor > FloorNumber)))
		{
			OutError = FString::Printf(
				TEXT("Companion %s has incoherent death metadata for its run state."),
				*Entry.Definition.StableCompanionId.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		if (SeenIds.Contains(Entry.Definition.StableCompanionId))
		{
			OutError = FString::Printf(TEXT("Duplicate StableCompanionId: %s"), *Entry.Definition.StableCompanionId.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}
		SeenIds.Add(Entry.Definition.StableCompanionId);
	}

	if (ActiveParty.Num() > 2)
	{
		OutError = TEXT("The active companion party exceeds the hard cap of two.");
		return false;
	}
	TSet<FGuid> SeenPartyIds;
	for (const FGuid& PartyId : ActiveParty)
	{
		if (!SeenIds.Contains(PartyId) || SeenPartyIds.Contains(PartyId))
		{
			OutError = TEXT("ActiveParty contains an unknown or duplicate StableCompanionId.");
			return false;
		}
		const FProjectCompanionRunEntrySnapshot* PartyEntry = Entries.FindByPredicate(
			[&PartyId](const FProjectCompanionRunEntrySnapshot& Candidate)
			{
				return Candidate.Definition.StableCompanionId == PartyId;
			});
		if (!PartyEntry || PartyEntry->State != EProjectCompanionRunState::Alive)
		{
			OutError = TEXT("ActiveParty may contain only living companions.");
			return false;
		}
		SeenPartyIds.Add(PartyId);
	}
	const FString CanonicalHash = ComputeCanonicalHash();
	if (CanonicalHash.Len() != 64 || SnapshotHash != CanonicalHash)
	{
		OutError = TEXT("Companion snapshot SHA-256 does not match its canonical contents.");
		return false;
	}
	return true;
}
