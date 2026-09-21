#include "Calysto/EFCalystoLightingResolver.h"

namespace EFCalystoLightingPrivate
{
bool SupportWithin(const FEFCalystoFloatDistribution& D, double Minimum, double Maximum)
{
	if (!FEFCalystoDirectorProbability::IsValid(D)) return false;
	const double Low = D.Distribution == EEFCalystoDistribution::Fixed ? D.Value : D.Minimum;
	const double High = D.Distribution == EEFCalystoDistribution::Fixed ? D.Value : D.Maximum;
	return Low >= Minimum && High <= Maximum;
}
}

bool FEFCalystoLightingResolver::ValidateFlicker(const FEFCalystoLightFlicker& R, FString& Error)
{
	Error.Reset();
	if (!R.bEnabled) return true;
	if (!FMath::IsFinite(R.MinimumMultiplier) || !FMath::IsFinite(R.MaximumMultiplier)
		|| R.MinimumMultiplier < 0 || R.MaximumMultiplier > 1 || R.MinimumMultiplier > R.MaximumMultiplier)
	{ Error = TEXT("Enabled flicker multipliers must be finite and ordered within 0..1."); return false; }
	if (!FMath::IsFinite(R.FrequencyHz) || R.FrequencyHz < .01 || R.FrequencyHz > 30)
	{ Error = TEXT("Enabled flicker frequency must be finite within 0.01..30 Hz."); return false; }
	return true;
}

bool FEFCalystoLightingResolver::Validate(const FEFCalystoLighting& Rules, FString& Field, FString& Error)
{
	Field.Reset(); Error.Reset();
	auto Fail = [&](const TCHAR* F, const TCHAR* Message) { Field = F; Error = Message; return false; };
	if (!EFCalystoLightingPrivate::SupportWithin(Rules.IntensityMultiplier, 0.0, 4.32))
		return Fail(TEXT("IntensityMultiplier"), TEXT("Intensity distribution support must be finite within 0..4.32."));
	if (!FMath::IsFinite(Rules.IntensityCeiling) || Rules.IntensityCeiling < 0 || Rules.IntensityCeiling > 4)
		return Fail(TEXT("IntensityCeiling"), TEXT("Intensity ceiling must be finite within 0..4."));
	if (!EFCalystoLightingPrivate::SupportWithin(Rules.WallLightHeight, 0.0, 1000.0))
		return Fail(TEXT("WallLightHeight"), TEXT("Wall light height support must be finite within 0..1000 cm."));
	int32 Ignored = 0;
	if (!SampleTileSpacing(Rules.WallLightTileDistance, 0.0, Ignored, Error))
	{ Field = TEXT("WallLightTileDistance"); return false; }
	if (!ValidateFlicker(Rules.Flicker, Error))
	{
		Field = !FMath::IsFinite(Rules.Flicker.FrequencyHz) || Rules.Flicker.FrequencyHz < .01 || Rules.Flicker.FrequencyHz > 30
			? TEXT("Flicker.FrequencyHz") : TEXT("Flicker.MinimumMultiplier");
		return false;
	}
	return true;
}

