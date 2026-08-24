#pragma once

#include "CoreMinimal.h"
#include "EFCalystoDungeonTypes.generated.h"

class AActor;

/** Stable, explicit counter-RNG domains that must never share a random stream. */
namespace EFCalystoDungeonDomains
{
	inline constexpr uint64 PCGSeed = 0x5043475F53454533ULL;      // PCG_SEE3
	inline constexpr uint64 ThreatBudget = 0x5448525F42554433ULL; // THR_BUD3
	inline constexpr uint64 Pacing = 0x504143494E475F33ULL;      // PACING_3
	inline constexpr uint64 Rarity = 0x5241524954595F33ULL;      // RARITY_3
}

/** Why a dungeon map load was requested. The value is diagnostic; it never drives random draws. */
UENUM(BlueprintType)
enum class EEFCalystoDungeonTravelKind : uint8
{
	None,
	NewRun,
	Advance,
	Reroll,
	Replay,
	DebugJump
};

/** Transient lifecycle state. The subsystem has no permanent tick. */
UENUM(BlueprintType)
enum class EEFCalystoDungeonTravelState : uint8
{
	Idle,
	TravelPending,
	AwaitingFloorReady
};

UENUM(BlueprintType)
enum class EEFCalystoGenerationState : uint8
{
	Idle,
	Generating,
	RealizingPopulation,
	Ready,
	Recovering,
	Failed,
	Returning
};

/** Styles are probability biases, never exact layout presets. */
UENUM(BlueprintType)
enum class EEFCalystoDungeonStyle : uint8
{
	Auto,
	Standard,
	Compact,
	Branching
};

UENUM(BlueprintType)
enum class EEFCalystoSpawnCategory : uint8
{
	Enemy,
	Food,
	Chest,
	Loot,
	SpecialEvent
};

/** Immutable identity of one floor generation. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonGenerationContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 PCGSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString PolicyHash;

	bool IsValid() const
	{
		return RunSeed > 0 && FloorNumber > 0 && GenerationSerial > 0 && PCGSeed > 0 && !PolicyHash.IsEmpty();
	}
};

/** A bounded Beta-PERT distribution. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloatDistribution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	float Mode = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	float Max = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (ClampMin = "2.0", ClampMax = "8.0"))
	float Concentration = 4.0f;
};

USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoIntDistribution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	int32 Min = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	int32 Mode = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution")
	int32 Max = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distribution", meta = (ClampMin = "2.0", ClampMax = "8.0"))
	float Concentration = 4.0f;
};

/** High-level request consumed at the next floor boundary. Values are normalized and never exact counts. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorIntent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director")
	EEFCalystoDungeonStyle PreferredStyle = EEFCalystoDungeonStyle::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ScaleBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float BranchingBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ThreatBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ResourceBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ThemeBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Volatility = 0.5f;
};

/** Normalized performance sample submitted before Advance. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoFloorOutcome
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director")
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Combat = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Survival = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Resources = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Pace = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0"))
	int32 Deaths = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Director", meta = (ClampMin = "0"))
	int32 Failures = 0;
};

/** Last committed floor on which one cooldown-enabled catalog entry appeared. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoPopulationCooldownState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FName StableId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (ClampMin = "1"))
	int64 LastSelectedFloor = 0;
};

/** Runtime lifecycle of one project-owned companion in the current run. */
UENUM(BlueprintType)
enum class EEFCalystoCompanionRunState : uint8
{
	Alive UMETA(DisplayName = "Alive"),
	PendingDead UMETA(DisplayName = "Pending Death"),
	Dead UMETA(DisplayName = "Confirmed Dead"),
	PendingRevival UMETA(DisplayName = "Pending Revival")
};

/** Whether an NPC is local to one floor or may enter the persistent run roster. */
UENUM(BlueprintType)
enum class EEFCalystoCompanionLifecycle : uint8
{
	FloorLocal UMETA(DisplayName = "Floor Local"),
	Recruitable UMETA(DisplayName = "Recruitable")
};

