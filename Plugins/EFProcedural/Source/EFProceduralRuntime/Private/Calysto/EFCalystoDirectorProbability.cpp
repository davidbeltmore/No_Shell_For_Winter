#include "Calysto/EFCalystoDirectorProbability.h"

namespace EFCalystoProbabilityPrivate
{
uint64 Mix(uint64 Value)
{
	Value += 0x9E3779B97F4A7C15ull;
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
	return Value ^ (Value >> 31);
}

uint64 AddGuid(uint64 Value, const FGuid& Id)
{
	Value = Mix(Value ^ (static_cast<uint64>(Id.A) << 32) ^ Id.B);
	return Mix(Value ^ (static_cast<uint64>(Id.C) << 32) ^ Id.D);
}

bool GuidLess(const FGuid& A, const FGuid& B)
{
	if (A.A != B.A) return A.A < B.A;
	if (A.B != B.B) return A.B < B.B;
	if (A.C != B.C) return A.C < B.C;
	return A.D < B.D;
}

bool UniformValid(const double Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0 && Value < 1.0;
}

bool Fail(FString& Error, const TCHAR* Message)
{
	Error = Message;
	return false;
}

double Linear(const int32 FirstFloor, const int32 LastFloor,
	const double FirstValue, const double LastValue, const int64 FloorNumber)
{
	if (FloorNumber <= FirstFloor) return FirstValue;
	if (FloorNumber >= LastFloor) return LastValue;
	const double Alpha = static_cast<double>(FloorNumber - FirstFloor) /
		static_cast<double>(LastFloor - FirstFloor);
	return FMath::Lerp(FirstValue, LastValue, Alpha);
}

double TriangleCDF(const double X, const double Minimum, const double Mode, const double Maximum)
{
	if (X <= Minimum) return 0.0;
	if (X >= Maximum) return 1.0;
	const double Span = Maximum - Minimum;
	if (X < Mode)
	{
		return FMath::Square(X - Minimum) / (Span * (Mode - Minimum));
	}
	return 1.0 - FMath::Square(Maximum - X) / (Span * (Maximum - Mode));
}

double RarityWeight(const FEFCalystoRarityWeights& Weights, const EEFCalystoRarity Tier)
{
	switch (Tier)
	{
	case EEFCalystoRarity::Common: return Weights.Common;
	case EEFCalystoRarity::Uncommon: return Weights.Uncommon;
	case EEFCalystoRarity::Rare: return Weights.Rare;
	case EEFCalystoRarity::Epic: return Weights.Epic;
	case EEFCalystoRarity::Winter: return Weights.Winter;
	default: return -1.0;
	}
}
}

uint64 FEFCalystoDirectorProbability::Hash(const FEFCalystoRandomKey& Key,
	const EEFCalystoRandomDomain Domain, const FGuid& OpportunityId, const int64 DrawIndex)
{
	using namespace EFCalystoProbabilityPrivate;
	uint64 Value = Mix(0x454643414C595354ull ^ static_cast<uint32>(Domain));
	Value = Mix(Value ^ static_cast<uint64>(Key.RunSeed));
	Value = Mix(Value ^ static_cast<uint64>(Key.FloorNumber));
	Value = Mix(Value ^ static_cast<uint64>(Key.RerollIndex));
	if (Domain != EEFCalystoRandomDomain::Style)
	{
		Value = Mix(Value ^ static_cast<uint64>(Key.AttemptIndex));
		Value = AddGuid(Value, Key.StyleId);
	}
	Value = AddGuid(Value, OpportunityId);
	return Mix(Value ^ static_cast<uint64>(DrawIndex));
}

double FEFCalystoDirectorProbability::Unit(const FEFCalystoRandomKey& Key,
	const EEFCalystoRandomDomain Domain, const FGuid& OpportunityId, const int64 DrawIndex)
{
	return static_cast<double>(Hash(Key, Domain, OpportunityId, DrawIndex) >> 11) /
		9007199254740992.0;
}

FGuid FEFCalystoDirectorProbability::RoomIdentity(const int64 RoomId)
{
	const uint64 Value = static_cast<uint64>(RoomId);
	return FGuid(0x524F4F4D, 0x43414C59, static_cast<uint32>(Value >> 32), static_cast<uint32>(Value));
}