bool FEFCalystoLightingResolver::SampleTileSpacing(const FEFCalystoTileSpacing& R, double U, int32& Out, FString& Error)
{
	Out = 0; Error.Reset();
	if (!FMath::IsFinite(U) || U < 0 || U >= 1) { Error = TEXT("Spacing draw must be finite in [0,1)."); return false; }
	if (R.Distribution == EEFCalystoTileSpacingDistribution::Weighted)
	{
		if (R.Choices.IsEmpty() || R.Choices.Num() > 100)
		{ Error = TEXT("Weighted tile spacing requires 1..100 explicit choices."); return false; }
		TArray<FEFCalystoWeightedAlternative, TInlineAllocator<16>> Table;
		TSet<int32> Seen;
		for (int32 I = 0; I < R.Choices.Num(); ++I)
		{
			const auto& C = R.Choices[I];
			if (C.Tiles < 1 || C.Tiles > 100 || Seen.Contains(C.Tiles) || !FMath::IsFinite(C.Weight) || C.Weight < 0)
			{ Error = FString::Printf(TEXT("Choices[%d] requires a unique tile count in 1..100 and finite nonnegative weight."), I); return false; }
			Seen.Add(C.Tiles);
			Table.Add({FGuid(0x4C495447, 0x54494C45, 0, uint32(C.Tiles)), C.Weight});
		}
		FGuid Selected;
		if (!FEFCalystoDirectorProbability::SelectWeighted(Table, U, Selected, Error)) return false;
		Out = int32(Selected.D); return true;
	}
	FEFCalystoAmountDistribution D;
	switch (R.Distribution)
	{
	case EEFCalystoTileSpacingDistribution::Fixed: D.Distribution = EEFCalystoDistribution::Fixed; break;
	case EEFCalystoTileSpacingDistribution::Uniform: D.Distribution = EEFCalystoDistribution::Uniform; break;
	case EEFCalystoTileSpacingDistribution::Triangular: D.Distribution = EEFCalystoDistribution::Triangular; break;
	default: Error = TEXT("Unsupported native tile-spacing distribution."); return false;
	}
	D.Amount = R.Tiles; D.Minimum = R.Minimum; D.Mode = R.Mode; D.Maximum = R.Maximum;
	const int32 Low = D.Distribution == EEFCalystoDistribution::Fixed ? D.Amount : D.Minimum;
	const int32 High = D.Distribution == EEFCalystoDistribution::Fixed ? D.Amount : D.Maximum;
	if (!FEFCalystoDirectorProbability::IsValid(D) || Low < 1 || High > 100)
	{ Error = TEXT("Tile spacing requires ordered supported bounds within 1..100."); return false; }
	TArray<int32, TInlineAllocator<100>> Supported;
	for (int32 V = Low; V <= High; ++V) Supported.Add(V);
	double Mass = 0;
	return FEFCalystoDirectorProbability::SampleFeasibleAmount(D, Supported, U, Out, Mass, Error);
}

bool FEFCalystoLightingResolver::Resolve(const FEFCalystoLighting& Rules, FEFCalystoRandomKey Key,
	FEFCalystoResolvedLighting& Out, FString& Error)
{
	Out = {};
	FString Field;
	if (!Validate(Rules, Field, Error)) { Error = TEXT("Lighting.") + Field + TEXT(": ") + Error; return false; }
	// Preserve the selected floor lighting across topology recovery; an explicit reroll still changes it.
	Key.AttemptIndex = 0;
	FEFCalystoResolvedLighting Proposed;
	if (!FEFCalystoDirectorProbability::SampleFloat(Rules.IntensityMultiplier,
		FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Lighting, {}, 0x494E5445), Proposed.RequestedIntensityMultiplier, Error)
		|| !FEFCalystoDirectorProbability::SampleFloat(Rules.WallLightHeight,
			FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Lighting, {}, 0x48454947), Proposed.WallLightHeightCm, Error)
		|| !SampleTileSpacing(Rules.WallLightTileDistance,
			FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Lighting, {}, 0x54494C45), Proposed.WallLightTileDistance, Error)) return false;
	Proposed.IntensityMultiplier = FMath::Min(Proposed.RequestedIntensityMultiplier, Rules.IntensityCeiling);
	Proposed.Flicker = Rules.Flicker;
	Out = Proposed; return true;
}

bool FEFCalystoLightingResolver::EvaluateFlicker(const FEFCalystoLightFlicker& Rules, FEFCalystoRandomKey Key,
	FGuid LightIdentity, double Seconds, double& Out, FString& Error)
{
	Out = 1.0; Error.Reset();
	// Exactly no influence when disabled, including otherwise irrelevant time/random inputs.
	if (!Rules.bEnabled) return true;
	if (!ValidateFlicker(Rules, Error)) return false;
	if (!LightIdentity.IsValid() || !FMath::IsFinite(Seconds) || Seconds < 0 || Seconds > 8640000.0)
	{ Error = TEXT("Flicker requires a stable light identity and a finite elapsed time within 100 days."); return false; }
	Key.AttemptIndex = 0;
	const double Phase = Seconds * Rules.FrequencyHz;
	const int64 Segment = FMath::FloorToInt64(Phase);
	const double Fraction = Phase - double(Segment);
	const double Smooth = Fraction * Fraction * (3.0 - 2.0 * Fraction);
	const double A = FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Lighting, LightIdentity, 0x100000000ll + Segment);
	const double B = FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Lighting, LightIdentity, 0x100000001ll + Segment);
	Out = FMath::Lerp(Rules.MinimumMultiplier, Rules.MaximumMultiplier, FMath::Lerp(A, B, Smooth));
	return FMath::IsFinite(Out) && Out >= Rules.MinimumMultiplier && Out <= Rules.MaximumMultiplier;
}
