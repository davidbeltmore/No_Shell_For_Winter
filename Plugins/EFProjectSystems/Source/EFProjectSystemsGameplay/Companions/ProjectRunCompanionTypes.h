#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "ProjectRunCompanionTypes.generated.h"

class AACFCharacter;
class UDataTable;

UENUM(BlueprintType)
enum class EProjectCompanionLifecycle : uint8
{
	FloorLocal UMETA(DisplayName = "Floor Local"),
	Recruitable UMETA(DisplayName = "Recruitable")
};

UENUM(BlueprintType)
enum class EProjectCompanionDifficultyGrade : uint8
{
	Common UMETA(DisplayName = "Common"),
	Uncommon UMETA(DisplayName = "Uncommon"),
	Rare UMETA(DisplayName = "Rare"),
	Epic UMETA(DisplayName = "Epic"),
	Winter UMETA(DisplayName = "Winter")
};

UENUM(BlueprintType)
enum class EProjectCompanionRunState : uint8
{
	Alive UMETA(DisplayName = "Alive"),
	PendingDead UMETA(DisplayName = "Pending Death"),
	Dead UMETA(DisplayName = "Confirmed Dead"),
	PendingRevival UMETA(DisplayName = "Pending Revival")
};

UENUM(BlueprintType)
enum class EProjectCompanionDirectorTravelMode : uint8
{
	None UMETA(Hidden),
	NewRun UMETA(DisplayName = "New Run"),
	Replay UMETA(DisplayName = "Replay"),
	Reroll UMETA(DisplayName = "Reroll"),
	Advance UMETA(DisplayName = "Advance"),
	DebugJump UMETA(DisplayName = "Development Jump")
};

UENUM(BlueprintType)
enum class EProjectCompanionSpawnFailure : uint8
{
	None,
	InvalidRequest,
	ClassNotPreloaded,
	NoAuthority,
	NoNavigation,
	NoSafeLocation,
	SpawnFailed,
	InitializerMissing,
	InitializerConfigurationFailed,
	StatisticsRepairAssetMissing,
	StatisticsRepairRowMissing,
	StatisticsRepairFailed,
	LogicalLevelComponentFailed,
	CompanionGroupMissing,
	GroupRegistrationFailed,
	ControllerMissing,
	TeamComponentMissing,
	PerceptionComponentMissing,
	SocialRegistrationFailed
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCompanionDefinition
{
	GENERATED_BODY()

	FProjectCompanionDefinition();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ToolTip = "Stable GUID unique within the run."))
	FGuid StableCompanionId;

	/** Deterministic identity of the Director spawn instance that originated
	 * this recruit. This is deliberately independent from catalog content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ToolTip = "Deterministic ID of the spawn instance that originated this companion. It does not identify a catalog variant."))
	FName SourceSpawnId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ToolTip = "Human-readable stable ID of the catalog that originated this companion."))
	FName ContentId = NAME_None;

	/** Concrete reusable variant selected inside ContentId's catalog entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ToolTip = "Concrete variant selected within the catalog. Multiple recruited instances may share it."))
	FName CatalogVariantId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ToolTip = "Preloaded ACF class that represents the companion."))
	TSoftClassPtr<AACFCharacter> CharacterClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions")
	FName Archetype = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions")
	FName Gender = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions")
	EProjectCompanionDifficultyGrade DifficultyGrade = EProjectCompanionDifficultyGrade::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions", meta = (ClampMin = "1", ToolTip = "Logical Director level. ACF receives a maximum physical level of 100."))
	int32 ResolvedLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions")
	EProjectCompanionLifecycle Lifecycle = EProjectCompanionLifecycle::Recruitable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions|ACF", meta = (ToolTip = "Repairs only the instance when the ACF Data Asset has no valid statistics row."))
	bool bRepairMissingStatisticsRow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions|ACF", meta = (EditCondition = "bRepairMissingStatisticsRow", RowType = "/Script/AscentGASRuntime.ACFAttributeInits"))
	TSoftObjectPtr<UDataTable> StatisticsRepairDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Companions|ACF", meta = (EditCondition = "bRepairMissingStatisticsRow"))
	FName StatisticsRepairRow = TEXT("MMEnemy");

	bool IsValid(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCompanionRunEntrySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	FProjectCompanionDefinition Definition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	EProjectCompanionRunState State = EProjectCompanionRunState::Alive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	int64 DeathFloor = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	int64 DeathGenerationSerial = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCompanionRunSnapshot
{
	GENERATED_BODY()

	/** Session identity. Intentionally excluded from SnapshotHash and all RNG. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	int64 RunEpoch = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	int64 FloorNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	int64 GenerationSerial = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	TArray<FProjectCompanionRunEntrySnapshot> Entries;

	/** Desired party membership. Only living entries are submitted to the Director as active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	TArray<FGuid> ActiveParty;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	FString SnapshotHash;

	/** Builds the canonical SHA-256 for this snapshot. RunEpoch is intentionally excluded. */
	FString ComputeCanonicalHash() const;

	/** Replaces SnapshotHash with the canonical value after an intentional mutation. */
	void RefreshHash();

	bool IsValid(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCompanionSpawnResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	EProjectCompanionSpawnFailure Failure = EProjectCompanionSpawnFailure::InvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	FString Diagnostic;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions")
	TObjectPtr<AACFCharacter> SpawnedCharacter = nullptr;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCompanionRevivalCandidate
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	FGuid StableCompanionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	FName Archetype = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	FName Gender = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	EProjectCompanionDifficultyGrade DifficultyGrade = EProjectCompanionDifficultyGrade::Common;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	int32 ResolvedLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Companions|Revival")
	bool bDeathPendingAdvance = false;
};