bool FEFCalystoDirectorProbability::IsValid(const FEFCalystoFloatDistribution& D)
{
	if (D.Distribution == EEFCalystoDistribution::Fixed) return FMath::IsFinite(D.Value);
	if (D.Distribution != EEFCalystoDistribution::Uniform &&
		D.Distribution != EEFCalystoDistribution::Triangular) return false;
	return FMath::IsFinite(D.Minimum) && FMath::IsFinite(D.Maximum) && D.Minimum <= D.Maximum &&
		(D.Distribution != EEFCalystoDistribution::Triangular ||
			(FMath::IsFinite(D.Mode) && D.Mode >= D.Minimum && D.Mode <= D.Maximum));
}

bool FEFCalystoDirectorProbability::IsValid(const FEFCalystoAmountDistribution& D)
{
	if (D.Distribution == EEFCalystoDistribution::Fixed) return D.Amount >= 1 && D.Amount <= 1024;
	if (D.Distribution != EEFCalystoDistribution::Uniform &&
		D.Distribution != EEFCalystoDistribution::Triangular) return false;
	return D.Minimum >= 1 && D.Maximum <= 1024 && D.Minimum <= D.Maximum &&
		(D.Distribution != EEFCalystoDistribution::Triangular || (D.Mode >= D.Minimum && D.Mode <= D.Maximum));
}

bool FEFCalystoDirectorProbability::IsValid(const FEFCalystoPercentageCurve& C)
{
	return C.FirstFloor >= 1 && C.LastFloor >= C.FirstFloor &&
		FMath::IsFinite(C.FirstPercent) && FMath::IsFinite(C.LastPercent) &&
		C.FirstPercent >= 0.0 && C.FirstPercent <= 100.0 &&
		C.LastPercent >= 0.0 && C.LastPercent <= 100.0 &&
		(C.FirstFloor != C.LastFloor || C.FirstPercent == C.LastPercent);
}

bool FEFCalystoDirectorProbability::IsValid(const FEFCalystoScalarCurve& C)
{
	return C.FirstFloor >= 1 && C.LastFloor >= C.FirstFloor &&
		FMath::IsFinite(C.FirstValue) && FMath::IsFinite(C.LastValue) &&
		(C.FirstFloor != C.LastFloor || C.FirstValue == C.LastValue);
}

double FEFCalystoDirectorProbability::EvaluateLinearCurve(const FEFCalystoPercentageCurve& C, const int64 FloorNumber)
{
	return EFCalystoProbabilityPrivate::Linear(C.FirstFloor, C.LastFloor, C.FirstPercent, C.LastPercent, FloorNumber);
}

double FEFCalystoDirectorProbability::EvaluateLinearCurve(const FEFCalystoScalarCurve& C, const int64 FloorNumber)
{
	return EFCalystoProbabilityPrivate::Linear(C.FirstFloor, C.LastFloor, C.FirstValue, C.LastValue, FloorNumber);
}

FEFCalystoRarityWeights FEFCalystoDirectorProbability::EvaluateRarity(const FEFCalystoRarityCurve& C, const int64 FloorNumber)
{
	using namespace EFCalystoProbabilityPrivate;
	FEFCalystoRarityWeights Result;
	Result.Common = Linear(C.FirstFloor, C.LastFloor, C.FirstWeights.Common, C.LastWeights.Common, FloorNumber);
	Result.Uncommon = Linear(C.FirstFloor, C.LastFloor, C.FirstWeights.Uncommon, C.LastWeights.Uncommon, FloorNumber);
	Result.Rare = Linear(C.FirstFloor, C.LastFloor, C.FirstWeights.Rare, C.LastWeights.Rare, FloorNumber);
	Result.Epic = Linear(C.FirstFloor, C.LastFloor, C.FirstWeights.Epic, C.LastWeights.Epic, FloorNumber);
	Result.Winter = Linear(C.FirstFloor, C.LastFloor, C.FirstWeights.Winter, C.LastWeights.Winter, FloorNumber);
	return Result;
}

bool FEFCalystoDirectorProbability::RollChance(const double Percent, const double Uniform)
{
	return FMath::IsFinite(Percent) && Percent >= 0.0 && Percent <= 100.0 &&
		EFCalystoProbabilityPrivate::UniformValid(Uniform) && Uniform < Percent * 0.01;
}

