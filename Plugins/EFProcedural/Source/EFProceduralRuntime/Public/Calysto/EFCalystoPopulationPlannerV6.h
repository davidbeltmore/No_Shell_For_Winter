#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "UObject/SoftObjectPath.h"

/** The kind of immutable population instruction emitted by the V6 planner. */
enum class EEFCalystoPopulationDecisionKindV6 : uint8
{
	Actor,
	ChestContent
};

/** Runtime-only inputs which are not part of topology or the frozen Director policy. */
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationBuildOptionsV6
{
	/** One-based dungeon floor used by chance, tier, threat, and eligibility curves. */
	int64 FloorNumber = 1;

	/** Enables entries which explicitly require graveyard eligibility. */
	bool bGraveyardEligible = false;

	/** Stable actor entry IDs currently excluded by the run-level cooldown ledger. */
	TSet<FName> CoolingDownActorEntryIds;

	/** Stable chest-content entry IDs currently excluded by the run-level cooldown ledger. */
	TSet<FName> CoolingDownContentEntryIds;
};

/** One load-free actor or chest-content decision assigned to a stable room. */
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationDecisionV6
{
	EEFCalystoPopulationDecisionKindV6 Kind = EEFCalystoPopulationDecisionKindV6::Actor;
	int64 StableRoomId = 0;
	FName StyleId = NAME_None;
	FName ThemeId = TEXT("NoTheme");
	FName CategoryId = NAME_None;
	FName EntryId = NAME_None;
	FSoftObjectPath ClassPath;
	int32 SpawnOrdinal = 0;
	/** Frozen authored placement for actor decisions. Chest contents keep the neutral Floor/zero-jitter defaults. */
	EEFCalystoPlacementZoneV6 PlacementZone = EEFCalystoPlacementZoneV6::Floor;
	float PositionJitterCm = 0.0f;
	EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common;
	EEFCalystoLifecycleV6 Lifecycle = EEFCalystoLifecycleV6::FloorLocal;
	float ThreatCost = 0.0f;
	FString ParentDecisionId;
	FString DecisionId;
};

/** Canonical decisions for one room. No UObject or actor is created by this record. */
struct EFPROCEDURALRUNTIME_API FEFCalystoRoomPopulationPlanV6
{
	int64 StableRoomId = 0;
	FName ThemeId = TEXT("NoTheme");
	FString EffectiveCatalogHash;
	TArray<FEFCalystoPopulationDecisionV6> Decisions;
	FString RoomPopulationHash;
};

/** A deterministic diagnostic count stored without relying on TMap iteration order. */
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationCountV6
{
	FName Id = NAME_None;
	int32 Count = 0;
};

/** Immutable floor population plan consumed later by the PCG materialization bridge. */
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationPlanV6
{
	int64 FloorSeed = 0;
	int64 FloorNumber = 1;
	FName StyleId = NAME_None;
	FString FloorPlanHash;
	FString RoomManifestHash;
	TArray<FEFCalystoRoomPopulationPlanV6> Rooms;
	TArray<FEFCalystoPopulationCountV6> CategoryCounts;
	TArray<FEFCalystoPopulationCountV6> EntryCounts;
	TArray<FSoftObjectPath> PreloadClassPaths;
	int32 ActorDecisionCount = 0;
	int32 ChestContentDecisionCount = 0;
	int32 EnemyCount = 0;
	int32 LooseFoodCount = 0;
	int32 ChestCount = 0;
	int32 LootActorCount = 0;
	int32 SpecialEventCount = 0;
	float TotalEnemyThreat = 0.0f;
	/** Immutable actor alias tables compiled for the effective catalogs actually queried. */
	int32 CompiledActorSelectionTableCount = 0;
	/** Immutable chest-content alias tables compiled for the effective catalogs actually queried. */
	int32 CompiledContentSelectionTableCount = 0;
	/** Constant-time actor-table draws performed before central budget arbitration. */
	int32 ActorSelectionDrawCount = 0;
	/** Constant-time content-table draws performed after accepted chest arbitration. */
	int32 ContentSelectionDrawCount = 0;
	FString PopulationHash;
};

/**
 * Pure, load-free V6 population compiler.
 *
 * It consumes the selected Style and immutable room manifest, performs all
 * room-local rolls in independent hash domains, and arbitrates the resulting
 * candidates against the selected Style's floor-wide limits exactly once.
 */
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationPlannerV6
{
	/** Resolves raw Theme overlays for tooling/tests before a floor snapshot is frozen. */
	static bool ResolveEffectiveCatalogs(
		const TArray<FEFCalystoCatalogOverlayV6>& StyleCatalogs,
		const TArray<FEFCalystoCatalogOverlayV6>& ThemeOverlays,
		TArray<FEFCalystoCatalogOverlayV6>& OutCatalogs,
		FString& OutError);

	/** Builds a canonical, immutable and load-free population plan. */
	static bool BuildPlan(
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationBuildOptionsV6& Options,
		FEFCalystoPopulationPlanV6& OutPlan,
		FString& OutError);

	/** Gathers the exact deduplicated soft class closure selected by BuildPlan. */
	static void GatherPreloadClassPaths(
		const FEFCalystoPopulationPlanV6& Plan,
		TArray<FSoftObjectPath>& OutPaths);
};
