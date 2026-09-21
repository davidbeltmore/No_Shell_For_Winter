#include "Calysto/EFCalystoPopulationPlannerV6.h"

namespace EFCalystoPopulationV6Private
{
enum class EBudgetBucket : uint8
{
	None,
	Enemy,
	LooseFood,
	Chest,
	LootActor,
	SpecialEvent
};

struct FActorProposal
{
	int64 StableRoomId = 0;
	FName ThemeId = TEXT("NoTheme");
	FName CategoryId = NAME_None;
	FName EntryId = NAME_None;
	FSoftObjectPath ClassPath;
	int32 SpawnOrdinal = 0;
	EEFCalystoPlacementZoneV6 PlacementZone = EEFCalystoPlacementZoneV6::Floor;
	float PositionJitterCm = 0.0f;
	int32 EffectiveCategoryLimit = 0;
	int32 MaximumPerVariant = 0;
	EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common;
	EEFCalystoLifecycleV6 Lifecycle = EEFCalystoLifecycleV6::FloorLocal;
	float ThreatCost = 0.0f;
	uint64 BudgetPriority = 0;
};

struct FAcceptedChest
{
	int32 RoomPlanIndex = INDEX_NONE;
	FEFCalystoPopulationDecisionV6 Parent;
};

bool Fail(FString& OutError, const FString& Message)
{
	OutError = Message;
	return false;
}

FString CanonicalToken(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.ToLowerInline();
	return Value;
}

FString CanonicalToken(const FName Value)
{
	return CanonicalToken(Value.ToString());
}

FString FloatBits(const float Value)
{
	const float CanonicalValue = Value == 0.0f ? 0.0f : Value;
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &CanonicalValue, sizeof(Bits));
	return FString::Printf(TEXT("%08X"), Bits);
}

bool IsSha256(const FString& Value)
{
	if (Value.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (!FChar::IsHexDigit(Character))
		{
			return false;
		}
	}
	return true;
}

uint64 Hash64(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	uint64 Result = 1469598103934665603ULL;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		Result ^= static_cast<uint8>(Utf8.Get()[Index]);
		Result *= 1099511628211ULL;
	}
	Result ^= Result >> 33;
	Result *= 0xff51afd7ed558ccdULL;
	Result ^= Result >> 33;
	Result *= 0xc4ceb9fe1a85ec53ULL;
	Result ^= Result >> 33;
	return Result;
}

uint64 LaneHash(
	const TCHAR* Domain,
	const int64 FloorSeed,
	const int64 StableRoomId,
	const FName CategoryId,
	const int32 DrawIndex,
	const FString& Extra = FString())
{
	return Hash64(FString::Printf(
		TEXT("EFCalystoPopulationV6|%s|%lld|%lld|%s|%d|%s"),
		Domain, FloorSeed, StableRoomId, *CanonicalToken(CategoryId), DrawIndex,
		*CanonicalToken(Extra)));
}

double UniformFromHash(const uint64 Hash)
{
	return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
}

double Progression(const int64 FloorNumber, const float Tau)
{
	if (FloorNumber <= 1)
	{
		return 0.0;
	}
	return FMath::Clamp(
		1.0 - FMath::Exp(-static_cast<double>(FloorNumber - 1) /
			FMath::Max(static_cast<double>(Tau), UE_DOUBLE_SMALL_NUMBER)),
		0.0, 1.0);
}

double ResolveChance(const FEFCalystoChanceCurveV6& Curve, const int64 FloorNumber)
{
	return FMath::Clamp(FMath::Lerp(
		static_cast<double>(Curve.ChanceAtFloor1),
		static_cast<double>(Curve.ChanceAtFloor100),
		Progression(FloorNumber, Curve.Tau)), 0.0, 1.0);
}

bool SelectTier(
	const FEFCalystoCatalogOverlayV6& Category,
	const int64 FloorNumber,
	const double Uniform,
	EEFCalystoRarityTierV6& OutTier)
{
	const double Alpha = Progression(FloorNumber, Category.Presence.Tau);
	const FEFCalystoTierMixV6& A = Category.Tiers.AtFloor1;
	const FEFCalystoTierMixV6& B = Category.Tiers.AtFloor100;
	const double Weights[] = {
		FMath::Max(0.0, FMath::Lerp(static_cast<double>(A.Common), static_cast<double>(B.Common), Alpha)),
		FMath::Max(0.0, FMath::Lerp(static_cast<double>(A.Uncommon), static_cast<double>(B.Uncommon), Alpha)),
		FMath::Max(0.0, FMath::Lerp(static_cast<double>(A.Rare), static_cast<double>(B.Rare), Alpha)),
		FMath::Max(0.0, FMath::Lerp(static_cast<double>(A.Epic), static_cast<double>(B.Epic), Alpha))};
	const EEFCalystoRarityTierV6 Tiers[] = {
		EEFCalystoRarityTierV6::Common,
		EEFCalystoRarityTierV6::Uncommon,
		EEFCalystoRarityTierV6::Rare,
		EEFCalystoRarityTierV6::Epic};
	double Cursor = 0.0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Weights); ++Index)
	{
		Cursor += Weights[Index];
		if (Uniform < Cursor)
		{
			OutTier = Tiers[Index];
			return true;
		}
	}
	return false;
}

template <typename EntryType>
struct TCompiledAliasTable
{
	bool bCompiled = false;
	TArray<const EntryType*> Entries;
	TArray<double> Probability;
	TArray<int32> Alias;