bool FEFCalystoDirectorProbability::SampleFloat(const FEFCalystoFloatDistribution& D,
	const double Uniform, double& OutValue, FString& OutError)
{
	using namespace EFCalystoProbabilityPrivate;
	OutError.Reset();
	OutValue = 0.0;
	if (!IsValid(D) || !UniformValid(Uniform)) return Fail(OutError, TEXT("Invalid distribution or uniform draw."));
	if (D.Distribution == EEFCalystoDistribution::Fixed) { OutValue = D.Value; return true; }
	if (D.Minimum == D.Maximum) { OutValue = D.Minimum; return true; }
	if (D.Distribution == EEFCalystoDistribution::Uniform)
	{
		OutValue = FMath::Lerp(D.Minimum, D.Maximum, Uniform);
		return true;
	}
	const double Span = D.Maximum - D.Minimum;
	const double Split = (D.Mode - D.Minimum) / Span;
	OutValue = Uniform < Split
		? D.Minimum + FMath::Sqrt(Uniform * Span * (D.Mode - D.Minimum))
		: D.Maximum - FMath::Sqrt((1.0 - Uniform) * Span * (D.Maximum - D.Mode));
	return true;
}

double FEFCalystoDirectorProbability::AmountProbability(const FEFCalystoAmountDistribution& D, const int32 Amount)
{
	if (!IsValid(D)) return 0.0;
	if (D.Distribution == EEFCalystoDistribution::Fixed) return Amount == D.Amount ? 1.0 : 0.0;
	if (Amount < D.Minimum || Amount > D.Maximum) return 0.0;
	if (D.Minimum == D.Maximum) return 1.0;
	if (D.Distribution == EEFCalystoDistribution::Uniform) return 1.0 / static_cast<double>(D.Maximum - D.Minimum + 1);
	using namespace EFCalystoProbabilityPrivate;
	return TriangleCDF(Amount + 0.5, D.Minimum, D.Mode, D.Maximum) -
		TriangleCDF(Amount - 0.5, D.Minimum, D.Mode, D.Maximum);
}

bool FEFCalystoDirectorProbability::SampleFeasibleAmount(const FEFCalystoAmountDistribution& D,
	const TConstArrayView<int32> FeasibleAmounts, const double Uniform, int32& OutAmount,
	double& OutFeasibleProbabilityMass, FString& OutError)
{
	using namespace EFCalystoProbabilityPrivate;
	OutAmount = 0;
	OutFeasibleProbabilityMass = 0.0;
	OutError.Reset();
	if (!IsValid(D) || !UniformValid(Uniform) || FeasibleAmounts.Num() > 1024)
		return Fail(OutError, TEXT("Invalid amount distribution, draw, or bounded feasibility set."));
	TArray<int32, TInlineAllocator<32>> Ordered;
	Ordered.Append(FeasibleAmounts.GetData(), FeasibleAmounts.Num());
	Ordered.Sort();
	int32 Previous = INDEX_NONE;
	for (const int32 Count : Ordered)
	{
		if (Count < 1 || Count > 1024 || Count == Previous)
			return Fail(OutError, TEXT("Feasible amounts must be unique supported positive counts."));
		Previous = Count;
		OutFeasibleProbabilityMass += AmountProbability(D, Count);
	}
	if (OutFeasibleProbabilityMass <= 0.0)
		return Fail(OutError, TEXT("No feasible amount has positive requested probability."));
	const double Target = Uniform * OutFeasibleProbabilityMass;
	double Cumulative = 0.0;
	int32 LastPositive = 0;
	for (const int32 Count : Ordered)
	{
		const double Mass = AmountProbability(D, Count);
		if (Mass <= 0.0) continue;
		LastPositive = Count;
		Cumulative += Mass;
		if (Target < Cumulative) { OutAmount = Count; return true; }
	}
	// Only floating-point summation roundoff can reach the final positive interval.
	OutAmount = LastPositive;
	return OutAmount > 0;
}

