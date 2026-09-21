#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"
#include "EFCalystoDungeonRuntimeV6.generated.h"

class AActor;

namespace EFCalystoDungeonRuntimeSchemaV6
{
inline constexpr int32 SchemaVersion = 6;
inline constexpr int32 GeneratorVersion = 6;
inline constexpr int32 MinimumDungeonEdge = 18;
inline constexpr int32 MaximumDungeonEdge = 30;
inline constexpr float MinimumCandidateDensity = 0.20f;
inline constexpr float MaximumCandidateDensity = 0.50f;
inline constexpr float MinimumSidePathChance = 0.30f;
inline constexpr float MaximumSidePathChance = 0.70f;
}

/** User-visible identity of a run transition. It is diagnostic and never feeds a random draw. */
UENUM(BlueprintType)
enum class EEFCalystoDungeonTravelKindV6 : uint8
{
	None UMETA(DisplayName = "None"),
	NewRun UMETA(DisplayName = "New Run"),
	Advance UMETA(DisplayName = "Advance Floor"),
	Reroll UMETA(DisplayName = "Reroll Floor"),
	Replay UMETA(DisplayName = "Replay Floor"),
	Retry UMETA(DisplayName = "Retry Generation"),
	RestartSameSeed UMETA(DisplayName = "Restart Same Seed"),
	DevelopmentJump UMETA(DisplayName = "Development Jump"),
	RecoverToHub UMETA(DisplayName = "Recover to HUB")
};

/** Fail-closed travel and generation state exposed to UI, automation, and the floor door. */
UENUM(BlueprintType)
enum class EEFCalystoDungeonTravelStateV6 : uint8
{
	Idle UMETA(DisplayName = "Inactive"),
	Preloading UMETA(DisplayName = "Preloading"),
	Traveling UMETA(DisplayName = "Traveling"),
	Generating UMETA(DisplayName = "Generating"),
	Ready UMETA(DisplayName = "Ready"),
	Recovering UMETA(DisplayName = "Recovering"),
	Failed UMETA(DisplayName = "Failed")
};

UENUM(BlueprintType)
enum class EEFCalystoCompanionRosterStateV6 : uint8
{
	ActiveParty UMETA(DisplayName = "Active Party"),
	RecruitedInactive UMETA(DisplayName = "Recruited Inactive"),
	Dead UMETA(DisplayName = "Confirmed Dead"),
	PendingDead UMETA(DisplayName = "Pending Death")
};