/** Canonical, actor-independent record of one companion owned by the run. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionRunEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FGuid StableCompanionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FName ContentId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FName Archetype = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FName Gender = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FName DifficultyGrade = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions", meta = (ClampMin = "1"))
	int32 ResolvedLogicalLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	EEFCalystoCompanionLifecycle Lifecycle = EEFCalystoCompanionLifecycle::Recruitable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	EEFCalystoCompanionRunState State = EEFCalystoCompanionRunState::Alive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	int64 DeathFloor = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	int64 DeathGenerationSerial = 0;
};

/** Frozen roster supplied to the Director before resolving a floor. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoCompanionRunSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	bool bIsValid = false;

	/** Changes only when the player explicitly starts a New Run; excluded from RNG. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	int64 RunEpoch = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions", meta = (TitleProperty = "ContentId"))
	TArray<FEFCalystoCompanionRunEntry> Entries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	TArray<FGuid> ActiveParty;

	/** Frozen inventory eligibility used only to exclude duplicate Winter's Recall drops. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	bool bPlayerOwnsWintersRecall = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companions")
	FString SnapshotHash;
};

/** Persistent, GameInstance-only personality and pacing memory for one run. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRunEcologyState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	bool bInitialized = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	bool bSyntheticHistory = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Scale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Branching = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Threat = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Abundance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Mystery = 0.0f;

	/** Immutable identity of the five run-DNA traits; smooth noise never uses mutable history. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FString RunDNAHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	float PerformanceEMA = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	int64 LastCommittedFloor = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	int64 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	int32 ConsecutiveFloorsWithoutFood = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	int32 ConsecutiveFloorsWithoutChest = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<EEFCalystoDungeonStyle> RecentStyles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<FName> RecentDominantThemes;

	/** Canonically hashed catalog memory. Entries are updated only when Advance commits a completed floor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology", meta = (TitleProperty = "StableId"))
	TArray<FEFCalystoPopulationCooldownState> PopulationCooldowns;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ecology")
	FString EcologyHash;
};

/** Flattened immutable theme value consumed by the transient Calysto adapter. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoThemeWeight
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FName ThemeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	TSoftObjectPtr<UObject> RoomType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Weight = 1;
};

/** Exact, frozen population request. Placement remains the PCG runtime adapter's responsibility. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoSpawnDirective
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	FName StableId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	EEFCalystoSpawnCategory Category = EEFCalystoSpawnCategory::Enemy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population")
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0"))
	int32 Count = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0.0"))
	float CostPerActor = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Population", meta = (ClampMin = "0"))
	int32 RelativeWeight = 1;
};

/** Frozen result of the Director, created once before travel and consumed by exactly one Calysto generation. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedFloorIntent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 PCGSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString PolicyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString EcologyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString IntentHash;

	/** Exact performance sample consumed by this roll; Replay never re-reads mutable telemetry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FEFCalystoFloorOutcome FrozenOutcome;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString OutcomeHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	EEFCalystoDungeonStyle Style = EEFCalystoDungeonStyle::Standard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Scale = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Branching = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Threat = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Abundance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Mystery = 0.0f;

	/** Independent per-floor cadence offset, bounded by the active policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Personality")
	float Pacing = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Layout")
	FIntVector DungeonSize = FIntVector(25, 25, 1);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Layout")
	float CandidateAnchorDensity = 0.32f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Layout")
	float SidePathChance = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Layout")
	int32 RoomMinSize = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Layout")
	int32 RoomMaxSize = 8;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 DifficultyTier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	float ThreatBudget = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	float ResourceBudget = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Probability")
	float EnemyPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Probability")
	float FoodPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Probability")
	float ChestPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Probability")
	float LootPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Probability")
	float SpecialEventPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 EnemyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 FoodCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 ChestCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 LootCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 SpecialEventCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Themes")
	FName DominantTheme = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Themes")
	TArray<FEFCalystoThemeWeight> ThemeWeights;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	TArray<FEFCalystoSpawnDirective> SpawnDirectives;
};

/** Post-navigation, post-placement record. This is the replay/debug evidence for realized population. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoRealizedFloorManifest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 PCGSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString IntentHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString AnchorTopologyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString PopulationHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString ResourceHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString ManifestHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 CandidateAnchorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 EnemyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 FoodCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 ChestCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 LootCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 SpecialEventCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	float RealizedThreatCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	float RealizedResourceCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	int32 SpawnedActorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Population")
	TArray<FEFCalystoSpawnDirective> SpawnDirectives;
};

/** Cached status view. GetSnapshot never loads assets or performs random draws. */
USTRUCT(BlueprintType)
struct EFPROCEDURALRUNTIME_API FEFCalystoDungeonSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	bool bHasActiveRun = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 RunEpoch = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString CompanionSnapshotHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	bool bPolicyValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString PolicyError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 RunSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 PCGSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	EEFCalystoDungeonStyle Style = EEFCalystoDungeonStyle::Auto;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FIntVector DungeonSize = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float CandidateAnchorDensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float SidePathChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float Pacing = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float ThreatBudget = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float ResourceBudget = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float EnemyPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float FoodPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float ChestPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float LootPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float SpecialEventPresenceChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 EnemyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 FoodCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 ChestCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 LootCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int32 SpecialEventCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float RealizedThreatCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	float RealizedResourceCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString PolicyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString EcologyHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString IntentHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	FString ManifestHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	bool bHasQueuedDirectorIntent = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	EEFCalystoDungeonTravelKind TravelKind = EEFCalystoDungeonTravelKind::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	EEFCalystoDungeonTravelState TravelState = EEFCalystoDungeonTravelState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 PendingFloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon")
	int64 PendingGenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	EEFCalystoGenerationState GenerationState = EEFCalystoGenerationState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	FName FailureCode = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	FString FailureMessage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	int32 CurrentAttempt = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	int32 MaximumAttempts = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto Dungeon|Generation")
	FString ReturnMapPackage;
};