bool FEFCalystoDirectorProbability::SelectWeighted(const TConstArrayView<FEFCalystoWeightedAlternative> Eligible,
	const double Uniform, FGuid& OutId, FString& OutError)
{
	using namespace EFCalystoProbabilityPrivate;
	OutId.Invalidate();
	OutError.Reset();
	if (!UniformValid(Uniform) || Eligible.Num() > 65536)
		return Fail(OutError, TEXT("Invalid weighted draw or bounded alternative set."));
	TArray<FEFCalystoWeightedAlternative, TInlineAllocator<16>> Ordered;
	Ordered.Append(Eligible.GetData(), Eligible.Num());
	Ordered.Sort([](const FEFCalystoWeightedAlternative& A, const FEFCalystoWeightedAlternative& B)
	{
		return GuidLess(A.Id, B.Id);
	});
	double Scale = 0.0;
	FGuid Previous;
	for (const FEFCalystoWeightedAlternative& Entry : Ordered)
	{
		if (!Entry.Id.IsValid() || Entry.Id == Previous || !FMath::IsFinite(Entry.Weight) || Entry.Weight < 0.0)
			return Fail(OutError, TEXT("Alternative identity is missing/duplicated or weight is invalid."));
		Previous = Entry.Id;
		Scale = FMath::Max(Scale, Entry.Weight);
	}
	if (Scale <= 0.0) return Fail(OutError, TEXT("No eligible alternative has positive weight."));
	double Total = 0.0;
	for (const FEFCalystoWeightedAlternative& Entry : Ordered) Total += Entry.Weight / Scale;
	const double Target = Uniform * Total;
	double Cumulative = 0.0;
	FGuid LastPositive;
	for (const FEFCalystoWeightedAlternative& Entry : Ordered)
	{
		if (Entry.Weight <= 0.0) continue;
		LastPositive = Entry.Id;
		Cumulative += Entry.Weight / Scale;
		if (Target < Cumulative) { OutId = Entry.Id; return true; }
	}
	OutId = LastPositive;
	return OutId.IsValid();
}

bool FEFCalystoDirectorProbability::SelectPopulatedRarity(const FEFCalystoRarityWeights& Weights,
	const TConstArrayView<EEFCalystoRarity> PopulatedEligibleTiers, const double Uniform,
	EEFCalystoRarity& OutTier, FString& OutError)
{
	using namespace EFCalystoProbabilityPrivate;
	TArray<FEFCalystoWeightedAlternative, TInlineAllocator<5>> Choices;
	TSet<uint8> Seen;
	for (const EEFCalystoRarity Tier : PopulatedEligibleTiers)
	{
		const uint8 Index = static_cast<uint8>(Tier);
		if (Index > static_cast<uint8>(EEFCalystoRarity::Winter) || Seen.Contains(Index))
			return Fail(OutError, TEXT("Populated rarity tiers must be valid and unique."));
		Seen.Add(Index);
		Choices.Add({FGuid(0, 0, 0, Index + 1), RarityWeight(Weights, Tier)});
	}
	FGuid Selected;
	if (!SelectWeighted(Choices, Uniform, Selected, OutError)) return false;
	OutTier = static_cast<EEFCalystoRarity>(Selected.D - 1);
	return true;
}

bool FEFCalystoDirectorProbability::SelectThemedRooms(const FEFCalystoRandomKey& Key,
	const TConstArrayView<FEFCalystoThemeOpportunity> Rooms, const double AdditionalChancePercent,
	TArray<FEFCalystoThemeDecision>& OutDecisions, FString& OutError)
{
	using namespace EFCalystoProbabilityPrivate;
	OutDecisions.Reset();
	OutError.Reset();
	if (!FMath::IsFinite(AdditionalChancePercent) || AdditionalChancePercent < 0.0 || AdditionalChancePercent > 100.0 || Rooms.Num() > 2048)
		return Fail(OutError, TEXT("Additional Room Theme Chance or room count is invalid."));
	TSet<int64> Seen;
	TArray<const FEFCalystoThemeOpportunity*, TInlineAllocator<32>> EligibleRooms;
	for (const FEFCalystoThemeOpportunity& Room : Rooms)
	{
		if (Room.RoomId <= 0 || Seen.Contains(Room.RoomId))
			return Fail(OutError, TEXT("Room identities must be positive and unique."));
		Seen.Add(Room.RoomId);
		if (Room.Protected != EEFCalystoProtectedRoom::None) continue;
		FGuid Dummy;
		FString EligibilityError;
		// Empty/all-zero tables are ineligible. Malformed populated tables are configuration failures.
		bool bPositive = false;
		for (const FEFCalystoWeightedAlternative& Theme : Room.EligibleThemes) bPositive |= Theme.Weight > 0.0;
		if (!bPositive) continue;
		if (!SelectWeighted(Room.EligibleThemes, 0.0, Dummy, EligibilityError))
		{
			OutError = FString::Printf(TEXT("Room %lld eligible Themes: %s"), Room.RoomId, *EligibilityError);
			return false;
		}
		EligibleRooms.Add(&Room);
	}
	if (EligibleRooms.IsEmpty()) return Fail(OutError, TEXT("No eligible room can satisfy the guaranteed Theme."));
	EligibleRooms.Sort([](const FEFCalystoThemeOpportunity& A, const FEFCalystoThemeOpportunity& B) { return A.RoomId < B.RoomId; });
	const FEFCalystoThemeOpportunity* Guaranteed = EligibleRooms[0];
	uint64 BestRank = Hash(Key, EEFCalystoRandomDomain::ThemeGuarantee, RoomIdentity(Guaranteed->RoomId));
	for (const FEFCalystoThemeOpportunity* Room : EligibleRooms)
	{
		const uint64 Rank = Hash(Key, EEFCalystoRandomDomain::ThemeGuarantee, RoomIdentity(Room->RoomId));
		if (Rank < BestRank) { Guaranteed = Room; BestRank = Rank; }
	}
	TArray<FEFCalystoThemeDecision> Pending;
	for (const FEFCalystoThemeOpportunity* Room : EligibleRooms)
	{
		const FGuid Opportunity = RoomIdentity(Room->RoomId);
		const bool bGuaranteed = Room == Guaranteed;
		if (!bGuaranteed && !RollChance(AdditionalChancePercent, Unit(Key, EEFCalystoRandomDomain::ThemePresence, Opportunity))) continue;
		FGuid Selected;
		if (!SelectWeighted(Room->EligibleThemes, Unit(Key, EEFCalystoRandomDomain::ThemeType, Opportunity), Selected, OutError)) return false;
		Pending.Add({Room->RoomId, Selected, bGuaranteed});
	}
	OutDecisions = MoveTemp(Pending);
	return true;
}