	template <typename PredicateType, typename WeightType>
	bool Compile(
		const TArray<EntryType>& Source,
		PredicateType&& IsEligible,
		WeightType&& GetWeight,
		FString& OutError)
	{
		bCompiled = true;
		Entries.Reset();
		Probability.Reset();
		Alias.Reset();
		TSet<FString> StableIds;
		for (const EntryType& Entry : Source)
		{
			if (!IsEligible(Entry))
			{
				continue;
			}
			const FString StableId = CanonicalToken(Entry.StableId);
			const double Weight = GetWeight(Entry);
			if (StableId.IsEmpty() || StableIds.Contains(StableId) ||
				!FMath::IsFinite(Weight) || Weight <= 0.0)
			{
				return Fail(OutError,
					TEXT("V6 population could not compile a canonical weighted selection table."));
			}
			StableIds.Add(StableId);
			Entries.Add(&Entry);
		}
		Entries.Sort([](const EntryType& Left, const EntryType& Right)
		{
			return CanonicalToken(Left.StableId) < CanonicalToken(Right.StableId);
		});
		if (Entries.IsEmpty())
		{
			return true;
		}
		// Sum in canonical Stable ID order so authored-array order cannot alter
		// floating-point accumulation or an alias threshold at the boundary.
		double TotalWeight = 0.0;
		for (const EntryType* Entry : Entries)
		{
			TotalWeight += GetWeight(*Entry);
		}
		if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
		{
			return Fail(OutError, TEXT("V6 population weighted selection total is invalid."));
		}

		const int32 Count = Entries.Num();
		Probability.SetNumUninitialized(Count);
		Alias.SetNumUninitialized(Count);
		TArray<double> ScaledWeights;
		TArray<int32> Small;
		TArray<int32> Large;
		ScaledWeights.SetNumUninitialized(Count);
		Small.Reserve(Count);
		Large.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ScaledWeights[Index] = GetWeight(*Entries[Index]) * Count / TotalWeight;
			(ScaledWeights[Index] < 1.0 ? Small : Large).Add(Index);
		}
		while (!Small.IsEmpty() && !Large.IsEmpty())
		{
			const int32 SmallIndex = Small.Pop(EAllowShrinking::No);
			const int32 LargeIndex = Large.Pop(EAllowShrinking::No);
			Probability[SmallIndex] = ScaledWeights[SmallIndex];
			Alias[SmallIndex] = LargeIndex;
			ScaledWeights[LargeIndex] =
				(ScaledWeights[LargeIndex] + ScaledWeights[SmallIndex]) - 1.0;
			(ScaledWeights[LargeIndex] < 1.0 ? Small : Large).Add(LargeIndex);
		}
		for (const int32 Index : Large)
		{
			Probability[Index] = 1.0;
			Alias[Index] = Index;
		}
		for (const int32 Index : Small)
		{
			Probability[Index] = 1.0;
			Alias[Index] = Index;
		}
		return true;
	}

	const EntryType* Select(const double ColumnUniform, const double CoinUniform) const
	{
		if (!bCompiled || Entries.IsEmpty() ||
			Probability.Num() != Entries.Num() || Alias.Num() != Entries.Num())
		{
			return nullptr;
		}
		const int32 Column = FMath::Clamp(
			FMath::FloorToInt(ColumnUniform * Entries.Num()), 0, Entries.Num() - 1);
		const int32 SelectedIndex = CoinUniform < Probability[Column]
			? Column
			: Alias[Column];
		return Entries.IsValidIndex(SelectedIndex) ? Entries[SelectedIndex] : nullptr;
	}
};

FString SelectionTableKey(
	const FString& CatalogHash,
	const FName CategoryId,
	const EEFCalystoRarityTierV6 Tier)
{
	return FString::Printf(TEXT("%s|%s|%d"), *CatalogHash,
		*CanonicalToken(CategoryId), static_cast<int32>(Tier));
}

const FEFCalystoCatalogOverlayV6* FindCategory(
	const TArray<FEFCalystoCatalogOverlayV6>& Catalogs,
	const FName CategoryId)
{
	return Catalogs.FindByPredicate([CategoryId](const FEFCalystoCatalogOverlayV6& Candidate)
	{
		return Candidate.CategoryId.IsEqual(CategoryId, ENameCase::IgnoreCase);
	});
}

const FEFCalystoResolvedThemeProfileV6* FindTheme(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FName ThemeId)
{
	return FloorPlan.Themes.FindByPredicate([ThemeId](const FEFCalystoResolvedThemeProfileV6& Candidate)
	{
		return Candidate.ThemeId.IsEqual(ThemeId, ENameCase::IgnoreCase);
	});
}

bool GetFrozenRoomCatalogs(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoRoomContextV6& Room,
	const TArray<FEFCalystoCatalogOverlayV6>*& OutCatalogs,
	FString& OutError)
{
	OutCatalogs = nullptr;
	if (!Room.StyleId.IsEqual(FloorPlan.StyleId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, FString::Printf(
			TEXT("Room %lld does not belong to the selected V6 Style."), Room.StableRoomId));
	}
	if (!Room.bIsThemed)
	{
		if (!Room.ThemeId.IsEqual(UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase) ||
			Room.CatalogHash != FloorPlan.StyleCatalogHash)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Room %lld has an invalid frozen NoTheme catalog identity."), Room.StableRoomId));
		}
		OutCatalogs = &FloorPlan.StyleCatalogs;
		return true;
	}

	if (Room.ThemeId.IsNone() ||
		Room.ThemeId.IsEqual(UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, FString::Printf(
			TEXT("Room %lld is marked themed without a valid Theme ID."), Room.StableRoomId));
	}
	const FEFCalystoResolvedThemeProfileV6* Theme = FindTheme(FloorPlan, Room.ThemeId);
	if (!Theme || Room.CatalogHash != Theme->CatalogHash)
	{
		return Fail(OutError, FString::Printf(
			TEXT("Room %lld does not match its frozen V6 Theme catalog."), Room.StableRoomId));
	}
	// Theme.Catalogs is already the complete effective snapshot. Reapplying it as
	// an overlay would incorrectly resurrect Style categories removed by Block.
	OutCatalogs = &Theme->Catalogs;
	return true;
}

