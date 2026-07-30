#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EFBlinkTypes.h"
#include "EFBlinkSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EF Blink"))
class EFBLINKRUNTIME_API UEFBlinkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEFBlinkSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (DisplayName = "Auto Attach To Local Player"))
	bool bAutoAttachToLocalPlayer = true;

	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (DisplayName = "Start Enabled"))
	bool bStartEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Morphs", meta = (DisplayName = "Morph Targets"))
	TArray<FEFBlinkMorphTarget> MorphTargets;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0.1", UIMin = "0.1", DisplayName = "Pulse Interval Seconds"))
	float PulseIntervalSeconds = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0.02", UIMin = "0.02", DisplayName = "Pulse Duration Seconds"))
	float PulseDurationSeconds = 0.12f;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Initial Delay Seconds"))
	float InitialDelaySeconds = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Target Mesh", meta = (DisplayName = "Preferred Mesh Name Tokens"))
	TArray<FString> PreferredMeshNameTokens;

	UPROPERTY(Config, EditAnywhere, Category = "Validation", meta = (DisplayName = "Require All Morph Targets"))
	bool bRequireAllMorphTargets = true;

	UPROPERTY(Config, EditAnywhere, Category = "Validation", meta = (DisplayName = "Log Validation"))
	bool bLogValidation = true;
};