/** Stable deterministic helpers shared with automation without loading a world. */
namespace EFCalystoDungeonDeterminism
{
	EFPROCEDURALRUNTIME_API int32 DerivePCGSeed(
		int64 RunSeed,
		int64 FloorNumber,
		int64 GenerationSerial,
		int32 GeneratorVersion,
		const FString& PolicyHash,
		const FString& EcologyHash);
	EFPROCEDURALRUNTIME_API float EffectivePERTConcentration(float AuthoredConcentration, float Volatility);
	EFPROCEDURALRUNTIME_API uint64 DeriveDomainValue(
		const FEFCalystoDungeonGenerationContext& Context,
		int32 GeneratorVersion,
		const FString& EcologyHash,
		uint64 Domain,
		uint64 StableEntityId = 0,
		uint32 DrawIndex = 0);
	EFPROCEDURALRUNTIME_API double Uniform01(
		const FEFCalystoDungeonGenerationContext& Context,
		int32 GeneratorVersion,
		const FString& EcologyHash,
		uint64 Domain,
		uint64 StableEntityId = 0,
		uint32 DrawIndex = 0);
	EFPROCEDURALRUNTIME_API bool Bernoulli(
		double Probability,
		const FEFCalystoDungeonGenerationContext& Context,
		int32 GeneratorVersion,
		const FString& EcologyHash,
		uint64 Domain,
		uint64 StableEntityId = 0,
		uint32 DrawIndex = 0);
	EFPROCEDURALRUNTIME_API float SamplePERT(
		const FEFCalystoFloatDistribution& Distribution,
		float Volatility,
		const FEFCalystoDungeonGenerationContext& Context,
		int32 GeneratorVersion,
		const FString& EcologyHash,
		uint64 Domain,
		uint64 StableEntityId = 0);
	EFPROCEDURALRUNTIME_API int32 SamplePERT(
		const FEFCalystoIntDistribution& Distribution,
		float Volatility,
		const FEFCalystoDungeonGenerationContext& Context,
		int32 GeneratorVersion,
		const FString& EcologyHash,
		uint64 Domain,
		uint64 StableEntityId = 0);
	EFPROCEDURALRUNTIME_API double Progression(int64 FloorNumber, double Tau);
	EFPROCEDURALRUNTIME_API int32 SelectWeightedIndex(const TArray<int32>& Weights, uint64 RandomValue);
}