int32 StyleCategoryLimit(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FName CategoryId)
{
	const FEFCalystoCatalogOverlayV6* StyleCategory = FindCategory(FloorPlan.StyleCatalogs, CategoryId);
	if (!StyleCategory || StyleCategory->Mode == EEFCalystoCatalogOverlayModeV6::Block)
	{
		return 0;
	}
	return FMath::Max(0, StyleCategory->Limits.MaximumPerFloor);
}

EBudgetBucket BudgetBucket(const FName CategoryId)
{
	static const FName EnemyId(TEXT("Enemy"));
	static const FName FoodId(TEXT("Food"));
	static const FName ChestId(TEXT("Chest"));
	static const FName LooseLootId(TEXT("LooseLoot"));
	static const FName ClothingId(TEXT("Clothing"));
	static const FName NpcId(TEXT("NPC"));
	static const FName SpecialEventId(TEXT("SpecialEvent"));
	if (CategoryId.IsEqual(EnemyId, ENameCase::IgnoreCase)) return EBudgetBucket::Enemy;
	if (CategoryId.IsEqual(FoodId, ENameCase::IgnoreCase)) return EBudgetBucket::LooseFood;
	if (CategoryId.IsEqual(ChestId, ENameCase::IgnoreCase)) return EBudgetBucket::Chest;
	if (CategoryId.IsEqual(LooseLootId, ENameCase::IgnoreCase) ||
		CategoryId.IsEqual(ClothingId, ENameCase::IgnoreCase)) return EBudgetBucket::LootActor;
	if (CategoryId.IsEqual(NpcId, ENameCase::IgnoreCase) ||
		CategoryId.IsEqual(SpecialEventId, ENameCase::IgnoreCase)) return EBudgetBucket::SpecialEvent;
	return EBudgetBucket::None;
}

int32 BucketLimit(const FEFCalystoFloorBudgetV6& Budgets, const EBudgetBucket Bucket)
{
	switch (Bucket)
	{
	case EBudgetBucket::Enemy: return FMath::Max(0, Budgets.MaximumEnemies);
	case EBudgetBucket::LooseFood: return FMath::Max(0, Budgets.MaximumLooseFood);
	case EBudgetBucket::Chest: return FMath::Max(0, Budgets.MaximumChests);
	case EBudgetBucket::LootActor: return FMath::Max(0, Budgets.MaximumLootActors);
	case EBudgetBucket::SpecialEvent: return FMath::Max(0, Budgets.MaximumSpecialEvents);
	default: return MAX_int32;
	}
}

FString GroupKey(const FName ThemeId, const FName CategoryId)
{
	return CanonicalToken(ThemeId) + TEXT("|") + CanonicalToken(CategoryId);
}

FString MakeDecisionId(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationDecisionV6& Decision)
{
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(FString::Printf(
		TEXT("EFCalystoPopulationDecisionV6|%s|%s|%lld|%s|%s|%s|%d|%d|%d|%s|%s|%s"),
		*FloorPlan.FloorPlanHash, *RoomManifest.ManifestHash, Decision.StableRoomId,
		*CanonicalToken(Decision.ThemeId), *CanonicalToken(Decision.CategoryId),
		*CanonicalToken(Decision.EntryId), static_cast<int32>(Decision.Kind),
		Decision.SpawnOrdinal, static_cast<int32>(Decision.PlacementZone),
		*FloatBits(Decision.PositionJitterCm), *CanonicalToken(Decision.ClassPath.ToString()),
		*Decision.ParentDecisionId));
}

void IncrementNamedCount(
	TMap<FString, int32>& Counts,
	TMap<FString, FName>& Names,
	const FName Id)
{
	const FString Key = CanonicalToken(Id);
	++Counts.FindOrAdd(Key);
	Names.FindOrAdd(Key, Id);
}

void ExportCounts(
	const TMap<FString, int32>& Counts,
	const TMap<FString, FName>& Names,
	TArray<FEFCalystoPopulationCountV6>& OutCounts)
{
	TArray<FString> Keys;
	Counts.GetKeys(Keys);
	Keys.Sort();
	OutCounts.Reset(Keys.Num());
	for (const FString& Key : Keys)
	{
		FEFCalystoPopulationCountV6& Item = OutCounts.AddDefaulted_GetRef();
		Item.Id = Names.FindRef(Key);
		Item.Count = Counts.FindRef(Key);
	}
}

bool ProposalLess(const FActorProposal& Left, const FActorProposal& Right)
{
	if (Left.BudgetPriority != Right.BudgetPriority) return Left.BudgetPriority < Right.BudgetPriority;
	if (Left.StableRoomId != Right.StableRoomId) return Left.StableRoomId < Right.StableRoomId;
	const FString LeftCategory = CanonicalToken(Left.CategoryId);
	const FString RightCategory = CanonicalToken(Right.CategoryId);
	if (LeftCategory != RightCategory) return LeftCategory < RightCategory;
	if (Left.SpawnOrdinal != Right.SpawnOrdinal) return Left.SpawnOrdinal < Right.SpawnOrdinal;
	return CanonicalToken(Left.EntryId) < CanonicalToken(Right.EntryId);
}