/** Immutable identity inputs for one generation attempt. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonGenerationContextV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	int64 RunSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1"))
	int64 FloorNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0"))
	int64 GenerationSerial = 0;

	/** Monotonic session identity. It is excluded from deterministic generation draws. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0"))
	int64 RunEpoch = 0;

	/** Diagnostic operation identity. Replay and Retry can therefore preserve the same generation identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	EEFCalystoDungeonTravelKindV6 TravelKind = EEFCalystoDungeonTravelKindV6::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	FString PolicyHash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Development", meta = (ClampMin = "0", ClampMax = "30"))
	int32 DevelopmentForcedDungeonEdge = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Development")
	FName DevelopmentPopulationScenario = NAME_None;

	/** Canonical identity hash. TravelKind and RunEpoch are deliberately excluded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generation")
	FString ContextHash;
};

/** Optional player/debug guidance. A room Theme is never selected at floor scope. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorIntentV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Scale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Branching = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Danger = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Safety = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Abundance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Mystery = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0", DisplayName = "Clothing Influence"))
	float ClothingInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Volatility = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (DisplayName = "Has Preferred Style"))
	bool bHasPreferredStyle = false;

	/** Stable open-ended Style identifier. This is a preference, not a second selected Style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent", meta = (EditCondition = "bHasPreferredStyle", DisplayName = "Preferred Style ID"))
	FName PreferredStyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent")
	FString IntentHash;
};

/** Frozen performance signal consumed by adaptation; all values are normalized. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloorOutcomeV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Combat = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Survival = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Resources = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Pace = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outcome", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DeathsAndFailures = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Outcome")
	FString OutcomeHash;
};

/** Canonical, actor-independent companion record owned by the run. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionRecordV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FGuid StableCompanionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FName SourceSpawnId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FName SourceCatalogId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FName SourceVariantId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FName Archetype = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	EEFCalystoGenderV6 Gender = EEFCalystoGenderV6::Any;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	EEFCalystoRarityTierV6 Grade = EEFCalystoRarityTierV6::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	EEFCalystoCompanionRosterStateV6 State = EEFCalystoCompanionRosterStateV6::RecruitedInactive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion", meta = (ClampMin = "0"))
	int64 DeathFloor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion", meta = (ClampMin = "0"))
	int64 DeathGenerationSerial = 0;
};

/** Frozen roster supplied before floor resolution. RunEpoch never affects its canonical identity. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionRosterSnapshotV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions", meta = (ClampMin = "0"))
	int64 RunEpoch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions", meta = (TitleProperty = "SourceVariantId"))
	TArray<FEFCalystoCompanionRecordV6> Records;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	TArray<FGuid> ActiveParty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions", meta = (DisplayName = "Player Owns Winter's Recall"))
	bool bPlayerOwnsWintersRecall = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FString SnapshotHash;
};

/** Deterministic level frozen for every roster record, including inactive companions. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedCompanionLevelV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion")
	FGuid StableCompanionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion")
	EEFCalystoRarityTierV6 Grade = EEFCalystoRarityTierV6::Common;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion", meta = (ClampMin = "1"))
	int32 LogicalLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion", meta = (ClampMin = "1", ClampMax = "100", DisplayName = "Physical ACF Level"))
	int32 PhysicalACFLevel = 1;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCooldownStateV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FName StableId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int64 LastSelectedFloor = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int32 CooldownFloors = 0;
};

/** Persistent GameInstance-owned run memory. Room Theme selections are intentionally absent. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRunEcologyStateV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	bool bInitialized = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	bool bDevelopmentSyntheticHistory = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FString RunDNAHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Scale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Branching = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Threat = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Abundance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Mystery = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PerformanceEMA = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int64 LastCommittedFloor = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int64 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int32 ConsecutiveFloorsWithoutFood = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "0"))
	int32 ConsecutiveFloorsWithoutChest = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<FName> RecentStyleIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (TitleProperty = "StableId"))
	TArray<FEFCalystoCooldownStateV6> Cooldowns;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FEFCalystoCompanionRosterSnapshotV6 CompanionRoster;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FString EcologyHash;
};

/** Concrete native lighting values frozen before travel. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedLightingV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting")
	EEFCalystoLightingModeV6 Mode = EEFCalystoLightingModeV6::Balanced;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntensityDraw = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float IntensityMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting", meta = (ClampMin = "100.0", ClampMax = "400.0", DisplayName = "Wall Light Height (cm)"))
	float WallLightHeightCm = 200.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lighting", meta = (ClampMin = "4", ClampMax = "20", DisplayName = "Wall Light Tile Distance"))
	int32 WallLightTileDistance = 10;
};

/** Complete pre-travel V6 contract. Exactly one Style is selected; Themes remain room-local. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedFloorIntentV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	int32 SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	int32 GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoDungeonGenerationContextV6 GenerationContext;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoDirectorIntentV6 DirectorIntent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoFloorOutcomeV6 FrozenOutcome;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoCompanionRosterSnapshotV6 CompanionRoster;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FString EcologyHash;

	/** Immutable Style policy snapshot. It contains reachable Theme definitions, never a selected floor Theme. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoResolvedFloorPlanV6 FloorPlan;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent", meta = (DisplayName = "Selected Style ID"))
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FIntVector DungeonSize = FIntVector(24, 24, 1);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent", meta = (ClampMin = "0.20", ClampMax = "0.50"))
	float CandidateDensity = 0.32f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent", meta = (ClampMin = "0.30", ClampMax = "0.70"))
	float SidePathChance = 0.50f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent", meta = (ClampMin = "4", ClampMax = "8"))
	int32 MinimumRoomSize = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent", meta = (ClampMin = "4", ClampMax = "8"))
	int32 MaximumRoomSize = 8;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FEFCalystoResolvedLightingV6 Lighting;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	int32 PCGSeed = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	TArray<FEFCalystoResolvedCompanionLevelV6> ResolvedCompanionLevels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Floor Intent")
	FString IntentHash;
};

/** Actor-independent evidence for one population actor that was actually materialized. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRealizedPopulationActorRecordV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FName StableActorId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	int64 StableRoomId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FName CategoryId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FName CatalogEntryId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	EEFCalystoRarityTierV6 Tier = EEFCalystoRarityTierV6::Common;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	EEFCalystoLifecycleV6 Lifecycle = EEFCalystoLifecycleV6::FloorLocal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0"))
	int32 LogicalLevel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.0"))
	float ThreatCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.0"))
	float ResourceCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0"))
	int32 CooldownFloors = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FGuid StableCompanionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	TArray<FName> VerifiedChestContentIds;
};

/** Canonical post-placement evidence. It records facts and can never trigger a reroll or UObject load. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRealizedFloorManifestV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int32 SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int32 GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString IntentHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString FloorPlanHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString RoomManifestHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString AnchorTopologyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString PopulationPlanHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString CompanionSnapshotHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (ClampMin = "0"))
	int32 CandidateAnchorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (ClampMin = "0"))
	int32 SpawnedActorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (ClampMin = "0.0"))
	float RealizedThreatCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (ClampMin = "0.0"))
	float RealizedResourceCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest", meta = (TitleProperty = "StableActorId"))
	TArray<FEFCalystoRealizedPopulationActorRecordV6> Actors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FString ManifestHash;
};

/** Public immutable snapshot for UI, automation, failure recovery, and the generated floor door. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonSnapshotV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int32 SchemaVersion = EFCalystoDungeonRuntimeSchemaV6::SchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int32 GeneratorVersion = EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	EEFCalystoDungeonTravelStateV6 State = EEFCalystoDungeonTravelStateV6::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	EEFCalystoDungeonTravelKindV6 TravelKind = EEFCalystoDungeonTravelKindV6::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	bool bHasActiveRun = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	bool bPolicyValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString PolicyError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	bool bHasQueuedDirectorIntent = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FName StyleId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FIntVector DungeonSize = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	int32 PCGSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString FloorPlanHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString FloorIntentHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString RoomManifestHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString PopulationManifestHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bPCGComplete = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bNavigationPathReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bRoomManifestReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bPopulationReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bVisualsReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Readiness")
	bool bDoorEnabled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString FailureReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Snapshot")
	FString SnapshotHash;
};

/** Load-free deterministic functions for the definitive V6 run/travel IR. */
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonRuntimeMathV6 final
{
	static bool IsCanonicalHash(const FString& Hash);

	static FString ComputeGenerationContextHash(const FEFCalystoDungeonGenerationContextV6& Context);
	static FString ComputeDirectorIntentHash(const FEFCalystoDirectorIntentV6& Intent);
	static FString ComputeFloorOutcomeHash(const FEFCalystoFloorOutcomeV6& Outcome);
	static FString ComputeCompanionRosterHash(const FEFCalystoCompanionRosterSnapshotV6& Snapshot);
	static FString ComputeRunEcologyHash(const FEFCalystoRunEcologyStateV6& Ecology);
	static FString ComputeResolvedFloorIntentHash(const FEFCalystoResolvedFloorIntentV6& Intent);
	static FString ComputeRealizedFloorManifestHash(const FEFCalystoRealizedFloorManifestV6& Manifest);
	static FString ComputeDungeonSnapshotHash(const FEFCalystoDungeonSnapshotV6& Snapshot);

	static bool ValidateGenerationContext(const FEFCalystoDungeonGenerationContextV6& Context, FString& OutError);
	static bool ValidateDirectorIntent(const FEFCalystoDirectorIntentV6& Intent, FString& OutError);
	static bool ValidateFloorOutcome(const FEFCalystoFloorOutcomeV6& Outcome, FString& OutError);
	static bool ValidateCompanionRoster(const FEFCalystoCompanionRosterSnapshotV6& Snapshot, FString& OutError);
	static bool ValidateRunEcology(const FEFCalystoRunEcologyStateV6& Ecology, FString& OutError);
	static bool ValidateResolvedFloorIntent(const FEFCalystoResolvedFloorIntentV6& Intent, FString& OutError);
	static bool ValidateRealizedFloorManifest(const FEFCalystoRealizedFloorManifestV6& Manifest, FString& OutError);

	/**
	 * Freezes one Style-scoped pre-travel intent. All samples use independent hash
	 * lanes derived from immutable generation identity; no global random state or
	 * UObject load is touched.
	 */
	static bool BuildResolvedFloorIntent(
		const FEFCalystoDungeonGenerationContextV6& Context,
		const FEFCalystoDirectorIntentV6& DirectorIntent,
		const FEFCalystoFloorOutcomeV6& FrozenOutcome,
		const FEFCalystoRunEcologyStateV6& Ecology,
		const FEFCalystoCompanionRosterSnapshotV6& CompanionRoster,
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		const FEFCalystoLightingPolicyV6& StyleLighting,
		FEFCalystoResolvedFloorIntentV6& OutIntent,
		FString& OutError);
};
