#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDecalPool.h"
#include "Calysto/EFCalystoDungeonDirectorAsset.h"

class UPrimitiveComponent;

/** A finite, collision-proven native surface opportunity.  It is assembled before any
 * decal Chance or Weight draw and remains tied to its exact managed support component. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalOpportunity
{
	FGuid Id;
	int64 RoomId = 0;
	int64 NativeOpportunityId = 0;
	FGuid ThemeId;
	EEFCalystoDecalSurface Surface = EEFCalystoDecalSurface::Floor;
	FTransform WorldTransform = FTransform::Identity;
	FVector SurfaceNormal = FVector::UpVector;
	/** Entire supported square side length, proved against the actual collision surface. */
	double MaximumSizeCm = 0.0;
	TWeakObjectPtr<UPrimitiveComponent> Support;
	int32 InstanceIndex = INDEX_NONE;
	FTransform SupportTransform = FTransform::Identity;
	/** Exact interior witnesses for each actual collision face covering the maximum footprint. */
	TArray<FEFCalystoDecalCoverageSupport> CoverageSupports;
};

enum class EEFCalystoDecalOpportunityOutcome : uint8
{
	Ineligible,
	NoCompatibleVariant,
	CapacityExcluded,
	ChanceAbsent,
	Reserved
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalOpportunityReport
{
	FGuid OpportunityId;
	int64 RoomId = 0;
	EEFCalystoDecalSurface Surface = EEFCalystoDecalSurface::Floor;
	FGuid ThemeId;
	EEFCalystoDecalOpportunityOutcome Outcome = EEFCalystoDecalOpportunityOutcome::Ineligible;
	double RequestedChancePercent = 0.0;
	bool bChanceRolled = false;
	FGuid SelectedVariant;
};

/** The immutable result of bounded feasibility, Chance, Weight and Size decisions. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoReservedDecalManifest final
{
public:
	bool IsValid() const { return bValid; }
	const TArray<FEFCalystoReservedDecal>& GetElements() const { return Elements; }
	const TArray<FSoftObjectPath>& GetSelectedDependencies() const { return Dependencies; }
	const FString& GetHash() const { return Hash; }
private:
	friend struct FEFCalystoDecalReservationPlanner;
	bool bValid = false;
	TArray<FEFCalystoReservedDecal> Elements;
	TArray<FSoftObjectPath> Dependencies;
	FString Hash;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalPlanningReport
{
	FName FailureCode;
	FString Message;
	int32 FeasibleRooms = 0;
	int32 CapacityReservedRooms = 0;
	int32 SelectedDecals = 0;
	/** An invalid native support is recoverable spatial evidence; authored closure errors are not. */
	bool bSpatialFailure = false;
	TArray<FEFCalystoDecalOpportunityReport> Opportunities;
};

/** Pure, bounded selected-decal reservation.  It never loads, traces, spawns, or changes a pool.
 * A Chance miss does not backfill another room, and no selected decal can be silently dropped. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalReservationPlanner final
{
	static bool ValidateConfiguration(const FEFCalystoCompiledDirector& Configuration,
		const FGuid& StyleId, FString& Error);
	static bool HasPotentiallyActiveDecals(const FEFCalystoCompiledDirector& Configuration,
		const FGuid& StyleId);
	static bool Build(const FEFCalystoCompiledDirector& Configuration, const FEFCalystoRandomKey& Random,
		TConstArrayView<FEFCalystoDecalOpportunity> Opportunities,
		FEFCalystoReservedDecalManifest& OutManifest, FEFCalystoDecalPlanningReport& OutReport);
};