bool DecisionLess(
	const FEFCalystoPopulationDecisionV6& Left,
	const FEFCalystoPopulationDecisionV6& Right)
{
	if (Left.Kind != Right.Kind) return Left.Kind < Right.Kind;
	const FString LeftCategory = CanonicalToken(Left.CategoryId);
	const FString RightCategory = CanonicalToken(Right.CategoryId);
	if (LeftCategory != RightCategory) return LeftCategory < RightCategory;
	if (Left.SpawnOrdinal != Right.SpawnOrdinal) return Left.SpawnOrdinal < Right.SpawnOrdinal;
	const FString LeftEntry = CanonicalToken(Left.EntryId);
	const FString RightEntry = CanonicalToken(Right.EntryId);
	if (LeftEntry != RightEntry) return LeftEntry < RightEntry;
	return Left.DecisionId < Right.DecisionId;
}

FString BuildOptionsToken(const FEFCalystoPopulationBuildOptionsV6& Options)
{
	auto SortedNames = [](const TSet<FName>& Values)
	{
		TArray<FString> Result;
		Result.Reserve(Values.Num());
		for (const FName Value : Values) Result.Add(CanonicalToken(Value));
		Result.Sort();
		return FString::Join(Result, TEXT(","));
	};
	return FString::Printf(TEXT("%lld|%d|A:%s|C:%s"), Options.FloorNumber,
		Options.bGraveyardEligible ? 1 : 0,
		*SortedNames(Options.CoolingDownActorEntryIds),
		*SortedNames(Options.CoolingDownContentEntryIds));
}
} // namespace EFCalystoPopulationV6Private

bool FEFCalystoPopulationPlannerV6::ResolveEffectiveCatalogs(
	const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
	const TArray<FEFCalystoCatalogOverlayV6>& ThemeOverlays,
	TArray<FEFCalystoCatalogOverlayV6>& OutCatalogs,
	FString& OutError)
{
	return FEFCalystoDungeonDirectorMathV6::ResolveCatalogs(
		StyleCatalogs, ThemeOverlays, OutCatalogs, OutError);
}

