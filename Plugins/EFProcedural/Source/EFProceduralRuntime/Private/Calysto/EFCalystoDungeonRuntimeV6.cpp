#include "Calysto/EFCalystoDungeonRuntimeV6.h"

#include "Algo/Sort.h"
#include "GameFramework/Actor.h"

namespace EFCalystoDungeonRuntimeV6Private
{
bool Fail(FString& OutError, const FString& Message)
{
	OutError = Message;
	return false;
}

bool IsFiniteRange(const float Value, const float Minimum, const float Maximum)
{
	return FMath::IsFinite(Value) && Value >= Minimum && Value <= Maximum;
}

FString NameToken(const FName Name)
{
	return Name.IsNone() ? TEXT("<none>") : Name.ToString().ToLower();
}

FString PathToken(const TSoftClassPtr<AActor>& Class)
{
	return Class.IsNull() ? TEXT("<null>") : Class.ToSoftObjectPath().ToString().ToLower();
}

FString GuidToken(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString FloatBits(const float Value)
{
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return FString::Printf(TEXT("%08x"), Bits);
}

FString DoubleBits(const double Value)
{
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Bits));
}

void AppendToken(FString& Canonical, const TCHAR* Label, const FString& Value)
{
	Canonical += Label;
	Canonical += TEXT(":");
	Canonical += FString::FromInt(Value.Len());
	Canonical += TEXT(":");
	Canonical += Value;
	Canonical += TEXT("|");
}

void AppendInt(FString& Canonical, const TCHAR* Label, const int64 Value)
{
	AppendToken(Canonical, Label, FString::Printf(TEXT("%lld"), Value));
}

void AppendFloat(FString& Canonical, const TCHAR* Label, const float Value)
{
	AppendToken(Canonical, Label, FloatBits(Value));
}

void AppendBool(FString& Canonical, const TCHAR* Label, const bool bValue)
{
	AppendToken(Canonical, Label, bValue ? TEXT("1") : TEXT("0"));
}

void AppendTransform(FString& Canonical, const FTransform& Transform)
{
	const FVector Translation = Transform.GetTranslation();
	const FQuat Rotation = Transform.GetRotation();
	const FVector Scale = Transform.GetScale3D();
	AppendToken(Canonical, TEXT("TX"), DoubleBits(Translation.X));
	AppendToken(Canonical, TEXT("TY"), DoubleBits(Translation.Y));
	AppendToken(Canonical, TEXT("TZ"), DoubleBits(Translation.Z));
	AppendToken(Canonical, TEXT("RX"), DoubleBits(Rotation.X));
	AppendToken(Canonical, TEXT("RY"), DoubleBits(Rotation.Y));
	AppendToken(Canonical, TEXT("RZ"), DoubleBits(Rotation.Z));
	AppendToken(Canonical, TEXT("RW"), DoubleBits(Rotation.W));
	AppendToken(Canonical, TEXT("SX"), DoubleBits(Scale.X));
	AppendToken(Canonical, TEXT("SY"), DoubleBits(Scale.Y));
	AppendToken(Canonical, TEXT("SZ"), DoubleBits(Scale.Z));
}

bool ValidatePertRange(
	const FEFCalystoPertRangeV6& Range,
	const float HardMinimum,
	const float HardMaximum,
	const TCHAR* Label,
	FString& OutError)
{
	if (!FMath::IsFinite(Range.Minimum) || !FMath::IsFinite(Range.Mode) ||
		!FMath::IsFinite(Range.Maximum) || !FMath::IsFinite(Range.Shape) ||
		Range.Minimum < HardMinimum || Range.Maximum > HardMaximum ||
		Range.Minimum > Range.Mode || Range.Mode > Range.Maximum || Range.Shape <= 0.0f)
	{
		return Fail(OutError, FString::Printf(
			TEXT("%s must be finite, ordered, and remain inside the certified V6 bounds."), Label));
	}
	return true;
}

bool ValidateFloorPlan(const FEFCalystoResolvedFloorPlanV6& Plan, FString& OutError)
{
	if (Plan.StyleId.IsNone() ||
		Plan.StyleId.IsEqual(UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("A V6 floor plan must contain exactly one valid Style ID."));
	}
	if (!IsFiniteRange(Plan.ThemeRoomChance, 0.0f, 1.0f) ||
		!FMath::IsFinite(Plan.RoomIdentityQuantizationCm) || Plan.RoomIdentityQuantizationCm <= 0.0f ||
		Plan.MaximumRoomRecords < 1 || Plan.MaximumRoomRecords > 10000 ||
		Plan.DecalComponentPoolCapacity < 1 || Plan.DecalComponentPoolCapacity > 24)
	{
		return Fail(OutError, TEXT("The V6 floor plan contains an invalid probability or safety ceiling."));
	}
	if (!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Plan.PolicyHash) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Plan.FloorPlanHash) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Plan.StyleCatalogHash) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Plan.StyleMaterialHash) ||
		!FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(Plan.StyleDecalHash))
	{
		return Fail(OutError, TEXT("The V6 floor plan requires canonical policy, plan, catalog, material, and decal hashes."));
	}
	if (!ValidatePertRange(Plan.Layout.DungeonSize,
			static_cast<float>(EFCalystoDungeonRuntimeSchemaV6::MinimumDungeonEdge),
			static_cast<float>(EFCalystoDungeonRuntimeSchemaV6::MaximumDungeonEdge),
			TEXT("Dungeon Size"), OutError) ||
		!ValidatePertRange(Plan.Layout.CandidateDensity,
			EFCalystoDungeonRuntimeSchemaV6::MinimumCandidateDensity,
			EFCalystoDungeonRuntimeSchemaV6::MaximumCandidateDensity,
			TEXT("Candidate Density"), OutError) ||
		!ValidatePertRange(Plan.Layout.SidePathChance,
			EFCalystoDungeonRuntimeSchemaV6::MinimumSidePathChance,
			EFCalystoDungeonRuntimeSchemaV6::MaximumSidePathChance,
			TEXT("Side Path Chance"), OutError))
	{
		return false;
	}
	if (Plan.Layout.MinimumRoomSize < 4 || Plan.Layout.MaximumRoomSize > 8 ||
		Plan.Layout.MinimumRoomSize > Plan.Layout.MaximumRoomSize)
	{
		return Fail(OutError, TEXT("V6 room-size bounds must be ordered inside [4, 8]."));
	}
	if (Plan.Themes.Num() != Plan.ThemeAliasProbability.Num() ||
		Plan.Themes.Num() != Plan.ThemeAliasIndex.Num() ||
		(Plan.ThemeRoomChance > 0.0f && Plan.Themes.IsEmpty()))
	{
		return Fail(OutError, TEXT("V6 reachable Theme profiles and their frozen alias table must have equal nonzero cardinality."));
	}
	for (int32 Index = 0; Index < Plan.Themes.Num(); ++Index)
	{
		if (Plan.Themes[Index].ThemeId.IsNone() ||
			Plan.Themes[Index].ThemeId.IsEqual(UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase) ||
			!IsFiniteRange(Plan.ThemeAliasProbability[Index], 0.0f, 1.0f) ||
			!Plan.Themes.IsValidIndex(Plan.ThemeAliasIndex[Index]))
		{
			return Fail(OutError, TEXT("V6 reachable Theme metadata contains an invalid stable ID or alias entry."));
		}
	}
	return true;
}

