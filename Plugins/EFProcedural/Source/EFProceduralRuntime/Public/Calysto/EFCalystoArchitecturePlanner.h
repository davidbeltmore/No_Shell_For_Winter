#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentReservationPlanner.h"

/** Native opportunity plus finite compatibility established by actual surface/clearance tests.
 * A bounds value alone never establishes compatibility. Bounds enclose every supported variation. */
struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureOpportunity
{
	FGuid Id;
	int64 RoomId = 0;
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	FTransform NativeTransform = FTransform::Identity;
	TMap<FGuid, FBox> CompatibleEntryBounds;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureRequest
{
	FEFCalystoRandomKey Random;
	TArray<FEFCalystoContentRoom> Rooms;
	TArray<FEFCalystoArchitectureOpportunity> Opportunities;
	TArray<FGuid> CoolingDownIds;
	TArray<FBox> ExistingReservedSpace;
};

enum class EEFCalystoArchitectureOutcome : uint8
{
	Ineligible, CapacityExhausted, NoCompatibleAlternative, ChanceAbsent, ExplicitEmpty, Reserved
};

struct EFPROCEDURALRUNTIME_API FEFCalystoArchitectureDecision
{
	FGuid OpportunityId;
	int64 RoomId = 0;
	FGuid ThemeId;
	EEFCalystoPlacementZone Zone = EEFCalystoPlacementZone::Floor;
	EEFCalystoArchitectureOutcome Outcome = EEFCalystoArchitectureOutcome::Ineligible;
	bool bChanceRolled = false;
	double ChancePercent = 0;
	FEFCalystoArchitectureEntry Entry;
	FTransform WorldTransform = FTransform::Identity;
	FBox ReservedBounds = FBox(ForceInit);
};

/** Bounded optional native-slot decisions. No world spawning and no structural slot suppression. */
class EFPROCEDURALRUNTIME_API FEFCalystoArchitecturePlanner final
{
public:
	static bool Build(const FEFCalystoCompiledDirector& Config, const FEFCalystoArchitectureRequest& Request,
		TArray<FEFCalystoArchitectureDecision>& Decisions, FString& Error);
};