bool FEFCalystoPopulationPlannerV6::BuildPlan(
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoRoomManifestV6& RoomManifest,
	const FEFCalystoPopulationBuildOptionsV6& Options,
	FEFCalystoPopulationPlanV6& OutPlan,
	FString& OutError)
{
	using namespace EFCalystoPopulationV6Private;
	OutPlan = FEFCalystoPopulationPlanV6();
	OutError.Reset();
	if (Options.FloorNumber < 1)
	{
		return Fail(OutError, TEXT("V6 population requires a one-based Floor Number."));
	}
	if (FloorPlan.StyleId.IsNone() || !IsSha256(FloorPlan.FloorPlanHash) ||
		!IsSha256(FloorPlan.StyleCatalogHash))
	{
		return Fail(OutError, TEXT("V6 population received an incomplete frozen Floor Plan."));
	}
	if (RoomManifest.FloorSeed != FloorPlan.FloorSeed ||
		!RoomManifest.StyleId.IsEqual(FloorPlan.StyleId, ENameCase::IgnoreCase) ||
		RoomManifest.FloorPlanHash != FloorPlan.FloorPlanHash ||
		!IsSha256(RoomManifest.ManifestHash))
	{
		return Fail(OutError, TEXT("V6 population received a Room Manifest for a different floor or Style."));
	}
	if (RoomManifest.Rooms.Num() > FloorPlan.MaximumRoomRecords)
	{
		return Fail(OutError, TEXT("V6 population Room Manifest exceeds the frozen room-record ceiling."));
	}

	OutPlan.FloorSeed = FloorPlan.FloorSeed;
	OutPlan.FloorNumber = Options.FloorNumber;
	OutPlan.StyleId = FloorPlan.StyleId;
	OutPlan.FloorPlanHash = FloorPlan.FloorPlanHash;
	OutPlan.RoomManifestHash = RoomManifest.ManifestHash;

	TArray<const FEFCalystoRoomContextV6*> OrderedRooms;
	OrderedRooms.Reserve(RoomManifest.Rooms.Num());
	TSet<int64> StableRoomIds;
	TMap<int64, const FEFCalystoRoomContextV6*> SourceRoomById;
	for (const FEFCalystoRoomContextV6& Room : RoomManifest.Rooms)
	{
		if (Room.StableRoomId == 0 || StableRoomIds.Contains(Room.StableRoomId))
		{
			return Fail(OutError, TEXT("V6 population requires unique non-zero Stable Room IDs."));
		}
		StableRoomIds.Add(Room.StableRoomId);
		const TArray<FEFCalystoCatalogOverlayV6>* FrozenCatalogs = nullptr;
		if (!GetFrozenRoomCatalogs(FloorPlan, Room, FrozenCatalogs, OutError))
		{
			return false;
		}
		OrderedRooms.Add(&Room);
		SourceRoomById.Add(Room.StableRoomId, &Room);
	}
	OrderedRooms.Sort([](const FEFCalystoRoomContextV6& Left, const FEFCalystoRoomContextV6& Right)
	{
		return Left.StableRoomId < Right.StableRoomId;
	});

	TMap<int64, int32> RoomPlanIndexById;
	OutPlan.Rooms.Reserve(OrderedRooms.Num());
	for (const FEFCalystoRoomContextV6* Room : OrderedRooms)
	{
		FEFCalystoRoomPopulationPlanV6& RoomPlan = OutPlan.Rooms.AddDefaulted_GetRef();
		RoomPlan.StableRoomId = Room->StableRoomId;
		RoomPlan.ThemeId = Room->ThemeId;
		RoomPlan.EffectiveCatalogHash = Room->CatalogHash;
		RoomPlanIndexById.Add(Room->StableRoomId, OutPlan.Rooms.Num() - 1);
	}

	TArray<FActorProposal> Proposals;
	TMap<FString, TCompiledAliasTable<FEFCalystoCatalogEntryV6>> ActorSelectionTables;
	TMap<FString, TCompiledAliasTable<FEFCalystoChestContentEntryV6>> ContentSelectionTables;
	for (const FEFCalystoRoomContextV6* Room : OrderedRooms)
	{
		if (FEFCalystoDungeonDirectorMathV6::IsProtectedRoom(Room->RoomFlags))
		{
			continue;
		}
		const TArray<FEFCalystoCatalogOverlayV6>* FrozenCatalogs = nullptr;
		if (!GetFrozenRoomCatalogs(FloorPlan, *Room, FrozenCatalogs, OutError))
		{
			return false;
		}
		TArray<const FEFCalystoCatalogOverlayV6*> OrderedCategories;
		OrderedCategories.Reserve(FrozenCatalogs->Num());
		for (const FEFCalystoCatalogOverlayV6& Category : *FrozenCatalogs)
		{
			OrderedCategories.Add(&Category);
		}
		OrderedCategories.Sort([](const FEFCalystoCatalogOverlayV6& Left, const FEFCalystoCatalogOverlayV6& Right)
		{
			return CanonicalToken(Left.CategoryId) < CanonicalToken(Right.CategoryId);
		});

		for (const FEFCalystoCatalogOverlayV6* Category : OrderedCategories)
		{
			if (!Category || Category->Mode == EEFCalystoCatalogOverlayModeV6::Block ||
				Category->CategoryId.IsNone() ||
				Category->CategoryId.IsEqual(FName(TEXT("ChestContents")), ENameCase::IgnoreCase) ||
				Category->Catalog.IsEmpty() ||
				StyleCategoryLimit(FloorPlan, Category->CategoryId) <= 0 ||
				Category->Limits.MaximumPerFloor <= 0)
			{
				continue;
			}
			const double PresenceRoll = UniformFromHash(LaneHash(
				TEXT("CategoryPresence"), FloorPlan.FloorSeed, Room->StableRoomId,
				Category->CategoryId, 0));
			if (PresenceRoll >= ResolveChance(Category->Presence, Options.FloorNumber))
			{
				continue;
			}
			const int32 AttemptCount = FMath::Clamp(
				FMath::Max(1, Category->Limits.MinimumWhenPresent),
				1, Category->Limits.MaximumPerFloor);
			for (int32 Attempt = 0; Attempt < AttemptCount; ++Attempt)
			{
				EEFCalystoRarityTierV6 SelectedTier = EEFCalystoRarityTierV6::Common;
				if (!SelectTier(*Category, Options.FloorNumber,
					UniformFromHash(LaneHash(TEXT("Tier"), FloorPlan.FloorSeed,
						Room->StableRoomId, Category->CategoryId, Attempt)), SelectedTier))
				{
					continue;
				}
				const FString TableKey = SelectionTableKey(
					Room->CatalogHash, Category->CategoryId, SelectedTier);
				TCompiledAliasTable<FEFCalystoCatalogEntryV6>* SelectionTable =
					ActorSelectionTables.Find(TableKey);
				if (!SelectionTable)
				{
					SelectionTable = &ActorSelectionTables.Add(TableKey);
					if (!SelectionTable->Compile(
						Category->Catalog,
						[&](const FEFCalystoCatalogEntryV6& Candidate)
						{
							return Candidate.Rule == EEFCalystoCatalogEntryRuleV6::Allow &&
								Candidate.Tier == SelectedTier &&
								Candidate.FirstEligibleFloor <= Options.FloorNumber &&
								!Options.CoolingDownActorEntryIds.Contains(Candidate.StableId) &&
								Candidate.ActorClass.ToSoftObjectPath().IsValid();
						},
						[](const FEFCalystoCatalogEntryV6& Candidate)
						{
							return static_cast<double>(Candidate.SelectionWeight);
						}, OutError))
					{
						return false;
					}
					++OutPlan.CompiledActorSelectionTableCount;
				}
				if (SelectionTable->Entries.IsEmpty())
				{
					continue;
				}
				++OutPlan.ActorSelectionDrawCount;
				const FEFCalystoCatalogEntryV6* Entry = SelectionTable->Select(
					UniformFromHash(LaneHash(TEXT("ActorEntryColumn"), FloorPlan.FloorSeed,
						Room->StableRoomId, Category->CategoryId, Attempt)),
					UniformFromHash(LaneHash(TEXT("ActorEntryCoin"), FloorPlan.FloorSeed,
						Room->StableRoomId, Category->CategoryId, Attempt)));
				if (!Entry)
				{
					continue;
				}
				FActorProposal& Proposal = Proposals.AddDefaulted_GetRef();
				Proposal.StableRoomId = Room->StableRoomId;
				Proposal.ThemeId = Room->ThemeId;
				Proposal.CategoryId = Category->CategoryId;
				Proposal.EntryId = Entry->StableId;
				Proposal.ClassPath = Entry->ActorClass.ToSoftObjectPath();
				Proposal.SpawnOrdinal = Attempt;
				Proposal.PlacementZone = Entry->PlacementZone;
				Proposal.PositionJitterCm = Entry->PositionJitterCm;
				Proposal.EffectiveCategoryLimit = Category->Limits.MaximumPerFloor;
				Proposal.MaximumPerVariant = Entry->MaximumPerVariant;
				Proposal.Tier = Entry->Tier;
				Proposal.Lifecycle = Entry->Lifecycle;
				Proposal.ThreatCost = FMath::Max(0.0f, Entry->BaseThreatCost);
				Proposal.BudgetPriority = LaneHash(TEXT("BudgetPriority"), FloorPlan.FloorSeed,
					Room->StableRoomId, Category->CategoryId, Attempt,
					Entry->StableId.ToString());
			}
		}
	}
	Proposals.Sort(ProposalLess);

	TMap<FString, int32> GlobalCategoryCounts;
	TMap<FString, FName> CategoryNames;
	TMap<FString, int32> EffectiveCategoryCounts;
	TMap<FString, int32> ActorEntryCounts;
	TMap<FString, int32> OutputEntryCounts;
	TMap<FString, FName> EntryNames;
	int32 BucketCounts[6] = {};
	const double EnemyThreatLimit = FMath::Max(0.0, FMath::Lerp(
		static_cast<double>(FloorPlan.Threat.BudgetAtFloor1),
		static_cast<double>(FloorPlan.Threat.BudgetAtFloor100),
		Progression(Options.FloorNumber, FloorPlan.Threat.Tau)));

	TArray<FAcceptedChest> AcceptedChests;
	for (const FActorProposal& Proposal : Proposals)
	{
		if (OutPlan.ActorDecisionCount >= FMath::Max(0, FloorPlan.GlobalBudgets.MaximumDirectorActors))
		{
			break;
		}
		const FString CategoryKey = CanonicalToken(Proposal.CategoryId);
		if (GlobalCategoryCounts.FindRef(CategoryKey) >= StyleCategoryLimit(FloorPlan, Proposal.CategoryId))
		{
			continue;
		}
		const FString EffectiveKey = GroupKey(Proposal.ThemeId, Proposal.CategoryId);
		if (EffectiveCategoryCounts.FindRef(EffectiveKey) >= Proposal.EffectiveCategoryLimit)
		{
			continue;
		}
		const FString EntryKey = CanonicalToken(Proposal.EntryId);
		if (ActorEntryCounts.FindRef(EntryKey) >= FMath::Max(0, Proposal.MaximumPerVariant))
		{
			continue;
		}
		const EBudgetBucket Bucket = BudgetBucket(Proposal.CategoryId);
		const int32 BucketIndex = static_cast<int32>(Bucket);
		if (BucketCounts[BucketIndex] >= BucketLimit(FloorPlan.GlobalBudgets, Bucket))
		{
			continue;
		}
		if (Bucket == EBudgetBucket::Enemy &&
			OutPlan.TotalEnemyThreat + Proposal.ThreatCost > EnemyThreatLimit + UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const int32* RoomPlanIndex = RoomPlanIndexById.Find(Proposal.StableRoomId);
		if (!RoomPlanIndex || !OutPlan.Rooms.IsValidIndex(*RoomPlanIndex))
		{
			return Fail(OutError, TEXT("V6 population lost a canonical room assignment."));
		}
		FEFCalystoPopulationDecisionV6 Decision;
		Decision.Kind = EEFCalystoPopulationDecisionKindV6::Actor;
		Decision.StableRoomId = Proposal.StableRoomId;
		Decision.StyleId = FloorPlan.StyleId;
		Decision.ThemeId = Proposal.ThemeId;
		Decision.CategoryId = Proposal.CategoryId;
		Decision.EntryId = Proposal.EntryId;
		Decision.ClassPath = Proposal.ClassPath;
		Decision.SpawnOrdinal = Proposal.SpawnOrdinal;
		Decision.PlacementZone = Proposal.PlacementZone;
		Decision.PositionJitterCm = Proposal.PositionJitterCm;
		Decision.Tier = Proposal.Tier;
		Decision.Lifecycle = Proposal.Lifecycle;
		Decision.ThreatCost = Proposal.ThreatCost;
		Decision.DecisionId = MakeDecisionId(FloorPlan, RoomManifest, Decision);
		if (!IsSha256(Decision.DecisionId))
		{
			return Fail(OutError, TEXT("V6 population could not hash an actor decision."));
		}
		OutPlan.Rooms[*RoomPlanIndex].Decisions.Add(Decision);
		++OutPlan.ActorDecisionCount;
		++GlobalCategoryCounts.FindOrAdd(CategoryKey);
		CategoryNames.FindOrAdd(CategoryKey, Proposal.CategoryId);
		++EffectiveCategoryCounts.FindOrAdd(EffectiveKey);
		++ActorEntryCounts.FindOrAdd(EntryKey);
		IncrementNamedCount(OutputEntryCounts, EntryNames, Proposal.EntryId);
		++BucketCounts[BucketIndex];
		switch (Bucket)
		{
		case EBudgetBucket::Enemy:
			++OutPlan.EnemyCount;
			OutPlan.TotalEnemyThreat += Proposal.ThreatCost;
			break;
		case EBudgetBucket::LooseFood: ++OutPlan.LooseFoodCount; break;
		case EBudgetBucket::Chest:
		{
			++OutPlan.ChestCount;
			FAcceptedChest& AcceptedChest = AcceptedChests.AddDefaulted_GetRef();
			AcceptedChest.RoomPlanIndex = *RoomPlanIndex;
			AcceptedChest.Parent = Decision;
			break;
		}
		case EBudgetBucket::LootActor: ++OutPlan.LootActorCount; break;
		case EBudgetBucket::SpecialEvent: ++OutPlan.SpecialEventCount; break;
		default: break;
		}
	}

	AcceptedChests.Sort([](const FAcceptedChest& Left, const FAcceptedChest& Right)
	{
		return Left.Parent.DecisionId < Right.Parent.DecisionId;
	});
	TMap<FString, int32> ContentEntryCounts;
	for (const FAcceptedChest& Chest : AcceptedChests)
	{
		if (!OutPlan.Rooms.IsValidIndex(Chest.RoomPlanIndex))
		{
			return Fail(OutError, TEXT("V6 population lost a chest room assignment."));
		}
		const FEFCalystoRoomContextV6* const* SourceRoomValue =
			SourceRoomById.Find(Chest.Parent.StableRoomId);
		const FEFCalystoRoomContextV6* SourceRoom = SourceRoomValue ? *SourceRoomValue : nullptr;
		if (!SourceRoom)
		{
			return Fail(OutError, TEXT("V6 population could not recover a chest Room Context."));
		}
		const TArray<FEFCalystoCatalogOverlayV6>* FrozenCatalogs = nullptr;
		if (!GetFrozenRoomCatalogs(FloorPlan, *SourceRoom, FrozenCatalogs, OutError))
		{
			return false;
		}
		const FEFCalystoCatalogOverlayV6* ContentCategory = FindCategory(*FrozenCatalogs, TEXT("ChestContents"));
		if (!ContentCategory || ContentCategory->Mode == EEFCalystoCatalogOverlayModeV6::Block ||
			ContentCategory->ChestContentsCatalog.IsEmpty() ||
			ContentCategory->Limits.MaximumPerFloor <= 0 ||
			StyleCategoryLimit(FloorPlan, ContentCategory->CategoryId) <= 0)
		{
			continue;
		}
		const FString CategoryKey = CanonicalToken(ContentCategory->CategoryId);
		const FString EffectiveKey = GroupKey(SourceRoom->ThemeId, ContentCategory->CategoryId);
		if (GlobalCategoryCounts.FindRef(CategoryKey) >= StyleCategoryLimit(FloorPlan, ContentCategory->CategoryId) ||
			EffectiveCategoryCounts.FindRef(EffectiveKey) >= ContentCategory->Limits.MaximumPerFloor)
		{
			continue;
		}
		const int32 DrawIndex = Chest.Parent.SpawnOrdinal;
		if (UniformFromHash(LaneHash(TEXT("ChestContentPresence"), FloorPlan.FloorSeed,
			SourceRoom->StableRoomId, ContentCategory->CategoryId, DrawIndex,
			Chest.Parent.DecisionId)) >= ResolveChance(ContentCategory->Presence, Options.FloorNumber))
		{
			continue;
		}
		EEFCalystoRarityTierV6 SelectedTier = EEFCalystoRarityTierV6::Common;
		if (!SelectTier(*ContentCategory, Options.FloorNumber,
			UniformFromHash(LaneHash(TEXT("ChestContentTier"), FloorPlan.FloorSeed,
				SourceRoom->StableRoomId, ContentCategory->CategoryId, DrawIndex,
				Chest.Parent.DecisionId)), SelectedTier))
		{
			continue;
		}
		const FString TableKey = SelectionTableKey(
			SourceRoom->CatalogHash, ContentCategory->CategoryId, SelectedTier);
		TCompiledAliasTable<FEFCalystoChestContentEntryV6>* SelectionTable =
			ContentSelectionTables.Find(TableKey);
		if (!SelectionTable)
		{
			SelectionTable = &ContentSelectionTables.Add(TableKey);
			if (!SelectionTable->Compile(
				ContentCategory->ChestContentsCatalog,
				[&](const FEFCalystoChestContentEntryV6& Candidate)
				{
					return Candidate.Tier == SelectedTier &&
						Candidate.FirstEligibleFloor <= Options.FloorNumber &&
						(!Candidate.bRequiresGraveyardEligibility || Options.bGraveyardEligible) &&
						!Options.CoolingDownContentEntryIds.Contains(Candidate.StableId) &&
						Candidate.ContentClass.ToSoftObjectPath().IsValid();
				},
				[](const FEFCalystoChestContentEntryV6& Candidate)
				{
					return static_cast<double>(Candidate.SelectionWeight);
				}, OutError))
			{
				return false;
			}
			++OutPlan.CompiledContentSelectionTableCount;
		}
		if (SelectionTable->Entries.IsEmpty())
		{
			continue;
		}
		++OutPlan.ContentSelectionDrawCount;
		const FEFCalystoChestContentEntryV6* Entry = SelectionTable->Select(
			UniformFromHash(LaneHash(TEXT("ChestContentEntryColumn"), FloorPlan.FloorSeed,
				SourceRoom->StableRoomId, ContentCategory->CategoryId, DrawIndex,
				Chest.Parent.DecisionId)),
			UniformFromHash(LaneHash(TEXT("ChestContentEntryCoin"), FloorPlan.FloorSeed,
				SourceRoom->StableRoomId, ContentCategory->CategoryId, DrawIndex,
				Chest.Parent.DecisionId)));
		if (!Entry)
		{
			continue;
		}
		// Per-entry caps are intentionally arbitrated after the immutable alias draw.
		// A capped result fails closed; the weighted table is never rebuilt or rescanned.
		if (ContentEntryCounts.FindRef(CanonicalToken(Entry->StableId)) >= Entry->MaximumPerFloor)
		{
			continue;
		}
		FEFCalystoPopulationDecisionV6 Decision;
		Decision.Kind = EEFCalystoPopulationDecisionKindV6::ChestContent;
		Decision.StableRoomId = SourceRoom->StableRoomId;
		Decision.StyleId = FloorPlan.StyleId;
		Decision.ThemeId = SourceRoom->ThemeId;
		Decision.CategoryId = ContentCategory->CategoryId;
		Decision.EntryId = Entry->StableId;
		Decision.ClassPath = Entry->ContentClass.ToSoftObjectPath();
		Decision.SpawnOrdinal = DrawIndex;
		Decision.Tier = Entry->Tier;
		Decision.ParentDecisionId = Chest.Parent.DecisionId;
		Decision.DecisionId = MakeDecisionId(FloorPlan, RoomManifest, Decision);
		if (!IsSha256(Decision.DecisionId))
		{
			return Fail(OutError, TEXT("V6 population could not hash a chest-content decision."));
		}
		OutPlan.Rooms[Chest.RoomPlanIndex].Decisions.Add(Decision);
		++OutPlan.ChestContentDecisionCount;
		++GlobalCategoryCounts.FindOrAdd(CategoryKey);
		CategoryNames.FindOrAdd(CategoryKey, ContentCategory->CategoryId);
		++EffectiveCategoryCounts.FindOrAdd(EffectiveKey);
		++ContentEntryCounts.FindOrAdd(CanonicalToken(Entry->StableId));
		IncrementNamedCount(OutputEntryCounts, EntryNames, Entry->StableId);
	}

	FString Canonical = FString::Printf(
		TEXT("EFCalystoPopulationPlanV6|%s|%s|%s|%s|"),
		*FloorPlan.FloorPlanHash, *RoomManifest.FloorPlanHash,
		*RoomManifest.ManifestHash,
		*BuildOptionsToken(Options));
	for (FEFCalystoRoomPopulationPlanV6& Room : OutPlan.Rooms)
	{
		Room.Decisions.Sort(DecisionLess);
		FString RoomCanonical = FString::Printf(TEXT("R:%lld|%s|%s|"),
			Room.StableRoomId, *CanonicalToken(Room.ThemeId), *Room.EffectiveCatalogHash);
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			RoomCanonical += FString::Printf(TEXT("D:%s,%d,%s,%s,%d,%d,%s,%s,%s|"),
				*Decision.DecisionId, static_cast<int32>(Decision.Kind),
				*CanonicalToken(Decision.CategoryId), *CanonicalToken(Decision.EntryId),
				Decision.SpawnOrdinal, static_cast<int32>(Decision.PlacementZone),
				*FloatBits(Decision.PositionJitterCm), *FloatBits(Decision.ThreatCost),
				*CanonicalToken(Decision.ClassPath.ToString()));
		}
		Room.RoomPopulationHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(RoomCanonical);
		if (!IsSha256(Room.RoomPopulationHash))
		{
			return Fail(OutError, TEXT("V6 population could not hash a room population plan."));
		}
		Canonical += Room.RoomPopulationHash + TEXT("|");
	}
	ExportCounts(GlobalCategoryCounts, CategoryNames, OutPlan.CategoryCounts);
	ExportCounts(OutputEntryCounts, EntryNames, OutPlan.EntryCounts);
	GatherPreloadClassPaths(OutPlan, OutPlan.PreloadClassPaths);
	Canonical += FString::Printf(TEXT("C:%d,%d,%d,%d,%d,%d,%d,%s|"),
		OutPlan.ActorDecisionCount, OutPlan.ChestContentDecisionCount,
		OutPlan.EnemyCount, OutPlan.LooseFoodCount, OutPlan.ChestCount,
		OutPlan.LootActorCount, OutPlan.SpecialEventCount,
		*FloatBits(OutPlan.TotalEnemyThreat));
	for (const FSoftObjectPath& Path : OutPlan.PreloadClassPaths)
	{
		Canonical += TEXT("P:") + CanonicalToken(Path.ToString()) + TEXT("|");
	}
	OutPlan.PopulationHash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
	if (!IsSha256(OutPlan.PopulationHash))
	{
		return Fail(OutError, TEXT("V6 population could not hash the floor population plan."));
	}
	return true;
}

void FEFCalystoPopulationPlannerV6::GatherPreloadClassPaths(
	const FEFCalystoPopulationPlanV6& Plan,
	TArray<FSoftObjectPath>& OutPaths)
{
	TSet<FSoftObjectPath> UniquePaths;
	for (const FEFCalystoRoomPopulationPlanV6& Room : Plan.Rooms)
	{
		for (const FEFCalystoPopulationDecisionV6& Decision : Room.Decisions)
		{
			if (Decision.ClassPath.IsValid())
			{
				UniquePaths.Add(Decision.ClassPath);
			}
		}
	}
	OutPaths = UniquePaths.Array();
	OutPaths.Sort([](const FSoftObjectPath& Left, const FSoftObjectPath& Right)
	{
		return EFCalystoPopulationV6Private::CanonicalToken(Left.ToString()) <
			EFCalystoPopulationV6Private::CanonicalToken(Right.ToString());
	});
}