uint64 HashLane64(const FString& ImmutableIdentity, const TCHAR* Lane)
{
	const FString Hash = FEFCalystoDungeonDirectorMathV6::HashCanonicalText(
		FString::Printf(TEXT("EFCalystoRuntimeLaneV6|%s|%s"), Lane, *ImmutableIdentity));
	return FCString::Strtoui64(*Hash.Left(16), nullptr, 16);
}

double UniformLane(const FString& ImmutableIdentity, const TCHAR* Lane)
{
	constexpr double Denominator = 9007199254740992.0; // 2^53
	return static_cast<double>(HashLane64(ImmutableIdentity, Lane) >> 11) / Denominator;
}

float SamplePertLike(const FEFCalystoPertRangeV6& Range, const double Uniform)
{
	if (Range.Maximum <= Range.Minimum)
	{
		return Range.Minimum;
	}
	const double Minimum = Range.Minimum;
	const double Mode = Range.Mode;
	const double Maximum = Range.Maximum;
	const double Span = Maximum - Minimum;
	const double Split = (Mode - Minimum) / Span;
	const double Triangular = Uniform < Split
		? Minimum + FMath::Sqrt(Uniform * Span * (Mode - Minimum))
		: Maximum - FMath::Sqrt((1.0 - Uniform) * Span * (Maximum - Mode));
	const double Concentration = FMath::Clamp(
		static_cast<double>(Range.Shape) / (static_cast<double>(Range.Shape) + 8.0), 0.0, 0.90);
	return static_cast<float>(FMath::Clamp(
		FMath::Lerp(Triangular, Mode, Concentration * 0.35), Minimum, Maximum));
}

FString BuildSamplingIdentity(
	const FEFCalystoDungeonGenerationContextV6& Context,
	const FEFCalystoResolvedFloorPlanV6& Plan)
{
	// Topology and its PCG seed intentionally exclude the aggregate policy and
	// floor-plan hashes. Those hashes include Theme weights, materials, catalogs,
	// and decals; letting them enter this lane would allow an authoring-only Theme
	// weight change to rebuild the room topology and therefore change which rooms
	// passed the independent Theme-presence roll.
	FString Canonical(TEXT("EFCalystoFloorTopologySamplingIdentityV6|"));
	AppendInt(Canonical, TEXT("RunSeed"), Context.RunSeed);
	AppendInt(Canonical, TEXT("Floor"), Context.FloorNumber);
	AppendInt(Canonical, TEXT("Serial"), Context.GenerationSerial);
	AppendInt(Canonical, TEXT("FloorSeed"), Plan.FloorSeed);
	AppendToken(Canonical, TEXT("Style"), NameToken(Plan.StyleId));
	AppendFloat(Canonical, TEXT("DungeonSizeMinimum"), Plan.Layout.DungeonSize.Minimum);
	AppendFloat(Canonical, TEXT("DungeonSizeMode"), Plan.Layout.DungeonSize.Mode);
	AppendFloat(Canonical, TEXT("DungeonSizeMaximum"), Plan.Layout.DungeonSize.Maximum);
	AppendFloat(Canonical, TEXT("DungeonSizeShape"), Plan.Layout.DungeonSize.Shape);
	AppendFloat(Canonical, TEXT("CandidateDensityMinimum"), Plan.Layout.CandidateDensity.Minimum);
	AppendFloat(Canonical, TEXT("CandidateDensityMode"), Plan.Layout.CandidateDensity.Mode);
	AppendFloat(Canonical, TEXT("CandidateDensityMaximum"), Plan.Layout.CandidateDensity.Maximum);
	AppendFloat(Canonical, TEXT("CandidateDensityShape"), Plan.Layout.CandidateDensity.Shape);
	AppendFloat(Canonical, TEXT("SidePathChanceMinimum"), Plan.Layout.SidePathChance.Minimum);
	AppendFloat(Canonical, TEXT("SidePathChanceMode"), Plan.Layout.SidePathChance.Mode);
	AppendFloat(Canonical, TEXT("SidePathChanceMaximum"), Plan.Layout.SidePathChance.Maximum);
	AppendFloat(Canonical, TEXT("SidePathChanceShape"), Plan.Layout.SidePathChance.Shape);
	AppendInt(Canonical, TEXT("MinimumRoomSize"), Plan.Layout.MinimumRoomSize);
	AppendInt(Canonical, TEXT("MaximumRoomSize"), Plan.Layout.MaximumRoomSize);
	AppendInt(Canonical, TEXT("ForcedEdge"), Context.DevelopmentForcedDungeonEdge);
	AppendToken(Canonical, TEXT("PopulationScenario"), NameToken(Context.DevelopmentPopulationScenario));
	return Canonical;
}

int32 GradeLevelOffset(const EEFCalystoRarityTierV6 Grade)
{
	switch (Grade)
	{
	case EEFCalystoRarityTierV6::Uncommon: return 2;
	case EEFCalystoRarityTierV6::Rare: return 5;
	case EEFCalystoRarityTierV6::Epic: return 9;
	case EEFCalystoRarityTierV6::Winter: return 15;
	default: return 0;
	}
}

bool CompanionRecordLess(const FEFCalystoCompanionRecordV6& A, const FEFCalystoCompanionRecordV6& B)
{
	return GuidToken(A.StableCompanionId) < GuidToken(B.StableCompanionId);
}

