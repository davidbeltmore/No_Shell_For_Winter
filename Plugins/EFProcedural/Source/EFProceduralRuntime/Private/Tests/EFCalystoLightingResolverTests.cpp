#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoLightingResolver.h"
#include "Misc/AutomationTest.h"

namespace CalystoLightingTests
{
	FEFCalystoLighting Rules()
	{
		FEFCalystoLighting R;
		R.IntensityMultiplier.Distribution = EEFCalystoDistribution::Uniform;
		R.IntensityMultiplier.Minimum = .92; R.IntensityMultiplier.Maximum = 1.08;
		R.WallLightHeight.Distribution = EEFCalystoDistribution::Uniform;
		R.WallLightHeight.Minimum = 170; R.WallLightHeight.Maximum = 210;
		R.WallLightTileDistance.Distribution = EEFCalystoTileSpacingDistribution::Weighted;
		R.WallLightTileDistance.Choices = {{8, 1}, {9, 2}, {10, 1}};
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoLightingResolvedDistributionsTest,
	"NoShellForWinter.CalystoDungeon.Director.Lighting.ResolvedDistributions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoLightingResolvedDistributionsTest::RunTest(const FString&)
{
	auto R = CalystoLightingTests::Rules(); FString Error; int32 Tiles = 0;
	TestTrue(TEXT("Lower spacing endpoint"), FEFCalystoLightingResolver::SampleTileSpacing(R.WallLightTileDistance, 0, Tiles, Error));
	TestEqual(TEXT("First spacing"), Tiles, 8);
	TestTrue(TEXT("First CDF boundary"), FEFCalystoLightingResolver::SampleTileSpacing(R.WallLightTileDistance, .25, Tiles, Error));
	TestEqual(TEXT("Quarter boundary enters center"), Tiles, 9);
	TestTrue(TEXT("Second CDF boundary"), FEFCalystoLightingResolver::SampleTileSpacing(R.WallLightTileDistance, .75, Tiles, Error));
	TestEqual(TEXT("Three-quarter boundary enters upper"), Tiles, 10);
	FEFCalystoRandomKey Key; Key.RunSeed = 1779679224; Key.FloorNumber = 3; Key.StyleId = FGuid(1, 2, 3, 4);
	FEFCalystoResolvedLighting A, B;
	TestTrue(TEXT("Resolve actual production rules"), FEFCalystoLightingResolver::Resolve(R, Key, A, Error));
	Key.AttemptIndex = 3;
	TestTrue(TEXT("Resolve retry"), FEFCalystoLightingResolver::Resolve(R, Key, B, Error));
	TestEqual(TEXT("Retry preserves intensity"), A.IntensityMultiplier, B.IntensityMultiplier);
	TestEqual(TEXT("Retry preserves height"), A.WallLightHeightCm, B.WallLightHeightCm);
	TestEqual(TEXT("Retry preserves spacing"), A.WallLightTileDistance, B.WallLightTileDistance);
	R.IntensityMultiplier.Minimum = .5; R.IntensityMultiplier.Maximum = .75;
	R.WallLightTileDistance.Choices.Swap(0, 2);
	TestTrue(TEXT("Intensity edit and spacing reorder"), FEFCalystoLightingResolver::Resolve(R, Key, B, Error));
	TestNotEqual(TEXT("Authored intensity affects result"), A.IntensityMultiplier, B.IntensityMultiplier);
	TestEqual(TEXT("Independent height draw"), A.WallLightHeightCm, B.WallLightHeightCm);
	TestEqual(TEXT("Stable spacing identity"), A.WallLightTileDistance, B.WallLightTileDistance);
	R.IntensityMultiplier.Distribution = EEFCalystoDistribution::Fixed; R.IntensityMultiplier.Value = 4.16;
	R.WallLightHeight.Distribution = EEFCalystoDistribution::Fixed; R.WallLightHeight.Value = 205;
	TestTrue(TEXT("Resolve explicit saturation and height"), FEFCalystoLightingResolver::Resolve(R, Key, B, Error));
	TestEqual(TEXT("Requested intensity retained"), B.RequestedIntensityMultiplier, 4.16);
	TestEqual(TEXT("Ceiling retains probability mass"), B.IntensityMultiplier, 4.0);
	TestEqual(TEXT("Authored fixed height used"), B.WallLightHeightCm, 205.0);
	R.WallLightTileDistance.Choices = {{8, 0}, {9, 0}};
	TestFalse(TEXT("Zero weights fail before selection"), FEFCalystoLightingResolver::Resolve(R, Key, B, Error));
	TestTrue(TEXT("Exact invalid field"), Error.StartsWith(TEXT("Lighting.WallLightTileDistance:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoLightingProbabilityTest,
	"NoShellForWinter.CalystoDungeon.Director.Lighting.Probability100000",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoLightingProbabilityTest::RunTest(const FString&)
{
	auto R = CalystoLightingTests::Rules(); R.IntensityCeiling = 1.0;
	FEFCalystoRandomKey Key; Key.StyleId = FGuid(1, 2, 3, 4);
	int32 Counts[3] = {}, Saturated = 0; double IntensityTotal = 0, HeightTotal = 0; FString Error;
	for (int32 Trial = 0; Trial < 100000; ++Trial)
	{
		Key.RunSeed = Trial; FEFCalystoResolvedLighting Result;
		if (!FEFCalystoLightingResolver::Resolve(R, Key, Result, Error)) { AddError(Error); return false; }
		if (Result.WallLightTileDistance < 8 || Result.WallLightTileDistance > 10
			|| Result.RequestedIntensityMultiplier < .92 || Result.RequestedIntensityMultiplier >= 1.08
			|| Result.WallLightHeightCm < 170 || Result.WallLightHeightCm >= 210
			|| Result.IntensityMultiplier > 1 || Result.IntensityMultiplier < .92)
		{ AddError(TEXT("Lighting escaped authored support.")); return false; }
		++Counts[Result.WallLightTileDistance - 8]; Saturated += Result.IntensityMultiplier == 1.0;
		IntensityTotal += Result.RequestedIntensityMultiplier; HeightTotal += Result.WallLightHeightCm;
	}
	TestTrue(TEXT("Lower spacing 25 percent, six sigma"), FMath::Abs(Counts[0] - 25000) <= 823);
	TestTrue(TEXT("Center spacing 50 percent, six sigma"), FMath::Abs(Counts[1] - 50000) <= 950);
	TestTrue(TEXT("Upper spacing 25 percent, six sigma"), FMath::Abs(Counts[2] - 25000) <= 823);
	TestTrue(TEXT("Saturation point mass 50 percent, six sigma"), FMath::Abs(Saturated - 50000) <= 950);
	TestTrue(TEXT("Requested intensity mean, six sigma"), FMath::Abs(IntensityTotal / 100000 - 1) < .000877);
	TestTrue(TEXT("Height mean, six sigma"), FMath::Abs(HeightTotal / 100000 - 190) < .2191);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoLightingFlickerTest,
	"NoShellForWinter.CalystoDungeon.Director.Lighting.FlickerControlsAndDisabledIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoLightingFlickerTest::RunTest(const FString&)
{
	FEFCalystoLightFlicker Rules; FEFCalystoRandomKey Key; Key.RunSeed = 91;
	const FGuid Light(1, 2, 3, 4); double A = 0, B = 0; FString Error;
	TestTrue(TEXT("Disabled ignores irrelevant invalid identity/time"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, {}, -1, A, Error));
	TestEqual(TEXT("Disabled is exactly identity"), A, 1.0);
	Rules.bEnabled = true; Rules.MinimumMultiplier = .3; Rules.MaximumMultiplier = .9; Rules.FrequencyHz = 2;
	TestTrue(TEXT("Enabled actual resolver"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, Light, .27, A, Error));
	TestTrue(TEXT("Authored bounds"), A >= .3 && A <= .9);
	Key.AttemptIndex = 3;
	TestTrue(TEXT("Retry same flicker"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, Light, .27, B, Error));
	TestEqual(TEXT("Retry does not alter visual sequence"), A, B);
	Rules.FrequencyHz = 8;
	TestTrue(TEXT("Authored frequency"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, Light, .27, B, Error));
	TestNotEqual(TEXT("Frequency changes temporal result"), A, B);
	Rules.MinimumMultiplier = Rules.MaximumMultiplier = .6;
	TestTrue(TEXT("Degenerate authored amplitude"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, Light, .27, B, Error));
	TestEqual(TEXT("Constant amplitude exactly applied"), B, .6);
	Rules.FrequencyHz = 0;
	TestFalse(TEXT("Enabled invalid frequency rejected"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, Light, .27, B, Error));
	Rules.bEnabled = false;
	TestTrue(TEXT("Disabled invalid inactive values have zero effect"), FEFCalystoLightingResolver::EvaluateFlicker(Rules, Key, {}, -1, B, Error));
	TestEqual(TEXT("Disabled restored identity"), B, 1.0);
	return true;
}
#endif