double FEFCalystoDirectorProbability::AdaptationMultiplier(const FEFCalystoAdaptation& Policy, const double NormalizedInput)
{
	if (!Policy.bEnabled) return 1.0;
	const double Input = FMath::IsFinite(NormalizedInput) ? FMath::Clamp(NormalizedInput, -1.0, 1.0) : 0.0;
	const double Effect = FMath::IsFinite(Policy.MaximumEffectPercent) ? FMath::Clamp(Policy.MaximumEffectPercent, 0.0, 100.0) * 0.01 : 0.0;
	return 1.0 + Input * Effect;
}

bool FEFCalystoDirectorProbability::IsEligible(const FEFCalystoSelection& Selection,
	const int64 FloorNumber, const TConstArrayView<FGuid> CoolingDownIds)
{
	return Selection.Id.IsValid() && Selection.bEnabled && FMath::IsFinite(Selection.Weight) && Selection.Weight > 0.0 &&
		FloorNumber >= Selection.FirstEligibleFloor &&
		(Selection.LastEligibleFloor == 0 || FloorNumber <= Selection.LastEligibleFloor) &&
		!CoolingDownIds.Contains(Selection.Id);
}

namespace
{
	double TraitValue(const FEFCalystoTraits& T, EEFCalystoTrait Trait)
	{
		switch (Trait)
		{
		case EEFCalystoTrait::Mystery: return T.Mystery;
		case EEFCalystoTrait::Danger: return T.Danger;
		case EEFCalystoTrait::Safe: return T.Safe;
		case EEFCalystoTrait::Abundance: return T.Abundance;
		case EEFCalystoTrait::ClothingInfluence: return T.ClothingInfluence;
		default: return -1.0;
		}
	}
	bool BindingLess(const FEFCalystoTraitBinding& A, const FEFCalystoTraitBinding& B)
	{
		if (A.Source != B.Source) return uint8(A.Source) < uint8(B.Source);
		if (A.Trait != B.Trait) return uint8(A.Trait) < uint8(B.Trait);
		if (A.Role != B.Role) return uint8(A.Role) < uint8(B.Role);
		if (A.Control != B.Control) return uint8(A.Control) < uint8(B.Control);
		return A.EntryId.ToString() < B.EntryId.ToString();
	}
}

bool FEFCalystoDirectorProbability::IsValid(const FEFCalystoTraits& Traits)
{
	const double Values[] = {Traits.Mystery, Traits.Danger, Traits.Safe, Traits.Abundance, Traits.ClothingInfluence};
	for (double Value : Values) if (!FMath::IsFinite(Value) || Value < 0.0 || Value > 1.0) return false;
	return true;
}