bool RealizedActorLess(
	const FEFCalystoRealizedPopulationActorRecordV6& A,
	const FEFCalystoRealizedPopulationActorRecordV6& B)
{
	const int32 IdCompare = NameToken(A.StableActorId).Compare(NameToken(B.StableActorId));
	return IdCompare == 0 ? A.StableRoomId < B.StableRoomId : IdCompare < 0;
}
} // namespace EFCalystoDungeonRuntimeV6Private

bool FEFCalystoDungeonRuntimeMathV6::IsCanonicalHash(const FString& Hash)
{
	if (Hash.Len() != 64)
	{
		return false;
	}
	for (const TCHAR Character : Hash)
	{
		if (!FChar::IsHexDigit(Character))
		{
			return false;
		}
	}
	return true;
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeGenerationContextHash(
	const FEFCalystoDungeonGenerationContextV6& Context)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoDungeonGenerationContextV6|"));
	AppendInt(Canonical, TEXT("Schema"), Context.SchemaVersion);
	AppendInt(Canonical, TEXT("Generator"), Context.GeneratorVersion);
	AppendInt(Canonical, TEXT("RunSeed"), Context.RunSeed);
	AppendInt(Canonical, TEXT("Floor"), Context.FloorNumber);
	AppendInt(Canonical, TEXT("Serial"), Context.GenerationSerial);
	AppendToken(Canonical, TEXT("Policy"), Context.PolicyHash.ToLower());
	AppendInt(Canonical, TEXT("ForcedEdge"), Context.DevelopmentForcedDungeonEdge);
	AppendToken(Canonical, TEXT("PopulationScenario"), NameToken(Context.DevelopmentPopulationScenario));
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeDirectorIntentHash(
	const FEFCalystoDirectorIntentV6& Intent)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoDirectorIntentV6|"));
	AppendFloat(Canonical, TEXT("Scale"), Intent.Scale);
	AppendFloat(Canonical, TEXT("Branching"), Intent.Branching);
	AppendFloat(Canonical, TEXT("Danger"), Intent.Danger);
	AppendFloat(Canonical, TEXT("Safety"), Intent.Safety);
	AppendFloat(Canonical, TEXT("Abundance"), Intent.Abundance);
	AppendFloat(Canonical, TEXT("Mystery"), Intent.Mystery);
	AppendFloat(Canonical, TEXT("Clothing"), Intent.ClothingInfluence);
	AppendFloat(Canonical, TEXT("Volatility"), Intent.Volatility);
	AppendBool(Canonical, TEXT("HasPreferredStyle"), Intent.bHasPreferredStyle);
	AppendToken(Canonical, TEXT("PreferredStyle"),
		Intent.bHasPreferredStyle ? NameToken(Intent.PreferredStyleId) : TEXT("<none>"));
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeFloorOutcomeHash(
	const FEFCalystoFloorOutcomeV6& Outcome)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoFloorOutcomeV6|"));
	AppendFloat(Canonical, TEXT("Combat"), Outcome.Combat);
	AppendFloat(Canonical, TEXT("Survival"), Outcome.Survival);
	AppendFloat(Canonical, TEXT("Resources"), Outcome.Resources);
	AppendFloat(Canonical, TEXT("Pace"), Outcome.Pace);
	AppendFloat(Canonical, TEXT("DeathsAndFailures"), Outcome.DeathsAndFailures);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeCompanionRosterHash(
	const FEFCalystoCompanionRosterSnapshotV6& Snapshot)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoCompanionRosterSnapshotV6|"));
	AppendBool(Canonical, TEXT("Valid"), Snapshot.bIsValid);
	AppendBool(Canonical, TEXT("OwnsRecall"), Snapshot.bPlayerOwnsWintersRecall);
	TArray<FEFCalystoCompanionRecordV6> Records = Snapshot.Records;
	Records.Sort(CompanionRecordLess);
	for (const FEFCalystoCompanionRecordV6& Record : Records)
	{
		AppendToken(Canonical, TEXT("Companion"), GuidToken(Record.StableCompanionId));
		AppendToken(Canonical, TEXT("Spawn"), NameToken(Record.SourceSpawnId));
		AppendToken(Canonical, TEXT("Catalog"), NameToken(Record.SourceCatalogId));
		AppendToken(Canonical, TEXT("Variant"), NameToken(Record.SourceVariantId));
		AppendToken(Canonical, TEXT("Class"), PathToken(Record.ActorClass));
		AppendToken(Canonical, TEXT("Archetype"), NameToken(Record.Archetype));
		AppendInt(Canonical, TEXT("Gender"), static_cast<int32>(Record.Gender));
		AppendInt(Canonical, TEXT("Grade"), static_cast<int32>(Record.Grade));
		AppendInt(Canonical, TEXT("State"), static_cast<int32>(Record.State));
		AppendInt(Canonical, TEXT("DeathFloor"), Record.DeathFloor);
		AppendInt(Canonical, TEXT("DeathSerial"), Record.DeathGenerationSerial);
	}
	TArray<FString> ActiveIds;
	for (const FGuid& Id : Snapshot.ActiveParty)
	{
		ActiveIds.Add(GuidToken(Id));
	}
	ActiveIds.Sort();
	for (const FString& ActiveId : ActiveIds)
	{
		AppendToken(Canonical, TEXT("Active"), ActiveId);
	}
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeRunEcologyHash(
	const FEFCalystoRunEcologyStateV6& Ecology)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoRunEcologyStateV6|"));
	AppendBool(Canonical, TEXT("Initialized"), Ecology.bInitialized);
	AppendBool(Canonical, TEXT("Synthetic"), Ecology.bDevelopmentSyntheticHistory);
	AppendToken(Canonical, TEXT("DNA"), Ecology.RunDNAHash.ToLower());
	AppendFloat(Canonical, TEXT("Scale"), Ecology.Scale);
	AppendFloat(Canonical, TEXT("Branching"), Ecology.Branching);
	AppendFloat(Canonical, TEXT("Threat"), Ecology.Threat);
	AppendFloat(Canonical, TEXT("Abundance"), Ecology.Abundance);
	AppendFloat(Canonical, TEXT("Mystery"), Ecology.Mystery);
	AppendFloat(Canonical, TEXT("PerformanceEMA"), Ecology.PerformanceEMA);
	AppendInt(Canonical, TEXT("LastCommittedFloor"), Ecology.LastCommittedFloor);
	AppendInt(Canonical, TEXT("Revision"), Ecology.Revision);
	AppendInt(Canonical, TEXT("NoFood"), Ecology.ConsecutiveFloorsWithoutFood);
	AppendInt(Canonical, TEXT("NoChest"), Ecology.ConsecutiveFloorsWithoutChest);
	for (const FName StyleId : Ecology.RecentStyleIds)
	{
		AppendToken(Canonical, TEXT("RecentStyle"), NameToken(StyleId));
	}
	TArray<FEFCalystoCooldownStateV6> Cooldowns = Ecology.Cooldowns;
	Cooldowns.Sort([](const FEFCalystoCooldownStateV6& A, const FEFCalystoCooldownStateV6& B)
	{
		return NameToken(A.StableId) < NameToken(B.StableId);
	});
	for (const FEFCalystoCooldownStateV6& Cooldown : Cooldowns)
	{
		AppendToken(Canonical, TEXT("Cooldown"), NameToken(Cooldown.StableId));
		AppendInt(Canonical, TEXT("LastSelected"), Cooldown.LastSelectedFloor);
		AppendInt(Canonical, TEXT("Duration"), Cooldown.CooldownFloors);
	}
	AppendToken(Canonical, TEXT("Companions"), ComputeCompanionRosterHash(Ecology.CompanionRoster));
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeResolvedFloorIntentHash(
	const FEFCalystoResolvedFloorIntentV6& Intent)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoResolvedFloorIntentV6|"));
	AppendInt(Canonical, TEXT("Schema"), Intent.SchemaVersion);
	AppendInt(Canonical, TEXT("Generator"), Intent.GeneratorVersion);
	AppendToken(Canonical, TEXT("Context"), ComputeGenerationContextHash(Intent.GenerationContext));
	AppendToken(Canonical, TEXT("Director"), ComputeDirectorIntentHash(Intent.DirectorIntent));
	AppendToken(Canonical, TEXT("Outcome"), ComputeFloorOutcomeHash(Intent.FrozenOutcome));
	AppendToken(Canonical, TEXT("Companions"), ComputeCompanionRosterHash(Intent.CompanionRoster));
	AppendToken(Canonical, TEXT("Ecology"), Intent.EcologyHash.ToLower());
	AppendToken(Canonical, TEXT("FloorPlan"), Intent.FloorPlan.FloorPlanHash.ToLower());
	AppendToken(Canonical, TEXT("Style"), NameToken(Intent.StyleId));
	AppendInt(Canonical, TEXT("SizeX"), Intent.DungeonSize.X);
	AppendInt(Canonical, TEXT("SizeY"), Intent.DungeonSize.Y);
	AppendInt(Canonical, TEXT("SizeZ"), Intent.DungeonSize.Z);
	AppendFloat(Canonical, TEXT("CandidateDensity"), Intent.CandidateDensity);
	AppendFloat(Canonical, TEXT("SidePathChance"), Intent.SidePathChance);
	AppendInt(Canonical, TEXT("MinimumRoomSize"), Intent.MinimumRoomSize);
	AppendInt(Canonical, TEXT("MaximumRoomSize"), Intent.MaximumRoomSize);
	AppendInt(Canonical, TEXT("LightingMode"), static_cast<int32>(Intent.Lighting.Mode));
	AppendFloat(Canonical, TEXT("LightingDraw"), Intent.Lighting.IntensityDraw);
	AppendFloat(Canonical, TEXT("LightingIntensity"), Intent.Lighting.IntensityMultiplier);
	AppendFloat(Canonical, TEXT("WallLightHeight"), Intent.Lighting.WallLightHeightCm);
	AppendInt(Canonical, TEXT("WallLightDistance"), Intent.Lighting.WallLightTileDistance);
	AppendInt(Canonical, TEXT("PCGSeed"), Intent.PCGSeed);
	TArray<FEFCalystoResolvedCompanionLevelV6> Levels = Intent.ResolvedCompanionLevels;
	Levels.Sort([](const FEFCalystoResolvedCompanionLevelV6& A, const FEFCalystoResolvedCompanionLevelV6& B)
	{
		return GuidToken(A.StableCompanionId) < GuidToken(B.StableCompanionId);
	});
	for (const FEFCalystoResolvedCompanionLevelV6& Level : Levels)
	{
		AppendToken(Canonical, TEXT("CompanionLevel"), GuidToken(Level.StableCompanionId));
		AppendInt(Canonical, TEXT("Grade"), static_cast<int32>(Level.Grade));
		AppendInt(Canonical, TEXT("Logical"), Level.LogicalLevel);
		AppendInt(Canonical, TEXT("Physical"), Level.PhysicalACFLevel);
	}
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeRealizedFloorManifestHash(
	const FEFCalystoRealizedFloorManifestV6& Manifest)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoRealizedFloorManifestV6|"));
	AppendInt(Canonical, TEXT("Schema"), Manifest.SchemaVersion);
	AppendInt(Canonical, TEXT("Generator"), Manifest.GeneratorVersion);
	AppendInt(Canonical, TEXT("RunSeed"), Manifest.RunSeed);
	AppendInt(Canonical, TEXT("Floor"), Manifest.FloorNumber);
	AppendInt(Canonical, TEXT("Serial"), Manifest.GenerationSerial);
	AppendToken(Canonical, TEXT("Style"), NameToken(Manifest.StyleId));
	AppendToken(Canonical, TEXT("Intent"), Manifest.IntentHash.ToLower());
	AppendToken(Canonical, TEXT("FloorPlan"), Manifest.FloorPlanHash.ToLower());
	AppendToken(Canonical, TEXT("Rooms"), Manifest.RoomManifestHash.ToLower());
	AppendToken(Canonical, TEXT("Anchors"), Manifest.AnchorTopologyHash.ToLower());
	AppendToken(Canonical, TEXT("PopulationPlan"), Manifest.PopulationPlanHash.ToLower());
	AppendToken(Canonical, TEXT("Companions"), Manifest.CompanionSnapshotHash.ToLower());
	AppendInt(Canonical, TEXT("CandidateAnchors"), Manifest.CandidateAnchorCount);
	AppendInt(Canonical, TEXT("SpawnedActors"), Manifest.SpawnedActorCount);
	AppendFloat(Canonical, TEXT("Threat"), Manifest.RealizedThreatCost);
	AppendFloat(Canonical, TEXT("Resources"), Manifest.RealizedResourceCost);
	TArray<FEFCalystoRealizedPopulationActorRecordV6> Actors = Manifest.Actors;
	Actors.Sort(RealizedActorLess);
	for (const FEFCalystoRealizedPopulationActorRecordV6& Actor : Actors)
	{
		AppendToken(Canonical, TEXT("Actor"), NameToken(Actor.StableActorId));
		AppendInt(Canonical, TEXT("Room"), Actor.StableRoomId);
		AppendToken(Canonical, TEXT("Category"), NameToken(Actor.CategoryId));
		AppendToken(Canonical, TEXT("Entry"), NameToken(Actor.CatalogEntryId));
		AppendToken(Canonical, TEXT("Class"), PathToken(Actor.ActorClass));
		AppendTransform(Canonical, Actor.Transform);
		AppendInt(Canonical, TEXT("Tier"), static_cast<int32>(Actor.Tier));
		AppendInt(Canonical, TEXT("Lifecycle"), static_cast<int32>(Actor.Lifecycle));
		AppendInt(Canonical, TEXT("Level"), Actor.LogicalLevel);
		AppendFloat(Canonical, TEXT("ThreatCost"), Actor.ThreatCost);
		AppendFloat(Canonical, TEXT("ResourceCost"), Actor.ResourceCost);
		AppendInt(Canonical, TEXT("Cooldown"), Actor.CooldownFloors);
		AppendToken(Canonical, TEXT("Companion"), GuidToken(Actor.StableCompanionId));
		TArray<FString> ContentIds;
		for (const FName ContentId : Actor.VerifiedChestContentIds)
		{
			ContentIds.Add(NameToken(ContentId));
		}
		ContentIds.Sort();
		for (const FString& ContentId : ContentIds)
		{
			AppendToken(Canonical, TEXT("ChestContent"), ContentId);
		}
	}
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

FString FEFCalystoDungeonRuntimeMathV6::ComputeDungeonSnapshotHash(
	const FEFCalystoDungeonSnapshotV6& Snapshot)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	FString Canonical(TEXT("EFCalystoDungeonSnapshotV6|"));
	AppendInt(Canonical, TEXT("Schema"), Snapshot.SchemaVersion);
	AppendInt(Canonical, TEXT("Generator"), Snapshot.GeneratorVersion);
	AppendInt(Canonical, TEXT("State"), static_cast<int32>(Snapshot.State));
	AppendInt(Canonical, TEXT("Travel"), static_cast<int32>(Snapshot.TravelKind));
	AppendBool(Canonical, TEXT("Active"), Snapshot.bHasActiveRun);
	AppendBool(Canonical, TEXT("PolicyValid"), Snapshot.bPolicyValid);
	AppendToken(Canonical, TEXT("PolicyError"), Snapshot.PolicyError);
	AppendBool(Canonical, TEXT("QueuedIntent"), Snapshot.bHasQueuedDirectorIntent);
	AppendInt(Canonical, TEXT("RunSeed"), Snapshot.RunSeed);
	AppendInt(Canonical, TEXT("Floor"), Snapshot.FloorNumber);
	AppendInt(Canonical, TEXT("Serial"), Snapshot.GenerationSerial);
	AppendToken(Canonical, TEXT("Style"), NameToken(Snapshot.StyleId));
	AppendInt(Canonical, TEXT("SizeX"), Snapshot.DungeonSize.X);
	AppendInt(Canonical, TEXT("SizeY"), Snapshot.DungeonSize.Y);
	AppendInt(Canonical, TEXT("SizeZ"), Snapshot.DungeonSize.Z);
	AppendInt(Canonical, TEXT("PCGSeed"), Snapshot.PCGSeed);
	AppendToken(Canonical, TEXT("FloorPlan"), Snapshot.FloorPlanHash.ToLower());
	AppendToken(Canonical, TEXT("FloorIntent"), Snapshot.FloorIntentHash.ToLower());
	AppendToken(Canonical, TEXT("Rooms"), Snapshot.RoomManifestHash.ToLower());
	AppendToken(Canonical, TEXT("Population"), Snapshot.PopulationManifestHash.ToLower());
	AppendBool(Canonical, TEXT("PCG"), Snapshot.bPCGComplete);
	AppendBool(Canonical, TEXT("Navigation"), Snapshot.bNavigationPathReady);
	AppendBool(Canonical, TEXT("RoomManifest"), Snapshot.bRoomManifestReady);
	AppendBool(Canonical, TEXT("PopulationReady"), Snapshot.bPopulationReady);
	AppendBool(Canonical, TEXT("Visuals"), Snapshot.bVisualsReady);
	AppendBool(Canonical, TEXT("Door"), Snapshot.bDoorEnabled);
	AppendToken(Canonical, TEXT("Failure"), Snapshot.FailureReason);
	return FEFCalystoDungeonDirectorMathV6::HashCanonicalText(Canonical);
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateGenerationContext(
	const FEFCalystoDungeonGenerationContextV6& Context, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	if (Context.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion ||
		Context.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion ||
		Context.FloorNumber < 1 || Context.GenerationSerial < 0 || Context.RunEpoch < 0 ||
		!IsCanonicalHash(Context.PolicyHash))
	{
		return Fail(OutError, TEXT("Generation context requires schema/generator 6, valid nonnegative identity, and a canonical policy hash."));
	}
	if (Context.DevelopmentForcedDungeonEdge != 0 &&
		(Context.DevelopmentForcedDungeonEdge < EFCalystoDungeonRuntimeSchemaV6::MinimumDungeonEdge ||
			Context.DevelopmentForcedDungeonEdge > EFCalystoDungeonRuntimeSchemaV6::MaximumDungeonEdge))
	{
		return Fail(OutError, TEXT("Development Forced Dungeon Edge must be zero or inside [18, 30]."));
	}
	const FString Expected = ComputeGenerationContextHash(Context);
	if (!Context.ContextHash.IsEmpty() && !Context.ContextHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Generation context hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateDirectorIntent(
	const FEFCalystoDirectorIntentV6& Intent, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	const float Values[] = {Intent.Scale, Intent.Branching, Intent.Danger, Intent.Safety,
		Intent.Abundance, Intent.Mystery, Intent.ClothingInfluence, Intent.Volatility};
	for (const float Value : Values)
	{
		if (!IsFiniteRange(Value, -1.0f, 1.0f))
		{
			return Fail(OutError, TEXT("Every V6 Director Intent influence must be finite inside [-1, 1]."));
		}
	}
	if (Intent.bHasPreferredStyle != !Intent.PreferredStyleId.IsNone())
	{
		return Fail(OutError, TEXT("Preferred Style ID must be set if and only if Has Preferred Style is true."));
	}
	if (Intent.bHasPreferredStyle &&
		Intent.PreferredStyleId.IsEqual(UEFCalystoDungeonDirectorPolicyV6Asset::NoThemeId, ENameCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("NoTheme is a room result and cannot be used as a Style ID."));
	}
	const FString Expected = ComputeDirectorIntentHash(Intent);
	if (!Intent.IntentHash.IsEmpty() && !Intent.IntentHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Director Intent hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateFloorOutcome(
	const FEFCalystoFloorOutcomeV6& Outcome, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	const float Values[] = {Outcome.Combat, Outcome.Survival, Outcome.Resources,
		Outcome.Pace, Outcome.DeathsAndFailures};
	for (const float Value : Values)
	{
		if (!IsFiniteRange(Value, 0.0f, 1.0f))
		{
			return Fail(OutError, TEXT("Every V6 Floor Outcome value must be finite inside [0, 1]."));
		}
	}
	const FString Expected = ComputeFloorOutcomeHash(Outcome);
	if (!Outcome.OutcomeHash.IsEmpty() && !Outcome.OutcomeHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Floor Outcome hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateCompanionRoster(
	const FEFCalystoCompanionRosterSnapshotV6& Snapshot, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	if (!Snapshot.bIsValid || Snapshot.RunEpoch < 0)
	{
		return Fail(OutError, TEXT("The V6 Companion Roster must be explicitly valid and have a nonnegative Run Epoch."));
	}
	TSet<FGuid> RecordIds;
	for (const FEFCalystoCompanionRecordV6& Record : Snapshot.Records)
	{
		if (!Record.StableCompanionId.IsValid() || Record.SourceCatalogId.IsNone() ||
			Record.SourceVariantId.IsNone() || Record.ActorClass.IsNull() ||
			Record.DeathFloor < 0 || Record.DeathGenerationSerial < 0 ||
			RecordIds.Contains(Record.StableCompanionId))
		{
			return Fail(OutError, TEXT("Companion records require unique GUIDs, stable source IDs, a soft class path, and valid death identity."));
		}
		RecordIds.Add(Record.StableCompanionId);
	}
	TSet<FGuid> ActiveIds;
	for (const FGuid& Id : Snapshot.ActiveParty)
	{
		const FEFCalystoCompanionRecordV6* Record = Snapshot.Records.FindByPredicate(
			[&Id](const FEFCalystoCompanionRecordV6& Candidate)
			{
				return Candidate.StableCompanionId == Id;
			});
		if (!Id.IsValid() || ActiveIds.Contains(Id) || !Record ||
			Record->State != EEFCalystoCompanionRosterStateV6::ActiveParty)
		{
			return Fail(OutError, TEXT("Active Party IDs must be unique and reference Active Party companion records."));
		}
		ActiveIds.Add(Id);
	}
	const FString Expected = ComputeCompanionRosterHash(Snapshot);
	if (!Snapshot.SnapshotHash.IsEmpty() && !Snapshot.SnapshotHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Companion Roster hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateRunEcology(
	const FEFCalystoRunEcologyStateV6& Ecology, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	if (!Ecology.bInitialized || !IsCanonicalHash(Ecology.RunDNAHash) ||
		!IsFiniteRange(Ecology.Scale, -1.0f, 1.0f) ||
		!IsFiniteRange(Ecology.Branching, -1.0f, 1.0f) ||
		!IsFiniteRange(Ecology.Threat, -1.0f, 1.0f) ||
		!IsFiniteRange(Ecology.Abundance, -1.0f, 1.0f) ||
		!IsFiniteRange(Ecology.Mystery, -1.0f, 1.0f) ||
		!IsFiniteRange(Ecology.PerformanceEMA, 0.0f, 1.0f) ||
		Ecology.LastCommittedFloor < 0 || Ecology.Revision < 0 ||
		Ecology.ConsecutiveFloorsWithoutFood < 0 || Ecology.ConsecutiveFloorsWithoutChest < 0)
	{
		return Fail(OutError, TEXT("Run Ecology contains invalid identity, traits, performance, revision, or pity counters."));
	}
	TSet<FName> CooldownIds;
	for (const FEFCalystoCooldownStateV6& Cooldown : Ecology.Cooldowns)
	{
		if (Cooldown.StableId.IsNone() || Cooldown.LastSelectedFloor < 0 ||
			Cooldown.CooldownFloors < 0 || CooldownIds.Contains(Cooldown.StableId))
		{
			return Fail(OutError, TEXT("Run Ecology cooldown entries require unique IDs and nonnegative values."));
		}
		CooldownIds.Add(Cooldown.StableId);
	}
	for (const FName StyleId : Ecology.RecentStyleIds)
	{
		if (StyleId.IsNone())
		{
			return Fail(OutError, TEXT("Run Ecology recent Style history cannot contain None."));
		}
	}
	if (!ValidateCompanionRoster(Ecology.CompanionRoster, OutError))
	{
		return false;
	}
	const FString Expected = ComputeRunEcologyHash(Ecology);
	if (!Ecology.EcologyHash.IsEmpty() && !Ecology.EcologyHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Run Ecology hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateResolvedFloorIntent(
	const FEFCalystoResolvedFloorIntentV6& Intent, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	if (!Intent.bIsValid ||
		Intent.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion ||
		Intent.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion ||
		!ValidateGenerationContext(Intent.GenerationContext, OutError) ||
		!ValidateDirectorIntent(Intent.DirectorIntent, OutError) ||
		!ValidateFloorOutcome(Intent.FrozenOutcome, OutError) ||
		!ValidateCompanionRoster(Intent.CompanionRoster, OutError) ||
		!ValidateFloorPlan(Intent.FloorPlan, OutError))
	{
		return OutError.IsEmpty()
			? Fail(OutError, TEXT("Resolved Floor Intent is not a valid direct V6 contract."))
			: false;
	}
	if (!IsCanonicalHash(Intent.EcologyHash) ||
		Intent.GenerationContext.PolicyHash != Intent.FloorPlan.PolicyHash ||
		!Intent.StyleId.IsEqual(Intent.FloorPlan.StyleId, ENameCase::IgnoreCase) ||
		Intent.DungeonSize.X < EFCalystoDungeonRuntimeSchemaV6::MinimumDungeonEdge ||
		Intent.DungeonSize.X > EFCalystoDungeonRuntimeSchemaV6::MaximumDungeonEdge ||
		Intent.DungeonSize.Y != Intent.DungeonSize.X || Intent.DungeonSize.Z != 1 ||
		!IsFiniteRange(Intent.CandidateDensity,
			EFCalystoDungeonRuntimeSchemaV6::MinimumCandidateDensity,
			EFCalystoDungeonRuntimeSchemaV6::MaximumCandidateDensity) ||
		!IsFiniteRange(Intent.SidePathChance,
			EFCalystoDungeonRuntimeSchemaV6::MinimumSidePathChance,
			EFCalystoDungeonRuntimeSchemaV6::MaximumSidePathChance) ||
		Intent.MinimumRoomSize < 4 || Intent.MaximumRoomSize > 8 ||
		Intent.MinimumRoomSize > Intent.MaximumRoomSize || Intent.PCGSeed <= 0 ||
		!IsFiniteRange(Intent.Lighting.IntensityDraw, 0.0f, 1.0f) ||
		!IsFiniteRange(Intent.Lighting.IntensityMultiplier, 0.0f, 4.0f) ||
		!IsFiniteRange(Intent.Lighting.WallLightHeightCm, 100.0f, 400.0f) ||
		Intent.Lighting.WallLightTileDistance < 4 || Intent.Lighting.WallLightTileDistance > 20)
	{
		return Fail(OutError, TEXT("Resolved Floor Intent violates a certified V6 layout, Style, seed, or lighting bound."));
	}
	TSet<FGuid> LevelIds;
	for (const FEFCalystoResolvedCompanionLevelV6& Level : Intent.ResolvedCompanionLevels)
	{
		if (!Level.StableCompanionId.IsValid() || Level.LogicalLevel < 1 ||
			Level.PhysicalACFLevel < 1 || Level.PhysicalACFLevel > 100 ||
			Level.PhysicalACFLevel != FMath::Min(Level.LogicalLevel, 100) ||
			LevelIds.Contains(Level.StableCompanionId))
		{
			return Fail(OutError, TEXT("Resolved companion levels must be unique, positive, and capped physically at 100."));
		}
		LevelIds.Add(Level.StableCompanionId);
	}
	const FString Expected = ComputeResolvedFloorIntentHash(Intent);
	if (!IsCanonicalHash(Intent.IntentHash) || !Intent.IntentHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Resolved Floor Intent hash does not match its canonical V6 fields."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::ValidateRealizedFloorManifest(
	const FEFCalystoRealizedFloorManifestV6& Manifest, FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutError.Reset();
	if (!Manifest.bIsValid ||
		Manifest.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion ||
		Manifest.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion ||
		Manifest.FloorNumber < 1 || Manifest.GenerationSerial < 0 || Manifest.StyleId.IsNone() ||
		!IsCanonicalHash(Manifest.IntentHash) || !IsCanonicalHash(Manifest.FloorPlanHash) ||
		!IsCanonicalHash(Manifest.RoomManifestHash) || !IsCanonicalHash(Manifest.AnchorTopologyHash) ||
		!IsCanonicalHash(Manifest.PopulationPlanHash) || !IsCanonicalHash(Manifest.CompanionSnapshotHash) ||
		Manifest.CandidateAnchorCount < 0 || Manifest.SpawnedActorCount < 0 ||
		Manifest.CandidateAnchorCount < Manifest.SpawnedActorCount ||
		Manifest.SpawnedActorCount != Manifest.Actors.Num() ||
		!FMath::IsFinite(Manifest.RealizedThreatCost) || Manifest.RealizedThreatCost < 0.0f ||
		!FMath::IsFinite(Manifest.RealizedResourceCost) || Manifest.RealizedResourceCost < 0.0f)
	{
		return Fail(OutError, TEXT("Realized Floor Manifest contains an invalid V6 identity, count, cost, or dependency hash."));
	}
	TSet<FName> ActorIds;
	for (const FEFCalystoRealizedPopulationActorRecordV6& Actor : Manifest.Actors)
	{
		if (Actor.StableActorId.IsNone() || Actor.StableRoomId <= 0 ||
			Actor.CategoryId.IsNone() || Actor.CatalogEntryId.IsNone() || Actor.ActorClass.IsNull() ||
			Actor.Transform.ContainsNaN() || Actor.LogicalLevel < 0 || Actor.CooldownFloors < 0 ||
			!FMath::IsFinite(Actor.ThreatCost) || Actor.ThreatCost < 0.0f ||
			!FMath::IsFinite(Actor.ResourceCost) || Actor.ResourceCost < 0.0f ||
			ActorIds.Contains(Actor.StableActorId))
		{
			return Fail(OutError, TEXT("Realized population actors require unique IDs, stable room/content identity, a soft class path, and finite facts."));
		}
		ActorIds.Add(Actor.StableActorId);
	}
	const FString Expected = ComputeRealizedFloorManifestHash(Manifest);
	if (!IsCanonicalHash(Manifest.ManifestHash) || !Manifest.ManifestHash.Equals(Expected, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Realized Floor Manifest hash does not match its canonical V6 facts."));
	}
	return true;
}

bool FEFCalystoDungeonRuntimeMathV6::BuildResolvedFloorIntent(
	const FEFCalystoDungeonGenerationContextV6& Context,
	const FEFCalystoDirectorIntentV6& DirectorIntent,
	const FEFCalystoFloorOutcomeV6& FrozenOutcome,
	const FEFCalystoRunEcologyStateV6& Ecology,
	const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster,
	const FEFCalystoResolvedFloorPlanV6& FloorPlan,
	const FEFCalystoLightingPolicyV6& StyleLighting,
	FEFCalystoResolvedFloorIntentV6& OutIntent,
	FString& OutError)
{
	using namespace EFCalystoDungeonRuntimeV6Private;
	OutIntent = FEFCalystoResolvedFloorIntentV6();
	OutError.Reset();
	if (!ValidateGenerationContext(Context, OutError) ||
		!ValidateDirectorIntent(DirectorIntent, OutError) ||
		!ValidateFloorOutcome(FrozenOutcome, OutError) ||
		!ValidateRunEcology(Ecology, OutError) ||
		!ValidateCompanionRoster(CompanionRoster, OutError) ||
		!ValidateFloorPlan(FloorPlan, OutError))
	{
		return false;
	}
	if (!Context.PolicyHash.Equals(FloorPlan.PolicyHash, ESearchCase::IgnoreCase))
	{
		return Fail(OutError, TEXT("Generation Context and frozen Floor Plan must reference the same V6 policy hash."));
	}
	if (!FMath::IsFinite(StyleLighting.IntensityMultiplier) ||
		StyleLighting.IntensityMultiplier < 0.0f || StyleLighting.IntensityMultiplier > 4.0f)
	{
		return Fail(OutError, TEXT("Style lighting intensity must be finite inside [0, 4]."));
	}

	OutIntent.SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;
	OutIntent.GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;
	OutIntent.GenerationContext = Context;
	OutIntent.GenerationContext.ContextHash = ComputeGenerationContextHash(OutIntent.GenerationContext);
	OutIntent.DirectorIntent = DirectorIntent;
	OutIntent.DirectorIntent.IntentHash = ComputeDirectorIntentHash(OutIntent.DirectorIntent);
	OutIntent.FrozenOutcome = FrozenOutcome;
	OutIntent.FrozenOutcome.OutcomeHash = ComputeFloorOutcomeHash(OutIntent.FrozenOutcome);
	OutIntent.CompanionRoster = CompanionRoster;
	OutIntent.CompanionRoster.SnapshotHash = ComputeCompanionRosterHash(OutIntent.CompanionRoster);
	OutIntent.EcologyHash = ComputeRunEcologyHash(Ecology);
	OutIntent.FloorPlan = FloorPlan;
	OutIntent.StyleId = FloorPlan.StyleId;

	const FString SamplingIdentity = BuildSamplingIdentity(Context, FloorPlan);
	const float SampledEdge = SamplePertLike(
		FloorPlan.Layout.DungeonSize, UniformLane(SamplingIdentity, TEXT("DungeonSize")));
	const int32 DungeonEdge = Context.DevelopmentForcedDungeonEdge > 0
		? Context.DevelopmentForcedDungeonEdge
		: FMath::Clamp(FMath::RoundToInt(SampledEdge),
			EFCalystoDungeonRuntimeSchemaV6::MinimumDungeonEdge,
			EFCalystoDungeonRuntimeSchemaV6::MaximumDungeonEdge);
	OutIntent.DungeonSize = FIntVector(DungeonEdge, DungeonEdge, 1);
	OutIntent.CandidateDensity = FMath::Clamp(
		SamplePertLike(FloorPlan.Layout.CandidateDensity,
			UniformLane(SamplingIdentity, TEXT("CandidateDensity"))),
		EFCalystoDungeonRuntimeSchemaV6::MinimumCandidateDensity,
		EFCalystoDungeonRuntimeSchemaV6::MaximumCandidateDensity);
	OutIntent.SidePathChance = FMath::Clamp(
		SamplePertLike(FloorPlan.Layout.SidePathChance,
			UniformLane(SamplingIdentity, TEXT("SidePathChance"))),
		EFCalystoDungeonRuntimeSchemaV6::MinimumSidePathChance,
		EFCalystoDungeonRuntimeSchemaV6::MaximumSidePathChance);
	OutIntent.MinimumRoomSize = FloorPlan.Layout.MinimumRoomSize;
	OutIntent.MaximumRoomSize = FloorPlan.Layout.MaximumRoomSize;

	OutIntent.Lighting.Mode = StyleLighting.Mode;
	OutIntent.Lighting.IntensityDraw = static_cast<float>(
		UniformLane(SamplingIdentity, TEXT("LightingIntensity")));
	OutIntent.Lighting.IntensityMultiplier = FMath::Clamp(
		StyleLighting.IntensityMultiplier *
		FMath::Lerp(0.92f, 1.08f, OutIntent.Lighting.IntensityDraw), 0.0f, 4.0f);
	float BaseHeight = 200.0f;
	int32 BaseDistance = 10;
	switch (StyleLighting.Mode)
	{
	case EEFCalystoLightingModeV6::Warm: BaseHeight = 190.0f; BaseDistance = 9; break;
	case EEFCalystoLightingModeV6::Cold: BaseHeight = 220.0f; BaseDistance = 11; break;
	case EEFCalystoLightingModeV6::Dark: BaseHeight = 240.0f; BaseDistance = 13; break;
	default: break;
	}
	OutIntent.Lighting.WallLightHeightCm = FMath::Clamp(
		BaseHeight + FMath::Lerp(-20.0f, 20.0f,
			static_cast<float>(UniformLane(SamplingIdentity, TEXT("LightingHeight")))),
		100.0f, 400.0f);
	OutIntent.Lighting.WallLightTileDistance = FMath::Clamp(
		BaseDistance + FMath::RoundToInt(FMath::Lerp(-1.0f, 1.0f,
			static_cast<float>(UniformLane(SamplingIdentity, TEXT("LightingDistance"))))), 4, 20);
	OutIntent.PCGSeed = 1 + static_cast<int32>(
		HashLane64(SamplingIdentity, TEXT("PCGSeed")) % static_cast<uint64>(MAX_int32 - 1));

	TArray<FEFCalystoCompanionRecordV6> CompanionRecords = CompanionRoster.Records;
	CompanionRecords.Sort(CompanionRecordLess);
	for (const FEFCalystoCompanionRecordV6& Record : CompanionRecords)
	{
		FEFCalystoResolvedCompanionLevelV6& Level = OutIntent.ResolvedCompanionLevels.AddDefaulted_GetRef();
		Level.StableCompanionId = Record.StableCompanionId;
		Level.Grade = Record.Grade;
		const FString CompanionIdentity = SamplingIdentity + TEXT("|") + GuidToken(Record.StableCompanionId);
		const int32 Jitter = FMath::FloorToInt(
			UniformLane(CompanionIdentity, TEXT("CompanionLevel")) * 5.0) - 2;
		Level.LogicalLevel = FMath::Clamp(
			static_cast<int32>(FMath::Min<int64>(Context.FloorNumber, MAX_int32 - 64)) +
			GradeLevelOffset(Record.Grade) + Jitter, 1, MAX_int32);
		Level.PhysicalACFLevel = FMath::Min(Level.LogicalLevel, 100);
	}

	OutIntent.bIsValid = true;
	OutIntent.IntentHash = ComputeResolvedFloorIntentHash(OutIntent);
	if (!ValidateResolvedFloorIntent(OutIntent, OutError))
	{
		OutIntent = FEFCalystoResolvedFloorIntentV6();
		return false;
	}
	return true;
}
