#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDirectorProbability.h"

struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedLighting
{
	double RequestedIntensityMultiplier = 1.0;
	double IntensityMultiplier = 1.0;
	double WallLightHeightCm = 200.0;
	int32 WallLightTileDistance = 10;
	FEFCalystoLightFlicker Flicker;
};

/** Shared, pure floor lighting decisions. Routing IDs, labels and asset paths are absent. */
struct EFPROCEDURALRUNTIME_API FEFCalystoLightingResolver final
{
	static bool Validate(const FEFCalystoLighting& Rules, FString& Field, FString& Error);
	static bool ValidateFlicker(const FEFCalystoLightFlicker& Rules, FString& Error);
	static bool Resolve(const FEFCalystoLighting& Rules, FEFCalystoRandomKey Key,
		FEFCalystoResolvedLighting& Out, FString& Error);
	static bool SampleTileSpacing(const FEFCalystoTileSpacing& Rules, double Uniform, int32& Out, FString& Error);
	/** Disabled flicker is exactly one. Enabled values interpolate deterministic targets smoothly. */
	static bool EvaluateFlicker(const FEFCalystoLightFlicker& Rules, FEFCalystoRandomKey Key,
		FGuid LightIdentity, double ElapsedSeconds, double& OutMultiplier, FString& Error);
};