bool FEFCalystoDirectorProbability::ValidateTraitBindings(const FEFCalystoAdaptation& Policy, FString& OutField, FString& OutError)
{
	OutField.Reset(); OutError.Reset();
	auto Invalid = [&](const FString& Field, const TCHAR* Message) { OutField = Field; OutError = Message; return false; };
	if (!FMath::IsFinite(Policy.MaximumEffectPercent) || Policy.MaximumEffectPercent < 0 || Policy.MaximumEffectPercent > 100)
		return Invalid(TEXT("MaximumEffectPercent"), TEXT("Adaptation maximum effect must be finite within [0,100]."));
	if (Policy.Bindings.Num() > 64) return Invalid(TEXT("Bindings"), TEXT("At most 64 explicit trait bindings are supported."));
	for (int32 Index = 0; Index < Policy.Bindings.Num(); ++Index)
	{
		const auto& B = Policy.Bindings[Index]; const FString At = FString::Printf(TEXT("Bindings[%d]."), Index);
		if (uint8(B.Source) > uint8(EEFCalystoTraitSource::Snapshot)) return Invalid(At + TEXT("Source"), TEXT("Unknown trait input source."));
		if (uint8(B.Trait) > uint8(EEFCalystoTrait::ClothingInfluence)) return Invalid(At + TEXT("Trait"), TEXT("Unknown normalized trait."));
		if (uint8(B.Role) > uint8(EEFCalystoGameplayRole::SpecialEvent)) return Invalid(At + TEXT("Role"), TEXT("Unknown typed content role."));
		if (uint8(B.Control) > uint8(EEFCalystoTraitControl::EntryWeight)) return Invalid(At + TEXT("Control"), TEXT("Only Chance and exact-entry Weight are supported."));
		if (B.EntryId.IsValid() != (B.Control == EEFCalystoTraitControl::EntryWeight))
			return Invalid(At + TEXT("EntryId"), TEXT("Weight requires an exact entry identity; Chance must not name an entry."));
		if (!FMath::IsFinite(B.EffectAtZeroPercent) || FMath::Abs(B.EffectAtZeroPercent) > 100)
			return Invalid(At + TEXT("EffectAtZeroPercent"), TEXT("Effect endpoint must be finite within [-100,100]."));
		if (!FMath::IsFinite(B.EffectAtOnePercent) || FMath::Abs(B.EffectAtOnePercent) > 100)
			return Invalid(At + TEXT("EffectAtOnePercent"), TEXT("Effect endpoint must be finite within [-100,100]."));
		for (int32 Previous = 0; Previous < Index; ++Previous)
			if (!BindingLess(B, Policy.Bindings[Previous]) && !BindingLess(Policy.Bindings[Previous], B))
				return Invalid(At.LeftChop(1), TEXT("Duplicate input/control/target binding; combine its authored endpoint effects explicitly."));
	}
	return true;
}

bool FEFCalystoDirectorProbability::ResolveTraitMultiplier(const FEFCalystoAdaptation& Policy, const FEFCalystoTraits& Style,
	const FEFCalystoTraits* Theme, const FEFCalystoTraits* Snapshot, EEFCalystoGameplayRole Role,
	EEFCalystoTraitControl Control, const FGuid& EntryId, double& OutMultiplier, FString& OutError)
{
	OutMultiplier = 1.0; OutError.Reset();
	if (!Policy.bEnabled || Policy.MaximumEffectPercent == 0.0) return true;
	FString Field;
	if (!ValidateTraitBindings(Policy, Field, OutError)) { OutError = Field + TEXT(": ") + OutError; return false; }
	TArray<FEFCalystoTraitBinding, TInlineAllocator<64>> Matching;
	for (const auto& Binding : Policy.Bindings)
		if (Binding.Role == Role && Binding.Control == Control && Binding.EntryId == EntryId) Matching.Add(Binding);
	Matching.Sort(BindingLess);
	double Effect = 0.0;
	for (const auto& Binding : Matching)
	{
		const FEFCalystoTraits* Values = Binding.Source == EEFCalystoTraitSource::Style ? &Style
			: Binding.Source == EEFCalystoTraitSource::Theme ? Theme : Snapshot;
		if (!Values && Binding.Source == EEFCalystoTraitSource::Theme) continue;
		if (!Values || !IsValid(*Values))
		{ OutError = TEXT("A referenced trait snapshot/profile must be explicit and finite within [0,1]."); return false; }
		Effect += FMath::Lerp(Binding.EffectAtZeroPercent, Binding.EffectAtOnePercent, TraitValue(*Values, Binding.Trait));
	}
	OutMultiplier = AdaptationMultiplier(Policy, Effect / Policy.MaximumEffectPercent);
	return true;
}
