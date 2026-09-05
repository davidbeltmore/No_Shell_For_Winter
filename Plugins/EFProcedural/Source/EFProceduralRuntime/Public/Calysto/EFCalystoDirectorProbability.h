#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDirectorTypes.h"

/** Routing request IDs, labels, resource paths and aggregate config hashes are deliberately absent. */
struct EFPROCEDURALRUNTIME_API FEFCalystoRandomKey
{
	int64 RunSeed = 0;
	int64 FloorNumber = 1;
	int32 RerollIndex = 0;
	int32 AttemptIndex = 0;
	FGuid StyleId;
};

enum class EEFCalystoRandomDomain : uint32
{
	Style = 0x5354594C, Topology = 0x544F504F, ThemeGuarantee = 0x47554152,
	ThemePresence = 0x50524553, ThemeType = 0x5448454D, Chance = 0x43484E43,
	Amount = 0x414D4E54, Rarity = 0x52415245, Entry = 0x454E5452,
	Placement = 0x504C4143, Architecture = 0x41524348, Lighting = 0x4C495447,
	Decal = 0x44454341, Extension = 0x4558544E
};

struct EFPROCEDURALRUNTIME_API FEFCalystoWeightedAlternative
{
	FGuid Id;
	double Weight = 0.0;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoThemeOpportunity
{
	int64 RoomId = 0;
	EEFCalystoProtectedRoom Protected = EEFCalystoProtectedRoom::None;
	/** Already filtered for depth, cooldown, supported room type and positive capacity. */
	TArray<FEFCalystoWeightedAlternative> EligibleThemes;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoThemeDecision
{
	int64 RoomId = 0;
	FGuid ThemeId;
	bool bGuaranteed = false;
};

/** Pure bounded decisions. Callers supply finite compatibility/reservation sets before any draw. */
struct EFPROCEDURALRUNTIME_API FEFCalystoDirectorProbability final
{
	static uint64 Hash(const FEFCalystoRandomKey& Key, EEFCalystoRandomDomain Domain,
		const FGuid& OpportunityId = FGuid(), int64 DrawIndex = 0);
	static double Unit(const FEFCalystoRandomKey& Key, EEFCalystoRandomDomain Domain,
		const FGuid& OpportunityId = FGuid(), int64 DrawIndex = 0);
	static FGuid RoomIdentity(int64 RoomId);
	static bool IsValid(const FEFCalystoFloatDistribution& Distribution);
	static bool IsValid(const FEFCalystoAmountDistribution& Distribution);
	static bool IsValid(const FEFCalystoPercentageCurve& Curve);
	static bool IsValid(const FEFCalystoScalarCurve& Curve);
	static double EvaluateLinearCurve(const FEFCalystoPercentageCurve& Curve, int64 FloorNumber);
	static double EvaluateLinearCurve(const FEFCalystoScalarCurve& Curve, int64 FloorNumber);
	static FEFCalystoRarityWeights EvaluateRarity(const FEFCalystoRarityCurve& Curve, int64 FloorNumber);
	static bool RollChance(double Percent, double Uniform);
	static bool SampleFloat(const FEFCalystoFloatDistribution& Distribution, double Uniform,
		double& OutValue, FString& OutError);
	static double AmountProbability(const FEFCalystoAmountDistribution& Distribution, int32 Amount);
	/** Exact requested PMF conditioned on feasible amounts; never samples then truncates. */
	static bool SampleFeasibleAmount(const FEFCalystoAmountDistribution& Distribution,
		TConstArrayView<int32> FeasibleAmounts, double Uniform, int32& OutAmount,
		double& OutFeasibleProbabilityMass, FString& OutError);
	/** Canonical O(n log n) bounded table; source order does not affect the selected identity. */
	static bool SelectWeighted(TConstArrayView<FEFCalystoWeightedAlternative> Eligible,
		double Uniform, FGuid& OutId, FString& OutError);
	static bool SelectPopulatedRarity(const FEFCalystoRarityWeights& Weights,
		TConstArrayView<EEFCalystoRarity> PopulatedEligibleTiers, double Uniform,
		EEFCalystoRarity& OutTier, FString& OutError);
	static bool SelectThemedRooms(const FEFCalystoRandomKey& Key,
		TConstArrayView<FEFCalystoThemeOpportunity> Rooms, double AdditionalChancePercent,
		TArray<FEFCalystoThemeDecision>& OutDecisions, FString& OutError);
	static double AdaptationMultiplier(const FEFCalystoAdaptation& Policy, double NormalizedInput);
	static bool IsEligible(const FEFCalystoSelection& Selection, int64 FloorNumber,
		TConstArrayView<FGuid> CoolingDownIds);
};
